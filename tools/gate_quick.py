#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Quick mode's choice (tools/gate.py quick): the gate cases a change touches, from the paths it changes.

A quick run is for the loop between full gates: it never records a pass, and the full gate (tools/gate.py run) stays
the only way code is committed. Each changed path takes the cases of the first rule that matches it:

  notes and evidence (gate.notes)   none
  RULES below                       the cases listed (ALL: every gate case)
  tests/<x>-probe.c                 the gate cases that run <x>-probe.exe, <x>32-probe.exe or <x>64-probe.exe, or are
                                    named <x>, <x>32 or <x>64 (run-battery.py's CASES)
  tools/run-<x>.py                  the gate cases whose own launch runs it (run-battery.py's runner_of)
  anything else                     ALL

A path no rule knows takes every case, so a quick run under-selects only where a rule is wrong; a rule is added only
for a file that only those cases reach. tests/test_gate_quick.py checks that every rule matches a tracked file and
names only gate cases.

  tools/gate_quick.py [--since REV]   the cases the working tree's changes since REV (default HEAD) touch
"""
import argparse
import fnmatch
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALL = 'ALL'

_spec = importlib.util.spec_from_file_location('run_battery', ROOT / 'tools/run-battery.py')
battery = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(battery)
_spec = importlib.util.spec_from_file_location('gate', ROOT / 'tools/gate.py')
gate = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gate)

# The cases that run in the grouped launch or the persistent session (run-battery.py's GROUPS).
GROUPED = [name for names in battery.GROUPS.values() for name in names]
STEAM_GUIDE = ['steam-guide-pages', 'steam-guide-screens']
# Cases that hold GiBs of memory never share a session or a grouped launch in a quick run (run-battery.py --alone): on a
# fresh boot payload-memory's two helpers of 2 GiB each pushed the page-table pool to the guard (memory session,
# 2026-10-28). large-shared-section's 13.5 GiB section already runs in a launch of its own.
HOLDS_GIB = {'payload-memory', 'large-shared-section'}

# (fnmatch pattern on the repository path, cases), first match wins.
RULES = [
    # Host-only: nothing on the console runs them.
    ('tests/test_*', []),
    ('tests/extract_*.py', []),
    ('tools/git-hooks/*', []),
    ('tools/gate_quick.py', []),
    ('tools/host-tests.sh', []),
    ('tools/privacy_check.py', []),
    ('tools/publication-check.py', []),
    ('.github/*', []),
    # Probes whose .exe is named otherwise than their source (tools/build-wine-probe.sh).
    ('tests/theme-notification-probe.c', ['theme-notify32', 'theme-notify64']),
    ('tests/initial-environment-probe.c', ['initial-env32', 'initial-env64']),
    ('tests/nt-relative-dir-probe.c', ['relative-dir32', 'relative-dir64']),
    ('tests/game-mode-probe.c', ['game-mode64', 'game-mode-dxvk64']),
    ('tests/wallpaper-choice-probe.c', ['wallpaper-choice']),
    # The runners of the grouped cases.
    ('tests/gate-sequence-probe.c', GROUPED),
    ('tests/wt-session.c', GROUPED),
    ('tools/run-wine-session.py', GROUPED),
    # The title's feature modules only these cases reach (their includes: ps5/CMakeLists.txt and the module itself).
    ('src/pw_steam_guide.*', STEAM_GUIDE),
    ('src/pw_steam_screen.*', STEAM_GUIDE),
    ('src/pw_setup.*', ['setup-tour'] + STEAM_GUIDE),
    ('src/pw_first_run.*', ['setup-tour']),
    ('src/pw_game_folders.*', ['games-autodetect']),
    ('src/pw_detailed_logs.*', ['detailed-logs']),
    ('src/pw_controls.*', ['controls-menu', 'mouse-controls']),
    ('src/pw_game_mode.*', ['game-mode64', 'game-mode-dxvk64']),
    ('src/pw_wallpaper.*', ['wallpaper-choice']),
    ('src/pw_shortcut.h', ['desktop-view']),   # File Explorer's and the desktop's Create shortcut: the desktop-view probe makes one
    ('wine/ps5/pw_shell_shortcut.h', ['desktop-view']),
    ('src/pw_browser*', ['mihawk-browser']),
    ('src/pw_memory_guard.*', ['memory-guard']),
    ('src/pw_umip.*', ['umip32', 'umip64']),   # ntdll's UMIP emulation (patch 2102), reached only on those instructions
]
PROBE = re.compile(r'tests/([\w-]+)-probe\.c')
RUNNER = re.compile(r'tools/(run-[\w-]+\.py)')


def probe_cases(stem):
    exes = {f'{stem}-probe.exe', f'{stem}32-probe.exe', f'{stem}64-probe.exe'}
    names = {stem, f'{stem}32', f'{stem}64'}
    cases = dict(battery.CASES)
    return [name for name in battery.GATE_CASES if name in names or any(
        word in exes for arg in cases[name] if isinstance(arg, str) for word in re.split(r'[\\/= ]', arg))]


def cases_for(path):
    """The gate cases a change to path touches: a list, or ALL."""
    if gate.notes(path):
        return []
    for pattern, cases in RULES:
        if fnmatch.fnmatchcase(path, pattern):
            return cases
    match = PROBE.fullmatch(path)
    if match:
        return probe_cases(match.group(1)) or ALL
    match = RUNNER.fullmatch(path)
    if match and match.group(1) != 'run-console.py':
        return [name for name in battery.GATE_CASES if battery.runner_of(name) == match.group(1)] or ALL
    return ALL


def select(paths):
    """The cases the paths touch, in GATE_CASES order, and the paths that took every case."""
    wanted, unknown = set(), []
    for path in paths:
        cases = cases_for(path)
        if cases == ALL:
            unknown.append(path)
        else:
            wanted.update(cases)
    if unknown:
        return list(battery.GATE_CASES), unknown
    return [name for name in battery.GATE_CASES if name in wanted], []


def changed_paths(since='HEAD'):
    """What the working tree changes since `since` (from its merge base with HEAD, so a branch name counts the
    branch's own changes): tracked changes and untracked files git does not ignore."""
    def lines(*args):
        return [line for line in subprocess.check_output(['git', *args], cwd=ROOT, text=True).split('\0') if line]
    base = subprocess.check_output(['git', 'merge-base', since, 'HEAD'], cwd=ROOT, text=True).strip()
    return sorted(set(lines('diff', '--name-only', '-z', base)) | set(lines('ls-files', '--others', '--exclude-standard', '-z')))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--since', default='HEAD', help='the revision the change is counted from')
    args = parser.parse_args()
    cases, unknown = select(changed_paths(args.since))
    if unknown:
        print(f'every gate case: {len(unknown)} path(s) no rule narrows ({", ".join(unknown[:5])}'
              f'{", ..." if len(unknown) > 5 else ""})')
    print(' '.join(cases) if cases else 'no gate case: only notes, evidence or host-only files changed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
