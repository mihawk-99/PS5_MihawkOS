/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_PRESENT_PACING_H
#define PW_PRESENT_PACING_H
#include <stdint.h>
/* A present loop's pacing against the display, for tests/vk-present-probe.c: whether a swapchain in FIFO holds the
 * presenting thread to the display's refresh and ends each present wait only once its frame is shown, as on a PC, and
 * whether MAILBOX and IMMEDIATE run free.
 *
 * Why: Fable through DXVK (a tester's run, published Alpha 7, 2026-11-01) logged "Detected frame rate (~113) higher than
 * selected refresh rate of ~60 Hz. Engaging frame rate limiter." DXVK times vkWaitForPresentKHR (dxvk_presenter.cpp) and
 * engages its own limiter when any 8 consecutive waits end faster than the refresh allows, buffering included
 * (util_fps_limiter.cpp, testRefreshHeuristic). Our broker WSI ended a present wait when the GPU finished the frame,
 * not when it was shown, so the first frames' waits ended in a burst and the limiter stayed on for the whole game:
 * frames of 33 ms and the camera's lag.
 *
 * Times are microseconds. presented[i]: when frame i's vkQueuePresentKHR returned; shown[i]: when its present wait
 * ended (0: not waited). fps over frames skip..n-1; max8_fps the fastest 8 consecutive present waits (7 intervals,
 * DXVK's window); shown_us the mean time from present to the end of its wait. All in tenths or whole microseconds, so
 * the probe needs no C runtime. Host test: tests/test_pw_present_pacing.c. */
typedef struct PwPresentPacing {
    unsigned fps_x10, max8_fps_x10, shown_us, waited;
} PwPresentPacing;

static inline PwPresentPacing pw_present_pacing(const uint64_t *presented, const uint64_t *shown, unsigned n, unsigned skip)
{
    PwPresentPacing p = {0, 0, 0, 0};
    if (n > skip + 1 && presented[n - 1] > presented[skip])
        p.fps_x10 = (unsigned)((uint64_t)(n - 1 - skip) * 10000000u / (presented[n - 1] - presented[skip]));
    uint64_t shown_sum = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!shown[i]) continue;
        ++p.waited;
        shown_sum += shown[i] > presented[i] ? shown[i] - presented[i] : 0;
        if (i + 7 < n && shown[i + 7] > shown[i]) {
            const unsigned fps = (unsigned)(70000000u / (shown[i + 7] - shown[i]));
            if (fps > p.max8_fps_x10) p.max8_fps_x10 = fps;
        }
    }
    if (p.waited) p.shown_us = (unsigned)(shown_sum / p.waited);
    return p;
}

/* holds (FIFO): the loop within 10% of the refresh and no 8 present waits faster than 10% over it. Otherwise (MAILBOX,
 * IMMEDIATE): the loop at least 1.5 times the refresh. 1 passes. */
static inline int pw_present_pacing_ok(const PwPresentPacing *p, int holds, unsigned refresh_hz)
{
    const unsigned refresh_x10 = refresh_hz * 10;
    if (holds)
        return p->waited && p->fps_x10 * 10 >= refresh_x10 * 9 && p->fps_x10 * 10 <= refresh_x10 * 11 &&
               p->max8_fps_x10 * 10 <= refresh_x10 * 11;
    return p->fps_x10 * 2 >= refresh_x10 * 3;
}
#endif
