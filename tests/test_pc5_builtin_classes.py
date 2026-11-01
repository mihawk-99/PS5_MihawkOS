#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2414 (win32u registers the builtin classes without waiting on another thread). class.c is a file the PS5
series leaves alone: the text of register_builtins and register_builtin_classes is cut from the pin's copy with the patch
(and without it, to show the hang) and run against stand-ins that take the loader lock where win32u calls back into user32
(tests/pc5_builtin_classes_harness.c)."""
import os
import subprocess
import unittest
from pathlib import Path

import pc5_support as support

FILES = ['dlls/win32u/class.c']


def cut(text):
    start = text.index('static void register_builtins(')
    end = text.index('\n}\n', text.index('void register_builtin_classes(void)')) + 3
    return text[start:end]


class BuiltinClasses(unittest.TestCase):
    def build(self, patched, tmp):
        work = Path(tmp)
        if patched:
            support.patched_files(work, FILES, '2414-*.patch')
        else:
            support.pristine_files(work, FILES)
        (work / 'counts.inc').write_text(cut((work / FILES[0]).read_text()))
        flags = [f'-I{work}', f'-I{support.ROOT}/tests'] + ([] if patched else ['-DOLD'])
        built = subprocess.run(['cc', '-O1', '-w', *flags, '-pthread', '-o', str(work / 'h'),
                                str(support.ROOT / 'tests/pc5_builtin_classes_harness.c')], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr[-2000:])
        return work / 'h'

    def run_case(self, patched, case):
        with support.scratch() as tmp:
            exe = self.build(patched, tmp)
            return subprocess.run([str(exe), case], capture_output=True, text=True, timeout=30)

    def test_a_thread_holding_the_loader_lock_gets_through(self):
        result = self.run_case(True, 'hold')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('hold passed', result.stdout)

    def test_many_threads_register_once(self):
        result = self.run_case(True, 'hammer')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('hammer passed', result.stdout)

    @unittest.skipUnless(os.environ.get('PC5_SHOW_OLD'), 'the pinned hang is shown on request (PC5_SHOW_OLD=1)')
    def test_pinned_code_hangs(self):
        result = self.run_case(False, 'hold')
        self.assertEqual(result.returncode, 1)
        self.assertIn('still waiting', result.stdout)


if __name__ == '__main__':
    unittest.main()
