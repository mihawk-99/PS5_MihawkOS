/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SPEED_DEFAULTS_H
#define PW_SPEED_DEFAULTS_H
#include <stddef.h>
/* The speed switches the title turns on by default (native/proton_wine.cpp, after the game's and the run's own
 * environment, so either can still turn one off). A normal build runs a game at the display's best rate with no
 * settings; NAME=0 turns that one off. Each was measured on and off with a Direct3D 12 game at 119.88 Hz, the owner off
 * the pad, the same settings and stretch (docs/PLAN.md, 2026-10-17 and 2026-10-18; records removed from the tree, in git history).
 *
 *   WINE_PS5_NTSYNC=1             the session's one synchronization device for the title and every helper (Wine patch
 *                                 1180, src/pw_ntsync.h): 30 to a locked 60 fps at 60 Hz (2026-10-17).
 *                                 The session's alone: a game's game.json cannot set it.
 *   WINE_PS5_HELPER_CPUS_WIDEN=1  a game's helper on the title's CPUs, all sixteen, not five at the lowest priority
 *                                 (src/pw_cpu_placement.h): 1.70 to 4.47 CPU-s/s at 120 Hz (2026-10-18, two runs
 *                                 compared).
 *   WINE_PS5_ASYNC_PRESENT=1      the broker WSI presents from the swapchain's own thread (graphics patch 0024): 95.0 to
 *                                 118.6 fps (2026-10-17, two runs).
 *   WINE_PS5_PRESENT_LINK=1       a client's presents on a connection of their own (graphics patch 0026): the 1% low 88
 *                                 to 100 fps (2026-10-18, two runs).
 *   PROTON_120HZ=1                the display at 119.88 Hz where the TV takes it (graphics patch 0023). Wine offers
 *                                 120 Hz modes only when the broker's display kept it (pw_display_rate.h); on a 59.94 Hz
 *                                 fallback a program sees 60 Hz modes alone. A game that counts vblanks for its timing
 *                                 would run twice as fast: game.json "env": PROTON_120HZ=0 in the launcher route.
 *                                 The player's 120 Hz setting (src/pw_high_refresh.h) off sets it to 0 first.
 *   PROTON_GPU_FAST_COPY=1        the broker copies a 1920x1080 frame to the display 64 bytes at a time (graphics patch
 *                                 0022): 7.4 to 0.6 ms a present on the swapchain route, every launch from Files and the
 *                                 library (Witcher 3, w3-ab-b-pro-2026-11-01; the owner watched it on the TV). The gate's
 *                                 fast-copy-capture case checks the picture that was scanned out.
 *
 * Host test: tests/test_pw_speed_defaults.c. */
static const char *const pw_speed_defaults[] = {"WINE_PS5_NTSYNC", "WINE_PS5_HELPER_CPUS_WIDEN", "WINE_PS5_ASYNC_PRESENT",
                                                "WINE_PS5_PRESENT_LINK", "PROTON_120HZ", "PROTON_GPU_FAST_COPY"};
enum { PW_SPEED_DEFAULTS = sizeof(pw_speed_defaults) / sizeof(pw_speed_defaults[0]) };

/* Sets each default to "1" where the environment has no value yet (set: setenv with overwrite 0, or a test's own);
 * a value already there, "0" or anything else, stays. Returns how many it set. */
static inline unsigned pw_speed_defaults_apply(const char *(*get)(const char *name),
                                               int (*set)(const char *name, const char *value, int overwrite))
{
    unsigned set_count = 0;
    for (size_t i = 0; i < PW_SPEED_DEFAULTS; ++i)
        if (!get(pw_speed_defaults[i]) && !set(pw_speed_defaults[i], "1", 0)) ++set_count;
    return set_count;
}
#endif
