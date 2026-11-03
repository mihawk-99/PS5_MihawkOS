#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Visual C++ runtimes Wine patch 2455 records as installed: x64 and x86 carry the same records, each one says
Installed = 1, and every version it claims is the one the runtime's own builtin reports (read from the staged DLL's
version resource when a production build is present). A record must never claim more than the builtins provide."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH = next((ROOT / 'wine/patches').glob('2455-*.patch'))
STAGED = ROOT / '.deps/staged/runtime/ge11-7/lib/wine'


def records():
    lines = [line[1:] for line in PATCH.read_text().splitlines() if line.startswith('+HKLM')]
    found = {}
    for line in lines:
        key, name, kind, value = re.match(r'HKLM,([^,]+),"([^"]+)",([^,]+),(.*)', line).groups()
        found.setdefault(key, {})[name] = value.strip('"')
    return found


def file_version(dll):
    text = dll.read_bytes().decode('utf-16-le', 'ignore')
    match = re.search(r'FileVersion\x00+([0-9.]+)', text)
    return match.group(1) if match else None


class Records(unittest.TestCase):
    def test_both_architectures_and_installed(self):
        found = records()
        for key, values in found.items():
            self.assertEqual(values['Installed'], '0x00000001', key)
            twin = key[:-3] + ('x86' if key.endswith('x64') else 'x64')
            self.assertEqual(found[twin], values, key)
        self.assertEqual(len(found), 6)

    def test_versions_are_the_builtins(self):
        claims = {'12.0': ('msvcr120.dll', '12.0.40664'), '10.0': ('msvcr100.dll', '10.0.30319')}
        found = records()
        self.assertEqual(found[r'SOFTWARE\Microsoft\VisualStudio\12.0\VC\Runtimes\x64']['Version'], 'v12.0.40664.00')
        self.assertEqual(int(found[r'SOFTWARE\Microsoft\VisualStudio\12.0\VC\Runtimes\x64']['Bld'], 16), 40664)
        self.assertEqual(found[r'SOFTWARE\Microsoft\VisualStudio\10.0\VC\VCRedist\x64']['Version'], 'v10.0.30319')
        self.assertNotIn('Bld', found[r'SOFTWARE\Microsoft\VisualStudio\11.0\VC\Runtimes\x64'])   # msvcr110 has none
        if not STAGED.is_dir():
            self.skipTest('no production build staged')
        for arch in ('x86_64-windows', 'i386-windows'):
            for dll, version in claims.values():
                self.assertTrue(file_version(STAGED / arch / dll).startswith(version), (arch, dll))
            self.assertIsNone(file_version(STAGED / arch / 'msvcr110.dll'))


if __name__ == '__main__':
    unittest.main()
