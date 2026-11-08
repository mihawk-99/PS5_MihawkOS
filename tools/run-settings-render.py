#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""MihawkOS Settings against the prototype's Settings, pixel by pixel, on the PC: one command, one verdict per page.

Renders Home, Personalization and System > Display with the built app (.deps/probes/settings64-probe.exe, its
--render mode paints the client into a bitmap, no window shown) on the pinned Wine on Linux with the null display
driver, and compares each with the same page of prototypes/windows11-shell drawn by headless Chromium
(tools/measure-prototype-surface.mjs, light theme, 1920 x 1080). The Settings window sits at 371,117 in the prototype's
picture and its client is 1178 x 798 (src/pw_settings.c, the window's geometry).

    tools/run-settings-render.py --name settings-render-2026-11-07-01 [--reference DIR]

--reference holds <page>.png prototype captures; without it the captures are made into klog/<name>-reference, which
needs the prototype served and Chromium listening as the measure-prototype tools describe. Writes evidence/<name>.json
({page: within1, within8, mean}) and klog/<name>/<page>.bmp, <page>-side.png. A PC oracle, not console proof.
"""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WINE = Path(os.environ.get('PW_SETTINGS_WINE', ROOT / '.deps/wine-runtime/build/wine'))   # a private A/B build
APP = ROOT / '.deps/probes/settings64-probe.exe'
PREFIX = ROOT / '.deps/settings-render-prefix'
THEME = ROOT / '.deps/windows11-theme'
PAGES = ('home', 'personalization', 'display')
WINDOW = (371, 117, 371 + 1178, 117 + 798)   # the prototype's Settings client in its 1920 x 1080 picture


def windows_path(path):
    return 'Z:' + str(path).replace('/', '\\')


def environment():
    return dict(os.environ, WINEPREFIX=str(PREFIX), WINEDEBUG='-all', DISPLAY='', WAYLAND_DISPLAY='',
                WINEDLLOVERRIDES='winemenubuilder.exe=d;mscoree=d;mshtml=d')


def prepare_prefix():
    """A host prefix with the null driver and the Proton UI fonts the console draws with; made once."""
    fonts = PREFIX / 'drive_c/windows/Fonts'
    if (fonts / 'ProtonUI-Regular.ttf').exists(): return
    subprocess.run([str(WINE), 'wineboot', '-i'], env=environment(), check=True, capture_output=True, timeout=300)
    subprocess.run([str(WINE), 'reg', 'add', r'HKCU\Software\Wine\Drivers', '/v', 'Graphics', '/d', 'null', '/f'],
                   env=environment(), check=True, capture_output=True, timeout=120)
    for font in (THEME / 'fonts').glob('ProtonUI-*.ttf'): shutil.copy2(font, fonts / font.name)
    subprocess.run([str(WINE.parent / 'server/wineserver'), '-w'], env=environment(), timeout=120)


def capture_reference(folder):
    folder.mkdir(parents=True, exist_ok=True)
    for page in PAGES:
        subprocess.run(['node', str(ROOT / 'tools/measure-prototype-surface.mjs'), str(folder),
                        f'settings&settingsPage={page}', '.stg-app', 'light'], check=True, timeout=300)
        (folder / f'settings&settingsPage={page}-light.png').replace(folder / f'{page}.png')


def compare(reference, ours):
    a = np.asarray(Image.open(reference).convert('RGB').crop(WINDOW)).astype(int)
    b = np.asarray(Image.open(ours).convert('RGB')).astype(int)
    if a.shape != b.shape: raise SystemExit(f'{ours}: {b.shape[1]} x {b.shape[0]}, the prototype is {a.shape[1]} x {a.shape[0]}')
    d = np.abs(a - b).max(axis=2)
    result = {'within1': round(float((d <= 1).mean() * 100), 1), 'within8': round(float((d <= 8).mean() * 100), 1),
              'mean': round(float(d.mean()), 2)}
    result.update(ink_match(a, b))
    return result


def ink_match(a, b):
    """The text alone: the pixels where either picture is at least 24 levels darker than the lightest pixel around it
    (7 x 7, the background under dark text), dilated by one. ink_within8 and ink_mean compare those pixels; ink_ratio
    is our total darkness over the prototype's there (100 = as heavy, below = thinner or lighter text)."""
    def lum(x): return x @ np.array([0.299, 0.587, 0.114])
    def around(x, op, r):
        padded = np.pad(x, r, mode='edge')
        return op(np.stack([padded[dy:dy + x.shape[0], dx:dx + x.shape[1]]
                            for dy in range(2 * r + 1) for dx in range(2 * r + 1)]), axis=0)
    la, lb = lum(a), lum(b)
    ink_a, ink_b = around(la, np.max, 3) - la, around(lb, np.max, 3) - lb
    mask = around(((ink_a >= 24) | (ink_b >= 24)).astype(np.uint8), np.max, 1).astype(bool)
    if not mask.any(): return {}
    d = np.abs(a - b).max(axis=2)[mask]
    return {'ink_pixels': int(mask.sum()), 'ink_within8': round(float((d <= 8).mean() * 100), 1),
            'ink_mean': round(float(d.mean()), 2),
            'ink_ratio': round(float(ink_b[mask].sum() / max(ink_a[mask].sum(), 1) * 100), 1)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--name', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--current', default='aero-meadow', help='the wallpaper the pages show (the prototype draws the meadow)')
    args = parser.parse_args()
    out = ROOT / 'klog' / args.name
    out.mkdir(parents=True, exist_ok=True)
    reference = args.reference or ROOT / 'klog' / f'{args.name}-reference'
    if not all((reference / f'{p}.png').exists() for p in PAGES): capture_reference(reference)
    prepare_prefix()
    result = {'run': args.name, 'oracle': 'prototypes/windows11-shell in headless Chromium, light theme',
              'renderer': 'pinned Wine on Linux, null display driver', 'window': list(WINDOW), 'pages': {}}
    for page in PAGES:
        picture = out / f'{page}.bmp'
        picture.unlink(missing_ok=True)
        subprocess.run([str(WINE), str(APP), f'--render={page}', f'--out={windows_path(picture)}',
                        f'--wallpapers={windows_path(THEME)}', f'--current={args.current}'],
                       env=environment(), capture_output=True, timeout=180)
        if not picture.exists(): raise SystemExit(f'{page}: the app wrote no picture')
        result['pages'][page] = compare(reference / f'{page}.png', picture)
        side = Image.new('RGB', (1178 * 2, 798))
        side.paste(Image.open(reference / f'{page}.png').convert('RGB').crop(WINDOW), (0, 0))
        side.paste(Image.open(picture).convert('RGB'), (1178, 0))
        side.save(out / f'{page}-side.png')
        print(page, result['pages'][page])
    (ROOT / 'evidence' / f'{args.name}.json').write_text(json.dumps(result, indent=1) + '\n')


if __name__ == '__main__':
    main()
