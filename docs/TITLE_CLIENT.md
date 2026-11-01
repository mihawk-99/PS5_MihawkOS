# One Windows client in the title, beside the GPU broker (design, 2026-10-26)

Design only: nothing here is built or run. The owner decides the build. The goal and the memory measurements are in
[FLEXIBLE_MEMORY.md](FLEXIBLE_MEMORY.md) ("Next: the game inside the title").

## Why

A helper's code (JIT memory) and flexible memory come from the system pool, which every helper and the console share
(~1 GiB). The title's own partition has ~950 MiB free. One game run as a client inside the title takes its code and
flexible memory from that partition instead, on every model and firmware. Steam and the launchers stay helpers under the
64 MiB code rule.

## What exists and what was measured

- A program runs in the title on 13.60: its code is converted to JIT memory in the title's partition (8835394,
  `fmem-intitle-p2-2026-10-26-02`).
- It can start children: the shared syscall slot no longer sends the title to a helper's dispatcher (patch 2300,
  `fmem-intitle-p4-2026-10-26-04`).
- One child can already run in the title: the owner-graphics mode (patch 0927, `start_owner_graphics`). It works only
  without the broker, and without the broker Steam's helpers have no Vulkan, so Steam never started the 2D game
  (`fmem-isaac-intitle-2026-10-26-01`).
- 32-bit code runs in the title (`fmem-intitle-pe32-2026-10-26-01`), but a fault in 32-bit code there ends the process:
  the title has no WoW64 signal entry. The helpers' route to one (the kernel's own `sigaction`, made by the payload) is
  not available to the title and is not pursued. The first version is for 64-bit games only.

## Display: the broker owns it, and the game in the title is one of its clients

The broker already owns the GPU context and VideoOut, composes the helpers' windows, and flips a fullscreen game's
images directly (Game Mode). The game in the title should be one more broker client, not a second owner of the
display:
- one presenter: the broker flips. Frames of the game in the title and of Steam's helpers are composed by the same code
  that composes helpers today, by foreground window, and the game in front goes direct as a helper's would;
- input and focus follow the foreground window, as for helpers;
- no second native GPU context, so no handover of VideoOut, and the loading screen rules stay as they are.

The other choice, the game drawing with the title's own RADV and VideoOut as the owner-graphics mode does, needs two
presenters in one process and a handover of the display whenever focus moves between Steam and the game. Not
recommended.

## Vulkan: through the broker, over a connection inside the title

A helper's winevulkan uses the broker client RADV (`libvulkan-helper.prx`), which allocates, submits, waits and presents
over a socket. The game in the title does the same over a socketpair the broker accepts from its own process. Nothing
changes for the game, and the broker's allocation, fences and cleanup stay the only path. Two points differ from a
helper and are the design's real work:
1. **Two RADV builds in one process**: the broker's native RADV and the client RADV. Their symbols and global state
   must not meet. To check first: whether the client module loads beside the broker's in the title (the loader's
   per-module symbol resolution), before anything draws.
2. **Shared buffers in one address space**: the broker reserves ranges so that a host-visible buffer has compatible
   addresses in the owner and in a client. In the title owner and client are one address space, so a second mapping
   at the same address is impossible. A local client takes the broker's own mapping of the buffer instead (the same
   memory, mapped once).

**Found before building (2026-10-26), point 2 decided by the code:** the owner maps every host-visible buffer at
the address it chooses, in its own address space, and grants it to the GPU there (`server.cpp`, `allocate`:
`map_host` then `radv_ps5_memory_grant_gpu`). A client maps a buffer only over its own bare reservation of that
address (`client.c`, `radv_ps5_memory_alloc`: `bare()`), so in the title every host-visible allocation would be
refused, and a client's free (`sceKernelMunmap` or a reservation laid over it) would take the owner's own mapping.
A client in the title needs a local mode in `client.c`: use the owner's mapping as it is, and leave it to the owner on
free. The code that refuses it (`bare()`, the free that lays a reservation over the buffer) is not PS5_Vulkan's: it comes
from Proton's own patches on the exported foundation (`platform/graphics/patches/0009`, `0013`, `0014`, `0016`), so
the local mode belongs in that series too (a patch 0031), with no change to PS5_Vulkan.

The client's per-submit cost is a helper's (one socket round trip); the game in the title gains memory, not speed.

## Choosing the game

Alpha 7's default ("Run inside MihawkOS"): ntdll asks `pw_ps5_title_route` (patch 2403) as each program starts, with the
image's Unix path. The first program that passes goes to the title; a second goes to a helper (EBUSY). A program passes
when it is:
- 64-bit, from a library (`\steamapps\common\`, `\games\`, or any program a library entry names, wherever it
  lives: a USB drive, a folder of the player's own; passed on as `WINE_PS5_TITLE_LISTED`, the store client's
  `steam.exe` left out), not a redistributable installer, crash reporter or
  `*launcher*` by folder or name, and not opted out (`WINE_PS5_TITLE_GAMES=0` in its `game.json` env, passed on as
  `WINE_PS5_TITLE_OPTOUT`) (`wine/ps5/pw_wine_title_game.h`, host-tested);
- and a game by what it is: its import or delay-import table, or that of a DLL it imports from its own folder (an
  engine's player DLL), reaches d3d8/9/10/11/12, dxgi, ddraw, vulkan-1 or opengl32. A program that imports a graphics
  API itself is a game even when it also imports CEF (a game's menus); one that imports a browser shell (CEF, Qt
  WebEngine) and no graphics API, or a .NET program, is not (`wine/ps5/pw_wine_pe_imports.h`, host-tested on fixture
  images, including a game with its CEF web helper beside it). A launcher or a
  bootstrap stub that only starts the game is no game, so the game it starts gets the title.
- Or its entry picks it (`WINE_PS5_TITLE_GAME=<exe>` in `game.json` env, passed on as `WINE_PS5_TITLE_PICK`): the one
  program of a game that runs in the title whatever the rules say, for a game that loads its renderer only at run time.

A Chromium-based game (Electron, NW.js) is told by its folder: Chromium's `resources.pak`, `icudtl.dat` and a V8
snapshot (`v8_context_snapshot.bin` or `snapshot_blob.bin`) beside the exe, and no `libcef.dll` (a CEF program is a
launcher's browser). Its imports say nothing reliable, since it loads Direct3D at run time, so the folder decides, and
it stays in a helper ("a Chromium-based game: a helper until one draws in the title"). It starts as one process:
kernelbase patch 2404 appends `WINE_PS5_CHROMIUM_ARGS` (the title's default `--single-process`, the switch the store
client's browser runs with) to its main process's command line. Its own processes (`--type=`) are helpers too.

The 2D NW.js game on the standard PS5, 2026-10-31 (evidence/moonstone-base-2026-10-31-01.json, -nonone, -fallback, -helper-fallback and -helper-single):

| Where, switches | Its processes | Draws | System pool, lowest free of 8,192 | End |
| --- | --- | --- | --- | --- |
| Alpha 6: a helper, none | 8 | yes | 915 | ended by the page-table guard |
| The title, `--single-process` | 2 | no (no swapchain in 10 min) | 3,068 | clean close |
| The title, none | 3 (no renderer, no GPU process) | no | 3,085 | clean close |
| The title, in-process GPU, one renderer, network in-process | 2 | no | 3,133 | clean close |
| A helper, in-process GPU, one renderer, network in-process | 9 | yes | 811 | ended by the page-table guard at 87 s |
| **A helper, `--single-process` (the default)** | 2 | yes, smoothly (the owner watched) | 2,004, flat at about 2,034 after 8 min | clean close |

Why such a game does not draw in the title is open: it loads d3d11, dxgi and Vulkan and never makes a swapchain.

**A game the launcher starts is the session's root**, which no helper requests. The title applies the same rules to
it from its own file (`root_in_title`) and, when it passes, runs it itself (`start_title_root`: the title client's
environment, the root's own command line) instead of in a helper; a start that fails before Wine runs falls back to
a helper. Before this, a library entry started from the launcher never ran in the title, wherever it lived (a big
Direct3D 12 racing game on a USB drive, 2026-10-31). Logged as `the session's root <exe>: inside MihawkOS (…)`.

Every library program's route is logged with its reason, for example `wine-ps5: game64.exe: inside MihawkOS (a 64-bit
library game; imports a graphics API)` or `wine-ps5: start.exe: a helper (not a game; no graphics imports)`; the title
adds `the title's place is taken; a helper`. The title turns the default on beside the GPU broker (`WINE_PS5_TITLE_GAMES=1`
unless the launch set it).

**The title's end waits for its game.** The helpers that the program in the title starts, and those they start, are its
game's: when that program ends (a launcher that started the game and exited), the title waits for them with no time
limit, instead of closing every helper after 15 s (`pw_ps5_wait_children`). Other helpers, Steam's among them, are still
closed after 15 s, so closing a game in the title still restarts MihawkOS (option a). Before this, a launcher in the title
had its game closed 15 s after it exited. Gate case `launcher-pair`: a launcher copy (no graphics import) starts the
renderer and exits; the renderer must run in the title and end with 0. Proven on the base PS5 on 2026-10-31:
library copies of a renderer (rung8-base-2026-10-31-fallbacks, now the gate case `title-games`) and the 3D demo started
from Steam's Big Picture (ownerplay-base-2026-10-31).

The design before the default, kept for its reasons:
The first process a listed client (`pw_code_budget_listed`: Steam, the launchers) starts that is neither listed nor
one of Wine's own goes to the title, if nothing runs there yet; every later one goes to a helper. A setting names it
outright (`WINE_PS5_TITLE_PROCESS=<image>`, the by-name selection kept in the sessions' notes until something
exercises it). A 32-bit image is never chosen while the title has no WoW64 signal entry.

## Stop and end

What ships in Alpha 7 (2026-10-31):
- **The game's end ends the session** (the first version below): MihawkOS returns to its launcher, about 10 s, and
  Steam opens again. Closing only the game while Steam stays is not built.
- **`end_game` never replaces the title under a game in it.** The hang breaker, the memory guard and the player's stop
  all end a game through `end_game`. With a game in the title, it ends the helpers by SIGKILL and then the game through
  the server (patch 0899), and returns only once the game has ended. Before this fix it replaced the title with the
  game's threads still running, the case that powered the console off on 2026-10-03.
- **The title watchdog** (`pw_hang_breaker_title_decide`, `src/pw_hang_breaker.h`): a game in the title that shows no
  new frame for 180 s, once it has shown one, is ended that way. The census plays no part. 180 s is three times the
  3D demo's longest healthy gap (60 s while it loads). Proven with a spinning and a blocked renderer
  (rung2-base-2026-10-31-spin and -wait, at a test's 15 s).
- **A crash of the game in the title** (a 64-bit Windows fault, even with frames in flight) goes through Wine's SEH to
  NtTerminateProcess and the normal return, in about 0.7 s (crash-base-2026-10-31-gpu6). A broker failure
  (`PROTON_GPU_TEST_FAIL_SUBMIT`) returns the same way with the broker left wedged (rung3-base-2026-10-31-gpufail).

The design notes before Alpha 7:
- **Steam ignores the stop request** (`fmem-isaac-intitle-2026-10-26-01`: the title was still running minutes later).
  The stop path is therefore bounded by the title itself: ask the game in the title to close; after a grace period, end
  every helper the way a helper is ended today (control EOF, native-PID absence, GPU completion); then the title
  returns to the launcher. No step waits on Steam's cooperation, and no kill from outside is part of the design.
- **The game ends while Steam stays**: Wine ends a process with `_exit`, which in the title ends the session. First
  version: the game's end ends the session (helpers ended as above, then back to the launcher). Later: the title's
  client ends its threads and gives back its memory and GPU objects, and the title keeps serving Steam. A second game
  then runs in a helper.
- **A crash of the game** is a crash of the title: the broker, the server and every helper go with it. A helper's crash
  ends only the helper. This is the main cost and stays one.

## Risks

- One process holds the broker, the server and the game: any fault in the game ends the session.
- Two RADV builds in one process (above); a symbol or global clash is a crash at device creation.
- Shared-buffer addressing in one address space (above); a wrong mapping is silent corruption, so the first proof reads
  pixels back.
- Teardown of the game's GPU memory in the title: wired memory's teardown has hung the console ("unwire timeout"); the
  first version ends the session instead of tearing down.
- The title's partition also holds the title's own use; with the broker running there, the room left for the game is
  measured, not assumed (944 MiB free in `fmem-isaac-intitle-2026-10-26-01`'s title before any game).
- 32-bit games are out of scope until the title has a WoW64 signal entry.

## The smallest first proof

On the base PS5, launcher layout (broker on):
1. A parent in a helper starts a 64-bit Vulkan child chosen by name (the gate's DXVK window probe's child). The child
   runs in the title as a local broker client and presents one window.
2. Pass: the child's pixels read back correct (the probe's existing readback), the parent keeps running in its helper,
   the stop path ends everything within its bound, 0 storage errors.
3. Measure: the title's flexible free before, during and after; the system pool's census against the same child in a
   helper.

**The first proof, done (base PS5, 13.60, 2026-10-26).** The gate's `launcher-render` case: `process-probe.exe
--graphics-broker`, in a helper, starts two DXVK renderers (`graphics-render-probe.exe`, each starting a rendering
descendant), with `WINE_PS5_TITLE_PROCESS=graphics-render-probe.exe`. The first renderer ran in the title as a broker
client in the owner's process; the second, and every descendant, in helpers.

| Run | Renderers in helpers (peak resident) | In the title | Title's flexible free | Verdict |
| --- | --- | --- | --- | --- |
| `fmem-titleclient-base-2026-10-26-01` (no setting) | 4: 33, 33, 31, 30 MiB | none | 945 MiB | PASS, broker stopped |
| `fmem-titleclient-p1-2026-10-26-04` | 3: 33, 30, 30 MiB | the first renderer | 901 MiB | PASS, broker stopped |

Every renderer, the one in the title included, printed `PS5_PROTON_D3D11_PROBE PASS: frames and readback` and `GPU
initialized, frames, readback and explicit teardown`; the parent printed `PS5_PROTON_GRAPHICS_BROKER PASS`. 0 storage
errors. The renderer's ~33 MiB left the system pool, and the title's partition paid 44 MiB for it.

What it took (graphics patch 0031, Wine patch 2301, `pw_wine_process.c`, `pw_wine_dl.c`, `native/proton_wine.cpp`):
- the broker opens a slot for its own process (`ps5_gpu_broker_open_in_owner`; INIT may name the owner's pid there only),
  and that client uses the owner's mappings (`ps5_gpu_client_in_owner`, `PROTON_GPU_IN_OWNER=1`);
- the title starts the program as a client of its own server (`start_title_client`, beside the owner-graphics start),
  with `WINE_PS5_GRAPHICS_OWNER` (the in-process server's callbacks, patch 0927: without it the client stopped after
  `server_init_process`, `fmem-titleclient-p1-2026-10-26-02`) and `WINE_PS5_TITLE_CLIENT` (the helpers' `win32u` and
  Vulkan builds, the helpers' window surfaces; its window and frame pages attached in the title directly);
- at the session's end the title closes that slot before stopping the broker; its pid, the title's, is never absent
  (`-03`: `broker_stopped=0` before that).
- `fmem-titleclient-p1-2026-10-26-01`: the broker refused a slot for its own pid (EINVAL) before 0031's owner side.

**The Unity game in the title (base PS5, 2026-10-26).** Started by the desktop shell in the owner's
`mihawk` prefix (`--profile mihawk-os --program 'C:\windows\system32\mihawk.exe'` with the game's path), 240 s, the
first-run "Default settings" screen on both (the pad's crosses open its language list). The owner's copy on the
console was checked first, read only: 16,552 files, 9,006,934,458 bytes, the same as the PC copy.

| Run | Game | Helpers at their peak | System census peak | Title's flexible free | Broker's frames for the game |
| --- | --- | --- | --- | --- | --- |
| `fmem-bd-helper-2026-10-26-02` | in a helper (68 MiB) | 158 MiB in 8 | 955 MiB | 944 MiB | 85 fps composed |
| `fmem-bd-title-2026-10-26-02` | in the title | 95 MiB in 7 | 887 MiB | 946 -> 897 MiB | 95 fps composed |

About 63 MiB left the system pool; the title's partition paid 49 MiB. The game's window in the title had no title bar
(the desktop shows through at the top): its frame is not composed yet.

`fmem-bd-title-2026-10-26-01` crashed the title at the stop ("A user thread receives a fatal signal"): the end path
closed the game's broker slot, freeing buffers its render thread still used. The slot is now never closed under a
program in the title; the broker ends with the process, and the verdict line says `title_client=1`. That run's record
was also lost to the runner, which choked on a perf line the in-title program's own output ran into (now parsed item
by item).

**The Direct3D 12 game in the title (base PS5, 2026-10-26)**, the heavy step: 64-bit, D3D12 through VKD3D-Proton, the owner's
save loaded with Continue (pad crosses at 150-215 s), started by `cmd /c start "" /wait /d` with its folder and its program
in the mihawk prefix (its own folder as the working directory: started elsewhere, by the desktop shell, it
presented a black window for 460 s, `fmem-re3-helper-2026-10-26-02`; neither the 256 MiB section threshold nor the code
budget off changed that, `-dsec256-`, `-nobudget-`). Gameplay in both, the alley at 260 s, full screen (direct flip).

| Run | Game | Helpers at their peak | System census peak | SceShellCore RSS peak | Title's flexible free | Frames |
| --- | --- | --- | --- | --- | --- | --- |
| `fmem-re3-helper-2026-10-26-03` | in a helper (112 MiB; code budget raised to 96 MiB) | 160 MiB | 917 MiB | 914 MiB | 946 MiB | 117 fps direct |
| `fmem-re3-title-2026-10-26-01` | in the title | 54 MiB | 812 MiB | 807 MiB | 898 MiB | 117 fps direct |

About 106 MiB left the system pool; the title's flexible free fell 48 MiB. The game's direct memory (about 9.3 GiB
in both) is in neither pool. Both runs: verdict PASS (the title's with `title_client=1`), 0 storage errors.

The code budget now applies to a program in the title too, but the Direct3D 12 game never reaches it there. Its code runs from
direct memory with read and execute (the game's image, 0x140000000-0x145ca8000, type 4, in the title's map at 290 s,
`fmem-re3-title-map-2026-10-26-03`), so nothing faults into conversion. The cache held 16 KiB, the self-check's
(`fmem-re3-budget-title-2026-10-26-01`). The 48 MiB the title's flexible free fell is the game's anonymous data, not
its code. The budget's figures for the game in a helper are in docs/FLEXIBLE_MEMORY.md ("The Direct3D 12 game against
the budget").

Only after that, the owner's games (2026-10-26):
1. The Unity game: 64-bit Unity, D3D11, no `steam_api`, started without Steam.
2. The Direct3D 12 game: 64-bit, D3D12 through vkd3d-proton, through Steam. Compared on memory (both pools) against its
   earlier Pro runs, not on frame rate: the base PS5's display runs at 60 Hz, the Pro runs were at 120 Hz.
3. Then the game's end without the session's end.
