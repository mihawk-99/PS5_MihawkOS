#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""DXVK patch 0007 (a texture shared between a Direct3D 9Ex and a Direct3D 11 device of one process, without Win32 external
memory). What a host can show: the series of platform/dxvk/patches applies in order to the pinned DXVK source, and the table
the two devices share their textures through (dxvk_local_share.h, a header the patch adds) behaves: handles in a range of
their own, found by the other device, removed with the texture, a full table refused, threads. What it cannot show is the
two APIs using it on a GPU: that is tests/d3d9ex-d3d11-share-probe.c, on the console or (with d3d9.shareD3D11DeviceAlways)
under Wine on a PC. The cached archive of the pinned source is .deps/dxvk-601930949.tar.gz; without it these tests skip."""
import json
import re
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = ROOT / 'platform/dxvk/pins.json'
PATCHES = sorted((ROOT / 'platform/dxvk/patches').glob('*.patch'))
PATCH = next(p for p in PATCHES if p.name.startswith('0007-'))


def archive():
    main = json.loads(PINS.read_text())[0]
    path = ROOT / '.deps' / main['archive']
    if not path.is_file():
        raise unittest.SkipTest(f'{path.name} is not cached in .deps (tools/fetch-all.sh)')
    return path


def added_file(patch_text, name):
    """The text of a file a patch adds."""
    start = patch_text.index(f'diff --git a/{name} b/{name}')
    end = patch_text.find('\ndiff --git ', start + 1)
    body = patch_text[start:end if end >= 0 else len(patch_text)]
    lines = body.split('\n')
    first = next(i for i, line in enumerate(lines) if line.startswith('@@'))
    return '\n'.join(line[1:] for line in lines[first + 1:] if line.startswith('+') and not line.startswith('+++')) + '\n'


class LocalShare(unittest.TestCase):
    def test_the_series_applies_in_order_to_the_pinned_source(self):
        with tempfile.TemporaryDirectory(prefix='dxvk-series-') as tmp:
            with tarfile.open(archive()) as source:
                for member in source.getmembers():
                    parts = Path(member.name).parts
                    if len(parts) > 1:
                        member.name = str(Path(*parts[1:]))
                        source.extract(member, tmp, filter='data')
            for patch in PATCHES:
                with patch.open('rb') as stream:
                    done = subprocess.run(['patch', '--batch', '--forward', '-p1'], cwd=tmp, stdin=stream,
                                          capture_output=True, text=True)
                self.assertEqual(done.returncode, 0, f'{patch.name}: {done.stdout}{done.stderr}')
            for path in ('src/d3d11/d3d11_local_share.cpp', 'src/d3d9/d3d9_local_share.cpp', 'src/dxvk/dxvk_local_share.h'):
                self.assertTrue((Path(tmp) / path).is_file(), path)
            conf = (Path(tmp) / 'dxvk.conf').read_text()
            self.assertIn('d3d9.shareD3D11Device = True', conf)
            options = (Path(tmp) / 'src/d3d9/d3d9_options.cpp').read_text()
            self.assertRegex(options, r'"d3d9\.shareD3D11Device",\s+true')
            self.assertRegex(options, r'"d3d9\.shareD3D11DeviceAlways",\s+false')
            exports = (Path(tmp) / 'src/d3d11/d3d11.def').read_text()
            self.assertIn('DXVKLocalShareGetDevice', exports)
            # A clear DXVK defers is not in the commands a device submits; for a listed texture the other device reads next,
            # so the context puts it there, and the image says it is listed (the first Pro run read zeros without this).
            context = (Path(tmp) / 'src/dxvk/dxvk_context.cpp').read_text()
            start = context.index('void DxvkContext::prepareSharedImages()')
            prepare = context[start:context.index('\n  }\n', start)]
            self.assertIn('m_device->localShares().count()', prepare)
            self.assertRegex(prepare, r'isLocalShared\(\)\)\s*\{\s*flushClears\(false\);')
            self.assertIn('VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT', prepare)
            self.assertIn('m_image->setLocalShared();', (Path(tmp) / 'src/d3d9/d3d9_common_texture.cpp').read_text())

    def test_the_shared_table(self):
        with tempfile.TemporaryDirectory(prefix='dxvk-share-') as tmp:
            work = Path(tmp)
            (work / 'dxvk').mkdir()
            (work / 'dxvk/dxvk_local_share.h').write_text(added_file(PATCH.read_text(), 'src/dxvk/dxvk_local_share.h'))
            (work / 'util/rc').mkdir(parents=True)
            wanted = {'src/util/util_likely.h': 'util/util_likely.h', 'src/util/rc/util_rc.h': 'util/rc/util_rc.h',
                      'src/util/rc/util_rc_ptr.h': 'util/rc/util_rc_ptr.h'}
            with tarfile.open(archive()) as source:
                for member in source.getmembers():
                    rest = str(Path(*Path(member.name).parts[1:]))
                    if rest in wanted:
                        (work / wanted[rest]).write_bytes(source.extractfile(member).read())
            built = subprocess.run(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', f'-I{work}', '-pthread',
                                    '-o', str(work / 'h'), str(ROOT / 'tests/dxvk_local_share_harness.cpp')],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-3000:])
            ran = subprocess.run([str(work / 'h')], capture_output=True, text=True, timeout=120)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('dxvk local share passed', ran.stdout)

    def test_the_harness_includes_the_header_the_way_the_sources_do(self):
        self.assertIn('#include "dxvk/dxvk_local_share.h"', (ROOT / 'tests/dxvk_local_share_harness.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
