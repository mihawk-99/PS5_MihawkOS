/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_freeze_watch.h"

PwFreezeAction pw_freeze_watch_step(PwFreezeWatch *watch, uint64_t present_age_ms, int shown, int away,
                                    uint32_t threshold_ms)
{
    const uint64_t threshold = threshold_ms ? threshold_ms : PW_FREEZE_SNAPSHOT_MS;

    if (!watch) return PW_FREEZE_QUIET;
    if (watch->fired) {
        if (away) {   /* the ages restart at the return: a fall then is not a frame */
            watch->fired = 0;
            return PW_FREEZE_QUIET;
        }
        if (present_age_ms < watch->stalled_ms) {   /* a new frame reset the age */
            watch->fired = 0;
            return PW_FREEZE_RECOVERED;
        }
        watch->stalled_ms = present_age_ms;
        return PW_FREEZE_QUIET;
    }
    if (!shown || away || present_age_ms < threshold || watch->taken >= PW_FREEZE_MAX_SNAPSHOTS) return PW_FREEZE_QUIET;
    watch->fired = 1;
    watch->taken++;
    watch->stalled_ms = present_age_ms;
    return PW_FREEZE_SNAPSHOT;
}
