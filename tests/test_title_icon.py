#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The title's icon: ps5/sce_sys/icon0.png is ps5/tools/icon0.svg (the Aero user tile, 2026-10-31) rendered at 512 x 512
as 8-bit RGB, not a stale picture of an older icon (checked against librsvg when it is installed)."""
import io
import shutil
import subprocess
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class TitleIcon(unittest.TestCase):
    def test_png_is_512_rgb(self):
        image = Image.open(ROOT / 'ps5/sce_sys/icon0.png')
        self.assertEqual((image.size, image.mode), ((512, 512), 'RGB'))

    @unittest.skipUnless(shutil.which('rsvg-convert'), 'librsvg (rsvg-convert) is not installed')
    def test_png_is_the_svg(self):
        png = subprocess.run(['rsvg-convert', '-w', '512', '-h', '512', str(ROOT / 'ps5/tools/icon0.svg')],
                             capture_output=True, check=True).stdout
        want = np.asarray(Image.open(io.BytesIO(png)).convert('RGB')).astype(int)
        have = np.asarray(Image.open(ROOT / 'ps5/sce_sys/icon0.png').convert('RGB')).astype(int)
        error = np.abs(want - have)
        self.assertLessEqual(error.mean(), 0.5)  # another librsvg may antialias an edge a level or two apart
        self.assertLessEqual(int(error.max()), 16)


if __name__ == '__main__':
    unittest.main()
