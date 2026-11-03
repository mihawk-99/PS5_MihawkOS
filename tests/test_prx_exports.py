#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What the title looks up in ntdll (native/proton_wine.cpp's symbol<>), and what ntdll looks up in the server (dlsym in
the Wine patches), is in that module's export list (tools/build-wine-ps5.sh, gen_prx_descriptor.py): a name left out
builds, links and is NULL on the console. The shared synchronization device's calls were once (build 90aa28aa)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / 'tools/build-wine-ps5.sh').read_text()


def exports(descriptor):
    match = re.search(r'gen_prx_descriptor\.py" "\$prx/obj/' + descriptor + r'" \\\n((?:.*\\\n)*.*)\n', BUILD)
    assert match, descriptor
    return set(match.group(1).replace('\\', ' ').split())


class PrxExports(unittest.TestCase):
    def test_the_title_finds_what_it_looks_up_in_ntdll(self):
        looked = set(re.findall(r'symbol<[^>]*>\("([A-Za-z_0-9]+)"\)', (ROOT / 'native/proton_wine.cpp').read_text()))
        self.assertIn('pw_ntsync_process_gone', looked)
        self.assertEqual(sorted(looked - exports('ntdll_desc.c')), [])

    def test_the_title_finds_what_it_looks_up_in_the_server(self):
        looked = set(re.findall(r'module_symbol\(server, *"([A-Za-z_0-9]+)"\)',
                                (ROOT / 'native/proton_wine.cpp').read_text()))
        self.assertIn('pw_wineserver_set_ntsync_region', looked)
        self.assertEqual(sorted(looked - exports('wineserver_desc.c')), [])

    def test_ntdll_finds_what_it_looks_up_in_the_server(self):
        looked = set()
        for patch in (ROOT / 'wine/patches').glob('*.patch'):
            looked |= set(re.findall(r'^\+.*dlsym\( *module, *"(pw_wineserver_[A-Za-z_0-9]+)"',
                                     patch.read_text(errors='replace'), re.M))
        self.assertIn('pw_wineserver_set_ntsync_region', looked)
        self.assertEqual(sorted(looked - exports('wineserver_desc.c')), [])

    def test_wine_modules_find_what_they_look_up_in_ntdll(self):
        # pw_wine_pad_changes was left out from patch 2423 (2026-11-01) to the control overlay: the driver's dlsym gave
        # NULL and no window was ever told the controller came or went
        looked = set()
        for patch in (ROOT / 'wine/patches').glob('*.patch'):
            looked |= set(re.findall(r'^\+.*dlsym\( *RTLD_DEFAULT, *"((?:pw_|ps5_)[A-Za-z_0-9]+)"',
                                     patch.read_text(errors='replace'), re.M))
        self.assertIn('pw_wine_pad_changes', looked)
        self.assertEqual(sorted(looked - exports('ntdll_desc.c')), [])


if __name__ == '__main__':
    unittest.main()
