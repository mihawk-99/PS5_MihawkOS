# Architecture

Production architecture as of 2026-11-26. See [PLAN.md](PLAN.md) for acceptance
status and persistent store-client / elevated local-process measurements, and
[the earlier architecture](history/2026-10-07/docs/ARCHITECTURE.md) for the
single-title and earlier un-elevated Sony-managed experiments.

## Ownership

| Layer | Owner |
| --- | --- |
| Title, Vulkan UI, native pad/audio integration and harness | PS5_VulkanTemplate foundation |
| Game library, profiles, runtime staging and launch/return flow | PS5_Proton |
| Wine PS5 adapters, shared wineserver, payload and local-process bootstraps | PS5_Proton, derived from prospero-win |
| Wine Windows modules, DXVK, VKD3D-Proton and Proton patches | Pinned GE-Proton stack, with this repository's ordered PS5 patches |
| RADV, canonical link recipe, reusable GPU broker and WGL/Zink builder | PS5_Vulkan |
| RADV broker overlays and Zink presentation/memory fixes | PS5_Mesa |
| Filesystem and local-process elevation | Pinned Lapy daemon and platform client |
| Native payload creation | Resident websrv/elfldr |
| Elevated Sony local-process helper (`/app0/wine-local-child.bin`) | Title via `sceSystemServiceAddLocalProcess` / `sceSystemServiceKillLocalProcess`, built by `tools/build-local-child.sh` |

## Boot and shutdown

```text
PS5 home screen
  -> launcher title: filesystem grant, game library, thirty-theme UI
  -> select a game or store client: persist one-shot request, release launcher devices, LoadExec
  -> owner title: shared wineserver + memory helper + GPU broker + input/audio
       -> 32-bit programs, services, and default helpers in elfldr payloads (system PT pool)
       -> 64-bit store-client games and browser in elevated Sony local processes
          (/app0/wine-local-child.bin; 65,536 CPU / 32,768 GPU app PT pool; 0 system PT)
       -> direct-launched 64-bit games may also run in-title (TITLE_CLIENT.md)
  -> game exit inside a persistent store-client session:
       local process closed via sceSystemServiceKillLocalProcess (100% PT/memory reclaimed),
       store client stays alive (no LoadExec handover)
  -> session close: stop/reap helpers and local children, finish GPU work, save runtime state
  -> LoadExec back to launcher
```

The persisted one-shot request survives the observed loss of restart arguments.
The title and its Sony local processes (`/app0/wine-local-child.bin`) never call
`exit()` or return from `_start`; the title ends or replaces itself through its
shell-close and `LoadExec` paths, and a Sony local process closes its control
endpoint (fd 3) and parks until the title ends it via
`sceSystemServiceKillLocalProcess`. An `elfldr` payload has its own native
lifetime and may return from its payload entry point.

## Processes and bootstrap

On `mihawk-os`, a built-in desktop choice uses the shared `mihawk` prefix.
Its entry, `C:\\windows\\system32\\mihawk.exe`, is run by the title itself
(`src/pw_desktop_session.h`): Explorer is the session root, enabling its shell
on `WinSta0\\Default`, and the title unloads and restarts it around full-screen
games (game mode). As on Windows, the desktop window is the system's and
Explorer's desktop is Progman, so Explorer can be replaced. Applications are
normal children/descendants of Explorer and share the existing owner services.
Closing an application leaves the shell running; closing the shell (log off)
ends the owner through its normal restart path. The native game-library boot remains the default during qualification.
See [Mihawk OS](MIHAWK_OS.md) for evidence and display-composition limits.

The everyday shell is native Wine Explorer code enabled by `MIHAWK_DESKTOP=1`.
Patches 0981–0987 connect
the centered taskbar, `pw_shell_start.c` and `pw_shell_explorer.h` to existing
Windows controls and shell APIs. Wine owns window membership, foreground state,
browser history, selection, clipboard, file operations and process launch. Start
owns only a bounded local application snapshot and its HKCU pin preference;
Explorer retains `IExplorerBrowser` and its real shell view. There is no browser
frontend or second filesystem/window registry.

`themes/windows11/appearance.json` supplies shared colours and layout metrics;
`pw_shell_style.h` reads the managed theme and current system menu font, with
system-colour fallback for alternate themes. Theme notifications refresh native
controls while retaining explicitly application-set list-view backgrounds.
Owned Fluent/original icon resources and Proton UI remain the shared visual
system. Taskbar buttons query same-process window icons, but cache icons from
foreign executable images because Wine cannot marshal foreign `WM_GETICON`.
Dynamic foreign per-window icon changes are not implemented. The shell batch
passes the exact-source 58-case gate; [Mihawk OS](MIHAWK_OS.md#everyday-shell-batch)
records the focused evidence, final visual review and remaining proof limits.

`PROTON_LAYOUT=launcher` is the production default for every executable name.
The title owns wineserver; each Windows client has its own process/address space.
Requests from descendants return to that owner, while Wine retains immediate
Windows parenthood and inherited handles.

The owner requests payload creation through websrv/elfldr. A unique rendezvous
transfers the existing packet control socket; the loader acknowledges holding it
before the owner releases its copy. Every unix socket handed over by SCM_RIGHTS
follows that rule, because the console's kernel flushes a socket whose every
reference is in flight (`wine/ps5/pw_payload_rendezvous.h`). Descriptor 3 remains control;
the wineserver connection, GPU channel and input/audio transports are distinct.
Loader/runtime protocol checks refuse mismatches. EOF on control ends the payload.
Native-PID disappearance and completed GPU work gate resource reclamation.

The loader separates optional memory/performance telemetry from its
native owner-lifetime supervisor. Telemetry runs on a detached thread and is
never joined during shutdown. The supervisor observes only control hangup/error;
Wine's control agent owns the packets. Polling readable data without consuming
it made the previous observer spin on a queued packet. A host SOCK_SEQPACKET
regression preserves data for the consumer while testing bounded waits and
hangup with unread data.

The owner's `payload_close` consumes its control endpoint before awaiting
native disappearance. On the console, shutdown alone could leave unread data
without the hangup needed by the peer supervisor. All callers transfer descriptor
ownership exactly once; ESRCH remains required within the existing timeout.
The runner rejects a native-close timeout even when Windows reports PASS.
Optional direct-file lifecycle records locate shutdown, endpoint release,
exit-message and native-absence phases without depending on stderr locks.

Qualification includes a deliberately held telemetry worker, eight children
ready/alive together followed by normal exits, descendant spawning and graphics
teardown. Candidate12 passes the complete 59-case exact-source gate in 1,893.9 s;
all 591 recorded native closes finish within 305 ms, with no close timeout or
recorded fatal/protocol fault. The owned Appearance app also uses the current
system brush at paint time, without awaiting asynchronous theme notification.
See [PLAN.md](PLAN.md) for evidence and prior failures. The passing gate does not
establish the exact cause of the earlier console outage.

The process/GPU/frame tables have 32 slots. Console evidence proves eight
simultaneous Windows children; memory and bootstrap helpers also consume resources.
`title` and `single` layouts remain diagnostics.

**Hybrid payload + elevated Sony local-process execution (2026-11-26).**
In persistent store-client sessions (`WINE_PS5_LOCAL_GAMES=1`), the title registers
a local spawner (`pw_ps5_set_local_spawner`) backed by `/app0/wine-local-child.bin`
(`native/proton_local_child.c`, `native/pw_local_child.h`, `tools/build-local-child.sh`),
a headless SELF importing only `libkernel.sprx` and `libSceLibcInternal.sprx`.
The title keeps one pre-warmed elevated local child ready (`25–30 ms` warm spawn):
on entry, the local child elevates through the resident Lapy daemon (waiting for
`geteuid() == 0` with private credentials so JIT shared memory is permitted),
sends a `PW_LOCAL_HELLO_MAGIC` handshake on fd 3, loads `ntdll.prx` via
`sceKernelLoadStartModule` (`~5–6 ms`), and runs the shared `proton_wine_child.c`
loop. 64-bit graphics processes (games and the store client's browser) route to
these elevated local processes so they draw 100% of their page tables from the
title's expanded **65,536 CPU / 32,768 GPU** application page-table pools
(`ps5/sce_sys/param.json`: `cpuPageTableSize = 268435456`,
`gpuPageTableSize = 134217728`, leaving `11,456 MiB` direct memory) and `1 GiB`
flexible pool instead of the 8,192-entry system pool, while 32-bit programs and
Wine services remain in `elfldr` payload helpers. On exit or crash,
`sceSystemServiceKillLocalProcess` reclaims 100% of the local process's page
tables and flexible memory without restarting the title or closing the store
client. Direct-launched titles outside the store client keep their existing
payload and in-title routing unchanged.

## 32-bit Windows execution

PE32 x86 programs execute natively in CPU compatibility mode inside payloads,
using Wine's WoW64 and i386 Windows modules. The low-address TEB, image placement,
signal transitions and allocations follow [IA32_DESIGN.md](IA32_DESIGN.md).
WoW64 starts 32-bit children through the same owner/bootstrap contract. Every
32-bit-required program uses the launcher layout; a diagnostic title layout
cannot replace the payload address-space contract.

The i386 graphics DLLs use the same broker. Mapped GPU memory stays below 4 GiB;
i686 Mesa/Zink and i386 DXVK/VKD3D modules are staged in `syswow64`, alongside the
32-bit HID drivers. Patch 0955 widens the internal hardware request's nested
`INPUT32`, using the same converter as public `SendInput`. The exact integer
interpreter remains a diagnostic fallback, not the game execution path.

## Memory and shared ownership

The payload imports `sceKernelAllocateDirectMemoryForApp` from `libkernel_sys`
and supplies it to Wine through `__wine_ps5_set_payload_direct_memory`. Elevated
Sony local processes (`/app0/wine-local-child.bin`) live inside the application
container and call ordinary `sceKernelAllocateDirectMemory` directly without
importing `libkernel_sys`. Wine's existing mapper backs Windows commits and the
Unix heap from the shared application direct-memory pool (**11,456 MiB** with
`cpuPageTableSize = 256 MiB` and `gpuPageTableSize = 128 MiB` in
`ps5/sce_sys/param.json`; `11,712 MiB` with default page-table sizes). Payload
helpers retain budget 0; no kernel budget patch is applied. Title PRXs do not
import the private allocator.

In the GPU broker client (`platform/graphics/patches/0003-gpu-broker-cpu-only-direct-memory-fallback.patch`),
Sony local processes without an initialized AGC GPU VM fall back from `0x33` to
CPU-only `0x03` when mapping shared direct-memory host buffers; the owning title
retains `0x33` GPU access on the same physical range.

Reservations stay virtual until committed. Commit, zeroing, decommit and release
use the direct-memory mapper. The payload SDK supplies executable-memory callbacks.
`WINE_PS5_PAYLOAD_DMEM=0` selects the old system-memory path for diagnostics;
the guard test deliberately uses it. Native modules/bootstrap and some shared
transport still consume system memory, so the memory guard remains enabled.

**Direct memory shared between processes.** A direct-memory range belongs to the process that allocated it; another process's
`sceKernelMapDirectMemory` fails with EACCES until the owner grants its pid with `sceKernelProtectDirectMemoryForPID(offset, length,
0x33, pid)` (the call the system compositor uses to share memory with games; `native/pw_dmem_share.h`). The GPU broker's host-visible
buffers use it (patch `platform/graphics/patches/0009`): the memory helper, which outlives every mapper, allocates the range from the
11,712 MiB application pool and grants the title and the client; the title maps it, the client maps the same offset at the same
address, and the reply carries the offset where it used to carry a descriptor. On free the client unmaps first, the broker unmaps and
the helper releases the range. None of it is charged to the title's 1 GiB flexible pool, which shared memory (physhm) was
(docs/PLAN.md, B3). `PROTON_GPU_HOST_DMEM=0` returns to shared memory. Wine's own sections and the wineserver's shared pages still
use the flexible pool; the same route could carry them.

Shared pages must not remain charged to a title Sony has already deleted.
Helpers create their input/audio objects. A dedicated memory payload creates and
touches wineserver shared sections/session memory and broker host-visible buffers
before the title uses them. This ownership change removed the reproduced
PS-menu-close leak/panic in the tested sequences.

On the shared-prefix branch, patch 0962 adds demand mapping for aligned large
shared file views. Windows view and page-protection metadata stay in ntdll;
`wine/ps5/pw_wine_section` owns retained descriptors and a bounded cache of real
shared file pages. Faults are serialized by Wine's virtual mutex. Eviction
flushes writable pages, unmaps them and restores no-overwrite guards. Unmapping
removes all cached pages before releasing a descriptor. Unconstrained PE64 views
can use an internal 64 GiB virtual arena outside Wine's generic free reservations;
fixed views retain their requested addresses. The mapped cache is capped at
64 MiB per client; this is not a physical memory capacity claim.

When native shared-object sizing fails, the memory payload creates a sparse
linked file through `src/pw_memory_file`. Shared file-lock leases survive descriptor
duplication and SCM_RIGHTS. Collection takes a separate exclusive nonblocking
lease only after the last user closes, flushes/closes while linked, then unlinks.
A file still in use at owner closure remains linked for later collection. The
server propagates helper refusal rather than creating title-owned shared pages.
Native concurrent/lease tests and a Windows 13.5 GiB section probe pass on console;
the complete 26-case exact-source regression gate passes. Actual emulator/game
acceptance remains pending.

The pending memory-only route separates backing contracts explicitly.
`pw_memory_alloc` retains ordinary section descriptors for byte I/O and
copy-on-write views. `pw_memory_alloc_shared` prefers payload-created physical
objects for broker buffers and census; input/audio/frame pages use the same
native creator. PRXs receive a frame allocator callback and never import the
private physhm calls. Physical refusal retains ordinary backing. Native tests
qualify zeroed shared aliases, growth, SCM_RIGHTS, fixed offset mapping and
last-map lifetime; they also prove byte I/O, private maps and shrinking are
unsupported. The pages consume payload-visible flexible backing, not ForApp
DMEM. This route is deployed but production qualification is blocked by a
stopped loader child; see [PLAN.md](PLAN.md). It is not a launcher capacity claim.

Cross-process ReadProcessMemory/WriteProcessMemory still use the memory service.
Its owned pipe writer is nonblocking: short capacity writes are drained and
continued, while inaccessible pages return the completed prefix. This removes
the reproduced host deadlock when a 16 KiB copy piece encounters an 8 KiB pipe;
the permanent sanitized regression forces 4 KiB capacity.

## Graphics, display and desktop

Each helper keeps its own Vulkan/RADV client state. The title alone owns native
GPU mappings/submissions and VideoOut. Broker packets carry allocation, submission,
completion and presentation requests; the owner displays completed client images.
Ordinary Windows desktop pictures use a frame-page path into the compositor.
Foreground-window state selects presentation and pointer routing. Production presents the full
1920x1080 desktop (`WINE_PS5_VIEW=desktop`) and preserves application-selected
window sizes; `window` remains an explicit crop diagnostic.

- DirectX 9/10/11 → DXVK → winevulkan → brokered RADV.
- DirectX 12 → VKD3D-Proton → winevulkan → brokered RADV.
- OpenGL/WGL → architecture-matched PE64/PE32 Mesa/Zink → winevulkan → brokered RADV.

On `mihawk-os`, `WINE_PS5_WINDOW_SURFACES=1` selects independent retained
HWND composition. Wine publishes the authoritative lifetime/order/geometry;
helper-owned pages carry stable GDI pixels, while broker-completed GPU planes
carry their matching HWND identities. The shape/alpha candidate uses snapshot
version 3 and page version 2. Whole-window shapes are independent of GPU-client
clips. Opaque pages copy directly; declared premultiplied pages blend once with
retained lower windows. A format change uses acknowledged page replacement.

For layered top-level GDI windows, Wine's own surface conversion supplies
colour-key/per-pixel coverage and constant opacity. Binary input coverage is
published separately to the owning server HWND generation. The existing server
hit-test uses it without changing application-visible regions. Storage is
bounded and freed at destruction; layered-style changes reset driver opacity.
Layered GPU/child surfaces remain explicitly unsupported. The exact-source gate
status for this candidate is in [PLAN.md](PLAN.md); focused passes alone do not
qualify a code commit or prove blur, shadows, thumbnails or frame pacing.

DWM surfaces (snapshot v4, patch 0992): `dwmapi` validates corner preference,
border colour, system backdrop, dark material and frame margins and sends them
through one private win32u call to server window state; only the window's own
process may set them. Thumbnails are bounded server objects owned by their
destination HWND. The server resolves Windows 11 policy (frames round at 8 px,
standard menus at 4 px, maximized/regioned/layered windows never) and exports
radius, shadow class, border, glass area and visible thumbnails. Everything is
visual: regions, hit-testing and window state stay Wine's. The compositor
(`src/pw_window_effects.c`) applies supersampled corners, border rings,
shadows, Mica from the retained desktop root, acrylic from the frame beneath,
and area-scaled thumbnails from retained source images, with bounded caches in
the broker's display thread. Glass interprets alpha only in the client area of
a window that selected a backdrop. uxtheme's buffered-paint alpha and
composited text (patch 0993) are the Win32 recipe for content over glass; the
Mihawk taskbar (Mica, live previews) and Start (acrylic) use it (patch 0994,
`pw_shell_style.h`).

The owner/broker layout defaults to Zink and helper audio. On the shared-prefix
baseline, ntdll provisions Mesa and the other runtime-owned DLLs after wineboot
and before application imports. Fresh PE32/PE64 prefixes passed their first-launch
checks; see [SHARED_PREFIX.md](SHARED_PREFIX.md). DirectWrite's upstream FreeType
backend is packaged as `dwrite.prx`, with native and WoW64 call tables.
The old title-layout EGL/OpenGL implementation remains diagnostic history.

The loading display stays live until the game's presenter opens VideoOut.
On broker failure, the owner reports device loss and avoids releasing memory
whose GPU lifetime cannot be established. A successful return alone does not
prove another homebrew can launch; test that separately when changing teardown.

## Input, sound and network

The title polls the DualSense. Shared pages deliver state and HID reports to
helpers; process-specific device identities prevent stale controller interfaces.
The touchpad injects pointer motion/clicks into the shared Wine desktop, routed
to its foreground or hit-tested window. Rumble is not a verified feature.

On the shared-prefix branch, launcher Settings → Controls persists global mouse-button mappings
through `src/pw_controls`. Each owner loads them before its input loop. The shared
mapper emits left/right/middle press and release events, consumes mapped buttons
before both XInput and HID publication, and releases held clicks on disconnect or
shell interception. Touch motion remains independent of the selected click button.

Each helper writes 48 kHz stereo grains into its audio ring. The title mixes and
plays them through AudioOut. A .NET (MonoGame) game's manual-launch music is human-confirmed;
that does not prove arbitrary concurrent application audio.

Only the title loads the console resolver library. Helpers request Winsock
lookups and the actual configured IPv4/IPv6 DNS servers over control. On the
shared-prefix branch, DNSAPI's native and WoW64 Unix tables use pinned c-ares for real DNS
packets; Wine retains record parsing and Windows status mapping. Each query owns
its resolver channel and cancels after a bounded deadline. No configured server
addresses or synthetic DNS records are stored in profiles. Paired console
qualification and the full gate are tracked in PLAN.md. NSI's upstream FreeBSD
backend enumerates actual interfaces using Sony's `getifaddrs`, with missing
interface-name APIs supplied by the shared libc adapter. Normal payload launches
start the Windows service manager; explicit diagnostic suppression remains.
Winsock uses the response and ordinary sockets. GnuTLS-backed Windows
TLS and the runtime's pinned certificate roots support strict HTTPS. The console
clock must be correct for certificate validity checks.

## Storage and pinned graphics exports

`/data/ps5proton` contains runtime versions, user-supplied games, prefixes,
caches and logs. On `mihawk-os` every game lives in Mihawk OS: its files on the
`mihawk` prefix's C: drive (`C:\Games\<name>`), its library entry in
`games/<id>/game.json`, and it runs in `prefixes/mihawk` unless `game.json` names
another prefix ID (the test harnesses' throwaway prefixes). The per-game prefixes
were moved in by a one-time migration tool (since removed) on 2026-10-10 and retired to
`retired-prefixes/`. Inside the desktop session each game keeps its own
`game.json` settings (`src/pw_app_environment.h`) and has a desktop shortcut
(`src/pw_shell_link.h`). Bare diagnostics retain `prefixes/probe`. After elevation `/app0` may disappear; resolve the title's
install folder through the platform helpers rather than assuming `/app0` exists.
Game files and saves are never repository assets.

`tools/setup-graphics-foundation.sh` exports PS5_Vulkan
`87a48676dda0977d5ba8ff1ddcf02816d41c443b`. Its platform header comes from Mesa
`7b59ef27c1b09b9671bc4153c41940c3155c3af2`; broker overlays use
`f7ce91ac4c3697990bf89d79fdc61ae7cb98570b`, and WGL/Zink uses
`b3588f78dd7fd46fbadc7ecd487e5f14d4213e4b`. The scripts and manifests are
the source of truth for pins. Exports live in ignored caches; never hand-edit
a dependency checkout or invent a RADV link recipe.
