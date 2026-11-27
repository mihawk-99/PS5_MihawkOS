# Features and accomplishments

Desktop/runtime evidence reviewed through 2026-11-27.
Application results retain the dates of their own evidence; the 2026-11-27 section below is the newest.
These are focused console accomplishments, not a universal compatibility list.

## Alpha 10: 1440p/4K, multi-controller + rumble, universal local processes & D3D12 Tier 2 (2026-11-27, base PS5 13.60)

| Feature | Accomplishment and limit |
| --- | --- |
| 1440p & 4K display output | Native `2560x1440` (`92.1 FPS`, `a10-res1440p-2026-11-27-01.json`) and `3840x2160` (`96.1 FPS`, `a10-res4k-2026-11-27-02.json`) at 120 Hz with `fast_copy=1` and graceful fallback to 1080p; configurable in Settings, per-game Options, and WebUI (10 languages). Pro 10.01 not yet tested |
| Multi-controller (2–4 DualSense) + Rumble | Up to 4 logged-in DualSense slots polled with 2 s hot-plug refresh across `xinput1_3` (64/32-bit) and `winebus.sys` HID; XInput and DualSense USB `0x02` rumble forwarded to `scePadSetVibration` (`a10-xinput-rumble-2026-11-27-01.json`) |
| Universal elevated local processes | All 64-bit games, browsers, and Chromium apps share the 65,536 CPU / 32,768 GPU app PT pool (`0` system PT) with deterministic `SHUT_WR`/`MSG_PEEK` exit flush (`a10-d3d12-local-2026-11-27-03.json`); 32-bit WoW64 stays in `elfldr` payload helpers |
| D3D12 Tier 2 & compatibility fixes | VKD3D-Proton Tiled Resources Tier 2 (`tiled_tier=2`, `a10-d3d12-local-2026-11-27-03.json`), 256 MiB executable cache budget, DXVK `dxbc-spirv` SM3 constant swizzle fix (`#42`), VM `EINTR`/`EAGAIN` retry (`#43`), atomic rename writeback flush (`#56`/`#59`), and orphan local-child cleanup (`#70`) |

## Persistent store client and elevated local-process 64-bit games (2026-11-26, base PS5 13.60)

Host suite passes **219/219** (`tools/host-tests.sh`) and `--check-patches` passes; full console commit gate (`tools/gate.py run`) and PS5 Pro 10.01 verification remain pending on this branch.

| Feature | Accomplishment and limit |
| --- | --- |
| Persistent store-client sessions | Games launch directly from Big Picture without closing the store client or restarting MihawkOS via `LoadExec`, return directly to live Big Picture on exit, and keep Cloud save sync (`AC Launch` + `AC Exit`) working across repeated launches (`evidence/p3-e2e-spelunky2-twice-2026-11-26-01.json`). Pro 10.01 not yet tested |
| Elevated Sony local-process runner | `/app0/wine-local-child.bin` (`sceSystemServiceAddLocalProcess` + Lapy elevation) executes JIT shared memory on firmware 13.60 (`jit80_value=42`, `evidence/sony-local-elev-pt-probe-02.json`), loads `ntdll.prx` via `sceKernelLoadStartModule` (`~5–6 ms`), spawns from a pre-warmed child in `25–30 ms`, and reclaims 100% of page tables and flexible memory on `sceSystemServiceKillLocalProcess` |
| 65,536 CPU / 32,768 GPU page tables | `ps5/sce_sys/param.json` sets `cpuPageTableSize = 256 MiB` (`65,536`) and `gpuPageTableSize = 128 MiB` (`32,768`), leaving `11,456 MiB` direct memory. 64-bit store-client games and the browser draw `0` entries from the 8,192-entry system pool |
| Verified 64-bit game runs (base PS5 13.60) | Heavy 3D game: `4.5 GiB` direct memory, `7,636` direct flips at `119.9 FPS`, `62,395/65,536` CPU PT free (`p3-local-village-2026-11-26-02.json`); 3D Direct3D 12 demo: `4,802` frames at `117.7 FPS`, `62,543/65,536` CPU PT free (`p3-local-re3demo-2026-11-26-01.json`); 2D action game launched twice in one session: `122–122.7 FPS`, live return and Cloud sync on both cycles (`p3-e2e-spelunky2-twice-2026-11-26-01.json`) |

## Windows 11 desktop (2026-10-12, branch `windows11`)

The latest [full gate](../evidence/gate-2026-10-12-win11-02-desktop-view.json) passes **72/72** on build `c8ca8a8a`.

| Feature | Accomplishment and limit |
| --- | --- |
| Desktop as a folder view | Progman hosts the shell's own view of the Desktop folder: rubber-band selection, click and drag with remembered places, rename, type-ahead, context menu, both Desktop folders merged, files made by other programs appear without a notification (32 checks by input in [desktop-view-2026-10-12-30](../evidence/desktop-view-2026-10-12-30.json)). Not yet: Delete and Enter checks, drag to an Explorer window, controller navigation, the View / Sort by / New menu entries |
| Desktop look | 48 px icons, shortcut icons with their own pictures and Windows 11's badge, soft label shadows, translucent selection plates; judged by pictures, not yet measured against a reference |
| Reference data | Windows 11 numbers from Microsoft's published design pages, the MIT WinUI and Windows Terminal sources, with every source recorded: `docs/windows11-reference/` |

## Memory, Chromium launchers and downloads (2026-10-12)

The latest [full gate](../evidence/gate-2026-10-12-dmem-03-host-dmem64.json) passes **71/71** on build `a4c50ce1`.

| Feature | Accomplishment and limit |
| --- | --- |
| Direct memory shared between processes | The kernel ties a direct-memory range to the process that made it; the owner grants another pid with `sceKernelProtectDirectMemoryForPID` (the call the system compositor uses, read from the 10.01 firmware files). Proved with two payloads, with the title mapping a helper-owned range, and with the GPU drawing correct pixels through it |
| GPU broker memory | Host-visible client buffers come from the 11,712 MiB application pool by default (patch 0009), not from the title's 1 GiB flexible pool; a range the helper refuses falls back to shared memory (gate cases `host-dmem64`, `host-dmem-fallback64`). Measured: four 64 MiB staging textures cost a helper 273 MiB of flexible memory before and 3 MiB after. **Not yet confirmed in a Chromium session's totals** |
| Memory facts | The console has about 10.8 GB of swap; the 1 GiB flexible pool is a soft limit past which pages go to the slow disk (the cause of slow writes and a .NET (MonoGame) game's slow loads). Windows sections are charged to the flexible pool (a 128 MiB section: +128 MiB) |
| A game launcher's client (CEF) | Its renderer no longer crashes (patch 1004: written image pages report PAGE_READWRITE); Windows programs see the console's real RAM (patch 1005, 11,711 MiB); the registry is saved every 30 s; a Unity game's download through it ran at 16 MiB/s in a stretch, with 0.5 to 2 MiB/s at other times that is not explained (server request latency was 250 to 900 microseconds then) |
| Downloads | Sequential regular-file writes merge into up to 256 KiB, flushed within 15 ms and before any other operation can see the file (patch 1007; 64 KiB writes 22 to 55 MB/s in the probe) |
| A second game launcher's client | Its service starts on demand and its install scripts no longer rerun (the registry survived an abnormal end); library reached earlier; UI correctness, login and launching games remain open |
| Memory guard | A steep-climb rule ends a runaway process within about a second, host-tested and gated |
| Gate | 71 cases including image-protect, system-memory, file-write, and the two direct-memory cases; the first run of the newest tree failed one hover pixel in `appearance32` that passed 3 of 3 alone (a flake, not fixed) |

## Mihawk OS desktop branch

The native Wine desktop has a centered taskbar, Start with local search and
persistent pins, and Explorer navigation/file commands. The shared desktop
prefix supports ordinary Windows applications and children. The opt-in retained
window route combines GDI and GPU windows; top-level GDI windows now support
regions, constant/per-pixel transparency, colour keys and click-through where
pixels are transparent. Wine owns window state and input.

The latest [full gate](../evidence/mihawk-alpha-gate-2026-10-09-12.json) passes
**59/59 in 31 min 33.9 s**, including eight simultaneous children, descendants
and graphics/telemetry shutdown. All 591 recorded native closes finish within
305 ms. Layered GPU/child windows, blur, shadows, previews and complete Windows
11 equivalence remain unqualified. See [Mihawk OS](MIHAWK_OS.md).

## Launcher and runtime

| Feature | Accomplishment and limit |
| --- | --- |
| Windows games on PS5 | 64-bit and native 32-bit Windows execution through a GE-Proton-derived runtime; tested workloads have console evidence, not universal compatibility |
| Game library | Controller-operated list, per-game profiles and separate Windows environments |
| Appearance | Thirty themes, persistent selection, Gloss / Skeuomorphic default; Square opens the picker |
| Loading and exit | Game-name loading screen, display handoff and return to the launcher |
| Console setup | Verified kstuff operation without etaHEN; resident websrv/elfldr is required |
| Filesystem access | Pinned Lapy service started by the title when needed |

## Multiple processes and shared console access

| Feature | Accomplishment and limit |
| --- | --- |
| Payload children | Production default; eight simultaneous Windows children verified |
| Descendants | Multiple generations, arguments, environment, pipes, handles and exit codes verified |
| Process control | Focused suspend/resume, termination and Windows-parent-exit checks passed |
| Java subprocesses | Small non-graphical JVM chains passed; this does not prove a Java game's launch chain |
| Graphics | Two DXVK children plus two rendering descendants displayed correct pixels and received input |
| OpenGL | Mesa/Zink translates OpenGL into Vulkan; visible PE32/PE64 probes, a .NET (MonoGame) game, a 32-bit native game and an FNA game's OpenGL build verified |
| DirectX | DXVK and VKD3D-Proton integrated; DirectX 11 rendering and an earlier focused DirectX 12 suite verified |
| Desktop windows | Helper windows/dialogs, foreground presentation and touchpad pointer/clicks verified |
| Controller | DualSense through the Windows controller interfaces used by tested programs; rumble is unproved |
| Audio | Helper audio reaches the title's mixer; a .NET (MonoGame) game's music confirmed by ear |
| Internet | Website-name lookup, HTTP and strict HTTPS verified in focused helper tests |
| Application memory | Helpers use the shared 11,712 MiB application pool; 4 GiB simultaneously verified across two helpers |
| Clean closure | PS-menu closure with a live 2 GiB graphics helper left no recorded helper alive |

Eight-child capacity and four-renderer validation are separate tests. The owner
tables contain 32 slots; that is not proof of 32 simultaneous Windows programs.
The memory pool is shared, not a separate 11,712 MiB allowance for every helper.

## Application results

| Program (by kind) | Evidence-backed status |
| --- | --- |
| A .NET (MonoGame) game | Manual loading/menu/music and human controller input confirmed; latest memory build rendered a world and returned to the launcher |
| The same game's save lifecycle | Earlier architecture passed save, relaunch/load and a 30-minute session; repeat on the current payload/Zink build |
| A Java game | Earlier Vulkan runs loaded worlds and reached 113–120 FPS in generated areas; today's complete chain from a Java launcher through Java to the game remains unproved |
| A game launcher's client | Latest normal-command-line run stayed up for 120 seconds and closed normally; UI correctness, login, library and launching games remain open |
| A 32-bit native game | Captured active 32-bit gameplay on a relaunch; person confirmed picture, controls and sound; roughly 39-second startup pause remains |
| An FNA game (OpenGL build) | 32-bit-required .NET installation: 3,470 displayed frames, captured level, non-silent stereo submission and clean closure |
| The same FNA game (DirectX build) | Person subsequently confirmed games with video/input/sound; an earlier relaunch crashed in Mono during loading, so repeatability remains to qualify |
| Windows setup | Earlier fresh-prefix, service and 64-bit MSI tests passed; repeat first-launch setup with today's default Zink staging |

## Shared foundations and reliability

PS5_VulkanTemplate supplies the launcher foundation. PS5_Vulkan owns the reusable
GPU broker and Mesa/Zink builder; PS5_Mesa owns the associated graphics changes.
Proton owns their integration with Windows programs. A known ShadowMountPlus
limitation (another homebrew may not start after MihawkOS exits) is described in
[LAUNCH_AFTER_EXIT.md](LAUNCH_AFTER_EXIT.md); MihawkOS waits for an upstream fix.

The 2026-10-07 main-branch build passed its then-current 15 mandatory gate cases: the existing process, memory, rendering,
HTTPS, desktop and memory-guard checks, plus PE32 execution, WoW64, Vulkan/Zink
32-bit graphics and focused input/upload regressions. Patch 0955 fixes the shared
WoW64 hardware-input layout; the PE32 upload probe now finishes naturally in
11.4 seconds (maximum frame 44 ms), where it previously required watchdog
intervention after 221.3 seconds (maximum frame 35.8 seconds).

Sources: [PLAN.md](PLAN.md), [WINDOWS_PROCESSES.md](WINDOWS_PROCESSES.md),
[GRAPHICS_PROCESS_BROKER.md](GRAPHICS_PROCESS_BROKER.md),
[GAME_LIFECYCLE.md](GAME_LIFECYCLE.md) and
[the 2026-10-07 gate](../evidence/gate-message-fix-2026-10-07-01.json).
