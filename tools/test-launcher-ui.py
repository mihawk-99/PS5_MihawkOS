#!/usr/bin/env python3
"""Host UI checks after ps5/tools/host-reference.sh; --render also exercises the picker.
Copyright (C) 2026 Mihawk
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/ps5-host'
APP = BUILD / 'app'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render', action='store_true')
    args = parser.parse_args()
    subprocess.run(['ninja', '-C', str(BUILD), 'ps5-proton-ui-test', 'ps5-samples-host'], check=True,
                   stdout=subprocess.DEVNULL)
    preference = APP / 'hui/theme.txt'
    script = APP / 'test-run.txt'
    request, marker, notices = APP / 'wine-run.txt', APP / 'session-running', APP / 'session-notices'
    backup = {p: p.read_bytes() if p.exists() else None for p in (preference, script, request, marker, notices)}
    out = ROOT / 'klog/ui-host'
    out.mkdir(parents=True, exist_ok=True)
    try:
        preference.unlink(missing_ok=True)
        result = subprocess.run([str(BUILD / 'ps5-proton-ui-test')], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert result.stderr.count('to=ui-test cycle=7 reason=ui-test') == 1, result.stderr
        assert 'to=changed' not in result.stderr
        subprocess.run([str(BUILD / 'ps5-proton-ui-test'), 'code'], check=True)
        preference.write_text('not-a-theme\n')
        subprocess.run([str(BUILD / 'ps5-proton-ui-test'), 'gloss'], check=True)
        print(result.stdout.strip())
        print('Fresh-process reload, corrupt preference fallback, deferred handoff: PASS')
        if args.render:
            os.environ['PROTON_WEBUI_ADDRESS'] = '192.168.x.y'   # the QR code names a made-up address, never this PC's
            # All thirty rows, including scrolling; no game can launch in a frame-budget run.
            presses = [(2, 'square')]
            presses += [(4 + i * 2, 'up') for i in range(4)]  # Gloss is row 4
            for i in range(30):
                presses.append((20 + i * 4, 'cross'))
                if i != 29:
                    presses.append((22 + i * 4, 'down'))
            def render(name, frames, events, files={}):
                script.write_text(f'frames {frames}\nscreenshot\nsamples ps5proton\n' +
                                  ''.join(f'hold ps5proton {e[0]} {e[1]} {e[2]}\n' if len(e) == 3 else
                                          f'press ps5proton {e[0]} {e[1]}\n' for e in events))
                for path, text in files.items():
                    path.write_text(text)
                run = subprocess.run([str(BUILD / 'ps5-samples-host')], capture_output=True, text=True, timeout=120)
                log = run.stdout + run.stderr
                (out / f'{name}.log').write_text(log)
                assert run.returncode == 0 and 'ends: status 0' in log, log[-2000:]
                assert 'sample ps5proton: ok' in log
                from PIL import Image, ImageStat
                image = Image.open(APP / 'screenshots/ps5proton.ppm').convert('RGB')
                assert max(ImageStat.Stat(image).stddev) > 2
                image.save(out / f'{name}.png')
                return log
            log = render('all-themes', 180, presses)
            selected = [line.split('launcher theme: ')[1] for line in log.splitlines() if 'launcher theme: ' in line]
            assert len(selected) == len(set(selected)) == 30, selected
            assert preference.read_text() == 'not-a-theme\n', 'render tests changed preference'
            render('gloss-picker', 100, [(2, 'square')])
            render('preview-cancel', 100, [(2, 'square'), (4, 'down'), (6, 'circle')])
            render('apply-return', 100, [(2, 'square'), (4, 'down'), (6, 'cross'), (8, 'circle')])
            print(f'Picker: all 30 themes selected; preview cancel and apply/return rendered. Pictures: {out}')
            # The launcher's reasons (docs/SELF_INSTALL.md, phase 1). The PC build has no /data: every render shows
            # that reason under Mihawk OS; these add a failed session's reason and a session that never came back.
            log = render('session-ended', 30, [],
                         {request: 'cycle=1\nended=Wine: requested profile gate is not in the library\n'})
            assert 'launcher: Last session ended: Wine: requested profile gate is not in the library' in log
            assert 'launcher: games cannot start: No access to /data:' in log
            assert not request.exists(), 'the request is one-shot'
            log = render('session-crashed', 30, [], {marker: 'Mihawk OS'})
            assert 'launcher: The last session (Mihawk OS) did not end normally' in log
            assert not marker.exists(), 'the launcher consumes the marker'
            # What a session said in its notifications (native/proton_wine.cpp notices), shown again after it.
            log = render('session-notices', 30, [], {notices: 'A program could not start (errno 5). Restart the PS5.\n'
                                                              'A test notice: notices show here.\n'})
            assert ('launcher: During the last session: A program could not start (errno 5). Restart the PS5. '
                    'A test notice: notices show here.') in log
            assert not notices.exists(), 'the launcher consumes the notices'
            print(f'Launcher reasons: a failed session, a crashed one and no /data rendered. Pictures: {out}')
            home_grid(render)
            checklist_skip(render)
            steam_warning(render)
            # Settings > Check for updates (docs/SELF_INSTALL.md, phase 8): row 6. A frame-budget run asks no network.
            # Settings is the main menu's dock tile under Mihawk OS: DOWN, then CROSS.
            settings = [(2, 'down'), (4, 'cross')]
            log = render('updates-panel', 60, settings + [(6 + i * 2, 'down') for i in range(6)] + [(20, 'cross')])
            assert 'update:' not in log, 'a scripted run must not check for updates'
            print(f'Updates panel rendered. Pictures: {out}')
            # Settings > Logging (src/pw_detailed_logs.h): row 7, off by default; a scripted press previews it, saving nothing.
            log = render('logging-panel', 60, settings + [(6 + i * 2, 'down') for i in range(7)] + [(22, 'cross')])
            assert 'Logging: preview on' in log, log[-2000:]
            assert not (APP / 'logging.txt').exists(), 'a scripted run must not save Logging'
            print(f'Logging panel rendered. Pictures: {out}')
    finally:
        for path, content in backup.items():
            if content is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(content)


def footer_margins(picture):
    """The hint row's left and right margins on the screen: its dark glyphs and words (the ink is far darker than the sky,
    the grass and their bubbles) in the band the row is drawn in."""
    from PIL import Image
    image = Image.open(picture).convert('L')
    xs = [x for x in range(image.width) for y in range(1024, 1062, 2) if image.getpixel((x, y)) < 90]
    return min(xs), image.width - 1 - max(xs)


def steam_warning(render):
    """The Steam memory warning (src/pw_steam_memory.h): before Steam installs or starts, a census whose room under the
    1,800 MiB swap point is less than Steam's measured need shows what to do first; CIRCLE goes back, TRIANGLE held 5 s
    goes on. The PC has no such census: PROTON_MEMORY_TEST="system MiB,payloads MiB,payloads" makes one up."""
    tile = [(10, 'right'), (14, 'cross')]   # Steam's tile, from Mihawk OS
    def says(log, *lines):
        return [line for line in lines if line not in log]
    try:
        os.environ['PROTON_MEMORY_TEST'] = '1400,74,9'
        log = render('steam-warning', 40, tile)
        assert not says(log, "steam memory: warned before Steam's tile: 1400 MiB", 'Steam needs about 500 MiB more; the swap point is 1800'), log[-2000:]
        log = render('steam-warning-back', 40, tile + [(24, 'circle')])
        assert 'steam memory: the player went back' in log and 'went on' not in log
        log = render('steam-warning-let-go', 260, tile + [(20, 'triangle'), (20, 200, 'triangle')])
        assert re.search(r'steam memory: hold let go at [23]\.\d s', log) and 'went on' not in log, log[-2000:]
        log = render('steam-warning-hold', 420, tile + [(20, 'triangle'), (20, 340, 'triangle')])
        assert 'steam memory: the player held TRIANGLE 5 s and went on' in log, log[-2000:]
        # Files' steam.exe asks the same, and CIRCLE goes back to Files
        drive = APP / 'data/prefixes/mihawk/drive_c/Steam'
        drive.mkdir(parents=True)
        (drive / 'steam.exe').write_bytes(b'x' * 4096)
        log = render('steam-warning-files', 60, [(6, 'down'), (8, 'right'), (10, 'cross'), (14, 'cross'), (18, 'cross'), (26, 'circle')])
        assert "steam memory: warned before Files' steam.exe" in log and 'the player went back' in log, log[-2000:]
        # The setup's Install Steam (Done, step 6) asks it too
        os.environ['PROTON_SETUP_TEST'] = '6'
        log = render('steam-warning-setup', 40, [(10, 'cross')])
        assert "steam memory: warned before the setup's Install Steam" in log, log[-2000:]
        os.environ.pop('PROTON_SETUP_TEST')
        # Room enough (1,300 + 500 = 1,800 fits): no warning; Steam's own path runs
        os.environ['PROTON_MEMORY_TEST'] = '1300,54,6'
        log = render('steam-warning-fits', 30, tile)
        assert 'steam memory: 1300 MiB' in log and 'it fits under 1800' in log and 'warned' not in log, log[-2000:]
        # No census (no /data, or no figure): never warn
        os.environ.pop('PROTON_MEMORY_TEST')
        log = render('steam-warning-none', 30, tile)
        assert "steam memory: no census before Steam's tile: no warning" in log and 'warned' not in log
        # A payload counted as extra adds its step: through the checklist first (held 5 s), then Steam's tile
        os.environ.update(PROTON_MEMORY_TEST='1400,80,10', PROTON_PAYLOAD_TEST='fail')
        log = render('steam-warning-payloads', 420, [(10, 'triangle'), (10, 330, 'triangle'), (350, 'right'), (354, 'cross')])
        assert 'checklist: skipped by the player' in log and "steam memory: warned before Steam's tile" in log, log[-2000:]
        os.environ.pop('PROTON_PAYLOAD_TEST')
        os.environ.update(PROTON_MEMORY_TEST='1400,74,9', PROTON_SETUP_LANGUAGE='ja-JP')
        log = render('steam-warning-ja', 40, tile)
        assert "steam memory: warned before Steam's tile" in log and re.search(r'home: language ja-JP, .* 0 characters missing', log)
    finally:
        for name in ('PROTON_MEMORY_TEST', 'PROTON_PAYLOAD_TEST', 'PROTON_SETUP_TEST', 'PROTON_SETUP_LANGUAGE'):
            os.environ.pop(name, None)
        shutil.rmtree(APP / 'data', ignore_errors=True)
    print(f'Steam memory warning: warns at 1400 MiB (back, a let-go, a 5 s hold that goes on), from the tile, Files\' '
          f'steam.exe and the setup\'s Install Steam; none at 1300 or without a census; the extra-payloads step; en and ja. '
          f'Pictures: {ROOT / "klog/ui-host"}')


def checklist_skip(render):
    """The payload checklist's way through a fail (src/pw_payload_check.h, the owner, 2026-10-31): TRIANGLE held 5 s goes on
    to the menu, which says so; let go sooner, nothing happens; the next start checks again. The PC has no payloads:
    PROTON_PAYLOAD_TEST=fail gives the real check a made-up table without Kstuff and with one payload too many. A scripted
    frame is 1/60 s."""
    os.environ['PROTON_PAYLOAD_TEST'] = 'fail'
    try:
        log = render('checklist-skip', 420, [(10, 'triangle'), (10, 330, 'triangle')])
        assert 'launcher: the payload checklist refuses entry' in log, log[-2000:]
        skip = re.findall(r'checklist: (skipped by the player .*)', log)
        assert skip == ['skipped by the player after 5 s hold; missing: Kstuff Lite 1.11 Beta or later; extra: extra.elf'], skip
        assert 'launcher: the player went on past the payload checklist' in log and 'home: language en-US' in log
        # let go after 4 s: still on the checklist, nothing skipped; the skip above was not remembered (it shows again)
        log = render('checklist-let-go', 300, [(10, 'triangle'), (10, 250, 'triangle')])
        assert 'launcher: the payload checklist refuses entry' in log and 'skipped by the player' not in log
        let_go = re.findall(r'checklist: hold let go at ([\d.]+) s', log)
        assert len(let_go) == 1 and 3.9 <= float(let_go[0]) < 5.0, let_go
        assert 'home: language' not in log, 'the menu showed without a pass or a skip'
        render('checklist-hint', 40, [])
        os.environ['PROTON_SETUP_LANGUAGE'] = 'ja-JP'
        render('checklist-hint-ja', 40, [])
        log = render('checklist-skip-ja', 420, [(10, 'triangle'), (10, 330, 'triangle')])
        assert 'home: language ja-JP, face noto-sans-jp' in log and re.search(r'\d+ strings, 0 characters missing', log)
    finally:
        os.environ.pop('PROTON_PAYLOAD_TEST', None)
        os.environ.pop('PROTON_SETUP_LANGUAGE', None)
    print(f'Payload checklist: TRIANGLE held 5 s goes on (logged with what failed), let go at 4 s does not, the next start '
          f'checks again; hint in en and ja. Pictures: {ROOT / "klog/ui-host"}')


def home_grid(render):
    """The main menu (examples/ps5proton/home_grid.hpp, src/pw_home_grid.h): fourteen made-up entries in the title
    folder's games/ make three pages beside Mihawk OS, Steam and the QR code; a made-up drive_c beside the app is what
    Files shows (the PC's reference build reads its own data folder). Both are removed afterwards."""
    catalog, data = APP / 'games', APP / 'data'
    names = ['A Quiet Garden', 'Brick Runner', 'Copper Skies', 'Glass Harbor', 'Lantern Hollow', 'Moss Knight', 'Night Ferry',
             'Orbit Racer', 'Paper Planets', 'Sunlit Rails', 'Tidewater', 'Velvet Peaks', 'Willow Lane', 'Zephyr Tactics']
    assert not catalog.exists() and not data.exists(), 'the menu checks need the app folder without games/ and data/'
    try:
        for name in names:
            entry = catalog / name.lower().replace(' ', '-')
            entry.mkdir(parents=True)
            (entry / 'game.json').write_text('{"name": "%s", "detail": "Windows"}\n' % name)
        drive = data / 'prefixes/mihawk/drive_c'
        for folder in ('Games/Tide Harbor', 'Games/Empty', 'Program Files', 'users/Public'):
            (drive / folder).mkdir(parents=True)
        for file, size in (('Games/Tide Harbor/TideHarbor.exe', 3 << 20), ('Games/Tide Harbor/setup.exe', 1 << 20),
                           ('Games/Tide Harbor/readme.txt', 100), ('notes.txt', 10)):
            (drive / file).write_bytes(b'x' * size)
        (drive / 'Games/out').symlink_to('/etc')   # a link out of drive_c is not listed

        # The D-pad: RIGHT to Steam, DOWN to the game under it, DOWN into the dock (Files under it), UP back, RIGHT; then
        # the left stick moves the pointer (shown from its first push) over the QR code's tile, which takes the focus
        log = render('home-grid', 60, [(10, 'right'), (14, 'down'), (18, 'down'), (22, 'up'), (26, 'right')] +
                     [(30 + i, 'stick-right') for i in range(12)])
        moves = re.findall(r'home: focus on ([^\n]*)', log)
        assert moves[:5] == ['Steam (page 1)', 'game 1 (page 1)', 'Files (page 1)', 'game 1 (page 1)', 'game 2 (page 1)'], moves
        assert moves[5:] == ['the Web UI by the pointer (page 1)'], moves
        # L1/R1: three pages, each's first tile focused; nothing past the last; RIGHT from the QR code is the next page
        log = render('home-pages', 50, [(10, 'r1'), (14, 'r1'), (18, 'r1'), (22, 'l1'), (26, 'l1'), (30, 'right'),
                                        (34, 'right'), (38, 'right'), (42, 'right'), (46, 'l1'), (48, 'r1')])
        moves = re.findall(r'home: focus on ([^\n]*)', log)
        assert moves == ['game 3 (page 2)', 'game 13 (page 3)', 'game 3 (page 2)', 'Mihawk OS (page 1)', 'Steam (page 1)',
                         'game 0 (page 1)', 'the Web UI (page 1)', 'game 3 (page 2)', 'Mihawk OS (page 1)',
                         'game 3 (page 2)'], moves
        # TRIANGLE: a game's Options, never Mihawk OS's or Steam's; a scripted run previews each change (nothing saved)
        before = {path: path.read_bytes() for path in catalog.glob('*/game.json')}
        log = render('home-options', 60, [(6, 'triangle'), (10, 'right'), (12, 'triangle'), (16, 'right'), (20, 'triangle'),
                                          (24, 'cross'), (28, 'down'), (32, 'cross'), (36, 'cross'), (40, 'cross'),
                                          (44, 'down'), (48, 'cross')])
        assert re.findall(r'options: (.*)', log) == [
            'A Quiet Garden', 'A Quiet Garden preview WINE_PS5_GAME_MODE=1', 'A Quiet Garden preview WINE_PS5_CONTROLLER=xbox-360',
            'A Quiet Garden preview WINE_PS5_CONTROLLER=xbox-one', 'A Quiet Garden preview WINE_PS5_CONTROLLER=dualsense',
            'A Quiet Garden preview WINE_PS5_TITLE_GAMES=0'], log[-3000:]
        assert before == {path: path.read_bytes() for path in catalog.glob('*/game.json')}, 'a scripted run saved Options'
        render('home-options-back', 30, [(6, 'right'), (8, 'right'), (10, 'triangle'), (14, 'circle')])
        # Files from the dock: the root (folders first, the link out and notes.txt left out), Games, an empty folder and
        # back, Tide Harbor, a program's launch request (refused on the PC, which has no /data; a scripted run starts
        # nothing), then CIRCLE up to the root and back to the menu. Nothing is written to drive_c.
        log = render('home-files', 80, [(6, 'down'), (8, 'right'), (10, 'cross'), (14, 'cross'), (18, 'cross'),
                                        (22, 'circle'), (26, 'down'), (30, 'cross'), (34, 'down'), (38, 'cross'),
                                        (44, 'circle'), (48, 'circle'), (52, 'circle')])
        assert re.findall(r'files: (.*)', log) == [
            'C:\\: 3 shown, 1 others; Only programs are shown. Other files here: 1',
            'C:\\Games: 2 shown, 0 others',
            'C:\\Games\\Empty: 0 shown, 0 others; This folder is empty.',
            'C:\\Games: 2 shown, 0 others',
            'C:\\Games\\Tide Harbor: 2 shown, 1 others; Only programs are shown. Other files here: 1',
            'run TideHarbor.exe (profile=mihawk-os path=C:\\Games\\Tide Harbor\\TideHarbor.exe) refused: No access to '
            '/data: this is the PC build, which runs no games. (a scripted run starts nothing)',
            'C:\\Games: 2 shown, 0 others', 'C:\\: 3 shown, 1 others; Only programs are shown. Other files here: 1'], log[-3000:]
        assert 'home: focus on Files (page 1)' in log and not (APP / 'wine-run.txt').exists()
        assert sorted(p.name for p in drive.rglob('*')) == sorted(['Games', 'Tide Harbor', 'TideHarbor.exe', 'setup.exe',
            'readme.txt', 'Empty', 'out', 'Program Files', 'users', 'Public', 'notes.txt']), 'Files changed drive_c'
        # Every language of the setup's table (src/pw_setup.h): the menu in the language chosen there (a scripted run previews
        # PROTON_SETUP_LANGUAGE), Chinese and Japanese in their own face, loaded only for them; every word the menu, Files
        # and Options draw is in the face it is drawn in (the launcher checks each against the face's glyphs)
        files_presses = [(6, 'down'), (8, 'right'), (10, 'cross'), (14, 'cross'), (18, 'down'), (22, 'cross')]
        try:
            for tag in ('en-US', 'fr-FR', 'es-ES', 'de-DE', 'it-IT', 'ru-RU', 'zh-CN', 'ja-JP', 'pt-BR', 'pl-PL'):
                os.environ['PROTON_SETUP_LANGUAGE'] = tag
                # Settings (since settings-i18n): Game Mode's panel, and About's licence notices in every language
                for picture, frames, presses in (('menu', 24, []), ('files', 40, files_presses),
                                                 ('settings', 30, [(6, 'down'), (8, 'cross'), (12, 'down')]),
                                                 ('about', 30, [(6, 'down'), (8, 'cross'), (12, 'down'), (14, 'down')]),
                                                 ('themes', 30, [(6, 'square'), (10, 'down')])):
                    if picture in ('files', 'settings', 'themes') and tag not in ('en-US', 'fr-FR', 'ru-RU', 'zh-CN', 'ja-JP'):
                        continue
                    log = render(f'home-{tag}-{picture}', frames, presses)
                    check = re.search(r'home: language ([\w-]+), face ([^,]+), (\d+) strings, (\d+) characters missing', log)
                    assert check and check.group(1) == tag and check.group(4) == '0', (tag, check and check.group(0))
                    face = 'noto-sans-sc' if tag == 'zh-CN' else 'noto-sans-jp' if tag == 'ja-JP' else "the kit's"
                    assert check.group(2) == face, (tag, check.group(0))
                    assert 'pictures in assets/launcher' not in log, 'a menu picture is missing'   # all eight load
                    themes = re.search(r'home: themes: (\d+) of (\d+) in the table, (\d+) whose English differs', log)
                    assert themes and themes.group(1) == themes.group(2) and themes.group(3) == '0', themes and themes.group(0)
                    loaded = re.search(r'home: face \S+ loaded: .*', log)
                    assert bool(loaded) == (face != "the kit's"), (tag, loaded and loaded.group(0))
                    if picture in ('menu', 'files'):   # the owner: the footer's hints centred under the dock, on every menu screen
                        left, right = footer_margins(ROOT / f'klog/ui-host/home-{tag}-{picture}.png')
                        # (the row's box is centred exactly; a font's last letter can end 1-2 px inside its own advance)
                        assert abs(left - right) <= 3 and left >= 60, (tag, picture, left, right)
                    if picture == 'menu':
                        print(f'  {tag}: {check.group(3)} strings, 0 missing in {face}' + (f'; {loaded.group(0)[6:]}' if loaded else '')
                              + f'; hints centred ({left} | {right} px)')
        finally:
            os.environ.pop('PROTON_SETUP_LANGUAGE', None)
        # No C: drive yet: the page says so in plain words, and CIRCLE still goes back
        shutil.rmtree(data / 'prefixes')
        log = render('home-files-none', 30, [(6, 'down'), (8, 'right'), (10, 'cross')])
        assert 'C: drive is not made yet. Start Mihawk OS once' in log, log[-2000:]
        print(f'Main menu: D-pad, pointer, pages, dock, Options (preview), Files (listing, guard, launch request) and Settings in '
              f'ten languages. Pictures: {ROOT / "klog/ui-host"}')
    finally:
        shutil.rmtree(catalog, ignore_errors=True)
        shutil.rmtree(data, ignore_errors=True)


if __name__ == '__main__':
    main()
