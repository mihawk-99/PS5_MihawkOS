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

    def test_stamp_inf_names_the_modules_as_they_ship(self):
        """stamp_inf runs once this build's PE modules replaced the PC build's and were stripped: its line and its time
        follow them (the 8 -> 8.1 update kept every prefix's Alpha 8 copies: the line was taken before, from the PC
        build's modules, and did not change; a81upd-base-2026-11-04)."""
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder)
            (runtime / 'share/wine').mkdir(parents=True)
            (runtime / 'lib/wine/x86_64-windows').mkdir(parents=True)
            dll = runtime / 'lib/wine/x86_64-windows/kernelbase.dll'
            dll.write_bytes(b'MZ the PC build')
            inf = runtime / 'share/wine/wine.inf'
            base = b'[Version]\r\nSignature="$CHICAGO$"\r\n'
            dll.write_bytes(b'MZ this PS5 build, stripped')
            tool.stamp_inf(runtime, base)
            text, time = inf.read_bytes(), inf.stat().st_mtime
            self.assertTrue(text.startswith(base) and text.endswith(b'\r\n'))
            self.assertEqual(text.splitlines()[-1].split()[-1].decode(), tool.builtins_digest(runtime))
            tool.stamp_inf(runtime, base)
            self.assertEqual((text, time), (inf.read_bytes(), inf.stat().st_mtime))   # an identical rebuild: no update
            dll.write_bytes(b'MZ the next PS5 build')
            tool.stamp_inf(runtime, base)
            self.assertNotEqual(text, inf.read_bytes())                               # a changed module: one update
            self.assertNotEqual(time, inf.stat().st_mtime)


    def test_the_template_leaves_out_what_wineboot_deletes(self):
        """wine.inf's "11,,name,-" modules are gone after a prefix's first update; the template must not hold them, or
        the deep read-back calls them damaged (console-setup-2026-11-07-16: 12 files)."""
        text = (
            '[FakeDllsWow64]\r\n11,,iexplore.exe\r\n11,,mscorwks.dll,-\r\n11,,*\r\n11,,comctl32_v6.dll,-\r\n'
            '[FakeDlls]\r\n11,gecko\\plugin,npmshtml.dll\r\n11,,fusion.dll,-\r\n12,,*.sys,-\r\n[Other]\r\n11,,x.dll,-\r\n')
        skipped = tool.inf_skipped(text)
        self.assertIn('mscorwks.dll', skipped['syswow64'])
        self.assertIn('comctl32_v6.dll', skipped['syswow64'])
        self.assertIn('fusion.dll', skipped['system32'])
        for names in skipped.values():
            self.assertFalse({'iexplore.exe', 'npmshtml.dll', 'x.dll', '*.sys', '*'} & names)

if __name__ == '__main__':
    unittest.main()
