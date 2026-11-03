/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A present loop's pacing verdict (src/pw_present_pacing.h), on the two shapes the console gives at 60 Hz: present waits
 * that end when the GPU finishes (the first frames in a burst, then throttled by the acquire, as Fable's DXVK saw: its
 * limiter engaged at ~113) and present waits that end at the vblank that shows the frame. */
#include "../src/pw_present_pacing.h"
#include <assert.h>
#include <stdio.h>

enum { N = 120, PERIOD = 16683 };

int main(void)
{
    uint64_t presented[N], shown[N];
    /* Shown at vblanks: frame i presented 4 ms into period i, its wait ending at the vblank after it. */
    for (unsigned i = 0; i < N; i++) presented[i] = 1000000 + (uint64_t)i * PERIOD + 4000, shown[i] = presented[i] + PERIOD - 4000;
    PwPresentPacing p = pw_present_pacing(presented, shown, N, 30);
    assert(p.fps_x10 >= 597 && p.fps_x10 <= 601 && p.max8_fps_x10 <= 601 && p.shown_us == PERIOD - 4000 && p.waited == N);
    assert(pw_present_pacing_ok(&p, 1, 60) && !pw_present_pacing_ok(&p, 0, 60));
    /* Waits ending at the GPU's finish: frames 0-4 in 1 ms steps (three images and two pending flips take them), then
     * one a vblank. The loop still averages 60, but 8 waits end inside 4 vblanks: the burst DXVK takes for a display
     * that does not hold FIFO. */
    for (unsigned i = 0; i < N; i++) {
        presented[i] = i < 5 ? 1000000 + (uint64_t)i * 1000 : 1004000 + (uint64_t)(i - 4) * PERIOD;
        shown[i] = presented[i] + 300;
    }
    p = pw_present_pacing(presented, shown, N, 30);
    assert(p.fps_x10 >= 597 && p.fps_x10 <= 601 && p.max8_fps_x10 > 1000 && p.shown_us == 300);
    assert(!pw_present_pacing_ok(&p, 1, 60));
    /* MAILBOX running free (1 ms a frame), and the same mode held to 60 as a FIFO chain would hold it. */
    for (unsigned i = 0; i < N; i++) presented[i] = (uint64_t)i * 1000, shown[i] = 0;
    p = pw_present_pacing(presented, shown, N, 30);
    assert(p.fps_x10 == 10000 && !p.waited && pw_present_pacing_ok(&p, 0, 60) && !pw_present_pacing_ok(&p, 1, 60));
    for (unsigned i = 0; i < N; i++) presented[i] = (uint64_t)i * PERIOD;
    p = pw_present_pacing(presented, shown, N, 30);
    assert(!pw_present_pacing_ok(&p, 0, 60));
    /* At 120 Hz (8,341 us) a FIFO loop holds at 120, not 60. */
    for (unsigned i = 0; i < N; i++) presented[i] = (uint64_t)i * 8341 + 2000, shown[i] = presented[i] + 6341;
    p = pw_present_pacing(presented, shown, N, 30);
    assert(pw_present_pacing_ok(&p, 1, 120) && !pw_present_pacing_ok(&p, 1, 60));
    /* Too few frames: no rate, no pass. */
    p = pw_present_pacing(presented, shown, 20, 30);
    assert(p.fps_x10 == 0 && !pw_present_pacing_ok(&p, 0, 60));
    puts("test_pw_present_pacing: ok");
    return 0;
}
