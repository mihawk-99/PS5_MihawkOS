/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The freeze snapshot's trigger (src/pw_freeze_watch.h), sampled once a second as the title's loop does: when it fires,
 * that it fires once per stall, the recovery line, nothing before a first frame or while away, and the session cap. */
#include "../src/pw_freeze_watch.h"
#include <assert.h>
#include <stdio.h>

/* The game's frames as ages: drawn every second up to `stop`, nothing from then until `resume` (0: never), sampled
 * at seconds 1..end. Returns the second of the first snapshot (0: none) and counts each action. */
static unsigned run(PwFreezeWatch *watch, unsigned stop, unsigned resume, unsigned end, int shown, unsigned *snapshots,
                    unsigned *recoveries)
{
    unsigned first = 0;
    *snapshots = *recoveries = 0;
    for (unsigned second = 1; second <= end; second++) {
        const int dark = second > stop && (!resume || second < resume);
        const uint64_t age = dark ? (uint64_t)(second - stop) * 1000 : 0;
        const PwFreezeAction action = pw_freeze_watch_step(watch, age, shown, 0, 0);
        if (action == PW_FREEZE_SNAPSHOT) { (*snapshots)++; if (!first) first = second; }
        if (action == PW_FREEZE_RECOVERED) (*recoveries)++;
    }
    return first;
}

int main(void)
{
    unsigned snapshots, recoveries;

    /* Timing: 19 s dark takes nothing; the 20th second takes it, once, however long the stall lasts. */
    PwFreezeWatch watch = {0};
    assert(run(&watch, 100, 120, 130, 1, &snapshots, &recoveries) == 0 && !snapshots);
    watch = (PwFreezeWatch){0};
    assert(run(&watch, 100, 0, 400, 1, &snapshots, &recoveries) == 120 && snapshots == 1 && !recoveries);

    /* Recovery: a frame after the snapshot says so once and re-arms; the next stall is taken again. */
    watch = (PwFreezeWatch){0};
    assert(run(&watch, 10, 60, 70, 1, &snapshots, &recoveries) == 30 && snapshots == 1 && recoveries == 1);
    assert(!watch.fired && watch.taken == 1);
    assert(pw_freeze_watch_step(&watch, 20000, 1, 0, 0) == PW_FREEZE_SNAPSHOT && watch.taken == 2);

    /* No first frame (a loading screen, or a program that never draws): nothing, however long. */
    watch = (PwFreezeWatch){0};
    assert(run(&watch, 0, 0, 600, 0, &snapshots, &recoveries) == 0 && !snapshots && !recoveries);

    /* Away or just back: nothing; a stall going on when the app goes away is dropped, with no recovery line. */
    watch = (PwFreezeWatch){0};
    assert(pw_freeze_watch_step(&watch, 50000, 1, 1, 0) == PW_FREEZE_QUIET && !watch.taken);
    assert(pw_freeze_watch_step(&watch, 25000, 1, 0, 0) == PW_FREEZE_SNAPSHOT);
    assert(pw_freeze_watch_step(&watch, 26000, 1, 1, 0) == PW_FREEZE_QUIET && !watch.fired);
    assert(pw_freeze_watch_step(&watch, 1000, 1, 0, 0) == PW_FREEZE_QUIET);

    /* The session's cap: three snapshots, then quiet. */
    watch = (PwFreezeWatch){0};
    for (unsigned i = 0; i < 5; i++) {
        pw_freeze_watch_step(&watch, 30000, 1, 0, 0);
        pw_freeze_watch_step(&watch, 0, 1, 0, 0);
    }
    assert(watch.taken == PW_FREEZE_MAX_SNAPSHOTS);

    /* A test's lower threshold, and a NULL watch. */
    watch = (PwFreezeWatch){0};
    assert(pw_freeze_watch_step(&watch, 5000, 1, 0, 5000) == PW_FREEZE_SNAPSHOT);
    assert(pw_freeze_watch_step(NULL, 99000, 1, 0, 0) == PW_FREEZE_QUIET);

    printf("freeze watch passed: fires at 20 s once per stall, reports a recovery and re-arms, never before a first "
           "frame or while away, at most %u a session\n", PW_FREEZE_MAX_SNAPSHOTS);
    return 0;
}
