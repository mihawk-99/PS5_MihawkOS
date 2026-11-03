/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_suspend.h"
#include "../src/pw_hang_breaker.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    PwSuspendClock c = {0};
    /* Ticks every 33 ms, in the foreground: nothing held. */
    for (uint64_t t = 1000; t <= 2000; t += 33) assert(pw_suspend_tick(&c, t, 0) == PW_SUSPEND_NONE);
    assert(!pw_suspend_hold(&c, 2000) && pw_suspend_since(&c, 500) == 500);
    /* Announced: the overlay comes up, then the app is frozen 5 minutes, then the status says foreground. */
    assert(pw_suspend_tick(&c, 2033, 1) == PW_SUSPEND_LEFT && c.away && pw_suspend_hold(&c, 2033));
    assert(pw_suspend_tick(&c, 2066, 1) == PW_SUSPEND_NONE);
    assert(pw_suspend_tick(&c, 302066, 0) == PW_SUSPEND_GAP && !c.away && c.resumed_ms == 302066);
    assert(c.suspends == 1 && c.unannounced == 0 && c.away_total_ms == 300000);
    /* The hang breaker's last frame was before the suspend: it counts only from the return, and waits the grace. */
    assert(pw_suspend_since(&c, 2000) == 302066 && pw_suspend_hold(&c, 302066 + PW_SUSPEND_GRACE_MS - 1));
    assert(!pw_suspend_hold(&c, 302066 + PW_SUSPEND_GRACE_MS));
    /* Unannounced: the clock jumps between two foreground ticks. */
    assert(pw_suspend_tick(&c, 400000, 0) == PW_SUSPEND_GAP && c.unannounced == 1 && c.suspends == 2);
    assert(pw_suspend_tick(&c, 400033, -1) == PW_SUSPEND_NONE && !c.away);
    /* Announced both ways without a gap (the overlay over a running app): away, then back. */
    assert(pw_suspend_tick(&c, 400066, 1) == PW_SUSPEND_LEFT);
    for (uint64_t t = 400099; t < 403000; t += 33) assert(pw_suspend_tick(&c, t, -1) == PW_SUSPEND_NONE && c.away);
    assert(pw_suspend_tick(&c, 403000, 0) == PW_SUSPEND_BACK && c.resumed_ms == 403000 && c.suspends == 3);
    /* A tick just under the gap is not one; the clock never going back counts nothing. */
    assert(pw_suspend_tick(&c, 403999, 0) == PW_SUSPEND_NONE && pw_suspend_tick(&c, 403999, 0) == PW_SUSPEND_NONE);
    /* A helper's reading: nothing before the first beat, away when said, and when the beat stops a second. */
    PwSuspendPage word = {0, 0};
    uint32_t seen = 0;
    uint64_t at = 0;
    assert(!pw_suspend_page_away(&word, &seen, &at, 5000) && !pw_suspend_page_away(&word, &seen, &at, 9000));
    word.beat = 1;
    assert(!pw_suspend_page_away(&word, &seen, &at, 9100) && !pw_suspend_page_away(&word, &seen, &at, 9100 + PW_SUSPEND_GAP_MS - 1));
    assert(pw_suspend_page_away(&word, &seen, &at, 9100 + PW_SUSPEND_GAP_MS));
    word.beat = 2;
    assert(!pw_suspend_page_away(&word, &seen, &at, 12000));
    word.away = 1;
    assert(pw_suspend_page_away(&word, &seen, &at, 12001));
    /* A short unannounced gap is the loop's own stall: counted, and nothing restarts or is held. */
    PwSuspendClock s = {0};
    assert(pw_suspend_tick(&s, 1000, 0) == PW_SUSPEND_NONE);
    assert(pw_suspend_tick(&s, 4018, 0) == PW_SUSPEND_STALL && s.stalls == 1 && !s.resumed_ms && !s.suspends);
    assert(!pw_suspend_hold(&s, 4018) && pw_suspend_since(&s, 1000) == 1000);
    assert(pw_suspend_tick(&s, 4018 + PW_SUSPEND_LONG_GAP_MS - 1, 0) == PW_SUSPEND_STALL && s.stalls == 2);
    /* The status read on a stall's tick still counts; a long unannounced gap is a suspend; announced, any gap is. */
    assert(pw_suspend_tick(&s, 20000, 1) == PW_SUSPEND_LEFT && s.stalls == 3 && s.away);
    PwSuspendClock l = {0};
    assert(pw_suspend_tick(&l, 1000, 0) == PW_SUSPEND_NONE && pw_suspend_tick(&l, 3000, 1) == PW_SUSPEND_LEFT && l.stalls == 1);
    assert(pw_suspend_tick(&l, 5000, -1) == PW_SUSPEND_GAP && l.unannounced == 0 && l.resumed_ms == 5000);
    assert(pw_suspend_tick(&l, 5000 + PW_SUSPEND_LONG_GAP_MS, 0) == PW_SUSPEND_GAP && l.unannounced == 1);
    /* The tester's Risk of Rain 2 (rc4, 2026-11-01): the title loop ticks every 8 ms and stalls 3018 ms every 30 s; its
     * last frame was at 10 s. Ages count as the loop counts them (native/proton_wine.cpp): the watchdog fires at 190 s,
     * 180 s after that frame. Before, each stall restarted the age and it never passed 30 s. */
    PwSuspendClock r = {0};
    uint64_t fired = 0, next_stall = 30000;
    for (uint64_t t = 1000; t < 400000 && !fired; t += 8) {
        if (t >= next_stall) { t += 3018; next_stall += 30000; }
        assert(pw_suspend_tick(&r, t, -1) != PW_SUSPEND_GAP);
        PwHangSample sample = {0};
        sample.present_age_ms = t - pw_suspend_since(&r, 10000 < t ? 10000 : t);
        PwHangVerdict verdict = PW_HANG_FINE;
        assert(!pw_hang_breaker_title_decide(&sample, 1, &verdict));
        if (verdict == PW_HANG_STALLED) fired = t;
    }
    assert(fired >= 10000 + PW_HANG_TITLE_STALL_MS && fired < 10000 + PW_HANG_TITLE_STALL_MS + 3100 && r.stalls == 6);
    _Static_assert(PW_SUSPEND_PAGE_OFFSET + sizeof(PwSuspendPage) <= 16384, "the title's word fits the input page");
    puts("suspend clock: PASS announced, unannounced, overlay, grace, rebase, a helper's reading of the title, short stalls, "
         "a title stalled 3 s every 30 s is still ended by the watchdog");
}
