#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Steam guide sees every program start, the title's own included (native/proton_wine.cpp). A helper start reaches
guide_process through title_application_lookup; a program the title takes (start_title_client: Steam's browser since
8.1) never did, so the sign-in session never saw the browser and stayed on "Steam is updating itself" until its 600 s
limit, on every try (issue 62). Both start paths must call guide_process."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def body(source, signature):
    start = source.index(signature)
    depth, at = 0, source.index('{', start)
    for i in range(at, len(source)):
        depth += {'{': 1, '}': -1}.get(source[i], 0)
        if depth == 0:
            return source[at:i + 1]
    raise AssertionError(f'{signature}: no end')


class GuideSeesEveryStart(unittest.TestCase):
    def test_both_start_paths_feed_the_guide(self):
        source = (ROOT / 'native/proton_wine.cpp').read_text()
        for signature in ('int title_application_lookup(', 'int start_title_client('):
            self.assertRegex(body(source, signature), r'\bguide_process\(', signature)

    def test_the_guide_watches_for_the_browser(self):
        source = (ROOT / 'native/proton_wine.cpp').read_text()
        self.assertIn('"steamwebhelper"', body(source, 'static void guide_process('))
        self.assertTrue(re.search(r'input\.browser\s*=\s*guide_browser\.load', source))


if __name__ == '__main__':
    unittest.main()
