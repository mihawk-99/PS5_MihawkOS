#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2421 (the PS5 high area is as large as the kernel grants). ntdll's virtual.c is not built on the host, so
the sizing functions are cut out of the patch's own added and context lines (PS5_HIGH_* definitions through the halving
loop and the environment parse; the console reservation call that follows them is the fake kernel here) and compiled
into tests/pw_high_area_harness.c, which checks the size granted against a kernel that grants at most N bytes."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def cut():
    patch = next((ROOT / 'wine/patches').glob('2421-*.patch')).read_text()
    hunk = patch[patch.index('@@ -1007,6'):]
    hunk = hunk[:hunk.index('\n@@ -1224')]
    lines = [l[1:] for l in hunk.split('\n')[1:] if l[:1] in ' +']
    text = '\n'.join(lines)
    start = text.index('#define PS5_HIGH_RESERVE ')
    end = text.index('static int ps5_reserve_high_area')
    text = '\n'.join(l for l in text[start:end].split('\n') if l not in ('#endif', '#ifdef __PROSPERO__'))
    return '#include <stdint.h>\n' + text


class HighArea(unittest.TestCase):
    def test_largest_granted_size_and_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / 'high_area.inc').write_text(cut())
            exe = work / 'harness'
            build = subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                                    f'-I{work}', str(ROOT / 'tests/pw_high_area_harness.c'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('pw high area passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
