# Plan

## Steam warns first when the console's memory leaves it too little room (2026-10-31; host-tested; not on the console)

The owner: "I want for situations like this a warning screen that says something like 'Steam will likely not work under
your configuration, please follow these steps ...'". An Alpha 6 tester's console used 1,277 MB before MihawkOS.
- **When:** before Steam installs or starts, from Steam's tile, the setup's Install Steam and Files' `steam.exe`. The
  desktop's Steam shortcut runs inside the shell and is not covered.
- **The check** (`src/pw_steam_memory.h`): the memory helper's census (every process's resident memory but the title's)
  is taken by the launcher at that moment, so a fresh start is covered too. It warns when the census plus Steam's need
  passes the 1,800 MiB swap point.
  - Steam's need, from the memory session's records: 500 MiB until it is signed in, and 450 MiB after.
  - The first install's first start measured +396 MiB (steam-signin-real-2026-10-24-01); +100 margin.
  - Big Picture with a download measured +328 MiB (tester-steam-download-2026-10-31); +120 margin.
  - So it warns above 1,300 or 1,350 MiB. The tester's 1,277 MB would not have warned, and their failure was not
    memory.
  - With no /data or no figure, there is no warning.
- **The page:** "Steam will likely not work right now", the numbers, then the steps:
  - fully close games and apps (rest mode is not enough);
  - close other payloads, when the checklist counts extra ones;
  - restart the PS5 with only the official payloads;
  - install or open Steam before anything else.
- **Buttons:** ○ goes back. △ held 5 s goes on, with the checklist's ring. Both choices are logged.
- **Not proven:** that `sceKernelGetResidentFmemCount` answers for other processes when the title itself calls it; the
  memory helper calls it today. If it does not, the census is unknown and nothing warns. The console check should look
  for `steam memory:` lines.
- **Proof, PC only.** `tests/test_pw_steam_memory.c` covers the edges (1,300/1,301 for an install, 1,350/1,351 signed
  in), the tester's figures, and no census. `tools/test-launcher-ui.py --render` uses a made-up census
  (`PROTON_MEMORY_TEST`) and covers every path, in English and Japanese.

## A failing payload checklist can be passed by holding TRIANGLE for 5 s (2026-10-31; host-tested; not on the console)

The owner: "Payload checklist: add a hold button for 5 seconds to skip it in case the user doesn't pass the checklist.
Not giving a way through causes frustration".
- Only a failing check offers it. "Hold △ for 5 seconds to continue anyway" sits at the bottom right with the
  "Before you play" page's ring, which fills as △ is held. Letting go starts over, and ✕ still checks again.
- Held 5 s, the launcher goes on as after a pass, to the guided setup when it is due or else the menu. The menu's top
  line says once, for this start: "Some payloads are missing or extra, so games may not start or may close."
- The title log names what failed, for example: `checklist: skipped by the player after 5 s hold; missing: Kstuff Lite
  1.11 Beta or later; extra: extra.elf`.
- Nothing remembers a skip: the next start checks again.
- `src/pw_payload_check.h`: `pw_payload_skip_offered`, `pw_payload_skip_hold` and `pw_payload_skip_line`, host-tested.
  The hint and the line are in the setup's ten languages; the rest of the checklist page is still English.
- **Proof, PC only.** `tests/test_pw_payload_check.c` checks that a pass offers nothing, that letting go at 4.875 s
  starts over, that the skip lands on the 40th 0.125 s frame, and the log line.
  `tools/test-launcher-ui.py --render` uses a made-up failing table (`PROTON_PAYLOAD_TEST=fail`, PC build only). It
  checks the skip into the menu, a release at 4 s that does nothing, and that the next start checks again. The hint is
  rendered in English and Japanese.

## The main menu is a grid of tiles, with Files and a game's own options (2026-10-31; host-tested; not on the console)

The owner chose the tile-grid mockup (v2), a Frutiger Aero tile grid, and asked for: the QR code big, "users must be
able to find it easily"; Mihawk OS "always Leftmost and big"; Steam on its own tile; a pointer the left stick moves (not
the touchpad). Then: the setup's sky behind it; Files: "Main menu
should have its own file explorer where a user can simply launch an executable without even needing to open the
desktop shell"; Triangle: a game's options, with Settings moved to the dock.
- **The grid** (`src/pw_home_grid.h`, drawn by `examples/ps5proton/home_grid.hpp`): five columns, two rows a page.
  - Page 1: Mihawk OS over two columns and both rows, always first; Steam's tile; three games (four without Steam);
    the Web UI's QR code, a column two rows tall. Later pages: ten games each.
  - The dock under every page: Settings, Files, Payloads.
  - The D-pad moves to the nearest tile that way, pages at the grid's edges, and goes down into the dock. L1/R1 page.
    The left stick moves a pointer (the Mihawk hand in Aero glass); the tile under it takes the focus. The D-pad hides
    it. The touchpad does not move it.
  - Steam's tile is always there; without /data it says "Needs access to /data". It shows the word Steam and our own
    icon, a store bag carrying a controller (the owner's pick of three), never Valve's logo or a likeness of it (Legal,
    2026-10-31: that logo is licensed to Steamworks partners only and would imply endorsement).
  - The dock's Settings is a cog (the owner, 2026-10-31). The hint row is centred under the dock on the menu, Files
    and Options ("I want them centered"); a run checks its left and right margins from the picture, in every language.
  - Across the top: why the focused game cannot start, then Steam's or the runtime's problem, a newer release (R2
    restarts into a downloaded one), the payloads' memory.
  - Themes (Square) now style Settings' pages only; the menu, Files and Options keep the sky and the gloss look.
- **Files** (`src/pw_drive_browse.h`, shared with the WebUI's browser): the mihawk prefix's `drive_c`, read only.
  - Folders first, then programs (`.exe`, any case), each by name; other files are counted, links and hidden names
    left out. Every path is checked part by part: no `..`, no separator, no link on the way out of `drive_c`.
  - Cross on a program queues `profile=mihawk-os` with `path=C:\...` (wine-run.txt). The session runs it with the
    entry's prefix, but none of the desktop's words, and checks it and picks in-title or a helper from its image, as
    for a library game (`native/proton_wine.cpp`). The launcher runs the same check first, so a program that cannot
    start says why on the page.
  - Plain words for: no /data, no C: drive yet, an empty folder, a folder gone, a program that cannot start.
- **Options** (Triangle on a game): Game Mode (as Settings, on, off), Controller (as Settings, then each), Run inside
  MihawkOS (`WINE_PS5_TITLE_GAMES=0` when off). Saved in the entry's `game.json` `env`, the words the WebUI's editor
  writes (`proton_game_set_env`: temporary file, fsync, rename). A scripted run previews and writes nothing.
- **Languages** (the owner, 2026-10-31: the whole menu in the setup's languages, in Alpha 7). The menu, Files, Options,
  Steam's tile, the QR tile, the dock and the hints use 74 keys in `src/pw_setup.c`'s table, in all ten languages. The
  language is the one recorded in `setup.txt`.
  - Chinese and Japanese draw in their own Noto face, baked from that table. The menu loads it only for those
    languages, only once it shows, and frees it when the language changes. Its cost while loaded is the atlas, once in
    memory and once as an R8 texture (the `home: face ... loaded` line).
  - Names are never translated: games, files, Steam, Mihawk OS, addresses. A name is drawn in the language's face
    when that face carries every character, else in the kit's.
  - Settings, Controls and the theme picker followed (113 more keys): every row, its line and badge, every panel,
    the hints, the music plate and the close ring.
    - The About page's licence notices are translated too; the English wording stays the reference.
    - The 30 themes' names, families and summaries are in the table too (90 keys), read by the theme's id. Names
      taken from a framework (Daisy, Pico, Chakra, Material) and the "after ..." credits keep the framework's name.
      English is the kit's own words, and each run checks it still is.
  - Still English: reasons that come from deeper layers (a game's complaint from the program check, the runtime and
    /data problems, update and payload notices, the installer's and updater's own messages).
  - The setup's Done page now says Settings is in the main menu's dock and Steam installs from its tile (it said "in
    the library"), in every language.
- **Proof, PC only.** `tests/test_pw_home_grid.c` (layouts, no overlap, moves, every tile reachable, L1/R1, the
  pointer's hit test and speed); `tests/test_pw_drive_browse.c` (listing, order, the `.exe` filter, the guard, the
  Windows path); `tools/test-launcher-ui.py --render` (D-pad, pointer, pages, the dock, Options previews with
  `game.json` unchanged, Files' listing and launch request with `drive_c` unchanged, no C: drive yet; the menu in each
  of the ten languages, About in all ten, Files and Settings in five, each run checking every word against its face's
  glyphs: none missing).
  `tools/run-launcher.py --controls` reaches Settings by Down, Cross.
- **Not proven yet:** on the console (the pointer's feel at 120 Hz, a program from Files started in-title and in a
  helper, Options read by the next session).

## The runtime ships in the title folder; the setup installs it by itself (2026-10-31; host-tested; not on the console)

The owner: "It doesn't make sense to have a usb zip file and FTP/USB installation screen. Why not package everything in
a single PPSA title file, and have the setup place the runtime by itself".
- **One folder.** `tools/package-install.py` puts the runtime's pieces in `PPSA99170/runtime-pieces/` and makes one
  zip, `PS5_MihawkOS-v<version>.zip`, with `SHA256SUMS` and the README.
  - On Alpha 6's build: 137 pieces, 508 MiB packed, 1.52 GiB installed.
  - The folder grows from 55 MB to about 0.6 GB.
  - The development and gate builds (`dist/PPSA99170`) keep no pieces; the gate installs the runtime with
    `tools/deploy-wine.py` as before.
- **The setup.** How to install, USB and FTP are gone: Welcome, Language, Installing (starts by itself), Preparing,
  Before you play, Done. Steps 2, 3 and 7 are not reused.
  - The engine reads the pieces only from `/app0/runtime-pieces` (`proton_install_find("title")`). It checks every
    piece's and every file's SHA-256 against `runtime-pack.txt` as it streams.
  - A problem (no pieces, some missing, damaged, a full disk) shows its sentence and Try again only.
  - A set-up console whose runtime is another build's (the folder replaced by a newer release) starts at Installing,
    copies, and closes into the library. The launcher then clears its "games cannot start" note without a restart.
- **Measured on the PC** (`src/pw_pack.c`'s loop over Alpha 6's real pieces, read from disk):
  - 532 MB read and 1.60 GB in 2,556 files written and checked in 17.5 s;
  - the same again over it (an update where nothing changed) in 7.8 s.
- **Expected on the console:** the USB and FTP installs, the same engine, took 44-48 s for 1.55 GB
  (`first-install-unattended-2026-10-19-02/04/05`), and a check where only the stamp changed took 9.9 s
  (`setup-ftp-pro-2026-10-24-01`). Reading the pieces from the internal SSD instead of a USB stick (114 MiB/s) should
  not be slower.
- **Free space.** The title cannot read it (statfs and statvfs are null imports). A full disk is the write's ENOSPC,
  "storage full", with Try again. The docs ask for about 4 GB free: 0.6 GB for the folder, 1.6 GB for the runtime,
  1.4 GB for the first Windows drive.
- **Not changed:** the signed online updater (`tools/sign-release.py`). Its title pack does not carry the pieces, so
  after an online update the folder would hold the previous release's. No release key exists yet; decide before one
  does.
- **Not proven yet:** any of it on the console.

## The guided setup cannot be skipped, and says what MihawkOS cannot run (2026-10-31; host-tested; not on the console)

The owner's two Alpha 7 changes, by their own words:
- "Make it mandatory and remove the Skip button": there is no Skip button and no △ shortcut, and the text key is gone.
- A screen with MihawkOS's limits that the player must hold ✕ for 10 seconds to pass.

**Before you play** (`PW_SETUP_LIMITS`, step 8) comes after Preparing and before Done.
- It says what MihawkOS is: a compatibility layer, an alpha.
- A warning box lists what it cannot run: games with DRM such as Denuvo, and games with anti-cheat or kernel-level
  protection such as Easy Anti-Cheat or BattlEye. Such games won't start, or they close.
- A ring around the ✕ fills over `PW_SETUP_HOLD_SECONDS`. Letting go empties it, and a hold counts only if it starts on
  the page.
- `done=1` is still written only at Done, so a console turned off on the way comes back to the tour. A re-run from
  Settings cannot be left either: Settings clears `done` as it starts it. A failed step keeps its own Try again and
  Back, and nothing moves past it.
- The counter has six steps: Before you play is 5 of 6, Done is 6 of 6. Each step's log line says what it shows.
- A test run may hold a button over frames: `hold <program> <first> <last> <action>` in test-run.txt.

Proven on the PC only:
- `test_pw_setup` covers the way through, the hold (9 s held does not go on; let go, it starts over; an unbroken 10 s
  goes on) and all ten languages.
- `tools/test-setup-ui.py` adds the page in ten languages, a short hold, a full hold that reaches Done, and △ on
  Welcome and Language ending nothing.
- `tools/run-setup-unattended.py` holds ✕ for 1,500 frames before Done.

Not proven yet: any of it on the console.

## The guided setup ends on a choice: Install Steam or the game library (2026-10-31; host-tested; not on the console)

The owner's Alpha 7 change. Done used to restart straight into the Mihawk OS shell; it now shows "You're all set" and
two tiles (`examples/ps5proton/setup_tour.hpp`, `PW_TEXT_DONE_*` in `src/pw_setup.c`, all ten languages). **Install
Steam**, which is focused first, opens the Steam row's guide at its intro page. "Not now" there lands in the library.
**Go to the game library** closes the tour into the library with Mihawk OS focused, one press from the shell. A
player who skips Steam finds it later on the library's Steam row.

- Proven on the PC only. `tools/test-setup-ui.py` (lavapipe) passes with 128 pictures. It renders Done in every
  language and presses both tiles: the library tile logs `setup: ends (the library)`, and Install Steam logs
  `steam guide: starts at page 0`. `test_pw_setup` checks every key is present and drawable in all ten languages.
  The PC library has only Mihawk OS, so the focus moving to Mihawk OS among other games is not shown yet.
- `tools/run-setup-unattended.py` now goes Done → library → Mihawk OS. A scripted launcher may launch from the library
  only in that unattended run.
- Not proven yet: any of it on the console. The next standard-PS5 slot runs the guided-setup walk and the unattended
  rehearsal.

## A title never runs another build's runtime or helper (2026-10-29; host-tested, built, not yet on the console)

**Why.** A tester on firmware 11.00 "reinstalled Alpha 4" and still hit errno 92. The klog showed the kernel loader's
fself decrypt failure at ntdll.prx. Their helper printed no `module loader=` line, so it was not Alpha 4's: it spoke
Alpha 4's protocol 0x50573512 like the c4a3458 prototype. Their runtime's cache-version (96555103…) matches no
published build. Reinstalling the title touches neither `/data/ps5proton/runtime` nor `run/`, and three gaps let the
mix run:
- the runtime stamp was checked only in the launcher's library, and their starts came from home-screen tiles
  (`launch: mode=game profile=... cycle=0`);
- the guided setup counted any ntdll.prx as an installed runtime;
- `runtime-stamp` covers only `runtime/`, so the helper in `run/` was never compared.
Write-up: errno92-stale-runtime-alpha4.md in the coordinator's notes.

**What changed.**
1. **The check, in one place.** `src/pw_runtime_check.h` (host test `test_pw_runtime_check`, which includes the
   tester's case): this build's runtime, missing, or another build's. Another build's covers a stamp that is missing,
   for example an install cut short.
2. **Every Wine start checks it.** `proton_run_wine` (native/proton_wine.cpp) refuses another build's runtime before
   any helper starts, whatever the start: a tile, a shortcut, `profile=`, or a restart. It returns to the launcher with
   the plain reason.
3. **The setup tour judges by the stamp.** `facts.runtime` (examples/ps5proton/setup_tour.hpp) is "this build's
   runtime", so the tour opens on its install step for another build's runtime, as it does for a missing one.
4. **The title carries its own helper.** tools/build-production.sh copies `proton-payload-child.elf` into the title
   folder, ps5/tools/deploy.sh always sends it, and the title points `WINE_PS5_PAYLOAD_IMAGE` at it. A title therefore
   always starts the helper it was built with. The runtime's `run/` copy stays for older titles and test tools. A
   helper whose ntdll is another build's is refused by the existing protocol check.
   Why the title folder rather than adding `run/` to the stamp: the stamp would then flag the helper, but the title
   would still start whatever `run/` holds whenever the check is skipped. A title folder is replaced whole by every
   install and update, so the helper can no longer drift.
5. **Every session log names its build.**
   - The title logs `Build: title <contentVersion>, runtime stamp pinned <16> installed <16> (this build's | missing |
     another build's)`, at the top of every session: the launcher's and each game's.
   - Each game session also logs `Build: helper <path> sha256 <16>`.
   - Each helper's log starts with `wine-child: build <id>`, a digest of the sources and headers it was compiled from
     (tools/build-payload-loader.sh).

## Memory plan: every fix, and how each is proven (2026-10-28; the owner's plan, in progress)

Goal: no freeze or crash on any console; games get as much memory as each console can safely give; the player sees a
clear message instead of a crash. Approved by the owner on 2026-10-28 ("I want the entire plan executed end-to-end and
proven"). Data: docs/TESTER_MEMORY_RESULTS.md (tester-probe-3step), the ceiling test's runs, the base's kernel logs.

**Part 1, a limit per console** (src/pw_memory_guard.h, `pw_memory_marks_for`; host-tested, not yet on a console).
The guard keys on the console's generation, from the application's direct pool (12,928 MiB on a Pro, 11,712 on a
standard PS5 or Slim; a size not known takes the Pro's marks), and the INSTALLED firmware, never the reported one.
- WARN, an on-screen notice, on the resident census, where the console starts to swap.
- STOP, a clean close back to the launcher, on the same resident census. A first try keyed STOP on "committed" memory
  (the others' resident plus our helpers' mapped), but a helper's mapped figure is its address space: the gate
  memplan-gate-2026-10-28-01 read 48 GiB and the guard ended every game (4/104). No figure on the console counts what
  has been swapped (no swap sysctl on 13.x), and a standard PS5's resident memory flattens at the swap point (1,800-1,846
  MiB while the ceiling test filled 4 GiB), so there STOP comes from behavior, the triggers below, until the owner's run
  settles it.
- A second STOP trigger: at WARN or past it, a helper start more than 3 times slower than the session's first and
  150 ms or more (`pw_memory_start_slowed`; the ceiling test's starts went from 51 ms to 187-249 ms).

STOP fires on whichever comes first, on every console: the resident mark (below); a helper start 3 times slower than
the session's first, at WARN or past it; resident memory held within 32 MiB of its plateau for 60 s with a frame gap
over 1 s in that time (`pw_plateau_sample`; our Pro plateaued at about 869 MiB, under its 950 mark); the page-table net.

| Console | Firmware | WARN (resident) | STOP's resident mark |
| --- | --- | ---: | ---: |
| standard PS5 or Slim | 12.00 and later | 1,750 | none (resident flattens at 1,800-1,846) |
| standard PS5 or Slim | before 12.00 (5.50-11.60 measured; 6.xx-9.xx untested, given these), or not known | 1,650 | none |
| Pro | every firmware | 850 | 950 |

Every warn and stop logs the resident census and the helpers' address space, so the owner's runs can set the final STOP.

**Part 2, crash protection.**
- Done: the page-table guard (close at 768 free, end at 256); Steam's browser in one process; a page-table reading
  with more free than total (the Pro's 9,436 of 9,216) is unknown, logged once and never acted on.
- Explained, no safe fix in our layer: the kernel's "DMEM leak is detected" at a close from the home screen. Each
  helper's direct memory is charged to the application's container, which the system deletes 100-170 ms after it kills
  the title, while some helper is still tearing down. A helper that gave its ranges back itself, before exiting, hung in
  the kernel and outlived SIGKILL (gate dmem-giveback-gate-2026-10-28-04, shell64; most likely wired module data), so
  the give-back was removed. The kernel takes those ranges back itself at the container's deletion; the plan's
  after-close check (memory back within 16 MiB) measures that on both consoles.
- Open, Alpha 6: the page-table drift. The kernel's used count falls 20-35 for each MihawkOS session and never comes back
  (drift-base-2026-10-28: the ceiling test, which runs no Wine, +13; MihawkOS's launcher -18; a Wine program in helpers
  -35, the same with Wine's sections out of direct memory; the 2D game closed from the home screen -29). About -1,700 over a
  gate; after enough sessions it wraps, as the Pro's did. Not the memory the memory helper makes and the title maps: the
  title mapping 256 MiB more of it (shared or granted direct memory, PROTON_PT_PROBE) changed nothing, -34 to -38 for
  every run (ptprobe-base-2026-10-28). It follows the title's processes: about -17 for each MihawkOS title process (two
  per launched session). The bisection (PROTON_PT_EXIT=start|granted|platform, the title ending at that stage) places it
  against the title's root grant from the Lapy daemon and its platform layer. Its first try did not bisect: the flag
  matched the raw launch word, which never matched, so all seven runs were whole sessions (-32 to -37 each again,
  bisect-base-2026-10-28); Alpha 6 reads it from the parsed launch (proton_launch().env).
  - Alpha 6, 2026-10-29, the Pro (10.01): the probe now reads the one-shot wine-run.txt, where the env= words are (they
    never reach argv), and gains a stage after the USB module preload. The count's idle change per run, three runs each:
    the title ending at its first line 0, 0, 0; after the keyboard and mouse modules load 0, 0, 0; after the Lapy grant
    +18, +18, +18 (and +16, +18 on the next series); after the platform layer +18, +14, +18; a whole session +35, +34
    (drift-pro-2026-10-29-02, -03). The reading rises: each granted title process leaves the count 18 page tables more
    free than real, so a console that has run many sessions reads optimistic, never short. Our own code adds nothing.
  - What the grant does to the title (the resident daemon, owned_root_daemon.c): uid and gids 0, every cap bit, the
    system authid, attrs bit 0x80, a ptrace attach, and its root and jail swapped through two donor children.
  - Variant C (platform/lapy, not shipped; built on the hotfix daemon 00006be4): after the roots move, the daemon puts
    the title's own authid and attrs back. It works: "identity_restored error=0", the title then proves /data read and
    write, three whole sessions passed and the owner's own session ran. It is not the cause: on a fresh Pro boot the
    grant runs still gave +17, +18, +18 and the sessions +34, +34, +36 (drift-C-pro-2026-10-29-01). Left: the caps
    (variant D, caps put back too), then the ids (E): both built, paused at the owner's word ("Yes, pause drift") for
    the page-table margin test and Part 3.

**Part 3, MihawkOS uses less.** Alpha 6 (the owner, 2026-10-28: "release Alpha 5 with: the new warning and stop limits;
the crash protections; the payload tip; the conformance and installer fixes. Then the drift fix and "MihawkOS uses less"
would go in Alpha 6"). To do then: measure MihawkOS idle and with Steam on both consoles, fewer always-on helpers (about
140 page tables each), trim the biggest parts; before and after numbers here.
- Before (Alpha 6, 2026-10-29, the Pro on a fresh boot; Big Picture idle, the 2D game from Steam's library with the
  browser held, quit, Big Picture again; p2-pro-2026-10-29-01 to -03): over each run's rest value, Big Picture idle
  +1,783 to +1,799 page tables, in game +1,160 to +1,250. Per process (each helper's own estimate, peak): the browser
  1,000-1,225, the game 388-523, steam.exe 300-317, steamservice 74-77, steamerrorreporter64 65-68 (it ends by itself),
  services.exe 47, winedevice 46, rpcss 43; a trivial program's floor about 37.
- Where (the census's new "vm page-table windows" line, src/pw_vm_census.c: regions and mapped MiB per 1 GiB window): a
  process costs about one page table per 2 MiB it has mapped, and most of it is already dense. The DLL images
  (libcef, D3DCompiler_47, libglesv2, DXVK, steamclient64) are 162 tables for 298 MiB in the browser and 77 for 139 MiB
  in steam.exe, read whole at load (patch 0944: 16 KiB host pages); the live browser's own heap 233 for 464 MiB; Wine's
  allocations about 180 in the browser and 158 in steam.exe. Wine's direct memory is not the cause: off, Big Picture
  idle took +2,212.
- What can be cut, honestly: perhaps 100-200 at Big Picture idle (the browser's heap, by Chromium's flags, untested).
  Not taken: steamservice's 74-77 (the owner, 2026-10-29: "Drop it"; refused, the client opens a "Steam Service Error"
  dialog, docs/STEAM.md lean-r7, and services.exe, not the client, starts it, so the hold cannot reach it); on-demand
  DLL pages (high risk to every game for about 30 in game: the code that ran is copied at its own address anyway,
  patch 1127, and relocations touch the data); the browser's sparse areas (its own allocator's 2 MiB blocks, which
  Wine does not place); the held relaunch's ~37 (refusing the start makes Steam retry every frame, which is why patch
  2104 holds it); merging or idling Wine's services (unsupported, and rpcss serves every COM call). So Alpha 6 does
  not cut the in-game overhead, and Big Picture idle by about 10% at most; a big cut is not reachable without changing
  what Steam runs.
- A big 3D game (2026-10-29/30, the Pro, Big Picture, the game from Steam's library with the browser held, 5 minutes;
  `klog/p2re4-pro-2026-10-29-04` and `-05`): in game +3,230 to +3,450 over rest, minimum 1,589 free (1,176 in `-04`).
  Its own process is about 2,450 of that: about 1,560 are 2 MiB-aligned direct regions, which still take a page table
  each in a game process (the census counted them as free until 6dbeeda6; since then it lands within about 100 of the
  pool at Big Picture idle and in game). In the title the same mapping costs the system pool nothing (a 64 MiB probe,
  aligned or not: 0), so sizing the title's graphics memory to 2 MiB saves nothing. The remaining lever is putting a
  game's direct memory on 2 MiB pages (promotion): the owner, 2026-10-30, "Keep it off". So this game stays a Pro
  benchmark for now.
- The base, projected (not run): the 3D demo's increment is the same on both consoles (Pro `p2re3-pro-2026-10-29-03`
  +2,155 in game, peak +2,412; base, Alpha 5's proof, +2,232 and +2,467), so the big game's +3,437 to +3,691 peak on
  the base's 2,738 at rest gives about 1,760 to 2,020 free of 8,192, above the 768 close mark. Not shown: that rest was
  read on a drifted counter (each title session reads about 35 low), the game's settings and scenes beyond its first
  minutes, and the base's smaller memory changing what the game maps.

**Fresh-boot page tables are the baseline** (2026-10-29). Every gate of ours before that night ran on counters the drift
had lowered (about 35 used per title session, so a gate takes about 1,700 off): the Pro's ~2,040 used before MihawkOS
and its gates 02/03 were read that way. On a freshly restarted Pro a title session starts at about 5,900 used of 9,216
(gpu2m-pro-gate-2026-10-29-01: 3,276 free at the first case, rising about 35 a case). That is what a player has, and
every page-table target from here on is measured from a fresh boot. In that gate the grouped session's 4 GiB
payload-memory probe took the pool to 1,526 free and the guard's fixed-fall rule (300 within 2 s) ended the group: so
payload-memory runs in a launch of its own again, and the fall rule became a projection (ended once the last 2 s of fall,
repeated, would reach 512 free; the 3D demo's start is still ended at 807 free).

**Steam's browser while a game runs** (fresh Pro, 2026-10-29, build 896448177e155ec6; `klog/wh-pro-2026-10-29-*`). System
page tables of 9,216: rest 5,810; Steam idle with its browser about 7,486; with `steamwebhelper` ended every ~5 s for
90 s 6,525-6,580; back to about 7,500 10 s after the kills stopped; peak while Steam and the game start 8,148-8,282 (934
free, the guard did not act). So the browser is about 950 of Steam's ~1,676 (57%). Steam relaunched it after every kill
(5 launches over 20 kills, 10-41 s apart; never "webhelper crashing too often, stopping restart"), steam.exe and
steamservice stayed up, and the browser was back after the kills and after the game. This client (1788652215) has no
`-no-browser` switch at all (taken out of tools/steam-install-app.py; the console's 250900 entry still passes it,
harmlessly). The 2D game crashed at its intro in both runs (libgallium_wgl.dll writing to NULL, a 1921-wide window; the last
good run drew 960x540), so no game ran during the kills: the game's own survival, achievements and cloud sync are
still to be shown.

Built and measured on the Pro (patch 2104 and the client entry's rule `steamwebhelper.exe|hold|\steamapps\common\`; results below).
What Steam does, read from its client library (steamclient64.dll, CChromeIPCClient::RunFrame, client 1788652215):
- A browser that dies is relaunched at least 10 s after its last start. A 5th death within 10 s of the first logs "webhelper
  crashing too often, stopping restart" and adds 30 s to the window; the count then starts again, so Steam never gives up
  for good.
- A start that fails (an invalid process handle) is asserted ("m_hCEFHandle != INVALID_PROCESS_HANDLE") and retried from
  the next frame, outside that count. Refusing the start (a `fail` rule) would make it retry every frame.
- A browser that starts but never connects is ended after 60 s ("Failed connecting to webhelper process, started ... ago")
  and relaunched.
So the browser is not refused but held. In the process holding the rule (steam.exe):
1. When it starts a program whose path holds `\steamapps\common\` (a library game, no list), it ends the browser running
   then, once.
2. While that game runs, every relaunch starts suspended: a live, idle process (a nearly empty helper's floor), which
   Steam relaunches about once a minute, each one held again.
3. When the game ends, the held browser is resumed.
Each step is a fixme line. `WINE_PS5_PROCESS_HOLD=0` turns it off. To measure on the Pro once a game runs there:
the system page tables with the browser held, the game's licence, achievements and cloud sync, and Big Picture back
after the game.
Measured on the Pro (2026-10-29, build 6e09348ce15e549e, Big Picture, a 2D game started from Steam's library, not a fresh
boot; `klog/hold-pro-2026-10-29-03*`): Big Picture idle about 5,500 page tables used; the game's start ended the
browser (4,711); with the browser held the game ran at 4,911-4,921, against about 6,100 in the same run without the hold
(`-02`): about 1,190 saved. All four lines in order (game, ending, held relaunch 0.5 s later, resumed at the game's end).
The game presented 61 frames a second in front the whole time (Big Picture's surround black behind it, as nothing draws
it); Steam stayed logged on; the client wrote the game's cloud cache at its end; Big Picture came back on screen (the
resumed browser in front at about 106 frames a second). Not seen: an achievement (none earned in the menu).
A game.json an older build wrote keeps its own rule list, which wins over the launch's, so the title merges the built-in
rules a stored list lacks when it reads the catalog (`pw_steam_rules_merge`): the console's hand-written 250900 entry,
with no hold rule, held the browser the same way (`klog/merge-pro-2026-10-29-01*`, build 7577e424431259ad).

**Alpha 5's proof: a 3D game's demo with Steam open on the base PS5** (2026-10-29, build 7577e424431259ad = bee7ea8c, base
13.60, restarted 08:19 and drifted by the payload checklist's runs since; the base hold run of 2026-10-29 in `klog/` of
PS5_Proton-int1028). Big Picture, the demo started from Steam's library, 5 minutes, a normal close, Big Picture
again. System page tables of 8,192: 2,738 at rest; Big Picture idle about 4,600; the demo with the browser held
about 4,970, peak 5,205 at the game's start (2,987 free, the minimum: the close mark is 768); Big Picture back after
the game about 4,500-4,670; 2,704 after the session. All four hold lines (the game, the browser ended, its relaunch
held, resumed at the end). The guard never acted (no close, end or page-table line); the census's resident peak
1,126 MiB (the base's WARN is 1,750). The game ran without a fault; the owner: it ran great on the base with no crashes
(some hitches here and there). The day before, the same demo with Steam open took the base's pool from 7,016 to 8,188
used and out (the base run with Steam of 2026-10-28). The PS5 Pro, same test (the Pro hold run of 2026-10-29, `-01`): 4,909 free at
least, census peak 474 MiB, no guard action.

**Part 4, player help** (Alpha 5; host-tested). At the session's first census the title says what the user's payloads
hold ("Wine: payloads use N MiB in M processes at the start"), and from 64 MiB on shows "Payloads are using N MiB;
closing them frees room." A payload is a process started by the loader the helpers come from or adopted by init, not
named "Sce..." (`pw_memory_is_payload`). At WARN the notice "The PS5's memory is getting full: games may slow down as it
starts to swap." shows once a session.

**Payload checklist** (2026-10-29, the owner: "no other additional payload should be accepted due to RAM usage").
Every start takes `src/pw_payload_check.h`: Kstuff Lite (its process; a title cannot make the kekcall that would tell
1.11, it ended the title), ShadowMountPlus 1.7beta10+ (its
`/api/v1/version`), elfldr, websrv, NanoDNS required; klog, ftpsrv, pldmgr allowed; anything else refuses entry. The
launcher shows the list with CROSS to check again; a game start that fails returns to it. Measured on the base (13.60)
with the official set: 88 MiB in 8 payloads (`evidence/payloads-base-2026-10-29-01.json`). ShadowMountPlus's
launch-after-exit fix (the owner's PR 139) is upstream since 1.7beta6, so MihawkOS carries none (`docs/LAUNCH_AFTER_EXIT.md`).

**Testing ladder.** PC: host tests (marks for every model and firmware, spoofed firmware, unknown model, broken
page-table readings). Base 13.60: full gate on the exact build; the owner's 3D demo with Steam past WARN to set STOP;
a fill test with MihawkOS running; 5 open/close cycles for drift. Pro 10.01: the same gate; the 2D game with Steam and a fill
test (warn ~850, clean stop ~950); 5 cycles. No volunteer step: every proof comes from the owner's two consoles
(the owner, 2026-10-28: "I will not send testers any more test").

**Approval:** both consoles' gates pass (104, the two dnsapi named skips); no freeze or panic in any run; the warning
within 16 MiB of its number and every stop in the launcher; after a close, memory back within 16 MiB of the start and
page tables within 10; MihawkOS's own use measurably lower, numbers here; the owner's own check. Then one local commit.

## Installers: where a helper start goes, and making it cheaper (2026-10-27; host-tested, not yet on the console)

**Measured from existing records** (the Pro's `prefix-fresh64` gate case, where Wine Mono's msi runs three exe custom
actions in a row; kernel `EXEC` lines lined up with the title's monotonic stamps): a sequential child process costs
1.3-2.1 s (typically 1.7) against well under 0.1 s on a PC. From one action's end to the next carrier's `EXEC`
0.75-1.1 s, unstamped (msiexec noticing the end, CreateProcess, the title's spawn, the upload to websrv/elfldr,
elfldr's spawn); from `EXEC` to the first Windows module 150-250 ms (user-space loader; 230-280 ms kernel loader); then
10-50 ms for each builtin DLL, 0.3-0.6 s for a small tool. msi's tests start ~100 helpers, which is most of their
171-227 s against the PC's 4-6 s. The starts are what Windows does too (a custom-action server per architecture per
install); each start is the cost. Analysis and plan: the coordinator's notes, `installer-speed.md`.

**P0, stamps (staged).** `wine-ps5: spawn native=` (title, every step after the request), `wine-child: start at=`
(helper, main to wine-main) and `wine-child: life summary` (server, file and section time of a helper's whole life,
which the 10 s census never reaches for a short helper); `tools/start_stages.py` (host test `test_start_stages`) lines
them up with Wine's module lines and `tools/run-console.py` records `start_stages`. The console run that splits the
0.9 s is queued (docs/CONSOLE_QUEUE.md).

**P2, the rendezvous without 100 ms sleeps (staged after P0).** The title connects to a new helper's socket as soon
as it listens: the pause between refused connects starts at 1 ms and stops at 8 (`wine/ps5/pw_payload_spawn.h`), not
a fixed 100 ms; same 20 s limit and protocol. Reversing the rendezvous (the title listens, the helper connects) was
not done: a title cannot bind a unix socket under /data (errno 45, 2026-10-08) and nothing shows it can in
/system_tmp. Expected: 0-100 ms off each cold start, about 50 on average.

**P1, a warm helper and a narrower startup lock (staged after P2).** After each start the title makes the next
helper in the background (`warm_keeper`, `src/pw_warm_helper.h`, host test `test_pw_warm_helper`): carrier spawned,
rendezvous done, its loader waiting for a bootstrap (now up to 60 s, `PW_WARM_BOOTSTRAP_MS`). The next start takes it
and sends the bootstrap at once; one parked at most, closed 30 s after the last start, none once the title ends its
game, `WINE_PS5_WARM_HELPER=0` turns it off. A parked helper that hung up is closed and the start goes cold.
Everything a process decides still happens after the bootstrap. `startup_lock` now covers only the GPU broker's open:
it held a whole start for the elevation service's one request path, and helpers no longer elevate, so starts overlap;
the upload to websrv/elfldr alone stays one at a time (`payload_upload_lock`), as elfldr has only ever seen it.
The spawn line says `warm=1`, and `tools/start_stages.py` keeps warm and cold medians apart. Expected: the spawn's
share of the 0.75-1.1 s gap and of the 150-250 ms after EXEC leaves a start's path (P0's run says how much). Needs its
own gate and a Steam end-to-end check before it ships: it changes every helper start.

**Measured with P0 on the base PS5 (13.60), 2026-10-27** (start-stamps-gate-2026-10-27-01, 246 helper starts in the
74 cases that ran before the console froze at a relaunch, kfreeze-base-2026-10-27-gate): medians after the title's
spawn request: elfldr answered 52 ms, carrier main 65, title connected 152 (87 ms of it the 100 ms poll: 2 attempts in
every start, P2's target), READY 200, first Windows module 258, startup modules loaded ~594. Per helper life: 2,117
server calls in 50 ms, file I/O ~4 ms, no section maps: the ~335 ms of module loading is elsewhere.

**P3, timing module loading (staged after P1).** Execute conversions and image reads (`__wine_ps5_load_timing`,
`wine/ps5/pw_wine_dmem_ps5.c`), code-share round trips to the title (`pw_ps5_code_round_timing`,
`pw_wine_process.c`) and module opens found and missed (`pw_wine_dl_timing`; every builtin DLL without a Unix side
misses once: two failed loads and five log lines under the loader's lock) are counted in calls and ns, lock-free, and
printed in the helper's `life summary`; `tools/start_stages.py` keeps their run medians (`median.life`).
The part before the title's spawn request (about 1 s of the earlier 1.7 s per installer action) is stamped too:
the requesting helper logs `wine-ps5: spawn asked ms= replied_ms= child=`, the title `spawn served requester= native=
received_ms= replied_ms=` and `child exit received native= ms=`, and the life summary carries the program's own
`exit_ms` (its EXIT doom stamp). The exit chain this measures: the program sends EXIT and parks; the title closes the
helper; the helper sees the hangup (its watch polls every 100 ms), prints its tallies, fsyncs and exits; only then can
the server end the process and wake the parent. `start_stages` links each start to the same parent's previous child
(`chain`, `median_chain`: from that child's exit, the title's receipt of it, and its end, to the parent's next ask).
Patch 2153 stamps font setup and the wallpaper decode per process (`fonts_*_us`, `wallpaper_us`).

**Measured and fixed (base 13.60, 2026-10-29, `evidence/installer-speed-2026-10-29.json`).** On Alpha 5's warm
helpers an installer process costs ~420 ms (DirectX redistributable, 223 warm starts): first DLL at 90 ms after the
request, last at 330, exit at 422. The parent wakes at the child's EXIT, not after its teardown (exit to the next
ask: 239 ms in dxsetup, the parent's own cab work; 11 ms in vcredist). The biggest cost was the missed module opens:
18 per process, 135 ms (vcredist 293 ms), each two loader tries for a `.prx` that is not there. Now `pw_wine_dl.c`
asks the loader only for a file that exists (host test `test_pw_wine_dl`): 1 ms. Second, every user32 process
decoded the 1920x1080 wallpaper (25 ms); patch 2402 decodes it on the first desktop compose (dxsetup: 67 helpers
decoded it, now 1). Before -> after, PC in brackets: dxsetup install 97.9 -> 78.3 s (12.0), reinstall 89.4 -> 73.2 s
(11.8); vcredist install 12.0 -> 9.8 s (1.3), reinstall 9.3 -> 7.2 s, uninstall 9.9 -> 7.6 s; same checks; gate
installer-speed-base-2026-10-29-01 passed 104/104. Next per process: font setup 35-45 ms (every font file opened),
spawn to first DLL 90 ms, DLL loading itself ~120 ms.

## Helpers load Wine's modules themselves on every firmware (2026-10-27; host-tested, not yet on the console)

**What changed.** A payload helper no longer picks its module loader by firmware. It maps the staged
`<name>.prx.elf` files itself (`native/pw_prx_elf_ps5.c`, core `src/pw_prx_elf.c`) on every console;
`WINE_PS5_PRX_LOADER=0` still asks for the kernel's `sceKernelLoadStartModule`, for an A/B. The choice is
`pw_prx_elf_wanted` (host test `test_pw_prx_elf`), and each helper logs it before ntdll:
`wine-child: module loader=user-space (default)` or `=kernel (WINE_PS5_PRX_LOADER=0)`. The memory guard and the other
firmware-keyed decisions are unchanged.

**Why.** The rule was "user-space from 12.00 on, the kernel's below" (alpha-2 to c4a3458). A tester's PS5 Slim on
11.00 (kstuff-fpkg 1.13 dr-test6, ShadowMountPlus 1.7beta5, elfldr 0.27; bundle reviewed 2026-10-27, not in the repo)
never reached the desktop on alpha-3 or the c4a3458 prototype: the title loaded ntdll, the desktop's helper took the
kernel's loader and stopped inside it (last line `loading ntdll`, no klog: klogsrv was not loaded), and the notice said
errno 92 on firmware 11.00. The same stop is klog-proven on 12.40 (pager error at ntdll's text, "Decrypt error in SELF
block") and seen on 13.40 and 13.59. A helper runs in a system carrier process, and whether the kernel decrypts a
fake-signed SELF there is the jailbreak tools' doing, not the firmware's: the title loads the same files everywhere.
A firmware cut cannot follow that; a loader of our own does not depend on it.

**What is proven and what is not.** The user-space loader runs the base PS5's gates (13.40/13.60) since alpha-2, and
it ran the Pro's alpha-2 gate by accident (94/94, 517 helper logs; the reported word 0x12090001 picked it). The address
space it cost there (917 to 31 GiB) is patch 1231's, fixed for both loaders. Open: `mt3-2026-10-24-01` (the Pro,
Steam, `WINE_PS5_PRX_LOADER=1` together with the malloc trace, VMMAP and window surfaces) reached the program at 6.7 s
and crashed at 27.7 s; and on the Pro (limit 870 MiB) module text becomes JIT memory, which is counted as flexible
memory. Neither is measured yet. The console plan is in `docs/CONSOLE_QUEUE.md` (gate, plain Steam, a loader memory
A/B). No console run was made for this change.

## The WebUI: the library, art, tiles, saves, settings and logs in a browser (2026-10-26; host-tested and used on the PC; not yet on the console)

The owner asked for PS5_RetroArch's WebUI in MihawkOS: port 6767, the Frutiger Aero look, the games list, media and
details, game forwarders, the settings, served by the launcher only, the two projects' title ids kept apart, and its
QR code easy to find in the launcher. [`docs/WEBUI.md`](WEBUI.md) is the reference.

Built: the portable routes (`src/pw_webui.cpp`), the libmicrohttpd glue with RetroArch's safety model
(`src/pw_webui_http.cpp`), the page in ten languages (`webui/`), the launcher's QR under the library panel and in
Settings > Web UI, live reload of a setting or entry, covers drawn in the library, the store's art and details, tiles
`PPSA99300`-`PPSA99399` through ShadowMountPlus (MihawkOS stays `PPSA99170`), saves as ZIPs and put back, theme and
music, and a bug-report ZIP with addresses masked.

The owner's review of the first page (2026-10-26): "not user-friendly/guided/descriptive at all", no per-game Game Mode
or controller, and 120 Hz is global, not per game. So: the page rewritten for the average player (guided steps, plain
words, technical fields under "Advanced settings"); a game's own Game Mode and controller (`WINE_PS5_GAME_MODE`,
`WINE_PS5_CONTROLLER`) from the launcher, from the desktop (per process: Wine's new `PW_CHILD_NOTICE_GAME_MODE`,
`pw_window_direct_set_process`, `WINE_PS5_HID` per program) and in the programs a game starts (inherited); and 120 Hz
output as one setting (`src/pw_high_refresh.h`), in the launcher's Settings and the page's. The runtime changes
(`wine/ps5/`), so this needs a runtime build and deploy.

Proven on the PC: `test_pw_webui`, `test_webui_http` (7 of 7 on the real server) and `test_webui_i18n` pass; each page
was used in a browser against `tools/run-webui-host.sh`, the store against the real Steam store.

Next, on the console (the owner's go): the page from another machine, the QR, a reload, the store through the title's
HTTPS, a tile made, started from the home screen and removed, a game with Game Mode off and a DualSense of its own
started from the desktop and from the launcher (the title log's "Game Mode:" and "Controller:" lines, the broker's
direct-flip verdict "off"), and 120 Hz off (the title log's "120 Hz: off"). Then, in PS5_RetroArch (its own repository): its tile range
skips `PPSA99170`.

## Helpers' converted code is held to a budget, and module zero pages stay unbacked (2026-10-25, the base PS5)

Why and what was measured: [FLEXIBLE_MEMORY.md](FLEXIBLE_MEMORY.md).
- A payload executes only JIT memory, which is flexible (on 13.60 the title too). So converted code is the part of a
  helper that cannot move to direct memory.
- The code cache holds it to a budget, on by default since 2026-10-25 (the owner's rule):
  - 64 MiB fixed for Steam and the launchers;
  - for games, 64 MiB raised in 32 MiB steps whenever they thrash;
  - `WINE_PS5_CODE_BUDGET_MIB` overrides it, and 0 turns it off.
  - Steam's web helper: 234-250 -> 173-193 MiB.
  - The 2D game through Steam: 59.9 fps, same frame times as without it.
  - The arms ran at 60 Hz; the owner waived 120 Hz.
- The PRX loader no longer writes zeros over memory a fresh mapping holds as zero: ~8 MiB off every helper.
- Sony local processes draw on the title's partition but cannot execute anything on 13.60. On 10.01 that is unmeasured.

## The owner's launcher session on the standard PS5: a guard loop, a renderer storm, pipe memory out (2026-10-21)

The owner ran the launcher on the standard PS5 (build `f94586d65070fca6`, the "Launcher test desktop" entry), 11:24 to
12:09. Logs kept off the tree: they name the launcher. Reviewed by three analyses, a synthesis and a check against the
logs.
- **0-600 s, a guard loop.** The steep rule ended the browser 38 times (26 main processes, 12 renderers), each at a
  census of 1590-1628 MiB with helpers at 889-927 MiB in 16: every new browser tree's normal start-up climb.
  - The browser was no bigger than in the runs that held the sign-in window. What differed was everything that is
    neither the title nor a helper: 705 MiB (727 at the start), against 682 and 646 MiB in those runs.
  - After each end the launcher started a new tree within 0.5 s, and the guard, which forgets what it ended, ended
    it again.
- **600-1690 s, a renderer storm.** One browser main process survived. Each of the 3,416 renderers it started was
  ended about 0.35 s after it was created, before its program loaded. Unsandboxed children of the same process ran.
  The cause is not proven; the browser's own log (`cef_log.txt` in that prefix) should name it.
- **About 11:53, pipe memory ran out.** A fresh pipe took 0 bytes and a helper start failed with errno 5. Over 54
  more launches, the loader made a process each time and then failed, and nothing reaped them. They were 54 stopped
  processes, still alive after the title closed. The console needed a restart.
- Platform fixes proposed, none made yet:
  - The guard should remember the lineage it ended, so it does not end a relaunched tree's start-up climb again.
  - Processes a failed helper start leaves should be reaped.
  - A parent whose children die before they run should be slowed down.
  - Pipe memory should be checked before each helper start.
  - The census should name the processes that are neither the title nor a helper.

## A launcher to its sign-in window on the standard PS5 (2026-10-20, console; the Pro not yet)

The goal (the owner, 2026-10-20): a launcher end to end, installer, start from the installer, the launcher itself, its
sign-in window, run honestly (no launch options or per-program settings). Records of these runs are kept off the tree:
they name the launcher.
- **Installer and its Finish start the launcher** on both consoles (2026-10-20). The launcher then updates itself and
  restarts.
- **End to end from a new prefix, standard PS5, build `f94586d65070fca6`** (2026-10-21), in one 15-minute run:
  - The installer (Next, Next, Install, Finish with "Run" checked), then the launcher's update and restart.
  - Its sign-in window from about 8 minutes to the end, with a caret in the account field. The installer had also
    made its desktop shortcut.
  - No memory-guard end. The census peaked at 1671 MiB, past the 1670 MiB end mark for under the 3 s the rule needs.
  - The first try's clicks fell on a black screen: a new prefix's desktop drew after 20 s, before the first click.
- **Its sign-in window, on the standard PS5** (13.40, guard 1600 MiB), build `09acfc9faec09198`: drawn from the
  2-minute capture to the end of a 10-minute run (account name, password, and a sign-in QR code that came from its
  servers). No memory-guard end. Helpers 886 MiB in 16, census peak 1631 MiB, no steep climb; SceShellCore's swap-out
  went from 359 to 364 MiB.
- **The gate on that build**, standard PS5: 96 of 96 in 2124.7 s. Its klogs hold no maxpipekva, TTY(STDIN) or
  errno=5 line. Its record is kept off the tree too: its name has the launcher's.

What it took, each measured on the console:
- **The sync device maps its objects in 64 KiB chunks per helper** (`src/pw_ntsync.c`, layout 3). The census counts a
  shared region in full in every helper that maps it (K14), and the whole table was 8.6 MiB per helper. A helper now
  maps the fixed tables, then each 64 KiB chunk of 1024 objects the first time it reaches one. An idle desktop's 9
  helpers: 160 MiB (the whole table: 214 MiB; the device off: 153 MiB). At the launcher's guard ends on the Pro,
  helpers stood at 613-647 MiB in 15-16 (was 831-881).
- **Counting the sync region once in the census was tried and reverted.** It let the census reach about 1000 MiB raw on
  the Pro, where VM Stats read RSS 1081 MiB and swap-out went from 38 to 139 MiB. The raw census is what tracks the
  console's pressure; `src/pw_memory_guard.h` says so.
- **Patch 1241: a process publishes the display only when its mode is new.** Publishing loads the GPU driver (about 30
  MiB). Every process that took the desktop window did it, a menu builder's and an installer's too. Now 1 publish in
  place of one per GUI process.
- **DXVK patch 0004: DXGI lists adapters without Vulkan.** A factory reads what DXGI needs of each adapter from a
  summary file in the runtime's cache namespace (`DXVK_DXGI_ADAPTER_SUMMARY`, set by `src/pw_shader_cache.c`). It
  creates the Vulkan instance only when a device, a swap chain, the interop or the memory budget needs it. In the
  launcher's start, 18 processes had loaded the GPU driver only to list displays. Now only the browser's GPU process
  loads it; the browser's main process and the launcher read the summary.
  - The first build crashed the browser's main process in `wsi::enumMonitors`: only the instance had started DXVK's WSI
    driver. Each factory now holds its own WSI reference, and WSI's reference count is taken under a lock.
- **Patch 1127's code cluster is 16 KiB** (was 64). A payload runs code only from JIT memory, so each helper copies the
  code it runs in clusters charged to its own flexible memory.
  - At 64 KiB, each start of the browser's renderer took the census up 120 MiB or more in 8 s, to 1591-1629 MiB. The
    steep rule ended the browser 45 times in 10 minutes (helpers 949-985 MiB in 16, the browser's main process 153-158
    MiB).
  - At 16 KiB: 0 ends. The browser's five processes copy 15-57 MB of code each, 181 MB in all (at 64 KiB: 28-71 MB,
    257 MB).
  - 16 KiB had stalled `winedevice.exe`'s exit once (`gate-2026-10-16-browser-01`), the race patch 1171 closed.
- **The memory guard's end line names the rule that fired.** The steep climb ends below the end mark, and the line
  had said "stood past 940 MiB" at 872-900 MiB.

**The Pro** (10.01, guard 870/940 MiB): the owner ran this build there on 2026-10-21 (logs kept off the tree). The
console's own processes held 272 MiB before Wine started. The guard's steep rule ended the browser 7 times in 2 minutes
with helpers at 616-629 MiB in 15-16 (census 867-880 MiB).
- Per process the Pro's figures are lower than the standard PS5's: Wine's base helpers 14-19 MiB (21-29 there), the
  launcher 108 MiB (119), its service 24 (45), the browser's main process 120-122 (139).
- Scaled the same way, the launcher's whole start needs about 710 MiB of helpers. The Pro leaves about 600 MiB under
  its 870 MiB limit, so it is about 110-130 MiB short. An earlier line here said 500 MiB: it compared the standard
  PS5's whole census, which includes about 640 MiB of that console's own processes, with the Pro's ceiling.
- The browser's five processes copy 181 MB of code between them, mostly the same `libcef.dll` pages. Shared copies
  save physical memory on the standard PS5 (K19 below); whether the Pro's kernel acts on physical pages or on its
  printed RSS limit is not measured.

## The combined alpha-3 gates (2026-10-20, console: both, one build)

One tree with every alpha-3 change (the user Mihawk, Settings > Logging, the taskbar fix, the firmware-keyed decisions,
the address-space and free-area-search fixes, the contributor docs), built in a clean room, build fingerprint
`4fd9c7f4f502f44a`, 146 host tests, 96 gate cases (new: `detailed-logs`, `mihawk-browser`):
- `alpha3-base-01` (standard PS5, 13.40): 96/96, 2167.7 s. Its klogs hold 352 "kern.ipc.maxpipekva exceeded" lines in 43
  launches (base-gate-05 on the same console: 337); no TTY(STDIN) or errno=5 line. The console needs a full restart before any
  further use.
- `alpha3-pro-01` (PS5 Pro, 10.01): 96/96, 1862.6 s; 0 such lines.
- On both: `search_errors` 0 in every case; `late-library` reserved 917 GiB (floor 900); `mihawk-browser` passed.
- Each console's own mihawk prefix moved to the user Mihawk during its gate; their registries were backed up off the
  tree first.

## The Windows user is Mihawk; older prefixes move once (2026-10-20, console: base PS5, a new and a moved prefix pass)

The owner wants the Windows user to be Mihawk, not Proton's `steamuser`, for v3.0.0-alpha-3. Every profile path Wine
makes follows GetUserNameW (shell32's `%USERPROFILE%`, so Documents, AppData and Saved Games; wineboot's ProfileList
ProfileImagePath and Volatile Environment; LookupAccountSid), which Proton fixed at `steamuser`. Patch 1240 makes it
Mihawk, and the title's Unix `USER` (Wine's `WINEUSERNAME`) agrees. The title's user folders, `tools/deploy-wine.py`
and the staged probe prefix (its template is a host wineboot's) use the same name.
- A prefix of alpha-1 or alpha-2 holds the user's saves in `C:\users\steamuser`. Before Wine starts on a prefix, the
  title moves it once (`src/pw_wine_profile.h`): the folder (one rename, or name by name into an existing Mihawk; a
  file both hold stays in steamuser), a link `C:\users\steamuser` to Mihawk for stored absolute paths (a line of
  `users/.pw-symlinks`, since a title cannot make links), every registry reference (paths after `users\`, the saved
  `USERNAME`), each file synced and renamed over the old, then the marker `.pw-profile-user`. Each step is safe to
  repeat, so an interrupted move finishes on the next start; an error is logged and Wine starts anyway.
- Host proof: `tests/test_pw_wine_profile.c` moves a prefix laid out as an alpha-2 one and finishes each interrupted
  state (after the rename, mid-merge, mid-registry with a half-written copy, before the marker, a torn marker). A copy
  of a console prefix's registry (`PW_PROFILE_REAL_REGISTRY`, 4,153,452 bytes, 2026-10-12) had 36 references, all
  moved, every other byte kept; the probe template had 61, all moved, and the staging's Python rules give the same
  bytes as the C ones on both.
- On the console (base PS5, 2026-10-20; the owner's mihawk prefix untouched, its registry backed up off the tree; no
  desktop session, no gate):
  - `profile-new-base-01` (`tools/run-prefix-test.py`) passed 16/16: a prefix the console's wineboot made has the user
    Mihawk, USERPROFILE `C:\users\Mihawk` and no steamuser folder, and an MSI installed and ran; the title wrote only
    the marker.
  - `profile-move-base-01` failed 1 of its 10 checks. The probe prefix had been put back in alpha-2's layout
    (`users/steamuser`, the registry as alpha-2's staging wrote it, a save planted in Documents). The title moved it
    (1 rename, the link, 3 registry files, 61 references), and the user, USERPROFILE, the save in the new Documents
    and a `C:\users` listing without steamuser all passed. But the old path `C:\users\steamuser\Documents\...` was not found:
    Wine's Unix side read link tables only along the path as named (`dosdevices/c:/users`, which the kernel does not
    have), never along the followed one (`drive_c/users`). It now reads both (`wine/ps5/pw_wine_cwd_libc.c`, reload;
    `tests/test_pw_wine_cwd_links.c` fails without it).
  - `profile-move-base-02`, the fix deployed and the prefix reset to the old layout (63 references), passed 10/10:
    the save is found through the profile and through the old steamuser path.
- Not shown on the console: Explorer and Start drawing the name (they read the same GetUserNameW the probe checks;
  that needs a desktop session, left to the combined alpha-3 gate). Limits: the link matches the exact old name (a
  stored `C:\Users\SteamUser` is not followed), and registry hex values keep the old path (the link covers them).

## Settings > Logging: one folder of a session's logs for a player to post (2026-10-20; host-tested, ran on the standard PS5)

The owner wants non-technical players to send logs without a PC running a klog client. With Settings > Logging on
(default off, `logging.txt`), each session writes `/data/ps5proton/logs/detailed/<UTC start>/`
([src/pw_detailed_logs.h](../src/pw_detailed_logs.h)):
- **At the session's start:** README.txt (what to zip and post, mask the IP, program folder names show), the process's
  title-early.log, and klog.txt.
- **At the next title start, after the grant:** the session's other logs (title-last, wine-last, lapy-service, wine-stdout,
  DXVK's, wine-child-*, flight-recorder, setup.txt). Copying then also covers a session that crashed or froze.
- **The kernel log** comes only from klogsrv on 127.0.0.1:3232. klogsrv serves several clients at once and opens
  /dev/klog only while one is connected, so the PC's client keeps its stream. Without klogsrv the connect is refused
  and the folder has no klog.txt; README.txt says so.
- **Detail:** while on, WINEDEBUG adds `+process,warn+all` (warn-seh stays last) and the sync device keeps its last
  8192 operations for a hang's dump.
- **Privacy:** every copy masks dotted IPv4 addresses. On random text, chunked masking is fuzzed against whole-text
  masking and never masks less.
- **Bounds:** 5 folders, 96 MiB each, 480 MiB in all.
- **Console check:** `tools/run-detailed-logs.py` runs one session with Logging off and then on, and checks the
  folder. It is in `GATE_CASES` as `detailed-logs` (the coordinator, 2026-10-20), which runs the on leg only.

**On the standard PS5 (13.40), 2026-10-20, build cc9508d2, the Mihawk desktop for 30 s:**
- **Run `loud-failures-logging-2026-10-20-02`:** both sessions passed (closed on request, back to the launcher).
  - The folder `2026-10-20_16-16-56` held 19 files, 436 KB in all: README.txt, title-early.log, title-last.log,
    wine-last.log, flight-recorder.log, 11 wine-child logs, setup.txt, wine-stdout.txt and klog.txt (51 KB).
  - The launcher said "16 files copied, 0 left out (status 0)".
  - lapy-service.log was not on this console's /data/ps5proton/logs, so it was not copied.
  - The PC's own klog of the run kept going while the title read klogsrv.
- **The runner's verdict was FAILED**, from its own scan: the numeric example address in README.txt.
  - README.txt now shows only the placeholder 192.168.x.y, and a host test checks that it holds no address. The
    scan still covers every file, since a player's folder must pass it too.
  - On the host, a re-scan of the downloaded folder found no other address, and the console's own address in no file.
- **The cost, off against on:**
  - The helpers wrote 228,749 and 355,094 bytes (+55%).
  - The additions were trace:process 40 KB, warn:font 40 KB, warn:wgl 28 KB and warn:file 7 KB. The wgl and font
    lines are one-time start-up lines in each process, not per frame.
  - The sessions took 55.9 and 56.3 s. The desktop session has no pacing record.
  - A game at 120 Hz is not measured yet: it is planned for the combined alpha-3 cycle.
- **Run `-01`:**
  - Its off leg was the first session on the newly deployed runtime. Its prefix was updated, and its desktop did not
    close on request: the title ended 6 helpers after the stop, so the run failed.
  - Its on leg passed.
  - The symptom may be real: the first session on a freshly deployed runtime did not end on request. The combined
    gate's desktop-stop case will show whether it repeats.
  - The runner then counted FTP's `.` and `..` as new folders (fixed), so its folder `2026-10-20_16-14-25` was not
    checked.
- **Not run:** a session without klogsrv.

## Done: no File Explorer button on the taskbar of an idle desktop (2026-10-20, console; patch 1215)

The owner saw a File Explorer "tab" open in every Mihawk OS session: the taskbar's File Explorer pin carried its running
pill with no File Explorer window (pro-gate-01's desktop-session captures 01 and 02, only the probe's window on screen).
A window lights a pin when the taskbar counts it and its process image is the pin's, and the shell itself is
explorer.exe. The window was the desktop's own folder view (SHELLDLL_DefView, a visible child of Progman): Progman hands
the messages it does not handle to the desktop window's procedure, whose WM_PARENTNOTIFY case adopted the new child as a
program's window, and the taskbar never asked whether a window is top-level. Patch 1215 counts only top-level windows, as
Windows does, and logs one line (warn+explorer) the first time it counts each window: what it lights or why it has no
button, with its class, process, styles, parent and owner.

Proof, `taskbar-pill-01` (standard PS5, 60 Hz, tools/run-frame-pacing.py on 67d0b55 + 1215): no pill on the start-up and
idle captures; the probe's window has its own button; File Explorer opened lights its pin (capture 11). The log names the
folder view (0x1007A, the shell's pid 0020) as having no button for not being top-level. The run passed: 571 steady
compositions, 2,742 us mean, 8,723 us max; File Explorer's opening 14,087 us max (the Pro at 120 Hz on 2026-10-18:
14,654). Game Mode was not in this run (the combined gate's cases cover it). Not changed: Progman still forwards its
children's WM_PARENTNOTIFY, so the folder view stays in the taskbar's list, uncounted.

## MihawkOS without a PC: installs itself, says what failed, adds games easily (2026-10-18; phases 1-3 ran, 5-7 built)

The owner's goal: a user with a jailbroken PS5 and no PC installs MihawkOS over the console's internet connection,
every failure is said on screen, and adding a game is copying a folder. The design, the owner's decisions and the
phases are in [SELF_INSTALL.md](SELF_INSTALL.md). In short:
- Decisions: downloads come from GitHub Releases, which the owner publishes. Installing is copying the MihawkOS folder to
  `/data/homebrew` and opening the tile; the title sets itself up and updates itself (a setup program was dropped).
  MihawkOS never ships ShadowMountPlus (the owner, 2026-10-18): users run their own upstream copy, and MihawkOS waits for an upstream launch-after-exit fix. Every build ships the Aero Meadow wallpaper.
- Phases 1 to 4 need no hosting: failure reasons in the launcher; drop a folder into C:\Games to get an icon; system
  notifications during a session; release hygiene, which includes the never-ship rule for the Bloom-derived
  `proton.bmp` that every runtime built on this PC carries today.
- Phases 5 to 9 are the PC-free install: the title installs its runtime from storage, then over HTTPS; it sets itself
  up on its first start; it updates itself from a signed manifest; then the first public release.
- Phase 0, a probe round with the owner at the console, answers the unknowns before phases 5 to 7 are coded.
  **Ran 2026-10-18** ([SELF_INSTALL.md](SELF_INSTALL.md), "Phase 0's answers"). Nearly every answer favours the plan:
  - the elevated title writes `/data` at 143 MiB/s with no throttling;
  - websrv and pldmgr start a 31.6 MB ELF;
  - the title reads USB at 114 MiB/s, and helpers see it;
  - the title can create in `/data` before root;
  - ShadowMountPlus rescans on request.
  The exception: **HTTPS from the title refuses every certificate chain** (`0x8095f00c`), while plain HTTP works, so
  phase 6 needs its cause or its own TLS. `statvfs` and `sceNotificationSend` are null imports in the title.
- **Phases 1 and 2 are built, and their cases passed on the console (2026-10-18).** At the owner's instruction they
  were committed with no gate, after host tests, a production build and a privacy check. Their cases, `launcher-ended`
  and `games-autodetect`, are in `GATE_CASES`. They ran as ordinary runs, with phase 3's, in
  `loud-failures-2026-10-18-03`, which passed 3/3 on 6381a31 (clean revision, verified deploys):
  - launcher-ended: the failed session's reason reached the launcher, a planted crash marker was reported and removed,
    and the runtime stamp matched.
  - games-autodetect: the folder's shortcut points to its program and starts in its folder; a folder with two
    programs got none, and its notification posted 0.
  - Runs -01 and -02 failed on harness bugs only: the console's FTP refuses NLST, answers DELE with 226, and
    exit-probe never ended its desktop session. Fixed in 6381a31.
  - Phase 1: the launcher says why a session ended (`ended=`), that one did not end normally (`session-running`), why
    `/data` is unreachable, and that the runtime is missing or is not this build's (`runtime-stamp`).
  - Phase 2: a folder in `C:\Games` with one x86 or x64 program at its top gets a desktop shortcut, with no
    `game.json` (`src/pw_game_folders.h`).
- **Phase 3 is built too, committed the same way, and its case passed on the console (2026-10-18).** In
  `notice-fixed-base32` the fixed-base program was refused before Wine and never ran, its notification posted 0, and
  the launcher gave the reason and repeated the notice.
  - What it says: PS5 system notifications (`sceKernelSendNotificationRequest`, seen over the title in probe 5) for a
    memory-guard close or end, a helper that cannot start ("Restart the PS5" for errno 5 or low pipe memory), a helper
    that gave the GPU broker up, a 32-bit program fixed at 0x400000, and a `C:\Games` folder with several programs.
  - The launcher says each again after the session: "During the last session: ...".
  - Over a full-screen game in Game Mode (`loud-failures-gm-notice-2026-10-18-01`,
    `tools/run-game-mode.py --env PROTON_TEST_NOTICE=game-mode`, passed in 26 s): the notice posted 0 on the line
    after "gpu-direct enter". The game then held the screen for 644 more direct flips. The owner, watching, saw it: "I
    saw the test notice notification!" So probe 5 is closed for Game Mode too, and the fallback, a banner the GPU
    broker draws (graphics 0030, reviewed by perf-120fps), stays shelved for good.
- **Phase 5, install from storage: committed 2026-10-18 without a gate** (the owner: "Gates are skipped so we can ship
  faster"). It is host-tested, and the title builds and links. Not yet run on the console. It was waiting on two
  things: phase 1's `runtime-stamp` on main (until then `pack-runtime.py` skipped the pack), which is there now, and
  the owner-approved rehearsal (`tools/run-install-rehearsal.py`), which is still to run. As built: SELF_INSTALL.md,
  "Phase 5 as built".
- **Phase 6, the same install over HTTPS: committed 2026-10-18 without a gate.** It uses the title's own TLS
  (Mbed TLS 3.6.7 against the console's CA_LIST.cer), which is proven in `probe-tls-2026-10-18-01`. Not yet run: a
  full LAN-fixture install. No release address is built in until the owner publishes.
- **Phase 7, the title sets itself up: committed 2026-10-18 without a gate.** Before its first root request it writes
  `dev.conf` by S6's rule and makes `/data/ps5proton` (`src/pw_first_run.h`). It is host-tested, and the title builds
  and links. Not yet run on the console. The setup program it replaces (the owner, 2026-10-18) is archived on branch
  `claude/self-installer-setup-archive`.
- **Phase 8, MihawkOS updates itself: committed 2026-10-18 without a gate.** It has a signed release manifest
  (Ed25519 through Monocypher 4.0.2, signed by `tools/sign-release.py`), the title staged whole, and a switch at the
  next start that resumes when cut. The previous title is kept until the new one draws. Host-tested, and the title
  builds and links. Not yet run on the console. It waits on the owner's key (`keygen`) and an owner-present LAN
  rehearsal (SELF_INSTALL.md, "As built").
- **A fresh clone builds (2026-10-19, the first-install rehearsal's clean room).** Fresh clones of all five
  repositories, built by the README's steps, hit four gaps. Three are fixed:
  - 995cbb6: the title's licences are now staged after Mesa's WGL build;
  - 995cbb6: stage-wine.py strips only PE images, not a fresh wineboot's 16-bit placeholders;
  - 5c5728e: the production build runs extract-dxvk.py, which nothing ran before.

  **Open, for the owner:** the Lapy daemon's build depends on the host compiler. Its pin needs
  clang/LLD 23.1.1; this PC now has 23.1.3, which builds `aa30237b...` instead of the pinned `c9710b7a...`, and
  build-production.sh refuses it. Pinning the compiler (an LLVM 23.1.1 release by SHA-256) would close it; until
  then a fresh clone needs the pinned ELF from a machine that built it.

- **The unattended first install (2026-10-19, `tools/run-setup-unattended.py`, scripted presses, the real engine).**
  - `-01`: the USB stick dropped off the bus at 48 of 1,552 MB (`umass0 USB_ERR_TIMEOUT`; all 48 storage lines on the
    stick, none on the SSD). The install failed cleanly as `read_failed`.
  - `-02`: the install resumed over the partial one: written 1,529,459,285 B, 2,527 files, 142 of 142 chunks, in
    44.1 s. The read-back hashed 2,535 of 2,535 files equal to the package, with runtime-pack and stamp equal.
  - `-03`: the tour went from Welcome to Preparing → Start, and the first session made the prefix about 1 minute in.
    It never ended itself, though. wineboot writes `.update-timestamp` through msvcrt in text mode
    (`1734871457 wow64\r\n`), and the title stripped only LF. 0 stop lines. Fix: `pw_setup_prefix_made()` also
    strips CR (`test_pw_setup`). `-04` reruns the first session with no prefix to prove it.
  - Not proven yet: Done → Enter Mihawk OS, and the new-console path after a PS5 restart.

## Parked: native stderr lines spliced by Wine's records in helper logs (2026-10-19, console; after the hotfix)

A gate verdict behind spliced native text is read since 84aefbf (`wine_records` in `tools/run-battery.py`;
gate-2026-10-19-splice-01 94/94). The splices themselves remain: 848 spliced lines in 558 helper logs, against 945
in 564 for gate-release-2026-10-19-01. A native `fprintf(stderr, ...)` writes each piece apart, and Wine's whole
records (`wine_write_stderr`) land between them.
- Fixed: `pw_wine_dl`'s report now goes out in one write (70 spliced lines to 0).
- Not fixed: the line-buffered stderr in `native/proton_wine_child.c` left the counts unchanged. Its comment's
  "564 spliced lines" is the number of logs; the lines were 945. Find out why (another FILE or libc, or
  `setvbuf` ignored). Then revert the hunk or make it work, and correct the comment.
- To do, in a gated batch: one write per line, as `pw_wine_dl_libc.c` `report()` does, for
  `wine/ps5/pw_wine_threads.c` ("kill thread", 334 lines), the "thread … killed" writer (193), `wine/ps5/pw_wine_process.c`
  ("child signal") and the GPU client's "gpu-client present link=" (platform/graphics patch 0026).
  Done when the helper logs show 0 spliced lines from these writers.

## A fixed-base 32-bit installer at 0x400000: its helper gives the carrier's image back (2026-10-19, console)

Old 32-bit installers (many NSIS setup programs; the browser's C4) can load only at `0x400000`, held in every
helper by Sony's carrier executable (`NPXS40112/eboot.bin`, `0x400000`-`0x584000`); MihawkOS refused them. The owner
held v1.0.0-alpha-1 for this. What was measured, in order ([IA32_DESIGN.md](IA32_DESIGN.md) has every run):
- A helper can unmap the carrier's four segments and keep working (`carrier-image-2026-10-19-03`, `-04`); the stale
  libkernel record of `eboot.bin` cannot be unloaded (`carrier-xo-2026-10-19-01`).
- Releasing it in every helper made 32-bit programs at `0x400000` stop (first build) or lose their audio and
  controller drivers (second build, `wow64` failing where the release build passes: the control
  `free-0x400000-control-2026-10-19-01`).
- **The fix:** only the helper of a 32-bit image with stripped relocations releases the carrier (the title decides from
  the PE header and flags the spawn, `PW_CHILD_FIXED_BASE`); executable memory in the released range becomes JIT
  memory at the protection change. On build `93357310`: `wow64` (audio, XInput), `pe32`, `pe64`, `virtual-query32` and
  `fixed-base32` pass (`free-0x400000-narrow-2026-10-19-01`), and a real game launcher's NSIS installer
  installed with `/S`, though that run's verdict is FAIL: one eager execute conversion in the released range failed
  and fell back (`convert_failed=1`; the records are kept outside the public tree, the program is not named). The refusal and its notice are gone; gate case `fixed-base32` replaces
  `notice-fixed-base32`.
- Not yet: the full gate battery on this build (the release's), audio and controller inside a program at `0x400000`,
  an installer driven through its windows, and the cause of the installer's failed eager conversion.

## Done: a desktop session the title ends from outside closes on request (2026-10-18, console)

Seen in run browser-desktop-02 (2026-10-18) (build `5c451695`): `tools/run-console.py --path C:\windows\system32\mihawk.exe
--args <program>` with the desktop's environment (`PROTON_LAYOUT=launcher`, `WINE_PS5_WINDOW_SURFACES=1`,
`MIHAWK_DESKTOP=1`) and a 100 s budget. The program it started, MihawkBrowser, ended by itself at 32.6 s. At the budget
the title asked the session to close and logged "the game did not close in 10 s (inputs delivered 0 of 0 posted)",
then ended its seven helpers (Explorer, services, plugplay, svchost, two winedevice, rpcss) by SIGKILL; they were gone
in 2,349 ms, and the run had no clean owner restart. The hang snapshot: Explorer's main thread (0024), its queue
empty, waiting since the close request (10,121 ms) in a sync-device wait-any of one manual-reset event with no name
(handle 0x11c), with no timeout. run browser-desktop-01 (2026-10-18) ended the same way, Explorer on the same handle, but
waiting for 42.9 s, so from before the close request; the browser itself was deadlocked in that run (patch 1173).

The gate's desktop cases (`desktop-session`, `desktop-view`, `game-mode64`, ...) end through a probe's marker and never
take this path, so no gate shows it. How the owner's own close from the PS button ends a session was not measured.
Where to look: what the title's stop path sends a desktop session ("0 of 0 posted" says no close input reached a
helper), Explorer's Mihawk desktop shutdown, and what Explorer's handle 0x11c is. Done when the same run ends with the
session closing on request: no SIGKILL, a clean owner restart, and a check that keeps it so.

**Cause (from the PC, on the runs' own records).** The close did reach Explorer. The title's stop sends input type 4,
WM_CLOSE to the foreground window, through the in-process server (patch 0931); "inputs delivered 0 of 0 posted" counts
only the in-title sink, which the launcher layout never uses, so it reads 0 of 0 in every such run. Handle 0x11c is
Explorer's main thread's own message queue: the server's queue object has no type name, so the snapshot tags it by
handle alone, and its manual-reset sync is what the server sets when a message arrives, so the wait is GetMessage
idle. In -02 that thread was woken by the close (blocked 10,121 ms of the 10 s window) and slept again: with the
browser gone, the foreground window was one of Explorer's own, whose close is not the session's end. In -01 the
deadlocked browser was still up (three of its processes in the snapshot) and Explorer was never woken (42.9 s): the close
went elsewhere, most likely to the browser's window. WM_CLOSE to the shell window,
Progman, is the session's end: it ends Explorer's message loop and Explorer, the root, and the title returns to the
launcher. Start's Power does that, and so do the gate's probes that log off (`shell64` in
`gate-2026-10-18-terminate-01`: "process completed; returning to launcher", owner PASS).

**Fix, proven on the console (build `5e789e29caa907ea`).** Patch 1214 adds input type 8, the desktop session's log off: WM_CLOSE to
the shell window, or, with Explorer unloaded in game mode (no shell window), to the foreground window as type 4, after
which the session ends once the game has gone. The title sends it at a desktop session's stop (`native/proton_wine.cpp`),
and the stop's 10 s line now says the close went to the server instead of the sink's counts. The check: gate case
`desktop-stop`, the desktop session with no program and the run's budget as its end; run browser-desktop-02 (2026-10-18)'s
shape (owner FAIL initial_status=1 after SIGKILL) fails it. `desktop-stop-2026-10-18-01` passed: "the desktop session logs off", the
session ended within 3 s of the budget's stop at 50 s (program ended 50.28 s, Wine 52.74 s), owner PASS initial_status=0, a clean
owner restart. run browser-desktop-03 (2026-10-18), -02's exact run: the browser ended by its own script at 30-40 s as in -02,
the stop logged off with only Explorer and the services left, Wine ended at 122.99 s, owner PASS initial_status=0, a
clean owner restart, no helper ended by SIGKILL. No maxpipekva, TTY(STDIN) or errno=5 line in either run. Not covered: programs still open at the log off are not
sent WM_QUERYENDSESSION, as Windows would, and a stop that arrives while Explorer is starting again in game mode
finds no shell window and closes the foreground window instead.

## The guided setup, MihawkOS's first launch (2026-10-18; on the console: the walk and the language probe pass)

A step-by-step tour in the launcher: Welcome, Language, how to install, the install, the first Mihawk OS session, Done.
It is the friendly front end of SELF_INSTALL phases 5 to 7. The design is in [GUIDED_SETUP.md](GUIDED_SETUP.md).

**Built:**
- The tour and its rules (`src/pw_setup.*`, `examples/ps5proton/setup_tour.hpp`), on the installer's engine
  (`proton_install_find("usb")`), with a fake engine for tests and a simulated first run;
- The Frutiger Aero sky shader;
- ten languages, in the owner's order (Polish the tenth, 2026-10-26);
- The launcher's faces re-baked for accents and Cyrillic, plus Noto Sans SC/JP faces baked to the setup's text;
- The first session ending itself once the prefix is made;
- The first session's cover (2026-10-19, in v1.0.0-alpha-1; not yet on the console): the minute wineboot
  takes showed a black screen with the pointer. Start's frame is now saved, and the title shows it through the GPU
  broker (graphics patch 0030) with a mark for each stage: Windows's settings, .NET support, finishing up
  ([GUIDED_SETUP.md](GUIDED_SETUP.md), "The first session's cover");
- Wine patch 1230: games take the setup's language, the shell stays English.

**Proven on the host:**
- `tools/test-setup-ui.py`: every step and error state rendered in all ten languages, the QR code read back, the
  gate's walk.
- `test_pw_setup`, `test_pw_wine_inherit`.

**On the console** (build 07421f38, 2026-10-18):
- `setup-tour-2026-10-18-02` walked Welcome to Preparing in Japanese at 59.9 fps; the sky costs 0.51 ms GPU a frame
  at 3840x2160.
- `locale64-2026-10-18-01`: a program and its child report fr-FR, UI language 040c and keyboard 040c040c.

**Open:**
- A walk with the owner watching;
- The real engine's install from a stick;
- The first session ending itself;
- Explorer staying English, shown on the console;
- native-speaker review of the eight translations.

## Shared code copies between helpers: K11-K17 on the console (2026-10-18; the code is not merged)

Patch 1127 copies the code each helper runs into a JIT object charged to that helper, so every helper that loads a DLL
pays for its code again. At 16 KiB clusters CEF 150's three processes held 214 MiB against MihawkBrowser's 200 MiB
ceiling (2026-10-18; record removed from the tree; in git history). They had copied 71,216 KiB (browser), 44,112 (renderer) and 12,928
(StorageService), largely the same libcef.dll pages. The idea: one JIT object per code cluster, made by the first helper
that runs it and mapped by every other helper whose cluster has the same bytes.

That design is built and host-tested on branch `shared-code-copies`, uncommitted and backed up with the other unmerged
work of 2026-10-18 (its own PLAN.md section there has the details):
- The title keeps a registry keyed by content (the cluster's size and a 64-bit hash), and a helper compares the bytes
  before it maps a copy;
- only image code is shared;
- A write grant first gives the helper a copy of its own;
- helpers ask and offer on their own channel (`PW_CHILD_CODE_ASK`, `PW_CHILD_CODE_OFFER`).

**The probe**: `tools/dmem-share-probe.c` modes `exec5 serve` and `exec5 take`, run by `tools/run-dmem-share.py --exec5`.
They live on branch `codeshare-probe` (main plus those two files, uncommitted, since code is committed only behind a
gate). One payload makes a 16 MiB JIT object with code and a pattern and hands its descriptor over a Unix socket. The
other maps it, runs it, places it, writes it, and runs it again after the maker has gone. The run,
[exec-share-2026-10-18-01](../evidence/exec-share-2026-10-18-01.json), came at the end of a boot, with the owner present
and the title idle. Both payloads finished, the SDK's kernel write was never reached, and the klog before and after had
no maxpipekva, TTY(STDIN), errno=5, storage or fault line.

| K | Asked | Result |
| --- | --- | --- |
| K11 | A JIT object's descriptor to another payload over SCM_RIGHTS | passes (sent, received) |
| K12 | mapped there: its bytes, RX, run | The same bytes (0 wrong), RX 0x5 by sceKernelMprotect, the code runs |
| K13 | `MAP_FIXED` over the taker's reservation at exactly the object's size | maps there, RX, runs |
| K14 | whose resident flexible memory it is | each mapping's, in full (below) |
| K15 | after the maker unmapped, closed and ended | it still runs in the taker; the memory goes when the taker unmaps |
| K16 | write asked by the taker | granted (RWX 0x7), and the maker sees the write |
| K17 | The same name in another process | A different object |

K14, `sceKernelGetResidentFmemCount`, the figure the census sums:
- The taker went from 3,616 KiB to 20,048 after mapping and reading the 16 MiB object; the maker stayed at 20,048.
- With the object mapped at two addresses in one process, the taker read 36,432 KiB.
- It went back to 3,664 once the taker unmapped.
- The flexible-pool query read 8,268,800 KiB throughout, which tells nothing.

**What it means.** The mechanism works, but the measure does not move.
- A shared copy counts in every process, and every mapping, that maps it, exactly as a private copy does. So shared
  copies as designed would not lower the census, the memory guard's figures, or the browser's 214 MiB.
- Physical memory may well drop; this run does not show it.
- Whether it helps at all depends on the kernel's own limit, SceShellCore's `VM Stats: RSS <used> <limit>` (which tracked
  the census before: 919 against 928.9). If that counts per mapping too, sharing saves nothing on the console. If it
  counts a page once, the guard would have to count shared objects once to see the saving.
- K16 confirms the design's copy before a write grant is required.

**Decision** (2026-10-18): no gate and no merge of the sharing code; branch and sources kept. Next is the hold run
(CONSOLE_QUEUE.md, "Shared code copies"), which reads the kernel's own figure by phase, in an owner-present slot.

**K18, the kernel's own figure, measured in Wine (2026-10-21, standard PS5).** The sharing design was ported onto the
tree of the launcher's sign-in window (build `ae34b5fe4283bec4`, 147 host tests; not committed, backed up off the tree
in `~/PS5-Backups/unmerged-2026-10-21/`) and run in place of the hold probe: the launcher's 10-minute start, against
the same start without sharing on build `09acfc9faec09198`. Records kept off the tree: they name the launcher.

| Steady state | Without sharing | With sharing |
| --- | --- | --- |
| Clusters mapped from another helper's copy | 0 | 28,054 (448,864 KiB of mappings; 0 mismatched, 2 failed) |
| Census (helpers) | 1532 MiB (886 in 16) | 1568 MiB (892 in 16) |
| SceShellCore VM Stats RSS | 1524 MiB | 1560 MiB |
| VM Stats kernel | 431 MiB | 464 MiB |
| Page tables, CPU | 6840 of 8192 | 5170 of 8192 |
| Memory-guard ends in 10 minutes | 0 | 4 |

- SceShellCore's VM Stats RSS counts a shared JIT object in every process that maps it, as the census does (K14):
  449 MB of shared mappings moved neither figure down. That RSS is SceShellCore's own sum of the per-process figures
  (its FMEM lines add up to it to 0.1 MiB), not the kernel's: K19 shows the kernel counts a shared object once, not
  per mapping.
- About 27,000 objects of 16 KiB cost the kernel about 33 MiB more of its own memory.
- Only the CPU page tables fell, by about a fifth.
- An idle desktop ran clean with it (930 clusters shared, 0 mismatched).

**Decision** (2026-10-21, morning): shared code copies stay off the tree, as they saved no memory that counted.
Superseded the same day by K19: they do save physical memory on the standard PS5; what does not see it is the census.

**K19, what the standard PS5's kernel acts on (2026-10-21, console; the owner asked for the risky test).**
`tools/shared-charge-probe.c` and `tools/run-shared-charge.py` (not committed: code needs a gate). One payload maps
64 MiB JIT objects step by step, each step held 30 s and granted by the runner only after the step before passed:
- shared: one object mapped again at each step (the census grows 64 MiB a step; the physical pages stay one object);
- distinct, the control: a new object written at each step (both grow).

The probe reads every process's resident flexible memory itself each second, as the title's census does, because
SceShellCore prints its VM Stats and FMEM lines only on the running app's own process events: none at idle, none for
payloads started beside a title, none while the title drew its menu for 120 s. The title stayed on its launcher
screen for each run (test-run.txt `samples launcher`, 60 frames a second, no Wine session), which gives SceShellCore's
swap-out at its start and its exit. Fresh boot of the standard PS5 (13.40), console processes at about 1220 MiB.

| Run | Steps held | Per-mapping charge added | Object / pages added | Other processes | vm.swap_reserved | Swap-out |
| --- | --- | --- | --- | --- | --- | --- |
| `shared-charge-shared-2026-10-21-07` | 24 of 24 | 1,536 MiB | one 64 MiB object | flat (1,234 MiB throughout) | +76 MiB | 0.0 to 0.0 |
| `shared-charge-distinct-2026-10-21-01` | stopped at 10 | 640 MiB | 640 MiB of new pages | -56 MiB at step 10 (step 9: SceShellUI -5.7) | +64 MiB a step | 0.0 to 0.0 |

`shared-charge-shared-2026-10-21-03` was the first shared run; its probe still read pid 0 as the caller's own figure, so
its "other processes" grew by the probe's own mappings. The fixed probe (ELF `1e4ce…`, the distinct run's binary) reran
it as `-07`: 24 of 24, others flat at 1,234 MiB, `vm.swap_reserved` +76 MiB, swap-out 0.0, passed.

- The kernel took memory from other processes once 576-640 MiB of distinct memory was added, and never for 2.4
  times that charge in shared mappings. On 13.40 it counts a shared object once, not the per-mapping figure that
  SceShellCore and the title's census add up.
- After the release the processes it had trimmed were SceNKUIProcess (-21.5 MiB), SceNKNetworkProcess (-20.9) and
  SceShellUI (-7.7). Swap-out stayed 0.0: the system reclaimed, it did not swap, at that level.
- The kernel's own commit count, vm.swap_reserved, counted the shared object once and each distinct one again.
- `shared-charge-shared-2026-10-21-02` (3 steps) checked the tooling first.
- So on the standard PS5 shared code copies would save real memory: the browser's code is copied about five times.
  The census, and so the memory guard, would not see it unless shared objects are counted once.
- A census quirk found on the way: pid 0 answers sceKernelGetResidentFmemCount with the caller's own figure, so the
  title's census (`src/pw_memory_helper.c`, which skips only the title) counts its memory helper twice, a few MiB.
The launcher with both (`launcher-share-census-2026-10-21-01`, standard PS5, 13.40, build `0e78baf9`, the
`launcher-e2e` profile): 625.8 s, passed, returned to the launcher; 930 clusters shared (14,880 KiB mapped, 4,752 KiB
held at the peak), 0 mismatched, 0 failed; the census 1,216 MiB (helpers 225 in 9), 11 MiB of shared copies left out,
the raw peak 1,227 MiB, and no guard end; 0 storage errors. This profile is not the CEF one of K18; it validates the
census correction, not the 448 MiB saving.
- Not settled: the Pro (10.01), which prints an RSS limit (about 989-1014 MiB) and froze twice past it, may act on
  that printed sum. The 2026-10-20 run there, where counting the sync region once let RSS reach 1081 MiB and swap
  rose from 38 to 139 MiB, is consistent with that. It needs the same test on the Pro, which is riskier.

## The helpers' synchronization device, patch 1180 (2026-10-17, console; on by default since a5a0d88)

**Now on by default** (a5a0d88, `src/pw_speed_defaults.h`); `WINE_PS5_NTSYNC=0` turns it off for a run. The paragraphs
below are from when it was opt-in. Its first full gate with it on found a session that did not end, once in 12: a
live wait slept behind a dead helper's thread on a reused place and never woke. Fixed in `src/pw_ntsync.c` (a gone
process's places are kept until it has ended), host-tested: see "The session that did not end" below.

Each helper's SetEvent, ResetEvent and wait was a round trip to the title's server thread: patch 0890's device kept its
objects in the title's heap, and patch 0925 left it out of helpers. Patch 1180 gives the title and every helper one
device over shared memory the memory helper makes (`src/pw_ntsync.h`, `wine/ps5/pw_wine_ntsync.c`). The title formats
it before its server starts. The server sends the region to each helper with its first thread's reply, and a helper
that cannot join stops. The title takes a helper out of the device before it kills it and once it has ended
(`pw_ntsync_process_gone`). `WINE_PS5_NTSYNC=1` turns it on for the whole session; unset, nothing changes.

Measured on the console:
- **Operations** (`tests/sync-probe.c` in a helper, `xsync-probe-on-2026-10-16-02` against `xsync-probe-off-2026-10-16-02`):
  SetEvent+ResetEvent 2.17 us against 51.1 through the server, a signalled wait 2.11 against 29.3, semaphore
  release+wait 3.23 against 54.0, mutex take+release 3.38 against 54.1, an event round trip between two threads
  17.9 against 61.8, and between two processes 65.0 against 113.2.
- **The Direct3D 12 game's menus**: in the same 234 s, 3,419 frames shown with the device against 153 without (2026-10-17; record removed from the tree; in git history). These runs were on an earlier build where the device was on by default.
- **The Direct3D 12 game in gameplay** (the perf-120fps session's scripted stretch, main dcfe94f plus the loader session's CPU
  placement `WINE_PS5_HELPER_CPUS_WIDEN=1` and graphics 0021-0023, uncommitted): 30.5 fps off
  (2026-10-17; record removed from the tree; in git history) and 60.0 fps on, 1% low 58.0, p50 16.75 ms (2026-10-17; record removed from the tree; in git history), held at 60 by
  the Direct3D 12 game's only offered mode, 60 Hz (run sync-03 (2026-10-17) at 119.88 Hz: still 60.1). Server round trips per 10 s
  went from 1.27 M to 36.6 k, blocked selects from 273 k to 48. No fallback to the server, no Shell heartbeat gap, no
  wedge, no TTY(STDIN) or errno=5 line.
- **At 120 Hz**, with the game offered 120 Hz (win32u 1188, uncommitted) and the device on: 108.8 fps
  (run hz-03), and 119.8 with the asynchronous present as well (run hz-04, graphics 0024, uncommitted); the
  owner played in both, so they are not an identical A/B. The device is the base those results stand on.

Gates of this commit's tree, the device off (build 8c6a32f9 for the last): `gate-ntsync-2026-10-17-01` 82 of 84
(shell32 and shell64 on a folder an earlier run left, fixed in the shell probe), `-02` 83 of 84 (appearance32's capture
before its window was published, fixed in the appearance probe), `-03` 83 of 84: shell64's "hover shows the live
window preview" (below). Committed on `-03` with the hook bypassed once, at the owner's direction.

Not proven: a full gate with the device on, a soak, and a helper killed while it waits on the device on the console
(host-tested with forked processes: `tests/test_pw_ntsync.c`). The default stays off until those pass. Held back for
later, uncommitted: patch 1181 (which files a process reads most), patch 1182 (a helper tells its programs the CPUs it
may run on, `WINE_PS5_HELPER_OWN_CPUS=1`), and a fix for the helpers' per-thread CPU census, which read 0 threads in a
payload (run sync-01 (2026-10-17): kern.proc lists no threads of a payload, even its own).

## Open: the taskbar's hover preview did not show once (2026-10-17, console; cause unknown)

In `gate-ntsync-2026-10-17-03` shell64 failed one check of 88, "hover shows the live window preview"
(`tests/shell-probe.c`, `preview_checks`): the probe found the taskbar button of its target window, moved the pointer
onto it with SendInput (from (1800, 100), so a real move), and no visible `MihawkTaskbarPreview` came within 4 s.
The same check passed in `gate-ntsync-2026-10-17-02` (the preview up 0.43 s after the button was found), in every
earlier gate, and in that gate's shell32. No log line says why the taskbar did not show it: possibly a real shell bug
(the hover timer, or the preview's window), possibly the probe. For the port session when the owner resumes it.
Since gate-2026-10-18-shell-batch3-01 the check logs, when it fails, the button's rectangle when found and now, the
pointer, the window under it with its class and id, and whether the button is hot (`shell-probe preview missing:`).
It passed in that gate, so the line has not fired yet.

## The Direct3D 12 game above 120 fps: where a frame goes, and the plan (2026-10-17, console)

Goal: the Direct3D 12 game above 120 fps in real gameplay, by fixes that hold for every game. A frame then has 8.33 ms; today it
has 32.6. Stage 1, measured: the GPU works 2 ms of the frame, the present takes 5.8 ms on the game's queue thread,
and the rest is CPU, dominated by Wine's synchronization through the wineserver. Built and run from worktree
`perf-120fps` on main dcfe94f plus, uncommitted: the loader session's CPU placement (`WINE_PS5_HELPER_CPUS_WIDEN=1`:
The Direct3D 12 game's helper moved from CPU set 0xea00 to 0xffff), the port session's winedmo header line (a fresh Wine configure
fails without it), and graphics patches 0021-0023 below. Nothing of it is gated or committed yet.

**Status (2026-10-18):** The clean scripted A/B at 119.88 Hz, hands off, same settings (VSync on, FSR off): the
asynchronous present (0024) takes the Direct3D 12 game from 95.0 to 118.6 fps, p50 10.50 to 8.25 ms (run ab-02 (2026-10-17), `-03`);
the present link (0026) on top holds it at 119.8 fps and lifts the 1% low from 88-71 to 100-98 fps
(run ab-04 (2026-10-18), `-05`). Earlier, in the owner's own play, 119.8 fps. More than 120 shown frames a second is
not possible on this display; the Direct3D 12 game uncapped (VSync off) has not run. See "the Direct3D 12 game at 120 Hz" and "The clean A/B" below.

**Rule (the owner, 2026-10-17): games are tested at 120 Hz only.**
- Every game run uses `PROTON_120HZ=1`, with the game's own refresh setting at 120.
- No new 60 Hz baselines.
- The 60 Hz runs below stay as history. Every new comparison, such as the asynchronous present on and off or a
  hitch fix, is 120 Hz against 120 Hz.

**The instrument, graphics 0021** (`src/pw_gpu_timing.h`, `tests/test_pw_gpu_timing.c`): a `gpu-split` line beside
each client's `gpu-pacing` line, over the same 10 s window, per shown frame:
- GPU busy, with `PROTON_GPU_TIMESTAMPS=1` (off: no submission changes). Each submission is wrapped in the two
  timestamps RADV writes for `vkCmdWriteTimestamp`: a top-of-pipe COPY_DATA of the GPU clock and a bottom-of-pipe
  RELEASE_MEM. The ring runs in order, so the part of [start, end] after the previous end is the submission's own.
  The clock is measured against CLOCK_MONOTONIC: 99.77 MHz.
- The broker's present and its copy into the display buffer, and its handling of submissions.
- The client's share, sent once a second (`PW_GPU_TIMING`): the WSI's wait for the frame's fence, its cache flush
  of the frame, the present's round trip and its submissions' round trips.

First on the console in `gpu-ts-check-2026-10-17-01` (the DXVK window probe, passed, 1,060 fences, none wedged).

**The runs.** The Direct3D 12 game started from its desktop route, Continue from the owner's save into the same alley each time (pad
scripts in the evidence; Story, Continue, then a fixed 180 s stretch: 60 s turning the camera, 60 s walking 2 s
out and back, 60 s turning; no capture inside it, as each costs ~0.6 s in its present). Volumetric light off, the
owner's setting. Medians of the 10 s windows of the stretch; 1% low is 1000 / the windows' p99 interval, median to
worst.

| phase | run | output | fps | 1% low | p50 ms | GPU ms (busy) | present ms (copy) | fence wait ms | submits (round trips ms) | server calls / 10 s (avg us) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | run split-01 (2026-10-17) | 59.94 Hz | 30.6 | 27.4-26.5 | 32.50 | 1.98 (6%) | 5.64 (5.59) | 0.02 | 4.1 (2.10) | 1.27 M (32.5) |
| d | run split-02 (2026-10-17) | 119.88 Hz | 30.5 | 27.0-26.0 | 32.75 | 2.00 (6%) | 5.64 (5.60) | 0.01 | 4.2 (2.08) | 1.27 M (32.8) |
| c | run split-03 (2026-10-17) | 59.94 Hz | 30.5 | 27.2-25.3 | 32.75 | 1.99 (6%) | 5.62 (5.58) | 0.02 | 4.1 (2.10) | 1.27 M (32.7) |

The client's flush of the frame was 0.13 ms and the broker's handling of a submission 0.09 ms in every phase. No
0.7-1.4 s hitch came in any stretch; the worst interval was 49 ms. Phase b (the render resolution lowered) was not
run: the Direct3D 12 game's options menus are not scripted yet (run 1 captured the pause menu: Options is four down), and at 6% GPU
busy the resolution cannot move the frame rate.

**What it says.**
- **The GPU is not the limit.** It is busy 2 ms of each 32.6 ms frame. The WSI's wait for the frame's fence is
  0.02 ms: the frame is long done on the GPU when the game presents it.
- **The present costs 5.8 ms on VKD3D-Proton's queue thread.** VKD3D-Proton calls `vkQueuePresentKHR` from its D3D12
  queue's submission thread (`libs/vkd3d/swapchain.c:1213`, `d3d12_command_queue_enqueue_callback`). The broker
  WSI's `broker_present` then waits for the fence, flushes the frame and makes a synchronous round trip, in which
  the broker copies the frame into the 64KB_R_X display buffer pixel by pixel on the CPU (5.6 ms) and submits the
  flip. No later submission of that queue reaches the GPU meanwhile.
- **Submissions: 4.1 a frame at 0.5 ms each** (2.1 ms), almost all of it the IPC wake-up between helper and title.
- **The rest, about 25 ms, is the game's and Wine's CPU work, and Wine's synchronization dominates it.** The Direct3D 12 game's
  helper makes 1.27 million wineserver round trips per 10 s (127,000 a second, about 4,200 a frame) at 32.5 us:
  41 thread-seconds per 10 s. 890,000 are `event_op` (SetEvent/ResetEvent) and 366,000 `select` (273,000 of them
  blocked, 545 us on average). A helper has no in-process sync device (patch 0925).
- **120 Hz works through the broker** (graphics 0023): `supported=1 configured=0`, vblanks 8,348 us apart, kept. It
  changed nothing for the Direct3D 12 game (it is CPU-bound at 30 fps). Phase c's display, opened right after phase d's process
  ended, found 16,683 us: the mode did not outlive the process that asked for it.

**Is 120 reachable?** Not yet known. The GPU leaves room (2 of 8.33 ms). The present and the submissions fit (they
can leave the critical path). The question is how much of the 25 ms is the server round trips and how much is RE
Engine's own work on these cores. The sync device A/B answers it: it removes most of the round trips, with no other
change.

**The plan, ranked by the measured cost:**
1. **The helpers' sync device** (the Direct3D 12 game's session's patches 1180-1182, opt-in `WINE_PS5_NTSYNC=1`). It replaces
   about 4,200 round trips a frame at 32.5 us with device operations (2.2 vs 51.1 us per set+reset in its
   sync-probe). Next slot: the same scripted stretch, device off and on, the full split, the server counts and the
   Shell heartbeat. The Direct3D 12 game's session owns and gates the code; this session measures.
2. **The present off the queue thread** (this session): an asynchronous present in the broker WSI, so
   `vkQueuePresentKHR` returns at once and a present thread waits for the fence and makes the round trip. That
   takes 5.8 ms off VKD3D's queue thread. Then the copy itself, as a step:
   - graphics 0022, `PROTON_GPU_FAST_COPY=1`: an unscaled frame copied a 64-byte line at a time with streaming
     stores, byte-identical to the pixel loop on the host and 2-3.4 times faster there. Not run on the console: the
     display's view of streaming stores can only be judged on screen.
   - the destination: the GPU writes the display's tiled buffer (or the client's image is flipped), and the broker
     only flips.
3. **Submission round trips** (4.1 a frame, 2.1 ms): replies that do not block the submitting thread, or batching.
4. **120 Hz output**: done (0023), needed for anything above 60 fps to reach the screen. It needs a gate before it
   is relied on.
5. **The GPU**: 2 ms, nothing to do yet.
6. **Scheduling** (every Windows thread SCHED_RR 767): looked at again after 1, when the threads stop waiting on the
   server.

To commit 0021-0023: a gate on a tree without the loader session's diff, or once that lands (AGENTS.md, the commit
gate). Title-ended helpers in this slot: about 16 in 4 launches (12 closed per launch less those that exited
themselves); no TTY(STDIN), errno=5 or maxpipekva line.

### The sync device for helpers: 30 to 60 fps, then the Direct3D 12 game's own 60 Hz pacing (2026-10-17, console)

Build 7ecdfd641cb62978: the stage-1 tree plus the Direct3D 12 game's session's patches 1180-1182, merged for measurement and not
committed (three hand merges: the build script's units, `WINE_PS5_HELPER_OWN_CPUS=` beside the loader's
`WINE_PS5_HELPER_CPUS_WIDEN=` in `inherited()`, a comment). Same spot and stretch as stage 1; the scripts now press
OPTIONS for the title's START GAME at 112 s (the owner's instruction; the owner started S0 by hand, so S0's stretch
came from another spot and phase c is the like-for-like device-off reference). CPU-seconds per second are the flight
recorder's `run_ms` rows for the Direct3D 12 game's helper and the title (1182's census reported 0 threads in a helper: kern.proc's
thread rows answer ENOENT in a payload; the Direct3D 12 game's session has a fix).

| run | device | output | fps | 1% low | p50 ms | GPU ms | present ms (copy) | submits (ms) | rest ms | server calls / 10 s | CPU-s/s the Direct3D 12 game / title |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run split-03 (2026-10-17) (c) | off | 59.94 Hz | 30.5 | 27.2-25.3 | 32.75 | 1.99 | 5.83 (5.58) | 4.1 (2.10) | 24.8 | 1.27 M at 32.7 us | 1.79 / 1.22 |
| run sync-01 (2026-10-17) (S0) | off | 59.94 Hz | 29.2 | 25.8-24.0 | 34.00 | 2.00 | 5.80 (5.58) | 4.2 (2.06) | 26.4 | 1.26 M at 32.2 us | 1.78 / 1.21 |
| run sync-02 (2026-10-17) (S1) | on | 59.94 Hz | 60.0 | 58.0 | 16.75 | 2.40 | 13.91 (5.57) | 4.4 (3.52) | ~0 | 36.6 k at 26.1 us | 2.17 / 0.52 |
| run sync-03 (2026-10-17) (S2) | on | 119.88 Hz | 60.1 | 57.1-56.3 | 16.75 | 2.42 | 5.79 (5.56) | 4.4 (2.75) | 8.1 | 25.2 k at 23.1 us | 2.26 / 0.62 |

- **The device removes the server from the Direct3D 12 game's frame.** Round trips fell 35-50 times and blocked selects from 273,000 to
  48 per 10 s. The rest is handle churn: create_event, get_inproc_sync_fd and close_handle, ~3,750 each per 10 s;
  send_hardware_message, 1,250. The device served 7-8 processes, refused none and dropped 4 waits (a helper's exit).
  No process fell back, no "No heart beat of Shell UI", no wedge; 13 title-ended helpers in the 3 launches.
- **With the sync device off, the Direct3D 12 game was waiting, not computing**: 1.8 CPU-s/s of the 13 CPUs it may use. At 60 fps
  with the device on it uses 2.2.
- **At 59.94 Hz the display is the limit** (S1): the present waits for a free buffer (13.9 ms, of which 5.6 is the
  copy).
- **At 119.88 Hz the Direct3D 12 game holds itself to 60** (S2): the broker's display took 119.88 Hz (vblanks 8,343 us). Its presents
  no longer wait (5.8 ms, the copy), but the game was offered only a 60 Hz mode (its display settings list only
  60 Hz, the owner saw) and paces to it. The title sets `WINE_PS5_REFRESH=120` before any helper is spawned, and the
  PS5 driver publishes its one mode at that rate once a process sets up its desktop (`ps5drv.c`), so either a
  process published 60 Hz (the variable missing in it: `inherit_environment` skips an entry that does not fit the
  4 KiB block, and this one is appended last) or the registry's display cache from an earlier session was read
  instead. Not settled: no probe prints the modes a helper sees yet.

**120 fps, the updated read.** Reachable in principle, not shown; 60 is now proven. The GPU uses 2.4 of 8.33 ms,
Wine's synchronization is out of the frame, and the Direct3D 12 game uses 2.3 of 13 CPUs at 60 fps. Once the Direct3D 12 game may go past 60, the
next floor is VKD3D-Proton's queue thread. Each frame it spends 5.8 ms in the present and 2.75 ms in submission
round trips: 8.6 ms, over the 8.33 ms budget by itself.

**Next, in order:**
1. **A 120 Hz mode for the game.** A probe that prints the modes and `WINE_PS5_REFRESH` a helper sees, then the fix
   in that path, and `PROTON_120HZ=1` for the Direct3D 12 game.
2. **The present off the queue thread**: an asynchronous present in the broker WSI (5.8 ms off that thread). Then
   the copy: 0022, or GPU-side or zero-copy.
3. **Submission round trips**: 2.75 ms a frame.
4. Then the Direct3D 12 game at 119.88 Hz with the device on, the same stretch.

### The Direct3D 12 game at 120 Hz: the mode, 109 fps, then 120 with the asynchronous present; the periodic hitch was the capture (2026-10-17, console)

Build 7dc047752f9860ea: the tree above plus win32u 1187-1188 and graphics 0021-0024, uncommitted (the code waits for
the loader diff and the Direct3D 12 game's session's 1180 to land, then a gate). Same route and pad script as above: Continue from
the owner's save, the fixed stretch 270-450 s with no capture inside it.

**The 120 Hz mode (win32u 1188).** In the Mihawk OS session every program runs in a virtual desktop. Win32u's
`add_virtual_source` gave that source a fixed 60 Hz, and Proton's `emulate_modeset` cut its list to the current mode,
so a game was offered 60 Hz whatever the display ran at. 1188 takes the virtual source's rate from the display's
current mode and lists the full mode set for it. 1187 logs which process publishes which rate. The probe is
`tests/display-modes-probe.c`:
- `modes-p2-2026-10-17-04` PASS at 120: current 1920x1080@120, 72 modes at 60 and 120 Hz, DXGI the same.
- `modes-p3-2026-10-17-03` PASS at 60 (`PROTON_120HZ` unset, the control).
- P2 runs 02-03 failed on an earlier 1188 (a persisted registry mode, a wrong guess; dropped).

The Direct3D 12 game then offered 120 Hz and the owner set it; each 120 Hz run logs "Setting display mode: 1920x1080@120" five times.

| run | display | async present | The Direct3D 12 game's settings | fps | 1% low | p50 ms | frames over 20 ms in the stretch | GPU ms (busy) | present ms (copy) | submits (round trips ms) | CPU-s/s the Direct3D 12 game / title |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run hz-01 (2026-10-17) | 59.94 Hz | off | refresh 60, frame rate variable, VSync off, FSR on | 60.0 | 58.0-57.1 | 16.75 | 3 | 1.96 (12%) | 14.34 (5.58) | 4.2 (2.83) | 1.92 / 0.50 |
| run hz-03 (2026-10-17) | 119.88 Hz | off | refresh 120, variable, VSync off, FSR on | 108.8 | 90.9-72.7 | 9.25 | 10 | 2.44 (27%) | 6.28 (6.03) | 4.3 (2.80) | 4.31 / 0.88 |
| run hz-04 (2026-10-17) | 119.88 Hz | on | refresh 120, variable, VSync on, FSR off, CAS off, image quality 100%, textures raised | 119.8 | 97.6-90.9 | 8.50 | 9 | 3.27 (39%) | off the queue thread (6.39) | 4.0 (8.05, the vblank wait) | 4.47 / 0.96 |

How the runs differ:
- **The owner played during runs 03 and 04.** Their captures show the character in different streets, a cutscene and
  a load. Run 04's capture at 140 s shows the Graphics options open, where the owner turned FSR and CAS off. The
  config read after run 04 confirms those settings, and VSync on. So the two 120 Hz stretches are real play, in
  different scenes and on different settings. They are not the identical scripted A/B.
- **FSR's mode in runs 01 and 03 was not recorded**: only the refresh, frame-rate and VSync keys were read before.
- **Run 02 was the options capture pass.** The owner set 120 Hz in it at about 155 s. The Direct3D 12 game then ran 111-113 fps in
  gameplay (157-199 s, idle in the alley; capture 02 shows it).

**What it says.**
- **At 120 Hz without the asynchronous present, VKD3D-Proton's queue thread is the whole frame.** Of 9.19 ms, the
  present takes 6.28 and the submission round trips 2.80. The GPU works 2.44 ms, and the Direct3D 12 game's helper uses 4.3 CPU-s/s.
- **The asynchronous present (0024) takes the present off that thread.** The Direct3D 12 game then runs 119.8 fps, the display's rate,
  even with more GPU work per frame (3.27 ms, FSR off). The queue thread's submissions now wait behind the presenter,
  which paces to the vblank: 8.05 ms of round trips a frame. So the split's "rest" no longer applies.
- **The 1% low is 91-98 fps**: about 9 frames per 10 s over 20 ms, and one over 34 ms. The broker's CPU copy takes
  6.39 ms of each 8.34 ms vblank on the presenter's path. That is the margin the next step removes.
- **Not shown:** more than 120 frames a second. The display shows at most 119.88, and the Direct3D 12 game with VSync on stops there.

**The periodic ~0.65 s hitch is the capture, not the game.** A pad script's `capture` makes the broker
(`tooling/gpu-broker/display.cpp`, `capture_buffer`) do three things inside that client's present: wait for the flip
to be shown, read the 1920x1080 frame back from the display buffer pixel by pixel through the tiling, then write and
fsync a 6 MiB PPM.
- **Every capture window hitched.** In this slot's four runs of the Direct3D 12 game, each of the 60 windows (10 s each) that held a
  capture had a gap over 400 ms (13, 22, 12 and 13 per run).
- **No window without a capture hitched**, except at startup (the logos) and at the save's load.
- **The hitches the owner saw in run 02** (about every 2 s) were its captures: it took one every 1.8 s in the options
  menus.
- **The earlier "~690 ms every 20 s" hitch has the same signature.** run mf-03 (2026-10-17) took 59 captures in
  1,187 s (one per 20 s), and 53 of its 56 windows with a gap over 400 ms held one; run mf-04 (2026-10-17) took one
  every 30 s.
- **The ~160 MiB re-read is not the cause.** In run 02's gameplay at 111-113 fps, the Direct3D 12 game read 230-945 MiB per 10 s
  (1181's lines), its slowest read was 6 ms, and no gap came.

Until the capture leaves the present path, no capture goes inside a measured stretch, as the scripts already do.

Also seen: the Direct3D 12 game calls `set_cursor` once a frame (1,127-1,218 server calls per 10 s at 72-79 us each). Run 02's
capture shows the session's arrow pointer drawn mid-screen over the full-screen game.

Helpers: the title ended none in the four runs of the Direct3D 12 game. It ended 6 in each probe run (`modes-p2-04`, `modes-p3-03`): the
Mihawk OS session does not close within 10 s once the probe ends, so the title ends the session's helpers. There was
no TTY(STDIN), errno=5, maxpipekva, wedge, device-lost or Shell UI heartbeat line in any run.

**Next, in order:**
1. **A clean A/B on the owner's new settings**, hands off the pad: 120 Hz with and without the asynchronous present.
   VSync off shows the uncapped rate; whether benchmarks keep VSync on is the owner's call.
2. **The copy off the frame**: a GPU-side detile or a zero-copy flip (6.39 ms of each 8.34 ms vblank). 0022, the
   fast CPU copy, is the fallback; it needs the owner's eyes.
3. **Submission round trips** (2.8 ms a frame before the asynchronous present).
4. **The capture off the present path** (the instrument): copy the shown buffer, write the file on another thread.

### The clean A/B, first attempt: the capture's write leaves the present; the boot's pipes ran out (2026-10-17, console)

Build 9d7b83d75cffdf85: the tree above plus graphics 0025, first version: the capture's present copies the display's
buffer whole, and a thread writes the PPM (`src/pw_display_capture.h`). Verified deploy: title 101 files, runtime 1,163.
The owner kept off the pad. The Direct3D 12 game's settings, read from its config before the run without changing it: refresh 120, frame
rate variable, VSync on, FSR off, sharpness 0, rendering normal, image quality index 5, texture quality index 6.

**run ab-01 (2026-10-17)** (async present off, 120 Hz, sync device on), stretch 270-450 s:
- **98.4 fps**, 1% low 89.9-70.2, p50 9.50 ms.
- **Split**: frame 10.16 ms = GPU 2.91 (29% busy) + present 6.75 (copy 5.94) + submissions 3.08 ms (4.0 a frame) + 0.33.
- **CPU-s/s**: the Direct3D 12 game 3.96, title 0.80. The Direct3D 12 game logged 1920x1080@120.
- **Not comparable with earlier runs.** Every 10 s window, the stretch included, had one gap of about 344 ms, always in
  the same second of the window. run hz-03 and -04, the same code without 0025, had none: their stretch's worst gap was
  21-31 ms.

**Stopped there** (stop list):
- **The boot's pipe memory was exhausted.** "kern.ipc.maxpipekva exceeded" came 11 times, at every pipe the run made: the
  title's start, wineboot, each helper spawn. It came in none of the four run hz runs; this boot had just run the Direct3D 12 game
  session's 84-case gate.
- **No other stop line**: no errno=5 or TTY(STDIN) line, no wedge, and the title ended no helper.
- **The likely cause of the 344 ms gap** (not shown): once maxpipekva is exceeded, FreeBSD gives new pipes small
  buffers, and a helper's 10 s telemetry burst into its stderr pipe can then block the game thread writing it.
- **A target for every game:** a log line must never block a game thread.
- **Not run:** run ab-02 (async present on). The pair reruns after a console restart.

**The capture, first version, on the console** (17 captures in run ab-01, all written, none failed):
- **The write is off the present**: 336-439 ms on the capture thread (write_us).
- **The copy stayed in it**: 80 ms (copy_us). The display's buffer reads back at about 105 MB/s.
- **So a capture cost about 80 ms in its present, not 630-750 ms.** Its window's own gap could not be told from the
  344 ms one above.
- **Revised 0025** (host-tested, built, not run): the present fills the same frame a second time into memory of its own,
  from the frame's source, and never reads the display's buffer back.

### The clean A/B: the asynchronous present, 95.0 to 118.6 fps at 120 Hz; the capture costs 7 ms (2026-10-18, console)

Fresh boot (restarted, `new-boot` recorded, no storage-damage line).
- **Build 4cd76607c16faf92**: main 0c9c4b7 (the sync device committed) plus, uncommitted, the loader diff, 1181-1182,
  1187-1188 and graphics 0021-0027. 0025 is the revised capture, filled from the frame's source; 0026 and 0027 stay off.
- **Verified deploy**: title 101 files, runtime 119 changed.
- **The runs**: the owner kept off the pad. The same script and stretch as above, sync device on, `PROTON_120HZ=1`.
- **The Direct3D 12 game's settings**, read before each run without changing them, identical in both: refresh 120, frame rate variable,
  VSync on, FSR off, sharpness 0, rendering normal, image quality index 5, texture quality index 6.

| run | async present | fps | 1% low | p50 ms | worst gap ms | frames over 20 / 34 ms | GPU ms (busy) | present ms (copy) | submissions (round trips ms) | CPU-s/s the Direct3D 12 game / title |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run ab-02 (2026-10-17) | off | 95.0 | 84.2-70.2 | 10.50 | 77 | 19 / 2 | 2.92 (28%) | 7.11 (6.88) | 4.0 (3.04) | 3.65 / 0.84 |
| run ab-03 (2026-10-17) | on | 118.6 | 87.9-71.4 | 8.25 | 76 | 18 / 2 | 2.96 (35%) | off the queue thread (7.29) | 4.0 (8.07, behind the present) | 4.31 / 1.05 |

- **The asynchronous present is worth 23.6 fps here**, with everything else the same. Both runs logged
  1920x1080@120, and the second logged "gpu-wsi async present=1".
- **The queue thread's submissions now wait behind the present.** The connection carries one call at a time: 8.07 ms
  of round trips a frame against 3.04 without the asynchronous present. That is 0026's target (the present link).
- **The broker's copy is 7.29 ms of each 8.34 ms vblank** (6.88 without the asynchronous present), and the title uses
  1.05 CPU-s/s. Moving the copy to the GPU is the margin for the 1% low.
- **The capture fix holds.** Each capture now costs 6.8 ms in its present (copy_us, the frame filled from its source),
  and the file is written on the capture thread (write_us about 345 ms). 17 captures per run, none failed.
  - Windows with a capture had a median worst gap of 21.6 ms, windows without one 21.1 ms. Before, a capture window's
    gap was 630-750 ms, and about 80 ms with the first version. The one capture window over 400 ms is the save's load.
- **Healthy runs**: no maxpipekva line in either, so the 344 ms gap of the stopped attempt did not come back. No
  errno=5, TTY(STDIN), wedge or device-lost line, and the title ended no helper.

**0028, the broker's record through a socket pair** (`src/pw_log_drain.h`, host-tested; not built or run):
- **The change**: the record goes through a non-blocking AF_UNIX socket pair asking for 1 MiB, not a pipe, so it no
  longer depends on the system's pipe memory. A line that does not fit is lost whole, never waited for under the lock.
- **Host measurement**, with the drain held 300 ms: a writer through a 4 KiB pipe waited 300 ms for its worst line.
  Through the socket pair its worst line took 1.6-3.7 ms, and every line that reached the file was whole and in order.
- **Helpers are not changed.** A helper's log is written straight to its file. Making those writes asynchronous would
  lose the last lines when a helper crashes or calls _exit, so that waits for a design that flushes on those paths.

### The present link: the 1% low from 88 to 100 fps; the broker's record through a socket (2026-10-18, console)

Same boot as the clean A/B, build 4adbe9a6c38eb0e7 = that build plus 0028. Verified deploy: title 101 files, runtime
117 changed. The owner kept off the pad. Same script, stretch and settings (read again before the runs, unchanged).
Asynchronous present on in both.

| run | | fps | 1% low | p50 ms | worst gap ms | frames over 20 / 34 ms | GPU ms (busy) | copy ms | present round trip ms | submissions (round trips ms) | CPU-s/s the Direct3D 12 game / title |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run ab-04 (2026-10-18) | 0028 alone | 119.0 | 87.9-71.4 | 8.25 | 76 | 20 / 3 | 2.94 (35%) | 7.26 | 7.93 | 4.0 (8.06) | 4.28 / 1.06 |
| run ab-05 (2026-10-18) | 0028 + `WINE_PS5_PRESENT_LINK=1` | 119.8 | 100.0-97.6 | 8.50 | 58 | 3 / 3 | 2.98 (36%) | 7.51 | 8.20 | 4.0 (0.29) | 4.42 / 1.35 |

- **0028 costs nothing.** Run 04 matches run ab-03 (2026-10-17) (118.6 fps, the same 1% low). The broker logged
  "gpu-broker log=socket".
- **The present link takes the submissions out from behind the present**: 8.06 to 0.29 ms of round trips a frame. Both
  sides logged it (broker "present_link=1 status=0", client "gpu-client present link=1 status=0").
  - The frame holds 119.88 Hz.
  - The 1% low rises from 88-71 to 100-98 fps, and frames over 20 ms in the stretch fall from 20 to 3.
- **Its cost**: the title uses 1.35 CPU-s/s, up from 1.06. That is likely the second broker thread spinning 1 ms after
  each present (0027 can weigh it).
- **The captures in both runs**: copy_us about 6.8 ms, written on the capture thread, 17 per run, none failed.
  - Windows with a capture had a median worst gap of 15.5-20.8 ms.
  - The one window over 400 ms is the save's load: 437 ms in run 04, 905 ms in run 05.
- **No stop line** in either run (maxpipekva, errno=5, TTY, wedge, device lost, broker given up), and the title ended
  no helper.

### Landing what is proven: the gate tree (2026-10-18, console)

Built on main 0c9c4b7, without the loader diff, 1181-1182 or 0027. Fingerprint d2b7530e54d1fd61; host tests 111 of 111.

What it holds:
- **Wine 1187**: one line per display publish.
- **Wine 1188**: a virtual desktop offers the display's rate.
- **graphics 0021**: GPU timestamps, off by default (`PROTON_GPU_TIMESTAMPS=1`).
- **0022**: the fast copy, off by default (`PROTON_GPU_FAST_COPY=1`). It is here only because 0025's fill code sits on
  its branch, and it has not run with its switch on.
- **0023**: the broker's display at 119.88 Hz with `PROTON_120HZ=1`.
- **0024**: the asynchronous present, off by default (`WINE_PS5_ASYNC_PRESENT=1`).
- **0025**: the capture off the present.
- **0026**: the present link, off by default (`WINE_PS5_PRESENT_LINK=1`).
- **0028**: the broker's record through a socket pair.
- **Supporting files**: the headers and host tests of each, and the display-modes probe.
- **Helper environment**: what a helper inherits now lives in `wine/ps5/pw_wine_inherit.h`, with both switches in it.
  Host test `tests/test_pw_wine_inherit.c` runs the real block builder: both switches reach the helper, and the
  title's own settings stay behind.

**Sanity launch** (run sanity-01 (2026-10-18), both switches on, 120 Hz, sync device on, 345 s):
- **The switched-on path works on this tree.** The broker logged "present_link=1 status=0", the client "gpu-client
  present link=1 status=0", the WSI "gpu-wsi async present=1", and the Direct3D 12 game "1920x1080@120".
- **Clean**: no stop line, no helper ended by the title.
- **The Direct3D 12 game ran 57-70 fps, not 119.** Without the loader's CPU placement its helper has main's five CPUs (`0xea00`) at the
  console's lowest priority. It used 1.70 CPU-s/s, against 4.47 on all sixteen (`0xffff`) in run ab-05 (2026-10-18).
- **The 120 fps measurements above need the loader's work as well.**

**Gate** (`gate-2026-10-18-perf-01`, every switch off): **not passed. The tree was committed on the owner's waiver**
("I allow you to skip the gate and commit directly", 2026-10-18).
- **45 of 48 cases passed** before the battery stopped.
- **dnsapi32 and dnsapi64 failed on the console's DNS.** The DNS configuration read fine, but every query, of every
  type, over UDP and TCP, came back 9002 (server failure), and Wine logged "getaddrinfo Failed to resolve your host
  name". Nothing in this tree touches DNS.
- **dxvk-window64 failed when the console restarted during it.** ps5vkctl stopped answering, and the title did not
  return before the watchdog. The case's klog is empty, and the process list taken after it shows a console clock of
  193 s with every payload newly started.
- **The restart was the owner's, not a crash.** The owner restarted the PS5 while fixing its DNS ("Yes, I restarted
  the PS5 during the gate myself (for the DNS problem); it was not a crash").
- **Cases 49-84 did not run.**

**The second gate** (`gate-2026-10-18-perf-02`, the same deploy, every switch off) **was cancelled by the owner**,
superseded by the gate with every switch on (below).
- **Before it**: `new-boot` recorded, the title verified, and DNS checked from the console (`dns-check-2026-10-18-02`:
  dnsapi32 and dnsapi64 PASS).
- **Distorted by the owner's FTP copy.** The owner was copying a large game to the console over FTP during it. The
  probe group ran 2.4 times slower than in perf-01: 340 s against 140 s, mf-video32 45 s against 20, payload-eight 29
  against 13.
  - Its memory-classes probe held Wine's virtual mutex 3.2 s ("stall vlock ... held_ms=3206"), and the backstop rule
    failed the whole grouped launch for it. So all 42 probe cases read FAIL, though each probe logged PASS with status 0.
  - prefix-fresh32 then failed at case 72.
  - An environmental condition, not this build.
- **Main moved to d7405f9 on the combined evidence** of perf-01 and perf-02, both distorted, by the owner's decision
  ("Fast-forward main to d7405f9 now on the combined evidence; rerun the gate later as a check"). The confirmation
  gate it owes is the switches-on gate below.

### Every speed switch on by default (2026-10-18, console; gate-2026-10-18-defaults-02 84/84)

The owner's decision: a normal build runs a game at the display's best rate with no settings. On main c09e631 (the
loader's CPU placement included), the title turns on each speed switch, `src/pw_speed_defaults.h`. It does this after
the game's game.json and the run's own environment, so `NAME=0` from either still turns one off.

| switch | what it does | measured | off |
| --- | --- | --- | --- |
| `WINE_PS5_NTSYNC` | The session's one synchronization device (Wine 1180) | The Direct3D 12 game 30 to a locked 60 fps at 60 Hz | `=0` for a run; a game's game.json cannot set it |
| `WINE_PS5_HELPER_CPUS_WIDEN` | A game's helper on all sixteen of the title's CPUs | The Direct3D 12 game 1.70 to 4.47 CPU-s/s at 120 Hz | `=0`, also per game |
| `WINE_PS5_ASYNC_PRESENT` | The broker WSI presents from the swapchain's thread (0024) | 95.0 to 118.6 fps | `=0`, also per game |
| `WINE_PS5_PRESENT_LINK` | presents on their own connection (0026) | 1% low 88 to 100 fps | `=0`, also per game |
| `PROTON_120HZ` | The display at 119.88 Hz where the TV takes it (0023) | needed for any of the above above 60 | `=0` for a run, or a game's game.json in the launcher route |

**120 Hz is safe on a 60 Hz display.**
- **Before**: the title set `WINE_PS5_REFRESH=120` as soon as 120 Hz was asked, before the broker had opened its
  display. Where the TV refused high frame rate and the broker restored 59.94 Hz, Wine still offered 120 Hz modes.
- **Now** (graphics 0029): the title asks the broker for the rate its display kept right after starting it, before
  any helper: `ps5_gpu_broker_display_hz()` opens the display as the first present would. The title sets
  `WINE_PS5_REFRESH=120` only on 120.
- **On a 60 Hz fallback** The variable stays unset, and a program sees 60 Hz modes alone (`modes-p3-2026-10-17-03`:
  36 modes, DXGI 12, all at 60).
- **The rule is in one place** (`src/pw_display_rate.h`, used by the broker): 120 Hz only when asked, supported and
  configured, with vblanks within 12.5 ms. The title logs "Wine: the display kept N Hz", and the broker logs
  "gpu-display early hz=".
- **Host-tested**: `tests/test_pw_display_rate.c` checks kept versus each fallback; `tests/test_pw_speed_defaults.c`
  checks unset to on, and `=0` staying off.
- **The display now opens at the broker's start**, not at the first present. So every launch that starts the broker
  sets the TV's mode then. In the title layout (no broker) Wine offers 60 Hz alone.

**The gate does not assume 60 Hz.** No battery or runner verdict reads a frame time: they check pixels, verdict lines,
exit statuses and relative fps, and frame-pacing-probe's 1/60 s cadence only drives its input. The one 60 Hz constant
left is behaviour, not a check: the broker paces a client that does not hold the screen with a 16 ms sleep.

**Watch at 120 Hz**: the Shell's heartbeat ("No heart beat of Shell UI") and the desktop's compose cost (the broker's
"gpu-windows" lines) in the gate.

**Known risk**: a game that counts vblanks for its timing runs twice as fast at 120 Hz. `PROTON_120HZ=0` in its
game.json turns 120 Hz off for it in the launcher route.

**On the console, build 2a3cd9bd5a814ea6** (host tests 114 of 114; verified deploy: title 101 files, runtime 118):
- **The DNS check passed** (`dns-check-2026-10-18-03`), with the pad awake, and its launch logged every switch at 1
  and "the display kept 120 Hz; Wine offers 60 and 120 Hz".
- **The Direct3D 12 game with no settings at all** (run defaults-01 (2026-10-18), the A/B's route, the stretch's first 75 s, owner off
  the pad): 119.8 fps, p50 8.50 ms, 1% low 100 fps, worst gap 59 ms; the Direct3D 12 game 4.43 CPU-s/s, the title 1.36.
  - Every fast-path line is there: the sync device ("processes=7 ... refused=0"), the present link on both sides,
    "gpu-wsi async present=1", "gpu-display early hz=120", and the Direct3D 12 game's "Setting display mode: 1920x1080@120".
  - Every program helper ran on CPU set 0xffff; the memory helper keeps 0xea00.
  - No stop line, no helper ended by the title.

**First gate with the defaults** (`gate-2026-10-18-defaults-01`, build 2a3cd9bd): 82 of 84, no stop line. The first
full gate ever run with the sync device on: the Direct3D 12 game's session's three gates ran it off.
- **gpu-window32 found a real bug in the asynchronous present.** Its presenter into another process's window (Chromium's
  shape) presents once.
  - Its first window present was refused with status 70 (ESTALE: the window not yet in the owner's snapshot).
  - Synchronously the failure goes back to the program, which presents again: perf-01 and perf-02 show 3-5 refusals,
    then frame 1 taken.
  - Behind the asynchronous present the presenter swallowed it as a lost device, and the one frame was never shown.
  - **Fixed in 0024's WSI patch**: on ESTALE the presenter sends the same image again every 2 ms for up to 500 ms,
    while nothing newer is queued, then drops it, never a lost device.
  - On build 7732875266e270c0, gpu-window32 alone passed: refused 16 times (about 32 ms), then taken.
- **game-mode-dxvk64 printed PASS, then the session did not end** until the run's budget stopped it.
  - The probe ended, and the desktop had been restored 4 ms before. The restored Explorer never ended the session.
  - On 7732875 it then passed 11 times in 11 alone with the defaults (about 26 s each).
  - **A new, intermittent mode**: 1 in 12 runs with the defaults, never in the 34 earlier runs with the switches off,
    whose two failures were other modes. The failing run had no refused present.
  - **Named to the coordinator as a flake candidate**: a sync-device-off comparison cannot separate the switches at
    that rate in reasonable console time.

**The gate passed on 7732875266e270c0** (`gate-2026-10-18-defaults-02`, every speed switch on by default): 84 of 84 in
1,688 s, game-mode-dxvk64 and gpu-window32 included, no stop line.
- Every launch logged "gpu-display early hz=120".
- **The Shell at 120 Hz**: no "No heart beat of Shell UI" in any case. The desktop's compose averaged 2.9 ms in
  desktop-session (worst 22.2 ms) and 3.0 ms in desktop-view (worst 6.6 ms), against an 8.33 ms frame;
  window-compose64 took 1.0 ms.

**Built for the rerun: 53bebfb34acfba45** (host tests 112 of 112; not run; uncommitted). It adds two broker patches,
both off by default:
- **0026 (`WINE_PS5_PRESENT_LINK=1`): a client's presents on their own connection.** Each connection takes one call at a
  time, so with the asynchronous present the presenter's call held the submissions behind the display's 6.4 ms copy:
  8.05 ms of submission round trips a frame in run hz-04.
  - At the first present the client asks for a second socket. The broker serves it on a second thread and takes
    presents and their timing alone there.
  - A free of a frame being copied waits for that copy, and closing a client joins both threads.
- **0027 (`WINE_PS5_RPC_SPIN_US`, `PROTON_GPU_SERVE_SPIN_US`): how long a call spins for its reply** (2,000 us in the
  client, 1,000 us in the broker), so one run can weigh that CPU against round-trip time.

### The session that did not end: a wait asleep behind a dead thread (2026-10-18, console and host)

**The hang**: game-mode-dxvk64 with every speed switch on printed PASS, then its session did not end until the run's
budget stopped it, once in 12 (`gate-2026-10-18-defaults-01`). Never seen with the switches off.

**What the snapshot showed** (build 0e2ed1b2 on a5a0d88; Wine 1189 names what keeps a session; about 30 runs asked
for, stopping at the first hang):
- `stress-gmdx-2026-10-18-01` to `-10` passed, about 26 s each, no helper ended by the title. `-11` hung (185 s,
  "run budget reached"), with no errno=5, TTY(STDIN) or maxpipekva line.
- The server still counted one user process: the restored explorer.exe (system=0). shutdown_stage=0, no timeout;
  every other process was a system process.
- Explorer's main thread had been blocked 159 s in a sync-device wait on its "Event hf4" (auto-reset, clear) whose
  queue was 0. The waiter was no longer queued on its only object, so it had been served and had not woken.
- Its last line was the load of actxprxy.dll (COM setup, before its message loop), 19 ms after the game's process
  called its exit.

**The cause, in the device** (`src/pw_ntsync.c`, the Direct3D 12 game's session's 1180):
- At a helper's exit message the title calls `pw_ntsync_process_gone(pid, 0)`, while the helper still runs. That call
  freed its wait places too.
- The next live wait to block took a freed place and slept on the same word behind the helper's thread, still
  asleep. The kernel wakes the older sleeper first. The dead thread took the wake and stopped in the device, as a
  gone process's threads do; the live wait was served and slept on.
- Run 11's counts fit. The device dropped 5 waits: the old explorer's 4 (logged once it had ended), and the game's
  one blocked wait, at its exit message (that call logs nothing). Explorer's main thread blocked inside the 200 ms
  before the game was confirmed gone.

**The fix**: the waits still leave the queues at the first call, so no signal is spent on them. Their places are kept
until the call made once the process has ended. The dump says how many are kept ("N of them kept until theirs end").
- Host test (`tests/test_pw_ntsync.c`): a helper told gone with its thread asleep, then a live wait on that helper's
  process handle, which the server sets.
- The old device failed it: the wait woke only at its 3 s timeout. The fixed device wakes 0.00 ms after the set.
- The hang's object type under threads: a wait-any of 1 on an auto-reset event, set 20,000 times against waits that
  time out after 0 to 100 us, each set answered before the next. No set is lost to a timed-out wait.
- Host tests: 116 of 116.

**A second gap on the same path, found by the host suite** (one run in about 36 failed in the older test of helpers
killed while their threads spin through the lock):
- The title tells a helper gone, then kills it. A thread of the helper on its way into the device takes the lock,
  sees its process gone, and gives the lock back. A kill between the two left the lock held by a dead process.
- Then the call once the helper had ended returned -EBUSY, and every later device call of every process would have
  waited forever.
- **The fix**: the lock word names its holder's process (layout 2). Once a process told gone has ended still holding
  it, that call takes the lock back: its holder can only have been a thread on its way in, which changed nothing. A
  process that held the lock when it was to be told gone was never marked gone, so its lock is never taken.
- **Host test**: a test hook stops such a thread holding the lock until its kill. The old device fails it every time
  (-EBUSY); the fixed one takes the lock back and goes on, and the dump says "the lock taken back from 1".
- The whole device suite passed 15 runs of 15 with both fixes; host tests 117 of 117 on main 05d8d68.

**On the console, build a57f978fb224798c** (main 05d8d68 with both fixes; verified deploy):
- **A first attempt on the previous boot was stopped**: `dns-check-2026-10-18-05` passed but its klogs had 6
  "kern.ipc.maxpipekva exceeded" lines per test, and `stress-gmdx-2026-10-18-fix-01` 9. The boot's pipe memory was used
  up by earlier runs, so nothing was counted. The owner restarted the console.
- **On the fresh boot**: no maxpipekva line in the boot's klog; `dns-check-2026-10-18-06` passed with the pad awake,
  every switch at 1 and the display at 120 Hz.
- **`stress-gmdx-2026-10-18-b3-01` to `-30`: 30 of 30 passed**, 25.8 to 28.6 s each, no helper ended by the title, no
  stop line, no maxpipekva line. Before the fix the same loop hung at its 11th run (1 in 11; 1 in 12 in the gate).
- **`gate-2026-10-18-ntsync-fix-01`: 83 of 84** in 1,684 s, no stop line, no maxpipekva line; game-mode-dxvk64,
  gpu-window32 and broker-wedge64 passed. The one failure, host-dmem-fallback64, was its cursor sample only:
  - Before each capture the probe puts the cursor at (1800,100), and GetCursorPos confirmed it every time.
  - In captures 2 to 4 of 6 the composed cursor was elsewhere and moving: about (1407,91), then the top edge near
    (1827,0), then out of view. Capture 5 was back in place.
  - The broker composed about 118 frames a second throughout, so the frames were fresh and the cursor itself moved.
  - **The owner's touchpad input, confirmed**: the owner touched the controller during that gate (the DualSense
    touchpad drives the pointer). No log records touch events, so the run alone could not show it. The gate was run
    again on the same deploy with the owner off the pad, and that run is the one that counts.
- **`gate-2026-10-18-ntsync-fix-02`: 84 of 84** in 1,677 s on the same deploy, no stop line, no maxpipekva line;
  host-dmem-fallback64's 6 cursor samples passed. Committed on it.
  - Later, cheaply: the cursor-sample checks should log the touch and mouse event counts around each capture, to tell
    outside input from a broken cursor.
  - The same probe's cursor samples passed in gpu-window64, gpu-window32 and host-dmem64 in the same gate, and this
    check had never failed in 186 earlier logs.
- **`frame-pacing-2026-10-18-120hz-02`** (PASS, display 120 Hz): the steady phases averaged 2.57 ms per
  composition, worst 8.00 ms. Explorer opening with main's separable Mica Alt draw: worst 14.7 ms (was 19.5 ms on
  a5a0d88), average 3.6 ms over 13. Still over one 120 Hz frame.

**Also in this build** (uncommitted until the gate):
- **The asynchronous present's retry** is now a portable function, `src/pw_present_retry.h`, host-tested
  (`tests/test_pw_present_retry.c`). It logs one line per burst: "gpu-wsi present refused tries= waited_ms=
  outcome=".
- **Wine 1189**: the hang snapshot's process lines.
- **The frame-pacing runner judges against the display's frame**: 8.33 ms at 120 Hz, from 0029's "gpu-display early
  hz=" line (`tests/test_frame_pacing_runner.py`).
- **Handed over by other sessions**: the explorer-opening phase, makedep rerun on a series change, and the console
  guard's new-boot check by uptime.

**Frame pacing at 120 Hz with the defaults** (`frame-pacing-2026-10-18-120hz-01`, PASS, display kept 120 Hz):
- The steady phases' 602 compositions averaged 2.55 ms, worst 8.01 ms (menu-hover), so none went over a frame. Drag
  averaged 1.64 ms (worst 2.09 ms).
- Explorer opening is the one composition over a frame: worst 19.5 ms, average 4.2 ms over 12. Start opening peaked
  at 9.1 ms. That build is a5a0d88, before main 6f573b5's separable Mica Alt draw, so 19.5 ms is the old draw's
  baseline. Bringing it under 8.33 ms is this session's compositor work for a later round.

### A thread terminated while it waits leaves the device's queues (2026-10-18, host and console)

**The gap** (no console failure seen yet; found by reading the path after the hang above):
- Wine ends a thread with SIGQUIT (the server's kill_thread). On the console its handler only marks the thread, which
  ends at the user-mode boundary (Wine 0885), so that no lock is taken with it.
- Wine installs its handlers with SA_RESTART, so a thread asleep in a device wait went straight back to sleep.
- The terminated thread kept its place in the queues. The next set was given to it rather than to the live waiter
  behind it, and on a wait without a timeout it never ended at all.
- Upstream's kernel device drops a wait that a signal interrupts, so this happens only on the console.

**The fix, in the device alone** (`src/pw_ntsync.c`; Wine's quit handler already calls `pw_ntsync_signal_deferred`
first, patch 0923):
- A SIGQUIT on a thread outside the device marks the thread as ending. If it is asleep in a wait, the device also writes
  its wake word, so the restarted sleep returns at once.
- That wait leaves its queues and returns -ECANCELED, and so does any later wait of the thread that would block.
  Wine's retry loop stops only on EINTR, so the call returns, and the thread ends at the boundary.
- A wait served before the signal still returns its object, as the kernel's does.

**Host test** (`tests/test_pw_ntsync.c`): a thread blocked on an auto-reset event, a live waiter behind it, then a
SIGQUIT handled as Wine's console handler does (with SA_RESTART).
- 40fd0dd's device fails it: the terminated thread's wait ran on to its timeout.
- The fixed device ends that wait with -ECANCELED, and the next set wakes the live waiter 0.01 ms later.

**Console check, permanent**: `tests/sync-probe.c` gains "set-after-terminate-wakes-live" (TerminateThread on a
blocked waiter, then SetEvent must wake the live one), and the probe joins the gate as `sync64` in the grouped probes
launch.

**On the console, build e39e250d815c29ec** (main 71bb2f8 plus this; verified deploy; no maxpipekva line on the boot
or in its runs):
- `sync-terminate-2026-10-18-01`: sync64 alone passed with the sync device on, "terminated waiters" included.
  Its benches: SetEvent+ResetEvent 0.22 us, a signalled wait 0.99 us, an event round trip between threads 8.85 us, and
  between processes 48.2 us.
- gate-2026-10-18-terminate-01: 85 of 85 in 2,712 s (sync64 included), no stop line, no maxpipekva line. It ran about
  twice as slow as gate-2026-10-18-ntsync-fix-02 across unrelated cases (prefix-fresh64 174 to 347 s, window-compose
  and shell about 2x), as did this boot's deploys: the owner confirmed copying files to or using the console during
  that gate, so it was load, not this change (which only acts on a SIGQUIT).

### A thread's mark as ending stayed with its number (2026-10-18, host; found reading the Direct3D 12 game's black window)

The device keeps a thread's state in a slot keyed by its kernel thread number, kept for good. e9b9eac's mark (a
SIGQUIT reached the thread, so its waits end) stayed there after the thread ended. The console gives a number again
to a new thread, which then inherited the mark: every wait it made would have ended at once with -ECANCELED.
- **The fix**: the thread's exit resets its slot (a pthread key's destructor, set at its first entry into the device).
- **Host test**: two threads given the same number (a test hook). The first is ended while it waits; the second must
  block and wake at the set. e9b9eac's device fails it (the second never blocks); the fix passes.
- **Not the Direct3D 12 game's black window**: no thread was killed in that Direct3D 12 game before its stall.

**The Direct3D 12 game's black window** (the owner's session on e39e250d, read from its logs):
- The Direct3D 12 game's main thread stalled partway through its load: after the swapchain's resize to 1920x1080 it never loaded its
  audio stack, never set its display mode and never pumped messages.
- The render side kept presenting black frames, about 92 a second; no present was refused.
- In that session the earlier emulator experiment's program had run and ended 13 s before, a second explorer.exe had started, a USB mouse was
  in use (1,159 events) and the pad was asleep.
- **Not reproduced fresh**: run black-01 (2026-10-18), the same build by the desktop route, reached the title screen
  by 85 s and closed on the stop request.
- **The cause was the launch route, not the earlier emulator experiment or the sync device.** The owner reproduced it by hand: after an emulator
  run, then with none at all, the Direct3D 12 game opened from a File Explorer window stayed black; started from a
  command line it loaded every time. In the hang snapshot the Direct3D 12 game's main thread took the close request and exited cleanly,
  so it was not stuck in a wait. The Direct3D 12 game's log differed in one line: vkd3d-proton logged "Failed to map read-only cache:
  vkd3d-proton.cache", where a good launch maps the game folder's cache. The program started in File Explorer's
  folder, so it missed its shader cache and built every pipeline again behind a black window.
- **The fix, shell32 patch 1184**: a program opened from a folder starts in that folder, as on Windows. The shell view
  takes the folder from its path, without the attribute check on the folder's absolute ID list. The item menu's verbs
  now pass the folder too; they gave ShellExecuteEx none, upstream as well.
- **Proven on the console** (build 9e29e0c3acedf5d5 with the thread-reuse fix and the census; verified deploy; the
  owner at the console): the Direct3D 12 game opened from File Explorer found its shader cache, loaded its audio stack, set its display
  mode and reached the game (helper 2171). Closed and opened again from File Explorer (2173), it did the same.
- **Which half**: the view's own open already started a program in its folder without 1184, on the console at the
  drive's root and two folders deep with spaces (`shell-open-2026-10-18-before`, `-before-deep`). The item menu's
  default Open did not: on upstream Wine 11.20 on Linux the probe's menu copy started in `C:\windows` (`view=C:\shell-open
  menu=C:\windows`), the same code as the pinned tree's. So the owner's launch went through the menu half.
- **The permanent check fails on the bug**: `tests/shell-open-probe.c` (gate case shell-open64) opens itself both ways,
  the view's open and the item menu's default Open, and fails if either copy starts outside its folder. It fails on
  upstream Wine 11.20 on Linux, whose item menu code is the pinned tree's; the before-evidence on the console is the
  owner's two black-window sessions on e39e250d. It runs in the bundle's gate, expected to pass.
- **The census, cut to what works**: the title may list processes but every descriptor read was refused (89 of 89),
  and the walk cost 0.8 s at each launch's start. Only the pipe-buffer probe stays (`src/pw_pipe_memory.h`): one line
  per launch, "pipe memory at start: a fresh pipe takes N bytes", low once more than half is used.

### Open: the boot's pipe memory runs out after a few gates (2026-10-18, console)

**What is seen**: "kern.ipc.maxpipekva exceeded" in the klog once a boot has run about three gates
(`dns-check-2026-10-18-05`, 6 lines per test; `dns-check-2026-10-18-07`, 16, after the batch 4 and legal gates). Every
new pipe then gets a small buffer, and runs on that boot are stopped and not counted. Only a restart frees it.

**What it is not**: the title restarts for every launch and its descriptors go with it; Wine's pipes are AF_UNIX
socket pairs (`wine/ps5/pw_wine_compat.c`); the broker's record went to a socket pair in graphics 0028. What lives
across launches: the Lapy daemon, ps5vkctl, and the loader's websrv, klogsrv and ftpsrv.

**The first census did not work**: the title read `kern.ipc.pipes`, `pipekva`, `maxpipekva` and `pipeallocfail` at
each launch's start, and the console's kernel gave none of them (all -1 in `dns-check-2026-10-18-07`), although it
prints the maxpipekva message itself. It was taken out before the gate; the patch is kept aside.

**The second census, built (7e7c9e993ebaef0d, host-tested, not run)**: at each launch's start the title lists every
process (`kern.proc.proc`, as the memory helper does) and reads each one's descriptors (`kern.proc.filedesc`). It
counts the pipe ends each holds and logs the six largest holders, with how many processes refused
(`src/pw_pipe_holders.h`). It also measures what a fresh pipe takes before a write would wait: 16 KiB or more while
less than half the pipe memory is used, one page past that. One line per launch: "Wine: pipes at start buffer=
ends= holders= processes= refused= ms= top: name(pid)=n ...". Ten launches in a row then name the process whose
count grows; if the kernel refuses other processes' descriptors, the buffer probe still shows the exhaustion coming.
Then the fix, and the census kept as a permanent check.

## Direct3D 12 for every program: the native graphics DLLs the runners verified (2026-10-16, console)

Every program the title started got `WINEDLLOVERRIDES=d3d11,dxgi=n;bcrypt=b`, so `d3d12.dll` stayed Wine's own while
`dxgi.dll` was DXVK's. Wine's `D3D12CreateDevice` asks the adapter for Wine's private `IWineDXGIAdapter` (or
`IDXCoreAdapter`) even when the game passes none (`dlls/d3d12/d3d12_main.c`); DXVK answers neither, so no Direct3D 12
program launched from the title could make a device. VKD3D-Proton's `d3d12`/`d3d12core` were already staged
(`tools/stage-wine.py`) and the conformance runners loaded them, so the Direct3D 12 suites passed while real launches
failed. The title now gives Proton's default native set, `d3d11,dxgi,d3d10core,d3d9,d3d12,d3d12core=n;bcrypt=b`
(`native/proton_wine.cpp`; d3d8 stays Wine's unless a game opts in), and the Zink branch, which compares against the
same constant, prepends `opengl32` as before. `tests/test_dll_overrides.py` fails if the title's list and the runners'
(`tools/run-vkd3d-tests.py`, `tools/run-wine-tests.py`) drift apart again. Direct3D 9 and 10 programs now get DXVK
instead of wined3d.

Console:
- run d3d12-01 (2026-10-16), the Direct3D 12 game from the Mihawk OS session (its folder, the session's environment):
  `d3d12.dll` and `d3d12core.dll` load native (VKD3D-Proton af89350c), the device is made ("Detected RE Engine",
  SM 6.8), and `re3_dxgi.log` no longer asks for `IWineDXGIAdapter` (17399d75-...); the one `IDXCoreAdapter`
  (f0db4c7f-...) query left is VKD3D-Proton's own `d3d12_get_adapter` trying DXCore before `IDXGIAdapter`
  (`libs/d3d12core/main.c:395`). It reached the health warning, the CAPCOM logo, the autosave notice and a "Next"
  screen at 1 to 3 fps windowed and 5 to 25 full screen; why it is slow is the next section's subject. Before:
  the review's klog (`d3d12.dll` builtin, both queries, no device).
- Gate `gate-d3d12-overrides-2026-10-16-01` on main e47bb00 with this change: 80 of 82 in 1,759 s; every graphics case
  passed (pe32, wow64, gpu-window32/64, dxvk-window32/64, game-mode-dxvk64, vk32, gl32, gl-upload32, launcher-render);
  shell32/64 failed three Explorer checks fixed since on main (762df4f).
- Gates on this change plus the CPU census below, rebased as main moved: `gate-d3d12-census-2026-10-17-01` (main
  68b30d4) 81 of 82 in 1,743 s, `window-effects64` failing on the probe's capture numbering (a retaken capture reused
  number 01; fixed on main in 163d8d3); `gate-d3d12-census-2026-10-17-02` (main 163d8d3, build 8fe8d70a, the tree
  committed here) 81 of 82 in 1,816 s, `appearance32` failing on 10 of 4,322 caption-strip pixels (the minimize
  glyph's line drawn in the hot bar's colour, as if captured mid-repaint; the probe itself passed). In all three gates
  every graphics, DXVK and 32-bit case passed. Committed without a full pass at the owner's direction (2026-10-17):
  the hook was bypassed for this commit only, the privacy check run by hand.

Where helpers may run (the title's `cpu:` line and each helper's `wine-ps5: cpus` line, read with
`cpuset_getaffinity` only, nothing changed): in `gate-d3d12-census-2026-10-17-01` all 77 title starts said "may run on
0x1fff (CPUs 0-12), its CPU set holds 0xffff, the root set above it 0xffff", and all 477 helper starts said "may run
on 0xea00 (CPUs 9, 11, 13, 14, 15), its CPU set holds 0xea00, the root set above it 0xffff". So a helper's own set is
those five CPUs: its threads cannot be widened by affinity, and the title's idle CPUs 0-12 are reachable only by
leaving that set (`cpuset()` or `cpuset_setid`, both in `libkernel_sys`). The Direct3D 12 game's threads used about 1.5
CPUs in its menus while CPUs 1, 3, 5 and 7-12 sat 99% idle (SceShellCore "CPU Top", run menus-on-02 (2026-10-16)).
Moving game helpers there is the user's call (pending); the fallback is telling games the CPU count their helper has.

### Where a helper's 0xea00 set comes from (2026-10-17; proven and inferred)

**Proven: not the loader, not the title.** The loader's first look at its own scheduling reads the set before it changes
anything: `sched_entry` (`native/proton_wine_child.c:205`) calls `sched_self` → `sched_cpus` (`:139-146`) with
`CPU_LEVEL_WHICH`/`CPU_WHICH_TID`, and in `evidence/clock-text-2026-10-16-01-desktop-session.json` all 15 helpers there
record `entry_cpus: 0xea00` (line 86) and `plain_cpus: 0xea00` (line 89) — the second is a thread the loader makes with
no attributes, so the same value is the process's set, not the main thread's affinity. The only affinity write in the
loader is `sched_apply_self` (`:173-183`), gated on `program_plan.cpus != 0` (`:221-224`), and the same record has
`program_cpus: 0` (line 92) for every helper: it wrote nothing. The title's spawn request carries no CPU at all — the
`/elfldr` form has only `args`, `pipe` and `elf` (`wine/ps5/pw_payload_spawn.h:63-68`) — and the title's own line
(`native/proton_wine.cpp:206-207`, `cpus_allowed` at `:162-172`) is 0x1fff/0xffff/0xffff. A child inherits its
parent's CPU set, so a helper that inherited from the title would hold 0xffff, not 0xea00.

**Inferred: the payload-spawn side gives it.** The helper is created by elfldr (`wine/ps5/pw_payload_spawn.h:47-127`:
POST `/elfldr`, then the rendezvous), not by the title, and the set is already there when the loader's first line runs.
So it is either inherited from elfldr's own process or assigned by the kernel to payload processes; elfldr's own
`cpuset_getaffinity` is not measured, and nothing in this repository sets it. Proving which one needs a census of
elfldr's process (`SceSpZeroConf`) — not taken. What is proven is enough for the fix: widening from inside the helper
is the only place that can undo it, and the loader is the first code of every helper.

### The placement: every game helper on the title's CPUs (2026-10-17; host-tested)

The loader places itself once, before any Wine or game thread exists: `sched_place_cpus` in
`native/proton_wine_child.c` runs ahead of `sched_entry`. With the switch on it reads the helper's affinity and its
set, creates a set under the root set (`cpuset()`), tries to narrow that set to the title's 0x1fff
(`cpuset_setaffinity(CPU_LEVEL_CPUSET, CPU_WHICH_CPUSET, id)`), moves the process into it
(`cpuset_setid(CPU_WHICH_PID, -1, id)`), then narrows this thread to 0x1fff if the moved-into set is wider, and reads
all three back. If the result cannot run on every target CPU it moves the helper back to the set it came from
(`cpuset_getid` before the move), so a refused move never leaves a helper narrower than it was; a refused exact
narrowing that still leaves every target CPU reachable is kept and reported. The decision, the typed failures and the
undo are portable and host-tested (`src/pw_cpu_placement.c`,
`tests/test_pw_cpu_placement.c`: off, already, moved, affinity-only, each failing call, a set no wider, and a failed
undo). The memory helper is left on its service CPUs; it is not a program.

The switch is `WINE_PS5_HELPER_CPUS_WIDEN=1` in the title's environment. `inherited()` in
`wine/ps5/pw_wine_process.c` forwards it in the bootstrap's settings block to every helper, and a helper forwards it to
its own children, so it covers the initial program and every descendant. Unset — the default — the loader changes
nothing but prints `status=off`; that is the build the commit gate runs.

Every helper logs `wine-child: cpus placement status=… before=affinity/set root=… after=affinity/set/root …`, and the
existing census in `pw_ps5_child_start` (`wine-ps5: cpus may run on …`) reads the placed result once Wine runs.

Console, 2026-10-17 (`loader-cpus-check-2026-10-17-01/-02/-03`, a 40 s Mihawk OS session with the switch on, one run
per attempt): the kernel gives a payload only part of the plan.
- `cpuset()` and `cpuset_setid(CPU_WHICH_PID, -1, id)` work unprivileged: each helper moves into a new set, and the
  census changes from `may run on 0xea00, its CPU set holds 0xea00` to `may run on 0xffff, its CPU set holds 0xffff,
  the root set above it 0xffff` (10 of 10 helpers). The title's CPUs 0-12 are now reachable; the set is the same width
  as the title's own set (0xffff).
- `cpuset_setaffinity` is refused with EPERM for both the set-wide mask (`CPU_LEVEL_CPUSET`/`CPU_WHICH_CPUSET`) and
  the exact narrowing (`CPU_WHICH_TID` and `CPU_WHICH_PID`): `set_mask_errno=1 narrow_errno=1 affinity_which=0`, every
  helper, every attempt. So a payload cannot narrow to the title's 0x1fff; the move is the only lever it has. The
  placement keeps the move when the refused narrowing still leaves every target CPU reachable (it does) and reports
  the refusal instead of undoing the widening.
- The first attempt (PID-only affinity, no set-wide mask) had failed with `error=8 errno=1` and moved back; that is
  why the second attempt tries the set mask and the thread first.

## Ended helpers leak the console's TTY(STDIN) slots until no helper can start (2026-10-16, console; fixed 2026-10-17)

run menus-off-02 (2026-10-16): a helper spawn failed (`wine-ps5: child spawn service=0xffffffff`, `child bootstrap
failed errno=5`), and the kernel printed `WARNING: Exceed the max number of process which TTY(STDIN) can be open at the
same time` with its list of 128: 95 entries `!!LEAKED OPEN!!`, pids 384 to 7000, the whole day's, the rest live system
processes and helpers (elfldr starts each payload as a `SceSpZeroConf` process). No earlier run that evening printed
it; the table filled over the day, and only a restart empties it. This is likely the root of the recurring
`child bootstrap failed errno=5` / payload loader `errno=35` spawn failures after a long day of runs.

Which helpers leak, from that evening's own runs (their pids against the list):
- every Explorer the title ended while it ran: the session root unloaded when a game starts (`session root native=…
  unloading`, the control socket shut: 5806, 6897, 6987, which still reached `exit()`; the kernel logged
  `SceSpZeroConf calls exit()` for 6897 and 6987) and the one SIGKILLed when a run stops (5819, 7000: no `exit()` line);
- not the helpers that end through Wine's own process exit (wineboot, services, winedevice, cmd, the games themselves,
  even crashed ones), nor the probes' helpers, nor an Explorer the system tore down when the title closed (6910).
A Wine process exit stops every Windows thread before the loader returns from `main`; an unload or a SIGKILL ends the
process with its threads still running. So a helper ended mid-run seems to keep its stdin registration.

Implemented 2026-10-17 (`native/proton_payload_child.c`, `tty_release_console`, the first call in `main`, before the
rendezvous or any failure path); entry point for the evidence is the loader's own log line, `wine-child: console tty
stat=… dev=… rdev=… mode=… stdin_null=… stdout_null=… stderr_null=… error=…`. It `fstat`s fd 0 (the plan's item 1),
opens `/dev/null` O_RDWR, `dup2`s it over fd 0, and does the same for fd 1 and 2 only when their `st_dev`/`st_rdev`
equal fd 0's — so a log or pipe is left alone. Every exit from the loader passes through it, `take_control`'s failure
return and the final `_exit(0)` alike. What remains open is the plan's deterministic proof (item 3: 110 Explorer
unloads overflow the table without the fix and must not with it): the console slot for this change ran the gate and
one gameplay run, not that scenario.

## The loader stops forcing WINE_PS5_NTSYNC=0 into helpers (2026-10-17)

The loader set `WINE_PS5_NTSYNC=0` in every helper, beside `WINE_PS5_CHILDREN=1`. Since patch 0925 the only reader,
ntdll's `connect_inprocess_server`, refuses the in-process sync device when `WINE_PS5_CHILDREN` is set
(`wine/patches/0925-ntdll-ps5-service-managed-children.patch`, `!getenv("WINE_PS5_CHILDREN")`), and the loader sets
that in the same `set_env` chain before `pw_wine_start_load` loads ntdll (`native/proton_wine_child.c`). The forced `0`
therefore changed nothing and is removed; the name leaves `loader_owned` (the loader no longer sets it), while
`pw_app_environment.c` still keeps a catalog game's `WINE_PS5_NTSYNC` out of a helper's block. The title's own
`WINE_PS5_NTSYNC=1` is untouched: that is the title's Wine, not a helper's.

## The PS5 memory backend (decided 2026-10-15)

Nearly every console hang so far was Wine's memory code (`dlls/ntdll/unix/virtual.c`), written for Linux and macOS, broken
by one PS5 difference after another. It assumed:

- 4 KiB pages; the console's are 16 KiB;
- that `mprotect` grants execute; in a payload it silently drops it (`exec-grant-measure-2026-10-14-01`);
- that its reservations are its own; the kernel places other code's wired GPU memory inside them, and unmapping that
  memory hangs the console (`unwire timeout at 0xc80000000`, 2026-10-13/14 the earlier emulator experiment and 2026-10-15 `late-library`).

Decision (the user, 2026-10-15): keep all of Wine's API surface and give it one deliberate PS5 memory backend, finished
**before** any further emulator run. It is a refactor inside our port, not a reason to drop Wine. Its rules:

1. **One choke point.** Every host memory call a helper makes (map, unmap, protect, reserve) goes through the backend
   (`counted_mmap`/`munmap`/`mprotect` → `__wine_ps5_*` → the engine in `wine/ps5/pw_wine_dmem*.c`). Nothing in the
   helper runtime calls the kernel's memory functions directly; a path found doing so is moved behind the backend.
2. **Ownership.** The backend unmaps, maps over and re-reserves only what it made. A kernel entry with protection inside
   a range Wine thinks is reserved is foreign: it is left alone, kept out of the free ranges, and reported (patch 1128,
   `pw_wine_vm_foreign`, the engine's holes; host-tested in `test_wine_virtual_guard`, `test_pw_wine_dmem_*`).
3. **16 KiB is the unit.** Windows' 4 KiB semantics are emulated on top: write tracking per 4 KiB page (patch 1122,
   the strictest protection of a host page's committed pages), with a helper's stores beside read-only pages run
   through the 4 KiB MMU (patch 1134, docs/MMU4K.md; built and host-tested, not yet run on the console).
4. **Executable means JIT shared memory.** Code is converted at its first execute fault (patch 1127); no grant through
   the payload SDK's unlocked kernel write ever (the loader is linked with it wrapped out and checked by disassembly).
5. **A fake kernel for every crash.** Host tests model the console's kernel as measured (`tools/dmem-share-probe.c`
   exec/exec4, K1-K10: reservations show protection 0, execute dropped except on JIT objects, a JIT object maps
   MAP_FIXED only at its exact size, no pread/pwrite on it, foreign wired entries inside reservations), and replay
   each past console crash as a permanent test.

Done 2026-10-15, by file (host-tested: `tools/host-tests.sh` 94 passed; `tools/build-production.sh` built it, gate
fingerprint `395c99ae142e365e`; **no console run**, so none of it is proven on the console):

- `wine/patches/1129` (`virtual.c`, `loader.c`): `NtLockVirtualMemory` wires nothing, the address pool's give-back
  stays reserved, MEM_RESET keeps the pages, a launcher client library's `.text` protection goes through `NtProtectVirtualMemory`.
- `wine/patches/1130` (`win32u/vulkan.c`, `virtual.c`): before a placed GPU mapping the range goes back to a bare
  reservation the engine forgets, through ntdll's `__wine_ps5_placed_range_bare`, under the virtual mutex with Wine's
  signals blocked (a thread suspended while it held the engine's spinlock would stop every other).
- `wine/patches/1131` (`win32u/opengl.c`): a helper (`WINE_PS5_CHILD`) never starts the AGC OpenGL stack, whose
  `eglInitialize` mapped wired GPU memory where the kernel chose; a helper's OpenGL is Mesa/Zink.
- `platform/graphics/patches/0013` (GPU client `client.c`): a buffer is mapped only over the client's own bare
  reservation of a host range, never where the owner's address points; the first eight refusals are printed with the
  entry found (`gpu-client: ... not bare at`).
- `platform/graphics/patches/0014` (`client.c`, `client.h`): every host-visible buffer's range is laid back (or
  unmapped) and its source dropped before FREE, since the owner hands the range out at once (another thread's new
  buffer was torn down); a placed mapping is direct memory only (shared memory over a reservation is invisible to the
  engine), so no descriptor is kept.
- `wine/ps5/pw_wine_dmem_ps5.c` (glue): no GPU bits in a payload, a GPU-bit check before a fixed mapping, an unmap or a
  conversion outside the regions, the heap area reserved exactly at its hints and not at the GPU client's range, a
  helper whose direct memory fails its self-check ends, file-region exports round to 16 KiB.
- `wine/ps5/pw_wine_pages*.c`: a page not laid back stays lost; a helper without a range maps none.
  `wine/ps5/pw_wine_heap.c`: never maps by itself once backed.
- `native/proton_payload_child.c`, `wine/ps5/pw_wine_prx.{c,h}`, `wine/ps5/pw_wine_dl_libc.c`: the loader wires every
  module segment it cannot write, text and read-only data (`pw_prx_wired`: both are paged from the fake-signed SELF;
  `.rodata`/`.eh_frame` evicted is the same "pager error" as text), and ntdll unwires that same set before an unload
  (`pw_prx_unwire`).
- Tests: the shared fake kernel (`tests/fake_ps5_kernel.{c,h}`; a module now has text, read-only data and data) and 23
  crash replays on it (`tests/test_crash_replays.c`: the real engine, glue, pages, heap, GPU client, and code extracted
  from the applied trees, virtual.c's and the broker owner's `shared_object`), each checked to fail with its guard taken
  out of a scratch copy; this round's: the free order, placed shared memory, the refusal print, the map's bare check,
  the uninterrupted section, `shared_object`'s refusal when the maker is gone, the read-only unwire. The callers the
  replays cannot compile are source checks: `tests/test_module_wiring.py` (the loader uses `pw_prx_wired`,
  `stop_unload` unwires first), `tests/test_helper_opengl.py` (both OpenGL gates, evaluated per environment),
  `tests/test_placed_unmap_order.py`.

On the console, 2026-10-15 (no hang in any run):

- `v1-backend-2026-10-15`: payload-vm, image-protect32/64, large-shared-section, pe64, pe32, wow64 passed (foreign
  mappings found 0; convert_failed, exec_refused, sdk_refused 0). gpu-window64 and dxvk-window64 failed only on
  declined captures: the broker held the request under the display lock, which a present holds throughout. Graphics
  patch 0015 gives the request a lock of its own; `v1-backend-2026-10-15-02`: both passed.
- `v2-backend-gate-2026-10-15`, every gate case but late-library: 65 of 71 passed. Four failures were the reset
  console: prefix-fresh64/32 and launcher-tls (FTP 550, `/data/ps5proton/games` absent before setup S9) and
  desktop-view (it expected a launcher's shortcut on the Public Desktop, there only once the game launcher is installed in the prefix). Two
  were this round's:
  - virtual-query32 found no free block below 16 MiB (16 before). A log in `__wine_ps5_vm_gap` named it
    (`v3-gap-2026-10-15`): Wine's read-write DOS area, 0x10000-0x110000, read as a mapping not Wine's. Wine patch
    1132 keeps it a bare reservation.
  - host-dmem-fallback64: a WoW64 child's DXVK device failed, "Failed to map Vulkan memory:
    VK_ERROR_MEMORY_MAP_FAILED", because 0014 refused to place shared memory. Now a range win32u lends to a placed map
    is foreign by record (`__wine_ps5_dmem_lend` from 1130, dropped at Wine's release in `__wine_ps5_dmem_keep` and
    `__wine_ps5_munmap`), whatever the kernel shows, and graphics patch 0016 places shared memory again; the "placed
    shared memory" replay now checks the placement, a refused protection change and the release.
  - `v4-lend-2026-10-15` (fingerprint `89e5a8b110aa4fd8`): virtual-query32 (reserved=16), host-dmem-fallback64 (20
    made, 63 refused, 6 captures), host-dmem64, gpu-window32, dxvk-window32 all passed.

- The deep read-back (console-setup's S12 then, S10 now) flagged 477, then 373 files. None was damage: each was a module of the probe
  prefix's system32 that the console's wineboot had replaced with the runtime's build of the same name (byte-equal),
  where staging had shipped the prefix template's older build. `tools/stage-wine.py` now copies the runtime's modules
  into the template's system32; `console-setup-2026-10-15` --check --deep: 5935 files match.
- `late-library` alone, the direct proof (`v5-latelib-2026-10-15`, then `v9-exitfix-2026-10-15`): no hang. The entry
  that hung the console reads `0xc80000000-0xc80048000 protection 0x33 type 0 flags 0x11 (NoName)`, 288 KiB of wired
  GPU memory the kernel placed inside Wine's reservation once the helper loaded vulkan-1.dll; the release went around
  it (17 spans, then 1: that entry), and the probe passed.
- The helper then died at exit: SIGILL at the loader's 0x398, the SDK crt's trap after libc's `exit` came back, and a
  core dump. The same trap ends nearly every earlier emulator experiment run since 2026-10-13. Marks in the exit path (`v6`/`v7`/`v8-exit`)
  showed it: in that GPU helper the destructors ran but libc's last exit handler never did, and `exit` returned to the
  crt (the 13 helpers of gpu-window64 ran every handler). Wine has killed its threads asynchronously by then, so the
  helper now ends with `_exit` (`native/proton_payload_child.c`). `v9-exitfix-2026-10-15`: late-library (result
  finished, PASS, the hole kept), gpu-window64 and pe32 passed; no fatal signal and no core dump in any log.
- **A create_view assertion** (2026-10-15, a run of the earlier emulator experiment; record removed from the tree; in
  git history): a 20 MiB section view at 0x2002c4000, after a 16 KiB one at 0x2002c0000, left 0x2002d0000-0x2016c0000
  listed free (Wine patch 1133 fixes the lookup; a replay holds it).
- **White glyph blocks** in the earlier emulator experiment's workload (2026-10-15): 1123's neighbour fault reports
  (5,000-6,000 a 10 s window) made the program re-upload stale guest bytes over GPU-written images; the cause and its
  fix are in [MMU4K.md](MMU4K.md) section 1. A run with file-backed guest sections (`PROTON_SECTION_DMEM=0`)
  **dropped the console** (the klog went silent within about 20-40 s of the first frame, the flight recorder stopped
  at t=27 s with the verdict "fine"); file backing must not be used for such a workload again.
- desktop-view no longer needs the game launcher in the prefix: the probe makes its own Public Desktop shortcut (to notepad.exe,
  with its icon location) and removes it before the item counts.
- The project licence is GPL-3.0-or-later (the author's decision, 2026-10-15; `LICENSING.md`).

Remains:

- What makes the kernel place 288 KiB of wired GPU memory in a helper is not known (the GPU client opens no GPU device
  and maps only at fixed addresses).
- Wiring read-only module data is reasoned (its SELF entry is blocked like text's), not measured; the loader's
  `locked=` line counts the segments wired.
- The older module tests (`test_pw_wine_dmem_ps5`, `test_pw_wine_vm`, `test_wine_virtual_guard`, the pages and section
  tests) still use fakes of their own.
- A laid reservation the kernel ignores still falls back to unmap and reserve (an anonymous PROT_NONE mapping would
  replace in one call but is charged to flexible memory).
- Outside the one choke point, with no ownership risk found: the create-only reservations in `anon_mmap_tryfixed` and
  `mmap_init`, the USD `mprotect` in `signal_x86_64.c`, `free_reserved_memory` unmapping about 2 GiB of bare low
  reservation at start-up, before the GPU starts.
- The pinned Lapy root daemon (`lapy-root-daemon.elf`, a payload of its own, not a helper) still links the SDK's so
  loader, which calls `kernel_mprotect`; every helper ELF and PRX built here calls neither SDK grant.
- `tools/build-payload-runtime.py` (not part of the production build) relinks `pw_wine_prx.o` from its base build: a
  base built before this round lacks `pw_prx_unwire`.
- Unmeasured fake-kernel assumptions: an anonymous PROT_NONE mapping reads as a bare reservation; `mprotect` keeps an
  entry's GPU bits; a no-address placement may land inside a reservation; the 1 GiB `FK_BIGBARE` threshold.

## Hang safety, commit A: contain and observe (2026-10-15; built and host-tested; this version not run on the console)

run mmu4k-01 (2026-10-15) froze the console with the GPU broker's client lock held at every flight sample from t=40 to
t=278 (busy:239, fences 6881/6881) and nothing recording who held it, or Wine's virtual mutex, which the helper's census
waited on for more than 160 s. The earlier emulator experiment's session before salvage-20261015-092043 held the same lock from t=75 to its last
row at t=328.7 (busy:252, fences 21073/21073). Commit A ends such a game and names the stuck call; it does not change
the 4 KiB MMU's engine (that is commit B: the exit hand-over, no runs while exiting, the heap's signal mask).

- **A1, the title ends a wedged game** (`src/pw_hang_breaker.{h,c}`, `native/proton_flight_recorder.cpp`,
  `native/proton_wine.cpp`): the client lock found held at 20 one-second samples in a row spanning 19 s or more (the
  span too: a recorder that fell behind takes the samples it missed back to back, 32 rows at once in that earlier emulator experiment's log)
  writes `wedge broker_busy_s=N`, and the title ends the helpers by SIGKILL and returns to the launcher. Healthy logs
  peak at 2, the two device losses that cleared at 15 and 16. Limit: each ended helper's GPU close still waits on that
  lock, so a wedge that never clears keeps the title from being replaced; the title log says it waits, and
  `pw_ps5_wait_children` names the slots still open every 30 s.
- **A2, the broker names its stuck call** (graphics patch 0017): an atomic phase, start and holder thread around each
  call that can wait under the lock, `phase=<name>:<ms> tid=<n>` in the flight log, and the test hook
  `PROTON_TEST_BROKER_HOLD_S`. The client gives the broker up for good in one place (no answer in 15 s, the connection
  gone, a malformed reply, a free not taken back): the first time only, it writes one line saying why and stamps the
  helper's GPU reason (A3) through `pw_ps5_child_doomed`, looked up once at bind with the lookup ntdll's module loader
  hands it as it loads (`wine/ps5/pw_wine_dl.c`, `pw_module_lookup`; the client's own dlsym is libkernel's).
- **A3, a helper ends itself** (Wine patch 1135, `wine/ps5/pw_wine_process.{h,c}`, `src/pw_control_watch.h`,
  `native/proton_wine_child.c`): the GPU broker's client stamps GPU when it gives the broker up (A2),
  NtTerminateProcess of the process stamps EXIT, and the loader's control thread ends the process 5 s (GPU) or 20 s
  (EXIT) later unless the title closed it first. That thread runs above the program's threads and never below where
  `WINE_PS5_HELPER_PRIORITY` put it. `WINETEST_DOOM_AFTER_MS` is the test. GPU was first stamped by win32u at any
  VK_ERROR_DEVICE_LOST of a host submit, submit2 or present: in gate-hang-a-2026-10-15-01 (72 of 75 passed)
  gpu-window32 and gpu-window64 each logged `payload doom seen reason=gpu` when the broker answered the first three
  (32-bit) and two (64-bit) window presents with status 70 (ESTALE), and both programs presented on (40 and 39 frames)
  and ended by themselves about 2 s later. A refusal the broker answers is no lost broker; a game that rebuilds its
  window or swapchain, or recreates its device, would have been ended.
- **A4, the virtual mutex names its holder** (`wine/ps5/pw_wine_perf.{h,c}`, patch 1135's `mutex_lock` hook,
  `pw_wine_dmem.c`'s holder stamp): owner, held time, caller, the holder's tag (fault, alias ops, pager map and ask),
  waiters, acquisitions, owner changes, the longest hold. The helper's log thread writes a stall line every second
  while the mutex or the engine's lock has been held 2 s, or threads waited at three looks in a row with waiters getting
  through; the census skips image_stats while one owner has held the mutex 1 s.
- **A5, battery** (`tools/run-battery.py`, `tools/run-console.py`): `doom-backstop` is in `GATE_CASES`;
  `broker-wedge64` and `gpu-loss` run on request. In `broker-wedge64` the probe's GPU stamp (its client's give-up 15 s
  into the hold) and the wedge kill are due at about the same moment, by design, and either may end the helper. In
  `gpu-loss` the broker answers a submission with a failure, which is no lost broker: no backstop may end a helper.

Host: `tools/host-tests.sh` 96 passed; `tools/build-production.sh` built it, gate fingerprint `e6a8256b0c577838`.
Console: the first version ran in gate-hang-a-2026-10-15-01: 72 of 75 passed (doom-backstop and vm-truth32/64 among
them; 1,250 engine runs across the battery, no engine fault, no stall line). The three failures:
- **window-effects64**, a race in the probe. Its root capture, which Mica's reference is computed from, was taken when
  Explorer's shell window existed but before Explorer had painted the wallpaper: white but the icons (mean 254.7 of
  255). The Mica it drew matched gate-2026-10-15-02's capture pixel for pixel. `tests/window-effects-probe.c` now
  retakes the root (same number) while the frame is one flat colour, for up to 10 s, and logs the retakes.
- **dnsapi32/64**, the network: `DnsQuery_A("example.com")` got SERVFAIL (9002) for A, AAAA, TXT and TCP from the
  console's resolver. An hour later the router answered all four NOERROR from the PC.

**This version has not run** yet, so none of it is proven on the console. The runs are queued in
[CONSOLE_QUEUE.md](CONSOLE_QUEUE.md), "Hang safety, commit A".

## Hang safety, commit B: the 4 KiB MMU hands the virtual mutex over (2026-10-16; host-tested; no console build or run)

A 4 KiB MMU run held Wine's virtual mutex and every 256 instructions unlocked and at once relocked it, which handed it
over 0 times in 50 on the host and emptied the run's page table each time (about 89,600 times per 10 s at
run mmu4k-01 (2026-10-15)'s rate), the mutex run mmu4k's census waited on for more than 160 s.
- **B1** (`wine/ps5/pw_wine_mmu4k.{h,c}`, patch 1136): a run holds the mutex from its start to its exit and every 256
  instructions reads commit A's waiter count (`pw_wine_perf_vlock_waiters`); when a thread waits it ends with the new
  exit `yield` and the program resumes natively. The census line gains `yield:` (nine exits). Who takes the mutex
  then is the mutex's and the scheduler's choice: on the host a blocked thread took it first 49-50 times in 50 when
  its holder relocked 80 us later (about a run's exit and next fault), 0 in 50 at once; not measured on the console.
- **B2** (patch 1136): `ps5_mmu4k_wanted` starts no run once `process_exiting` is set, and a run that asked just
  before takes and gives the mutex with plain `pthread_mutex_lock`/`unlock` from then on (`mutex_lock` does nothing
  after the flag), so runs still exclude each other.
- **B3** (patch 1136): a fault inside the engine (NOT_OURS, the run's lock never released) stamps
  `PW_CHILD_DOOM_ENGINE`, so the loader ends the helper 5 s later.
- **B4** (`wine/ps5/pw_wine_dmem_ps5.c`): the heap's direct-memory calls run with SIGUSR1, SIGQUIT and SIGINT blocked.

Host: `tests/test_pw_wine_mmu4k.c` (M8 one lock per run; M8-yield 78 hand-overs at exactly 256 instructions, results
as native; M5 and M6 at the hand-over), `tests/test_crash_replays.c` (the heap case fails with the mask taken out),
`tests/test_pw_wine_perf.c` (the hand-over rates, printed), `tests/test_module_wiring.py`. Not built with `tools/build-production.sh` and not run on the console: queued in
[CONSOLE_QUEUE.md](CONSOLE_QUEUE.md), "Hang safety, commit B".

## Broker staging reuse (graphics 0018) (2026-10-16; host-tested; not run on the console)

run ab-01 (2026-10-16) (the emulator workload in the earlier emulator experiment, build `57ead6f6f8514184`): the flight recorder found the broker's client
lock held from t=34 to t=48 (busy:1 to 15) in phase `reap-free`, the free of a submission's staging (0017's phase
record), with fences 8125/8125 and flips +0. In the same seconds the helper's stall lines (held_ms 2829 to 14900) showed
one thread holding Wine's virtual mutex and the direct-memory engine's lock at NtProtectVirtualMemory, that is
`__wine_ps5_mprotect` → `pw_wine_dmem_protect` → `sceKernelMprotect` of direct memory (the earlier emulator experiment's write watches), in the
kernel; both began within about 0.1 s. At 15 s the helper's GPU client gave the broker up, the earlier emulator experiment got ErrorDeviceLost
and exited, and only then did the free return (t=49). The broker had made and freed one staging buffer per submission:
8126 in that session, about 420 of each a second (fences 6842 to 8100 from t=30 to t=33).
- **Graphics patch 0018** (`platform/graphics/patches/0018-the-broker-keeps-its-staging-and-takes-it-again.patch`, the
  foundation's `tooling/gpu-broker/staging.h`): reap keeps a staging buffer whose fence has passed as a spare, and submit
  takes a spare of its own rounded size (64 KiB at least, powers of two to 2 MiB, then 2 MiB steps), with no kernel
  call. A buffer is made only when every one of its size is in flight, so per size never more than were once in flight
  together, and a small submission never holds a large buffer. The spares hold at most 32 MiB; reap frees a buffer past
  that (phase `reap-free`), otherwise they are freed only in `fences_reset` at the broker's stop. The stop writes
  `gpu-broker staging allocs= frees= reuses= refused= peak_bytes= held_bytes=`: the next run shows whether the churn is
  gone (`refused` counts frees during play).
- Host: `tests/test_pw_gpu_staging.c` drives the bookkeeping as the server does: 252,000 submissions (4 KiB mostly,
  1% up to 1 MiB, 0.01% at 16 MiB) with 1 to 40 in flight made 51 buffers and took a spare 251,949 times, refusing
  none; the spares held 25.3 MiB at the end (26.8 MiB after a modeled hour). The first form (smallest spare that fits,
  replacing the largest when none did) let small submissions hold the large spares: the same model held about 262 MiB
  (279 MiB after an hour, 14 buffers of 18 MiB) until the stop, found in review and fixed before any console run.
- **Unproven**: this removes the title's side of one collision, not the kernel's cause. Why the title's release of its
  own direct memory waited while the helper's `sceKernelMprotect` of direct memory was held up is not known, nor that
  one waited on the other. The title still releases direct memory during play: reap's release of deferred client
  buffers (`release(fences.deferred[i].buffer)`, unchanged), `free_buffer`, and client allocations; they follow the
  game's buffers, not its submissions (35 host-visible buffers made and freed in run ab), and are the next suspects if
  the stall recurs (phases `munmap`, `give-back`, `alloc`). Not run on the console.

## Kernel mprotect stall (2026-10-16; host-tested and built; no console run)

What the records show (logs only; no new console run):
- **The stall.** Six emulator-workload runs of 2026-10-16 had one emulator thread holding Wine's virtual mutex and the
  direct-memory engine's lock in NtProtectVirtualMemory until the helper died. The site is 0x8004d8adb, the return from
  `server_enter_uninterrupted_section` by an inferred ntdll load base. The last stall lines came at 14.9 s (run ab),
  44.9 s (run win48), 53.0 s (run giveunmap), 56.2 s (run doing), 60.2 s (run ranges) and 62.5 s (run 0018).
  `wine-child: stall` lines are in those runs' helper logs under `klog/` (not committed).
- **The call.** Between the engine's `doing(PROTECT)` and its unlock, the only call is `sceKernelMprotect`. The op
  record named protect of 0x21578c000+0xc000 (48 KiB) in both run ranges and run doing, and protect of
  0x201804000+0x4000 in run win48.
- **Ruled out as necessary:**
  - An alias window: none covered 0x21578c000 in run ranges.
  - Eviction: run win48 never evicted a window and stalled.
  - The broker's staging churn: graphics 0018 removed it in run 0018, and that run stalled for the longest.
- **The clean run.** run noevict-01 (2026-10-16) (16 windows, no eviction) ran 469 s with no hold of Wine's virtual mutex
  over 157 ms (`longest_ms`; 25 ms in gate-hang-ab-2026-10-16-01). The engine lock keeps no longest hold: no look, 1 s
  apart, saw it held 2 s in either run (run noevict's 128 stall lines are waits, with the engine held 1 ms at most), so
  its clean holds stayed under 3 s; the margin below that is not measured. It played at only 2 to 3 fps (20 to 31
  frames shown per 10 s).
- **The cost.** During the stalls the Shell UI lost its heartbeat (SceLncService), and the helper's unnamed
  priority-700 threads used 95%, 40% and 40% of a CPU (CPU Top, run ranges). In run win48, SceShellCore force-aborted
  JSCd after 31 s. Everything recovered once the helper died.

The changes:
- **F1, containment** (`wine/ps5/pw_wine_process.h` `PW_CHILD_DOOM_STALL`, `src/pw_control_watch.h`
  `pw_control_stalled`, `native/proton_wine_child.c` `stall_watch`).
  - The loader's log thread stamps STALL once per helper when either lock has been held 3 s in one hold. The virtual
    mutex's half leaves out the pager's ask for a chunk (`pager-ask`, a wait of up to 10 s for the title by design) and
    every hold once EXIT is stamped (after `process_exiting` Wine's unlock no longer reaches the record, and EXIT's own
    20 s bound the exit).
  - The control loop then ends the helper 5 s later, 8 to 9 s into the hold, with no Wine lock and no fsync, as for the
    other reasons.
  - `tools/run-console.py` fails any run in which a helper saw `stall`, and keeps it in the evidence (`helper_doom`).
- **R-A, thread records** (`stall_threads`), written at the stamp and once more 2 s later.
  - One `wine-child: stall thread tid= stat= wmesg= wchan= lock= cpu= runtime_us= delta_us= pri= name= pass= holds=`
    line for each thread of the helper (`KERN_PROC_PID | KERN_PROC_INC_THREAD` on its own pid). The lock owners are
    marked in `holds=`.
  - Then a `wine-child: stall threads pass= records= sysctl_errno= bytes= self_tid=` summary. `records=0` means the
    kernel listed no threads; a payload got none for another process before.
  - The records use a static buffer and take no lock. They are written after the stall line is synced.
  - `ki_tid` is unverified on the console. `self_tid` is there to find this thread among the records.
- **F2, waiters stop burning a core** (`wine/ps5/pw_wine_spin.h`, used by the engine's lock and the alias module's).
  - A waiter spins 1000 pause loops, then yields once, then sleeps 50 us per look.
  - The holder stamps are unchanged.
  - the earlier emulator experiment's own SpinGuard waiters are not covered.

Host: `tools/host-tests.sh` 97 passed.
- `test_pw_control_watch`: the stall rule, and the STALL deadline against EXIT and GPU.
- `test_pw_wine_dmem_regions`: 4000 contended protects with no overlap. Behind a 200 ms hold, the waiter used 2.3 ms of
  CPU. The same test fails with the old pure spin.
- `test_run_console_verdicts`, `test_module_wiring`: doom names in enum order, and no lookup, allocation or lock in the
  stall path.

`tools/build-production.sh`: gate fingerprint `a7177890cc69053a`.

Not run on the console; console work stays stopped until the user clears the storage errors.

Still unknown:
- **H-A:** what the kernel's mprotect of granted direct memory waits on, and whether the title's "dmem" sleepers wait
  on the same thing.
- **H-B:** whether a starved lower-priority thread holds that resource. F2 removes our engine lock's spinning, not
  the earlier emulator experiment's.
- **H-C:** whether another mapping change in the helper left an entry busy.
- Why the 4 KiB MMU builds stall more often.

The R-A lines of one stalled run decide among H-A, H-B and H-C, as the table in the 2026-10-16 synthesis sets out:
- The owner sleeping on the same wmesg as the Shell and the title points to H-A.
- `vmmaps` points to H-C.
- The owner runnable with flat runtime points to H-B.
- Runtime rising about 1 s per second points to a kernel livelock.

**Console results, 2026-10-16** (0 storage damage in each):
- run stallrec-01 (2026-10-16) (build 0933396603712473): the stall came ~40 s in
  (`protect:0x201850000+0x4000:1`, this time inside a live alias window). STALL was stamped at 3 s and the helper
  ended itself 5 s later ("payload doomed reason=stall ... ending without Wine"). The Shell UI's heartbeat gap was
  4 s, there was no JSCd abort, and the title came back. **F1 works.**
  - R-A got nothing: `sysctl KERN_PROC_PID|KERN_PROC_INC_THREAD` on the helper's own pid answered ENOENT with 0 bytes.
    A payload cannot list threads that way; a different route is needed if that record is wanted.
  - The game still burned 1.9 CPU-s/s while stalled with F2 in, so the spinning is not only our lock: the earlier emulator experiment's
    SpinGuard busy-waits on its own.
- run prio767-01 (2026-10-16) (same build, `WINE_PS5_HELPER_PRIORITY=700,767`: the program's threads RR 767, the
  lowest, the loader's main thread RR 700): **no stall in 463.5 s.** PASS verdict, 19 captures, 22-57 fps (38-57 in
  the first 200 s, 22-26 later in play).
  - The seven runs before it at the default (program threads RR 700) all stalled within 83 s.
  - So H-B fits: the program's threads level with the console's own starve something the kernel's mprotect waits for.
    Which thread that is is still not recorded.
- **Default changed** (src/pw_helper_sched.h `PW_HELPER_SCHED_SERVICE_DEFAULT` 700, `PW_HELPER_SCHED_PROGRAM_DEFAULT`
  767): the tested configuration, now used whenever the setting is unset.

## The gate's time (2026-10-16; on the console: 76 of 76 in 1,647 s, was 2,452 s)

**Result:** [gate-2026-10-16-grouped-01](../evidence/gate-2026-10-16-grouped-01.json) passed 76 of 76 in 1,647 s on
build `a97544193eeb2e53`, 27.5 min instead of 40.9 (gate-hang-final-2026-10-16-01, 2,452 s, 73 of 76: dnsapi32/64 on
the console's resolver then, wow64 on a sleeping DualSense; all three pass here). The 73 cases that passed there pass
here. No per-case gate of this same build was finished: the user stopped
[gate-2026-10-16-grouping-01](../evidence/gate-2026-10-16-grouping-01.json) (the build before memory-classes and
launcher-tls changed) after 10 of 76 cases, all passed, to have the fast gate.

Where the time went ([gate-timing-review-2026-10-16-01](../evidence/gate-timing-review-2026-10-16-01.json), from the
old run's records and helper logs):
- **A short case is its launch.** virtual-query64's 14 s: Wine's boot (wineboot --init and its services) 5.9 s, the
  probe 0.3 s, Wine's teardown 0.4 s, and the launch, LoadExec, the launcher's ten frames and its close the rest.
- **The four over 100 s were mostly not their own checks.** launcher-tls (177 s): its probe 0.6 s; 122 s making its own
  fresh prefix (wineboot 106 s, Mono's install 70 s of it) and 47 s deleting that prefix file by file over FTP.
  prefix-fresh64/32 (189 s each): making the prefix is what they test (122 s, the probe 11 s), plus 48 s of deletion.
  memory-classes (171 s): 156 s were thirteen fixed 12 s holds so the title's census could sample each memory class;
  nothing in the gate read those samples (its verdict is the probe's exit status and run-console.py's checks).

What changed (the user's go-ahead, 2026-10-16: "make me faster gates"):
- **Grouped launches** (`tools/gate.py run` passes `--grouped`): the cases of each group in `GROUPS`
  (`tools/run-battery.py`) share one launch; `tests/gate-sequence-probe.c` runs each as its own Windows process (its
  own helper), brackets it with begin/end lines and stops the launch through the title's stop-game when one overruns.
  Each case is judged on its exit status, its marker's first verdict in its window, 15 s at most from verdict to exit,
  and every check of the shared launch. On the console: `probes`, 34 cases in one 104 s launch (708.9 s as launches of
  their own; Wine's boot 8.4 s, the probes 89.6 s, the return 3.8 s), and `graphics`, 5 in 42.5 s (107 s). Everything
  with its own runner, environment, layout, prefix or verdict rule keeps its own launch (37 cases), the lifecycle
  checks with it.
- **memory-classes** runs with `--hold-ms=0` (3.2 s, in `probes`); the 12 s holds stay its default for a census run.
- **launcher-tls** runs `C:\tls-probe.exe` in the probe prefix with the run's root beside it (`tools/run-tls-fixture.py`):
  16 s instead of 177 s; the root is still removed by the probe and checked, the file by the runner.
- **`stages`** in every run-console.py record. Over this gate's 36 launches of their own: wineboot 389 s (5.6 s each),
  the programs 448 s, Wine's end 50 s, LoadExec to the launcher 54 s, the launch to the title 18 s.
- The per-case form never checked an exit stall for a launcher-layout probe (its verdict is only in the helper's log,
  read after the run); the grouped form does, and every grouped case passed it.

Left, measured: the fresh prefixes' FTP deletion (46 s each); wineboot's 5.6 s in every launch (every game pays it too);
the first case after a deploy paid 46 s more after its run (shell32 101 s in both of today's gates, shell64 next to it
41 s), not looked into; 16 s between wineboot's end and the program's image in a fresh prefix, no Wine trace in between.

## Stability round after the 2026-10-14 hangs (built and host-tested; no console run)

The goal: heavy programs run in MihawkOS without crashes or freezes, and nothing we run
damages the console again (it was factory-reset twice after hard hangs). The review plan and its critique are in
`klog/salvage-2026-10-14/review/` (local, ignored). Everything below is in the working tree, passes `tools/host-tests.sh`
(89) and builds with `tools/build-production.sh`. **Nothing of it has run on the console** (rule 7), so it claims nothing.

- **Memory helper, bounded** (`src/pw_memory_helper.h`, `wine/ps5/pw_wine_dsection.h`): every request has a 5 s deadline
  (30 s for the first census); a missed one marks the helper dead for the session and every later call fails with
  `ENOTCONN` at once. The title stops at startup when the helper does not answer, and never makes that memory itself.
  Direct-section chunks are freed by a collector once no admitted process lives (patch 1126). The deadline is not
  measured: the gate's `memory helper slowest answers` line calibrates it.
- **Execute through JIT memory** (design: `klog/salvage-2026-10-14/jit/merged.md`, local; written after the rest of this
  round, through `tools/host-tests.sh` (89) and `build-production.sh`). `exec-grant-measure-2026-10-14-01`
  showed that in a payload `sceKernelMprotect` returns 0 on direct, flexible, file and granted memory but drops execute
  (0x1/0x3), and leaves PRX text at 0x4. So a helper never asks the payload SDK for execute (its grant writes the kernel's
  vm_map entry without the map's lock): the loader links `kernel_mprotect` and `kernel_set_vmem_protection` wrapped to a
  counted refusal, and its build fails if the disassembly still calls either; PRX text keeps 0x4 with no protection call
  and is `mlock`ed; Wine copies a range into its own JIT shared-memory object at the range's first execute fault (patch
  1127, `__wine_ps5_exec_convert`). Helpers print `executable protection refused= conversions= converted_kib=
  convert_failed= exec_refused= grant_retries= modules= sdk_refused=`; a run fails when `convert_failed`, `exec_refused`
  or `sdk_refused` is above 0. Measured on the console with the kernel alone (the SDK write wrapped out, the disassembly
  checked; V0, `exec-kernel-2026-10-14-02` and `-03`):
  - K1: a JIT object's main fd mapped RW, then `sceKernelMprotect(7)`: 0x7; the code runs and can be rewritten (no W^X flip).
  - K2: RX/RW toggles, 1000 each before and after `close(fd)`, all correct, 31-32 µs each.
  - K3: a live `sceKernelMapDirectMemory` page made R, copied into a new JIT object through a temporary RW mapping of
    it, the object mapped `MAP_SHARED|MAP_FIXED` RW at the same address: contents intact, the direct memory released,
    RX gives 0x5 and the code runs.
  - K4: `MAP_FIXED` of a JIT object over a reservation, an anonymous or a file mapping works only when the object is
    exactly the mapping's size; a larger one fails with 0x80020016. So one object per converted range.
  - K5: `pread`/`pwrite` on a JIT fd fail with ESPIPE (29) (`exec-kernel-2026-10-14-01` failed K3 on exactly this), so
    the copy must go through a mapping. In V0 that scratch was a `sceKernelMmap(NULL)` mapping, placed by the kernel.
    Wine instead maps its scratch `MAP_FIXED` in its shared-page range (`pw_wine_pages_map`, laid back after): a design
    choice that only the startup self-check runs, not measured on the console.
  - K6: with the fd closed the mapping keeps the object and the code runs; `munmap` gives the memory back (4624 → 3600 KiB).
  - K7: 4096 objects of 16 KiB, all executable (69,136 KiB resident while held, 3600 after).
  - K8: execute asked at map time is dropped (anonymous RWX gives 0x3; `sceKernelMapDirectMemory` with 7 fails 0x80020016).
  - K9: PRX text with no protection call, `mlock`ed: 27 of 27 pages resident (`mincore`), still 0x4.
  - Not measured yet (first exercised by V1): a multi-page JIT object mapped `MAP_FIXED` over part of a larger direct or
    flexible mapping; partial `munmap`/`mprotect` of a JIT mapping, and whether a partly unmapped object keeps its cut
    pages resident; one `sceKernelMprotect` over JIT and non-JIT mappings at once (the host model keeps execute per
    mapping).
  - JIT memory is charged to the process's resident flexible memory, which only the title's guard watches, so a
    `PROTON_SYSTEM_MEMORY_LIMIT` above 870 is now clamped to 870 (the earlier emulator experiment runs set 1250, past the console's ~989), and
    `PROTON_SYSTEM_MEMORY_END` above 940 to 940 (1300 had turned the steep-climb rule off).
  - Executable views of shared sections are refused and counted (`exec_refused`) in helpers: file and granted direct
    memory never keep execute, and only private and image views are converted. `section-view-probe` (gate case
    `large-shared-section`) no longer runs code from section views.
  - After the review (host-tested, no console run): a conversion records in Wine's host-protection cache what the kernel
    now holds (a host page opened for a 4 KiB step stayed cached as open, and its store could fault forever); the
    grant retry's twice-in-a-row bound resets after any conversion; a conversion that failed beside a faulting page it
    made executable runs the instruction; outside the regions pages already JIT are never copied or made read-only
    again; the shared-page lock blocks Wine's suspend signals while held, since a conversion takes it under
    `virtual_mutex`.
  - Corrected: `jit-placed` (`exec-routes-measure-2026-10-14-01`) and `jit-own-rwx` 0x7 (`exec-jit-limits-` and
    `exec-jit-pool-2026-10-14-01`), from the probe's exec2/exec3 modes, were **SDK-granted**: the probe's libc `mmap`
    maps without execute and then grants it through the SDK's kernel write. They prove nothing about the kernel. Both
    modes now map through `sceKernelMmap` without execute, ask `sceKernelMprotect`, and print `sdk_writes`; not rerun.

  This supersedes the SDK-grant and 16-retry description under "Why the .NET game does not start" below once V1 passes. The
  console ladder (V1-V5) is in [CONSOLE_QUEUE.md](CONSOLE_QUEUE.md).
- **Ending a game** (`src/pw_hang_breaker.h`): a game that does not close 10 s after stop-game, or that the memory guard
  ends, is ended by SIGKILL to its helpers; the title is replaced only once each is gone (ESRCH). The server terminate
  is an opt-in fallback (`PROTON_STOP_TERMINATE=1`, only after a refused SIGKILL). The hang breaker ends a game when no
  frame was shown for 45 s while the memory helper's census is as old (`PROTON_HANG_BREAKER=0` only reports). A flight
  recorder writes one block a second to `sessions/0/flight-recorder.log`.
- **Session logs** (`src/pw_log_sessions.h`): `/data/ps5proton/logs/sessions/0` is this session (helper logs, flight
  recorder), 1 and 2 the two before; the title says `Wine: session logs status=N`, and a run fails unless it is 0.
- **Wine** (patches 1122-1126): a write to a read-only 4 KiB page beside a writable one in the same 16 KiB host page
  faults (1122); 1123, which offered the program's handler that page before Wine single-stepped the write, is removed
  (its false reports drew the earlier emulator experiment's glyphs as white blocks) and patch 1134 runs those writes through the 4 KiB MMU
  (docs/MMU4K.md; host-tested, the probes pass on the PC, not yet run on the console);
  host-protection leaves come from Wine's heap (1124); a direct-section view maps a 2 MiB chunk at a time with each host
  page's protection (1125); the server hands the title the section's file and frees chunks only when no process maps
  them (1126). An assertion in a helper ends it with `STATUS_ASSERTION_FAILURE` instead of `abort()` under the virtual
  mutex (`wine/ps5/pw_wine_dl_libc.c`).
- **Helper scheduling** (`src/pw_helper_sched.h`): helpers run their program threads round-robin
  (`WINE_PS5_HELPER_SCHED=rr`, the default; `keep` is today's equal FIFO), the loader's own threads at a service tier;
  `WINE_PS5_HELPER_PRIORITY` and `WINE_PS5_HELPER_CPUS` come from the C2 census. The in-title Wine is unchanged.
- **Tools** (`tools/console_guard.py`, `tools/verify-title.py`, `tools/console-setup.py`): storage-damage lines in klog block
  every launch and write; deploys and the probe tools' uploads are bracketed and synced, launches wait 60 s after
  them (test runners' setup writes are not: docs/CONSOLE_DROPS.md item 9); the title is read
  back by sha256 after each deploy and each `new-boot`. After a reset, and on any boot:
  `python3 tools/console_guard.py new-boot` (only after a real restart), then `python3 tools/console-setup.py --check`,
  then `python3 tools/console-setup.py` (steps S0-S9, then the S10 read-back; it reads no config file). After a hang the
  same command salvages the logs (S5) and reads back the integrity (S10) before anything launches.

The console steps, in order, are in [CONSOLE_QUEUE.md](CONSOLE_QUEUE.md).

## Status at 2026-10-12

- **Branch:** `windows11` (from `main`, for [WINDOWS11_PLAN.md](WINDOWS11_PLAN.md)); `main` is the 2026-10-12 promotion of `mihawk-os-own`.
- **Gate:** 72/72 on build `c8ca8a8a` ([gate-2026-10-12-win11-02](../evidence/gate-2026-10-12-win11-02-desktop-view.json)).
- **Done this round:** Windows 11 phase 1, a real desktop (below); and the earlier work: the second game launcher's renderer crash (1004), the real RAM figure (1005),
  registry saved every 30 s, a steep-climb memory guard rule, write merging (1007), and **shared direct memory between processes** (patch 0009).
- **Open:** a Chromium session (the second game launcher, the game launcher) to confirm B3 in its totals, the unexplained swings in download speed, the game launcher's UI and login, the .NET game
  save/reload, DirectX 12 breadth, first-launch setup, and phases 2 to 9 of the Windows 11 plan. See [CONSOLE_QUEUE.md](CONSOLE_QUEUE.md).
- **Publishing:** commits on `windows11` stay local.

## What counts against the console's resident-memory figure (2026-10-12, measured)

[memcap-heap-2026-10-12-01](../evidence/memcap-heap-2026-10-12-01.json) and [memcap-section-2026-10-12-01](../evidence/memcap-section-2026-10-12-01.json)
(`tests/memory-cap-probe.c`, guard off, read from the kernel's `SceShellCore VM Stats` lines):

| Memory | Probe | Result |
| --- | --- | --- |
| Heap: `VirtualAlloc`, served from direct memory | 1,280 MiB committed and touched in 64 MiB steps | system direct memory fell 11,323 to about 10,000 MiB; **RSS stayed flat at 330.8 of 971.8 MiB; swap out 0.0** |
| Section: pagefile-backed `CreateFileMapping`, physical shared memory (flexible class) | 640 MiB | RSS fell from 346 to 279 MiB and **swap out rose from 0 to 181 MiB** |

So direct-memory heap is not held to the "RSS x of ~970" figure; flexible-class memory (sections, libc, stacks) is, and swaps past it. The figure differs per
boot (1,008, 1,065, 962, 967), so it is derived, not constant. Consequences: (1) the guard's census sums the kernel's per-process RSS and so counts what the console counts;
(2) a Wine heap in direct memory costs nothing against the budget; (3) the second game launcher's GPU process shows about 44 MiB of flexible resident memory at 4 to 8 s and **345 MiB at
10 s** (`wine-child: memory resident` in the helper log; direct heap and DLL images do not change between processes, so the surge is neither): a transient of about
300 MiB of flexible-class memory, which pushes the system past the figure (1,002 MiB against 967) and is what the guard ends. What it is made of is not shown yet
(the helper's breakdown line is cut off when the guard ends the process); sections shared with the browser (Chromium's GPU command buffers and shared images) are
the candidate, because they are the flexible-class allocations a GPU process makes. If so, backing sections with direct memory (shared by pid grant, as
patch 0009 does for GPU buffers) is the fix. Not shown: whether `elfldr` and the control payloads die because of the overshoot (they were gone after
run fix-02 (2026-10-12) and `-03`, which both went past the figure, and present after the memory probes, which did not).

## A Unity game shows Update and its launcher's client crashes (2026-10-12)

Causes found, in the order they were found, each from a log and not a guess:
- **`Permissions check failure (2123)`** (the Agent marks the game unplayable; the second game launcher shows Update; "unexpected error while running setup tools" is the client
  showing `BLZBNTBNA00000005` for it). The Agent walks the install folder, reads each file's DACL, sets it, and reads it back (10,500 files per start;
  `+advapi` trace). Three things differed from Wine on Linux: `GetVolumePathNameW` failed for every path (a virtual link named with a trailing slash was not
  followed: `pw_wine_cwd_libc.c`), Everyone had write (the file system reports 0777 and ignores chmod: patch 1107), and a DACL that was set never read back
  (no extended attributes on the console: patch 1108 keeps them in the server's folder). After 1108 the Agent no longer answers 2123 for `hs_beta`;
  the launcher's own entry was still flagged at its first check and the run ended before it settled.
- **The GPU process dies, then the second game launcher does.** Chromium's log, in every session since 2026-10-11: the first three GPU processes exit with code 1 (the memory guard
  ended them: the census is flexible-class resident memory, the kernel's RSS figure, with direct-memory pages not counted (corrected 2026-10-16, see the section above), and a fresh GPU process holds 330 to 417 MiB in its Wine heap), then the software
  renderer's GPU process dies of an access violation six times, and Chromium logs "GPU process isn't usable. Goodbye." The violation: a 5.8 MiB view of a
  section placed above 4 GiB for a 32-bit process, whose code kept the low half (`libcef.dll` + 0x318E992, write to 0x02180000). Patch 1109. With it,
  the software-renderer session ran with no crash and 790 MiB peak (2026-10-12; record removed from the tree; in git history), but its main window is fully transparent: hardware
  acceleration must stay on.
- **Not shown:** whether the console really struggles when direct-memory pages push the RSS past the guard's mark (the one freeze on record, at 1,026 MiB, was
  with flexible pages); whether the Unity game itself then starts. See [CONSOLE_QUEUE.md](CONSOLE_QUEUE.md) for the runs.

## Video through Media Foundation and DirectShow: winedmo over FFmpeg (2026-10-17, console; gate-2026-10-17-mf-03 84/84)

The Direct3D 12 game crashed at New Game: its intro opens a video through Media Foundation, and the console had no media
backend at all. Valve's Wine 1c78596 only demuxes in winedmo; every decoder and converter forwards to winegstreamer,
which is not built. So the media stack is GE-Proton's own "ffmpeg + winedmo only" rework (proton-ge-custom e24b389e,
patches/ge-video-rework), as patches 2001-2062 (62 of 66; the RSX3D shim and three winepulse patches are left out; 2001,
2004 and 2006 rebased, notes in their headers), plus two of ours:

- 2063 (mfreadwrite): a decoder's new output size (1920x1088 with a 1080 aperture) is signalled against the type
  downstream holds. Without it the reader kept 1080 rows while buffers held 1088, and chroma was read 8 rows early.
- 2064 (rtworkq): `MFShutdown` waited for a callback while holding the queues' lock that the callback's last release
  needs. GE-Proton11-7's own Linux build hung there in 5 of 8 probe runs.

winedmo's Unix side is `winedmo.prx` (19.2 MB): FFmpeg 8.1.3 (`tools/build-ffmpeg-ps5.py`, tarball pinned by SHA-256),
static, LGPL-2.1-or-later only (the script stops unless FFmpeg's `config.h` says so), configured with GE's components
exactly: every demuxer, decoder, encoder and bitstream filter, VVC off, no parsers. Not "plus parsers": with a VC-1
parser present winedmo hands WVC1 to `av_parser_parse2`, which never completes a frame from ASF packets, and the Direct3D 12 game's VC-1
intro stayed black (run mf-trace-01 (2026-10-17): 1809 packets in, 0 frames out). Six libc gaps are
`wine/ps5/pw_winedmo_libc.c`.

- **Host oracle** (`tools/check-media-host.sh`: the pinned Wine built for Linux with this tree's 29 media modules, its
  wine.inf and a Linux winedmo.so from the same sources and FFmpeg configuration): `tests/mf-video-probe.c` on a
  generated 1080p60 H.264 High + AAC clip (`tools/make-mf-video-clip.py`) 22/22 checks in 64-bit (146 fps) and 32-bit
  WoW64 (128 fps): NV12 bit-exact against FFmpeg's own decode (luma and chroma CRC), synchronous and asynchronous
  reader, RGB32 mean colour, AAC length and level. A public WVC1-in-ASF sample (pinned by hash, fetched for the host
  only, never staged) 17/17 in both, 1451 of 1451 frames bit-exact, 248 fps. The probe on the Linux host sometimes ends
  with SIGSEGV after its verdict: that build's SIGQUIT at process end reaches a thread already in `pthread_exit`; not
  media code, no verdict changes.
- **Console** (build 7f920767, 68b30d4 + the Direct3D 12 game's d3d12 diff + this): `mf-video64-2026-10-17-03` PASS 22/22, 105.4 fps
  decode; `mf-video32-2026-10-17-03` PASS 22/22, 97.6 fps; the first launch after the deploy updates the prefix once
  (patched wine.inf registers winedmo): 28-31 s.
- **The Direct3D 12 game** (run mf-03 (2026-10-17), 1187 s, closed on request, no helper ended by the title): New Game no
  longer crashes; the intro plays (240-365 s) and the game reaches its first controllable scene (Jill's apartment,
  first person; the stick turns and walks from 604 s). The game presents about 14 fps during the intro and 15.0 fps
  average over the fixed stretch 900-1140 s (1% low about 2.3 fps from the 10 s windows' p99 of 440 ms, median frame
  61 ms, a ~690 ms hitch every 20 s in the intro and in gameplay alike). The decoder is not the limit (about 100 fps on
  the console); the Direct3D 12 game's helper runs on 5 CPUs at the console's lowest priority (see the Direct3D 12 game's frame-rate work). The Direct3D 12 game's
  helper: resident 225 MiB, Unix heap 43 MiB, 59 MiB during the intro, then 46; direct memory 4180, 4242, then 4202 MiB.
- **Counters** (patch 2065, PS5 only): every demuxer's opens, seeks (and those to time 0), byte stream rewinds to
  offset 0, reads and bytes go to ntdll's runtime counters (`pw_wine_perf_media`); each helper logs them in its 10 s
  telemetry as `wine-child: media opens= seeks= to_start= rewinds= reads= read_KiB=` when there were any. Host-tested
  (`tests/test_pw_wine_perf.c`); first console reading with the gate.
- **Gate**: `gate-2026-10-17-mf-01` (build ce13c872, main 1148c13 + this) stopped at 51 of 84: 46 PASS, among them
  mf-video32 (20.2 s) and mf-video64 (18.3 s); its five failures (payload-eight, dxvk-window64, host-dmem64,
  host-dmem-fallback64, window-compose32) were the boot's helper-spawn limit, not media: "kern.ipc.maxpipekva exceeded"
  160 times, "child bootstrap failed errno=5" with the TTY(STDIN) line 14 times; the title ended 23 helpers. After a
  restart, `gate-2026-10-17-mf-02` passed 58 of 58 before the host PC rebooted (no spawn-limit line), and
  `gate-2026-10-17-mf-03` passed 84 of 84 in 1,658 s on the same build (mf-video32 19.6 s, mf-video64 17.9 s; the
  title ended 6 helpers, all in broker-wedge64, which ends them by design; no maxpipekva, errno=5 or TTY line).
- **The hitch is not media** (run mf-04 (2026-10-17), 802 s on the gated build, closed on request, no helper ended by
  the title): the Direct3D 12 game's helper logged media activity in two 10 s windows only, at the intro's start (`opens=1 seeks=1
  to_start=1 rewinds=1 reads=27 read_KiB=6912`, then `seeks=1 to_start=1 rewinds=1 reads=9 read_KiB=2304`), and none
  after, while the 670-1360 ms hitch kept coming every 20-30 s from 240 s to the end, in the scene without input
  (450-600 s: 13.9 fps, 7 windows over 500 ms) and walking (600-770 s: 14.1 fps, 6). No demuxer opened, seeked or read
  during any of them: the ~160 MiB read every ~20 s goes another way.
- **Fresh configure broken at 788b0ec**: patch 2065 includes `pw_wine_perf.h` in `dlls/winedmo/unix_private.h`, and
  `tools/build-wine-ps5.sh` never copies it beside winedmo's sources, so a fresh Wine configure stops in makedep
  ("pw_wine_perf.h: No such file or directory"; found by the port session). The gate passed because this worktree's
  Wine tree was configured on 2026-10-16, before 2065, and later builds re-applied the patches without reconfiguring;
  winedmo's Unix files compile through the script's own line with `-I wine/ps5`. The port session copies the header
  into `dlls/winedmo/` before configure in shell batch 2 (the script's pattern for `pw_shell_style.h` and
  `pw_wine_cursor.h`); a follow-up may make the script reconfigure whenever the patch series changes.
- **Open**: the Direct3D 12 game at full rate (CPU placement, not media); what reads ~160 MiB every ~20 s (the Direct3D 12 game's session's per-file
  read attribution).

## Windows 11: the prototype's window shadow (2026-10-17, console; gate-2026-10-17-shadow-01 82/82)

A framed window's shadow is now the prototype's `--shadow-window` while it is active and `--shadow-window-inactive`
otherwise, light and dark (`src/pw_window_effects.h`, `shadow_layer` in `src/pw_window_compose.c`). Measured from the
reference captures, not assumed: Chromium (`--disable-gpu`, Skia's CPU raster) draws each box-shadow layer as an 8-bit
mask, the rounded rectangle blurred across then down by the W3C three-box approximation of a Gaussian of deviation
blur/2, each pass rounded; the layer then darkens by a=(alpha*(mask+1))>>8, each channel (v*(256-a))>>8, bottom layer
first. That fits every one of 1,120 per-layer samples read off the captures (both themes, four sides, active and
inactive); a true Gaussian left 2-3% of the shadow off (its tail reaches past where a is already 0). Wine patch 1200
gives the compositor what it needs: the foreground's active window (`PW_WINDOW_ACTIVE`) and every window's theme as its
material.

- **Host** (the production compositor over the reference captures, the shadow's pixels outside the windows, within a
  level): File Explorer 64.4% -> 99.997% light, 56.3% -> 99.992% dark; Notepad active over an inactive Explorer 77.9% ->
  99.65%, 73.0% -> 99.86%. Left over: the 1 px ring's anti-aliased corner tips (12 px light, 30 dark per window; cause
  not found); and where the two shadows overlap, Chromium composited Notepad as its own layer (it overlaps Explorer's
  Mica Alt canvas), which a layer model fits at 99.997-100% and sequential drawing at 95.5-98.3%: not modelled.
  `tests/test_pw_window_effects.c` holds the captured alphas and a naive recipe; `tools/run-window-effects.py` (the
  gate's oracle) the same model in Python.
- **Console**: `gate-2026-10-17-shadow-01` 82/82 in 1,645 s on build 1b9f5008; window-effects checks that the snapshot
  marks the foreground window, and only it. `shell-scorecard-2026-10-17-01`: File Explorer's shadow 99.9970% light,
  99.9925% dark within a level of the reference around the window, the same pixels as on the host; the state 79.4% ->
  100.0% light, 75.7% -> 100.0% dark, the four caption-button states 99.6-100%.
- **Cost**: an active 1100 x 700 window's shadow is 0.38 ms on the host (clang, znver2), the old 18 px one 0.63; 500 x
  300, 0.25 against 0.29. `frame-pacing-2026-10-17-01` passed: 384 compositions, 1,755 us mean (`-01` 1,782, `-02`
  1,588), max 11,271; drag 1,626 mean, 2,058 max (`-02` 1,492, 1,834), Quick Settings opening 2,627 (1,654), and one
  composition of 9,017 us while Start was held (none before). Found on the host: an activation flips `PW_WINDOW_ACTIVE`
  on rows beneath a glass window, and `backdrop_key` hashes whole rows, so both the taskbar's and a flyout's backdrop
  are rebuilt (6 ms instead of a hit) though no pixel changed. The fix, a row keyed without the flag unless its shadow
  is a framed window's, came with the shell batch (gate-2026-10-17-shell-batch-02, 82/82):
  `frame-pacing-2026-10-17-shell-batch-01` (build 521fbcb8, where a capture's own present no longer counts in a phase)
  has Quick Settings opening at 1,756 us mean (3,081 max), no composition at all while Start or Quick Settings is held,
  Start's opening at 2,855 mean (9,200 max, one), and the steady phases at 2,243 us mean, 7,803 max (the menu's row
  sweep), none at 16.7 ms. The drag's slow intervals were the old probe's own pace: it moved the window every 19-24 ms,
  and all 99 intervals over 20 ms were "content" (graphics patch 0020: none was composition, copy or the flip's wait);
  moved on a 60 Hz grid, the drag runs at 55.4 fps with one interval over 20 ms, the phase's first. Every present
  copies for about 4.8 ms.

## Windows 11: the shell scorecard (2026-10-16, console; gated with the window shadow, gate-2026-10-17-shadow-01)

The owner's goal, "a shell that looks and feels like our prototype", measured instead of judged:
`tests/shell-scorecard-probe.c` puts the native shell in each state of the prototype's reference set it can reach
(44 of 86, light then dark), and `tools/run-shell-scorecard.py` compares each capture with
`.impeccable/reference-1920` (4e52ee1, brightness 100; in the design worktree, never committed: it holds Bloom).
A state is compared over its footprint in both shells (every pixel where it differs from its theme's desktop),
so a surface in the wrong place counts both places; the desktop is compared whole. Left out: the pointer and live
data (status and clock buttons, the notification list, the calendar, File Explorer's Recent rows and item count).
Self-check: the reference against itself scores 100% on all 88 reachable captures; until the 84 states with no
native surface or no route exist, the ceiling is **51.2%**.

**First run** (`shell-scorecard-2026-10-16-01`, build 217ac776 on 762df4f, 352 s, 70 of 88 captures):
**13.3% like the prototype** (pixels within one level over all 172 reference states, an unscored state counting
0); **32.6%** over the 70 scored.

| Surface | Scored | Like the prototype | Mean of scored |
| --- | --- | --- | --- |
| Start and search | 8/8 | 87.0% | 87.0% |
| File Explorer (Home) | 2/2 | 77.6% | 77.6% |
| Window chrome | 10/14 | 44.4% | 62.2% |
| Taskbar | 6/22 | 12.7% | 46.6% |
| Flyouts | 4/16 | 7.7% | 30.9% |
| Desktop | 12/16 | 12.5% | 16.6% |
| Dialogs | 16/36 | 4.8% | 10.8% |
| Apps | 6/18 | 1.9% | 5.6% |
| Menus | 6/6 | 0.1% | 0.1% |
| Controls, Snap and switching, lock and sign-in | 0/34 | 0% | (no native surface) |

**Second run** (`shell-scorecard-2026-10-17-01`, build 1b9f5008: the window shadow and the probe's fixes: property
sheets through the Properties verb, the Settings pin's hover with File Explorer open, windows-active-inactive marked
when Notepad fails, and Notepad there placed at 512,228 as in the reference): **17.5%** like the prototype, **39.5%**
over the 76 scored (was 70). File Explorer 100.0% in both themes, caption buttons 99.6-100%; windows-active-inactive
scored for the first time, 80.0% light and 17.4% dark (Wine's Notepad is not drawn dark: Notepad alone 0.5% dark).

**Third run** (`shell-scorecard-2026-10-17-batch-01`, build 521fbcb8: the shell batch's desktop icons, menus and
backdrop key, with the probe on the reference's menu routes and Read me in the prototype's cell): **23.6%** like the
prototype, **53.3%** over the 76 scored. Desktop 16.6% -> 72.3% of what is scored: hover 88.7 / 88.9% (light / dark),
selected 87.1 / 87.0%, rubber band 94.6 / 94.6%, several selected 55.2 / 55.0%, infotip 18.3 / 0.7%, the whole desktop
99.0 / 98.6%. Menus 0.1% -> 63.1%: the desktop menu 85.5 / 82.1%, a file's menu 50.6 / 48.2%, View's submenu 56.4 /
55.8%. The gate it rode found that a press on an icon's plate edge, which the prototype counts as the item, misses
it natively (only the icon and label do); the desktop-view probe now presses the icon itself, and the plate's hit
test is the icons' next change.

**Fourth run** (`shell-scorecard-2026-10-18-batch2-01`, build 9115428c on a5a0d88, the first at 120 Hz with every
speed default on; gate-2026-10-18-shell-batch2-01 84/84): **23.8%** like the prototype, **54.0%** over the 76 scored.
The desktop's View submenu, which now has the prototype's icon sizes and arranging, 56.4 -> 81.7% light and 55.8 ->
79.8% dark (menus 63.1 -> 71.3%); no other state moved by more than a point, so the scorecard holds at 120 Hz and
File Explorer's separable Mica Alt keeps its pixels. The desktop's time to its wallpaper, now logged by every gate's
window-effects cases: the shell window was there at once and the root was flat for 1.6 s (32-bit) and 1.8 s (64-bit),
one retake each, to within a capture of about a second; the "no frame in 10 s" of gate-d3d12-census-2026-10-17-01 was
the old retake numbering, not the desktop.

**Fifth run** (`shell-scorecard-2026-10-18-batch3-01`, build 02cdf9fb on 6f573b5: menu text as Chromium draws it,
the flyout shadow as Skia's layers, the window ring's corner tips, and every tooltip and the desktop's infotips as the
prototype's; gate-2026-10-18-shell-batch3-01 84/84, the title ended 6 helpers): **23.8%** like the prototype (held),
**53.9%** over the 76 scored. Menus 71.3% -> 81.9%: the desktop menu 85.5 -> 99.0% light, 82.1 -> 96.8% dark; View's
submenu 81.7 -> 95.2 / 79.8 -> 95.1%; a file's menu 50.6 -> 54.0 / 48.2 -> 51.3%. The infotip 18.3 -> 84.4% light (dark
1.1%: the reference's dark infotip capture is 4-6 levels darker than its own hover capture over the whole screen).
The hover state fell 88.7 -> 18.4% light, 88.9 -> 17.5% dark, and that is the probe: its capture shows the infotip.
The title takes up a capture request only every 120 ticks (`native/proton_wine.cpp`, a second at 120 Hz), so the shot
asked 360 ms after pointing lands after the tip's 500 ms. The next batch's probe shoots the infotip first and the hover
after the tip's autopop (5 s; it does not come back while the pointer rests), whatever the poll. With the hover's
batch 2 scores the run would be about 24.6%.

**Sixth run** (`shell-scorecard-2026-10-18-batch4-01`, build ff77b4a4 on 40fd0dd: the probe's hover after the
infotip's autopop, and the file's menu; gate-2026-10-18-shell-batch4-01 84/84, the title ended 6 helpers): **24.8%**
like the prototype, **56.2%** over the 76 scored. The hover, now shot after the tip has popped, 18.4 -> 89.3% light and
17.5 -> 89.5% dark (batch 2's were 88.7 / 88.9%), with the infotip unchanged (84.4% light); desktop 49.5 -> 58.5%.
The file's menu 54.0 -> 70.5% light, 51.3 -> 69.0% dark; menus 81.9 -> 87.6%.

What the first run's captures show, most seen first:
- **Desktop**: 98.7% light, 98.3% dark whole. The icons differ: the native desktop has no This PC, so the
  session's launchers sit in the prototype's slots, and an icon under the pointer has no hover plate (icon states
  0.0-0.8%). The selection plate is there.
- **Menus** (0.0-0.4%): the desktop menu is Wine's classic square menu with other items (Line up Icons, Refresh,
  Paste, Paste as Link, Properties); the prototype's is the rounded Windows 11 menu with icons.
- **Window shadow**: File Explorer's window itself equals the prototype's (157 differing pixels in it), but its
  shadow is darker at the edge and ends about 20 px out, where the prototype's fades over about 60 px below and
  40 px to the right: 79.4% light, 75.7% dark, and the same in the four caption-button states.
- **Dialogs**: message boxes 0.7-7.6% and Notepad's save prompt 1.1-1.7% (classic layout and buttons, no footer
  band); task dialog 3.5-3.7%; Run 8.7-11.0%; Open and Save 53.5-53.9% light, 5.9-6.9% dark (Wine's classic
  "Look in" dialog, where the prototype draws Windows 11's).
- **Apps**: Settings 1.2-1.4%, maximized 0.5-1.9%; Task Manager 1.3-1.8% (Performance 26.2% light).
- **Flyouts**: Quick Settings 28.3-29.8%, notifications 32.5-32.9%. **Taskbar**: Start button hover 95.4% light
  but 61.3% dark, pressed 58.5-64.2%; the Settings pin's hover 0.1%, because the prototype's state has File Explorer
  open and the probe's did not (fixed in the probe for the next run).
- **Did not open** (findings, not scores): Notepad started while File Explorer was open failed in both themes
  ("payload loader failed at answer ... errno=35" after 15 s, child bootstrap errno=5, beside
  `kern.ipc.maxpipekva exceeded`); Notepad alone started. The property sheets did not open: `SHObjectProperties` is a
  stub in Wine, so the next run opens them through the Properties verb. Terminal: no console window was found. The
  hidden icons have no notification-area icons to show, as on Windows. Hovering File Explorer's button showed no
  preview.

## Windows 11: menus as the prototype draws them (2026-10-17, console-measured; gated in the shell batch)

Every program's popup menus (Wine's #32768: context menus, menu-bar drop-downs, the window menu) now draw as the
approved prototype's `.menu` (css/ui.css): Wine patch 1190 makes each popup ask the compositor for the flyout's
acrylic, 8 px corners, stroke and --shadow-28 (PS5_DWMSBT_FLYOUT, dark when COLOR_MENU is), lays its items out as
`src/pw_menu_style.h` measures the prototype (32 px rows 4 px inside the list, icon column 11 px and labels 39 px into
a row, 12 px shortcuts and chevron ending 11 px from its edge, 1 + 2 + 2 px separators, 200-456 px wide; leading
picture-only items form Windows 11's 40 x 40 command row) and paints through one premultiplied buffer: plates,
keyboard ring, separators, check, radio dot and chevron are the prototype's own pixels
(`tools/build-menu-sprites.py` from `themes/windows11/menus/*.json.gz`), text is each glyph's 256-level coverage
composed with Skia's mask gamma in the TextFillColor tokens. The desktop's menus (`wine/ps5/pw_shell_desktop_menus.h`,
Explorer's desktop view) give the prototype's items where the shell has the action: View (icon sizes, Auto arrange and
Align to grid since the desktop-icons follow-up, Show desktop icons), Sort by, Refresh, New (Folder, Text Document),
Display settings and Personalize (the Settings app), Open in Terminal, Show more options; on an item the command row
(Cut, Copy, Rename, Share disabled, Delete), Open, Copy as path, Properties, Edit in Notepad. Shift (Shift+F10,
Shift+right-click) opens the classic menu, for an item the shell's own.

Proven:
- Host: `tests/test_pw_menu_style.c` (layout against the prototype's measured boxes; the plates redrawn from their
  pieces equal the captures in every pixel, light and dark; text tokens; Skia's mask table). A private `win32u.so` in a
  shadow of the host runtime rendered the desktop menu through WM_PRINTCLIENT in light and dark: rest, keyboard ring,
  View's submenu with the ring on its first row, the ring back after Left, the pointer's plate (it found 32-bit icons
  drawn upside down, fixed before the console run).
- Host, after the console run: a selection change repaints only the rows it reaches, 0.19 ms per hover in the host
  render (it repainted the whole menu, 0.71 ms).
- Console, build 3aabea0f (f06b39d + this): frame-pacing-menus-2026-10-17-01 passed; the desktop menu's opening 5
  compositions at 3.9 ms mean, 6.5 ms max; the pointer sweeping its rows 224 compositions at 3.85 ms mean, 7.7 ms max,
  presents at a 16.75 ms median; no phase reached 16.7 ms; steady mean 2.49 ms (budget 3 ms). The sweep's 51 fps over its 7.1 s
  is the phase boundary's display capture (one 1.33 s interval, as every phase of every frame-pacing run has): of its
  364 intervals 357 are under 20 ms and 5 between 20 and 34 (the window drag has 93 over 20 in every run).
  shell-scorecard-menus-2026-10-17-02 (desktop states only, the probe with the reference's routes: the submenu after a
  right-click at 700,400 with View hovered, the item menu from the menu key): **menus 0.1% -> 53.6%**:
  menu-desktop 85.5% light / 82.1% dark, menu-desktop-view-submenu 56.7% / 56.1%, menu-desktop-item 21.1% / 19.8%.
  (-01 captured all 16 states but had no plain-desktop baseline to score them against.) 9, 10 and 10 helpers, all
  exited by themselves; no storage errors.

What is left, from the captures:
- menu-desktop: of its 18,904 mismatched pixels (light), 14,722 are its shadow (the compositor's --shadow-28 is still
  Gaussian; main's window shadow model, Chromium's three-box layers, would draw it exactly) and 3,907 its text (GDI
  advances are whole pixels, Chromium's fractional, so the native menu is 4 px wider).
- View's submenu had only Show desktop icons: the desktop had no icon sizes, auto arrange or align-to-grid switch. The
  desktop-icons follow-up made them the shell view's settings (IFolderView2); the View items call them since (host-built,
  not yet run on the console).
- The item menu opens where the native "Read me" is, 88 px above the prototype's, and lacks Open with and Compress to
  ZIP file (no native action).

**Menu text as Chromium draws it (2026-10-18, host).** Labels and shortcuts are now Chromium's own glyphs
(`pw_menu_glyphs.h`: the prototype's `.menu-label` and `.menu-shortcut`, printable ASCII and the ellipsis, captured with
`tools/measure-prototype-glyphs.mjs` at Skia's four pen phases, as alpha that black or white tints) at their linear
advances: the menu's width is the widest row rounded once (267 px for the desktop menu, Chromium's 267.34; GDI's whole
pixels made it 271), and right-aligned shortcuts end at that row's fractional edge as Blink aligns them. Other text
(characters outside the set, the default item's semibold) keeps the coverage path. Host: the 14 strings Chromium laid
out compose from the glyphs within a level (`tests/test_pw_menu_style.c`); the desktop menu rendered by win32u in a
shadow of the host runtime equals the prototype's list layer (labels, shortcut, icons, chevrons, separators) in every
pixel within a level, light and dark (4,796 ink pixels, worst 1). Console, with the flyout shadow below
(`shell-scorecard-2026-10-18-batch3-01`): the desktop menu 85.5 -> 99.0% light, 82.1 -> 96.8% dark.

**Flyout shadow as Chromium draws it (2026-10-18, host).** The flyouts' --shadow-28 (menus, Start, Quick Settings) is
now the framed window's model, Skia's three-box layers (`pw_flyout_shadow_layers`, `shadow_flyout` in
`themes/windows11/appearance.json`), in place of the CSS Gaussians, with one measured difference: Chromium draws a
flyout (its backdrop filter makes it a layer) and its two shadows into a layer of their own, so the layers combine
(A = a + A(256-a)>>8) and darken the page once; darkening per layer, as a window's shadow does, missed 9-10 of 51
sampled pixels. Around the desktop menu over the reference desktop: 100.000% of the shadow's 28,418 pixels within a
level light (99.8% exact, worst 1), 99.994% of 33,547 dark (99.5%, worst 2); the Gaussians were 94.8% and 94.0%. Window
shadows and everything else draw the same bytes (`tests/window-effects-render.c`'s six frames identical). Compose cost
on the host (clang -O2 x86-64-v3, median of 31, two runs): the desktop menu opening 2.7-2.8 -> 2.4-2.5 ms, held
1.8-1.9 -> 1.5-1.6; Start opening 6.4-6.5 -> 5.7-5.8, held 2.9-3.0 -> 2.2; Quick Settings opening 4.1-4.3 -> 3.7-3.8,
held 2.2-2.3 -> 1.8; an active framed window unchanged (2.2 ms). Console (`shell-scorecard-2026-10-18-batch3-01`):
the desktop menu's worst level 247 -> 34 light, 213 -> 31 dark; Start 88.0 -> 88.6% light, 87.6 -> 88.0% dark.

**The window ring's corner tips as Skia draws them (2026-10-18, host).** The tips of the 1 px ring
(`0 0 0 1px`, a rounded rectangle of radius 9 around the 8 px frame) were the shadow's last misses. Their cause:
Chromium's Skia fills that rectangle with analytic AA, each corner's conic as two quads, each quad as a few line
segments with y snapped to 1/4 px, and each row's coverage with its own approximations, so the arcs are lighter than an
exact circle and not symmetric (top from bottom, left from right). `tools/skia-rrect-coverage.py` re-expresses those
steps; its corners do not depend on the window's place or size, so the compositor takes them as a table
(`pw_window_ring_corners`) for unblurred layers of radius 9. Around File Explorer over the reference desktop, the tip
pixels outside the window: 248/272 -> 272/272 within a level light (worst 4 -> 1), 223/272 -> 271/272 dark (worst
17 -> 2); the inactive frame's (under Notepad, top corners) 124/136 -> 136/136 light, 112/136 -> 136/136 dark (worst
15 -> 1); the whole footprint 99.986% -> 100.000% light, 99.975% -> 99.9995% dark. Each tip's coverage range
read off those captures is a host check (`tests/test_pw_window_effects.c`); the one pixel the model leaves 2 levels
off is the bottom left's third along its diagonal, dark. Everything else draws the same bytes:
`tests/window-effects-render.c`'s frames differ only inside the ring's corner squares (90 pixels a frame, at most 3
levels), and the gate's oracle (`tools/run-window-effects.py`) takes the same corners and agrees with the compositor at
every corner pixel outside a window (240 of 240, active and inactive, light and dark). Cost unchanged (an active
window 2.21-2.28 ms on the host, 2.22-2.33 before). Console: gate-2026-10-18-shell-batch3-01's window-effects cases
passed with the oracle's corners; the scorecard's File Explorer states were 100.0% before and after, so they do not
resolve the tips.

**The file's menu (2026-10-18, host).** Batch 3's capture (`shell-scorecard-2026-10-18-batch3-01`, 54.0% light, 51.3%
dark) showed three causes. The command row's pictures were drawn 10 px right of and below their cells' centres: win32u
never measured a command cell's picture (its `bmpsize` stayed 0), and now does (patch 1190). The menu-key menu opened
8 px low and 1 px right: Explorer took the middle of the item's `LVIR_BOUNDS`, its 91 px grid cell, where the prototype
takes its element's, the plate; it now takes the plate's (1 px inside the cell, 2 px narrower, as tall as the list
view's hit test finds it). Open with was missing: a .txt now has the prototype's submenu (Notepad, then Choose another
app), another file a single Open with; Choose another app is disabled, as the prototype shows it with no app picker
(Wine's `SHOpenWithDialog` is a stub). Its icon is the prototype's (`tools/build-menu-sprites.py`, from the capture
it already had). Compress to ZIP file stays out (no zip writer in Wine's shell). Host, the file menu rendered by win32u
in a shadow of the host runtime against the prototype's capture of the same menu (`themes/windows11/menus/item-*`):
the command bar 1.8% -> 100% of its 491 ink pixels within a level (worst 0), light and dark; every row's icon 100%
(625 ink pixels, worst 0); the rows end 202 px below the list's top, the capture's 234 less its Compress to ZIP file
row. The plate's middle is reasoned from the console capture, not host-run. Console
(`shell-scorecard-2026-10-18-batch4-01`): the file's menu 54.0 -> 70.5% light, 51.3 -> 69.0% dark; the rest of the gap
is mostly the missing Compress to ZIP file row, which moves every row below it.

## MihawkBrowser: the default web browser (2026-10-16; host-proven; on the console it browses but is over its memory budget)

The user chose a real browser that browses the open web (HTTPS only), not only a launcher-download panel. The reviewed
plan is [MIHAWK_BROWSER.md](MIHAWK_BROWSER.md): CEF 150 LTC (branch 7871) under our own small C client, the GPU in the
browser process, one jitless renderer, native Win32 chrome, a downloads list whose Open runs installers through the
shell. Its §0 lists what the build changed in the plan.

- **Built:** `tools/fetch-cef.py` pins `cef_binary_150.0.22+gc1a097e+chromium-150.0.7871.256_windows64_minimal` by
  SHA-1 and SHA-256 and unpacks a 316 MB ship list; `tools/build-mihawk-browser.sh` builds `src/pw_browser.c` and
  `src/pw_browser_policy.c` (host-tested: address box, HTTPS-only navigation, download names, log privacy, switches)
  with clang for x86_64-windows; `tools/build-production.sh` runs it and `tools/stage-wine.py` stages the folder in
  `runtime/ge11-7/lib/mihawk-browser` with CEF's licences. The title's desktop-shortcut sync adds a MihawkBrowser
  shortcut (Public Desktop, so also Start) with the interim Fluent globe icon.
- **Host-proven** (run browser-host-01 (2026-10-16), pinned Wine on Linux, null display driver, software
  compositing): four HTTPS sites at status 200, a click-style download of a launcher's installer byte-exact (2,380,800 B),
  Open started it and the browser handed off 146 ms after its first window. Window 654 ms after process start, first
  page 1.94 s. Three persistent processes (browser, renderer, StorageService) and two named transients.
- **On the console** ([MIHAWK_BROWSER.md](MIHAWK_BROWSER.md) §0): C0 could not run (the game launcher is no longer installed).
  C1 found a Wine bug (DXCore's attribute stub made Chromium read "PS5" as a pointer; patch 1170 fixes it for every
  program), then loaded https://example.com at status 200 with the native chrome drawn, until the memory guard ended
  it: the browser process alone had converted 122 MiB of code (patch 1127; CEF 150's `.text` is 221.3 MiB) and the
  helpers reached 456 MiB. Measured at the user's request: at the default limit it browsed a DuckDuckGo search on to
  a launcher's store page and downloaded its installer byte-exact, with the three CEF processes at about 430 MiB;
  with patch 1127's code cluster cut from 256 to 16 KiB they stood at about 207 MiB (55-73% of the copies were code
  that never ran). The user chose an A/B of 64 against 16 KiB with a game check (`evidence/cluster-ab-2026-10-16-ab.json`):
  16 KiB won on every count (CEF 214 against 281 MiB, the first page 6.29 against 6.48 s, the emulator workload in the earlier emulator experiment
  to its first frame 14.0 against 19.6 s). But the gate on that build (`gate-2026-10-16-browser-01`, 36 of 82) found
  `winedevice.exe` stalling at exit with 16 KiB windows (three launches), besides four cases main's ungated design
  commits fail on their own (`shell32/64`, `window-effects32/64`, which the design side is updating). The stall's
  cause, found on the host from those logs: the exit killed a thread in the middle of a code conversion, and a kill
  ends a thread in Wine's fault handler at once, so it died holding the virtual mutex and the dmem engine's lock.
  Candidate fix, Wine patch 1171 (a kill waits until the trap or fault handler returns), with a new gate probe
  `kill-convert`; not yet run on the console. C4 downloaded the installer byte-exact and Open started it, but the
  PE32 installer could not map its image (c0000018, the setup.exe high-base issue): **C4 is blocked on that runtime
  fix**, not on the browser. The user then chose 64 KiB as patch 1127's default: the three launches that stalled
  at 16 KiB had no stall at 64 KiB (`stall-check64k-2026-10-16-01`), and the gate on main 762df4f with the browser,
  64 KiB and patch 1171 passed 83 of 83 on a fresh boot (`gate-2026-10-16-browser-02`, build `60e799a5074a3b9a`): no
  stall line in 516 helper logs, `kill-convert` 300 kills clean, system memory at most 520 MiB outside memory-guard.
  The browser's code is committed with it; at 64 KiB CEF stands at about 281 MiB, over the 200 MiB ceiling.
  In the desktop session the browser deadlocked after its first page (a blank page, a window that would not close):
  the PS5 driver's surface flush took win32u's lock inside the surface's, the reverse of `update_surface_region`.
  Wine patch 1173 fixes it for every program; the same desktop-session run then loaded, drew and closed
  (run browser-desktop-01 (2026-10-18), `-02`; [MIHAWK_BROWSER.md](MIHAWK_BROWSER.md) §0).
- **Approved in the prototype (2026-10-16, host only):** The chrome with a Downloads flyout, the taskbar pin after
  Terminal and an original icon (B3), drawn by `prototypes/windows11-shell/js/apps/browser.js` (H4, decisions 8 and
  11). Its measured boxes, type and pixels are in `docs/windows11-reference/mihawk-browser.json`
  (`tools/measure-browser-prototype.mjs`); [MIHAWK_BROWSER.md](MIHAWK_BROWSER.md) §4.1 lists what `src/pw_browser.c`
  already matches (command row, hover and press levels) and the six things it must change.
- **Drawn as approved (host, 2026-10-17):** The native window as the prototype draws it (Downloads flyout on the
  compositor's backdrop 7, the WinUI address box, start view, error panel, icon B3), checked against the reference by
  `tools/check-browser-reference.py` (light and dark 89 of 89); the taskbar pin after Terminal and the Start pin
  (MIHAWK_BROWSER.md §4.1). On the console it shows in the desktop session with a page loaded
  (run browser-desktop-02 (2026-10-18), captures in klog); no pixel check against the reference there yet.
- **Open:** C4, launching a downloaded installer, waits for the setup.exe high-base runtime fix; CEF stands at about
  281 MiB at 64 KiB windows, over the 200 MiB ceiling (shared code copies are the next lever); 16 KiB with patch 1171
  is untested and kill-convert is not sensitive; the default-browser registration (decision 6).

## Windows 11: File Explorer as the prototype draws it (2026-10-16, console; no gate, by the owner's request)

- **Window** (Wine patch 1150, `wine/ps5/pw_shell_explorer.h`): the client takes the caption; a 48 px title bar of
  Mica Alt glass with the tab (the location's icon and name, close) and the 46 x 48 caption buttons; the address row
  (back, forward, up, refresh, the crumbs, an edit for typing a path, the search box), the command bar (New, Cut, Copy,
  Paste, Rename, Share, Delete, Sort, View, Filter in folders, See more, Details), the navigation pane (Home, Gallery,
  the six pinned folders, This PC, Network) and the status bar. Without a folder Explorer starts on **Home** (Quick
  access tiles, Favorites, Recent: the newest files of the user's folders). Layout `src/pw_explorer_layout.h`
  (host-tested equal to the prototype's boxes, fractions kept); fixed chrome the prototype's own pixels
  (`themes/windows11/explorer`, 166 sprites per theme, `tools/measure-explorer-sprites.mjs`).
- **Folders**: Wine's shell view keeps the files; its list has no border, and in details view the prototype's columns
  (Name 280, Date modified 170, Type 150, Size 100), column header (its pixels, sort glyph) and 32 px rows with
  hover and selection plates. Patch 1151: shell32's `IFolderView::ItemCount` counts the selection (it counted every
  item, so an unselected folder enabled Cut, Copy and Delete).
- **Mica Alt** (`src/pw_window_effects.h`): the prototype's `.mica-alt` (its own Mica image of the wallpaper,
  luminosity #e6e6e6, 65% #f2f2f2); the compositor's C code over Bloom is within a level of the prototype's title bar
  everywhere, light and dark.
- **Console** (`explorer-2026-10-16-04`, desktop-session, Explorer where the prototype's is): Home 98.6% of the window
  within a level of the prototype's capture (was 5.3% with the classic browser); header 99.99%, command bar 100%,
  navigation pane 99.99%, Home above its Recent rows 100%, the title bar's Mica 100%, caption buttons 99.6% (the
  rounded corner). Documents 98.7%. desktop-session now captures both and checks them against the prototype's sprites:
  538,600 and 215,000 pixels, none more than a level off.
- **Tabs** (`explorer-tabs-2026-10-16-02`, capture 8): one IExplorerBrowser per tab, shown one at a time (each its own
  location and history); +, Ctrl+T, Ctrl+W, middle click and the close button, Ctrl+Tab; the prototype's strip
  (equal widths shrinking from 240 to 88 px, host-tested) and unselected tab. The probe checks Ctrl+T, Ctrl+Tab and
  Ctrl+W by the window's title (38/38); the two-tab capture matches the sprites (538,711 pixels within a level).
- **Filter and the details pane** (`explorer-pane-2026-10-16-02`, captures 8 and 9): Filter through the browser's site
  (`ICommDlgBrowser::IncludeObject`, which Wine's ExplorerBrowser asks on every navigation) with the prototype's kinds,
  checked look and empty-list notes; the 280 px details pane with the location or selection (icon, name, type, Contains
  or Size, Date modified, Path; several items' count and size), its fixed parts the prototype's pixels. Both captures
  match the sprites (371,912 and 227,671 pixels within a level).
- **Navigations without a classic frame** (patch 1152): shell32 showed and painted a new view before the browser
  reported it, so each folder to folder navigation flashed Wine's classic list; the browser's window is now held from
  drawing from the navigation's start until the view is styled. Shown on the console (explorer-repaint-2026-10-16-02,
  light, and explorer-dark-2026-10-16-02): every view File Explorer created was invisible when created (3 of 3 in
  each run, both navigations from a shown window held), all 11 captures equal to the sprites. Up from the navigation
  pane's Documents goes to the Desktop (Wine's Documents there is the shell's own item under it; the prototype's goes
  to This PC). The first run found the enabled Back sprite captured disabled (an enabled Back drew grey): recaptured.
  The gate's shell32/64 cases then failed address navigation (gate-d3d12-overrides-2026-10-16-01): the title, set before
  the hold was released, told the probe the folder was there while its list was still hidden; the title is now set
  last. That let the probe reach select-all, which File Explorer lacked: Ctrl+A, C, X and V are now the prototype's.
  explorer-title-2026-10-16-02: shell32 and shell64 95/95 each.
- **Not yet**: text (GDI's; the glyph atlas of the clock is the way), the search box.
  [PROTOTYPE_DIFF.md](windows11-reference/PROTOTYPE_DIFF.md).
- **Icon views** (`explorer-views-2026-10-16-01`, desktop-session captures 5-7: Documents after Ctrl+Shift+2, 4, 5):
  large icons, small icons and list draw the prototype's tiles where its grid puts them (`pw_explorer_tiles`,
  host-tested against its boxes); the list keeps the places (its own arrangement off), so hit-testing and scrolling
  follow them. The folder and document icons are the prototype's own pixels at 96 and 16 px: all 15 tile icons equal
  them (worst 0), and the details view's folder icon too (100% within a level, was 52.7%). Selection plates take the
  prototype's colour (#0078d4 at 16%/23%, 30%/38% in dark; they used the accent shade at the light opacities).
- **Dark** (`explorer-dark-2026-10-16-01`, desktop-session `--theme dark`): Home 98.6% and Documents 98.7% of the
  window within a level of the prototype's dark captures, Mica Alt 100%, header and command bar 99.99-100%; the
  sprites' check passes (538,650 and 215,050 pixels within a level).
- **Not run**: the gate; the commit hook was bypassed at the owner's request.

## Windows 11: Start, Search and the taskbar's flyouts as the prototype draws them (2026-10-16, console; no gate, by the owner's request)

- **Start and Search** (`wine/ps5/pw_shell_start.c`): the prototype's 600 x 566 panel with its views, Start (search
  pill, Pinned 6 x 3, Recommended from the shell's recent files, account and power), All apps (letter headings) and
  Search (Top apps, Recent, Quick searches, results with Pin to Start); the taskbar's search pill opens Search. Fixed
  chrome is the prototype's own pixels (`themes/windows11/start`); names, dates and labels are GDI text.
- **Flyouts** (`wine/ps5/pw_shell_flyouts.c`, patch 1161): Quick Settings with what the console has (the network
  tile from the adapters' real state, Settings), the notification centre's empty state and the calendar (the clock),
  and the hidden icons (the notification area's own icon windows; none on the console, so it opens nothing, as on
  Windows). Not drawn because nothing is behind them: volume (Wine's endpoint volume is a stub and the title has no
  master gain), brightness, battery, the other tiles, do-not-disturb, Focus.
- **Materials** (patch 1160, `src/pw_window_effects.h`): two private backdrops for the prototype's .surface-flyout,
  host-checked with the compositor's C code against its bare panels over Bloom: Start's interior 100% within a level
  (light), 95% (dark, all within 2); Quick Settings' 100% / 99.9%; --shadow-28 91-100% outside the corners.
- **Console** (`shell-surfaces-2026-10-16-04`, `tools/run-shell-surfaces.py`, every surface opened): Start light 92.9%
  of its pixels and shadow within a level of the prototype's capture (search pill 97.3%, headings 99.9%, empty
  material 100%, shadow 96-98.5%); dark 91.6%; Search 96.5%; Quick Settings' tile 99.9-100%, the notification
  centre's header and empty state 98.9-99.6%, the calendar's weekday row 93.3% (all within 2). What differs is text,
  pinned and recent items (the prefix's own) and the parts not drawn:
  [PROTOTYPE_DIFF.md](windows11-reference/PROTOTYPE_DIFF.md).
- **Not run**: the gate (the commit hook was bypassed at the owner's request); the shell gate case's Start verdict
  (`tools/run-shell.py`) is updated to the new material but has not run on the console.

## Windows 11: text as the prototype draws it, the taskbar clock first (2026-10-16, console; no gate, by the owner's request)

- **How Chromium draws the prototype's text** (Chromium 154 on the host, measured): web fonts go through Fontations
  (skrifa's autohinter in light mode, Skia's analytic path coverage), not FreeType; a preblend chosen by the paint's
  luminance (gamma 1.2 stored as 76/64, contrast 0.2); horizontal subpixel positioning at quarter pixels (pen =
  floor(x + 1/8)), baseline rounded; linear advances (Selawik has no kerning); Blink right-aligns by the width
  rounded up to 1/64 px. FreeType 2.14's light autohinter with that preblend matches only 48% of the ink pixels.
- **What matches**: the prototype's own glyphs (`tools/measure-prototype-glyphs.mjs`: each character alone at the four
  phases, in an element's font and colour) placed on that grid by `src/pw_text_glyphs.h`.
  `tests/test_pw_text_glyphs.c`: 8 clock strings per theme as Chromium drew them; light equal in every channel, dark
  equal except 16 of 46,800 channels one level off where "/" overlaps a digit.
- **Clock** (patch 1140): drawn with those glyphs when every character was measured (en-US), its width lays the tray
  out (61 px for "10/16/2026", was 58).
- **Console** (`clock-text-2026-10-16-01`, light, "3:26 PM 10/16/2026" against the prototype at that time, no badge):
  the clock's 539 ink pixels 67.7% exact and 99.4% within a level, worst 2 (before, `taskbar-2026-10-16-03`: 2.9%
  exact, 3.7% within a level, worst 219); the tray 78.1% exact and 99.97% within a level, worst 2 (was 70.1%, 90.3%,
  worst 220). The difference image is the acrylic's noise, no glyph shape. Host prediction: 74.0% of ink exact, all
  within a level; dark (host only) 33.0% exact, 99.4% within a level (`evidence/clock-text-2026-10-16-01-prototype.json`).
- **Not run**: the gate; the dark theme on the console.
- **Next**: window titles (12/16 px, TextFillColorPrimary, Tertiary when inactive), Start (12 px labels, 14 px body,
  600 14 px headings), Explorer (12 px items and navigation, 14 px body, 600 14 px headings): an ASCII and Latin-1
  atlas per size, weight and colour (about 160 KB as premultiplied BGRA, a quarter as alpha plus a colour).

## The composed desktop back under its 3 ms (2026-10-16, console; no gate, by the owner's request)

The prototype's materials had made composition 3.7 times its old cost (shell-surfaces-2026-10-16-04: compose_avg
11,072 us, max 34,116; gate-2026-10-15-01: 3,000), against WINDOWS11_PLAN.md 5c's +10%. Every glass window recomputed
its whole backdrop every frame, opaque body included, and blurred the pixels beneath it again though they had not
changed: on the host File Explorer's Mica Alt bar alone was 18 ms of a frame.

- `src/pw_window_compose.c`: each glass window's finished backdrop is kept (up to 8, `PwWindowEffects.backdrops`) under
  a key of everything it is made from (the rows beneath its blur area with their images' sequences and thumbnails, its
  own row, the screen, the Mica images) and rebuilt only when that changes; opaque spans are copied whole; a shadow's
  alpha beside straight sides is taken once per column or row. The blur stays at a quarter of the resolution, so no
  oracle number moves: the output is byte-identical to the previous compositor on 24 scripted desktop frames and 300
  random shadow scenes, and `tests/test_pw_window_materials.c` checks the cache against building afresh.
- Host (one core): desktop + File Explorer 21.3 -> 2.7 ms; + taskbar 24.1 -> 2.7; + Start 35.4 -> 4.2; + Quick Settings
  41.3 -> 5.0; a window dragged across the taskbar 6.1 ms mean, 9.7 worst.
- Measuring it: graphics patch 0019 ends a broker phase at each display capture (compositions, a histogram, the
  desktop presents' pacing); `tools/run-frame-pacing.py` with `tests/frame-pacing-probe.c` runs the scenes.
- **Console (`frame-pacing-2026-10-16-01`, build 71c30741)**: 400 compositions, compose_avg 1,782 us, compose_max 27,597.
  Idle desktop, Start held, Quick Settings held: no composition at all. A window dragged across the taskbar: 326
  compositions, 1,465 us mean, 1,789 max, all under 2 ms; 46 frames a second for the probe's 54 moves a second
  (interval p50 18.75 ms, p95 27.0; 82 over 20 ms, 2 over 34 ms): the move cadence and the present path set that
  pace, not composition. Not yet met: opening Start (12 compositions, 7,386 us mean, 27,597 max: 3 frames between 16.7
  and 33 ms while its backdrop is first built) and Quick Settings (5,948 mean, 12,896 max).

## Opening Start and Quick Settings inside a 60 Hz frame (2026-10-16, console; no gate, by the owner's request)

Timed on the host part by part (desktop, File Explorer on Mica Alt, the taskbar, then Start), the slow opening frames
were not the backdrop's first build but the flyout's --shadow-28: `pw_window_flyout_shadow_alpha` called four `erfcf`
per pixel around the flyout, 19.3 of Start's 29.4 ms, and in every composition while a flyout is open, not only the
first. The host's clang build for the console's CPU matched the console: 29.4 ms with Explorer, 27.4 without it, against
27,597 us measured in `frame-pacing-2026-10-16-01`.

- `src/pw_window_effects.c`: the shadow is separable (each Gaussian's blur of the rectangle is a column's span times a
  row's), so `pw_window_flyout_shadow_span` is taken once per column and row and `pw_window_flyout_shadow_mix` combines
  them; the per-pixel function is built from the same two, so the floats, contractions included, are the same.
  `pw_window_material_row` blends each blurred column pair vertically once and then horizontally (the same integer as
  the other order), then applies the recipe over the row.
- Byte-identical to the previous compositor on 52 scripted desktop scenes composed twice (Start, Quick Settings and
  both, opening and held, over Explorer and the taskbar, light and dark, a drag under them, repaints, flyouts clipped by
  the screen's edges and a tiny one), 400 random scenes with every backdrop, shadow, radius and border (each with a
  kept and a fresh cache) and the shadow's alpha over a grid: with gcc and with clang -march=x86-64-v3 (FMA,
  -ffp-contract=on as on the PS5).
  `tests/test_pw_window_materials.c` now checks every recipe's rows and the compositor's flyout shadow per pixel.
- Host (clang, the console's compiler; Explorer, taskbar and the flyout): Start's opening frame 29.4 -> 9.7 ms (dark
  31.6 -> 9.9), Start open 23.1 -> 4.9; Quick Settings 17.2 -> 6.3 opening, 14.2 -> 3.9 open. Shadow 19.3 -> 1.3,
  Start's backdrop build 4.1 -> 2.8 (dark 6.6 -> 2.9).
- **Console (`frame-pacing-2026-10-16-02`, main a474c14 + this change, build c97d4f23)**: every composition under 16.7
  ms. Opening Start: 11 compositions, 2,851 us mean, 9,170 max (was 7,386 and 27,597, 3 frames 16.7-33 ms), histogram
  8 under 2 ms, 1 under 4, 2 at 8-12. Opening Quick Settings: 7, 1,654 mean, 2,968 max (was 5,948 and 12,896). The
  whole run 391 compositions, 1,572 mean, 9,170 max (was 1,782 and 27,597); the drag across the taskbar unchanged
  (317, 1,492 mean, 1,834 max); held Start and Quick Settings still compose nothing. Probe and session PASS.

## Windows 11: the taskbar and wallpaper as the prototype draws them (2026-10-16, console; no gate, by the owner's request)

- **Wallpaper byte-identical**: Chromium draws the 3840 x 2160 Bloom at half scale with libjpeg's own scaled
  decode; `tools/build-wine-theme.py` now does the same (PIL `draft`), and `proton.bmp` equals the prototype's
  wallpaper in every pixel. On the console 98.4% of a wallpaper region is exact, 99.8% within a level.
- **Taskbar**: Explorer's taskbar takes the prototype's layout (`src/pw_taskbar_layout.h`, host-tested against its
  boxes) and its own pixels (sprites measured by `tools/measure-prototype-sprites.mjs`, Wine patch 1140): Start, the
  search pill, Task View, the pinned File Explorer, Settings, Notepad and Terminal with running and focused pills,
  the chevron, status icons and clock. Its material is the prototype's taskbar acrylic (patch 1141, a private
  backdrop; `PW_BACKDROP_TASKBAR` in the compositor), host-checked against the prototype's own bar over Bloom with
  the compositor's C code (light 93.9% within a level, dark 79.0%, worst 3).
- **Console** (`taskbar-2026-10-16-03`): Start to Terminal 91.2% within a level of the prototype, all within 3; tray
  icons all within 1; bar background 93.8% within 1. Differences left: [PROTOTYPE_DIFF.md](windows11-reference/PROTOTYPE_DIFF.md).
- **Not run**: the gate. The commit hook was bypassed at the owner's request.

## Windows 11: the prototype's visual system in the native theme (2026-10-16, console-proven)

The approved browser prototype is now the reference ([PROTOTYPE_DIFF.md](windows11-reference/PROTOTYPE_DIFF.md) lists
every difference found and what remains).

- **Caption buttons and frame, pixel-exact with the prototype** (the user's requirement): patch 1139 gives framed
  windows Windows 11's invisible resize border (1 px visible, the compositor's default #66757575 border), a 32 px
  title bar, 46 x 32 full-height buttons with hot tracking, 16 px icon and title margins, and
  `DWMWA_EXTENDED_FRAME_BOUNDS` as the visible bounds; the theme's caption bitmaps are fitted to the prototype's own
  measured pixels with Wine's arithmetic.
- **Tokens and theme**: the shell palette follows the prototype (light accent #005FB8, the user's choice); classic
  programs get the gallery's system colours and its check boxes, radio buttons, push buttons, edit borders, group
  box and progress colours. Fixed on the way: the theme's role guess had made progress fills white, the combo box's
  highlighted item white on white, and link texts grey. Explorer's file list now takes the shell palette.
- **Pointer scheme** (patches 1137, 1138; `tools/build-cursors.py`) and **Cascadia Mono** as the monospace face
  (OFL, pinned by `tools/fetch-cascadia-mono.py`).
- **Console, gate `gate-2026-10-16-win11-01`** (build `1080c1d1acc2c9e7`, after a hand restart and
  `console_guard.py new-boot`): **78/78 cases passed in 2285 s**. New cases: `cursor-scheme32/64` (user32 loads the
  scheme's files, 14/14), `theme-parts32/64` (13/13: every changed part equal to its reference to the level),
  `monospace32/64` (8/8). The appearance pair's caption strip: **0 wrong of 4322 pixels light and 0 of 4457 dark**,
  PE32 and PE64, the hovered button included. `dnsapi32/64` are back in the gate: the console's DNS is automatic
  again (`dns-check-2026-10-16-win11-01`: dnsapi64 and dns pass).
- **The default border, measured on the window-surfaces route** (window-effects capture 2 of
  `win11-visual-2026-10-16-01`): #c1c1c1 over the #f3f3f3 inactive bar, as `(0xf3*153 + 0x75*102 + 127) / 255`
  gives; Explorer's active frame in the desktop session reads #bec0c4 over #eef2f8, likewise. The appearance probe
  runs on the single-surface route, which draws no border, rounded corner or shadow; its verdict expects Wine's
  frame row there.
- **Two permanent checks, gate `gate-2026-10-16-win11-02`** (build `7fd835c5e3f864c2`, 78/78 in 2708 s):
  window-effects32/64 now sample the framed window's 1 px border on all four sides and on its top left ring (the
  probe logs the colour its frame is painted in): #c1c1c1 everywhere, as expected, the corner within 1 level; the
  host render of the same scene fails them when the border is removed. desktop-session now compares File Explorer's
  frame with the prototype pixel for pixel (`explorer_frame_verdict` in `tools/run-desktop-session.py`): border,
  32 px bar, icon box at 16 px, title from 16 px after it, caption buttons against the measured bitmaps, top
  corners' ring: **34,144 pixels, 0 wrong** (corners within 0.6 of the model).
- **Section 5c**, against `gate-2026-10-15-02`: compositor `compose_avg_us` shell64 4338 (was 4276, +1.4%), shell32
  4148 (4142), window-compose64 970 (961), window-compose32 957 (945), desktop-session 2988 (2902, +3.0%): inside
  the +10% budget. **The shell helper's memory is not compared:** each run holds one census of the session's
  explorer.exe, taken at different points of its life (this one at 49 mappings with no images loaded, the
  baseline at 319 mappings and 26 images: 2 / 16 / 8 MiB resident, flexible and direct against 5 / 43 / 117), so the
  difference says nothing; a census at the same moment on both builds is queued.

## Windows 11, phase 1: the desktop is the shell's folder view (2026-10-12)

Plan: [WINDOWS11_PLAN.md](WINDOWS11_PLAN.md). Progman now hosts the shell's own folder view of the Desktop folder (patch 1100, `wine/ps5/pw_shell_desktop_view.h`)
instead of Explorer's array of hand-drawn launchers. What a person can do, each proved on the console by `tests/desktop-view-probe.c` with `SendInput`
(32 checks, [desktop-view-2026-10-12-30](../evidence/desktop-view-2026-10-12-30.json)) and by pictures in `klog/`:

| Behaviour | Where it comes from |
| --- | --- |
| Rubber-band selection, select all, type-ahead, F2 rename, context menu, Escape | The shell's `shlview` and comctl32's listview, now that the desktop's message loop translates keys (1100) and the desktop folder can rename (1101) |
| Dragging icons: the places are remembered per name in `HKCU\Software\Mihawk\Desktop`, snap to a 75 px cell, a free cell is found when one is taken | patch 1102 |
| Files made by programs that send no notification appear (the game launcher writing a shortcut) | A thread of `pw_shell_desktop_view.h` that watches the user's and the Public Desktop (overlapped reads, no polling) |
| Items of both Desktop folders in one view, rename of either | patch 1101 |
| System icons: only the Recycle Bin by default (`HideDesktopIcons\NewStartPanel`), named "This PC" and "Recycle Bin" | 1102, 1103 |
| Real icons on shortcuts, with Windows 11's badge | patch 1104: the old arrow code made a transparent picture of an icon with an alpha channel (PNG icons); both are now composed as ARGB, the badge is drawn in code |
| 48 px icons, soft label shadows, translucent selection plates and rubber band, no dotted focus rectangle | The extra large image list on the desktop's list, and patch 1105 (`LVS_EX_TRANSPARENTSHADOWTEXT`, which Windows' desktop sets, selects this look) |
| The view lies in the work area (above the taskbar) and follows it | `pw_desktop_view_fit` |

**Cost** (probe run, 13 items): the Explorer helper holds 38 MiB resident in the flexible pool (unchanged by the folder view) and peaked at 127 MiB of
direct memory in the run; helpers in all 137 MiB in 9 processes; compositor frame 5.4 ms average, 8.3 ms worst, 194 compositions in the run.

**Not done in phase 1:** Delete and Enter checks, dragging a file between the desktop and an Explorer window, the controller moving the focus (D-pad and
face buttons as keys while the shell is in front), the View, Sort by and New entries of the desktop menu, icon size choices, hover plates.

**Findings worth keeping**
- The first full gate of this tree failed 3 cases for two reasons that were not the desktop. The GPU broker's log reached its file through a pipe, and the
  console's pipe write returned ENOMEM after hours of runs: the broker's results file stayed empty (`gate-2026-10-12-win11-01-host-dmem64`) while the same
  file was complete hours earlier on the same console (`gate-2026-10-12-dmem-03`). The broker now logs straight to its file when the first pipe write is
  refused (graphics patch 0010; `dmem-diag-2026-10-12-11` shows `log=file errno=12` and 86 of 86 buffers made and freed). The pipe memory is the kernel's
  (`sysctl` is not readable from the title); what holds it is not known. And `desktop-session` made its file by process number, which an earlier
  run's leftover could hold: the probe now deletes it first.
- A diagnostic that joins a thread must not run in the title: one did, the title hung, and `tools/kill-hung-title.py --force` ended it.

## Current work: Mihawk OS (`mihawk-os`, 2026-10-09)

**Current status:** **every game now lives in Mihawk OS, and the .NET game
starts from its desktop shortcut** (section "Every game in Mihawk OS"; the
user's direction: one prefix, no prefix per game).

- **The move:** on 2026-10-10 the library moved into the `mihawk` prefix
  (a one-time migration tool, since removed; journalled; backup
  `/data/ps5proton/migration/20261010-131518`).
  - Game folders went onto C:.
  - Each game prefix's saves and settings were copied in.
  - the game launcher's registry was merged.
  - The 8 per-game prefixes were retired.
- **From the launcher, in `mihawk`** (build `09f9e1ce`): the FNA game's OpenGL build and
  DirectX, a native 64-bit game (its first run), the 32-bit native game and the Java file test work. The .NET game
  then failed on a refused executable grant.
- **The .NET game, on build `7f63dc69`:** The grant is retried. The .NET game ran 4/4
  from the launcher and 2/2 from its desktop shortcut at 59.7-59.8 fps,
  with its catalog settings given by the title and Explorer unloaded in game
  mode.
- **Not run since the move:** The Java game (set aside by the user), the game launcher
  (paused) and the earlier emulator experiment (exits 1 when stopped).
- **Gate passed:** [the migration's gate](../evidence/mihawk-games-gate-2026-10-10-01.json)
  passed **63/63 in 1,960.8 s** on `7f63dc69`, with zero fault records and no
  `maxpipekva` message. It covers step 3, its review fixes, patches
  0996-0998, the per-game settings, the shortcuts and the prefix policy.
  game-mode64 and game-mode-dxvk64 unloaded Explorer in 311 and 383 ms and
  restored it in 224 and 225 ms. Committed locally.
- **Next, at the user's request:** The game launcher ahead of the Java game (section "The game launcher in
  Mihawk OS").
- **Earlier:** steps 1 and 2 are qualified and committed (`d63816a`, 61/61).

Priorities, set by the user on 2026-10-10 (replacing the earlier window-first
order):

Target architecture (user, 2026-10-10): every game lives in Mihawk OS with a
desktop shortcut, and starting a game pauses or unloads the desktop so the game
runs as if launched directly. **Game mode**, in three steps measured against
the launcher (`tools/run-game-routes.py`): (1) while a full-screen game is the
foreground window the broker flips its frames directly and the desktop
compositor sleeps; (2) the hidden shell is truly idle (no taskbar polling;
touch keyboard only on demand); (3) the desktop (Explorer, touch keyboard) is
unloaded for every full-screen and borderless full-screen game and restarted
when it ends. Unloaded, never frozen: a frozen Explorer would hang any game that
broadcasts a window message. Steps 1 and 2 are qualified (below). Step 3 and
its permanent gate cases (`game-mode64`, `game-mode-dxvk64`) are built and
pass focused runs; the full gate is next.

1. **Play real games inside Mihawk OS.** First, one console comparison of the
   same game (the .NET game or the FNA game) launched from the launcher and from the
   desktop, measuring frame times. The desktop route copies each completed GPU
   frame to private storage and composes on the CPU, and its frame pacing has
   never been measured. If the desktop is clearly slower, build a path where a
   full-screen game skips composition (Windows' independent flip) before moving
   games over. Then build catalog-to-desktop shortcuts (arguments, working
   directory, per-game environment) and an explicit, reversible prefix
   migration tool. One session hosts one prefix; registries are never blindly
   combined, and saves are left alone until migration is explicit.
2. **The game launcher, through memory-pressure and teardown work**: shell memory near
   capacity (black freeze), the launcher's CEF helper with DXVK at 464 MiB or more, the
   shared-file allocation class still drawn from system memory, the black
   sign-in capture; high-pressure capacity and PS-menu closure under load fold
   in here. The launcher's full-screen mode becomes the default once the game launcher works.
3. **Stability only where a real workload exposes it**: the paused broker
   results file (below), and one look at the wrapped shell counter after a
   fresh reboot.
4. **The earlier emulator experiment is parked.**

Batch related changes per gate (31 min each). Export
`PROTON_TLS_TRUSTED_PORT=1749` and `PROTON_TLS_UNTRUSTED_PORT=1750` for every
gate. Pushes only as AGENTS.md rule 10 allows.

### Every game in Mihawk OS (2026-10-10)

The user's direction: "I don't want a prefix per game. I want ALL of my games
to live inside Mihawk OS", each with a desktop shortcut, and Mihawk OS the most
complete prefix. Design and code: [Mihawk OS](MIHAWK_OS.md#every-game-in-mihawk-os-2026-10-10).

**The move, on the console** (a one-time migration tool, since removed from the tree; survey then apply, every
step journalled; backup `/data/ps5proton/migration/20261010-131518`). A read-only inventory of all prefixes came
first: the per-game prefixes held almost no game state; a game launcher's registry and its browser's keys were the
only game-specific registry. 9 entries moved onto C: (`C:\Games\<name>`, a launcher under `C:\Program Files (x86)`),
one of them a never-catalogued install given an entry; every `game.json` now names the C: executable with
`"prefix": "mihawk"`. 259 prefix files were copied into `mihawk` (two of mihawk's own, displaced by the games' copies,
kept in the backup), 22 registry changes merged, and 6 files rewritten for their new paths (one of them a log,
rewritten by mistake; logs are now excluded). 8 per-game prefixes moved to `/data/ps5proton/retired-prefixes/`. Only
`probe` and `mihawk` remain in `prefixes/`.

**Each entry launched from the library in `mihawk`** (build `09f9e1ce`, the
deployed build; [library-mihawk-2026-10-10-01-*](../evidence/library-mihawk-2026-10-10-01-5.json)):

| Entry | Result |
| --- | --- |
| The FNA game's OpenGL build | 4,953 frames, 59.6 fps |
| The FNA game's DirectX build | 4,878 frames, 59.6 fps |
| A native 64-bit game | 7,458 frames, 58.4 fps: its first run on the console |
| The 32-bit native game | 2,132 frames, 24.1 fps |
| Java file test | `FS_TEST2 PASS: rounds=200 bad=0 held=400`, written by this run |
| The .NET game | **nothing drawn**: the console refused to make a 16 KiB page of its .NET runtime readable, writable and executable (`mprotect ... protection 7 failed`), and the first call into it faulted (`c0000005` on execute) |
| The Java game | **nothing drawn**: the same refusal for LWJGL's native libraries (`protection 5`, its `.text`) |
| The game launcher | 1 frame: its known state (paused behind memory pressure, priority 2) |
| The earlier emulator experiment | its Qt window drew (4 frames at the end), then exit status 1 when the run stopped it |

**Why the .NET game does not start** (investigated at the user's request; the Java game
had problems before and is set aside). Nothing in the move: the .NET game's
helper loaded its .NET runtime from its folder under `C:\Games`. Then a 16 KiB
page of the runtime could not be made executable (`mprotect ... protection 7
failed`), and the first call into it faulted (`c0000005` on execute). Of the
45 .NET game's helper logs kept, 3 hold such a refusal, each in a different place:
Mesa's `.text` (run routes-02 (2026-10-10), no OpenGL, fatal), `ws2_32`'s
`.text` (run routes-03 (2026-10-10), not fatal) and this runtime page. A
payload makes memory executable through the payload SDK's `mprotect`, which
for `PROT_EXEC` calls `kernel_mprotect`. That is `kernel_set_vmem_protection`
in the SDK's `crt1.o`, disassembled here, and it does two things:
- It finds the map entry with kernel memory reads: the process list, the
  vmspace, then a descent of the vm_map's entry tree. None of it holds the
  map's lock.
- It writes the new protection into every entry overlapping the range.

Any failure comes back as `EPERM`. The entry tree is a splay tree that
another thread's mmap, munmap or mprotect rotates, and a .NET runtime has
several such threads. The working hypothesis is that a walk taken during one
misses the entry and the grant is refused. It is not proven.

The fix retries a refused grant up to 16 times; the grant is idempotent.
It also records how many attempts each grant took, so a race (a later
attempt succeeds) is told apart from a lasting refusal (all 16 refused).

On build `7f63dc69` it was run 6 times:
- **Launcher, 4 runs**
  (a run (2026-10-10; record removed from the tree; in git history)):
  all 4 drew, at 59.7-59.8 fps. Two of them caught the race: 4 grants were
  refused at the first attempt and given at the second (a 16 KiB page at
  `0x68000`, module pages at `0xdffc540000` and `0xdfffbf0000`, and a 128 KiB
  range). No grant was refused 16 times; no refusal and no execute fault
  remained.
- **Desktop shortcut, 2 runs**
  (a run (2026-10-10; record removed from the tree; in git history),
  `tools/run-game-routes.py --desktop-only`, nothing added to the session):
  - The title wrote 8 shortcuts in the first session and found them
    unchanged in the second.
  - The .NET game started through its shortcut in `C:\users\Public\Desktop` and
    got its 2 catalog settings from the title.
  - It entered game mode, and Explorer unloaded in 224 and 216 ms.
  - 59.8 and 59.7 fps, worst p99 24.75 and 25.0 ms, max 29.9 and 29.3 ms;
    system memory 251 and 250 MiB with 8 helpers.
  - Each session ended cleanly on the stop.

So the refusals are transient: a second attempt succeeds. That fits the
unlocked walk, though the walk itself is not observed. The SDK stays as it
is; it is the payload SDK the loader is built with, outside this repository.
The Java game's LWJGL refusals are the same call and should be helped too, but
the Java game is set aside.

**The build on a new machine** (2026-10-10, the user moved the session to an
i5-10600K host). Two inputs came from outside the pins and stopped it:
- **The Vulkan registry:** `tools/generate-wine.sh` took `vk.xml` from the
  host's `vulkan-headers` (1.4.363, another hash). It now takes the copy in
  the pinned Wine tree (`dlls/winevulkan`), still checked by hash.
- **The RADV archive:** The GPU client's check pinned the bytes of
  `../PS5_Vulkan`'s RADV archive. That archive had just been rebuilt there at
  Mesa `47e9eb6` (the user's 119.88 Hz commit), and an archive rebuilt
  elsewhere differs byte for byte anyway. Three changes:
  - `tools/setup-pinned-radv.sh` now exports PS5_Vulkan at the foundation's
    pinned revision into `.deps`, read-only on the sibling.
  - It builds that revision's RADV release there (Mesa `7b59ef2`, SDK
    `b83202be`, 1 min 48 s).
  - Graphics patch `0008` checks the archive's PROVENANCE instead of its
    hash.

  The title and both runtime builds link that archive, and the build log
  names only `7b59ef2`. Adopting `47e9eb6` is a separate, deliberate step.

**Not yet on the console** (built next, then the gate):
- Desktop shortcuts: the title writes one `.lnk` per library game into
  `C:\users\Public\Desktop` before Explorer starts (`src/pw_shell_link.h`).
  It is read back by Wine's own `IShellLink` on the host (`tools/check-shell-link.sh`).
- Each game's own settings in the session: the title finds the new process's
  catalog entry and gives its helper the settings (`src/pw_app_environment.h`,
  patch 0998).
- The prefix policy: every catalog game runs in `mihawk`; an explicit
  `"prefix"` still wins (the gate's throwaway prefixes now name theirs).

### The game launcher in Mihawk OS (2026-10-10, the user's next priority)

The library run ([library-mihawk-2026-10-10-01-9](../evidence/library-mihawk-2026-10-10-01-9.json),
launcher layout, 150 s, build `09f9e1ce`) was analysed from its logs by four
readers and a synthesis (no console time). It corrects the library table: the
one frame shown was gldriverquery64's probe window, not the game launcher's.

- **The game launcher ran clean.** It ran 165 s and shut down with status 0.
  - No helper crashed, and no CEF process was restarted.
  - the launcher's main executable started CEF: the browser (pid 418, owner of the 700x440
    sign-in window), the GPU process (419) and five more helpers.
  - The 32-bit `gldriverquery.exe` lacks `SDL2.dll`: the game launcher's packaging, not a
    blocker.
- **B1, the sign-in window was drawn and hidden.** The GPU process drew it
  through ANGLE, D3D11, DXVK and RADV: a 700x440 swapchain, 248 frames and 755
  submissions, with no errors. The broker hid all 248 (`hidden=248`).
  - Graphics patch 0001 shows a GPU client's frames only when its process is
    the foreground one (`server.cpp:554`). The foreground was the browser,
    418; Chromium draws only in its GPU process.
  - The run used the per-process route, without window surfaces. The window
    route composes frames by the window they are presented into; the Mihawk
    OS session and the gate's cross-process `gpu-window` probes use it.
  - Next run: the same entry with `WINE_PS5_WINDOW_SURFACES=1` and captures at
    25, 45 and 90 s.
    - Expected if it is the fix: route composed, `hidden=0`, the dialog in the
      captures.
    - If the GPU process presents into a window it does not own,
      `ps5drv.c:977` refuses the surface. Then the window route binds a surface
      to the window's identity rather than the presenter's pid, and gets a
      gate case for it.
- **B2, no connection to the game launcher's servers.** `LogonFailure No Connection` came
  twice, 68 s apart; the HTTPS update check worked.
  - On 2026-10-08, sessions over WebSockets failed every CM ping within about
    a second, while a session over UDP connected.
  - The same client connects under Linux Wine, so the failure is the
    console's.
  - Next: read this run's `connection_log.txt` and `cef_log.txt` (read only).
    Then the game launcher-shaped socket probe against the gate's TLS fixture
    (getaddrinfo, non-blocking connect with WSAEventSelect and
    WSAWaitForMultipleEvents, send and receive, CertGetCertificateChain), on
    Linux Wine first. The step that fails names the layer.
- **B3, memory headroom.** The census peaked at 886 MiB: 16 MiB over the
  870 MiB close mark for 1 s, 54 MiB under the end mark.
  - The GPU process's resident memory went 44, 441, 312, 358, then 44 MiB,
    while its direct memory and Wine's heap barely moved and Wine made no host
    mmap meanwhile. That points to native allocations in the GPU client's
    RADV/ACO through the system libc; this is not proven.
  - Measurement: libc's heap statistics in each helper's memory sample. If
    confirmed, a malloc shim in gpu-client.prx sends those allocations to
    direct memory, for every DXVK, VKD3D and Zink program.
- **The game launcher's own logs** (read only from the console, kept off the
  repository) settle B2.
  - The connection works. The first round's WebSocket pings to Asian
    servers failed; the second round reached every European server; both
    rounds connected over WebSocket.
  - About 65 s after connecting, the server ends each connection with "Try
    another CM / Failure", which the client ignores ("not waiting for a logon
    response"). The sign-in window ("Sign in to") loaded, and its QR session
    poll failed at that disconnect.
  - The Linux Wine client on the host, signed out too, shows exactly the
    same cycle on 2026-10-07: connect, then the same failure 60-70 s later,
    over WebSocket and UDP.
  - So it is how the game launcher's servers treat a connection nobody signs in on, not
    a network fault. B2 is not a blocker until a sign-in is attempted.
- **Window surfaces, first run**
  (run window-01 (2026-10-10; record removed from the tree; in git history),
  build `7f63dc69`, `WINE_PS5_WINDOW_SURFACES=1`, captures at 25, 45 and 90 s).
  - Frames reached the screen: 48 shown in the second 10 s window, and
    captures 01 and 02 were saved on the console.
  - The census peaked at 576 MiB (886 without window surfaces), and the
    shell's RSS held at 540 of 1,030 MiB.
  - About 80 s in, the console stopped answering: klog ended mid-run, and
    control, websrv and FTP timed out. The guard recorded a drop.
  - Memory was not the cause: no growth, no swap, the guard far away.
  - After the user's restart (they report the pointer moved; nothing else
    answered) the records were recovered read-only
    (post-mortem (2026-10-10; record removed from the tree; in git history)).
    - The 3 captures are black except the pointer.
    - The GPU process created its swapchain on a window it does not own: the
      browser's. `ps5drv` refused the surface (`VK_ERROR_SURFACE_LOST_KHR`,
      then `CreateSwapChainForHwnd` failed). So B1's fix is the window route
      accepting a presenter that does not own the window, as Windows does.
      That is general for every Chromium-based program.
    - The helper logs end 78 s in and klog at 80 s. Capture 03 was saved at
      90 s, so the title and broker outlived the network services and the PS
      menu. Memory and the title's sockets were steady.
    - What froze is not in the records: the title's persisted log was empty
      and the broker's record ends at 25 s. The next build keeps a crash-safe
      title log before the game launcher's runs again.

- **The game launcher's sign-in window on the console** (build `056031d6`;
  summary (2026-10-10; record removed from the tree; in git history)).
  - Wine patch 0999 and `src/pw_window_scene` let a process present into a
    window another process owns, as Windows does. The GPU-window probe now
    checks exactly that, and the four GPU-window gate cases pass
    (cross-window-2026-10-10-02).
  - Run 02: the game launcher's frames reached the composed window. Then the GPU
    process's resident memory went from 43 to 465 MiB, outside Wine's direct
    memory (511 to 516 MiB) and outside the broker (host-visible 90 MiB). The
    guard ended the run at 947 MiB. The console answered throughout.
  - Run 03 (one DXVK compiler thread, which changed nothing measurable):
    - The first GPU process spiked the same way, to 433 MiB, and was closed
      from outside (most likely Chromium's GPU watchdog).
    - Its replacement stayed at 43 MiB and drew real content.
    - **The 90 s capture shows the game launcher's sign-in window**: account name,
      password and QR sign-in. The capture is not committed, because it
      holds a live sign-in code.
    - Memory settled at 490 MiB.
  - Gate [cross-window-gate-2026-10-10-01](../evidence/cross-window-gate-2026-10-10-01.json):
    63/63 in 1,949.9 s on `056031d6`, zero fault records; committed locally.
  - Next: B3, the GPU process's native memory spike.
    - Measure which allocator holds it: libc's heap statistics per helper
      sample.
    - Then move it into direct memory, for every DXVK program.
    - B4: typing, and QR sign-in with the user's phone.

- **B4, typing in the dialog.** Not measured. QR sign-in avoids typing but
  needs B2. A script never types credentials.

- **The user's own desktop session** (build `be8a2772`, the user at the console with a
  USB keyboard and mouse through a USB-C hub; the title's own log, kept in `klog/`):
  - USB keyboard and mouse work (`src/pw_usb_input.h`, `native/pw_usb_input_ps5.h`,
    patch 1001). Both system modules loaded after the `/data` grant and opened on the
    first try (keyboard handle `0x19e0700`, mouse `0x9f0700`, one merged mouse handle).
    The logged records match the expected layouts: a 40-byte mouse record with its
    timestamp, connected byte, buttons and motion; a 96-byte keyboard state with
    connected at byte 16 and its key count. 42 key events and about 1,700 mouse events
    reached Windows; the user typed in Notepad and the game launcher.
  - the game launcher opened from its shortcut, signed in, and its service ran this time.
    About 45 s in, the game launcher's GPU process (native 1547) held 462 MiB, the census stood
    past 940 MiB for 3 s, and the guard ended **The whole session**: a black screen,
    then PS5 Proton's menu. Fix: in the desktop session the guard acts on the largest
    process only (patch 1002, input type 7: close its windows, or end a process with
    none) and re-arms; Chromium starts a new GPU process. B3 itself stays open.
  - Desktop launchers: no hover or selection effect, a double click (with the USB
    mouse too) started nothing any log showed, and the shortcut menu had no Open.
    Wine's desktop has no feedback; shell32 shows no verbs for a shortcut. Patch 1000
    now highlights, selects, opens with the default verb on a double click or Enter
    and logs each open (`warn+explorer`), and puts Open first in the menu. The double
    click's cause is not established; the next session's log names it.
  - The user's controls map left click to L2 and right click to R2, so the touchpad's
    two-finger right click (left on the touchpad only) was not in play.
  - Second session (build `64610c29`, Game Mode off): the game launcher reached its library and
    downloaded a game, slowly. The guard ended the game launcher's GPU process (native 1584,
    432 MiB) and not the session ("ending that process, not the desktop session");
    the census then settled near 595 MiB (peak 992 MiB).
  - B3, measured: that GPU process's resident system memory went 356, 311, 376, 46,
    48, 325 MiB while Wine's direct memory stayed 523-541 MiB, Wine's Unix heap
    71 MiB, and its DLL images 302 MiB mapped (the other game launcher's helpers, with
    274 MiB of images each, stayed at 40-60 MiB resident). So neither the heap Wine
    counts nor the images hold it, and the earlier guess (RADV/ACO through libc
    malloc) is not shown. Next: each helper sample walks its mappings
    (`src/pw_vm_census.h`: sceKernelVirtualQuery, residency by mincore) and logs
    the largest resident groups by name and flags, with one raw record in hex to
    check the layout. One game launcher's run names the memory; the fix follows from it.
  - **A second Direct3D 12 game exits 4 s after it starts**, every launch, with
    0xC0000409. Patch 1003 (an unhandled exception's registers and stack) and one
    `+seh` launch (run seh-01 (2026-10-10)) showed the order: the game's protected
    startup code reads address -1 at `a second Direct3D 12 game.exe+0x148C8DE3` (an access violation); no
    handler takes it; the unwinder walks into stack garbage (no unwind data there),
    and the game launcher's GameOverlayRenderer64.dll, reached by that walk, fails its stack
    cookie check in `__GSHandlerCheck` (fail-fast code 2). The overlay is where it
    ends, not the cause; turning it off in the game launcher changed nothing.
  - Reference (the user's PC, Linux game launcher, Proton Experimental, the same build
    22672075, one Denuvo activation): the game runs and builds its D3D12 pipelines.
    Its trace has 56 `SIGSYS, rax 0x36` lines in its first 6 s: the game's own code
    executes `syscall` (static `9d 0f 05 9c`, popf/syscall/pushf, in its 254 MB
    `.text`) with Wine's number for **NtQuerySystemInformation**, and Linux traps it
    for Wine (seccomp / syscall user dispatch). The PS5 kernel has no such trap:
    system call 54 there is `ioctl`, which fails, and the game uses what comes back.
    Every game that makes direct system calls (Denuvo's among them) fails this way.
    No PS5 kernel trap for system calls by caller address is known.
    An owned ptrace tracer for such calls was built and measured on 2026-10-10; its code is archived on the
    local branch `archive/direct-syscall-tracer-2026-10-10` (`1063ef0`) and its record is below
    ("Direct PE64 syscall tracer").
  - At the user's request, a Game Mode switch in Settings (`src/pw_game_mode.h`,
    `game-mode.txt` beside `controls.txt`, on by default): off composes every frame
    (direct-flip reason `off`), so the desktop is never unloaded.

- **Signed in, then stuck on a spinner; the game launcher's Client Service** (build
  `056031d6`, `WINE_PS5_WINDOW_SURFACES=1`). The game launcher's own logs and the captures
  were read from the console and kept in `klog/`, because they show the
  account.
  - run signin-01 (2026-10-10; record removed from the tree; in git history),
    600 s: the user signed in with QR. The game launcher's login log reached `Success`
    23 s after the code was scanned. Cloud sync then finished. The game launcher's
    1280x800 main window ("SP Desktop") was created and drawn at a steady
    30 fps; it showed only the game launcher's loading spinner for the rest of the run,
    more than 5 minutes.
    - the game launcher's JS log printed its "Before Login" startup group but never an
      "After Login" group, which the game launcher prints only when every store it waits
      on has resolved. File and socket activity went flat at about 280 s,
      memory held at 400 MiB, and no helper crashed.
    - `service_log.txt`: `Failed to load the game launcher's Service (GLE 126)`. The user saw
      "the game launcher's service requires some maintenance" with an "Install service"
      button, and then the spinner. CEF created no window for that dialog, so
      it was the launcher's main executable's own modal dialog, covered by the main window a
      second later, and the run had no taskbar to bring it back.
    - Cause: the prefix had no game launcher's Client Service. There was no
      `Common Files\the game launcher` folder and no `Services\the game launcher's Client Service`
      registry key, because the game launcher's files were copied into Mihawk OS rather
      than installed by its setup program, which runs The launcher's service installer (`/install`).
  - run service-install-01 (2026-10-10; record removed from the tree; in git history):
    that step, once, through a new `tools/run-console.py --program` (the game
    entry's prefix and settings, another program). It ran 10 s, wrote
    its service executable under `Common Files` and registered the service with
    `ImagePath` `... /RunAsService`, `Start=3` (on demand), as on Windows.
  - run library-01 (2026-10-10; record removed from the tree; in git history),
    340 s: the game launcher signed in by itself (saved credentials). No dialog appeared.
    "After Login" completed in 42.5 s (AppStore 23.3 s). The 240 s capture
    shows the Store, the Friends list online with friends' activity, and
    the game launcher's "What's New" popup, all drawn correctly. The run ended by
    stop-game; the game launcher's exit status 1 there is what the runner reports as
    failed.
    - The memory census peaked at 972 MiB: the first GPU process (native
      1506) reached 343 MiB and was over 870 MiB for 7 of 30 s, then ended
      (Chromium replaced it, as in run 03). The guard did not end the run.
      This is B3, still open.
  - Still open: the game launcher does not reach its service (`Failed to create Service
    pipe (GLE 2)`): the service is registered but not started. The game launcher works
    without it; games with anti-cheat or that need the game launcher's elevated helpers
    may not. The `--program` change is uncommitted until the next gate.

### Direct PE64 syscall tracer (2026-10-10; archived 2026-10-18)

Archived on 2026-10-18 at the owner's decision: the code (the tracer, its probes and runners, Wine patch
`1004-ntdll-ps5-traced-direct-syscalls`, which collides with main's 1004) is on the local branch
`archive/direct-syscall-tracer-2026-10-10` (`1063ef0`), not on main. It never passed a Wine-integrated console run
(`direct-syscall-pro-2026-10-10-01` to `07`) and costs about 170 µs per traced call. The elfldr upload encoder that
came with it (a boundary absent from the binary) is on main on its own, gated with patch 1172 (gate-2026-10-18-item3-01). What follows is the
record as it stood on 2026-10-11.

At the user's request, implement a global opt-in interceptor, preserving Wine's
service table, thread and stack-argument semantics. No application instruction
patches or game-specific service numbers. Details: [SYSCALL_TRACE.md](SYSCALL_TRACE.md).

- Owned native Linux and Pro probes pass (`syscall-trace-pro-gate-2026-10-10-03`):
  PT_ATTACH and PT_TO_SCE stop entry, PT_SETREGS substitutes getpid before the
  original one-byte pipe write executes, and an owned return gate restores the
  original registers. The pipe stays empty and the owned dispatcher runs on the
  stopped thread. Pro tracer time 2 ms, entire paired run 0.463 s; idle and no
  surviving helpers afterward. The shared scoped debugger-identity adapter restores
  its own authentication ID and capabilities after every request. Both SDK
  syscall shims lack errno normalization; the shared adapter captures carry
  explicitly. The owned invalid-register-buffer check returns EFAULT (14).
- The owned PE64 Linux Wine oracle passes 338 checks in two processes, with four
  worker threads per process and one descendant. Imported APIs are the comparison
  for output buffers, errors, time and six-argument virtual-memory allocation.
  Evidence: `direct-syscall-host-2026-10-10-02.json`.
- Candidate: native PRX executable ranges plus the payload ELF are published to a
  shared classification page. Other PE64 syscall entries are neutralized, original
  register state restored at the return gate, and SIGSYS delivered to Wine's
  existing dispatcher on that same thread. `WINE_PS5_DIRECT_SYSCALLS=1` enables it;
  production defaults remain off. Descendants inherit the opt-in.
- First Wine run (`direct-syscall-pro-2026-10-10-01`) failed before helper
  rendezvous: HTTP EOF/reset, no Windows probe or interception. An independent
  upload of the same loader succeeded (bootstrap evidence). The loader contained
  the exact fixed multipart closing delimiter at ELF offset 122032. The shared
  upload encoder now chooses a boundary absent from the binary and is used by
  title, Wine spawning and tracer spawning. Its sanitized regression scans beyond
  NUL and skips two embedded delimiter candidates. Run 05 proves corrected uploads
  bootstrap the helper and tracer; the full exact-source gate remains pending.
- Run `direct-syscall-pro-2026-10-10-05` reaches the owned PE64 query (service 54,
  probe PC 0x14000100b) and substitutes the native operation. No SIGSYS delivery
  is proven: the root fails in stop-info after one interception; two service
  helpers also mistake the kernel signal-return stub for PE code. The public
  module-list wrapper returns only eboot; read-only sys_dynlib_get_list (592)
  enumerates all six native modules, resolving the earlier native recvfrom error.
  Thread-ID register access works; PT_TO_SCE resumes by process ID (a TID gives
  EALREADY). The root records 214047 native entries before failure; no performance
  acceptance. Run 04 repeated the previous loader after a failed build and does
  not qualify the metadata fix.
- Cleanup: run 05 left three terminated helper entries (state 5) owned by websrv.
  They fail the existing process-census gate; tracer processes are now included
  in that census too. The user confirmed the Pro rebooted and ready afterward;
  fresh-boot bookkeeping was reset only on that confirmation.
- Three focused native probes pass after reboot: entry-gate-01, signal-gate-01
  and thread-gate-01 (see `syscall-trace-pro-*-2026-10-10-01.json`). They qualify
  PT_TO_SCE at the return gate, TID register read/write with PID continuation,
  and an owned native signal handler identifying the kernel's exact read-only
  executable trampoline mapping. All finish with no surviving helpers/tracers.
  KERN_PROC_SIGTRAMP returns four zero bytes on this firmware, so the candidate
  no longer assumes FreeBSD's address record. It validates the observed native
  return address through sceKernelVirtualQuery before publishing that mapping.
- Entry stops retain stale PL_FLAG_SI from the attach SIGSTOP (signo=17/code=2),
  so the candidate treats SIGTRAP+SCE as syscall entry irrespective of that flag;
  the exact owned return gate is recognized before syscall classification.
  Real breakpoint traps outside that gate still reach Wine.
- Candidate adds stop/thread and ten-second progress diagnostics and refuses a
  success verdict with an undelivered call. Local classifier/upload sanitizer
  tests, cleanup guards and battery integration pass (13 host groups,
  `syscall-trace-local-boundaries-2026-10-10-01`). Next: owned PE64
  threads/descendant probe, then full exact-source gate. No commit/game-fix claim.
- Run `direct-syscall-pro-2026-10-10-06` is a failure: watchdog after 241 s,
  no owned PE verdict and zero intercepted Windows calls. Ten native clients
  record 1,142,004 entries in total; tracing startup is too slow, with service
  restart activity. Signal-return classification no longer misroutes those
  shutdown calls. All ten clients remain terminated (state 5) under websrv,
  so the console census blocks another launch. The user has been asked to
  reboot; no new boot is recorded without confirmation.
- Follow-up, native qualification below: scope the dedicated tracer's own debug identity
  once per session, restore and verify it at teardown; detach at a classified
  native SYS_exit with no pending Windows call, so websrv remains responsible
  for reaping its child. Failure releases tracing only with SIGKILL. No other
  process credentials or kernel instructions change. Native diagnostic logging
  is bounded to eight incidental signal stops, plus pending calls/progress.
- The focused console command is `tools/run-tracer-lifecycle.py --name <fresh>`:
  compiles the production tracer into an owned native probe, compares 4096
  getpid results and timing before/after attachment, and requires native-exit
  detach plus no surviving helpers/tracers. It launches no title or Wine. The
  scoped owned return-gate probe must also pass. Only after both qualify can
  Wine run again. No code commit before full exact-source console gate.
- After explicit fresh-boot confirmation, production-tracer lifecycle run 01
  passes: 4096 correct calls, 3.673 ms baseline versus 616.876 ms traced,
  4281 native entries, classified native-exit detach and empty cleanup census.
  The scoped return-gate run 01 then fails before interception: a tracer-only
  SIGSEGV, not a console outage. The preserved klog and exact ELF locate the
  null write in event() after PT_LWPINFO. The kernel clears R8/R9, but the raw
  adapter omitted their clobbers; the compiler kept event() output pointers
  in those registers. Both payloads disappear, so no additional reboot is needed.
- Shared adapter now declares R8/R9 clobbers and the fourth argument register
  as mutable. `syscall-trace-pro-scoped-gate-2026-10-10-02` passes native entry
  prevention, same-thread register restoration/dispatch, EFAULT normalization
  and verified credential restoration in 0.431 s (tracer 6 ms), with no leftovers.
  Its Linux oracle also passes. On the identical adapter,
  `tracer-lifecycle-pro-2026-10-10-02` passes all 4096 call results, 4281 entries,
  native-exit detach and empty census in 1.256 s. Baseline 3.652 ms, traced
  697.192 ms: about 170 microseconds per traced call, roughly 191 times this
  minimal untraced getpid sample. This is an overhead measurement, not a
  game-performance result or a before/after speedup comparison with run 06.
  Next: bounded owned PE64 Wine dispatch/threads/descendant qualification.
  Wine SIGSYS delivery, game compatibility and the full exact-source gate
  remain unqualified; no code commit or public claim.
- Bounded Windows run `direct-syscall-pro-2026-10-10-07` fails after 177.7 s
  (240 s watchdog, normal owner return, initial status 1). Exact loader manifest
  is retained in its evidence. Wine finishes prefix baseline preparation and
  reaches the owned PE64 query, service 54 at 0x14000100b. The tracer substitutes
  getpid, but never records the expected return-gate stop: foreign=1, delivered=0,
  pending deadline ETIMEDOUT (60). Wine instead reports EXCEPTION_BREAKPOINT at
  the exact owned gate, 0x200010b14 (symbolized against this run's ELF), and starts
  its debugger. The observed intervening SCX stops are in native signal-return
  code. The simple native probe therefore does not qualify the gate in Wine's
  signal-handler/multiple-thread environment. No Windows dispatch success claim.
- Run 07 totals 3,404,186 native entries across ten tracers. Eight end at
  classified native exit, one observes final process exit instead, and the root
  fails its pending deadline. Read-only post-run control is idle, but one forced
  service helper remains terminated (state 5) under websrv. Hooks are removed;
  cleanup fails and the existing census blocks another launch until fresh boot.
  This is not a network outage or a console-crash claim. Native-exit detach
  alone does not qualify externally terminated service cleanup.
- Local follow-up bounds pending-stop diagnostics to 32 records; run 07 wrote
  13019 trace lines while awaiting one gate. Not console-qualified. Before another
  full Wine run, isolate gate interception with an installed native SIGTRAP
  handler and concurrent threads, and qualify forced termination/reaping. Fix
  the demonstrated signal/lifecycle boundary globally, then repeat the owned
  Windows oracle; do not extend the watchdog or bypass the cleanup census.
- Small probes are prepared locally while the user reboots. Linux oracles
  `syscall-trace-host-handled-gate-2026-10-10-01` and
  `syscall-trace-host-threaded-gate-2026-10-10-01` both pass: installed
  SA_SIGINFO/SA_ONSTACK SIGTRAP handler, respectively zero and two workers,
  zero handler hits, prevented pipe write and restored owned dispatch. Native
  builds pass with warnings treated as errors. Linux does not qualify the Pro's
  all-thread tracing or signal delivery. The forced lifecycle case builds with
  the actual production tracer and self-SIGKILL after the 4096-call timing loop;
  its verdict requires an observed SIGKILL exit and empty helper/tracer census.
  This isolates signal termination; it does not yet qualify a racing owner kill.
  No console run this preparation step. Await the user's fresh-boot/ready
  confirmation, then run `redirect-handled`, `redirect-threaded` and
  `run-tracer-lifecycle.py --case forced` sequentially, stopping on any survivor.

### The second game launcher, the game launcher's memory and registry (2026-10-11, worktree `mihawk-os-own`)

- **The second game launcher (32-bit, CEF / Chromium 108) reaches its sign-in window and its Home page.** It stayed on a spinner
  because every CEF *renderer* ended about 1 s after it started, not because of its GPU process.
  - Run trace-03 (2026-10-11; record removed from the tree; in git history) (build `a5ed7a27`, 341.9 s):
    11 of 11 renderers ended on `0x80000003` at `libcef.dll+0x16D00E1`; the second game launcher reloaded the page every 5 s, 10 times, and
    stopped ("Render process was terminated and reloaded 10 times").
  - The address is an `int3` after `VirtualProtect(page, 0x1000, PAGE_READONLY, &old)` returned `old != PAGE_READWRITE`: a Chromium
    check that it seals a data page it wrote (the only caller sets a flag byte, then seals the page at image offset
    `0x97e9000`). Wine returned `PAGE_WRITECOPY` (8): a program's write to a copy-on-write image page is invisible to it (the host maps
    the page writable), and Windows reports `PAGE_READWRITE` once the page is a private copy.
  - Proton's Wine already has `simulate_writecopy` for this (the first protection change of a copy-on-write page counts as copied) and
    switches it on for two launchers' executables by `argv[1]`. A PS5 helper has no such argv, and
    `WINE_SIMULATE_WRITECOPY` is not among the variables helpers inherit
    (run writecopy-01 (2026-10-11; record removed from the tree; in git history): the variable set, 11 renderers still ended).
  - Patch 1004 turns it on for every process on the PS5 (`WINE_SIMULATE_WRITECOPY=0` turns it off). The new `image-protect32/64` gate
    probe writes two data pages of its image and seals each: with the simulation off `old=0x8`
    ([image-protect32-off-2026-10-11-01](../evidence/image-protect32-off-2026-10-11-01.json), `image-protect64-off-2026-10-11-01`),
    with it on both PASS ([image-protect32-on-2026-10-11-02](../evidence/image-protect32-on-2026-10-11-02.json),
    [64](../evidence/image-protect64-on-2026-10-11-02.json)). `VirtualQuery` before a page's first protection change still says
    `PAGE_WRITECOPY`; nothing observed needs it.
  - Run fix-01 (2026-10-11; record removed from the tree; in git history) (build `3efb5a17`, 324 s): 0 unhandled exceptions in
    every helper (25 logs). The 60 s capture is the second game launcher's sign-in window; the 200 s capture is the signed-in main window (friends, the Unity game page).
    The 280 s capture shows "Oops! Something went wrong" in the content area (frame connection timeouts); see the GPU watchdog below.
    Run fix-02 (2026-10-11; record removed from the tree; in git history) (305.6 s): 0 exceptions; the 220 s capture is the
    Home page with its "What's New?" dialog. Captures stay in `klog/` (they show the account).
  - **The GPU hypotheses, settled by the logs:** (a) the first GPU process initialised (DXVK device, feature level 11_1, ANGLE
    D3D11 through `d3d11.dll` native) and ran to the end of the pre-fix run, so it did not fail; (b) `gpu_init.cc(523)` is a
    `LOG(ERROR)` of Chromium's software-compositing GPU process (`--use-gl=disabled`, the display compositor with no GL), which
    exits with status 0 (`NtTerminateProcess ... exit_code 0`); the second game launcher's browser starts it 120 s after the first GPU process in
    all three runs; it is not a crash and not a loop; (c) DXVK's "Failed to create global buffer, 4 MiB" warning appears in every
    DXVK process, including those that work; not investigated further.
  - **Open: Chromium's GPU watchdog.** In run 01 the first GPU process (native 475) called `NtTerminateProcess(-1, 0xC0000005)` from
    a watchdog thread at +125 s (its memory was flat at 44 MiB resident; the browser replaced it). Run 02 did not show it. The game launcher's GPU
    process ended the same way in run window-03 (2026-10-10). The cause (which stall the watchdog saw) is not known. Chromium's
    own log at `--log-severity=verbose` did not reach the libcef log: the second game launcher passes `--log-severity=error` to its children.
- **The game launcher's install scripts (Task 3, from the game launcher's own logs and the prefix registry, read over FTP; no new run).**
  - `runprocess_log.txt` shows the two full script runs (DirectX, Visual C++ 2019 and 2022, x86 and x64) at 21:00 and 21:22, and none
    in the three launches after 21:22. The markers the game launcher keeps are in the prefix
    (`HKLM\Software\Wow6432Node\Valve\the game launcher\Apps\CommonRedist\...`: `dxsetup`, `x64`/`x86 14.28.29334.0`, `14.51.36247.0`), written
    at 21:22 and 21:25. So no step is "unfinished" now; the first run's markers never reached the disk.
  - Why: the wineserver saves the registry only when its last process ends (and when the title asks, patch 0886). A session that is
    ended from outside (the memory guard, the PS button, a crash) loses everything it wrote. The title now asks for a save every 30 s
    (`native/proton_wine.cpp`; the server skips a clean registry). Not yet run on the console (see below).
  - `Failed to load the game launcher's Service (GLE 126)` is the only service line in every session since the service was installed, once per
    start; its service DLL (PE32; imports only KERNEL32, USER32, ADVAPI32, SHELL32, ole32, OLEAUT32, SHLWAPI, PSAPI, CFGMGR32,
    SETUPAPI, WS2_32, bcrypt) is in both `the game launcher\bin` and `Common Files\the game launcher`. Which load fails needs `+loaddll,warn+module` in one
    the game launcher's run. The "Failed to create Service pipe (GLE 2)" lines are from the sessions before 22:51.
- **B3 (Task 2): the census record layout is confirmed** against the first hex line in a helper log
  (`start 0x10000, end 0x110000, protection 3, type 12, flags 0x12, name "anon:0008004e7b35"`: the fields of `src/pw_vm_census.h`).
  the second game launcher's 32-bit GPU process shows its resident memory flat (43 to 46 MiB) with `anon:00080167ccd6` (31 MiB) the largest
  group. The game launcher's run for the memory census, run census-01 (2026-10-11; record removed from the tree; in git history), ended 20 s in:
  the klog shows every helper closed and "gpu-windows stopped" (why is not in the records: no memory guard line, 8.2 GiB direct
  memory free), and the control payload then timed out once on the end-of-run check, so `tools/console_guard.py` recorded the console as dropped. The console answered afterwards (FTP, the payload's
  `ping`, 9111 open), so this looks like a transient timeout, but the guard's rule is a restart by hand and `new-boot`, so no
  further launches were made. The census, the .NET game timing, the registry-flush proof, the game launcher's service load and Task 4 wait on that.

- **The RAM a program is told (user, 2026-10-11: the second game launcher's the Unity game's install said "Your RAM: 665 MB, Minimum: 8 GB").**
  - New probe `system-memory32/64` (a gate case) prints GlobalMemoryStatusEx and checks it against itself. Before any change, in PE32 and PE64
    ([system-memory64-before-2026-10-11-01](../evidence/system-memory64-before-2026-10-11-01.json)): total 665 MiB, free 0, commit limit 0,
    load 100 %, installed 681,388 KiB: three failures.
  - Why: the total is `sysconf(_SC_PHYS_PAGES)` (a small pool of the process, not the console's memory); the free figure is patch 0912's
    direct-memory question, which a helper's kernel answers with ENOMEM for every range (a helper is a payload; its direct allocations are
    `...ForApp`, from the application's pool, so the plain question reaches the system partition, which is empty); the commit limit
    follows from the total (0 MiB).
  - Patch 1005: the total is `sceKernelGetDirectMemorySize()` (11,711 MiB) in both SystemBasicInformation and the performance
    figures; a free figure of 0 from the direct question falls back to the helper's free flexible memory (8,038 MiB in a helper), capped at
    the total. After, PE32 and PE64 PASS ([system-memory64-after-2026-10-11-02](../evidence/system-memory64-after-2026-10-11-02.json)):
    total 11,711, free 8,038, commit limit 11,712, load 31 %, installed 11,993,084 KiB.
  - Not the same number as B3: the 665 MiB is not the 1 GiB pool the guard watches (that pool is the system RSS budget, "RSS x / 997.7" in
    SceShellCore's "VM Stats" klog line); the two share only that helpers' native memory is small next to what the console has.
- **B3 measured (run census-02 (2026-10-11) to 04, run kernel-map-01 (2026-10-11) and 02).**
  - The game launcher's GPU process's resident count (`sceKernelGetResidentFmemCount`) goes 44, 434, 321, 359, 48 ... 335, 323, 362, 44 MiB in
    plateaus of about 30 s, as before. The mapping walk of the same instant stays at 89 to 130 MiB, direct mappings (about 585 to 640 MiB, all
    of Wine's pool as `anon:0008004e7b95`, flags 0x12) counted apart and reservations counted too (0 resident). So the swing is in no mapping
    `sceKernelVirtualQuery` lists: its largest mappings at the spike are the same as at the base (176 MiB direct, 64 MiB flexible).
  - The kernel's own map of that process (`tools/console-memory.py map=PID`, the launcher's GPU process, pid 387, at a spike): type 1 (anonymous), 8
    regions, 228 MiB of address space, 14,613 resident pages (16 KiB), one region of 198 MiB with 12,086 of them (`refs=10`, shared:
    the physical-shared memory of the memory helper, type 0x0c, which "eagerly consumes flexible backing", `native/pw_physical_shared.h`);
    type 4 (direct memory) 598 MiB mapped, 9,451 resident; images 166 pages. The 8 regions are invisible to the walk.
  - So the spike is the GPU broker's host-visible memory (the title's `proton_gpu_host_limit`, 1 GiB per program by default, made by the memory
    helper as physical shared memory and mapped by the helper), allocated and freed by DXVK in chunks. The census of 2026-10-10
    pointed at native heap from the GPU driver path and was wrong: it is memory the transport allocator makes eagerly resident.
  - Added to the helper's census line: `direct_resident`, `reserved_resident`, and `vm big` (the largest mappings with addresses, to match
    the kernel's map). Not yet decided: the host-visible limit or what allocates the 198 MiB region. The test with
    `DXVK_CONFIG=dxvk.maxChunkSize = 32` (run chunk32-02 (2026-10-11; record removed from the tree; in git history): peak 305 MiB against
    431 to 459 in earlier runs) proves nothing: this DXVK build has no such setting (its chunk sizes are fixed in
    `determineMaxChunkSize`: 256 MiB device, 64 MiB host-visible, halved until 7 fit the heap), so the difference is run-to-run
    variance. DXVK logs each mapped region (address, size) at `DXVK_LOG_LEVEL=debug`; that is the next instrument.
  - **Console freeze, 2026-10-11 (run dxvk-debug-01 (2026-10-11), the game launcher, `DXVK_LOG_LEVEL=debug`):** The user's screen went black and
    unresponsive during the run. The klog ends at the title's "ending process ..." lines, after the run's 150 s budget (the last
    "alive" line is second 170; census peak 980 MiB, 0 s at the 870 MiB mark, no guard action), i.e. in the stop of the game launcher, not
    during its load. The control payload then reset connections and the guard recorded a drop after its new 60 s retry. Earlier game launcher
    runs ended at their stop without trouble, and the game launcher freezes before (run window-01 (2026-10-10) at about 80 s) were at other times,
    so the cause is not named; DXVK's debug logging is the one thing this run added. The console must be restarted by hand.
  - **Lesson, recorded because it cost the console:** `tools/console-memory.py` (and any payload sent through websrv's launcher) must
    never run while a title is running: the title's helper spawns go through the same elfldr one at a time, and four polls 2 s apart made
    the game launcher's run's helper upload fail after 160 s (errno 35, "payload loader failed at upload"), then websrv reset every connection and did not
    recover in 3 minutes. The game was stopped (stop-game, `kill`); websrv and elfldr must be reloaded by hand.

- **Results after the console was restarted (2026-10-11, afternoon).**
  - **The registry reaches the disk during a session**
    ([registry-flush-2026-10-11-03](../evidence/registry-flush-2026-10-11-03.json), `tools/run-registry-flush.py`, probe
    `registry-flush-probe.c` setting one value and idling 60 s; nothing is killed). The value on disk before the run was `000a8645`; at 12 s
    (before the title's first 30 s request) it was still that value, and at 45 s, with the program still running, it was this run's
    `000bbfec`. A first attempt (`-01`, with `RegFlushKey` in the probe) showed the value at 12 s: in this Wine `RegFlushKey` makes the client
    write the hive, which hid what was being tested, so the probe does not call it (the game launcher's scripts do not). So a session cut short now loses
    at most 30 s of registry writes, not all of them.
  - **The game launcher's Client Service starts on demand in our runtime.** Every game launcher start since the service was installed has a
    service process (`/RunAsService`, this run's title log) and a `MSG: Startup` line in its service log within a second
    of the game launcher's own line (11:23:19 for the 15:23:18 UTC `Failed to load the game launcher's Service (GLE 126)`); no "Failed to create Service pipe" line is
    written after the 22:51 session of 2026-10-10. The GLE 126 line is the game launcher's own attempt to load the service in-process and appears once per start;
    nothing was found that depends on it. The run's helper logs were not collected (the runner stopped at its end-of-run check, see below), so the
    module load itself (`+loaddll`) was not seen; not needed unless the game launcher feature that wants the service fails.
  - **The guard's retry works in practice:** at the end of run service-load-01 (2026-10-11) the control payload timed out once and answered
    on the first retry ("the console answered again after 1 tries; not recorded as dropped"). Both earlier false alarms and the freeze
    happened while the game launcher was being stopped; the retry costs nothing when the console is fine.
  - **The .NET game (profile 1) on the current build**, three 60 s runs
    (run timing-01 (2026-10-11; record removed from the tree; in git history)): 2,514, 2,739 and 2,845 frames shown in the first 40 s windows,
    about 50 to 59 fps on the direct route, longest gap 0.78 to 1.12 s. The time to the first frame is not comparable with 2026-10-03's 8.4 s:
    the title's `perf: first <api> frame` line, which that figure came from, was not printed in these runs, and the first "frames shown" line
    covers the second 10 s window after Wine started, so the first frame was shown within about 25 s including wineboot. No before/after for the memory work
    was possible (no B3 change has been made). Not a finding of slow loading.
  - **A Go game**: the first `--program` runs ended in 0.3 s: the game printed "open external/script/main.lua: Path not found" and
    panicked. The cause was the runner, not the game: `--program` kept the game launcher's entry's start folder and its arguments, so the game started in the game launcher's
    folder with the game launcher's switches. `native/proton_wine.cpp` now starts a program run by path in its own folder with no arguments
    (as Explorer does). run 03 (2026-10-11; record removed from the tree; in git history): OpenGL 4.6 through Zink on RADV, the game's title demo is drawn
    (capture 02 shows a match at 90 s). The "Failed to open BGM" lines are real: its `sound` folder only has a `.keep` file.
  - **A Unity game** (the user was to re-copy its 0-byte `swiftshader` file, not done): the NW.js
    browser process ran the whole 150 s, with 7 utility, 1 renderer, 2 crashpad and 3 `chrome` processes and no GPU process; two crash reports
    were being uploaded by crashpad (no unhandled exception is in the `+seh` logs, so which process crashed is not known), and every capture is black
    (run 03 (2026-10-11; record removed from the tree; in git history)). Not investigated further until the `swiftshader` folder is whole.

- **Gate, 2026-10-11** ([gate-own-2026-10-11-04](../evidence/gate-own-2026-10-11-04.json)): 67 of 67 cases passed on build `693e14e1`,
  including the new `image-protect32/64` and `system-memory32/64`. Three earlier batteries on the same code failed on things that were not the
  code, each fixed in the case and not by skipping it:
  - `-01` (65 of 67): `game-mode64` and `game-mode-dxvk64` ran with the player's Game Mode setting `off` ("Game Mode: off" in their log). The
    cases test game mode, so `tools/run-game-mode.py` now sets it on for its run and puts the player's value back.
  - `-02` (66 of 67): `shell64`, "New folder command creates real directory": the probe looked once, 250 ms after the click, and the
    folder was there in 5 runs of 6; it now waits up to 4 s.
  - `-03` (66 of 67): `shell32`, "disposable new folder removed error=32" (Explorer still held the folder after Escape), then "owned paste
    directory cleaned error=145"; the cleanup now retries for up to 4 s. `shell32` and `shell64` then passed 3 of 3 together before `-04`.

- **Download speed (user, 2026-10-11: the Unity game's download in the second game launcher ran at 2.5 MB/s on a 1 Gbps line; the RAM dialog is gone).**
  Measured layer by layer on the idle console, with a sender on this PC that does 6,379 MB/s to its own loopback:
  - **Network, Wine versus native:** `tests/net-throughput-probe.c` (PE64 and PE32,
    [net-throughput-2026-10-11-01](../evidence/net-throughput-2026-10-11-01.json), [-02](../evidence/net-throughput-2026-10-11-02.json))
    got 14.3 / 19.0 / 19.2 MB/s for blocking recv of 4 KiB / 64 KiB / 1 MiB, 16.4 for a libcurl-style non-blocking select loop, and 33.4 to
    35.1 for four connections together. The native payload `tools/console-iobench.py` (no Wine) got 18.1 to 19.8 MB/s on one stream. So Wine's
    winsock adds nothing; about 19 MB/s per stream is what this LAN path gives (it is not a 1 Gbps path: the console may be on Wi-Fi), and
    several connections add up to about 35 MB/s.
  - **Disk, native:** `/data` is a `nullfs` over `/user/data`, `bfs` on `ssd0.user`, not mounted synchronous (flags 0x1000). A `write()` costs
    0.74 ms for 4 KiB (5.5 MB/s), 1.14 ms for 16 KiB (14.4), 2.8 ms for 64 KiB (23.7), 2.6 ms for 128 KiB (51), 3.5 ms for 256 KiB (74),
    5.4 ms for 512 KiB (97), 7.9 ms for 1 MiB (134), and 8 MiB writes reach 217 MB/s. Preallocating the file changes nothing;
    O_APPEND, O_DIRECT and O_SYNC do not help (5.7, 3.6, 3.0 MB/s at 4 KiB); `/data` and `/user/data` are the same. Four writers of
    4 KiB reach 12.9 MB/s together, eight 18.0, four of 64 KiB 35.2. Copying into a shared mapping runs at 660 MB/s and flushing it
    with msync at 70 MB/s whatever the block size, so the cost is per `write()` call. Reads come from the cache (700 to 2,400 MB/s).
  - **Disk, in Wine:** The same probe's 64 KiB writes reached 14.1 MB/s (PE64) and 14.0 (PE32), against 22 to 24 MB/s natively: 4.6 ms per
    write inside `NtWriteFile` (patch 1006's counters) where the native call takes 2.8 ms; why Wine's call is slower is not known.
  - **So the 2.5 MB/s is not the network (19 to 35 MB/s) and not Wine's socket layer.** A downloader that writes small blocks is held to the
    rates above: 4 KiB writes give 5.5 MB/s at best, before the decompression and TLS work. What the second game launcher's Agent writes is not known yet:
    patch 1006 counts regular-file writes per helper (calls, bytes, time, five size buckets) and the helper's census line
    "wine-child: file writes calls=... le4k=... le16k=... le64k=... le256k=... bigger=... slowest_ms=..." reports them every 10 s; the next
    the Unity game's download will show the sizes. Fixes under consideration, none made: fewer and larger writes (coalescing in Wine's
    `NtWriteFile` by handle, flushed at close, flush, read, seek and size queries, with a time bound; a risk for another process reading the file
    at once) or writing through a shared mapping, which is coherent with other readers in the page cache.
  - **The console froze again** (run ui-01 (2026-10-11; record removed from the tree; in git history)): the second game launcher's run (165 s budget) was at
    second 180 when its klog stopped on "No heart beat of Shell UI has been observed for 4159 msec"; the control payload reset connections and
    FTP timed out for more than 2 minutes. Freezes now: run window-01 (2026-10-10) (80 s), run dxvk-debug-01 (2026-10-11) (170 s), this one (180 s);
    each in a long Chromium run. See the next item: the cause is the system's resident memory crossing the kernel's limit (shown for the
    last of them); the registry request is not it, as the first of them (2026-10-10) predates it.

- **The console froze because a Chromium GPU process pushed the system's resident memory past the kernel's limit
  (run ui-02 (2026-10-11; record removed from the tree; in git history); I started that run, a scripted second game launcher's session).**
  The klog stops 55 s into the run. The title's census read 670 MiB at 40 s and 919 MiB at 50 s (the largest helper, the GPU process, 331 MiB),
  and SceShellCore's "VM Stats" line, which carries the kernel's own limit, read `RSS 627.5 of 988.9` then `RSS 928.9 of 988.9`, and the last line
  before the console stopped read `RSS 1026.2 of 988.9` with swap starting. So the system climbed about 400 MiB in about 14 s (the B3 spike,
  on top of 520 MiB of everything else) and crossed the limit; the memory guard (close after 20 s at 870 MiB, end after 3 s past 940) was too slow
  for that rate: from 919 MiB the kernel's limit was about 3 s away at 25 MiB/s.
  - Evidence for the other freezes is weaker: their last readings are 30 s old and lower (run dxvk-debug-01: RSS 562 of 989; run ui-01: 501 of 1,042), so
    the same cause is likely but not shown. Near misses: run census-01 read 952 of 999, and run service-load-01 read 982.5 of 975.8 (over the
    limit) and survived.
  - Guard change (host-tested only, not on the console): a census within 10 MiB of the limit or past it that has risen by 120 MiB in the last 8 s ends the
    game at once (`src/pw_memory_guard.h`, "steep climb"); not for a run that sets its own end mark; a slow arrival in the same band is left to the old
    rules (tests: the game launcher's 706 to 865 MiB ramp at 8 MiB/s, then its 865 to 948 wave; the second game launcher's climb at 24 MiB/s ended at 860 to 930 MiB). It stops a
    session earlier; it does not remove the spike, which remains B3.
  - No Chromium-based session (the game launcher, the second game launcher, NW.js) should be run on the console until this is on it and the user has agreed. The gate has no such
    session and passed four times without a freeze.

- **Gate, 2026-10-11 evening** ([gate-own-2026-10-11-05](../evidence/gate-own-2026-10-11-05.json)): 67 of 67 on build `d1908c42` (the steep-climb guard, patch 1006's
  write counters, the throughput probe and the iobench tool). No Chromium session is in the gate; none was run after the crash.

- **The Unity game's download, measured in the user's own session (2026-10-11, build `d1908c42`, patch 1006's counters).**
  - The Agent (`Agent.9824\\Agent.exe`) wrote 1,024,245 KiB (1 GiB) in 17,588 calls: nearly all of 64 KiB or less (96 % in the 64 KiB bucket, average
    58 KiB, none above 256 KiB), 5.1 ms per call on average in the host write (4.1 to 7.5 ms over the session), 89.9 s in writes in total.
    That is 5 to 10 MiB/s to disk; the Unity game's folder grew 1.1, 4.2 and 6.3 MB/s in successive 22 s windows (it also lost files while it
    gained others: 345 to 275 to 303, so part of the writing is rewriting). Plain 64 KiB writes cost what `console-iobench` measured; the
    slow disk path, not the network (19 to 35 MB/s), set the speed. Every other helper stayed at 40 to 70 MiB resident, no GPU spike.
  - **The console hung again** about a minute after this session's `stop-game` was written (the user could move the cursor but not reach the home
    screen; FTP and the control payload timed out for minutes; restart needed). The helpers 1384 to 1397 of that session end with ordinary teardown
    lines. Cause not shown: the Agent had 1 GiB of writes in flight and the close of the session was asked while it ran; no memory reading
    covers the end (the last of them is from before). Do not stop a session that is writing heavily.
  - **Native tests on the idle console (`tools/console-iobench.py`, after a restart):** appending through a sliding shared mapping, with
    `ftruncate` to extend the file by each write and `memcpy` into the mapping (no flush): 601 MB/s with 64 KiB writes, 477 with 16 KiB, 303
    with 4 KiB, against 24, 14 and 5.5 for `write()`; `ftruncate` and `mmap` cost under 0.005 ms per write; the kernel's later flush runs at about
    84 MB/s (0.76 s for 64 MiB). Another descriptor sees the new size and the new bytes at once, and the mapping sees another descriptor's
    `pwrite`, on `/data` (nullfs) and on `/user/data`. 24 files of 1 MiB: 0.59 s mapped against 1.12 s with `write()`. 192 MiB appended and left dirty:
    0.37 s, flushed by itself within 35 s, the kernel's RSS unchanged at 235 to 245 MiB (limit 952 to 1,023), wire count up about 86 MiB for two samples.
  - **The mapped-write design was built and rejected (2026-10-11): it is fast because it skips block allocation, and then other readers get stale bytes.**
    - Built as patch 1007 (kept out of the series): `NtWriteFile` copying into a sliding mapping, `ftruncate` first, the server opening write handles
      read-write. On the console it ran: 1,051 of 1,051 writes mapped, 64 KiB writes at about 400 MB/s. The new gate probe
      `file-write32/64-probe` (contents, sizes, a second handle, a file mapping, a child process, delete and recreate) failed in both PE sizes
      ([file-write64-2026-10-11-06](../evidence/file-write64-2026-10-11-06.json): 5 failures: a second handle read zeros where bytes had been
      written, and a gap that should be zero was not). The same probe passes on stock Wine on the host and, without patch 1007, on the console
      ([file-write64-2026-10-11-07](../evidence/file-write64-2026-10-11-07.json), PE32 `-07` too).
    - Reproduced natively (`console-iobench --only unaligned`, 600 appends of 1 to 70,000 bytes, read back through a second descriptor right after):
      `ftruncate` to the exact end then a copy into the mapping: 305 of 600 writes read back wrong (5.7 MB), at 276 MB/s; extending to the next 16 KiB
      boundary first: the same 305; one real byte written at the end first (so the block is allocated): 0 wrong, 18.3 MB/s; `msync(MS_ASYNC)` after each
      copy: 0 wrong, 13.8; a real byte at the end of every new 16 KiB or 64 KiB unit: 0 wrong, 12.8 and 19.9. Plain `write()` of the same sizes is about 15 to 20.
      So reads on this filesystem (`bfs`) do not see mapped pages of ranges `ftruncate` created without blocks, and what makes them correct (a real write that allocates
      the block) is the expensive part: the cost of a write on this disk is allocating blocks, about 2.3 ms per call plus 0.3 ms per 64 KiB, not copying.
      `posix_fallocate` is ENOSYS here, and so are `statfs` and `fstatfs` in a helper (the libc shim stubs them: a system call outside what a title may use
      kills it). A full volume does fail `ftruncate` up front with ENOSPC (tested on `/system_tmp`), which was the one good result of the design.
    - The earlier native results were not wrong, but they measured aligned 64 KiB appends, which fill whole blocks; the unaligned sizes real programs use did not.
  - **The share-mode question, answered from the Agent's code (no console time).** `Agent.9824\\Agent.exe` (PE32, 7.3 MB) calls `CreateFileW` from 20 sites;
    decoding the arguments pushed before each: none opens with sharing off. The writers use share 3 (read and write: the log at `0x8abe35`, the
    create-always site at `0x509916`) or share 7 (read, write and delete: an append handle at `0x69da32`, and the likely archive writer at `0x72fd67`, which
    opens with `FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING`, the access and disposition taken from variables); the read-write sites at `0x72df2d` and `0x72df66` use
    share 3. So buffering writes "invisibly because Windows forbids other openers" does not apply to the Agent: any other process may legally open
    these files while they are written.
  - **What that leaves.**
    - Buffering behind the program's back (any size) makes bytes late for other openers; it cannot be made exact for another process without the server
      asking the writer to flush on every open, which is not built and which a buffered tail lost in a kill would still defeat.
    - **Concurrent overlapped writes would not help one file.** Measured natively (`console-iobench --only one_file`, writers at disjoint offsets of one
      growing file): 1, 2, 4 and 8 writers of 64 KiB blocks give 22.7, 23.2, 23.4 and 23.8 MB/s; of 16 KiB 13.9, 13.7, 13.6 and 13.5; of 256 KiB, 1, 4 and 16
      writers 74.8, 74.5 and 73.5 MB/s. The file serializes them. Only writes to different files overlap (4 files of 64 KiB blocks: 35 MB/s, 1.5x; 8 files of
      4 KiB: 3.3x, earlier), so running a program's overlapped writes in parallel would help the Agent only where it writes several files at once.
    - What does move the number is fewer calls: a 128 KiB write costs 2.6 ms, the same as a 64 KiB one (2.8), so merging two adjacent 64 KiB writes (held until the
      next write or a 2 ms timer) would be about 2x, and 1 MiB writes are 134 MB/s; both show late to another opener, which the Agent's share modes allow.
    - Otherwise the console's disk caps a downloader that writes 64 KiB at a time near 10 MiB/s, below the network's 19 to 35.

- **Gate, 2026-10-11 night** ([gate-own-2026-10-11-06](../evidence/gate-own-2026-10-11-06.json)): 69 of 69 on build `14e482f3` (adds `file-write32/64`). Nothing of
  the mapped-write experiment is in the build.

- **Write merging for regular files (patch 1007, 2026-10-11).** Adjacent sequential writes to one file (pwrite, file-pointer and overlapped explicit-offset) are held in
  a 512 KiB buffer (up to 4 files, flush at 256 KiB, writes over 256 KiB go straight through) and written as one call by a flusher thread at the latest 15 ms after
  the first held byte. Anything that could observe the file flushes first: another read/query/set/fsctl/ioctl on that handle or any handle of the file, directory
  queries, open/delete/attribute queries (flush all), close, section creation, process creation and termination. The bytes are real writes to real blocks, so other
  openers see them (unlike the rejected mapped path). A failed flush is reported to the next call on that file as an error.
  - Probe `file-write32/64` now also checks overlapped writes at explicit offsets, a second process that must see 300 bytes within 100 ms of three small writes with
    the writer's handle open, and the speed of both writers. Stock Wine on the PC passes; the console passes on both (64-bit, 32-bit).
  - Measured on the console, 64 KiB writes: file pointer 56.5 MB/s (64-bit) and 54.6 (32-bit); overlapped NO_BUFFERING 57.7 and 54.4. Unmerged it was about 2.8 ms per
    write, 22 MB/s at best. Helper census: `merged_calls == calls`, 88 flushes for 627 writes (64-bit probe), 643 for 3242 (32-bit).
    Evidence: [file-write64](../evidence/file-write64-2026-10-11-merge-01.json), [file-write32](../evidence/file-write32-2026-10-11-merge-01.json).
  - Gate [gate-2026-10-11-merge-01](../evidence/gate-2026-10-11-merge-01-file-write64.json): all cases passed on build `51f9df34`.
  - **First real Unity game's download (2026-10-12 01:5x console time; title log `/data/ps5proton/logs/title-last.log`, helper logs `wine-child-2949.log` and others; not committed).**
    The merge was active in the Agent (`calls=18350 merged_calls=18350 flushes=9073`, about 2 writes per flush, 118 KiB). Its numbers moved with memory pressure, not with the disk:
    the first 25 s window ran 12.4 MiB/s with the flusher 39% busy (2.9 ms per flush); while the system stood at 880 to 934 MiB the Agent's writes averaged 8 to 9 ms each
    and the download ran 0.7 to 4.6 MiB/s. The flusher was never saturated, so the disk is not what held it at 2.5 MB/s here.
    Then the second game launcher crashed (`BlizzardError.exe`, 21:58:23): its GPU process (`--type=gpu-process --no-sandbox`) climbs to 310 to 350 MiB (B3), system memory crosses the
    guard's 940 MiB mark, the guard ends that process (seven times in the log: 894, 923, 880, 927, 889, 930 MiB), Chromium starts another, and after the last one second game launcher gave up.
    Baseline before the GPU process starts is already 640 to 700 MiB across 17 helpers. So the download speed cannot be judged until B3 is fixed.

### Direct memory shared between processes (2026-10-12): not possible by offset, and one probe variant crashed the console

**Question.** The shell's `VM Stats: RSS x of 1025.8` line is the title's 1 GiB flexible pool (param.json `flexibleMemorySize`, `FMEM size: 0x40000000`).
Everything a helper does not put in direct memory counts against it, B3's host-visible GPU buffers included (physical shared memory, flexible backing).
Could that memory come from the 11,712 MiB pool instead? Windows commits and Wine's heap already do.

**Result** ([dmem-share-2026-10-12-01](../evidence/dmem-share-2026-10-12-01.json), `-02`): payload A allocated 64 MiB with
`sceKernelAllocateDirectMemoryForApp`, mapped it and wrote a pattern; payload B, run beside it under an idle owner title, could not map that offset.
- `sceKernelMapDirectMemory` read-write and read-only: `0x8002000d` (EACCES). `sceKernelMapDirectMemory2` types 0 to 3 and 10: `0x8002002d`.
- `/dev/dmem0`, `/dev/dmem1`, `/dev/dmem2` open, and `mmap` at the offset: errno 13.
So the kernel ties a direct-memory allocation to the process that made it. Two processes cannot share a direct-memory range by its offset.

**What crashed the console.** The next variant (`dmem-share-2026-10-12-03`) added, to the allocating payload, an `mmap` of the device nodes on its own range and a
`fork()` whose child mapped the same offset. The run timed out, then the console stopped answering ("No route to host"), and the user restarted it. Which of the
two did it is not known. **Neither is to be run again**: a payload does not fork, and the dmem device nodes are not to be mapped.

**A way exists in the firmware (read from the 10.01 system files, `<your firmware dump>/system/`, static analysis only, no console run).**
- `AgcCompositor.elf`, the system compositor, shares direct memory with game processes. For each 2 MiB chunk it allocates the range
  (`sceKernelAllocateDirectMemory`/`...ForApp`), then calls `sceKernelProtectDirectMemory(offset, 2 MiB, 0)` and
  **`sceKernelProtectDirectMemoryForPID(offset, 2 MiB, 0x33, clientPid)`**, writes the offsets into shared memory (`writeDmemOffsets`) and the client maps them.
- `libkernel_sys.sprx` implements both as one ioctl on the dmem device, `0xc018800f` with `{offset, length, pid, prot}` (pid 0 for the plain call; the
  PID call refuses pid 0). So the kernel keeps a per-pid access list on each dmem range, and our EACCES (0x8002000d) is the empty list for the second process.
- NID of `sceKernelProtectDirectMemoryForPID` is `cBp0Ad-24LA`; the payload SDK declares it (`libkernel_sys.so`). prot 0x33 is read, write, GPU read, GPU write.
- Also in the files: `sceKernelEnableDmemAliasing`/`...2`, `sceKernelJitCreateSharedMemory`/`...AliasOfSharedMemory`, and `libSceSisrMgr`/`libSceSisrSys`
  share their 128 MiB `/SceSisrShm` as physhm (type 3 game side, 0 system side, mapped with prot 0x33), so physhm is also what the firmware uses for that.
- **Result, on the console (2026-10-12): it works** ([dmem-share-2026-10-12-05](../evidence/dmem-share-2026-10-12-05.json), under an idle owner title). Payload A allocated
  64 MiB with `sceKernelAllocateDirectMemoryForApp`, mapped it, wrote a pattern, and called `sceKernelProtectDirectMemoryForPID(offset, 64 MiB, 0x33, pidB)`: status 0.
  Payload B, a separate process, mapped the same offset with `sceKernelMapDirectMemory` (status 0), read all 67,108,864 bytes (0 wrong), wrote a mark in the last page,
  and A saw it: the memory is shared. Each process's resident flexible memory stayed at 3.5 MiB (3,504 to 3,552 KiB) while 64 MiB was mapped, so none of it is in the 1 GiB pool.
  The run before it (`-04`) failed only because the probe's offer-writing code had been removed by mistake with the unsafe variants; the same probe without the grant, run
  earlier (`-01`, `-02`), got EACCES.
- **The title as the second process (2026-10-12)** ([dmem-title-share-2026-10-12-01](../evidence/dmem-title-share-2026-10-12-01.json); title log in the run's klog, not committed;
  build with the temporary `PROTON_DMEM_TITLE_TEST` hook in `native/proton_wine.cpp`, uncommitted): payload A allocated 64 MiB and granted the **title's** pid (132) with prot 0x33.
  The title mapped the offset with `sceKernelMapDirectMemory(prot 0x33)` at once: status 0, so the kernel accepts CPU and GPU read/write bits on a range another process
  allocated. It read all 67,108,864 bytes (0 wrong) and its mark was seen by A. The separate `sceKernelMprotect(0x33)` path RADV uses as `radv_ps5_memory_grant_gpu` was not
  needed and was not exercised. **Not shown: the GPU itself reading or writing such a range** (the kernel accepting the bits is not the GPU using them); that needs a
  Vulkan run through the broker with host-visible memory taken from this route.
- **The GPU using such memory, through the broker (2026-10-12)** ([dmem-gpu-dxvk64-2026-10-12-04](../evidence/dmem-gpu-dxvk64-2026-10-12-04.json)): with the experimental patch
  0009 and `PROTON_GPU_HOST_DMEM=1` the broker takes every client's host-visible buffers from the application's direct memory (the memory helper allocates and grants
  the title's and the client's pids; each side maps by offset with prot 0x33) instead of physical shared memory. The DXVK GPU-window probe passed: `PS5_PROTON_GPU_WINDOW PASS
  checks=29 failures=0 captures=6`, window pixels verified, a 64-bit client and a 32-bit one (placed mappings included), 86 buffers (55,214,080 bytes) from direct memory, none refused.
  The kernel accepted the GPU bits and the GPU drew and read through them: **The GPU permission works on a range another process allocated.**
  - Three defects found on the way, each in the client or broker side of the patch: the client insisted on a descriptor in every allocation reply (it refused the offset,
    so DXVK saw "No adapters found"); then `vkMapMemory2EXT` placed mappings (32-bit programs) need the offset kept per allocation.
  - **Freeing and default on (same day).** The client now unmaps before it sends FREE; the broker unmaps its side and gives the range back to the memory helper
    (`pw_memory_dmem_free`, host-tested), and a client that is gone has no mappings, so its ranges are given back when the broker closes it. The probe then showed
    `dmem_host made=83 freed=83 refused=0 free_failed=0` ([dmem-gpu-dxvk64-2026-10-12-05](../evidence/dmem-gpu-dxvk64-2026-10-12-05.json)). The route is now the default
    (`PROTON_GPU_HOST_DMEM=0` returns to shared memory), the temporary title test hook is removed, and the gate has a new case, `host-dmem64`, which requires made > 0,
    freed = made, none refused or failed.
  - **Gate** [gate-2026-10-12-dmem-01](../evidence/gate-2026-10-12-dmem-01-host-dmem64.json): all 70 cases passed on build `a3dfe593`, with every GPU case (OpenGL/Zink,
    DXVK, Vulkan, 32- and 64-bit, game mode, desktop) running on the direct-memory route; `host-dmem64`: made 83, freed 83.
  - **The effect, measured with a probe that holds host-visible memory** (`tests/gpu-host-memory-probe.c`, uncommitted until the next gated change: four 64 MiB Direct3D 11 staging
    textures written in full and held 12 s; [dmem0](../evidence/gpu-host-memory-dmem0-2026-10-12-01.json), [dmem1](../evidence/gpu-host-memory-dmem1-2026-10-12-01.json)). Both pass
    (4 of 4, read back correct). The probe's helper reports, at its fullest:
    | `PROTON_GPU_HOST_DMEM` | `vm resident` (the 1 GiB flexible pool) | `direct_resident` (the 11,712 MiB pool) |
    | --- | --- | --- |
    | 0 (shared memory) | **273 MiB** | 108 MiB |
    | 1 (direct memory, now the default) | **3 MiB** | 379 MiB |
    So about 270 MiB, all of the host-visible memory, left the 1 GiB pool. The title's `system memory` census lines did not show the difference (their samples are about ten
    seconds apart and missed the hold), so the helper's own record is the evidence here, and the Chromium comparison is still to be made.
  - **The console has swap** (`tools/console-memory.py`, idle owner title, 2026-10-12: `vm.swap_total = 10,830,741,504`, SceShellCore's `VM Stats` line shows `swap out`).
    The 1 GiB flexible pool is not a wall: past it the kernel pages our memory out to the disk, and the disk is slow (the Agent's writes took 8 to 9 ms each at 880 to 934 MiB, and
    the .NET game's slow loads that first showed B3 are the same thing). So no swap is to be added; the point is to stay under the pool. In that idle title each helper held 10 to 14 MiB of
    flexible memory (fmem 0x27b to 0x396 pages of 16 KiB) and the title 60 MiB; a Chromium helper held 25 to 60.
  - **Fallback and a second gate** ([gate-2026-10-12-dmem-03](../evidence/gate-2026-10-12-dmem-03-host-dmem-fallback64.json)): a range the memory helper refuses (pool full, grant failed) or the broker
    cannot map is made as a shared-memory object instead, and the client maps it by descriptor. New gate case `host-dmem-fallback64` (the title's test hook
    `PROTON_GPU_TEST_DMEM_REFUSE=20` refuses every range after the first 20): made 20, freed 20, 63 from shared memory, the probe still passes. 71 of 71 on build `a4c50ce1`
    (`host-dmem64`: made 86, freed 86). The first run of this tree, gate-2026-10-12-dmem-02, passed 70 of 71: `appearance32`'s light capture caught a hover highlight on a caption button
    (238,242,248 where 174,174,174 was expected, eight samples of part 15), while its probe reported PASS checks=1037 failures=0. The same case passed 3 of 3 alone
    (appearance32-recheck-2026-10-12-01..03) and in the next full run: a pointer-state flake of the case, not of this change (it is a 32-bit GDI window, no GPU). Not fixed.
  - **What Wine's own sections cost** (`tests/section-memory-probe.c`, uncommitted until a gated change; [section-memory-2026-10-12-01](../evidence/section-memory-2026-10-12-01.json)): one
    128 MiB pagefile-backed section (how Chromium shares memory between its processes), mapped and written in one helper: the helper's kernel fmem went 14 to 142 MiB and its walk-visible
    flexible resident to 131 MiB. So sections are charged to the 1 GiB pool and could use the same grant route. But a Chromium helper in the second game launcher's session held only 24 MiB of walk-visible
    flexible memory against 60 MiB in the kernel's count, so large sections are not what holds the second game launcher's baseline; the other 36 MiB per helper (an idle helper has 8 to 11) is not
    in any mapping the walk can list (page tables, thread kernel structures: not shown, not proven). Moving sections is therefore not next; the Chromium measurement is.
  - Not yet: a real Chromium workload (queued in [CONSOLE_QUEUE.md](CONSOLE_QUEUE.md), needs the user present).
- Not yet shown: the GPU using such a range; the allocator
  outliving a helper that exits; large and many ranges. These decide how the broker's host-visible memory moves over.

**What is left** for B3: shared host-visible memory needs an fd (physhm, flexible), so the options are to cut how much DXVK holds host-visible in a Chromium GPU
process, or to bring the helpers' other flexible use (about 25 to 60 MiB each over 17 helpers) down.

### Games in the desktop: launcher versus desktop frame times (2026-10-10)

**Question** (priority 1's first step): is a game slower or less even on the
Mihawk desktop, where the broker copies each completed GPU frame and composes
the desktop on the CPU, than from the launcher, where it flips the game's own
buffer? **Method:** The broker now keeps a pacing record per GPU client
(`src/pw_frame_pacing.h`, graphics patch `0005-record-gpu-frame-pacing`,
host-tested). It records the intervals between shown presents' ends in 250 us
buckets, misses over 20 and 34 ms, and the time inside each present, on stderr
every 10 s. Both routes reply only after the flip, so this is the time between
new game frames on screen. `tools/run-game-routes.py` runs one catalog game in
both routes and judges thresholds fixed before the first run (5 % fps, 2 ms
worst p95, 1 point more intervals over 20 ms). The desktop run starts the game
the way a "Start in" shortcut would, in the `mihawk` prefix, with the entry's
environment. Build `c29b6b072b79011e` (uncommitted; it will be batched behind a
gate with the next code change).

**Result**, the .NET game idle at its title screen, both routes full-screen
1920x1080 (the desktop capture shows the title screen drawn correctly):

| Run | Route | fps | p50 | worst p95 | worst p99 | max | over 20 ms | inside present avg/max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 01-launcher (2026-10-10; record removed from the tree; in git history) | direct | 59.8 | 16.75 | 17.25 | 25.0 | 26.0 | 1.0 % | 14.4 / 16.3 ms |
| 03-launcher (2026-10-10; record removed from the tree; in git history) | direct | 59.8 | 16.75 | 17.25 | 24.75 | 25.4 | 0.9 % | 14.6 / 16.3 ms |
| 01-desktop (2026-10-10; record removed from the tree; in git history) | composed | 58.8 | 16.75 | 17.25 | 33.5 | 40.3 | 3.2 % | 15.2 / 32.6 ms |
| 01-desktop-capture (2026-10-10; record removed from the tree; in git history) | composed | 58.4 | 16.75 | 17.25 | 33.5 | 33.6 | 3.5 % | 15.2 / 32.4 ms |
| 02-desktop-capture (2026-10-10; record removed from the tree; in git history) | composed | 58.1 | 16.75 | 17.25 | 33.5 | 670* | 3.1 % | 15.4 / 668* ms |

(times in ms; *the capture's own disk write, inside one present). The typical
frame is identical, but the desktop misses a refresh (a 33.5 ms interval,
the previous frame shown twice) about once or twice a second in every 10 s
window, which the launcher never does. The cost is visible: composition
averages 2.6 ms and peaks at 7.9 ms, once per game frame (4,985 compositions
for 4,987 frames), and occasionally pushes the flip past the refresh. By the
fixed thresholds the comparison (2026-10-10; record removed from the tree; in git history)
says **desktop slower**, on evenness rather than throughput (about 1.7 % fewer
frames). Per the agreed sequence, the next step is a path where a full-screen
game skips composition (independent flip) before games move to the desktop.
Limits: one 2D game at an idle title screen, no input, about 85 s per run. A
heavier scene, a windowed game and input latency are not measured.

**Game mode, steps 1 and 2** (the user's direction after this comparison:
unload the desktop for every full-screen and borderless full-screen game, and
start steps 1 and 2). Step 1 is graphics patch `0006` with
`src/pw_window_direct.h`. A GPU window that is exactly the whole screen is
flipped directly and the compositor sleeps. Its host test proves every "yes"
against the compositor, and a mutant that ignores covering windows fails.
Step 2 is Wine patch `0995`: the taskbar follows window events instead of a
one-second rescan that also moved and repainted every button, its clock wakes
each minute, and `tabtip.exe` starts only with upstream's own environment condition (it held 37 MiB idle with a system-wide focus watcher). Build
`618b8c39d3b290f8`; run routes-04 (2026-10-10; record removed from the tree; in git history):
launcher 59.9 fps, p50 16.75, worst p95 18.5, worst p99 25.0, max 27.0,
0.9 % over 20 ms; desktop in game mode 59.8 fps, p50 16.75, worst p95 17.25,
worst p99 23.0, max 25.0, 0.9 % over 20 ms, and all 4,883 frames direct
(one entry, left when the window closed). Explorer makes 41 server calls per
10 s (was 1,457) but still wakes about once a second, from a source not yet
identified. System memory is 291 MiB against the launcher's 244 (was 338
against 256). The complete [gate](../evidence/gamemode-gate-2026-10-10-01.json)
passes 61/61 in 1,936.0 s with zero fault records; no gate case enters game
mode, so the gate shows the composed path unchanged and the .NET game's run is the
game-mode evidence.

**Game mode, step 3** (2026-10-10; the user also asked that the session not
depend on a Windows `mihawk.exe`). Build `0faddbfe741c823b`, uncommitted:

- **The title supervises the session** (`src/pw_desktop_session.h`,
  host-tested, replacing the PE `mihawk.exe`). Asked for
  `C:\windows\system32\mihawk.exe`, the title starts
  `explorer.exe /desktop=Default,1920x1080 [program]` as the session's root
  with `MIHAWK_SESSION=1`. Every 100 ms it reads the broker's game-mode client
  (graphics patch `0007`). After 2,000 ms of unbroken game mode it ends
  Explorer, through the same native close as any helper (`pw_ps5_unload_root`;
  never a freeze). It starts Explorer again 1,500 ms after game mode ends, or
  at once when the game's process is gone. A stop with Explorer unloaded ends
  the session when the game has closed. An Explorer that does not go within
  15 s stays until game mode has had a break (the first version asked again
  every ~17 s; step 3 review). The restart runs off the title's input loop
  (it held the running game's controller for 225-614 ms per restore), and a
  restart that fails is tried again every 5 s while the game runs: the game
  is never closed for it; with the game gone, three failures in a row end the
  session (status 1). These three fixes are host-tested, not yet on the
  console.
- **Explorer can be restarted**: Wine refuses a second desktop window, and a
  restarted Explorer exited at once ("aborted by WM_NCCREATE",
  [step3-diag-2026-10-10-01](../evidence/step3-diag-2026-10-10-01.json)). In
  the session Explorer now makes a Windows-style `Progman`: the bottom-most
  shell window that paints the wallpaper. The desktop window stays the
  system's (Wine patch `0996`). The server keeps the shell window below other
  bottom-most windows and marks it as the desktop in the window snapshot
  (`0997`). The taskbar learns window creation and destruction from window
  events and adopts windows already open.
- **Gate cases**: `game-mode64` and `game-mode-dxvk64` (`tests/game-mode-probe.c`,
  `tools/run-game-mode.py`). A full-screen window checks six capture samples of
  its own colour and sees the taskbar go. It then closes, sees the taskbar
  come back with its other window's button, and the title's records must show
  a direct entry, an unload and a restore with status 0.

**Console results.** [step3-focus-2026-10-10-02](../evidence/step3-focus-2026-10-10-02.json):
`game-mode64`, `game-mode-dxvk64`, shell32/64, wallpaper32/64, work-area64,
window-snapshot64, appearance64 and desktop-overlap pass. Three failed and
were fixed: window-compose64 and window-effects64 (backdrop windows went below
Progman; patch `0997`), and desktop-session (its logoff posted `WM_CLOSE` to
the system's desktop window; it now closes the shell window).
[step3-focus-2026-10-10-03](../evidence/step3-focus-2026-10-10-03.json) passes
those and `game-mode64` again, 5/5. The .NET game,
run routes-05 (2026-10-10; record removed from the tree; in git history):

| Route | fps | p50 | worst p95 | worst p99 | max | over 20 ms | system memory |
| --- | --- | --- | --- | --- | --- | --- | --- |
| launcher (direct) | 59.7 | 16.75 | 19.75 | 25.25 | 197.1 | 77 | 256 MiB, 9 helpers |
| desktop (independent flip, Explorer unloaded) | 59.7 | 16.75 | 17.5 | 25.0 | 30.6 | 58 | 251 MiB, 9 helpers |

All 5,196 desktop frames were flipped directly. Explorer was unloaded 259 ms
after the title asked, and the stop with the desktop unloaded ended the
session with status 0. The comparison says the desktop is not slower. Limits:
one 2D game at its title screen with no input. The launcher's 197 ms maximum
is one interval and is not explained.

**The first full gate**, [step3-gate-2026-10-10-01](../evidence/step3-gate-2026-10-10-01.json),
stopped at 9 of 63 cases (8 run, 7 passed, 388 s):

- **gpu-window32 failed** "persistent shell ready" (error 1400). Explorer is
  now the session's first process. It started the probe before registering
  the shell window, then waited on COM (`RPC_S_SERVER_UNAVAILABLE` in
  `shellwindows_init`), and the probe gave up after 5 s. Fix: `0996` makes
  Progman the shell window as soon as it exists, before any program or COM, as
  Windows does; gpu-window32 then passed in 37.7 s on the console
  ([step3-focus-2026-10-10-04](../evidence/step3-focus-2026-10-10-04.json), 8/8).
- **Two helpers were left stopped** during gpu-window64 (which itself passed).
  Both were made by the payload loader (`EXEC` in klog) for spawns that
  failed with `errno 5`: the memory helper and one child. Wine retried and
  the probe passed, but the console guard then refused the next case ("2
  helper process(es) outlived their title"). The memory helper's failure
  came right after a `kern.ipc.maxpipekva exceeded` kernel message; the
  child's did not (the nearest message preceded a spawn that succeeded), and
  four successful spawns also follow the message, which the kernel prints at
  most once a second, so the record cannot tie either failure to it. The
  message is in all 662 saved logs of the three boots since 10-08 23:51, first
  at 217-451 min of uptime and in every run after; earlier boots with hundreds
  of runs and up to 19 h of uptime never show it, so something introduced
  around 10-08/09 uses pipe memory up, not the console itself (step 3 review).
  A stopped websrv child at a failed upload was seen before (2026-10-08,
  below). A failed spawn now logs the loader's answer, the stage that failed
  (connect, upload or answer), the errno of the call that failed there, the
  time and the token. The title may not read `kern.ipc.pipekva` ("not
  approved", errno 2 in `step3-focus-2026-10-10-04`); the kernel's own
  message in each run's klog is the record. No guard was cleared; the user
  restarted the console, and the 8 focused runs after it show no message.

**Found on the way** (stability item, recorded, not chased):
run routes-02-launcher-capture (2026-10-10) failed. The .NET game ended at 21 s
with an unhandled .NET exception (`0xE0434352`, `NoSuitableGraphicsDeviceException`
from `GL.LoadExtensions`) and drew nothing. Its helper logged `mprotect ...
protection 5 failed` for the 9.5 MB `.text` of `libgallium_wgl.dll` ("failed
to set 60000020 protection ... noexec filesystem?"). Wine then loaded and
unloaded OpenGL32, so the game had no OpenGL. This line appears in 1 of 357
saved helper logs that load `libgallium_wgl.dll`. The next observation is the
`mprotect` errno and the process's executable-memory state at that moment.
Also, the title's "frames shown" counts about two display flips per game
frame in both routes, so it is not a game frame rate.

### Child bootstrap refusal: root cause and fix (2026-10-10)

**Symptom.** Three recorded instances among about 16,600 helper rendezvous in
the saved owner logs (run fix-01 (2026-10-07), socket-options64 in
`mihawk-work-area-gate-2026-10-09-01`, pe64 in
`mihawk-effects-gate-2026-10-10-03`): the owner cannot adopt the child's input
page (`0 descriptors`), then reports `child bootstrap failed`. Gate03, the only
instance with the boundaries added on 2026-10-09, shows the refused packet was
READY with value 1, and shows the cause in that helper: its first read of the control endpoint returned **0 bytes,
readable without hangup** (`revents=0x1`). The owner's 18,452-byte bootstrap
send on the other end succeeded, and the helper's refusal reached the owner. So
the endpoint's receive side was shut and empty while its send side worked. That
is FreeBSD's `sorflush()`, which the unix-domain garbage collector applies to a
socket whose every reference is in flight. The collector cannot see a socket
between `soreceive()` taking it off the carrier's buffer and the receiver's
table taking it in. The title sent the endpoint through the rendezvous
connection and closed that connection, which schedules the collector while
rights are in flight. It then closed its own copy before the loader had taken
the endpoint.

**Probe.** `tests/unix-handoff-probe.c` (`tools/run-unix-handoff-probe.py`) is a
plain payload with no title or Wine. Eight threads hand a fresh SEQPACKET
endpoint through a fresh stream carrier, 160,000 times per mode. With the
sender's copy released at once, **67 of 160,000 endpoints arrive flushed** on
the Pro. Every one is readable without hangup, reads 0, and still delivers a
byte sent afterwards: the gate's signature. With the copy held until the
receiver acknowledges, **0 of 160,000** arrive flushed. The Linux oracle shows
0 and 0. See [unix-handoff-2026-10-10-01](../evidence/unix-handoff-2026-10-10-01.json).

**Fix**, global and stated in `wine/ps5/pw_payload_rendezvous.h`: a unix socket
handed over by SCM_RIGHTS stays open in its sender until the receiver shows it
holds it.
- The rendezvous is shared by both sides. The loader answers `{PWHD, pid}` once
  the endpoint is in its table, and `payload_spawn` returns only then, for
  every helper and the memory helper. The hello changes from `PWP1` to `PWP2`,
  so a title and a loader from different builds refuse each other at the hello.
- `spawn_native` keeps the child's GPU-broker socket until READY.
- The child keeps its copy of the title's memory-service end for its life.
- Audit of the other handoffs: Wine's own descriptor passing closes only after
  the server's reply, which proves receipt. The reply sockets (resolve, DNS,
  window, spawn requests) are only written by their receiver, and a
  receive-side flush cannot affect writing. Input, audio, frame and window
  pages, and the memory helper's objects, are not sockets.

Host: `test_pw_payload_rendezvous` uses forked loaders. It covers the
acknowledgement order, a late acknowledgement, the old hello, a missing or
foreign acknowledgement, and a non-packet endpoint. A mutant that returns
before the acknowledgement fails it. All 63 host suites pass.

Console, build `ef358326f91dbf92`: the
[focused batch](../evidence/bootstrap-focus-2026-10-10-02.json) (pe64,
payload-eight, payload-memory, descendants) passes **4/4 in 65.8 s**, with
49/49 rendezvous acknowledged and 49/49 full bootstraps. An earlier attempt,
`bootstrap-focus-2026-10-10-01`, was refused before launch because the helper
census was momentarily unavailable after deploy; it launched nothing. The
[complete gate](../evidence/bootstrap-gate-2026-10-10-01.json) passes **61/61
in 1,904.5 s**: 671/671 rendezvous acknowledged (`held=1`), 671/671 helpers
received the full 18,452-byte bootstrap, zero refusals, zero fault records,
zero native-close failures (611 closes, all within 301 ms).

Limits: before the fix the refusal hit about one spawn in 5,500, so a passing
gate cannot show its absence by itself. The claim rests on the reproduced
mechanism (67 against 0 in 160,000 handoffs each) and on the order the
production path now keeps (671/671 acknowledged).

### DWM surfaces batch: audit and design (2026-10-09)

The user directed one coherent batch for rounded shell surfaces, materials and
previews, starting with an audit of the existing APIs. Read-only audit of the
pinned production tree and our compositor:

| Boundary | Actual implementation | Consequence |
| --- | --- | --- |
| `dwmapi` | `DwmSetWindowAttribute` and `DwmExtendFrameIntoClientArea` return `S_OK` and store nothing; thumbnail register/update/unregister and blur-behind return `E_NOTIMPL`; `DwmQueryThumbnailSourceSize` is an unimplemented stub export. Composition reports enabled for Windows 8.1+. | No API state reaches Wine or the compositor; a success code is not an effect. |
| Server | Owns HWND order, lifetime, regions and layered coverage; snapshot v3 has no DWM fields. Patch 0990's owner/lifetime-checked request is the precedent for bounded PS5 state. | DWM state belongs on the server window, exported in the snapshot. |
| PE → server path | dwmapi is PE; it reaches the server through win32u. `NtUserCallHwndParam` has private codes; WoW64's default case passes a 32-bit pointer unchanged, valid for integer-only structures. | One private call code carries attributes and thumbnail operations for PE32/PE64. |
| Compositor | `src/pw_window_compose.c`: CPU back-to-front pass over retained GDI pages and GPU planes; opaque memcpy or premultiplied blend; no corners, shadows, backdrop or scaling. Runs in the broker thread, recomposing on scene/cursor change. | Effects are pure functions of snapshot + retained images, host-testable. Material blur must be bounded (downsampled). |
| GDI alpha | Window surfaces are 32bpp `BI_RGB`; Wine's DIB engine writes alpha 0 for GDI colours, as Windows does. Opaque pages already transport the alpha byte unchanged. | Faithful glass needs no producer change: only frame-extended glass regions of windows with an explicit backdrop interpret alpha. |
| uxtheme | `BeginBufferedPaint` ignores parameters; `BufferedPaintClear`/`SetAlpha` are `E_NOTIMPL`; `DrawThemeTextEx` has no `DTT_COMPOSITED`. | The documented Win32 recipe for opaque GDI content over glass does not work yet; implementing it is a global fix. |
| Shell | Start is a `WS_POPUP` panel, the tray a `WS_POPUP`, Explorer an ordinary framed window; all opaque, square; no previews. | Consumers of the shared contracts, not owners of effects. |

Windows semantics adopted, from Microsoft's rounded-corner guidance: frame
windows (`WS_CAPTION`/`WS_THICKFRAME`) round at 8 px by default; maximized
windows, window regions and layered windows never round; pop-ups round only on
request (`ROUND` 8 px, `ROUNDSMALL` 4 px). Rounding is visual; hit-testing is
unchanged. Thumbnail destination rectangles are taken relative to the
destination client area (identical to window-relative for frameless shell
pop-ups; framed destinations are not compared with Windows). Glass alpha is
interpreted only when an application explicitly selects a system backdrop, so
existing frame-extending applications (CEF/the game launcher) keep today's opaque pixels.
Win8+ blur-behind transparency, inactive Mica fallback, DWM-drawn dark captions,
iconic thumbnails and cloaking stay unimplemented in this batch.

Design and sequence:

1. **State transport**: dwmapi validates and sends corner preference, border
   colour, backdrop type, dark-mode material tint and frame margins through one
   win32u call to a server request; the owning process is required. Thumbnails
   are bounded server objects owned by the registering process and destination
   HWND lifetime. Snapshot v4 exports resolved radius/border/backdrop/glass and
   a bounded thumbnail table; overflow stays explicit.
2. **Compositor**: anti-aliased rounded clipping of both planes, optional border
   stroke, shadows for rounded frames and `CS_DROPSHADOW` pop-ups, Mica from the
   retained root (wallpaper) and acrylic from composed content beneath, both
   blurred at reduced resolution and tinted from `appearance.json`; live
   thumbnails scaled from retained source images with opacity, clipped to the
   destination. Host tests use independent reference calculations.
3. **Glass painting APIs**: `BufferedPaintClear`/`SetAlpha` and buffered-paint
   parameters, plus composited theme text where shell text needs it.
4. **Shell**: Start (acrylic, rounded, shadow), taskbar (Mica), Explorer frames
   (default rounding/shadow), taskbar hover previews through the public
   thumbnail API.
5. Paired PE32/PE64 console probes capture actual corners, borders, shadows,
   materials and thumbnails, with host-computed expected pixels; update the shell
   captures whose design legitimately changes; then freeze, full gate, commit.

Candidate01 (uncommitted): patch 0992 (dwmapi → win32u → owner-checked server
state, bounded thumbnail table, snapshot v4), patch 0993 (buffered-paint
parameters, clear/alpha, composited theme text), `src/pw_window_effects.c` and
broker overlay 0004. Host: the new effects, materials and thumbnail suites pass
under ASan/UBSan, including blur against a tap-by-tap reference and a
differential test in which the production compositor renders the probe scene
and the independent float verdict (`tools/run-window-effects.py`) accepts all
54 samples within one count. The gate list gains `window-effects32/64`
(61 cases). Production build01 (`4e9131fe…`, 8.5 min) and its
[focused Pro batch](../evidence/mihawk-effects-focus-2026-10-10-01.json) run
**3/5 in 188.3 s**: shell64, window-compose64 and window-snapshot64 pass with the
new compositor and default framed rounding/shadows. Both effects probes pass
**87/88 checks** and **all 54 displayed samples per architecture** (corners,
border ring, shadows, light/dark acrylic, dark Mica over the real desktop root,
opaque/half/live/hidden thumbnails), maximum deviation one count. The single
failure is `DTT_COMPOSITED`: Wine reports `abs(biHeight)` for DIB sections, so
the composited path refused every DIB as bottom-up and fell back to GDI text.
No fatal faults, protocol errors or native-close failures were recorded.

Candidate02 adds the shell adoption and the build01 corrections. Composited
text addresses the documented top-down DIB directly; standard menus (`#32768`)
round at 4 px by default. Patch 0994 makes the Mihawk taskbar a square Mica
surface whose buttons, clock and indicators paint with alpha, and adds live
previews: hovering an application button opens a rounded acrylic pop-up with
the window's DWM thumbnail (or its icon while minimized), and clicking it
activates the window. Start becomes a rounded, bordered acrylic panel. Shared
`pw_shell_style.h` helpers implement the Win32 glass recipe: buffered paint to
transparent glass, composited text, premultiplied fills, and an opaque fix-up
after standard controls paint (alpha-0 GDI pixels with colour become opaque;
transparent black stays glass). The shell probe captures the desktop beneath
Start once; the host judges the light and dark acrylic glass, rounded corner
and border from it, and checks a hovered preview's thumbnail pixels, server
state and click activation through the snapshot rendezvous. A second
differential test validates the acrylic float model against the compositor
over a non-uniform backdrop at the screen edge. All 62 host suites pass.

Build02 (`8eafd2363afd3ccc…`) and its [focused Pro batch](../evidence/mihawk-effects-focus-2026-10-10-02.json)
pass **5/5 in 184.6 s**: shell32, shell64, gpu-window64 and both effects probes,
with zero recorded fatal faults, protocol errors or native-close failures.
Each effects probe passes all API/server/uxtheme checks (including composited
text and standard-menu rounding) and all 54 displayed samples within one
count. In both shell architectures the host-judged light/dark acrylic Start
glass, its rounded corner over the shadowed desktop, its border, and the hovered
preview's thumbnail match their references exactly; the preview's server row
(rounded bordered acrylic), the taskbar's square Mica row, click activation and
thumbnail release also pass. Visual review of the captures shows blurred
wallpaper through Start, a Mica taskbar, the titled live preview above its
button, and rounded shadowed Explorer frames.

The first complete [gate](../evidence/mihawk-effects-gate-2026-10-10-02.json) on
the unchanged build02 fingerprint ran **60/61 in 1,989.7 s**, with zero recorded
fatal faults or protocol errors. Every composition, effects, shell, memory,
process and teardown case passed. `launcher-tls` failed with zero answers on
either LAN endpoint: this run lacked `PROTON_TLS_TRUSTED_PORT=1749` and
`PROTON_TLS_UNTRUSTED_PORT=1750`, which the previous passing gates supplied, so
the fixture chose random ports that the host firewall blocks. A
[focused repeat](../evidence/mihawk-effects-tls-2026-10-10-02.json) with those
ports passes on the same build (two trusted answers, one untrusted rejection,
root and test data removed, 147.4 s). The complete gate is repeated unchanged
with the ports set as `mihawk-effects-gate-2026-10-10-03`; no commit is claimed
before it passes.

[Gate03](../evidence/mihawk-effects-gate-2026-10-10-03.json) runs **60/61 in
1,954.8 s**; `launcher-tls` now passes. `pe64` fails in 5.2 s before its probe:
the owner reports `child's input page not adopted (0 descriptors)` and `child
bootstrap failed errno=2`, then `OWNER_BOOTSTRAP FAIL initial client`. This is
the recorded intermittent bootstrap signature (2026-10-07 the game launcher's run, 2026-10-09
work-area gate01). It passed in gate02 on the same fingerprint, and three
focused repeats pass (10.1, 10.1 and 9.7 s) without the refusal. The input-page
path is not changed by this batch; the cause was later found and fixed (see
"Child bootstrap refusal" above). Gate04 repeats the unchanged build as
`mihawk-effects-gate-2026-10-10-04`.

[Gate04](../evidence/mihawk-effects-gate-2026-10-10-04.json) passes **61/61 in
1,883.3 s** on the unchanged fingerprint, with zero recorded fatal faults or
protocol errors and 616 native closes within 301 ms. This qualifies the batch
for its local commit. It does not fix the intermittent bootstrap failure, prove
Windows 11 pixel equivalence, or qualify animations, blur-behind transparency,
iconic or minimized-window thumbnails, DWM-drawn captions, layered GPU/child
windows or frame pacing.

Glass performance follow-up (measured, not yet built): a host benchmark of the
committed compositor costs 1.6 ms for a plain two-window desktop, 2.9 ms with
rounded shadowed frames, 16.4 ms with acrylic Start and 71.6 ms with a
full-screen Mica window (minimum of 60 frames on the development host; the
Pro's CPU is slower). The broker recomposes on pointer motion, so open Start
lowers pointer smoothness. Prepared changes: a bit-identical row expander for
material (one vertical blend and tint table per row, incremental column math,
checked against the per-pixel functions over 42,768 rows), transparent/opaque
glass fast paths, and an interior fast path for the blur downsample bring the
same scenes to 9.5 ms and 27.1 ms. The broker would re-present the last
composition for pointer-only motion and captures (present draws the pointer
into the display buffer, never into the composed frame) and log composition
count, average and maximum time. These need their own build, focused console
timing and exact-source gate.

Performance candidate (uncommitted): the changes above, a permanent host check
that the row expander equals the per-pixel definition over more than 10,000
rows, `tools/bench-window-effects.py` with its
[host record](../evidence/mihawk-effects-host-bench-2026-10-10-01.json)
(acrylic Start 14.9 → 8.8 ms, full-screen Mica 63.3 → 26.9 ms against
`628168a`; plain and shadowed scenes unchanged within noise), and the
composition statistics on the owner's stderr as well as the broker record.
Build03's [focused batch](../evidence/mihawk-effects-focus-2026-10-10-03.json)
passes 7/7 but recorded no statistics: the broker's results file has been
unreadable since `launcher-render` in gate03 (see the observation below).
Build04 (`5f8f0a33…`) routes the summary through stderr; its
[focused batch](../evidence/mihawk-effects-focus-2026-10-10-04.json) passes **7/7
in 284.5 s** and records console composition times: shell cases average
4.8–5.0 ms (maximum 17.0–17.2 ms with acrylic Start, the Mica taskbar and a
preview), effects probes 5.4–5.6 ms (maximum 12.7–14.0 ms), the rounded GPU
and DXVK framed windows 2.7 ms (maximum 3.4 ms) and the unaffected composition
probe 1.2 ms (maximum 1.5 ms). Presents slightly exceed compositions because
pointer-only motion now re-presents. No pre-change console baseline exists;
the host ratio above is the comparison. The complete
[gate05](../evidence/mihawk-effects-gate-2026-10-10-05.json) passes **61/61 in
1,909.0 s** on `5f8f0a33562cee53…`, with zero recorded fatal faults or protocol
errors and 612 native closes within 400 ms (no timeout); 60 cases record
composition statistics (highest average 5.8 ms, highest maximum 17.3 ms).
This qualifies the performance follow-up for its local commit.

Observations recorded for separate work (not caused by this batch):
- **Broker results file (investigation paused 2026-10-10 at the user's
  direction).** Since 2026-10-09 the file has three times stopped changing for
  whole sessions of launches: onsets at window-gate-06 `appearance32`,
  work-area-gate-01 `wow64` and effects-gate-03 `launcher-render`; recoveries
  mid-sequence at pro-only-gate-03 `wallpaper32` and teardown-focus-06
  `memory-classes`. So it is not one case, and no recorded reboot preceded a
  recovery. Since effects gate03 it is 0 bytes for every launch, its console
  modification time frozen at that broker start, while files beside it update.
  An observation patch (graphics `0005` when written, to be renumbered `0009` on
  resuming since `0005`-`0008` are now taken; with a runner record;
  uncommitted, saved as `git stash` "broker-results-observation") on build `36ffed0d…`
  ([broker-file-2026-10-10-01](../evidence/broker-file-2026-10-10-01.json),
  pe64 and window-compose64, 2/2) shows the file at open and at stop is the
  same linked inode behind a valid descriptor, and the drain thread ends
  normally at end of file, but **`bytes_in=0`**: no broker line ever entered
  the pipe, and none appears in klog, stdout or helper logs. The loss is on
  the pipe's write side (the `front` stream or its descriptor), not the file.
  Next: record the write end's identity at open and at stop, and the first
  failing write's errno. The open-file budget's unpark (`pw_wine_fdv.c`
  reopens a parked file onto its old number with `dup2`) is one candidate to
  check, not established. That console build is still deployed; it only adds
  two stderr lines.
- SceShellCore's `page table CPU` statistic has fallen steadily across
  launches since 2026-10-09 (5,462 → 2,266 → wrapped below zero during gate02
  and still falling by a few counts per launch) with both old and new code.
  Its meaning is not established; a fresh boot would show whether it resets.

The dated candidate/run paragraphs below are the investigation history. Earlier
statements about queued jobs, missing payloads and reboot requirements describe
those points in time; this status and the latest candidate11 evidence take
precedence.

### Batch qualification (user direction, 2026-10-09)

Group related roadmap areas into one integration milestone. The preceding 56-case
gate took 1,700.9 seconds (28 min 20.9 s); this is a measured baseline, not a
fixed duration as coverage grows. Avoid paying that cost for each small UI slice.

The qualified batch is **everyday shell**: the centered taskbar, Start with real local
search and persistent pins, and the approved Explorer navigation/command layout.
These were developed together on `mihawk-os`, retaining focused verdicts for
each area. The combined implementation is console-qualified on the exact source:
patches 0981–0987 supply the centered taskbar, native Start and real Explorer
navigation/commands. Start enumerates actual local applications, saves pins in
HKCU and reads shell recent files; Explorer keeps IExplorerBrowser and its real
file operations. Owned Windows helpers are copied by the existing pinned build
recipe. There is no browser UI or duplicate filesystem model.

The initial integrated host pair passed 33 interaction checks per architecture.
The expanded host run records live-dark failures on host Wine without production
patch 0973, plus a failed attempt to restore a nonempty clipboard through a lazy
OLE object. The probe now requires an empty qualification clipboard before
mutating it; arbitrary user clipboard preservation is not claimed.
See [host evidence](../evidence/mihawk-shell-host-2026-10-09-01.json).

The first focused Pro pair passed **56 checks per architecture**, eight captures
and clean teardown in **71.7 seconds**. Visual review nevertheless found an
unchanged white file pane, low-contrast header text, stale caption buttons and
shell layout/state gaps. The stronger second pair correctly fails: captions,
header/file text and Start search-accent pixels now pass, but the file pane
remains white. Navigation checks also exposed early-read/focus timing and a
leftover disposable paste fixture. Both runs exited to the launcher with no
recorded CPU/GPU fatal faults or process-protocol errors. See
[first pair](../evidence/mihawk-shell-focus-2026-10-09-01.json) and
[second pair](../evidence/mihawk-shell-focus-2026-10-09-02.json).

The final candidate updates implicit native list-view backgrounds on system
colour changes while preserving explicit application colours. Browser history
owns navigation availability; focused native views retain keyboard focus across
navigation. The taskbar uses real executable icons for foreign processes because
Wine cannot marshal foreign `WM_GETICON`; runtime foreign icon changes remain
unsupported. Disabled arrows retain their alpha at half opacity. The probe uses
bounded state waits, unique owned fixtures and permanent pixel regressions.

Focused runs 03–05 exposed missing focus, stale prefix executable resources and
premature title assertions. Run06 passes 70 checks per architecture. The final
[run08](../evidence/mihawk-shell-focus-2026-10-09-08.json) passes **2/2**, **70 checks
per architecture** and **eight pixel-verified captures** in **70.7 seconds**, with
normal returns and zero recorded CPU/GPU fatal faults or process-protocol errors.
The finish reviewer scores all seven original findings resolved, with a **ship**
disposition limited to those fixes. Square opaque surfaces remain the accepted
fallback; materials and previews are later work.

The affected host suites and six capture-verdict tests pass. Two integrated shell
cases expand the mandatory gate to **58 cases**. The [full gate](../evidence/mihawk-shell-gate-2026-10-09-08.json) passes **58/58**
in **1,748.1 seconds (29 min 8.1 s)** on exact fingerprint
`7b415f582b48d3c8aaead8052a90bf825ed8614eae3a6af4913cf2b761fc9305`, with zero
recorded CPU/GPU fatal faults and zero process-protocol errors. The source stayed
frozen throughout qualification. This qualifies one combined local batch; it
does not establish universal Windows 11 parity or fix the previously recorded
intermittent bootstrap failure. Broader window behavior and materials/previews
remain subsequent work.

1. Use incremental builds and relevant host/oracle checks during development.
   Use focused Pro probes when a question requires actual rendering, input,
   process lifetime or cleanup; do not defer all console feedback until the end.
2. Finish the batch's functional and visual acceptance checks, including shared
   application state, launching real files/apps, keyboard navigation, persistence
   and clean session teardown. Add required regressions to the existing gate.
3. Freeze the combined source, build/deploy it, then run the entire mandatory
   battery once for that candidate. Keep the source unchanged during the run.
4. Commit the qualified batch locally. A code change after the gate requires a
   new exact-source gate; a failed gate is diagnosed with focused checks before
   repeating the complete battery. Never qualify intermediate commits from the
   final batch's fingerprint or bypass the hook.

Commit-per-step now means per qualified integration batch. Do not run a separate
full battery solely to commit each component. Split a batch only when an actual
dependency or observed failure warrants isolating it, and record the reason.
Materials/previews and broader window behavior remain subsequent batches.

### Autonomous continuation: window composition prerequisites

The user authorized continued unattended iteration on 2026-10-09. The everyday
shell batch is committed locally as `a966406`. A thread heartbeat named
`continue-mihawk-os-development` resumes this work every 15 minutes; it must
inspect active jobs and current evidence before starting anything. Keep using
Pro only, one console job at a time, existing readiness/drop guards and local
exact-source-gated batch commits. No push, publication, main promotion or
sibling edits are authorized. Notify on meaningful milestones or blockers.

A read-only audit of the pinned production source establishes the next dependency:

| Boundary | Actual implementation | Required extension |
| --- | --- | --- |
| Server snapshot | `server/window.c` marks every shaped or layered window `PW_WINDOW_UNSUPPORTED`; its existing `get_visible_region` already intersects window/ancestor regions and its input hit-test checks `win_region`. | Export bounded authoritative whole-window clips separately from the existing GPU-client clips; preserve server hit-testing and identity. |
| Retained compositor | `src/pw_window_compose.c` refuses the complete frame with `ENOTSUP` when any visible unsupported row is present; ordinary pixels are opaque `memcpy`. | Apply whole-window clips and explicit alpha/colour-key contracts without weakening validation or silently omitting unsupported content. |
| GDI driver | `ps5_surface_flush` sends colour bits but ignores shape bits; `ps5_CreateWindowSurface` ignores the layered argument and supplies a zero alpha mask. | Trace Wine's existing surface conversion first, preserve its ownership, and transport the actual pixel/shape semantics once. Avoid applying constant opacity twice. |
| DWM API | Pinned `DwmSetWindowAttribute` returns success from a stub; blur and thumbnail registration/update return `E_NOTIMPL`. | A successful attribute call is not evidence of an effect. Qualify composition before adding material/thumbnail consumers. |

The next coherent implementation batch is **shaped and layered window
composition**: real region clipping, constant alpha/colour keys and per-pixel
alpha, preserving mixed GDI/GPU stacking and native input. Host checks must
compare independently calculated pixels and reject malformed/overflow metadata.
Paired PE32/PE64 console probes must capture actual cutouts and blended pixels,
change shape/opacity at runtime, check input through cutouts, move/hide/show,
destroy/reuse HWNDs, and complete clean logout/relaunch. Preserve exact source
and run the full expanded gate once the combined candidate passes focused checks.
Only then implement rounded shell surfaces, shadows/blur and previews using
those shared contracts.

The first shape candidate is now uncommitted: snapshot v3 carries independent
whole-window region ranges; patch 0988 exports Wine's canonical region, and the
retained GDI compositor applies it. Empty regions stay empty; layered windows
remain explicitly unsupported until their separate pixel contract is ready.
Eight affected ASan/UBSan host suites pass, including 169 shape translations and
10,816 independent expected pixels. The sandbox's ptrace prevented LeakSanitizer
startup; the same suites passed outside it without disabling sanitizers. The
capture-runner checks pass, and both composition/snapshot probes compile for
PE32 and PE64. The expanded existing composition cases require eight captures,
including a cross-process cutout, input through the hole, an empty region and
region removal. No full gate is justified yet: shape feedback comes before the
remaining alpha work in this combined batch. Production build01 and its focused Pro batch pass **4/4 in 117.5 seconds**,
with zero recorded fatal faults/protocol errors and clean returns. Both
composition clients pass all eight captures, including input through the shaped
cutout. Both snapshot cases pass on version3. See
[shape evidence](../evidence/mihawk-shape-focus-2026-10-09-01.json).

The next alpha candidate adds explicit immutable premultiplied-page format,
coverage/opacity normalization and source-over composition; opaque pages retain
memcpy. Patch0989 connects Wine's surface alpha and shape bits and its separate
UpdateLayeredWindow opacity callback. Nine affected sanitized host suites pass,
including every one of 65,536 alpha/opacity pairs and page replacement/refusal.
Both expanded probes compile and require 13 captures. Pixel-alpha input routing
is deliberately included to expose the next actual failure. GPU/child layered
surfaces remain explicitly unsupported. Alpha build02's focused pair fails **0/2 in 118.1 seconds**: all 13 displayed
captures pass in each architecture, but both fail the one transparent-pixel
mouse assertion (44/45 checks). Both return normally with zero recorded fatal
faults/protocol errors. See
[alpha evidence](../evidence/mihawk-alpha-focus-2026-10-09-02.json).

Patch0990 now carries the real surface coverage into Wine's authoritative
hit-testing via an owner/lifetime-checked request. It keeps the application's
window region separate, bounds bitmap/total storage, replaces transactionally,
and frees at HWND destruction. The probe also checks zero constant alpha and
colour-key click-through. Production build03's [focused pair](../evidence/mihawk-alpha-focus-2026-10-09-03.json)
passes **2/2 in 112.6 seconds**, **47 checks and 13 actual captures per
architecture**, with normal returns and zero recorded fatal faults/protocol
errors. Its fingerprint is
`fa4d9c0c2651438a35f9589751f7bc37770ea8a037219537a2062174a130907b`.

Before the complete gate, the final probe also requires positive input on
partially transparent/non-key pixels, coverage after moving the layered window,
and opaque pixels/input after removing the layered style. Build04's [focused pair](../evidence/mihawk-alpha-focus-2026-10-09-04.json)
fails **0/2 in 132.7 seconds**, **54/55 checks per architecture**: the new
positive input/movement checks pass, but removing `WS_EX_LAYERED` leaves pixels
at the preceding 50% opacity (expected red 220/50/60; displayed 125/115/65 over
green). Both return normally, with zero recorded fatal faults/protocol errors.

Patch0991 resets the driver-owned opacity and input cache through Wine's existing
style-change callback, before its normal surface update. No per-application
exception is added. Build05's [focused pair](../evidence/mihawk-alpha-focus-2026-10-09-05.json)
passes **2/2 in 140.4 seconds**, **55 checks and 14 pixel-verified captures per
architecture**, with normal returns and zero recorded fatal faults/protocol
errors. The [full gate](../evidence/mihawk-alpha-gate-2026-10-09-05.json)
stopped after **27 passes and one runner refusal (28/58 attempted)** in
**805.5 seconds**, on fingerprint
`2db565aff5d92dcbce5333a1e36b027df8c46cca2cb9b652a970c2633d0178ac`.
All composition, snapshot, shell, theme, memory-class and graphics-teardown
cases that ran pass. Before `network-adapters64`, the native census finds a
surviving payload from the preceding `network-adapters32` run; no PE64 adapter
probe launched. The latter's Windows checks pass and the title returns, but the
owner reports native close timeout (`closed=0xffffffff`) for its `winedevice`
helper. This prevents qualification despite the earlier probe's pass marker.

A subsequent bounded read-only census confirms that helper still exists, with
four threads and first-thread wait `umtxn`. Its last loader line is the census,
immediately before `__wine_virtual_image_stats`; no payload-terminal line follows.
That export takes Wine's virtual mutex on the same native loader thread that
polls owner closure. A blocked statistics call is therefore a concrete liveness
hazard and the leading explanation, not a captured stack proving the exact lock.
The control agent did log owner EOF. No outage guard was reset and no helper was
force-killed. Console launches remain blocked by the surviving-helper census.
A full Pro reboot/restored services has been requested asynchronously.

The local correction now separates optional telemetry into a detached 256 KiB
native worker; the loader's owner-closure supervisor no longer calls Wine
statistics. Metrics remain available with image-statistics phase markers, and
sampling no longer counts immediately readable control polls as elapsed time.
A blocked telemetry thread is never joined during payload shutdown. The existing
payload return route remains unchanged; no title calls exit or returns from its
entry point. This fixes the identified architectural hazard, but the exact
console hang diagnosis and correction still need qualification.

The runner now treats a recorded native-close timeout as failure in the
originating run. Its permanent host verdict test passes. Reanalysis finds that
error only in `network-adapters32` among this gate's completed records, so its
old aggregate pass is retained as historical output, not accepted cleanup proof.
See [cleanup review](../evidence/mihawk-alpha-gate-2026-10-09-05-cleanup-review.json).
The isolated payload candidate compiles without warnings to
`/tmp/mihawk-telemetry-candidate/proton-payload-child.elf`. Production build06
succeeds locally with fingerprint
`ac2e5f43ad1407a92e774f7cced4cd6837c6c59b8d06a69d3346a5babea41113`,
log `klog/mihawk-alpha-build-2026-10-09-06.log`. It is staged but **not deployed**,
with **no console job queued**. Do not deploy or launch until the user confirms
a fresh reboot and services. Then validate adapter/graphics teardown and memory telemetry under
shutdown, with native absence proof, before a new complete exact-source gate.
A deliberately blocked telemetry test remains to add/qualify; a successful build
is not evidence of liveness. All composition/runtime changes remain uncommitted
and unqualified. No push or publication.

Offline regression design (2026-10-09): use a diagnostic-only bounded hold in
the actual telemetry worker, with a per-native-PID entered marker and a monotonic
deadline. Request normal owner closure only after observing that marker; require
the payload's terminal record and native PID absence before the hold expires.
Reuse the existing `payload_absence` check in `tools/run-process-probe.py`, which
also includes helpers identified by rendezvous before READY. A released/expired
hold, missing entry marker, close timeout or surviving PID must fail the case;
an ordinary Windows PASS marker cannot qualify it. Do not acquire Wine's internal
locks or create an unbounded diagnostic stall. This tests process lifetime with
a blocked worker, including the payload CRT after `wine_child_run()` returns;
it does not prove which lock caused the original survivor. No diagnostic has
been added, built or run yet; build06 remains unchanged and undeployed.

Recovery continuation (2026-10-09): the user explicitly confirmed a full Pro
reboot and idle restored services. The guard recorded that boot, readiness found
no leftover helpers, and build06 was deployed with its fingerprint unchanged.
The [focused teardown batch](../evidence/mihawk-teardown-focus-2026-10-09-06.json)
passes **5/5 in 256.6 seconds**: memory classes, graphics teardown, both network
adapter architectures and native signals. All return cleanly with no native-close
timeouts. Six long-lived helpers each complete 16 image-statistics samples.
This is focused evidence for the detached telemetry correction, not a full gate.

Candidate07 adds the bounded hold regression through the existing inherited
`WINETEST_` diagnostic namespace. The runner observes the actual worker marker
and a created probe window in the same native process before requesting normal
owner closure. It requires that window's WM_CLOSE/exit and native terminal record,
plus absence of every rendezvous/READY PID before 60 seconds from launch. Timing
the entire run conservatively avoids comparing host and console clocks. Missing,
duplicate or live process records, an expired hold, and a late success all fail.
Four host verdict tests, six battery-runner tests and the conformance verdict
checks pass. The production build and focused console validation are running as
`ps5proton-mihawk-telemetry-focus-07`; no full gate is queued. The mandatory gate
now has **59 cases**. Code remains uncommitted pending its complete pass.

Build07 and its [focused batch](../evidence/mihawk-telemetry-focus-2026-10-09-07.json)
pass **4/4 in 66.0 seconds**, on fingerprint
`da2f23c859575bcf4ffed05550f825a649b31ae03d4a28545c211fb56146121f`.
The deliberate telemetry hold is observed in the window's native helper;
normal WM_CLOSE/exit and terminal records follow, and all **nine** recorded
native PIDs are absent at **12.532 seconds** from launch, before the 60-second
hold expires. The graphics and paired adapter teardown cases also pass, without
native-close failures. The complete frozen-source gate is now running as
`ps5proton-mihawk-alpha-gate-07`, evidence name
`mihawk-alpha-gate-2026-10-09-07`. Do not change source or start another console
job while it runs. No full-pass or code-commit qualification is claimed yet.

Gate07 stopped at the requested case boundary after **2/59** cases in **91.3 s**:
PE32 shell failed native cleanup; PE64 shell passed. The PE32 shell's Windows
interaction verdict and all four pixel captures pass, but a `tabtip.exe` helper
reported `closed=0xffffffff`. Unlike gate05, its log contains the native terminal
record, and klog records its exit after the timeout. The following PE64 readiness
census is empty, so no persistent survivor or new reboot requirement is inferred.
The stricter timeout verdict correctly prevents qualification. This does not
disprove the bounded telemetry test; it exposes a further shutdown delay.

Candidate08 instruments the existing path without changing timeout or cleanup:
monotonic timestamps bracket the final log fsync, payload main return, and the
owner's native-absence polling. The payload SDK's local CRT disassembly shows
runtime finalizers/library closure after main; current evidence does not yet
locate the delay in that path versus the final flush. The build/deploy and paired
shell plus telemetry checks are running as `ps5proton-mihawk-terminal-focus-08`,
evidence name `mihawk-terminal-focus-2026-10-09-08`. No complete gate is queued.

Terminal-focus08 completes **1/3 in 127.2 s**: both shell architectures fail
native close, while the held-telemetry case passes. PE32's delayed helper starts
its terminal path at monotonic **1,456,182 ms**, exactly when the owner's absence
wait ends (**1,446,172–1,456,182 ms**). Its final flush then takes **28 ms**, and
main returns at **1,456,231 ms**. The delay precedes the flush/CRT: the peer sees
hangup only after the owner's old wait-then-close path closes its descriptor.
With unread control packets, shutdown alone does not supply the hangup that
the native supervisor needs independently of Wine's control consumer.

Candidate09 makes `payload_close` consume its control descriptor before waiting
for native absence. All three call sites transfer ownership once, including
failed bootstrap cleanup; the monitor removes the fd from its shared slot before
closing it. Native absence still requires ESRCH within the existing timeout;
EPERM or a timeout never qualifies cleanup. A deterministic ASan/UBSan host
regression passes close-before-poll, single ownership, failed shutdown and denied
absence cases (run outside the ptrace sandbox for LeakSanitizer). The production
build/deploy and six focused shell/graphics/adapter/telemetry cases are running as
`ps5proton-mihawk-control-close-focus-09`. No complete gate is queued yet.

Build09's [focused batch](../evidence/mihawk-control-close-focus-2026-10-09-09.json)
passes **6/6 in 137.4 s** on fingerprint
`de8a0fff128f3c50446eacfe0c8037b6da5abd656b66efb90a822d297a2caae8`.
Both shell architectures pass their interaction and pixel checks, with all
**30** recorded native closes completing within **601 ms** and no timeouts.
The graphics, paired network-adapter and held-telemetry cases also pass. This
qualifies the focused correction to the reproduced control-endpoint lifetime
failure; it does not yet qualify the integration commit. The complete 59-case
gate is now running as `ps5proton-mihawk-alpha-gate-09`, evidence name
`mihawk-alpha-gate-2026-10-09-09`. Source is frozen; do not start another console
job or change code while it runs.


Gate09 has stopped: **50 passing cases, one runner failure, eight unrun**, in
**1,785.0 s**. The failure is case 51, `payload-eight`: all eight requested child
native READY records appear (16 helpers including bootstrap), then the saved
klog ends. Control and the independent post-run process query time out; the
90-second follow-up klog has no bytes. No Windows probe verdict, native absence
or owner cleanup was captured. The user subsequently confirmed that the console crashed; no kernel panic
record or specific crash cause was captured.
The last sample shows 11,072 MiB direct memory free, 956 MiB flexible memory free
and 444/12,555 descriptors; it does not demonstrate resource exhaustion.
See [outage review](../evidence/mihawk-alpha-gate-2026-10-09-09-outage-review.json).

The new drop guard remains active. The earlier full-reboot confirmation predates
this outage and cannot clear it. The user has been asked for the present power/
responsiveness state and a fresh reboot with restored services. No console job
is queued. After that confirmation, first preserve the failed session's helper
logs through existing FTP before any launch overwrites them; the saved host
record contains only klog, so native READY does not establish Windows semaphore
readiness or locate the final stall. Select focused lifecycle instrumentation
from that evidence, before repeating the full gate. Do not guess a kernel fix,
repeat the old mmap inspection, or count the partial gate as qualification.
All runtime/composition code remains uncommitted; a966406 remains the last
qualified code milestone.


Recovery log retrieval: the user confirmed another full reboot and restored
payloads. The guard recorded the fresh boot; readiness found no running title
or leftover helpers. Existing FTP recovered **17 per-PID logs** from the failed
session before any new launch. The parent explicitly records **eight Windows
children ready and alive simultaneously**. All eight then request expected exit
code `0x600d` at console time **3507.601 s**. Terminal records occur around
**3723.135 s**, after the captured host klog stopped; that gap alone cannot
separate the original fault from later recovery/intervention. No complete exit
or native absence verdict exists. This narrows the suspected regression to
lifecycle completion, not loss of eight-child creation capacity. The responsible
code change and crash cause remain unproved. See [recovered evidence](../evidence/mihawk-payload-eight-recovered-2026-10-09.json).
Stale one-shot test hooks were removed on the idle console; no new launch or
repeat full battery was made. Next isolate the concurrent exit path with bounded
phase records before any further full gate; preserve the successful spawning
contract and do not add a child-count workaround.


Candidate10 adds opt-in direct-file lifecycle phase records around remote
shutdown, monitor close/native absence/resource release, and the child's exit
message. These bypass stderr/klog locks and preserve errno; each record owns
its short-lived descriptor. The runner clears the diagnostic file before a
traced launch and collects it afterward. The syscall-order/trace host test
passes under ASan/UBSan, and the existing conformance verdict test passes.
No shutdown behavior, timeout, child-count limit or kernel state is changed.
The build/deploy and one focused eight-child run are queued as
`ps5proton-mihawk-exit-trace-10` with a 120-second watchdog. The question is which
native exit phase precedes a stall; a full battery is not queued.


Candidate10 exact fingerprint is
`800c03186c823eae4305a07e986021ab5894bc53d6965f7108388907f487a92f`.
Its host log records successful FTP deployment, idle/census readiness, a control
launch and eight requested native READY records (123–130; parent 115). Two
earlier bootstrap helpers close within 206 ms. Later FTP/control queries time
out. **The user clarified that payloads were not loaded before process-probe**;
therefore this is not accepted as another confirmed crash or a controlled
fresh-boot reproduction. The timing discrepancy between that statement and the
recorded launch remains unresolved. The host job was stopped during post-outage
collection, so it has no finalized run verdict. No new launches are queued.

An apparent target mismatch was a diagnostic comparison error: project `.env`
contains only `ORIGINAL_PS5_HOST`/firmware, not `PS5_HOST`. Shared tooling uses
PS5_Vulkan's configured target, which differs from the unused Original target;
the systemd manager has no PS5_HOST override. No evidence establishes that the
job contacted the second console. The incorrect warning was corrected to the
user. Keep the outage guard active; do not infer crash, reboot or safe launch
from payload readiness alone. Once the user's usual services are restored and
the guard's boot requirement is met, retrieve
`/data/homebrew/PPSA99170/payload-lifecycle.log` and per-PID logs before any launch.
The diagnostic file was not recovered in this turn. Runtime changes remain
uncommitted and unqualified.


Recovery11: FTP recovered candidate10's direct lifecycle trace and all 17 helper
logs. The trace contains only two successful bootstrap-helper exits; none of
the eight test children reached `exit-send-begin`. Their logs stop at concurrent
NtTerminateProcess requests, after the parent's eight-ready/eight-alive marker.
No evidence places this stall inside the newly reordered endpoint close.

Code inspection finds an observer defect: the loader polls POLLIN and peeks at
packets that belong to its control-agent thread. A queued packet makes poll
return immediately, so the observer can spin indefinitely. Removing telemetry
from this loop removed its incidental blocking work. Candidate11 observes only
hangup/error with `events=0`, leaving all packets to their actual consumer.
A real SOCK_SEQPACKET host regression reproduces immediate wakes in the old
observer, verifies five bounded waits with queued data in the new one, preserves
that data, and detects hangup with buffered data plus invalid descriptors.
It passes under ASan/UBSan. This is a proven observer defect and a candidate
explanation for the console stall; console causality remains unqualified.

The control port initially refused connections. A read-only process census
measured console monotonic time **717,800 ms**, below the failed run's
**993,405 ms**; that is independent evidence of a fresh boot. The known control
payload was restored through existing websrv/elfldr (HTTP 200), the guard
recorded this boot, and readiness passed with no leftover helpers. No unrelated
payloads or kernel state were changed. Build/deploy and one traced eight-child
run are queued as `ps5proton-mihawk-exit-watch-11`; no full battery is queued.


Candidate11 passes the traced eight-child run in **27.8 s**, including the
Windows PASS, normal owner return and verified cleanup. Its lifecycle trace has
**16 native closes**, maximum **303 ms**, with no close timeouts. Exact source:
`dbd2a4a6ad6aa71c7677c66a064b556fcaa952ee786f24c580cbf4395e7d822d`.
The unchanged build is now running a focused **four-case** batch without the
extra trace: eight children, descendants, graphics teardown and held telemetry
(`ps5proton-mihawk-exit-focus-11`). This tests the observer's production path
before another full gate; no code commit is yet qualified. See
[observer review](../evidence/mihawk-control-watch-review-2026-10-09.json).


The untraced candidate11 [focused batch](../evidence/mihawk-exit-focus-2026-10-09-11.json)
passes **4/4 in 75.6 s**: graphics teardown 24.5 s, held telemetry 12.7 s,
eight children 23.9 s and descendants 14.5 s. Source remains unchanged.
The mandatory **59-case** gate is now running as
`ps5proton-mihawk-alpha-gate-11`, evidence name
`mihawk-alpha-gate-2026-10-09-11`. No other console job may start, and no code
may change while it qualifies the combined composition/lifecycle batch.


Gate11 stopped at a clean case boundary after **18 passes and one visual
failure (19/59 attempted)** in **507.7 s**. Appearance32's API checks and cleanup
pass, but its Dark capture retains a white pane behind newly dark-themed
text/icons. Appearance64 and the next theme-notification case pass. This is
not a process-lifetime failure or console crash. Source inspection finds that
ApplyTheme posts WM_THEMECHANGED, while our appearance app cached its background
brush until that notification arrived. A synchronous repaint could use the old
brush after system colours and selected icons had already changed.

Candidate12 removes that redundant brush ownership, using GetSysColorBrush at
paint/control-colour time and invalidating on WM_SYSCOLORCHANGE. Its existing
probe now checks the actual pane pixel immediately after a synchronous repaint,
before queued theme notification dispatch. No Wine notification semantics or
per-application runtime exception changes. The [focused pair and notifications](../evidence/mihawk-appearance-focus-2026-10-09-12.json)
pass **4/4 in 77.9 s**, with **1,037 checks per appearance architecture**, both
light/dark captures correct, and clean returns. The complete 59-case gate is
queued on exact fingerprint
`fc1dc2db483b895d8325c826304065a1fc1f649aa71275646ee64cec237ab7b3`.
See [failed-gate review](../evidence/mihawk-alpha-gate-2026-10-09-11-appearance-review.json).


Candidate12's [complete exact-source gate](../evidence/mihawk-alpha-gate-2026-10-09-12.json)
passes **59/59 in 1,893.9 s (31 min 33.9 s)** on `fc1dc2db483b895d8325c826304065a1fc1f649aa71275646ee64cec237ab7b3`.
The gate repeats both composition clients (**55 checks and 14 pixel-verified
captures each**), both corrected Appearance clients (**1,037 checks each**),
eight concurrent children, descendants, graphics teardown and held-telemetry
shutdown. There are **591 successful recorded native closes**, maximum **305 ms**,
and zero recorded fatal CPU/GPU faults, process-protocol errors or close timeouts.
This qualifies the combined local integration batch. The observer busy-poll
and endpoint-lifetime defects are fixed and covered; no captured scheduler or
kernel stack proves the entire cause of the earlier console outage. Layered
GPU/child windows, materials, previews and universal Windows 11 parity remain
unqualified. See [qualification summary](../evidence/mihawk-alpha-qualified-2026-10-09-12.json).


### Qualified foundation and evidence

The shared theme/caption increment passes the complete **42-case console gate**
on `2c0ced39a91045cd0d1c773e8e216e3b3bdb826b83d0a01b377bfcf528f3ba4d` in
**1,382.5 seconds (23 min 2.5 s)**, with zero recorded CPU/GPU fatal faults and
zero process-protocol errors. Each appearance architecture passes **1,029
checks**, exact selected-part caption equality and independent light/dark
pane/icon/glyph captures. Both cross-process receivers pass **14/14 checks**,
including colour/brush refresh and retained old theme-handle ownership.
All **41 sanitized host suites**, **7 image-verdict tests**, **5 battery-runner
tests** and the **141-patch pinned replay** pass. Evidence:
[full gate](../evidence/mihawk-theme-gate-2026-10-09-14.json),
[host checks](../evidence/mihawk-host-2026-10-09-12.json).
The theme increment is committed locally as `2d42026`.
The previous committed foundation is `b00444a`; its earlier 40-case evidence
remains [recorded](../evidence/mihawk-gate-2026-10-09-01.json).

Proven: persistent Default desktop, real PE32/PE64 children and Explorer display,
shared prefix/file/environment, normal application exits and desktop logout,
retained foreground pictures, approved licensed icons, light/dark selection,
managed metric upgrades and consistent live nine-point system fonts. The TLS
gate uses owned LAN endpoints and removes its test root/prefix/entry; no public
internet or firewall changes are required.

The qualified environment increment carries a separate, bounded UTF-8 application environment
from explicit catalog/launch overrides into the initial payload. Wine overlays
it after prefix/registry provisioning; ordinary Windows children retain their
existing startup environment. Paired Linux Wine probes pass 19 root, 10 child,
7 descendant and 3 explicit-child checks per architecture. The portable transport
and all **42 sanitized host suites** pass, with **142/142 patches** replaying
cleanly. An unchanged timed signal test failed to observe any deferral; its
host-only injection/handshake now passes ten repeats without changing runtime
synchronization. The matching production build and focused console pair pass: both architectures
pass 19 root, 10 child, 7 descendant and 3 explicit-child checks, returning
normally in 32.2 seconds for the pair. The complete **44/44 console gate passes in 1,408.5 seconds (23 min 28.5 s)**
on exact fingerprint `5b3bc04da9eaa3dbebd5d09091c4d3163b8a3c12de976e35e27eb5af89a64932`,
with zero recorded CPU/GPU fatal faults and zero process-protocol errors.
The paired oracle was repeated on the exact production probe binaries. See
[focused evidence](../evidence/mihawk-environment-focused-checkpoint-2026-10-09-03.json) and
[host evidence](../evidence/mihawk-environment-host-2026-10-09.json) and
[full gate](../evidence/mihawk-environment-gate-2026-10-09-03.json).

The environment milestone is committed locally as `a4b93be`.
The qualified metadata slice publishes Wine's authoritative HWND list as a bounded
coherent record, with native/Windows process ownership, lifetime generation,
stacking, geometry and independent visibility flags. A marked diagnostic compares
that record with real Win32 windows from both architectures and checks teardown
and explicit overflow. This is a prerequisite to retained-window pixels, not a
composition or full-desktop claim. Its build and console results follow. Inspection also found repeated
per-DLL Make startup dominated unchanged builds; PE targets are now batched
per architecture with the same explicit selection and failure rules. All four
completed module lists match the preceding per-DLL recipe (234 PE64 / 248 PE32
modules per configuration). An eight-target up-to-date benchmark has median
3.786 s individually versus 0.487 s batched, with unchanged binary hashes;
this does not measure a clean build. Build04 and matching deployment complete
on `ff34462d8e8c140a134ef2de7ecc059da32b9cc7c23d0ac3bd0e1e12b807f9c4`;
the first paired console batch is **0/2** because the harness requested
`C:\mihawk.exe`, which is not installed. Both roots report `c0000135` and
return to the launcher without entering either probe. The harness now uses
the existing managed `C:\windows\system32\mihawk.exe` path. No metadata
pass or code commit is claimed by that run. Build05 reaches both probes: each
passes **23/24** checks, including all metadata/lifetime/overflow checks. The
initial shell-readiness assertion races Explorer registration, while the later
shell-survival check passes. The diagnostic now waits up to five seconds for the
actual shell HWND. The observed desktop root has a real Explorer native owner,
which supports the forthcoming retained-root surface route. Build06 and its matching deployment now pass **24/24 checks per architecture**
in **50.1 seconds** for the pair, with normal shell logout/owner cleanup and
zero recorded fatal faults or process-protocol errors. Exact fingerprint:
`d6d774c24aa76a75ccce0af142fc0ced87e833f63202e3a8250a0fb9d82575a5`.
The complete **46/46 gate passes in 1,422.9 seconds (23 min 42.9 s)** on that
exact build, with zero recorded fatal faults and zero process-protocol errors.
See [full gate](../evidence/mihawk-window-gate-2026-10-09-06.json).
This qualifies copied window state, committed locally as `6beb560`.
The next uncommitted slice adds bounded, ACKed per-window GDI pages and retained
composition in that order. Focused displayed-pixel and input qualification is
pending; GPU HWND association and shaped/layered content are still separate.
The first console pair passes 14/14 pixel/input/lifetime checks each, and all
ten downloaded images pass an independent offline review. Its batch is 0/2
because the shared runner omits capture paths without a timed pad script; that
recorder is now corrected and the failed record is preserved. The actual
Explorer root also needs the normal Wine wallpaper setting, rather than a
renderer background. Its one-time initializer passes 9/9 checks per architecture
on pinned Linux Wine, preserving nonempty and subsequent empty user choices.
Build03 and the corrected repeat now pass **4/4 cases in 80.1 seconds**:
both wallpaper probes pass 9/9, both mixed-process probes pass 14/14,
and all ten captures pass independent pixel and real-background checks.
There are zero recorded fatal faults, process-protocol errors or exit stalls.
Fingerprint: `94111f67b346b946dcca33109ad12b36c8b57da60a49a7175f3bd527f18d5573`.
The matching full gate finishes **49/50 in 1,516.1 seconds**.
`appearance64` returns before its marker with initial status 1; its helper
records owner disconnection immediately after worker startup. The records do
not yet distinguish socket EOF from a receive error. All other cases pass,
including the new composition cases. No code commit is allowed; the unchanged
paired appearance repeat passes **2/2 in 41.6 seconds** on unchanged code.
The next build adds terminal receive/disconnect details without changing socket
handling, publishes canonical Wine cursor positions and makes a missing optional
wallpaper a plain-background success. The expanded wallpaper diagnostic passes
13/13 checks for both architectures on Linux Wine with both initially present
and absent wallpaper assets. Those changes remain uncommitted pending the gate.

The user withdrew the second-console test request and selected **Pro only**.
The second console is untouched going forward; no split gate or speedup is
claimed. Its basic startup failed before Wine and the owned restart-loop title
was closed after its normal watchdog stop failed. Second-console tooling and
unqualified daemon SDK/logging experiments are shelved in an ignored cache.
The existing production daemon was rebuilt from hash-verified cached SDK inputs
and exactly matches the previously qualified
`3479be42ed69ef1b9dbbd1bbde9d0c5c443faaa4e1e59b219e6487ecf8695f7e`.
(2026-10-16: re-pinned. That pin hashed its inputs by absolute path, so it rebuilt
only at the owner's paths; `platform/lapy/deterministic-build.patch` now names them
by relative path or file name, and the resident build gives
`c9710b7addda8615c995113c5db3f32334a3cb5814ec8053391108da41621ab4` (build id
`cc31319a...`) from three different folders with clang/LLD 23.1.1, which
`tools/build-production.sh` now pins. It has not run on the console yet: proof is
pending the next gate.)
The Pro's expanded focused batch passes **4/4 in 85.4 seconds**: wallpaper is
13/13 per architecture, composition/cursor is 19/19 per architecture, with ten
verified displayed captures. Both basic PE64/PE32 preflights also pass on the
Pro in 28.0 seconds. Those records refer to earlier build fingerprints; the
restored single-console build and deployment are complete. Its 47 sanitized
host suites pass. Its own full Pro gate finishes **48/50 in 1,593.2 s** on
`0a699e5e72bbc9c05beea77372355eeaf4dd87d03705862a499acaf32ed4cbf7`.
The TLS fixture used random ports rather than the already-qualified allowed
ports 1749/1750: zero HTTP answers and WinHTTP timeout 12002, matching the earlier
recorded host firewall failure. This is not a certificate or public-internet
failure. The following desktop helper terminates during startup: the owner
records a zero-byte receive and the helper records a zero-byte nonblocking
peek with POLLIN but no POLLHUP. The intermittent startup remains unresolved;
no CPU/GPU fatal or malformed protocol packet is recorded. Composition/cursor,
wallpaper, fresh prefixes and the remaining 48 cases pass. No code commit is
allowed. The unchanged desktop follow-up passes **2/2 in 53.7 s**, including
normal input and session cleanup. A second complete gate now uses the established
fixture ports 1749/1750 on the same exact build. The user's router internet block
remains compatible with these owned LAN tests; no firewall or router changes are
made. The intermittent startup is not claimed fixed by the passing repeat.
That second battery stopped before launching its first probe because the control
service refused connections; FTP, klog and the resident payload service remained
reachable. No new wallpaper failure or kernel crash is established by that run.
The user confirmed a full Pro reboot and restored services; a read-only check
then showed all four services answering and the title idle. The outage guard was
cleared only after that confirmation. A third full Pro gate now runs on the same
exact fingerprint with fixture ports 1749/1750. The user has also enabled internet
access, which is not needed for these owned LAN fixtures. That third run finds a
different failure in `appearance32`: all 1,029 API checks, normal cleanup, both
panes and caption comparisons pass, but the light search icon fails its displayed
pixel comparison (mean RGB error 6.8802, tolerance 2). The same capture matches
two alpha blends (error 0.033), locating an idempotent-painting bug in Appearance:
its WM_PAINT handler relies on a separate WM_ERASEBKGND. Repainting without erase
can blend artwork twice. The prepared fix clears the dirty backing inside paint
and clips the parent's paint away from real child controls. A diagnostic forces
three redraws without erase. On the installed GUI-capable host Wine 11.18, both
architectures fail the old code (368/576 light, 484/576 dark matching pixels) and
pass the fixed code (576/576 in both modes). This is a secondary GUI reference:
the pinned Linux host build has no display driver and cannot qualify this check.
The candidate is now included in the next production build, which must pass
both focused appearance checks and the full exact-source gate. No tolerance is weakened and no code commit is allowed yet.
See [repaint host evidence](../evidence/mihawk-appearance-repaint-host-2026-10-09-01.json).
The third complete gate finishes **47/50 in 1,581.2 seconds**: the repaint
comparison above, another pre-marker PE64 helper EOF, and a desktop-session
capture timing failure. TLS passes on 1749/1750 with normal cleanup; no recorded
fatal CPU/GPU or malformed protocol fault occurs. Desktop session's 16 checks,
child exits and normal logout pass, but the three fixed-time screenshots all
show the shell. Broker logs show each child actually becoming foreground later:
capture timing must follow the probe phase rather than time since shell's first
frame. The next diagnostic uses the existing capture rendezvous per child/browser.
Its first build stopped on the newly used `strncmp` import; the probe recipe now
links the existing msvcrt library for both architectures. The incremental second
build is in progress; the failed build was never deployed.
A bounded standalone control-socket diagnostic passes on the Pro: 100,000 ordered
64-byte messages, 52,925 observer poll events, 31,793 peeks and 21,132 expected
EAGAIN races, with zero empty peeks, errors, hangups or timeouts (196 ms). Both
socket ends stay open until observation ends. This does not reproduce the helper
startup failure, so the production disconnect rule remains unchanged; add actual
control-descriptor and close-origin observations before another hypothesis.
The repaint/capture/instrumentation candidate now builds and deploys on
`932c68c32a631a32e2a46c154e0ed7051afbd22bc7a7dcc64576873393f6a27a`. Its focused Pro battery passes **5/5 in 105.6 s**: both appearances
pass 576/576 repeated alpha pixels per mode and their normal API/capture checks;
basic PE64/PE32 start and return normally; the actual desktop session captures
both child colours and real File Explorer (559,338 light content pixels), then
logs out cleanly. Zero recorded fatal faults or process-protocol errors occur.
The complete **50/50 exact-source gate passes in 1,554.2 seconds (25 min 54.2 s)**,
with zero recorded fatal CPU/GPU faults and zero process-protocol errors. Each
appearance architecture passes 1,035 checks. Each mixed GDI/cursor probe passes
19/19 checks with five displayed captures; wallpaper is 13/13 each. Desktop
session passes 18 parent and eight checks in each child, with all three actual
window captures and clean logout. This qualifies the opt-in opaque GDI route,
canonical cursor coordinates and optional wallpaper handling. The observed
intermittent initial-helper EOF remains unresolved: this passing repeat and
added socket observations are not a fix claim. See
[complete gate](../evidence/mihawk-repaint-gate-2026-10-09-02.json). See
[focused evidence](../evidence/mihawk-repaint-focus-2026-10-09-02.json).
See [full failed gate](../evidence/mihawk-pro-only-gate-2026-10-09-03.json) and
[socket isolation](../evidence/mihawk-seqpacket-observer-2026-10-09-01.json).
See [focused Pro evidence](../evidence/mihawk-dual-primary-focus-2026-10-09-01.json)
and [Pro basic preflight](../evidence/mihawk-dual-primary-preflight-2026-10-09-02.json).
See [failed full gate](../evidence/mihawk-composition-gate-2026-10-09-03.json).
See [focused repeat](../evidence/mihawk-composition-focused-2026-10-09-03.json).

The GDI milestone is committed locally as `94185f8`. The next candidate extends
the opt-in route with GPU HWND planes, actual client rectangles and Wine's
canonical visible-region clips. The owner binds each GPU slot to its spawned
native PID before helper bootstrap; INIT must match. The WSI binds the full HWND
and 64-bit generation to its own native surface, preserves fence completion and
retains private pixels before replying. Nested opposite-architecture OpenGL/Zink
probes check GPU/GDI clipping, modal input, resize/hide/restore and normal child
teardown. Both are now mandatory gate cases (52 total). All 50 sanitized host suites and the pinned 146-patch replay pass. The first
PE64-root console run draws real 752×537 parent and 450×350 PE32 child GPU
images with successful presentations, plus the GDI modal and cursor. It passes
12/18 diagnostic checks: the GDI control is covered/black and misses input;
child exit misses its deadline. Four captures are preserved. The diagnostic
now explicitly puts the GDI sibling above the graphics child and pumps the
parent window while waiting for cross-process destruction, with GDI backing
observations before another compositor hypothesis. No complete GPU window
pass or new code commit is claimed. See
[first GPU run](../evidence/mihawk-gpu-window-focus-2026-10-09-01.json). The second
run passes 16/21 checks in 43.8 s: sibling input and normal child teardown now
pass, but all five displayed captures reject black GDI control pixels.
GetPixel returns CLR_INVALID despite a valid 200×100 visible clip. Inspection
locates Wine's ordinary GPU-client path dropping its parent GDI backing; the
PS5 broker cannot provide the native GDI drawable that path expects. Patch 0979
retains ordinary backing only in the opt-in route without changing canonical
GPU clips. Its paired probe now asserts actual GDI backing pixels; console
qualification remains pending. See
[second GPU run](../evidence/mihawk-gpu-window-focus-2026-10-09-02.json). The exact-build third
run passes 23/23 with all five independently checked captures in 43.8 s;
ordinary GDI backing pixels survive resize, input reaches each real HWND,
and the nested PE32 GPU child exits normally. The shell then logs out normally,
with zero recorded fatal/protocol faults. Reverse-architecture and Direct3D/DXVK
qualification follows through the same diagnostic and capture verdicts. Both
DXVK cases join the mandatory gate (54 total); no code commit is permitted yet.
See [first backing pass](../evidence/mihawk-gpu-window-focus-2026-10-09-03.json). The expanded exact-build
focus passes 8/10 cases in 325.9 s. Both OpenGL architectures pass 23/23 and all
five captures; GDI/snapshot pairs and existing graphics teardown pass. Both
Direct3D/DXVK roots pass 18/23: ordinary GDI pixels, input and lifetime pass, but
all five captures reject black GPU content. No fatal/protocol fault is recorded.
The next boundary observation counts coloured samples in validated, completed
GPU backing and reports canonical flags/clip counts, without changing rendering
or clipping. See [expanded focus](../evidence/mihawk-gpu-window-focus-2026-10-09-04.json). The fifth diagnostic
observes valid CLIENT/VISIBLE flags and nonempty canonical clips for both DXVK
HWNDs, but zero coloured samples out of nine in each of their first eight
fence-completed images. The missing colour precedes composition. The next run
uses explicit pre-Present staging readback; that diagnostic can drain work and
is therefore excluded from default gate presentations. No runtime workaround
or pass claim follows. See [DXVK boundary](../evidence/mihawk-gpu-window-focus-2026-10-09-05.json).


The sixth diagnostic adds staging readback before first Present and resize.
Both render targets match their intended RGB bytes, and all five actual display
captures pass (25/25 root checks). This is a diagnostic, not default-path
qualification: CopyResource/Map changes execution. Normal presentation without
readback reproduces black pixels in both architectures on Linux Wine 11.18 with
the same pinned DXVK version. Source inspection finds deferred unbound clears
survive the external blitter entry: endCurrentCommands suspends the render pass
and prepares only shared images. The owned DXVK patch ends the pass without
suspension before external rendering, preparing ordinary image clears and
shader-readable layouts for every caller. A checksum-pinned source builder and
both-architecture managed-prefix packaging are integrated; host and console
qualification remain pending. No application readback or binding workaround
is enabled in the gate. See
[console readback diagnostic](../evidence/mihawk-gpu-window-readback-2026-10-09-06.json) and
[Linux presentation reproduction](../evidence/mihawk-dxvk-present-host-2026-10-09-02.json).


The source-built DXVK fix now passes the identical Linux owned-window oracle
in both architectures: actual displayed RGB is 220,50,60 after normal Present,
without staging copies, Map or output-merger binding. The unmodified pinned
release produced RGB 0,0,0. All ten rebuilt modules are manifest-verified.
This is host evidence; exact production build07 and Pro qualification follow.
See [patched Linux presentation](../evidence/mihawk-dxvk-present-host-2026-10-09-03.json).


Production build07, deployment and all ten installed DXVK hashes match exact
fingerprint `e90c3d885030ed489dcafa9a769a3398ed8c9c05daf18fe5c6793f8a1610c5be`.
The GPU/DXVK milestone is committed locally as `8842689`.
The focused Pro battery passes **4/4 in 168.5 seconds**: each OpenGL/Zink and
Direct3D/DXVK root passes **23/23** with all five independently checked display
captures. Both opposite-architecture graphics children render, receive hardware
mouse input and exit normally; GDI siblings/modal pixels, resize/hide/restore
and later colour changes pass. No staging readback or output-merger binding is
used. Fatal CPU/GPU records and process-protocol errors are zero. The full
**54/54 mandatory gate passes in 1,720.2 seconds (28 min 40.2 s)** on
the same exact fingerprint, with zero recorded fatal CPU/GPU faults or
process-protocol errors. This qualifies the opt-in mixed GPU/GDI route and
source-built shared DXVK fix. Frame pacing, shaped/layered windows and the
complete shell design remain unqualified. See
[complete gate](../evidence/mihawk-gpu-window-gate-2026-10-09-07.json).
See [focused GPU/DXVK pass](../evidence/mihawk-gpu-window-focus-2026-10-09-07.json).

The next uncommitted prerequisite qualifies taskbar work-area reservation.
A paired GUI-capable Linux Wine 11.18 diagnostic reproduces per-process SPI
caching: both roots fail 5/21 checks, and opposite-architecture children fail
3/9. GetMonitorInfo misses the changed rectangle and a pre-existing parent
misses its child's update. The candidate stores raw geometry coherently in
Wine's existing desktop shared object, keeps normal DPI conversion, and sends
requested change notifications without a registry write. Paired probes also
check actual maximization and restore the original area. The mandatory gate
now includes both work-area cases (56 total); no new code commit is qualified.
See [Linux reproduction](../evidence/mihawk-work-area-linux-2026-10-09-01.json).
The exact production build and deployment now pass the focused Pro pair
**2/2 in 34.9 seconds**: each root passes 26/26 checks and each
opposite-architecture child passes 9/9, with normal cleanup and zero recorded
fatal/protocol faults. SPI, monitor information, notifications and actual
maximized client bounds agree, including an already-running parent's refresh.
The complete gate finishes **55/56 in 1,736.7 seconds (28 min 56.7 s)** on
`2ef0a584e3751ca9` (full fingerprint in the gate build record).
See [focused Pro evidence](../evidence/mihawk-work-area-focus-2026-10-09-01.json).
That gate records a pre-marker failure in socket-options64: the helper
reports bootstrap error 1 before its input-page success marker, and the owner
cannot adopt that page. Its later zero-byte control peek follows owner cleanup;
this instance is not evidence that EOF caused the bootstrap refusal. Existing
records cannot distinguish first-packet receive/validation from input allocation
or send failure. The next candidate records those boundaries and the owner's
actual refused packet header, preserving transport handling. The failing
record stays in the gate; no commit is qualified by it. The prepared observations
are now applied in the owned payload and process adapter, including mapping
refusal errno; transport behavior stays unchanged. Production build02 and deployment complete on
`769acb894a94c69af517c48629df3df3819a880bb2908a1aaff04afc8ddee0e1`.
The sanitized packet-transport host test passes; the paired work-area/socket
probes and eight-child focus pass **5/5 in 77.2 seconds**, with complete
bootstrap packets and successful input-page transfers in the sampled records.
The complete **56/56 gate02 passes in 1,700.9 seconds (28 min 20.9 s)**
on that same exact fingerprint, with zero recorded fatal CPU/GPU faults or
process-protocol errors. This qualifies the shared primary-monitor work area
and the added observations. Neither instrumentation nor a passing repeat
establishes a startup fix; the intermittent failure remains unresolved.
See [complete gate](../evidence/mihawk-work-area-gate-2026-10-09-02.json) and
[focused repeat](../evidence/mihawk-work-area-focus-2026-10-09-02.json). See [failed full gate](../evidence/mihawk-work-area-gate-2026-10-09-01.json).


Next, in dependency order (the current summary above owns active-job status):

1. Build on the qualified everyday shell and shaped/layered top-level GDI route
   for the next coherent window-behavior/materials/previews batch. Audit the real
   Wine/DWM APIs first; preserve canonical ownership, lifetime, input and clips.
2. Add the remaining explicit prefix selection/import and accessibility
   milestones from the approved request, using actual files and applications.
3. Qualify each complete candidate with focused host/oracle and console checks,
   then one frozen full gate before its local code commit.

The centered taskbar, Start and Explorer interaction batch and shaped/layered
GDI composition now have console qualification. Full Windows 11 fidelity,
rounded shell styling, blur/shadows, previews and complete shell behavior remain
**unqualified**. Layered GPU/child surfaces remain explicitly unsupported.

The native launcher stays the boot entry during qualification. Do not promote
this branch to `main` or publish it without the user's request. Code commits
still require the entire exact-source gate; focused passes are insufficient.
See [Mihawk OS](MIHAWK_OS.md) for responsibilities, proof limits and the next
composition contract. The dated investigations below preserve earlier failures
and constraints; their superseded “next” actions do not replace this ordering.

Production baseline: **2026-10-07**, local `main`, incorporating the user-accepted
`win-32bit` work based on `2a58eb3`. Earlier milestones and experiments are in the
[historical plan](history/2026-10-07/docs/PLAN.md).

PE32 CPU, WoW64, processes, input/audio and graphics have console evidence in
[IA32_DESIGN.md](IA32_DESIGN.md). The paired conformance workflow compares identical
PE32/PE64 tests with their host baselines; see [CONFORMANCE.md](CONFORMANCE.md).
Patch 0955 fixes the PE32 message-processing stall: WoW64 now widens the inner
hardware input as well as its outer request. The focused input check passes all
60 checks in each architecture, and the PE32 upload probe completes naturally in
11.4 seconds (previously 221.3 seconds with watchdog intervention).

The 32-bit native game reaches active gameplay on a repeat launch; the person confirmed picture,
controls and sound. The FNA game's OpenGL build has captured gameplay and non-silent audio.
The .NET game's PE64 control run loaded a world, moved the character and returned to the
launcher. The person subsequently confirmed video, input and sound across the
games and requested this baseline become `main`. Keep the earlier FNA game's DirectX
Mono relaunch crash, the 32-bit native game's startup pause and wider memory API differences open.
Manual acceptance does not erase a recorded intermittent failure.

The current build passed all 15 required gate cases, including focused input
and large-upload regressions; its result is recorded in
[gate-message-fix-2026-10-07-01](../evidence/gate-message-fix-2026-10-07-01.json).
The full fix/game comparison is
run message-stall-fix-01 (2026-10-07; record removed from the tree; in git history).

## Active shared-prefix qualification (2026-10-08)

At the user's request, the shared-prefix branch now starts from stable local `main` at `8682902`;
`main` itself is unchanged. The shared [prefix baseline](SHARED_PREFIX.md) moves
runtime-owned provisioning after wineboot, before the first application imports.
Normal launches also receive working stdout/stderr handles. The restored build's
old output probe falsely passed despite both writes returning error 233; its
[corrected interpretation](../evidence/stable-manual-stdout-2026-10-07-01.json)
preserves that failure.

Focused console checks now pass complete output writes and redirected child pipes.
The font probe exposed a missing DirectWrite Unix FreeType backend; after it was
added through the existing PRX recipe, the existing PE64 prefix and fresh PE64/PE32
prefixes each pass all 42 import, converter, glyph and raster checks. Fresh setup
plus the probe takes about 149 seconds in either architecture. See the
[DirectWrite isolation](../evidence/directwrite-isolation-2026-10-08-01.json).
The expanded [19-case gate](../evidence/gate-shared-baseline-2026-10-08-01.json)
passed on source fingerprint `ec09e6de9420cd9cc2246d0faf0a8a3febe56ac0a9426117a70958e5fb1022fd`.
The baseline is committed locally on the shared-prefix branch as `d3e0244`. The 32-bit native game and the FNA game
OpenGL repeat runs render and submit non-silent audio, close with status 0 and
return to the launcher. The 32-bit native game's pause menu responds to input; the FNA game reaches a
level. These runs do not independently prove character movement. The 32-bit native game's
38.6-second startup pause remains.

The earlier emulator experiment's normal launcher run lasts 143.9 seconds, shows readable text and closes
normally. The person reports immediate startup. A touchpad script makes the
cursor visible and opens Settings between 0.5 and 1 second after the click.
The initial pointer was outside the cropped window view; the person's question
about delayed cursor creation remains to be isolated with a stationary timed run.
The first settings run exposed background repaints overwriting the dialog. Patch
0958 retains each window's pixels and composes in the server's stacking order.
A run (2026-10-08; record removed from the tree; in git history)
shows a readable unobscured dialog, closes the dialog and launcher normally,
returns to Proton and reports status 0. The person confirms correct windows.

Patch 0957 routes native SDK signals to their prior native handlers and retains
Wine handling for Wine threads. Its focused PE32/native-handler test passes.
The FNA game's DirectX build still crashes: the repeat now exposes the original native SDK
fault in `libSceLibcInternal`, offset `0x448d`, address `0x60`, instead of Wine's
secondary GS:0x30 fault. This is a diagnosis improvement, not the FNA game's fix.
The person independently confirms the FNA game's OpenGL build and the 32-bit native game work, while DirectX
crashes after a few seconds. See the
repeat DirectX failure (2026-10-08; record removed from the tree; in git history).

### Immediate priority: Settings / Controls

At the person's request, Triangle opens launcher Settings; Controls remaps left,
right and middle mouse clicks to controller buttons, with Restore defaults.
The saved global map applies to every subsequent launch. Assigned buttons are
consumed before XInput/HID; touchpad motion remains independent. Disconnect and
shell interception release held clicks. The default keeps touchpad left click.
`src/pw_controls.h` owns the contract and the stable persisted button IDs.

Three sanitized host tests pass for controls, touchpad and DualSense publication.
The console menu check navigates to Controls, selects R1 for left click and
captures the resulting page without changing the player's saved file. The [PE32 click check](../evidence/controls-clicks-2026-10-08-02.json) receives
185 moves and one press/release for each remapped mouse button, closes with status
0 and restores the player's exact previous controls-file state. The full
[23-case gate](../evidence/gate-controls-2026-10-08-02.json) passes on source
fingerprint `c78d3b84340e7e12c3dd1ce50834ef7e5001603d3e3cabae5671a59597c661e5`.
The earlier attempt stopped because the console was occupied.
Do not promote this branch or claim the FNA game's DirectX build fixed.

### A guest-startup address conflict (the earlier emulator experiment, removed)

The person reported an address-reservation error starting a game inside the earlier emulator experiment.
Direct emulator launches reproduce it and close with status 1; bypassing the earlier emulator experiment's
launcher does not avoid it. The requested placeholder spans `0x40000` through
`0x64940000`. Focused diagnostics confirm small ordinary and placeholder
reservations work, but Wine calls the native carrier's `0x400000` block
`MEM_FREE`; the console refuses it with error 487 / `STATUS_CONFLICTING_ADDRESSES`.
The identical PE64 query probe passes 97 reservations on pinned Linux Wine and
finds 25 falsely free 64 KiB blocks on PS5, first at `0x400000`. This is a
Windows-query/native-mapping ownership gap, not a demonstrated shortage of physical memory. See the isolation record (2026-10-08; record removed from the tree; in git history).

`virtual-query32` / `virtual-query64` are permanent battery cases and are required
by the pending query-fix gate. The already committed 23-case checkpoint predates
that fix. Fix the shared native-memory census/query boundary; never overwrite the
carrier or relocate a single game's guest addresses as a workaround.

The next shared query fix is built in the working tree (0959), with passing
sanitized query/ownership tests. Its native interval subtraction also handles a
Wine reservation inside a larger host mapping, including queries in the owned
middle and the remaining native tail. It has now passed the paired focused console checks: PE32 reserves 18 free blocks and PE64 reserves 230, each with zero conflicts and clean return. The full 25-case exact-source gate passes on fingerprint `0f2fe8b10c1b8ab0195469cee110484b2f393eb5720f3994742e30a9b3aee927`.
Read-only payload checks validate the kernel query record and NEXT flag against
the protection-query API. Separate one-page tests reject reservations, anonymous
and shared-object mappings at the earlier emulator experiment's 8 TiB extended arena with EINVAL. The
read-only VM/ABI headers contain a 128 TiB boundary; that alone does not prove
that the mapping APIs accept the range.

The same supplied earlier emulator experiment binary completes guest memory initialization on pinned
Linux Wine and system Wine 11.18. Both host runs stop at display initialization;
neither is gameplay proof. The game-independent `guest-address-space` diagnostic
reproduces the earlier emulator experiment's three-band placeholder contract without game assets: pinned
Linux Wine reserves six spans totaling 1,632,073,809,920 virtual bytes and releases
all six, with no physical commit by the probe. It remains outside the console
gate until the extended band is supported. See [the host oracle](../evidence/guest-address-space-host-2026-10-08-01.json).

User-mode library review identifies a separate newer direct-memory mapping route
in `sceKernelMapDirectMemory2`. The bounded one-page probe has now run and returns
`0x8002002d` at both its ordinary control and extended address; neither mapping
succeeds. This does not establish that the newer route supports the extended band.
Do not use sanitizer-specific flags as a game-memory workaround. See
the interface review (2026-10-08; record removed from the tree; in git history)
and [the measured probe](../evidence/modern-direct-2026-10-08-01.json).

After the 23-case gate completed, a bounded read-only mmap-handler inspection
timed out. FTP, web, control and klog then became unreachable. No kernel writes
were made; power state is unconfirmed. The drop guard is recorded. Do not repeat
the handler inspection. The user restored the console. The first title launch was rejected with `0x80940033` before runtime startup; after the user reloaded ShadowMount, both query probes passed. The second emulator workload has not run; diagnose the next actual earlier emulator experiment failure. See the interruption record (2026-10-08; record removed from the tree; in git history).


The larger console diagnostic and the emulator run directly now both pass the
original carrier conflict and fail allocating Wine's 1 MiB page-protection
metadata at the span starting `0x260000000`. Five placeholders totaling
9,647,226,880 virtual bytes were reserved and released by the diagnostic; its
first failed span is 23,865,196,544 bytes. The title reports 11,542 MiB of direct
memory free, so the recorded error is native bookkeeping placement, not physical
pool exhaustion. Patch 0960 moves these zeroed process-lifetime tables to Wine's
existing application-memory heap. Sanitized heap/query tests pass. The rebuilt/deployed patch passes both initial address bands: 15 placeholders totaling 1,058,093,400,064 virtual bytes are reserved and released. The full [25-case exact-source gate](../evidence/gate-native-vm-2026-10-08-01.json) passes on the same production fingerprint. The two shared fixes are committed locally as `1910088` on the shared-prefix branch. See the
[focused console battery](../evidence/native-mapping-console-2026-10-08-02.json).
Both the diagnostic and the direct emulator now reach the 8 TiB band and fail
its 512 GiB placeholder reservation (Win32 error 487 in the probe). The direct
emulator reports the same address and size, then the helper records SIGILL at
loader address `0x200000398` during its failure path; this is not clean-gameplay
or clean-emulator teardown evidence. See the
[extended-band diagnostic](../evidence/guest-address-space-console-2026-10-08-03.json).

The ordinary direct-memory one-page control at 64 GiB maps and unmaps correctly;
the same API rejects 8 TiB with `0x80020016`. The memory-pool control also returns
`0x80020016`, so it does not establish a working alternate route.
`sceKernelMapDirectMemory2` returns `0x8002002d` at both addresses. Offline review
shows its user-mode gate requires `kern.app.memconf` version >= 2 and a cleared
bit 1 in the second word returned by `sceKernelGetAppInfo`; the payload's read-only query returns version 1, flags 1.
This explains the early rejection but does not establish whether configuration
is selected per process, budget, hardware or another policy. No configuration
write, raw syscall bypass or kernel inspection was attempted. See the
[mapping-class checks](../evidence/mapping-class-2026-10-08-01.json).

The bounded active-owner comparison completed cleanly: the payload reports native
ABI 1 and memory configuration version 1; the title reports the same ABI and
version 2. The title's newer direct-memory ordinary-address control succeeds,
but its 8 TiB mapping and ordinary anonymous mapping still fail. Production was
restored and checked after the isolated title run, with no fault records. This
rules out changing payload memory configuration alone as a sufficient high-band
solution. See [the active payload](../evidence/active-mapping-2026-10-08-01.json)
and [the title comparison](../evidence/owner-mapping-2026-10-08-01.json).

The direct-emulator trace provides the next shared inconsistency: Wine measures
`host_addr_space_limit=0xffffff0000` and rejects larger fixed allocations before
calling its host mapper, yet `VirtualQuery` reports the 8 TiB range free. Patch 0961 bounds free-space queries by that existing measured limit and reports
the unavailable upper range through Wine's existing reserved-region query path.
It does not extend the mapping aperture or report synthetic allocation success.
The expanded permanent PE64 query check tests eleven power-of-two upper addresses;
PE32 retains its low-address test. Pinned Linux Wine passes both: 102 PE32 and
108 PE64 reservations, with all eleven upper queries free and reservable on Linux.
The exact production build is deployed. Both focused console checks pass:
18 PE32 reservations, and 232 PE64 reservations including two reported-free upper
addresses; the remaining nine upper queries are unavailable. All tested free
addresses are reservable. The complete [25-case exact-source gate](../evidence/gate-host-query-limit-2026-10-08-04.json)
passes on fingerprint
`b6a1b5d9fe812986b5dd1470c120f5b6cecb89f4f26c95d16d2aa103c84c98cf`.
The earlier foreground runner interruptions (status 143) and initial shell
rejection are preserved as incomplete attempts. The successful gate ran in a
transient user service; 0961 is committed locally as `1731123`. See
[the focused query comparison](../evidence/query-host-limit-console-2026-10-08-01.json).
The stricter three-band diagnostic still requires usable capacity in every band;
the earlier emulator experiment itself skips unavailable intervals. Only an actual emulator/game run can
establish whether its workload needs the unavailable band.

### Current next failure: large shared-section backing

The actual direct emulator run (2026-10-08; record removed from the tree; in git history)
now completes its guest placeholder scan and fails its 13,824 MiB shared backing
view with `STATUS_NO_MEMORY` (`0xc0000017`). It produces no game frames. The
scan has occupied almost every ordinarily available Windows address, leaving no
contiguous backing-view placement. Separately, native tests establish that a
large view still fails even inside an available, owned 64 GiB virtual arena.
An internal section-view arena alone therefore cannot solve this failure.

All following native probes keep their own temporary sparse file linked until
unmap/close, then unlink it; no game assets or kernel configuration are changed.
They reserve virtual space with no overwrite and release it on completion.
They establish mapping behavior, not physical memory capacity.

| Probe | Measured result |
| --- | --- |
| [Sparse file sizing](../evidence/section-arena-native-2026-10-08-01.json) | `/system_tmp` refuses 13.5 GiB sizing with `ENOSPC`; cleanup succeeds. |
| [Anonymous shared object sizing](../evidence/section-arena-native-2026-10-08-02.json) | `SHM_ANON` refuses 13.5 GiB with `ENOMEM`; cleanup succeeds. |
| [Data-file full view](../evidence/section-arena-native-2026-10-08-03.json) | `/data` sparse sizing succeeds; the full 13.5 GiB shared view fails `ENOMEM` before any page touch. |
| [Length controls](../evidence/section-scale-native-2026-10-08-01.json) | Single shared views from 16 KiB through 8 GiB succeed; 13.5 GiB fails. Every successful view is unmapped before the next. |
| [Adjacent chunks](../evidence/section-chunks-native-2026-10-08-01.json) | 8 GiB succeeds; the adjacent 5.5 GiB remainder fails. Splitting alone is insufficient. |
| [High offsets and aliases](../evidence/section-offset-native-2026-10-08-01.json) | 16 KiB at offsets 0, 8 GiB and the final page succeeds; a second 8 GiB alias of offset 0 fails. High offsets alone are not the limit. |
| [Accounting](../evidence/section-accounting-native-2026-10-08-01.json) | 8 GiB plus aliases through 1 GiB succeeds; a 4 GiB alias fails. Residency stays 3,588,096 bytes and available flexible memory stays 8,555,331,584 bytes; no pages touched. |
| [Protection and resource limits](../evidence/section-protection-native-2026-10-08-01.json) | 13.5 GiB fails for no-access, read-only and read-write. Memory-lock, virtual-size and swap limits are unlimited; data limit is 32 GiB. No limit was changed. |

The [read-only policy and finer-length controls](../evidence/section-policy-native-2026-10-08-01.json)
accept 8 GiB plus a 2 GiB alias, but reject aliases of 2.5 GiB and larger.
`vm.swap_total` reports 10,830,741,504 bytes; the other queried standard VM
policy keys are absent. This is consistent with a reservation-accounting limit,
but the enforcing counter has not been identified and no parameter was written.

The [asset-free Windows section oracle](../evidence/section-view-host-2026-10-08-01.json)
passes both a 2 MiB control and a full 13.5 GiB view on pinned Linux Wine. It checks
initial zeros, bidirectional aliases, section-handle-close lifetime and the
surviving alias after the full view is removed. The single-thread
[demand-mapping host smoke test](../evidence/section-lazy-host-2026-10-08-01.json)
passes two 13.5 GiB virtual aliases using real shared file pages and four faults,
including an evicted page remap. The [same native payload smoke test](../evidence/section-lazy-native-2026-10-08-01.json)
now passes on the console: initial zeros, bidirectional aliases, flushed/evicted
page remap, a surviving alias after the full view is removed, and complete
file/range/signal-handler cleanup. Residency rises from 3,588,096 to 3,637,248
bytes (48 KiB). The separate [small-window controls](../evidence/section-window-native-2026-10-08-01.json)
pass all three offsets. These are single-thread native proofs; Windows view
metadata, protection changes, concurrent faults, child descriptor lifetime,
cache pressure and Wine integration remain unqualified.
A scratch portable cache now passes a host-only eight-thread shared-alias test
with a four-page cache, forced eviction, retained descriptors and full cleanup.
ASan, UBSan and LeakSanitizer also pass outside the host sandbox; see
[the host cache record](../evidence/section-cache-host-2026-10-08-01.json).
The expanded [Windows host oracle](../evidence/section-view-host-2026-10-08-03.json)
also passes both sizes with eight threads, two full aliases, handle-close lifetime,
read-only/no-access faults and restored protections. Its first build omitted SEH
unwind records; that probe-build failure is preserved separately.
The [bounded concurrent console cache](../evidence/section-cache-native-2026-10-08-01.json)
passes eight threads, two 13.5 GiB virtual aliases, four cached pages, 389 faults
and 384 evictions with full cleanup. The [native lease check](../evidence/section-lease-native-2026-10-08-01.json)
confirms descriptor duplication retains a shared file lock until the last alias
closes. The [combined paging/backing test](../evidence/section-pager-native-2026-10-08-01.json)
also passes queued SCM_RIGHTS lease lifetime, a surviving alias, 389 page faults,
389 evictions including removal, and final linked-file cleanup. These are native
proofs, not Wine or the second emulator workload's acceptance.

Patch 0962 and the repository-owned shared-section modules are now under build
qualification. Large shared views retain Windows metadata while faulting real
shared file pages through a bounded cache; unconstrained PE64 file views use a
separate internal native arena. Fixed mappings retain their caller's addresses.
The memory helper falls back to sparse linked files when native SHM sizing fails,
with shared descriptor leases and nonblocking collection only after all users
close. The server must propagate helper refusal instead of making title-owned
backing. `tools/run-section-native.py` reproduces the combined native test;
`large-shared-section` is a new mandatory Windows gate case. The [same Windows contract now passes on PS5](../evidence/section-view-console-2026-10-08-01.json)
in 6.8 seconds: both section sizes, two full aliases, a tail alias, eight threads,
read-only/no-access exceptions, CloseHandle lifetime, fixed placeholder replacement
and preservation, and clean return without an exit stall. The matching
[host oracle](../evidence/section-view-host-2026-10-08-05.json) passes in 1.78 seconds.
The exact production build fingerprint is
`d83e6ae652fd1108cb2da79d9c8692d08e86aa45677e73e1c519d7ac76d35bad`.
The complete [26-case gate](../evidence/gate-shared-section-2026-10-08-01.json)
passes on that exact build. This qualifies the shared-section implementation;
the actual emulator has now been retested, with the next failure recorded below.

The [subsequent isolated title comparison](../evidence/owner-section-2026-10-08-01.json)
reaches native EXEC and `mDBG: Waiting for debug subsystem`, but no section
measurements are collected. Control requests and the bounded FTP result read
time out, although all four known TCP ports accept connections. This is not
proof of a failed large mapping or a kernel panic. The runner cannot establish
idle state and cannot restore production; its signed backup is retained in
`klog/owner-section-2026-10-08-01/production-eboot.bin`. The person subsequently reported a powered-on black screen, then restored console
access. Production is now restored from the signed backup and executable-readback
verified; the PE64 query and launcher controls runs pass and close normally. The
first recovery launch was rejected before Wine with `0x80940033`; that gate is
recorded incomplete; the subsequent complete 25-case gate passes. See
[production restoration](../evidence/production-restore-2026-10-08-01.json). Do not
repeat the isolated title comparison.

The refusal concerns native mapping accounting/placement; physical exhaustion
and an exact aggregate threshold are not established. The native demand-mapping route now has a bounded proof. Production is restored and the exact-source query gate passes. The bounded concurrent native cache and focused Windows shared-section contract
now pass. The exact-source regression gate now passes. Retest the actual emulator and
launch through the earlier emulator experiment before claiming game compatibility. Any demand-mapping backend must preserve
shared aliases, protections, descriptor lifetime, page-fault behavior and clean
unmap; do not substitute a private copy or report a full mapping as working.
The preceding source review found that the old memory helper created only
shared-memory objects, leaving Wine to create and immediately unlink a fallback
file in the title. Patch 0962 instead propagates helper refusal, and the memory
payload creates and retains a sparse linked backing file when shared-object
sizing fails. Shared descriptor leases keep the file linked through live aliases;
collection flushes and closes it before unlinking after the last lease ends.
The native lifetime probe and focused Windows test cover this integration.

Qualification remains bounded: the mapped page cache is 64 MiB per client and
its private arena is 64 GiB of virtual space, not physical application memory.
The focused Windows test covers read/write views and protection exceptions;
execution from shared pages, truncation/in-page errors, and faults in native SDK
threads remain unqualified. Private/write-copy and unaligned views retain the
existing mapper. Neither this cache nor the query fix extends the unavailable
8 TiB address band. Retest the actual emulator before choosing another change.
The implementation is committed locally as `c573c46`. The actual emulator
retest (2026-10-09; record removed from the tree; in git history) maps its complete
13.5 GiB backing section at `0x8000000000`, replaces a 16 KiB fixed placeholder
with a coherent backing alias, and coalesces it again. After the native Vulkan
helper loads, a late image mapping of 212,992 bytes cannot find space. Wine then
retries native occupied intervals outside its measured host aperture and finally
reports an exception frame below the thread's stack limits. The follow-up DLL/exception trace reproduces an assertion in 20.0 seconds,
with no rendered frame. The new asset-free `late-library` probe reproduces that
assertion at `LoadLibrary("uxtheme.dll")` after Vulkan's loader succeeds; it reserves
16 spans totaling 989,373,792,256 virtual bytes. The same probe passes all stages
on pinned Linux Wine in 2.52 seconds. Its initial PS5 failure returns the owner
without an exit stall. Patch 0963 keeps the native-view retry within the original
host-clipped bounds and uses host-page-rounded size. Native assertion and loader
reports now use the existing output sink. The [fixed focused probe](../evidence/late-library-validation-2026-10-08-01.json)
passes on PS5 in 7.3 seconds: both libraries load/unload, all 16 placeholders are
released, and the process closes with status 0. The production PE also passes on
pinned Linux Wine in 3.13 seconds. The actual emulator retest is next; the new
27-case gate has not passed yet.
The 120-second stop returns the owner to Proton with emulator status 1, no exit
stall and no rendered frames. This is memory-initialization progress, not game
acceptance. The second emulator workload rendering, controls, sound, clean exit and relaunch remain
unproved.

### The game launcher review after the shared memory and DLL fixes

The 2026-10-08 review (2026-10-09; record removed from the tree; in git history)
reproduces the rendered **Unexpected Transport Error (0x3008)** dialog. The run
lasts 137.7 seconds, returns to the launcher without an exit stall and leaves the
console idle. It does not reach login, library or game launch. The game launcher's internet
connectivity check succeeds, but its local UI WebSocket reconnect fails.

Socket tracing identifies 67 rejected `SO_SNDBUF=0` requests, each returning
`STATUS_INVALID_PARAMETER` / Windows error 10022. A small asset-free program on
pinned Linux Wine accepts that request and reports zero through `getsockopt` for
both IPv4 and IPv6. Upstream wineserver already preserves this Windows behavior
on Apple hosts whose native sockets reject zero; the PS5 branch has no equivalent
handling. This is a concrete shared Winsock compatibility gap, not yet proof that
it is the sole cause of the transport dialog. Adapter enumeration also fails with
error 2; the production Unix module list does not include the NSI proxy backend.

Next: paired socket-option/loopback probes and a shared semantics fix, then adapter
and local WebSocket qualification. Browser shared-memory corruption reported in
an earlier same-day run and DXVK descriptor-buffer allocation warnings remain
separate findings to isolate if they persist. There is no controlled build
comparison proving which the earlier emulator experiment's fix changed the game launcher behavior. This review changed no
runtime code or the game launcher installation settings.

## What is being built

A PS5 launcher and a GE-Proton-derived runtime for 64-bit and 32-bit Windows games and
applications. Every Windows program defaults to a separate payload process.
The title hosts wineserver, the GPU broker, display, input and audio. Descendant
requests return to that owner while Wine preserves Windows parent relationships.
OpenGL uses Mesa/Zink; DirectX uses DXVK or VKD3D-Proton over Vulkan/RADV.

The payload loader provides Sony's application-memory allocator to Wine.
The reported **11,712 MiB application pool is shared** by the title, GPU and
helpers. No kernel budget patch or per-helper pool reassignment was needed.

## Latest verified checkpoint

| Capability | Proof |
| --- | --- |
| Production regression gate | All 15 cases passed on source fingerprint `f27ba1ac28864daa5a4cc96ba863d16ea334f00df72ee562a52e17fc087d3ed5` |
| Payload capacity | Eight simultaneous Windows children, plus repeat and descendant lifecycle tests |
| Child graphics | Separate test: two DXVK children and two rendering descendants, correct pixels/input and native cleanup |
| Application memory | Two Windows payloads held 2 GiB each simultaneously, verified contents, decommitted/recommitted and released/reused backing |
| Windows memory behavior | Executable protections, adjacent contents and sparse 16 GiB virtual reservations passed focused checks |
| Application closure | Shell closure with a live 2 GiB graphics helper left all recorded helper PIDs absent and no CPU/GPU fault |
| Networking and desktop | HTTPS and touchpad pointer/click checks passed in the latest gate |
| The .NET game | Manual loading/menu/music confirmed; human input confirmed; latest default Zink run captured a world and returned to launcher |
| The game launcher | Normal command line, 120-second budget, normal close with status 0; complete client operation unproved |

The earlier payload-memory checkpoint is
[gate-payload-forapp-2026-10-07-02](../evidence/gate-payload-forapp-2026-10-07-02.json).
Detailed measurements and preserved failures are in [WINDOWS_PROCESSES.md](WINDOWS_PROCESSES.md).
An eight-child pass does not prove eight simultaneous multimedia games. A 16 GiB
virtual reservation does not mean the console has 16 GiB of physical application RAM.

## Acceptance targets

1. **The game launcher:** qualify its displayed UI, isolate the remaining nonfatal
   `wbemprox.dll` protection warning, then verify login, library, game launch,
   closure and relaunch. The person saw unusual UI on both console and host Wine;
   compare equivalent host behavior before assigning its cause to this port.
2. **The .NET game on the current foundation:** repeat the save/reload and longer-session
   lifecycle already proven on the earlier title architecture. Include manual
   input, audible sound and a follow-up launch of another homebrew.
3. **The Java game:** validate Java subprocesses on the current payload build, then
   the Java launcher → Java → the Vulkan configuration, copied-world load, sound/input,
   clean exit and relaunch. Earlier direct-launch Vulkan performance is a baseline,
   not acceptance of this chain.
4. **The FNA game's OpenGL build and DirectX:** extend the accepted short-session checks on
   `main`, especially repeated DirectX launches after the recorded Mono crash. The supplied installations require x86 and contain x86 native dependencies.
   Do not edit executable headers to claim they became 64-bit.
5. **Broader compatibility:** refresh Wine/DirectX test baselines for the new
   process architecture; reduce each new console-specific failure to a general fix.

No title-specific graphics/process architecture, synthetic acceptance bypass or
profile trick is a substitute for a shared fix. Keep necessary game configuration
documented, and test defaults separately from diagnostic overrides.

## Qualification still required

- Fresh-prefix setup and first-launch Zink staging under the current defaults.
- Process/GPU capacity beyond the eight-child and separate four-renderer proofs.
- Longer concurrent graphics/audio workloads and abnormal helper exits.
- Current-build DirectX 9/10/12 and wider Wine conformance results. Earlier counts
  do not transfer automatically to a new loader or memory backend.
- General 32-bit game compatibility, rumble, kernel drivers and anti-cheat are not supported claims.

## Working sequence

Read [ARCHITECTURE.md](ARCHITECTURE.md), then follow
[NEXT_CONSOLE_SESSION.md](NEXT_CONSOLE_SESSION.md). Portable checks come first.
For code changes, build production, deploy the matching title/runtime, and pass
the exact-source gate before a local commit. Add new mandatory checks to
`GATE_CASES` in `tools/run-battery.py`; never bypass the hook.

There is no per-boot launch cap. The tools retain 60-second pacing and refuse
launches after a recorded disconnect until a real restart is confirmed. Do not
clear the drop guard merely because a port accepts a connection.

Inputs are pinned by the build scripts and [NOTICE.md](../NOTICE.md). Sibling
repositories are read-only references unless separately authorized. Commits stay
local; the current user-requested baseline is `main`, promoted from `win-32bit`.

### The game launcher shared socket and network-service qualification

Patch 0964 retains Windows' advisory zero send-buffer size on PS5, using the
existing Wine behavior for hosts that reject the native zero setting. Identical
PE32/PE64 probes each pass all 30 checks on pinned Linux Wine. On PS5 the old
build fails 12 of 30 checks in each architecture; the rebuilt runtime passes
30/30 in both, including IPv4/IPv6 loopback traffic and buffer-size readback.
The paired probes are permanent gate cases. See
the comparison (2026-10-09; record removed from the tree; in git history).

The game launcher's 137.5-second retest still renders its Unexpected Transport Error 0x3008
dialog. Its previous 67 zero-send-buffer errors disappear; its local WebSocket
reconnect and adapter-discovery failures persist. Both runs return to the launcher
without an exit stall. Login, library, game launch and Big Picture are unproved.
The user requests Big Picture as the default after the game launcher's startup works.

A separate asset-free adapter probe fails with error 2 because the Nsi device
is absent. Enabling Windows services for a diagnostic run confirms the native
`nsiproxy` backend was not packaged. This is the next shared network gap to fix;
normal service suppression also retains reasoning from the obsolete seven-helper
limit. The new socket and late-library code remains uncommitted until the complete
exact-source gate passes.

The NSI backend is now built and packaged through the production PRX recipe,
with real FreeBSD interface names/indices from Sony's `getifaddrs`. Enabling
services exposes and opens Nsi. The next exception trace locates a null
`dnsapi` Unix-call table, rather than a routing-table fault. Patch 0965 reports
the unavailable backend as error 50 instead of STATUS_ACCESS_VIOLATION.
Normal payload launches now start Wine's service manager by default.

The adapter-only probe (DNS-server configuration checked separately) passes
53/53 on pinned Wine and PS5 in both architectures, including interface-ID
round trips. See [PE32](../evidence/network-adapters-default32-2026-10-08-01.json)
and [PE64](../evidence/network-adapters-default64-2026-10-08-01.json). The game launcher's next
run ends after 41.2 seconds with status 1 and clean owner return; its adapter
error is now 50 and Chromium reports unavailable DNS configuration. It also
reports persistent-memory corruption warnings; no cause is yet established.
A standard pinned c-ares DNS candidate builds outside production. The next
qualification is the title-only read-only `sceNetGetDnsInfo` / IPv6 counterpart,
then the shared DNS backend. No full gate or commit qualifies these new changes.


The title-only DNS configuration probe now completes: the native API reports one
configured IPv4 server, zero IPv6 servers and successful pool cleanup. Production
is restored and verified afterward. The addresses remain private. See
[the owner probe](../evidence/dns-info-owner-2026-10-08-02.json).
The shared DNSAPI backend now receives that configuration over the existing
packet-control transport and uses pinned, unmodified c-ares 1.34.8 for raw DNS
queries. Wine keeps its Windows record parser and native/WoW64 contracts.
A source-level sanitizer check passes 14/14 after correcting a signed bit-mask
shift; paired pinned Linux Wine probes each pass 29/29 configuration, adapter,
A/AAAA/TXT, TCP and NXDOMAIN checks. The corrected production build is in progress;
no console DNSAPI, the game launcher's acceptance or full gate claim follows from host results.
See [the host comparison](../evidence/dnsapi-host-backend-2026-10-08-01.json).

The corrected production build is deployed. Both PS5 DNSAPI probes pass 29/29,
including complete `GetAdaptersAddresses` with DNS configuration, real A/AAAA/TXT
records, TCP DNS and NXDOMAIN. PE32 takes 8.7 seconds and PE64 takes 8.2 seconds;
both report initial status 0 and clean return without an exit stall. See
the paired qualification (2026-10-09; record removed from the tree; in git history).
The follow-up game launcher's run and complete 33-case exact-source gate remain pending.


The game launcher DNSAPI retest was interrupted by a user-reported completely black,
unresponsive console. Before the interruption, the shell's actual system RSS
reached 997.1 MiB of its reported 1,050.3 MiB capacity. The memory guard then
requested Windows termination with status 0 at a census of 1,090 MiB, including
955 MiB across 15 helpers; the largest held 540 MiB. No later helper cleanup or
owner return was captured. FTP, control and klog all refuse connection afterward;
the local test service is stopped and the drop guard records the interruption.
A full reboot is required before another launch; the user is rebooting it.
This is not a confirmed kernel panic or a proven DNS-backend shutdown defect.

The same trace still has 8,227 MiB free of the 11,712 MiB application direct pool,
and 963 MiB free in the title's flexible pool. The investigation must distinguish
remaining shared/file/native allocations in system memory from ordinary Windows
private allocations already backed by application direct memory. Do not disable
the guard, raise its threshold, run another long game launcher's session or claim clean
teardown. Recover the persisted child logs after a confirmed boot, identify the
540 MiB helper and its allocation class, then qualify a shared correction with
bounded probes. One capture remains on the console and was not retrieved before
the interruption. See the interruption record (2026-10-09; record removed from the tree; in git history).
The current DNS/NSI/socket/late-library code remains uncommitted; the complete
33-case exact-source gate has not run. The game launcher's login, library, game launch and Big
Picture remain unproved.


Persisted logs recovered after the interruption (read-only FTP and a bounded
klog backlog; no launch or drop-guard reset). All 21 named helper logs and the
broker record survived in the title folder. The largest helper, native PID 1517,
is the launcher's CEF browser helper running DXVK: its last direct-memory account holds
464 MiB with zero allocator failures. Application backing is working; the
remaining system-memory allocation class is still unmeasured. Do not infer it
from the private allocation totals or mismatched-time resident samples.

The broker records `wine_gpu_service_still_live` during close and marks itself
wedged. This means the software could not prove a helper absent, not a confirmed
hardware GPU fault. It later records display close and broker stop; this does
not establish clean helper teardown. The close routine continues after the
failed absence check, and its absent/released labels are unconditional even
when wedged suppresses actual buffer release. Termination enqueue status zero
also proves only delivery to the server channel. Several helper records continue
well beyond the owner capture. These facts narrow the lifecycle failure, without
proving the exact point that made the whole console unresponsive.

The latest game launcher's session has no CEF DNS-configuration, adapter or persistent
shared-memory corruption warnings; HTTP connectivity, the server directory and
a UDP connection complete. Winsock lookup error 8 and socket error 10045 remain.
The game launcher creates its sign-in window, but the recovered capture at 20 seconds is
black, before pressure-triggered termination: visible presentation is a separate
failed check. No login or game-launch success and no recovered kernel-panic
record. Before another game launcher's run, qualify system allocation accounting,
process-wide cancellation/native absence/GPU drain ordering, and visible
compositor selection with small bounded probes. See
the review (2026-10-09; record removed from the tree; in git history).


Focused memory/teardown preparation (2026-10-08): the new asset-free
`memory-class-probe.exe` touches 32 MiB at a time across private, heap,
pagefile, small/large shared-file and copy-on-write views, with held/released
phase markers for independent census measurements. Pinned Linux Wine passes
40/40 checks in 157.72 seconds. Its build has neither X11 nor Wayland and
cannot render the graphics oracle; that failed attempt remains recorded.
The supplemental installed Wine 11.18 check passes a four-frame/readback
rendering child through normal exit, then another through `TerminateProcess`,
including a warm handle-balance check. The first GUI cycle increases the parent total by two handles; the original
cold-total assertion failed and remains in evidence. Balance is enforced on
the subsequent termination cycle.
This reference is explicitly not the pinned Wine build. See
[the pinned host run](../evidence/memory-teardown-host-2026-10-08-01.json) and
[the desktop run](../evidence/graphics-teardown-host-ui-2026-10-08-02.json).

Helper census instrumentation now records monotonic elapsed milliseconds and
raw resident pages/page bytes. The legacy seconds label counts poll-loop
iterations, so prior records with 1650 cannot prove that many elapsed seconds.
The recovered broker live-helper failure still stands independently. The
console runner rejects a wedged broker even after a logical exit/display
close; its verdict regression check and the three affected portable memory
sanitizer tests pass. No functional allocator or process-teardown correction
is claimed. Both focused cases join the now 35-case commit gate.

The corrected probe production build is a local persistent build job. Console
control reports idle, but the drop guard remains active pending explicit
confirmation of the full reboot. No probe or the game launcher's launch has been made in
this preparation. After that confirmation and exact-build deployment, run
`tools/run-battery.py --name memory-teardown-2026-10-08-01 --only memory-classes graphics-teardown`,
compare class deltas and native absence/broker safety, then qualify owner-wide
cancellation before another game launcher's run. No full gate, local code commit or push.

The corrected full production build is complete and staged; exact-source
fingerprint `3562f355e6f58d05d9ef4ea581b6d7a38a3a93d13d8a40d561357dd892110641`. The focused console cases remain
unrun, with the recorded drop guard unchanged. See
the preparation record (2026-10-09; record removed from the tree; in git history).


Focused console qualification after the human confirmed recovery (2026-10-08):
exact build `3562f355e6f58d05d9ef4ea581b6d7a38a3a93d13d8a40d561357dd892110641`
passes both memory classes and graphics teardown. The memory program passes all
40 checks. Private, heap and copy-on-write phases raise application direct memory
by 32 MiB without raising helper FMEM. Pagefile and small/large shared-file
phases raise resident FMEM by exactly 2,048 native 16 KiB pages (32 MiB), then
return to the 898-page baseline on unmap. These measurements identify a remaining
system-pool allocation class, not the full game launcher peak or a capacity fix.
Both rendering children submit four frames and twelve submissions; normal exit
and Windows TerminateProcess each report native close status zero. Warm parent
handles stay 54/54. The broker is not wedged, the owner returns, and a later
native census finds zero websrv-owned payload helpers. See
the review (2026-10-09; record removed from the tree; in git history).

An additional owner-close attempt shows a colored triangle and clean normal
shutdown, but does not reach wineserver's whole-session cancellation. Initial
helper environment filtering omitted the requested probe knobs; the renderer
finished its default 180-frame workload first. This run is explicitly inconclusive
for cancellation, despite the ordinary runner PASS. The extended asset-free
probe now holds the existing four-frame, 640x360 child device and its parent alive
for the owner's bounded server stop. That source change requires its own exact
build. The game launcher remains paused; no allocator correction, full gate or code commit.


The explicit live-device owner-cancellation probe is now built and deployed as
exact source `fb5c27c906b117cde889a8d6b5beb046b0f9692b24cefc3bc945fb329ae96e3c`.
Its parent and 640x360 child both report ready after four rendered/readback
frames. The normal stop cannot end the waiting programs; after ten seconds the
owner enqueues whole-session cancellation, and server logs confirm processing.
Parent native PID 169 and rendering PID 177 both close with status zero; the
broker records four frames/twelve submissions, no wedge and completed stop.
The owner returns and a read-only native census finds no websrv-owned helpers.
The ordinary runner remains FAIL with intentional initial_status=1; the
separate expected-cancellation qualification passes without rewriting that
failure. See the qualification (2026-10-09; record removed from the tree; in git history).

These results qualify one rendering child's normal exit, TerminateProcess and
whole-session cancellation under low system pressure. They do not reproduce or
clear the high-pressure game launcher freeze. Next investigate backing of shared
pagefile/file views while preserving real cross-process aliasing and file
coherence; native-library/stacks accounting also remains relevant. Do not raise
or disable the memory guard, or infer the largest game launcher's helper's allocation
composition from a 32 MiB single-class test. The game launcher is not launched in this round.
No functional memory/teardown fix, full gate, code commit or publication.


Full-HD desktop qualification (2026-10-08): production now presents the whole
1920x1080 Wine desktop by default (`WINE_PS5_VIEW=desktop`), preserving each
application's chosen window size. The prior window crop stretched small app
windows across the display. Graphics process probes also request 1920x1080.
Exact build `b375e478c8ba921837b7ebe1054ca4aa753b077b6d333bd6014cc833da7178c0`
is built and deployed. The normal/TerminateProcess graphics-child test passes
with four frames/twelve submissions per child and clean owner restart. The
repeated desktop overlap test passes all four displayed-pixel checks, normal
program exit and launcher return. See
[desktop display](../evidence/desktop1080-2026-10-08-02.json) and
[full-HD graphics teardown](../evidence/desktop1080-2026-10-08-01-graphics-teardown.json).
The first desktop attempt remains FAIL: the person closed Proton through the
PS menu and loaded onionHEN mid-test. Its pixel checks passed, but normal
completion is unqualified and kernel DMEM leak warnings occurred during that
closure. This does not qualify arbitrary PS-menu closure as leak-free.

An 8 MiB native physical shared-object candidate (`physhm_open`, firmware
10.01 AudioOut's four-argument create/size/map contract) passes cross-process
writes, two-alias coherence, namespace unlink and survival after the last fd
closes, followed by unmap in both payloads. Resident FMEM remains 214 native
pages before, during and after each role. No application ran in that test;
its direct-pool query returns zero, so it does not establish the physical pool
or a capacity improvement. See
[the candidate](../evidence/physical-shared-2026-10-08-04.json).
Earlier scratch runs preserve failures: incorrect argv[0] role parsing, then
unsupported UNIX-socket binding under /data (errno 45). Production's existing
/system_tmp rendezvous resolves that transport issue. No Wine backing change,
the game launcher rerun, full gate, code commit or publication has occurred in this round.


The physical shared candidate also passes beside a bounded Proton diagnostic
owner. A 20-second hold reduces payload-visible available flexible memory by
exactly 8,388,608 bytes, returning it after both aliases unmap. Each role's FMEM
remains 214 pages; the owner's application-direct pool stays unchanged. Its
ordinary direct-availability query is denied (0x8002000c), so do not claim these
pages use ForApp or that a larger capacity is proven. The person loaded onionHEN
before this test; that environment is recorded rather than assuming kstuff-only
results. The owner later exits naturally and returns cleanly. See
[held native backing](../evidence/physical-shared-owner-native-2026-10-08-02.json)
and [owner lifecycle](../evidence/physical-shared-owner-2026-10-08-02.json).
Next qualify zeroing, grow/shrink, byte I/O, page protections and transferred fds
before changing shared Wine allocation. The person has requested Windows 11
Wine theming from the local FluentLight-Wine reference next; memory improvement
and the game launcher remain outstanding. No further game launcher launch has occurred.

Windows-style theme and open-font work (2026-10-08): `themes/windows11/`
contains the pinned editable Fluent Light resources. The user requested a close
open font instead of proprietary Segoe: **Proton UI** is built from five pinned
OFL Selawik weights, with distinct names and 815 independently chosen
Latin/accented kerning pairs per face. No Segoe binary is a build input.
The shared prefix bundle now installs these fonts and updates the managed
standard light theme while preserving user-selected alternatives. The original
local wallpaper is staged privately at 1920x1080, never vendored. Patch 0966
paints it beneath opaque retained windows when no Explorer paints the desktop;
this is a startup default, not dynamic Windows wallpaper API support.

Pinned host Wine passes 36 font discovery/weight/glyph/kerning/raster checks in
each architecture. Both host architectures load the theme through standard
UxTheme APIs, resolve Proton UI at -12 pixels and draw a themed button. The
builder independently validates 484 resources and 478 bitmap references, and
repeated builds produce the same theme hash. See
[host fonts](../evidence/proton-ui-host-2026-10-08-02.json) and
[theme/package checks](../evidence/windows11-theme-host-2026-10-08-01.json).

Initial console batch: both architectures report 36 checks, zero failures and
clean owner restart, but the runner remains FAIL because the probe omitted its
required PASS/FAIL token. Those failures are preserved. The console desktop
case passes all four pixel comparisons, including the supplied wallpaper behind
retained windows and after window closure; see
[display evidence](../evidence/proton-ui-console-2026-10-08-01-desktop-overlap.json).
The marker is corrected and the repeated font batch runs on exact production
build `224d733fad990271fea4d33c21a9f7b1285b82752c8b9c6a19f3a61f23fa1b32`.
No native Windows 11 pixel oracle, full regression gate, code commit or push.
The theme retains Wine's classic control implementation; exact Windows 11,
Segoe UI Variable optical masters, Mica and rounded outer windows are unqualified.
Shared-memory qualification and the game launcher remain outstanding, with no game launcher launch.

The corrected repeated console font batch passes PE32 and PE64, 36 checks each,
with normal program exit, clean owner restart and no broker wedge. The font
files are identical to the host-qualified five OFL outputs. See
console qualification (2026-10-09; record removed from the tree; in git history).
The first missing-marker failures stay unchanged. Both required font cases
are now in the commit battery (37 cases); no full gate or code commit was made.
The rebuilt theme and local wallpaper are deployed. Next return to physical
shared-object zeroing, resize, byte-I/O, transferred-fd and protection checks
before replacing Wine shared backing or launching the game launcher.

The native descriptor-contract probe establishes a boundary, not a drop-in
file replacement: all bytes start zero, shared aliases/protection transitions,
msync and growth pass. Byte I/O/seek, fsync, flock, private maps and shrinking
are rejected. Linux POSIX shm passes 37 checks; native completes 27 checks with
nine failures and releases its full 2 MiB. See
descriptor contract (2026-10-09; record removed from the tree; in git history).
Wine's anonymous sections can later be mapped copy-on-write through ntdll's
pread fallback, so they must retain ordinary backing. Image shared-section
construction also requires pwrite and stays on its existing file path.

The independent two-payload SCM_RIGHTS check passes 19 creator and 18 reader
checks against the same Linux source. The unlinked object transfers in both
directions over SOCK_SEQPACKET, maps at an owned fixed reservation with a
nonzero offset, preserves cross-process writes and survives the last fd closing.
The last unmap returns all 2 MiB; FMEM stays at 219 pages in each role. The first
attempt failed because overlapping elfldr uploads rejected the reader (HTTP
400); backing was released and control remained idle. Serialized uploads pass;
see transfer (2026-10-09; record removed from the tree; in git history).

Pending production qualification: explicit memory-only requests now select
physical backing for input/audio pages, frame transport, census and broker
host-visible buffers. Wine sections still use the original allocator; physical
refusal retains ordinary backing. Only native payloads import physhm; PRXs use
an allocator callback. Sanitized allocator/protocol/file-lease/compositor host
tests pass. The production build and focused graphics/memory/teardown checks
are next. No game launcher run, complete gate, code commit or capacity claim yet.

The expanded sanitized host suite passes all 35 tests, including the helper's
physical-selection branch with a POSIX provider and the actual frame callback.
It exposed a separate global memory-copy deadlock: a 16 KiB write preceded the
read on an 8 KiB host pipe. The owned writer is now nonblocking; partial capacity
writes are drained and continued, while inaccessible boundaries still report
the completed prefix. The permanent regression forces a 4 KiB pipe and passes
full copying, unreadable source and read-only destination checks. An unchanged
signal-timing assertion failed once, then passed independently and in the full
repeat; that failure is retained rather than treated as an allocator regression.
See host qualification (2026-10-09; record removed from the tree; in git history).
The first production build predates the copy fix and was not deployed; a second
exact production build completes on fingerprint
`8610beac1e5514713bbb820c608d63eb7b4aa6adf1416d525e0201635b6c1476`.
Deployment verifies 60 runtime files and uploads three title files. The first
focused run is refused before the title launches: the runner finds a stopped
websrv child left at the first failed dual-upload timestamp. Its precise
identity is unproven because that rejected upload recorded no PID marker;
neither kill nor resume is attempted. A fresh boot is requested. The guard is
not reset, the game launcher remains paused, and this build's production graphics/lifecycle
behavior is still unqualified. See
the refusal (2026-10-09; record removed from the tree; in git history).

The transfer runner now rejects leftover payloads and waits for the creator's
streamed `listen PASS` marker before uploading the peer, replacing the earlier
one-second delay. That tooling change postdates the deployed fingerprint and
remains console-unqualified. After the confirmed fresh boot, rebuild the exact
tree, repeat transferred-fd readiness/lifetime qualification, then run memory
classes, graphics-child teardown and desktop pixels. Pass the complete gate
before a local code commit; no game launcher retest until backing and teardown qualify.


After a confirmed fresh boot, the readiness-driven transfer runner passes all
37 native checks (19 creator, 18 reader), matches the Linux oracle and returns
the full 2 MiB after the last mapping is released. Both roles remain at 219
resident FMEM pages; control is idle afterward. See
readiness transfer (2026-10-09; record removed from the tree; in git history).
The third exact production build completes on fingerprint
`c1b5e6d22b420ea1467519ffa646196dcd9e738cc27ae828c1208c131f0ec3f4`.
Its deployment and focused memory/graphics/desktop qualification are running.
This native transfer result does not qualify GPU grants, Wine transports at
scale or the complete gate. The game launcher remains paused; no code commit or publication.


Build c1b5e6d2 passes the focused production checks: memory classes report
40 checks, both graphics children render four frames/twelve submissions and
finish normal/TerminateProcess teardown without a broker wedge, and desktop
occlusion/wallpaper passes all four displayed-pixel comparisons. Each run
returns cleanly to the launcher. See
focused review (2026-10-09; record removed from the tree; in git history).
The saved records omit native stderr, so they do not identify physical
selection versus the permitted ordinary fallback. Native stderr now shares
the payload's saved stage log; an exact rebuild and repeat must capture the
allocator marker before claiming that backend passed. No game launcher launch,
complete gate, code commit or publication.


Exact diagnostic build
`cc8fb4a2dab4fe5a34560e6a8a4f9fb73b70e990b81008ef7ad5b08dfb179710`
is deployed. Saved native stderr confirms physical backing active in the
production memory helper and Windows payloads, with no memory-helper
allocation fallback reported. Repeated graphics teardown passes both children
(four frames/twelve submissions each), normal/TerminateProcess closure and
owner restart; desktop pixels pass all four checks. See
diagnostic repeat (2026-10-09; record removed from the tree; in git history).
The complete 37-case gate is now running against this exact source. It must
finish successfully before a code commit. High-pressure capacity and arbitrary
PS-menu closure remain unqualified; the game launcher is not launched in this batch.


The full physical-backing gate did not pass. PE32/PE64 font checks, all
40 memory-class checks and graphics teardown passed; DNS32 reported nine
wire-query/record/NXDOMAIN failures (status 9002). Before DNS64 loaded, klog
closed and control stopped answering. The user reprioritized battery speed;
the host batch was stopped after its active runner ended, without signalling
console processes. See
interruption review (2026-10-09; record removed from the tree; in git history).
The user confirms a reboot but has not restored payload services yet. The
outage guard stays active until safe access and fresh-boot state are verified.
Owned one-shot hooks from the interrupted run may remain: remove them while
idle before a manual launch or further test. No game launcher run or code commit.

Battery latency is now the immediate priority. Production console/launcher
runners replace fixed one-minute pacing with verified readiness: idle before
and after the unchanged native helper census. A prior normal completion and
successful hook removal permit immediate checking; unknown/failed cleanup
retains the historical eight-second display settle. The outage refusal is
unconditional, and older diagnostic tools keep their conservative pacing.
Input scripts now anchor to the first presented frame and preserve their
movement/click actions. All 37 gate cases and all case commands match the
previous exact build. Each case remains a separate launch; no lifecycle,
fresh-prefix, graphics, input, audio or process checks are omitted.

Twelve readiness-policy checks and four battery orchestration checks pass
without any console connection. The sanitized pad parser also validates the
actual accelerated input scripts; existing Python conformance, child-probe,
Lapy capture and WoW64-input regressions pass. See
[host qualification](../evidence/battery-readiness-host-2026-10-08-01.json).
Per-case evidence now records readiness/run/runner-overhead timing. Creating
`.deps/<battery-name>.stop` stops between cases without interrupting the
active console runner; a partial gate cannot pass. An exact production build
is running locally. No measured console speedup or new gate qualification
is claimed yet: after services return, repeat fonts/input/graphics to measure
readiness and presentation, then run the complete unchanged gate. Diagnose
the preserved DNS failure if it repeats; do not drop that case to pass.


Numbered battery progress is implemented: each case prints `Test N/total`,
its name and PASS/FAIL with elapsed time. A subset reports its own selected
total; the final line reports completed/passed/failed counts. Five battery
orchestration checks now pass, including full 37-case progress, subset
progress and partial-stop totals; the twelve readiness checks still pass.
The progress change followed the first local readiness build, so a second
exact build is running. Console services remain unavailable by the user's
choice while tooling is prepared; no speed measurement, deployment, gate
pass or code commit is claimed.

The second readiness/progress production build finishes successfully. Its
fingerprint matches the current source exactly:
`20294822ed0713a979689d2808699772ec32e2c468fb5d44f2e278b7966cac5a`.
The title and 4,324 staged runtime files are ready for qualification after
console services return. Host progress checks pass; actual console timings
and the complete gate remain pending. No console contact, deployment or
code commit was made while the user kept payload services unloaded.

### Readiness battery measured; Mihawk desktop direction

After services returned, the native census confirmed the user's fresh reboot
and no leftover helpers. The exact readiness/progress build above was deployed
and all 37 gate cases completed in 1,308.6 seconds (21 min 49 s), excluding
5.7 seconds of deployment. Median readiness was 0.179 seconds. There were
33 passes and four failures; see
[console benchmark review](../evidence/battery-readiness-console-2026-10-09-01.json).
The user reports router-level internet blocking and requests setting network
failures aside for this review. TLS timed out; both socket probes failed local
IPv6 loopback binding, whose cause is not established by the WAN restriction.

The remaining desktop failure was a harness mapping mismatch: its script
pressed touchpad while the user's saved left-click button was L3. Pointer
movement reached the window (39 moves), but no clicks did. A focused repeat
using L3 passes with two moves, one press and one release, normal closure and
verified hook cleanup. Neither runtime nor saved preferences changed. The
desktop harness must derive clicks from the saved mapping before repeating
the gate. Raw failed results remain; this batch does not qualify a code commit.
The console is idle afterward, with no leftover websrv helpers or outage.

At the user's request, `mihawk-os` was created and checked out after the full
battery, carrying the current uncommitted work. The proposed direction is one
shared default prefix with optional additional prefixes, a C: drive containing
or exposing installed games, and a Windows-style desktop shell with real file
operations, taskbar and application launching. A persistent owner/wineserver/
broker session is required before a desktop can remain open across game exits;
the existing LoadExec-per-game lifecycle is not that session yet. No prefix
migration or desktop-shell implementation was made in this round.

### Active `mihawk-os` foundation (2026-10-09)

The shared prefix policy is implemented and host-tested: new catalog entries
use `mihawk`, explicit prefix IDs select another environment and existing
per-game folders remain untouched. Fresh-prefix tests explicitly select their
marked throwaway prefix. No saves, registry or game files were migrated.

The library now includes a Mihawk OS session. Its small PE64 starter enables
Wine Explorer on Windows' `Default` desktop and waits for normal logout. The
corrected console session passes 16 parent checks and six in each architecture
child: shell residency, shared file/environment access, window creation/painting,
exit codes and real File Explorer start/close. The shell remains across those
exits, then closes normally; root status 0, broker stopped, owner restarted and
hook cleanup verified in 39.1 seconds without watchdog intervention. See
[session result](../evidence/mihawk-session-2026-10-09-03.json).

The initial display qualification failed: the captures showed the shell over
foreground applications. The broker's idle rule admitted background repaints
despite a correct foreground PID. A pinned local foundation overlay now retains
the foreground frame and re-presents static GDI frames when focus changes. The
new 38th gate case verifies session behavior **and displayed pixels**, rejecting
the prior shell-only captures. Its exact-build repeat passes all three captures,
including the real File Explorer C: window, with normal logout and cleanup in
39.1 seconds. A second session, graphics teardown and input32/input64 also pass.
See [display qualification](../evidence/mihawk-display-2026-10-09-01.json) and
[focused regressions](../evidence/mihawk-focused-2026-10-09-01.json).

The overlap case passes its first three captures but fails the final capture.
That request was made before the front-window close and could consume a queued
repaint. The harness now captures after closure, with pointer motion to redraw
retained pixels and unchanged color expectations. The exact-build repeat passes all four overlap captures and another desktop
session: two cases pass in 75.5 seconds, with normal logout and cleanup; see
[repeat](../evidence/mihawk-final-focused-2026-10-09-01.json). Whole-window composition across processes, catalog shortcuts, explicit
migration and the Windows 11 taskbar remain later work; the current broker still
selects one process frame at a time.
No full gate pass, code commit or publication in this round. Native launcher
boot remains the default. See [Mihawk OS](MIHAWK_OS.md) for the implementation
contract and migration boundaries.

### Approved Mihawk OS appearance slice

The user supplied the approved light/dark design and fourteen original object
icons plus thirty-four MIT Fluent glyphs, pinned to Microsoft revision
`cd332ec1f90cda2fad16f86754f8da52f1e9ab30`. All input hashes match. Editable
SVGs, licences and provenance now live in `themes/windows11/icons`; the HTML,
private embedded wallpaper and preview font are not imported. One appearance
JSON owns palette/metrics at 1920×1080 and 96 logical DPI.

The first slice builds transparent icons at six sizes, maps object icons to
existing Wine shell resources via patch 0967, packages both theme variants,
and adds a real appearance selector using Wine's existing theme persistence
and notifications. Its desktop shortcut preserves a same-named user file.
Host package/negative pixel-verdict checks pass. Exact production and host
Wine builds/tests are running; actual light/dark console captures are next,
then the entire required gate before a local code commit. No appearance,
material, centered-taskbar or full composition acceptance claim is made yet.
See [implementation matrix](MIHAWK_OS.md#approved-appearance-implementation-2026-10-09).

The first appearance batch failed all three cases. The appearance executables
were built but omitted from the explicit staged-probe list, so neither program
started. The session starter's new static GUI imports initialized an implicit
desktop before `main`, and its configured Explorer exited immediately; the
session repeat had no displayed frames or persistent shell. Both failures are
retained in `mihawk-appearance-console-2026-10-09-01`. Staging now includes both
probes, and patch 0968 puts shortcut creation in Explorer after desktop startup;
the starter again imports only kernel32/advapi32. The corrected exact production
build is running. Focused sanitized controls, pad-script and prefix tests pass,
as do thirteen Python runner checks. The host Wine comparison cannot open a GUI
because the pinned host runtime was configured without X/Wayland; its failed
record remains and is not treated as visual proof.

The corrected appearance/API and display checks pass in both architectures:
1,001 checks each, both light/dark captures, normal exit and owner cleanup.
The unchanged PE32/PE64 socket probes also pass all 30 checks after patch 0970
limits implicit IPv6 scope selection to link-local addresses. The saved-control
launcher check passes without altering preferences, and the desktop-session
check passes again. See [focused results](../evidence/mihawk-qualified-focused-2026-10-09-01.json).

That batch's owned LAN TLS case failed before the handshake. Local pinned Wine
reproduced an absent ROOT store in the fresh prefix and then a missing registry
store commit before the chain engine opened its snapshot. The corrected local
fixture now receives HTTPS 200, reports trusted chain errors 0 and unrelated
root errors 0x20, and commits removal of the test root. Hostname verification
stays enabled; the certificate binds both IP and DNS SAN entries to the endpoint
because this pinned Wine policy matches only DNS names. This qualifies the
fixture locally, not console TLS or general IP-only certificate support. See
[host fixture verdict](../evidence/mihawk-tls-host-2026-10-09-04.json).

Actual appearance captures exposed another shared live-theme issue: INI font
point sizes were cached as positive LOGFONT pixel heights until registry reload.
Patch 0971 converts nonclient font points to negative logical heights before
setting metrics. The new paired checks require 12-pixel fonts at 96 DPI and
34-by-46 caption metrics. They await the exact-source console repeat and complete
40-case gate; no code commit is yet qualified.

The exact-source font repeat now passes all 1,009 checks in each architecture,
including twelve-logical-pixel caption/menu/message fonts and the 34-by-46
caption metrics. Both light/dark icon/pane captures pass and normal cleanup
is verified. The owned TLS case is still running; complete-gate qualification
is not yet established. All 41 sanitized host suites pass on the repeat; the
earlier scheduling-sensitive signal-deferral stress assertion is retained in
[host evidence](../evidence/mihawk-host-2026-10-09-08.json).

The latest owned TLS console attempt removes its root and owned test data but
fails four connection attempts with WinHTTP timeout 12002; both LAN servers
receive zero HTTP requests. Read-only host firewall inspection finds UFW
blocking the randomly selected ports. The runner now accepts explicit owned
endpoint ports (or PROTON_TLS_TRUSTED_PORT / PROTON_TLS_UNTRUSTED_PORT), so
validation can use unused ports already allowed by the existing policy. No
firewall rules are changed and no certificate checks are disabled. Private
fixture addresses are omitted from the probe's diagnostic output. A rebuild
and console TLS repeat are pending.

The allowed-port console repeat receives HTTPS 200 with strict validation,
trusted chain errors 0 and unknown-root errors 0x20, and removes its exact root.
No public internet is required. The final owned-prefix cleanup is still running
before the complete gate. Source/deployed fingerprint:
`dc021394024cd0186060e3c8e67d396e7ed9efd014691df7f705697e8bd2d36f`.

The complete [40-case gate](../evidence/mihawk-gate-2026-10-09-01.json) now
passes on exact deployed fingerprint
`dc021394024cd0186060e3c8e67d396e7ed9efd014691df7f705697e8bd2d36f`.
Both appearance architectures pass 1,009 checks and light/dark display captures;
the gate also passes native execution, memory/query/sharing, adapter/DNS/socket
checks, eight children and descendants, graphics teardown, fresh prefixes,
saved mouse input, persistent desktop and memory-guard cleanup. CPU/GPU fault
records and process-protocol errors total zero. The owned LAN TLS repeat passes
strict HTTPS/trust checks and removes its root, prefix and test entry. All 41
sanitized host suites pass; their earlier intermittent stress failure remains
recorded. This qualifies the checkpoint for a local code commit, not full
Windows 11 fidelity, a centered taskbar/Start or cross-process HWND composition.

### Next shared theme slice (uncommitted)

Patch 0972 routes ordinary caption buttons through the existing nonclient theme
callback, with defined Window parts and the classic fallback. It preserves the
callback layout for the existing PE32/PE64 crossing. The new paired
`theme-notify32` / `theme-notify64` probes keep a parent window alive while a
separate process changes appearance, and check notification delivery, current
theme metadata, system colours and newly opened caption theme data. They restore
the diagnostic prefix's previous selection. These are required cases in the
pending **42-case gate**, not yet qualified. The current known-good checkpoint
remains the 40-case `b00444a` build.

Source inspection shows uxtheme loads registry configuration into process-local
state at DLL initialization and GetCurrentThemeName returns that cache. The
console probes will establish whether another process's change remains stale
after WM_THEMECHANGED before choosing a shared refresh mechanism.

The cross-process baseline reproduces **3/11 failed checks per architecture**
on the qualified runtime: one notification arrives, but theme name, pane colour
and newly opened caption theme data stay light. Patch 0973 refreshes the receiver
through the USER hook and invalidates colour/brush caches before dispatch; it
does not republish metrics or theme selection. The expanded probe also checks
brush refresh and retained old theme-handle ownership. The obsolete build09 was
stopped before deployment; these changes remain unqualified until build10 and
the complete 42-case gate pass.

Build10 (`17c233ab2fd5ea7b…`) passes the focused four-case batch in **84.9 s**.
Both receiver probes pass **14/14 checks**, including refreshed colours/brushes
and a retained old theme handle. Appearance still passes its preceding
1,009-check pane/icon/metric scope, but inspection shows **classic captions**.
The new caption hook incorrectly passed a nonzero state to Wine's part-availability
query, which only accepts zero. The corrected hook is in build11.

A new diagnostic run against build10 rejects all six caption images:
**6/1,025 failed checks** with normal cleanup. It compares live nonclient pixels
with actual selected theme-part drawing and sends reference samples to the
independent display-capture verdict. Six host pixel-verdict tests pass, including
missing/mismatched caption negatives. Alternate themes now leave both managed
preset radios unchecked and select legible glyphs from their actual pane colour.
No caption fix or code commit is qualified by these focused runs; the full
**42-case** exact-source gate remains mandatory.

Build11 passes **1,025/1,025 API checks per architecture**, including full
1,320-pixel equality for each of six light/dark caption parts. Both receiver
probes again pass **14/14**. Its focused batch is **3/4**, because the PE32
light capture catches the transition: its pane is already dark while caption
samples are still light. The capture failure is retained; it does not qualify
the slice. Build12 replaces fixed capture timers with a bounded displayed-image
rendezvous and requires eight distinct foreground-glyph samples per part.
Static retained frames can answer capture requests without synthetic pointer
motion. Host verdict tests pass **7/7**, including repeated-log negatives;
runner completeness tests pass **5/5**. Console qualification remains pending.

Build12's bounded focused run completes **2/4 in 73.1 s** with normal
cleanup: the two receiver cases pass, while the appearance diagnostic cannot
read the Unix-only capture variable through the Windows environment API.
Its new foreground-colour filter also rejects the lighter light-mode minimize
and maximize glyphs despite full pixel equality. The corrected diagnostic uses
an explicit Windows-side capture path and measures central glyph contrast
against the theme's own button background. No expected capture pixels or full
nonclient equality are waived. Build13 and its exact-source gate are pending.

The diagnostic-only build13 executables pass all three light caption images,
including distinct lighter maximize/minimize glyph pixels, but their initial
Windows environment lacks the custom capture path as well. Both bounded runs
restore the theme and close normally (**0/2**, 35.6 s). The obsolete production
build13 is stopped before deployment. The probe now receives its capture path
through the existing explicit command-line contract; native Unix environment
forwarding into the initial PE process remains a separate compatibility
question. No initial-environment correctness is claimed by this diagnostic.

The explicit-path diagnostic now passes **2/2 in 41.4 s** on the unchanged
build12 runtime with individually verified replacement probe executables.
Both architectures pass **1,029/1,029 checks**, full equality for all six
caption parts, and independent light/dark pane/icon/foreground-glyph captures.
Both captures complete without scripted pointer motion; previous appearance
is restored and owner cleanup passes. This is diagnostic evidence, not an
exact production gate. Build14 is the next immutable production build.

The missing initial environment is explained by inspection of the payload
bootstrap's `inherited()` allowlist: it includes runtime switches but omits
arbitrary per-application variables. Windows descendants already carry their
ordinary Win32 environment; initial native catalog overrides need an explicit
bounded forwarding contract. This global follow-up must have paired console
checks rather than adding title-specific variable names to the allowlist.

Build14 completes the entire mandatory **42/42 gate in 1,382.5 s** on exact
fingerprint `2c0ced39a91045cd0d1c773e8e216e3b3bdb826b83d0a01b377bfcf528f3ba4d`.
All cases return cleanly; recorded fatal CPU/GPU faults and process-protocol
errors are zero. The final owned TLS case passes in **196.6 s**, the persistent
desktop in **41.8 s**, and memory-guard cleanup in **31.3 s**. This qualifies
the local shared-theme/caption increment. It does not qualify complete caption
interaction states, cross-process HWND composition or the full approved shell.

Current uncommitted follow-up (2026-10-09): the unchanged appearance pair
passes 2/2 in 41.6 seconds, confirming the early startup failure is intermittent.
Shared owner/helper terminal socket logs now name receive bytes/errno and
poll/peek flags instead of treating the unexplained loss as a successful exit.
Canonical native cursor publication, displayed pointer checks and optional
wallpaper fallback are integrated. All 47 sanitized host suites and 145 patch
replays pass; paired Linux Wine checks pass 13/13 with the asset present and
absent. Build04, focused console qualification and the complete gate are next.
Code remains uncommitted; the previous 49/50 gate does not authorize a commit.

## 2026-10-20: the base PS5 (firmware 13.40) and the Pro (10.01) on one tree

One tree passes the whole mandatory gate on both consoles, on exact fingerprint
`fd8dd9e63d7219188200de96d16b10951b8b745171e3681ad87ab00698723080`: **94/94 on the base PS5
(`base-gate-05`)** and **94/94 on the Pro (`pro-gate-01`)**. The gate has 94 cases in this tree (an earlier "98" was a miscount).
`base-gate-01` to `-04` are the failed gates on the way (17, 8, 20 and 86 of 94), kept at the same detail; their cause was the
memory guard, then two prefixes it had left half-initialised.

What each console proved, and nothing more:

- **errno 92 (a helper killed in the kernel's loader on firmware 12.00 and later):** proven only by runs on the base PS5. Its
  helpers load the Wine modules themselves from the staged `<name>.prx.elf` files (`native/pw_prx_elf_ps5.c`, the portable core
  `src/pw_prx_elf.c`), ntdll first and every module Wine opens after it, and a helper that does die in a load now leaves the
  module and the firmware in the title's notice (`src/pw_start_failure.c`). The klog of the base runs holds none of "Decrypt error
  in SELF block", "pager error" or SYSTEM_INTERNAL_DATA_ACCESS_ERROR. The Pro (10.01) keeps the kernel's loader: its helpers
  never reach this code, and its gate shows the Pro unchanged by it. Every errno 92 run before 2026-10-20 07:00 went to the second
  console of this checkout's tooling (firmware word 0x13590001, the PS5_Vulkan configuration), not to the base PS5: none of them
  proves the fix.
  *Correction, release review of 2026-10-20:* the Pro's helpers take this loader too: 517 of its helper logs in
  `release-v2-pro-01` show `pw_prx_elf: loaded`, since the version word the Pro reports to programs is 0x12090001. And
  0x13590001 is the base PS5's own word (all 558 of its helper logs in `release-v2-base-01`), so it does not show that the
  earlier runs went to another console. Those earlier runs are simply not counted.
- **`/data` on 13.40:** the access daemon on payload SDK v0.42 (commit e6c9497, daemon sha256 `5651f2ad...bd7f`), granted on the
  base PS5 on every launch of the gates.
- **USB keyboard on every index:** proven on the Pro only (3 of 3 replugs, by the owner); on 13.40 only the gate's non-replug
  cases cover it.
- **The memory guard (`src/pw_memory_guard.h`):** its 870/940 MiB marks are the Pro's. The base PS5's census reads 731 to
  1223 MiB with no Wine process (the same 87 processes: no first reading can scale from it), a Wine start is ten helpers holding
  270-430 MiB, and the console took 1447 MiB over a whole gate with swap-out at 13.4 to 19.5 MiB. The limit is now the firmware
  generation's: 1600 MiB from 12.00 on, 870 below; a limit a run sets itself is held to that firmware's. Marks scaled from the first
  census (+200, +300, +400, half the difference) each ended the helpers of every launch.
  *Correction, release review of 2026-10-20:* the limit is keyed on the version word programs see, so the Pro (word
  0x12090001) also ran with 1600/1670 MiB in `pro-gate-01` and `release-v2-pro-01`, not 870/940. Whether a Pro can stall
  before that limit is open; the fix is to key the limit on the installed firmware, then run the Pro again.
- **late-library (a fixed 0x811000000-byte reservation) on 13.40:** the kernel refuses it with SCE_KERNEL_ERROR_ENOMEM (the
  process's address-space budget: 31 GiB were reserved before it) while it holds no entry in the range and no Wine view overlaps.
  Wine told the program ERROR_INVALID_ADDRESS (anon_mmap_tryfixed turned every refusal into EEXIST); patch 1231 keeps ENOMEM
  (error 8) and the probe stops at the budget, as it would at the end of the free space. Passes on both consoles.
  *Correction, release review of 2026-10-20:* on this build the Pro stops at the same budget (6 spans, about 31 GiB),
  where alpha-1-era Pro gates reserved 17 spans (about 917 GiB). The cause is not established; the new module loader,
  which the Pro now uses, is the leading suspect. The changed probe counts a stop at the budget as a pass, so it no longer
  catches such a drop. Open.

Two lessons kept for the next agent. (1) The shared tooling's console is PS5_Vulkan's configured one, not this project's
`ORIGINAL_PS5_HOST`: set PS5_HOST for the base PS5 and point `.deps/console-guard.json` and `.deps/last-console-run` at
`.deps/consoles/original/`. (2) A wineboot that is ended in the middle (here, by the memory guard) leaves a prefix with a
`.update-timestamp` and an incomplete registry (no `.lnk` class, no mmdevapi or mfplat classes) that nothing repairs: the
base PS5's `probe` and `mihawk` prefixes were moved aside (`probe-stale-20261020`, `mihawk-partial-20261020`) and made again.
A prefix that is half made should be detected and rebuilt by the title (open).

## 2026-10-20: alpha-3 — firmware-keyed decisions, the address-space regression, the search regression

Written plainly, so the next agent does not rebuild the history from the log. Measurements are in `evidence/` and the helper
logs of the named runs; the alpha-3 build has had no gate of its own yet (the combined gates run from the integration branch).

**1. Decisions that depend on the console's firmware now read the installed firmware.** The version word programs see
(`sceKernelGetSystemSwVersion`) is spoofed by the enabler: 0x12090001 on the Pro (installed 10.01), 0x13590001 on the base PS5
(installed 13.40). Keying on it made the Pro take the base PS5's memory-guard limit (1600/1670 MiB, not 870/940) and the
user-space module loader in alpha-2, which the earlier text above says. `src/pw_firmware.[ch]` + `native/pw_firmware_ps5.c`
read `sysctl kern.sdk_version` (the installed firmware, BCD) and fall back to the reported word. The memory guard's default limit
(`pw_memory_guard_default_limit_mib`), the helpers' module loader (`prx_loader_wanted`: `pw_prx_elf_ps5_needed` on the
installed firmware; `WINE_PS5_PRX_LOADER=1|0` overrides it for an A/B), the launcher's firmware line and the late-library probe's floor
all take it. *Superseded 2026-10-27 for the module loader:* helpers use the user-space loader on every firmware (11.00
stopped in the kernel's too; see the section of that date at the top). Verified on the console: Pro "firmware 10.01 installed (the system reports 12.09), limit 870 MiB, kernel loader";
base PS5 "13.40 installed (13.59), limit 1600 MiB, user-space loader".

**2. The address-space regression of alpha-2 (introduced in e15960a, shipped in alpha-2, found 2026-10-20, fixed in alpha-3).**
alpha-1 reserved 17 spans (about 917 GiB) in `late-library-probe` on the Pro; alpha-2 reserved 6 (about 31 GiB) and the probe
still passed, because it counted any stop as the end of the free space. Cause: the kernel keeps JIT-arena holds that are not
Wine views, not in its entry list, and that refuse `sceKernelReserveVirtualRange` with ENOMEM — at 32 GiB (0x800000000) and each
2 GiB above, up to about 230 MiB each. The user-space loader ran at those addresses on the Pro (item 1), where alpha-1's kernel
loader had put its own views over them. Not the budget, RLIMIT_DATA (32 GiB on both consoles; vmem and as unlimited) or the
per-mapping size. Fix, patch 1231 (`...find-the-holds-the-kernel-keeps-in-a-refused-reservation`): after a refused fixed
reservation of 1 GiB or more, scan the range in 256 MiB windows (refused windows in 64 KiB steps), claim each hold as a native
`VPROT_SYSTEM` view that skips Wine's own views and reserved areas, retry once. About 40 ms the first time a process asks for 34 GB,
microseconds after. Measured: 917 GiB on both consoles (16 spans in the development runs, 17 in the gates' klogs), three holds claimed in 7ef000000-1000000000 on the base PS5.
The probe now records the reserved total and fails below a per-console floor (900 GiB for 10.01 and 13.40); a console with no
floor line fails and says so.

**3. The search regression of alpha-2 (patch 1231's first version, e15960a; found 2026-10-20; fixed in alpha-3).** That version
returned the kernel's ENOMEM from `anon_mmap_tryfixed`. Wine's `try_map_free_area` goes on to the next window only on EEXIST and
aborts the search on any other answer ("err:virtual:try_map_free_area mmap() error Cannot allocate memory"), so every search that
met a hold failed "out of memory". libcef's 1 TiB sandbox reservation is one: MihawkBrowser failed to start identically on the Pro
and the base PS5, and this is very likely the failure the owner saw on the standard PS5. `anon_mmap_tryfixed` answers EEXIST for
every refusal again. **A permanent check:** `launch_outcome()` in `tools/run-battery.py` counts that error line in each launch's
helper logs (`search_errors`), and a launch with any fails — in every gate case except the inner sessions of `detailed-logs` and `games-autodetect`,
whose `<run>-on-` and `<run>-session-` helper logs the glob does not reach yet (release review, 2026-10-20; to fix in the next
gated code commit). Whether MihawkBrowser starts on both
consoles after the fix is recorded in its own run, not here.

**Open:** a prefix left half-initialised by a wineboot that was ended midway has nothing that repairs it (title-side
detection and rebuild).

## Aurelia in the launcher: sign-in, library, and the Playden session screen (2026-10-22; the Pro; play still open)

The official Steam client's CEF was what made the console's memory guard end a
helper every few minutes and what blacked the windows; the headless client
[Drackrath/Aurelia](https://github.com/Drackrath/Aurelia) (GPL-3.0, no CEF, no
WebView) talks Steam's protocols and is driven here as a backend over its
`--json` lines. Decided with the owner: an **Aurelia** entry and page in the
launcher, in the launcher's 30 themes, never the word "Steam" as branding.

What is built:

- The pinned client binary (`tools/fetch-aurelia.sh`, v0.1.40, SHA-256) runs
  under the mihawk prefix; the launcher's catalog entry is a `game.json`
  (`arguments: login --qr --json`). The launcher's library lists only entries
  with a prefix of their own, so `/data/ps5proton/library-show-all` keeps this
  one visible while it shares the mihawk prefix (the owner's choice).
- The session captures the client's JSON (src/pw_aurelia.h): a payload helper's
  stdout lands in `WINE_PS5_STDOUT` and is buffered until it exits, so the title
  tails that file four times a second and once more after the helpers end, and
  accumulates a document across lines (a signed-in `list --json` is printed
  pretty, a login result spans four lines).
- The sign-in screen: the title's loading screen becomes "Aurelia" with the live
  QR (Nayuki's encoder, MIT) on a white card. An Aurelia session skips the GPU
  broker (its presenter stole VideoOut after 0.1 s, and a headless client needs
  no broker), both display buffers refresh together (the first version flickered),
  and the login result ends the session by itself with "Signed in as <account>"
  (the client keeps its daemon, so nothing else ended it).
- The page (examples/ps5proton, opened with R2): the snapshot the session wrote
  (`/data/ps5proton/aurelia-library.txt`) as a themed list with a detail panel,
  the account line, and Sign in / Refresh (restarts carrying `args=`, new) and
  Play (a catalog game by app id). A one-shot probe
  (`/data/ps5proton/aurelia-open`) and the page's marker
  (`/data/ps5proton/aurelia-page.txt`) let a run prove the page without a
  controller; the kit's scripted input learned r2/l2/touch/options for the same.

Proven on the Pro (firmware 10.01, 2026-10-22): the QR was scanned and
`{"account":"freshpotato99","logged_in":true}` came back; `Aurelia: the library
lists 59 games` and the snapshot holds all 59; the page opened and its marker
says `games=59, focus=0 of=59`. The client's public HTTPS works
(`example.com status=200`, one trusted root reached, an unknown one refused).

Registering what the client installs is `tools/register-aurelia-games.py`: it
reads the client's Steam library over FTP and writes a catalog entry per game
under its app id (name from the snapshot, the largest .exe as its executable),
idempotently. It ran on the Pro (2026-10-22) and registered the client's one
installed game, 1476970 "IdleOn - The Incremental MMO"; the page's Cross now
resolves it by app id. A working game's play run is still open: IdleOn dies on
the known D3D11 descriptor-heap global-buffer failure, and the 2D game's install did
not leave files to register (below).

The session screen now carries Playden's own visual language (2026-10-22, the
owner's request). Its theme values are copied exactly, not approximated, from
[leolobato/playden-ps5](https://github.com/leolobato/playden-ps5) (GPL-3.0),
`src/homebrew/view.cpp` at `054668246f1a967da55aeb4f79eab110246a4e78`
(`playden_theme()`): background `#0E0D0C`, surface `#16140F`, surface high
`#2A2623`, accent `#F0863A`, text `#F3EFE9`, muted `#A8A29E`, success
`#7BBF6A`, danger `#E05A4F`, radius 8, card radius 10, focus width 4 and gap 5;
the library page's 6-column 210x315 grid on 24 px gaps, its boxed tab and left
rail are its metrics too. They live in one `Palette` in
`native/proton_loading.cpp` (credited beside it), and the QR card stays white
because contrast is a hard requirement. No Playden code is copied; README.md
and NOTICE.md credit the values and say so.

The loading screen is the client's own states now: a centred mark and "Aurelia"
in the title font, a phase line, the last status the client reported, a Playden
spinner while the phase is indeterminate (or the determinate bar when an
install's percent is known), and the footer. The phases are the session's real
ones: "Starting Wine", "Signing in" (a challenge arrived, and the launch
arguments say login), "Code scanned" (`{"event":"qr_scanned"}`, a new event and
handler), "Signed in as <account>", "Listing your library" (a list session after
the client starts), "Installing <name> - N%" (progress; the parser's progress
branch was unreachable before 2026-10-22 and every line was logged as unknown),
and "Library ready". The library is the Playden page: the tab bar, the left rail
with real Installed/All/Not installed filters, the six-column poster grid with
the store capsules (`tools/fetch-aurelia-covers.py` fetches them on the PC and
uploads them under `/data/ps5proton/covers`; 59/59), the focused poster's bottom
overlay and ring, and the button legend. Up from the grid's first row reaches
the tab bar, left from its first column the rail; Home leaves the session to
the launcher, and the reference's other tabs say this session has only the
library.

The hand-off is the session's own restart: a static next request is passed to
`return_to_launcher`'s final `proton_restart` (the earlier wine-run.txt write was
overwritten), and the session clears its captured stdout before a hand-off
session starts (the first console run replayed the login from the stale file and
looped). Proven on the Pro (2026-10-22): a login run's QR was scanned; the log
shows "phase Signing in", "the code was scanned", "phase Code scanned", "Signed
in as freshpotato99", "phase Signed in as freshpotato99", "Wine: the session
hands over to aurelia", then the new session "previous output cleared", "phase
Listing your library" and "the library lists 59 games"
(`klog/aurelia-login-phases.log`). A list run showed "phase Starting Wine" ->
"phase Listing your library" -> "the library lists 59 games", and the owner
confirmed the Playden library page, the up/down/tab/rail navigation, the
filters, the legend and the covers on the TV ("All good"; `evidence/aurelia-library-nav.json`).
An install run showed "phase Installing <the game>: Rebirth - 14% ...
29%" and the owner confirmed the determinate bar
(`evidence/aurelia-install-progress.json`). The cover art is the store's own and
is not in this repository; README/NOTICE say so.

The install's completion is a failure to record. The long run downloaded the
client's full 2,058,088,168 bytes (progress 100%, 159 s of program time) and the
client answered `{"event":"result","status":"installed"}` (no `ok` field; the
result parser now treats "installed" as success and the screen says Installed
instead of "The command failed"). But neither `appmanifest_250900.acf` nor
`common/<the game's folder>` exists in the mihawk prefix (FTP LIST,
2026-10-22 20:13), and `tools/register-aurelia-games.py` still finds only
1476970 IdleOn, so there is no executable to register and no play run to make.
The download is real and fast (the client reported 8-35 MB/s); the failure is
after it, in the client's install/finalisation step, and is not yet diagnosed.
A working game rendering and being controllable therefore remains unproven
(IdleOn 1476970 still dies on the D3D11 descriptor-heap global-buffer failure).

**Correction (2026-10-24, docs/AURELIA_REVIEW.md):** the paragraph above, "the install left no files", compared two different prefixes. A bare-path
launch gets the prefix `probe` (`src/pw_wine_launch.c:20`) and the listing read `mihawk`; the client's install of the 2D game in `C:\users\<user>\Games\Aurelia` is
what the official client later launched (lib-isaac-2026-10-24-01 and -02). The install worked.

Open, and why: the home-screen exit after a session ends (the log says
`restart: to=launcher` but the shell appeared; the relaunched launcher died) is
still a separate, undiagnosed fix; the external klog recorder is unreliable, so
it needs the guard's boot klog or the payload's. The top tabs Downloads, Saves
and Settings are the reference's labels, not pages: Cross there says the
session has only the library; Home leaves and Library is the page. The
launcher's R2 page remains a debug entry. The commit gate could not run: 95 of
96 cases passed on an earlier build of this code and `launcher-tls` fails only
because this host's ufw drops the console's connects to the owned LAN fixture
(the console's public HTTPS passes); the owner directed committing with the
hook skipped.

## Steam runtime (SteamCMD) in the Aurelia session (2026-10-23; slice 1)

The Steam client cannot run on this homebrew: its CEF is mandatory, renders black
and its memory took the homebrew down. The legitimate headless replacement is
Valve's SteamCMD, installed at `C:\Games\SteamCMD` in the mihawk prefix (461 MiB
under Wine, no CEF; it was reaching the `Steam>` prompt on the console). The owner
decided the runtime is integrated into the Aurelia session with the Playden UI as
the only interface, never a console or a login file; GOG is out of scope.

Plan: `docs/aegis/plans/2026-10-23-steamcmd-runtime.md`, five slices. Slice 1 is
done: `src/pw_steamcmd.{h,c}` maps SteamCMD's console lines to
idle/update/signing-in/guard/signed-in/ready/failed (host test
`tests/test_pw_steamcmd.c`), and the title's `profile=steamcmd` session polls the
program output and drives the overlay. The Pro run of 2026-10-23
(`--profile steamcmd`, `evidence/steamcmd-slice1.json`, `klog/steamcmd-slice1.log`)
showed the "Steam runtime" phase and `SteamCMD: ready Steam Console Client ...`;
the wrapper starts/stops the program through the title's own session. Slices 2-5
remain: the setup page with account/password/Steam Guard and stdin, the game
prelaunch with `SteamAppId`, the library tile, and the closeout tests (the game's
`SteamInternal_SteamAPI_Init` accepting SteamCMD's client pipe is the open
question).

### Slice 2: the Steam runtime setup page (2026-10-23)

`src/pw_steamcmd_setup.{h,c}` holds the page's state machine: account, masked password and Steam Guard
fields, Tab/Enter/Backspace editing, the submit lines for SteamCMD's stdin, and the mapping of SteamCMD's
parsed states (signing in, Guard, refused, signed in). The Playden overlay draws it (`proton_aurelia_setup`),
the title feeds it the USB keyboard events and writes the login/Guard lines to SteamCMD's stdin through a
pipe (`SteamCMD: the console's input is a pipe`). The password is cleared after the attempt and never
stored; the SteamCMD's own machine token is the persisted credential. Host tests: 150 passed, including the
setup state machine.

Slice 2 is now console-proven (2026-10-23, 13:16 console time). The page was reached from the launcher's
**SteamCMD** tile, the account and password were typed on the TV, and SteamCMD's own
`logs/connection_log.txt` recorded `[Logging On] ... Using JWT ..., persistence: 1`, `Successfully
generated token via password authentication` and `[Logged On] ... RecvMsgClientLogOnResponse(): OK`; a
Steam Guard notification arrived on the owner's phone during the attempt, and the page ended on
"Steam runtime ready". The persisted token now lives in SteamCMD's `C:\Games\SteamCMD\config`.

What the console run forced into the design, each proven on the device:

- The setup page stayed up because the SteamCMD session is **headless** (the title skips the GPU broker and
  the graphics owner for `profile=steamcmd`) and refuses a second presenter while it owns the display.
- SteamCMD does not read the title's stdin pipe for its interactive login; the page writes
  `logs/steamcmd-login.txt` and the `steam-runtime.exe` wrapper starts SteamCMD with `+runscript`. That file
  is deleted at session start and removed after the run; the password never appears in a process command line.
- SteamCMD's console text is not the redirected stdout, so the title also parses `connection_log.txt` for the
  verdict (signed in / invalid password / Guard / rate limit) and shows it on the page.
- The keyboard map was producing plain digits for shifted digits (a shifted-symbol password silently
  became wrong); shifted symbols and numpad keys are mapped, the password is visible while typing, and the
  failed state retries on any key. The pad moves between fields; Cross is Enter, Circle is Back.

### Slice 3: the game prelaunch (in progress)

`tools/steam-runtime.c` is the prelaunch wrapper deployed at `C:\Games\SteamRuntime\steam-runtime.exe`.
It waits for a login file, starts SteamCMD with `+runscript` and `-nobuffer`, forwards SteamCMD's lines,
then starts the game with `SteamAppId` set (default 250900) and waits for it. The remaining slice-3 work is
the game's catalog entry through the wrapper, hiding SteamCMD's console in the game path, and recording the
game's Steam API result plus the frame/GPU log; the deciding question is still whether the game's
`SteamInternal_SteamAPI_Init` accepts SteamCMD's client pipe.

### The direct-memory investigation of 2026-10-23, corrected (2026-10-24)

An earlier entry here called a direct-memory "claim" for file views a measured negative: "counter 9 did not fall with the claim active". The
review of that work and the logs on disk say otherwise, and the entry is replaced:
- **The instrument could not show it.** The child's `file_private_mib` and "files read in" are one counter (`VM_STAT_FILE_READ`) that adds each
  `pread` into a view and never subtracts, so it cannot fall when a view is released or when memory moves; the two runs compared were also
  different processes. What the guard counts is each process's resident flexible memory (`sceKernelGetResidentFmemCount`).
- **Those bytes were never in the guard's pool.** A helper's file and image views are backed by the application's direct memory before the read
  (the helper adopts the ForApp allocator; every reserved area and fixed reservation is a direct-memory region), and direct memory is not counted.
  What the guard sees of an image is its converted code (JIT objects, patch 1127), which the registry already shares by content.
- **The claim does nothing:** a view lies inside a region registered when it was made, so `__wine_ps5_dmem_claim` merges into it and the commit
  after it is skipped. Claims 1-4 were all refused (an alignment bug the code's own comment records); only the fifth succeeded, and it changed nothing.
- **Sharing image text as file-keyed shared objects is not safe** (Wine writes relocations and import tables into a section after it is mapped, and
  those writes would reach every helper), and would move bytes from uncounted direct memory into counted JIT memory. It was not pursued.
- The memory guard was changed in the same worktree (end margin 70 -> 119 MiB, a 12 s "swap grace" on the steep rule, its tests rewritten): that
  loosened the guard against the owner's word and was reverted.

### Pro memory facts, measured (2026-10-24, the PS5 Pro, installed 10.01; records `bit22-try-2026-10-24-01`, `flex512-2026-10-24-01`)

One short run each with the title's own `param.json` changed and put back (one launch, restored and compared with the production file):
- **`attribute3` bit 22 (0x400000) is the Pro memory mode** (found by github.com/obl1vn/Unlock-ps5-pro-memory on 13.60; it works on 10.01
  too). 64 -> 4194368 (0x400040, our high-frame-rate bit kept): the direct pool 11712 -> 12928 MiB (+1216), the largest free block
  11277 -> 12246 MiB. `FMEM size` (0x40000000), the flexible 946 MiB free and the console's RSS limit (1038.7) did not change. The production
  title carries 4194368 from this date (the driver tests the 120 Hz bit by mask, so that flag still reads). A release for a standard PS5 would
  carry 64: the bit is untested there (it may be refused at launch).
- **`kernel.flexibleMemorySize` is a partition slider.** 1.5 GiB is refused (`max: 1073741824`, with or without bit 22). 512 MiB: `FMEM size`
  0x20000000, the direct pool 12928 -> 13440 MiB (+512, exactly what was given back), flexible 434 MiB free. A user's report on a 10.20 console
  (1 GiB -> 256 MiB freed 768 MiB, the title's own flexible use about 77 MiB) agrees. What is NOT known: whether the helpers (payload processes)
  draw on the title's entitlement. Their counted memory exceeded the 1 GiB entitlement in runs of a store client without failing, but no run was made under a
  smaller entitlement with helpers loaded; do not lower it without that run.
- **The console's printed RSS limit is not the title's entitlement.** With the entitlement at 512 MiB the line still read `RSS 141.8 1039.3`
  (the note above, "RSS x of 1025.8 is the title's 1 GiB flexible pool", is wrong). It stays near 1038-1039 whatever the title asks.
- **The guard's default is keyed by installed firmware** (`pw_memory_guard_default_limit_mib`: below 12.00 the 870 MiB limit, 12.00 and
  later the larger one), fitted to two consoles: this Pro on 10.01 (prints a limit, ~1039) and the standard PS5 on 13.40 (prints 0.0, ran past
  1700 with swap flat in the earlier records). A user reports that 1600 MiB "works perfectly fine" on a 10.20 console. If that console is a
  standard PS5, the rule gives it the Pro's limit: the key should be the console model (or its measured reclaim behaviour), not the firmware
  number. Needed from that user: the console model and the `VM Stats: RSS a b` line from klog (b = 0.0 means the base behaviour).

### Why testers prefer alpha-2: the guard's firmware key (2026-10-24)

alpha-3 changed where the guard's firmware number comes from
(`1263ccf`: installed firmware, `kern.sdk_version`, instead of `sceKernelGetSystemSwVersion`, which the jailbreak enabler spoofs: the Pro reports
12.09, the standard PS5 13.59). The threshold did not change (below 12.00 the 870 MiB limit, from 12.00 the larger one), so in alpha-2 every
enabled console ran with the larger limit and in alpha-3 every console whose INSTALLED firmware is below 12.00 gets the Pro's 870, a base PS5 on
10.x or 11.x included, where the 13.40 base console's measurements (kernel acting on physical pages, ran past 1700 with swap flat) and a user's
report (1600 "works perfectly fine" on a 10.20 console) say it is far too low. The 870 is right only for the Pro. Other alpha-3 changes that
cost memory: MihawkBrowser works again (alpha-2's address-space regression had broken it; about 350 MiB with its processes), and helpers on
consoles below 12.00 now load Wine's modules themselves (the Pro's path). Fix to make: key the default on the console model
(`sceKernelGetHwModelName`, reachable from a payload with `-lkernel_sys`; not yet read on a Pro or a base console), and give testers a setting.

### A launcher option that installs a store client, and sessions that follow their processes (2026-10-24, Pro 10.01)

The launcher has an option that downloads a store client from its vendor's own servers, installs it silently in the Windows prefix, and opens it
once for its sign-in; its steps, numbers and the open items are in docs/STEAM.md. Three engine facts it established, each measured on the console:
- A program that updates and restarts itself ends its first process after starting its second (twice on a fresh install). A session used to end
  with its root process; an entry that sets `PROTON_SESSION_FOLLOWS=processes` now ends when the server's user-process count (patch 2073, the count
  Wine's own shutdown waits on) has been 0 for 5 s. Off for every other entry; making it the default is a gate-run decision.
- The console's file system is case-sensitive and Windows is not: a name an installer wrote as `Client.exe` is found by Windows as `client.exe`.
  `src/pw_path_nocase.{h,c}` resolves paths ignoring case where the launcher checks a program.
- A new export of the server module must also be listed in `tools/build-wine-ps5.sh` (the wineserver descriptor), or the title's symbol lookup
  returns null without an error (the title now says so for this one).

### The guard is a last resort (2026-10-24, the owner's decision)

The default limit for firmware below 12.00 is 990 MiB with the end mark at 1010 (`PW_MEMORY_GUARD_LAST_RESORT_MIB`, `pw_memory_guard_default_end_mib`); 12.00 and
later keep 1800/1870, and PROTON_SYSTEM_MEMORY_LIMIT still lowers it. Why: the signed-in client window runs at a kernel RSS of about 725 of ~1039 MiB with
swap-out rising 64 to 113 MiB (steam-signin-raise-2026-10-24-02), and the 870 mark ended it before the client had saved its sign-in. What the October records
say about the early builds: no visible, signed-in Steam run was fine before the guard (10-05 an error box, 10-06/07 swap to 1115-2066 MiB and a stopped
console, 10-10/11 the browser's GPU process at 430-470 MiB and the census to 940-992 MiB); the early client was version 1788652215, today's fresh one
1792618285; another launcher's game played only with the guard raised to 1250/1300 (10-12). The 990 mark leaves 49 MiB to the kernel's limit, so a climb of 15-36 MiB/s
can still pass it before a decision lands: the steep rule stays armed and the freeze risk is the owner's accepted one. Not yet run on the console with this default.

### What Big Picture's start-up jump is not (2026-10-24, the Pro; kit-bp-*, kit-flex/direct/reserved-*, kit-nogpu-*, dsec-*)

The signed-in client's browser helper holds 225-241 MiB of kernel-counted memory until about 15 s, then 411, 524, 575, 613 MiB in 4 s (the start-up freeze:
the census to 880-920, swap-out 64 to 166-184). Measured with the class-budget census (`WINE_PS5_CENSUS_MS=F:D:R`, `WINE_PS5_EARLY=1`), each class once
with a budget that finished it: at 20 s, kernel 602-623 MiB; flexible-flag mappings **162 MiB resident** (6871 of 6871 mappings, 84 ms); direct **1081 MiB**
(7416 of 7416; already 1069-1088 at 13-15 s, so it does not move with the jump); reserved (PROT_NONE) **0 MiB** (7383 of 7383). So the +380 MiB the kernel
counts is in none of the three classes the walk lists. Not the GPU: with `--disable-gpu --disable-gpu-compositing` the helper still jumps at 11-12 s to 540 MiB,
the swap-out peak is lower (74) but the UI draws about 8 frames a second. Not sections: 2 sections of 25 MiB were 8 MiB or more
(`WINE_PS5_DSECTION_MIN_MIB`, default 256, changes nothing). Not libc: about 20 MiB. Next: a kernel-side map summary (kern.proc.vmmap, taken from the memory
helper with a direct-memory buffer) at the jump, as the plan for the measurement kit allowed once a residual over 60 MiB survived.

**The kernel's own map names the jump (kit-vmmap-2026-10-24-01, `WINE_PS5_VMMAP=1`, kern.proc.vmmap read inside the browser helper).** By region type
(FreeBSD's: 1 default, 3 swap-backed anonymous, 4 device): at 12 s the helper holds 104 MiB of type 3 and 446 MiB of type 1 at 13 s; at 15 s, after the jump (kernel count
534 to 605 MiB), type 3 holds **519 MiB resident** (6847 regions) and type 1 only 29. The largest regions are two anonymous ranges, `0x880420000-0x88f0a0000`
(221, 236, 233 MiB) and `0x88fd30000-0x899990000` (95 then 156 MiB), growing with the jump: about 390 MiB of the 600. **`sceKernelVirtualQuery` does not list them**: the largest
mappings of the walk (`vm big`) are 64, 32, 29 MiB at other addresses, which fits every class of the walk, measured whole, holding 162 MiB flexible and 0 reserved, and means the
walk's "libc about 20 MiB" (above) did not account for them. Owner: Sony's libc heap, filled by the GPU client (see "The owner" below), so the libc heap
experiment (idea 1) was the right place to look: an allocation log of the helper's malloc by size and caller at the jump is the next step.

**The owner of the start-up jump: the GPU client's platform heap could not get direct memory in a helper, so Mesa's whole heap went to Sony's libc (2026-10-24, Pro fw 10.01).**
How it was found, in order (each a run under `klog/` with its evidence JSON): `vq-` (libkernel's `sceKernelVirtualQuery` refuses the exact query for the two big
ranges and skips them in the next-mapping walk: why the census never listed them); `ms-` (`malloc_stats_fast` reads the libc heap: 13 to 282 to 388 MiB in use between
12 s and 14 s); `pr-` (`malloc` hands out new blocks inside the growing region); `mt22-` (a counter on every Wine module's libc allocation imports, output through
`stderr`, not `write(2)`, which reaches no helper log: **every earlier silence from `write(2)` logs is void**): `libvulkan-helper.prx` asked libc for 2.7 GiB in 6.4 million
calls, 394 MiB live at peak, all through five call sites in the platform heap's `__wrap_malloc`. Disassembling it (`llvm-objdump`, REA times out on the 8 MB module):
it calls `ps5_heap_malloc` first and tail-jumps into libc's `malloc` when that returns NULL, and it always did. The SDK heap (`PS5_PayloadSDK/platform/src/heap.c`) maps
direct memory with `sceKernelAllocateDirectMemory`; a payload must use `sceKernelAllocateDirectMemoryForApp` (native/proton_payload_child.c, `payload_allocate_direct`:
the plain call draws on the system partition, which has no budget in a payload), so the heap was refused every time.
Not the owner (each measured): window surfaces, the Wine `mmap` wrap (it is never linked into a helper; helpers map through the carrier's `sdk_mmap`, which saw
no mapping of 1 MiB or more), libfreetype and libgnutls (under 32 MiB asked), the carrier's own code, libc's mapping call (the heap reserves its regions at start).
Dead ends worth knowing: forcing the helper's user-space PRX loader on 10.01 (`WINE_PS5_PRX_LOADER=1`) crashes the title; `--wrap` of the allocation family in the
carrier crashes the helper at 6 s; redirecting the client's allocation to Wine's heap fixes Steam (resident 664 to 294 MiB) but the 2D game then needs 72 s to reach its intro,
because one mutex serves Mesa's allocation rate (`isaac-ch-2026-10-24-01` against `isaac-ctl-2026-10-24-01`); patching a Wine module's `malloc` import without checking its library token breaks
`winehid` and the desktop window: most Wine modules import `malloc` from ntdll's own heap, only the imports whose token names `libSceLibcInternal` reach Sony's.
**The fix, first** (`native/pw_malloc_trace_ps5.c` in the loader's per-module hook): the client's one `sceKernelAllocateDirectMemory` import replaced with the `ForApp`
call. Wine's heap area now tries `0x5000000000` before `0x2000000000` so the platform heap can reserve its range at its hint (`wine/ps5/pw_wine_dmem_ps5.c`;
`tests/test_crash_replays.c` follows the address). **The fix, where it belongs** (PS5_PayloadSDK, ours): `ps5p_allocate_direct` (`platform/src/placement.c`, commit
`c32d2be`) tries the plain call and then the weak `ForApp` one, for the heap, exec, shm and thread stacks; then `9d04878` makes an arena's growth step 2 MiB instead of
the dlmalloc default (direct memory is committed whole when mapped, so each of the eight arenas held a large first segment it never used: the 2D game's +80 MiB below).
The SDK pin is `9d04878` (`ps5/tools/setup-sdk.sh`, `docs/PROVENANCE.md`; build with `PS5_PAYLOAD_SDK_FORK` pointing at the fork); the import patch is removed.
Measured on the Pro, Big Picture, same console and day: the browser helper's resident memory across the old jump 588-664 to 247-278 MiB, libc heap 400 to 6 MiB in use,
peak system RSS 945-964 to 705-707 MiB (swap out unchanged at 82 MiB), frame windows of 110-118 fps with a longest gap of 2.7 s in the first window (the stall
totals were 51-52 s; the first of the two 24 s gaps is gone, a later 5-8 s one remains: not this cause). The 2D game from the library: 57-62 fps as before, peak system RSS
**754 against 672 MiB** (the client's platform heap holds more than libc's lazy heap did: about +20 MiB in each of three GPU-client processes), still under the
swap onset of 780-820. `fin1-2026-10-24-01` (Steam) and `fin2-2026-10-24-01` (the 2D game) are the runs of the final build with nothing set.
Diagnostics left in, all env-gated: `WINE_PS5_MALLOC_TRACE=1` (counts every libc allocation of the Wine modules by caller), `WINE_PS5_VMMAP=1` (kernel map by region type,
`malloc_stats_fast`, a malloc probe, a peek into the biggest regions), `WINE_PS5_SAMPLE=1` now reports every second across 10-20 s. No gate was run on any of it.
With the SDK fix and the import patch removed (the Pro, 2026-10-24): Big Picture peak system RSS 642 MiB, the 2D game 673 MiB at 60 fps (the +80 MiB is gone).

**A .NET game (Wine-Mono) crashed after a long load, then started in 2 s (2026-10-24, the Pro).** Two causes, each a fix for every program:
- *The crash*: "Access to the path ...\Content\Dialog\Fonts is denied". The console has no `*at()` calls; the in-process server opens a name relative to a directory
  handle with `fchdir(that descriptor)`, and `pw_wine_cwd` keeps one path per descriptor number in a table that stopped at 4096. The title's server held descriptor 8269
  with Steam and the game running, so every relative open above 4096 was refused (patch 2074's ERR line named the open; the `fchdir` wrap now says why it refuses).
  `PW_CWD_MAX_FDS` is 65536 (`wine/ps5/pw_wine_cwd.h`, a host check for descriptor 8269).
- *The slow start* (37 s to the first frame): Mono's hazard table is a writable 4 KiB page whose host page (16 KiB) holds three no-access pages. Patch 1122 maps a host
  page read-only when its pages disagree on write and steps each store; a no-access neighbour counted as a disagreement, so every hazard-pointer store faulted. Patch
  2075 leaves no-access pages out of that rule (they are not readable, so nothing is lost; read-only and guard neighbours still step). Start-up 37 s to 2.2 s; the
  owner played it and reported it "runs great"; the last session: about 61 fps, no crash, peak system RSS 678 MiB.
Gate cases added for both: `relative-dir32`/`relative-dir64` (4400 files held, then every subdirectory of `C:\windows` opened relative to the listing's handle) and
V7 in `vm-truth32`/`vm-truth64` (200,000 stores into a writable page beside only no-access pages take under 100 ms). The gate is now 98 cases.

**The OpenGL SDK moves to its mirror, 1.0.0 to 1.0.2 (2026-10-24; PS5_MihawkOS PR 29 by RikyPy, issue 28).** blackbearreloaded/ps5-opengl is gone (404), so a fresh
clone could not fetch the SDK. darkxex/ps5-opengl mirrors it but carries only 1.0.2 (SHA-256 `30d8e51d...`, matching its release note). 1.0.2 is a later release,
not a re-host: runtime source `dce7491` to `a519561`, the three static archives differ (EGL, the gallium screen, the AGC runtime; Compatibility-profile contexts
added), headers and `LICENSES/` unchanged, corresponding source included. The PR's five commits moved the URL, the pin, the download's file name and
the SDK path in `tools/build-wine-ps5.sh` and `tools/stage-licences.py`; the URL in `tools/fetch-opengl-sdk.sh` and `NOTICE.md`, `README.md`, `docs/PROVENANCE.md` were
finished here.

**Gate on the Pro with all of this (`gate-celeste-gl102-2026-10-24-01`, 1800 s): 97 of 98 passed**, the first gate with 1.0.2 in `win32u.prx` and with
`relative-dir32/64` and V7 (all four passed; the probe's `C:\windows` handle was 0x457c, after the 4400 held files, and its 20 subdirectories opened relative to it).
The one failure, `desktop-session`, was the check, not the build: going up from Documents shows the Desktop, which now lists the owner's 19 shortcuts (13 on
10-22), and its vertical scrollbar covered 141 compared pixels (x 1073-1089 of the window, over the column header too). `tools/pw_explorer_expected.py` now leaves that
column out for the Desktop; both captures (13 and 19 items) compare with 0 wrong. A second gate on the fixed check was stopped by the owner before it reached the
console, and the commits were made with `--no-verify` at the owner's word.

## Steam games start: where a 2D game's two minutes went (2026-10-25, the Pro, the owner asleep; goal: as close to a PC as possible)

`tools/start-timeline.py RUN` gives a run's start one line a second from its flight log (frames flipped, each Windows program's CPU), and the milestones. The 2D game from its
library entry (`steam.exe -silent -no-browser steam://run/250900`), `isaac-load-2026-10-25-01`: Steam's first CPU at 10 s, the game's process at 50 s, steady 60 fps at
120 s. Two parts, each measured, each fixed for every program:

- **Steam, 40 s** (its own logs, `klog/steam-logs-2026-10-25/`): 15 s "Verifying all executable checksums" on every start, then about 13 s to its sign-in and 5 s of
  launch tasks. `-noverifyfiles` skips the verification ("Verification skipped"; an update still replaces the files): on the 250900 entry (the original kept in
  `~/PS5-Backups/game-json-2026-10-25/`) and on the launcher's Big Picture entry (`src/pw_steam.c`).
- **The game's own load, 70 s, almost all of it file calls in the game's helper** (its loading thread 80% inside NtReadFile, NtQueryAttributesFile, NtCreateFile,
  NtClose; `isaac-load-sample-2026-10-25-01`): 370,000 reads (1.43 GB, all its archives, 4 KiB at a time), 47,000 attribute queries, 25,000 opens. Each was slow for
  its own reason, measured by `tests/file-cost-probe.c` (32- and 64-bit, in a helper):

| Cause | Before | Fix | After |
| --- | --- | --- | --- |
| A child got every file uncached (patch 0929), so each read was a get_handle_fd and a descriptor passed over the socket | ReadFile 4 KiB 36.8 us | 2076: a regular file is leased to the child (cached; pinned until the handle closes; 32 a process, 96 in all) | 6.8 us |
| A missing name read the whole directory (case-sensitive disk) | 340-360 us | 2077: a process keeps a directory's names (32 directories, checked by device, inode and modification time) | 78 us |
| Every open resolved its whole path with realpath(), a system call a component | open+read+close 158 us | 2080: the server keeps resolved directories (64) | 132 us |
| The game asks, in lower case, about each resource of each installed mod: every component looked up one at a time, with the file system's case asked each time | 180 us (probe), ~400 us (the game) | 2081: a process keeps the directories it resolved by their Windows path (1024), and the PS5 answers case-sensitive without asking | 64 us |
| The kernel does not read ahead for small reads: a 1.5 GB file read 4 KiB at a time 88 MB/s cold (both passes: it does not stay cached), 830 MB/s in 1 MiB reads (`readspeed-cold-2026-10-25-01`); FreeBSD's F_READAHEAD changed nothing (`readspeed-ahead-2026-10-25-01`, not kept) | the game's reads 13 s | 2082: a synchronous read of 64 KiB or less is served from a 512 KiB piece read at once (4 a process, by device and inode, checked with fstat each read; this process's writes forget them) | 350 MB/s cold (`readspeed-ra-2026-10-25-01`); the game's reads 4.8 s |

  Also found: a 32-bit program's server requests had no name (wow64_wine_server_call), the counters kept the stack's bytes as a label, and the game's helper crashed in
  the census's snprintf (`isaac-load-fix1-2026-10-25-01`): patch 2079. `WINE_PS5_FILE_TRACE=N` (patch 2078) logs a helper's first N opens and attribute queries with
  their names and microseconds; it is what named the game's mod lookups (`isaac-load-trace-2026-10-25-02`).
- **So far:** the game's process at 36 s and steady 60 fps at 66 s (`isaac-load-fix4-2026-10-25-01`), from 50 and 120: its own load 70 s to 30 s. What is left
  of it: reads 4.8 s, opens 3.1 s, attribute queries 7.4 s (156 us each: the reparse-point retry and the exact-name stat still cost system calls for every
  missing component), and about 15 s of the game's own work at two cores. Before the game: about 10 s of Wine's session start (wineboot, services) and 26 s of Steam
  (its UI and sign-in about 15 s, its launch tasks about 5 s). From a running Big Picture, a launch skips the session's and Steam's start: that is the PC-like
  path.
- **The gate:** `gate-load-2026-10-25-01` passed 64 cases; at the 65th (`initial-env64`) the console's Shell UI froze 60 s before the title printed anything
  and was force-aborted, and the title could not be killed (the control payload busy for 13+ minutes, FTP silent). After the owner restarted the console the
  same build passed all 98 (`gate-load-2026-10-25-02`, 1817 s). The freeze's cause is not known; it did not recur.

## The Steam guide, Polish and the controller choice (2026-10-26; on the base PS5, the owner's first run through to Big Picture)

The owner: "users are left confused" by the Steam setup; guided screens in the guided tour's style, from the first press
to Big Picture, and Polish as the tenth language. The design and what each screen says: [STEAM.md](STEAM.md), "The guide".

**Built** (branch `steam-guide`, from `hypervisor` d352dfe):
- `src/pw_steam_guide.{h,c}`: the launcher's pages, the record kept across the restarts, where the launcher resumes, the
  sign-in session's screens as a state machine (`tests/test_pw_steam_guide.c`).
- `src/pw_steam_screen.{h,c}`: the session screens, drawn by the title over the client's windows through the broker's cover
  (`tests/test_pw_steam_screen.c`: every screen in every language, words on the card, every character with a face).
- The launcher pages as the setup tour's Steam mode; the library's Steam row opens them until the client is signed in.
- The sessions (`PROTON_STEAM_GUIDE=install|signin` in the entries): the screens, the pad kept from the client while a
  screen shows, the results written back for the launcher.
- Polish across the tour and the guide; the Chinese and Japanese cuts also shipped as TrueType.
- Console pictures: `tools/run-launcher.py --steam-guide-pictures 1` (the pages) and `PROTON_STEAM_GUIDE_SHOWCASE=1`
  (the session screens); gate cases `steam-guide-pages` and `steam-guide-screens` (English).

**On the console** (base PS5 13.60, 2026-10-26; the gate was skipped at the owner's word, so there is no gate record and the
two new gate cases have not run inside one):
- `steam-guide-pages-2026-10-26-02` PASS: the launcher's 5 pages in all 10 languages (50 pictures) on the committed build,
  1300 frames at 37.0 fps, no fault or driver error. Its first run (`-01`, 36.8 fps) showed the Polish "Pierwsze
  uruchomienie" shrunk to fit its stage tile; a name too wide for the tile now wraps to two lines.
- `steam-guide-screens-2026-10-26-01` PASS: the title drew the 5 session screens in all 10 languages over Notepad through
  the broker's cover, 50 of 50 broker captures, 164 s.

**The owner's run** (base PS5 13.60, 2026-10-26, a new prefix `steamguide-fresh` through the test hook; klog followed into
`klog/steam-guide-owner-2026-10-26-01/`, `evidence/steam-guide-owner-2026-10-26-01.json`). Times from the intro page:
- the intro 19 s; the installer was already in `/data/ps5proton/downloads`, so the download page passed at once (a real
  download through the guide is still not shown);
- the install session 105 s: about 67 s making the new prefix (wineboot, Wine Mono), 32 s the installer. Its screen: 931
  frames shown, 0 refused. A set-up console's prefix exists already, so its "under a minute" holds there only;
- the sign-in session 3 s after it; the client's browser 292 s into it, 258 MB downloaded, without the "taking longer"
  screen; the phone screen its 25 s; the sign-in saved 66 s after the client's QR window (382 s);
- the launcher's done page, then Big Picture 58 s after the sign-in;
- the memory notice ("the PS5's memory is getting full", 816 MiB) 5 s into the sign-in session and again in Big Picture;
- the client's connection log never showed the logon line the watcher looks for.
An earlier harness attempt (`steam-guide-trial-2026-10-26-01`) never left the launcher: the rebuild had changed the
runtime's stamp and only the title had been deployed, so the launcher refused every program.

**Found in that run, fixed after it** (build 6c54dca6, rebased onto hypervisor 6837d44; on the base PS5 the same day):
- intense flicker on the signed-in screen: the client's frames replaced the cover between its pictures (graphics patch
  0032; `docs/STEAM.md`, "The guide");
- Steam stayed up 48 s after the sign-in (a 20 s countdown, 10 s of a close the client ignores, a 16 s hang snapshot):
  the session now ends at the sign-in, the client is ended once its token (`local.vdf`) is written after it, 25 s after the
  close at the latest, with no snapshot;
- Steam saw the pad twice, XInput's Xbox 360 controller and the DualSense HID device, and its glyphs swapped: Settings >
  Controller (`src/pw_controller.h`, Wine patch 2400) shows programs one, Xbox 360 by default, Xbox One or DualSense.

**On the console after the fixes** (`evidence/controller-owner-2026-10-26-01.json`):
- `cover-held-2026-10-26-01` PASS: the English guide screens over `d3d11-probe.exe` drawing in a window: `gpu-cover
  ended flips=188 held=522`, so 522 of its frames were kept off the screen, and all 5 captures show the guide's screen.
- the controller, by probe: Xbox 360 (default) has XInput's controller 0 (`controller-xinput-360-...` PASS) and no HID
  device (`controller-hid32-360-...`: "DualSense not enumerated"); DualSense has no XInput controller (`controller-xinput-ds-...`,
  xinput: "hidden: the DualSense is chosen") and the HID device `054c:0ce6` (`controller-hid32-ds-...` PASS).
- the owner, in Big Picture: the glyphs were right for each choice and no longer swapped (the klog holds the DualSense and
  Xbox One sessions).

**The owner's second run, with the fixes** (`evidence/guide-owner-2026-10-26-02.json`, a new prefix, Xbox One chosen): the
install 110 s, the phone screen 443 s from the intro, the sign-in 18 s after the client's QR window. Steam was ended 26 s
after the sign-in and the session was over at 28 s (50 s in the first run); Big Picture opened from the done page's
Continue. The owner: satisfied with all the fixes, the guided setup tested end to end. **Found:** `local.vdf` is not written
in the sign-in session at all; it is written when the next session starts, in both runs. So the wait for it always runs to
its 25 s cap, and the sign-in survived that end. A file the client writes at the sign-in would let the session end sooner.

**Not yet shown:** a real download through the guide (the installer was cached in both runs); the Steam Guard words in the client's connection log; native speakers' review of the Polish and of the guide's text in every language.
