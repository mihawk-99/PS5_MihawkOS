/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_CODE_BUDGET_H
#define PW_WINE_CODE_BUDGET_H
/* How much converted code a helper keeps (the code cache, pw_wine_dmem_ps5.c; docs/FLEXIBLE_MEMORY.md). The owner's rule
 * (2026-10-25): "64MiB always on for Steam and launchers", and a smarter default for everything else; changed on 2026-10-27
 * to fix Big Picture's stutter: "also let Steam and launchers be raised on real thrash", by the same rule as a game.
 *
 *   WINE_PS5_CODE_BUDGET_MIB set  that budget, fixed, for any program (0 is off)
 *   a listed image                64 MiB to start, raised as a game's is on real thrash: Steam and the common launchers
 *                                 and their browser helpers (pw_code_budget_listed), whose code is large and mostly cold
 *   any other program (games)     64 MiB to start (WINE_PS5_CODE_BUDGET_START_MIB for a test), raised PW_CODE_BUDGET_STEP_MIB at a time, up to
 *                                 PW_CODE_BUDGET_MAX_MIB, whenever it re-converts PW_CODE_BUDGET_THRASH_KIB of code
 *                                 it evicted, within PW_CODE_BUDGET_WINDOW_MS; never lowered
 *
 * The raise's threshold, measured on the base PS5 (2026-10-25):
 * - The 2D game with 16 MiB of its 23 MB of code evicted 1,571 clusters, ~2 MB per 10 s, and its worst p99 went from
 *   18 to 33 ms (fmem-isaac-b16-2026-10-25-01).
 * - At 64 MiB it evicted none and kept its frame times (fmem-isaac-b64-2026-10-25-01).
 * So 1 MiB re-converted in 10 s raises a game's budget before its frames pay for it.
 * Only clusters that come back a second time or more count: the Direct3D 12 game at a fixed 64 MiB re-converted 3.4 MiB
 * as its loading ended and 160 KiB in the 230 s of play after it, with the same frames as with no budget (116.6 against
 * 116.8 fps, p99 9.5 ms: fmem-re3-budget-64-2026-10-26-01), yet the first return of its loading code raised it to 96
 * MiB (fmem-re3-budget-default-2026-10-26-01). A cycle comes back again; a phase change does not. A thrashing game is
 * raised one cycle later than before (the 2D game at 16 MiB: an estimate of 35 s, its ~7 MiB over budget at ~2 MB per
 * 10 s).
 * Steam's web helper at 64 MiB evicted ~15 MB per 10 s with no visible cost to Steam's desktop window, which redraws at about
 * 2 frames a second. Big Picture draws at 120 Hz in that helper when it runs as one process (the Pro), and there the same
 * churn was stutter: 299 MiB came back in its first minute, 79 frames over 20 ms, against 35 with no budget (base PS5,
 * 2026-10-27, bp-a/bp-b). So a listed image is raised too, by the same rule (the owner's word of 2026-10-27: the 64 MiB
 * may be raised if it fixes the stutter); its cold churn comes back once and raises nothing.
 *
 * A re-conversion is a conversion of a cluster the cache gave back: PW_CODE_BUDGET_EVICTED addresses are remembered,
 * one slot per hash, so a collision only forgets (undercounts). No heap, no locks: the caller holds virtual_mutex. */
#include <stddef.h>
#include <stdint.h>

enum {
    PW_CODE_BUDGET_START_MIB = 64,
    PW_CODE_BUDGET_BROWSER_START_MIB = 96,   /* CEF/Chromium browser helpers always raise 64->96 MiB on boot */
    PW_CODE_BUDGET_STEP_MIB = 32,
    PW_CODE_BUDGET_MAX_MIB = 256,
    PW_CODE_BUDGET_WINDOW_MS = 10000,
    PW_CODE_BUDGET_THRASH_KIB = 1024,
    PW_CODE_BUDGET_EVICTED = 4096,
    PW_CODE_BUDGET_SAMPLES = 256,     /* 4 MiB of 16 KiB re-conversions: four times the threshold */
};

enum pw_code_budget_mode {
    PW_CODE_BUDGET_OFF,        /* WINE_PS5_CODE_BUDGET_MIB=0 */
    PW_CODE_BUDGET_SET,        /* a budget the user set */
    PW_CODE_BUDGET_LISTED,     /* Steam or a launcher */
    PW_CODE_BUDGET_ADAPTIVE,   /* every other program */
};

typedef struct PwCodeBudget {
    enum pw_code_budget_mode mode;
    uint64_t bytes;                                  /* the budget now; 0 when off */
    uintptr_t evicted[PW_CODE_BUDGET_EVICTED];       /* clusters given back, by hash of address; low bits: given back, returned */
    uint64_t sample_ms[PW_CODE_BUDGET_SAMPLES];      /* re-conversions in the window: when, and how many KiB */
    uint32_t sample_kib[PW_CODE_BUDGET_SAMPLES];
    unsigned sample_head, sample_count;
    uint64_t window_kib;                             /* their sum */
    uint64_t reconverted_kib, raises;                /* since the start, for the log */
} PwCodeBudget;

/* 1 when the image (a path or a bare name; '\\' and '/' both separate) is one of the listed clients, by name and
 * ignoring case. */
int pw_code_budget_listed(const char *image);
/* setting: WINE_PS5_CODE_BUDGET_MIB's value or NULL; start: WINE_PS5_CODE_BUDGET_START_MIB's, an adaptive program's first
 * budget (a test's: the forced case of docs/FLEXIBLE_MEMORY.md) or NULL; image: the process's executable or NULL when not
 * known yet. A setting that is not a number in 0..4096, or a start not in 1..PW_CODE_BUDGET_MAX_MIB, is ignored. */
void pw_code_budget_setup(PwCodeBudget *budget, const char *setting, const char *start, const char *image);
/* The cache gave the cluster at back to direct memory. */
void pw_code_budget_evicted(PwCodeBudget *budget, uintptr_t at);
/* A conversion of bytes at at, at now_ms; a re-conversion counts toward a raise from a cluster's second return. 1 when it
 * raised the budget (then *window_kib is what was re-converted in the
 * window that raised it), else 0. */
int pw_code_budget_converted(PwCodeBudget *budget, uintptr_t at, size_t bytes, uint64_t now_ms, uint64_t *window_kib);
#endif
