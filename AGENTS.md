# AGENTS.md — instructions for an agent working in PS5_Proton

Read this first, then [`docs/PLAN.md`](docs/PLAN.md) (what is being built, in what
order, and what is already proven) and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
(who owns what). The human README is [`README.md`](README.md); provenance and licences
are [`NOTICE.md`](NOTICE.md) and [`LICENSING.md`](LICENSING.md).

> **Contributing from a fork?** These instructions were written for the maintainer's own agents. Rules 1 to 7 and 9
> apply to everyone as written. Rule 8's "work on `main`, locally" and rule 10 are the maintainer's. In a fork you work
> on a branch, push it to your fork and open a pull request against `main`, as [`CONTRIBUTING.md`](CONTRIBUTING.md)
> and [`skills/contribute/SKILL.md`](skills/contribute/SKILL.md) describe. Run their checks before you open it.

## What this repository is

A PS5 homebrew title that **launches 64-bit and 32-bit Windows games** and a **Wine runtime** they
run on. The launcher is an ordinary title on my
[PS5 Vulkan Template](https://github.com/mihawk-99/PS5_VulkanTemplate) foundation
(Vulkan display through RADV, the DualSense, AudioOut, the test harness). The runtime is
Wine carrying Proton's stack, pinned from my
[GE-Proton fork](https://github.com/mihawk-99/proton-ge-custom), with the PS5 patch
series that makes Wine's Unix side run on the console.

The `win-32bit` work is now part of local `main`: **32-bit (IA-32) Windows
programs** run natively through WoW64 (rule 9). Focused probes and short real-game
sessions have console evidence; broader compatibility and API conformance remain
work to qualify, not universal support claims.

Windows programs run as separate isolated processes by default: `elfldr` payload
helpers for 32-bit programs, Wine services, and direct-launched helpers, and
pre-warmed Lapy-elevated Sony local processes (`/app0/wine-local-child.bin`,
built by `tools/build-local-child.sh`) for 64-bit store-client games and the
client's browser so they share the title's expanded 65,536 CPU / 32,768 GPU
application page-table pool (`ps5/sce_sys/param.json`) without consuming the
8,192-entry system pool or closing and reopening the store client across games.
The title owns the shared wineserver, GPU broker, display, controller feed and
audio mixer. OpenGL uses Mesa/Zink through Vulkan/RADV. Helpers use the shared
application memory pool (`11,456 MiB` direct memory with 256 MiB CPU / 128 MiB
GPU page tables); see [docs/WINDOWS_PROCESSES.md](docs/WINDOWS_PROCESSES.md)
and [docs/PLAN.md](docs/PLAN.md) for measured capacity and limits.

The knowledge base for the Wine side is
[prospero-win](https://github.com/mpereiraesaa/prospero-win) — read
`docs/WINE_PS5_BUILD.md` and `docs/ARCHITECTURE.md` there before touching anything in
`wine/`. It has already solved: Wine as PRX modules, the in-process wineserver, the
`win32u` PS5 user driver, winevulkan display-plane integration, the audio driver, XInput
on the DualSense, `/data` mounting, and running one game per process.

## Rules that are never broken

1. **Directories beside this one are read-only.** `prospero-win-main/`,
   `PS5_VulkanTemplate/`, `PS5_Vulkan/`, `PS5_Mesa/`, `proton-ge-custom/` are references
   and dependencies. Change them only in their own repositories, deliberately, and only
   when the task says so.
2. **A title never calls `exit()` and never returns from `_start`** — it asks the shell
   to close it (`exitFatal` throws on the console). The launcher restarts the title with
   `sceSystemServiceLoadExec` to run a game in a clean process; a game ends by returning
   to the launcher.
3. **RADV is linked only with PS5_Vulkan's recipe.** Never hand-write the link.
4. **No game data, ever.** No game files, saves, keys, ISOs or firmware in the
   repository, in a commit, in a release or in a log line. The user supplies their own
   copy; the launcher reads it from `/data`.
5. **Nothing private leaves the machine.** The console's address lives only in an ignored
   `.env`; mask addresses in anything printed or pasted.
6. **Only clear licences are vendored.** Every upstream brought in gets its revision
   pinned, its licence recorded in [`NOTICE.md`](NOTICE.md), and, if it is patched, its
   patch series in this repository (never a hand-edited vendor tree).
7. **Prove it on the console before claiming it.** A README line, a commit message or a
   report says what a run showed, at the same level of detail whether it passed or
   failed. "It builds" is not "it runs"; "it started" is not "it drew the right frames".
8. **Work on `main`, locally, unless the user requests another branch.** The current
   request selects `main`. A subject line that says what changed and a body that
   says why and what was proven, with numbers. Upstream files change as little as
   possible, so revisions stay mergeable.
9. **32-bit Windows programs are part of `main`, promoted from `win-32bit`.** IA-32
   executables follow the design in [`docs/IA32_DESIGN.md`](docs/IA32_DESIGN.md):
   native execution in compatibility mode, every guest↔host crossing through one
   measured path, and a small exact interpreter as the fallback. prospero-win is a
   reference we learn from, never copy ([`docs/IA32_REFERENCE_REVIEW.md`](docs/IA32_REFERENCE_REVIEW.md),
   reviewed at `a4d3e73`; the sibling `../prospero-win-main/` is an older snapshot).
   Nothing claims 32-bit support until a 32-bit program has run on the console
   (rule 7), and 64-bit programs keep passing the gate battery on every code commit.
10. **Push only when I ask, and only named branches.**
    The GitHub repository is public. Commit locally on `main`
    (or the requested branch) as often as the work needs;
    push only when I ask in the current request, only the
    named branch (never `--all` or `--mirror`), never force
    without my explicit go-ahead, and only after the pre-push
    privacy check passes (`tools/privacy_check.py`). Never
    create tags or releases unless I ask.

## Where the knowledge is

| Question | Read |
| --- | --- |
| What is being built, in what order, what is proven | [`docs/PLAN.md`](docs/PLAN.md) |
| Which part owns what, how a game boots | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| How Wine's Unix side was made to run on PS5 (patches, PRX, server, drivers) | `../prospero-win-main/docs/WINE_PS5_BUILD.md`, [`wine/patches/README.md`](wine/patches/README.md), [`docs/WINDOWS_PROCESSES.md`](docs/WINDOWS_PROCESSES.md) |
| What a PS5 title's foundation offers (build, deploy, run, tests, pad, klog) | `../PS5_VulkanTemplate/AGENTS.md`, `../PS5_VulkanTemplate/skills/ps5-homebrew/` |
| How to run, deploy, debug and read a crash on the console | `../PS5_VulkanTemplate/skills/ps5-console/` |
| Proton's components and their licences | `../proton-ge-custom/dist.LICENSE`, `Makefile.in`, `.gitmodules` |
| What the console's RADV does and does not support | `../PS5_Vulkan/docs/CTS_GAPS.md` |
| What each optimization round tried and measured | [`docs/OPTIMIZATION_LEDGER.md`](docs/OPTIMIZATION_LEDGER.md), `evidence/` |
| Plain-language accomplishments and proof limits | [`docs/FEATURES.md`](docs/FEATURES.md) |
| How code, probes, tools and documents are designed for the agent's loop | `../PS5_VulkanTemplate/skills/ps5-agent-native/SKILL.md` (the method), [`skills/proton-agent-native/SKILL.md`](skills/proton-agent-native/SKILL.md) (here) |
| How 32-bit programs run, and what was learned from prospero-win | [`docs/IA32_DESIGN.md`](docs/IA32_DESIGN.md), [`docs/IA32_REFERENCE_REVIEW.md`](docs/IA32_REFERENCE_REVIEW.md) |
| How to contribute from a fork, and the checks before a pull request | [`CONTRIBUTING.md`](CONTRIBUTING.md), [`skills/contribute/SKILL.md`](skills/contribute/SKILL.md) |
| Dated investigations and obsolete defaults | [`docs/history/2026-10-07/README.md`](docs/history/2026-10-07/README.md); historical instructions do not override the current plan |

## How this repository is designed: for the agent's loop

**This is extremely important.** Agents write all of this code; I direct the work and
judge what it does. So the repository is designed for the agent's loop, not for a
human reader:

```text
inspect → hypothesize → change → build → deploy → collect evidence → compare → iterate
```

What limits progress is never typing speed or the language's level. It is
comprehension (how much the agent must read and reconstruct before changing
something correctly), build latency, test latency, observability (whether a failure
shows up in a record at all) and feedback quality (a verdict against a named
reference, or a log someone must read). Every design decision is judged by those
five. The method is `../PS5_VulkanTemplate/skills/ps5-agent-native/SKILL.md`; what it
means here is [`skills/proton-agent-native/SKILL.md`](skills/proton-agent-native/SKILL.md).
Read both before designing code, a probe, a tool or a document. In short:

- **Write in the representation with the most meaning.** C/C++ for our code, with the
  compiler doing the machine's work. Assembly only where the abstraction itself is
  the problem (entry and exit stubs, saving CPU state, ABI and system-call
  transitions), each piece checked against a C version. Machine code (a game's PE, a
  crash address, guest code) is read through tools, never by eye.
- **Code an agent can change from one place**: flat `pw_<subsystem>_<verb>`
  functions, explicit state and ownership, the contract and the measurement that
  shaped it in the header (`src/pw_audio_ring.h` is the model), invariants checked
  with typed errors and never `abort()`. Wine's files keep upstream's shape (rule 8);
  PS5 logic lives in `wine/ps5/` and is host-tested.
- **Every question has a command that ends in a verdict**: host tests in seconds,
  probes through `tools/run-console.py` writing `evidence/<run>.json`, the gate
  battery. Compare against an oracle: the pinned Wine on Linux, the same program on
  the PC, the previous run. A failure found once becomes a permanent check.
- **The console is an instrument, and its time is the scarcest in the loop.** Each
  run answers a question decided before it. When a failure is not in the records,
  the next run adds what would show it, not a guess. Then fix the layer the evidence
  names, for every program.
- **Knowledge lives where the next agent starts**: invariants beside the code, status
  in `docs/PLAN.md`, measurements in `evidence/`, failures recorded at the same detail
  as passes.

## Working in this repository

- **Build steps land in `tools/`** as scripts, and every one of them is idempotent and
  pins its inputs (revision, tarball, hash). Nothing is fetched implicitly at build time
  without a pin.
- **Portable code goes in `src/` and is host-tested**; anything that touches the console
  goes in `platform/` (or `native/`), and anything that patches Wine goes in `wine/`.
- **Test runs** follow the template's harness (`ps5/tools/run.sh`); Wine probes and games
  run through `tools/run-console.py`, which ends a run through the title's own paths and
  writes a small `evidence/<run>.json`. Pictures and klog stay in `klog/`, never
  committed. Host tests: `tools/host-tests.sh`.
- **Commits**: on `main` or the branch explicitly requested by the user, a subject
  that says what changed and a body that says why and what was proven, with numbers.
  For documentation-only work, identify the existing evidence and say that no new
  console run was made. Push only as rule 10 allows.
- **The commit gate is mandatory.** Code is committed only when the console passed the
  gate battery on a build of exactly that code: `bash tools/build-production.sh`, deploy,
  then `python3 tools/gate.py run --name <run>`. The pre-commit hook
  (`git config core.hooksPath tools/git-hooks`) refuses anything else; commits of only
  notes and evidence (`docs/`, `evidence/`, `*.md`) pass without it. A check every
  commit needs goes into `GATE_CASES` in `tools/run-battery.py`. Never bypass the hook.
