/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The hang breaker (src/pw_hang_breaker.h): its decision table, a replay of every hang and near-hang on record, and the
 * SIGKILL ending against a fake kill(). */
#include "../src/pw_hang_breaker.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static PwHangVerdict decide(uint64_t present_ms, uint64_t census_ms, uint32_t known, uint32_t stall_ms)
{
    const PwHangSample sample = {present_ms, census_ms, known, stall_ms};
    PwHangVerdict verdict = PW_HANG_STALLED;
    assert(!pw_hang_breaker_decide(&sample, &verdict));
    return verdict;
}

/* A run as its title log shows it, in alive seconds (the title's 10 s lines): the spans in which the screen showed no
 * new frame, and those in which the memory helper's census was not taken. Both ends inclusive; a zero span ends a
 * list. Outside them frames come and the census is fresh. */
struct span { unsigned from, to; };
struct timeline {
    const char *run;
    struct span dark[6], frozen[2];
    unsigned end;
};

/* The first alive second at which the breaker fires, sampled once a second as the title's loop does; 0 for none. */
static unsigned replay(const struct timeline *run, uint32_t stall_ms)
{
    for (unsigned second = 1; second <= run->end; ++second) {
        PwHangSample sample = {0, 0, 1, stall_ms};
        for (const struct span *s = run->dark; s < run->dark + 6 && s->to; ++s)
            if (second >= s->from && second <= s->to) sample.present_age_ms = (uint64_t)(second - s->from) * 1000;
        for (const struct span *s = run->frozen; s < run->frozen + 2 && s->to; ++s)
            if (second >= s->from && second <= s->to) sample.census_age_ms = (uint64_t)(second - s->from) * 1000;
        PwHangVerdict verdict;
        assert(!pw_hang_breaker_decide(&sample, &verdict));
        if (verdict == PW_HANG_STALLED) return second;
    }
    return 0;
}

/* The fake kill(): which pids exist, how many polls each takes to go, and every call. */
static struct { int pid, polls_left, refuse_kill, refuse_probe; } world[8];
static unsigned world_count, calls;
static int fake_kill(int pid, int signal)
{
    ++calls;
    for (unsigned i = 0; i < world_count; ++i) {
        if (world[i].pid != pid) continue;
        if (signal == SIGKILL) return world[i].refuse_kill ? EPERM : 0;
        if (world[i].polls_left == 0) return ESRCH;
        if (world[i].polls_left > 0) --world[i].polls_left;
        return world[i].refuse_probe ? EPERM : 0;
    }
    return ESRCH;
}

int main(void)
{
    /* The decision table. */
    assert(decide(44999, 60000, 1, 0) == PW_HANG_FINE);
    assert(decide(45000, 45000, 1, 0) == PW_HANG_STALLED);
    assert(decide(3600000, 1000, 1, 0) == PW_HANG_FINE);     /* an idle desktop: no frame for an hour, census fresh */
    assert(decide(1000, 3600000, 1, 0) == PW_HANG_FINE);     /* a census stuck while frames come */
    assert(decide(3600000, 3600000, 0, 0) == PW_HANG_FINE);  /* no census yet: no verdict */
    assert(decide(5000, 5000, 1, 5000) == PW_HANG_STALLED);  /* a test's lower threshold */
    assert(decide(4999, 9000, 1, 5000) == PW_HANG_FINE);
    assert(decide(50000, 50000, 1, 90000) == PW_HANG_STALLED);   /* a test can never raise it */
    PwHangVerdict verdict;
    const PwHangSample sample = {0, 0, 1, 0};
    assert(pw_hang_breaker_decide(NULL, &verdict) == EINVAL && pw_hang_breaker_decide(&sample, NULL) == EINVAL);

    /* The title watchdog: frames alone, once the game in the title has shown one; the census plays no part. */
    const PwHangSample hung = {180000, 0, 0, 0}, early = {179999, 3600000, 1, 0}, tested = {5000, 0, 0, 5000};
    const PwHangSample loading = {60003, 0, 0, 0}, raised = {200000, 0, 0, 900000};   /* the demo's loading gap; a raise */
    assert(!pw_hang_breaker_title_decide(&loading, 1, &verdict) && verdict == PW_HANG_FINE);
    assert(!pw_hang_breaker_title_decide(&raised, 1, &verdict) && verdict == PW_HANG_STALLED);
    assert(!pw_hang_breaker_title_decide(&hung, 1, &verdict) && verdict == PW_HANG_STALLED);
    assert(!pw_hang_breaker_title_decide(&hung, 0, &verdict) && verdict == PW_HANG_FINE);   /* never drew: not ended */
    assert(!pw_hang_breaker_title_decide(&early, 1, &verdict) && verdict == PW_HANG_FINE);
    assert(!pw_hang_breaker_title_decide(&tested, 1, &verdict) && verdict == PW_HANG_STALLED);
    assert(pw_hang_breaker_title_decide(NULL, 1, &verdict) == EINVAL && pw_hang_breaker_title_decide(&hung, 1, NULL) == EINVAL);

    /* The hangs, each read both ways: the latest onset the log allows must be ended by the time given, the earliest
     * must not be ended before the hang began. */
    /* A 2026-10-14 freeze (record removed from the tree; its title log, not in the tree): 187 frames between alive 150 and
     * 160, longest gap 26 ms (:282), then none to 360 (:297-:588); the census 5,120 ms old between 150 and 160 (:273),
     * its figures unchanged from then on; the scripted stop at about 235 (:394). */
    const struct timeline dmem_late = {"2026-10-14 freeze latest", {{160, 360}}, {{155, 360}}, 360};
    const struct timeline dmem_early = {"2026-10-14 freeze earliest", {{153, 360}}, {{150, 360}}, 360};
    assert(replay(&dmem_late, 0) == 205);
    assert(replay(&dmem_early, 0) == 198);
    /* A 2026-10-14 log (not in the tree): an idle desktop with no frame from about 6 to 50 and a fresh census (:190, the
     * 43,914 ms gap); klog stops after alive 130 (:2418) with the console hung. Assumed from 136: no frame, no census. */
    const struct timeline manual_idle = {"2026-10-14 idle desktop continued", {{6, 50}, {136, 400}}, {{136, 400}}, 400};
    assert(replay(&manual_idle, 0) == 181);

    /* Runs that came back, at their widest reading: never ended. */
    /* A 2026-10-11 launcher run's log (not in the tree): 18.4 and 17.6 s without a frame (:1843, :2001); its -after log: 25.1 and 20.7 s
     * (:349, :1356), none from 140 to the end at 160 (:2182, :2476), and the census stale from at most 65 until 100 (:32,
     * its figures moving again at :348). */
    const struct timeline launcher = {"2026-10-11 launcher run",
        {{31, 50}, {50, 68}, {74, 100}, {109, 130}, {140, 160}}, {{65, 100}}, 160};
    assert(replay(&launcher, 0) == 0);
    /* A 2026-10-14 run (record removed from the tree; its title log :157, :171, :200, :219): two gaps, census fresh. */
    const struct timeline direct = {"2026-10-14 direct run", {{69, 90}, {99, 120}}, {{0, 0}}, 120};
    assert(replay(&direct, 0) == 0);
    /* dmem-gpu-dxvk64-2026-10-12-03 (-title.log:98-309): an idle probe, 161.7 s without a frame, census fresh. */
    const struct timeline idle = {"dmem-gpu-dxvk64-03", {{28, 190}}, {{0, 0}}, 190};
    assert(replay(&idle, 0) == 0);
    /* A 2026-10-14 game run's log (not in the tree): wedged from about 20 s on black frames, 1,200 of them every 10 s (:748-:1225),
     * census fresh: this rule does not see it (src/pw_hang_breaker.h). */
    const struct timeline black = {"2026-10-14 black frames", {{0, 0}}, {{0, 0}}, 432};
    assert(replay(&black, 0) == 0);
    /* The console proof's lower threshold (PROTON_HANG_STALL_MS) fires as many seconds after the onset. */
    assert(replay(&dmem_late, 5000) == 165);

    /* The broker wedge, in broker=busy samples one second apart: healthy (2), the device losses that cleared (15, 16)
     * and just under the line are left; 20 on, and a 2026-10-15 run's 239, end the game. */
    const unsigned left_alone[] = {0, 2, 15, 16, 19}, wedged[] = {20, 40, 239};
    for (unsigned i = 0; i < sizeof(left_alone) / sizeof(left_alone[0]); ++i)
        assert(!pw_hang_breaker_wedged(left_alone[i], left_alone[i] ? (left_alone[i] - 1) * 1000ull : 0));
    for (unsigned i = 0; i < sizeof(wedged) / sizeof(wedged[0]); ++i)
        assert(pw_hang_breaker_wedged(wedged[i], (wedged[i] - 1) * 1000ull));
    /* A recorder that fell behind and caught up (salvage-20261015-092043: 32 rows at t=281.5): a 16 s hold read as 21
     * samples, or 20 taken within half a second, is not a wedge; the samples' time decides as well as their count. */
    assert(!pw_hang_breaker_wedged(21, 16000) && !pw_hang_breaker_wedged(20, 500) && !pw_hang_breaker_wedged(19, 30000));

    /* The ending: three helpers that go 2, 4 and 6 polls after SIGKILL. */
    world_count = 3;
    world[0] = (typeof(world[0])){101, 2, 0, 0};
    world[1] = (typeof(world[0])){102, 4, 0, 0};
    world[2] = (typeof(world[0])){103, 6, 0, 0};
    PwHangEnd end;
    const int three[] = {101, 102, 103};
    calls = 0;
    assert(!pw_hang_breaker_end(three, 3, 50, fake_kill, 1000, &end));
    assert(calls == 3 && end.count == 3 && end.sent[0] == 0 && end.sent[2] == 0 && end.waiting == 7);
    unsigned left = 3, polls = 0;
    for (uint64_t now = 1100; left; now += 100, ++polls) left = pw_hang_breaker_gone(&end, fake_kill, now);
    assert(polls == 7 && end.gone_ms[0] == 300 && end.gone_ms[1] == 500 && end.gone_ms[2] == 700);
    /* Latched: once gone, a pid is never asked again. */
    calls = 0;
    assert(pw_hang_breaker_gone(&end, fake_kill, 9999) == 0 && calls == 0);

    /* SIGKILL refused (EPERM) and a pid that never goes: still waited for; EPERM from kill(pid, 0) is not gone. */
    world_count = 2;
    world[0] = (typeof(world[0])){201, -1, 1, 0};
    world[1] = (typeof(world[0])){202, -1, 0, 1};
    const int two[] = {201, 202};
    assert(!pw_hang_breaker_end(two, 2, 50, fake_kill, 0, &end));
    assert(end.sent[0] == EPERM && end.sent[1] == 0 && end.waiting == 3);
    for (unsigned i = 0; i < 100; ++i) assert(pw_hang_breaker_gone(&end, fake_kill, i * 100) == 2);
    assert(end.last[0] == 0 && end.last[1] == EPERM);

    /* A pid already gone at SIGKILL is gone at once. */
    world_count = 0;
    const int gone[] = {301};
    assert(!pw_hang_breaker_end(gone, 1, 50, fake_kill, 0, &end) && end.sent[0] == ESRCH && !end.waiting);

    /* Pids never signalled: 0 (the process group), -1 (every process), 1 (init) and the title's own; the rest are. */
    world_count = 1;
    world[0] = (typeof(world[0])){401, 0, 0, 0};
    const int bad[] = {0, -1, 1, 50, 401};
    calls = 0;
    assert(!pw_hang_breaker_end(bad, 5, 50, fake_kill, 0, &end));
    assert(calls == 1 && end.waiting == 1u << 4);
    for (unsigned i = 0; i < 4; ++i) assert(end.sent[i] == PW_HANG_SKIPPED);
    assert(pw_hang_breaker_gone(&end, fake_kill, 10) == 0 && calls == 2);

    /* Nothing to end, and refusals before any signal. */
    assert(!pw_hang_breaker_end(NULL, 0, 50, fake_kill, 0, &end) && end.count == 0 && !end.waiting);
    int many[PW_HANG_MAX_HELPERS + 1];
    for (unsigned i = 0; i <= PW_HANG_MAX_HELPERS; ++i) many[i] = 1000 + (int)i;
    calls = 0;
    assert(pw_hang_breaker_end(many, PW_HANG_MAX_HELPERS + 1, 50, fake_kill, 0, &end) == EINVAL && calls == 0);
    assert(pw_hang_breaker_end(NULL, 1, 50, fake_kill, 0, &end) == EINVAL);
    assert(pw_hang_breaker_end(many, 1, 50, NULL, 0, &end) == EINVAL);
    assert(pw_hang_breaker_end(many, 1, 50, fake_kill, 0, NULL) == EINVAL);
    assert(pw_hang_breaker_gone(NULL, fake_kill, 0) == 0 && pw_hang_breaker_gone(&end, NULL, 0) == 0);

    puts("pw_hang_breaker: ok");
    return 0;
}
