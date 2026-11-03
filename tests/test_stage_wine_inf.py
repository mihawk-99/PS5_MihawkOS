#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""wine.inf names Wine's builtin DLLs (tools/stage-wine.py builtins_digest): a changed builtin changes wine.inf, so every
prefix updates once and gets the new copy (Wine loads a prefix's own copy of a builtin; a8-rel2-base-2026-11-02 ran
an old kernelbase.dll from the probe prefix). An identical rebuild changes nothing, and only the Windows modules count."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('stage_wine', ROOT / 'tools/stage-wine.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class BuiltinsDigest(unittest.TestCase):
    def test_builtins_change_wine_inf(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder)
            for arch in ('x86_64-windows', 'i386-windows'):
                (runtime / 'lib/wine' / arch).mkdir(parents=True)
                (runtime / 'lib/wine' / arch / 'kernelbase.dll').write_bytes(b'MZ old ' + arch.encode())
            (runtime / 'lib/wine/x86_64-unix').mkdir()
            (runtime / 'lib/wine/x86_64-unix/ntdll.so').write_bytes(b'ELF')
            first = tool.builtins_digest(runtime)
            self.assertEqual(first, tool.builtins_digest(runtime))                 # an identical rebuild: no update
            (runtime / 'lib/wine/x86_64-unix/ntdll.so').write_bytes(b'ELF 2')
            self.assertEqual(first, tool.builtins_digest(runtime))                 # the Unix side is not copied
            (runtime / 'lib/wine/x86_64-windows/kernelbase.dll').write_bytes(b'MZ new')
            changed = tool.builtins_digest(runtime)
            self.assertNotEqual(first, changed)                                   # a changed builtin: one update
            (runtime / 'lib/wine/i386-windows/user32.dll').write_bytes(b'MZ')
            self.assertNotEqual(changed, tool.builtins_digest(runtime))           # a new one too
            self.assertRegex(changed, r'^[0-9a-f]{64}$')


if __name__ == '__main__':
    unittest.main()
