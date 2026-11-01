#!/usr/bin/env python3
"""The guided setup's pictures on the PC, and its checks (docs/GUIDED_SETUP.md).

    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json python3 tools/test-setup-ui.py

After ps5/tools/build.sh and ps5/tools/host-reference.sh. Each step is shown through PROTON_SETUP_TEST
(<step>[:<the fake install's scenario>][@<share held>]) and PROTON_SETUP_LANGUAGE, walked with the test run's presses,
and kept as klog/ui-host/setup-<name>.png. The checks: every run ends well, the log names the step it reached, the
picture is not blank, choosing a language switches the text, and Try again after a missing piece starts the install.

The Steam guide (src/pw_steam_guide.h): its launcher pages through PROTON_STEAM_GUIDE_TEST
(<page>[:<failure>][@<share downloaded>][#<display name>]) as setup-steam-<page>-<language>.png, every page in every
language and every failure in English; and the screens its sessions draw, from the same C code the title runs
(tests/test_pw_steam_screen.c's pictures), as setup-steam-session-<screen>-<language>.png.
Copyright (C) 2026 Mihawk
SPDX-License-Identifier: GPL-3.0-or-later
"""
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/ps5-host'
APP = BUILD / 'app'
OUT = ROOT / 'klog/ui-host'

COVER_LANGUAGES = ('en-US', 'fr-FR', 'es-ES', 'de-DE', 'it-IT', 'ru-RU', 'zh-CN', 'ja-JP', 'pt-BR', 'pl-PL')
STEAM_PAGES = {'intro': 0, 'download@0.45': 1, 'starting': 1, 'failed:offline': 2, 'done#Mihawk': 3}  # PwSteamPage
STEAM_FAILURES = ('offline', 'clock', 'download', 'install', 'stalled', 'cancelled', 'timeout', 'guard', 'ended')
COVER_STAGES = ('settings', 'dotnet', 'finishing', 'ready')  # PwSetupBuild's order
NO_EXIT = ['triangle', 'square', 'options', 'touch', 'l1', 'r1', 'l2', 'r2']   # the buttons that move nothing in the tour

# name, PROTON_SETUP_TEST, language, frames, presses (frame, action), the step the log must end on
RENDERS = [
    ('welcome', '0', 'en-US', 90, [], 0),
    ('language', '1', 'en-US', 60, [], 1),
    ('language-pick-french', '1', 'en-US', 90, [(20, 'right'), (28, 'cross')], 4),
    ('installing', '4@0.62', 'en-US', 40, [], 4),
    ('installing-damaged', '4:damaged@0.5', 'en-US', 40, [], 4),
    # The runtime comes from the title folder alone (no How to install, USB or FTP pages since 2026-10-31): a folder
    # without its pieces, or with some, says so with Try again, and nothing else; Try again starts the install once
    # the pieces are there (the fake engine finds them the second time)
    *[('installing-no-pieces-' + tag, '4:no-pieces', tag, 40, [], 4) for tag in COVER_LANGUAGES],
    ('installing-partial', '4:partial', 'en-US', 40, [], 4),
    ('installing-retry', '4:no-pieces', 'en-US', 60, [(10, 'cross')], 4),
    ('preparing', '5', 'en-US', 60, [], 5),
    ('done', '6', 'en-US', 90, [], 6),
    # Done's two choices (Install Steam focused first): the library tile, and Install Steam, which opens the Steam guide
    ('done-pick-library', '6', 'en-US', 60, [(10, 'right'), (18, 'cross')], 6),
    ('done-pick-steam', '6', 'en-US', 60, [(10, 'cross')], 6),
    *[('done-' + tag, '6', tag, 60, [], 6) for tag in COVER_LANGUAGES if tag != 'en-US'],
    # The limits page (step 8, before Done): in every language; cross held 60 frames then let go stays there; held
    # 3,000 frames (10 s at up to 300 fps) goes on to Done. A hold is (first frame, last frame, action).
    *[('limits-' + tag, '8', tag, 60, [], 8) for tag in COVER_LANGUAGES],
    ('limits-short-hold', '8', 'en-US', 120, [(10, 70, 'cross')], 8),
    ('limits-full-hold', '8', 'en-US', 3100, [(10, 3000, 'cross')], 6),
    # No way out, on a first run as on a re-run from Settings (the same tour; the owner, 2026-10-31): every other button
    # on Welcome, Language and Before you play ends nothing, and a press of cross on Before you play is not a hold
    *[(f'{name}-no-exit', step, 'en-US', 80, [(f, b) for f, b in zip(range(10, 80, 6), buttons)], int(step))
      for name, step, buttons in (('welcome', '0', NO_EXIT + ['circle']), ('language', '1', NO_EXIT),
                                  ('limits', '8', NO_EXIT + ['circle', 'cross']))],
    ('welcome-fr', '0', 'fr-FR', 90, [], 0),
    ('installing-fr', '4@0.62', 'fr-FR', 40, [], 4),
    ('preparing-fr', '5', 'fr-FR', 60, [], 5),
    ('installing-ru', '4@0.62', 'ru-RU', 40, [], 4),
    ('welcome-zh', '0', 'zh-CN', 90, [], 0),
    ('preparing-ja', '5', 'ja-JP', 60, [], 5),
    ('language-ja', '1', 'ja-JP', 60, [], 1),
    # Polish (pl-PL, the tenth): its pages, the ten language tiles, and its pick from them (the last tile)
    ('welcome-pl', '0', 'pl-PL', 90, [], 0),
    ('language-pl', '1', 'pl-PL', 60, [], 1),
    ('language-pick-polish', '1', 'en-US', 90, [(14, 'down'), (20, 'right'), (26, 'right'), (32, 'right'), (38, 'right'),
                                               (46, 'cross')], 4),
    ('installing-pl', '4@0.62', 'pl-PL', 40, [], 4),
    ('preparing-pl', '5', 'pl-PL', 60, [], 5),
    # The console gate's walk (tools/run-launcher.py --setup): Welcome to Preparing, in Japanese, through the fake install,
    # which starts by itself (each frame is at most 0.05 s of the tour's time: 700 frames cover the 8.6 s install at
    # 15 fps or more)
    # The first session's cover (src/pw_setup.h): Start's frame, kept by the launcher, in every language; the title's
    # marks are drawn over it by the same C code (test_pw_setup's picture mode) as setup-cover-<language>-<stage>.png.
    *[('cover-' + tag, '5:cover', tag, 40, [], 5) for tag in COVER_LANGUAGES],
    ('walk-ja', '0', 'en-US', 700, [(30, 'cross'), (50, 'down'), (56, 'right'), (62, 'right'), (70, 'cross')], 5),
    # The Steam guide's launcher pages ("steam:" runs PROTON_STEAM_GUIDE_TEST; the number is the page the log names)
    *[(f"steam-{test.split('@')[0].split(':')[0].split('#')[0]}-{tag}", 'steam:' + test, tag, 40, [], page)
      for tag in COVER_LANGUAGES for test, page in STEAM_PAGES.items()],
    *[(f'steam-failed-{word}', f'steam:failed:{word}', 'en-US', 40, [], 2) for word in STEAM_FAILURES],
]


def main():
    from PIL import Image, ImageStat
    subprocess.run(['ninja', '-C', str(BUILD), 'ps5-samples-host'], check=True, stdout=subprocess.DEVNULL)
    marks = OUT / 'cover-marks'
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cc', '-std=gnu11', '-O2', str(ROOT / 'tests/test_pw_setup.c'), str(ROOT / 'src/pw_setup.c'), '-lm',
                    '-o', str(marks)], check=True)
    script = APP / 'test-run.txt'
    backup = script.read_bytes() if script.exists() else None
    OUT.mkdir(parents=True, exist_ok=True)
    try:
        for name, test, language, frames, presses, step in RENDERS:
            script.write_text(f'frames {frames}\nscreenshot\nsamples ps5proton\n' +
                              ''.join(f'press ps5proton {p[0]} {p[1]}\n' if len(p) == 2 else
                                      f'hold ps5proton {p[0]} {p[1]} {p[2]}\n' for p in presses))
            steam = test.startswith('steam:')
            env = dict(os.environ, PROTON_SETUP_LANGUAGE=language)
            env.pop('PROTON_SETUP_TEST' if steam else 'PROTON_STEAM_GUIDE_TEST', None)
            env['PROTON_STEAM_GUIDE_TEST' if steam else 'PROTON_SETUP_TEST'] = test[6:] if steam else test
            run = subprocess.run([str(BUILD / 'ps5-samples-host')], capture_output=True, text=True, timeout=900, env=env)
            log = run.stdout + run.stderr
            (OUT / f'setup-{name}.log').write_text(log)
            assert run.returncode == 0 and 'ends: status 0' in log, f'{name}: {log[-1500:]}'
            steps = re.findall(r'steam guide: page (\d+)' if steam else r'setup: (?:starts at )?step (\d+)', log)
            assert steps and int(steps[-1]) == step, f'{name}: reached step {steps[-1:]} not {step}'
            assert 'setup: the sky' not in log, f'{name}: the sky failed'
            image = Image.open(APP / 'screenshots/ps5proton.ppm').convert('RGB')
            assert max(ImageStat.Stat(image).stddev) > 8, f'{name}: blank picture'
            image.save(OUT / f'setup-{name}.png')
            if test == '5:cover':
                stages = range(len(COVER_STAGES)) if language == 'en-US' else (1,)
                for stage in stages:
                    marked = OUT / 'cover-marked.ppm'
                    subprocess.run([str(marks), str(ROOT), str(APP / 'screenshots/ps5proton.ppm'), str(marked), str(stage), '3'],
                                   check=True, stdout=subprocess.DEVNULL)
                    Image.open(marked).save(OUT / f'setup-{name}-{COVER_STAGES[stage]}.png')
                    marked.unlink()
            print(f'setup-{name}: step {step}, {frames} frames')
        if 'setup: language fr-FR' not in (OUT / 'setup-language-pick-french.log').read_text():
            raise AssertionError('choosing the third tile did not choose French')
        if 'setup: language pl-PL' not in (OUT / 'setup-language-pick-polish.log').read_text():
            raise AssertionError('choosing the last tile did not choose Polish')
        short, full = ((OUT / f'setup-limits-{name}-hold.log').read_text() for name in ('short', 'full'))
        if 'setup: hold let go at' not in short or 'setup: held' in short:
            raise AssertionError('a short hold on the limits page went on, or letting go was not seen')
        if 'setup: held' not in full:
            raise AssertionError('a full hold on the limits page did not go on to Done')
        for name in ('welcome', 'language', 'limits'):
            if re.search(r'setup: (ends|held)', (OUT / f'setup-{name}-no-exit.log').read_text()):
                raise AssertionError(f'a button on {name} left the page: the tour has a way out')
        # The counter: Before you play is step 5 of 6 and Done is 6 of 6 (the log says what the counter shows)
        if 'setup: step 8, 5 of 6' not in full or 'setup: step 6, 6 of 6' not in full:
            raise AssertionError('the step counter does not read 5 of 6 on Before you play and 6 of 6 on Done')
        library, steam = ((OUT / f'setup-done-pick-{name}.log').read_text() for name in ('library', 'steam'))
        if 'setup: ends (the library)' not in library or 'steam guide' in library:
            raise AssertionError("Done's library tile did not close the tour into the library")
        if 'setup: ends (Install Steam)' not in steam or 'steam guide: starts at page 0' not in steam:
            raise AssertionError("Done's Install Steam did not open the Steam guide at its intro")
        # The sessions' screens: the title's own drawing, from tests/test_pw_steam_screen.c's picture mode
        screens, sessions = OUT / 'steam-screens', OUT / 'steam-session'
        cuts = BUILD.parent / 'ps5/generated/setup/fonts'
        sessions.mkdir(exist_ok=True)
        subprocess.run(['cc', '-std=gnu11', '-O2', str(ROOT / 'tests/test_pw_steam_screen.c'), str(ROOT / 'src/pw_steam_screen.c'),
                        str(ROOT / 'src/pw_loading_art.c'), str(ROOT / 'src/pw_setup.c'), '-lm', '-o', str(screens)], check=True)
        subprocess.run([str(screens), str(ROOT / '.deps/hui/third_party/fonts'), str(cuts) if cuts.is_dir() else '-',
                        str(sessions)], check=True)
        for ppm in sorted(sessions.glob('*.ppm')):
            Image.open(ppm).save(OUT / ('setup-steam-session-' + ppm.stem[len('steam-'):] + '.png'))
            ppm.unlink()
        print(f'Steam guide session screens: {len(list(OUT.glob("setup-steam-session-*.png")))} pictures')
        retry = (OUT / 'setup-installing-retry.log').read_text()
        if 'setup: install starts' not in retry or 'setup: title folder check: phase 0 problem 2' not in retry:
            raise AssertionError('Try again after a folder without pieces did not start the install')
        print(f'Setup pictures: {OUT}')
    finally:
        if backup is None:
            script.unlink(missing_ok=True)
        else:
            script.write_bytes(backup)


if __name__ == '__main__':
    main()
