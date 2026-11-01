#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Is this repository safe to publish? One command, one verdict (docs/PUBLICATION_CHECKLIST.md).

  publication-check.py tree              the tracked files as they are on disk (gzip members unpacked)
  publication-check.py history [REV...]  every line, author and message `git log -p REV...` adds (default: --all)
  publication-check.py all [REV...]      both
    --repo DIR        the checkout to check (default: this one)
    --env FILE        an ignored .env naming the console (PS5_HOST, ORIGINAL_PS5_HOST) and FTP credentials; repeatable
                      (default: DIR/.env, ../PS5_Vulkan/.env and ../PS5_Proton/.env beside DIR, those that exist)
    --allow CATEGORY  accept that category's findings for this run (the owner's decision, e.g. 'home path' in history)

Exit 0: clean; 1: something to fix. Each category prints its count and where (paths, commits), never a value: the
console's address and the FTP credentials are compared exactly with the .env, this PC's LAN addresses with `ip addr`,
personal e-mail addresses through tools/privacy_check.py's hash list. A tree check also refuses tracked files under
klog/ or .claude/, files whose extension or magic bytes say game data, firmware or keys, source files the project
wrote without an SPDX line (LICENSING.md), a game, launcher or emulator experiment named in a tracked file (NAMES), and,
when a production build is present, a licence text missing from it
(tools/stage-licences.py). Known false positives are named in ALLOWED and reported as allowed, not hidden.
"""
import argparse
import collections
import gzip
import hashlib
import importlib.util
import ipaddress
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, TOOLS / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


privacy = load('privacy_check', 'privacy_check.py')
licences = load('stage_licences', 'stage-licences.py')
WALLPAPER = Path(__file__).resolve().parents[1] / 'themes/windows11/wallpapers/skeuoss-bubble-city.jpg'  # the default

IPV4 = re.compile(rb'(?<![\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d.]*\d)')
EMAIL = re.compile(rb'[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+')
TOKENS = [re.compile(p) for p in (
    rb'\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36}\b', rb'\bgithub_pat_[A-Za-z0-9_]{60,}', rb'\bAKIA[0-9A-Z]{16}\b',
    rb'\bxox[baprs]-[A-Za-z0-9-]{10,}', rb'\bsk-(?:ant-)?[A-Za-z0-9_-]{32,}',
    rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |PGP )?PRIVATE KEY-----',
    rb'\b[MN][A-Za-z\d]{23,25}\.[\w-]{6}\.[\w-]{27,38}\b',  # a Discord bot token
    rb'(?i)authorization:\s*bearer\s+[A-Za-z0-9._-]{20,}',
    rb'(?i)\b(?:password|passwd|pwd)\s*[=:]\s*["\']?[^\s"\'<>{}$()]{6,}')]
MAC = re.compile(rb'(?i)(?<![0-9a-f:])(?:[0-9a-f]{2}:){5}[0-9a-f]{2}(?![0-9a-f:])')
SERIAL = re.compile(rb'(?i)\b(?:serial(?:[ _-]?(?:number|no))?|psid|openpsid|idps|console[_ ]?id)\b["\']?\s*[:=]\s*'
                    rb'["\']?[A-Za-z0-9-]{8,}')
HOME = re.compile(rb'/home/([a-z_][a-z0-9_-]{0,31})/')
WINUSER = re.compile(rb'(?i)C:\\{1,2}Users\\{1,2}([A-Za-z0-9._-]+)')
WINUSERS_OK = {b'steamuser', b'public', b'default', b'all users', b'mihawk'}  # Proton's, Wine's, Mihawk OS's (patch 1240)
LAN = [ipaddress.ip_network(n) for n in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '100.64.0.0/10')]

# Binary formats that are never this project's: a game, firmware, a key or a disc image could hide in them.
MAGIC = {b'\x7fELF': 'ELF', b'MZ': 'PE', b'\x4f\x15\x3d\x1d': 'PS4 SELF', b'\x54\x14\xf5\xee': 'PS5 SELF',
         b'SCEUF': 'PUP', b'\x7fCNT': 'PKG', b'PK\x03\x04': 'zip', b'7z\xbc\xaf': '7z', b'Rar!': 'rar',
         b'\xfd7zXZ': 'xz', b'-----BEGIN': 'PEM'}
RISKY_SUFFIXES = ('.iso', '.pkg', '.self', '.sprx', '.prx', '.pup', '.elf', '.exe', '.dll', '.sys', '.key', '.pem',
                  '.p12', '.pfx', '.sfo', '.pfs', '.ucp', '.chd', '.nsp', '.xci', '.cso', '.img', '.dmp', '.sav',
                  '.wad', '.pak', '.assets', '.unity3d', '.zip', '.7z', '.rar')
# Source files the project writes carry an SPDX line; vendored trees keep their own headers (NOTICE.md), and the
# prototype and the triangle shader are LICENSING.md's named exceptions. Artwork and data are recorded in NOTICE.md.
SOURCE_SUFFIXES = ('.c', '.h', '.cpp', '.hpp', '.cc', '.py', '.sh', '.mjs', '.js', '.css', '.cmake', '.ld', '.S',
                   '.asm', '.hlsl', '.glsl', '.vert', '.frag', '.comp', '.rc')
SPDX_EXEMPT = ('external/', 'base/', 'ps5/third_party/', 'shaders/glsl/base/', 'prototypes/windows11-shell/',
               'tests/shaders/d3d11-triangle.hlsl')

# (category, path prefix, why): findings there are reported as allowed.
ALLOWED = [
    ('token or key', 'external/glm/doc/api/jquery.js', "jQuery's own password option, vendored with GLM's manual"),
    ('private LAN address', 'tests/test_console_guard.py', 'a made-up fixture address'),
    ('token or key', 'native/proton_loading.cpp', "the Steam setup page's password field passed on, never a value"),
    ('private LAN address', 'tools/publication-check.py', 'the private ranges it checks against'),
    ('binary that could hold game data, firmware or keys', 'themes/windows11/source/template.msstyles',
     "the Wine theme's resource-only container (NOTICE.md)"),
    # The launcher's store-client option names the client it installs; every such word is kept in these files (the owner
    # asked for the option on 2026-10-24: the tree otherwise names no game or launcher).
    *[('named game or launcher', path, "the launcher's store-client option (docs/STEAM.md)")
      for path in ('src/pw_steam.', 'tests/test_pw_steam.c', 'tests/test_process_rules.c', 'ps5/src/proton_steam.',
                   'docs/STEAM.md', 'tools/run-steam-option.py', 'docs/AURELIA_REVIEW.md')],
]
INFO = ('other e-mail address', 'GitHub no-reply address')
VALUES = ('console address (.env)', "this PC's LAN address", 'private LAN address', 'FTP credential (.env)',
          'blocked personal e-mail', 'token or key', 'MAC address', 'serial or console id', 'home path',
          'Windows user folder')
TREE = ('tracked under klog/ or .claude/', 'binary that could hold game data, firmware or keys',
        'source file without SPDX', 'licence text missing beside a build', 'file that must never ship in a build',
        'named game or launcher')
# The tree names no game, game launcher or the earlier PS5-emulator experiment, and no PS5 title id but the project's
# own (PPSA99170, its memory probe PPSA99171, its WebUI's home screen tiles PPSA99300 to PPSA99399, and PPSA99130 and
# PPSA99008 of its sibling titles); history keeps the old names. Not names:
# 'steamuser' (Proton's fixed prefix user) and emoji names ('face with steam from nose', 'steaming bowl').
NAMES = re.compile(rb'(?i)kyty|hollow ?knight|dead ?cells|stardew|binding of isaac|\bisaac\b|celeste|\bhades\b|hearthstone|'
                   rb'bravely ?default|battle\.net|battlenet|\bbnet\b|resident evil|\bre3\b|moonstone|ikemen|minecraft|'
                   rb'\bprism\b|prismlauncher|persona ?3|\bp3r\b|vrchat|\bsteam(?!user|ing| from nose)|'
                   rb'PPSA(?!99170|99171|993[0-9]{2}|99130|99008)[0-9]{5}')
# (path, a line it allows, why): the trademark notices name their marks (LEGAL.md, NOTICE.md, LICENSING.md), and NOTICE.md
# records the upstream boilerplate's own default title id.
NAMED_LINES = [
    ('LEGAL.md', re.compile(rb'^\| Steam, Proton \| Valve Corporation \|$'), 'the trademark table'),
    ('LICENSING.md', re.compile(rb'(?i)trademark'), 'the trademark notice'),
    ('NOTICE.md', re.compile(rb'(?i)trademark'), 'the trademark notice'),
    ('NOTICE.md', re.compile(rb'`target_title_id` changed from `PPSA99790`'), "the boilerplate's default title id"),
    # Upstream Wine/Proton code the patches touch or carry as context, which must match the pinned tree byte for byte,
    # and the environment variables it reads, named where a patch message explains them.
    ('wine/patches/', re.compile(rb'SteamGameId|STEAM_COMPAT_|SteamDeck|Steam Deck|steam_overlay|steamclient_'),
     'upstream code and its environment variables'),
    # GE-Proton's own messages and comments in its ge-video-rework patches 0015, 0028, 0042-0044 and 0055, carried
    # unchanged with their authorship (NOTICE.md, "GE-Proton's ffmpeg + winedmo video rework"; compared 2026-10-19).
    # The Steam guide's own gate cases (steam-guide-pages, steam-guide-screens), named in every gate's records.
    ('evidence/', re.compile(rb'steam[-_]guide'), "the Steam guide's gate cases"),
    # The code budget's list of launcher program names, which it matches (wine/ps5/pw_wine_code_budget.c), and its test.
    *[(path, re.compile(rb'(?i)"battle\.net[ .]'), 'a launcher program name the code budget matches')
      for path in ('wine/ps5/pw_wine_code_budget.c', 'tests/test_pw_wine_code_budget.c')],
    *[(f'wine/patches/{n}-', re.compile(rb'(?i)vrchat'), "GE-Proton's own patch text")
      for n in ('2015', '2026', '2040', '2041', '2042', '2053')],
]
# What a user's package must never carry (docs/SELF_INSTALL.md, phase 4), in the title folder, the runtime and run/: a
# wallpaper other than the default, Skeuoss's Bubble City (proton.bmp is stamped from it by tools/build-wine-theme.py
# since 2026-10-31, used with the artist's permission; before that the project's own Bubble Earth and Aero Meadow;
# Windows 11's Bloom, which Aero Meadow replaced, is not licensed for redistribution), a native Microsoft binary (a PE without Wine's builtin
# marker that names Microsoft Corporation, such as Microsoft's d3dcompiler_47.dll; Wine's own d3dcompiler_47.dll is
# fine), and the probe prefix or test programs, which only the PC tools and the gate use.
TEST_PROGRAM = re.compile(r'(?i)(-probe\.exe|_test(32)?\.exe|^gate-sequence\.exe)$')
MICROSOFT = 'Microsoft Corporation'.encode('utf-16-le')


def env_values(paths, keys):
    out = set()
    for path in paths:
        try:
            lines = Path(path).read_text().splitlines()
        except OSError:
            continue
        for line in lines:
            key, _, value = line.strip().partition('=')
            value = value.strip().strip('"\'')
            if key.removeprefix('export ').strip() in keys and value:
                out.add(value)
    return out


def lan_addresses():
    try:
        lines = subprocess.run(['ip', '-4', '-o', 'addr'], capture_output=True, text=True).stdout.splitlines()
    except OSError:
        return set()
    return {line.split()[3].split('/')[0] for line in lines if len(line.split()) > 3} - {'127.0.0.1'}


class Findings:
    def __init__(self, consoles, credentials, lan, user, allow):
        self.consoles, self.credentials, self.lan, self.user, self.allow = consoles, credentials, lan, user, allow
        self.hits = collections.defaultdict(collections.Counter)
        self.allowed = collections.defaultdict(collections.Counter)

    def add(self, category, place, path=''):
        for allowed, prefix, why in ALLOWED:
            if allowed == category and path.startswith(prefix):
                self.allowed[category][f'{prefix} ({why})'] += 1
                return
        self.hits[category][place] += 1

    def text(self, data, place, path):
        for m in IPV4.finditer(data):
            # An octet over 255, or one with a leading zero (binary data reads as "08.00.00.00"), is no address as written.
            if max(int(o) for o in m.groups()) > 255 or any(len(o) > 1 and o.startswith(b'0') for o in m.groups()):
                continue
            ip = m.group().decode()
            if ip in self.consoles:
                self.add('console address (.env)', place, path)
            elif ip in self.lan:
                self.add("this PC's LAN address", place, path)
            elif any(ipaddress.ip_address(ip) in net for net in LAN):
                self.add('private LAN address', place, path)
        for secret in self.credentials:
            if secret.encode() in data:
                self.add('FTP credential (.env)', place, path)
        if b'@' in data:
            for m in EMAIL.finditer(data):
                address = m.group().lower()
                if privacy.blocked_token(address):
                    self.add('blocked personal e-mail', place, path)
                elif address.endswith(b'users.noreply.github.com'):
                    self.add('GitHub no-reply address', place, path)
                else:
                    self.add('other e-mail address', place, path)
        for rx in TOKENS:
            for _ in rx.finditer(data):
                self.add('token or key', place, path)
        for m in MAC.finditer(data):
            # 00:00:5e:00:53:xx is reserved for documentation (RFC 7042), as fixtures use it
            if m.group().lower() not in (b'00:00:00:00:00:00', b'ff:ff:ff:ff:ff:ff') and \
                    not m.group().lower().startswith(b'00:00:5e:00:53:'):
                self.add('MAC address', place, path)
        for _ in SERIAL.finditer(data):
            self.add('serial or console id', place, path)
        for m in HOME.finditer(data):
            if m.group(1).decode() == self.user:
                self.add('home path', place, path)
        for m in WINUSER.finditer(data):
            name = m.group(1).rstrip(b'.').lower()  # a sentence's full stop is not part of the name
            if name not in WINUSERS_OK:
                self.add('Windows user folder', place, path)

    def report(self, out, tree):
        failed = 0
        categories = sorted(set(self.hits) | set(self.allowed) | set(VALUES) | (set(TREE) if tree else set()))
        for category in categories:
            hits = self.hits.get(category, collections.Counter())
            count = sum(hits.values())
            if category in INFO:
                state = 'info'
            elif not count:
                state = 'ok'
            elif category in self.allow:
                state = 'accepted'
            else:
                state, failed = 'FAIL', failed + 1
            places = ', '.join(p for p, _ in hits.most_common(6)) + (f' and {len(hits) - 6} more' if len(hits) > 6 else '')
            shown = count and state != 'info'
            out.append(f'  {state:8} {category}: {count}' + (f' in {len(hits)} places: {places}' if shown else ''))
            for where, n in self.allowed.get(category, {}).items():
                out.append(f'  {"allowed":8} {category}: {n} in {where}')
        return failed


STEAM_WORD = re.compile(rb'(?i)\bsteam(?!user|ing| from nose)')


def load_names(path):
    """tools/publication-names.txt: {'steam': {path: why}, 'records': {path: why}, 'games': {path: why}, 'stems': a
    pattern of the records' run names, or None}. Each path is exact; a missing file lists nothing."""
    names = {'steam': {}, 'records': {}, 'games': {}, 'stems': None}
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return names
    kind = why = None
    for line in lines:
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        head = re.fullmatch(r'\[(steam|records|games)\] (.+)', line)
        if head:
            kind, why = head.groups()
        elif kind:
            names[kind][line] = why
    stems = sorted({Path(record).name.rsplit('.', 1)[0] for record in names['records']}, key=len, reverse=True)
    names['stems'] = re.compile('|'.join(re.escape(stem) for stem in stems).encode()) if stems else None
    return names


def name_lines(findings, path, data, names):
    """Each line of a text file that names a game or launcher (NAMES): allowed when the file is a kept record or a [games]
    file, when the names left once kept records' run names are taken out are only Steam's in a [steam] file, or by
    NAMED_LINES; else a finding."""
    for line in data.splitlines():
        if not NAMES.search(line):
            continue
        rest = names['stems'].sub(b'', line) if names['stems'] else line
        found = [m.group(0) for m in NAMES.finditer(rest)]
        why = (names['records'].get(path) or names['games'].get(path) or
               ('a kept record cited by its run name' if not found else None) or
               (names['steam'].get(path) if all(STEAM_WORD.fullmatch(name) for name in found) else None) or
               next((why for named, rx, why in NAMED_LINES if path.startswith(named) and rx.search(line)), None))
        if why:
            findings.allowed['named game or launcher'][f'{path} ({why})'] += 1
        else:
            findings.add('named game or launcher', path, path)


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], capture_output=True, check=True).stdout


def check_tree(repo, findings):
    # what is tracked and what a commit would add: untracked files git does not ignore
    files = [f.decode() for f in git(repo, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0') if f]
    names = load_names(repo / 'tools/publication-names.txt')
    for path in files:
        try:
            data = (repo / path).read_bytes()
        except OSError:
            continue
        if path.endswith('.gz'):
            try:
                data = gzip.decompress(data)
            except OSError:
                pass
        findings.text(data, path, path)
        if path.startswith(('klog/', '.claude/')):
            findings.add('tracked under klog/ or .claude/', path, path)
        head = data[:16] if not path.endswith('.gz') else (repo / path).read_bytes()[:16]
        kind = next((name for magic, name in MAGIC.items() if head.startswith(magic)), None)
        if kind is None and len(data) > 0x8006 and data[0x8001:0x8006] == b'CD001':
            kind = 'ISO 9660'
        if kind or path.lower().endswith(RISKY_SUFFIXES):
            findings.add('binary that could hold game data, firmware or keys', f'{path} ({kind or "suffix"})', path)
        if path.endswith(SOURCE_SUFFIXES) and not path.startswith(SPDX_EXEMPT) and b'SPDX-License-Identifier' not in data[:2048]:
            findings.add('source file without SPDX', path, path)
        if path not in ('tools/publication-check.py', 'tools/publication-names.txt') and b'\0' not in data[:8192] \
                and NAMES.search(data):  # text only
            name_lines(findings, path, data, names)
    runtime, title, run = repo / '.deps/staged/runtime/ge11-7', repo / 'dist/PPSA99170', repo / '.deps/staged/run'
    for root, folder, names in ((title, 'licenses', list(licences.TITLE)),
                                (runtime, 'share/licenses', licences.STAGE_WINE + list(licences.RUNTIME))):
        if root.is_dir():
            label = 'title' if root == title else 'runtime'
            if not (root / folder).is_dir():
                findings.add('licence text missing beside a build', f'{label}: no {folder}/ folder', folder)
            for name in names:
                if (root / folder).is_dir() and not (root / folder / name).is_file():
                    findings.add('licence text missing beside a build', f'{label} {name}', name)
    for root, label in ((repo / 'dist', 'dist'), (runtime, 'runtime'), (run, 'run')):  # dist/: the title and any release
        if root.is_dir():
            never_ship(root, label, findings)
    return len(files)


def is_default_wallpaper(path):
    """Whether a wallpaper bitmap is the default, Bubble City: its 192 x 108 thumbnail against the picture's own (the
    build's, stamped with its corner note, is 0.4 levels away on average; Bubble Earth 68.5, Aqua World 54.7, Glass City
    75.5, Aero Meadow 72.8; measured 2026-10-31)."""
    from PIL import Image
    with Image.open(WALLPAPER) as picture:
        want = picture.convert('RGB').resize((192, 108), Image.Resampling.LANCZOS)
    with Image.open(path) as image:
        have = image.convert('RGB').resize((192, 108), Image.Resampling.LANCZOS)
    return float(np.abs(np.asarray(want, dtype=np.int16) - np.asarray(have, dtype=np.int16)).mean()) <= 8


# ShadowMountPlus's own banner and data folder, in every build of it (1.7beta4..10: 56 and 19 times); the title names
# it only by its plain name, in the payload checklist the owner asked for, and must not be flagged for that.
SMP_BINARY = re.compile(rb'ShadowMount\+ v|/data/shadowmount/')


def never_ship(root, label, findings):
    for path in sorted(root.rglob('*')):
        if not path.is_file():
            continue
        relative = f'{label}/{path.relative_to(root)}'
        if path.name.lower() == 'proton.bmp':
            if not is_default_wallpaper(path):
                findings.add('file that must never ship in a build', f'{relative} (a wallpaper that is not Bubble City)',
                             relative)
        elif 'shadowmount' in path.name.lower() or (path.suffix.lower() in ('.elf', '.bin', '') and path.stat().st_size
                                                    < 64 << 20 and SMP_BINARY.search(path.read_bytes())):
            # each user runs their own upstream ShadowMountPlus; MihawkOS never ships one, patched or not (2026-10-18)
            findings.add('file that must never ship in a build', f'{relative} (ShadowMountPlus)', relative)
        elif TEST_PROGRAM.search(path.name) or '/prefixes/probe/' in f'/{relative}/':
            findings.add('file that must never ship in a build', f'{relative} (a test program)', relative)
        elif path.suffix.lower() in ('.dll', '.exe', '.sys', '.drv', '.ocx'):
            data = path.read_bytes()
            if data[:2] == b'MZ' and b'Wine builtin DLL' not in data[:1024] and MICROSOFT in data:
                findings.add('file that must never ship in a build', f'{relative} (a native Microsoft binary)', relative)


def check_history(repo, revs, findings):
    command = ['git', '-C', str(repo), 'log', '--format=%x01C %h%n%an <%ae>%n%cn <%ce>%n%B', '-p', '--text', '-U0',
               '--no-color', '--no-ext-diff', '--no-textconv', '--diff-merges=first-parent', *revs]
    commit, path, commits = '?', '', 0
    with subprocess.Popen(command, stdout=subprocess.PIPE) as proc:
        for line in proc.stdout:
            if line.startswith(b'\x01C '):
                commit, path, commits = line[3:].strip().decode(), '', commits + 1
            elif line.startswith(b'+++ '):
                path = line[6:].rstrip(b'\n').decode('utf-8', 'replace') if line.startswith(b'+++ b/') else ''
            elif not line.startswith((b'-', b'@@', b'diff --git', b'index ', b'--- ')):
                findings.text(line, f'{commit} {path}'.strip(), path)
    if proc.returncode:
        raise SystemExit('publication check: git log failed')
    return commits


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=('tree', 'history', 'all'))
    parser.add_argument('revs', nargs='*')
    parser.add_argument('--repo', type=Path, default=TOOLS.parent)
    parser.add_argument('--env', action='append')
    parser.add_argument('--allow', action='append', default=[])
    args = parser.parse_args(argv)
    repo = args.repo.resolve()
    envs = args.env or [repo / '.env', repo.parent / 'PS5_Vulkan/.env', repo.parent / 'PS5_Proton/.env']
    credentials = {v for v in env_values(envs, {'PS5_FTP_USER', 'PS5_FTP_PASSWORD'})
                   if len(v) >= 4 and v.lower() not in {'anonymous', 'user', 'pass', 'password'}}
    consoles = env_values(envs, {'PS5_HOST', 'ORIGINAL_PS5_HOST'})
    import getpass
    findings = Findings(consoles, credentials, lan_addresses(), getpass.getuser(), set(args.allow))
    out = [f'publication check of {repo.name} ({len(consoles)} console addresses and {len(credentials)} FTP '
           f'credentials from .env)']
    if args.mode in ('tree', 'all'):
        out.append(f'  tree: {check_tree(repo, findings)} files (tracked, and untracked ones not ignored)')
    if args.mode in ('history', 'all'):
        out.append(f'  history: {check_history(repo, args.revs or ["--all"], findings)} commits')
    failed = findings.report(out, args.mode in ('tree', 'all'))
    out.append('verdict: clean' if not failed else f'verdict: {failed} {"category" if failed == 1 else "categories"} to fix')
    print('\n'.join(out))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
