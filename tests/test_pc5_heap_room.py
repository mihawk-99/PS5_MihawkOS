#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2416 (a large heap block gets a page of room after its data). ntdll's heap.c is not built on the host, so
the expression that sizes a large block's region is cut out of the pinned file with the patch (and without it, to show
the pin leaving no room) and evaluated for sizes that end exactly on a 64 KiB boundary and for sizes that overflow
(tests/pc5_heap_room_harness.c)."""
import os
import re
import subprocess
import unittest
from pathlib import Path

import pc5_support as support

FILES = ['dlls/ntdll/heap.c']


def cut(text):
    macros = '\n'.join(re.search(rf'^#define {name}\b.*$', text, re.M).group(0) for name in ('REGION_ALIGN', 'BLOCK_ALIGN', 'ROUND_SIZE'))
    expression = re.search(r'SIZE_T total_size = (ROUND_SIZE\(.*?\));\n    struct block \*block;', text, re.S).group(1)
    return macros + f'\n#define TOTAL_SIZE_EXPR {" ".join(expression.split())}\n'


class HeapRoom(unittest.TestCase):
    def run_with(self, patched):
        with support.scratch() as tmp:
            work = Path(tmp)
            if patched:
                support.patched_files(work, FILES, '2416-*.patch')
            else:
                support.pristine_files(work, FILES)
            (work / 'counts.inc').write_text(cut((work / FILES[0]).read_text()))
            return support.compile_and_run([support.ROOT / 'tests/pc5_heap_room_harness.c'], work / 'h', [f'-I{work}'])

    def test_every_large_block_has_room(self):
        result = self.run_with(True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('pc5 heap room passed', result.stdout)

    @unittest.skipUnless(os.environ.get('PC5_SHOW_OLD'), 'the pinned sizing is shown on request (PC5_SHOW_OLD=1)')
    def test_pinned_sizing_leaves_none(self):
        result = self.run_with(False)
        self.assertEqual(result.returncode, 1)
        self.assertIn('slack 0', result.stdout)


if __name__ == '__main__':
    unittest.main()
