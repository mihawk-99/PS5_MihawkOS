#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/sign-release.py against the title's own check (src/pw_release.c over Monocypher, built here for the host): a
key made, a small release signed with OpenSSL and accepted, and every changed byte, another key and an old version
refused. The private key never appears in what the tool prints."""
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/sign-release.py'


def build(work):
    driver = work / 'driver'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-o', str(driver),
                    str(ROOT / 'tests/pw_release_driver.c'), str(ROOT / 'src/pw_release.c'),
                    str(ROOT / 'external/monocypher/monocypher.c'), str(ROOT / 'external/monocypher/monocypher-ed25519.c')],
                   check=True)
    return driver


def tool(*args):
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)


class SignRelease(unittest.TestCase):
    def test_keygen_sign_and_the_title_check(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            driver = build(work)
            key, public = work / 'keys/release-ed25519.pem', work / 'release-key.txt'
            made = tool('keygen', '--key', key, '--public', public)
            self.assertEqual(made.returncode, 0, made.stderr)
            self.assertEqual(stat.S_IMODE(key.stat().st_mode), 0o600)
            hex_key = public.read_text().strip()
            self.assertRegex(hex_key, r'^[0-9a-f]{64}$')
            secret = key.read_text()
            self.assertNotIn(secret.splitlines()[1], made.stdout + made.stderr)
            self.assertNotEqual(tool('keygen', '--key', key, '--public', public).returncode, 0)   # never replaced

            title, runtime = work / 'title', work / 'runtime'
            for relative, data in {'eboot.bin': b'\x7fELF' + bytes(3000), 'assets/a.ktx': b'a' * 2000,
                                   'sce_sys/param.json': json.dumps({'contentVersion': '01.002.000'}).encode(),
                                   'runtime-pack.txt': b'mihawkos-runtime-pack 1\nzstd 1.5.7 -19\n'}.items():
                (title / relative).parent.mkdir(parents=True, exist_ok=True)
                (title / relative).write_bytes(data)
            runtime.mkdir()
            out = work / 'release'
            signed = tool('sign', '--base', 'https://example.invalid/releases/download/v1.2/', '--title', title,
                          '--runtime', runtime, '--out', out, '--key', key)
            self.assertEqual(signed.returncode, 0, signed.stderr)
            self.assertNotIn(secret.splitlines()[1], signed.stdout + signed.stderr)
            names = {p.name for p in out.iterdir()}
            self.assertTrue({'mihawkos-update.txt', 'title-pack.txt', 'runtime-pack.txt'} <= names)
            self.assertFalse(any(name.endswith('.pem') for name in names))
            self.assertTrue(any(name.endswith('.zst') for name in names))
            manifest = out / 'mihawkos-update.txt'

            def title_check(path, public_hex, running='01.001.000'):
                return subprocess.run([str(driver), str(path), public_hex, running], capture_output=True, text=True,
                                      check=True).stdout.split()

            self.assertEqual(title_check(manifest, hex_key),
                             ['ok', '01.002.000', 'https://example.invalid/releases/download/v1.2/', 'title-pack.txt',
                              'runtime-pack.txt', '1'])
            self.assertEqual(title_check(manifest, hex_key, '01.002.000')[-1], '0')   # not newer than the running title
            self.assertEqual(tool('verify', manifest, '--public', public).returncode, 0)

            other = work / 'other.txt'
            self.assertEqual(tool('keygen', '--key', work / 'keys/other.pem', '--public', other).returncode, 0)
            self.assertEqual(title_check(manifest, other.read_text().strip()), ['status', '-2'])
            self.assertNotEqual(tool('verify', manifest, '--public', other).returncode, 0)
            text = manifest.read_bytes()
            for at in (0, 25, len(text) // 2, len(text) - 140):
                changed = work / f'changed-{at}.txt'
                changed.write_bytes(text[:at] + bytes([text[at] ^ 1]) + text[at + 1:])
                self.assertEqual(title_check(changed, hex_key)[0], 'status')

            self.assertNotEqual(tool('sign', '--base', 'https://example.invalid/latest', '--title', title, '--runtime',
                                     runtime, '--out', out, '--key', key).returncode, 0)   # not a folder URL


if __name__ == '__main__':
    unittest.main()
