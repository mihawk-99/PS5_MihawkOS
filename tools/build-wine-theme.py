#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the editable Wine theme with the project's existing resource compiler.

Original Fluent Light inputs are pinned in themes/windows11/reference.json.
Only LGPL resources enter this module; wallpaper and Microsoft fonts stay local.
"""
import hashlib
import io
import importlib.util
import json
import re
import struct
import subprocess
from pathlib import Path
import pefile
from PIL import Image
import pw_caption_buttons

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'themes/windows11/source'
OUTPUT = ROOT / '.deps/windows11-theme'
WALLPAPER = ROOT / 'themes/windows11/wallpapers/skeuoss-bubble-city.jpg'  # the desktop's default (NOTICE.md)


def rgb(value):
    return tuple(bytes.fromhex(value.removeprefix('#')))


# System colours as Windows 11 sets them for classic programs (the prototype's controls gallery; appearance.json
# "classic"), except the caption, which is the shell's (its bar under the caption buttons, tools/pw_caption_buttons.py).
SYSTEM_ROLES = {'Window': 'classic.window', 'Menu': 'classic.menu', 'MenuBar': 'classic.menu_bar', 'InfoBk': 'classic.window',
                'ActiveCaption': 'mica_fallback', 'InactiveCaption': 'caption_inactive',
                'GradientActiveCaption': 'mica_fallback', 'GradientInactiveCaption': 'caption_inactive',
                'CaptionText': 'text', 'InactiveCaptionText': 'caption_text_inactive',
                'BtnFace': 'classic.face', 'WindowFrame': 'classic.frame', 'BtnShadow': 'classic.shadow',
                'DkShadow3d': 'classic.dark_shadow', 'Light3d': 'classic.light3d', 'BtnHighlight': 'classic.window',
                'ActiveBorder': 'classic.active_border', 'InactiveBorder': 'classic.inactive_border', 'GrayText': 'classic.gray',
                'WindowText': 'classic.text', 'MenuText': 'classic.text', 'BtnText': 'classic.text', 'InfoText': 'classic.text',
                'Highlight': 'classic.highlight', 'MenuHilight': 'classic.highlight', 'HighlightText': 'classic.highlight_text',
                'HotTracking': 'classic.hot_light', 'Scrollbar': 'classic.window'}
# The parts the gallery names (section, property): what Windows 11 draws there, not a guess from the name.
PART_ROLES = {
    ('[Button.Pushbutton]', 'TextColor'): 'classic.text', ('[Button.Pushbutton(Pressed)]', 'TextColor'): 'classic.text',
    ('[Button.Pushbutton(Disabled)]', 'TextColor'): 'classic.button_disabled_text',
    ('[Button.Groupbox]', 'TextColor'): 'classic.text',
    ('[Button.Commandlink]', 'TextColor'): 'classic.link', ('[Button.Commandlink(Hot)]', 'TextColor'): 'classic.link_hot',
    ('[Button.Commandlink(Pressed)]', 'TextColor'): 'classic.link', ('[Button.Commandlink(Disabled)]', 'TextColor'): 'classic.gray',
    ('[Button.PushButtonDropDown(Normal)]', 'TextColor'): 'classic.text',
    ('[Button.PushButtonDropDown(Disabled)]', 'TextColor'): 'classic.button_disabled_text',
    ('[ComboBox]', 'FillColor'): 'classic.window', ('[ComboBox]', 'BorderColor'): 'classic.edit_border',
    ('[ComboBox.ReadOnly(Disabled)]', 'TextColor'): 'classic.gray', ('[ComboBox.CueBanner]', 'TextColor'): 'classic.gray',
    ('[ComboBox.DropDownItem(Normal)]', 'FillColor'): 'classic.window', ('[ComboBox.DropDownItem(Normal)]', 'TextColor'): 'classic.text',
    ('[ComboBox.DropDownItem(HighLighted)]', 'FillColor'): 'classic.highlight',
    ('[ComboBox.DropDownItem(HighLighted)]', 'TextColor'): 'classic.highlight_text',
    ('[Edit]', 'FillColor'): 'classic.window', ('[Edit]', 'BorderColor'): 'classic.edit_border',
    ('[Edit.Edittext(Disabled)]', 'FillColor'): 'classic.edit_disabled', ('[Edit.Edittext(Disabled)]', 'TextColor'): 'classic.gray',
    ('[Edit.Edittext(ReadOnly)]', 'FillColor'): 'classic.edit_disabled', ('[Edit.Edittext(ReadOnly)]', 'TextColor'): 'classic.text',
    ('[Edit.EditText(CueBanner)]', 'TextColor'): 'classic.gray',
    ('[ListBox]', 'FillColor'): 'classic.window', ('[ListBox]', 'BorderColor'): 'classic.list_border',
    ('[ListView]', 'FillColor'): 'classic.window', ('[ListView]', 'BorderColor'): 'classic.list_border',
    ('[TreeView]', 'FillColor'): 'classic.window', ('[TreeView]', 'BorderColor'): 'classic.list_border',
    ('[Explorer::TreeView]', 'FillColor'): 'classic.window', ('[Explorer::TreeView]', 'BorderColor'): 'classic.list_border',
    ('[Menu.PopupItem(Hot)]', 'TextColor'): 'classic.highlight_text',
    ('[Progress]', 'FillColor'): 'classic.progress_bar', ('[Progress]', 'BorderColor'): 'classic.progress_border',
    ('[Progress.Bar]', 'FillColor'): 'classic.progress_bar', ('[Progress.BarVert]', 'FillColor'): 'classic.progress_bar',
    ('[Progress.Chunk]', 'FillColor'): 'classic.progress_fill', ('[Progress.ChunkVert]', 'FillColor'): 'classic.progress_fill',
    ('[Progress.Fill]', 'FillColor'): 'classic.progress_fill', ('[Progress.FillVert]', 'FillColor'): 'classic.progress_fill',
    ('[Progress.Fill(Error)]', 'FillColor'): 'classic.progress_error', ('[Progress.FillVert(Error)]', 'FillColor'): 'classic.progress_error',
    ('[Progress.Fill(Paused)]', 'FillColor'): 'classic.progress_paused', ('[Progress.FillVert(Paused)]', 'FillColor'): 'classic.progress_paused',
    ('[Progress.Fill(Partial)]', 'FillColor'): 'classic.progress_bar', ('[Progress.FillVert(Partial)]', 'FillColor'): 'classic.progress_bar',
    ('[TaskDialog.MainInstructionPane]', 'TextColor'): 'classic.link', ('[Tooltip.BallonTitle]', 'TextColor'): 'classic.link',
    ('[Tooltip.Standard]', 'TextColor'): 'classic.tooltip_text', ('[Tooltip.StandardTitle]', 'TextColor'): 'classic.text',
    ('[Window.Caption]', 'TextColor'): 'text',
}


def appearance_ini(text, palette, metrics, classic):
    """Reuse Wine's part/state definitions; change only appearance properties."""
    colour = lambda role: classic[role[8:]] if role.startswith('classic.') else palette[role]
    section = ''
    lines = []
    for line in text.splitlines():
        if line.startswith('['): section = line
        match = re.fullmatch(r'(\w+)\s*=\s*(\d+)\s+(\d+)\s+(\d+)', line)
        if match:
            prop = match[1]
            role = SYSTEM_ROLES.get(prop) if section == '[SysMetrics]' else PART_ROLES.get((section, prop))
            if not role:
                if 'Text' in prop or prop == 'GlyphColor':
                    role = 'classic.gray' if 'disabled' in section.lower() else 'classic.text'
                elif 'Border' in prop or 'Shadow' in prop: role = 'line'
                elif 'Fill' in prop or 'Light' in prop: role = 'classic.window'
            if role: line = prop + ' = ' + ' '.join(map(str, rgb(colour(role))))
        if line.startswith('CaptionBarHeight ='):
            line = 'CaptionBarHeight = ' + str(metrics['caption_height']) + '\nCaptionBarWidth = ' + str(metrics['caption_button_width'])
        lines.append(line)
    return '; Managed metrics schema 2: nonclient font sizes are points.\n' + '\n'.join(lines) + '\n'


GLYPH_FILES = (13, 19, 23, 26, 32, 39, 52)  # the theme's ImageFile1-7 names, for MinDpi 96 to 384
GLYPH_SIZES = (10, 13, 15, 20, 25, 30, 40)  # the prototype's 10-pixel glyph at each of those DPIs


def bmp32(rows):
    """A 32-bit BI_RGB bitmap with straight alpha, the form uxtheme detects and premultiplies."""
    pixels = b''.join(bytes((b, g, r, a)) for row in reversed(rows) for r, g, b, a in row)
    return (struct.pack('<2sIHHI', b'BM', 54 + len(pixels), 0, 0, 54) +
            struct.pack('<IiiHHIIiiII', 40, len(rows[0]), len(rows), 1, 32, 0, len(pixels), 2835, 2835, 0, 0) + pixels)


def caption_bitmaps(theme):
    """Window.CloseButton, MinButton, MaxButton and RestoreButton as the approved prototype draws them: uniform
    plates and its measured glyphs at 96 DPI (tools/pw_caption_buttons.py), each of the eight states. Larger DPIs
    scale the 96-DPI glyph. shortcut: only 96 DPI is measured; measure 125-400% with phase 8's scaling."""
    bitmaps = {}
    for kind in pw_caption_buttons.GLYPH:
        states = pw_caption_buttons.button_states(theme, kind)
        with Image.open(SOURCE / 'bitmaps' / f'blue_window_{kind}_button_background.bmp') as source:
            width, height = source.size
        bitmaps[f'window_{kind}_button_background.bmp'] = bmp32(
            [[plate] * width for plate, _ in states for _ in range(height // 8)])
        for name, size in zip(GLYPH_FILES, GLYPH_SIZES):
            rows = []
            for _, glyph in states:
                if size == 10:
                    rows += [list(row) for row in glyph]
                    continue
                alpha = Image.new('L', (10, 10))
                alpha.putdata([pixel[3] for row in glyph for pixel in row])
                alpha = alpha.resize((size, size), Image.Resampling.LANCZOS)
                ink = max((pixel for row in glyph for pixel in row), key=lambda pixel: pixel[3])[:3]
                rows += [[(*ink, alpha.getpixel((x, y))) for x in range(size)] for y in range(size)]
            bitmaps[f'window_{kind}_button_glyph_{name}px.bmp'] = bmp32(rows)
    return bitmaps


CHECK_SIZES = {'checkbox': (13, 16, 20, 26, 32, 40, 52), 'radiobutton': (13, 16, 19, 22, 25, 28, 31)}
# Button.Checkbox's twenty images (unchecked, checked, mixed, implicit, excluded; each normal, hot, pushed, disabled)
# and Button.Radiobutton's eight: Windows 11 draws implicit and excluded as unchecked.
CHECK_IMAGES = {'checkbox': ['unchecked', 'checked', 'mixed', 'unchecked', 'unchecked'],
                'radiobutton': ['radio-unchecked', 'radio-checked']}


def control_bitmaps(appearance):
    """Classic controls as the prototype's controls gallery draws them (its measured pixels,
    themes/windows11/controls/controls.json): Button.Checkbox and Button.Radiobutton (13 x 13 at 96 DPI, scaled for
    the larger DPIs; shortcut: only 96 DPI is measured, measure 125-400% with phase 8's scaling) and
    Button.Pushbutton (the 22 x 23 nine-slice), light; push buttons dark too (the dark Explorer theme). Edit borders
    and the group box are the gallery's 1-pixel squares in appearance.json's classic colours. Returns the light and
    the dark bitmaps; a part missing from dark keeps the dark transform of its previous sprites."""
    measured = json.loads((SOURCE.parent / 'controls/controls.json').read_text())['images']
    states = ('normal', 'hot', 'pushed', 'disabled', 'defaulted', 'defaulted-animating')
    light, dark = {}, {}
    light['button.bmp'] = bmp32([[tuple(pixel) for pixel in row] for state in states for row in measured[f'pushbutton-{state}']])
    dark['button.bmp'] = bmp32([[tuple(pixel) for pixel in row] for state in states for row in measured[f'pushbutton-dark-{state}']])
    for variant, bitmaps in (('light', light), ('dark', dark)):
        c = {name: (*rgb(value), 255) for name, value in appearance['classic'][variant].items() if name != 'about'}
        edges = (c['edit_border'], c['edit_hot_border'], c['edit_focus_border'], c['edit_disabled_border'])
        fills = (c['window'], c['window'], c['window'], c['edit_disabled'])
        rows = [[edge] * 5 if y in (0, 4) else [edge, fill, fill, fill, edge] for edge, fill in zip(edges, fills) for y in range(5)]
        for name in ('noscroll', 'hscroll', 'vscroll', 'hvscroll'): bitmaps[f'edit_border_{name}.bmp'] = bmp32(rows)
    border, clear = (*rgb(appearance['classic']['light']['group_border']), 255), (0, 0, 0, 0)
    light['groupbox.bmp'] = bmp32([[border] * 22 if y in (0, 19) else [border] + [clear] * 20 + [border] for y in range(20)])
    for kind, sizes in CHECK_SIZES.items():
        for size in sizes:
            rows = []
            for base in CHECK_IMAGES[kind]:
                for state in ('normal', 'hot', 'pushed', 'disabled'):
                    pixels = measured[f'{base}-{state}']
                    if size == 13:
                        rows += [[tuple(pixel) for pixel in row] for row in pixels]
                        continue
                    image = Image.new('RGBA', (13, 13))
                    image.putdata([tuple(pixel) for row in pixels for pixel in row])
                    image = image.convert('RGBa').resize((size, size), Image.Resampling.LANCZOS).convert('RGBA')
                    rows += [[image.getpixel((x, y)) for x in range(size)] for y in range(size)]
            light[f'{kind}_{size}px.bmp'] = bmp32(rows)
    return light, dark


def dark_bitmap(data, name, palette):
    """Retain Wine's sprite geometry, states and alpha; map their colour roles.

    Glyph sprites are foreground ink. Checked checkbox/radio sprites have ink
    inside accent regions, unlike the unchecked control's plain fill. Other
    sprites retain the approved pane/button/caption distinction. This is an
    opaque standard-control fallback, not a claim of Mica or backdrop blur.
    """
    with Image.open(io.BytesIO(data)) as source:
        image = source.convert('RGBA')
    fill = 'mica_fallback' if 'caption' in name else 'button' if any(
        item in name for item in ('button', 'checkbox', 'radio', 'thumb')) else 'pane'
    glyph = 'glyph' in name or 'expand_collapse' in name
    pixels = image.load()
    states = 20 if 'checkbox' in name else 8 if 'radio' in name else 1
    state_height = image.height // states
    for y in range(image.height):
        checked = states > 1 and y // state_height >= 4 and y // state_height % 4 != 3
        for x in range(image.width):
            r, g, b, alpha = pixels[x, y]
            if not alpha: continue
            if max(r,g,b) - min(r,g,b) < 12:
                role = ('accent_text' if checked and r > 230 else
                        'text' if r < 80 else 'secondary' if r < 170 else
                        'text' if glyph else
                        'line' if r < 230 else fill)
            elif b > r + 25 and g > r + 10:
                role = 'accent'
            else: continue  # Distinct warning/error colours retain their meanings.
            pixels[x, y] = (*rgb(palette[role]), alpha)
    output = io.BytesIO(); image.save(output, format='BMP')
    return output.getvalue()


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    appearance = json.loads((SOURCE.parent / 'appearance.json').read_text())
    header = '/* Generated from appearance.json; not an independent palette. */\n'
    for variant in ('light', 'dark'):
        for name, value in appearance[variant].items():
            header += '#define PW_APPEARANCE_' + variant.upper() + '_' + name.upper() + ' RGB(' + ','.join(map(str,rgb(value))) + ')\n'
    for variant in ('light', 'dark'):
        for name, value in appearance['classic'][variant].items():
            header += '#define PW_APPEARANCE_CLASSIC_' + variant.upper() + '_' + name.upper() + ' RGB(' + ','.join(map(str,rgb(value))) + ')\n'
    for name, value in appearance['metrics'].items():
        if not isinstance(value, int) or value <= 0:
            raise ValueError('Appearance metrics must be positive integer pixels: ' + name)
        header += '#define PW_APPEARANCE_' + name.upper() + ' ' + str(value) + '\n'
    generated = OUTPUT / 'appearance.h'
    if not generated.exists() or generated.read_text() != header:
        generated.write_text(header)
    controls, dark_controls = control_bitmaps(appearance)
    # What tests/theme-parts-probe.c compares Wine's own drawing with: the prototype's measured pixels.
    targets = json.loads((SOURCE.parent / 'caption/glyphs.json').read_text())['targets']
    rgb_word = lambda pixel: '0x%06x' % (pixel[0] << 16 | pixel[1] << 8 | pixel[2])
    reference = ('/* Generated by tools/build-wine-theme.py from themes/windows11/caption/glyphs.json: the prototype\'s\n'
                 ' * caption buttons over the reference bar. [light, dark][close, min, max, restore][msstyles image 0-7]\n'
                 ' * [the plate, then the 10 x 10 glyph box at 18,11], 0x00RRGGBB. */\n'
                 '#define PW_CAPTION_REFERENCE_BAR {0xf3f3f3, 0x202020}\n'
                 'static const unsigned int pw_caption_reference[2][4][8][101] = {\n' +
                 ',\n'.join('{' + ',\n'.join('{' + ',\n'.join('{' + ','.join(
                     [rgb_word(image['plate'])] + [rgb_word(pixel) for row in image['glyph'] for pixel in row]) + '}'
                     for image in targets[theme][kind]) + '}' for kind in ('close', 'min', 'max', 'restore')) + '}'
                     for theme in ('light', 'dark')) + '};\n')
    # Check boxes and radio buttons: the measured gallery pixels as Wine composes them over white.
    measured = json.loads((SOURCE.parent / 'controls/controls.json').read_text())['images']
    over_white = lambda pixel: pw_caption_buttons.wine((255, 255, 255), pixel[:3], pixel[3]) if pixel[3] else (255, 255, 255)
    for kind, count in (('checkbox', 20), ('radiobutton', 8)):
        names = [f'{base}-{state}' for base in CHECK_IMAGES[kind] for state in ('normal', 'hot', 'pushed', 'disabled')]
        reference += (f'static const unsigned int pw_{kind}_reference[{count}][169] = {{\n' + ',\n'.join(
            '{' + ','.join(rgb_word(over_white(pixel)) for row in measured[name] for pixel in row) + '}' for name in names) + '};\n')
    def composite(data):
        with Image.open(io.BytesIO(data)) as image: width, height = image.size
        pixels = data[54:]
        return [[over_white(tuple(pixels[((height - 1 - y) * width + x) * 4 + k] for k in (2, 1, 0, 3))) for x in range(width)]
                for y in range(height)]
    for name, variant, data in (('pushbutton', 0, controls['button.bmp']), ('pushbutton', 1, dark_controls['button.bmp']),
                                ('editborder', 0, controls['edit_border_noscroll.bmp']),
                                ('editborder', 1, dark_controls['edit_border_noscroll.bmp']), ('groupbox', 0, controls['groupbox.bmp'])):
        rows = composite(data)
        reference += (f'static const unsigned int pw_{name}{"_dark" if variant else ""}_reference[{len(rows)}][{len(rows[0])}] = {{\n' +
                      ',\n'.join('{' + ','.join(rgb_word(pixel) for pixel in row) + '}' for row in rows) + '};\n')
    reference += ''.join(f'#define PW_PROGRESS_{name.removeprefix("progress_").upper()} 0x{value[1:]}\n' for name, value in appearance['classic']['light'].items()
                         if name.startswith('progress_'))
    generated = OUTPUT / 'theme_parts_reference.h'
    if not generated.exists() or generated.read_text() != reference:
        generated.write_text(reference)
    (OUTPUT / 'caption_reference.h').unlink(missing_ok=True)
    ini = (SOURCE / 'theme.ini').read_text()
    light_ini = appearance_ini(ini, appearance['light'], appearance['metrics'], appearance['classic']['light'])
    dark_ini = appearance_ini(ini, appearance['dark'], appearance['metrics'], appearance['classic']['dark'])
    dark_ini = re.sub(r'((?:ImageFile\d*|GlyphImageFile)\s*=\s*)blue_', r'\1dark_', dark_ini)
    dark_resources = []
    for item in list(manifest):
        if item['type'] == 2:
            extra = dict(item, name=item['name'].replace('BLUE_', 'DARK_'))
            dark_resources.append(extra)
    manifest += dark_resources + [{'type': 'TEXTFILE', 'name': 'DARK_INI', 'language': 0, 'path': 'theme.ini'}]
    generated = {'BLUE_': {**caption_bitmaps('light'), **controls}, 'DARK_': {**caption_bitmaps('dark'), **dark_controls}}
    lines = ['LANGUAGE 0, 0']
    expected = {}
    for i, item in enumerate(manifest):
        source = SOURCE / item['path']
        data = source.read_bytes()
        resource = OUTPUT / str(i)
        typ, name = item['type'], item['name']
        if typ == 'TEXTFILE':
            text = dark_ini if name == 'DARK_INI' else light_ini if name == 'BLUE_INI' else source.read_text()
            data = text.replace('\n', '\r\n').encode('utf-16le')
        if typ == 'COLORNAMES': data = 'Blue\0Dark\0\0'.encode('utf-16le')
        if typ == 'FILERESNAMES': data = 'BLUE_INI\0DARK_INI\0\0'.encode('utf-16le')
        if typ == 2:
            key = source.name.removeprefix('blue_')
            if key in generated.get(str(name)[:5], {}): data = generated[str(name)[:5]][key]
            # shortcut: the dark variant keeps its previous check boxes (the gallery's are light, as Windows keeps
            # classic controls); draw WinUI's dark ones if the dark theme should follow the prototype's shell there.
            elif str(name).startswith('DARK_'): data = dark_bitmap(data, source.name, appearance['dark'])
            with Image.open(source) as image:
                image.load()
                if image.width <= 0 or image.height <= 0:
                    raise ValueError('Empty bitmap: ' + str(source))
            expected[(typ, name, item['language'])] = data[14:]
            resource.write_bytes(data)
            resource_type = 'BITMAP'
        else:
            expected[(typ, name, item['language'])] = data
            resource.write_bytes(data)
            resource_type = str(typ)
        resource_name = str(name)
        lines.append(f'{resource_name} {resource_type} {json.dumps(str(resource))}')
    # Hash actual compiled payloads, including transformed dark bitmaps. A
    # change in output at the same managed path refreshes metrics once.
    revision_hash = hashlib.sha256()
    for key, data in sorted(expected.items(), key=lambda item: str(item[0])):
        revision_hash.update(str(key).encode() + b'\0' + data)
    appearance_revision = revision_hash.hexdigest()
    revision_resource = OUTPUT / 'appearance-revision'
    revision_data = (appearance_revision + '\0').encode('utf-16le')
    revision_resource.write_bytes(revision_data)
    expected[('TEXTFILE', 'PW_APPEARANCE_REVISION', 0)] = revision_data
    lines.append('PW_APPEARANCE_REVISION TEXTFILE ' + json.dumps(str(revision_resource)))
    rc = OUTPUT / 'theme.rc';rc.write_text('\n'.join(lines)+'\n')
    obj = OUTPUT / 'theme.o';theme = OUTPUT / 'windows11.msstyles'
    subprocess.run([str(ROOT/'.deps/llvm-mingw/bin/x86_64-w64-mingw32-windres'), str(rc), str(obj)],check=True)
    subprocess.run([str(ROOT/'.deps/llvm-mingw/bin/lld-link'),'/dll','/noentry','/machine:x64','/timestamp:0',f'/out:{theme}',str(obj)],check=True)
    pe = pefile.PE(str(theme));actual = {}
    for typ in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for name in typ.directory.entries:
            for language in name.directory.entries:
                entry = language.data.struct
                key = (str(typ.name) if typ.name else typ.id, str(name.name) if name.name else name.id, language.id)
                actual[key] = pe.get_data(entry.OffsetToData,entry.Size)
    if actual != expected or pe.OPTIONAL_HEADER.AddressOfEntryPoint or getattr(pe,'DIRECTORY_ENTRY_IMPORT',None):
        raise ValueError('Resource-only theme round-trip failed')
    refs = set()
    for variant in ('BLUE_INI', 'DARK_INI'):
        ini = actual[('TEXTFILE',variant,0)].decode('utf-16le')
        refs.update(re.findall(r'(?:ImageFile\d*|GlyphImageFile)\s*=\s*(\S+)',ini))
    for name in refs:
        if (2,name.upper().replace('.','_').replace('\\','_'),0) not in actual:
            raise ValueError('Missing referenced bitmap: '+name)
    record = {'resources':len(actual),'bitmap_references':len(refs),'variants':['Blue','Dark'],
              'target':appearance['target'],'sha256':hashlib.sha256(theme.read_bytes()).hexdigest(),'passed':True,'runtime_qualified':False, 'appearance_revision':appearance_revision}
    (OUTPUT/'build.json').write_text(json.dumps(record,indent=1)+'\n')
    # The desktop's wallpaper is Bubble City by Skeuoss (themes/windows11/wallpapers/skeuoss-bubble-city.jpg, used with
    # the artist's permission, the owner's choice on 2026-10-31: NOTICE.md), which replaced the project's own Bubble Earth
    # (tools/make-aero-wallpapers.py, 2026-10-25). It is 1920x1080, the desktop's size; the build stamps it again only
    # when the picture, the stamp's generator or the corner note changed. The note, top right, is the release's version
    # (VERSION) and the community link (the owner's choice, 2026-10-20), drawn by Aero Meadow's stamp().
    destination = OUTPUT / 'proton.bmp'
    stamp = OUTPUT / 'proton.bmp.sha256'
    picture, generator = WALLPAPER, ROOT / 'tools/make-mihawk-wallpaper.py'
    note = [f'MihawkOS v{(ROOT / "VERSION").read_text().strip()}', 'https://discord.gg/mihawk']
    want = hashlib.sha256(picture.read_bytes() + generator.read_bytes() + '\n'.join(note).encode()).hexdigest()
    if not destination.is_file() or not stamp.is_file() or stamp.read_text().strip() != want:
        spec = importlib.util.spec_from_file_location('mihawk_wallpaper', generator)
        meadow = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(meadow)
        with Image.open(picture) as image:
            meadow.stamp(image.convert('RGB').resize((1920, 1080), Image.Resampling.LANCZOS), note).save(destination)
        stamp.write_text(want + '\n')
    print(json.dumps(record,indent=1))


if __name__=='__main__':main()
