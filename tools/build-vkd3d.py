#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build VKD3D-Proton's d3d12 and d3d12core for both architectures from the pinned revision and owned patches.

The revision is GE-Proton11-7's, whose prebuilt DLLs were staged before. Its source is exported by Git from
.deps/vkd3d-proton (tools/fetch-runtime.sh), each submodule at the revision that commit records: the commit pins them.
No network activity; a submodule that is not there yet names the command that fetches it.
"""
import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
WORK = DEPS / 'vkd3d-patched'
CHECKOUT = DEPS / 'vkd3d-proton'
REVISION = 'af89350cc2eacd9da2293fbae96bd9ab4987c9bb'
PATCHES = sorted((ROOT / 'platform/vkd3d-proton/patches').glob('*.patch'))
MODULES = ('d3d12', 'd3d12core')
TOOLCHAIN_SHA256 = 'bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def source_stamp():
    h = hashlib.sha256(REVISION.encode())
    for path in (Path(__file__), *PATCHES):
        h.update(path.read_bytes())
    return h.hexdigest()


def export(repository, revision, destination):
    """The tree of one commit, then each submodule at the revision it records, recursively."""
    if subprocess.run(['git', '-C', str(repository), 'cat-file', '-e', revision + '^{commit}'],
                      capture_output=True).returncode:
        raise SystemExit(f'VKD3D-Proton source missing: {repository} at {revision}\n'
                         f'git -C {CHECKOUT} submodule update --init --recursive --depth 1 '
                         f'(the revisions {REVISION[:12]} records)')
    archive = subprocess.run(['git', '-C', str(repository), 'archive', '--format=tar', revision],
                             capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(destination, filter='data')
    tree = subprocess.run(['git', '-C', str(repository), 'ls-tree', '-r', revision],
                          capture_output=True, text=True, check=True).stdout
    for line in tree.splitlines():
        meta, path = line.split('\t', 1)
        mode, kind, sha = meta.split()
        if kind == 'commit':
            export(repository / path, sha, destination / path)


def prepare_source():
    if subprocess.run(['git', '-C', str(CHECKOUT), 'rev-parse', 'HEAD'], capture_output=True,
                      text=True).stdout.strip() != REVISION:
        raise SystemExit(f'{CHECKOUT} is not at {REVISION}: tools/fetch-runtime.sh fetches it')
    stamp = source_stamp()
    WORK.mkdir(exist_ok=True)
    source = WORK / 'source'
    if (source / '.source-stamp').is_file() and (source / '.source-stamp').read_text().strip() == stamp:
        return source, stamp
    with tempfile.TemporaryDirectory(dir=WORK) as temporary:
        staging = Path(temporary) / 'source'
        staging.mkdir()
        export(CHECKOUT, REVISION, staging)
        for patch in PATCHES:
            with patch.open('rb') as stream:
                subprocess.run(['patch', '--batch', '--forward', '-p1'], cwd=staging, stdin=stream, check=True)
        (staging / '.source-stamp').write_text(stamp + '\n')
        if source.exists():
            shutil.rmtree(source)
        shutil.move(staging, source)
    return source, stamp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=12)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    source, stamp = prepare_source()
    toolchain = DEPS / 'mesa-foundation-wgl/toolchain'
    if not (toolchain / '.archive-sha256').is_file() or (toolchain / '.archive-sha256').read_text().strip() != TOOLCHAIN_SHA256:
        raise SystemExit('Build the pinned Mesa WGL toolchain first')
    # The exported tree has no .git: Git must not find the PS5_Proton checkout above it (patch 0002 names the build).
    env = dict(os.environ, PATH=str(toolchain / 'bin') + os.pathsep + os.environ['PATH'], GIT_CEILING_DIRECTORIES=str(WORK))
    files = {}
    for arch, cpu in (('x86_64', 'x86_64'), ('i686', 'x86')):
        cross = WORK / ('cross-' + arch + '.ini')
        cross.write_text("[binaries]\n" +
            '\n'.join(f"{key} = '{toolchain / 'bin' / name}'" for key, name in (
                ('c', arch + '-w64-mingw32-clang'), ('cpp', arch + '-w64-mingw32-clang++'),
                ('ar', 'llvm-ar'), ('strip', 'llvm-strip'), ('windres', arch + '-w64-mingw32-windres'),
                ('widl', arch + '-w64-mingw32-widl'))) +
            "\npkg-config = 'false'\n[properties]\nneeds_exe_wrapper = true\n[host_machine]\n" +
            f"system = 'windows'\ncpu_family = '{cpu}'\ncpu = '{arch}'\nendian = 'little'\n")
        build = WORK / ('build-' + arch)
        command = ['meson', 'setup'] + (['--reconfigure'] if (build / 'build.ninja').exists() else [])
        # No link time in the DLLs (--no-insert-timestamp), as DXVK's and Mesa's: the same source links to the same bytes.
        subprocess.run([*command, str(build), str(source), '--cross-file', str(cross),
            '--wrap-mode=nodownload', '--buildtype=release', '-Dc_link_args=-Wl,--no-insert-timestamp',
            '-Dcpp_link_args=-Wl,--no-insert-timestamp'], env=env, check=True)
        subprocess.run(['ninja', '-C', str(build), '-j', str(args.jobs)], env=env, check=True)
        destination = WORK / ('i386' if arch == 'i686' else 'x86_64')
        destination.mkdir(exist_ok=True)
        for name in MODULES:
            candidates = list((build / 'libs').glob('*/' + name + '.dll'))
            if len(candidates) != 1:
                raise SystemExit('VKD3D-Proton output not unique: ' + arch + '/' + name)
            output = destination / (name + '.dll')
            shutil.copy2(candidates[0], output)
            files[str(output.relative_to(WORK))] = digest(output)
    (WORK / 'manifest.json').write_text(json.dumps({'revision': REVISION, 'version': 'v3.1.0-af89350cc-ps5-1',
        'source_stamp': stamp, 'toolchain_sha256': TOOLCHAIN_SHA256, 'files': files, 'console_validated': False},
        indent=2) + '\n')
    print('Built pinned patched VKD3D-Proton: both architectures, 4 modules')


if __name__ == '__main__':
    main()
