#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The licence texts the shipped binaries need, staged beside them (NOTICE.md; docs/PUBLICATION_CHECKLIST.md, 5).

  stage-licences.py runtime DIR   DIR/share/licenses/<component>/<file>, beside what tools/stage-wine.py stages itself
                                  (GE-Proton's notices, DXVK, Proton UI, c-ares, FFmpeg)
  stage-licences.py title DIR     DIR/licenses/<component>/<file>, the title folder (dist/<title id>)
  stage-licences.py --list        every destination, one per line ("runtime <path>" or "title <path>")

A source is a file of this repository, or of a pinned input the build already has in .deps, or one of the small
pinned downloads below, kept in .deps/licence-texts and checked by SHA-256 like the CA bundle. A missing or changed
source stops the build: a binary is never staged without its text.
"""
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
CACHE = DEPS / 'licence-texts'

# The texts no build input carries, each at a fixed upstream tag. Wine Mono's MSI carries no licence; ICU's DLLs come
# from the GE-Proton archive, which names ICU nowhere (its exact ICU 68 revision is not recorded: 68-2's text); the CA
# bundle is MPL-2.0 without the licence; llvm-mingw links mingw-w64's runtime into every PE DLL it builds (its headers
# say 15.0.0-alpha, a snapshot: the last release's notice).
DOWNLOADS = {
    'wine-mono-COPYING': ('https://raw.githubusercontent.com/madewokherd/wine-mono/wine-mono-10.4.1/COPYING',
                          '0265443ffc97c9450dbcf402c2430bf9fe87d7a6c92d5d7684fcecfe749bf75d'),
    'icu-68-LICENSE': ('https://raw.githubusercontent.com/unicode-org/icu/release-68-2/icu4c/LICENSE',
                       '7915b19db903070778581ae05d8bf4ea241b34a05deb51ca4f5cbb15ea1cbba3'),
    'MPL-2.0.txt': ('https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/MPL-2.0.txt',
                    '66a3107d5ad6a058aab753eaac2047ccb2ed0e39465dd0fe5844da3e300d5172'),
    'COPYING.MinGW-w64-runtime.txt': (
        'https://raw.githubusercontent.com/mingw-w64/mingw-w64/v14.0.0/COPYING.MinGW-w64-runtime/'
        'COPYING.MinGW-w64-runtime.txt', '1db8da07b436c68833c0673ffee3d9fcb2526047f3820b81661865dfedc79a1f'),
    # The GE-Proton archive's arial/times/cour.ttf are Liberation 1.x (their name tables say "Liberation Fonts license",
    # GPL-2.0 with font exceptions; their bold faces are 2.x, OFL): the 1.07.4 text, from Debian's fixed packaging tag.
    'liberation-1.07-License.txt': (
        'https://salsa.debian.org/fonts-team/fonts-liberation/-/raw/debian/1%251.07.4-11/License.txt',
        'c40dd6adebad817defd68c7edad151d9c13d2ac3b4e1790ce2b727b024438b6d'),
    'GPL-2.0-only.txt': ('https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/GPL-2.0-only.txt',
                         'aaf135472f81c5b4a0dca9367e5bb5e9750032b5bebe5442b36e4c0a47430df3'),
    # Wine Mono's parts (its COPYING): the compiler is LGPL v2, SharpZipLib GPL with an exception, FNA partly MS-PL.
    'LGPL-2.0-only.txt': ('https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/LGPL-2.0-only.txt',
                          '86dc99d7e5060915ab1dfc1378b7dd351c62088bfa74067e8aa1868c6fdba7d8'),
    'MS-PL.txt': ('https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/MS-PL.txt',
                  '7a162b1da10f1c22db4c68f07bec4a8355259f8d4aab5b00a2b2bbd423d833dd'),
    # tinygltf's nlohmann/json (3.5.0, compiled in by base/VulkanglTFModel.cpp); its stb_image is public domain.
    'nlohmann-json-LICENSE.MIT': ('https://raw.githubusercontent.com/nlohmann/json/v3.5.0/LICENSE.MIT',
                                  '7b875bb7a4e28abb7956ea821b4b7d88ff6a2a4fabf261c3993730a365ec56dd'),
}

# What tools/stage-wine.py stages itself, under the runtime's share/licenses (for the checks of a staged tree).
STAGE_WINE = ['ge-proton/LICENSE', 'ge-proton/LICENSE.OFL', 'dxvk-patched/DXVK.txt', 'dxvk-patched/dxbc-spirv.txt',
              'dxvk-patched/libdisplay-info.txt', 'dxvk-patched/Vulkan-Headers.txt', 'dxvk-patched/SPIRV-Headers.txt',
              'dxvk-patched/dxbc-SPIRV-Headers.txt', 'vkd3d-patched/VKD3D-Proton.txt',
              'vkd3d-patched/VKD3D-Proton-COPYING.txt', 'vkd3d-patched/dxil-spirv.txt', 'vkd3d-patched/dxbc-spirv.txt',
              'vkd3d-patched/Vulkan-Headers.txt', 'vkd3d-patched/SPIRV-Headers.txt',
              'vkd3d-patched/dxil-spirv-SPIRV-Headers.txt', 'vkd3d-patched/bc-decoder.txt', 'vkd3d-patched/glslang-spirv.txt', 'proton-ui/OFL.txt', 'proton-ui/theme-LGPL.txt',
              'proton-ui/CascadiaMono-OFL.txt', 'proton-ui/Fluent-LICENSE', 'proton-ui/Fluent-NOTICE', 'cares/LICENSE.md',
              'ffmpeg/LICENSE.md', 'ffmpeg/COPYING.LGPLv2.1']

# Short notes written here: which text covers which file, and where a binary's source is.
TEXTS = {
    'fonts-README': (
        'The fonts in lib/wine/prefix/drive_c/windows/Fonts come from the GE-Proton11-7 archive, Wine and this project.\n'
        '- arial.ttf, times.ttf, cour.ttf: Liberation 1.x by Red Hat, renamed by Proton, under the Liberation Fonts\n'
        '  license (GPL-2.0 with font exceptions): Liberation-1.x-License.txt and GPL-2.0.txt here.\n'
        '- msyh.ttf: Droid Sans Fallback (Google) with WenQuanYi glyphs, under the Apache License 2.0: Apache-2.0.txt.\n'
        '- The Liberation 2.x faces (arialbd.ttf, courbd.ttf, georgia.ttf and others), Source Han Sans, Noto and Ume:\n'
        '  ../ge-proton/LICENSE and ../ge-proton/LICENSE.OFL.\n'
        "- Wine's own fonts (marlett.ttf, tahoma.ttf, symbol.ttf, webdings.ttf, wingding.ttf and the .fon files):\n"
        '  ../wine/COPYING.LIB (LGPL-2.1-or-later).\n'
        '- Proton UI and Cascadia Mono: ../proton-ui.\n'),
    'wine-mono-SOURCE': (
        'Wine Mono 10.4.1, installed into each prefix from wine-mono-10.4.1-x86.msi (SHA-256\n'
        '071f4b2887e1c97a11d791ff3d65be9429eed6dec4c2708888bfd546ba358e23), is built from its source at\n'
        'https://dl.winehq.org/wine/wine-mono/10.4.1/wine-mono-10.4.1-src.tar.xz\n'
        "COPYING here says which parts are under which licence; the GPL, LGPL and MS-PL texts are beside it.\n"),
    'wine-gecko-SOURCE': (
        'Wine Gecko 2.47.4, installed into each prefix from wine-gecko-2.47.4-x86_64.msi and wine-gecko-2.47.4-x86.msi\n'
        '(SHA-256 e590b7d988a32d6aa4cf1d8aa3aa3d33766fdd4cf4c89c2dcc2095ecb28d066f and\n'
        '26cecc47706b091908f7f814bddb074c61beb8063318e9efc5a7f789857793d6), is Mozilla code under the Mozilla Public\n'
        'License 2.0 (MPL-2.0.txt here). Its source is at\n'
        'https://dl.winehq.org/wine/wine-gecko/2.47.4/wine-gecko-2.47.4-src.tar.xz\n'
        "The full licence page of its components is inside the package (omni.ja, chrome/toolkit/content/global/license.html).\n"),
}

OWN = {'ps5-proton/LICENSE': 'LICENSE', 'ps5-proton/NOTICE.md': 'NOTICE.md', 'ps5-proton/LICENSING.md': 'LICENSING.md'}
# Mesa's licence page, the same at the RADV (PS5_Vulkan's export) and WGL/Zink (b3588f78) revisions.
MESA = '.deps/mesa-foundation-wgl/source/docs/license.rst'

# The PS5 OpenGL SDK 1.0.2 (tools/fetch-opengl-sdk.sh), statically linked into the runtime's win32u.prx: its licence,
# its third-party notices and every component licence its package carries.
OPENGL_SDK = '.deps/ps5-opengl-sdk-1.0.2'
OPENGL_SDK_TEXTS = ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'LICENSES/Dear-ImGui.txt', 'LICENSES/Mesa/Apache-2.0', 'LICENSES/Mesa/BSL-1.0', 'LICENSES/Mesa/exceptions/Linux-Syscall-Note', 'LICENSES/Mesa/GPL-1.0-or-later', 'LICENSES/Mesa/GPL-2.0-only', 'LICENSES/Mesa/MIT', 'LICENSES/Mesa/SGI-B-2.0', 'LICENSES/NanoVG.txt', 'LICENSES/OpenGNM-PSBC.txt', 'LICENSES/OpenGNM.txt', 'LICENSES/SDL2.txt', 'LICENSES/Sokol-Samples.txt', 'LICENSES/Sokol.txt', 'LICENSES/SPIRV-Headers.txt', 'LICENSES/vecmath.txt', 'LICENSES/VK-GL-CTS.txt', 'LICENSES/Vulkan-Headers.txt']

RUNTIME = {
    **OWN,
    'wine/COPYING.LIB': '.deps/wine-proton/COPYING.LIB', 'wine/LICENSE': '.deps/wine-proton/LICENSE',
    'wine/AUTHORS': '.deps/wine-proton/AUTHORS',
    'vkd3d-proton/COPYING': '.deps/vkd3d-proton/COPYING', 'vkd3d-proton/LICENSE': '.deps/vkd3d-proton/LICENSE',
    'vkd3d-proton/AUTHORS': '.deps/vkd3d-proton/AUTHORS',
    'gnutls/COPYING': '.deps/wine-ps5/gnutls/gnutls/COPYING',
    'gnutls/COPYING.LESSERv2': '.deps/wine-ps5/gnutls/gnutls/COPYING.LESSERv2',
    'gnutls/AUTHORS': '.deps/wine-ps5/gnutls/gnutls/AUTHORS',
    'nettle/COPYING.LESSERv3': '.deps/wine-ps5/gnutls/nettle/COPYING.LESSERv3',
    'nettle/COPYINGv2': '.deps/wine-ps5/gnutls/nettle/COPYINGv2', 'nettle/COPYINGv3': '.deps/wine-ps5/gnutls/nettle/COPYINGv3',
    'nettle/AUTHORS': '.deps/wine-ps5/gnutls/nettle/AUTHORS',
    'freetype/LICENSE.TXT': '.deps/wine-ps5/freetype/src/LICENSE.TXT',
    'freetype/FTL.TXT': '.deps/wine-ps5/freetype/src/docs/FTL.TXT',
    'freetype/GPLv2.TXT': '.deps/wine-ps5/freetype/src/docs/GPLv2.TXT',
    'mesa/license.rst': MESA,
    'wine-mono/COPYING': 'download:wine-mono-COPYING',
    'icu/LICENSE': 'download:icu-68-LICENSE',
    'ca-certificates/MPL-2.0.txt': 'download:MPL-2.0.txt',  # Wine Gecko's own page is in its MSI (license.html)
    'llvm-mingw/LICENSE.TXT': '.deps/llvm-mingw/LICENSE.TXT',
    'mingw-w64/COPYING.MinGW-w64-runtime.txt': 'download:COPYING.MinGW-w64-runtime.txt',
    'fonts/README.txt': 'text:fonts-README',
    'fonts/Liberation-1.x-License.txt': 'download:liberation-1.07-License.txt',
    'fonts/GPL-2.0.txt': 'download:GPL-2.0-only.txt',
    'fonts/Apache-2.0.txt': 'ps5/licenses/Apache-2.0.txt',
    'wine-mono/SOURCE.txt': 'text:wine-mono-SOURCE',
    'wine-mono/GPL-2.0.txt': 'download:GPL-2.0-only.txt',
    'wine-mono/LGPL-2.0.txt': 'download:LGPL-2.0-only.txt',
    'wine-mono/LGPL-2.1.txt': '.deps/wine-proton/COPYING.LIB',
    'wine-mono/MS-PL.txt': 'download:MS-PL.txt',
    'wine-gecko/SOURCE.txt': 'text:wine-gecko-SOURCE',
    'wine-gecko/MPL-2.0.txt': 'download:MPL-2.0.txt',
    **{f'ps5-opengl/{name}': f'{OPENGL_SDK}/{name}' for name in OPENGL_SDK_TEXTS},
}

TITLE = {
    **OWN,
    'lapy-daemon/LICENSE': 'platform/lapy/LICENSE',
    'mesa/license.rst': MESA,
    'sascha-willems-vulkan/LICENSE.md': 'base/LICENSE.md',
    'volk/LICENSE.md': 'ps5/third_party/volk/LICENSE.md',
    'imgui/LICENSE.txt': 'external/imgui/LICENSE.txt',
    'ktx/LICENSE.md': 'external/ktx/LICENSE.md',
    'tinygltf/LICENSE': 'external/tinygltf/LICENSE',
    'nlohmann-json/LICENSE.MIT': 'download:nlohmann-json-LICENSE.MIT',
    'glm/manual.md': 'external/glm/manual.md',  # its revision has no copying.txt; the licences are the manual's section 0
    'zstd/LICENSE': 'external/zstd/LICENSE',
    'mbedtls/LICENSE': '.deps/mbedtls-3.6.7/LICENSE',   # tools/setup-mbedtls.sh, run by the title's CMake
    'monocypher/LICENCE.md': 'external/monocypher/LICENCE.md',
    'libmicrohttpd/COPYING': '.deps/webui/libmicrohttpd-1.0.10/COPYING',   # tools/build-webui-http.sh, run by the title's CMake
}


def source(name):
    """The file a table entry names, fetched and checked first if it is a pinned download, written if it is a note."""
    if name.startswith('text:'):
        CACHE.mkdir(parents=True, exist_ok=True)
        path = CACHE / (name[len('text:'):] + '.txt')
        path.write_text(TEXTS[name[len('text:'):]])
        return path
    if not name.startswith('download:'):
        path = ROOT / name
        if not path.is_file():
            raise SystemExit(f'stage-licences: {name} is missing (its pinned input is not built or fetched)')
        return path
    url, digest = DOWNLOADS[name[len('download:'):]]
    path = CACHE / name[len('download:'):]
    if not path.is_file():
        CACHE.mkdir(parents=True, exist_ok=True)
        subprocess.run(['curl', '-sSfL', '-o', str(path), url], check=True)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f'stage-licences: {path} does not match its pinned SHA-256 ({url})')
    return path


def stage(table, destination):
    paths = {dest: source(name) for dest, name in table.items()}  # every source checked before anything is written
    for dest, path in paths.items():
        (destination / dest).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination / dest)


def main(argv):
    if argv == ['--list']:
        print('\n'.join([f'runtime {d}' for d in RUNTIME] + [f'title {d}' for d in TITLE]))
    elif len(argv) == 2 and argv[0] == 'runtime':
        stage(RUNTIME, Path(argv[1]) / 'share/licenses')
    elif len(argv) == 2 and argv[0] == 'title':
        target = Path(argv[1]) / 'licenses'
        shutil.rmtree(target, ignore_errors=True)  # the title folder outlives builds; no stale text stays
        stage(TITLE, target)
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
