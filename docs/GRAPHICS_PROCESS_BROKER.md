# Graphics process broker

Current guide, updated 2026-11-26. The GPU broker is the
production path for both `elfldr` payload helpers and elevated Sony local-process
helpers (`/app0/wine-local-child.bin`). The earlier native experiments, failed
direct-GPU attempts, throughput measurements and overlay development are in
[the graphics history](history/2026-10-07/docs/GRAPHICS_PROCESS_BROKER.md).

## Mechanism

The title owns the console GPU context and display. Each Windows child process
(payload helper or Sony local process) keeps its own Wine/Vulkan/RADV client
state and asks the broker to allocate GPU resources, submit work, wait for
completion and present frames. This avoids requiring each child to initialize a
separate native Sony GPU context.

For Sony local-process clients (`/app0/wine-local-child.bin`), which run as
`native_game` without an initialized AGC GPU VM,
`platform/graphics/patches/0003-gpu-broker-cpu-only-direct-memory-fallback.patch`
falls back from `0x33` to CPU-only `0x03` in `tooling/gpu-broker/client.c` when
mapping shared direct-memory host buffers; the owning title (`eboot.bin`) keeps
`0x33` GPU access on the same physical range.

The owner displays completed child images through VideoOut. Desktop/window frames
also reach its compositor. Foreground-window state directs presentation and pointer
input. The loading screen remains visible until a game presenter opens the display.

DirectX uses DXVK/VKD3D-Proton over winevulkan. OpenGL defaults to Mesa/Zink over
Vulkan. A child does not need a title-specific executable-name rule to use graphics.

## Foundation extraction, 2026-10-06

PS5_Vulkan owns `tooling/gpu-broker` and the optional PE64 WGL/Zink builder.
PS5_Mesa owns the ordered RADV broker overlays and the Zink presentation/buffer
fixes. Proton retains the bootstrap, Wine integration, loader exports, profiles,
DLL staging, Windows desktop callbacks and native-lifetime checks.

`tools/setup-graphics-foundation.sh` exports pinned sources into ignored caches.
The RADV client is linked by the foundation's canonical recipe. Do not duplicate
the broker here, hand-link RADV or modify sibling repositories during Proton work.
Pins and ownership are in [ARCHITECTURE.md](ARCHITECTURE.md) and
[NOTICE.md](../NOTICE.md).

## What has been proved

| Scope | Console evidence |
| --- | --- |
| Native clients | Separate helpers initialized/recreated Vulkan devices, cleared/drew images and verified complete readbacks |
| Owner display | Child images reached VideoOut with confirmed flips and scanout pixel checks |
| Windows clients | Two DXVK children plus two rendering descendants; correct pixels, input and native cleanup |
| Zink | Offscreen shader/vertex probe reported OpenGL 4.6; visible presentation passed; a .NET (MonoGame) game used the production default |
| Audio/input | Helper controller state and HID reports; desktop pointer/click gate; a .NET (MonoGame) game's human input and audible music |
| Memory/closure | Application-backed Windows memory plus shell closure with a live 2 GiB graphics helper passed |
| Local-process clients (2026-11-26) | Elevated Sony local-process 64-bit games rendered `7,636` direct flips at `119.9 FPS` (`p3-local-village-2026-11-26-02.json`), `4,802` direct frames at `117.7 FPS` (`1.99 GiB` VRAM + `1.14 GiB` host DMEM, `p3-local-re3demo-2026-11-26-01.json`), and `8,823` total flips across two consecutive launches + live browser return (`p3-e2e-spelunky2-twice-2026-11-26-01.json`), all with `dmem_host refused=0 free_failed=0` |

Representative records are [payload-graphics-10](../evidence/payload-graphics-10.json),
[manual game audio](../evidence/manual-default-audio-2026-10-06-02.json),
[latest rendering gate](../evidence/gate-payload-forapp-2026-10-07-02-launcher-render.json)
and [latest shell closure](../evidence/payload-forapp-shell-close-2026-10-07-01-validation.json).
The four-renderer test is a synthetic workload, separate from the eight-child
capacity test. It is not proof of four arbitrary games running concurrently.

## Memory, synchronization and cleanup

Host-visible shared buffers must use compatible addresses in owner and client.
The broker reserves ranges, tracks allocation/reuse and waits for GPU completion.
Device-local allocations stay under owner GPU control; persistently mapped Zink
buffers remain host-visible so CPU descriptor writes reach the actual GPU backing.

A dedicated memory payload creates/touches shared broker objects before owner use.
Helpers own their input/audio shared objects. This prevents those shared pages
from remaining charged to a deleted title in the reproduced shell-close sequence.
Application-memory support in Windows helpers does not imply every GPU/shared
allocation has moved into the application pool.

Native-PID absence and completed fences gate reclamation. If GPU completion cannot
be established, report device loss and retain uncertain memory until replacement
instead of freeing in-flight resources. An injected submission failure is a
diagnostic check; it is not evidence for all hardware failure recovery.

## Remaining qualification

Longer multi-application graphics/audio sessions, current-build DirectX 9/10/12,
full operation of a game launcher's browser and games, and a Java launcher's chain into a Java game remain open.
An OpenGL 4.6 version string is not a complete OpenGL conformance pass. Rumble and
fresh-prefix first-launch Zink staging are unproved. Use the production gate and
application acceptance sequence in [PLAN.md](PLAN.md).
