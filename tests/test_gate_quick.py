#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The gate's records per console (tools/gate.py) and quick mode's choice of cases (tools/gate_quick.py)."""
import importlib.util
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gate_quick', ROOT / 'tools/gate_quick.py')
quick = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quick)
gate, battery = quick.gate, quick.battery


class ChoiceTests(unittest.TestCase):
    def test_every_rule_matches_a_tracked_file_and_names_only_gate_cases(self):
        tracked = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'], cwd=ROOT, text=True).splitlines()
        for pattern, cases in quick.RULES:
            self.assertTrue(any(quick.fnmatch.fnmatchcase(path, pattern) for path in tracked), pattern)
            self.assertEqual([c for c in cases if c not in battery.GATE_CASES], [], pattern)

    def test_unknown_code_takes_every_case(self):
        for path in ('wine/patches/0001-x.patch', 'native/proton_wine.cpp', 'tools/run-console.py', 'tools/gate.py',
                     'tests/appearance.c', 'tests/hang-probe.c', 'VERSION'):
            self.assertEqual(quick.cases_for(path), quick.ALL, path)
        self.assertEqual(quick.select(['docs/PLAN.md', 'src/pw_umip.c', 'ps5/src/main.cpp']),
                         (list(battery.GATE_CASES), ['ps5/src/main.cpp']))

    def test_notes_and_host_only_files_take_none(self):
        self.assertEqual(quick.select(['docs/PLAN.md', 'evidence/x.json', 'tests/test_pw_umip.c',
                                       'tools/git-hooks/pre-commit', 'README.md']), ([], []))

    def test_probes_and_runners_take_their_cases(self):
        self.assertEqual(quick.cases_for('tests/umip-probe.c'), ['umip32', 'umip64'])
        self.assertEqual(quick.cases_for('tests/vm-protect-probe.c'), ['payload-vm', 'vm-truth32', 'vm-truth64'])
        self.assertEqual(quick.cases_for('tests/window-probe.c'), ['telemetry-teardown', 'doom-backstop', 'osk-open64'])
        self.assertEqual(quick.cases_for('tests/desktop-view-probe.c'), ['desktop-view'])
        self.assertEqual(quick.cases_for('tools/run-shell.py'), ['shell32', 'shell64'])
        self.assertEqual(quick.cases_for('tools/run-launcher.py'), ['controls-menu', 'setup-tour', 'steam-guide-pages'])
        self.assertEqual(quick.select(['src/pw_steam_guide.c', 'tests/umip-probe.c'])[0],
                         ['umip32', 'umip64', 'steam-guide-pages', 'steam-guide-screens'])


class RecordTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.state = Path(scratch.name)
        context = patch.object(gate, 'STATE', self.state)
        context.start()
        self.addCleanup(context.stop)

    def test_a_pass_on_any_console_admits_exactly_its_build(self):
        (self.state / 'passed-aaaaaaaaaaaa.json').write_text(json.dumps({'fingerprint': 'f1', 'console': 'aaaaaaaaaaaa'}))
        (self.state / 'passed-bbbbbbbbbbbb.json').write_text(json.dumps({'fingerprint': 'f2', 'console': 'bbbbbbbbbbbb'}))
        (self.state / 'passed.json').write_text(json.dumps({'fingerprint': 'f0'}))   # one record for every console, before
        self.assertEqual(gate.passing_record('f1')['console'], 'aaaaaaaaaaaa')
        self.assertEqual(gate.passing_record('f2')['console'], 'bbbbbbbbbbbb')
        self.assertEqual(gate.passing_record('f0'), {'fingerprint': 'f0'})
        self.assertIsNone(gate.passing_record('f3'))

    def test_a_quick_run_never_records_a_pass(self):
        run = Mock(return_value=SimpleNamespace(returncode=0))
        with patch.object(gate, 'chooser', return_value=quick), \
                patch.object(quick, 'changed_paths', return_value=['tests/umip-probe.c']), \
                patch.object(gate, 'deployed_build', return_value=({'fingerprint': 'f1'}, 'aaaaaaaaaaaa')) as deployed, \
                patch.object(gate.subprocess, 'run', run), patch('sys.stdout', io.StringIO()):
            status = gate.quick(SimpleNamespace(name='quick-test', since='HEAD'))
        self.assertEqual(status, 0)
        deployed.assert_called_once()
        command = run.call_args[0][0]
        self.assertEqual(command[command.index('--only') + 1:], ['umip32', 'umip64'])
        self.assertNotIn('--gate', command)
        self.assertEqual(list(self.state.iterdir()), [])

    def test_a_case_that_holds_gibs_never_shares_a_session(self):
        self.assertLessEqual(quick.HOLDS_GIB, set(battery.GATE_CASES))
        run = Mock(return_value=SimpleNamespace(returncode=0))
        with patch.object(gate, 'chooser', return_value=quick), \
                patch.object(quick, 'changed_paths', return_value=['tests/process-probe.c']), \
                patch.object(gate, 'deployed_build', return_value=({'fingerprint': 'f1'}, 'aaaaaaaaaaaa')), \
                patch.object(gate.subprocess, 'run', run), patch('sys.stdout', io.StringIO()):
            gate.quick(SimpleNamespace(name='quick-test', since='HEAD'))
        command = run.call_args[0][0]
        self.assertEqual(command[command.index('--alone') + 1:], ['payload-memory'])
        self.assertIn('payload-eight', command[command.index('--only') + 1:command.index('--alone')])

    def test_quick_refuses_a_change_no_rule_narrows(self):
        with patch.object(gate, 'chooser', return_value=quick), \
                patch.object(quick, 'changed_paths', return_value=['native/proton_wine.cpp']), \
                patch.object(gate, 'deployed_build') as deployed:
            with self.assertRaisesRegex(SystemExit, 'full gate'):
                gate.quick(SimpleNamespace(name='quick-test', since='HEAD'))
        deployed.assert_not_called()


if __name__ == '__main__':
    unittest.main()
