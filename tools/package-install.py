#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The release package: one folder to copy, which installs MihawkOS (docs/SELF_INSTALL.md; phase 9 publishes it beside
the signed update, tools/sign-release.py).

  tools/package-install.py [--title dist/PPSA99170] [--runtime dist/runtime] [--out dist/package] [--version V]

  OUT/PPSA99170/                 the title folder, the runtime's pieces in its runtime-pieces/: copy it into
                                 /data/homebrew (FTP from a phone or a PC, or a file manager). The guided setup copies
                                 the runtime from there (the owner, 2026-10-31: one folder, no USB drive, no FTP step)
  OUT/SHA256SUMS                 every file's SHA-256, to check a copy (sha256sum -c, from OUT)
  OUT/README.txt                 those steps
  OUT/PS5_MihawkOS-vV.zip        the three above, the release's one download (V: the VERSION file's)
  OUT/PS5_MihawkOS-vV.zip.sha256 the zip's SHA-256

From a production build (tools/build-production.sh). Every piece the title pins (its runtime-pack.txt) must be in
--runtime at its size and SHA-256 (its name), and the title folder must hold nothing a development run leaves behind.
"""
import argparse
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# What a console run or a PC tool writes into a title folder, never part of a release.
LEFTOVERS = {'install-source.txt', 'update-source.txt', 'test-run.txt', 'wine-run.txt', 'elevation-retry',
             'session-running', 'session-notices', 'install-probe.txt', 'install-probe-results.txt', 'title-pack.txt'}
NEEDED = ('eboot.bin', 'sce_sys/param.json', 'runtime-pack.txt', 'runtime-stamp')
README = """MihawkOS: install it on a jailbroken PS5 (tested on firmware 10.01 and 13.40; supported up to 13.60)

1. Copy the PPSA99170 folder into /data/homebrew on the console: over FTP from a phone or a PC, or with a file manager
   from a USB stick. It is about 0.6 GB: the Windows runtime is inside it. ShadowMountPlus shows its tile on the home
   screen within about 45 seconds.
2. Open MihawkOS. Its guided setup copies the Windows runtime from its own folder, about a minute, checks every file,
   and starts Mihawk OS once. Leave about 4 GB free on the console: 1.6 GB for the runtime, 1.4 GB for the first
   Windows drive, and some room.
3. To update, replace the PPSA99170 folder with the new release's and open MihawkOS: it updates its runtime by itself.

Each boot: load kstuff, elfldr and websrv, and ShadowMountPlus first. If another homebrew will not start after MihawkOS
closes, restart the PS5 (a known issue with ShadowMountPlus 1.7beta4).
SHA256SUMS lists every file here; from this folder, `sha256sum -c SHA256SUMS` checks a copy.
"""


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as file:
        for block in iter(lambda: file.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--title', type=Path, default=ROOT / 'dist/PPSA99170')
    parser.add_argument('--runtime', type=Path, default=ROOT / 'dist/runtime')
    parser.add_argument('--out', type=Path, default=ROOT / 'dist/package')
    parser.add_argument('--version', default=(ROOT / 'VERSION').read_text().strip())
    args = parser.parse_args()
    missing = [name for name in NEEDED if not (args.title / name).is_file()]
    if missing:
        raise SystemExit(f'{args.title}: no {", ".join(missing)} (run tools/build-production.sh)')
    files = sorted(p for p in args.title.rglob('*') if not p.is_dir())
    stray = [str(p.relative_to(args.title)) for p in files if p.is_symlink() or p.name in LEFTOVERS]
    if stray:
        raise SystemExit(f'{args.title} holds what no release carries: {", ".join(stray)}')
    pieces = []
    for line in (args.title / 'runtime-pack.txt').read_text().splitlines():
        if line.startswith('chunk '):
            _, digest, packed = line.split()[:3]
            piece = args.runtime / f'{digest}.zst'
            if not piece.is_file() or piece.stat().st_size != int(packed) or sha256(piece) != digest:
                raise SystemExit(f'{piece}: missing, or not the piece runtime-pack.txt pins')
            pieces.append(piece)
    if not pieces:
        raise SystemExit('runtime-pack.txt pins no pieces')
    if args.out.exists():
        shutil.rmtree(args.out)
    title = args.out / args.title.name
    shutil.copytree(args.title, title)
    inside = title / 'runtime-pieces'   # where the title looks (ps5/src/proton_install.cpp)
    inside.mkdir()
    for piece in pieces:
        shutil.copyfile(piece, inside / piece.name)
    (args.out / 'README.txt').write_text(README)
    sums = [f'{sha256(p)}  {p.relative_to(args.out)}' for p in sorted(args.out.rglob('*')) if p.is_file()]
    (args.out / 'SHA256SUMS').write_text('\n'.join(sums) + '\n')
    # The one download: the pieces stored as they are (zstd already), the rest deflated
    archive = args.out / f'PS5_MihawkOS-v{args.version}.zip'
    with zipfile.ZipFile(archive, 'w') as out:
        for path in sorted(p for p in args.out.rglob('*') if p.is_file() and p != archive):
            out.write(path, path.relative_to(args.out).as_posix(),
                      zipfile.ZIP_STORED if path.suffix == '.zst' else zipfile.ZIP_DEFLATED)
    (args.out / f'{archive.name}.sha256').write_text(f'{sha256(archive)}  {archive.name}\n')
    size = lambda folder: sum(p.stat().st_size for p in folder.rglob('*') if p.is_file())
    print(f'package: {args.out}\n  {title.name}/: {len(files)} files and {len(set(pieces))} runtime pieces for '
          f'{len(pieces)} chunks (identical pieces share one), {size(title)} bytes\n  SHA256SUMS: {len(sums)} files\n'
          f'  {archive.name}: {archive.stat().st_size} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
