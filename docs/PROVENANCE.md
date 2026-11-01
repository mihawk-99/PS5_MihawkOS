# Provenance of the PS5 platform knowledge

Where this project's knowledge of the PS5's interfaces (system calls, exported functions and their NIDs, kernel
structures, the GPU's command and display interfaces) comes from, one section per platform dependency. Licences and
attributions are in [`NOTICE.md`](../NOTICE.md); the plain-language summary is in [`LEGAL.md`](../LEGAL.md).

Each section answers four questions: where the interface knowledge came from, whether any Sony or AMD proprietary
file, leaked document or firmware was used, what is pinned, and what evidence supports calling it clean. Answers come
from the dependency's own files at its pinned revision, from this repository, and, where the files could not answer,
from the owner. The open questions of 2026-10-18 were closed on 2026-10-19; none remains (end of this page).

How this was checked (2026-10-18, no console run): each sibling checkout was read at the revision this project pins;
its tracked files were searched for Sony and SCE copyright or confidentiality markers, official SDK headers, `.sprx`
modules, firmware files, decrypted or dumped binaries and leaked-document names; prospero-win's imported files were
compared with `wine/prospero-import.sha256` (43 of 43 equal; on 2026-10-19 also against prospero-win's public commit
itself, below). Archives that are fetched at build time (the payload SDK
releases, pacbrew) were not downloaded again; their pins are recorded, not re-verified here.

## PS5_PayloadSDK (platform layer, payload SDK, NIDs)

- **Interface knowledge.** The platform layer (`platform/include/ps5platform/*.h`, GPL-3.0-or-later, written by the
  project's author) declares the exported functions its titles call. Its headers say the behaviour they state is
  "what our own console probe measured" (`kernel.h`, `agc.h`, `videoout.h`). Its signatures match the open-source
  OpenOrbis PS4 toolchain's public headers where that toolchain declares the same functions (for example the 0x28-byte
  software-version record and the protection bits); the rest (JIT memory, configured flexible memory, thread affinity)
  are checked by the layer's own probe (`platform/src/probe.c`, `probe_threads.c`). The header's phrase "the public PS4
  SDK" (`kernel.h:13`, `:77`) is wording chosen by the AI agent that co-authored commit `33637c2`; its working record
  shows it looked up no PS4 SDK and built the declarations from prototypes already in the owner's other PS5 ports.
  Neither upstream payload SDK (ps5-payload-dev, ps4-payload-dev) declares these functions. Which functions each
  system module exports was recorded by reading the owner's local firmware reference (below), in a note kept in a
  separate project that MihawkOS does not need. The link stubs (`sce_stubs/`) are upstream ps5-payload-dev's
  generated stubs (symbols with no code); `sce_stubs/genstub.py` names them from `aerolib.csv`, a public NID-to-name
  table fetched from github.com/zecoxao/sce_symbols at build time.
- **Proprietary material.** None found in the tree: no Sony or SCE copyright or confidentiality marker (the only
  "CONFIDENTIAL" lines are FreeBSD header boilerplate), no `.sprx` module (`.gitignore` excludes them), no firmware
  file. The `libSce*.so` files are the upstream link stubs, not console modules.
- **Pinned.** The fork at `e105cf6f25801d9e9981812a89a3b74021d2d2a2` (`ps5/tools/setup-sdk.sh`), its main: fdv tracks 16384 files instead of 8192 (a
  10,000-file probe stopped at 8294 on a PS5 Pro, 2026-10-31, and passed with it). It installs upstream's payload SDK v0.42, SHA-256
  `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`, and lays its headers and platform layer over it.
  The Lapy daemon builds with the same upstream v0.42 archive, SHA-256
  `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da` (`tools/fetch-all.sh`,
  `tools/build-lapy-validation.py`); v0.42 is the oldest release whose crt accepts firmware 13.40, 13.42 and 13.60.
- **Evidence for clean.** The fork is one public upstream (github.com/ps5-payload-dev/sdk) plus the author's own
  GPL-3.0-or-later layer, whose headers name their source of each behaviour and whose host tests run with
  `make -C platform test`; the release archives are checked by SHA-256 and used unchanged.

## PS5_Mesa (RADV's PS5 GPU winsys)

- **Interface knowledge.** The winsys (`src/amd/vulkan/winsys/ps5/`, MIT, written by the project's author) models the
  GPU as Mesa's own GFX10.3 chip `CHIP_GFX1013`, from Mesa's public models and AMD's public documentation; each
  PS5-specific trait is recorded as first found on the console through PS5_Vulkan's probes and Vulkan CTS runs
  (`radv_ps5_winsys.c`, PS5_Vulkan `RADV_PHASE.md`). The AGC and VideoOut calls go through PS5_PayloadSDK's platform
  headers (above).
- **Proprietary material.** None found in the PS5 files: no Sony, SCE or confidentiality marker, no binary blob.
  "firmware" appears only in Mesa's upstream amdgpu comments. No AMD file outside Mesa's own public tree.
- **Pinned.** `radv_ps5_platform.h` from `1ba9874f478657ccf6da72ce55f0260164cc0bd0`
  (`tools/setup-graphics-foundation.sh`); the GPU broker's winsys, command-stream and WSI overlays at
  `7b59ef27c1b09b9671bc4153c41940c3155c3af2` ([`NOTICE.md`](../NOTICE.md)); the Windows Zink/WGL DLLs from
  `b3588f78dd7fd46fbadc7ecd487e5f14d4213e4b` (`tools/build-mesa-windows-zink-i686.sh`; the 64-bit build takes the same
  revision from PS5_Vulkan's recipe, [`NOTICE.md`](../NOTICE.md)). Builds export these revisions with `git archive`, never the working tree.
- **Evidence for clean.** MIT code on Mesa's MIT tree, reproducible from the pinned revisions; the console-specific
  values are each tied to a recorded console measurement.

## PS5_Vulkan (RADV recipe, graphics foundation, ps5vkctl, native converter)

- **Interface knowledge.** Its README's provenance table credits the console's own AGC and VideoOut modules as "used on
  the console through the SDK's published import stubs; never redistributed", the register knowledge to the PS5
  OpenGL SDK, AMD's public register database and a sibling format reference, and its runtime shim states it is not
  copied from a game, console firmware or a Sony SDK (`docs/RUNTIME_SHIM.md`). The graphics transport, owner/client
  runtime and display that this project uses were written in this project (extracted from Proton `878c1e9`).
- **Proprietary material.** None in what this project takes. The owner's separate repository PS5_Vulkan carries three
  probe shader packages, `probes/shaders/{vertex,pixel,resources}.bin`, recorded as "copied byte-for-byte" from the
  ProsperoLight project's assets (`probes/shaders/PROVENANCE.txt`), with no source or generator. MihawkOS does not use
  or ship them: the build exports PS5_Vulkan's tree at its pin only into the git-ignored `.deps/`
  (`tools/setup-pinned-radv.sh`), the title takes only `runtime/libc.prx` from it (`ps5/tools/link-title.sh`), and a
  byte search of a production build's title folder and runtime found none of the three (2026-10-19). Their origin
  belongs to PS5_Vulkan's own publication.
- **Pinned.** The graphics foundation and RADV link recipe at `f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed`
  (`tools/setup-graphics-foundation.sh`, `tools/setup-pinned-radv.sh`); the headless helper's native converter at
  `ab37acd506305ba3e575435e3ae9a351cfaf7cf4` (`tools/build-helper-converter.py`); ps5vkctl's source
  `payload/ps5vkctl/main.c` at `f3cbf875f89978deae9f855b9be2610cec2818ae`, SHA-256
  `f625ff130b4bbca07b92c20184cc69c9cd381d5cd2f9e65ff9d7dcf910bcb83f` (`tools/build-console-control.py`).
- **Evidence for clean.** The project's own MIT, LGPL and GPL code on Mesa; every input exported from a pinned
  revision and checked; the probe shaders above are not read by any build step and are not in its outputs.

## prospero-win (the PS5 Wine series and runtime adapters)

- **Interface knowledge.** prospero-win (LGPL-2.1-or-later) is the origin of the PS5 Wine patch series and of the
  platform knowledge behind it: Wine as PRX modules, the in-process wineserver, the PS5 user driver, the display, audio
  and controller paths ([`NOTICE.md`](../NOTICE.md)). Its NOTICE says it is independently authored and that no
  proprietary SDK file is copied into it; AGC and VideoOut are reached through link-only import stubs whose
  implementations the console supplies; its controller layouts are the ones public PS4/PS5 reimplementations agree on.
- **Proprietary material.** None found in the snapshot: no Sony, SCE or confidentiality marker, no `.sprx`, no firmware
  or game file. Its NOTICE lists "no firmware dump, no disassembly and no decompiled output" among what it does not
  include, and its own publication check blocks capture and dump folders.
- **Pinned.** Runtime adapters at `c8a2f00137fb0a95f88541252b4e47f53f9677e6`; `wine/ps5/pw_ws2_32_libc.c` and its test
  at `d1ca1e597648f28e1f1d9c55f7d7ba9b1f0a0690`; patch 0918 from its patch 0770 at
  `1532474b0f792378b9fbe3f4f0282c4ed030e7d5`; the logger header `ps5log.h` at `e665a2c1f9168c911cf1f6131b832f89a5582fb4`,
  SHA-256 `394af67d0f8b60b3335deb53396e52855ea2daa50ca914a456ea7663f48900c6` (`tools/fetch-all.sh`). The 43 imported
  files and their hashes are in `wine/prospero-import.sha256`; the adaptations are `wine/prospero-adaptations.patch`.
- **Evidence for clean.** The 43 imported files equal their recorded hashes (checked 2026-10-18 against the local
  snapshot); every patch from prospero-win keeps its authorship and rebase notes. On 2026-10-19 all 43 were also found
  equal to `wine/prospero-import.sha256` at prospero-win's public commit `c8a2f00137fb0a95f88541252b4e47f53f9677e6`,
  read from git (`git fsck` clean). The local snapshot `../prospero-win-main/` is an older revision and differs in 14
  files; the build does not use it, except `ps5log.h`, which it takes only when its SHA-256 equals the pin. To repeat
  the check:

  ```sh
  git init x
  git -C x fetch --depth 1 https://github.com/mpereiraesaa/prospero-win c8a2f00137fb0a95f88541252b4e47f53f9677e6
  git -C x show FETCH_HEAD:<path> | sha256sum    # each path in wine/prospero-import.sha256
  ```

## PS5-Lapy-JB-Daemon (`/data` access)

- **Interface knowledge.** An upstream MIT project (Arksama / Team PHU). Its documentation describes an owned-root
  design validated on firmware 12.02 that checks its kernel-layout assumptions at run time and refuses when they fail.
  This project's client is the platform layer's `ps5platform/elevation.h`, derived from ps5-native-app-boilerplate's
  (GPL-3.0-or-later).
- **Proprietary material.** None found: no Sony or SCE marker, no official SDK header. Upstream tracks one prebuilt
  release ELF; this project does not use it and builds the daemon from source.
- **Pinned.** Revision `5b8397b9f2b5f12a7bc2f9c8745a00d1c2dd01ad` with the patches in `platform/lapy/`; the resident
  build is pinned by SHA-256 `76481d3d2ea5f9e588111e74281ae47eea283b0f23481ef8fb71c2402c6e7fc0`
  (`tools/build-production.sh`; payload SDK v0.42), reproducible from any folder with clang and LLD 23.1.3
  (`platform/lapy/deterministic-build.patch`). Licence: `platform/lapy/LICENSE` (MIT), equal to upstream's.
- **Evidence for clean.** Built from public source at a pinned revision, reproducibly. The daemon raises a title's
  privileges on a console that already runs homebrew; it is a jailbreak-side component by nature. This project's eight
  patches in `platform/lapy/` add no new console constants; `sdk042-ucred-attrs.patch` adapts an existing field
  access to payload SDK v0.42's interface, and `optional-log-channel.patch` keeps the daemon running without its logs. The cooperative owned-root backend MihawkOS builds
  finds the credential slot at run time and refuses unless exactly one slot matches (upstream `owned_root_daemon.c`,
  `README.md`). Upstream credits named public projects for the offsets of its legacy backend (`README.txt`,
  `docs/ELEVATION.md`), which MihawkOS does not build.

## ShadowMountPlus (mounting the title)

A separate GPL-3.0 project by drakmor that the user installs from its upstream release. MihawkOS neither ships nor
modifies it, so none of its interface knowledge is in this repository.

## Local firmware reference (static reading)

Some of this project's notes on how the console's system modules behave were made by reading a copy of the firmware
10.01 system modules that the owner dumped from their own PS5 Pro and keeps on their own computer, outside every
repository: `docs/PLAN.md` (the shared direct-memory route), `docs/FEATURES.md`, the three
`evidence/*-static-10.01.json` records and `tools/verify-service-spawn.py`, which checks those landmarks against a
copy the user supplies. Only interface facts are kept from it: function names, NIDs, export offsets, file hashes and
notes on how a few functions are called. No firmware file, and no bytes or disassembly of one, is in this repository or
in any release of it; the rule is in `docs/CHILD_PROCESSES.md`.

## Other platform inputs

The PRX converter is built from `mpereiraesaa/ps5-native-app-boilerplate` at
`30597512539e7edfde079cbcaf4a626bc0a948c5` (`tools/fetch-prx-tools.sh`, GPL-3.0-or-later), and the diagnostic
title-layout OpenGL runtime links the PS5 OpenGL SDK 1.0.2 (release SHA-256 in `tools/fetch-opengl-sdk.sh`, with its
own `THIRD_PARTY_NOTICES.md` and provenance). Both are recorded in [`NOTICE.md`](../NOTICE.md); their own provenance
was not audited for this page.

## Open questions for the owner

None remain. The five of 2026-10-18 were closed on 2026-10-19: the PS4 SDK wording and the export list (PS5_PayloadSDK,
and the local firmware reference above, from the owner's answer), the probe shaders (PS5_Vulkan, not used by
MihawkOS), the prospero-win revision (checked against its public commit) and the Lapy daemon's kernel layout (found at
run time; upstream credits its legacy offsets).
