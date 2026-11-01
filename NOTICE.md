# Provenance and attribution

## Licence

PS5_Proton (MihawkOS) is GPL-3.0-or-later; see [`LICENSE`](LICENSE) and [`LICENSING.md`](LICENSING.md).
Every source file written here carries an SPDX identifier (exceptions named in `LICENSING.md`); imported upstream
code and patches to upstream projects keep their upstream licence. Game data is never included, fetched or
redistributed by this project.

## Derived from prospero-win

[prospero-win](https://github.com/mpereiraesaa/prospero-win) (mpereiraesaa) is
LGPL-2.1-or-later and is the origin of the PS5 platform work this project builds on:

- the **PS5 patch series** in `wine/patches/` starts from prospero-win's series against
  Wine `490f6d5dcbb2a5047345b8af88d114bbcaad69a8` (wine-mirror/wine). Every patch that
  comes from there keeps its authorship and records the rebase it needed for the Proton
  Wine tree. The series is LGPL-2.1-or-later, as Wine, at directory level
  ([`LICENSING.md`](LICENSING.md)); only 13 of its 305 patches contain
  an SPDX line, and the ones from prospero-win had none upstream either.
- the **`/data` grant protocol** (the same-UID call, the atomic `{"PID":<pid>}` request) comes from
  its `native/pw_data_mount.{c,h}`; the client this title now runs is the platform layer's
  (below);
- the **platform knowledge** — Wine as PRX modules, the in-process wineserver, the
  `win32u` PS5 user driver, the winevulkan display-plane patches, the audio driver, the
  XInput mapping, the `/data` mount and the one-game-per-process launcher — comes from
  its `docs/WINE_PS5_BUILD.md` and `docs/ARCHITECTURE.md`.

The runtime adapters are pinned to upstream revision
`c8a2f00137fb0a95f88541252b4e47f53f9677e6`; `wine/ps5/pw_ws2_32_libc.c` and
`tests/test_pw_ws2_32_libc.c` were taken again at `d1ca1e597648f28e1f1d9c55f7d7ba9b1f0a0690` (prospero-win's
resolver, its pull requests 332 and 335; one argument-order fix for clang 23 here), and
`wine/patches/0918-server-ps5-run-requests-on-client-threads.patch` is its patch 0770 at
`1532474b0f792378b9fbe3f4f0282c4ed030e7d5` (its pull requests 341 and 342), rebased onto this tree. All 43 imported source/test files
were compared byte-for-byte with that checkout before local adaptations; original
hashes are in `wine/prospero-import.sha256` and adaptations are in
`wine/prospero-adaptations.patch`. They include `wine/ps5/`, `wine/wineps5/`,
`src/pw_*`, `native/pw_audio_ps5.*`, `include/prospero_win.h`, the `test_pw_*`
tests and the descriptor/RADV tools. `tools/build-wine-ps5.sh` is adapted from
that revision's `tools/build_wine_ps5.sh` for the pinned Proton tree.

Adaptations use Proton's audio main-loop ABI, export its win32u Unix-call table,
and omit GDI display helpers absent from this RADV revision. The current title
does not use a libc DNS resolver. Its PS5 resolver adapter uses the console's
network library in the owner title; helpers forward lookups over their control
connection. Windows TLS uses the pinned GnuTLS stack described below.

DXVK's PE64 `d3d11`, `dxgi` and `d3d10core` binaries are extracted from the
GE-Proton11-7-x86_64 release archive, SHA-256
`c5448b76a230384e2d7bc6beb5ccb97bafb7e2c3b6c527cb03a1a546bbcb00a0`.
The upstream DXVK source revision is the GE fork's pin `601930949d111edbbcf9dd463948426d9f8f6ddd` (zlib).
For 32-bit programs, the same archive's i386 builds of those DLLs, of `d3d9` and `d3d8`, and of VKD3D-Proton's
`d3d12`/`d3d12core` are extracted too (`tools/extract-dxvk.py`) and staged into a prefix's `syswow64`.

VKD3D-Proton's `d3d12` and `d3d12core` are source-built in both architectures (`tools/build-vkd3d.py`) at the same
revision GE-Proton11-7 ships, `af89350cc2eacd9da2293fbae96bd9ab4987c9bb` (LGPL-2.1-or-later), exported by Git with each
submodule at the revision that commit records, and staged over GE's prebuilt builds. Its owned patch series
`platform/vkd3d-proton/patches/` is LGPL-2.1-or-later like the code it changes: 0001 lets feature level 12_0 rest on
tiled resources tier 1, the console's ceiling (sparse binding without PRT, a game's through the GPU broker's patch
0041). The problem was found by reading PC5's
`patches/vkd3d-proton/0001-vkd3d-Feature-level-12_0-with-tiled-resources-tier-1.patch`
([bornaradusin/PC5](https://github.com/bornaradusin/PC5) at `81323dc3`, LGPL-2.1); our patch is independently
written. 0002 names the build. 0003 makes a shared resource or fence ordinary when the device has no Win32 external
handles, and `CreateSharedHandle` refuse it; with Wine patch 2407, which allocates memory asked to be exported as
ordinary memory when there is no host handle type. Their problem was found by reading PC5's vkd3d-proton 0002 and
prospero-win 0054 ([bornaradusin/PC5](https://github.com/bornaradusin/PC5) at `81323dc3`, LGPL-2.1); both patches are
independently written. Its licence texts and those of its compiled-in subprojects are staged in the runtime's
`share/licenses/vkd3d-patched/`.

The shared prefix baseline also extracts that same verified GE-Proton11-7 archive's
ICU 68 Windows DLLs and default font collection for both architectures. ICU carries
the Unicode/ICU permissive licence; the font collection includes Liberation,
Source Han Sans and Noto under SIL OFL 1.1, Ume under its permissive font licence,
and Wine's bundled fonts. The archive's `LICENSE` and `LICENSE.OFL`, including
component attributions, are staged at `runtime/ge11-7/share/licenses/ge-proton/`.
`tools/stage-wine.py` verifies the archive SHA-256 before extracting these assets;
alternative per-game font collections and Linux binaries are not extracted.

The experimental headless helper uses PS5_Vulkan's GPL-3.0-or-later native
converter at `ab37acd506305ba3e575435e3ae9a351cfaf7cf4` (BlackBearReloaded's
ps5-native-app-boilerplate tooling). `tools/build-helper-converter.py` exports
that exact local revision into an ignored build cache and applies
`tools/patches/native-helper-preload.patch`; no sibling source is modified.
The patch adds an optional process-parameter preload mask and its relocation.
The ordinary title converter and RADV recipe remain the existing dependencies.

The PRX conversion tool is built from the GPL-3.0-or-later fork
`mpereiraesaa/ps5-native-app-boilerplate` at
`30597512539e7edfde079cbcaf4a626bc0a948c5`. FreeType 2.13.3 is built from its
SHA-256-pinned upstream tarball (FreeType License/GPLv2); the build script records
the hash. RADV is linked by PS5_Vulkan's recipe from its recorded PS5_Mesa pin.

The retained title-layout OpenGL backend statically links [PS5 OpenGL SDK 1.0.2](https://github.com/darkxex/ps5-opengl)
by BlackBearReloaded, from runtime source `a519561626933257213d64a4a8bd5e02b55dceb2`. The original repository
(blackbearreloaded/ps5-opengl) is gone; the release is fetched from darkxex's mirror of it.
`tools/fetch-opengl-sdk.sh` verifies release archive SHA-256
`30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4`
and its SDK manifest. The package includes corresponding source archives,
`LICENSE` (GPL-3.0-or-later), `THIRD_PARTY_NOTICES.md`, component `LICENSES/`,
and provenance. Mesa/OpenGNM and other components retain their own notices.
The package labels this binary host-checked, not console-validated; this project's
console evidence is recorded separately in `docs/PLAN.md`. Production payload
OpenGL instead defaults to the foundation's PE64 Mesa/WGL/Zink build; the older
SDK remains a dependency of the diagnostic title-layout runtime.

The IA-32 translator (`wine/wowprospero`) is **not** used here: 32-bit programs run
natively under WoW64 ([`docs/IA32_DESIGN.md`](docs/IA32_DESIGN.md)).

## The `/data` client: PS5-Lapy-JB-Daemon and ps5-native-app-boilerplate

A title cannot see `/data` unless the process is made privileged enough, and this project
asks the upstream [PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon)
(mpereiraesaa; credit for the owned-root design, the daemon and the cooperative protocol
belongs to that project, to Arksama / Team PHU and to its contributors) through the platform
layer's `ps5platform/elevation.h` (PS5_PayloadSDK, `platform/docs/ELEVATION.md`). That client
is derived from the one [ps5-native-app-boilerplate](https://github.com/BlackBearReloaded/ps5-native-app-boilerplate)
(BlackBearReloaded, **GPL-3.0-or-later**) adopted in its pull request 4, ported to C, with its
copyright and SPDX headers carried over. Daemon patches and a local validation builder
are kept under `platform/lapy/` and `tools/build-lapy-validation.py`; exported
upstream sources and built payloads stay in ignored `.deps/`. The helper ELF and the elfldr client this title carried before
(`ps5/src/elevation/`, `ps5/third_party/sandbox-elevator.elf`) are removed, and there is no
fallback to them; the helper's two committed builds are described below.

The validation daemon is pinned to `5b8397b9f2b5f12a7bc2f9c8745a00d1c2dd01ad`,
MIT, copyright Arksama / Team PHU (`platform/lapy/LICENSE`). Its patches preserve
upstream root-transfer code and are described in `platform/lapy/README.md`.
The read-only prospero-win logger header is LGPL-2.1-or-later under that project's
licence, pinned by SHA-256
`394af67d0f8b60b3335deb53396e52855ea2daa50ca914a456ea7663f48900c6`.
The daemon is linked with the upstream PS5 payload SDK v0.42 (github.com/ps5-payload-dev/sdk, tag `v0.42`, commit
`4eb701204fc3f8d31e84cf8ca272974e2be9c867`; release archive `ps5-payload-sdk.zip`, SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`). Its crt and libc system-call stubs are
GPL-3.0-or-later (John Törnblom and contributors), so the shipped `lapy-root-daemon.elf` is GPL-3.0-or-later as a
whole, while Lapy's own code is MIT; the GPL text ships as `licenses/ps5-proton/LICENSE`. No payload binary
is in the current tree; the history holds the two helper builds described next.

## Historical helper binaries

Two builds of the sandbox-elevation helper were committed as
`ps5/third_party/sandbox-elevator.elf` and removed in `a138409`. Both are unstripped
GPL-3.0-or-later programs of [ps5-native-app-boilerplate](https://github.com/BlackBearReloaded/ps5-native-app-boilerplate)
(BlackBearReloaded), built with the payload SDK's toolchain (the binaries record clang
22.1.8; the SDK revision was not recorded). Their exact corresponding source is public:

- **`5d37298`, blob `b8d6e0a1aa0c4a5c7d3d93f7bfc884348f27cf95`**, built for title
  `PPSA99008` from [PS5_ProsperoEden](https://github.com/mihawk-99/PS5_ProsperoEden)
  `headless/elevation/helper/` at `51544b7d054b4987c26fc2154b6fae811a25cff5`: its
  `main.cpp` and `Makefile` with `headless/elevation/protocol.hpp`. Those are the
  boilerplate's `examples/sandbox-elevation/payload/main.cpp`, `payload/Makefile` and
  `protocol.hpp` at `dd44bbdc75437332ed22e3ba95126733419ef25a`, with only
  `target_title_id` changed from `PPSA99790` to `PPSA99008`. The binary was not rebuilt to
  compare bytes; its title id, its embedded source folder and its absolute debug paths
  (the path-mapping flags added in that repository's later `b5ad8aa` are absent) match
  that revision, the only one of the helper's `main.cpp` in its history.
- **`991911a`, blob `92cc2ef1dd834b5ac9b16f2bfd141b9421f824d6`**, built for title
  `PPSA99170` by that commit's `tools/build-elevator.sh`, which copies the boilerplate's
  `examples/sandbox-elevation/payload/` and `protocol.hpp` at
  `dd44bbdc75437332ed22e3ba95126733419ef25a` and changes only `target_title_id` to
  `PPSA99170`. The binary is byte-identical to that script's build output, and the build
  copy of the source differs from the boilerplate revision in that one line.

A title as distributed is GPL-3.0 (it links the GPL-3.0-or-later platform layer), as is this
project's own source (GPL-3.0-or-later since 2026-10-15).

## Pinned upstreams

| Component | Source | Licence |
| --- | --- | --- |
| Proton runtime stack | [`mihawk-99/proton-ge-custom`](https://github.com/mihawk-99/proton-ge-custom), fork of [GloriousEggroll/proton-ge-custom](https://github.com/GloriousEggroll/proton-ge-custom) `GE-Proton11-7` | see the fork's `LICENSE`, `LICENSE.proton`, `dist.LICENSE` |
| Wine (Proton's tree) | the fork's `wine` submodule ([ValveSoftware/wine](https://github.com/ValveSoftware/wine)) | LGPL-2.1-or-later |
| Wine staging patches | the fork's `wine-staging` submodule | LGPL-2.1-or-later |
| DXVK | [doitsujin/dxvk](https://github.com/doitsujin/dxvk) `601930949d111edbbcf9dd463948426d9f8f6ddd`, source-built in both architectures with owned `platform/dxvk/patches/` | zlib |
| DXVK shader compiler | [doitsujin/dxbc-spirv](https://github.com/doitsujin/dxbc-spirv) `bf14419e5fa7eacb817b7b632f03cb61d61bbad7` | MIT |
| DXVK display metadata | [doitsujin/libdisplay-info](https://github.com/doitsujin/libdisplay-info) `275e6459c7ab1ddd4b125f28d0440716e4888078` | MIT |
| DXVK Vulkan headers | [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77` | Apache-2.0 or MIT |
| DXVK SPIR-V headers | [KhronosGroup/SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) `04f10f650d514df88b76d25e83db360142c7b174`, compiler copy `c8ad050fcb29e42a2f57d9f59e97488f465c436d` | MIT |
| VKD3D-Proton | [HansKristian-Work/vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) `af89350cc2eacd9da2293fbae96bd9ab4987c9bb` (the fork's `vkd3d-proton` submodule), source-built in both architectures with owned `platform/vkd3d-proton/patches/` | LGPL-2.1-or-later |
| VKD3D-Proton's shader compilers | [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv) `bed30ff2115d845fb6bf8752bcbe1c681efb0e01`, its [doitsujin/dxbc-spirv](https://github.com/doitsujin/dxbc-spirv) `37a97745bddaf56d717253b0e4565904ce5eb06c` | MIT |
| dxil-spirv's compiled-in third-party code | `third_party/bc-decoder` (RenderDoc's LLVM bitcode reader, Baldur Karlsson), `third_party/glslang-spirv` (glslang's SPIR-V builder, LunarG and Google), `third_party/spirv-headers` (KhronosGroup/SPIRV-Headers `c63848ecf2200425511319fd8bf2c17b751e501e`), all at dxil-spirv `bed30ff2115d845fb6bf8752bcbe1c681efb0e01`; its `SPIRV-Cross` and `SPIRV-Tools` are not built | MIT; BSD-3-Clause; MIT |
| VKD3D-Proton's headers | [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) `ee2ec5fd83dafce291024683b50dc89219333076`, [KhronosGroup/SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) `f88a2d766840fc825af1fc065977953ba1fa4a91` | Apache-2.0 or MIT |
| Title foundation | [mihawk-99/PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate), a fork of [SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan) at `41a4410243fca7640a2dd0115a5ff5cb9a29494b` | MIT: Sascha Willems' examples ([`base/LICENSE.md`](base/LICENSE.md)) and the template's own files (`SPDX-License-Identifier: MIT`) |
| GLM 0.9.9.0 (`external/glm`: headers, CMake files and manual; its `doc/`, `test/` and `util/` are not carried) | [g-truc/glm](https://github.com/g-truc/glm) `1ad55c5016339b83b7eec98c31007e0aee57d2bf`, SaschaWillems/Vulkan's submodule pin | Happy Bunny (modified MIT) or MIT; the text is in `external/glm/manual.md` (that revision has no `copying.txt`) |
| Dear ImGui 1.65 (`external/imgui`) | as vendored by SaschaWillems/Vulkan `41a4410`; ocornut/imgui revision not recorded | MIT (`LICENSE.txt`) |
| libktx (`external/ktx`), without its ETC decoder (`etcdec.cxx`, `etcunpack.cxx`, under Ericsson's own licence, are not carried) | as vendored by SaschaWillems/Vulkan `41a4410`; KhronosGroup/KTX-Software revision not recorded | Apache-2.0 (`LICENSE.md`, which is also its NOTICE); `lib/uthash.h` revised BSD (in `LICENSE.md`); `other_include/KHR/khrplatform.h` Khronos MIT-style (in its header) |
| tinygltf 2.4.2 with nlohmann/json 3.5.0 and stb_image 2.21 (`external/tinygltf`) | as vendored by SaschaWillems/Vulkan `41a4410`, `tiny_gltf.h` with the template's PS5 change; upstream revisions not recorded | MIT; stb_image MIT or public domain |
| Vulkan 1.4.355 headers (`external/vulkan`) and Vulkan Video codec headers (`external/vk_video`) | as vendored by SaschaWillems/Vulkan `41a4410`; KhronosGroup revision not recorded | Apache-2.0 OR MIT (`vk_icd.h` and `vk_layer.h`: Apache-2.0) |
| volk (`ps5/third_party/volk`) | [zeux/volk](https://github.com/zeux/volk) tag `vulkan-sdk-1.4.350.1`, byte-identical | MIT (`LICENSE.md`) |
| RADV | [mihawk-99/PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [mihawk-99/PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) | MIT (Mesa) |
| Platform layer | [mihawk-99/PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | GPL-3.0-or-later |
| GnuTLS 3.8.9 (`libgnutls.prx`), with its bundled libtasn1, libunistring and gnulib | [gnupg.org](https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/), SHA-256 `69e113d802d1670c4d5ac1b99040b1f2d5c7c05daec5003813c049b5184820ed` | LGPL-2.1-or-later (the library; libtasn1 LGPL-2.1-or-later; libunistring and gnulib's library parts LGPL-3.0-or-later or GPL-2.0-or-later) |
| Nettle 3.10.1 with mini-gmp (inside `libgnutls.prx`) | [ftp.gnu.org](https://ftp.gnu.org/gnu/nettle/), SHA-256 `b0fcdd7fc0cdea6e80dcf1dd85ba794af0d5b4a57e26397eee3bc193272d9132` | LGPL-3.0-or-later or GPL-2.0-or-later |
| GE-Proton's ffmpeg + winedmo video rework (`wine/patches/2001-2062`) | the fork's `patches/ge-video-rework` at [`e24b389e`](https://github.com/mihawk-99/proton-ge-custom/commit/e24b389e) (2026-10-02, on Wine `1c78596`), 62 of its 66 patches as GloriousEggroll wrote them; the RSX3D audio shim and the three winepulse patches are left out, and each patch notes its rebase | LGPL-2.1-or-later, as Wine |
| FFmpeg 8.1.3 (libavformat, libavcodec, libswscale, libswresample, libavutil; static inside `winedmo.prx`) | [ffmpeg.org/releases](https://ffmpeg.org/releases/), SHA-256 `7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3`; its signature by FFmpeg's release key `FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8` was checked when the pin was taken (2026-10-16). The 8.1 line is the one GE-Proton builds winedmo against (its `ffmpeg` submodule is `n8.1`) | LGPL-2.1-or-later: built without `--enable-gpl`, `--enable-version3` or `--enable-nonfree` and with nothing external (`tools/build-ffmpeg-ps5.py` checks FFmpeg's `config.h`) |
| Wine Mono 10.4.1 (`share/wine/mono`, installed by wineboot into each prefix) | [dl.winehq.org](https://dl.winehq.org/wine/wine-mono/10.4.1/), SHA-256 `071f4b2887e1c97a11d791ff3d65be9429eed6dec4c2708888bfd546ba358e23` (the hash Wine's appwiz.cpl names) | MIT for Wine Mono's own code; the package's components (Mono, .NET Core libraries) carry their own notices |
| Wine Gecko 2.47.4, x86_64 (`share/wine/gecko`) | [dl.winehq.org](https://dl.winehq.org/wine/wine-gecko/2.47.4/), SHA-256 `e590b7d988a32d6aa4cf1d8aa3aa3d33766fdd4cf4c89c2dcc2095ecb28d066f` | MPL-2.0 |
| Wine Gecko 2.47.4, x86 (`share/wine/gecko`) | Same Wine Gecko release, SHA-256 `26cecc47706b091908f7f814bddb074c61beb8063318e9efc5a7f789857793d6` (Wine's i386 appwiz.cpl pin) | MPL-2.0 |
| Mozilla's root certificates, as curl extracts them (`share/wine/ca-certificates.crt`) | [curl.se/ca](https://curl.se/ca/cacert-2026-10-20.pem), SHA-256 `796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe` | MPL-2.0 |
| Zstandard 1.5.7, decompression only (`external/zstd`: the single-file decoder, `zstd.h`, `zstd_errors.h`), built into the title for the runtime install (`src/pw_pack.c`) | [facebook/zstd](https://github.com/facebook/zstd) tag `v1.5.7`, commit `f8745da6ff1ad1e7bab384bd1f9d742439278e99`; `zstddeclib.c` is the output of that release's `build/single_file_libs/create_single_file_decoder.sh`, unchanged (`external/zstd/README.md`) | BSD-3-Clause (`external/zstd/LICENSE`), the BSD side of zstd's BSD-or-GPL-2.0 choice |
| Mbed TLS 3.6.7, the TLS of the title's downloads (`src/pw_https.c`; its `library/` built with `platform/tls/pw_mbedtls_config.h`, nothing of `3rdparty/`) | [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7), `mbedtls-3.6.7.tar.bz2` SHA-256 `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` (the pin of `../PS5_RetroArch/tools/build-curl.sh`), unpacked by `tools/setup-mbedtls.sh` | Apache-2.0, the Apache side of its Apache-2.0-or-GPL-2.0-or-later choice (`LICENSE`, shipped beside the title) |
| Monocypher 4.0.2, Ed25519 signature checks only (`external/monocypher`: `monocypher.c`, `monocypher.h`, `monocypher-ed25519.c`, `monocypher-ed25519.h`), built into the title for its updates (`src/pw_release.c`) | [monocypher.org](https://monocypher.org/download/monocypher-4.0.2.tar.gz) and the [GitHub release](https://github.com/LoupVaillant/Monocypher/releases/tag/4.0.2), the same `monocypher-4.0.2.tar.gz`, SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`; files unchanged (`external/monocypher/README.md`) | BSD-2-Clause (`external/monocypher/LICENCE.md`), the BSD side of its BSD-2-Clause-or-CC0-1.0 choice |
| Chromium Embedded Framework 150.0.22 (CEF commit `c1a097eaa7738483abe2ea9d6297a937d6a884c0`, Chromium 150.0.7871.256), unmodified Windows x64 binaries in `runtime/ge11-7/lib/mihawk-browser/` (MihawkBrowser, below) | [cef-builds.spotifycdn.com](https://cef-builds.spotifycdn.com/index.json) `cef_binary_150.0.22+gc1a097e+chromium-150.0.7871.256_windows64_minimal.tar.bz2`, SHA-1 `01011e3f5b05caacb3b724c7965b047ffd33fd18`, SHA-256 `a6afba8ee3d1cc9ef64318a734dfeb5ceafc0384a46df2af37b857e7d76c0544`, fetched by `tools/fetch-cef.py` | BSD-3-Clause (CEF, Chromium); third-party components under their own licences in `CREDITS.html`, FFmpeg's parts LGPL-2.1-or-later |
| Country flags (`ps5/assets/flags`, 20 SVGs of the 4x3 set; the guided setup's language tiles, rasterized at build time) | [lipis/flag-icons](https://github.com/lipis/flag-icons) `086f7e97d657358203916dbe84f61c2bccaa81eb`, each file's SHA-256 in `ps5/assets/flags/SOURCE.json`, checked byte for byte against that revision | MIT (`ps5/assets/flags/LICENSE`) |
| Noto Sans SC and Noto Sans JP at the Medium weight (the guided setup's and the Steam guide's Chinese and Japanese; cut to the characters their text uses, shipped as TrueType for the title's own screens and baked by `tools/bake-huifont.cpp` for the launcher) | [google/fonts](https://github.com/google/fonts) `47d54ca1ebb14fd864695f1d39dc734f39017c33`, `ofl/notosanssc` and `ofl/notosansjp` (variable TrueType), SHA-256 in `ps5/tools/generate_assets.py` | OFL-1.1 (`ps5/licenses/NotoSansCJK-OFL.txt`, Google Fonts' copy) |
| ImageMagick 7.1.2-32 Q16-HDRI, a build-time tool only: it renders the Mihawk OS icons (`tools/build-mihawk-icons.py`); nothing of it is shipped | Arch Linux's package [imagemagick-7.1.2.32-1-x86_64](https://archive.archlinux.org/packages/i/imagemagick/), SHA-256 `8eb08d75d74071afee8b70161bbed4e06a8a041281a1f0cb43aefa78061ec4e5`, fetched by `tools/fetch-all.sh` | ImageMagick License (Apache-2.0 style) |
| Aurelia 0.1.40, the headless Steam client the launcher drives as a backend (`tools/fetch-aurelia.sh`, the pinned `aurelia_windows_x86_64.exe`, SHA-256 `477f3d7b70866c5103a0ae17f5a95fb753067d7e376704220db8b125a126a806`; run under Wine, nothing of it rebuilt) | [Drackrath/Aurelia](https://github.com/Drackrath/Aurelia) release `v0.1.40` | GPL-3.0 (`LICENSE` in the release's source) |
| jsmn, the JSON tokenizer of the Aurelia client (`src/pw_jsmn.h`, unchanged apart from its header) | [zserge/jsmn](https://github.com/zserge/jsmn), commit `6e4b1d3`, fetched 2026-10-22 | MIT |
| GNU libmicrohttpd 1.0.10, the WebUI's HTTP server (static in `eboot.bin`, built without TLS by `tools/build-webui-http.sh` with PS5_RetroArch's recipe; `src/pw_webui_http.cpp` is its only user) | [ftp.gnu.org](https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.10.tar.gz), SHA-256 `04bfe8ef75db7d629a33de767599765cecadc56274a39822d5d081030d577685` | LGPL-2.1-or-later (`COPYING`, staged beside the title as `licenses/libmicrohttpd/COPYING`), the LGPL side of its LGPL-or-eCos choice; the pin and build script here rebuild it from source, as LGPL-2.1 section 6 asks of a static link |
| QR Code generator (Nayuki) 1.8.0, the sign-in QR of the Aurelia overlay (`src/pw_qrcodegen.{c,h}`, unchanged apart from their headers and file names) | [nayuki/QR-Code-generator](https://github.com/nayuki/QR-Code-generator), `c/qrcodegen.c` and `c/qrcodegen.h` at 2026-10-22 | MIT |

## The Aurelia library's covers

The shelf's cover pictures are not part of this repository: `tools/fetch-aurelia-covers.py` downloads the store capsules
the client's `list --json` names and uploads them to the user's own console at run time. The artwork remains the games'
publishers'; only the path and the fetching tool are ours.

The isolated GPU broker probe's eight-word completion packet in
`native/proton_gpu_broker_probe.h` follows Mesa's MIT-licensed
`src/amd/vulkan/winsys/ps5/radv_ps5_cs.c` at PS5_Mesa revision
`7b59ef27c1b09b9671bc4153c41940c3155c3af2`. The probe links the corresponding
RADV archive through PS5_Vulkan's existing recipe; each isolated build records
its archive and platform-header hashes. No firmware implementation is vendored.
The offscreen client replaces two archive objects with repository-owned
platform/WSI adapters, now owned by the pinned PS5_Vulkan foundation.
`PS5_Mesa/ps5/broker/radv-gpu-broker-features.patch` disables
unsupported sparse, capture/replay and user-pointer features in an isolated
copy of the same pinned MIT-licensed Mesa winsys. The ordinary title backend
is unchanged; the broker overlay is deliberately owned by PS5_Mesa.
The PRX converter is ps5-native-app-boilerplate revision
`30597512539e7edfde079cbcaf4a626bc0a948c5` (GPL-3.0-or-later), already used by the Wine build.

## Launcher UI foundation update

The UI module and platform input/audio additions come from
PS5_VulkanTemplate `ca553412935b6c50500e7dbf1329d133604446d4`. Foundation
platform code and the asset builder remain MIT; `ps5/ui/` and the launcher
screen that uses it are GPL-3.0-or-later. The module's local adaptations are
recorded in `ps5/ui/template-adaptations.patch` against that template revision.

`ps5/ui/setup-kit.sh` pins [PS5_VKHomebrewUI](https://github.com/mihawk-99/PS5_VKHomebrewUI)
at `2188642afbcc8d84d36149124f3d9e39dc1b1a05`, exported without modifications
into `.deps/hui`; the export then bakes Inter Regular, Inter SemiBold and Montserrat
Medium again with the kit's own `tools/font-baker` and its `european` set, so the
launcher draws accented Latin and Cyrillic. `tools/bake-huifont.cpp`, which bakes
the guided setup's Chinese and Japanese faces, is derived from that baker
(GPL-3.0-or-later, BlackBearReloaded). BlackBearReloaded's ps5-homebrew-ui supplies the thirty themes,
components and sounds (GPL-3.0-or-later); Mihawk's fork supplies the Vulkan
backend. The fonts are OFL-1.1 and Bitstream Vera. The exact asset entries and
licence files are in `ps5/assets.json`; the GPL text is in
`ps5/licenses/GPL-3.0.txt`, and the export retains its upstream notices.

## Written for this project

The Windows-style Wine theme in `themes/windows11/source/` (display name Mihawk Light)
is derived from *Fluent Light for Wine v0.1*, whose modifications of Wine Light were
made by this project's author, Mihawk; the package's README names no author. Wine
Light is copyright 2021–2024 Zhiyi Zhang for CodeWeavers and Wine contributors, and the
whole theme is LGPL-2.1-or-later. As that README states, the resource-only PE container
`source/template.msstyles` (kept byte-identical, SHA-256
`5bf9940a832cc855e6c75807faab324d715f0bd96ad5370dbcbb48da44c3636d`) is
`light/light.msstyles` from <https://github.com/listumps/wine_themes> (no revision
recorded), and the Wine Light source used to resolve shared resource aliases is
`dlls/light.msstyles` at Wine's `wine-10.0` tag. The Wine version that built the
container itself is not recorded. Original resource hashes and the LGPL notice
accompany the editable sources. The theme is rebuilt by `tools/build-wine-theme.py`;
modifications made here select Proton UI and 9-point caption metrics. See
`themes/windows11/README.md` for fidelity and asset limits.

Proton UI is a renamed and kerned derivative of Selawik, copyright 2015 Microsoft
Corporation, designed by Aaron Bell, **OFL-1.1**. Immutable inputs and their
license are under `themes/windows11/fonts/upstream/`, pinned from
`winjs/bootstrap-winjs` revision `342cf99031344f48917e14a5c3a728ec5ede8d8f`
(`src/fonts`); `fonts/upstream.json` records each SHA-256.
`tools/build-proton-ui-font.py` contains the complete modification. Neither Segoe
UI nor Segoe UI Variable is bundled or used to build this derivative.

### The desktop's wallpaper: Bubble City by Skeuoss

The desktop wallpaper since 2026-10-31 is Bubble City (soap bubbles and a globe holding a city and trees on a bright
blue sky), artwork by **Skeuoss**, a Frutiger Aero artist and archive. It is used and redistributed with the artist's
permission: on 2026-10-31 the project's author asked Skeuoss, in a Discord message, to feature their artwork in
MihawkOS with credit and any terms they set, and Skeuoss answered "sure, use everything you want". No other terms were
given. The permission is the project's, not a licence: the picture is not GPL, LGPL or any other open licence, and
anyone reusing it outside MihawkOS needs the artist's own permission. A record of the message is kept privately by the
author, not in the repository.

- File: `themes/windows11/wallpapers/skeuoss-bubble-city.jpg`, 1920×1080 JPEG, 371,509 bytes, the copy the owner
  received, with its metadata removed losslessly (`jpegtran -copy none`; the pixels are unchanged).
- Use: `tools/build-wine-theme.py` stamps it with the corner note below and stages it as `proton.bmp`, the desktop
  of every build (the owner's choice, 2026-10-31).
- Also the title's PS5 home-screen background (the owner's choice, 2026-10-31): `ps5/sce_sys/pic0.dds` (the selected
  app's background) and `ps5/sce_sys/pic1.dds` (the launch transition), the same picture scaled to 3840×2160 and
  encoded as BC7 by PS5_Vulkan's `tools/prepare-assets.sh` (f410e67), with no corner note. The two files are identical.

The earlier desktop wallpaper, Bubble Earth (a glossy Earth on a meadow among soap bubbles, a glass skyline behind), is original
artwork drawn only by `tools/make-aero-wallpapers.py` (LGPL-2.1-or-later, seeded: the same pixels on every run; it also
draws Glass City and Aqua World), with no photograph or third-party image as input; its soap bubbles are those of the
earlier wallpaper, Aero Meadow (a sky with a sun glow, soap bubbles and green hills, `tools/make-mihawk-wallpaper.py`,
the same licence). Bubble Earth was the desktop from 2026-10-25 to 2026-10-31. The desktop's top-right note
(MihawkOS v<VERSION> and the community link) is set in Selawik Semibold and Regular from
`themes/windows11/fonts/upstream/` (OFL-1.1, listed with the fonts); the OFL does not extend to images made with the
font. It replaced Windows
11's Bloom, the owner's local copy, which was never part of the redistributable package.

The launcher's music, Bubble Garden, Glass Meadow and Sky Lounge (Frutiger Aero pieces: Lydian and major pads, glassy
bells, marimba and celesta, bubbles), is original work made only by `tools/make-mihawk-music.py` (LGPL-2.1-or-later,
like the wallpaper; seeded: the same samples on every run), with no recording, sample or third-party note as input. It
is encoded with libogg 1.3.5 and libvorbis 1.3.7 (BSD-3-Clause, pinned tarballs, a build-time tool only:
`tools/vorbis-encode.c`); `tests/test_mihawk_music.py` renders it again and compares every byte. It replaced the UI kit's
First Light, which is no longer shipped.

The launcher, the game list and profile handling, the prefix management, the C
reimplementation of Proton's runtime effects, the platform adapters, and the build and
test tooling are written for PS5_Proton. No proprietary SDK file is copied into this
repository, and no file from a game is ever committed.

The SHA-256 the runtime install hashes with (`src/pw_sha256.c`) is written for this project from FIPS 180-4 and
checked against its examples and Python's `hashlib` (`tests/test_pw_sha256.c`); no implementation is copied.

The in-process synchronization device (`src/pw_ntsync.c`, `wine/ps5/pw_wine_ntsync.c`,
and `include/wine/ps5_ntsync.h` added by Wine patch `0890`) is written for this project
from the behaviour Linux documents for its ntsync driver
(`Documentation/userspace-api/ntsync.rst`) and from how Wine's own code calls that
driver. No code of the Linux driver (GPL-2.0) is copied; the argument structures in
`ps5_ntsync.h` are written here to the same layout as the interface Wine's code expects.

## Controller transport references

The expanded native pad sample layout follows prospero-win's
`native/pw_pad_ps5.h` at the already pinned revision
`c8a2f00137fb0a95f88541252b4e47f53f9677e6` (LGPL-2.1-or-later).
The USB report encoder and HID descriptor are written for this project, using
public protocol fields documented by SDL's `SDL_hidapi_ps5.c` at
`release-2.32.10` (zlib; file SHA-256
`a326d1c8085f14aefac58deddf8911c796026a58a96a72829304e398dc0cf2b8`) and Linux's
`hid-playstation.c` at `v6.17` (GPL-2.0-or-later; file SHA-256
`51973e6db85bb914d4186fa0fffc14d2a754f28123cc48994189a3d55ddf8bed`).
Those reference sources are not vendored. Wine's existing HID/PnP stack and
its PS5 transport patch remain LGPL-2.1-or-later.


The experimental broker WSI patch applies to PS5_Mesa’s MIT-licensed
`src/vulkan/wsi/wsi_common_videoout.c` at
`7b59ef27c1b09b9671bc4153c41940c3155c3af2`; the upstream copyright remains
in the generated source and the patch is in
`PS5_Mesa/ps5/broker/radv-gpu-broker-wsi.patch`. The control-service accept retry
patch applies to PS5_Vulkan’s GPL-3.0-or-later
`payload/ps5vkctl/main.c` at `f3cbf875f89978deae9f855b9be2610cec2818ae`.
`tools/build-console-control.py` pins its source digest, records the SDK and
patch digests and builds in this repository’s ignored dependency directory.

The broker client overlays the MIT-licensed PS5_Mesa winsys source/header and
command-stream source at the same `7b59ef27c1b09b9671bc4153c41940c3155c3af2`
pin. `PS5_Mesa/ps5/broker/radv-gpu-broker-features.patch` records the reduced command
ring and unsupported-feature restrictions; the optional overlay is kept in its
own foundation repository.
Wine patch `0928` applies to the existing LGPL-2.1-or-later PS5 Wine series
over `1c78596625336a73dbfc035dbbcf0dbdd95b06eb`.
The synthetic triangle DXBC is compiled from this repository's own HLSL by
host Wine at `a518d643873ff733bafb31595dfd01ab8bbdf790` (LGPL-2.1-or-later),
with the compiler DLL SHA-256 pinned in `tools/generate-d3d-probe-shaders.py`.
Only the generated synthetic shader bytes are included, not the compiler.

## Windows OpenGL over the GPU broker

`tools/build-mesa-windows-zink.sh` consumes PS5_Vulkan's pinned optional WGL
recipe. It builds Mesa 26.2.0 PE64 DLLs from PS5_Mesa revision
`b3588f78dd7fd46fbadc7ecd487e5f14d4213e4b` (exported with git archive).
Mesa's WGL, Zink and state tracker retain their MIT and component notices in
`docs/license.rst`. The generic advertised-presentation-mode fix lives in that
fork's `src/gallium/drivers/zink/zink_kopper.c`, and the fix that keeps
persistently mapped buffers host-visible lives in its `zink_bo.h`,
`zink_descriptors.c` and `zink_resource.c`; no duplicate Proton patch is
maintained. Wine DLL staging/overrides remain in this repository.
`tools/build-mesa-windows-zink-i686.sh` builds the same two DLLs for i686, for
32-bit programs, from the same Mesa revision and the same checksum-verified
llvm-mingw archive; it mirrors the foundation's recipe with an i686 cross file.

The graphics transport, owner/client runtime and display were extracted from
Proton `878c1e9` into PS5_Vulkan. They retain LGPL-2.1-or-later and are consumed
at `f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed` (`tools/setup-graphics-foundation.sh`,
which also takes `radv_ps5_platform.h` from PS5_Mesa
`1ba9874f478657ccf6da72ce55f0260164cc0bd0`, MIT); the foundation's canonical
RADV link recipe remains GPL-3.0-or-later. Build outputs are ignored, and no
proprietary driver is included.

`platform/graphics/patches/0001` to `0018` were local LGPL-2.1-or-later overlays on
that pin, applied in order by the same script; the first three are described next. Since 2026-11-01 every patch that
folder carried (0001 to 0045 and 0050) is a commit of the foundation's own repository, mihawk-99/PS5_Vulkan (one commit
each, the same licence; the pin is now 2e87500), and the folder is empty. The notes below keep their patch numbers.

`platform/graphics/patches/0001-retain-the-foreground-process-frame.patch`
is a local LGPL-2.1-or-later overlay on the graphics foundation pin.
It prevents background frame sources from replacing a quiet foreground
picture and re-presents retained GDI frames on focus changes. The export
script stamps the patch content; no mutable dependency tree is edited.

`platform/graphics/patches/0002-compose-retained-window-surfaces.patch` is a
local LGPL-2.1-or-later overlay on the same pin. It retains stable per-HWND GDI
pixels under bounded ownership and composes Wine's authoritative snapshot,
preserving the legacy default. Canonical RADV linking remains unchanged.

`platform/graphics/patches/0003-compose-gpu-window-surfaces.patch` extends that
same LGPL-2.1-or-later pin with owner-bound GPU clients, retained HWND planes and
a local WSI binding overlay. It uses the existing canonical client builder and
fence/display recipe; no proprietary driver or mutable sibling is modified.
Wine integration remains in patch 0978 and repository-owned lifecycle adapters.

The cross compiler is the existing llvm-mingw `20260922-ucrt` archive, SHA-256
`bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21`.
LLVM/libc++ retain Apache-2.0 WITH LLVM-exception and their component notices;
mingw-w64's runtime retains its public-domain, BSD and Zope Public License
notices. The toolchain's `share` licence files and Mesa's licence directory
remain in their ignored source/toolchain trees. No implicit build-time download
is performed. Console qualification is recorded separately from the build.

## The WebUI and its game forwarders, from PS5_RetroArch

The WebUI (`webui/`, `src/pw_webui*`) and the game forwarder (`forwarder/`, `tools/build-forwarder.sh`) follow
[mihawk-99/PS5_RetroArch](https://github.com/mihawk-99/PS5_RetroArch)'s WebUI and forwarders, the same author's
GPL-3.0-or-later project: its safety model (the console's own origin, a token on every write, a fixed list of page
files), its resumable uploads, and its forwarder design (a tile app that stamps its title id into a launch payload sent
to the ELF loader, registered through ShadowMountPlus). `forwarder/tile.c` and `forwarder/launch.c` are adapted from
its `forwarder/`, the rest written here. The tiles use their own title ids (PPSA99300 to PPSA99399), clear of
MihawkOS's PPSA99170, its memory probe's PPSA99171 and PS5_RetroArch's tiles.

## ShadowMountPlus

ShadowMountPlus, which mounts the title's folder on the console, is a separate project
([drakmor/ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus), GPL-3.0) that the user installs. MihawkOS
neither ships nor modifies it. The WebUI registers its home screen tiles through the user's copy (its local API).

## DNSAPI resolver

The PS5 DNSAPI Unix backend links unmodified [c-ares 1.34.8](https://github.com/c-ares/c-ares/releases/tag/v1.34.8)
under the MIT license. `tools/build-cares-ps5.py` pins its official source archive
SHA-256 to `c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`.
Its complete `LICENSE.md` is staged under `share/licenses/cares/`.
The backend's native/WoW64 parameter adapters derive from the pinned Wine
`dlls/dnsapi/libresolv.c` (Hans Leidekker, CodeWeavers; LGPL-2.1-or-later).
## PC5: Wine fixes written after reading another prospero-win launcher

[PC5](https://github.com/bornaradusin/PC5) (bornaradusin, LGPL-2.1-or-later, reviewed at `81323dc3`, 2026-10-31) is a
launcher built on prospero-win. Nothing of it is copied or vendored: its patches were read as a list of problems the
console exposes, and each fix below was written here from the problem and carries a host test (`tests/test_pc5_*`).
Rows say which PC5 patch described the problem, or which upstream project's commit the fix follows and its licence.

| Patch here | Problem | Read in |
| --- | --- | --- |
| 2410 | an XAPO effect's reference count lost updates between threads, so a voice used a freed effect | PC5 prospero-win 0002 |
| 2411 | calls on a destroyed XAudio2 voice read freed FAudio memory, and its object went to the next voice | PC5 prospero-win 0007 |
| 2412 | the process memory counts held `virtual_mutex` for a long walk, thousands of times a second | PC5 prospero-win 0075 |
| 2413 | a window's pixel format set twice (Godot 4 does) failed under Mesa's WGL, which refuses a second call | PC5 prospero-win 0066 (Wine patch 0956 it carries from Leo Lobato's prospero-win) |
| 2414 | a thread holding the loader lock that made its first window waited in a `pthread_once` for a thread that waited for the lock (Source engine games hung at start) | PC5 prospero-win 0066 (Wine patch 0981 it carries from Leo Lobato's prospero-win) |
| (title) | OpenSSL 1.0.2 to 1.0.2k, which games bundle, crashes on a CPU with the SHA instructions: `OPENSSL_ia32cap=:~0x20000000`, the setting Proton gives every game, is in the title's environment | PC5 prospero-win 0025 |
| 2415 | the nested-task flag a program set with `popf` made the next return fault, so old games' CPU checks crashed (Wine bug 33275) | PC5 prospero-win 0062 |
| 2416 | a write one element past the end of a large heap block faulted when the block ended where its region did (Windows' heap leaves slack) | PC5 prospero-win 0077 (its Wine patch 0991) |
| 2417 | a folder a game makes in a user's folders failed when its parent was never made, because the game was copied, not installed (an uncaught C++ exception) | PC5 prospero-win 0061 |
| 2418 | a game's pop-up (a dialog box) could not be answered from the controller, which is the game's pad and not a keyboard | PC5 prospero-win 0013 (its Wine patch 0911) |
| (probe) | a texture shared between a Direct3D 9Ex and a Direct3D 11 device of one process, where there is no Win32 external memory (Koei Tecmo's movie players); measured by a probe before any DXVK change | PC5 patches/dxvk 0001-0003 (read, not ported) |
| DXVK 0007 | a texture shared by a Direct3D 9Ex device could not be opened by the same game's Direct3D 11 device where the driver has no Win32 external memory, so Koei Tecmo-style movie players showed nothing; now shared inside the process (zlib, as DXVK) | PC5 patches/dxvk 0001 to 0003 (zlib, DXVK's licence); the design here is independent |

## Media: winedmo and FFmpeg

Media Foundation, DirectShow and the WM reader decode through winedmo, as in GE-Proton: patches 2001-2062 replace winegstreamer with winedmo's FFmpeg backend; patches 2063 and 2064 (this project's) make its source reader report a decoder's new frame size and keep `MFShutdown` from deadlocking with a work queue callback. FFmpeg is configured as GE-Proton configures it (every demuxer, decoder, encoder and bitstream filter, VVC off, no parsers), without GE's HTTPS, lzma and bzip2, which the console has no libraries for; `tools/build-ffmpeg-ps5.py` stops if FFmpeg's `config.h` reports anything but LGPL-2.1-or-later. `winedmo.prx` links unmodified FFmpeg 8.1.3 statically (`tools/build-ffmpeg-ps5.py`, pinned above); its `LICENSE.md` and `COPYING.LGPLv2.1` are staged under `share/licenses/ffmpeg/`, and the pins, patches and build scripts here rebuild that module from source, as LGPL-2.1 section 6 asks of a static link.

## Mihawk OS approved icon assets

`themes/windows11/icons/original/` contains fourteen original coloured SVGs
supplied by Mihawk and approved for reuse as LGPL-2.1-or-later project artwork.
Provenance and immutable input hashes accompany the source; no proprietary
Windows icon files are inputs. `themes/windows11/icons/fluent/` contains
thirty-six Microsoft Fluent System Icons (globe and arrow-clockwise added for
MihawkBrowser), copyright 2020 Microsoft Corporation,
MIT licence, from revision `cd332ec1f90cda2fad16f86754f8da52f1e9ab30` of
`https://github.com/microsoft/fluentui-system-icons`. The upstream paths and
SHA-256 hashes are in `upstream.json`; LICENSE and NOTICE are preserved and
staged with the runtime. Generated raster/icon resources use the unchanged SVG
paths, with command foreground colours from the approved light/dark palette.

Patch `0979-driver-retain-gdi-gpu-backing.patch` retains normal GDI backing
beside brokered GPU clients in the opt-in HWND route (Wine LGPL-2.1-or-later).
Its console qualification is pending.

## The README's art

`docs/media/readme/` holds the README's banner, section dividers and heading orbs, each in a day and a night version,
and `docs/media/readme/badges/` its badges. All are SVG drawn for this project by the two scripts in
[`docs/README_ART.md`](docs/README_ART.md); no photograph, font file or third-party image is in them. The banner draws the
Mihawk mark (`themes/windows11/start.svg`) and each orb one of the original icons above, so those files are
LGPL-2.1-or-later like them, each with its SPDX line. The badges hold no third-party art and are GPL-3.0-or-later like
the rest of this repository; their text names system sans-serif faces, which the reader's browser supplies.

## Windows 11 prototype visual system: pointers, caption buttons, monospace face

`themes/windows11/cursors/` holds the Mihawk pointer scheme: original vector artwork in the shape language of the
Windows pointers (white fill, 1 px black outline, pointer shadow), written for the approved Windows 11 shell
prototype by its `make-cursors.mjs` (PS5_Proton-design `prototypes/windows11-shell/assets/cursors`, 2026-10-15) and
copied unchanged; SHA-256 per file in `manifest.json`. No Microsoft cursor file was copied, traced or converted.
GPL-3.0-or-later with the rest of this repository. `tools/build-cursors.py` renders them into `.cur`/`.ani` with
the icon builder's pinned ImageMagick 7.1.2-32.

`themes/windows11/caption/glyphs.json` is measured data, not artwork: the caption-button pixels the prototype's
browser (Chromium 154) renders for its own 10 x 10 glyph strokes (`js/wm.js`) and state colours, recorded by
`tools/measure-prototype-caption.mjs`. The colours themselves are the WinUI resources in
`docs/windows11-reference/winui-colors.json` (MIT, see below) and Windows Terminal's close red (tier-a). No
Segoe Fluent Icons glyph is used.

`themes/windows11/taskbar/*.json` is measured data too: the prototype taskbar's own pixels (Chromium 154), each
element captured alone over black and over white by `tools/measure-prototype-sprites.mjs`. What they show is the
project's own artwork (the prototype's original app icons in `prototypes/windows11-shell/assets/icons/object`, GPL-3.0-or-later), Microsoft Fluent System Icons (search, Task View,
chevron, network, volume and battery glyphs: MIT, revision `cd332ec1f90cda2fad16f86754f8da52f1e9ab30`, as recorded below
for the prototype) and the word "Search" set in Proton UI (the Selawik derivative, OFL). No wallpaper pixel is in them:
the backdrop is cleared before each capture. `themes/windows11/taskbar/clock-*.json` (by
`tools/measure-prototype-glyphs.mjs`) are Chromium's renderings of single Proton UI characters and clock strings at
12 px: images of the OFL font's glyphs, not font software.

`themes/windows11/start.svg` is the Mihawk mark, the Start button's glyph: original artwork drawn for this project (a
white squircle, an amber frame and a navy M), not derived from any Microsoft, Sony or One Piece asset,
LGPL-2.1-or-later as the file states. The taskbar draws it natively (`src/pw_taskbar_logo.h`, Wine patch 1162) and the
prototype shows the same file; no Start sprite is captured. The Windows logo, which the Start button showed from
2026-10-16, is gone from the sprites, the native drawing and the prototype.

`ps5/tools/icon0.svg`, the title's icon (`ps5/sce_sys/icon0.png` is its rendering), is a Frutiger Aero user tile:
original artwork drawn for this project on 2026-10-31 (a white glass frame, a sky-over-meadow picture with bubbles and a
glossy blue user), from gradients and shapes alone, not derived from any Microsoft, Sony or other asset, GPL-3.0-or-later
as the file states.

`themes/windows11/start/*.json` and `themes/windows11/flyouts/*.json` are measured the same way from the prototype's
Start, Search, Quick Settings and notification centre: the project's own artwork and the prototype's original app and
file icons and its account picture (`assets/icons/object`, GPL-3.0-or-later), Microsoft Fluent System Icons
(search, chevrons, power, settings, Wi-Fi, globe and calendar glyphs: MIT, the revision above) and fixed words set in
Proton UI (OFL). No wallpaper pixel is in them.

`themes/windows11/explorer/*.json.gz` is measured data of the same kind: the prototype File Explorer's own pixels
(Chromium 154), each element captured alone over black and over white by `tools/measure-explorer-sprites.mjs` (the
tabs, caption buttons, address and search boxes, commands, navigation rows, Home's sections and tiles, the details
view's column header, the items' folder and document icons, the details pane's icons, labels and hint, the empty-list
notes, their plates). What they show is the prototype's original object icons
(`prototypes/windows11-shell/assets/icons/object`, GPL-3.0-or-later), Microsoft Fluent System Icons glyphs (MIT,
revision `cd332ec1f90cda2fad16f86754f8da52f1e9ab30`, as recorded below for the prototype) and fixed words set in
Proton UI (the Selawik derivative, OFL): the commands' and columns' labels, the navigation pane's and Home's names and
notes. No wallpaper pixel is in them: every backdrop is cleared before each capture.

`themes/windows11/fonts/cascadia/` contains Cascadia Mono Regular and Bold, copyright (c) 2019 - Present, Microsoft
Corporation, from the `v2407.24` release of <https://github.com/microsoft/cascadia-code> (asset
`CascadiaCode-2407.24.zip`, members `ttf/static/CascadiaMono-Regular.ttf` SHA-256
`06520d032ec274fa5040b22c6f4a1d829081b24ba40b2da56dae89bf10c7b481` and `ttf/static/CascadiaMono-Bold.ttf`
SHA-256 `b22cb603ed23cac36e8444846e1841caca21719a5e852780a8d37ae7a49b0a36`), read by `tools/fetch-cascadia-mono.py`
with HTTP range requests and CRC-32-checked, unmodified. Licence: SIL Open Font License 1.1, Reserved Font Name
"Cascadia Code" (`LICENSE`, SHA-256 `51882cd3cdba4e16f220f44ddb08a635c38c44ea6e0975db2574f4be6f958238`, staged
with the runtime as `share/licenses/proton-ui/CascadiaMono-OFL.txt`). The fonts are bundled, not sold, and keep
their names because they are not modified.

## Windows 11 reference data

`docs/windows11-reference/winui-colors.json` holds the 83 colour resources per theme (light and dark) read from
`controls/dev/CommonStyles/Common_themeresources_any.xaml` in <https://github.com/microsoft/microsoft-ui-xaml>, revision
`4c45fb8ea98afafc5cf1b0bafc7febbe20d67b19`, MIT licence, copyright (c) Microsoft Corporation. Only the values are kept, not the file.
`docs/windows11-reference/winui-controls.json` holds per-control numeric resources and brush references read from
`controls/dev/*/..._themeresources.xaml` at the same revision, under the same MIT licence; again only values are kept.
`docs/windows11-reference/tier-a.json` records numbers that Microsoft Learn publishes (geometry, title bar, layering, timing and easing,
typography), each with its page; the text and images of those pages are not copied. Sources and their trust tiers are in
`docs/windows11-reference/SOURCES.md`. Nothing from those pages (text, fonts, icons or images) is copied into `docs/windows11-reference/`.

`tools/skia-rrect-coverage.py` follows, re-expressed in Python, the fixed-point steps by which Skia's analytic
anti-aliasing fills a rounded rectangle (`src/core/SkScan_AAAPath.cpp`, `SkAnalyticEdge.cpp`, `SkEdge.cpp`,
`SkGeometry.cpp` in <https://skia.googlesource.com/skia>), so the compositor's 1 px window ring covers its corners as the
prototype's browser does; `pw_window_ring_corners` in `src/pw_window_effects.h` is its output. No Skia file is copied.
Skia's licence:

> Copyright (c) 2011 Google Inc. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
> following conditions are met:
>
> - Redistributions of source code must retain the above copyright notice, this list of conditions and the following
>   disclaimer.
> - Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
>   disclaimer in the documentation and/or other materials provided with the distribution.
> - Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
>   products derived from this software without specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
> INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
> DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
> SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
> SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
> WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
> OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Windows 11 shell prototype

`prototypes/windows11-shell/` is a browser design prototype, not shipped runtime code. Its third-party assets:

- `assets/icons/fluent/`: 108 Microsoft Fluent System Icons (20 px), MIT licence, copyright 2020 Microsoft Corporation, from
  revision `cd332ec1f90cda2fad16f86754f8da52f1e9ab30` of <https://github.com/microsoft/fluentui-system-icons>; the only change is
  `fill="currentColor"`. Upstream paths and SHA-256 hashes are in `upstream.json`; LICENSE and NOTICE are kept beside them.
- `assets/fonts/cascadia/CascadiaMono.woff2`: Cascadia Mono, SIL Open Font License 1.1, copyright 2019–present Microsoft
  Corporation (Reserved Font Name Cascadia Code), unmodified from release `v2407.24` of <https://github.com/microsoft/cascadia-code>.
  Hashes and the release member are in `provenance.json`; LICENSE is kept beside it.
- `assets/emoji/`: 347 Fluent Emoji in the Color style, MIT licence, copyright Microsoft Corporation, unmodified from revision
  `1ffb34c752ecf5d402f04cfb4b392c77f57c54bc` of <https://github.com/microsoft/fluentui-emoji>. Each file's upstream path, git blob
  hash and SHA-256 are in `provenance.json`; LICENSE is kept beside them.
- The object icons under `assets/icons/object/` are the approved originals above plus original artwork authored for the
  prototype (`provenance.json`); the cursors under `assets/cursors/` are original artwork. The font is Proton UI's Selawik
  inputs (OFL, above). The wallpaper is the project's own Aero Meadow, which `serve.sh` draws with `tools/make-mihawk-wallpaper.py`.

## Licence texts shipped with the binaries

Every binary carries its licence text beside it. The runtime's `share/licenses/` holds what `tools/stage-wine.py`
stages itself (GE-Proton's `LICENSE` and `LICENSE.OFL`, DXVK and its five subprojects, VKD3D-Proton and its seven
compiled-in subprojects and third-party parts, Proton UI's OFL, the theme's
LGPL, Cascadia Mono's OFL, the Fluent icons' `LICENSE` and `NOTICE`, c-ares, FFmpeg) and what
`tools/stage-licences.py` adds: this project's `LICENSE`, `NOTICE.md` and `LICENSING.md`; the PS5 OpenGL SDK's `LICENSE`,
`THIRD_PARTY_NOTICES.md` and every component licence in its `LICENSES/` (`ps5-opengl/`), for the SDK linked into
`win32u.prx`; Wine's `COPYING.LIB`,
`LICENSE` and `AUTHORS` from the pinned tree; VKD3D-Proton's `COPYING`, `LICENSE` and `AUTHORS` at the fork's
`vkd3d-proton` pin `af89350cc2eacd9da2293fbae96bd9ab4987c9bb`; GnuTLS's and Nettle's texts from the pinned tarballs the
build unpacks (`COPYING`, `COPYING.LESSERv2`; `COPYING.LESSERv3`, `COPYINGv2`, `COPYINGv3`; their `AUTHORS`); FreeType's
`LICENSE.TXT`, `FTL.TXT` and `GPLv2.TXT`; Mesa's `docs/license.rst` (the same at its RADV and WGL/Zink revisions); and
llvm-mingw's `LICENSE.TXT`. The title folder's `licenses/` holds this project's three files, the Lapy daemon's MIT
`LICENSE` (the payload SDK runtime linked into the daemon is covered by this project's GPL text), Mesa's page for RADV, Sascha Willems' `base/LICENSE.md`, volk, Dear ImGui, libktx, tinygltf, nlohmann/json,
Zstandard's BSD licence, Mbed TLS's `LICENSE` and GLM's manual (its licences are its section 0; the pinned revision has no `copying.txt`). The title's `libc.prx` is
ps5-native-app-boilerplate's (GPL-3.0-or-later), covered by the GPL text.

The texts no build input carries are fetched once into `.deps/licence-texts`, each from a fixed tag and checked by
SHA-256 (`tools/stage-licences.py`): Wine Mono's `COPYING` at `wine-mono-10.4.1` (its MSI carries none); ICU's `LICENSE`
at `release-68-2` for the ICU 68 DLLs of the GE-Proton archive, whose exact ICU revision is not recorded; the MPL-2.0
text from `spdx/license-list-data` `v3.27.0`, for the CA bundle, which states its licence without carrying it; the
mingw-w64 runtime's notice at `v14.0.0`, for the runtime llvm-mingw links into every PE DLL it builds (the toolchain's
headers say 15.0.0-alpha, a snapshot with no release of its own); and nlohmann/json's `LICENSE.MIT` at `v3.5.0`, the
version vendored in tinygltf. Wine Gecko's own licence page is inside its MSI (`omni.ja`, `license.html`). A missing or
changed text stops the build; `tests/test_stage_licences.py` lists every file expected.

The prefix bundle's fonts and the two add-ons get their own folders (`fonts/`, `wine-mono/`, `wine-gecko/`): the
GE-Proton archive's `arial.ttf`, `times.ttf` and `cour.ttf` are Liberation 1.x, whose name tables give the Liberation
Fonts license (GPL-2.0 with font exceptions; the 1.07.4 text from Debian's packaging tag `debian/1%1.07.4-11`, and
SPDX's GPL-2.0 text), unlike their OFL bold faces; `msyh.ttf` is Droid Sans Fallback with WenQuanYi glyphs, Apache-2.0
(the text in `ps5/licenses/`); a `README.txt` there says which text covers which font. Wine Mono gets SPDX's GPL-2.0,
LGPL-2.0 and MS-PL texts and Wine's LGPL-2.1 for the parts its `COPYING` names, and Wine Mono and Wine Gecko each a
`SOURCE.txt` naming the source tarball on dl.winehq.org beside the MSIs' pinned hashes, with Gecko's MPL-2.0 text.

## MihawkBrowser: the Chromium Embedded Framework

MihawkBrowser (`src/pw_browser.c`, docs/MIHAWK_BROWSER.md) is this project's own C program, GPL-3.0-or-later with the
rest of the repository, built against CEF's C API headers (`include/`, BSD-3-Clause) and linked with CEF's import
library. It ships beside these files of the pinned CEF distribution, unmodified, each recorded with its SHA-256 in
`.deps/cef/<name>/ship.json` by `tools/fetch-cef.py`: `libcef.dll`, `chrome_elf.dll`, `libEGL.dll` and
`libGLESv2.dll` (ANGLE), `v8_context_snapshot.bin`, `icudtl.dat`, `resources.pak`, `chrome_100_percent.pak` and the
four `locales/en-US*.pak`. CEF is copyright Marshall A. Greenblatt and portions Google Inc., BSD-3-Clause
(`LICENSE.txt`); Chromium is copyright The Chromium Authors, BSD-3-Clause; the notices of every third-party component
inside those binaries are in CEF's `CREDITS.html`. Both files are staged with the runtime as
`share/licenses/cef/LICENSE.txt` and `share/licenses/cef/CREDITS.html`. The FFmpeg parts linked into `libcef.dll` are
LGPL-2.1-or-later; their exact source is Chromium's `third_party/ffmpeg` at tag 150.0.7871.256
(<https://chromium.googlesource.com/chromium/src/+/refs/tags/150.0.7871.256/third_party/ffmpeg>), and CEF's at
<https://github.com/chromiumembedded/cef/tree/c1a097eaa7738483abe2ea9d6297a937d6a884c0>. No proprietary codec and no
Widevine module is included.

Never shipped: the distribution's `d3dcompiler_47.dll` (Microsoft's), `vulkan-1.dll` (Khronos's loader),
`vk_swiftshader.dll` and its ICD file, `dxcompiler.dll`, `dxil.dll`, the other locales and `chrome_200_percent.pak`.
`tools/fetch-cef.py` unpacks the first four into `.deps` only, for host experiments (docs/MIHAWK_BROWSER.md decision
5); no build or stage step copies them.

## prospero-win and ProsperoMC: fixes written after reading them

[prospero-win](https://github.com/mpereiraesaa/prospero-win) (mpereiraesaa, LGPL-2.1-or-later, reviewed at `18115ce1`,
2026-11-01) and [ProsperoMC](https://github.com/Ayoubec/ProsperoMC) (Ayoubec, GPL-3.0, reviewed at `5cdcb996`) are other
PS5 Wine launchers. Nothing here is copied from either: each fix is written for this tree, from the problem their
commit described. Rows say which commit described the problem, or which upstream project's data the fix follows.

| change | what it fixes | found by reading |
| --- | --- | --- |
| `pw-01` the game's controller stays connected, and reads idle, while the system has the DualSense | a game saw its XInput controller unplugged whenever the PS button's menu or the capture menu held the pad, and paused or asked to reconnect | prospero-win 83708cd |
| `pw-02` the root certificates are curl's 2026-10-20 extract of Mozilla's store | the pinned bundle was 13 months old, so a site on a newer root failed certificate checks; the SHA-256 is curl.se's own published `cacert-2026-10-20.pem.sha256` | prospero-win 404cac6 (the bump); the checksum fetched from curl.se |
| `pw-03` (Wine patch 2421) the PS5 high area is as large as the kernel grants | a 64-bit game that reserves tens of GiB at once (28 GiB, 48 GiB, 256 GiB) was refused by the fixed 16 GiB area and the console's mmap | prospero-win 0982 (ad7d313) |
| `pw-04` (`platform/graphics/mesa/0001`, for PS5_Mesa) Mesa's `u_vbuf_draw_vbo` releases the retired uploader buffers of each draw of a multi-draw | each draw wrote over the previous draw's retired buffers, which were never released: a client-array game that draws many pieces in one call ran the GPU out of buffers | ProsperoMC 03e6e27 (GPL-3.0) |
| `pw-05` (Wine patch 2422) no debugger starts in a PS5 title or helper | a crash called `start_debugger`, which allocates from the process heap another thread may hold (a frozen title), for a winedbg the runtime does not have | prospero-win 0971 (b9ee658) |
| `pw-06` (Wine patch 2423, and `wine/ps5/pw_wine_sink.[ch]`) the game's windows are told when the controller connects or disconnects | a game whose controller went off kept it lost after it came back, because Windows tells windows through the plug and play service (`WM_DEVICECHANGE`) that a title cannot run | prospero-win 0984 (7aa5d3c) |
| `pw-07` (Wine patch 2424, `src/pw_osk.[ch]`, `native/pw_osk_ps5.[ch]`) the on-screen keyboard's core: the gesture (touchpad with two fingers, L3 + R3, Options + touchpad), the pad hand-over, the Unicode typing into the game, the system IME dialog, and the setting (Off by default) | a game that asks for text has no keyboard unless one is plugged in | prospero-win d5a1601 (the idea of a system dialog whose line is typed into the game); the dialog's parameter block and status numbers follow this project's own working IME code (ps5-radio-browser) |
| `pw-08` (the title's wiring, the scripted console check `WINE_PS5_OSK_TEST` with `tests/osk-typed-probe.c`, Settings > Controls > On-screen keyboard in ten languages, the WebUI's `keyboard` setting) the on-screen keyboard reaches the player | pw-07 is the core; this opens the dialog from the chosen gesture in the title, types the line into the game, and lets the player choose the gesture (Off by default) | prospero-win d5a1601 (the feature); the dialog follows this project's own working IME code (ps5-radio-browser) |

## Trademarks

PlayStation, PS5 and DualSense are trademarks of Sony Interactive Entertainment Inc. Windows is a trademark of
Microsoft Corporation. Proton and Steam are trademarks of Valve Corporation. Google, Chrome and Chromium are
trademarks of Google LLC. Battle.net is a trademark of Blizzard Entertainment, Inc.; Epic Games of Epic Games, Inc.;
GOG and GOG Galaxy of GOG sp. z o.o.; EA of Electronic Arts Inc.; Ubisoft and Ubisoft Connect of Ubisoft
Entertainment; DuckDuckGo of Duck Duck Go, Inc. They are named to say what this project runs on and is compatible
with, which desktop style its theme follows, and whose installers and search MihawkBrowser reaches. No Microsoft logo
is shown: the Mihawk desktop's Start button shows the project's own Mihawk mark (above). PS5_Proton is not
affiliated with, sponsored by or endorsed by Sony, Microsoft, Valve, Shueisha, Toei Animation or any company named
here; see [`LEGAL.md`](LEGAL.md).
