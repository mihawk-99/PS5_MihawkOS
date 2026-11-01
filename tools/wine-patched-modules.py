#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Windows (PE) modules the console must get from the patched tree, read from the trees themselves.

The console runs the PE modules of the host build (tools/build-wine-runtime.sh, built from .deps/wine-proton)
except the ones tools/build-wine-ps5.sh builds again from the patched tree. That set was a hand-kept list, and
a patch to a module missing from it had no effect on the console, with nothing to say so: hidclass.sys and
dinput ran without patch 0940 (a run of 2026-10-06).

Here the set is every module whose sources the series changes (`git diff <pin> <patched tree>`), and every
module whose sources differ between the host build's generated tree and the patched one (--runtime-source),
so neither a new patch nor a host build made from an older tree leaves an unpatched module on the console.
A changed path counts for:

  dlls/X, programs/X  module X, and every module that builds from X's sources (PARENTSRC: every xinput from
                      xinput1_3's, dinput8 from dinput's). A C file marked `#pragma makedep unix` is a Unix
                      side, which the PRX link builds: it must belong to a module named with --unix-module, or
                      nothing would build it, and that stops the build.
  libs/X              a static library (STATICLIB, or an EXTLIB linked as $(X_PE_LIBS)): every module that imports it.
  include/            every module with a PE source that includes the header, directly or through a header
                      that does.
  server/             the in-process wineserver, which the PRX link builds.
  configure.ac,       how the tree is configured; a module a patch adds is found by its own files.
  configure, include/Makefile.in, include/config.h.in
  loader/wine.inf.in  what wineboot installs and registers: tools/build-wine-ps5.sh ships the patched wine.inf.
  a removed module    a deleted file of a directory left with no Makefile.in (winegstreamer, patch 2001): nothing
                      builds it, and the host build made none (no GStreamer) that the console could keep running.

Anything else (Wine's build tools, the loader, data files) stops the build: no rule here says which modules
it changes.

Prints one module per line as "<directory>/<file>", e.g. "dlls/hidclass.sys/hidclass.sys", sorted.

  tools/wine-patched-modules.py [--unix-module DIR]... [--runtime-source DIR] TREE PIN PATCHED
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

CODE = {'.c', '.cpp', '.m', '.s', '.S'}
UNIX_PRAGMA = re.compile(r'^\s*#\s*pragma\s+makedep\s+unix\b', re.M)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
NOT_MODULES = {'configure.ac', 'configure', 'include/Makefile.in', 'include/config.h.in', 'loader/wine.inf.in'}
SAME_IN_BOTH = {'vk.xml', 'video.xml'}   # tools/generate-wine.sh copies the same registry into each tree


def makefile_vars(path):
    values = {}
    for line in path.read_text(errors='replace').replace('\\\n', ' ').splitlines():
        match = re.match(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$', line)
        if match:
            values[match.group(1)] = match.group(2).split()
    return values


class Tree:
    def __init__(self, root):
        self.root = root
        self.modules = {}    # directory -> makefile variables, for every directory that makes a module
        self.static = {}     # libs/ directory -> the names IMPORTS gives its static library
        for makefile in sorted(root.glob('dlls/*/Makefile.in')) + sorted(root.glob('programs/*/Makefile.in')):
            values = makefile_vars(makefile)
            if values.get('MODULE'):
                self.modules[makefile.parent.relative_to(root).as_posix()] = values
        for makefile in sorted(root.glob('libs/*/Makefile.in')):
            # STATICLIB, or EXTLIB for a bundled library that modules link as $(<NAME>_PE_LIBS) (libs/faudio).
            values = makefile_vars(makefile)
            for lib in values.get('STATICLIB', []) + values.get('EXTLIB', []):
                self.static.setdefault(makefile.parent.relative_to(root).as_posix(), set()).add(
                    re.sub(r'^lib|\.a$', '', lib))
        self._unix = {}
        self._sources = {}

    def is_unix(self, path):
        if path not in self._unix:
            file = self.root / path
            self._unix[path] = (Path(path).suffix in CODE and file.is_file() and
                                bool(UNIX_PRAGMA.search(file.read_text(errors='replace'))))
        return self._unix[path]

    def dirs(self, module):
        """The directories a module builds from: its own, then its PARENTSRC."""
        return [module] + [os.path.normpath(f'{module}/{parent}') for parent in self.modules[module].get('PARENTSRC', [])]

    def sources(self, module):
        """A module's sources as tree paths, each where make finds it (its own directory first)."""
        if module not in self._sources:
            found = []
            for name in self.modules[module].get('SOURCES', []):
                for directory in self.dirs(module):
                    if (self.root / directory / name).is_file():
                        found.append(f'{directory}/{name}')
                        break
            self._sources[module] = found
        return self._sources[module]


def classify(tree, changed, unix_modules):
    pe, problems, headers = set(), [], []
    builders = {}   # directory -> the modules that build from it
    for module in tree.modules:
        for directory in tree.dirs(module):
            builders.setdefault(directory, set()).add(module)
    for path in changed:
        parts = path.split('/')
        if path in NOT_MODULES or parts[0] == 'server':
            continue
        if parts[0] in ('dlls', 'programs') and len(parts) > 2:
            home = '/'.join(parts[:2])
            users = builders.get(home)
            if not users and not (tree.root / home / 'Makefile.in').exists() and not (tree.root / path).exists():
                continue    # a removed module
            if not users:
                problems.append(f'{path}: no module builds from {home}')
            elif tree.is_unix(path):
                if home not in unix_modules:
                    problems.append(f'{path}: the Unix side of {home}, which the PRX link does not build')
            else:
                # A PE source: the modules that compile it; anything else (Makefile.in, the spec, a header, a
                # resource, IDL, or a file another includes): every module that builds from the directory.
                pe.update({m for m in users if path in tree.sources(m)} or users)
                if path.endswith('.h'):
                    headers.append(path)
        elif parts[0] == 'libs' and len(parts) > 2:
            names = tree.static.get('/'.join(parts[:2]))
            if not names:
                problems.append(f'{path}: {"/".join(parts[:2])} is not a static library modules import')
            else:
                imports = names | {f'$({name.upper()}_PE_LIBS)' for name in names}
                pe.update(m for m, v in tree.modules.items()
                          if imports & set(v.get('IMPORTS', []) + v.get('DELAYIMPORTS', [])))
        elif parts[0] == 'include' and len(parts) > 1:
            headers.append(path)
        else:
            problems.append(f'{path}: no rule says which modules it changes')
    # A changed header changes the PE sources that include it, directly or through other headers. A module's
    # header is included by its own name; one in include/ by its path there.
    pending, seen = list(headers), set(headers)
    while pending:
        header = pending.pop()
        spelled = header[len('include/'):] if header.startswith('include/') else Path(header).name
        for module in tree.modules:
            if not header.startswith('include/') and not any(header.startswith(d + '/') for d in tree.dirs(module)):
                continue
            files = set(tree.sources(module))
            for directory in tree.dirs(module):
                files.update(p.relative_to(tree.root).as_posix() for p in (tree.root / directory).rglob('*.h'))
            for file in files:
                if spelled not in INCLUDE.findall((tree.root / file).read_text(errors='replace')):
                    continue
                if file.endswith('.h'):
                    if file not in seen:
                        seen.add(file)
                        pending.append(file)
                elif not tree.is_unix(file):
                    pe.add(module)
    return pe, problems


def differing(runtime, tree):
    """The paths whose content differs between two generated trees, or that only one has."""
    # Backup copies (autoconf leaves configure~) are not sources: the host tree has one from 2026-10-03, and a work
    # tree made fresh has none, which stopped every clean build (build-production-own-2, 2026-10-11).
    listing = subprocess.run(['diff', '-rq', '--exclude=.git', '--exclude=*~', str(runtime), str(tree)],
                             capture_output=True, text=True)
    if listing.returncode > 1:
        raise SystemExit(f'wine-patched-modules: cannot compare {runtime} with {tree}: {listing.stderr.strip()}')
    paths = set()
    for line in listing.stdout.splitlines():
        match = re.match(r'^Files (.+) and (.+) differ$', line)
        if match:
            paths.add(Path(match.group(2)).relative_to(tree).as_posix())
            continue
        match = re.match(r'^Only in (.+): (.+)$', line)
        if not match:
            raise SystemExit(f'wine-patched-modules: cannot read the comparison: {line}')
        found = Path(match.group(1)) / match.group(2)
        side = runtime if found.is_relative_to(runtime) else tree
        files = [found] if found.is_file() else [p for p in found.rglob('*') if p.is_file()]
        paths.update(p.relative_to(side).as_posix() for p in files)
    return paths - SAME_IN_BOTH


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--unix-module', action='append', default=[],
                        help='a module directory whose Unix side the PRX link builds')
    parser.add_argument('--runtime-source', help="the host build's generated source tree")
    parser.add_argument('tree')
    parser.add_argument('pin')
    parser.add_argument('patched')
    args = parser.parse_args()
    root = Path(args.tree).resolve()
    changed = set(subprocess.run(['git', '-C', str(root), 'diff', '--name-only', '--no-renames', args.pin, args.patched],
                                 check=True, capture_output=True, text=True).stdout.split())
    if args.runtime_source:
        changed |= differing(Path(args.runtime_source).resolve(), root)
    tree = Tree(root)
    pe, problems = classify(tree, sorted(changed), set(args.unix_module))
    for problem in problems:
        print(f'wine-patched-modules: {problem}', file=sys.stderr)
    if problems:
        return 1
    for module in sorted(pe):
        print(f"{module}/{tree.modules[module]['MODULE'][0]}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
