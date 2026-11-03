/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The freeze snapshot's trigger: when a game that has drawn stops drawing for PW_FREEZE_SNAPSHOT_MS while it still runs,
 * the title records what every thread is doing and waiting on, once, and leaves the game running; when the game draws
 * again, it says so. A game that stops drawing and ends itself within a minute leaves no word in the log of which thread
 * waited on what. The title watchdog's dump comes at 180 s and ends the game (src/pw_hang_breaker.h); this one comes
 * early and ends nothing.
 *
 * Sampled once a second by the title's loop with the hang breaker's present age (suspends taken out, src/pw_suspend.h):
 * - never before the game's first frame (its own loading screen or a start that never draws), nor while the app is away
 *   or just back;
 * - once per stall: then quiet until a new frame comes (the age falls), which is reported as a recovery and re-arms;
 * - at most PW_FREEZE_MAX_SNAPSHOTS a session, so the log stays bounded.
 * A load that draws nothing for 20 s takes one snapshot and then reports its recovery: a few hundred lines, nothing more.
 * Pure: no clock, no allocation (tests/test_pw_freeze_watch.c).
 */
#ifndef PW_FREEZE_WATCH_H
#define PW_FREEZE_WATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_FREEZE_SNAPSHOT_MS 20000u   /* no new frame this long, once the game has drawn */
#define PW_FREEZE_MAX_SNAPSHOTS 3u     /* a session's */

typedef struct {
    uint32_t fired;       /* a snapshot was taken for the stall going on now */
    uint32_t taken;       /* snapshots this session */
    uint64_t stalled_ms;  /* the present age at the last sample of that stall */
} PwFreezeWatch;

typedef enum { PW_FREEZE_QUIET, PW_FREEZE_SNAPSHOT, PW_FREEZE_RECOVERED } PwFreezeAction;

/* One second's sample. present_age_ms: since the last new frame; shown: the game has drawn since it started; away: the
 * app is away or just back (pw_suspend_hold); threshold_ms: 0 for PW_FREEZE_SNAPSHOT_MS. A stall going on while the
 * app goes away is dropped without a recovery line. QUIET for a NULL watch. */
PwFreezeAction pw_freeze_watch_step(PwFreezeWatch *watch, uint64_t present_age_ms, int shown, int away,
                                    uint32_t threshold_ms);

#ifdef __cplusplus
}
#endif

#endif /* PW_FREEZE_WATCH_H */
