#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Qualify production WinHTTP/root trust against two owned LAN TLS servers.

The gate needs no public internet. A unique root is temporarily installed only
inside the probe prefix, and the unrelated server's root is never trusted. The
PE test checks both chains with crypt32, then removes its exact root context; its
keys are made for this run and discarded, so a root left behind trusts nothing.
Games and production prefixes are untouched. Until 2026-10-16 it ran in a fresh
proton-tls-test prefix: 122 of its 177 s made that prefix and 47 s deleted it
(evidence/gate-timing-review-2026-10-16-01.json); a leftover entry is still removed.
Run tls-probe.exe directly without fixture arguments for public-network diagnosis.
--winhttp runs winhttp-probe.exe instead (Xbox Live's HTTP client's calls, the certificate not checked) against the
trusted endpoint; --env NAME=VALUE passes to the launch (WINE_PS5_TITLE_PICK=winhttp-probe.exe: in the title).
A WINEDEBUG in --env replaces the default channels: keep warn+debugstr in it, or the probe's verdict never reaches
the log (winhttp64-title-base-2026-11-03-01 passed on its stdout and was recorded as failed).
"""
import argparse
from datetime import datetime, timezone
import ftplib
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import ipaddress
import json
import os
from pathlib import Path
import re
import runpy
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

import cryptography
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

ROOT = Path(__file__).resolve().parents[1]
ENTRY = 'proton-tls-test'
GAME = '/data/ps5proton/games/' + ENTRY
PREFIX = '/data/ps5proton/prefixes/' + ENTRY
OWNER = b'tools/run-tls-fixture.py\n'
MARKER = '.ps5-proton-test'
# The trusted root, beside tls-probe.exe in the probe prefix (C:\), for one run.
ROOT_FILE = 'tls-fixture-root.der'
ROOT_PATH = '/data/ps5proton/prefixes/probe/drive_c/' + ROOT_FILE


def certificates(directory, name, host):
    """New keys each run, bounded validity independent of a stale console clock."""
    start = datetime(2000, 1, 1, tzinfo=timezone.utc)
    end = datetime(2040, 1, 1, tzinfo=timezone.utc)
    root_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    issuer = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'PS5 Proton test ' + name)])
    root = (x509.CertificateBuilder().subject_name(issuer).issuer_name(issuer)
            .public_key(root_key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(start).not_valid_after(end)
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), critical=False)
            .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, None, None), critical=True)
            .sign(root_key, hashes.SHA256()))
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)])
    certificate = (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer)
                   .public_key(key.public_key()).serial_number(x509.random_serial_number())
                   .not_valid_before(start).not_valid_after(end)
                   .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                   .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), critical=False)
                   .add_extension(x509.KeyUsage(True, False, True, False, False, False, False, None, None), critical=True)
                   # Native TLS uses the IP identity; pinned Wine's SSL policy
                   # checks DNS entries even for numeric hosts. Bind both to
                   # the same endpoint, keeping hostname verification enabled.
                   .add_extension(x509.SubjectAlternativeName([
                       x509.IPAddress(ipaddress.ip_address(host)), x509.DNSName(host)]), critical=False)
                   .add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
                   .sign(root_key, hashes.SHA256()))
    certificate_file = directory / (name + '.pem')
    key_file = directory / (name + '.key')
    certificate_file.write_bytes(certificate.public_bytes(serialization.Encoding.PEM) + root.public_bytes(serialization.Encoding.PEM))
    key_file.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    key_file.chmod(0o600)
    return root.public_bytes(serialization.Encoding.DER), certificate_file, key_file


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.answers += 1
        data = b'PS5 Proton TLS fixture\n'
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_):
        pass


class Server(ThreadingHTTPServer):
    daemon_threads = True
    answers = 0

    def handle_error(self, *_):
        pass  # Refused certificate handshakes must not print private addresses.


def server(host, certificate, key, port=0):
    result = Server((host, port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certificate, key)
    result.socket = context.wrap_socket(result.socket, server_side=True)
    thread = threading.Thread(target=result.serve_forever, kwargs={'poll_interval': 0.1}, daemon=True)
    thread.start()
    return result, thread


def remove_owned(ftp, listing, remove_tree):
    game, prefix = listing(ftp, GAME), listing(ftp, PREFIX)
    if game is not None:
        marker = io.BytesIO()
        ftp.retrbinary('RETR ' + GAME + '/' + MARKER, marker.write)
        if marker.getvalue() != OWNER:
            raise RuntimeError('TLS fixture entry has another owner: nothing removed')
    elif prefix is not None:
        raise RuntimeError('TLS fixture prefix has no ownership marker: nothing removed')
    for path, contents in ((PREFIX, prefix), (GAME, game)):
        if contents is not None:
            remove_tree(ftp, path, {'files': 0, 'folders': 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--trusted-port', type=int, default=int(os.environ.get('PROTON_TLS_TRUSTED_PORT', '0')))
    parser.add_argument('--untrusted-port', type=int, default=int(os.environ.get('PROTON_TLS_UNTRUSTED_PORT', '0')))
    parser.add_argument('--winhttp', action='store_true', help='run winhttp-probe.exe against the trusted endpoint')
    parser.add_argument('--env', action='append', default=[], help='NAME=VALUE for the launch')
    args = parser.parse_args()
    if not all(0 <= port <= 65535 for port in (args.trusted_port, args.untrusted_port)):
        parser.error('fixture ports must be 0 (automatic) or 1..65535')
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.name):
        parser.error('use a plain run name')
    evidence = ROOT / 'evidence' / (args.name + '.json')
    if evidence.exists():
        parser.error('use a new run name')
    support = runpy.run_path(str(ROOT / 'tools/run-prefix-test.py'))
    console, connect = support['ps5_console'], support['connect']
    listing, remove_tree = support['listing'], support['remove_tree']
    settings = console.load_settings()
    def idle():
        return ' count=0 ' in console.ps5vkctl_command(settings, 'procs', timeout=15) + ' '
    if not idle():
        raise RuntimeError('console is occupied: nothing changed')
    # Route selection sends no datagram and does not scan the console.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((settings['host'], settings['klog_port']))
        host = route.getsockname()[0]
    probe, marker = ('winhttp-probe.exe', 'PS5_PROTON_WINHTTP') if args.winhttp else ('tls-probe.exe', 'PS5_PROTON_TLS_PROBE')
    executable = (ROOT / '.deps/probes' / probe).read_bytes()   # the deployed C:\<probe>
    endpoints = []
    installed = False
    status = 1
    removed = False
    try:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            trusted_root, certificate, key = certificates(directory, 'trusted', host)
            endpoints.append(server(host, certificate, key, args.trusted_port))
            _, certificate, key = certificates(directory, 'untrusted', host)
            endpoints.append(server(host, certificate, key, args.untrusted_port))
            arguments = (f'{host} {endpoints[0][0].server_port}' if args.winhttp else
                         f'{host} {endpoints[0][0].server_port} {endpoints[1][0].server_port} {ROOT_FILE}')
            with connect(settings) as ftp:
                remove_owned(ftp, listing, remove_tree)
                installed = True
                ftp.storbinary('STOR ' + ROOT_PATH, io.BytesIO(trusted_root))
                readback = io.BytesIO()
                ftp.retrbinary('RETR ' + ROOT_PATH, readback.write)
                if readback.getvalue() != trusted_root:
                    raise RuntimeError('TLS fixture upload differs: ' + ROOT_FILE)
            status = subprocess.run([sys.executable, str(ROOT / 'tools/run-console.py'), '--name', args.name,
                '--exe', probe, '--args', arguments, '--no-stdout', '--marker', marker,
                '--exit-stall', '60', *[f'--env={pair}' for pair in args.env]], cwd=ROOT).returncode
    finally:
        for endpoint, thread in endpoints:
            endpoint.shutdown()
            endpoint.server_close()
            thread.join(timeout=3)
        if installed and idle():
            with connect(settings) as ftp:
                try:
                    ftp.delete(ROOT_PATH)
                except (ftplib.error_reply, ftplib.error_perm) as error:   # "226 File deleted"; 550: never written
                    if str(error)[:3] not in ('226', '550'):
                        raise
            removed = True
        if evidence.exists():
            record = json.loads(evidence.read_text())
            log = '\n'.join(path.read_text(errors='replace') for path in (ROOT / 'klog').glob(args.name + '*.log'))
            fixture = {'trusted_answers': endpoints[0][0].answers if endpoints else 0,
                       'untrusted_answers': endpoints[1][0].answers if len(endpoints) == 2 else 0,
                       'root_removed': 'tls: fixture root removed' in log,
                       'test_data_removed': removed, 'public_internet_required': False,
                       'cryptography_version': cryptography.__version__,
                       'probe_sha256': hashlib.sha256(executable).hexdigest()}
            fixture['ports'] = [endpoint.server_port for endpoint, _ in endpoints]
            record['tls_fixture'] = fixture
            record['passed'] = record.get('passed', False) and removed and fixture['trusted_answers'] >= 1 and (
                args.winhttp or fixture['root_removed'] and fixture['untrusted_answers'] >= 1)
            evidence.write_text(json.dumps(record, indent=1) + '\n')
            status = 0 if record['passed'] else 1
    return status


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print('TLS fixture runner failed: ' + re.sub(r'\b\d{1,3}(\.\d{1,3}){3}\b', '<console>', str(error)), file=sys.stderr)
        sys.exit(1)
