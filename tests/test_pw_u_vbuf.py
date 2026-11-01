#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""platform/graphics/mesa/0001: the Mesa patch releases a draw's retired uploader buffers inside the multi-draw loop,
and the ownership model (tests/pw_u_vbuf_model.c) shows what that saves.

shortcut: Mesa itself is not built on the host, so the patch is checked as text (the release sits in the loop, after the
draw and before the next) and the model is a stand-in for the uploader; the console run is the real test."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'platform/graphics/mesa/0001-u_vbuf-releases-the-retired-uploads-of-each-draw.patch'


class UVbuf(unittest.TestCase):
    def test_release_is_inside_the_draw_loop(self):
        added = [l[1:] for l in PATCH.read_text().split('\n') if l.startswith('+') and not l.startswith('+++')]
        text = '\n'.join(added)
        self.assertIn('pipe_resource_release(pipe, releasebuf);', text)
        self.assertIn('releasebuf = NULL;', text)
        self.assertRegex(text, r'for \(unsigned i = 0; i < release_count; i\+\+\)\n\s+pipe_resource_release\(pipe, releasebufs\[i\]\);')
        self.assertIn('release_count = 0;', text)
        # the hunk's context: the release follows the draw's draw_id step and precedes the end of the loop
        hunk = PATCH.read_text()
        self.assertRegex(hunk, r' +if \(info->increment_draw_id\)\n +drawid_offset\+\+;\n\+\n\+ +/\* The driver holds')
        self.assertRegex(hunk, r'\+ +release_count = 0;\n    ?\}\n \n    ?if \(mgr->using_translate\)|\+ +release_count = 0;\n +\}\n +\n +if \(mgr->using_translate\)')

    def test_model_leaks_without_it_and_not_with_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'model'
            build = subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                                    str(ROOT / 'tests/pw_u_vbuf_model.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('pw u_vbuf model passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
