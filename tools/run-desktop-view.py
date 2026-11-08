#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Mihawk desktop as the shell's folder view: the probe drives it with input, the displayed pictures are checked here."""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def region_counts(image, box, predicate):
    return sum(1 for p in image.crop(box).getdata() if predicate(p))


def display_verdict(captures):
    """Pictures, in the order the probe asks for them: 1 icons with white labels over a wallpaper; 2 every item selected; 3 the selection moved;
    4 a menu open; 5 one item gone."""
    results = []
    for index, capture in enumerate(captures):
        with Image.open(ROOT / capture) as source:
            image = source.convert('RGB')
        item = {'capture': capture, 'passed': image.size == (1920, 1080)}
        if item['passed'] and index in (0, 1, 2, 4):
            white = region_counts(image, (0, 0, 700, 700), lambda p: min(p) >= 235)
            colours = len(set(image.crop((0, 0, 700, 700)).getdata()))
            item.update(white_label_pixels=white, distinct_colours=colours)
            item['passed'] = white >= 150 and colours >= 40
        if item['passed'] and index == 1:
            tint = region_counts(image, (0, 0, 700, 700), lambda p: p[2] > p[0] + 25 and p[2] > 120)
            item['selection_tint_pixels'] = tint
            item['passed'] = tint >= 300
        if item['passed'] and index == 3:
            light = region_counts(image, (1100, 600, 1920, 1040), lambda p: min(p) >= 225)
            item['menu_light_pixels'] = light
            item['passed'] = light >= 2000
        results.append(item)
    return results, len(results) >= 5 and all(item['passed'] for item in results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--winedebug', help='Wine channels for this run only (run-console.py --env PROTON_WINEDEBUG=...)')
    args = parser.parse_args()
    result = subprocess.run([sys.executable, str(ROOT / 'tools/run-console.py'), '--name', args.name,
        '--profile', 'mihawk-os',
        '--args=Z:\\data\\ps5proton\\prefixes\\probe\\drive_c\\desktop-view64-probe.exe --capture=Z:\\data\\ps5proton\\logs\\game-frame.ppm',
        '--marker', 'PS5_PROTON_DESKTOP_VIEW', '--seconds', '240', '--watchdog', '300']
        + (['--env', 'PROTON_WINEDEBUG=' + args.winedebug] if args.winedebug else []), cwd=ROOT)
    path = ROOT / 'evidence' / (args.name + '.json')
    if not path.exists():
        return result.returncode or 1
    record = json.loads(path.read_text())
    pixels, passed = display_verdict(record.get('captures', []))
    stdout = ROOT / 'klog' / (args.name + '-stdout.txt')
    record['shortcut_ms'] = [{'listed': int(a), 'left': int(b)} for a, b in re.findall(
        r'shortcut listed after (\d+) ms, left after (\d+) ms', stdout.read_text(errors='replace'))] if stdout.exists() else []
    record['view_passed'] = record['passed']
    record['display_pixels'] = pixels
    record['passed'] = record['passed'] and passed
    path.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({'display_pixels': pixels, 'shortcut_ms': record['shortcut_ms'], 'passed': record['passed']}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
