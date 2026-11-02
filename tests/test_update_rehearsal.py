#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The automatic update rehearsed on the PC (the owner, 2026-10-31: "update should be seamless"), against a LAN fixture
that answers as GitHub does: a release listing, a signed manifest and a release zip behind a redirect. The client is
tests/update_rehearsal.cpp, the title's own steps (src/pw_update_flow.h) over pw_https, then the switch and its confirm
(src/pw_update.h) on a title folder. A throwaway key signs the release (tools/sign-release.py sign --zip).

Checked: the newest signed release newer than the build is taken (a draft, an unsigned and an older one are not); the
zip's stored and deflated files land in the title; the old files are kept in previous/ until the confirm; a zip that
is not the signed one, a manifest signed with another key and a name outside the folder each leave the title
untouched; nothing newer offers nothing; the runtime's
pin goes in after its pieces, and a hand-copied folder's old pieces are dropped. (The runtime pin cannot be broken through sign-release.py, which takes it
from the zip; pw_update_install_zip checks it all the same.)"""
import hashlib
import json
import os
import random
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import test_pw_https  # noqa: E402  (its host build of Mbed TLS)

OUT = test_pw_https.OUT
TAG = 'v7.1.0-alpha-8'


def build():
    test_pw_https.build()   # the Mbed TLS objects, beside its driver
    work = OUT / 'pw_https'
    mbed = Path(subprocess.check_output(['bash', str(ROOT / 'tools/setup-mbedtls.sh')], text=True).strip())
    objects = [str(p) for p in sorted(work.glob('*.o'))]
    out = OUT / 'update_rehearsal'
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-O1', '-g', '-I', str(mbed / 'include'), '-I', str(ROOT), '-I', str(ROOT / 'external/tinygltf'),
             '-DMBEDTLS_USER_CONFIG_FILE="platform/tls/pw_mbedtls_config.h"', '-fsanitize=address,undefined']
    c_sources = ['src/pw_https.c', 'src/pw_sha256.c', 'src/pw_release.c', 'src/pw_update.c', 'src/pw_update_notice.c',
                 'src/pw_setup.c', 'src/pw_zip.c',
                 'src/pw_pack.c', 'external/zstd/zstddeclib.c', 'external/monocypher/monocypher.c',
                 'external/monocypher/monocypher-ed25519.c']
    c_objects = []
    for source in c_sources:
        target = out / (Path(source).stem + '.o')
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-c', *flags, '-w' if 'external' in source else '-Wall',
                        str(ROOT / source), '-o', str(target)], check=True)
        c_objects.append(str(target))
    driver = out / 'driver'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=gnu++17', *flags, '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-missing-field-initializers', '-o', str(driver),
                    str(ROOT / 'tests/update_rehearsal.cpp'), str(ROOT / 'src/pw_update_flow.cpp'), *c_objects, *objects],
                   check=True)
    return driver


def title_folder(path, version, eboot, runtime, extra):
    (path / 'sce_sys').mkdir(parents=True)
    (path / 'eboot.bin').write_bytes(eboot)
    (path / 'sce_sys/param.json').write_text(json.dumps({'contentVersion': version}))
    (path / 'runtime-pack.txt').write_bytes(runtime)
    for name, data in extra.items():
        (path / name).parent.mkdir(parents=True, exist_ok=True)
        (path / name).write_bytes(data)


class Fixture(BaseHTTPRequestHandler):
    files = {}       # path -> bytes
    listing = b''
    cut = False      # the zip's connection ends halfway, as a dropped network does

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path.startswith('/repos/'):
            body = self.listing
        elif self.path.startswith('/download/'):   # GitHub's release downloads redirect once, to their file host
            self.send_response(302)
            self.send_header('Location', '/objects/' + self.path.rsplit('/', 1)[1])
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        elif self.path.startswith('/objects/') and self.path[9:] in self.files:
            body = self.files[self.path[9:]]
        else:
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.cut and self.path.endswith('.zip'):
            self.wfile.write(body[:len(body) // 2])
            self.close_connection = True
            return
        self.wfile.write(body)


class UpdateRehearsal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.driver = build()
        cls.work = tempfile.TemporaryDirectory()
        cls.base = Path(cls.work.name)
        cls.key, cls.public = cls.base / 'keys/release.pem', cls.base / 'release-key.txt'
        cls.other_key, cls.other_public = cls.base / 'keys/other.pem', cls.base / 'other-key.txt'
        for key, public in ((cls.key, cls.public), (cls.other_key, cls.other_public)):
            subprocess.run([sys.executable, str(ROOT / 'tools/sign-release.py'), 'keygen', '--key', str(key), '--public',
                            str(public)], check=True, capture_output=True)
        cls.roots = cls.base / 'roots.pem'   # plain http here: pw_https needs roots to start, and never uses them
        cls.roots.write_text(Path('/etc/ssl/certs/ca-certificates.crt').read_text() if Path('/etc/ssl/certs/ca-certificates.crt').exists() else '')
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
        cls.port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        random.seed(7)
        cls.eboot = random.randbytes(3 << 20)    # deflated in the zip
        cls.piece = random.randbytes(2 << 20)    # stored, as the runtime pieces are

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.work.cleanup()

    def release(self, name, runtime=b'runtime B\n', key=None, extra=None, tamper=False, cut=False, manifest='mihawkos-update.txt'):
        """The new title as a release zip, its signed manifest, and GitHub's listing of it beside three it must skip."""
        folder = self.base / f'{name}-src/PPSA99170'
        title_folder(folder, '07.001.000', self.eboot, runtime, {'assets/x.txt': b'new\n', 'runtime-pieces/abc.zst': self.piece,
                                                                    'runtime-stamp': b'runtime B pin\n'})
        archive = self.base / f'{name}/PS5_MihawkOS-v7.1.0-alpha-8.zip'
        archive.parent.mkdir(parents=True)
        with zipfile.ZipFile(archive, 'w') as z:
            for path in sorted(folder.rglob('*')):
                if path.is_file():
                    method = zipfile.ZIP_STORED if 'runtime-pieces' in str(path) else zipfile.ZIP_DEFLATED
                    z.write(path, 'PPSA99170/' + str(path.relative_to(folder)), compress_type=method)
            z.writestr('SHA256SUMS', 'not read by the title\n')
            for entry, data in (extra or {}).items():
                z.writestr(entry, data)
        base = f'http://127.0.0.1:{self.port}/download/{TAG}/'
        subprocess.run([sys.executable, str(ROOT / 'tools/sign-release.py'), 'sign', '--zip', str(archive), '--base', base,
                        '--out', str(archive.parent), '--key', str(key or self.key)], check=True, capture_output=True)
        data = archive.read_bytes()
        if tamper:
            data = data[:1000] + bytes([data[1000] ^ 1]) + data[1001:]
        Fixture.cut = cut
        Fixture.files = {archive.name: data, manifest: (archive.parent / 'mihawkos-update.txt').read_bytes()}
        asset = lambda n, size=0: {'name': n, 'size': size, 'browser_download_url': base + n}
        Fixture.listing = json.dumps([
            {'tag_name': 'v8.0.0-alpha-9', 'draft': True, 'prerelease': True, 'assets': [asset('mihawkos-update.txt')]},
            {'tag_name': 'v7.2.0-alpha-9', 'draft': False, 'prerelease': True, 'assets': [asset('PS5_MihawkOS-v7.2.0.zip', 9)]},
            {'tag_name': TAG, 'draft': False, 'prerelease': True,
             'assets': [asset(archive.name, len(data)), asset(manifest)]},
            {'tag_name': 'v6.0.0-alpha-6', 'draft': False, 'prerelease': True, 'assets': [asset('mihawkos-update.txt')]},
        ]).encode()

    def run_update(self, name, public=None, build_tag='7.0.0-alpha-7', port=None):
        title = self.base / f'{name}-title'
        title_folder(title, '07.000.000', b'old eboot', b'runtime A\n', {'assets/x.txt': b'old\n', 'settings.txt': b'mine\n',
                                                                     'runtime-pieces/old.zst': b'old piece'})
        update = self.base / f'{name}-update'
        url = f'http://127.0.0.1:{port or self.port}/repos/mihawk-99/PS5_MihawkOS/releases?per_page=20'
        out = subprocess.run([str(self.driver), str(self.roots), url, (public or self.public).read_text().split()[0], build_tag,
                              '07.000.000', str(update), str(title)], capture_output=True, text=True, timeout=120)
        self.assertEqual(out.returncode, 0, out.stderr)
        return out.stdout, title, update

    def test_a_whole_update(self):
        self.release('whole')
        out, title, update = self.run_update('whole')
        self.assertIn(f'find=ok found=1 tag={TAG} version=07.001.000', out)
        self.assertIn('take=ok', out)
        # the main menu's line at each step, in the player's language (src/pw_update_notice.h)
        self.assertIn('notice en=Checking for a newer MihawkOS…\n', out)
        self.assertIn('notice en=MihawkOS 07.001.000 is available: it downloads in the background.\n', out)
        self.assertRegex(out, r'notice en=Downloading MihawkOS 07\.001\.000: [0-9]+ of [0-9]+ MB \(100%\)\n')
        self.assertIn('notice en=Checking the download of MihawkOS 07.001.000 against its signature…\n', out)
        self.assertIn('notice en=Preparing MihawkOS 07.001.000: 100%\n', out)
        self.assertIn('notice en=MihawkOS 07.001.000 is ready.\n', out)
        self.assertIn('notice fr=MihawkOS 07.001.000 est prêt.\n', out)
        steps = [line for line in out.splitlines() if line.startswith('notice en=')]
        self.assertTrue(steps[0].endswith('Checking for a newer MihawkOS…') and 'is available' in steps[1])
        self.assertTrue(steps[-1].endswith('is ready.'))   # in the order they happen
        self.assertIn('pending=1 version=07.001.000', out)
        self.assertIn('switch=1 failed=\n', out)
        self.assertIn('previous=1', out)
        self.assertIn('confirm=1', out)
        # every file of the switched title may be run: the system refuses an eboot.bin without its execute bit
        # (Alpha 7's update to 7.1 on the base, 2026-11-01: errno 13, "can't start the app")
        for name in ('eboot.bin', 'sce_sys/param.json', 'runtime-pieces/abc.zst'):
            self.assertEqual((title / name).stat().st_mode & 0o777, 0o777, name)
        self.assertEqual((title / 'eboot.bin').read_bytes(), self.eboot)
        self.assertEqual((title / 'runtime-pieces/abc.zst').read_bytes(), self.piece)
        self.assertEqual((title / 'assets/x.txt').read_bytes(), b'new\n')
        self.assertEqual(json.loads((title / 'sce_sys/param.json').read_text())['contentVersion'], '07.001.000')
        self.assertEqual((title / 'settings.txt').read_bytes(), b'mine\n')   # the folder's own state stays
        self.assertFalse((title / 'SHA256SUMS').exists())
        record = (title / 'title-pack.txt').read_text()                      # the record the next update reads
        self.assertIn('stamp runtime-stamp\nstamp eboot.bin\nstamp sce_sys/param.json\n', record)   # the pin goes in last but two
        self.assertFalse((title / 'runtime-pieces/old.zst').exists())        # a hand-copied folder's old pieces go
        self.assertEqual((title / 'runtime-stamp').read_bytes(), b'runtime B pin\n')
        self.assertFalse((update / 'previous').exists())
        self.assertFalse((update / 'download.zip').exists())

    def refused(self, name, words, **release):
        public = release.pop('public', None)
        self.release(name, **release)
        out, title, update = self.run_update(name, public=public)
        self.assertIn(words, out)
        self.assertEqual((title / 'eboot.bin').read_bytes(), b'old eboot')   # the running title untouched
        self.assertFalse((update / 'stage').exists())
        self.assertFalse((update / 'download.zip').exists() or (update / 'download.zip.part').exists())
        return out

    def test_a_damaged_download(self):
        self.refused('tampered', "is not the one its signed description names", tamper=True)
        self.refused('tampered2', 'notice en=An update was found, but it does not match MihawkOS’s signature, so it was not '
                     'used.\n', tamper=True)

    def test_a_cut_download(self):
        out = self.refused('cut', 'notice en=The download of MihawkOS 07.001.000 stopped: check the network. It starts again '
                           'the next time you open MihawkOS.\n', cut=True)
        self.assertNotIn('against its signature', out)   # a cut download never reaches its check

    def test_the_server_unreachable(self):
        closed = socket.socket()
        closed.bind(('127.0.0.1', 0))
        port = closed.getsockname()[1]
        closed.close()
        self.release('unreachable')
        out, title, _ = self.run_update('unreachable', port=port)
        self.assertIn('notice en=MihawkOS could not reach the update server. It tries again later.\n', out)
        self.assertEqual((title / 'eboot.bin').read_bytes(), b'old eboot')

    def test_another_key(self):
        self.refused('otherkey', 'not signed with MihawkOS', key=self.other_key)
        self.refused('otherkey2', 'notice en=An update was found, but it does not match MihawkOS’s signature', key=self.other_key)

    def test_a_name_outside_the_folder(self):
        self.refused('outside', 'outside its folder', extra={'PPSA99170/../evil.txt': b'x'})

    def test_alpha7s_manifest_name_is_not_read(self):
        # A release that carries only mihawkos-release.txt (the name Alpha 7 reads) is offered to nobody from 7.1 on
        self.release('oldname', manifest='mihawkos-release.txt')
        out, title, _ = self.run_update('oldname')
        self.assertIn('find=ok found=0', out)
        self.assertEqual((title / 'eboot.bin').read_bytes(), b'old eboot')

    def test_nothing_newer(self):
        self.release('newest')
        out, title, _ = self.run_update('newest', build_tag='7.1.0-alpha-8')
        self.assertIn('find=ok found=0', out)
        self.assertEqual((title / 'eboot.bin').read_bytes(), b'old eboot')


if __name__ == '__main__':
    unittest.main()
