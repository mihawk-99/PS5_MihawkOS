# Console validation queue

## Steam ids for Proton's per-game fixes (2026-10-31, uncommitted worktree nostalgic-babbage, any console)

Staged, not committed: the title now gives a game `SteamGameId` and `SteamAppId` from Steam's `appmanifest_*.acf` (or
`steam_appid.txt`), helpers inherit them (docs/PROTON_GAME_KNOWLEDGE.md §1.4). Host tests pass. Needs the usual gate on
the exact tree before it is committed; these are the questions that report left open, in one session:

1. **Library game, the 2D platformer (504230) then the 2D roguelike (250900)**, each one run with the Steam entry's usual setup. Pass = the
   title log has `Wine: Steam id from the installation: SteamGameId=<id>` and `SteamAppId=<id>`, the game reaches its
   menu as it did before, and no new `steam://` or client start appears in the process list.
2. **Unix side of a title game:** `python3 tools/run-console.py --profile <any> --env SteamGameId=292120` shows
   `HACK: fsync: yield to waiters` from `hacks_init` in the helper/title log.
3. **Unix side of a Steam-started child (decides whether the Wine patch of §5 step 3 is needed):** start a probe from
   Steam's console or a parent program with `SteamGameId=292120` in its `CreateProcess` environment only; the same
   `HACK:` line appears in the child's log or it does not. Also record whether Steam itself sets both names for its games
   (print the child's Windows environment, as `tests/initial-environment-probe.c` does).

## Alpha 7 rung 2: a game hung in the title (2026-10-31, branch alpha7-integration, the standard PS5): done 2026-10-31

Run on 2026-10-31 after the owner approved it (the first try was refused by the permission check). All three cases
passed: rung2-base-2026-10-31-spin, -wait and -healthy. Its script, in the session's scratchpad (`rung2-run.sh`), does
the following.

- **Gates:** it checks the console identity (the standard PS5's key, never the Pro's), that the console is idle, and
  that FTP is fast. Then it deploys this branch's build (the title watchdog in `native/proton_wine.cpp`, the hang knob
  in `tests/d3d11-probe.c`).
- **Runs:** three sessions, one at a time, each with `PROTON_LAYOUT=launcher`,
  `WINE_PS5_TITLE_PROCESS=graphics-render-probe.exe` and `PROTON_HANG_STALL_MS=15000`:
  1. `PS5_PROTON_PROBE_HANG_FRAME=4`: the in-title renderer spins after frame 4.
  2. The same with `PS5_PROTON_PROBE_HANG_WAIT=1`: it waits forever instead.
  3. `PS5_PROTON_PROBE_FRAMES=10800`: a healthy 3-minute draw.
- **PASS:** cases 1 and 2 log `title watchdog`, the game in the title is ended through the server, and the console
  returns to the launcher within about 30 s and answers with count=0. Case 3 never logs `title watchdog`.
- **Stop:** at once on a stuck title or a hung console, without restoring.
- **After:** it restores Alpha 6 and verifies it (staged files against the reference, title files by sha256).

## Helper start stamps: where an installer's process start goes (2026-10-27, branch start-path, the Pro)

Staged as P0 of the installer-speed plan (docs/PLAN.md, section of 2026-10-27): the title's `wine-ps5: spawn native=`
line (request, startup lock, payload read, elfldr's answer, rendezvous connect and attempts, endpoint held, bootstrap
sent, READY), each helper's `wine-child: start at=<step> ms=` stamps (main, control, entry, bootstrap, payload-entry,
ntdll-loading, ntdll-loaded, wine-main) and its `wine-child: life summary` at its end (server, file and section time).
`tools/start_stages.py` puts them and Wine's first and last module lines on one clock; `tools/run-console.py` writes
the result into each run's record as `start_stages`. No behaviour change. Host tests pass. Build, deploy title and
runtime together (the payload loader and ntdll's process unit changed), usual gate setup, then:

1. **The gate:** `python3 tools/gate.py run --name start-stamps-gate-2026-10-27-01`; pass = every case.
2. **The measurement (queue item 5 below, now with stamps):** `python3 tools/run-wine-session.py msi --arch x86_64
   --files install --force --sentinels 0 --name start-msi-2026-10-27-01`, then `python3 tools/start_stages.py run klog
   <its run name>` (or read `start_stages` in its evidence record). Answers: how much of the ~0.9 s between one custom
   action's end and the next carrier's EXEC is the title and elfldr (`answered`, `connected`), how much is the
   carrier's start (`main`, `control`), and which step after the bootstrap dominates. Also run the gate case
   `prefix-fresh64` once more if the msi run is short of starts.
3. Commit P0 after 1 passes, its body with step 2's medians.

Then **P2** (the rendezvous's 1-8 ms pauses) and **P1** (the warm helper, the narrower startup lock), staged on top in
that order (patches and trees in the coordinator's notes, `installer-speed-*`). Each is its own commit and gate:

4. **P2:** build, deploy, `gate.py run --name rendezvous-gate-2026-10-27-01`; repeat step 2's msi run
   (`--name start-msi-p2-2026-10-27-01`) and compare: `python3 tools/start_stages.py compare evidence/<P0 run>.json
   evidence/<P2 run>.json`; `connected` should fall and `attempts` rise.
5. **P1:** build, deploy, `gate.py run --name warm-gate-2026-10-27-01`; the msi run again
   (`--name start-msi-p1-2026-10-27-01`; most starts `warm=1`, the parked time not on their path), and once with
   `--env WINE_PS5_WARM_HELPER=0` as the same build's cold baseline. Then a Steam end-to-end check: `run-console.py
   --profile steam --seconds 120 --name warm-steam-2026-10-27-01`, Big Picture shown, no helper crash, the memory
   guard's peak against the Pro's 870 MiB (a parked helper is one more process for up to 30 s), and no
   `warm helper ... gone while parked` lines. A parked helper outliving the title would show as a leftover
   SceSpZeroConf in the next launch's readiness census: look for one.

## Loader everywhere: helpers load Wine's modules themselves on every firmware (2026-10-27, branch loader-everywhere, the Pro)

Staged, not committed (the hook needs a gate on exactly this tree): `pw_prx_elf_wanted` makes the user-space module
loader every helper's default; `WINE_PS5_PRX_LOADER=0` keeps the kernel's for an A/B. Why: docs/PLAN.md, section of
2026-10-27. Host tests pass. On the Pro (10.01), which used the kernel's loader until now; usual gate setup (pad awake,
DNS, `PROTON_TLS_TRUSTED_PORT=1749`, `PROTON_TLS_UNTRUSTED_PORT=1750`). Build `bash tools/build-production.sh` and
deploy title and runtime together (the payload loader changed; ntdll did not).

1. **The gate:** `python3 tools/gate.py run --name loader-gate-2026-10-27-01`. Pass = every case. Check in a few helper
   logs: `wine-child: module loader=user-space (default)`, then `elf load ... base=` and `ntdll loaded`. Its
   `late-library` case must still reserve at least 900 GiB (the probe's floor for 10.01; alpha-2 fell to 31 GiB with
   this loader until patch 1231) — read the reserved total in its record, not only the pass.
2. **Steam, plain:** `python3 tools/run-console.py --profile steam --seconds 90 --name loader-steam-2026-10-27-01`, with
   no extra `--env` (none of `mt3-2026-10-24-01`'s malloc trace, VMMAP, census or early lines). Pass = Steam still
   running past 30 s and no helper crash. A crash: keep its klog and helper logs, and repeat once with
   `--env WINE_PS5_PRX_LOADER=0` to show whether the loader is the cause.
3. **Memory A/B, the same launch twice:** step 2's run is A; B is the same command with
   `--env WINE_PS5_PRX_LOADER=0 --name loader-steam-kernel-2026-10-27-01`, same boot, nothing between. Compare, per
   helper and in total: the `wine-child: vm resident= direct_resident= flexible=` lines (the record's `vm_last` and
   peak), and the run's system flexible memory peak against the Pro's 870 MiB limit. Module text is JIT memory in A
   (counted as flexible), the kernel's own pages in B. Decides the open risk: a rise that brings Steam near 870 MiB
   means the Pro needs the text shared or budgeted before this ships; a few MiB per helper is the cost of not
   depending on the jailbreak tools.

Commit after 1 passes (2 and 3 are evidence for the commit body): subject "Load Wine's modules in every helper with
the user-space loader, on every firmware"; the drafted message is in the shared notes folder
(`loader-everywhere-commit-message.txt`).

## Conformance: two gated commits waiting for a fresh boot (2026-10-27, branch hypervisor, the Pro)

The Pro's pipe memory ran out at 01:14:59 during `gate-method-2026-10-27-01` (8 of 102 passed, the rest failed after
it), after five gates and the Direct3D reruns on the same boot. Console work stopped for the night; nothing is
committed from it. After a restart (kstuff + elfldr), on `hypervisor` at c4a3458 plus the uncommitted tree:

The commit hook ties one gate to one commit's tree, so the work is stacked into two commits, lowest risk first;
each can also be split into its own commits with a gate each.

1. **Gate A: the test method, the CJK fonts and FreeType's paths, as the tree is now** (host tests 168 passed;
   staged without msyhbd.ttf; rebuild before deploying, the tree changed after build 2f46576c198e14db):
   - tools/run-wine-tests.py D3D_DLLS and console_fonts, tests/test_dll_overrides.py, tests/wine-tests-serial-ps5.json
     (reg.exe, regedit.exe);
   - tools/stage-wine.py LEFT_OUT_FONTS and Fonts/.pw-retired, wine/ps5/pw_wine_prefix.c's retire step with
     tests/test_pw_wine_prefix.c, tools/deploy-wine.py's removal of runtime files no longer shipped
     (tests/test_deploy_wine_retire.py). What each may delete, and nothing else:
     - **deploy-wine.py** removes a console file only when the previous deploy's manifest
       (`/data/ps5proton/runtime/ge11-7/deploy-manifest.json`, written by deploy-wine itself) lists it and this build
       does not, and its path is plain and inside `/data/ps5proton/runtime/ge11-7/`, the runtime directory the build
       owns (runtime_files_gone: no prefix, drive_c, save, game folder or other /data path; no absolute, empty, `.`,
       `..` or backslash segment; not the manifest itself). Expected on the first deploy:
       `share/wine/fonts/msyhbd.ttf` and `lib/wine/prefix/drive_c/windows/Fonts/msyhbd.ttf`, both under that runtime.
     - **pw_wine_prefix.c** removes, at a prefix's update, only the exact names a staged folder's `.pw-retired` lists
       (today: `msyhbd.ttf` in `drive_c/windows/Fonts`), from that same folder of the prefix, and only a regular file:
       no pattern, no `/`, `\`, `.` or `..`, no directory or link. Nothing in drive_c's other folders, users or saves.
   - tools/build-wine-ps5.sh: win32u linked with ntdll's path wraps (it, not FreeType, opens font files), so
     AddFontResourceEx opens a font a program wrote on a drive letter (check `... gdi32 --files font --force --sentinels 0 --name a-font`: font.c:2118, 6300 and
     7080 pass);
   - docs/CONFORMANCE.md.
   `bash tools/build-production.sh`, deploy (expect "removed 1 runtime files this build no longer ships: msyhbd.ttf"),
   then before the gate: `python3 tools/run-wine-session.py user32 comctl32 --files edit --force --sentinels 0 --name
   a-edit` (expect no "Tahoma for charset 136 isn't available" skip in either build) and a look at a Chinese text
   screen, since YaHei's regular face changes from Source Han Sans Bold to msyh.ttf (Droid Sans Fallback): text that
   drew bold draws regular. Then `python3 tools/gate.py run --name gate-a-2026-10-27-01`, commit.
2. **Gate B: Wine 2100 and 2101** (bcrypt: a block cipher without an IV starts from zeros; dbghelp: a live process
   without a Unix loader address is still live). Both patches are in the shared notes folder and this session's
   pending folder; copy them into wine/patches, build, deploy, then
   `python3 tools/run-wine-session.py bcrypt --files bcrypt --force --sentinels 0 --name b-bcrypt` (complete in both
   builds, where it ended with a SIGFPE in nettle's cbc_decrypt or hung) and `... dbghelp --files minidump ... --name
   b-minidump` (no "Couldn't write minidump content" at minidump.c:91), `gate.py run --name gate-b-2026-10-27-01`,
   commit.
2b. **Gate C: Windows Zink at PS5_Mesa e7c317df9c11** (zink: present nothing when flush_frontbuffer has no context;
   one commit on PS5_Mesa main 1ba9874, branch zink-present-without-context, pushed by the Design branch). Copy
   `0033-windows-zink-presents-nothing-when-swapbuffers-has-no-context.patch` into platform/graphics/patches and
   `build-mesa-windows-zink-i686.sh` over tools/ (both from this session's pending folder: the revision
   b3588f78 -> e7c317df9c11 in the foundation's script, its manifest line, and the i686 script), build (the
   foundation and both Windows Zink builds are redone), deploy, then `python3 tools/run-wine-session.py opengl32
   --force --sentinels 0 --name c-opengl` (expect test_window_dc passed in both builds, where SwapBuffers with no
   context crashed; the PC shows the next stop in test_make_current_read), gate, commit. Proven on the PC: with
   b3588f78 the test stops after opengl.c:2716 in both builds, with e7c317d both run on to test_make_current_read.
   The Windows Zink build compiles none of the files b3588f78..1ba9874 changed (radv, ac_gpu_info, wsi videoout).
3. Evidence only, after the gates (they start helpers by the thousand, the last ones before pipe memory runs out):
   `python3 tools/run-wine-session.py opengl32 vulkan-1 winscard wpcap --force --sentinels 0 --name clean-crash` and
   `... dbghelp --files minidump --force --sentinels 0 --name clean-minidump` (their records are from +seh runs),
   then `... reg.exe regedit.exe --force --sentinels 0 --name serial-reg` (now alone); requalify those modules.
   **Done 2026-10-27 on the Pro** after integration-1027 (opengl32 was already rerun clean as zink-proof):
   minidump, reg.exe and regedit.exe match the PC in both builds; vulkan-1, winscard and wpcap end as before
   (docs/CONFORMANCE.md). Pipe memory ran out during serial-reg, which ended that boot's console work.
3b. Diagnosis, next boot: `python3 tools/run-wine-session.py vulkan-1 --force --sentinels 0 --name vulkan-trace
   --env WINEDEBUG=err+all,+vulkan`: which check of the helper's VideoOut WSI refuses the swapchain of an
   ordinary window (the extent against the mode's, or a 119.88 Hz mode not offered; docs/CONFORMANCE.md, Vulkan).
   **Done 2026-10-27** (vulkan-trace-111334): neither; the broker WSI refused the IMMEDIATE present mode. Fixed by
   graphics patch 0035 (vulkan-present-mode: both builds complete).
4. Diagnosis: `python3 tools/run-wine-session.py gdi32 --files font --arch x86_64 --jobs 1 --force --sentinels 0
   --name font-trace --env WINEDEBUG=err+all,+font`: why AddFontResourceEx fails for wine_vdmx, wine_longname and
   wine_ttfnames on the console only.
5. Measure one helper start (msi install, x86_64, alone, helper logs kept): the time from the parent's
   `Wine: process msiexec.exe` line to the helper's first Windows instruction and to its end, for msi's 96 starts.

## Steam games' start and a game from the owner's library (2026-10-25, branch hypervisor, the Pro): steps 1-2 done after the restart

Steps 1 and 2 are done: `gate-load-2026-10-25-02` passed 98 of 98 on the same build, and the work is committed. Step 3
is under way (`tools/steam-install-app.py`).


The console hung during `gate-load-2026-10-25-01` (64 of 98 cases had passed): at case 65 (`initial-env64`) the Shell UI
froze for 60 s and was force-aborted (`SYSTEM_SOFTWARE_TIMEOUT_ASYNC` in SceShellUI) before the title printed anything,
and the control payload's `kill PPSA99170` stayed busy for more than 13 minutes. Nothing more can run until it is
restarted (then kstuff + elfldr, see the per-boot setup). The work under test is uncommitted on `hypervisor`: patches
2076-2082, `-noverifyfiles` on the Big Picture entry, `tools/start-timeline.py`, `tests/file-cost-probe.c` (docs/PLAN.md,
"Steam games start").

1. **The gate on exactly this build**: `bash tools/build-production.sh` (with `PS5_PAYLOAD_SDK_FORK`), deploy title and
   runtime, `python3 tools/gate.py run --name gate-load-2026-10-25-02` (TLS ports 1749/1750, the DualSense awake).
   A repeat of the Shell UI freeze at a launch is the first thing to look at: its klog's first lines, and whether a
   launch without these patches (the `bdf8a09b` build minus 2076-2082) freezes the same way.
2. **Commit** the load-time work once the gate passes (one commit; the body has the numbers in PLAN.md).
3. **A game from the owner's library** (app 1809700, the owner's library): an `appmanifest_1809700.acf` in Steam's own library with
   StateFlags 1026 makes the signed-in client download it; keep a headless client running until the manifest says
   StateFlags 4; then a catalog entry like 250900's (`steam.exe -silent -noverifyfiles -no-browser steam://run/1809700`,
   the same rules), 120 Hz (`PROTON_120HZ=1` and the game's own 120 fps setting), highest settings, and a pad-script run
   to gameplay with the flight log's frames and the start timeline.

Reviewed 2026-10-07. The former recovery/input/audio queue is complete enough to
be superseded by [NEXT_CONSOLE_SESSION.md](NEXT_CONSOLE_SESSION.md), which is now
the operational sequence. Do not replay the old queue as if its fixes were pending.

## The carrier's image at 0x400000 (fixed-base 32-bit installers): done 2026-10-19, the gate is the release's

A 32-bit installer linked `/fixed` at 0x400000 cannot load in a helper (c0000018; MihawkOS refuses it with
NOTICE_FIXED_BASE32). What sits there is not our helper, which is position-independent and mapped at 0x200000000,
but the carrier websrv's elfldr starts every payload in: `/system/vsh/app/NPXS40112/eboot.bin`, whose module list in a
carrier crash dump reads `xotext: 0x400000:0x454000` (klog syscall-trace-pro-scoped-gate-01-after). Its code never
runs. The owner chose (2026-10-19) to probe whether a helper can give that range back, and approved the runs.
**Done:** step 1 `carrier-image-2026-10-19-02` (`-01` stopped in a libSceSystemService load, see IA32_DESIGN.md) and
step 2 `carrier-image-2026-10-19-03`, both PASS. One stopped probe (pid 572, named `carrier`) stays until a restart.

1. **List, read only**: `python3 tools/run-carrier-image-probe.py --name carrier-image-<date>-01 --modes list`. Pass:
   it ends and names the module holding 0x400000. Read before step 2: every mapping below 4 GiB, the carrier's
   segments and protections, and whether anything else of the carrier lies after them.
2. **Release**: `python3 tools/run-carrier-image-probe.py --name carrier-image-<date>-02 --modes list,release`. The
   release unmaps only that module's segments below 4 GiB, then maps 0x400000 read-write without replacing anything.
   Pass: every munmap status 0, 0x400000 taken and read back, and the thread (pthread_exit), signal and module load
   checks say what the list control said, with no fatal klog line. A refused munmap or a changed after-check is the
   answer, not a reason to force it.
3. **The fix, run 2026-10-19: FAILED** (`fixed-base32-2026-10-19-01`, docs/IA32_DESIGN.md): 32-bit programs at
   `0x400000` were stopped by SYSTEM_XO_VIOLATION; the title died when asked to stop; three stopped processes until
   a restart. Not shipped.
4. **Step 2 in a helper's order: passed** (`carrier-image-2026-10-19-04`): nothing below 4 GiB after `ntdll.prx`'s load.
5. **Execute-only reads: done 2026-10-19** (`carrier-xo-2026-10-19-01`, `carrier-xo32-2026-10-19-01`): plain, file and
   JIT pages, and 32-bit code from JIT pages, behave at `0x400000` as at `0x590000`.
6. **The narrower fix: done 2026-10-19** (docs/IA32_DESIGN.md): `free-0x400000-narrow-2026-10-19-01` 5/5, and a game launcher's NSIS
   installer run with `/S` (it installed, but its run's verdict is FAIL; IA32_DESIGN.md). Open: the full gate on the release with this fix; an installer driven through its windows; audio and
   controller inside a program at `0x400000`.

## The gate in grouped launches (docs/PLAN.md "The gate's time"): done 2026-10-16

gate-2026-10-16-grouped-01 passed 76 of 76 in 1,647 s; the per-case comparison run on the same build was stopped by
the user after 10 cases (all passed). `tools/gate.py run` is the grouped gate from now on.

## A suspended thread's context (patch 1172): runs 1 and 2 done 2026-10-18, run 3 open

Runs 1 and 2 were one gate: gate-2026-10-18-item3-01 passed 86 of 86 (battery 1,676 s) on build 4cd26debd444632b, main
e9b9eac with 1172 and the elfldr upload encoder; suspend-context64 in the probes launch: 46429 samples in 5 s, 0
outside the stack, 708814 system calls, 0 values or returns lost. Run 3 belongs to MihawkBrowser's landing on main.

A thread suspended while `__wine_syscall_dispatcher` returns reported its Unix registers; CEF's stack sampler read from
that `%rsp` into the no-access page under the main thread's stack (2026-10-16, record removed from the tree and in git history; `%rsp`
0x100010fc70, the frame + 0x70). Patch 1172 and the probe are host-proven (wine/patches/README.md, 1172): without the
patch 4405 of 144952 contexts were outside the stack, with it 0. Each run needs the user's go-ahead and the per-boot
setup (`export PROTON_TLS_TRUSTED_PORT=1749 PROTON_TLS_UNTRUSTED_PORT=1750`, pad awake, DNS).

1. **The probe**: `bash tools/build-production.sh` on the branch, deploy, then `python3 tools/run-console.py --exe
   stack-probe.exe --marker PS5_PROTON_STACK_PROBE --name suspend-context-<date>-01`. Pass: `samples` 1000 or more,
   `outside the stack 0`, `values or returns lost 0`. Record the samples per second: the PS5's suspend cost is not
   measured. A FAIL with `outside` above 0 prints the first `%rsp` and `%rip`: compare with the helper's kernel stack.
2. **Gate** on exactly that build: `python3 tools/gate.py run --name gate-<date>-suspend-01` (with
   `suspend-context64`); then commit. main's ungated design commits may fail shell32/64 and window-effects32/64 there
   (gate-2026-10-16-browser-01): judge those against main's own gate, not this patch.
3. **MihawkBrowser** with its stack profiler on again (no `--disable-stack-profiler`), browsing for a few minutes: no
   access violation in the browser's helper log (the browser session's run).

## Shared code copies (docs/PLAN.md "Shared code copies between helpers"): K11-K17 done 2026-10-18, the hold run open

- **Done**: `exec-share-2026-10-18-01` (owner present, title idle, end of a boot).
  - K11, K12, K13 and K15 pass: the descriptor passes, and the second payload runs the code, also at a fixed address and
    after the maker has gone.
  - K14: each process's resident count includes the object once per mapping.
  - K16: the taker's write is granted and the maker sees it.
  - K17: the same name gives a different object.
- **Open: the hold run.** It decides whether the kernel's own limit counts a shared page once.
  - Owner present, title PPSA99170 up and idle, never during a run (elfldr payloads during helper spawns jammed websrv
    on 10-11). About 4.5 min.
  - From worktree `PS5_Proton-codeshare-probe` (branch `codeshare-probe`, the probe's two files uncommitted):
    `python3 tools/run-dmem-share.py --exec5 --hold --name exec-share-hold-<date>-01`.
  - The same two payloads with a 64 MiB object. Each phase is held 45 s, past SceShellCore's ~30 s VM Stats line, and
    marked in the klog (`[exec-share] phase=`): baseline, one-mapping (the maker's), two-processes, taker-only, none.
  - The runner reads the klog throughout, from the end of klogsrv's replay buffer. The evidence's `vm_stats` holds each
    `VM Stats: RSS` reading by phase, and `fmem` each process's FMEM line.
  - **Verdict.** RSS up about 64 MiB from one-mapping to two-processes: the kernel counts per mapping, sharing saves
    nothing that matters, and the lever is closed. Flat: it counts a page once, and the census and guard must count
    shared objects once before the code is worth a gate.
  - Risks: the same calls as K11-K17 (all passed), and at most 128 MiB of flexible memory for about 45 s.

## The 4 KiB MMU (patch 1134, docs/MMU4K.md): console runs, in order (none run yet, queued 2026-10-15)

Built and host-tested; the probes pass on the PC under the system Wine. Each run passes before the next; each needs the
user's go-ahead and the per-boot setup below (`export PROTON_TLS_TRUSTED_PORT=1749 PROTON_TLS_UNTRUSTED_PORT=1750`).
Every helper log must say `wine-ps5: mmu4k on` (or why it is off) once, and never `mmu4k: fault in the engine` or
`mmu4k: off: a run did not have its signal mask` (the second is the unmeasured `uc_sigmask` place: then stop).

1. **Probes**: `python3 tools/run-battery.py --name mmu4k-probes-<date>-01 --only vm-truth64 vm-truth32
   large-shared-section payload-vm` (V1 signals first: a #UD and a #DE inside a run, the console staying up). Pass: every
   verdict PASS; the helper census line `wine-child: mmu4k` with `runs` and `alias=` above 0 for vm-truth64 and
   large-shared-section, `stack_damage=0`. Then, one per run: `--only vm-speed64` (1,024 stores in 600 us or less;
   then through `tools/run-console.py` with vm-speed64's arguments and `--env WINE_PS5_MMU4K=0`, recorded unbound,
   about 88 ms expected), `--only vm-rearm64`
   (0 violations), `--only payload-vm-contention` (no violation, no hang).
2. **Gate** on exactly this build: `bash tools/build-production.sh`, deploy, `python3 tools/gate.py run --name
   gate-<date>-mmu4k-01` (now with vm-truth32 and vm-truth64).
3. **A game workload whose uploads mix 4 KiB protections in one host page**, 60 s. Pass: no white blocks, at least
   52 fps, `opened` about 0 and zero EXITs; record the census every 10 s (`runs`, `insns`, `exits`, `stepfall`, `top`).
4. The same with `WINE_PS5_MMU4K=0` (1122's step: about 2 fps expected), for the A/B.

## Hang safety, commit A (patches 1135 and graphics 0017): console runs, in order (queued 2026-10-15)

Results on build `e6a8256b0c577838`, 2026-10-16, one boot:
- **L1** gate-hang-a-2026-10-16-01: 73 of 75 passed, doom-backstop and window-effects64 (root retakes=0) among them.
  dnsapi32/64 failed on the console's resolver, not the code: every query, the `.invalid` one included, 9002. The
  same with `WINE_PS5_MMU4K=0` (dnsapi64-mmu4k-off-2026-10-16-01), and dns-probe's getaddrinfo, the title's own
  libSceNet path, got 11001 for example.com while localhost resolved (dns-check-2026-10-16-01). The router answered
  the same names NOERROR from the PC. The console's DNS setting is the user's to fix; the gate waits for it.
- **L2 passed** (l2-broker-wedge-2026-10-16-01): `phase=test-hold` from t=17, the client's `no answer in 15 s` line,
  `wedge broker_busy_s=20` at t=36, the game's process gone (ESRCH) by t=38, the title back at the launcher after the
  hold. The title's own "gone 21485 ms after SIGKILL" is its late look (its main loop waits on the held lock), not the
  helper's end. 0 storage damage.
- **L3 passed** (l3-gpu-loss-2026-10-16-01): `gpu-broker wedged=1 reason=test_submit_failure`, only `reason=exit`
  stamps, no `payload doomed`, the title back. 0 storage damage.

What they validate is in [PLAN.md](PLAN.md), "Hang safety, commit A". The build is gate fingerprint `ca35e9d346e164e7`
(it carries the 4 KiB MMU too). Each run needs the user's go-ahead, the per-boot setup below and
`export PROTON_TLS_TRUSTED_PORT=1749 PROTON_TLS_UNTRUSTED_PORT=1750`; no payload tools during a run.

1. **L1, the gate on exactly this build**: `bash tools/build-production.sh`, deploy, `python3 tools/gate.py run --name
   gate-<date>-hang-a-01` (it serves as the 4 KiB MMU's step 2 when run on this build). Pass: every case, including
   the new `doom-backstop` (the probe's loader prints `payload doomed reason=test` 5.0-6.0 s after its stamp and the
   title comes back) and vm-truth32/64; every helper log has a `wine-child: vlock` census line with acquires above 0;
   no `wine-child: stall` line in any case. Then commit A may be made.
2. **L2, own boot**: `python3 tools/run-battery.py --name broker-wedge-<date>-01 --only broker-wedge64`. Pass: the
   flight log shows `phase=test-hold`, the title log `wedge broker_busy_s=20`, every helper sent SIGKILL gone (ESRCH),
   and the title back at the launcher, which it reaches only because the 40 s hold ends. The probe's helper may end
   itself first with `payload doomed reason=gpu` (its GPU client gives the broker up 15 s into the hold and stamps it,
   after one `gpu-client: the broker is given up on: no answer in 15 s` line); any other doom fails. After one pass
   `broker-wedge64` joins `GATE_CASES`.
3. **L3, own boot**: `python3 tools/run-battery.py --name gpu-loss-<date>-01 --only gpu-loss`. Pass: the broker logs
   `wedged=1 reason=test_submit_failure`, no helper is ended by its backstop (no `payload doomed` line: an answered
   failure is no lost broker, so nothing stamps the GPU reason), and the title stays up and returns.

## Hang safety, commit B (patch 1136): console runs, in order (none run yet, queued 2026-10-16)

What they validate is in [PLAN.md](PLAN.md), "Hang safety, commit B". After commit A's runs; same preconditions.

Results, 2026-10-16:
- On build `7c8e623917bad383`: payload-vm-contention and vm-speed64 passed (l4-payload-vm-contention-2026-10-16-01,
  l4-vm-speed64-2026-10-16-01; 0 storage damage). The probe ended inside its helper's first 10 s census window, so no
  engine line was left; the loader now prints `wine-child: mmu4k total` as a helper ends.
- On build `57ead6f6f8514184`: payload-vm-contention passed again (l4-payload-vm-contention-2026-10-16-02): `total
  runs=4181 insns=1095562`, exits `yield:4139` (a run handed the mutex to a waiting thread), protected 31, fetch 7,
  idle 3, form 1, fault 0, stack_damage 0; every store landed. 0 storage damage.

1. **L4, the gate on exactly this build**: `bash tools/build-production.sh`, deploy, `python3 tools/gate.py run --name
   gate-<date>-hang-b-01`. Pass: every case, vm-truth32/64 among them; every `wine-child: mmu4k` line ends its exits
   with `yield:`; no `mmu4k: fault in the engine` and no `payload doom seen reason=engine`. On request:
   `payload-vm-contention` (every store lands, and the census shows `yield:` above 0: a run handed the mutex to the
   re-protecting thread) and `vm-speed64` (within its bounds). Then commit B may be made.

## Stability round 2026-10-14: console steps, in order (only step 2 and V0 run)

What they validate is in [PLAN.md](PLAN.md), "Stability round after the 2026-10-14 hangs". Every step needs the user's
go-ahead. Per boot: a cold boot by hand, rest mode off, kstuff and elfldr freshly loaded with websrv (8080), ftpsrv
(2121) and klogsrv (3232), no OnionHEN, no other klog client, no payload tools during a run, one risky run per boot,
`export PROTON_TLS_TRUSTED_PORT=1749 PROTON_TLS_UNTRUSTED_PORT=1750`. After a hang: wait at least 3 min before cutting
power, salvage the logs, stop for the day.

1. **Boot** (read-only): `python3 tools/console_guard.py new-boot` before ps5vkctl is loaded. Exit 1 with storage lines:
   stop for the day.
2. **Execute in a payload: done, and it changed the design.** `exec-grant-measure-2026-10-14-01`: the kernel drops
   execute on direct, flexible, file and granted memory and leaves PRX text at 0x4, so helpers now execute from JIT
   memory only (PLAN.md, "Execute through JIT memory"). **V0 done**: `run-dmem-share.py --exec4`, the kernel alone,
   K1-K9 in `exec-kernel-2026-10-14-02` and `-03`. The JIT ladder below (V1-V5) stops at the first failure; each rung
   needs the user's go-ahead.
3. **ps5vkctl with `sync`**: `python3 tools/build-console-control.py`, load it with no title running, then
   `python3 tools/console_guard.py end-write` once: `sync: ok sync ms=N` and the same pid.
4. **Setup and deploy**: `python3 tools/console-setup.py --check`, then `python3 tools/console-setup.py` (or
   `bash ps5/tools/deploy.sh` and `python3 tools/deploy-wine.py`). The title, runtime and payload loader go together
   (protocol 0x50573511). The title must read back `all 101 files ... match`.
4b. **V1, JIT memory in Windows probes**: `python3 tools/run-battery.py --name jit-v1-<date>-01 --only payload-vm
   image-protect32 image-protect64 large-shared-section` (vm-protect-probe, image-protect, section-view-probe). Every
   probe line PASS; every helper's tally `conversions` above 0 and `convert_failed`, `exec_refused`, `sdk_refused` 0;
   no `SYSTEM_XO_VIOLATION` and no `pager error` in the klog. Then `vm-protect32-probe.exe` (WoW64 conversion, E4)
   through `run-console.py` with payload-vm's arguments; once it passes, add it to `GATE_CASES` as `payload-vm32`.
   **V2, flex mode**: the same probes through `run-console.py` with `--env WINE_PS5_PAYLOAD_DMEM=0`, same criteria.
5. **Gate, V3** (allows the commit): `python3 tools/gate.py run --name gate-<date>-stability-01`. Besides every case
   passing: `large-shared-section` shows `section of 13824 MiB in direct memory`, chunk maps, `gave back N chunks (0
   lost`; every case's evidence has `session_logs_status` 0, no `child_logs_missing`, `storage_errors` 0,
   `exec_protection` with `convert_failed`, `exec_refused` and `sdk_refused` 0, `helper_sched.scheduled.ok`;
   `payload-vm` prints a `stepped:` line under 1 s; record `memory_helper_slowest` (any answer over about 1000 ms: revisit the 5 s deadline) and the flight log of
   `gpu-window64` (one block a second; the `process rows` line). If the helpers' RR is refused, stop: the scheduling fix
   cannot apply.
6. **Ending games** (one per boot, in this order): `--only end-hang64` (P1: `SIGKILL sent (errno 0)`, each helper gone,
   census under 1000 ms after the kill), then a 256 MiB direct section ended by SIGKILL (P1b: `registry saved` and the
   launcher within 15 s), `--only end-present64` (P2: the broker shows the client closed, not wedged), `--only
   breaker64` (P-detect: `hang breaker: stalled` at about 30-40 s). If SIGKILL is refused or a process stays, stop the
   session and close the title from the home screen. Then they join `GATE_CASES`.
7. **Memory helper and Wine** (each on request): `--only section-dmem` (M1), `--only memory-stall` (M2: one dead line
   after 5000-5600 ms, the title keeps running), `--only payload-vm-contention` (C5, kstuff's #DB with three threads),
   `--only assert-probe` (P1: `completion=0xc0000420` within 5 s), then section-memory-probe with
   `PS5_PROBE_SECTION_MIB=512 PS5_PROBE_HOLD_SECONDS=90` for the chunks' page-table cost (stop if ShellCore's ptCPU
   projects past about 8,500). Then the gate's cases again with `--env WINE_PS5_PAYLOAD_DMEM=0`.
8. **Scheduling** (C2, C3): the census with `WINE_PS5_SCHED_CENSUS=1` under `keep` and under `rr` (run-window-compose,
   65 s or more), then `priority-probe.exe --args "ramp L 2000"` under `keep` (expected to stall) and, on the next boot,
   under `rr` with C2's priority and CPUs (must not stall).

## MihawkBrowser: console runs, in order (C1 run twice, 2026-10-16)

The plan and every pass criterion are in [MIHAWK_BROWSER.md](MIHAWK_BROWSER.md) §5. The user is present for every
run (a Chromium session can freeze the console), and never over another session's run.

Results, 2026-10-16, user present, build `52aa67fcbfe9f2a2` (main 322228b plus MihawkBrowser), each run's record
removed from the tree (in git history): C0 not possible (no CEF launcher is installed in `mihawk` any more). C1 run 1:
a startup fault in Chromium's NPU scan, Wine's DXCore stub (patch 1170 fixes it). C1 run 2: example.com at 200, chrome
drawn, then the guard's end mark (helpers 456 MiB in 10; the browser process 122 MiB of converted code). The console
stayed up both times. Measured next at the user's request (build `f43887469074c3ff` and an uncommitted 16 KiB-cluster
build): the default-limit run passed (a search, a launcher's store page, its installer byte-exact; CEF processes about
430 MiB); the 16 KiB-cluster run about 207 MiB, ended by a stack-profiler fault (now `--disable-stack-profiler`). A
third run is void (another session deployed during it).

**The PS5_EXEC_CLUSTER A/B** (the user chose it 2026-10-16; the rule written before the runs). Each arm was a complete
build with patch 1127's window at 64 or 16 KiB, run as the browser (example.com, held 20 s, a DuckDuckGo search, held
20 s, exit) and a game workload in the earlier emulator experiment (removed), 90 s. The rule: each arm must pass both
runs (every load 200, clean close, no guard action, no failed conversion; the game reaches its first frame). With M the
summed peak resident of the browser, renderer and StorageService helpers, T the browser's first page and G the game's
first frame, 16 KiB wins when M16 <= M64 - 25 MiB, T16 - T64 <= 1000 ms and G16 <= 1.15 x G64; otherwise 64 KiB.
Done 2026-10-16: all four runs passed and 16 KiB won (`evidence/cluster-ab-2026-10-16-ab.json`); the game timing is
the title's loading-screen close. Then `gate-2026-10-16-browser-01` on the 16 KiB build: 36 of 82 (main's
`shell32/64` and `window-effects32/64` fail on their own; three launches lost `winedevice.exe` to the exit-time
mprotect stall that 16 KiB windows expose), and C4 (2026-10-16 (record removed from the tree; in git history)): download and Open passed, the PE32
installer failed to map (c0000018); C4 is blocked on that runtime fix (the setup.exe high-base issue), not on the
browser. Host analysis: the exit killed a thread mid-conversion inside Wine's fault handler, where a kill ends a
thread at once, so it died holding the virtual mutex and the dmem engine's lock (MIHAWK_BROWSER.md §0). Fix: Wine
patch 1171, with the gate probe `kill-convert`.
The user then chose 64 KiB: the three stalled launches on the saved 64 KiB build (`stall-check64k-2026-10-16-01`) had
no stall, `winedevice.exe` exiting cleanly each time; the probes launch failed only on `payload-eight` and
`payload-memory`, whose helpers the payload loader never started (15 s, errno 35): each helper leaks one stdin TTY slot
of a 128-entry kernel table (95 leaked that evening). Done 2026-10-16/17 after a restart: `gate-2026-10-16-browser-02`
passed 83 of 83 on build `60e799a5074a3b9a` (main 762df4f, the browser, 1127 at 64 KiB, 1171).
2026-10-17: `kill-convert-before-2026-10-17-01` PASS on the build without 1171 (300 kills, 2,200 conversions, stuck
0): the probe is not sensitive (MIHAWK_BROWSER.md §0). The 16 KiB build with 1171,
`stall-check16k-1171-2026-10-17-01-probes`, was closed from the console at 85 s, the slot having collided with the
user's own benchmark. Still open for MihawkBrowser: C4 (the setup.exe high-base runtime fix), the memory ceiling
(shared code copies), 16 KiB with 1171, and a kill-convert that kills the worker as it enters fresh code.

1. **C0 `cef-cost`**: a CEF launcher's existing processes on the new backend, on whatever build is deployed (no new
   console code): `python3 tools/run-console.py --name cef-cost-<date>-01 --profile <the launcher's library entry>
   --seconds 150 --env WINE_PS5_WINDOW_SURFACES=1`. The evidence's `helpers` rows give each
   helper's program, `converted_kib`, census peak and last, images held and its vm line; read the title's
   `Wine: system memory` lines for the idle desktop's `system`/`helpers` in the first 20 s. Go/no-go: no CEF process
   above about 60-80 MiB converted (CEF 150's text is 221.3 MiB, a launcher's CEF 126's 176.2).
2. **C1 `browser-local`**: build and deploy this branch (`bash tools/build-production.sh`, then `bash ps5/tools/deploy.sh
   && python3 tools/deploy-wine.py`; the first deploy adds the 316 MB `lib/mihawk-browser`), then
   `python3 tools/run-browser.py --name browser-local-<date>-01 --limit <C0's idle census + 200>`. Pass: run-console's
   verdict, every load 200, pictures show the page, `debug.log` without a GPU retry loop, three persistent processes,
   the browser's census at most 200 MiB over the idle desktop. Then **C2** A/B (GPU in or out of process, JIT on, the
   other ANGLE arm), **C3 `browser-tls`**, **C4 `browser-download`** (`--script "download <a launcher's own installer URL>; open; wait 3000; exit"`
   with `--expect-sha256`), **C5** real launchers
   in a tool-made `work` entry, **C6** 10 min soak, **C7** teardown under load, last.

**The gate's unattended criterion** (decision 12): `browser64` and `browser-download64` join `GATE_CASES` after C6,
and run only with the user present until C1 and `browser-download` have passed five times across two boots, each with
the kernel's "RSS x of N" peak at least 250 MiB below N, no guard action, and clean MMU4K and convert counters.

## Windows 11 visual system: what is left (2026-10-16)

The visual system passed the gate (`gate-2026-10-16-win11-01`, 78/78; [PLAN.md](PLAN.md)). Still to run:

1. **Shell helper memory, same moment on both builds** (WINDOWS11_PLAN.md 5c): the session explorer.exe's
   `wine-child: vm` census at a fixed point (after the desktop is up and one Explorer window has opened and closed),
   on the build before the visual system and on this one. Budget: +8 MiB flexible.

## Completed checkpoints

- Production payload processes and descendants; eight simultaneous Windows children.
- Shared GPU broker with rendering descendants and desktop pointer/clicks.
- Process-specific controller devices and human input in a game.
- Default Mesa/Zink, loading handoff and human-confirmed helper music.
- Helper-owned shared-memory lifecycle and successful PS-menu closure.
- Application-pool backing, simultaneous 4 GiB Windows capacity/reuse and VM checks.
- Latest exact-source nine-case regression gate.

## Remaining queue

A game launcher's visual/complete-client acceptance; a game's save/reload on the current build;
a Java launcher's chain into a Java game through payloads; legitimate PE64 inputs for an FNA game; fresh-prefix
Zink staging; broader Wine/DirectX validation. Details and pass criteria are in
[PLAN.md](PLAN.md).

The [original queue](history/2026-10-07/docs/CONSOLE_QUEUE.md) retains the restart,
service-health and input investigation measurements. It is historical evidence,
not current instructions to deploy old artifacts or reset the drop guard.

## The DualSense in a game started in a new prefix's first session (2026-10-13)

**Report:** a .NET (MonoGame) game, started from its Mihawk OS desktop shortcut in the first session of a new `mihawk` prefix, ignored the pad.
Sound, video and 60 fps were fine.

**Measured so far** (three runs on 2026-10-13, records removed from the tree and in git history; scripted
pad, game started inside the desktop session by `cmd /c start` or by `tests/game-focus-probe.c`):

| Session | Game's window foreground and focus | DirectInput `acquired 054c:0ce6` | Pad presses reach the game |
| --- | --- | --- | --- |
| first session of a new prefix | yes (13 s after start) | **never** | no: in the user's run XInput read 103 presses and the game did not react (the scripted run's captures show only the desktop, so its effect is not pictured) |
| later sessions of the same prefix | yes | 8.6 s after SDL's start | yes: the title and load screens followed the script |

- Not a focus problem: with Explorer's desktop holding foreground and focus before the launch, the game's SDL window took both within 13 s
  (`game-focus at 13 s: foreground=... game_is_foreground=1`), in the failing and in the working session.
- Not a missing HID bus in the game: `HID bus start status 0; DualSense ready after 32 ms` in both.
- The first session also starts HID buses in programs outside `system32` that Wine runs while it builds the prefix: the Mono installer's
  `removeuserinstalls-x86.exe`, `installinf-x86.exe`, `installinf-x86_64.exe` and the `syswow64` `rundll32.exe`/`iexplore.exe` (four of them
  failed with `0xc0000142` after waiting 1000 ms each). Patch 0941 exempts only the 64-bit `system32` directory. These ended 20 s before
  the game's bus started, so they are a candidate and not yet the cause.
- In the failing game's log `process_hid_report ... 1 unread input reports dropped` appears 7 s after the bus: something opened the
  DualSense and never read it. The working sessions never log it.
- The registry after the first session holds the HID and USB device instances and the interface key; only the prefix that had later
  sessions also holds `HKCU\Control\MediaProperties\PrivateProperties\DirectInput\VID_054C&PID_0CE6\Calibration`, written on an acquire.

**To run, after a restart and `new-boot`** (the console dropped off the network at the end of the third 2026-10-13 run; nothing
could be run after it):

1. `python3 tools/prefix-forget-dualsense.py` on the working `mihawk` prefix, then
   `python3 tools/run-console.py --name hid-enum-forgotten --profile mihawk-os --args='Z:\data\ps5proton\prefixes\probe\drive_c\hid-enum64-probe.exe --rounds=8 --every=3000' --marker PS5_PROTON_HID_ENUM --seconds 120`.
   Pass criterion for the *diagnosis*: the probe's first rounds say where the controller search stops (SetupAPI lists nothing, CreateFile
   fails, `HidD_GetPreparsedData` fails, DirectInput lists nothing, or it lists and `Acquire` fails). If it acquires, forgetting the
   DualSense is not what the first session lacks: continue with 2.
2. A real first session: move the prefix aside as in `docs/MIHAWK_OS.md` (keep `drive_c/Games`), then the same run (`hid-enum-fresh`). The probe
   is started when Explorer is up, as the game was, and sees what the game saw.
3. Fix the layer the probe names, for every program, then repeat 1 and 2 with `game-focus64-probe.exe --exe=C:\Games\<game>\<game>.exe --click-desktop`
   and the pad script of this section's runs (`dinput acquired` must appear in the game's helper log and the captures must change).
   If step 1 or 2 shows the extra buses matter, widen patch 0941's exemption from `system32` to the whole Windows directory.

`tools/prefix-forget-dualsense.py --restore` puts the registry back.

## Queued for when the user is at the console (2026-10-12)

Needs the user present because a Chromium session can freeze the console and nobody else can restart it:

1. **B3 with the shared-direct-memory broker (patch 0009).** Run the two CEF launchers' clients and read the title's
   `Wine: system memory` lines. Before: the GPU process took the system figure from about 650 to 890 to 930 MiB and the
   guard ended it seven times. Expected now: no spike from host-visible memory; the helper census's `direct_resident`
   carries it. Compare `gpu-broker dmem_host` in the broker record (made = freed, none refused).
2. **A large launcher download's speed** with write merging (patch 1007) once memory no longer swaps: the earlier run
   was held back by pressure at 880 to 934 MiB.
3. Wine's own sections and the wineserver's shared pages (still physical shared memory in the flexible pool) through
   the same grant route.
4. **The memory guard and direct memory** (queued 2026-10-12): with `PROTON_SYSTEM_MEMORY_LIMIT=1000
   PROTON_SYSTEM_MEMORY_END=1040` for one run, watch `swap out` in the kernel's `VM Stats` lines. If it stays at 63
   while the census passes 1,000 MiB, the guard should count flexible memory only (direct-memory pages do not swap).
   (Since 2026-10-14 the title clamps a limit above 870 to 870, so this run as written now closes the program at 870.)
5. **Does the "RSS x of N" figure apply to direct memory?** `tests/memory-cap-probe.c` (built, host-checked, not yet
   on the console) commits and touches memory in 64 MiB steps, six seconds apart, first as heap (direct memory) and
   then as a section (flexible class), with the guard off:
   `python3 tools/run-console.py --name memcap-heap --exe memory-cap64-probe.exe --marker PS5_PROTON_MEMCAP --env PROTON_SYSTEM_MEMORY_LIMIT=0 --seconds 240`
   (then `--mode=section` through `--args`). Read the kernel's `VM Stats: RSS` and `swap out` lines against each step,
   and whether `elfldr` (port 9021) is still there afterwards. Heap: if RSS passes the cap with swap flat and the
   services alive, direct pages are not held to it. Section: the control, which should swap or lose services. Stop by
   hand if the console slows (watch the klog for `swap out` rising).
6. A launcher's software renderer draws a transparent window (2026-10-12 (record removed from the tree; in git history)): hardware acceleration must
   stay on until that is understood.

## A Unity D3D11 game crashed at start: DXVK's shared image (2026-10-12)

Both helpers of a 64-bit Unity game died with `page fault on execute access to 0x0` two seconds in. Read from the crash
dump and the staged `d3d11.dll` (DXVK, with symbols): the return address on the stack is
`dxvk::DxvkResourceAllocation::initKmtHandles` (RVA 0x141b24), called from `DxvkMemoryAllocator::createImageResource`
because the image is a shared resource; the call is `call [DeviceFn+0x7a8]`, and slot 0x7a8 of DXVK's `DeviceFn`
constructor is **`vkGetMemoryWin32HandleKHR`**.

**Result (2026-10-12, two runs; records removed from the tree, in git history).** The first reproduced the crash
outside the launcher and showed the cause: DXVK itself says `khrExternalMemoryWin32 : 0` and logs `Failed to create
shared resource: VK_KHR_EXTERNAL_MEMORY_WIN32 not supported` (the PS5's RADV has no external memory), and winevulkan
correctly returns NULL for `vkGetMemoryWin32HandleKHR`. So winevulkan is right; the fault is DXVK's:
`DxvkImage::allocateStorage` passed `m_info.sharing.type` to `createImageResource` even though `canShareImage` had
refused (`m_shared` false), and that calls `initKmtHandles` through the null pointer. Owned DXVK patch
`0003-an-image-that-cannot-be-shared-asks-for-no-handle.patch` asks for a handle only when `m_shared`. The second run
(60 s, patched DXVK deployed with `stage-wine.py` + `deploy-wine.py`): no `Unhandled exception`, the helper stays up,
`frames shown` goes 0 to 1172 per 10 s with the game's process in the foreground, system memory steady at 270 MiB.

## A Unity D3D11 game hitches (user session 2026-10-12, ~5 minutes; no new run, from the broker's `gpu-pacing` lines)

- The game (64-bit, Mono, Unity D3D11 over DXVK) is native code on the console's CPU; **no translator is involved**
  (prospero-win's translator is same-ISA and only for 32-bit programs).
- Its window: median frame 16.75 ms (a clean 60 Hz) in every 10 s window, but 28 to 52 fps overall: 15 to 25 % of
  frames take over 20 ms and every window has a stall of 260 ms to 2.6 s (p99). Present time is 13 to 17 ms (the
  vsync wait), so the stalls are not the GPU or the display. Steady for five minutes, so not only first-use shader
  compilation.
- The launcher's CEF window keeps being presented behind it at 2 to 4 fps with 4 to 7 s gaps and `hidden=0`, in the
  same 13 CPUs.
- Not known: which thread or call makes the stalls. Next: a game-only run (the launcher closed after launch) for the
  same minutes to separate the launcher's cost, then per-thread CPU sampling of the game's helper.

## Findings from the earlier emulator experiment (removed; 2026-10-12 and 2026-10-13)

The experiment and its runs are gone from the tree (records in git history). What it found about the runtime holds for
every program:

- **Address space: patches 1110 and 1111.** The console's probe finds 0xffffff0000 (1 TiB). A program that reserves
  a placeholder over every free address of large bands (1.6 TiB asked, 921 GiB granted) left the process no space for
  a 15 MiB image, a 4 MiB stack or a 1 MiB heap (patch 1110's report). Patch 1111 keeps a 4 GiB free placeholder at the
  top of every 64-bit process (a program's scan sees it as taken) and releases 16 MiB chunks of it when the ordinary
  search finds nothing.
- **Per-vertex fragment inputs.** On this GPU's GFX10.1-style parameter cache the second triangle of a list reads its
  per-vertex inputs rotated (PS5_Vulkan `HARDWARE_FINDINGS.md`, 2026-09-27); PS5 Mesa 1ba9874 puts
  `VK_KHR_fragment_shader_barycentric` in order in the compiler (PS5_Vulkan `docs/BARYCENTRIC_GFX101.md`, CTS
  `dEQP-VK.fragment_shading_barycentric` 18,254 of 20,991).
- **The helper's own census was costing the program.** `vm_census_report` asked `mincore` about every mapping; over a
  14 GiB shared section one census took 9 to 16 s of every 21 s, holding the address-space lock. It now has a 100 ms
  budget (`WINE_PS5_CENSUS_MS`; [PROFILING.md](PROFILING.md)).
- **File reads were not the gaps** (patch 1112): 34,304 reads, 265 MiB, 14.7 s of call time, the slowest 274 ms,
  against about 70 s of 7 to 16 s frame gaps in a 170 s load.
- **The shared-section cache was the suspect** for those gaps: a 14.5 GiB shared section faulted one 16 KiB page at a
  time through a 64 MiB cache (`wine/ps5/pw_wine_section*.c`) on a sparse file on `/data`, so every eviction of a
  written page is an `msync(MS_SYNC)`, a `munmap` and a `sceKernelReserveVirtualRange`. The real fix named then:
  back sections with direct memory through the pid grant route.
