#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2410 (FAPOBase's reference count is atomic): FAudio's own FAPOBase.c, from the pinned Wine, with the patch
applied, is hammered by eight threads (tests/pc5_apobase_threads.c). Files the PS5 series does not change, so the pin's
copy is the one the series builds."""
import os
import subprocess
import unittest
from pathlib import Path

import pc5_support as support

FILES = ['libs/faudio/src/FAPOBase.c', 'libs/faudio/src/FAudio_internal.h', 'libs/faudio/include']


class ApoBase(unittest.TestCase):
    def build(self, work, patched):
        if patched:
            support.patched_files(work, FILES, '2410-*.patch')
        else:
            support.pristine_files(work, FILES)
        shim = work / 'shim'
        shim.mkdir()
        # FAudio_internal.h wants windows.h on its Windows platform; FAPOBase.c uses one function of it.
        (shim / 'windows.h').write_text('#include <string.h>\n'
            'static inline char *lstrcpynA(char *d, const char *s, int n) { strncpy(d, s, n); if (n) d[n-1] = 0; return d; }\n')
        flags = ['-DFAUDIO_WIN32_PLATFORM', f'-I{shim}', f'-I{work}/libs/faudio/include', f'-I{work}/libs/faudio/src']
        return support.compile_and_run([work / 'libs/faudio/src/FAPOBase.c', support.ROOT / 'tests/pc5_apobase_threads.c'],
                                       work / 'apobase', flags)

    def test_patched_count_survives_threads(self):
        with support.scratch() as tmp:
            result = self.build(Path(tmp), True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('pc5 apobase passed', result.stdout)

    @unittest.skipUnless(os.environ.get('PC5_SHOW_OLD'), 'the unpatched count is shown on request (PC5_SHOW_OLD=1)')
    def test_pinned_count_loses_updates(self):
        with support.scratch() as tmp:
            result = self.build(Path(tmp), False)
        self.assertNotEqual(result.returncode, 0, 'the pinned FAudio kept its count: the test no longer shows the bug')


if __name__ == '__main__':
    unittest.main()
