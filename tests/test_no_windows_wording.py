#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""No user-facing string says "Windows": the owner's rule of 2026-11-06 ("No Windows wording. MihawkOS instead").

Every C string literal in the sources a person's screen is drawn from (the launcher's and the guided setup's words in all
ten languages, the shell's own programs, the WebUI's text) must not hold the word "Windows". A literal that is a path,
a registry key or another name the person never reads (C:\\Windows, Software\\Microsoft\\Windows NT) is skipped by
looking like one. Comments are not strings. Third-party product names are unaffected.

  python3 tests/test_no_windows_wording.py
"""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/pw_setup.c', 'src/pw_appearance.c', 'src/pw_settings.c', 'src/pw_browser.c',
           'ps5/src/proton_steam.cpp', 'ps5/src/proton_launch.cpp', 'examples/ps5proton/ps5proton.cpp',
           'examples/ps5proton/home_grid.hpp', 'examples/ps5proton/setup_tour.hpp', 'wine/ps5/pw_shell_*.c',
           'wine/ps5/pw_shell_*.h', 'webui/index.html', 'webui/app.js', 'webui/i18n.js', 'src/pw_webui.cpp']
LITERAL = re.compile(r'L?"((?:[^"\\\n]|\\.)*)"')
WORD = re.compile(r'Windows')
# What the person never reads: a path or a registry key, a file name with the word in it.
TECHNICAL = re.compile(r'(\\\\|/)(Windows|windows)|(Windows|windows)(\\\\|/)|Microsoft|i386-windows|x86_64-windows|'
                       r'windows\.|\.windows|-windows|_windows|windows_|C:\\\\|%SystemRoot%')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    return re.sub(r'(?<![:"\'])//[^\n]*', '', text)


def literals(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    if path.suffix not in ('.html',):
        text = strip_comments(text)
    for number, line in enumerate(text.splitlines(), 1):
        for match in LITERAL.finditer(line):
            yield number, match.group(1)


class NoWindowsWording(unittest.TestCase):
    def test_every_user_facing_string(self):
        offenders = []
        files = [p for pattern in SOURCES for p in sorted(ROOT.glob(pattern))]
        self.assertTrue(files)
        for path in files:
            for number, value in literals(path):
                if WORD.search(value) and not TECHNICAL.search(value):
                    offenders.append(f'{path.relative_to(ROOT)}:{number}: {value[:90]}')
        self.assertEqual([], offenders, 'a user-facing string says "Windows"; MihawkOS (or PC) instead:\n' + '\n'.join(offenders))

    def test_the_rule_catches_a_string(self):
        sample = 'x = "Your Windows desktop"; y = L"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion"; // Windows'
        path = ROOT / '.deps' / 'no-windows-sample.c'
        path.parent.mkdir(exist_ok=True)
        path.write_text(sample)
        found = [v for _, v in literals(path) if WORD.search(v) and not TECHNICAL.search(v)]
        path.unlink()
        self.assertEqual(['Your Windows desktop'], found)


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
