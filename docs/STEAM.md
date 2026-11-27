# Steam on MihawkOS: persistent sessions, elevated local-process games, and the launcher option

## Persistent Steam sessions and elevated Sony local processes (2026-11-26, branch `fast-steam`, base PS5 13.60)

On branch `fast-steam`, Steam sessions no longer shut Steam down or restart the title through `LoadExec` when starting or
closing a game, and 64-bit Steam games no longer contend for either the single in-title process slot (`eboot.bin`) or
the 8,192-entry system page-table pool.

### What changed

1. **Phase 1 — Faster Steam startup and I/O:**
   - `wine/patches/2485-ps5-fast-steam-file-and-server-ops.patch` and `wine/ps5/pw_wine_xattr.c`: faster Steam file,
     extended-attribute, and wineserver operations during client startup and game launch.
   - `src/pw_steam.c`, `src/pw_shader_cache.c`, `wine/ps5/pw_wine_code_budget.{h,c}`: shader-cache namespace reuse and
     tuned code-cache budgets for Steam and its browser helper (`tests/test_pw_wine_code_budget.c`).
2. **Phase 2 — Persistent Steam lifecycle (no close/reopen `LoadExec` handover):**
   - `native/proton_wine.cpp` and `ps5/src/proton_launch.cpp`: for Steam sessions (`is_steam_root && !play_session`),
     `steam.exe` remains alive across game launches and exits rather than shutting down and handing off through
     `LoadExec`.
   - `wine/patches/2486-kernelbase-ps5-reap-held-helper-before-starting-game.patch`: before creating a library game
     process, cleanly terminates and waits up to 2 s to reap the held `steamwebhelper.exe` process so its GPU broker
     slot and ~1 GiB direct memory are released before the game allocates its own; if game creation fails, resumes the
     held helper cleanly.
   - On game exit, Steam remains live, Big Picture immediately resumes foreground presentation, and **Steam Cloud**
     completes both `AC Launch` and `AC Exit` syncs reliably.
3. **Phase 3 — Hybrid elevated Sony local processes (`/app0/wine-local-child.bin`) + 65,536 CPU / 32,768 GPU PT pool:**
   - Why Phase 2 alone was not enough: running 64-bit Steam games in `elfldr` payload helpers kept them on the console's
     8,192-entry system page-table pool, where heavy 64-bit games mapping 3–6 GiB quickly dropped below the page-table
     guard threshold. Earlier `docs/FLEXIBLE_MEMORY.md` probes thought Sony local processes (`sceSystemServiceAddLocalProcess`)
     could not execute JIT code on firmware 13.60 only because the probe helper had not elevated through the resident
     Lapy daemon.
   - Proof (`evidence/sony-local-elev-pt-probe-02.json`): elevating a Sony local process via Lapy (waiting for
     `geteuid() == 0` with private credentials before touching memory) grants full JIT execution (`jit_made=0`,
     `jit80_made=0`, `jit80_value=42`, `way=jit prot=5 value=42`), `RLIMIT_NOFILE = 13952/13952`, and full access to
     the title's expanded `param.json` page-table pools (`65,513 / 65,536` CPU PT, `65,536 / 65,536` GPU PT), returning
     100.0% of page tables and `1,031,798,784` bytes of flexible memory on `sceSystemServiceKillLocalProcess`.
   - Implementation:
     - `ps5/sce_sys/param.json`: raises `kernel.cpuPageTableSize` to `268435456` (**256 MiB = 65,536 CPU PT**) and
       `kernel.gpuPageTableSize` to `134217728` (**128 MiB = 32,768 GPU PT**), leaving **11,456 MiB** direct memory.
     - `native/pw_local_child.h`, `native/proton_local_child.c`, `native/proton_wine_child.c`, `tools/build-local-child.sh`:
       builds and signs `/app0/wine-local-child.bin` (headless SELF importing only `libkernel.sprx` and
       `libSceLibcInternal.sprx`), elevates via Lapy with 3-attempt retry, loads `ntdll.prx` via
       `sceKernelLoadStartModule` (`~5–6 ms`), and runs the shared Wine child loop without ever calling `exit()` or
       returning from `_start`.
     - `wine/ps5/pw_wine_process.{h,c}` and `native/proton_wine.cpp`: keeps a pre-warmed elevated local helper ready
       (`25–30 ms` spawn latency) and routes 64-bit graphics processes (`steamwebhelper.exe` and 64-bit games) to Sony
       local processes when `WINE_PS5_LOCAL_GAMES=1` (the Steam-session default; `WINE_PS5_LOCAL_GAMES=0` falls back to
       payload helpers). `steam.exe` (32-bit), 32-bit games, and Wine services remain in `elfldr` payload helpers.
       Direct-launched titles outside Steam keep their existing routing untouched.
     - `platform/graphics/patches/0003-gpu-broker-cpu-only-direct-memory-fallback.patch`: falls back from `0x33` to
       CPU-only `0x03` direct-memory mapping in GPU broker clients inside Sony local processes without an initialized
       AGC GPU VM, while `eboot.bin` retains `0x33` GPU access.

### Measured results on the standard PS5 (firmware 13.60, 2026-11-26)

| Run (`evidence/`) | Workload | Warm spawn | Rendering | CPU PT free (of 65,536) | System PT free (of 8,192) | Memory & Cloud |
| --- | --- | --- | --- | --- | --- | --- |
| `sony-local-elev-pt-probe-02.json` | Elevated local-process probe | – | `jit80_value=42`, `prot=5` | `65,513 -> 65,396 -> 65,513` (100% reclaimed) | 0 system PT consumed | `1,031,798,784` B FMEM 100% reclaimed |
| `p3-local-village-2026-11-26-02.json` | Heavy 3D 64-bit game via Steam | `30 ms` | `7,636` direct flips, **119.9 FPS** (`p99 8.75 ms`, 0 gaps >= 100 ms) | `62,395` (`gpu 32,690/32,768`) | `3,721` | `4.5 GiB` direct memory (`4,925/11,456 MiB` free), clean exit to `11,178 MiB` free |
| `p3-local-re3demo-2026-11-26-01.json` | 3D Direct3D 12 demo via Steam | `26 ms` | `4,802` direct frames, **117.7 FPS**, verified 1080p capture | `62,543` (`gpu 32,277/32,768`) | `3,819` | `1.99 GiB` VRAM + `1.14 GiB` host DMEM, clean exit to `11,265 MiB` free |
| `p3-e2e-spelunky2-twice-2026-11-26-01.json` | 2D 64-bit game launched **twice** in one Steam session | `27 ms` (#1), `30 ms` (#2) | **122 FPS** (#1), **122.7 FPS** (#2), live Big Picture between runs (3 captures verified) | `63,661` (#1) -> `64,574` (returned) -> `63,644` (#2) -> `65,002` (exit) | `3,929` (#1) -> `3,971` (#2) | Steam Cloud `AC Launch` + `AC Exit` `Upload complete` on **both** cycles; zero `LoadExec` handovers |

**What remains open on `fast-steam`:**
- Running the full console commit gate (`tools/gate.py run`) and testing the hybrid elevated local-process path on the PS5 Pro (firmware 10.01).

---

## The launcher's Steam option, and the client inside the Pro's memory limit (2026-10-24)

Status 2026-10-24 (PS5 Pro, firmware 10.01, guard 870 MiB unchanged). Code: `src/pw_steam.{h,c}` (portable, host-tested by
`tests/test_pw_steam.c`, `tests/test_process_rules.c`), `ps5/src/proton_steam.{h,cpp}` (download thread, catalog entries, side panel),
the Aurelia page's Accounts rows in `examples/ps5proton/ps5proton.cpp`, and the sign-in watcher in `native/proton_wine.cpp`
(`PROTON_STEAM_SIGNIN_WATCH`).

## What the option does

0. **Where it lives.** R2 opens the Aurelia page; UP from its shelf opens the Accounts rows: *Library account* (Aurelia's own QR sign-in) and
   *Steam client* (this option). The library list has no Steam row (until 2026-10-24 it did). Every session started from the Accounts rows
   returns to those rows (`/data/ps5proton/aurelia-open` holding `accounts`).
1. **Download.** CROSS on the *Steam client* row starts a thread that fetches Valve's installer over the title's own TLS
   (`proton_fetch`, certificate and host name checked) into `/data/ps5proton/downloads/SteamSetup.exe.part`, then checks it
   (`pw_steam_installer_check`: 512 KiB..32 MiB, a PE header, a non-empty certificate table) and renames it. Nothing of Valve's is in the
   repository or a release.
2. **Install.** The launcher restarts into the catalog entry `steam-setup` (the installer with `/S`, prefix `mihawk`, no desktop). The session
   ends when the installer does.
3. **Sign in, once.** The row then reads *Installed*; CROSS restarts into the entry `steam` (the client's own window, the process rules in its
   `env`, `PROTON_STEAM_SIGNIN_WATCH=1`). The user scans the QR code with the phone app (hold OPTIONS for 2 s cancels; 30 minutes without a sign-in ends the session). The title polls `config/loginusers.vdf`; once a user with
   `RememberPassword 1` is saved it asks the client to close at once and ends it once the client's token (`local.vdf`) is written after the
   sign-in, 25 s after the close at the latest, without the hang snapshot (until 2026-10-26 it waited 20 s first, then 10 s,
   then printed a 16 s snapshot). In practice it is the 25 s: the client writes `local.vdf` only when it next starts
   (`evidence/guide-owner-2026-10-26-02.json`), and the sign-in survived that end. The row then reads *Signed in*.
4. **Afterwards** a game that needs the client starts it headless (`-silent -no-browser`, entry `250900` is the model), with the same rules.

The install and sign-in states are read from the prefix on every call (`proton_steam_state`), not remembered, so a restart or a manual change shows.
The hook `/data/ps5proton/client-press` (a file the launcher polls every 0.5 s) presses the row without a pad, for tests.

## The guide (2026-10-26; every page and screen drawn on the base PS5; the real first run not yet)

The owner: "users are left confused". So the first time Steam is asked for, guided screens in the guided setup's style say
what will happen and keep saying it until the client is signed in. Later presses go straight to Big Picture.

| What the user sees | Where it is drawn | Code |
| --- | --- | --- |
| **What will happen**: download from Valve's servers, install, first start (Steam updates itself, about 5 minutes), sign in with the phone app; internet needed, about 10 minutes, not affiliated with Valve. Start or Not now | the launcher (the setup tour's Steam mode) | `examples/ps5proton/setup_tour.hpp`, page `PW_STEAM_PAGE_INTRO` |
| **Downloading Steam**: a glass bar filling, "1.2 of about 2.4 MB" (the installer measured 2,380,800 bytes; a download carries no length) | the launcher | page `PW_STEAM_PAGE_DOWNLOAD` |
| **Installing Steam**: the install session, elapsed time, the flowing glass tube | the title, over the session | `src/pw_steam_screen.{h,c}`, screen `INSTALLING` |
| **Steam is updating itself**: about 5 minutes, a still screen is normal, megabytes downloaded and elapsed time; after 180 s without a moving byte or a new process, "taking longer than usual" with CROSS to try again; after 600 s the session ends and the launcher offers Try again | the title | screens `UPDATING`, `SLOW` |
| **Get your phone ready**: open the Steam app, tap the shield, scan the code; shown from the client's browser start for 25 s (its sign-in page came about 20 s after it on the Pro), CROSS goes to it at once | the title | screen `SCAN`; then nothing: the client's own window and its QR code |
| **You're signed in as <name>**: Steam closes and reopens in Big Picture, made for the controller, with settings tuned for the PS5. The session ends at once (the owner, 2026-10-26: Steam stayed up 48 s under a 20 s countdown); the screen stays still over the client until it is ended | the title | screen `SIGNED_IN` |
| **You're signed in** again with "Opening in 5 s" and Continue, then Big Picture | the launcher | page `PW_STEAM_PAGE_DONE` |
| **A failure**: no internet, the console's clock, a failed download, an install that did not finish, an update that stopped, a cancelled sign-in, 30 minutes without one, a Steam Guard code asked for, Steam closed before the sign-in. Try again or Back | the launcher | page `PW_STEAM_PAGE_FAILED`, `PwSteamFailure` |

How it holds together (`src/pw_steam_guide.{h,c}`, host-tested by `tests/test_pw_steam_guide.c`):
- **Across the restarts.** The launcher writes `/data/ps5proton/steam-guide.txt` (`stage=install|signin`) before it restarts into a
  session. The title adds `result=` when it ends the session itself (signed-in, retry, cancelled, timeout, stalled, guard). On its
  next start the launcher resumes from the record and from what the prefix holds (`pw_steam_guide_resume`): after the install it starts
  the sign-in session by itself; a sign-in on disk is the done page; anything else is a failure page with its reason.
- **In the session.** The entries carry `PROTON_STEAM_GUIDE=install|signin` (`src/pw_steam.c`). Such a session skips the old loading
  screen; once the GPU broker runs, the title draws the guide's screens over the client's windows through the broker's cover (graphics
  patch 0030, as the setup's first session does), ten frames a second, and takes them off for the client's sign-in page. Once a second
  it feeds the state machine (`pw_steam_session_tick`): the client's package folder's bytes, the processes it starts, its browser
  (`steamwebhelper`), the saved sign-in, a Steam Guard denial in the client's connection log.
- **A still screen over a drawing client.** Patch 0030's cover held back only the compositor's loop; a window's present still went to the
  screen, so the client's frames replaced the guide's picture between its pictures, ten a second. The owner saw intense flicker on the
  signed-in screen (Steam's library drew 617 frames in 10 s under it). Graphics patch 0032: while a cover is set, a window's frame only
  updates the scene (`gpu-cover ended ... held=` counts them).
- **The pad.** Over a guide screen CROSS is the guide's: every button pressed while a screen shows stays away from the client until
  it is let go, so a press never clicks what cannot be seen. OPTIONS held for 2 s still cancels.
- **Words and faces.** Every word is the guided setup's (`PW_TEXT_STEAM_*` in `src/pw_setup.c`), in all ten languages, in the setup's
  language. The title draws with Inter and, for Chinese and Japanese, the Noto Sans cuts the build now also ships as TrueType
  (`assets/setup/fonts/*.ttf`). A display name the faces cannot draw is left out ("You're signed in").
- **Pictures on the PC.** `tools/test-setup-ui.py` renders the launcher pages (`PROTON_STEAM_GUIDE_TEST`) in every language and the
  sessions' screens through `tests/test_pw_steam_screen.c`, which also checks that every word stays on the card, no line is cut and
  every character has a face.

On the console (base PS5 13.60): `steam-guide-pages-2026-10-26-02` drew the launcher's 50 pages and
`steam-guide-screens-2026-10-26-01` the title's 50 session screens (5 screens in 10 languages), both PASS. After graphics patch 0032,
`cover-held-2026-10-26-01`: a window drawing under the guide's screen had 522 frames held back and every capture shows
the guide (`docs/PLAN.md`).

Not shown yet: a real first install and sign-in through the guide. The sign-in's timings (the browser's start before the page, the 20 s after a sign-in) and the
Steam Guard words in the connection log are from one measured run or not measured; the console runs will say.

## Why the sign-in is the hard part

The process rules and the headless start are independent of the sign-in, but the first sign-in needs the client's real window: its browser draws the
QR code and the account page. The signed-in client's library window is the heaviest thing it does (503 MiB in one browser process on the Pro, a guard end at 44 s in
the earlier run; the sign-in page's largest process is 214 MiB), so the option asks for it once, ends the session when the sign-in is saved, and runs headless from then on. What the window costs on
a fresh install is measured below.

## The first run on a fresh install, measured (2026-10-24, PS5 Pro 10.01, guard 870 MiB unchanged)

Harness: `tools/run-steam-option.py` (`set-aside`, `run --leg install`, `run --leg signin`, `put-back`); records
`evidence/steam-option-2026-10-24-0{2,3,4}.json` (install) and `evidence/steam-signin-2026-10-24-0{2,3}.json` (sign-in), klog under
`klog/` (never committed). Each run starts from a prefix with no client folder (the signed-in one moved to `Steam.keep`, put back after).
- **Install leg: 22-27 s from the press to the client's program in the prefix** (three runs). The installer, 2,380,800 bytes, came from Valve's
  content network over the title's TLS (certificate and host name checked) and passed `pw_steam_installer_check` (so the "signed installer"
  assumption holds for the real file); the silent run (`/S`) ended by itself and the launcher came back. The installer writes `Steam.exe`
  (capital S); the console's file system is case-sensitive, Windows is not, so the option's checks and the library's executable check now match
  names ignoring case (`src/pw_path_nocase.{h,c}`; before, the first run showed the client as "not installed").
- **The client's first start updates itself twice and restarts itself twice.** Its bootstrap downloads and installs a 241 MB package, starts
  the new `steam.exe` and ends; that one downloads a 239 MB package for the 64-bit client, starts the next `steam.exe` and ends; the third is
  the client. The session used to end when the first process ended (the title took the root's end for the session's end and closed the
  others 15 s later), so the sign-in never appeared (`steam-signin-2026-10-24-01` and `-02`: 145-170 s, ended in the second update). Fix: the
  server exports its count of user processes (patch 2073, the count Wine's own shutdown waits on) and an entry that sets
  `PROTON_SESSION_FOLLOWS=processes` ends only when that count has been 0 for 5 s (native/proton_wine.cpp). The client's entry sets it; every
  other entry behaves as before (a stray process of a game must not hold a session that used to end with the game).
- **The sign-in window appeared about 5.5 minutes into the session on a fresh install** (the run's pictures every 20 s: the updater at 300 s, a black window at 320 s, the
  account form and the QR code at 340 s; `steam-signin-2026-10-24-03`, one run; the time is the two updates' downloads and extraction). The window is the real client UI in its single browser process (the `--single-process` rule).
- **Counted memory (the guard's census, MiB), the whole first run:** 166 at the start, 242 in the first update, 209-219 in the second, **526-577
  with the sign-in window up (8 helpers)**, 566-577 steady for the last 70 s. Peak 589 (the guard's own sample) of the 870 limit: 281-293 MiB of headroom, no guard action.
  In an earlier run the stock process set of an installed client counted 946 at its game's start; the rules are why this one stays under.
- **Nothing else moved:** after the runs the signed-in client folder was put back and Isaac was run again from the library entry on the final build
  (`lib-isaac-2026-10-24-02`: the client's window, the game's title screen, a room change by the pad script, about 61 frames a second, counted peak
  680 of 870 MiB, no guard action: the same as `-01`). The launcher's row and side panel on the Pro: `steam-row-2026-10-24-02` (the row reads
  *Signed in*, a console whose client is not signed in shows *Sign in*; the panel's words are `proton_steam_panel`). The library hides the option's
  own entries (`steam`, `steam-setup`) as it hides any entry in the desktop's prefix, unless `library-show-all` is present in the data folder
  (it is on the test console, so its list shows them).
- **Not tested:** an actual sign-in (no credentials were entered, by design): the watcher that ends the session 20 s after the sign-in is saved is
  covered by `tests/test_pw_steam.c` (the sign-in file's reading) and by the loop's own logic, not by a run. What the client does after a real QR
  sign-in (its library window's cost on a fresh account, a Steam Guard prompt) is unmeasured.

## Measurements

### The client and Isaac on the Pro, inside the memory limit (2026-10-24, Pro 10.01; records `lean-*` in evidence/, klog/)

The Steam client (`steam.exe -silent -no-browser steam://run/250900`) starts Isaac, and the Pro's guard (870 MiB, unchanged) no longer ends it.
Counted memory at the game's start (the guard's census, MiB): 946 with Steam's stock process set, 856-863 with the browser forced to one
process, **680 with the overlay window process refused as well** (`lean-r10-2026-10-24-01`: 243 s, Isaac at about 61 frames a second, no guard
action). Each step, measured:
- **Steam's start-up probes and the install-script runner do nothing useful here** (`steamsysinfo`, `gldriverquery[64]`, `vulkandriverquery[64]`,
  `hardwareupdater` x6, `bin\SteamService.exe /installscript` for the redistributables): refused or run as `hostname.exe`. A vendor updater
  verifies and restores its own files, so changing the files does not work (six stubbed programs were restored from its package cache): they are
  refused when started (patch 2070, `WINE_PS5_PROCESS_RULES`, per application in game.json "env").
- **Steam's browser helper cannot be refused** (`lean-e5`: the core crashes into its error reporter at once), **but it can run as one process**:
  `--single-process` appended to its command line by rule: 13 helpers / 555 MiB -> 2 processes / about 210 MiB (it grows to 260-270 when the
  game starts). `--disable-gpu` added nothing.
- **`gameoverlayui64.exe` (Steam's overlay window) counted 108 MiB** and is not needed: refused by rule.
- **`steamservice.exe /RunAsService` (25 MiB)** refused: the client opens a "Steam Service Error" dialog, so it is kept (`lean-r7`).
- **The two driver hosts for USB and Bluetooth** (`winedevice.exe` for wineusb.sys, winebth.sys, about 14 MiB each). Disabling the services'
  start type did not stop them: wineboot registers a root device for each, and the plug-and-play manager loads a root device's driver whatever the
  service's start type is. Deleting their `Enum\ROOT\WINE\WINEUSB|WINEBTH` entries stopped both (4 -> 2 `winedevice`): patch 2072 does it
  before the services start and does not make the devices.
- **`plugplay.exe` (19 MiB) and the Eventlog's `svchost.exe` (14 MiB)** are not used here (the PS5 port starts the controller bus itself): start type
  disabled in the prefix, no program or controller affected in the Isaac run (the pad script's CROSS presses reached the game).
- **A nearly empty Wine helper costs 14-15 MiB from its first second**, before Wine runs anything (carrier, libc, ntdll.prx, its own threads): it
  is the floor per process, so the number of Wine system processes matters (mountmgr, ndis/nsiproxy, services, rpcss remain: about 15 each).
- **Shared code copies are in effect** (patch 1127 and the registry): in the multi-process Steam run 177 of 313 MiB of code clusters (57%) came from
  another helper's object; the title counts 126 MiB as copies beyond the first. The Pro's guard still counts every mapping in full (the count-once
  census is off below firmware 13), so sharing saves memory the guard cannot see; with the browser in one process there is little left to share
  (29-32 MiB).
- **Also found:** the title link step did not depend on `ps5/sce_sys/param.json` (a changed param.json was not packaged): fixed in
  `ps5/CMakeLists.txt`. The in-title wineserver aborted twice (snapshot code reading a helper's TEB as its own: patch 0921 now checks the process;
  `process_destroy` asserting on pending asyncs: patch 2071 prints the asyncs before it aborts, cause still open).

**Isaac from the library, end to end, on the Pro (2026-10-24, `lib-isaac-2026-10-24-01`).** The catalog entry `250900` now starts the real Steam
client (`steam.exe -silent -no-browser steam://run/250900`, prefix `mihawk`) with the process rules in its own game.json "env" (the wrapper entry
it replaced is kept in `~/PS5-Backups/game-json-2026-10-24/`). No extra settings on the launch: Steam's update window, then the game (it reports
itself as Repentance+): intro, title, menu, the saved run continued, and a pad script's stick moved Isaac through a door into the next room (the
controller reaches the game under the Steam client). 280 s, census peak 679 MiB of the 870 limit, 61 frames a second, no guard action. The game's
first picture comes about 110-130 s after the launch (Steam's start-up and update window, then the game's load).
- The cuts that did it, all now carried by the build and the entry, none by hand-edited prefix files: patch 2070 (process rules), 2072 (no USB or
  Bluetooth root devices; the plug-and-play and event-log services off), the entry's rules (probes refused, install scripts as `hostname.exe`,
  the browser as one process, the overlay window refused). From the original prefix registry the first session already had 2 `winedevice`, no
  `plugplay`, no `svchost` (`lean-r11`); steady counted memory fell from 946 (stock) to about 580 (peak 679 at the game's start).
- **One run in nine did not reach the game** (`lean-r11-2026-10-24-01`, the first session after the prefix registry was put back): the client sat at
  `LaunchApp waiting for user response to ShowInterstitials` and its UI stopped logging, with the browser process alive and 0 frames. The two
  identical runs after it (and the library launch) went through. Cause not found; if it recurs, a watchdog that ends such a launch and starts it again
  is the fix to build, and the UI's logs (`steamui_html.txt`, `webhelper_js.txt` in the client's logs folder) are where it shows.
