#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The player's wallpaper reaches the desktop (src/pw_wallpaper.h), end to end: tests/wallpaper-choice-probe.c in a desktop session.

  tools/run-wallpaper.py --name RUN [--id glass-city] [--reference klog/<settings-render run>-reference]

wallpaper.txt is set to a picture that is not the default, the Mihawk desktop session starts, and the probe checks that
C:\\windows\\web\\wallpaper\\proton.bmp (what the desktop paints) is that picture byte for byte, then asks for a picture of the
displayed desktop, then (--settings) drives Settings > Personalization with the keys and checks wallpaper.txt changed. Here the displayed region (the middle of the screen, clear of the taskbar and the icons) is compared
with the chosen picture's own pixels, and must not be the default's. The player's own wallpaper.txt is put back, or
removed when there was none. The title's own line ("Mihawk: wallpaper <id> ...: applied") is in the record too.
Pictures 3 to 5 are Settings' Home, Personalization and System > Display; with --reference (the prototype's pages,
tools/run-settings-render.py) each window is compared with its page, the same numbers that tool gives on the PC.
"""
import argparse
import ftplib
import io
import json
import re
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'PS5_Vulkan/tools'))
SETTING = '/data/ps5proton/wallpaper.txt'
REGION = (760, 140, 1560, 560)   # clear of the icons (left), the taskbar (bottom) and the version note (top right)
LEFT_STRIP = (100, 420, 380, 860)   # clear of the Settings window (it starts at x 400), the icons and the taskbar


class Choice:
    """Sets wallpaper.txt for the run and puts the player's own value back."""

    def __init__(self, identifier):
        self.identifier = identifier

    def _ftp(self):
        ftp = ftplib.FTP()
        ftp.connect(self.settings['host'], self.settings['ftp_port'], timeout=20)
        ftp.login(self.settings['ftp_user'], self.settings['ftp_password'])
        return ftp

    def __enter__(self):
        import ps5_console
        self.settings = ps5_console.load_settings()
        with self._ftp() as ftp:
            lines = []
            try:
                ftp.retrlines('RETR ' + SETTING, lines.append)
                self.before = ('\n'.join(lines) + '\n').encode()
            except ftplib.error_perm:
                self.before = None
            ftp.storbinary('STOR ' + SETTING, io.BytesIO(f'wallpaper {self.identifier}\n'.encode()))
        return self

    def __exit__(self, *unused):
        with self._ftp() as ftp:
            if self.before is None:
                try:
                    ftp.delete(SETTING)
                except ftplib.all_errors:
                    pass
            else:
                ftp.storbinary('STOR ' + SETTING, io.BytesIO(self.before))
        return False


def mean(image, box):
    pixels = list(image.crop(box).convert('RGB').getdata())
    return [sum(p[i] for p in pixels) / len(pixels) for i in range(3)]


def display_verdict(capture, identifier):
    """The capture's middle region against the chosen picture's: the same colours to within 10 levels of the mean, and
    apart from the default's by more than 15 (so a desktop that kept the default fails)."""
    theme = ROOT / '.deps/windows11-theme'
    with Image.open(ROOT / capture) as shot, Image.open(theme / f'{identifier}.bmp') as chosen, \
            Image.open(theme / 'bubble-city.bmp') as default:
        got, want, other = mean(shot, REGION), mean(chosen, REGION), mean(default, REGION)
    distance = max(abs(a - b) for a, b in zip(got, want))
    apart = max(abs(a - b) for a, b in zip(got, other))
    return {'capture': capture, 'region': REGION, 'shown': [round(v, 1) for v in got], 'chosen': [round(v, 1) for v in want],
            'default': [round(v, 1) for v in other], 'distance_to_chosen': round(distance, 1),
            'distance_to_default': round(apart, 1), 'passed': distance <= 10 and (identifier == 'bubble-city' or apart > 15)}


def live_verdict(capture, chosen, before):
    """Settings' own choice, picture 2: the strip left of its window shows the chosen picture, not the one before it."""
    theme = ROOT / '.deps/windows11-theme'
    with Image.open(ROOT / capture) as shot, Image.open(theme / f'{chosen}.bmp') as new, Image.open(theme / f'{before}.bmp') as old:
        got, want, was = mean(shot, LEFT_STRIP), mean(new, LEFT_STRIP), mean(old, LEFT_STRIP)
    distance = max(abs(a - b) for a, b in zip(got, want))
    apart = max(abs(a - b) for a, b in zip(got, was))
    return {'capture': capture, 'region': LEFT_STRIP, 'chosen': chosen, 'before': before, 'shown': [round(v, 1) for v in got],
            'distance_to_chosen': round(distance, 1), 'distance_to_before': round(apart, 1),
            'passed': distance <= 12 and apart > 15}


def settings_pages(stdout, captures, reference):
    """Each captured Settings page against the prototype's: share of pixels within 1 and 8 levels, and the mean."""
    import numpy as np
    pages = {}
    for name, x, y, number in re.findall(r'settings page (\w+) client at (-?\d+),(-?\d+) capture (\d+)', stdout):
        x, y, number = int(x), int(y), int(number)
        if number > len(captures): continue
        entry = {'capture': captures[number - 1], 'client': [x, y]}
        if reference and (reference / f'{name}.png').exists():
            with Image.open(ROOT / captures[number - 1]) as shot, Image.open(reference / f'{name}.png') as page:
                a = np.asarray(shot.convert('RGB').crop((x, y, x + 1178, y + 798))).astype(int)
                b = np.asarray(page.convert('RGB').crop((371, 117, 371 + 1178, 117 + 798))).astype(int)
            d = np.abs(a - b).max(axis=2)
            entry.update(within1=round(float((d <= 1).mean() * 100), 1), within8=round(float((d <= 8).mean() * 100), 1),
                         mean=round(float(d.mean()), 2))
        pages[name] = entry
    return pages


def accent_verdict(stdout, captures):
    """The theme's accent on screen (src/pw_accent.h): pixels within 8 levels of its fill shade in Settings' client and in
    the taskbar's 48 px, in the picture taken with it stored, and in Personalization's picture before it (the default)."""
    import numpy as np
    note = re.search(r'accent ([0-9a-f]{6}) fill ([0-9a-f]{6}) capture (\d+)', stdout)
    page = re.search(r'settings page accent client at (-?\d+),(-?\d+) capture (\d+)', stdout)
    before = re.search(r'settings page personalization client at (-?\d+),(-?\d+) capture (\d+)', stdout)
    if not note or not page or int(note.group(3)) > len(captures):
        return {'passed': False, 'why': 'no accent picture'}
    fill = np.array([int(note.group(2)[i:i + 2], 16) for i in (0, 2, 4)])
    def counts(capture, x, y):
        with Image.open(ROOT / capture) as shot:
            a = np.asarray(shot.convert('RGB')).astype(int)
        near = np.abs(a - fill).max(axis=2) <= 8
        return int(near[y:y + 798, x:x + 1178].sum()), int(near[a.shape[0] - 48:, :].sum())
    settings, taskbar = counts(captures[int(note.group(3)) - 1], int(page.group(1)), int(page.group(2)))
    result = {'accent': note.group(1), 'fill': note.group(2), 'settings_pixels': settings, 'taskbar_pixels': taskbar}
    if before and int(before.group(3)) <= len(captures):
        result['before_settings_pixels'], result['before_taskbar_pixels'] = counts(captures[int(before.group(3)) - 1],
                                                                                  int(before.group(1)), int(before.group(2)))
    # Settings' navigation bar alone is 3 x 16 px; the taskbar's running mark under Settings' button 16 x 3. Personalization's
    # picture before, with the default accent, has none of that shade.
    result['passed'] = (settings >= 40 and taskbar >= 8 and result.get('before_settings_pixels', 0) == 0 and
                        result.get('before_taskbar_pixels', 0) == 0)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--name', required=True)
    parser.add_argument('--id', default='glass-city')
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    with Choice(args.id):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/run-console.py'), '--name', args.name,
            '--profile', 'mihawk-os',
            f'--args=Z:\\data\\ps5proton\\prefixes\\probe\\drive_c\\wallpaper-choice64-probe.exe {args.id} --settings '
            '--capture=Z:\\data\\ps5proton\\logs\\game-frame.ppm',
            '--marker', 'PS5_PROTON_WALLPAPER', '--seconds', '210', '--watchdog', '260'], cwd=ROOT)
    path = ROOT / 'evidence' / (args.name + '.json')
    if not path.exists():
        return result.returncode or 1
    record = json.loads(path.read_text())
    captures = record.get('captures', [])
    shown = display_verdict(captures[0], args.id) if captures else {'passed': False, 'why': 'no picture'}
    log = ROOT / 'klog' / (args.name + '.log')
    applied = re.findall(r'Mihawk: wallpaper (\S+) \(setting (\w[\w ]*)\): ([^\n]*)', log.read_text(errors='replace')) if log.exists() else []
    stdout = ROOT / 'klog' / (args.name + '-stdout.txt')
    chose = re.search(r"Settings chose '([\w.-]+)' \(was '([\w.-]+)'\)", stdout.read_text(errors='replace')) if stdout.exists() else None
    live = live_verdict(captures[1], chose.group(1), chose.group(2)) if chose and len(captures) > 1 else {'passed': False, 'why': 'no choice or picture'}
    record['wallpaper_live'] = live
    record['settings_pages'] = settings_pages(stdout.read_text(errors='replace') if stdout.exists() else '', captures, args.reference)
    record['accent'] = accent_verdict(stdout.read_text(errors='replace') if stdout.exists() else '', captures)
    record['probe_passed'] = record['passed']
    record['wallpaper'] = {'id': args.id, 'title_said': [' | '.join(a) for a in applied], 'display': shown}
    record['passed'] = (record['passed'] and shown['passed'] and live['passed'] and record['accent']['passed'] and
                        any(a[0] == args.id for a in applied))
    path.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({'start': record['wallpaper'], 'live': live, 'settings_pages': record['settings_pages'],
                      'accent': record['accent']}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
