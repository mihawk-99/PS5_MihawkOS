# Optimization ledger

Reviewed 2026-11-26. This index distinguishes retained improvements from current
performance claims. Full hypotheses, code changes, failed experiments and measured
rounds R1–R7 remain in [the original ledger](history/2026-10-07/docs/OPTIMIZATION_LEDGER.md).

## Retained work and evidence limits

| Work | Accomplishment | Limit |
| --- | --- | --- |
| Persistent store client & elevated local processes (2026-11-26) | Eliminated close/reopen `LoadExec` handovers; pre-warmed elevated `/app0/wine-local-child.bin` spawns in `25–30 ms` (`ntdll.prx` kernel load `~5–6 ms`); expanded `65,536` CPU / `32,768` GPU app PT pool leaves `>62,300/65,536` CPU PT and `>3,700/8,192` system PT free across heavy 64-bit games (`117.7–122.7 FPS`); faster store-client file/xattr/server paths (`2485`, `2486`) | Measured on base PS5 (13.60); PS5 Pro (10.01) and full `tools/gate.py run` battery pending |
| Measurement/tooling | Frame/server/memory counters, repeatable game/probe runs and preserved verdicts | Old single-title counters do not automatically describe every helper |
| Exit robustness | Threads terminate at the user-mode boundary; durable registry saves and normal-close fixes | Power-loss recovery is not universally proven |
| Synchronization | PS5 in-process synchronization implementation and focused correctness/closure tests | Kernel/platform differences still need suite-specific validation |
| Thread alerts | Descriptor-free alert path avoids the historical per-thread kqueue exhaustion | Not a claim of unlimited files/threads |
| Scheduling | Windows-to-native priority support and bounded tests | Strict-priority starvation remains possible; opt-ins need workload evidence |
| Server dispatch | Shared direct client-thread requests adopted from prospero-win | Preserve synchronization correctness while measuring |
| CPU reporting | Show the tested usable CPU count rather than sizing guest pools for unavailable CPUs | Earlier Java-game benchmark, not a current payload performance guarantee |
| Graphics foundation | Reusable broker and default Zink; verified pixels and a .NET (MonoGame) game | Broader APIs/concurrency and complete game acceptance remain open |
| Payload memory | Windows commits/Unix heap use the shared application pool; 4 GiB across two helpers verified | Pool is shared; native/shared transport still uses system memory |

## Earlier performance measurements

A .NET (MonoGame) game's old menu path improved from about 5 FPS to about 60 FPS after GPU
presentation changes. A later 30-minute title-layout session held about 59.9 FPS.
These measurements predate the current payload/Zink implementation.

A Java game's earlier Vulkan generated-area intervals reached 113–118 FPS, with
113–120 FPS in other bounded runs. In matched copied-world walking tests, reporting
13 usable CPUs changed mean FPS from 77.6 to 86.7 and p99 frame time from 45.6 to
17.8 ms. Configuration, terrain and architecture belong to those dated runs.

The new allocator's system-memory peak was 721 MiB for a game launcher's client and 246 MiB for a .NET (MonoGame) game,
with its helper using up to 1,024 MiB of application backing. These are capacity
and pressure measurements, **not a controlled FPS or loading-speed improvement**.

## Next measurement rules

Compare the same build/configuration/scene and fresh copied state, record both
system and application memory, and separate startup, steady rendering and teardown.
Keep correctness/cleanup checks alongside FPS. Do not infer performance from a
successful allocation, a new API route or a version string.

Current qualification order is in [PLAN.md](PLAN.md); current process measurements
are in [WINDOWS_PROCESSES.md](WINDOWS_PROCESSES.md).
