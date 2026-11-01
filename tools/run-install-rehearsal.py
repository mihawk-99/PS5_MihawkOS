#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""docs/SELF_INSTALL.md phase 5's proof: install the runtime from storage on the console, with the owner there.

  tools/run-install-rehearsal.py --name RUN --owner-present          rename aside, put the pieces in place, install
  tools/run-install-rehearsal.py --name RUN --owner-present --route http   the same, downloading from this PC (phase 6)
  tools/run-install-rehearsal.py --name RUN --restore ASIDE          put a renamed-aside runtime back

The deployed title must be this checkout's production build (its runtime-pack.txt is compared). Nothing is deleted:
  1. /data/ps5proton/runtime/ge11-7 and run/proton-payload-child.elf are renamed aside (<name>.aside-<time>);
  2. dist/runtime's pieces go to the title folder's runtime-pieces/ over FTP, where a release's are; or, with
     --route http, this PC serves them (the LAN fixture), the title folder's install-source.txt names it for this run
     only, and a pieces folder already in the title folder is renamed aside so the title downloads;
  3. the title is started; the owner opens Settings, Install runtime, and presses CROSS;
  4. klog's "install: piece" lines give the write curve, "install: done" the end;
  5. every file of the manifest is read back over FTP and hashed: N of N must match.
Then run the gate. --restore renames the installed runtime aside and the given one back. Writes evidence/<RUN>.json.
"""
import argparse
import ftplib
import functools
import http.server
import threading
import hashlib
import io
import json
import re
import socket
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'PS5_Vulkan/tools'))
import ps5_console  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools'))
import console_guard  # noqa: E402
from console_log import LineLog, redact  # noqa: E402

DATA = '/data/ps5proton'
TITLE = '/data/homebrew/PPSA99170'
MOVED = ('runtime/ge11-7', 'run/proton-payload-child.elf')
PIECE = re.compile(r'install: piece (\d+) status=(-?\d+) written=(\d+) of (\d+) at ([\d.]+) s')


def manifest_files(text):
    return [line.split(' ', 4)[1:] for line in text.splitlines() if line.startswith('file ')]


def connect(settings):
    ftp = ftplib.FTP()
    ftp.connect(settings['host'], settings['ftp_port'], timeout=60)
    ftp.login(settings['ftp_user'], settings['ftp_password'])
    return ftp


def exists(ftp, path):
    try:
        ftp.size(path)
        return True
    except ftplib.error_perm:
        try:
            ftp.cwd(path)
            ftp.cwd('/')
            return True
        except ftplib.error_perm:
            return False


def idle(settings):
    return ' count=0 ' in ps5_console.ps5vkctl_command(settings, 'procs', timeout=15) + ' '


def restore(args, settings, record):
    with connect(settings) as ftp:
        stamp = time.strftime('%Y%m%d-%H%M%S')
        for moved in MOVED:
            current, aside = f'{DATA}/{moved}', f'{DATA}/{moved}.{args.restore}'
            if not exists(ftp, aside):
                raise SystemExit(f'{aside} is not there; nothing renamed')
            if exists(ftp, current):
                ftp.rename(current, f'{current}.installed-{stamp}')
            ftp.rename(aside, current)
        record.update(restored=args.restore, installed_aside=f'installed-{stamp}', passed=True)


def rehearse(args, settings, record):
    text = (ROOT / 'dist/PPSA99170/runtime-pack.txt').read_text()
    files = manifest_files(text)
    chunks = [line.split()[1] + '.zst' for line in text.splitlines() if line.startswith('chunk ')]
    with connect(settings) as ftp:
        deployed = io.BytesIO()
        ftp.retrbinary(f'RETR {TITLE}/runtime-pack.txt', deployed.write)
        if deployed.getvalue().decode() != text:
            raise SystemExit('the deployed title is not this build (its runtime-pack.txt differs); deploy it first')
        aside = 'aside-' + time.strftime('%Y%m%d-%H%M%S')
        record['aside'] = aside
        for moved in MOVED:   # renamed, never deleted: --restore puts them back
            if exists(ftp, f'{DATA}/{moved}'):
                ftp.rename(f'{DATA}/{moved}', f'{DATA}/{moved}.{aside}')
        if args.route == 'http':
            if exists(ftp, f'{TITLE}/runtime-pieces'):
                ftp.rename(f'{TITLE}/runtime-pieces', f'{TITLE}/runtime-pieces.{aside}')
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:   # this PC as the console reaches it
                route.connect((settings['host'], 9))
                here = route.getsockname()[0]
            server = http.server.ThreadingHTTPServer(('0.0.0.0', args.port), functools.partial(
                http.server.SimpleHTTPRequestHandler, directory=str(ROOT / 'dist/runtime')))
            threading.Thread(target=server.serve_forever, daemon=True).start()
            ftp.storbinary(f'STOR {TITLE}/install-source.txt', io.BytesIO(f'http://{here}:{args.port}/\n'.encode()))
            record['route'] = 'http (LAN fixture)'
            chunks = []
        else:
            try:
                ftp.mkd(f'{TITLE}/runtime-pieces')
            except ftplib.error_perm:
                pass
            record['route'] = 'folder'
        started = time.monotonic()
        for name in chunks:
            path = ROOT / 'dist/runtime' / name
            with path.open('rb') as data:
                ftp.storbinary(f'STOR {TITLE}/runtime-pieces/{name}', data, blocksize=1 << 20)
            if ftp.size(f'{TITLE}/runtime-pieces/{name}') != path.stat().st_size:
                raise SystemExit(f'{name}: the upload has another size')
        record['upload_seconds'] = round(time.monotonic() - started, 1)
    curve, lines = [], []
    capture = ROOT / 'klog' / (args.name + '.log')
    with socket.create_connection((settings['host'], settings['klog_port']), timeout=10) as klog, capture.open('w') as output:
        klog.settimeout(0.5)
        log = LineLog(output, lines.append)
        console_guard.after_launch()
        ps5_console.ps5vkctl_command(settings, 'launch PPSA99170', timeout=120)
        print('MihawkOS started: open Settings, Install runtime, and press CROSS', flush=True)
        deadline = time.monotonic() + args.wait
        end = None
        while time.monotonic() < deadline and not end:
            try:
                chunk = klog.recv(65536)
                if not chunk:
                    break
                log.write(chunk)
            except socket.timeout:
                pass
            end = next((line for line in lines if re.search(r'install: (done|failed)', line)), None)
        log.finish()
    for line in lines:
        match = PIECE.search(line)
        if match:
            curve.append({'piece': int(match[1]), 'status': int(match[2]), 'written': int(match[3]),
                          'to_write': int(match[4]), 'seconds': float(match[5])})
    record.update(curve=curve, end=end, capture=str(capture.relative_to(ROOT)))
    if args.route == 'http':
        server.shutdown()
        record['tries'] = [redact(line) for line in lines if ' try ' in line and 'install: piece' in line]
    if not end or 'install: done' not in end:
        return
    passed = 0
    with connect(settings) as ftp:
        # The FTP server decrypts a SELF (every .prx) as it sends it unless SELF is off, as tools/deploy-wine.py reads
        # (exp3-smoke-pro-2026-10-31-install: 22 .prx files read back larger, all equal with SELF off).
        for _ in range(2):
            if 'disabled' in ftp.sendcmd('SELF').lower():
                break
        for digest, size, _, relative in files:
            check = hashlib.sha256()
            try:
                ftp.retrbinary(f'RETR {DATA}/{relative}', check.update, blocksize=1 << 20)
            except ftplib.error_perm:
                continue
            passed += check.hexdigest() == digest
        ftp.sendcmd('SELF')   # back to decrypting, as the server was
    record.update(hashes_passed=passed, hashes_total=len(files), passed=passed == len(files))
    if curve:
        seconds = curve[-1]['seconds']
        record['mib_per_second'] = round(curve[-1]['written'] / 1048576 / seconds, 1) if seconds else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--name', required=True)
    parser.add_argument('--owner-present', action='store_true', help='the owner is at the console and approved this run')
    parser.add_argument('--restore', metavar='ASIDE', help='put back the runtime renamed aside as <name>.ASIDE')
    parser.add_argument('--wait', type=int, default=1800, help='seconds to wait for the install to end')
    parser.add_argument('--route', choices=('folder', 'http'), default='folder')
    parser.add_argument('--port', type=int, default=8766, help='--route http: the port this PC serves on')
    args = parser.parse_args()
    if not args.restore and not args.owner_present:
        parser.error('the rehearsal needs the owner at the console: --owner-present')
    evidence = ROOT / 'evidence' / (args.name + '.json')
    if evidence.exists():
        raise SystemExit('evidence already exists; use a new run name')
    settings = ps5_console.load_settings()
    if not idle(settings):
        raise SystemExit('the console is running an application; nothing renamed')
    console_guard.before_launch()
    record = {'passed': False, 'started': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
    try:
        (restore if args.restore else rehearse)(args, settings, record)
    except BaseException as error:
        record['error'] = redact(error)
        raise
    finally:
        if record.get('route', '').startswith('http'):
            try:   # never leave a title pointing at this PC
                with connect(settings) as ftp:
                    ftp.delete(f'{TITLE}/install-source.txt')
            except ftplib.error_perm:
                pass
        evidence.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({k: v for k, v in record.items() if k != 'curve'}, indent=2))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
