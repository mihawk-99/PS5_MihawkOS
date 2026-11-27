# Windows processes

Current production guide, updated 2026-11-26. The chronological
investigation, including failed runs and obsolete opt-ins, is retained in
[the process history](history/2026-10-07/docs/WINDOWS_PROCESSES.md).

## Bootstrap and ownership

Every executable defaults to the owner/broker layout. The PS5 title hosts the
shared wineserver, graphics/display, input and audio. Windows programs run in
isolated native processes through two complementary backends:

- **`elfldr` payload helpers** (created via resident `websrv`/`elfldr`): used for
  32-bit programs (WoW64), Wine system services, non-graphics tools, and direct-launched
  helpers. They consume page tables from the console's 8,192-entry system pool and
  allocate direct memory through `sceKernelAllocateDirectMemoryForApp`.
- **Elevated Sony local processes (`/app0/wine-local-child.bin`)** (created via
  `sceSystemServiceAddLocalProcess`, enabled by default in persistent store-client
  sessions with `WINE_PS5_LOCAL_GAMES=1`): used for 64-bit graphics programs (games
  and the store client's browser). Each local child elevates through the resident
  Lapy daemon before touching memory (granting `euid=0` and JIT shared-memory
  execution on firmware 13.60), loads `ntdll.prx` via `sceKernelLoadStartModule`
  (`~5–6 ms`), draws 100% of its page tables from the title's expanded **65,536 CPU
  / 32,768 GPU** application page-table pools (`ps5/sce_sys/param.json`) and `1 GiB`
  flexible pool (`0` system page tables), allocates direct memory via ordinary
  `sceKernelAllocateDirectMemory`, and returns 100% of its page tables and memory
  when ended through `sceSystemServiceKillLocalProcess`. One pre-warmed local child
  is kept parked so game and browser launches complete in **25–30 ms**.

Descendant requests return to the owning title; Wine preserves their actual
Windows parent relationships, startup parameters and inherited handles.

A rendezvous transfers a payload helper's packet control connection, distinct from
the wineserver connection and GPU/input/audio transports (local processes inherit
their `SOCK_SEQPACKET` control endpoint directly as fd 3 via `SpawnOptions.fd`).
The loader acknowledges the endpoint before the owner closes its copy, and the owner
keeps the child's GPU-broker socket until READY: a unix socket whose every reference
is in flight can arrive flushed on the console (readable, first read 0 bytes). The
loader and runtime must agree on the bootstrap contract. Control EOF ends a helper
(payloads return; local children flush, close fd 3, and wait for
`sceSystemServiceKillLocalProcess`). The owner checks native-PID absence and GPU
completion before reclaiming resources.

The 32-entry tables are implementation capacity, not a 32-child console claim.
Eight simultaneous Windows children and four rendering Windows processes were
verified in separate tests. Both counts exclude any claim of universal game-scale
concurrency. The one-graphics-client arrangement remains an older diagnostic mode,
not the current default.

## Process behavior already checked

- Arguments with spaces, inherited environment, working directory and pipes.
- Inheritable/non-inheritable handles, completion events and full exit codes.
- Suspended creation/resume, termination and repeated resource reuse.
- Descendant chains and spawning after an immediate Windows parent exits.
- Cleanup when the owning application ends.
- Java ProcessBuilder chains in small interpreted, non-graphical JVM tests.

Representative evidence: [eight children](../evidence/payload-eight-02.json),
[descendants](../evidence/payload-descendants-02.json),
[owner replacement](../evidence/payload-owner-close-02.json),
[graphics](../evidence/payload-graphics-10.json) and
[Java](../evidence/java-process-console-03.json). Earlier Java and process results
must be repeated where the current loader/memory behavior matters.

## Graphics, desktop, input and audio

The GPU broker lets children and descendants render through the owner's GPU
context and display. Four DXVK rendering processes passed correct pixels,
controller input and native cleanup. Ordinary desktop windows use the compositor;
touchpad motion and clicks are routed through the shared Wine desktop.

OpenGL defaults to **Mesa WGL/Zink → Vulkan/RADV → GPU broker**. Audio defaults
to per-helper rings feeding the owner's mixer. The controller is available through
shared state/HID reports with process-specific Windows device identity.
A .NET (MonoGame) game's human input and manual-launch music were confirmed on the console.

## Manual launch defaults, 2026-10-06

Earlier automated tests enabled Zink/audio explicitly, leaving ordinary launches
without them. Production now enables both for the owner/broker layout, independent
of game name. The loading display hands over only when the game presenter opens
VideoOut, rather than when broker initialization starts.

[manual-default-audio-2026-10-06-02](../evidence/manual-default-audio-2026-10-06-02.json)
passed without graphics/audio overrides: correct title screen of a .NET (MonoGame) game, loading
handoff after 16.2 s, audible music, closure and native cleanup. A subsequent human
launch through the menu confirmed loading, menu and music together. Fresh-prefix
first-launch Mesa staging remains open.

## Networking, setup and Windows services

Helpers ask the title to resolve website names; the title owns the console resolver
library. HTTP and strict HTTPS passed, including trust checks with the console's
clock set correctly. GnuTLS and pinned certificate roots back Windows TLS.
See the 2026-10-05 battery run (record removed from the tree; in git history) and the latest gate's helper TLS case.

Earlier fresh-prefix tests created the Windows environment on-console, started
services, installed a 64-bit MSI and ran the installed program. Mono/Gecko are
pinned runtime components. Services run during required prefix setup or when
explicitly requested, rather than being enabled for every game. These setup
results predate today's full payload/Zink defaults; repeat them before claiming
current first-launch acceptance. See [BOOT_STATE.md](BOOT_STATE.md).

## Application memory in payload helpers, 2026-10-07

`sceKernelAllocateDirectMemoryForApp` explicitly selects the application pool.
The payload passes it to Wine's existing direct-memory mapper through
`__wine_ps5_set_payload_direct_memory`. Helpers retain budget 0, with no budget
reassignment or kernel data patch. Only the payload imports `libkernel_sys`.

The console reports **11,712 MiB (11.4375 GiB)**, shared by the title, graphics and
all helpers. Windows commits and Wine's Unix heap use this backing by default.
Reservations remain virtual until committed; decommit/release returns backing.
Native modules, bootstrap and some shared transport still use system memory.

| Test | Result |
| --- | --- |
| Ordinary payload allocator, 2 GiB | Refused allocation with `0x80020023`; retained negative control |
| Application allocator, 2 GiB | Allocate/map/touch/verify/release passed; system residency 3,504 KiB |
| Two Windows helpers | Each held 2 GiB simultaneously; two verify/decommit/zeroed-recommit/free cycles passed; each direct peak 2,075 MiB and system residency 14 MiB |
| Executable-memory probe | 32 executions, 480 adjacent writes/queries, 32 sparse-zero cycles, preserved neighbors; 16 GiB virtual reservation with small committed pieces |
| Full shell closure | Graphics helper held 2 GiB; 3/3 recorded helper PIDs absent after closure; no CPU/GPU fault |

See [capacity](../evidence/gate-payload-forapp-2026-10-07-02-payload-memory.json),
[VM behavior](../evidence/payload-forapp-vm-2026-10-07-02.json) and
[shell-close validation](../evidence/payload-forapp-shell-close-2026-10-07-01-validation.json).
The shell deliberately interrupted that run; the program-run file has no ordinary
completion verdict. The separate validation file proves the cleanup outcome.

The first VM attempt failed before Windows startup because the callback was not
in the PRX export descriptor. The export was added. The first nine-case gate then
failed its guard case because `WINE_PS5_PAYLOAD_DMEM=0` was not forwarded to helpers.
Inheritance was corrected and the full gate repeated. Neither failed run authorized
a code commit. Both remain in `evidence/` and the history.

## Latest application results

| Run (2026-10-07) | What it proves |
| --- | --- |
| A game launcher's client | Normal command line, 120-second budget, status 0 on close and return to launcher; system peak 721 MiB; application-pool peak 2,714 MiB including owner/GPU |
| A .NET (MonoGame) game | Default Zink path, loading handoff 15.8 s, captured world, normal requested closure/return; helper direct peak 1,024 MiB, system peak 246 MiB |

The launcher's swap-out counter did not show the earlier sustained growth. One browser
helper still recorded a nonfatal `wbemprox.dll` text-protection warning and a mapper
operation failure; it did not report a physical allocation refusal. The person
reports unusual UI on both console and host Wine. Login, library use, game launch
and visual correctness remain unproved; do not assign the UI's cause from this
observation alone.

Evidence: the four 2026-10-07 run and memory records (removed from the tree; in git history).

## Elevated Sony local processes and expanded page tables, 2026-11-26

With `kernel.cpuPageTableSize = 268435456` (65,536 CPU PT) and
`kernel.gpuPageTableSize = 134217728` (32,768 GPU PT) in `ps5/sce_sys/param.json`,
the shared direct-memory pool is **11,456 MiB** (`11,712 MiB` with default PT sizes).
On the standard PS5 (firmware 13.60), elevated Sony local processes were verified in
the standalone flex probe (`evidence/sony-local-elev-pt-probe-02.json`: `euid=0`,
`jit80_made=0`, `jit80_value=42`, `65,513/65,536` CPU PT and `1,031,798,784` B FMEM
100% reclaimed on close) and across three 64-bit games launched from a persistent
store-client session:
- Heavy 3D game (`evidence/p3-local-village-2026-11-26-02.json`): `30 ms` warm spawn,
  `4.5 GiB` direct memory, `7,636` direct flips at `119.9 FPS`, `62,395/65,536` CPU PT
  free, `3,721/8,192` system PT free.
- 3D Direct3D 12 demo (`evidence/p3-local-re3demo-2026-11-26-01.json`): `26 ms` warm
  spawn, `4,802` direct frames at `117.7 FPS`, `62,543/65,536` CPU PT free,
  `3,819/8,192` system PT free.
- 2D action game launched twice in one session (`evidence/p3-e2e-spelunky2-twice-2026-11-26-01.json`):
  `27–30 ms` warm spawns, `122–122.7 FPS`, full page-table reclamation between launches
  (`63,661 -> 64,574 -> 63,644 -> 65,002` free of `65,536`), `3,971/8,192` system PT free.

Set `WINE_PS5_LOCAL_GAMES=0` to disable local-process routing and fall back to
`elfldr` payload helpers.

## Diagnostics and remaining limits

`PROTON_LAYOUT=title` and `single`, `PROTON_ZINK=0`, `WINE_PS5_LOCAL_GAMES=0`,
`WINE_PS5_HELPER_AUDIO=0` and `WINE_PS5_PAYLOAD_DMEM=0` are explicit diagnostics,
not recommended game defaults. The old `WINE_PS5_PAYLOAD_CHILDREN=0` toggle is
obsolete; local-process routing is controlled by `WINE_PS5_LOCAL_GAMES`.

The system-memory guard stays enabled because application backing does not remove
all system-pool consumption. Closing under synthetic pressure passed in the latest
gate. Do not equate a bounded clean close with crash recovery or unlimited stability.

Open targets: complete operation of a game launcher; current-build Java-launcher chain;
fresh-prefix Zink staging; broader concurrent graphics/audio workloads; refresh of
the broad Wine/DirectX baselines. General 32-bit compatibility and rumble remain outside the
verified feature set. Follow [PLAN.md](PLAN.md) and
[NEXT_CONSOLE_SESSION.md](NEXT_CONSOLE_SESSION.md).
