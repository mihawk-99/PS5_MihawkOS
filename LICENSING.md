# Licence: GPL-3.0-or-later

MihawkOS (this repository, PS5_Proton) is licensed under the **GNU General Public License, version 3 or (at your
option) any later version**. The full text is in [`LICENSE`](LICENSE); every source file written here carries
`SPDX-License-Identifier: GPL-3.0-or-later`, except `tests/shaders/d3d11-triangle.hlsl`, whose bytes the generated
`tests/shaders/d3d11-triangle.h` pins by SHA-256 and which is GPL-3.0-or-later like that header, and the browser
prototype `prototypes/windows11-shell/`, whose licence its README states. Decided by the
project's author on 2026-10-15, replacing the LGPL-2.1-or-later proposal.

## Why this one

- **It is what a title already was.** The title links the GPL-3.0-or-later platform layer of `PS5_PayloadSDK`, the
  launcher UI comes from the GPL-3.0-or-later template module, and the OpenGL runtime links the GPL-3.0-or-later PS5
  OpenGL SDK. Every binary this project ships was GPL-3.0 already; one licence for the sources says so plainly.
- **It keeps improvements shared.** The value of a compatibility layer is a long tail of small fixes; copyleft keeps
  that tail returning to the project.
- **Its inputs allow it.** Wine, VKD3D-Proton and prospero-win are LGPL-2.1-or-later, which section 3 of that licence
  lets a recipient distribute under the GPL, version 2 or later; MIT, zlib, BSD and OFL inputs combine with GPL-3.0.

## What this means in practice

- **Windows programs are not covered.** MihawkOS runs Windows programs the user supplies; running a program on the
  layer is use, not the making of a combined work, and their licences are their own.
- **Upstream keeps its own licence.** Wine (LGPL-2.1-or-later), DXVK (zlib), VKD3D-Proton (LGPL-2.1-or-later), Mesa
  (MIT), FFmpeg (LGPL-2.1-or-later, linked statically into `winedmo.prx` and rebuildable from the pins here), Proton's bundled components (see the fork's `dist.LICENSE`) and everything in [`NOTICE.md`](NOTICE.md) are
  pinned with their notices.
- **Patches to an upstream project keep that project's licence**, so they stay mergeable there: `wine/patches/`
  (LGPL-2.1-or-later, as Wine) and `platform/graphics/patches/` (the graphics foundation's overlay, as recorded in
  `NOTICE.md`; since 2026-11-01 its patches are commits of mihawk-99/PS5_Vulkan itself and the folder is empty).
- **Imported code keeps its licence.** Four files carry upstream code and stay LGPL-2.1-or-later:
  `wine/ps5/pw_ws2_32_libc.c` and `tests/test_pw_ws2_32_libc.c` (from prospero-win), `wine/ps5/pw_dns_unix.c` (Wine's
  WoW64 marshalling) and `wine/wineps5/unix.c` (Wine's audio drivers and kernelbase). Files under `external/`, Sascha Willems'
  MIT code in `base/` ([`base/LICENSE.md`](base/LICENSE.md)), the title-foundation files from PS5_VulkanTemplate that
  say `SPDX-License-Identifier: MIT` and the one OFL-1.1 file keep theirs.
- **Derived work is credited.** The PS5 patch series starts from prospero-win's and says so in every patch header and
  in [`NOTICE.md`](NOTICE.md).
- **Fonts and artwork keep their licences.** Proton UI fonts are OFL-1.1, Microsoft Fluent command glyphs MIT, the
  Wine theme, the fourteen original icons in `themes/windows11/icons/original/`, the Mihawk mark
  (`themes/windows11/start.svg`, the Start button's glyph) the Bubble Earth, Glass City and Aqua World wallpapers and the Aero Meadow one with the scripts that draw
  them (`tools/make-aero-wallpapers.py`, `tools/make-mihawk-wallpaper.py`), and the README's banner, dividers and heading
  orbs in `docs/media/readme/` (they draw the mark and those icons; `docs/README_ART.md`) LGPL-2.1-or-later; the README's
  badges in `docs/media/readme/badges/` hold no third-party art and are GPL-3.0-or-later; no proprietary Microsoft fonts or
  Windows icon resources are bundled, and no Windows logo is shown.
- **The desktop's wallpaper is the exception.** Bubble City (`themes/windows11/wallpapers/skeuoss-bubble-city.jpg`) is
  the artist Skeuoss's work (also the title's home-screen background, `ps5/sce_sys/pic0.dds` and `pic1.dds`), used and
  redistributed with their permission (2026-10-31, [`NOTICE.md`](NOTICE.md)),
  under no open licence: none of this project's licences cover it.
- **Source ships with binaries.** Any binary release comes with the corresponding source, including the patch series
  and pins that produce the runtime.
- **Game data is never distributed.** The user supplies their own copy; nothing here fetches, bundles or
  redistributes it.

## Trademarks

PlayStation, PS5 and DualSense are trademarks of Sony Interactive Entertainment Inc. Windows is a trademark of
Microsoft Corporation. Proton and Steam are trademarks of Valve Corporation. They are named to say what this
project runs on and is compatible with, and which desktop style its theme follows. PS5_Proton is not affiliated with,
sponsored by or endorsed by Sony, Microsoft, Valve, Shueisha or Toei Animation. Plain-language notes on what the
project contains and who is responsible for what: [`LEGAL.md`](LEGAL.md).
