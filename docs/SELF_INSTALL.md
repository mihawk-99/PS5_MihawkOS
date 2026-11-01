# MihawkOS without a PC: installs itself, says what failed, adds games easily

Agreed with the owner on 2026-10-18. Phase 0's probe round ran that evening (its answers are under phase 0). Phases 1 to 3
and 5 to 7 are built, not yet all run on the console ([PLAN.md](PLAN.md)); the owner then dropped the setup program,
so the title sets itself up (phase 7) and updates itself (phase 8). Before this plan, 5 read-only researchers, a
planner and a critic read main at `71bb2f8`. Their findings are summarised below with the file:line they rest on.
Status lives in [PLAN.md](PLAN.md); this file is the design.

## The goal

1. **A user with a jailbroken PS5 and no PC** installs MihawkOS over the console's internet connection, and later
   updates it the same way.
2. **Every failure is said on screen** in plain words, not only in `/data/ps5proton/logs`.
3. **Adding a game is copying a folder.** No hand-written `game.json`.

## What is true today (main `71bb2f8`)

- **The PC installs everything.** `tools/console-setup.py` copies over FTP:
  - the title, to `/data/homebrew/PPSA99170` (S7, `:580-597`);
  - `/data/homebrew/dev.conf`, 35 bytes (S6, `:563-577`);
  - the Wine runtime, 1,189,920,691 B in 2,459 files, plus `run/proton-payload-child.elf` (S8, through
    `tools/deploy-wine.py`). `deploy-wine.py:191-192` also creates `/data/ps5proton/logs` and `caches`.

  The title builds the `mihawk` prefix itself on its first session (`native/proton_wine.cpp:1059-1087`). It also writes
  the desktop shortcuts (`:296-324`, called at `:1277`) and adds the built-in Mihawk OS entry
  (`ps5/src/proton_launch.cpp:704-716`).
- **Failures are silent.** Every return to the launcher carries the reason `wine-ended`
  (`native/proton_wine.cpp:442`). A missing runtime is only logged (`:1052-1057`). The launcher's "Game library
  unavailable" text needs an empty game list (`examples/ps5proton/ps5proton.cpp:209, 268-274`), which never happens
  because Mihawk OS is always in the list. A crash or a freeze never reaches `back_to_launcher` at all.
- **The title has no HTTPS client.** It has no zstd and no SHA-256 either. Its native side uses libSceNet only for name
  lookups (`native/proton_wine.cpp:518-545`). The pinned payload SDK already has the libSceHttp2 and libSceSsl stubs,
  and the title links the SDK's `*.so` with `--as-needed` (`ps5/tools/link-title.sh:83`), so using them changes no link
  recipe (rule 3). The same calls are proven on this console in the sibling PS5_RetroArch title: a 600,520,132-byte
  GitHub release download with the hash checked (`../PS5_RetroArch/src/webui_update.cpp:274-333`).
- **Sizes**, measured on the PC:
  - title folder: 57 MB, 25 MB at zstd -19;
  - runtime plus `run/`: 1.19 GB, 371 MB at zstd -19 --long (31%);
  - the Mono and Gecko MSIs: 192 MB of that, incompressible;
  - the probe prefix: 787 MB, needed only by the PC tools and the gate.
- **A Microsoft image shipped in every runtime built on this PC** until 2026-10-18: `tools/build-wine-theme.py` turned the
  local Windows 11 "Bloom" JPEG into `proton.bmp`. It now stages a wallpaper the project may redistribute instead
  (Aero Meadow from 2026-10-18, Bubble Earth from 2026-10-25, Bubble City by Skeuoss, with the artist's permission,
  from 2026-10-31), and `tools/publication-check.py` fails a build whose `proton.bmp` is anything else.

## The owner's decisions (2026-10-18)

| Decision | Choice |
| --- | --- |
| Where consoles download from | GitHub Releases of the public repo, published by the owner, built on a clean machine, with a source bundle attached. Nothing is published before phase 9 (rule 10). |
| How a user without a PC starts the first install | Copy the MihawkOS folder to `/data/homebrew` (FTP from a phone or a PC, or a file manager on the console), then open the tile. The title sets itself up (phase 7). This replaced, later on 2026-10-18, a setup program started from pldmgr or a USB stick: "the title can set itself up", as probe 3 proved. |
| How MihawkOS is updated | MihawkOS updates itself, on request, from a signed manifest (phase 8). This replaced version 1's "run the newest setup program again". |
| ShadowMountPlus | **Never shipped** (the owner, 2026-10-18): "we will not be shipping a patched shadowmountplus and instead wait for it to be fixed upstream. The user themselves must use their own SM+". Nothing MihawkOS ships carries or installs it. Releases before 1.7beta6 could leave other homebrew unable to start after MihawkOS closes; MihawkOS asks for beta11 or newer (its payload check, `src/pw_payload_check.h`, checks only that it runs), which has the fix (`docs/LAUNCH_AFTER_EXIT.md`). |
| The desktop in a public build | Bubble City by Skeuoss, used with the artist's permission (`themes/windows11/wallpapers/`), in every build (the owner, 2026-10-31); Bubble Earth (`tools/make-aero-wallpapers.py`) from 2026-10-25; Aero Meadow before it replaced Bloom and the plain colour (the owner, 2026-10-18). |

Defaults the plan takes unless the owner says otherwise:
- **The download starts on request:** the launcher asks once and shows the size and the free space needed.
- **A user install holds only the title, the runtime and `run/`:** no probe prefix, no test programs.
- **Other firmware:** anything but 10.01 is refused on screen.

Still open, needed by phase 6 at the latest:
- Mono and Gecko from `dl.winehq.org` by the pinned hashes (`tools/stage-wine.py:23-32`), or from the release.
- Whether a public build keeps a developer source override (plain HTTP, only for content pinned by hash).

## Phases

Each phase ships on its own and is proven on the console (rule 7, the gate). Phases 1 to 4 need no hosting.

### Phase 0: one probe round, with the owner present (small)

A few tiny probes, each writing `evidence/<run>.json`, run before any code for phases 5 to 7:
1. **The write speed.** The elevated title's buffered write plus `fsync` over 1.2 GB. RetroArch measured a burst of
   about 1.3 GiB, then about 2 MiB/s, but only with O_DIRECT (`../PS5_PayloadSDK/platform/docs/PROBE.md:195-262`). Also
   check whether that budget carries across LoadExec.
2. **HTTPS from the title.** One sceHttp2/sceSsl GET from the elevated title to a public host, with certificate and
   hostname checks on, including a console clock that is behind.
3. **Folders before root.** Whether the title can create `/data/ps5proton` and `dev.conf` before it is root. The two
   sources disagree: `ps5/src/proton_launch.cpp:198-214` and `../PS5_PayloadSDK/platform/docs/PROBE.md:225`.
4. **A large ELF.** Whether elfldr, websrv and pldmgr accept an ELF of about 30 MB. The title's own upload refuses
   anything over 4 MiB today.
5. **Notifications.** Whether a system notification (`sceKernelSendNotificationRequest`) shows over the title's
   VideoOut.
6. **USB.** Whether a Wine helper can list `/mnt/usb0`.
7. **ShadowMountPlus rescan.** Whether ShadowMountPlus answers `POST /api/v1/scan` on 127.0.0.1:10101.
8. **The owner's autoloader.** Whether it carries pldmgr.
9. **First-prefix size.** The size of a freshly built `mihawk` prefix (read-only).

#### Phase 0's answers (2026-10-18, console, the owner present)

Run on the owner's console with perf's build 9e29e0c3 installed. Three probe launches swapped in the probe title,
an isolated build `5588ac8dfac0eda9` of 515899b plus `native/proton_install_probe.cpp`. After each, the installed
`eboot.bin` was put back from its own raw readback (SELF off) and checked byte for byte. No `maxpipekva`,
`TTY(STDIN)` or `errno=5` line appeared in any capture. The probe's 2,070 files were deleted afterwards. The owner
did not agree to set the console's date back, so the clock was checked against servers' Date headers instead.

| # | Question | Answer | Evidence |
| --- | --- | --- | --- |
| 1 | Write speed, and LoadExec | Buffered `write()` plus `fsync` per 64 MiB file to `/data/ps5proton`: 1,216 MiB in 8.5 s (143 MiB/s), every file between 109 and 166 MiB/s, no throttling. 2,048 files of 16 KiB, each written, fsync'd and renamed: 6.85 ms each, 16.7 ms the slowest. After a LoadExec, 256 MiB more at 148 MiB/s: no held-back budget carried across, after 1.5 GB in all. RetroArch's 2 MiB/s buffered figure does not hold for this elevated title writing `/data`. | `probe-install-2026-10-18-02` |
| 2 | HTTPS from the title | **Fails.** Every chain is refused at `sceHttp2SendRequest`: GitHub's release download and dl.winehq.org with `0x8095f00c`, the same code as expired, self-signed and untrusted-root on badssl.com. A wrong host gives `0x8095f00b`. Plain HTTP works: `http://dl.winehq.org/` returned 200 and 4,964 B. The console's clock is right: its time minus the server's Date header was -1 s. The cause is known: `ps5-native-app-boilerplate/docs/CURL.md` (sections 1 and 8) measured that an elevated app's `sceSsl` rejects every public certificate with this code, even after `sceHttpsLoadCert`, while the same build without elevation works. The title's own TLS works: see phase 6 below (`probe-tls-2026-10-18-01`). | `probe-install-2026-10-18-02`, `probe-tls-2026-10-18-01` |
| 3 | Folders before root | Yes. As uid 1 and euid 1, before its root request, with ShadowMountPlus mounting `/data` into the sandbox, the title created `/data/mihawkos-preroot-probe/` and a file in `/data/homebrew`; both were removed at once. | `probe-install-2026-10-18-01`, `-02`, `-usb-…-03` |
| 4 | A 30 MB ELF | websrv's `POST /elfldr`, from the PC: 1 MiB and 31.6 MB accepted with HTTP 200. The payload hashed its 30 MiB blob intact in 3.5 s. elfldr's own port 9021 refuses connections from the PC. pldmgr 0.5.2, driven through its web API on 8084 as the owner chose: it downloaded the 31.6 MB ELF from the PC, checked the list's SHA-256 and started it, intact in 3.3 s. Its source list and files were put back as they were. | `probe-big-elf-*-2026-10-18-*` |
| 5 | Notifications | **Visible over the title.** `sceKernelSendNotificationRequest`, sent from the title while its launcher was drawing, returned 0, and the owner saw it ("I see system notification 1 of 2!"). That was the toast-free run. The first run's miss was the toast: `sceNotificationSend` is a null import in the title, and the title crashed (SIGSEGV, rip 0) about 4 s after the first notification. Not yet tested: the setup payload's notifications with no title in front. | `probe-install-2026-10-18-02`, `probe-tls-2026-10-18-01` |
| 6 | USB | Yes, once the stick was mounted (`/mnt/usb0`: exFAT, `/dev/da1s1`). The title read `mihawkos/probe.bin`, 314,572,800 B, at 114.5 MiB/s. A Wine helper listed and read it: `fs-probe` PASS, 1 folder and 1 file, no errors. During the first pass the stick was empty and GPT-partitioned, which the PS5 does not mount, so every `/mnt/usbN` was an empty tmpfs placeholder. That was the stick, not a console limit. Users' sticks must be MBR with exFAT or FAT32. | `probe-install-usb-2026-10-18-03`, `probe-usb-wine-2026-10-18-02`, `probe-space-payload-2026-10-18-02` |
| 7 | ShadowMountPlus rescan | `POST /api/v1/scan` with `{}` to 127.0.0.1:10101 answered 200. Its API takes JSON only. | `probe-install-2026-10-18-02` |
| 8 | pldmgr | The owner's autoloader carries it: version 0.5.2, read from its own page. Its list must be a bare JSON array: the documented `{"name", "payloads"}` form gives the first payload the list's name. | `probe-big-elf-pldmgr-30-2026-10-18-04` |
| 9 | First prefix | The owner's used `mihawk` prefix: 1,352,411,293 B in the parts a first session makes (`drive_c/windows` 1.07 GB, Program Files 250 MB, users 26 MB, registry 4 MB). A fresh one is smaller. | `probe-prefix-size-2026-10-18-01` |
| - | Free space | `statvfs` is a null import in the title and crashed the first launch. A payload reads it: 1.86 TB free of 1.96 TB on `/data` (statfs and statvfs agree). | `probe-install-2026-10-18-01`, `probe-space-payload-2026-10-18-01` |

What this changes:
- **Phase 5.**
  - An install's writes take seconds, not the half hour RetroArch's figure predicted. The 2,442 files' fsync and
    rename cost about 17 s on top.
  - The free-space check must not use the title's `statvfs`: use `statfs`, which comes from libkernel and is not yet
    tried in the title, or ask the setup program. **Tried on 2026-10-19, in the first-install rehearsal: `statfs` is a null
    import too.** The SDK declares it only in `libkernel_sys.so`, the system processes' libkernel, and calling it
    ended the title at "Check drive". The title no longer asks for free space; a full disk is reported by ENOSPC.
    `tools/check-title-imports.py` now fails any title build with an import that a title process has no provider
    for.
  - The first-prefix allowance is 1.35 GB.
- **Phase 6.** An elevated title cannot use the platform's TLS (`ps5-native-app-boilerplate/docs/CURL.md`), and the
  title must be elevated to write `/data`. So the title brings its own TLS. The candidate is mbedTLS 3.6.7 (the
  release, and the SHA-256, that `../PS5_RetroArch/tools/build-curl.sh` pins) under a small HTTP/1.1 client with
  redirects. It verifies certificates and hostnames against the console's `/system/common/cert/CA_LIST.cer` (readable
  once elevated) or against the Mozilla bundle the build already pins. Plain HTTP with every piece pinned by
  SHA-256 stays safe for integrity, but GitHub serves release assets only over HTTPS.

  **Proven on the console** (`probe-tls-2026-10-18-01`, 2026-10-18, the browser build 2ca89afb installed and
  restored byte for byte; no stop lines):
  - The candidate is `src/pw_https.c`, run in an isolated probe build `b6f7a4c270830f73`. `CA_LIST.cer` (120,132 B)
    was read once elevated, and its roots parsed. Entropy came from `/dev/urandom`.
  - GitHub's release download: 200, 158 B, after one redirect to `release-assets.githubusercontent.com`, in 0.28 s.
  - The Mono MSI from dl.winehq.org: 200, 85,504,000 B in 9.81 s (8.31 MiB/s). That is phase 6's "one public fetch"
    proof, ahead of the code.
  - Refused, each with its own sentence:
    - wrong.host: hostname, verify flags 0x4;
    - expired: "check the console's date and time", 0x1;
    - self-signed and untrusted-root: untrusted, 0x8.
  - Plain `http://dl.winehq.org/`: 200.
  - The host test `tests/test_pw_https.py` checks the same rules against the PC's own TLS servers, and every libc
    function they need is in the title's `libc.prx`.
- **Phase 7** (the first four shaped the setup program the owner then dropped; the title setting itself up rests
  on the third).
  - A self-contained `mihawkos-setup.elf` carrying the title is accepted by websrv and by pldmgr.
  - pldmgr's route is a bare-array list with `checksum`.
  - The title can make `/data/ps5proton` itself before root, where ShadowMountPlus is loaded.
  - USB works for the title and for helpers.
  - Refusals can use `sceKernelSendNotificationRequest`: it shows over a title. With no title in front it is untested.
- **Phase 3.** `sceKernelSendNotificationRequest` shows over the title; `sceNotificationSend` is not available to it.
  It also shows over a full-screen game in Game Mode: the owner saw the test notice in
  `loud-failures-gm-notice-2026-10-18-01`.

### Phase 1: the launcher says why (small)

- **Carry the real reason.** One `fail(text)` helper carries it back through `wine-run.txt` as an `ended=` line, with
  newlines stripped. It replaces every `say()` + `back_to_launcher` pair. The launcher draws "Last session ended:
  <reason>" in the slot it already uses for complaints.
- **Catch crashes.** A session-running marker, written at start and removed on a clean end. The next launch then shows
  "The last session did not end normally", with the log path. This covers crashes, freezes and power loss.
- **Check before starting Wine.** The launcher checks the runtime: missing, or not matching this title. A runtime
  stamp written at build time is compared with the copy pinned in the title folder.
- **Show the `/data` grant's real reason** in plain words: websrv not answering, `dev.conf` missing, daemon failed.
  Also mention rest mode: payloads are gone until a real restart.
- **Create `/data/ps5proton/{logs,prefixes,games,caches,run}`** after the grant.
- **Fix the wording.** Replace the stale "PS5 Proton" strings in the launcher. Fix the README rows that quote screens
  that never appear (`README.md:604-610`) and the stale comment in `tools/deploy-wine.py:20-23`.
- **Proof:** a gate case `launcher-ended`, plus host renders of the panel.

### Phase 2: drop a folder, get an icon (small)

- **Find games at session start.** Before `sync_desktop_shortcuts`, list `<prefix>/drive_c/Games/*`. A folder with
  exactly one top-level x86 or x64 `.exe` (`src/pw_pe_arch.h`) gets a shortcut. A folder with several gets a
  notification that names it.
- **`game.json` becomes optional,** only for arguments and environment.
- **Entries with their own prefix** get a shortcut, or the reason written down.
- **Host tests:** one exe, several exes, a non-x86 exe, an exe only in a subfolder, a folder already covered.
- **Proof:** a gate case `games-autodetect`.

### Phase 3: problems during a session become system notifications (small)

- **Post a notification for each event.** The title sends a PS5 notification for:
  - a memory-guard close or end;
  - a helper that cannot start, with "restart the PS5" for the per-boot class: errno=5, maxpipekva;
  - a GPU broker give-up;
  - a 32-bit installer fixed at 0x400000, which could not run (dropped 2026-10-19: that program's helper gives the
    range back, [IA32_DESIGN.md](IA32_DESIGN.md), and such a program starts; a real installer installed with `/S`, with one open conversion failure).

  This needs no shell code, so it stays clear of the shell sessions' looks-only work. It also works while Game Mode has
  unloaded the desktop. It depends on phase 0, probe 5.
- **Proof:** a gate case `notice-fixed-base32` (retired with the refusal; `fixed-base32` runs the program).

### Phase 4: release hygiene (small; can run alongside 1 to 3)

- **A never-ship rule in `tools/publication-check.py`** over `dist/` and the staged runtime. It catches `proton.bmp`,
  `d3dcompiler_47.dll`, the probe prefix and test programs. It also fails when a build has no licence folder at all:
  today a missing `licenses/` passes (`:216-226`).
- **Stage the missing licence texts:** the Liberation fonts (arial, times, cour); Apache-2.0 for `msyh.ttf`; the Wine
  Gecko (MPL-2.0) and Wine Mono (GPL/LGPL parts) source locations.
- **An About/Legal screen in the launcher.** It shows the copyright, no warranty, and where the licences and the source
  are. GPLv3 asks interactive programs for this (`LICENSE:103-110`). The owner approves the wording.
- **Reconcile two documents.** `LEGAL.md:27` says builds have "no jailbreak code", but `docs/PROVENANCE.md:111-114`
  calls the shipped Lapy daemon jailbreak-side. Agents draft the wording; the owner decides.
- **Proof:** a fresh production build passes the check, plus the gate.

### Phase 5: the title installs its runtime from storage (large)

- **`tools/pack-runtime.py`, on the PC.** It packs a sorted, normalised tar into zstd -19 chunks, each its own frame
  with the default 8 MiB window. Chunk boundaries are chosen by content, so an update reuses unchanged chunks; cutting
  by size would shift them.
- **The title folder pins the runtime's file manifest** `{path: size, sha256}`, written before `gate.py record`. The
  installed `runtime/ge11-7/runtime.json` is the copy the launcher compares in phase 1.
- **`src/pw_pack.{c,h}`, host-tested:**
  - hash each file as it streams;
  - rename it into place only after its size and sha256 match;
  - install only paths in the pinned manifest;
  - refuse links and devices;
  - cap each file at its manifest size;
  - delete files the old manifest lists and the new one does not;
  - keep `wine.inf`'s time stamp;
  - check free space first (files + largest file + first prefix + margin);
  - remove `runtime.json` first and write it last, so an interrupted install resumes.
- **Vendor a pinned zstd decoder and a SHA-256,** each with a `NOTICE.md` entry (rule 6).
- **The launcher installs from a folder.** "Install runtime from storage" works from
  `/data/ps5proton/install/` or `/mnt/usbN/mihawkos/`, with progress, MB/s and typed errors on screen.
- **Proof, approved by the owner:**
  - rename the deployed runtime aside (reversible);
  - put the chunks in place over FTP, playing the USB stick;
  - install;
  - check that 2,459 of 2,459 hashes pass;
  - measure the write curve;
  - run the gate.

#### Phase 5 as built (2026-10-18; host-tested, title compiled and linked, not yet run on the console)

Committed without a gate, as the owner chose on 2026-10-18 ("Gates are skipped so we can ship faster"). Three
changes from the plan above, each on purpose:
- **No tar.** `tools/pack-runtime.py` cuts its chunks at file boundaries. A chunk is one zstd -19 frame of its files'
  bytes, in manifest order. The pinned manifest already names every path, size, SHA-256 and time, so a tar header
  would add nothing. With no header, a chunk cannot bring in a path, a link or a device, and `pw_pack` has no header
  parser to harden. A cut comes after a file whose path and digest hash to 0 mod 8, once the chunk holds 2 MiB, or
  at 16 MiB. A file larger than that is a chunk of its own.
- **The stamp is phase 1's `runtime-stamp`, not a new `runtime.json`.** It is the manifest's last file. `pw_pack`
  removes it when an install starts and renames it into place last, after obsolete files are deleted and the
  manifest's own copy is written to `runtime/ge11-7/runtime-pack.txt`. That copy (or, for a PC-deployed runtime,
  `deploy-manifest.json`) is the old list the next install deletes from. Until phase 1 is on main, the build
  stages no stamp, and `pack-runtime.py` skips the pack.
- **The SHA-256 is written here** (`src/pw_sha256.c`), checked against FIPS 180-4 and Python's hashlib. The console's
  libraries carry none. zstd 1.5.7's single-file decoder is vendored in `external/zstd`, with its NOTICE.md entry.

Measured on the PC, on main's staged runtime: 2,442 files and 1,156,227,861 B make 128 chunks of 411,126,162 B
(35.6%) in 65 s. `--long`'s 371 MB needs a 128 MiB window; the 8 MiB default costs about 40 MB.

Other details:
- **Launcher:** Settings, "Install runtime" (item 3, after About). It looks in the title folder's `runtime-pieces/`
  (since 2026-10-31; before, `/data/ps5proton/install`, then each `/mnt/usbN/mihawkos`).
- **Free space:** checked through libkernel's `statfs`, not the title's `statvfs`, which is a null import there.
  When `statfs` gives no answer, a full disk is reported by the write that fails (ENOSPC).
- **Typed outcomes for the first-launch tour** (`ProtonInstall::Problem`), cancel, and a USB-only search.
- **Tests:** `tests/test_pw_pack.c` covers every rule above; `tests/test_pack_runtime.py` covers determinism, chunk
  reuse and the skip without a stamp.
- **Proof tool:** `tools/run-install-rehearsal.py` is the owner-approved rehearsal. It renames aside and never
  deletes, logs the write curve, and reads every hash back.

### Phase 6: the same install over HTTPS (medium)

- **The network code lives in the title only.** A Wine helper that loaded libSceNet took the title down
  (`native/proton_wine.cpp:518-520`). It follows RetroArch's pattern: certificate and hostname checks on, 15 s
  connect, 30 s receive, 64 KiB reads straight into `pw_pack`, no archive on disk.
- **No address is built in until the release exists.** Each title fetches its own release's manifest by tag or by
  hash, never a "latest" URL. Old release assets must stay online: they are the repair and rollback path.
- **Typed errors on screen:** DNS, TLS ("console clock behind"), HTTP status, hash, disk full. A retry resumes by
  chunk.
- **Proof:**
  - a full install from a LAN fixture;
  - one public fetch of the pinned Wine Mono MSI from `dl.winehq.org`, which proves TLS roots, clock and speed with
    nothing published;
  - a 1 MB fetch in every gate.

#### Phase 6 as built (2026-10-18; host-tested, title compiled and linked, its TLS proven on the console)

Committed without a gate, as the owner chose on 2026-10-18.
- **The title's own TLS.** `src/pw_https.c` is Mbed TLS 3.6.7 (`tools/setup-mbedtls.sh`, pinned by SHA-256;
  `platform/tls/pw_mbedtls_config.h`) under a small HTTP/1.1 client. Certificate and hostname checks are always on,
  against the console's `/system/common/cert/CA_LIST.cer`. It connects within 15 s, gives up after 30 s without a
  byte, follows up to 5 redirects, and takes Content-Length, chunked and read-to-close bodies. Names go through
  sceNetResolver. `ps5/src/proton_fetch.cpp` is the title's single context for it.
- **The install engine's URL source.** Each needed piece is fetched straight into `pw_pack`, with no archive on disk,
  and up to 3 tries per piece, each from the piece's first byte. A refused certificate, a wrong clock or a refusing
  server is not retried. The outcomes are typed for the first-launch tour: `network`, `certificate`, `clock`,
  `server`.
- **No address is built in:** `PROTON_RELEASE_BASE` stays empty until the owner publishes. Each title fetches only
  the pieces its own pinned manifest names.
- **A PC's LAN fixture** can be named in the title folder's `install-source.txt`. `tools/run-install-rehearsal.py
  --route http` writes it for one run and removes it.
- **Proven:**
  - on the console, the same client in a probe title (`probe-tls-2026-10-18-01`, above);
  - on the PC, `tests/test_pw_https.py`;
  - every libc function it needs is in the title's `libc.prx`.
- **Not yet run:**
  - a full install from the LAN fixture;
  - the 1 MB fetch in every gate (gates are off).
- **Phase 0's probe tooling lands with this commit:** `native/proton_install_probe.cpp` (the `PS5_INSTALL_PROBE`
  build), `tools/run-child-probe.py --kind install-probe`, `tools/run-big-elf-probe.py`, `tools/measure-prefix.py`
  and `tests/install-probe/`.

### Phase 7: the title sets itself up (small)

- **No setup program (the owner, 2026-10-18).** Installing MihawkOS is copying its folder to `/data/homebrew` and
  opening the tile. The owner's reason: the title can set itself up, as probe 3 proved.
  - The setup program was built and host-tested first: `mihawkos-setup.elf` with the title inside, preflight refusals as
    notifications, a pldmgr list and a USB autoload folder.
  - It is kept as an archive on the branch `claude/self-installer-setup-archive` (`45fac8b`), never on main. Its
    title packs for `pw_pack` (several stamps, `eboot.bin` then `sce_sys/param.json`) are where phase 8 starts.
  - Its two `pw_pack` fixes landed on their own: the installed manifest record kept byte for byte, and files made
    rwxrwxrwx.
- **Before its first root request,** on every start, the title writes as its own user (uid 1). This is
  `src/pw_first_run.h`, called from `ps5/src/main.cpp`:
  - `/data/homebrew/dev.conf`, only when it is missing, by S6's rule. Without it the Lapy daemon the title starts exits
    2, and a format removes it. A dev.conf holding anything else is the owner's and is kept.
  - `/data/ps5proton`, because how the title asks for root depends on it (`ps5/src/proton_launch.cpp` `request_root`).
  - `/data/ps5proton/logs`, because the resident Lapy daemon opens `logs/lapy-service.log` before anything else and
    exits 2 when it cannot (`platform/lapy/resident-log.patch`). A console that never had the folder was never granted
    `/data`; found by code review on 2026-10-19, before any console showed it.

  It can write there because ShadowMountPlus mounts `/data` into the sandbox, and ShadowMountPlus is also what puts a
  `/data/homebrew` folder on the home screen. The klog line `first run: dev.conf ..., /data/ps5proton ...` says what it
  found.
- **After the grant,** as root, phase 1 makes the folders inside (`logs`, `prefixes`, `games`, `caches`, `run`). The
  install (phases 5 and 6) makes `runtime/` as it writes.
- **The first launch hands over to the guided setup** (the first-launch tour, `docs/GUIDED_SETUP.md`). It drives phase 5
  and 6's engine through `ps5/src/proton_install.h`:
  - `proton_install_find` (`"usb"` for sticks only);
  - `proton_install_start`;
  - `proton_install_state`: the phase, a typed problem, `bytes` (what the install holds), needed and available, pieces,
    progress;
  - `proton_install_cancel`.

  A new prefix's first session has no DualSense (`README.md:441-447`), so the tour runs that session with no game.
- **Not built:**
  - nothing for ShadowMountPlus, by the owner's decision above: users bring their own. If other homebrew will not
    start after MihawkOS closes, the user restarts the PS5;
  - a firmware check: the setup program refused anything but 10.01; the title does not check yet.
- **Proof, with the owner present, after the merges:** rename the title, the runtime and `dev.conf` aside
  (reversible). Then copy the folder over FTP and open the tile. Expect dev.conf written, the grant, the tour, the
  runtime install and the first session.
- **As built (2026-10-18, host-tested, not run on the console):** `tests/test_pw_first_run.c` checks the rule:
  - missing: written through a part file;
  - run again: present;
  - other settings, a longer file or an empty one: kept;
  - no folder: `ENOENT`.

  The title builds and links.

### Phase 8: MihawkOS updates itself (medium)

The owner's decision (2026-10-18). It replaces version 1's "run the newest setup program again", and pldmgr plays no
part. PC-side and host-tested first, as before.
- **Only on request:** Settings > Check for updates, never at every start.
- **A small signed manifest** names the release's title archive and runtime manifest by SHA-256, never by a "latest"
  URL.
  - It is signed with Ed25519. The public key is in the eboot.
  - The verifier is vendored and pinned (rule 6).
- **Download** over `pw_https` into a staging folder, and check every hash.
  - The new title takes over at the next start, through `sceSystemServiceLoadExec` (rule 2).
  - The running eboot is never overwritten in place.
- **The runtime** updates through phase 5's engine: only the changed pieces, with its stamp written last.
- **Rollback:** the previous title files stay until the new title has started once, and old release assets stay
  online.
- **The private key:**
  - the owner generates it and keeps it only on their PC, in an ignored path the privacy check refuses to stage;
  - `tools/sign-release.py` signs at release packing (phase 9);
  - agents never print or commit it.
- **No built-in release URL** until the owner publishes.
- **As built (2026-10-18, host-tested, not run on the console):**
  - **The manifest:** `src/pw_release.h`, `mihawkos-release 1` with `version`, `base` (the release's own tag folder),
    `title` and `runtime` (each a SHA-256 and a file name) and a last `signature` line over every byte before it.
    The signature is checked before any field is read. A version not newer than the running title is refused, so an
    older signed manifest cannot be replayed.
  - **The verifier:** Monocypher 4.0.2's `crypto_ed25519_check` (`external/monocypher`, BSD-2-Clause, pinned by the
    tarball's SHA-256 from two sources, files unchanged; NOTICE.md). Mbed TLS 3.6 has no Ed25519.
  - **Signing:** `tools/sign-release.py`.
    - `keygen` writes the owner's key to `keys/release-ed25519.pem`, mode 0600. `keys/` is ignored, and
      `tools/privacy_check.py` refuses any staged or pushed path in it and any PEM private key under any name.
    - It writes the public half to `platform/update/release-key.txt`, which `ps5/CMakeLists.txt` builds in. Neither
      exists yet: the owner makes them.
    - `sign` makes the flat release folder (title pack, runtime pieces, both manifests, the signed
      `mihawkos-release.txt`), and `verify` checks one.
  - **The title pack:** `tools/pack-runtime.py --title-pack` and `pw_pack`'s title packs came back from the setup
    program's archive. The new title is staged whole in `/data/ps5proton/update/stage`, every file checked by its
    SHA-256 (about 25 MB, so no reuse of unchanged files: `shortcut:` in `ps5/src/proton_update.cpp`).
  - **The switch:** `src/pw_update.h`, at the next start, right after the grant (`ps5/src/main.cpp`).
    - Files are moved in by `rename`, each one it replaces kept in `update/previous/`. Files only the old record
      lists go there too. Then the record, then `eboot.bin` and `sce_sys/param.json`.
    - `ready`, then `switching`, then `switched` say where it is, so a cut switch goes on at the next start.
    - Then the title restarts into the new one with `sceSystemServiceLoadExec`.
    - When the new launcher has drawn once, `previous/` is removed. A process whose own restart was refused never
      removes it.
    - Shortcut: there is no automatic rollback. If a new title never draws, the user restores
      `/data/ps5proton/update/previous` over the title folder, or copies an older release's folder.
  - **On screen:** Settings > Check for updates, one CROSS a step: check, download, restart. Without a release key
    or an address, it says which. A rehearsal names a fixture in the title folder's `update-source.txt`, and a
    rehearsal build names a throwaway key (`cmake -DPROTON_RELEASE_KEY_FILE=...`).
  - **The runtime** the new title pins is installed by Settings > Install runtime from the new title's release base.
  - **Proven on the PC:**
    - `tests/test_pw_release.c`: RFC 8032's test 1; signed, tampered, other-key and out-of-shape manifests;
      versions.
    - `tests/test_sign_release.py`: OpenSSL's signature accepted by the title's check, built for the PC; every
      changed byte and another key refused; the key never printed.
    - `tests/test_pw_update.c`: a whole switch, three cut points resumed, a damaged record refused with the title
      untouched, a hand-copied folder, the confirm.
    - `tests/test_privacy_check.py`: the key rules.
    - `tests/test_pack_runtime.py`: title packs.
    - The title builds and links, and the launcher's Settings panel renders on the PC.
  - **Rehearsal still to run, with the owner present.**
    1. Build title A with a throwaway key, and build title B with a higher `contentVersion`.
    2. Sign B for a LAN base, and serve it on port 1749 (the PC's firewall).
    3. Put `update-source.txt` in A's title folder.
    4. Press check, then download, then restart.
    5. Expect the klog's `update: switch ... status 1`, B's launcher, and `previous/` gone.

### Phase 9: the first public release (the owner publishes)

- **Agents prepare:**
  - a clean build, passing the publication check;
  - the source bundle: the repo at the commit, the patches, pinned upstream tarballs, the sibling forks;
  - the MihawkOS folder as an archive, with its sha256 on the release page and in the README;
  - the signed update manifest (phase 8);
  - a release checklist.
- **Proof before publishing:** the exact release files are proven against the LAN fixture. After publishing, one
  public install: the folder copied, the tile opened.
- **Before publishing,** the owner takes the lawyer questions in
  [PUBLICATION_CHECKLIST.md](PUBLICATION_CHECKLIST.md) and [PROVENANCE.md](PROVENANCE.md):
  - the Lapy daemon under anti-circumvention law;
  - codec patents in FFmpeg (full set, or decoders only);
  - the "Proton" names left: the `Proton UI` font, `/data/ps5proton`, the `PS5PROTON` contentId suffix, `UP9000`;
  - "Mihawk";
  - whether links are enough for corresponding source;

## Rehearsal: a first install from nothing (owner present)

The test closest to a new user's console that one console allows. The owner does the console side. The agent reads
logs only, and writes nothing to the console. The package is built from a fresh clone of main, with fresh clones of the
four sibling repositories beside it, by the README's steps (`tools/package-install.py` on its `dist`).

**This package (2026-10-19)** is commit `33c587a`, main `1dbd80b` plus the `logs/` fix below.
- The clean room cloned `5c5728e` and ran `fetch-all` and the whole production build there.
- It then checked out `33c587a` and ran the production build again, incrementally, so the title and the runtime
  pieces are `33c587a`'s.
- Only the Lapy daemon is not built there: it is the pinned `c9710b7a...` (gap 4).

**What the clean room found on the way:**
1. **The title's licences came before Mesa's WGL build.** With main `47f081b`, the first build stopped after 1522 s.
   Fixed in `995cbb6`.
2. **`llvm-strip` met a fresh wineboot's 16-bit placeholders** in the tools' probe prefix: 50 files, none in the
   runtime. Fixed in `995cbb6`.
3. **Nothing ran `tools/extract-dxvk.py`.** `stage-wine.py` found no `d3d12.dll`: with `995cbb6`, after 1457 s.
   Fixed in `5c5728e`.
4. **The Lapy daemon is not reproducible on this PC any more.** Its pin needs clang/LLD 23.1.1; the host has 23.1.3
   and builds `aa30237b...`, so `build-production.sh` refuses it. The clean room uses the pinned ELF, checked by
   SHA-256. Closed on 2026-10-20: the daemon now pins payload SDK v0.42 (`5651f2ad...`) and the clean room rebuilt it
   from source to exactly that file with clang and LLD 23.1.3.
5. **Found by code review, not by the build:** the daemon opens `/data/ps5proton/logs/lapy-service.log` first, and
   before root the title made only `/data/ps5proton`. A new console would never have been granted `/data`. Fixed in
   `33c587a`.

**The package** (since 2026-10-31 one folder, the owner: "package everything in a single PPSA title file, and have
the setup place the runtime by itself"):
- `PPSA99170/` is the title folder, with the runtime's pieces in its `runtime-pieces/`;
- `SHA256SUMS` lists every file; `README.txt` is the user's three steps;
- `PS5_MihawkOS-v<version>.zip` holds the three, the release's one download, with its `.sha256` beside it.

**Before, on the PC:** to check the package, run `sha256sum -c SHA256SUMS` from its folder.

**On the console:**
1. **Restart the PS5** (a real restart, not rest mode).
2. **Load only these:**
   - kstuff;
   - elfldr and websrv. websrv is required, not only elfldr: the title starts the Lapy daemon and every Windows
     helper with `POST /elfldr` to websrv on 127.0.0.1:8080, never through elfldr's own port;
   - ShadowMountPlus;
   - klogsrv (port 3232, so the agent can read the kernel log);
   - ftpsrv, if the folder is copied over FTP and for the log reads afterwards.

   Do not load ps5vkctl, and run none of the PC's console tools (`console-setup.py`, `console_guard.py`, deploys):
   they write to the console.
3. **Move the current install aside**, in this order, over FTP or with a file manager. These are renames on the one
   `/data` file system; nothing is deleted.
   1. `/data/ps5proton` becomes `/data/ps5proton_bkp`.
   2. `/data/homebrew/PPSA99170` becomes `/data/PPSA99170_bkp`. It must leave `/data/homebrew`, not only change its
      name: ShadowMountPlus registers every folder there by the title id in its `param.json`, and two folders with
      `PPSA99170` would race.
   3. `/data/homebrew/dev.conf` becomes `/data/homebrew/dev.conf_bkp`.

   Check that `/data/ps5proton`, `/data/homebrew/PPSA99170` and `/data/homebrew/dev.conf` are gone. A stale tile may
   stay on the home screen: do not open it yet.
4. **Copy the title folder the user's way:** `PPSA99170/` into `/data/homebrew/`, either from a phone's FTP client
   (ftpsrv, port 2121) or with a file manager from a USB stick. It is about 0.6 GB with its `runtime-pieces/`. Wait
   about 45 s for ShadowMountPlus to register it.
5. **Open MihawkOS.** What should happen:
   - Before it asks for root, the title writes `dev.conf` and makes `/data/ps5proton` and its `logs/` (phase 7).
   - It starts the Lapy daemon and may restart itself once. The screen goes black for a few seconds; that is the
     elevation retry.
   - The guided setup opens at Welcome, with its music.
6. **Walk the tour:**
   1. Choose a language.
   2. Installing starts by itself: the runtime from the title folder, about 1.55 GB, about a minute.
   3. At "Preparing Mihawk OS", choose Start. That first session makes the `mihawk` prefix and ends itself.
   4. The tour comes back at Before you play. Hold ✕ until the ring is full (10 seconds).
7. **The first real Mihawk OS session:** at Done, choose Go to the game library, then Mihawk OS. Check the desktop, the DualSense, the Start menu and
   a window. End the session, then come back to the launcher.
8. **A game by folder (phase 2):** with MihawkOS in its launcher, copy a game folder that has one x86 or x64 `.exe` at
   its top over FTP to `/data/ps5proton/prefixes/mihawk/drive_c/Games/<Name>/`.
   - Let the copy finish first: big FTP copies slow the console.
   - Open Mihawk OS: the desktop should show the game's shortcut.
   - Start the game at 120 Hz, as every game test.

**What the agent reads, and nothing else:**
- **Live:** the kernel log from klogsrv, read-only over TCP (`../PS5_Vulkan/tools/ps5_console.py klog`), into an
  ignored `klog/first-install-<date>.log`, with addresses masked. The lines that tell the story:
  - `first run: dev.conf written (1), /data/ps5proton made, /data/ps5proton/logs made`;
  - `elevation: granted=1`;
  - `install: from /mnt/usbN/mihawkos, <n> pieces`, one `install: piece` line per piece, then
    `install: done into /data/ps5proton`;
  - the tour's steps;
  - the first session's end.
- **After the run,** with MihawkOS in its launcher or closed, by FTP downloads only (no upload, rename or delete):
  - `/data/homebrew/dev.conf`;
  - a listing of `/data/ps5proton`;
  - `logs/title-last.log`, `logs/wine-last.log`, `logs/lapy-service.log` and `logs/sessions/`;
  - `setup.txt`;
  - `runtime/ge11-7/runtime-pack.txt` and `runtime-stamp`, compared with the package's.

  If the owner asks, every installed runtime file is read back and hashed: 1.55 GB, a few minutes, read only.

**Restore, in this order** (renames again, nothing deleted):
1. Close MihawkOS: End session, then hold Options in the launcher.
2. Move the test's results aside, kept for reading:
   - `/data/ps5proton` becomes `/data/ps5proton_firstinstall`;
   - `/data/homebrew/PPSA99170` becomes `/data/PPSA99170_firstinstall`;
   - `/data/homebrew/dev.conf` becomes `/data/homebrew/dev.conf_firstinstall`.
3. Put the backups back:
   - `/data/ps5proton_bkp` becomes `/data/ps5proton`;
   - `/data/PPSA99170_bkp` becomes `/data/homebrew/PPSA99170`;
   - `/data/homebrew/dev.conf_bkp` becomes `/data/homebrew/dev.conf`.
4. Restart the PS5 and load the payloads as usual. The restart also ends the Lapy daemon the test started.
   ShadowMountPlus registers the restored title; open it once to check.

The results go aside before the backups come back, so no folder is ever written over. A game folder copied during the
test is in `/data/ps5proton_firstinstall`; copy it again if it is wanted in the restored prefix. The `_firstinstall`
folders can be removed later, at the owner's word.

## What stays the user's job every boot

Load kstuff, elfldr and websrv, and ShadowMountPlus after each cold boot; an autoloader can do it. Rest mode unloads
them. A user without a PC needs no ftpsrv or klogsrv: only the PC tools use them. From phase 1 on, the title checks what
is missing and says so on screen.

**Payload check:** MihawkOS opens only with the official payload set: Kstuff Lite 1.11 Beta or later, ShadowMountPlus
beta11 or newer (asked for; only its presence is checked), elfldr and websrv, with NanoDNS, klog, ftpsrv and pldmgr allowed
(`src/pw_payload_check.h`).
Releases of ShadowMountPlus before 1.7beta6 could leave other homebrew unable to start after MihawkOS closed
(`0x80940033`, `docs/LAUNCH_AFTER_EXIT.md`); every release MihawkOS asks for has the fix.

## Who does what

Phase 1, 2, 3, 5, 6, 7 and 8 code is title, launcher and platform code. It is not shell code: shell sessions change looks
only. Console slots go through the coordinator, one at a time, each with a gate. Phase 0 and the phase 5 and 7
rehearsals need the owner at the console.
