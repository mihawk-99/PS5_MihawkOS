#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2422 (no debugger starts in a PS5 title or helper). kernelbase's debug.c is not built on the host, so
ps5_no_debugger is cut out of the patch and run against a fake environment (tests/pw_no_debugger_harness.c); a second
process runs it with the variable absent, as on a PC; and the patch's UnhandledExceptionFilter hunk is checked to
call the test before start_debugger_atomic and to keep the game's own filter (top_filter) before it."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch_text():
    return next((ROOT / 'wine/patches').glob('2422-*.patch')).read_text()


def cut():
    added = '\n'.join(l[1:] for l in patch_text().split('\n') if l.startswith('+') and not l.startswith('+++'))
    start = added.index('static BOOL ps5_no_debugger(void)')
    end = added.index('\n}\n', start) + 3
    return added[start:end]


class NoDebugger(unittest.TestCase):
    def build(self, work, extra=()):
        (work / 'no_debugger.inc').write_text(cut())
        exe = work / 'harness'
        build = subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', f'-I{work}',
                                *extra, str(ROOT / 'tests/pw_no_debugger_harness.c'), '-o', str(exe)],
                               capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stderr)
        return exe

    def test_a_title_is_recognised_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = subprocess.run([str(self.build(Path(tmp)))], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('pw no debugger passed', run.stdout)

    def test_a_pc_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            source = (ROOT / 'tests/pw_no_debugger_harness.c').read_text()
            source = source.replace('environment_value = "/data/ps5proton/runtime/ge11-7/lib/wine/x86_64-unix";',
                                    'environment_value = NULL;').replace('!ps5_no_debugger() || !ps5_no_debugger()',
                                                                         'ps5_no_debugger() || ps5_no_debugger()')
            (work / 'pc.c').write_text(source)
            (work / 'no_debugger.inc').write_text(cut())
            exe = work / 'pc'
            build = subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', f'-I{work}',
                                    str(work / 'pc.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_the_filter_keeps_the_games_own_filter_first(self):
        text = patch_text()
        # the hunk's context holds the game's filter call, and the added test follows it and precedes the debugger
        self.assertRegex(text, r'if \(ret != EXCEPTION_CONTINUE_SEARCH\) return ret;\n         }\n \n\+        if \(ps5_no_debugger\(\)\)')
        self.assertLess(text.index('+        if (ps5_no_debugger())'), text.index('!start_debugger_atomic( epointers )'))


if __name__ == '__main__':
    unittest.main()
