# Hypervisor access on the standard PS5 (Byepervisor Reborn)

Status: measured 2026-10-21 on the standard PS5, worktree `PS5_Proton-hypervisor`
(branch `hypervisor`, from `alpha3-integration` at `b804a6b`). This file records
what the exploit was observed to do and the ranked changes it enables; it does
not claim any of those changes have been made.

## What was run and what it returned

The payload is `/data/pldmgr/payloads/local/byepervisor_reborn/byepervisor_reborn.elf`
(177,256 bytes, installed from USB at 13:37). Re-sent through websrv's
`POST /elfldr` (`pipe=1`), it printed:

```text
[+] Byepervisor reborn v1
[+] Detected firmware: 13.60
[+] Regular model detected, HV loader PA = 0x62800000
Hypervisor is already compromised
Here's a dump of kernel .text base (xotext defeated)!
E8 6B 5D 4F 00 00 00 00 ...
Here's a dump of some HV strings (hvcore+0x19830)!
... "PS Hypervisor" ...
Edited kernel .text
E8 6B 5D 4F 00 00 00 00  41 41 41 41 ...
```

That is evidence, in one run, that:

- the hypervisor is compromised on this boot (`already compromised`);
- the payload reads kernel `.text` (XOM/xotext defeated) and **edits** it
  (`41 41 41 41` written after the first qword);
- the payload reads hypervisor memory (`hvcore+0x19830` contains the HV's strings);
- it identified a non-Pro unit and the HV loader physical address (`0x62800000`).

The ELF also carries the primitives needed to repeat this from our own code:
`kernel_copyin`, `kernel_copyout`, `kdlsym`, `g_kernel_base`, `g_hv_base_pa`,
`break_hv`, `break_hv_len`, `KERNEL_ADDRESS_*` (allproc, prison0, rootvnode,
qa_flags, utoken_flags, security_flags, text_base, dmap_base, iommu_softc),
`KERNEL_SYM_*` (pmap_store, pml4pml4i, dmpml4i, idt_array, common_tss, printf,
sys_getuid, hv_mp_startup_call, mp_startup_init_vector,
acpi_sleep_machdep_hook, data_cave), `HV_SYM_HEAP_BASE`, `HV_SYM_PCPUS`.

Firmware note: the exploit reports 13.60, and its public README lists 13.60,
13.20 and 13.00 as supported. The last title run on this console printed
`firmware 13.40 installed (the system reports 13.59)`. The installed version
must be re-checked before any measurement here is compared with the K19 and
Steam results, which were taken on 13.40.

## What the exploit is (public description)

Byepervisor Reborn abuses the S3-resume `khv_startup_mp` hypercall (0x0f): on
resume the hypervisor reopens the one-shot AP-start window with APs still at
NPT root 0, so a compromised GameOS kernel can start an AP at an
attacker-chosen RIP/CR3 with the hypervisor's memory mapped. The AP then has
read/write to hypervisor memory and to every live GameOS VMCB (`NESTED_CTRL`
+0x90, `NESTED_CR3` +0xb0, `Control2` +0xb8). A VMCB write is a write to the
nested-paging root that enforces the GameOS/hypervisor split.

It ships no patches (HEN or otherwise); it is a base exploit. It needs elfldr
and a rest-mode resume, and it leaves XOM defeated and kernel `.text` writable
for the rest of the boot.

## What this gives our homebrew, and what it does not

Gives:

- Kernel `.text` read/write (persistent for the boot) — kernel patches are now
  possible without the title fighting the kernel.
- Hypervisor address-space read/write, including all 16 VMCBs — the nested page
  tables that define what memory the GameOS sees are writable.
- Kernel symbols for process/credential/flag structures (jailbreak-class flags:
  QA, uToken, security).

Does not give, by itself:

- More memory or a smaller Steam. The exploit is a primitive, not a patch.
- Persistence across a cold boot (it is an S3-resume exploit; the state lasts
  for the boot).
- A supported path on 13.40: the public support is 13.60/13.20/13.00, and the
  exploit selected 13.60 here.

## Ranked changes (highest ROI first)

| # | Change | What it unlocks | Evidence | Confidence | Risk |
| --- | --- | --- | --- | --- | --- |
| 1 | Kernel/HV R/W API for our payloads (fork the exploit's primitives; expose read/write/exec over a payload or ELF) | Every item below; HV access becomes usable by our code, not just the exploit | Symbols above; public source `EchoStretch/Byepervisor-Reborn` | High | Medium |
| 2 | OOM instrumentation: patch the kernel's allocation/panic path to print the failing allocation, `physmem`, VM thresholds, swap reservation | Finds why the console panics at ~1.9 GB instead of OOM-killing; decides the fix | Panic at ~1.86-1.99 GB RSS with the guard off; kernel text writable | High | Low |
| 3 | Raise the system-memory ceiling and retire the guard | Steam's post-login library fits; the 1800 MiB guard and its helper kills go away | Measured Steam peaks 1.9-2.0 GB; guard kills at 1.795-1.825 GB; VMCB/NPT write primitive | Medium | High (panic/brick) |
| 4 | Persistent boot patch: apply our patches on every boot through the rest-resume hook + elfldr/pldmgr autoload | The memory fix is present before the launcher starts, every boot | README's rest-mode steps; pldmgr autoload on the console | High | Medium |
| 5 | QA/debug flags via kernel R/W | Dev-menu/debug settings, faster iteration on the title itself | `KERNEL_ADDRESS_QA_FLAGS`, `UTOKEN_FLAGS`, `SECURITY_FLAGS` in the ELF | Medium | Low |
| 6 | Linux route: boot Linux with the HV defeated and run Steam/Proton there | A standard Linux + Proton stack instead of Wine inside the title; removes the system-memory partition problem entirely | Exploit README and the S3 writeup (Linux is the stated use case) | Medium | High effort, different product |

Why #3 is the payoff and #1/#2 are the route: the 1.9 GB wall is a policy
limit, not missing physical memory (the title reserves 11.7 GiB of 16; the
system side has the rest). Defeating the hypervisor does not change that policy
by itself. It makes the policy patchable: the kernel's text is writable, and the
VMCB's `NESTED_CR3` is a writable nested-paging root. #2 finds where the limit
is enforced; #3 changes it; #1 is what lets our own code do either.

## First probe (smallest question that changes the plan)

Build one payload from the exploit's primitives that prints, and nothing else:

1. `physmem`/`dmap_base` and the kernel's VM page counts;
2. the VMCB `NESTED_CR3` of core 0 and its NPT size;
3. the kernel's swap reservation and the `vm_pageout` thresholds.

If the system's usable memory is capped by a kernel variable, #3 is a kernel
patch. If it is the NPT mapping, #3 is a VMCB/NPT patch. The probe decides
which, and neither needs the title or the guard.

## Risks

- A wrong kernel/HV patch can panic the console (it already panicked once with
  the guard off at ~1.9 GB RSS); keep the exploit's restore/read-back pattern.
- The exploit is per-boot and requires a rest-mode resume; a cold boot loses it.
- Firmware support is stated for 13.60/13.20/13.00; this console's installed
  version must be confirmed before trusting a 13.60 offset.

## Measured on the base console (2026-10-21, hv-probe)

Read-only payload via websrv (`tools/hv-probe.c`), explicit host, 13.60 offsets:

```text
kernel data base 0xffffffffd1260000, text base 0xffffffffd05a0000
kernel text first 8 bytes 0x4f5d6be8 (matches the exploit's own dump)
dmap 0xffffeaa700000000
VMCB page 0x62a57000, NESTED_CTRL 0x8 (nested paging off), NESTED_CR3 0x62a8b000, Control2 0x1
NPT: 9728 entries, 31 x 1G + 1502 x 2M + 6664 x 4K, 34774 MiB mapped, guest=host identity 0x0..0x880000000
vm.swap_total 10543 MiB, vm.swap_reserved 4462 MiB; hw.physmem / vm.stats.* names absent (ENOENT)
```

Consequence for phase 3: the nested page table is an identity map of a 34 GiB
guest physical space, not the ~2 GB system window. The memory ceiling that
panics the console is therefore kernel-side (VM/physmem policy or the boot-time
system size), not an NPT restriction. Phase 3 should read the kernel's physmem
and VM limits next, not patch the NPT.

## Panic lead: the CPU page-table pool, not RAM (2026-10-21)

- `console-memory` on the base: `flexible: available 8,555,331,584,
  configured 8,589,934,592` (8 GiB). The flexible pool was nowhere near full,
  so the ~1.9 GB panic was not flexible-memory exhaustion.
- The kernel hides its own figures: `vm.stats.*`, `vm.vmtotal`, `hw.physmem`,
  `hw.usermem`, `hw.realmem` all ENOENT, including by raw MIB, and the kernel
  ships stripped (the exploit uses static offsets), so a read-only probe cannot
  read `physmem`/`vm_cnt` without 13.60 symbol offsets.
- The guard-off sign-in run's klog, last lines before the console dropped off
  the network: `VM Stats: RSS 1863.3`, `page table CPU 7784/8192` (95%);
  earlier samples 7343, 7567, 7618. The guarded run ended at census 1795 with
  the pool around 7000.
- Candidate cause of the kernel panic: exhaustion of the CPU page-table pool
  (8192 pages), not the system's RAM. Next: sample the pool through a guarded
  load and confirm it approaches 8192, or read the pool accounting through the
  kernel R/W path once its 13.60 offset is known.

## REA of the 8,192 page-table pool (kernel 13.60 text dump, 2026-10-21)

The kernel text was dumped with `hvctl` (13,369,344 bytes at the text base) and
opened in Ghidra via REA. What decides the pool:

- A static per-type parameter table at text+0x255b3e0: type 1 and 5 hold
  `0x2000/0x2000`, type 2 holds `0x800/0x1000`.
- `FUN_ffffffff9528cce0` selects the pair by the object's `+0x4` type and calls
  `FUN_ffffffff9521ef90(obj, pair[0], pair[1])`, which stores:
  - limit 1 at `obj+0x29c`, byte threshold `pair[0]*8` at `obj+0x2ac`;
  - limit 2 at `obj+0x154`, byte threshold `pair[1]*8` at `obj+0x164`;
  - a low watermark at `obj+0x2b4` (default `0x800`).
- Caps: `_DAT_...96e52080 = 0x555555` (max entries offered by the setter),
  unit `_DAT_...96e520b0 = 8`, threshold clamp `_DAT_...96e52078 = 0x600000`.
- The object is not in the kernel image: `FUN_...9528cce0` links a descriptor
  into per-type lists (`0xffffffff984a4a60` type 1, `...a58` type 2, `...a68`
  type 5); descriptor `+0x10` points at the pool object. Only type 1 exists.
- Writing the static table after boot changes nothing (it is read once at
  creation). The live limit is the object field. Patching the live type-1
  object (`+0x29c`, `+0x2ac`, `+0x154`, `+0x164`) to `0x3000`/`0x18000`
  verified by read-back; this is the candidate lever for the guard-off test.

## Second guard-off run with the live object patched (2026-10-21, panic)

- Patched live type-1 pool object: `+0x29c` and `+0x154` = `0x3000`,
  `+0x2ac` and `+0x164` = `0x18000`, all verified by read-back.
- VM Stats still reported `x/8192`; the pool reached **7757/8192** and the
  console panicked (turquoise screen) during `[wine-ps5] unmap/keep` of large
  reservations.
- Conclusion: the object limit fields are **not** the effective gate. The
  limiter is a reserve/capacity ~400-435 entries below 8,192 or a separate
  counter the allocator checks. Next REA target: the allocator that reads
  `obj+0x29c`/`obj+0x2b4` and fails the mapping; patch that check only if the
  reserve is a software policy, otherwise reduce page-table use below ~7,750.

## Goal round 1: zone hypothesis tested (negative)

- The mapper's allocation path uses `FUN_ffffffff94d3b880(_DAT_ffffffff98476968, 0, 2)`.
  At this boot's base the global resolves to `0xffff96995ff691a0`; its first
  0x400 bytes contain no `0x2000`, and the object looks like a lock/descriptor
  (0xffffffffdead0447, 0x1430000, small counts), not a UMA zone carrying a
  max-items field.
- The SDK's `vm/uma_int.h` has no `uz_maxitems` (the keg carries
  `uk_maxpages`/`uk_pages`/`uk_free`), and the PS5 kernel's layout may differ.
- Conclusion: the 8,192 is not shown to be a UMA zone cap. The working
  hypothesis returns to a **mapper arena sized at creation**.
- Next: dump the mapper class object in the config region at this boot's base
  and REA its method pointers (attach/create/map) to find where the arena is
  allocated and its size is fixed.

## Goal rounds 4-5: live mapper object (idle) confirmed

- Type-1 list head `0xffff96996eea9550` -> descriptor `+0x10` -> object
  `0xffff96996d00c000`; `+0x29c=0x2000`, `+0x2ac=0x10000`, `+0x2b4=0x800`.
- Used counters `+0x14c`/`+0x158` read 0 while no title runs: the mapper is
  idle, so the object carrying the 4,5xx page-table count is only populated
  under a title. `+0x318` -> budget object `0xffff96994411fc00`.
- Next: launch MihawkOS to activate the mapper, then re-read the same fields to
  identify the live counter and the exact instance; the allocation gate is then
  traced from that instance.

## Goal rounds 6-7: contradictory counts; mapper list traversed

- klog under the launcher: SceShellCore `page table CPU 4580/8192`, and the
  kernel itself prints `[KERNEL] INFO: CPU/GPU page table count: 0x4000 0x4000`
  (16,384 each). The two numbers disagree; the `8192` is a budget/telemetry
  ceiling, not obviously the page-table capacity.
- Traversed the type-1 list: >=8 mapper objects, each `+0x29c = 0x2000`; their
  `+0x14c`/`+0x158` counters are 0 or flag-like (one object `1`/`0x100`), so
  SceShellCore's 4,580 is not stored in those fields and is not matched by any
  plain u32 in the kernel `.data` scan.
- Next: a controlled mmap-fill payload (one 4 KiB page every 2 MiB so each
  mapping consumes a page-table page) to find the functional failure point and
  its error path; that distinguishes the 8,192 entry budget, the 16,384 page
  tables, and something else, without Steam.

## Goal round 9: fill probe parameterized (console still down)

- `tools/ptfill.c` now takes `count=` and `base=`; `tools/ptfill.py` passes
  them through. Built: `build/ptfill.elf`.
- Round 8 lesson: 4 GiB + MAP_FIXED crashed the console before any count was
  printed (all ports down). Next run uses a high, observed-free VA base
  (`base=0x1000000000`, 64 GiB) with a parallel klog capture, starting with
  `count=3000` to verify the probe is harmless.

## Goal: panic hook (#2) built and armed; first test capture invalid

- `hvctl hookinstall` verified on the console: PRINTF at `ktext(0x4ADD60)`
  (`...917edd60`), stub cave `ktext(0x121EB0)` (`...91461eb0`), scratch
  `ktext(0x2D39000)+0x100`; entry bytes became `E9 4B 41 C7 FF` and the
  cave held the stub bytes.
- First panic test (ptfill count=5000 base=0x1000000000) crashed the console as
  expected, but the poller produced no capture: payload stdout is block-buffered
  to the pipe and the runner read the response only at EOF. That is a harness
  fault, not evidence about the hook.
- Fixed: `hookpoll` sets unbuffered stdout; the runner streams the response
  chunk by chunk. Next run after restart: hookinstall -> hookpoll (background)
  -> ptfill -> read the streamed log.

## Panic hook validated for kernel prints; UDP egress blocked

- `hvctl hookpoll host=... port=0x270f` ran live (socket fd 8) and captured the
  fdstress entries: `EXEC` (a1 0x76..0x78), `# process pid=%d ... calls exit()`,
  and `[KERNEL] WARNING: called fdescfree(), but remain BUDGET_FD_FILE`.
- `hookdump` (ring readback) independently showed 7 entries persisted.
- UDP datagrams never reached the host listener (empty log, no heartbeat):
  either the host firewall blocks inbound UDP 9999 or the payload's egress is
  restricted. The stdout path works for payloads that exit; `/data` file
  delivery is the fallback.
- Still unproven: whether the panic path calls PRINTF (evidence says no). Hooking
  `panic` itself remains the next step; its address is not in the 13.60 map.

## Boundary result: the 8192 cap is real and panics

- `ptfill count=3000 base=0x1000000000 hold=25` -> VM Stats `page table CPU
  7563/8192` during the hold (baseline ~4553-4625; +3010 for 3,000 mappings,
  ~1 PT page per 2 MiB-spaced mapping); payload exited cleanly.
- `ptfill count=3700` -> console panicked (expected ~8,260). So crossing the
  8192 display cap takes the console down through the panic path, not a clean
  mmap error -- the same class as the Steam/Wine crashes.
- Next: `count=3600` confirmation (~8,15x, should survive) to pin the cap, then
  trace the pmap/PT allocation path behind the 8192 counter (which is computed,
  not a stored u32).

## Kernel page-table stats found; reported limit is not the allocator gate

- SceShellCore's VM Stats path: `FUN_...9b9a10` (shell) -> libkernel export
  `MhC53TKmjVA` (wrapper at 0x11510) and raw **syscall 671** (`WtR7OVvUgSo`).
  The shell scales a returned 512 by 16 for the `8192` display.
- Kernel syscall 671 (sysent table at `text+0x3B47E90`... actually sysent at
  0xffffffffccc1e5c0, stride 48, sy_call +8) -> stub 0x94b65f90 -> handler
  `FUN_0x953be8b0`: args (type 0..10, mode 1/2, out1, out2).
- Mode 1 callee `FUN_0x951ed4e0`: per-type table at `text+0x3B47E90`,
  stride 0x68; out1 = [entry+0x58], out2 = [entry+0x58]-[entry+0x5c].
  Live type 0: +0x58 = 0x2000 (8192 limit), +0x5c = 0x11F5 (4597 used,
  matches SceShellCore).
- Patched +0x58 to 0x3000 (verified); used stayed live, but the display
  stayed /8192 and a 4000-mapping fill (-> ~8597) still panicked. So +0x58
  is the reported limit, not the enforcement.
- The table is referenced by ~100 functions in the mapper/allocator cluster
  (0x951dc294..0x951f3e51). Next: find the write/check of +0x5c (used) there.

### Round 2: pool size is a boot memory parameter

- Boot init `FUN_0x952fa7b0` (called once from `0x952f89b1`) receives a config
  pointer; the config is filled by `FUN_0x952f8ce0`, which pulls it from the
  kernel memory-config API `0x94c998e0`.
- The per-type pool initializer `FUN_0x951ed0c0(type, base, ..., limit=6th arg)`
  is called with `limit = size >> 12` (a byte size over 4 KiB pages).
- For type 0 the size is 32 MiB -> 8192 pages. The value is a snapshot of the
  boot memory layout, taken once at init.
- Consequence: live patching cannot raise it (confirmed twice); the lever is the
  boot memory config / loader layout, consumed before the HV exploit runs.

### Round 3: config is a dmap heap object from the boot layout

- `0xffffffff96e63dc0` is a global pointer; it points into the direct map
  (`0xfffff80e030bb000`), so the memory-domain config is a runtime heap object
  populated at boot, not a static image structure.
- The pool init is boot-only: it zeroes the used counter, yet the VM Stats used
  count did not reset across a title/payload launch.
- Therefore the 8192 is fixed by the loader-supplied memory layout, before any
  post-boot code (including the S3-resume HV exploit) can reach it.
- Remaining question for round 4: is the boot memory layout changeable at all
  (loader config / pre-init hook / modified boot image)?

### Round 4: pool is set up on the app-launch path (SceAppMgr), not at boot

- The config API's object carries `SceAppMgr` strings; the config global points to
  a dmap heap object.
- With no title running, the same `text+0x3B47E90` entry is an uninitialized list
  head; the initialized limit/used entry appears when a title has launched.
- So the pool/table is initialized on the SceAppMgr launch path; the elfldr
  payload bypasses it (hence no reset during fills).
- The title's `ps5/sce_sys/param.json` declares
  `kernel.flexibleMemorySize = 1073741824` (1 GiB); prior evidence shows it
  reaches the kernel (`kernel_fmem = 0x40000000`).
- Hypothesis: the 8192 pool is sized from the title's flexible memory. Test:
  launch the title at 1 GiB and 2 GiB and read the entry's limit.

### Round 5: launch test inconclusive; address suspect

- Launched MihawkOS (PPSA99170) and re-read `text+0x3B47E90`: still the
  uninitialized list state (self-pointer list head, lock 4), same as shell-only.
- `hvctl pmap` reads zeros at `text+0x3B48FF8` (expected pmap store), so the
  exploit offset map may not match this boot's layout, or the launch did not run
  the pool init.
- Cannot yet distinguish: (a) homebrew launch does not take the SceAppMgr pool
  init path, or (b) the data segment's slide differs from the text base in this
  boot, invalidating `text+OFFSET` addresses.
- Round 6: re-derive the table address for this boot from the live code/data and
  verify the pmap store before re-testing launch.

### Round 6: table address verified; launcher does not init the pool

- `pmap` dump at `text+0x3B48FF8` is valid (code pointer + dmap pointers); the
  earlier "zeros" was the tail of the 0x200 dump. The data segment tracks the
  text base, so `text+0x3B47E90` is the correct table address this boot.
- With the launcher (PPSA99170) running, the entry is still the uninitialized
  list head: the launcher UI does not trigger the pool init.
- The init belongs to the app/child memory setup (SceAppMgr allocating the app's
  memory). Decisive test: child launch with `kernel.flexibleMemorySize` 1 GiB vs
  2 GiB via `run-child-probe.py --parameter-pointers-in-file`, reading the table
  after each.

### Round 7: the page-table size is a title parameter

- The project's history (`docs/history/2026-10-07/docs/GRAPHICS_PROCESS_BROKER.md`)
  records that SceShellCore reads, at each title launch:
  `kernel.flexibleMemorySize`, `kernel.cpuPageTableSize`,
  `kernel.gpuPageTableSize`, `kernel.extendedDirectMemorySizeInMiB`.
- `--param-kernel KEY=VALUE` overrides one of these for a single run and restores
  the file afterwards.
- This matches the kernel trace: the pool init consumes a SceAppMgr config at
  launch; `elfldr` payloads bypass it (hence no reset during fills).
- The 8192 is therefore the title's `cpuPageTableSize` (default), a per-title
  user-space parameter -- not a boot image or hypervisor value.
- Flexible memory is capped at 1 GiB (2 GiB refused 0x80020016), but the
  page-table sizes are separate fields.
- Next: run the child probe with `--param-kernel cpuPageTableSize=<N>`, read the
  limit, then push a real run past 8192.

### Round 8: param edit at /user/app did not raise the limit

- Set `kernel.cpuPageTableSize = gpuPageTableSize = 16384` in
  `/user/app/PPSA99170/sce_sys/param.json`, relaunched the title, read the entry
  (`text+0x3B47E90+0x58` = 4, still uninitialized list state) and ran a
  4000-mapping fill: the console panicked again -> the pool was not raised.
- Likely cause: the app dir has `mount.lnk` (24 bytes) redirecting the real
  content; the edited `param.json` may not be the one read at launch.
- Backup kept at PS5_Proton/build/param_orig.json.
- Next: read mount.lnk, find the real param.json the console reads, confirm the
  key/units, repeat the test.

### Round 10: params are parsed as strings; cpuPageTableSize is byte-valued

- The 9.60 shell parses `kernel/flexibleMemorySize`, `kernel/cpuPageTableSize`,
  `kernel/gpuPageTableSize`, `kernel/extendedDirectMemorySizeInMiB` (keys at
  0x1bbc278/0x1bf0f97/0x1bfbe7d/0x1c8b1df region) and passes them to the launch
  path.
- The flexible-memory validation is in byte bounds (2,097,152 .. 1,073,741,824),
  and the page-table sizes share that byte-valued interface.
- The round-8 test used `cpuPageTableSize=16384`, i.e. ~4 pages if read as
  bytes: the kernel ignored/clamped it, so the pool stayed at the 8192 default
  and the fill panicked. The lever is not disproved.
- Corrected test pending console restart:
  `cpuPageTableSize = gpuPageTableSize = 134217728` (128 MiB = 32768 pages),
  relaunch, read `text+0x3B47E90+0x58` (expect 0x8000), run the 4000-mapping
  fill.
- Backup of the title param: PS5_Proton/build/param_orig.json (restore after).

### Final: title param does not raise the pool (disproven)

- Set cpuPageTableSize = gpuPageTableSize = 134217728 (128 MiB) in the installed
  title's param.json, relaunched: SceShellCore still logs `page table CPU
  .../8192`, the kernel table at the expected address read 0, and a 4000-mapping
  fill panicked the console again.
- Conclusion: the title-level page-table params do not control the pool on this
  firmware/title. The launch-time SceAppMgr-computed config (size >> 12) remains
  the source; no user-space param changed it.
- Restore PS5_Proton/build/param_orig.json to the installed title when the console
  is next up.

### Safe gate hunt: pmap store and object flags are not the gate

- Safe 2500-mapping hold: the pmap store changed exactly one field (one +1),
  so it is not the per-mapping counter; the live count is the stats entry +0x5c.
- The map-path +0x29c/+0x2ac accesses (0x9528d824, 0x9528e21e) compare and then
  `andb $0xef` / `orb $0x10` on +0x2c1 -- threshold flag updates, no panic branch.
- Conclusion: the gate is the PT-page allocation/backing itself (the pool sized at
  boot), not the object limits nor the pmap store. Next safe probe: the stats
  entry pool pointers (+0x20/+0x28/+0x40) and the allocator during a safe hold.

### Gate identified: pool free-list exhaustion (safe probe)

- The stats entry +0x28 is a fixed 0x30-byte-node pool list; +0x20 is the free-list
  head and moves under allocation (safe 2500 fill: ...d36ef0 -> ...d37550 ->
  ...d368c0). +0x5c (used) tracks allocations exactly (0x11c1 -> 0x1b8e -> back).
- The fill boundary is used == 0x2000 <=> free list empty; the allocator then
  fails and its caller panics. The limit fields and pmap store are downstream
  symptoms, not the gate.
- Next: find the pop/empty-check on [entry+0x20] in the mapper cluster and its
  panic branch; fix options: make the failure non-fatal (live text patch,
  testable) or grow the free list at creation (boot-time only).

### Round 11: +0x20 is the dmap page-table base, not a free list

- 0x951ed620/0x951ed710 walk the stats entry +0x20 as a top-level page table
  (index by address bits) and build PTEs (0x951ed820); these are the direct-map
  translation helpers.
- The 0x30-byte nodes are page tables, not free-list nodes; the free-list
  hypothesis is retracted.
- The fill's 8192 gate is the user pmap's PT-page allocation (a different
  subsystem), tracked by the stats +0x5c counter. Next: the pmap PT allocation
  path around PMAP_STORE and its panic branch.

### Round 12: pmap allocation failure is handled (ENOMEM), panic is a caller

- pmap PT-page allocation helper 0x951de3e0 returns NULL; callers test it and
  return ENOMEM (0xfffffff2), so the allocator does not panic.
- A ud2 path exists at 0x94dde7a0 in the pmap cluster; function boundary still to
  resolve.
- The console panic therefore comes from a caller mishandling the failed mapping
  (the Wine unmap/keep case), not from the allocator itself.
- Next: the crash dump (priority #3) to capture the faulting caller PC, since the
  panic is silent.

### Round 13: the boundary caller is the Wine dmem unmap/keep path

- wine/ps5/pw_wine_dmem_ps5.c: kernel_munmap() -> dump_entries("unmap", ...) ->
  sceKernelMunmap(); the keep path (__wine_ps5_dmem_keep) completes the
  "[wine-ps5] unmap/keep" log seen at the 7757 crash.
- The same file documents console freezes unmapping >=1 GiB spans with this
  diagnostic, matching the boundary crash signature.
- Workaround route: make the large-span unmap/keep safe at the boundary (keep the
  reservation instead of unmapping when the page-table budget is tight, or handle
  the kernel error without the fatal path); verifiable with fill/Steam runs.
- No crash-dump tooling exists in PS5_Tools; building one remains an option but is
  a project in its own right.

### Round 14: dmem unmap/lay/refill mapped; mitigation route chosen

- unmap -> sceKernelMunmap; lay -> sceKernelReserveVirtualRange with the engine's
  documented "unmap and reserve" fallback; >=1 GiB spans logged by dump_entries.
- Without the crash PC the exact failing call is inferred. Route 2 (verifiable
  now): make the large-span lay/unmap bail out safely when the kernel refuses
  (keep the reservation, propagate the error) and test with fill/Steam. Route 1
  (crash dump) remains the fallback if the mitigation does not change the outcome.

### Round 15: dump support exists; readback is the open project

- The kernel data carries FreeBSD dump support (dumpdev/dumping strings), so a
  crash dump is feasible in principle.
- Readback (swap device, possible PS5 storage encryption) has no tooling; it is a
  project, not a quick fix.
- Remaining rounds: project-side mitigation (large-span lay/unmap bail-out) plus
  fill/Steam test; the dump stays the documented next diagnostic if that fails.

### Round 16: the fill uses anonymous mmap; gate is the fault-path pmap_enter

- tools/ptfill.c maps with plain mmap (anonymous, PROT_READ|WRITE, MAP_FIXED at
  2 MiB spacing); PT pages allocate on the page fault in pmap_enter.
- The boundary panic is the fault path's handling of the PT-allocation failure
  (should return a fault error; panics instead). The Wine unmap/keep crash hit the
  same underlying failure.
- Live-patch target: the failure branch in the fault/pmap path (the pool creation
  is boot-time and unreachable post-boot). Title param restored; kernel clean.

### Round 17: canary ud2 closed; pmap_enter not yet isolated

- The ud2 at 0x94dde7a0 is the stack-canary failure handler (cmp -0x8(rbp),rcx;
  jne -> call -> ud2), not a PT gate.
- The allocator (0x951de3e0) has 40 call sites; pmap_enter is among them but not
  yet separated from lookup/canary-bearing helpers.
- Gate remains the fault-path PT-allocation failure; naming the exact branch
  needs the crash PC (dump project). Round 18: one focused pass; else report
  blocked on the crash-dump requirement.

### Round 18: allocator callers not isolated by byte scan

- Prologue scans before the ENOMEM-handling allocator callers (0x94dde832,
  0x94dde8be) found no function starts; the optimized functions are large and the
  boundaries need the disassembler, not a byte scan.
- pmap_enter not yet named; exact failing branch unproven.
- Round 19: REA decompile of the containing functions; else round 20 reports the
  goal blocked on the crash-dump requirement (the only way to name the faulting
  branch), with the full evidence trail.

### Round 19: no frame-pointer prologues in the pmap cluster

- Backward scans up to 0x4000 bytes before the allocator callers find no
  push rbp/mov rbp,rsp prologue: the pmap functions are built with frame pointers
  omitted, so byte-scan function isolation fails; a Ghidra/REA decompile of the
  containing function is required.
- Round 20 decision: report the goal blocked on the crash-dump requirement rather
  than ship a speculative patch. The pool is boot-time 8192; every post-boot lever
  failed; the boundary failure is the fault/pmap PT-allocation path; the faulting
  PC needs the dump, whose readback is an unsolved project.

## Goal round 1 (crash-dump project): plumbing located, dump NOT configured, swap live

Session 2026-10-21, console boot A, kernel `.text` base `0xffffffff92e70000` (data base
`text+0xCC0000`). All addresses below are `.text`-relative ("+0x..."); data-image offsets are
also given where the string table was used. Console was reached through the ignored `.env`
host (masked here); ports 2121/3232/8080/9111 open. The `.deps/native/ps5-payload-sdk`
build path is absent in this worktree; `PS5_HV_SDK=$HOME/ps5-payload-sdk` builds
`hvctl.elf` (same flags as `tools/hvctl.py`).

Tooling added this round (read-only; `tools/hvctl.c`): `lsdev=DIR` (opendir), `stat=PATH`
(stat), `sysctl=NAME` (sysctlbyname, hex dump). Needed because the console's FTP server
answers `LIST` but not `NLST`.

### What the crash-dump code is, and where

- `"%s: init_dumpdev() failed (gp->name=%s, error=%d)"` at data-image offset `0x6a2c8c`
  (referenced from +0xade5ba). `"GEOM_DEV stuff"` at `0x6e1378`, `"dumpdev"` at `0x6e1387`
  (referenced from +0xade654).
- `dumpdev` is the **kernel environment variable** (loader tunable), not a variable of
  `kern_shutdown.c`: +0xade650 is `getenv("dumpdev")` via the FreeBSD `kern_getenv` routine
  at +0x47d400 and stores the string pointer in the data global **+0x3CF7758**; +0xade670
  reads and clears it. The value is compared against the *provider device name*.
- +0xade2a0 is the GEOM_DEV create-event handler. When a provider is created it walks the
  consumer/provider chain; if `dumpdev` is set and the provider's name matches (the code
  accepts either `name` or `/dev/name`), it does `g_access(pp, 1, 0, 0)` (+0x8f900 with
  `esi=1`) and registers a kernel-dump device whose cdev name is the literal
  `"GEOM::kerneldump"` (data `0x651c52`) through +0xa43800; on any error it prints the
  `init_dumpdev()` line with `gp->name` and the errno.
- The lower half is FreeBSD `geom_disk`: `g_disk_kerneldump` at **+0xa6e820** fills a
  `struct dumperinfo` from the disk (`d_dump` at disk+0x60, block size +0x78, max I/O +0x90;
  offset/length from the request). The ioctl dispatcher compares the cdev name at
  +0x90(cdev) with `"GEOM::kerneldump"` at +0xadfddd. String
  `"g_disk_kerneldump(%s, %jd, %jd)"` at data `0x5da7f5`.
- `kern_shutdown.c` is compiled in: `panic()` at **+0x7e4dc0** (`"panic: %s"` used at
  +0x7e4e51, `panicstr` global **+0x3A35180**), `boot()` at **+0x7f0950**, strings
  `"panicstr not set"` (data `0x6be0b4`), `"Automatic reboot in %d seconds ..."`
  (data `0x5e247c`), `"Please press any key to reboot."` (data `0x5e3ab0`).
  **`panic()`/`boot()` contain no call to a dumpsys page walker**; the only dump call on the
  reboot path is Sony's mini-dump.
- Sony `mDBG` mini-dump: entry **+0x7ef510** (mute audio, `clts`, context from +0x7fa730,
  header magic `0x1359000100000004`, writers +0xa6e040/+0xa6e160/+0xa6d550/+0xa6dd30).
  It is called from `boot()` (+0x7f0ac6 / +0x7f0adf) and from the system-error path
  (+0x7f10af). Strings: `"mDBG: Mute audio done."` (data `0x6c8ce2`), `"mDBG: Too early to
  dump. Giving up..."` (`0x6c8cfa`), `"mDBG: System error. SKIP dump for now!(system state
  left as-is)"` (`0x62c069`). The dump context is a static structure at **+0x253EBE0**
  (live read: `+0x00 -> text+0x2c0370`, `+0x08 -> text+0x7fa960`); the write helpers use a
  length-prefixed packet protocol (`0x90|len`, then bytes) through the context's function
  pointer under a kCFI range check, i.e. a **debug-transport (DECI-style) sink**, not
  (yet shown to be) a storage sink. `msgbuf` of this boot contains
  `mDBG: Waiting for debug subsystem .. 46`, matching that reading.

### Live configuration: the dump is NOT configured

- `dumpdev` env global +0x3CF7758 reads **0** on this boot.
- The static environment blob pointer at +0x352B568 -> `0xffffffff96ca3000` and the first
  `0x100` bytes there read **all zero** (`kern_getenv` therefore has no static `dumpdev`).
- `/dev` (230 entries, `lsdev=/dev`) contains **no** `GEOM::kerneldump` and no `dumpdev`
  cdev.
- No persisted kernel-panic record was found: `/user/devlog/system/sce_coredumps.0` is
  empty, `/system_data/priv/error/history/*.json` are user-facing `CE-...` errors only
  (one sample: `CE-108262-9`, 306-byte JSON), and
  `/user/devlog/system/sce_logs/log.bin` (16 MiB) contains no plaintext kernel strings
  (packed/compressed format; not decoded this round).

### The swap provider, live

- `sysctl vm.swap_total` = `0x292f00000` (10,543,481,856 B; the long-standing "10543 MiB"),
  `vm.swap_reserved` = `0x115618000` (4,462,280,704 B). Matching image globals, read live:
  **+0x2D410B0** = `0x292f00000` (swap_total), +0x2D41208 = `0xa4bc0`, +0x2D411E0 = `0x20`,
  +0x2D41098 = 4. The swap_pager cluster is +0x280000..+0x285000 (`swapongeom_locked`,
  `"Swap device %s removed."` at +0x283a04, swapoff/free at +0x283a50).
- Devices (cdev names + `st_rdev`): `/dev/swapdev0` `0xc6`, `/dev/swapdev0_blk` `0xf1`,
  `/dev/ssd0.swap` `0xd7`, `/dev/ssd0.app_swap` `0xdc`, `/dev/md0` `0xa4`,
  `/dev/ssd0.hibernation` `0xdd`, `/dev/da1` + `/dev/da1s1`. The swap sits on the SSD
  provider `ssd0.swap`; Sony's swap layer above it is the lpar manager (strings
  `"swapdev: failed to deactivate lpar%d in swapdev %d"`, `"[SWAPNSID3] ..."`, first code
  references at +0x4f40db / +0x4f4764).
- The exact `swdevt` array/`dumpcdev` globals were **not yet pinned** this round; the
  swap_pager statics are clustered at +0x2D41058..+0x2D41240 and the candidate pointer
  fields read as kernel-heap (`0xffffb113...`) pointers, so they need the `swapon`/
  `swap_pager_status` decompile rather than a byte scan.

### Decision for round 2 (not yet acted on)

Two candidate write paths, in order:

1. **GEOM dump (preferred for a storage dump).** Give `getenv("dumpdev")` a value by
   writing `"dumpdev=ssd0.swap\0\0"` into the (currently zeroed) static env blob at
   `0xffffffff96ca3000` whose pointer is already in the image, then force a provider-create
   for `ssd0.swap` (GEOM retaste; `g_retaste` log string at data `0x60cb35`, geom.ctl device
   creation at +0xc95190). Confirm `/dev/GEOM::kerneldump` appears (that is the readback
   gate) before panicking. Risk recorded before trying: `g_access(pp,1,0,0)` on the live
   swap provider may fail with EBUSY; retaste touches storage classes, so it must be done
   while no title is writing.
2. **mDBG mini-dump (fallback).** It already runs on panic, but its sink must first be
   proven to be persistent storage; the packet writer and the "debug subsystem" wait
   suggest DECI, which would put readback out of reach without a hardware debugger.

No panic was triggered this round; no kernel text or title parameter was changed. Title
param verified byte-identical to `PS5_Proton/build/param_orig.json`
(`sha256 36bd07cf…`, 900 B), i.e. still restored.

Round-1 addendum: `sysctl` probes `debug.dump`, `kern.dumpdev` and `kern.environ` all return
ENOENT on this kernel (`kern.msgbuf` exists but is 128 KiB, so the 256-byte probe buffer
returns ENOMEM — `hvctl msgbuf` reads it with a 1 MiB buffer). There is therefore no
on-demand dump trigger through a documented sysctl; the only trigger is `boot()` on panic,
and that path currently selects Sony's mDBG dump, not the FreeBSD dumper. Round 2 must
decide whether to (a) wire `boot()`/`panic()` to the FreeBSD dumper after configuring
`dumpdev`, or (b) prove the mDBG sink is persistent. Do not panic before that decision.

## Goal round 2: the real gate is the pool free list, not the 8192 field

Same boot A, `.text` base `0xffffffff92e70000`. Read-only: one held fill
(`ptfill count=3000 base=0x1000000000 hold=18`) plus a live pool walk. No panic,
no kernel patch, title param untouched.

### The allocator, the assert, and the caller chain

- **`pool_alloc` at +0x8F1E00**: `type` in `edi`, zero-flag in `esi`. It takes
  `entry = stats_table(0x3B47E90) + type*0x68`, locks `entry+0x18`, pops
  `node = entry+0x20` (the free-list head), sets `entry+0x20 = node->next`, unlocks,
  and — if the zero flag is set and `node+0x2c == 0` — memsets the page it backs.
  When the head is **NULL** it does not return NULL: it calls the kassert printer
  and `ud2` with **`"page table memory exhausted %td"`** (data `0x6d4232`,
  line `0x4c`; assert block at +0x8F1F15). This is the panic.
- Caller: **`pmap_pt_alloc` at +0x8E3060** → `pool_alloc` at +0x8E30E4; it then
  stores the VA at `node+0x20`, `1` at `node+0x28`, derives the PT page **physical
  address from the node's index in the array**, and writes the PDE/PTE.
- Caller of that: **`pmap_enter` at +0x8E2760**. On a walk miss it calls
  `pmap_pt_alloc` (+0x8E2AC5) and retries the walk; if the walk still misses and
  `-0x68(%rbp) != 0` it takes its own assert at **+0x8E2FE5**,
  `"pmap_enter: invalid page directory va=%#lx"`, and `ud2`. Both asserts end the
  same way on the console (turquoise screen, ports gone).
- **The 8192 is not enforced anywhere.** `pool_alloc` never reads `+0x58`; the
  charge function at +0x8ED550 (`limit-used < count → ENOMEM`) has **no callers**
  in the image (static function, fully inlined or dead), and the stats getter
  (+0x8ED4E0) is the only reader of `+0x58`. That is why patching the limit, the
  mapper object limits and the title params never moved the boundary.

### Node / region layout (confirmed from code and live memory)

- Nodes are 0x30 B, `arr = entry+0x28`, `head = entry+0x20`, second-region base
  `tail = entry+0x30`; nodes hold `next` at +0x00, a VA at +0x20, flag `1` at +0x2c,
  type at +0x2d (`pool_alloc` reads `+0x2d` to pick the stats entry).
- Physical page for a node: if `arr <= node < arr + pages1*0x30` →
  `phys = entry+0x40 + ((node-arr)/0x30)*0x1000`; otherwise (node `< arr` or
  `>= arr + pages1*0x30`) → `phys = entry+0x48 + ((node-tail)/0x30)*0x1000`
  (second region; `entry+0x54` = its page count). This is a designed two-region
  pool, but region 2 is empty on this console.
- **Free path** (+0x8F1380..+0x8F143D): given a *physical* page it scans the 11
  type entries (`valid` bit at `entry+0x60 & 1`), matches region 1 then region 2,
  computes the node address from the phys, sets `node+0x2c = 0` and pushes the node
  on the head. If **no** region matches, `rax` stays 0 and it executes
  `movb $0,0x2c(%rax)` — a NULL write, i.e. a second, independent fatal path that a
  bad/double free or a page outside every region would hit.

### Measured capacity (type 0, live)

```
idle:            head/arr walk: free-list 3392 nodes; used 4547; limit 8192;
                 pages1 8241 pages2 0; phys base 0x16cb0000; used+free = 7939 of 8241
after 3000 fill: used 7556, free 371, used+free = 7927 (free fell 3021, used +3009)
```

- So the true boundary is **used + free ≈ 7,930**, not 8,192: the earlier fills are
  consistent (3000 → ~7,55x safe, 3600 → ~8,14x fatal). The `8192` is a telemetry
  ceiling that happens to sit ~50 above the real capacity.
- 302 nodes of the 8241 are neither on the free list nor counted by `used`
  (leaked/uncounted). They are not a usable lever, just an accounting gap.

### Consequence for the fix (round 3 design, not yet attempted)

- The pool can grow only by adding **nodes** whose computed phys pages are free and
  dmap-mapped, i.e. by using the allocator's **region 2**: set `entry+0x48` (phys
  base), `entry+0x54` (pages), put N nodes at a kernel-writable VA `V` that is
  either `< arr` or `>= arr + pages1*0x30`, and set `entry+0x30 = V`. The allocator
  then maps node `V + i*0x30` to `base2 + i*0x1000`.
- The free path keeps this consistent because it finds the region by phys and
  reconstructs the node from `v`; **existing region-1 pages must keep matching
  region 1**, so `pages1`/`arr`/base 1 cannot be shrunk or moved.
- Blockers to solve before writing it: (a) the extra pages must be owned by the
  *kernel*, not by a transient payload (a payload's dmem is freed on exit, and the
  kernel would then hand out freed pages as page tables); (b) the node array needs
  N*0x30 bytes of kernel-writable VA that is not live data — the candidate is the
  dmap VA of the allocated block itself (put the node array in the first pages and
  set `base2` past it); (c) the free path's NULL write means every page ever
  handed out must stay inside a region forever.
- Verification instrument: the same held-fill + `ptpool` measurement, then a fill
  that today dies (3600+) and must complete, then a real title run.

### Instrument added

`tools/hvctl.py ptpool type=0` — reads the stats entry and **walks the free list**
(validating every node lies inside the array), printing limit/used/pages/phys and
`used+free`. This is the capacity measurement the earlier rounds lacked.

Round-2 addendum (design de-risking, all read-only):

- **Direct map confirmed from the image**, not guessed: globals `+0x3B49294 = 0x162`
  and `+0x3B49298 = 0x4c` give `dmap_base = 0xffff800000000000 | (0x162<<39) |
  (0x4c<<30) = 0xffffb11300000000`. The node array `arr = 0xffffb11318ce1000` is
  `dmap_base + 0x18ce1000`, i.e. exactly the byte after region 1
  (`0x16cb0000 + 8241*0x1000`): the array is a dmap allocation that starts where the
  PT region ends. So `dmap(phys) = 0xffffb11300000000 + phys` on this boot, and a
  page's physical address and kernel address are interconvertible.
- **Free/recycle verified**: after the held 3000-mapping fill exited, `ptpool`
  reads used 4547 / free 3392 / sum 7939 again — the fill's unmaps returned the
  pages through the free path, so the instrument is repeatable and region-1 frees
  are correct.
- **Physical pages are obtainable from a payload**: `libkernel.so` exports
  `sceKernelAllocateDirectMemory`, `sceKernelMapDirectMemory`,
  `sceKernelAvailableDirectMemorySize` and `sceKernelDirectMemoryQuery` (the
  payload SDK targets link them). The round-3 extender therefore can carve a
  physically contiguous block, compute `dmap(base)`, place a region-2 node array in
  the first pages of that block (so the allocator's own zeroing never touches it)
  and register region 2 via `entry+0x48/+0x54/+0x30`.
- The extender must (a) stay resident while the region is registered, (b) unlink
  every region-2 node and clear `+0x48/+0x54/+0x30` after the fill unmaps its
  pages, before the process exits — otherwise freed dmem would later be handed out
  as page tables; (c) never shrink or move region 1, because the free path
  classifies a page by its physical address.

## Goal round 3: extender implemented; direct memory is not a CPU-addressable page source

Same boot A. New `hvctl` modes: `dmemtest` (direct-memory probe) and
`growfill pages=N count=M` — the region-2 extender: allocate a contiguous block,
build the node array in its first pages, register `entry+0x48/+0x54/+0x30`, fill,
unmap, unlink, clear, release.

What the runs showed:

- `dmemtest`: `sceKernelGetDirectMemorySize()` returns **0**; `sceKernelAllocateDirectMemory(0, 0, 2 MiB, 2 MiB, 3, &phys)`
  returns **rc=0** with `phys = 0x201fa00000` (**>128 GiB**), identical on repeat.
  The shipped payload-SDK `libkernel.so` exports every `sceKernel*DirectMemory*`
  name at one zero-size address, so those are link stubs; the value we get is a
  *direct-memory (GPU address space) offset*, not a CPU physical address.
- `growfill pages=8`: the same allocation "succeeded", then
  **`kernel_copyin` of the node array to `dmap_base + 0x201fa00000` failed**
  (`growfill: copyin nodes failed`). The kernel's dmap is linear over CPU physical
  memory (`0xffffb11300000000 + phys`, confirmed in round 2), so dmem offsets are
  not reachable there. The run aborted before registration; the pool was left
  untouched (`pages2 0`, `used 4587`, `free 3328`).
- Two earlier `growfill` attempts (before unbuffered stdout was set at the top of
  `main`) produced no output and hung the request; they also aborted at the same
  copyin check (the second run is the one traced), so nothing was registered either
  time. `ptpool` still reads `pages2 0`.

Consequence: the region-2 design of round 2 stands, but **a payload cannot obtain
CPU physical pages for it from the SDK's direct-memory path**. The missing
primitive is VA→PA for our own mapping (then ordinary mlock'ed pages, kept alive by
a resident owner, would serve). The pieces for that are present but unused so far:
the exploit ELF still carries `find_pml4e`, `kdldmap`, and
`KERNEL_ADDRESS_ALLPROC`/`_DMAP_BASE`; the kernel-side path is `%gs:0` (curthread)
→ proc → vmspace → pmap → page tables via the dmap, all of which the existing
kernel-text hook mechanism could execute and record.

No panic, no kernel patch (the extender never wrote the pool), title param still
restored. `growfill`'s `count=0` currently falls back to 4200 — fix that when the
page source exists.

## Goal round 4: direct memory is a GPU aperture; the offset is not a CPU physical address

Same boot A. `dmemtest` was extended to map the block (`sceKernelMapDirectMemory`)
and to compare a written magic against the dmap of the candidate physical
addresses. One run:

```
dmemtest: allocate 2MiB rc=0 offset=0x201fa00000
dmemtest: map rc=0 va=0x200400000          (write 0x1122334455667788 at va+0)
dmemtest: base=0x2000000000 phys=0x1fa00000 dmap=0xffffb1131fa00000
           lo=0000000000120430 hi=000000000002000f   <- looks like page tables, not our data
dmemtest: base=0x0          phys=0x201fa00000 dmap=0xffffb1331fa00000  lo=0 hi=0
dmemtest: base=0x1000000000 phys=0x101fa00000 dmap=0xffffb1231fa00000  lo=0 hi=0
dmemtest: va[0]=0x1122334455667788         (the user mapping itself works)
```

- The mapping works, but **no candidate CPU physical address shows the written
  magic**. `offset - 0x2000000000` lands on kernel page-table-looking data, the
  other bases are unmapped: the direct-memory offset is a GPU-aperture address,
  not a linear CPU physical address. **There is no user-visible VA→PA for the
  block**, so the region-2 extender still cannot use it.
- Recorded consequence: the extender needs a VA→PA primitive for a mapping the
  payload owns (then mlock'ed pages, kept alive by a resident owner, can feed
  region 2 of the pool).

Lead for that primitive: the exploit ELF is still symbolic and carries a 13.60
offset table at file `0x202d0..0x20408`, with entries that match globals already
verified on the console — `0x3B49294/0x3B49298` (dmap slot/idx), `0x4ADD60`
(PRINTF), `0x3B48FF8` (pmap store), `0x2D39000` (the hook data cave) — plus
`0x83028`, `0x82A60`, `0x3BB4300`, `0x3BB6FE0` and the kernel-entry vectors.
That table, together with the pmap walk at +0x8DE3E0 and the `pool_alloc` /
`pmap_pt_alloc` code already mapped, is what a pmap-based VA→PA needs.

No panic, no pool write, no kernel patch; title param untouched.

## Goal round 5: dmap sweep wedged the console (lesson recorded)

Attempt: locate the CPU physical address of a direct-memory block by scanning
2 MiB-aligned physical addresses `0..16 GiB` through `dmap_base + phys` for the
magic written at block offset 0 (8192 reads, expected seconds).

Result: the payload produced no output and the console dropped off the network
(ports 2121/3232/8080/9111 closed, `EHOSTUNREACH` within the timeout). **A dmap
sweep is unsafe**: the direct map covers MMIO/holes as well as RAM, and a CPU read
of such a page can wedge the machine rather than fault back to `kernel_copyout`.
No crash record or PC was obtainable; the console needs a user restart.

Consequences:
- Never sweep dmap for unknown physical addresses. Only touch physical addresses
  whose mapping is known to be RAM, e.g. by walking the relevant page table first
  (a PTE with the present bit and a valid PFN), or by staying inside ranges the
  kernel itself reports (the pool region and its neighbours).
- The exploit's `find_pml4e` (disassembled from `klog/byepervisor_reborn.elf`)
  shows the short path once a pmap is known: `pmap+0x20` holds the **kernel VA of
  the PML4**, and each level is walked by reading the dmap VA and masking
  `0xffffffffff000`. So VA→PA is a page-table walk from a pmap pointer, not a
  physical sweep.
- Still missing: that pmap pointer for the payload's own process. The exploit's
  offset table (round 4) plus `allproc`/`proc`/`vmspace` offsets is the route; the
  direct-memory question is parked until then.

State after this round: console down (user restart required); nothing was written
to the pool or kernel text; title param untouched.

## Goal round 5b: offset map confirmed from the published 13.60 table

The dmap-sweep attempt crashed the console (ports closed; user restart needed), so
the rest of the round was offline analysis. Two results:

- The exploit's offset table decoded from its own code: `KERNEL_ADDRESS_ALLPROC =
  DATA_BASE + 0x28C9E80` (text-relative **+0x3589E80**), `SECURITY_FLAGS =
  DATA_BASE + 0xD9C064`, and `TEXT_BASE = DATA_BASE - 0xCC0000` (all three read
  directly out of `__kernel_init` at `0x71ab..0x71e5`).
- The published header matches the ELF table byte for byte:
  `DMPML4I 0x3B49294`, `DMPDPI 0x3B49298`, `PML4PML4I 0x3B47E4C`,
  `PRINTF 0x4ADD60`, `PMAP_STORE 0x3B48FF8`, `DATA_CAVE 0x2D39000`,
  `IDT_ARRAY 0x3BB4300`, `COMMON_TSS 0x3BB6FE0`; the separate gadget map lists
  `rep movsb` at `0x25D027`, `pop rax;ret` at `0x2912A1`, etc. So the ELF carries
  no extra hidden globals, and the pmap work can rely on this map.
- `pool_init` has exactly **one** caller, `0x9FA7B0` (text+0x9FA7B0), reached from
  `0x9F89B1` in the app-memory manager; it takes the per-type base/size out of a
  config entry (`+0xD0`, `+0xE8`, `+0xF8`, `+0x120`) and the limit as
  `size>>12`. `charge` (0x8ED550) has many real callers (0x2C77FD, 0x2C783E,
  …), so the round-2 note "charge has no callers" was a scan error (absolute vs
  relative target); the enforced value is still `limit-used`, but the free list
  empties first on this console, which is why the fill dies at ~7,93x.
- A public PS5 HLE reference describes direct memory as the title's *physical
  video memory*, pooled in 16 KiB units over a ~13.8 GiB budget, with the
  allocate call returning a physical offset that `sceKernelMapDirectMemory`
  later maps. That matches the API shape we observed; it does not make the
  0x201fa00000 offset CPU-addressable through the kernel dmap (round 4), so the
  page source question stays open.

Next (round 6): with `ALLPROC` at +0x3589E80, walk `allproc` → our proc →
vmspace → pmap → PML4 to translate a mapping we own (the safe VA→PA), instead of
touching dmap addresses blindly. Console is down until the user restarts it.

## Goal round 6: console down; safe VA->PA probe written, stronger page source scoped

The console stayed unreachable the whole round (`ping` fails, ports 2121/3232/8080/
9111 closed) after the round-5 dmap sweep: the user restart has not happened yet, so
no console experiment was possible. Work done offline:

- **`hvctl vtop va=...` written and compiled** (builds clean with `-Werror`): walks
  `allproc` at text+0x3589E80, finds our PID, then *heuristically* discovers the
  vmspace and pmap (the struct offsets are not known: it scans the proc and the
  candidate vmspace for kernel pointers and accepts a pmap whose `+0x20` is a
  canonical, page-aligned kernel VA, the `find_pml4e` contract), and only then walks
  the four levels via `dmap + (entry & 0xffffffffff000)`. It never touches an address
  the walk has not proven mapped, so it cannot repeat the round-5 mistake. First run
  will also *print* the proc field offset and the vmspace field offset that worked,
  which turns them into hard-coded constants for later rounds.
- **Next page source scoped**: instead of user-owned dmem, ask the *kernel* for
  contiguous pages from a text hook. The kernel image carries the needed strings
  (`"contigmalloc() for shuffle_buf failed."` at data 0x5AF0D4,
  `"%s kmem_alloc_contig error"` at data 0x63298C,
  `"[SCANIN:E][%s:%d] pmap_extract() returns 0 for 0x%lX"` at data 0x690ABF), so
  `contigmalloc`/`kmem_alloc_contig`/`pmap_extract` exist in this build. A hook stub
  can call `contigmalloc(size, M_WAITOK, 0, 0, alignment, 0)` once, translate the
  returned KVA with `pmap_extract`, and record the physical base in the data cave;
  userland then registers that block as region 2 exactly as `growfill` already does.
  Kernel-owned pages survive the payload exiting, which is the property dmem lacked.
  The code addresses still need locating (string cross-references use a table, not a
  direct `lea`, so the usual RIP-relative scan found nothing).

No writes to pool, kernel text or title data this round; title param unchanged.

## Goal round 7: pmap_enter's assert is a kernel-VA condition, not the fill's gate

Console still unreachable (third round; ping fails, all ports closed), so no console
work. Offline disassembly settled the pmap_enter failure path at +0x8E2760:

- At `0x8E2859` the function stores **`va >> 47`** in `-0x68(%rbp)` and the miss path
  tests it: `0x8E2AB0 cmpq $0,-0x68(%rbp); 0x8E2AB5 jne 0x8E2FE5`. So the
  `"pmap_enter: invalid page directory va=%#lx"` + `ud2` at 0x8E2FE5 fires only for
  **kernel VAs** (`va >= 0x8000000000000000`) whose page directory is missing — the
  kernel does not allocate PT pages for kernel mappings at fault time.
- For a *user* VA (`va>>47 == 0`) the miss path calls `pmap_pt_alloc` and retries the
  walk (`0x8E2AB B..0x8E2ACD`). That confirms the fill's fatal path is
  **`pool_alloc`'s "page table memory exhausted" assert**, exactly as measured in
  round 2, and that the pmap_enter assert is a separate, rarer condition.
- The retry walk uses `rax` from `pmap_pt_alloc` as the PT page (`mov %rax,%rsi`), and
  pmap_pt_alloc writes `va` to `node+0x20` without a NULL check — so "make the pool
  allocator return NULL" alone turns into a NULL write at 0x20, i.e. a second panic.
  A safe graceful-failure workaround therefore needs a cave stub that (a) checks the
  allocator's return, (b) returns ENOMEM from pmap_enter's epilogue when it is NULL,
  and (c) only then lets `pool_alloc` fail instead of `ud2`. That stub is designed but
  **not written or tested**, and it does not raise the boundary — it converts the
  console panic into a failed fault (process dies, console lives). Region-2 capacity
  is still the only way to actually pass 7,93x.

## Goal round 8: VA->PA works; direct-memory pages cannot be page tables

New boot, text base 0xffffffff91b3…, dmap base 0xffff81ee00000000.

- **Safe VA->PA proven** (`hvctl vtop`): allproc (text+0x3589E80) -> proc found by
  PID, `proc+0x200` -> vmspace, `vmspace+0x1d8` -> pmap, `pmap+0x20` -> PML4 kernel
  VA; the four-level walk (presence mask 0x50000000000001, 2 MiB PS bit honoured)
  translated our buffer to PA 0x1ea2a000 and reading `dmap+PA` returned
  `VTOPMAGIC`. No physical address is touched that the walk did not prove mapped.
- **Direct memory is physically contiguous** (`dmemvtop`): a 2 MiB block at
  aperture offset 0x201f200000 walked to PA 0x473400000, 512/512 pages mapped,
  511 contiguous steps, per-page tags read back through the dmap.
- **Registration is safe, allocation is not**: `growfill pages=1 count=0`
  (register one region-2 node, no mapping, clean up) completed and restored the
  pool. `growfill pages=1 count=1` — one 4 KiB mapping whose PT page would come
  from the direct-memory block — **killed the console instantly**. A host-side klog
  recorder on port 3232 captured only 140 bytes and no panic line, so the failure
  is immediate (machine check / hard wedge), not a printed panic.
- Interpretation (to be tested, not assumed): x86 page tables must live in normal
  write-back RAM, and Sony's direct-memory/dmap alias of GPU memory may not be WB;
  the tag reads prove CPU data access, not that the page is legal page-table
  memory. Registration alone touching no page table is harmless, which fits.
- **Crash logs on the console**: after the reboot there is no persisted panic text.
  `/user/devlog/system/sce_coredumps.0` is empty; `/system_data/priv/error/history/`
  gained only the generic `CE-108262-9` entries (0000004A at 01:24, 0000004B at
  01:29; `logReport` empty); `/user/devlog/system/sce_logs/log.bin` is 16 MiB of
  opaque/encrypted records; `/data/ps5vkctl.log` is the control payload's own log
  and contains no kernel messages. Live `klog` (port 3232) does stream kernel
  messages, so the recorder is the right tool for the next crash.
- Next candidate page source: ordinary anonymous memory (normal WB RAM). FreeBSD
  may back a large aligned anon mapping with 2 MiB pages, each giving 512
  contiguous 4 KiB pages; `hvctl anonvtop` (added) measures exactly that. If it
  yields a run, region 2 can be fed with WB pages; the dmem route is retired.

## Goal round 9: anonymous memory is not contiguous; anonvtop at 32 MiB wedged the console again

New boot (text base 0xffffffff81770000). Read-only measurements, no pool writes.

- `anonvtop pages=2048` (8 MiB anon mapping): 2048/2048 pages translated,
  **0 two-megabyte entries**, 1693 contiguous steps, **longest run 16 pages**
  (phys start 0x58e34000, then a jump to 0x431f10000 after 4 pages). The page
  allocator hands out short runs, so an ordinary anon mapping cannot supply a
  512-page contiguous region.
- `anonvtop pages=8192` (32 MiB) produced no output and the console dropped off the
  network again (ports closed, `EHOSTUNREACH`). A separate, read-only-ish probe
  (mmap + touch + page-table walks) is therefore also able to wedge this console;
  the cause is not known and the crash left no persisted record.
- Consequence for the plan: the only remaining *payload-level* WB-page source worth
  testing is kernel-issued memory (`contigmalloc`/`kmem_alloc_contig` via a text
  hook), and the direct-memory route stays retired unless the dmap attribute check
  explains the one-allocation wedge.
- Safe next check (small, after restart): read the **dmap PTE attributes** for a
  dmem physical page versus a normal RAM page through our own pmap's kernel-half
  walk and compare PAT/cache bits. That either confirms the page-table memory-type
  hazard or eliminates it.

## Goal round 10: attribute check wedged the console; allocator symbol hunt offline

- `hvctl attrs` (4 KiB anon + 2 MiB dmem mapping, then read the user PTEs and the
  dmap PTEs for both pages) produced **no output and the console dropped off the
  network again**. That probe installs nothing, registers nothing and writes only
  its own 4 KiB page — the cause is unexplained and is not assumed. It stays
  blocked until a live klog recorder catches a message at the moment of death.
- Offline, with correct string-start arithmetic (the earlier RVA scan used a
  mid-string offset, which is why it found nothing):
  - `pmap_extract` = **text+0xBEFAD0**. Verified by its only caller
    (`0x3ED61B`): args `(pmap=[r14+8], va=rsi, &pa=rdx)`, `test eax,eax` then
    `je` to a path that prints the `"[SCANIN:E][%s:%d] pmap_extract() returns 0
    for 0x%lX"` message — so 1 = success, 0 = failure.
  - The wrapper that prints `"%s kmem_alloc_contig error"` is
    `gc_allocate_system_memory_for_dir` (name string at `0xffffffff95CB540A`,
    function body from text+0x9E3950). The 7-argument call inside it at
    `0x9E3B4A` targets **text+0x43B0E0**, but that function's body calls
    `0x258FF0` (bzero), `0xC4B350`, `0xC4C300`, an internal helper and
    `0x6FD6B0` — a DMA-tag setup shape, **not** kmem_alloc_contig. Candidate
    discarded; the real call is elsewhere in the wrapper.
  - `malloc` = **text+0xC08B50** (2348 callers; `malloc(size, type, M_WAITOK|
    M_ZERO=0x102)` visible at 0x94EB917C), recorded for reference.
- Design for a kernel-owned WB page source, once the allocator is pinned: hook a
  *syscall* handler (e.g. `sys_getuid` at text+0x2A8D60) rather than PRINTF, so
  the allocation runs in our process's thread context with no kernel lock held
  and `M_WAITOK` is safe. The stub calls the allocator once, records the KVA in
  the data cave, and returns; userland then walks that KVA through its own pmap's
  kernel half (already proven) to obtain the physical base and prove contiguity
  before registering it as region 2. This avoids dmem entirely and keeps the
  pages kernel-owned after the payload exits.

Round-10 addendum: the real contiguous allocator candidate.

In `gc_allocate_system_memory_for_dir` the allocations are the calls at
0x9E3AA4 / 0x9E3D43 / 0x9E3EEE / 0x9E3F89 / 0x9E3FCF / 0x9E4021 / 0x9E40B9, all to
**text+0x503AF0** with the same argument shape:

```
mov $0x2000,%r9d ; mov $0x102,%edx ; xor %ecx,%ecx ; mov (%rax),%rdi
push $0x6 ; push $0x0 ; call text+0x503AF0
```

and every one is followed by a NULL check that prints the
`"%s kmem_alloc_contig error"` message with the wrapper's own name. The callee
(text+0x503AF0) reads the two stack arguments (`[rbp+0x10]`, `[rbp+0x18]`), takes
`edx` as malloc-style flags (0x102 = M_WAITOK|M_ZERO), looks up a memory domain
through `text+0x1F16D40` and selects between domain descriptors at
`0x97E1E2B0/0x97E1E0E8/0x97E1E640/0x97E1E478` — a contiguous, domain-aware kernel
allocator. That is the strongest candidate for `kmem_alloc_contig`; the earlier
text+0x43B0E0 candidate (a DMA-tag setup function) is discarded.

Consequence: a hook stub can replicate the gc call exactly —
`(size, 0x1000, 0x102, 0, 0, 0, 0, 6)` — and receive a KVA whose pages are
physically contiguous WB kernel memory, then translate it with the payload's own
pmap walk and register it as region 2. Still unverified on hardware; the console
needs a restart before that test.

## Goal round 11: allocator hook built; kernel printf does not fire in-run

- Assembled a proper PRINTF hook stub (`build/kmalloc_stub.s`, 125 bytes, embedded
  as `kmalloc_stub`): saves the volatile set + flags, reads a command word in the
  data cave, calls `text+0x503AF0` with the gc argument shape
  `(size, 0x1000, 0x101|0x102, 0, 0, 0, 0, 6)`, stores the KVA and the command
  state, then replays PRINTF's prologue and jumps to PRINTF+5.
- Hook install/restore verified on the console: the original entry bytes are
  `55 48 89 e5 53` (`push rbp; mov rbp,rsp; push rbx`), i.e. exactly the five
  bytes the stub replays; `kmalloc` installed the jump (`entry` readback
  `...e9`) and restored it.
- **The hook never fired during the run**: after 30 s of polling the command word
  stayed 1. `sceKernelDebugOutText` does not route through the kernel PRINTF, and
  the `[SceShellCore] FMEM` lines in the klog are user-space writes, not kernel
  PRINTF. The only reliable kernel prints observed on this console happen while a
  process exits (the `# process pid=... exit()` and `fd_file` warning lines).
- Therefore the test became two-phase: `kmallocarm` installs, arms and exits so the
  exit print fires the hook; `kmallocread` reads the KVA, restores PRINTF and
  verifies contiguity. The arm run never executed because the console went down
  between the two rounds of commands (fifth wedge; no panic record, no klog line).
- Open risk to state plainly: several recent probes (attrs, anonvtop-32M, and now
  after the kmalloc run) wedged the console without a printed panic, including
  probes that only read page tables. Until a live klog capture sees one of these
  deaths, the trigger for this instability is unknown.

## Goal round 12: the allocator hook fires, and the candidate call wedges the console

Fresh boot, all ports up. `kmallocarm size=0x100000` produced **no output** and the
console was gone immediately afterwards (`EHOSTUNREACH`); `kmallocread` could not
connect. The arm path is: read PRINTF entry -> copy the stub into the cave -> save
the original bytes -> arm the command word -> patch PRINTF with the jump -> print ->
return (process exits, and the exit path's kernel print executes the stub).

Since the previous single-phase run proved the hook does not fire while the payload
is alive and the exit print does fire, the only new event in the arm run is that
the stub actually executed and called `text+0x503AF0`. The console died at that
point. Conclusion: **calling that candidate from the PRINTF hook is fatal** — either
it is not the contiguous allocator, or the argument shape/context (kernel print
while exiting, M_NOWAIT) is wrong. Either way the "call a kernel allocator from a
text hook" route is retired; further guesses would only cost more restarts.

What remains that does not touch the kernel:
- The app-side workaround: make the large mappings 2 MiB-aligned and large enough
  that fewer 4 KiB page tables are needed (fewer scattered reservations). This does
  not raise the 7.93k pool, but it keeps real runs under the boundary. It is
  measured entirely through the title, not through kernel patches.
- Document the boundary and the eliminated hypotheses as the deliverable if the
  capacity raise is out of reach.

Record so far: the gate is named and measured (pool_alloc assert, free-list
capacity ~7.93k, the 8192 field is not enforced); the crash-dump route is dead on
this kernel (no dumpsys call in panic/boot, dumpdev unset); direct memory is
physically contiguous but not usable as page tables; anonymous memory is not
contiguous; kernel-allocator calling via a hook is fatal.

## Goal round 14: where the page tables go, and the first app-side change

Measured with MihawkOS's desktop and Steam (no game; Steam's CEF helper loops, its own problem):
pool peak **6794 used / 993 free** (VM Stats 6801), oscillating about 5550-6200, so the boundary is a
peak-live-window limit and the current workload keeps ~1.1k windows of headroom. Reclaim is already in
the kernel (`ptchurn`: map 2000 windows -> used 6561, unmap them -> used 4561 again, in-process).

2 MiB pages: a 4 MiB direct-memory mapping was backed by **1024/1024 two-megabyte entries** (page-directory
entries, no page tables) when allocated and mapped with 2 MiB alignment; a plain 8 MiB anonymous mapping
got zero. The gap was in our own code: `wine/ps5/pw_wine_dmem_ps5.c` asked for `PAGE` alignment in both
`sceKernelAllocateMainDirectMemory` and `sceKernelMapDirectMemory`, so no run could ever become a 2 MiB
entry. Fixed: large runs allocate 2 MiB-aligned, and a run whose address, size and offset are all
2 MiB-aligned is mapped with 2 MiB alignment (fallback to page alignment, klog marker `[wine-ps5] 2M map`).
`pw_wine_dmem.c`'s `back()` now splits a commit into unaligned head + whole aligned 2 MiB windows + tail
(rollback unmaps and releases the pieces before a failed one; falls back to one piece when the run table
cannot hold the pieces). Host tests: both dmem suites pass, including a new split case. Not yet on the
console; the production build of the runtime cannot start in this checkout without `tools/fetch-all.sh`.

## Goal round 18: after the 2 MiB change, with Steam — the pool is not Steam's blocker

Rebuilt the runtime from the main checkout with the 2 MiB alignment + window split + placement dump,
deployed, and ran the desktop shell with Steam.

- The 2 MiB path works: **1856 `[wine-ps5] 2M map`** events, and every commit of 2 MiB or more was
  2 MiB-aligned (`cbig == c2mal` in all seven placement snapshots: 217, 218, 287, …). Live aligned runs
  per snapshot: 25–40.
- Placement remains sparse: last snapshots `windows=1703/1108/2109`, of which **~88 % hold ≤16 of the
  16 KiB pages in a 2 MiB window (≤256 KiB)**; none is full. So 2 MiB entries can only remove the page
  table of the windows that are fully covered, which is why the pool peak did not move.
- **Pool peak with Steam: 6790 used / 1550 free** — the same as the 6794 measured before the change
  (VM Stats max 6155/8192). The pool has ~1.1k windows of headroom and is not what Steam hits.
- **The notification is the title's own system-memory guard, not the kernel pool.** The log says:
  `the console's system memory rose 120 MiB or more in 8 s to within 10 MiB of 1800 MiB (now 1809 MiB;
  helpers 641 MiB in 17, the largest 120 MiB in process 153): ending that process, not the desktop
  session` — repeated peaks at 1751/1790 MiB. The guard lives in `src/pw_memory_guard.h` /
  `native/proton_wine.cpp` (the memory helper's census). No module call failed (`refused=0` throughout),
  and direct memory was 9772 MiB of 11712 free.
- Direct consequence: Steam's failure is a title-side 1800 MiB budget being exceeded by its helpers,
  independent of page tables. The pool work neither caused nor can fix it; the 2 MiB/consolidation work
  reduces page tables (a different resource), and the measured peak shows it was never the constraint
  here.

## Goal round 19: the attribution settles Option 1 — consolidation is not the lever

Ran the desktop shell + Steam again on the build carrying the attribution (windows split by the reservation the
run lies in: `big` = inside a reservation >= 4 MiB, offsets chosen by the application; `small` = inside a
smaller reservation the port placed).

| snapshot | windows | sparse (<=256 KiB) | full 2 MiB | big | small |
|---|---|---|---|---|---|
| desktop peak | 2137 | 1753 | 88 | **1930 (90%)** | 207 (10%) |
| next | 1339 | 1015 | 113 | **1145 (86%)** | 194 (14%) |
| a child | 206–209 | 123–126 | 30 | 122–125 (59%) | 84 (41%) |
| a small child | 120 | 82 | 2 | 59 (49%) | 61 (51%) |

In the phases that matter (the desktop and Steam) **86–90% of the touched 2 MiB windows lie inside reservations
of 4 MiB or more**, where the application chose the offsets: no address-placement change can move those pages.
Only ~10–14% of windows are in port-placed small reservations, so packing them could save at most ~200 of 2137
windows — not worth a risky Wine placement change. The earlier "consolidation is the big lever" reading was wrong;
this run corrects it.

The 2 MiB path remains effective where it can be: all commits >= 2 MiB were aligned (`cbig == c2mal` in every
snapshot: 288/288, 180/180, 57/57, ...) and full windows do appear (88/2137, 113/1339). Page tables are simply not
Steam's constraint: this run peaked at 5058 used (Steam's helpers were ended early by the memory guard), the
previous one at 6790.

## Goal round 20: the pool reaches its display limit under Steam Install

With the guard limit at 2000 MiB and the pool traced every 200 ms through klog (`hvctl ptlog`, launched detached so
it does not hold the elfldr the runtime spawns helpers through), Steam was taken to login, library and Install.

- **Page tables: `used=8192 limit=8192`** (VM Stats `page table CPU 8174/8192`), and **no panic**: the samples then
  fell to `used=7567–7570`, so reclaim returned tables. The kernel's pool has 8241 nodes, so 8192 used leaves ~49
  spare: the real wall is above the display limit, and Install comes within tens of nodes of it without crossing it.
- The guard did not fire: census peak 1865 MiB (`0 s of the last 30 at the 2000 MiB limit`); the notification Steam
  showed was its own memory error dialog.
- The console reached **RSS 1988 MiB, swap-out 302 MiB**.
- Steam's windows turned black afterwards, and the log names why: `[KERNEL] ERROR: Not enough flexible memory to
  load "executable": map size: 0x54000, available: 0x211130000`. A helper's executable could not be loaded, so the
  renderer never started (`frames shown 0 in 10 s`). The stress markers at that moment: **descriptors 8870/12555 with
  `devices=7295`**, 19,682 sync timeouts, 61 dropped objects, direct memory in 4006 ranges (largest 6734 MiB of 8360
  free), flexible 942 MiB free.

Correction to a trap met on the way: a tracer launched with `pipe=1` (hvctl.py's default) holds the console's
`/elfldr` for the payload's whole life, and the runtime spawns every helper through that same `127.0.0.1:8080`
endpoint: Steam's webhelper then died of `payload loader failed at connect … errno=61` (ECONNREFUSED) and showed no
UI. The fix is `hvctl.py … detach` (`pipe=0`), which starts the payload and releases the loader; the trace then
streams to klog without blocking any helper.

## Goal round 21 (2026-11-26, branch `fast-steam`): elevated Sony local processes + 65,536 CPU / 32,768 GPU app page tables solve page-table exhaustion without hvctl

On the standard PS5 (firmware 13.60), raising `ps5/sce_sys/param.json` to
`kernel.cpuPageTableSize = 268435456` (**256 MiB = 65,536 CPU PT**) and
`kernel.gpuPageTableSize = 134217728` (**128 MiB = 32,768 GPU PT**) leaves
**11,456 MiB** of direct memory and expands the application page-table pool 8x
above the 8,192-entry system pool without any kernel patch or `hvctl`.

Because `elfldr` payload helpers draw from the 8,192-entry system pool, heavy
64-bit Steam games running in payload helpers still tripped the system page-table
guard when mapping 3–6 GiB quickly. Moving 64-bit Steam games and
`steamwebhelper.exe` into Lapy-elevated Sony local processes
(`/app0/wine-local-child.bin`, spawned via `sceSystemServiceAddLocalProcess`)
routes 100% of their page tables into the 65,536-entry application pool while
leaving `0` page tables charged to the 8,192-entry system pool:

- **Standalone probe (`evidence/sony-local-elev-pt-probe-02.json`):** after Lapy
  elevation (`euid=0`), `jit_made=0`, `jit80_made=0`, `jit80_value=42`,
  `way=jit prot=5 value=42`; CPU page tables moved `65,513 -> 65,396 -> 65,513`
  of `65,536` (100% reclaimed on `sceSystemServiceKillLocalProcess`) and title
  flexible memory moved `1,031,798,784 -> 960,495,616 -> 1,031,798,784` bytes
  (100% reclaimed).
- **Heavy 3D 64-bit game (`evidence/p3-local-village-2026-11-26-02.json`):**
  `4.5 GiB` direct memory, `7,636` direct flips at `119.9 FPS`, **`62,395 / 65,536`
  CPU PT free** (95.2% free), `32,690 / 32,768` GPU PT free, and **`3,721 / 8,192`
  system PT free**.
- **3D Direct3D 12 demo (`evidence/p3-local-re3demo-2026-11-26-01.json`):**
  `4,802` direct frames at `117.7 FPS`, **`62,543 / 65,536` CPU PT free**,
  `32,277 / 32,768` GPU PT free, and **`3,819 / 8,192` system PT free**.
- **2D 64-bit game launched twice in one Steam session (`evidence/p3-e2e-spelunky2-twice-2026-11-26-01.json`):**
  `63,661` (run 1) -> `64,574` (after exit 1) -> `63,644` (run 2) -> `65,002`
  (after exit 2) free of `65,536` CPU PT, and `3,929 -> 3,971` free of `8,192`
  system PT (zero leak across repeated launches).
