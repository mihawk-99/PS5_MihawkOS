# The flexible-memory wall

> [!IMPORTANT]
> **2026-11-26 update (branch `fast-steam`, standard PS5 13.60):** The 2026-10-25
> finding below that *"on 13.60, a Sony local process cannot execute anything (JIT
> create refused, `0x80020001 EPERM`)"* was measured on an **un-elevated** local
> process. When a Sony local process elevates through the resident Lapy daemon
> (waiting for `geteuid() == 0` with private credentials before touching memory),
> `sceKernelJitCreateSharedMemory` succeeds with full execute permissions even past
> 65 MiB (`evidence/sony-local-elev-pt-probe-02.json`: `euid=0`, `jit_made=0`,
> `jit80_made=0`, `jit80_mapped=0`, `jit80_protect=0`, `jit80_value=42`,
> `way=jit prot=5 value=42`). Combined with `kernel.cpuPageTableSize = 268435456`
> (**65,536 CPU PT**) and `kernel.gpuPageTableSize = 134217728` (**32,768 GPU PT**)
> in `ps5/sce_sys/param.json`, **route 1b is now proven and shipped on `fast-steam`**
> as `/app0/wine-local-child.bin` for 64-bit Steam games and `steamwebhelper.exe`:
> their JIT code, flexible memory, and page tables draw from the title's 1 GiB
> flexible pool and 65,536-entry CPU page-table pool (`0` system PT, 100% OS-level
> reclamation on `sceSystemServiceKillLocalProcess`, verified across three 64-bit
> Steam games in `evidence/p3-local-*.json` and `evidence/p3-e2e-spelunky2-twice-2026-11-26-01.json`).
> The 2026-10-25..27 investigation below is preserved intact as historical context.

The owner's question (2026-10-25): "Even if we pass all Wine compliance tests, 1GiB flexible memory is still a wall."
Then: bypass it entirely, without hvctl and without reading or writing kernel or hypervisor memory. This document says
what the wall is, measured, and which routes past it remain. Records: `evidence/flex-sony-base-2026-10-25-01.json`,
`-02` (this round, the base PS5, firmware 13.60 installed: the title's own log of the same day's runs; the console
was on 13.40 for the 2026-10-21 records), and the earlier records each line names.

## The answer in short

- **There is no single 1 GiB.** There are two flexible pools, and each is capped by the system, not by us:
  - the **system pool**: every payload helper is charged here;
  - the **title's partition**: the title and its Sony local processes are charged here; param.json caps it at 1 GiB.
- **Moving memory between them is not possible from our side.** The title's slider gives its flexible memory to the
  direct pool, never to the system pool.
- **Everything except executable code can already live in direct memory**, which neither pool counts.
- **Executable code cannot, on 13.60.** There, the only memory that executes, in the title as in a payload, is JIT
  shared memory, and JIT memory is flexible (measured below). A tester's Pro at 13.60 answers the same, so the model
  does not change it. On the Pro at 10.01 it is not measured; `wine/ps5/pw_wine_dmem_ps5.c` records that a title
  executes direct memory there.
- So on 13.60 the wall cannot be removed. What can be done, on every firmware, is to make each process's flexible use a fixed budget, not a
  function of the workload. That takes two parts:
  - a bounded code cache: converted code clusters are evicted and converted again at their next execute;
  - helpers that keep all data in direct memory.

## The two pools (measured)

| Pool | Who draws on it | Its size | Evidence |
| --- | --- | --- | --- |
| System | Every payload helper (they show as `SceSpZeroConf` in SceShellCore's FMEM lines), the memory helper, and the console's own processes (142-272 MiB on the Pro, ~1200 MiB on the base PS5) | Pro: printed limit ~1039 MiB (`VM Stats: RSS x 1039`); the kernel starts trimming other processes at ~780-840 MiB of physical pages. Base 13.40: no printed limit; the census ran to 1700 MiB with swap flat | `pro-charge-*`, `shared-charge-*`, guard header |
| Title's partition | The title, and Sony local processes (`sceApplicationAddProcess ... budget=[game]` in the klog) | `kernel.flexibleMemorySize`, at most 1 GiB; the title uses ~78 MiB of it ("flexible 946 MiB free") | `flex512`/`bit22` 2026-10-24; `flexible-memory-param-*` 2026-10-05; this round |

- **The slider does not feed the helpers.** `flex512-2026-10-24-01` against `bit22-try-2026-10-24-01`, same 2D game
  start on the Pro: with the title at 512 MiB, the helpers held 133 MiB in 7 processes, exactly as at 1 GiB, and
  `VM Stats` still read a limit of 1039.3.
- **A Sony local process is charged to the title.** `flex-sony-base-2026-10-25-02`: one local process mapped and wrote
  64 MiB of anonymous memory.
  - The title's free flexible memory went 1,032,847,360 -> 962,592,768 bytes and came back exactly after the close.
  - The system's `VM Stats` RSS moved 1217 -> 1224 MiB, about 7 MiB: the process itself, not the 64 MiB.
  - The same process also mapped and wrote 64 MiB of direct memory: neither figure moved.
- **Direct memory is not counted** in either pool (`memcap-heap-2026-10-12-01`; this round).

## What executes (base PS5, 13.60, measured)

`flex-sony-base-2026-10-25-02` tried six ways to an executable page holding `mov eax, 42; ret`, in a Sony local process
and in the title itself (`native/proton_flex_probe.h`). The code was run only where the kernel's query reported
execute.

| Way | Sony local process | Title |
| --- | --- | --- |
| Direct memory, type 0, `sceKernelMprotect(R+X)` | answers 0, query reads 1 (read only) | the same |
| Direct memory, type 12 | answers 0, reads 1 | the same |
| `sceKernelAllocateMainDirectMemory`, type 12 (the title backend's own call) | answers 0, reads 1 | the same |
| Direct memory mapped R+W+X at once | refused, 0x80020016 | refused, 0x80020016 |
| Anonymous memory, `mprotect(R+X)` | answers 0, reads 1 | the same |
| JIT shared memory | create refused, 0x80020001 (EPERM) | protection 5, **ran: 42** |

- On 13.60, a Sony local process cannot execute anything, so it cannot run Windows code there.
- On 13.60, the title runs code only from JIT memory. `wine/ps5/pw_wine_dmem_ps5.c` says code runs from "direct
  memory in a title", which is not true on 13.60. It may be true on 10.01, which is not measured; the difference may
  be a firmware change.
- A payload is the same as the title here: JIT memory only (`exec-kernel-2026-10-14`, K8).

## What a Steam session holds, by kind (the Pro, `fin1-2026-10-24-01`, at 100 s)

Census 661 MiB: the console's own 151 MiB, helpers 510 MiB in 8. The kernel's map, by type, at the helper's last
sample (`WINE_PS5_VMMAP=1`; type 4, direct memory, is not counted):

| Helper | Kernel count | Default anonymous (type 1) | JIT code (type 3) | Module text and data (type 9) | Other |
| --- | --- | --- | --- | --- | --- |
| Web helper's GPU process | 269-273 MiB | 120 MiB: two 25 MiB sections, Sony libc's heap 29 MiB (6 in use), stacks and the rest | 104 MiB | 32 MiB | file 8, type 11: 16 |
| `steam.exe` | 157 MiB | 93 MiB (libc 29, 3 in use) | 30 MiB | 32 MiB | 1 |
| Six small helpers | 13-16 MiB each | | | | |

What could move to direct memory, and what did (measured in "Non-code memory in direct memory" below):
- sections of 8 MiB and more: moved;
- the unused part of Sony libc's heap: it was our loader's file buffer, now direct memory;
- modules' read-only and writable data: moved;
- thread stacks: not moved.

What cannot move is the code: 104 + 30 MiB here, and 162 MB converted across the 2D game's session (`fin2`).

## Routes, ranked

| # | Route | What it takes off a pool | Cost | Status |
| --- | --- | --- | --- | --- |
| 1 | **Bounded code cache**: patch 1127's converted clusters are evicted, oldest first, once a process's converted code passes a budget; the next execute converts them again (the bytes come from the direct-memory copy the eviction leaves) | Code becomes a fixed budget per process: e.g. 104 -> 32 MiB in the web helper's GPU process, 30 -> 16 in `steam.exe` | A re-conversion fault per evicted cluster that runs again; memory-backend code, the project's riskiest | **On by default** (2026-10-25, the owner's rule): 64 MiB fixed for Steam and launchers, adaptive from 64 MiB for games (below) |
| 2 | **Direct-only data in helpers**: sections, libc heap slack, module data, stacks | Steam in Big Picture: helpers 467 -> 267 MiB in 7 (below); stacks not moved | Small | **Done** except stacks (`fmem-bp-*-2026-10-2[56]`) |
| 3 | **The game in the title** (`PROTON_LAYOUT=title`): its code and flexible memory go to the title's partition, ~950 MiB free | The game's whole share (the 2D game's helper: 61 MiB census plus its code) leaves the system pool | One process per title session; on 13.60 the title backend expects direct memory to execute, which it does not (above), so that path needs JIT conversion in the title first | Not run on 13.60 |
| 4 | **Title-made JIT for helpers' code**: payloads map JIT objects the title makes, so code is charged to the title's partition | All code off the system pool | 2026-10-06: memory the title made and a helper still held when the system closed the title **panicked the console** (`src/pw_shared_page.h`) | Not pursued without a safe close protocol |
| 1b | **Elevated Sony local processes (`/app0/wine-local-child.bin`) running JIT code in the title's partition** | Their JIT code, flexible memory, and page tables leave the system pool and draw from the title's 1 GiB flexible pool and expanded 65,536-entry CPU PT pool (`0` system PT), with 100% OS-level reclamation on close | Lapy elevation handshake before memory mapping; GPU broker CPU-only `0x03` fallback (`0003`) | **Proven & shipped on `fast-steam` (2026-11-26, base PS5 13.60)**: `sony-local-elev-pt-probe-02.json` (`jit80_value=42`, `65,513/65,536` PT reclaimed) + 3 real 64-bit Steam games (`p3-local-*`, `p3-e2e-spelunky2-twice-2026-11-26-01.json`). Earlier 2026-10-25 negative was un-elevated. **Pro 10.01: pending** |
| - | The slider | - | - | **Measured negative**: it only trades the title's flexible memory for direct |
| - | hvctl, kernel budget writes, bare-title `rfork` | - | - | Excluded: the owner's condition; a known panic |

## Design: the bounded code cache (route 1)

Where it lives: `wine/ps5/pw_wine_dmem_ps5.c`, beside `exec_convert`, which already runs under Wine's virtual mutex at
each first execute.

- **Record.** Each conversion appends its cluster `{address, bytes}` to a ring and adds its bytes to a total.
- **Evict.** When the total passes the budget ("The code budget's default" below; `WINE_PS5_CODE_BUDGET_MIB` overrides it, 0 turns it off), the oldest
  entries are evicted before the new cluster is converted. An entry is evicted only if the kernel still reports it
  executable and the engine still records it as the caller's mapping. Stale entries are dropped; they only make the
  total an overestimate.
- **Swap without a wrong byte.** No thread may ever read wrong code bytes; MSVC places jump tables in `.text`.
  1. The cluster is made read-only, so writers and executors fault and wait on the virtual mutex.
  2. Its bytes are copied into fresh direct memory through a scratch alias.
  3. The direct memory is mapped over the cluster in one call, with the cluster's protection minus execute, and the
     engine records it as backed.
  4. A shared copy (`pw_wine_codeshare`) is only unmapped here; the registry's object stays for the other helpers.
  - Readers see the same bytes throughout.
- **Convert again.** Wine's view still says execute, so the next execute faults into `exec_convert`, as a first
  execute does today. A return address into an evicted cluster faults the same way.
- **Measure first.** Add a counter for re-conversions per cluster, then measure the hot set: after one full flush,
  how much is converted again in 10 s and in 60 s, in Steam, Big Picture and the 2D game. The budget comes from that
  measurement, not a guess.
- **Host tests.** The fake kernel (`tests/fake_ps5_kernel.c`) gets JIT objects that evict, and a replay where a
  second thread reads the cluster during the swap and must see the same bytes.

## The code budget's default (`wine/ps5/pw_wine_code_budget.h`, the owner's rule of 2026-10-25)

| Program | Budget | Why |
| --- | --- | --- |
| `WINE_PS5_CODE_BUDGET_MIB=N` set | N MiB, fixed, for any program (0: off) | the user's setting wins |
| Steam and the common launchers, by image name | 64 MiB to start, raised as a game's is on real thrash (since 2026-10-27, the owner's word) | their code is large and mostly cold, and cold churn comes back once and raises nothing; Big Picture drawing in one process (the Pro) thrashed it: see "Big Picture's stutter" below |
| every other program (games) | 64 MiB to start, raised 32 MiB at a time up to 256 MiB whenever 1 MiB of evicted code comes back **a second time or more** within 10 s; never lowered | the 2D game at 16 MiB re-converted ~2 MB per 10 s and its worst p99 went from 18 to 33 ms (`fmem-isaac-b16-2026-10-25-01`); at 64 MiB it evicted nothing |

- The list, one line per client, with its reason beside it: `pw_code_budget_listed` in
  `wine/ps5/pw_wine_code_budget.c`.
  - Steam: `steam.exe`, `steamwebhelper.exe`, `steamservice.exe`, `steamerrorreporter.exe`.
  - Epic: `EpicGamesLauncher.exe`, `EpicWebHelper.exe`.
  - One more launcher: its program and its CEF helper (two names, in the code's list).
  - GOG Galaxy: `GalaxyClient.exe`, `GalaxyClient Helper.exe`, `GalaxyCommunication.exe`.
  - The EA app: `EADesktop.exe`, `EABackgroundService.exe`, `QtWebEngineProcess.exe`.
  - Ubisoft Connect: `upc.exe`, `UbisoftConnect.exe`, `UplayWebCore.exe`.
  - Rockstar: `RockstarService.exe`, `SocialClubHelper.exe`.
  - Embedded browsers: `CefSharp.BrowserSubprocess.exe`, `msedgewebview2.exe`.
- Names too generic to be only a launcher's are left out (Rockstar's `Launcher.exe`, another launcher's `Agent.exe`): a game
  of that name would be held to 64 MiB.
- A re-conversion is a conversion of a cluster the cache gave back. The last 4,096 evicted addresses are remembered,
  one slot per hash, so a collision only undercounts.
- Only a cluster's second return or later counts toward a raise (2026-10-27, branch `fmem-budget-retune`). A first
  return is a phase change, the level's code coming back after its loading code; a cycle comes back again. The
  Direct3D 12 game was raised to 96 MiB by the first return of its loading code, and at a fixed 64 MiB it plays as
  with no budget (below). A thrashing game is raised one cycle later: for the 2D game at 16 MiB, an estimate of about
  35 s.
- Every raise is logged: `code budget raised to N MiB for <image>: X MiB re-converted in 10 s`.
- The helper's end line, `wine-child: code cache`, gives the policy, the budget, the raises and the re-converted KiB.
- `WINE_PS5_CODE_BUDGET_START_MIB` moves an adaptive program's first budget, for a test (the forced case below).
- Both paths on the console (`fmem-isaac-start16-2026-10-25-01`: the 2D game through Steam, no user budget, the
  game started at 16 MiB):
  - Steam's processes were `listed` at 64 MiB. The web helper evicted 16,873 clusters and re-converted 210 MB of
    them, with 0 raises.
  - The 2D game was raised once, 16 -> 48 MiB, after 1 MiB re-converted in 10 s (104 evictions). It then held 26 MiB and
    drew 59.9 fps in all 12 of its windows, worst p99 18.25 ms (33 ms at a fixed 16 MiB, 18.0-18.5 ms with no budget).
  - The same on the build the commit was gated on (`fmem-isaac-start16-2026-10-25-02`): the web helper listed at
    64 MiB (18,605 evictions, 0 raises); the 2D game raised once, 16 -> 48 MiB, 59.9 fps, worst p99 18.0 ms; 0 lost.

**The Direct3D 12 game against the budget** (base PS5, 13.60, 2026-10-26, gated build `3b4c3463`, fresh boot): the owner's
save with Continue, then the alley, 300 s, full screen at 120 Hz (direct flip), in a helper started from the desktop
in the `mihawk` prefix (docs/TITLE_CLIENT.md). Steady windows are the 17 gameplay windows of 10 s between loading and
the area change.

| Run | Budget | fps (median window) | p99, steady windows | Worst frame (loading) | Evicted | Re-converted | Raises, budget at the end | The game's code held | The game's helper peak | Helpers peak |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `fmem-re3-budget-off-2026-10-26-01` | off (`WINE_PS5_CODE_BUDGET_MIB=0`) | 116.8 (119.9) | 9.5 ms | 1047 ms | 0 | 0 | -, off | 90 MiB (all its code) | 112 MiB | 161 MiB |
| `fmem-re3-budget-default-2026-10-26-01` | default | 116.6 (119.8) | 9.5 ms | 952 ms | 135 (2.1 MiB) | 1.7 MiB | 1, 96 MiB | 92 MiB | 112 MiB | 161 MiB |
| `fmem-re3-budget-64-2026-10-26-01` | fixed 64 (`=64`) | 116.6 (119.8) | 9.5 ms | 1132 ms | 2155 (33.7 MiB) | 4.6 MiB | none, 64 MiB | 64 MiB | 90 MiB | 138 MiB |

- At a fixed 64 MiB the game does not thrash: 4.6 MiB re-converted in 300 s. Its gameplay windows draw 116.5 fps against
  116.7 with the budget off, the same p99, and the 95-119 ms frames of the area change are in all three. The system
  pool keeps 22 MiB more.
- The default raised the game to 96 MiB about once loading was done, after 1 MiB re-converted in 10 s, which at 64 MiB
  cost nothing visible. At a fixed 64 MiB its re-conversion came in two bursts (3.4 MiB before its first stats line,
  as loading ended; 1.1 MiB at the stop) with 160 KiB in the 230 s of play between: code coming back once, not
  cycling. The rule now counts only second returns (above). On the console (c4a3458 with the rule, gate
  `tune-gate-2026-10-27-01`, 102/102, fresh boot):
  - The Direct3D 12 game, default budget (`tune-re3-default-2026-10-27-01`): not raised. It stayed at 64 MiB with 4.6 MiB re-converted
    and 0 raises; its helper peaked at 90 MiB (112 under the old rule), the helpers at 142 MiB; 117.3 fps, median
    window 118.9.
  - The 2D game through Steam, started at 16 MiB (its start16 run of 2026-10-27, record kept off the tree): still raised once, to 48 MiB, but
    later, as its play began (16.7 MiB re-converted at 16 MiB). Two gameplay windows dipped before the raise (51.8
    and 54.4 fps, one p99 of 32 ms); then 60 fps at a p99 of 18.5-19.5 ms. The old rule's run
    (`fmem-isaac-start16-2026-10-25-02`, another route) held 17.5-18 ms. That is the price of the rule for a game
    that does thrash.
- The earlier helper run (`fmem-re3-helper-2026-10-26-03`, default budget, 11.8 h boot) gave the same figures: 1
  raise to 96 MiB, 135 evictions, 1.7 MiB re-converted, 94.5 MiB held, 117.2 fps.
- In the title (`fmem-re3-budget-title-2026-10-26-01`, default budget, now applied to a program there): the cache
  held 16 KiB with 0 evictions, because the game's code there never faults into conversion. The title's map in the
  same run (`fmem-re3-title-map-2026-10-26-03`, at 290 s) lists the game's image, 0x140000000-0x145ca8000, as direct
  memory (type 4) with read and execute, and 115 MiB of direct memory with execute in all. The only JIT region
  (type 5) is the self-check's. So in the title the game's code runs from direct memory: it is in neither pool, and
  the budget has nothing to bound. The game drew 118.1 fps, steady p99 9.5 ms, worst frame 199 ms; helpers peaked at
  55 MiB; the title's flexible free went 924 -> 898 MiB, which is its anonymous data (114 MiB read-write
  flexible), not code.

**After the merge** (main c4a3458, base PS5, fresh boot, 2026-10-27): the branch's figures hold.
- Steam Big Picture with the 2D game (`main-e2e-2026-10-27-03`, the route of `fmem-e2e-2026-10-26-01`): helpers 371 MiB in
  11 at their peak (356 in 9 on the branch), census peak 1,593 MiB, RSS 1,596 MiB, swap-out 1.0 -> 1.9 MiB, no swap
  notice. The game: 60.0 fps median over 40 windows (39 above 45), worst p99 24.75 ms. The verdict line reads FAIL with
  `initial_status=1`, as both branch runs did: Steam's exit status when the run stops it.
- The Direct3D 12 game in the title (`main-re3-title-2026-10-27-02`): helpers 59 MiB at their peak, census 1,285 MiB,
  title flexible 945 -> 898 MiB, 116.7 fps (median window 117.8), verdict PASS with `title_client=1`.

## The swap notice (2026-10-26)

"The PS5's memory is getting full: games may slow down as it starts to swap" is said once a session when the census
reaches the firmware's mark (`pw_memory_guard_swap_notice_mib`, src/pw_memory_guard.h):
- 800 MiB before 12.00 (the Pro at 10.01, where swap begins there);
- 1700 MiB from 12.00 on. 800 said it in every base PS5 session, since the console's own processes hold 731-1223
  MiB before Wine starts; both runs of the Direct3D 12 game on 2026-10-26 said it with swap-out flat at 334-336 MiB.

The title cannot watch swapping itself: on 13.60 `vm.stats.vm.*` and `vm.swap_info` answer ENOENT, and the only swap
names, `vm.swap_total` and `vm.swap_reserved`, are sizes (`tools/console-memory.py`, 2026-10-26). 1700 is where the
standard PS5's swap-out grew in the records: flat at a census of 1631 MiB, 255 MiB with Steam at 1700, 302 MiB at
1865. It stays under the guard's 1800 MiB close mark. On the gated build (`fmem-gate-2026-10-26-09`, fresh boot,
census 1243-1280 MiB at the start of each launch) 84 of the 86 sessions said nothing. The two that did are the
memory-guard case, which fills the system pool on purpose (census 1721 MiB). There swap-out stayed at 0.4 MiB with
RSS at 1713 MiB, so on a fresh boot the swap starts later still. None of the four runs of that game above said it (census peaks
1251-1382 MiB). The startup line reads "the memory guard's limit is 1800 MiB, its swap notice 1700 MiB".

## The tester probe (title PPSA99171, 2026-10-25)

What it settles: per console model and firmware, which memory can execute code, in the title and in a Sony local
process. At 13.60 the answer is JIT only on the standard PS5 and the Pro, and at 4.03 on the standard PS5 (table below); the Pro at 10.01 is unmeasured.
Testers run it on their own consoles; the owner collects the results.

- Source and build: `native/proton_tester_probe.{h,cpp}`, `tools/build-child-probe.py --kind service-native --tester`.
  It reuses the flex probe's Sony helper (`proton_flex_run`) and the loading screen's report mode
  (`proton_loading_report`).
- Package: `tools/package-tester-probe.py BUILD OUT --sample RESULT.json` writes the zip with its README and
  SHA256SUMS. It refuses a sample result that carries an IP or MAC address, an e-mail address or a long hexadecimal
  identifier (`tests/test_package_tester_probe.py`).
- Its own title, PPSA99171, so it can never replace MihawkOS's folder. It asks MihawkOS's daemon for the `/data` grant
  once, as MihawkOS does, and writes no `dev.conf` and nothing under `/data/ps5proton`.
- Every call a firmware may lack is looked up at run time (the model call is "unavailable" on 13.60). Every execute
  attempt runs under a fault guard and an alarm; code is called only where the kernel's query reports execute.
- Output:
  - the verdict on screen for 60 s;
  - `memprobe-<model>-<firmware>.txt` and `.json` in its own folder (or a USB stick, or `/download0`), with each
    place's errno.

On the base PS5, 13.60 (`fmem-tester-base-2026-10-25-06`, the shipped build; `-01` to `-05`, earlier builds, answered the same and are kept only in `klog`):

| | This probe's title | Sony local process |
| --- | --- | --- |
| Direct memory types 0 and 12, AllocateMain, anonymous memory | no execute (protection reads 1) | no execute |
| Map with execute at once | refused (0x80020016) | refused |
| JIT | refused (0x80020001), granted or not | refused (0x80020001) |
| Its 64 MiB of anonymous memory | | from the title's partition: 985 -> 918 -> 985 MiB free |

Testers' results (one line per console; each record in `evidence/tester-memprobe-*.json`):

| Record | Console | Firmware (installed / reported) | Title, direct memory | Sony helper, direct memory | Sony helper JIT | Helper's memory from the title |
| --- | --- | --- | --- | --- | --- | --- |
| `fmem-tester-base-2026-10-25-06` | standard PS5 (ours) | 13.60 / 13.59 | no | no | refused | yes, 67 MiB |
| `tester-memprobe-2026-10-25-01` | not a Pro in memory mode; model to be asked | 04.03 / 09.04 | no | no | refused | yes, 67 MiB |
| `tester-memprobe-2026-10-25-02` | a tester's Pro (12,928 MiB direct, Pro memory mode on) | 13.60 / 13.59 | no | no | refused | yes, 67 MiB |
| `tester-memprobe-2026-10-25-03` | a tester's Pro, the same answer (one console run twice, or two) | 13.60 / 13.59 | no | no | refused | yes, 67 MiB |

- A standard PS5 at 4.03 answers as at 13.60: on that model the answer is not the firmware's.
- A Pro at 13.60 answers as the standard PS5 at 13.60, way for way: the answer is not the model's either. Direct
  memory and anonymous memory keep read only after `sceKernelMprotect(R+X)` returns 0; the `MAP_EXECUTABLE` map is
  refused (0x80020016); a Sony helper's JIT create is refused (0x80020001).
- On that Pro the probe's folder took the write (`/app0=0`), so its files came back as they are.
- On 4.03 the probe's folder refused the write (errno 13), so its files went to `/download0`, which a tester cannot
  reach. The photograph carried the result. A later version should also try `/data` when granted.

- JIT in a title depends on the title ID, not only the grant: granted, PPSA99170's title runs JIT memory
  (`flex-sony-base-2026-10-25-03`, the same day) and PPSA99171's does not.
- So the probe's "title" line is the probe's own title. The question it settles, whether direct memory can execute,
  does not depend on that.
- Unelevated, the probe still writes its own folder (`/app0`). Granted, `/data/homebrew/PPSA99171` was not visible to
  it (ENOENT) and `/app0` was: it finds its folder by its own `eboot.bin`.

## Measured on the base PS5 (13.60, 2026-10-25, branch `fmem-codecache`)

The `steam` profile (Steam's client to its window, 120 s), one helper census per second (`WINE_PS5_EARLY=1`), and the
kernel's map by type (`WINE_PS5_VMMAP=1`) where named. Every run finished: no helper ended, 0 storage errors.

**The code cache** (`WINE_PS5_CODE_BUDGET_MIB`; the web helper is the process over budget, `steam.exe` holds 32 MiB of
code and is never evicted):

| Run | Budget | Web helper resident at the end | Evicted (KiB) | Program start | First frame burst | Frames per 10 s | Longest gap |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `fmem-steam-kinds-2026-10-25-01` | off | 250 MiB | - | 10.7 s | 10 s | 20.4 | 4.8 s |
| `fmem-steam-b64-2026-10-25-01` | 64 MiB | 193 MiB | 10,463 (167,408) | 10.8 s | 10 s | 20.4 | 5.3 s |
| `fmem-steam-b32-2026-10-25-02` | 32 MiB | 161 MiB | 57,244 (915,904) | 16.2 s | 30 s | 21.5 | 8.4 s |

- 0 refused and 0 lost in every arm; stale entries 34 (64 MiB) and 195 (32 MiB), dropped untouched.
- The web helper's hot code lies between 64 and ~90 MiB. At 32 MiB it thrashes and Steam's start is visibly slower.
- Steam's window draws about 2 frames a second here, so this instrument is coarse. The 60 fps test is the
  2D game, started through Steam (`steam://run/250900`, 180 s, the owner's own copy, installed on 2026-10-25):

| Run | Budget | The game's fps (median of its 60 fps windows) | Worst p99 / worst frame | Windows at 60 fps | The game's process | Web helper |
| --- | --- | --- | --- | --- | --- | --- |
| `fmem-isaac-off-2026-10-25-01` | off | 59.9 | 18.5 / 33 ms | 13 | 121 MiB (23 MB of code) | 234 MiB |
| `fmem-isaac-b64-2026-10-25-01` | 64 MiB | 59.9 | 18.0 / 34 ms | 13 | 121 MiB, not evicted | 173 MiB (16,494 evictions) |
| `fmem-isaac-b16-2026-10-25-01` | 16 MiB | 59.9 | 33.3 / 34 ms | 5 | 112 MiB (1,571 evictions) | 150 MiB (504,907 evictions) |

  - 64 MiB is free: the web helper gives back ~60 MiB while the 2D game plays exactly as without it.
  - Below a game's own hot code (16 MiB against the 2D game's 23 MB) the cache costs frames, and the web helper's thrash
    delays the game's start.
  - These ran at 60 Hz: the 2D game caps at 60, and that display measured 59.94 Hz ("Wine offers 60 Hz alone"). The owner
    waived a 120 Hz check on 2026-10-25 ("No need to check 120Hz"), so the verdict stands on these runs.
  - The default since 2026-10-25 is the owner's rule ("I want a smarter default. 64MiB always on for Steam and
    launchers"); see "The code budget's default" above.
- `fmem-steam-b32-2026-10-25-01` had no effect in the web helper: helpers inherit only the names in
  `wine/ps5/pw_wine_inherit.h`, and the budget was not one. It is now (`tests/test_pw_wine_inherit.c`).

**What the rest is** (`fmem-steam-kinds-2026-10-25-01`, budget off; the census groups a mapping by the address that made
it). Type 1, default anonymous memory, is the largest kind: 131 MiB in the web helper, 119 in `steam.exe`.

| Region | Size | Owner | Status |
| --- | --- | --- | --- |
| ntdll's data and its 11.4 MiB of zero-filled data (`memsz 0xb65f80`) | 11 MiB resident in every helper | our PRX loader wrote zeros over memory that already read as zero (`src/pw_prx_elf.c`) | **fixed**: small helpers 23-24 -> 15-16 MiB, `steamservice.exe` 49 -> 30, `steam.exe` 165 -> 157, helpers 491-519 -> 437 MiB in 8 (`fmem-steam-kinds-2026-10-25-02`) |
| two 25 MiB read-write regions | 50 MiB in `steam.exe` and the web helper each | two shared sections of a little over 25 MiB, mapped as their file: a helper paged only views of 256 MiB or more and of a whole number of 16 KiB pages | **fixed** (below) |
| Sony libc's heap | 24 MiB resident, 2-5 MiB in use (heavy helpers) | our loader read each module file into it (`win32u.prx.elf` 26 MiB, `libvulkan-helper.prx.elf` 25 MiB); libc kept the pages | **fixed** (below) |
| `libvulkan-helper.prx` read-only data | 14 MiB in each process that loads it | our loader's private copy | **moved** to direct memory with every module's data (below); still one copy per process |

`fmem-steam-kinds-2026-10-25-02` also stopped flipping the display after 50 s while Steam's window kept presenting at
1 frame a second. No fault in any log, and the title, which composes and flips, does not use the changed loader. Not
explained. The rerun with captures (`-03`) was refused by the console guard: the base PS5's control payload had been
loaded again by someone at the console. Its record keeps the refusal.

## Non-code memory in direct memory (base PS5, 13.60, 2026-10-25/26, branch `fmem-codecache`)

Steam in Big Picture (`--profile steam --args "-bigpicture -noverifyfiles"`, 120 s), `WINE_PS5_EARLY=1` and
`WINE_PS5_VMMAP=1`, the code budget at its default. Each helper's census at the end of the run, MiB resident; type 1 is
the kernel's default anonymous memory, the flexible part:

| Helper | `fmem-bp-base-2026-10-25-01` (before) | `fmem-bp-ddata-2026-10-25-01` (module data, file buffer) | `fmem-bp-dsec-2026-10-26-03` (and sections) |
| --- | --- | --- | --- |
| `steam.exe` | 164 (type 1: 111) | 117 (64) | 67 (14) |
| `steamwebhelper.exe` | 212 (126) | 193 (98) | 135 (48) |
| `steamservice.exe` | 30 (17) | 20 (7) | 20 (7) |
| `services.exe`, `winedevice.exe`, `rpcss.exe`, `wineboot.exe` | 15-16 each | 11-12 each | 11-12 each |
| **All 7** | **467** | **375** | **267** |

The census counts a shared section in every process that maps it, so the sections' share of the system pool's saving
is smaller than their share of the census. Every run finished, 0 storage errors, no assertion, and frames the same in
all three: in the last 10 s about 40 per second, median 17 ms, and the same long worst frames.

What changed:
- **Module data** (`native/pw_prx_elf_ps5.c`, `direct_over`): after relocation, each run of a module that does not
  execute, read-only or writable, is copied into direct memory mapped over it, as `jit_over` does for text. A refusal
  says so and keeps the anonymous copy. Direct memory comes from the title's pool
  (`sceKernelAllocateDirectMemoryForApp`), mapped only over a range the loader reserves.
- **The file buffer** (same file): the loader read each `.prx.elf` whole into Sony libc's heap, which keeps freed pages.
  `steam.exe`'s libc heap went from 25 MiB taken from the system (28 at most) to 4. The buffer is now direct memory,
  given back after the load.
- **Sections of 8 MiB and more** (`PW_WINE_DSECTION_MIN_BYTES`, was 256 MiB; patch 0962): the title keeps them in
  direct memory and a helper's view is paged from it. A helper paged only views of 256 MiB or more and of a whole
  number of 16 KiB pages, so Steam's two shared sections of a little over 25 MiB stayed file mappings. Views start on
  64 KiB, so a view's last host page is its own and the pager now takes the size rounded up.
  `WINE_PS5_DSECTION_MIN_MIB` (8 to 256) reaches the helpers too (`wine/ps5/pw_wine_inherit.h`).

Runs that measured nothing new: `fmem-bp-dsec-2026-10-26-01` ran the `ddata` build (Wine's build had failed, so
nothing was deployed). `fmem-bp-dsec-2026-10-26-02` lowered only the threshold: the 25 MiB views still had sizes that
were not whole host pages and stayed file mappings (`steam.exe` 117 MiB).

## End to end: the branch against the morning (base PS5, 13.60, 2026-10-26)

`fmem-e2e-2026-10-26-01`, the head of `fmem-codecache` (5a35ba0): Steam online in Big Picture (`-gamepadui
steam://run/250900`), the 2D game started by Steam, crosses into a run at 170-210 s, then about 4 minutes in the first room
(the character not moved), the game windowed over Big Picture. The census is the title's `Wine: system memory` line
(the console's processes and the helpers, physical pages); VM Stats is SceShellCore's.

| | Morning: `fmem-steam-isaac-2026-10-25-01` | Branch head: `fmem-e2e-2026-10-26-01` |
| --- | --- | --- |
| What ran | Big Picture installing the 2D game (the download watched), 1,018 s | Big Picture, then the game in play, 500 s |
| Census peak | 1,816 MiB | 1,101 MiB |
| Helpers at their peak | 590 MiB in 10 (largest 312 MiB) | 356 MiB in 9 (largest 149 MiB) |
| VM Stats RSS, peak | 1,823.9 MiB | 1,097.2 MiB |
| VM Stats swap out, first / last | 1.3 / 2.3 MiB | 334.0 / 335.7 MiB |
| The 2D game's frames | (installing) | 59.9 fps median over 41 windows of 10 s above 45 fps, worst p99 33.75 ms |
| Boot | fresh that morning | 11.8 h, 502 launches |

What it means:
- The helpers are the fair comparison: 590 -> 356 MiB (-40 %). Swap grew 1.7 MiB during tonight's run, so the helpers'
  pages were not being swapped out while it was measured.
- The census peak is not: the console's own share was 1,167 MiB in the morning and 743 MiB tonight (system minus
  helpers at the helpers' peak), and 334 MiB of the console's pages were already swapped out at tonight's start
  (eleven hours of runs on one boot). A fresh boot reads higher; the census peak on a fresh boot is still to be measured.
- The work differed: the morning's run downloaded the 2D game through the web helper; tonight's played it. The web helper is
  the largest helper in both.

**The 2D game's worst p99 (33.75 ms tonight, 18.0 ms in `fmem-isaac-b64-2026-10-25-01` and `-start16-`) is loading, not the
code budget.** Repeated once (`fmem-e2e-2026-10-26-02`, same route). In both runs the game's code cache had 0 evictions,
0 re-conversions and 0 raises (it holds 25-28 MiB under its 64 MiB budget). The slow windows are the same ones each
time: the game's first windows after it starts (30-53 fps, p99 34-35 ms) and the window in which the crosses start a run
(p99 28-33.5 ms); every other window is 17.5-19.0 ms. The earlier runs pressed nothing and stayed on the game's title
screen, so they never loaded a run. All four runs drew windowed (composed, "not-fullscreen").

| Run | Windows above 50 fps | Worst p99 | Steady p99 | Evictions / re-converted / raises |
| --- | --- | --- | --- | --- |
| `fmem-e2e-2026-10-26-01` | 38 | 33.75 ms (run start) | 17.5-19.0 ms | 0 / 0 / 0 |
| `fmem-e2e-2026-10-26-02` | 39 | 34.0 ms (the game's start) | 17.5-19.0 ms | 0 / 0 / 0 |

## Next: the game inside the title (design, 2026-10-26; the owner's decision of 2026-10-26)

After the non-code memory above: the game's process runs in the title, so its code (JIT memory) and its flexible memory
come from the title's own partition (985 MiB free at the probe's start, `fmem-tester-base-2026-10-25-06`) instead of
the system pool, on every model and firmware. Steam and the launchers stay helpers under the 64 MiB code rule.

**What the title runs today.** In the default (launcher) layout the title loads Wine's runtime but runs only the
server, the GPU broker, audio and input (`native/proton_wine.cpp`, owner bootstrap): every Windows program, the first
included, starts in a helper (`pw_ps5_start_root`). Only `PROTON_LAYOUT=title` runs a program in the title
(`pw_wine_start_run`), the first one, at launch, with the title's own graphics; it has not run on 13.60.

**What the title backend needs to run code.**
- On 13.60 only JIT memory executes, in the title as in a payload (above). The title's Wine maps code as direct
  memory with execute, which the kernel accepts and drops. A program in the title would fault at its first
  instruction outside the title's own modules.
- The conversion that serves helpers (patch 1127, `exec_convert`) refuses in the title (`!payload_mmap`: ENOTSUP).
  It must also run with the title's engine (`dmem`, `state`), with `sceKernelMprotect` where a payload uses
  `payload_protect`. PPSA99170 may make JIT objects (`flex-sony-base-2026-10-25-03`). The code budget applies as in a
  helper, evicting back to the title's direct memory.
- What differs from a payload: when CPU-only direct memory is refused, the title maps it with the GPU bits (`map`),
  and tearing down wired GPU memory is what hung the console ("unwire timeout"). Only CPU-only runs may be
  converted; any other run stays as it is and is logged. Code sharing between helpers stays off in the title at first.

**How one game is chosen.** The title starts as today, with Steam in helpers. Among the processes a listed client
starts (`pw_code_budget_listed`: Steam, the launchers), the first whose image is neither listed nor one of Wine's
own (`wineboot`, `services`, `winedevice`, `rpcss`, `plugplay`, `explorer`, `conhost`, `steamwebhelper` and the
like) is started in the title, if nothing runs there yet. Every later one goes to a helper. A setting names it
outright or turns this off (`PROTON_TITLE_PROCESS=<image>|none`, to be added).

**What does not exist yet.**
1. A client started late in the title: the server has already created the process for Steam's `CreateProcess`.
   The title must start its own client for it, with the startup information, handles and parent a helper's loader
   receives over the rendezvous (`wine/ps5/pw_payload_rendezvous.h`), delivered in-process instead.
2. Its graphics: the broker owns the display for Steam's helpers. The game in the title draws either as a local
   client of the broker (no socket hop) or by taking the display from it, as the title layout does.
3. Its end: Steam ends games with `TerminateProcess`. Ending the title's client must leave the title, the server
   and Steam's helpers running.

**What it costs.**
- A crash of the game is a crash of the title: the session ends, Steam's helpers with it. A helper's crash ends only
  the helper.
- One process: a second game, and the game's own children, stay in helpers.
- The title's partition also holds the title's own flexible use. The probe measures what is left with the game in it.

**Measured.**
- `fmem-intitle-p1-2026-10-26-01` (`wine-probe.exe`, `PROTON_LAYOUT=title`, the build of 1372733): the title's
  direct-memory self-check failed ("execute granted but not set (query 0x1)"), so the engine was refused for the
  whole run ("direct memory refused for anonymous memory"). Wine then could not map the shared user data at
  0x7ffe0000 (c0000017) and ended 8 s in. The title returned to the launcher; no hang, 0 storage errors.
- With the title's code converted (`wine/ps5/pw_wine_dmem_ps5.c`, `title_jit`: when the title's direct memory does
  not execute, its self-check is the payload's, and `exec_convert` serves the title's own CPU-only direct memory):
  `fmem-intitle-p2-2026-10-26-02` passed ("the title's code becomes JIT memory"; PE64 probe PASS, 20 s; Wine's
  services in helpers). The title's flexible free went from 927 to 899 MiB while it ran: its code is charged to the
  title's partition. (`-01` refused its own retry: the engine's state was already -1; fixed.)
- A program in the title that started a child faulted (`fmem-intitle-p3-2026-10-26-0{1,2}`, `-p4-2026-10-26-0{1,2,3}`):
  as the child came up, every title thread fetched at 0x200330FB4 and looped in nested exceptions (each run ended with
  `tools/kill-hung-title.py`; the console stayed up, 0 storage errors). Cause: the x86_64 syscall thunks call through
  0x7ffe1000, which with 16 KiB host pages lies in KUSER_SHARED_DATA's host page, one section mapped by every process
  (patch 0530). Each process wrote its own dispatcher there. The helpers' ntdll is at 0x2002f8000 in all of them (the
  fault address is its `__wine_syscall_dispatcher`, offset 0x38fb4), the title's Sony-loaded ntdll is elsewhere (its
  dispatcher 0x80150fb4, still execute-only when the fault came: `-p4-...-03`), so the first helper started after the
  title sent the title's threads to the helper's address. **Fixed** by patch 2300: the slot holds ntdll.dll's
  `__wine_syscall_slot` (the same address in every process), which jumps through that process's own
  `__wine_syscall_dispatcher`. `process-probe.exe --descendants` now passes in the title (`-p4-2026-10-26-04`, 20 s).
- `gpu-window64-probe.exe` in the title (`-p3-2026-10-26-03`) no longer crashes and ends by itself; 5 of its 9 checks
  fail as the layout makes them: no desktop shell (error 1400), OpenGL through the title's driver rather than Zink, and
  no graphics for children (no broker in this layout). Those are what a game in the title beside Steam's helpers needs.
- Step 5 made the title's views of sections from 8 MiB go to the pager, which serves only helpers: in the title a
  14 MiB view faulted c0000017 (`-p3-...-01`). A view in the title keeps the 256 MiB threshold
  (`__wine_ps5_section_min_bytes`); the launcher layout's title maps no views.

**Stages for a game in the title beside Steam's helpers** (from the code, 2026-10-26; the design is [TITLE_CLIENT.md](TITLE_CLIENT.md)):
1. *The client.* This exists: the owner-graphics mode (patch 0927, `start_owner_graphics` in `native/proton_wine.cpp`)
   runs one child, marked `WINE_PS5_GRAPHICS_CHILD=1` in its Windows environment, in the title with
   `WINESERVERSOCKET` and the title's own graphics, but only when there is no GPU broker (`WINE_PS5_GPU_BROKER=0`,
   `WINE_PS5_GRAPHICS_HOST=1`). Naming the child by its image instead of marking it is a few lines in `pw_ps5_spawn`
   (kept as `fmem-title-process-by-name-f740117.patch` in the sessions' notes; not committed, never exercised).
   Measured (`fmem-isaac-intitle-2026-10-26-01`, that mode, Steam `-silent -no-browser steam://run/250900`): without
   the broker the helpers have no Vulkan ("Failed to create Vulkan instance"), Steam's web helper died and was started
   again about 25 times, its driver queries failed to launch, and Steam never asked for the 2D game. The title would not stop
   either (Steam does not end on the stop request); it was ended with `tools/kill-hung-title.py --force`, the console
   stayed up, 0 storage errors. So the mode that exists cannot carry Steam: stage 3 (the broker beside a client in the
   title) comes before any game.
- A 32-bit program runs in the title (`fmem-intitle-pe32-2026-10-26-01`, PASS, loaded at 0x110000), but the title has no
  raw sigaction for WoW64 ("a fault in 32-bit code ends the process"), so a 32-bit game such as the 2D game ends the session
  at its first 32-bit fault. A helper gets that call from the payload (`native/proton_wine_child.c`); the title does not.
2. *Its end.* Wine ends a process with `_exit`: in the title that ends the session, Steam's helpers with it. The
   title's client must end its threads and give its memory back, leaving the title running; until then, the game in the
   title is the session's last program.
3. *Its graphics.* The broker owns the display for the helpers. The game in the title draws as the broker's local
   client (the same compositor, no socket), the checks `gpu-window64` failed in the title layout.
4. *Measure.* The 2D game started by Steam: the title's flexible free and the system pool's census against
   `fmem-bp-*` and `fmem-steam-*`.

**Order.**
1. Probe on the base PS5: a small program in `PROTON_LAYOUT=title` as it is now (expected: the first execute fault,
   conversion ENOTSUP), then with conversion in the title, then a GPU program, measuring both pools (the title's
   flexible free and the system pool census).
2. The 2D game started by Steam, in the title, measuring both pools against `fmem-steam-*`.

## Open

- **Our Pro at 10.01**, once the conformance session releases it and the owner says so: one run of the tester probe.
  A Pro at 13.60 and a standard PS5 at 4.03 already answer JIT only, so route 1b is expected dead there too; only
  10.01 itself is unmeasured.
  On 2026-10-26 the Pro was free for it, but this session's permission check refused the install and launch, since
  the go-ahead came through another session: it waits for the owner's approval here, or a run of the tester zip by hand.
- Whether a Sony local process may map a JIT object the title made (route 4's charge, without its close risk, since
  the system ends local processes with the title).
- The display that stopped flipping in `fmem-steam-kinds-2026-10-25-02` (above).

## How to run the probe

```bash
PS5_VULKAN_GIT=$PWD/../PS5_Vulkan python3 tools/build-child-probe.py --kind service-native
PS5_HOST=<console> python3 tools/run-child-probe.py --kind service-native --mode flex --name <run> --build <printed folder>
```

The run swaps the installed title for the probe title and puts it back, checked by readback. `record['flex']` holds
the figures, and `vm_stats` and `fmem_lines` hold SceShellCore's lines from the klog.

## Big Picture's stutter (base PS5, 13.60, 2026-10-27)

The owner and testers: Big Picture stutters, mostly while it loads and while scrolling or animating menus. Measured
with `bp-scroll-pad.txt` (idle, continuous scroll, menu moves) and `bp-phases.py` (the web helper's 10 s pacing windows by
phase), Big Picture alone, 120 Hz, the owner's prefix. Frames over 20 / 34 ms, and p99:

| Run | First minute | Scrolling | Menus | Helpers at peak |
| --- | --- | --- | --- | --- |
| `bp-a-default-2026-10-27-01` (one process, 64 MiB) | 79 / 31, p99 77 ms | 162 / 48, p99 55 ms | 74 / 51, p99 62 ms | 320 MiB in 10 |
| `bp-b-nobudget-2026-10-27-01` (no code budget) | 35 / 19 | 80 / 10 | 65 / 32, p99 31 ms | 326 MiB in 10 |
| `bp-c-60hz-2026-10-27-01` (60 Hz) | 155 / 39 | 252 / 42 | 185 / 37 | 324 MiB in 10 |
| `bp-d-multiprocess-2026-10-27-01` (own processes) | 9 / 4, p99 21 ms | 15 / 9, p99 27 ms | 21 / 19 | 501 MiB in 15 |
| `bp-fix-base-2026-10-27-01` (the fix: own processes, raise rule) | 16 / 7 | 7 / 2, p99 16.5 ms | 24 / 20, p99 18.5 ms | 429 MiB in 16 |
| `bp-fix-prolayout-2026-10-27-01` (the Pro's layout: one process, raise rule) | 38 / 13 | 66 / 7, p99 25 ms | 60 / 37, p99 31 ms | 325 MiB in 10 |

- The cause: our own rule ran Steam's browser as one process (`--single-process`, src/pw_steam.c, to save memory on the
  Pro). Its whole code, about 300 MiB re-converted in Big Picture's first minute, was one helper's, held to the listed
  clients' 64 MiB, and every thread's code faults waited on that process's one lock.
- 120 Hz is not it (c is worse at 60 Hz), nor the broker's composition (an arm with the composed route off matched a).
- The fix: the listed clients may be raised on real thrash: in one process the raise to 96 then 128 MiB cut the first
  minute's re-conversion from 299 to 53 MiB (`bp-fix-prolayout`). From firmware 12.00 the browser first kept its own
  processes too (each under 64 MiB, 0 re-conversion; `bp-fix-base`), until 2026-10-28: each helper is a system process
  and takes the console's system page tables, and on the base its 10 to 12 processes took about 2,300 of the pool's
  8,192 pages, Steam alone left 486, and a game's start exhausted the pool ("system page table exhausted", errno 5 for
  the next helper). Since then it is one process on every console, at the owner's word.
- With a game: Big Picture with the 2D game (`bp-fix-game-2026-10-27-01`) peaked at a census of 1,123 MiB from 687 at the
  start (helpers 455 MiB in 17), the game at 60.0 fps median; on the highest baseline seen (1,223 MiB) that is about
  1,660 MiB, under the 1,800 MiB guard.
- A window of no presents (10-57 s) in some runs is Big Picture idle with nothing to redraw, not a freeze;
  `bp-phases.py` reports it apart.
- The runs' records name the library's games, so they are kept off the tree, in the worktree that ran them
  (`PS5_Proton-fmem/evidence/bp-*-2026-10-27-0*.json`); the gate's records are committed.

