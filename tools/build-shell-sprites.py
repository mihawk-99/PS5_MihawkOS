#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell's fixed chrome as the approved prototype's own pixels: Explorer's taskbar (patch 1140) and Start.

themes/windows11/taskbar/{rest,open}-{light,dark}.json hold each element of the prototype's taskbar captured alone over
black and over white (tools/measure-prototype-sprites.mjs, from prototypes/windows11-shell at 1920 x 1080):

  rest: node tools/measure-prototype-sprites.mjs <out> - <theme> search=.tb-searchbox
        taskview=.tb-taskview explorer=.tb-app:nth-of-type(1) settings=.tb-app:nth-of-type(2)
        notepad=.tb-app:nth-of-type(3) terminal=.tb-app:nth-of-type(4) chevron=.tb-chevron status=.tb-status
        browser=.tb-app:nth-of-type(5) (MihawkBrowser, the browser branch's: only it was added, from a 2026-10-17
        capture; the others stay the four-pin capture's, within one level of a five-pin one)
        'plate-hover=.tb-app:nth-of-type(2)!.tb-glyph,.tb-pill~hover' 'plate-press=.tb-app:nth-of-type(2)!.tb-glyph,.tb-pill~press'
        search-hover=.tb-searchbox~hover
  open: node tools/measure-prototype-sprites.mjs <out> explorer,notepad <theme> 'plate-active=.tb-app.is-active!.tb-glyph,.tb-pill'
        'pill-active=.tb-app.is-active .tb-pill' 'pill-running=.tb-app.is-running:not(.is-active) .tb-pill'

themes/windows11/start/rest-{light,dark}.json hold Start's (named start-* here):

  node tools/measure-prototype-sprites.mjs <out> start <theme> 'search=.st-panel .st-search'
        'head-pinned=.st-main > .st-head' 'head-recommended=.st-main > .st-pinned + .st-head'
        'icon-explorer=.st-pin:nth-child(1) .st-pin-icon' 'icon-settings=.st-pin:nth-child(2) .st-pin-icon'
        'icon-notepad=.st-pin:nth-child(3) .st-pin-icon' 'icon-terminal=.st-pin:nth-child(5) .st-pin-icon'
        'icon-browser=.st-pin:nth-child(6) .st-pin-icon' (added alone, 2026-10-17)
        'icon-image=.st-rec-item:nth-child(1) .icon' 'icon-document=.st-rec-item:nth-child(3) .icon'
        'foot=.st-foot!.st-user,.st-power' 'user=.st-user!.st-user-name' 'power=.st-power'
  all-{theme}.json:  ... start,start-allapps <theme> 'head-all=.st-sub > .st-head'
  find-{theme}.json (named start-find-*): ... search <theme> 'box=.sr-box!.sr-input' 'tabs=.sr-tabs'
        'group-top=.sr-home > .sr-group' 'group-recent=.sr-cols > .sr-col:nth-child(1) > .sr-group'
        'group-quick=.sr-cols > .sr-col:nth-child(2) > .sr-group' 'tile=.sr-tile!.icon,.sr-tile-name'
        'quick-N=.sr-cols > .sr-col:nth-child(2) > .sr-row:nth-child(N+1)' (N 1-4)
        'row-image=.sr-cols > .sr-col:nth-child(1) > .sr-row:nth-child(2) .sr-row-icon'
        'row-document=.sr-cols > .sr-col:nth-child(1) > .sr-row:nth-child(4) .sr-row-icon'

themes/windows11/flyouts holds the taskbar's flyouts (named flyout-*), PROTOTYPE_SETUP putting the prototype in the
console's state first:

  quicksettings-{theme}.json: ... quicksettings <theme> 'tile-on=.qs-tile.on .qs-tile-btns!.qs-tile-btn:first-child .icon'
        'glyph-wifi=.qs-tile.on .qs-tile-btn:first-child .icon' 'footer=.qs-footer!button' 'settings=.qs-footer > button:last-child'
  quicksettings-off-{theme}.json (PROTOTYPE_SETUP="Shell.set('wifi', false)"): ... quicksettings <theme>
        'tile-off=.qs-tile:first-child .qs-tile-btns!.qs-tile-btn:first-child .icon'
        'glyph-wifi-off=.qs-tile:first-child .qs-tile-btn:first-child .icon'
  quicksettings-wired-{theme}.json (PROTOTYPE_SETUP replaces the on-tile's glyph with Shell.icon('fluent/globe'), a
        wired adapter's): ... quicksettings <theme> 'glyph-network=.qs-tile.on .qs-tile-btn:first-child .icon'
  notifications-{theme}.json (PROTOTYPE_SETUP="document.querySelector('.nc-header .fly-icon-btn')?.click()", no
        notifications): ... notifications <theme> 'nc-header=.nc-header!.fly-icon-btn' 'nc-empty=.nc-list'
        'cal-toggle=.cal-toggle' 'cal-up=.cal-nav > button:nth-of-type(2)' 'cal-down=.cal-nav > button:nth-of-type(3)'
        'cal-dow=.cal-grid!.cal-day@334x40'

Over black a pixel is its premultiplied colour; white minus black is 255 minus its alpha (the same in every channel:
Chromium's chrome text is greyscale here, which the build checks). Written as premultiplied 32-bit BGRA, as
GdiAlphaBlend takes it, to .deps/windows11-theme/pw_shell_sprites.h. Composed over the bar the prototype's own way,
95.6% of the channels equal its capture and 99.4% are within one level (the rest is Chromium's own layer rounding).
"""
import base64
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'themes/windows11'
OUTPUT = ROOT / '.deps/windows11-theme/pw_shell_sprites.h'
# (folder, its capture files, sprite names, the prefix their enum names take)
SETS = [('taskbar', ('rest', 'open'), ['search', 'search-hover', 'taskview', 'explorer', 'settings',
                                       'notepad', 'terminal', 'browser', 'chevron', 'status', 'plate-hover', 'plate-press',
                                       'plate-active', 'pill-active', 'pill-running'], ''),
        ('start', ('rest',), ['search', 'head-pinned', 'head-recommended', 'icon-explorer', 'icon-settings', 'icon-notepad',
                              'icon-terminal', 'icon-browser', 'icon-image', 'icon-document', 'foot', 'user', 'power'], 'start-'),
        ('start', ('all',), ['head-all'], 'start-'),
        ('start', ('find',), ['box', 'tabs', 'group-top', 'group-recent', 'group-quick', 'tile', 'quick-1', 'quick-2',
                              'quick-3', 'quick-4', 'row-image', 'row-document'], 'start-find-'),
        ('flyouts', ('quicksettings',), ['tile-on', 'glyph-wifi', 'footer', 'settings'], 'flyout-'),
        ('flyouts', ('quicksettings-off',), ['tile-off', 'glyph-wifi-off'], 'flyout-'),
        ('flyouts', ('quicksettings-wired',), ['glyph-network'], 'flyout-'),
        ('flyouts', ('notifications',), ['nc-header', 'nc-empty', 'cal-toggle', 'cal-up', 'cal-down', 'cal-dow'], 'flyout-'),
        ('flyouts', ('quicksettings-grid',), ['plate-split-on', 'plate-simple-off', 'glyph-bluetooth', 'glyph-airplane',
                                               'glyph-battery-saver', 'glyph-moon', 'glyph-accessibility', 'glyph-cast',
                                               'glyph-phone', 'glyph-chevron', 'slider-brightness', 'slider-volume',
                                               'battery', 'edit'], 'flyout-')]
NAMES = [prefix + name for _, _, names, prefix in SETS for name in names]


def premultiplied(sprite):
    """BGRA words, premultiplied, row by row; alpha is the mean of the channels' (they agree to a level)."""
    black, white = base64.b64decode(sprite['black']), base64.b64decode(sprite['white'])
    words = []
    for i in range(sprite['w'] * sprite['h']):
        alphas = [255 - (white[i * 3 + k] - black[i * 3 + k]) for k in range(3)]
        if max(alphas) - min(alphas) > 2:
            raise ValueError(f'pixel {i}: per-channel alpha {alphas} (subpixel text?) cannot be one AlphaBlend alpha')
        alpha = max(0, min(255, round(sum(alphas) / 3)))
        red, green, blue = (min(alpha, black[i * 3 + k]) for k in range(3))
        words.append(alpha << 24 | red << 16 | green << 8 | blue)
    return words


def main():
    themes = []
    for theme in ('light', 'dark'):
        sprites = {}
        for folder, parts, names, prefix in SETS:
            for part in parts:
                found = json.loads((SOURCE / folder / f'{part}-{theme}.json').read_text())['sprites']
                sprites.update((prefix + name, sprite) for name, sprite in found.items() if name in names)
        missing = [name for name in NAMES if name not in sprites]
        if missing:
            raise ValueError(f'{theme}: no measured sprite for {missing}')
        themes.append(sprites)
    lines = ['/* Generated by tools/build-shell-sprites.py from themes/windows11/{taskbar,start}: do not edit. */',
             '#ifndef PW_SHELL_SPRITES_H', '#define PW_SHELL_SPRITES_H',
             'struct pw_shell_sprite { int width, height; const unsigned int *bgra; };',
             'enum {' + ', '.join(f'PW_SPRITE_{n.upper().replace("-", "_")}' for n in NAMES) + ', PW_SPRITE_COUNT};']
    for theme, sprites in zip(('light', 'dark'), themes):
        for name in NAMES:
            words = premultiplied(sprites[name])
            body = ',\n'.join(','.join(f'0x{w:08x}' for w in words[i:i + 12]) for i in range(0, len(words), 12))
            lines.append(f'static const unsigned int pw_sprite_{theme}_{name.replace("-", "_")}[] = {{\n{body}}};')
    lines.append('static const struct pw_shell_sprite pw_shell_sprites[2][PW_SPRITE_COUNT] = {')
    for theme, sprites in zip(('light', 'dark'), themes):
        lines.append('    {' + ', '.join(f'{{{sprites[n]["w"]}, {sprites[n]["h"]}, pw_sprite_{theme}_{n.replace("-", "_")}}}'
                                         for n in NAMES) + '},')
    lines += ['};', '#endif', '']
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    text = '\n'.join(lines)
    if not OUTPUT.exists() or OUTPUT.read_text() != text:
        OUTPUT.write_text(text)
    print(json.dumps({'sprites': len(NAMES), 'themes': 2, 'header': str(OUTPUT.relative_to(ROOT))}))


if __name__ == '__main__':
    main()
