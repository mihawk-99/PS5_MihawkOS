# Launching another homebrew after MihawkOS exits

Reviewed 2026-10-07. A reproduced launch-after-exit failure belonged to
ShadowMountPlus's ShellCore launch-hook lifetime. It was separate from the later
payload shared-memory closure panic and system-memory pressure issues.

## Reproduction and fix

On 10.01, game return through LoadExec followed by launcher closure left Samples
unable to launch (`0x80940033`). Proton's recorded helpers were absent; the mounting
service remained alive, but its launch hook had reverted to original instructions.
Direct PS-menu closure did not reproduce that failure in the first tests.

The cause the evidence points to: ShellCore's launch-hook pages are paged out and reloaded with their original
instructions. Keeping those pages resident before installing the hooks made the longer sequence complete on tested
10.01 (Samples then rendered 120 frames and exited with status 0); pageout and reload were not instrumented directly.
MihawkOS does not ship or carry a patched ShadowMountPlus.

**Fixed upstream (2026-10-20).** PR 139 proposed that change; upstream closed it and committed its own version of the
same retention the same day (`35fff9fc`, "Retain ShellCore hook pages before physical writes"; it also rounds each
range to whole 16 KiB pages). It is in 1.7beta6 and every later release. MihawkOS asks for beta11 or newer on its payload
check (`src/pw_payload_check.h`; since 2026-10-31 only its presence is checked), so it carries no mlock of its own: it could not, without
ShadowMountPlus's per-firmware hook addresses, and two lockers of the same pages would add nothing. On the owner's
base PS5 (13.60), 1.7beta10's log said `[SHELLCORE] hook pages retained: cave=1 sites=3` before installing its hooks
(`evidence/payloads-base-2026-10-29-01.json`). The full game, launcher, close and next-homebrew sequence has not been
repeated on 1.7beta10; it was proven on 10.01 with the PR's build (below).

The original reproduction, hashes, baseline host-test failure and earlier
user-authorized upstream publication record remain in
[the launch-after-exit history](history/2026-10-07/docs/LAUNCH_AFTER_EXIT.md).
That past publication does not authorize publishing Proton work now.

## Validation contract

Use `tools/run-console.py --launch-diagnostics` to capture control, mount and hook
state around a game run. Then launch another homebrew and require actual rendered
frames and normal exit. An idle app list or a successful Proton close alone does
not prove the next homebrew will start. Record the mounting-service build and
Proton build separately. Saving a payload file does not configure boot persistence.

The current allocator's live-2-GiB-helper shell-close test passed independently;
see [WINDOWS_PROCESSES.md](WINDOWS_PROCESSES.md). A fresh follow-up-homebrew launch
is still required when accepting changes to mounting or teardown behavior.
