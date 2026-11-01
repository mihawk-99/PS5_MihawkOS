#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The signed release MihawkOS updates itself from (docs/SELF_INSTALL.md, phase 8; src/pw_release.h).

  tools/sign-release.py keygen [--key keys/release-ed25519.pem] [--public platform/update/release-key.txt]
      The owner, once, on their own PC. A new Ed25519 key in the ignored keys/ folder (mode 0600; tools/privacy_check.py
      refuses to stage it), and its public half, which the title is built with (ps5/CMakeLists.txt). Refuses to replace
      either: a title built with one key takes updates signed by that key only.
  tools/sign-release.py sign --base URL [--version V] [--title dist/PPSA99170] [--runtime dist/runtime]
                             [--out dist/release] [--key keys/release-ed25519.pem]
      The release folder, flat, as its assets are published: the title pack (tools/pack-runtime.py --title-pack) and its
      title-pack.txt, the runtime's pieces and runtime-pack.txt, and mihawkos-release.txt signed with the key. --base is
      where those assets will be, the release's own tag folder (never a "latest" URL); --version defaults to the
      title's contentVersion. The manifest is checked against the public half before the command ends.
  tools/sign-release.py sign --zip PS5_MihawkOS-v<VERSION>.zip --base URL [--out DIR] [--key ...]
      The automatic update's form (2026-10-31): the manifest names the release zip itself (its SHA-256) and the
      SHA-256 of the runtime-pack.txt in its PPSA99170 folder; --version defaults to that folder's contentVersion.
      Only mihawkos-release.txt is written to --out; attach it to the GitHub release beside the zip. --base is the
      release's download folder: https://github.com/mihawk-99/PS5_MihawkOS/releases/download/<tag>/
  tools/sign-release.py verify FILE [--public platform/update/release-key.txt]
      Check a manifest's signature and fields as the title does.

OpenSSL signs (pkeyutl -rawin: RFC 8032 Ed25519, which Monocypher's crypto_ed25519_check in the title accepts;
tests/test_sign_release.py proves the two agree). The private key is read by OpenSSL alone: this tool never prints,
copies or logs it.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEY = ROOT / 'keys/release-ed25519.pem'
PUBLIC = ROOT / 'platform/update/release-key.txt'
DER_PREFIX = bytes.fromhex('302a300506032b6570032100')   # SubjectPublicKeyInfo of an Ed25519 key, before its 32 bytes


def openssl(*args, data=None):
    return subprocess.run(['openssl', *args], input=data, check=True, capture_output=True).stdout


def public_half(key):
    return openssl('pkey', '-in', str(key), '-pubout', '-outform', 'DER')[-32:].hex()


def keygen(args):
    for path in (args.key, args.public):
        if path.exists():
            raise SystemExit(f'{path} exists: a title built with that key takes updates signed by it only; '
                             'move it aside yourself if you mean to start again')
    args.key.parent.mkdir(parents=True, exist_ok=True)
    old = os.umask(0o077)
    try:
        openssl('genpkey', '-algorithm', 'ed25519', '-out', str(args.key))
    finally:
        os.umask(old)
    os.chmod(args.key, 0o600)
    public = public_half(args.key)
    args.public.parent.mkdir(parents=True, exist_ok=True)
    args.public.write_text(public + '\n')
    print(f'release key: {args.key} (keep it on this PC only; back it up offline)\npublic half: {public} in {args.public}')
    return 0


def body_of(text):
    """The signed bytes and the signature, from a manifest's text."""
    match = re.fullmatch(rb'(.*\n)signature ([0-9a-f]{128})\n', text, re.S)
    if not match:
        raise SystemExit('not a signed release manifest')
    return match.group(1), bytes.fromhex(match.group(2).decode())


def check(text, public):
    """The signature against `public` (hex) with OpenSSL, then the fields as src/pw_release.c reads them."""
    body, signature = body_of(text)
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        (work / 'public.der').write_bytes(DER_PREFIX + bytes.fromhex(public))
        (work / 'body').write_bytes(body)
        (work / 'signature').write_bytes(signature)
        if subprocess.run(['openssl', 'pkeyutl', '-verify', '-pubin', '-keyform', 'DER', '-inkey', str(work / 'public.der'),
                           '-rawin', '-in', str(work / 'body'), '-sigfile', str(work / 'signature')],
                          capture_output=True).returncode:
            raise SystemExit('the signature does not match the public half')
    lines = body.decode().splitlines()
    shapes = [r'mihawkos-release 1', r'version \d{1,9}(\.\d{1,9})*', r'base https?://[!-~]+/', r'title [0-9a-f]{64} [A-Za-z0-9_-][A-Za-z0-9._-]*',
              r'runtime [0-9a-f]{64} [A-Za-z0-9_-][A-Za-z0-9._-]*']
    if len(lines) != len(shapes) or not all(re.fullmatch(s, l) for s, l in zip(shapes, lines)) or re.search(r'["\'\\]', lines[2]):
        raise SystemExit('the manifest is signed, but its fields are not what the title reads')
    return dict(line.split(' ', 1) for line in lines[1:])


def sign(args):
    if not args.key.is_file():
        raise SystemExit(f'{args.key}: no release key (the owner makes it once: tools/sign-release.py keygen)')
    if not re.fullmatch(r'https?://[!-~]+/', args.base) or re.search(r'["\'\\]', args.base):
        raise SystemExit('--base: an http(s) folder URL ending in /, the release\'s own tag folder')
    if args.zip:
        return sign_zip(args)
    param = json.loads((args.title / 'sce_sys/param.json').read_text())
    version = args.version or param['contentVersion']
    runtime_manifest = args.title / 'runtime-pack.txt'
    if not runtime_manifest.is_file():
        raise SystemExit(f'{runtime_manifest}: the title pins no runtime (run tools/build-production.sh)')
    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)
    pack = args.out / '.title-pack'   # apart: pack-runtime.py removes the pieces in its --out that it did not make
    subprocess.run([sys.executable, str(ROOT / 'tools/pack-runtime.py'), '--title-pack', str(args.title), '--out', str(pack),
                    '--cache', str(ROOT / '.deps/runtime-pack')], check=True)
    title_text = (pack / 'title-pack.txt').read_bytes()
    runtime_text = runtime_manifest.read_bytes()
    pieces = [(pack, line.split()[1] + '.zst') for line in title_text.decode().splitlines() if line.startswith('chunk ')]
    pieces += [(args.runtime, line.split()[1] + '.zst') for line in runtime_text.decode().splitlines() if line.startswith('chunk ')]
    missing = [name for folder, name in pieces if not (folder / name).is_file()]
    if missing:
        raise SystemExit(f'{len(missing)} pieces are missing, the first {missing[0]} (run tools/build-production.sh)')
    for folder, name in pieces:
        shutil.copyfile(folder / name, args.out / name)
    shutil.rmtree(pack)
    (args.out / 'title-pack.txt').write_bytes(title_text)
    (args.out / 'runtime-pack.txt').write_bytes(runtime_text)
    body = (f'mihawkos-release 1\nversion {version}\nbase {args.base}\n'
            f'title {hashlib.sha256(title_text).hexdigest()} title-pack.txt\n'
            f'runtime {hashlib.sha256(runtime_text).hexdigest()} runtime-pack.txt\n').encode()
    with tempfile.TemporaryDirectory() as work:
        (Path(work) / 'body').write_bytes(body)
        signature = openssl('pkeyutl', '-sign', '-rawin', '-inkey', str(args.key), '-in', str(Path(work) / 'body'))
    text = body + b'signature ' + signature.hex().encode() + b'\n'
    check(text, public_half(args.key))
    (args.out / 'mihawkos-release.txt').write_bytes(text)
    size = sum(p.stat().st_size for p in args.out.iterdir())
    print(f'release {version}: {len(pieces)} pieces and 3 manifests, {size} bytes, in {args.out}; base {args.base}')
    return 0


def signed(body, key):
    with tempfile.TemporaryDirectory() as work:
        (Path(work) / 'body').write_bytes(body)
        signature = openssl('pkeyutl', '-sign', '-rawin', '-inkey', str(key), '-in', str(Path(work) / 'body'))
    text = body + b'signature ' + signature.hex().encode() + b'\n'
    check(text, public_half(key))
    return text


def sign_zip(args):
    import zipfile
    prefix = 'PPSA99170/'
    with zipfile.ZipFile(args.zip) as archive:
        names = set(archive.namelist())
        for needed in ('eboot.bin', 'sce_sys/param.json', 'runtime-pack.txt'):
            if prefix + needed not in names:
                raise SystemExit(f'{args.zip}: no {prefix}{needed}: not a MihawkOS release zip')
        version = args.version or json.loads(archive.read(prefix + 'sce_sys/param.json'))['contentVersion']
        runtime = hashlib.sha256(archive.read(prefix + 'runtime-pack.txt')).hexdigest()
    digest = hashlib.sha256()
    with open(args.zip, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    if re.search(r'\s', args.zip.name):
        raise SystemExit(f'{args.zip.name}: a name with a space cannot be a manifest field')
    body = (f'mihawkos-release 1\nversion {version}\nbase {args.base}\n'
            f'title {digest.hexdigest()} {args.zip.name}\n'
            f'runtime {runtime} runtime-pack.txt\n').encode()
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'mihawkos-release.txt').write_bytes(signed(body, args.key))
    print(f'release {version}: {args.zip.name} signed; {args.out / "mihawkos-release.txt"}; base {args.base}')
    return 0


def verify(args):
    public = args.public.read_text().split()[0]
    fields = check(args.file.read_bytes(), public)
    print(f'signed by {public}: version {fields["version"]}, base {fields["base"]}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    make = commands.add_parser('keygen')
    make.add_argument('--key', type=Path, default=KEY)
    make.add_argument('--public', type=Path, default=PUBLIC)
    release = commands.add_parser('sign')
    release.add_argument('--base', required=True)
    release.add_argument('--version')
    release.add_argument('--title', type=Path, default=ROOT / 'dist/PPSA99170')
    release.add_argument('--runtime', type=Path, default=ROOT / 'dist/runtime')
    release.add_argument('--out', type=Path, default=ROOT / 'dist/release')
    release.add_argument('--key', type=Path, default=KEY)
    release.add_argument('--zip', type=Path, help='sign this release zip (the automatic update) instead of a pack folder')
    check_one = commands.add_parser('verify')
    check_one.add_argument('file', type=Path)
    check_one.add_argument('--public', type=Path, default=PUBLIC)
    args = parser.parse_args()
    return {'keygen': keygen, 'sign': sign, 'verify': verify}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
