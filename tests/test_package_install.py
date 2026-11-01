#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/package-install.py on a small title and runtime: the layout a user copies (the pieces inside the title folder),
its checksums, the one zip, and the refusals (a development leftover in the title folder, a piece that is not the one
the title pins)."""
import hashlib
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PackageInstall(unittest.TestCase):
    def test_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            title, runtime, out = work / 'PPSA99170', work / 'runtime', work / 'package'
            runtime.mkdir()
            piece = b'(not really zstd)' * 100
            digest = hashlib.sha256(piece).hexdigest()
            (runtime / f'{digest}.zst').write_bytes(piece)
            for relative, data in {'eboot.bin': b'\x7fELF', 'sce_sys/param.json': b'{}', 'runtime-stamp': b'x\n',
                                   'runtime-pack.txt': f'mihawkos-runtime-pack 1\nchunk {digest} {len(piece)} 9 1\n'.encode()}.items():
                (title / relative).parent.mkdir(parents=True, exist_ok=True)
                (title / relative).write_bytes(data)
            run = [sys.executable, str(ROOT / 'tools/package-install.py'), '--title', str(title), '--runtime', str(runtime),
                   '--out', str(out), '--version', '9.9.9-test']
            made = subprocess.run(run, capture_output=True, text=True)
            self.assertEqual(made.returncode, 0, made.stderr)
            self.assertEqual((out / 'PPSA99170/runtime-pieces' / f'{digest}.zst').read_bytes(), piece)
            self.assertFalse((out / 'usb').exists())
            self.assertTrue((out / 'PPSA99170/eboot.bin').is_file() and (out / 'README.txt').is_file())
            checked = subprocess.run(['sha256sum', '-c', '--quiet', 'SHA256SUMS'], cwd=out, capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stdout)
            self.assertEqual(len((out / 'SHA256SUMS').read_text().splitlines()), 6)
            archive = out / 'PS5_MihawkOS-v9.9.9-test.zip'
            with zipfile.ZipFile(archive) as made_zip:
                names = set(made_zip.namelist())
                self.assertEqual(names, {'README.txt', 'SHA256SUMS', 'PPSA99170/eboot.bin', 'PPSA99170/sce_sys/param.json',
                                         'PPSA99170/runtime-stamp', 'PPSA99170/runtime-pack.txt',
                                         f'PPSA99170/runtime-pieces/{digest}.zst'})
                self.assertEqual(made_zip.read(f'PPSA99170/runtime-pieces/{digest}.zst'), piece)
            checked = subprocess.run(['sha256sum', '-c', '--quiet', f'{archive.name}.sha256'], cwd=out, capture_output=True)
            self.assertEqual(checked.returncode, 0)
            (title / 'install-source.txt').write_text('http://fixture/\n')
            self.assertNotEqual(subprocess.run(run, capture_output=True).returncode, 0)
            (title / 'install-source.txt').unlink()
            (runtime / f'{digest}.zst').write_bytes(piece + b'!')
            self.assertNotEqual(subprocess.run(run, capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
