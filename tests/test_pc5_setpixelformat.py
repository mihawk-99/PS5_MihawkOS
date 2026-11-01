#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2413 (setting a window's pixel format again to the same one succeeds). gdi32's opengl.c is one the PS5
series leaves alone, so the pin's copy with the patch is compiled with Wine's own headers and run against an opengl32
that refuses a second SetPixelFormat as Mesa's WGL does (tests/pc5_setpixelformat_harness.c)."""
import os
import unittest
from pathlib import Path

import pc5_support as support

FILES = ['dlls/gdi32/opengl.c', 'include']


class SetPixelFormat(unittest.TestCase):
    def run_with(self, patched):
        with support.scratch() as tmp:
            work = Path(tmp)
            if patched:
                support.patched_files(work, FILES, '2413-*.patch')
            else:
                support.pristine_files(work, FILES)
            flags = ['-D_UCRT', '-D__WINESRC__', '-D_WIN32', '-fshort-wchar', '-fno-builtin', f'-I{work}/include',
                     f'-I{work}/include/msvcrt', f'-I{work}/dlls/gdi32']
            return support.compile_and_run([work / 'dlls/gdi32/opengl.c', support.ROOT / 'tests/pc5_setpixelformat_harness.c'],
                                           work / 'spf', flags)

    def test_patched(self):
        result = self.run_with(True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('pc5 setpixelformat passed', result.stdout)

    @unittest.skipUnless(os.environ.get('PC5_SHOW_OLD'), 'the unpatched behaviour is shown on request (PC5_SHOW_OLD=1)')
    def test_pinned_fails_the_repeat(self):
        result = self.run_with(False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('the same format again succeeds', result.stdout)


if __name__ == '__main__':
    unittest.main()
