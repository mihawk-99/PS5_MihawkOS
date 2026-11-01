#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2426 (another process has pages; the log says what a program learns of it). ntdll's process.c is not built on
the host, so the two functions are cut out of the patch and run against the few Windows types they use
(tests/pw_foreign_process_harness.c); the patch's call sites are checked as text: the floor applies only when the server had
nothing and the process is not the caller's, and the log call comes last."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch_text():
    return next((ROOT / 'wine/patches').glob('2426-*.patch')).read_text()


def cut():
    added = '\n'.join(l[1:] for l in patch_text().split('\n') if l.startswith('+') and not l.startswith('+++'))
    start = added.index('static void ps5_floor_vm_counters')
    end = added.index('static void ps5_note_foreign_query')
    return '\n'.join(l for l in added[start:end].split('\n') if l not in ('#ifdef __PROSPERO__', '#endif'))


class ForeignProcess(unittest.TestCase):
    def test_the_floor_and_the_description(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / 'foreign.inc').write_text(cut())
            exe = work / 'harness'
            build = subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', f'-I{work}',
                                    str(ROOT / 'tests/pw_foreign_process_harness.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('pw foreign process passed', run.stdout)

    def test_call_sites(self):
        text = patch_text()
        self.assertIn('if (!pvmi.WorkingSetSize && ps5_is_own_process( handle )) fill_vm_counters( &pvmi, -1 );\n+                        else ps5_floor_vm_counters( &pvmi );', text)
        self.assertIn('ps5_note_foreign_query( handle, class, info, len, ret );\n+#endif\n     if (ret_len) *ret_len = len;', text)
        self.assertIn('TRACE( "pid %04x access', text)


if __name__ == '__main__':
    unittest.main()
