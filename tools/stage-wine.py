#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the built PE64 runtime and a clean, offline-initialized test prefix."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
# The public certificate authorities a Windows program's TLS trusts (crypt32's root store, through
# WINE_ADDITIONAL_CERTS_DIR): Mozilla's, as curl extracts them, one dated bundle (MPL-2.0, NOTICE.md).
CA_BUNDLE_URL = 'https://curl.se/ca/cacert-2026-10-20.pem'
CA_BUNDLE_SHA256 = '796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe'
# Wine Mono (.NET Framework) and Wine Gecko (the HTML engine), the versions and hashes this Wine's appwiz.cpl names
# (dlls/appwiz.cpl/addons.c): wineboot installs them into every new prefix from the runtime's share folder. Without
# them it asked to download them, in a dialog nobody answered, and a prefix made on the console never finished
# (prefix-02).
WINE_ADDONS = (
    ('mono', 'wine-mono-10.4.1-x86.msi', 'https://dl.winehq.org/wine/wine-mono/10.4.1/wine-mono-10.4.1-x86.msi',
     '071f4b2887e1c97a11d791ff3d65be9429eed6dec4c2708888bfd546ba358e23'),
    ('gecko', 'wine-gecko-2.47.4-x86_64.msi',
     'https://dl.winehq.org/wine/wine-gecko/2.47.4/wine-gecko-2.47.4-x86_64.msi',
     'e590b7d988a32d6aa4cf1d8aa3aa3d33766fdd4cf4c89c2dcc2095ecb28d066f'),
    ('gecko', 'wine-gecko-2.47.4-x86.msi',
     'https://dl.winehq.org/wine/wine-gecko/2.47.4/wine-gecko-2.47.4-x86.msi',
     '26cecc47706b091908f7f814bddb074c61beb8063318e9efc5a7f789857793d6'),
)


# The HID drivers installed by the shared baseline for a 32-bit game's process.
HID32_DRIVERS = ('hidclass.sys', 'hidparse.sys', 'mouhid.sys', 'winebus.sys', 'winehid.sys', 'winexinput.sys')

GE_NAME = 'GE-Proton11-7-x86_64'
# Proton's fonts the prefix does not take. msyhbd.ttf (Source Han Sans Bold) names itself "Microsoft YaHei", style
# Regular, weight 700, version 2.005, so Wine keeps it as YaHei's regular face in place of msyh.ttf (0.2): every YaHei
# text drew bold, and the only font declaring Big5 was gone, so a Traditional Chinese request (charset 136) fell back
# to Arial with charset 0 (user32 and comctl32 edit, 2026-10-27; the PC's Wine with the console's fonts and no
# system fonts does the same, and without msyhbd.ttf every charset 136 request gets Microsoft Yahei). Wine
# synthesizes YaHei's bold from msyh.ttf.
LEFT_OUT_FONTS = {'msyhbd.ttf'}
GE_SHA256 = 'c5448b76a230384e2d7bc6beb5ccb97bafb7e2c3b6c527cb03a1a546bbcb00a0'
GE_URL = 'https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-7/' + GE_NAME + '.tar.gz'


def patched_dxvk_modules(arch):
    """Refuse stale or changed graphics DLLs before any prefix is packaged."""
    work = DEPS / 'dxvk-patched'
    manifest = json.loads((work / 'manifest.json').read_text())
    stamp = hashlib.sha256()
    for path in (ROOT / 'platform/dxvk/pins.json', ROOT / 'tools/build-dxvk.py',
                 *sorted((ROOT / 'platform/dxvk/patches').glob('*.patch'))):
        stamp.update(path.read_bytes())
    if manifest['source_stamp'] != stamp.hexdigest():
        raise SystemExit('DXVK source changed: run tools/build-dxvk.py')
    directory = 'i386' if arch == 'i386-windows' else 'x86_64'
    modules = []
    for name in ('d3d11', 'dxgi', 'd3d10core', 'd3d9', 'd3d8'):
        relative = directory + '/' + name + '.dll'
        module = work / relative
        if hashlib.sha256(module.read_bytes()).hexdigest() != manifest['files'][relative]:
            raise SystemExit('Patched DXVK differs from its manifest: ' + relative)
        modules.append(module)
    return modules


def patched_vkd3d_modules(arch):
    """d3d12 and d3d12core from tools/build-vkd3d.py, refused when stale or changed, as the DXVK ones."""
    work = DEPS / 'vkd3d-patched'
    manifest = json.loads((work / 'manifest.json').read_text())
    stamp = hashlib.sha256(manifest['revision'].encode())
    for path in (ROOT / 'tools/build-vkd3d.py', *sorted((ROOT / 'platform/vkd3d-proton/patches').glob('*.patch'))):
        stamp.update(path.read_bytes())
    if manifest['source_stamp'] != stamp.hexdigest():
        raise SystemExit('VKD3D-Proton source changed: run tools/build-vkd3d.py')
    directory = 'i386' if arch == 'i386-windows' else 'x86_64'
    modules = []
    for name in ('d3d12', 'd3d12core'):
        relative = directory + '/' + name + '.dll'
        module = work / relative
        if hashlib.sha256(module.read_bytes()).hexdigest() != manifest['files'][relative]:
            raise SystemExit('Patched VKD3D-Proton differs from its manifest: ' + relative)
        modules.append(module)
    return modules


def stage_prefix_baseline(runtime, prefix):
    """One curated bundle, installed after wineboot by pw_wine_prefix_prepare.

    Extract only the pinned package's fonts, ICU and licence notices. Windows
    modules remain our patched Wine builds; no Linux binaries enter the runtime.
    """
    archive = DEPS / (GE_NAME + '.tar.gz')
    if not archive.is_file():
        raise SystemExit(f'{archive} is missing: tools/fetch-all.sh fetches {GE_URL}')
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != GE_SHA256:
            raise SystemExit('GE archive checksum mismatch: tools/fetch-all.sh fetches ' + GE_URL)
    bundle = runtime / 'lib/wine/prefix/drive_c/windows'
    fonts = bundle / 'Fonts'
    fonts.mkdir(parents=True)
    licences = runtime / 'share/licenses/ge-proton'
    licences.mkdir(parents=True)
    wanted = {}
    for arch, target in (('x86_64-windows', 'system32'), ('i386-windows', 'syswow64')):
        dest = bundle / target
        dest.mkdir()
        for component, names in (
                ('icu', ('icuuc68.dll', 'icuin68.dll', 'icudt68.dll')),
                ('dxvk', ('d3d11.dll', 'dxgi.dll', 'd3d10core.dll', 'd3d9.dll') +
                 (('d3d8.dll',) if arch == 'i386-windows' else ())),
                ('vkd3d-proton', ('d3d12.dll', 'd3d12core.dll'))):
            for name in names:
                wanted[GE_NAME + '/files/lib/wine/' + component + '/' + arch + '/' + name] = dest / name
        for name in ('opengl32.dll', 'libgallium_wgl.dll'):
            shutil.copy2(runtime / 'lib/wine/mesa' / arch / name, dest / name)
        if arch == 'i386-windows':
            drivers = dest / 'drivers'
            drivers.mkdir()
            for name in HID32_DRIVERS:
                shutil.copy2(runtime / 'lib/wine' / arch / name, drivers / name)
    for name in ('LICENSE', 'LICENSE.OFL'):
        wanted[GE_NAME + '/' + name] = licences / name
    # Wine also exposes Explorer at the Windows root. Keep that public image
    # identical to the patched runtime: shell icon/resource lookup reads the
    # prefix file, even when the loader resolves builtin code from the runtime.
    shutil.copy2(runtime / 'lib/wine/x86_64-windows/explorer.exe', bundle / 'explorer.exe')
    # C:\windows\system32\mihawk.exe is the desktop session's entry, run by the title itself (src/pw_desktop_session.h).
    shutil.copy2(DEPS / 'probes/mihawk-appearance.exe', bundle / 'system32/mihawk-appearance.exe')
    # Stream the large archive once. Wine's fonts take precedence over Proton's
    # duplicate names, independent of archive order. Per-game alternatives stay out.
    wine_fonts = set()
    with tarfile.open(archive, 'r|gz') as package:
        for member in package:
            if not member.isfile():
                continue
            dest = wanted.pop(member.name, None)
            for folder in ('share/fonts/', 'share/wine/fonts/'):
                base = GE_NAME + '/files/' + folder
                if member.name.startswith(base):
                    name = member.name[len(base):]
                    if '/' not in name and name.lower().endswith(('.ttf', '.ttc', '.otf', '.fon')):
                        if name.lower() in LEFT_OUT_FONTS:
                            continue
                        if folder == 'share/wine/fonts/':
                            wine_fonts.add(name)
                        elif name in wine_fonts:
                            continue
                        dest = fonts / name
            if dest is not None:
                dest.write_bytes(package.extractfile(member).read())
    if wanted:
        raise SystemExit('Pinned GE package lacks baseline files: ' + ', '.join(wanted))
    # Existing prefixes still hold what earlier runtimes put there: wine/ps5/pw_wine_prefix.c removes these names.
    (fonts / '.pw-retired').write_text(''.join(name + '\n' for name in sorted(LEFT_OUT_FONTS)))
    # The source-built external-rendering fix is shared by every program.
    for arch, target in (('x86_64-windows', 'system32'), ('i386-windows', 'syswow64')):
        for module in patched_dxvk_modules(arch) + patched_vkd3d_modules(arch):
            shutil.copy2(module, bundle / target / module.name)
    dxvk_licences = runtime / 'share/licenses/dxvk-patched'
    dxvk_licences.mkdir(parents=True, exist_ok=True)
    for name, source in (('DXVK', 'LICENSE'), ('dxbc-spirv', 'subprojects/dxbc-spirv/LICENSE'),
                         ('libdisplay-info', 'subprojects/libdisplay-info/LICENSE'),
                         ('Vulkan-Headers', 'include/vulkan/LICENSE.md'),
                         ('SPIRV-Headers', 'include/spirv/LICENSE'),
                         ('dxbc-SPIRV-Headers', 'subprojects/dxbc-spirv/submodules/spirv_headers/LICENSE')):
        shutil.copy2(DEPS / 'dxvk-patched/source' / source, dxvk_licences / (name + '.txt'))
    vkd3d_licences = runtime / 'share/licenses/vkd3d-patched'
    vkd3d_licences.mkdir(parents=True, exist_ok=True)
    for name, source in (('VKD3D-Proton', 'LICENSE'), ('VKD3D-Proton-COPYING', 'COPYING'),
                         ('dxil-spirv', 'subprojects/dxil-spirv/LICENSE.MIT'),
                         ('dxbc-spirv', 'subprojects/dxil-spirv/subprojects/dxbc-spirv/LICENSE'),
                         ('Vulkan-Headers', 'khronos/Vulkan-Headers/LICENSE.md'),
                         ('SPIRV-Headers', 'khronos/SPIRV-Headers/LICENSE'),
                         ('dxil-spirv-SPIRV-Headers', 'subprojects/dxil-spirv/third_party/spirv-headers/LICENSE')):
        shutil.copy2(DEPS / 'vkd3d-patched/source' / source, vkd3d_licences / (name + '.txt'))
    # dxil-spirv compiles two of its third_party folders into d3d12core (the build's own compile commands, 2026-10-31):
    # RenderDoc's LLVM bitcode reader (bc-decoder, MIT) and glslang's SPIR-V builder (glslang-spirv, BSD-3-Clause).
    # Neither carries a licence file, only the text atop each source, which is staged as it stands there.
    for name, source in (('bc-decoder', 'bc-decoder/llvm_decoder.cpp'), ('glslang-spirv', 'glslang-spirv/SpvBuilder.cpp')):
        lines = []
        for line in (DEPS / 'vkd3d-patched/source/subprojects/dxil-spirv/third_party' / source).read_text().splitlines():
            if not line.startswith(('/*', ' *', '//')):
                break
            lines.append(line)
        if not any('Copyright' in line for line in lines):
            raise SystemExit('No licence atop dxil-spirv third_party/' + source)
        (vkd3d_licences / (name + '.txt')).write_text('\n'.join(lines) + '\n')
    # One redistributable UI family for both architectures and every prefix.
    # Rebuild from checked-in pins; Microsoft development fonts are never inputs.
    subprocess.run(['python3', str(ROOT / 'tools/build-proton-ui-font.py')], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(['python3', str(ROOT / 'tools/build-wine-theme.py')], check=True,
                   stdout=subprocess.DEVNULL)
    ui = DEPS / 'windows11-theme'
    icons = DEPS / 'mihawk-icons'
    icon_destination = bundle / 'resources/mihawk'
    icon_destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(icons / 'icons.dll', icon_destination / 'icons.dll')
    # The pointer scheme's files; the session root names them in the registry (wine/ps5/pw_shell_cursors.h).
    subprocess.run(['python3', str(ROOT / 'tools/build-cursors.py')], check=True, stdout=subprocess.DEVNULL)
    cursors = bundle / 'Cursors'
    cursors.mkdir(parents=True, exist_ok=True)
    for name in json.loads((DEPS / 'windows11-cursors/build.json').read_text())['files']:
        shutil.copy2(DEPS / 'windows11-cursors' / name, cursors / name)
    if (ui / 'proton.bmp').is_file():
        wallpaper = bundle / 'web/wallpaper'
        wallpaper.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ui / 'proton.bmp', wallpaper / 'proton.bmp')
    for source in sorted((ui / 'fonts').glob('*.ttf')):
        shutil.copy2(source, fonts / source.name)
    # The monospace face, unmodified and pinned (tools/fetch-cascadia-mono.py); the session root names it.
    cascadia = ROOT / 'themes/windows11/fonts/cascadia'
    for name, pin in json.loads((cascadia / 'upstream.json').read_text())['files'].items():
        if hashlib.sha256((cascadia / name).read_bytes()).hexdigest() != pin['sha256']:
            raise SystemExit('Cascadia Mono differs from its pin: ' + name)
        if name.endswith('.ttf'): shutil.copy2(cascadia / name, fonts / name)
    ui_licences = runtime / 'share/licenses/proton-ui'
    ui_licences.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ui / 'fonts/LICENSE.txt', ui_licences / 'OFL.txt')
    shutil.copy2(ROOT / 'themes/windows11/COPYING.LIB', ui_licences / 'theme-LGPL.txt')
    shutil.copy2(cascadia / 'LICENSE', ui_licences / 'CascadiaMono-OFL.txt')
    for name in ('LICENSE', 'NOTICE'):
        shutil.copy2(ROOT / 'themes/windows11/icons/fluent' / name, ui_licences / ('Fluent-' + name))
    # Wine already selects this standard light path. Keep user-selected alternate
    # themes intact; replace the managed default, not their registry preferences.
    for base in (bundle / 'resources/themes', runtime / 'share/wine/themes'):
        destination = base / 'light'
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ui / 'windows11.msstyles', destination / 'light.msstyles')
    # Font paths registered by Wine must resolve in both locations.
    for font in fonts.iterdir():
        if not font.name.startswith('.'):
            shutil.copy2(font, runtime / 'share/wine/fonts' / font.name)
    for directory in sorted(bundle.rglob('*')) + [bundle]:
        if not directory.is_dir():
            continue
        files = sorted(p for p in directory.iterdir() if p.is_file())
        if not files:
            continue
        digest = hashlib.sha256()
        for file in files:
            digest.update(file.name.encode() + b'\0' + file.read_bytes())
        (directory / '.pw-proton-version').write_text(digest.hexdigest() + '\n')
    # The test prefix starts with exactly the same bundle as real prefixes.
    shutil.copytree(bundle, prefix / 'drive_c/windows', dirs_exist_ok=True)



def copy_prefix(source, dest):
    """Encode Wine links for the console, never copying files outside the prefix."""
    dest.mkdir(parents=True, exist_ok=True)
    links = []
    for path in sorted(source.iterdir()):
        target = dest / path.name
        if path.is_symlink():
            resolved = path.resolve()
            if path.parent.name == 'dosdevices':
                if path.name in ('c:', 'z:'):
                    links.append(f'{path.name}\t' + ('../drive_c' if path.name == 'c:' else '/'))
            elif resolved.is_relative_to(DEPS / 'prefix-probe'):
                links.append(f'{path.name}\t{path.readlink()}')
            else:
                target.mkdir(exist_ok=True)
        elif path.is_dir():
            copy_prefix(path, target)
        elif path.name not in ('.update-timestamp', '.wineserver'):
            shutil.copy2(path, target)
    if links:
        (dest / '.pw-symlinks').write_text('\n'.join(links) + '\n')


def pe_image(path):
    """A PE image, which llvm-strip reads. A fresh host wineboot writes 16-bit placeholders that are MZ alone (the probe
    prefix's 255-byte windows/rundll.exe), and llvm-strip refuses those."""
    with open(path, 'rb') as file:
        head = file.read(64)
        if len(head) < 64 or head[:2] != b'MZ':
            return False
        file.seek(int.from_bytes(head[60:64], 'little'))
        return file.read(4) == b'PE\0\0'

def main():
    out = DEPS / 'staged'
    runtime = out / 'runtime' / 'ge11-7'
    prefix = out / 'prefixes' / 'probe'
    report = json.loads((DEPS / 'wine-ps5/report.json').read_text())
    modules = report['prx']['modules']
    for name, module in modules.items():
        if not module['built'] or module['errors'] or module['unresolved'] or module.get('raw_syscalls'):
            raise SystemExit(f'Cannot stage broken module {name}')
    for path in (runtime, prefix):
        if path.exists():
            shutil.rmtree(path)
        path.mkdir(parents=True)
    payload = ROOT / 'build/proton-payload-child.elf'
    payload_manifest = json.loads(payload.with_suffix('.json').read_text())
    if not payload_manifest.get('payload') or hashlib.sha256(payload.read_bytes()).hexdigest() != payload_manifest['sha256']:
        raise SystemExit('The payload loader differs from its manifest')
    (out / 'run').mkdir(exist_ok=True)
    shutil.copy2(payload, out / 'run/proton-payload-child.elf')
    host = DEPS / 'wine-runtime/stage/usr/local'
    shutil.copytree(host / 'lib/wine/x86_64-windows', runtime / 'lib/wine/x86_64-windows')
    # Wine's i386 PE modules, for 32-bit programs under WoW64 (docs/IA32_DESIGN.md, M3), when the host build has them.
    if (host / 'lib/wine/i386-windows').is_dir():
        shutil.copytree(host / 'lib/wine/i386-windows', runtime / 'lib/wine/i386-windows')
    shutil.copytree(host / 'share/wine', runtime / 'share/wine')
    # wine.inf from the patched tree (tools/build-wine-ps5.sh): it names what wineboot registers (winedmo's classes,
    # patch 2006). A prefix updates when wine.inf's time differs from the one it recorded (patch 0952), so the time is
    # made from the content: a changed wine.inf updates every prefix once, a rebuilt identical one none.
    inf = runtime / 'share/wine/wine.inf'
    patched_inf = (DEPS / 'wine-ps5/pe/wine.inf').read_bytes()
    inf.write_bytes(patched_inf)
    inf_time = 1_600_000_000 + int(hashlib.sha256(patched_inf).hexdigest()[:7], 16)
    os.utime(inf, (inf_time, inf_time))
    bundle = DEPS / 'ca-bundle' / CA_BUNDLE_URL.rsplit('/', 1)[1]
    if not bundle.is_file():
        bundle.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['curl', '-sSfL', '-o', str(bundle), CA_BUNDLE_URL], check=True)
    if hashlib.sha256(bundle.read_bytes()).hexdigest() != CA_BUNDLE_SHA256:
        raise SystemExit(f'{bundle} does not match its pinned SHA-256')
    shutil.copy2(bundle, runtime / 'share/wine/ca-certificates.crt')
    for folder, name, url, digest in WINE_ADDONS:
        package = DEPS / 'wine-addons' / name
        if not package.is_file():
            package.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(['curl', '-sSfL', '-o', str(package), url], check=True)
        if hashlib.sha256(package.read_bytes()).hexdigest() != digest:
            raise SystemExit(f'{package} does not match its pinned SHA-256')
        (runtime / 'share/wine' / folder).mkdir(exist_ok=True)
        shutil.copy2(package, runtime / 'share/wine' / folder / name)
    shutil.copytree(DEPS / 'wine-ps5/prx/sce_module', runtime / 'lib/wine/x86_64-unix')
    # Windows programs in Sony-managed helper processes (docs/WINDOWS_PROCESSES.md, "Layouts") load two modules of
    # their own beside the title's (pw_wine_dl.c): win32u without the OpenGL driver, whose system libraries a helper
    # cannot use, and the GPU broker's Vulkan client in place of RADV. win32u's comes from the same series built
    # with --no-opengl, or it would not match the ntdll both load.
    helper = DEPS / 'wine-gpu-broker'
    helper_report = json.loads((helper / 'report.json').read_text())
    if helper_report['patches'] != report['patches'] or helper_report['wine_commit'] != report['wine_commit']:
        raise SystemExit('The helper runtime (.deps/wine-gpu-broker) is not built from the title runtime\'s series')
    win32u = helper_report['prx']['modules']['win32u']
    if not win32u['built'] or win32u['errors'] or win32u['unresolved'] or win32u.get('raw_syscalls'):
        raise SystemExit('Cannot stage the helper win32u')
    if any('Agc' in n or 'VideoOut' in n for n in win32u.get('needed', [])):
        raise SystemExit('The helper runtime links the OpenGL driver (build it with --no-opengl)')
    unix = runtime / 'lib/wine/x86_64-unix'
    shutil.copy2(helper / 'prx/sce_module/win32u.prx', unix / 'win32u-helper.prx')
    client = DEPS / 'gpu-wsi-client'
    client_manifest = json.loads((client / 'manifest.json').read_text())
    image = (client / 'gpu-client.prx').read_bytes()
    if not client_manifest.get('wsi') or hashlib.sha256(image).hexdigest() != client_manifest['sha256']:
        raise SystemExit('The GPU broker client (.deps/gpu-wsi-client) differs from its manifest')
    (unix / 'libvulkan-helper.prx').write_bytes(image)
    # The unsigned module ELFs, beside the fake-signed SELFs: every payload helper's user-space loader maps these
    # instead of asking the kernel (src/pw_prx_elf.h: the kernel refused a fake-signed SELF in a helper, errno 92,
    # on 11.00 to 13.59). The SELFs stay for the title and for WINE_PS5_PRX_LOADER=0.
    for source in sorted((DEPS / 'wine-ps5/prx').glob('*.elf')):
        if source.name.endswith('.shared.elf'):
            continue
        shutil.copy2(source, unix / (source.stem + '.prx.elf'))
    shutil.copy2(helper / 'prx/win32u.elf', unix / 'win32u-helper.prx.elf')
    shutil.copy2(client / 'client.elf', unix / 'libvulkan-helper.prx.elf')
    if not (DEPS / 'prefix-probe/system.reg').is_file():
        raise SystemExit('.deps/prefix-probe, the check prefix template, is missing: run tools/fetch-all.sh')
    copy_prefix(DEPS / 'prefix-probe', prefix)
    # The template's host wineboot named the profile steamuser (Proton's GetUserNameW); the console's Wine names it
    # Mihawk (patch 1240), so the staged prefix is Mihawk's, as the title moves an older one (src/pw_wine_profile.h).
    if (prefix / 'drive_c/users/steamuser').is_dir():
        (prefix / 'drive_c/users/steamuser').rename(prefix / 'drive_c/users/Mihawk')
    # A WoW64 prefix: the i386 modules in C:\windows\syswow64, as wineboot installs them (every module but import
    # libraries). The 32-bit loader takes a system DLL only from the prefix, and the template predates the i386
    # build: kernel32.dll was not found (m3-pe32-2026-10-07-09). The code still loads from lib/wine/i386-windows;
    # these are the same files. Drivers too, in syswow64\drivers: Windows has no 32-bit ones, but here the HID
    # drivers run in the game's process, a 32-bit game's are i386, and system32\drivers is redirected there
    # (ZwLoadDriver failed for winebus in a 32-bit process, m4s2-2026-10-07-02).
    def syswow64_target(module):
        return prefix / 'drive_c/windows/syswow64' / ('drivers' if module.suffix == '.sys' else '') / module.name
    if (host / 'lib/wine/i386-windows').is_dir():
        (prefix / 'drive_c/windows/syswow64/drivers').mkdir(parents=True, exist_ok=True)
        for module in sorted((host / 'lib/wine/i386-windows').iterdir()):
            if module.suffix != '.a':
                shutil.copy2(module, syswow64_target(module))
    for patched in (DEPS / 'wine-ps5/pe/x86_64-windows').iterdir():
        prefix_target = prefix / 'drive_c/windows/system32'
        if patched.suffix == '.sys':
            prefix_target /= 'drivers'
        prefix_target.mkdir(parents=True, exist_ok=True)
        for target in (runtime / 'lib/wine/x86_64-windows', prefix_target):
            shutil.copy2(patched, target / patched.name)
    # Every module of the template's system32 (drivers, wbem, DriverStore and the rest) is the runtime's own build,
    # as wineboot installs it on the console: the template held an older build, which wineboot replaced there, so the
    # deep read-back called 373 files damaged (console-setup-2026-10-15-05/-06), each equal to the runtime's module of
    # the same name.
    modules = {module.name.lower(): module for module in (runtime / 'lib/wine/x86_64-windows').iterdir()
               if module.suffix != '.a'}
    for path in (prefix / 'drive_c/windows/system32').rglob('*'):
        if path.is_file() and path.name.lower() in modules:
            shutil.copy2(modules[path.name.lower()], path)
    # The patched i386 modules, over the host build's, in the runtime and in syswow64.
    if (DEPS / 'wine-ps5/pe/i386-windows').is_dir():
        for patched in (DEPS / 'wine-ps5/pe/i386-windows').iterdir():
            shutil.copy2(patched, runtime / 'lib/wine/i386-windows' / patched.name)
            shutil.copy2(patched, syswow64_target(patched))
    # And every module of the template's syswow64 (wbem and the rest), as for system32 above: its wbem\wmiutils.dll
    # stayed an older i386 build of the runtime's size, which wineboot replaced on the console and the deploy, judging
    # by the last manifest and the size, never put back (the Pro, 2026-10-27).
    if (runtime / 'lib/wine/i386-windows').is_dir():
        modules32 = {module.name.lower(): module for module in (runtime / 'lib/wine/i386-windows').iterdir()
                     if module.suffix != '.a'}
        for path in (prefix / 'drive_c/windows/syswow64').rglob('*'):
            if path.is_file() and path.name.lower() in modules32:
                shutil.copy2(modules32[path.name.lower()], path)
    # The i386 HID drivers a 32-bit game loads into its own process, with a version stamp: the title syncs them into
    # each prefix's syswow64\drivers (native/proton_wine.cpp), where wineboot installs none (a run of 2026-10-07).
    if (runtime / 'lib/wine/i386-windows').is_dir():
        hid32 = runtime / 'lib/wine/drivers/i386-windows'
        hid32.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256()
        for name in HID32_DRIVERS:
            shutil.copy2(runtime / 'lib/wine/i386-windows' / name, hid32 / name)
            digest.update((hid32 / name).read_bytes())
        (hid32 / 'version').write_text('hid32 %s\n' % digest.hexdigest()[:16])
    for name in ('d3d11.dll', 'dxgi.dll', 'd3d10core.dll', 'd3d9.dll', 'd3d12.dll', 'd3d12core.dll'):
        source = (DEPS / 'vkd3d-patched/x86_64' if name.startswith('d3d12') else DEPS / 'dxvk-release') / name
        if source.is_file():
            shutil.copy2(source, prefix / 'drive_c/windows/system32' / name)
    # Their i386 builds for 32-bit programs, in syswow64 (tools/extract-dxvk.py), d3d8 among them.
    for source in sorted((DEPS / 'dxvk-release/i386').glob('*.dll')):
        shutil.copy2(source, prefix / 'drive_c/windows/syswow64' / source.name)
    for source in patched_vkd3d_modules('i386-windows'):   # over GE's prebuilt d3d12 of the same revision
        shutil.copy2(source, prefix / 'drive_c/windows/syswow64' / source.name)
    # Mesa's WGL over Zink: OpenGL for programs in helper processes, which cannot use the console's OpenGL. The title
    # copies these into a prefix's system32 for the launcher layout (native/proton_wine.cpp); "version" names them.
    mesa = DEPS / 'mesa-foundation-wgl'
    mesa_manifest = json.loads((mesa / 'manifest.json').read_text())
    mesa_target = runtime / 'lib/wine/mesa/x86_64-windows'
    mesa_target.mkdir(parents=True, exist_ok=True)
    for name, target in (('opengl32.dll', 'libgl-gdi'), ('libgallium_wgl.dll', 'wgl')):
        module = mesa / 'build/src/gallium/targets' / target / name
        if hashlib.sha256(module.read_bytes()).hexdigest() != mesa_manifest['files'][name]:
            raise SystemExit(f'Mesa {name} differs from its manifest (tools/build-mesa-windows-zink.sh)')
        shutil.copy2(module, mesa_target / name)
    (mesa_target / 'version').write_text('mesa %s %s %s\n' % (mesa_manifest['mesa_revision'],
        mesa_manifest['files']['opengl32.dll'][:16], mesa_manifest['files']['libgallium_wgl.dll'][:16]))
    # Its i686 build for 32-bit programs (tools/build-mesa-windows-zink-i686.sh); the title copies it into syswow64.
    mesa32 = DEPS / 'mesa-foundation-wgl-i686'
    if (mesa32 / 'manifest.json').is_file():
        mesa32_manifest = json.loads((mesa32 / 'manifest.json').read_text())
        mesa32_target = runtime / 'lib/wine/mesa/i386-windows'
        mesa32_target.mkdir(parents=True, exist_ok=True)
        for name, target in (('opengl32.dll', 'libgl-gdi'), ('libgallium_wgl.dll', 'wgl')):
            module = mesa32 / 'build/src/gallium/targets' / target / name
            if hashlib.sha256(module.read_bytes()).hexdigest() != mesa32_manifest['files'][name]:
                raise SystemExit(f'i686 Mesa {name} differs from its manifest (tools/build-mesa-windows-zink-i686.sh)')
            shutil.copy2(module, mesa32_target / name)
        (mesa32_target / 'version').write_text('mesa %s %s %s\n' % (mesa32_manifest['mesa_revision'],
            mesa32_manifest['files']['opengl32.dll'][:16], mesa32_manifest['files']['libgallium_wgl.dll'][:16]))
    # MihawkBrowser (docs/MIHAWK_BROWSER.md §3.6): once, read-only, in the runtime and outside every prefix (a prefix
    # copy would put its 316 MB in each one), reached as Z:\data\ps5proton\runtime\ge11-7\lib\mihawk-browser.
    browser = DEPS / 'mihawk-browser/app'
    if not (browser / 'MihawkBrowser.exe').is_file():
        raise SystemExit('MihawkBrowser is not built: run tools/build-mihawk-browser.sh')
    shutil.copytree(browser, runtime / 'lib/mihawk-browser')
    cef = DEPS / 'cef' / re.search(r"^NAME = '(.*)'$", (ROOT / 'tools/fetch-cef.py').read_text(), re.M).group(1)
    cef_licences = runtime / 'share/licenses/cef'
    cef_licences.mkdir(parents=True)
    for name in ('LICENSE.txt', 'CREDITS.html'):
        shutil.copy2(cef / name, cef_licences / name)
    for path in prefix.rglob('*.reg'):
        # Host-only Unix audio backends cannot load on the console.
        text = path.read_text()
        text = text.replace('"Audio"="pulse"', '"Audio"="ps5"').replace('"Audio"="alsa"', '"Audio"="ps5"')
        # The registry was made on the host: its font files are named by their host paths, and the computer by the
        # host's name. On the console the fonts are in the runtime's share folder, and the computer is a PS5.
        text = re.sub(r'Z:\\\\home\\\\[^"]*?\\\\wine-runtime\\\\build\\\\fonts\\\\',
                      lambda m: 'Z:\\\\data\\\\ps5proton\\\\runtime\\\\ge11-7\\\\share\\\\wine\\\\fonts\\\\', text)
        text = re.sub(r'(?i)"(ComputerName|Hostname|USERDOMAIN)"="CACHYOS-X8664"',
                      lambda m: f'"{m.group(1)}"="' + ('ps5' if m.group(1) == 'Hostname' else 'PS5') + '"', text)
        # the host's system fonts (Z:\\usr\\share\\fonts\\...) are registered too, and the console has none of those files
        text = re.sub(r'^"[^"\n]*"="Z:\\\\usr\\\\share\\\\fonts\\\\[^\n]*\n', '', text, flags=re.M)
        text = text.replace('"LOGONSERVER"="\\\\\\\\CACHYOS-X8664"', '"LOGONSERVER"="\\\\\\\\PS5"')
        # The profile's paths and name, by pw_wine_profile_rewrite's rules: after users and a separator, before a
        # separator, a quote, ';', ',' or ']'; or a whole quoted "steamuser" (the saved USERNAME).
        text = re.sub(r'(?i)([\\/]users(?:\\\\|/))steamuser(?=[\\/";,\]])', r'\1Mihawk', text)
        text = re.sub(r'(?i)"steamuser"', '"Mihawk"', text)
        path.write_text(text)
    # Keep user folders local instead of host-home symlinks.
    for path in ('windows/temp', 'users/Mihawk/AppData/Local/Temp'):
        (prefix / 'drive_c' / path).mkdir(parents=True, exist_ok=True)
    for name in ('wine-probe.exe', 'xinput-probe.exe', 'hid-probe.exe', 'sync-probe.exe', 'shell-open-probe.exe', 'exit-probe.exe', 'priority-probe.exe',
                 'hang-probe.exe', 'window-probe.exe', 'fs-probe.exe', 'stack-probe.exe', 'gl-exit-probe.exe', 'gl-present-probe.exe', 'gl-sprite-probe.exe', 'vm-protect-probe.exe', 'vm-protect32-probe.exe', 'remote-memory-probe.exe', 'mem-probe.exe', 'gl-vertex-probe.exe', 'stdout-probe.exe', 'crash-probe.exe', 'nt-file-probe.exe', 'thread-leak-probe.exe', 'd3d11-probe.exe', 'gpu-host-memory-probe.exe', 'section-memory-probe.exe',
                 'process-probe.exe', 'graphics-render-probe.exe', 'memory-class-probe.exe', 'desktop-probe.exe', 'desktop-overlap-probe.exe', 'mouse-controls32-probe.exe', 'virtual-query32-probe.exe', 'virtual-query64-probe.exe', 'registry-flush64-probe.exe', 'file-write32-probe.exe', 'file-write64-probe.exe', 'relative-dir32-probe.exe', 'relative-dir64-probe.exe', 'file-cost32-probe.exe', 'file-cost64-probe.exe', 'handles-paths32-probe.exe', 'handles-paths64-probe.exe', 'pipe-cancel32-probe.exe', 'pipe-cancel64-probe.exe', 'lock32-probe.exe', 'lock64-probe.exe', 'wt-session.exe', 'path-limit64-probe.exe', 'continue64-probe.exe', 'stack-limit64-probe.exe', 'aurelia_windows-fixture.exe', 'steamcmd-fixture.exe', 'syscall-cost32-probe.exe', 'syscall-cost64-probe.exe', 'net-throughput32-probe.exe', 'net-throughput64-probe.exe', 'system-memory32-probe.exe', 'system-memory64-probe.exe', 'image-protect32-probe.exe', 'image-protect64-probe.exe', 'shared-section32-probe.exe', 'shared-section64-probe.exe', 'kill-convert-probe.exe', 'guest-address-space-probe.exe', 'section-view-probe.exe', 'late-library-probe.exe', 'dnsapi32-probe.exe', 'dnsapi64-probe.exe', 'mf-video32-probe.exe', 'mf-video64-probe.exe', 'mf-video.mp4', 'mf-video.txt', 'network-adapters32-probe.exe', 'network-adapters64-probe.exe', 'socket-options32-probe.exe', 'socket-options64-probe.exe', 'memory-guard-probe.exe',
                 'dns-probe.exe', 'tls-probe.exe', 'pe32-probe.exe', 'fixed-base32-probe.exe', 'games-autodetect-probe.exe',
                 'cross32-probe.exe', 'cross64-probe.exe', 'wow64-probe.exe',
                 'hid32-probe.exe', 'vk32-probe.exe', 'vk64-probe.exe', 'gl-vertex32-probe.exe',
                 'd3d11-32-probe.exe', 'gl-upload32-probe.exe', 'gl-upload64-probe.exe', 'd3d12-levels-probe.exe', 'd3d12-sparse-probe.exe', 'many-files-probe.exe', 'log-flood-probe.exe', 'missing-import-probe.exe', 'parent-query-probe.exe', 'd3d12-shared-probe.exe', 'd3d12-existing-heap-probe.exe', 'osk-typed-probe.exe', 'd3d9ex-d3d11-share-probe.exe',
                 'input-message32-probe.exe', 'input-message64-probe.exe',
                 'prefix-baseline32-probe.exe', 'prefix-baseline64-probe.exe',
                 'proton-ui32-probe.exe', 'proton-ui64-probe.exe',
                 'appearance32-probe.exe', 'appearance64-probe.exe',
                 'theme-notify32-probe.exe', 'theme-notify64-probe.exe',
                 'initial-env32-probe.exe', 'initial-env64-probe.exe',
                 'locale32-probe.exe', 'locale64-probe.exe',
                 'display-modes32-probe.exe', 'display-modes64-probe.exe',
                 'window-snapshot32-probe.exe', 'window-snapshot64-probe.exe',
                 'window-compose32-probe.exe', 'window-compose64-probe.exe', 'gpu-window32-probe.exe', 'gpu-window64-probe.exe',
                 'window-effects32-probe.exe', 'window-effects64-probe.exe',
                 'wallpaper32-probe.exe', 'wallpaper64-probe.exe',
                 'cursor-scheme32-probe.exe', 'cursor-scheme64-probe.exe',
                 'theme-parts32-probe.exe', 'theme-parts64-probe.exe',
                 'monospace32-probe.exe', 'monospace64-probe.exe',
                 'work-area32-probe.exe', 'work-area64-probe.exe',
                 'shell32-probe.exe', 'shell64-probe.exe',
                 'desktop-session32-probe.exe', 'desktop-session64-probe.exe', 'shell-surfaces64-probe.exe', 'shell-scorecard64-probe.exe', 'frame-pacing64-probe.exe', 'desktop-view64-probe.exe', 'agent-permissions64-probe.exe', 'desktop-items64-probe.exe', 'memory-cap64-probe.exe', 'game-mode64-probe.exe',
                 'game-focus64-probe.exe', 'hid-enum64-probe.exe', 'umip32-probe.exe', 'umip64-probe.exe', 'gate-sequence.exe'):
        shutil.copy2(DEPS / 'probes' / name, prefix / 'drive_c' / name)
    for test in sorted((DEPS / 'wine-tests/exe').glob('*.exe')):  # tools/build-wine-tests.sh and tools/fetch-vkd3d-tests.sh
        shutil.copy2(test, prefix / 'drive_c' / test.name)
    images = [str(path) for path in out.rglob('*')
              if path.is_file() and path.suffix.lower() in ('.dll', '.exe', '.drv', '.ocx', '.sys') and pe_image(path)]
    # Many files to one llvm-strip, which strips each on its own: one process per file was 26 of staging's 41 s.
    for first in range(0, len(images), 500):
        subprocess.run(['llvm-strip', '--strip-debug', *images[first:first + 500]], check=True, capture_output=True)
    cares_license = DEPS / 'wine-ps5/prx/licenses/cares/LICENSE.md'
    if not cares_license.is_file():
        raise SystemExit('Pinned c-ares license is missing')
    cares_destination = runtime / 'share/licenses/cares'
    cares_destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(cares_license, cares_destination / 'LICENSE.md')
    # FFmpeg in winedmo.prx: LGPL-2.1-or-later, its licence beside the runtime that carries it.
    ffmpeg_destination = runtime / 'share/licenses/ffmpeg'
    ffmpeg_destination.mkdir(parents=True, exist_ok=True)
    for licence in sorted((DEPS / 'wine-ps5/prx/licenses/ffmpeg').iterdir()):
        shutil.copy2(licence, ffmpeg_destination / licence.name)
    # Wine, VKD3D-Proton, GnuTLS, Nettle, FreeType, Mesa, Wine Mono, ICU, the CA bundle's MPL and the PE runtime.
    subprocess.run(['python3', str(ROOT / 'tools/stage-licences.py'), 'runtime', str(runtime)], check=True)
    stage_prefix_baseline(runtime, prefix)
    # Native compiler/driver or DXVK changes get a fresh per-game namespace.
    # Upstream cache keys and validation still protect each individual record.
    identity = hashlib.sha256()
    for name in ('lib/wine/x86_64-unix/win32u.prx', 'lib/wine/x86_64-unix/libvulkan.prx'):
        identity.update(hashlib.sha256((runtime / name).read_bytes()).digest())
    for name in ('d3d11.dll', 'dxgi.dll', 'd3d10core.dll', 'd3d9.dll', 'd3d12.dll', 'd3d12core.dll'):
        source = (DEPS / 'vkd3d-patched/x86_64' if name.startswith('d3d12') else DEPS / 'dxvk-patched/x86_64') / name
        identity.update(hashlib.sha256(source.read_bytes()).digest())
    for source in patched_dxvk_modules('i386-windows'):
        identity.update(hashlib.sha256(source.read_bytes()).digest())
    (runtime / 'cache-version').write_text(identity.hexdigest() + '\n')
    files = {str(path.relative_to(out)): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted(out.rglob('*')) if path.is_file() and path.name != 'manifest.json'}
    # The runtime's identity, from every file's digest: tools/build-production.sh pins a copy in the title folder, and
    # the launcher starts no game on a runtime whose stamp differs (proton_runtime_problem, ps5/src/proton_launch.cpp).
    stamp = hashlib.sha256(''.join(f'{name}\0{digest}\n' for name, digest in files.items()
                                   if name.startswith('runtime/')).encode()).hexdigest() + '\n'
    (runtime / 'runtime-stamp').write_text(stamp)
    files = dict(sorted({**files, 'runtime/ge11-7/runtime-stamp': hashlib.sha256(stamp.encode()).hexdigest()}.items()))
    (out / 'manifest.json').write_text(json.dumps(files, indent=2) + '\n')
    print(f'Staged {len(files)} files; manifest: {out / "manifest.json"}')


if __name__ == '__main__':
    main()
