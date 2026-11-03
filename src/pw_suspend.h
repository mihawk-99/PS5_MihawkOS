/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SUSPEND_H
#define PW_SUSPEND_H
#include <stdint.h>
/* The app's time away. When another app or a native game comes to the front, the system freezes the title and its
 * helpers, with whatever GPU work the broker had submitted still in flight, and CLOCK_MONOTONIC keeps running. On
 * resume every rule that measures time saw the whole suspend at once: the hang breaker (no new frame and no new census
 * for 45 s) ended the game's helpers at the first look after a suspend longer than that, as the GPU resumed their work
 * (the Pro's kernel panic on closing a native game, 2026-10-27: the title had been frozen behind it with one fence in
 * flight). So a tick records whether the system says the app is away (pw_suspend_tick's away), and a jump of the clock
 * between two ticks of PW_SUSPEND_GAP_MS or more is a suspend nobody announced. Rules then count only from the resume
 * (pw_suspend_since) and act on nothing while the app is away or in the PW_SUSPEND_GRACE_MS after (pw_suspend_hold).
 * An unannounced gap shorter than PW_SUSPEND_LONG_GAP_MS is the ticking thread's own stall, not a suspend: it moves
 * nothing, so a game that stalls the title for 3 s every 30 s is still ended by the 180 s title watchdog. Every
 * unannounced gap in klog/ up to 2026-11-02 was one (1.0 to 8.5 s, none with the flight recorder's thread frozen too):
 * the 3018 ms ones are the 30 s registry save waiting its 3000 ms on a wineserver that does not answer (a tester's Risk
 * of Rain 2 frozen at loading, rc4, 2026-11-01: ten in one session, and the watchdog never fired).
 * Pure: the caller ticks it from one thread; others read its fields with relaxed loads, a stale value only delaying. */
enum { PW_SUSPEND_GAP_MS = 1000, PW_SUSPEND_LONG_GAP_MS = 10000, PW_SUSPEND_GRACE_MS = 10000 };
enum { PW_SUSPEND_NONE, PW_SUSPEND_LEFT, PW_SUSPEND_BACK, PW_SUSPEND_GAP, PW_SUSPEND_STALL };
typedef struct PwSuspendClock {
    uint64_t last_ms;       /* the previous tick, 0 before the first */
    uint64_t resumed_ms;    /* the last return (announced, or found by a gap), 0 never */
    uint64_t left_ms;       /* when the app was last announced away */
    uint64_t away_total_ms; /* every absence so far, announced or not */
    uint32_t away;          /* 1 while the system says the app is not in the foreground */
    uint32_t suspends, unannounced;
    uint32_t stalls;        /* short unannounced gaps: the ticking thread was held, the app was not frozen */
} PwSuspendClock;
/* One tick at now_ms. away: 1 when the system says the app is not in the foreground, 0 when it is, -1 unknown (the
 * status could not be read: the last known state stands). A gap is reported even while away is announced, and ends it:
 * a tick that ran after one is a tick of an app the system let run again. PW_SUSPEND_LEFT once on going away,
 * PW_SUSPEND_BACK on an announced return, PW_SUSPEND_GAP for a suspend's gap, PW_SUSPEND_STALL for a short unannounced
 * one (counted in stalls; a status read on the same tick still says LEFT or BACK), else PW_SUSPEND_NONE. */
static inline int pw_suspend_tick(PwSuspendClock *c, uint64_t now_ms, int away)
{
    const uint64_t last = c->last_ms;
    c->last_ms = now_ms;
    const uint64_t gap = last && now_ms > last ? now_ms - last : 0;
    const int stall = gap >= PW_SUSPEND_GAP_MS && !c->away && gap < PW_SUSPEND_LONG_GAP_MS;
    if (stall) ++c->stalls;
    else if (gap >= PW_SUSPEND_GAP_MS) {
        c->away_total_ms += gap;
        c->resumed_ms = now_ms;
        ++c->suspends;
        if (!c->away) ++c->unannounced;
        c->away = away > 0;   /* the status read after the gap says where the app is now */
        return PW_SUSPEND_GAP;
    }
    if (away > 0 && !c->away) { c->away = 1; c->left_ms = now_ms; return PW_SUSPEND_LEFT; }
    if (away == 0 && c->away) {
        c->away = 0;
        c->resumed_ms = now_ms;
        if (now_ms > c->left_ms) c->away_total_ms += now_ms - c->left_ms;
        ++c->suspends;
        return PW_SUSPEND_BACK;
    }
    return stall ? PW_SUSPEND_STALL : PW_SUSPEND_NONE;
}
/* The start a rule measuring since since_ms may count from: never before the last return. */
static inline uint64_t pw_suspend_since(const PwSuspendClock *c, uint64_t since_ms)
{
    return c->resumed_ms > since_ms ? c->resumed_ms : since_ms;
}
/* 1 while a rule that ends or frees something must wait: the app is away, or came back less than PW_SUSPEND_GRACE_MS
 * ago (the broker, the census and the helpers need that long to show they run again). */
static inline int pw_suspend_hold(const PwSuspendClock *c, uint64_t now_ms)
{
    return c->away || (c->resumed_ms && now_ms < c->resumed_ms + PW_SUSPEND_GRACE_MS);
}
/* What the title tells its helpers, in each helper's input page at PW_SUSPEND_PAGE_OFFSET (the page is the helper's,
 * src/pw_shared_page.h): away as above, and a beat the title bumps every tick, so a helper that reads the same beat
 * for PW_SUSPEND_GAP_MS knows the title is frozen even when nobody announced it. */
typedef struct PwSuspendPage { uint32_t away, beat; } PwSuspendPage;
enum { PW_SUSPEND_PAGE_OFFSET = 16384 - 64 };
/* A helper's reading of the title's word: 1 when the title says away, or its beat has not moved for PW_SUSPEND_GAP_MS
 * (a frozen title says nothing); 0 otherwise, also before the title's first beat. *beat_seen and *beat_at are the
 * reader's, zero to start. */
static inline int pw_suspend_page_away(const PwSuspendPage *word, uint32_t *beat_seen, uint64_t *beat_at, uint64_t now_ms)
{
    const uint32_t beat = __atomic_load_n(&word->beat, __ATOMIC_ACQUIRE);
    if (beat != *beat_seen) { *beat_seen = beat; *beat_at = now_ms; }
    return __atomic_load_n(&word->away, __ATOMIC_RELAXED) || (*beat_seen && now_ms - *beat_at >= PW_SUSPEND_GAP_MS);
}
#endif
