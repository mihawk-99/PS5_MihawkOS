/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The title's hang breaker: a game in helper processes that has stopped the console's payload processes is ended by
 * SIGKILL to its helpers, from the title, while the console still answers. In a run of 2026-10-14 (record removed
 * from the tree; in git history) the GPU broker showed no frame from about alive 160 and the memory helper's census
 * froze at about alive 155 (its title log, lines 264-297, the stale-census line 273; not in the tree); the title's own loop
 * ran on to alive 360, nothing ended the game, and the console's power had to be cut.
 *
 * What decides (pw_hang_breaker_decide, pure): no new frame on the screen for PW_HANG_STALL_MS and a census as old.
 * Neither alone does. Frame gaps with a fresh census reach 161.7 s in an idle probe (dmem-gpu-dxvk64-2026-10-12-03-title
 * .log:309), 43.9 s on an idle desktop (a 2026-10-14 log, line 190) and 20.2 s in a game's load (a 2026-10-14 log
 * of the earlier emulator experiment, removed); the one census on record that went stale and came back was at most
 * about 35 s old, with frame gaps under 26 s (a 2026-10-11 launcher run's log, lines 32 and 349; not in the tree). At
 * 45 s each, the 2026-10-14 run is ended between alive
 * 198 and 205, 30 s before its scripted stop, and no run that recovered is (tests/test_pw_hang_breaker.c replays them).
 * Not seen by this rule: a wedge that keeps presenting (a game's black frames, 2026-10-14; record removed from the
 * tree), and a hang that stops the title too (two 2026-10-14 runs; records removed from the tree), which no title-side rule can act on.
 *
 * The broker wedge (pw_hang_breaker_wedged, pure), decided by the flight recorder's thread, which kept writing through
 * a stall that held the title's own loop: the GPU broker's client lock found held at PW_HANG_WEDGE_S samples in a row
 * (its broker=busy:N, native/proton_flight_recorder.h) spanning PW_HANG_WEDGE_S - 1 s or more. The span as well, because
 * a recorder that fell behind takes the samples it missed back to back: 32 rows at t=281.5 and 16 at t=328.7 in
 * salvage-20261015-092043's session 0. Measured in klog/ on 2026-10-15, 336 flight logs: at most 2 in 330; 15
 * (a 2026-10-15 run, t=52; salvage-20261015-092043's session 2 is a copy) and 16 (another 2026-10-15 run,
 * t=169) in two device losses that cleared once the helper had exited; and two wedges with every fence done, each
 * followed by a salvage: busy:239, t=40 to 278, fences 6881/6881, in a 2026-10-15 run that froze the console
 * (klog/salvage-20261015-194903; that run's flight log, at 40, is a cut-short fetch of it), and busy:252,
 * t=75 to its last row at t=328.7, fences 21073/21073, in a session of the earlier emulator experiment (salvage-20261015-092043's session 0,
 * wine-child-2145). Past 15 s the helper's GPU client has given up on the broker for good (gpu-broker client.c rpc), so
 * a game ended at 20 s loses only what it had not yet written back.
 *
 * How a game is ended (pw_hang_breaker_end): SIGKILL to each helper straight from the title, so nothing in the helper or
 * the in-title server has to answer. A pid of 1 or less, or the title's own, is never signalled. Then, before the title
 * replaces itself, pw_hang_breaker_gone: only ESRCH from kill(pid, 0) proves a helper gone (EPERM does not), each pid is
 * latched the first time it does and never asked again, and the title is not replaced while any may still run
 * (2026-10-03: replacing the title while a program's threads still ran powered the console off).
 *
 * Nothing here allocates, keeps state of its own or aborts.
 */
#ifndef PW_HANG_BREAKER_H
#define PW_HANG_BREAKER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_HANG_STALL_MS 45000u   /* no frame, and a census this old */
/* A game in the title, frames alone: three times the longest healthy gap on record. The 3D demo shows almost no frame
 * for minutes while it loads (a frame about every 60 s, its GPU submissions stopped too: a 2026-10-29 base run's
 * flight log, t=540-742; 50 s on the Pro, 2026-10-30) and then plays on, so frames cannot be read at the 45 s above. */
#define PW_HANG_TITLE_STALL_MS 180000u
#define PW_HANG_WEDGE_S 20u       /* the broker's client lock held at this many one-second samples in a row */
#define PW_HANG_MAX_HELPERS 32u   /* the title's helper table (wine/ps5/pw_wine_process.c) */
#define PW_HANG_SKIPPED (-1)      /* a pid never signalled: 1 or less, or the title's own */

typedef enum { PW_HANG_FINE, PW_HANG_STALLED } PwHangVerdict;

typedef struct {
    uint64_t present_age_ms;  /* since the screen last showed a new frame (or since the breaker was armed) */
    uint64_t census_age_ms;   /* since the newest census read whole was taken */
    uint32_t census_known;    /* 0 until a census was read whole: no census, no verdict */
    uint32_t stall_ms;        /* 0: PW_HANG_STALL_MS; a test may lower it, never raise it */
} PwHangSample;

/* EINVAL for a NULL argument. The same sample always gives the same verdict. */
int pw_hang_breaker_decide(const PwHangSample *sample, PwHangVerdict *verdict);

/* The title watchdog, for a game the title runs itself (docs/TITLE_CLIENT.md): stalled when the screen has shown no new
 * frame for PW_HANG_TITLE_STALL_MS (or a test's lower stall_ms), once it showed one after the game started (shown, 0 until then: a program that never
 * draws is not ended). The census plays no part: the game's memory is the title's, and the payload processes the
 * census reads keep running under a game hung in the title. EINVAL for a NULL argument. */
int pw_hang_breaker_title_decide(const PwHangSample *sample, int shown, PwHangVerdict *verdict);

/* 1 when the broker's client lock was found held at this many samples in a row, PW_HANG_WEDGE_S or more, and the last
 * of them came span_ms after the first, PW_HANG_WEDGE_S - 1 s or more; else 0. */
int pw_hang_breaker_wedged(unsigned samples, uint64_t span_ms);

/* kill(pid, signal): 0, or the errno it set. */
typedef int (*PwHangSignal)(int pid, int signal);

typedef struct {
    unsigned count;
    int32_t pids[PW_HANG_MAX_HELPERS];
    int32_t sent[PW_HANG_MAX_HELPERS];      /* what SIGKILL got: 0, an errno, or PW_HANG_SKIPPED */
    int32_t last[PW_HANG_MAX_HELPERS];      /* what kill(pid, 0) said last: 0 or an errno */
    uint64_t gone_ms[PW_HANG_MAX_HELPERS];  /* ms after the SIGKILL when kill(pid, 0) first said ESRCH */
    uint64_t sent_ms;                        /* when the SIGKILLs went */
    uint32_t waiting;                        /* bit i: pids[i] not yet proven gone */
} PwHangEnd;

/* SIGKILL to each of pids, skipping (PW_HANG_SKIPPED) any of 1 or less or equal to self, and record what each got in
 * *end. A pid whose SIGKILL said ESRCH is gone at once; every other one signalled, or refused (EPERM), is waited for.
 * EINVAL, with nothing sent, for a NULL signal or end, NULL pids with a count, or a count over PW_HANG_MAX_HELPERS. */
int pw_hang_breaker_end(const int *pids, unsigned count, int self, PwHangSignal signal, uint64_t now_ms, PwHangEnd *end);

/* Ask kill(pid, 0) of each pid still waited for, latching those that say ESRCH. Returns how many are still waited for
 * (0 for a NULL argument). */
unsigned pw_hang_breaker_gone(PwHangEnd *end, PwHangSignal signal, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
