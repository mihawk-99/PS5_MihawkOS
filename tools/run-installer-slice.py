#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The installer slice: the six Tier 1 installer kinds, end to end, on the PC's Wine (the oracle) and on the console.

  tools/run-installer-slice.py host    [KIND ...]   in a fresh prefix of the runtime's Wine built for this PC's display
  tools/run-installer-slice.py console [KIND ...]   in an owned throwaway game entry and prefix on the console
  tools/run-installer-slice.py compare [KIND ...]   each check's console verdict against the host's
  tools/run-installer-slice.py --name RUN console KIND ...   as a battery case: also evidence/RUN.json, passed when
                                                           every kind returned and matches the host (tools/run-battery.py)

KIND: nsis inno msi steam vcredist dxsetup (default: every kind whose fixture is built). The fixtures come from
tools/build-installer-fixtures.sh; tests/installer-slice-probe.c is the program that runs one kind, timing each
step (install silently, files, registry, shortcuts, native DLLs, the installed program, reinstall or repair,
uninstall, a clean prefix). Records: evidence/installer-slice/<where>-<kind>.json, and compare writes
evidence/installer-slice/comparison.json. The owner's prefixes are never used: the console run makes
/data/ps5proton/games/proton-installer-test (marked as this runner's) and its prefix, and removes both afterwards.
"""
import argparse
import io
import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / '.deps/installers/fixtures'
OUT = ROOT / 'evidence/installer-slice'
# The oracle has to make windows (Inno's and Burn's silent installers still do): the runtime's own Wine revision
# built with its X11 driver (tools/build-wine-host-display.sh); the pinned headless build cannot.
HOST_WINE = ROOT / '.deps/wine-host-display/build/wine'
HOST_SERVER = ROOT / '.deps/wine-host-display/build/server/wineserver'
KINDS = {'nsis': ['fixture-nsis.exe'], 'inno': ['fixture-inno.exe'], 'msi': ['fixture.msi'], 'steam': [],
         'vcredist': ['vc_redist.x64.exe', 'vc_redist.x86.exe'], 'dxsetup': ['directx_Jun2010_redist.exe']}
BUDGET = {'vcredist': 900, 'dxsetup': 1200}   # seconds of one console launch; the first launch also makes the prefix
ENTRY = 'proton-installer-test'
GAME = '/data/ps5proton/games/' + ENTRY
PREFIX = '/data/ps5proton/prefixes/' + ENTRY
OWNER = b'tools/run-installer-slice.py\n'
MARKER = '.ps5-proton-test'
# A console log line can be a JSON string: its escaped newline and closing quote are not part of the detail.
CHECK = re.compile(r'PS5_PROTON_INSTALLER_CHECK (\w+) ([\w-]+) (PASS|FAIL|N/A) (\d+) ms: (.*?)(?:\\n)?"?,?$')
VERDICT = re.compile(r'PS5_PROTON_INSTALLER (PASS|FAIL): (.*?)(?:\\n)?"?,?$')


def parse(text):
    """The checks of one run, each once (the program writes every line to standard error and the debug log)."""
    checks, seen, verdict = [], set(), None
    for line in text.splitlines():
        found = CHECK.search(line.rstrip('\r'))
        if found and found.groups() not in seen:
            seen.add(found.groups())
            checks.append({'step': found.group(2), 'verdict': found.group(3), 'ms': int(found.group(4)), 'detail': found.group(5)[:300]})
        elif (found := VERDICT.search(line)) and not verdict:
            verdict = {'passed': found.group(1) == 'PASS', 'summary': found.group(2)[:200]}
    return checks, verdict


def built(kinds):
    missing = [k for k in kinds for name in KINDS[k] if not (FIXTURES / name).is_file()]
    if not (FIXTURES / 'installer-slice-probe.exe').is_file():
        raise SystemExit('build the fixtures first: tools/build-installer-fixtures.sh')
    return [k for k in kinds if not any(not (FIXTURES / name).is_file() for name in KINDS[k])], sorted(set(missing))


def save(where, kind, record):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f'{where}-{kind}.json').write_text(json.dumps(record, indent=1) + '\n')


def host(kinds):
    if not HOST_WINE.is_file():
        raise SystemExit('build the display-capable host Wine first: tools/build-wine-host-display.sh')
    for kind in kinds:
        with tempfile.TemporaryDirectory(prefix='pw-installer-', dir=ROOT / '.deps/installers') as temporary:
            prefix, capture = Path(temporary) / 'prefix', Path(temporary) / 'out.txt'
            env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='winemenubuilder.exe=d')
            started = time.monotonic()
            subprocess.run([str(HOST_WINE), 'wineboot', '-i'], env=env, timeout=300, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            made = time.monotonic() - started
            fixtures = 'Z:' + str(FIXTURES).replace('/', '\\')
            # unquoted: the list argument is quoted again on its way to cmd, which would keep the inner quotes
            assert ' ' not in fixtures, 'the fixture folder must have no spaces'
            command = f'{fixtures}\\installer-slice-probe.exe {kind} {fixtures} > Z:{str(capture).replace("/", chr(92))} 2>&1'
            # inside a Wine virtual desktop, so no installer window reaches the PC's screen
            try:
                subprocess.run([str(HOST_WINE), 'explorer', '/desktop=pwinstall,1024x768', 'cmd', '/c', command], env=env,
                               timeout=BUDGET.get(kind, 600), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                timed_out = False
            except subprocess.TimeoutExpired:
                timed_out = True
            subprocess.run([str(HOST_SERVER), '-k'], env=env, stderr=subprocess.DEVNULL)
            text = capture.read_text(errors='replace') if capture.is_file() else ''
        checks, verdict = parse(text)
        record = {'where': 'host', 'kind': kind, 'wine': str(HOST_WINE.relative_to(ROOT)), 'prefix_seconds': round(made, 1),
                  'seconds': round(time.monotonic() - started, 1), 'timed_out': timed_out, 'verdict': verdict, 'checks': checks}
        save('host', kind, record)
        print(f"host {kind}: {'timed out' if timed_out else (verdict or {}).get('summary', 'no verdict')}", flush=True)


def console(kinds, name=None):
    support = runpy.run_path(str(ROOT / 'tools/run-prefix-test.py'))
    ps5, connect, listing, remove_tree = support['ps5_console'], support['connect'], support['listing'], support['remove_tree']
    settings = ps5.load_settings()

    def remove_owned(ftp):
        game, prefix = listing(ftp, GAME), listing(ftp, PREFIX)
        if game is not None:
            marker = io.BytesIO()
            ftp.retrbinary('RETR ' + GAME + '/' + MARKER, marker.write)
            if marker.getvalue() != OWNER:
                raise SystemExit('the installer test entry has another owner: nothing removed')
        elif prefix is not None:
            raise SystemExit('the installer test prefix has no ownership marker: nothing removed')
        for path, contents in ((PREFIX, prefix), (GAME, game)):
            if contents is not None:
                remove_tree(ftp, path, {'files': 0, 'folders': 0})

    fixtures = 'Z:' + GAME.replace('/', '\\')
    game = {'name': 'Installer slice (test)', 'prefix': ENTRY, 'desktop': False,
            'executable': fixtures + '\\installer-slice-probe.exe', 'directory': fixtures, 'env': ['PROTON_LAYOUT=launcher']}
    names = ['installer-slice-probe.exe'] + [f'steam-app/{p.name}' for p in sorted((FIXTURES / 'steam-app').iterdir())] + \
            sorted({name for kind in kinds for name in KINDS[kind]})
    with connect(settings) as ftp:
        remove_owned(ftp)
        ftp.mkd(GAME)
        ftp.mkd(GAME + '/steam-app')
        for filename, data in [(MARKER, OWNER), ('game.json', (json.dumps(game) + '\n').encode())]:
            ftp.storbinary(f'STOR {GAME}/{filename}', io.BytesIO(data))
        for name in names:
            with (FIXTURES / name).open('rb') as f:
                ftp.storbinary(f'STOR {GAME}/{name}', f)
            if ftp.size(f'{GAME}/{name}') != (FIXTURES / name).stat().st_size:
                raise SystemExit(f'upload of {name} differs in size')
    runs = []
    try:
        for kind in kinds:
            run = f'installer-{kind}-{time.strftime("%Y-%m-%d-%H%M%S")}'
            budget = BUDGET.get(kind, 600)
            started = time.monotonic()
            result = subprocess.run([sys.executable, str(ROOT / 'tools/run-console.py'), '--name', run, '--profile', ENTRY,
                                     '--args', f'{kind} "{fixtures}"', '--marker', 'PS5_PROTON_INSTALLER', '--no-stdout',
                                     '--seconds', str(budget), '--watchdog', str(budget + 90), '--exit-stall', '60'],
                                    capture_output=True, text=True, cwd=ROOT)
            text = '\n'.join(p.read_text(errors='replace') for p in sorted((ROOT / 'klog').glob(run + '*')) if p.suffix in ('.log', '.txt'))
            checks, verdict = parse(text)
            execution = json.loads((ROOT / 'evidence' / (run + '.json')).read_text()) if (ROOT / 'evidence' / (run + '.json')).is_file() else {}
            record = {'where': 'console', 'kind': kind, 'run': run, 'seconds': round(time.monotonic() - started, 1),
                      'console_result': execution.get('result'), 'returned_to_launcher': execution.get('returned_to_launcher'),
                      'verdict': verdict, 'checks': checks}
            save('console', kind, record)
            print(f"console {kind}: {(verdict or {}).get('summary', 'no verdict: ' + str(execution.get('result')))}", flush=True)
            runs.append(record)
            # Before the stop below: a refused launch's reason (the leftover helpers it found) is only in this output.
            if result.returncode and not checks:
                print(re.sub(r'\b\d{1,3}(\.\d{1,3}){3}\b', '<console>', (result.stdout + result.stderr)[-1500:]), flush=True)
            if not execution.get('returned_to_launcher'):
                raise SystemExit(f'{run}: no verified return to the launcher; the slice stops here')
    finally:
        if ' count=0 ' in ps5.ps5vkctl_command(settings, 'procs', timeout=20) + ' ':
            with connect(settings) as ftp:
                remove_owned(ftp)
            print('installer test entry and prefix removed', flush=True)
        else:
            print('console not idle: the installer test entry is left for the next run to remove', flush=True)
        if name:
            verdict = compare([r['kind'] for r in runs], write=False) == 0 if len(runs) == len(kinds) else False
            returned = bool(runs) and all(r.get('returned_to_launcher') for r in runs)
            (ROOT / 'evidence' / (name + '.json')).write_text(json.dumps(
                {'run': name, 'kinds': kinds, 'passed': verdict and returned, 'returned_to_launcher': returned,
                 'result': 'finished' if returned else 'stopped', 'runs': [r['run'] for r in runs],
                 'seconds': round(sum(r['seconds'] for r in runs), 1)}, indent=1) + '\n')


def compare(kinds, write=True):
    """Every console check against the host's (by step and what it checked); a FAIL where the host passes is a
    console-only difference; a FAIL on both is the installer's or Wine's, not the port's."""
    result = {}
    for kind in kinds:
        h, c = OUT / f'host-{kind}.json', OUT / f'console-{kind}.json'
        if not h.is_file() or not c.is_file():
            result[kind] = {'passed': False, 'missing': [p.name for p in (h, c) if not p.is_file()]}
            continue
        hr, cr = json.loads(h.read_text()), json.loads(c.read_text())
        key = lambda check: (check['step'], re.sub(r'\d+', 'N', check['detail']) if check['step'].startswith(('install', 'reinstall', 'uninstall', 'clean')) and 'exit code' in check['detail'] else check['detail'])
        host_verdicts = {key(x): x['verdict'] for x in hr['checks']}
        differences = [f"{x['step']}: console {x['verdict']}, host {host_verdicts.get(key(x), 'absent')}: {x['detail']}"
                       for x in cr['checks'] if x['verdict'] == 'FAIL' and host_verdicts.get(key(x)) != 'FAIL']
        both = [f"{x['step']}: {x['detail']}" for x in cr['checks'] if x['verdict'] == 'FAIL' and host_verdicts.get(key(x)) == 'FAIL']
        timing = {x['step']: {'console_ms': x['ms'], 'host_ms': next((y['ms'] for y in hr['checks'] if y['step'] == x['step']), None)}
                  for x in cr['checks'] if x['step'] in ('install', 'reinstall', 'uninstall')}
        result[kind] = {'passed': bool(cr['verdict']) and not differences, 'console': (cr['verdict'] or {}).get('summary'),
                        'host': (hr['verdict'] or {}).get('summary'), 'console_only_failures': differences,
                        'failing_on_both': both, 'timing': timing}
    if write:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / 'comparison.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result, indent=1))
    return 0 if all(v['passed'] for v in result.values()) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=['host', 'console', 'compare'])
    parser.add_argument('--name', help='console: write evidence/NAME.json with the verdict, as a battery case')
    parser.add_argument('kinds', nargs='*')
    args = parser.parse_args()
    if any(k not in KINDS for k in args.kinds):
        parser.error('kinds: ' + ' '.join(KINDS))
    kinds = args.kinds or list(KINDS)
    if args.command == 'compare':
        return compare(kinds)
    kinds, missing = built(kinds)
    if missing:
        print('left out, fixture not built: ' + ', '.join(missing), flush=True)
    if args.command == 'host':
        host(kinds)
    else:
        console(kinds, args.name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
