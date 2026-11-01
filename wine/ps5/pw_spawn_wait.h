/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SPAWN_WAIT_H
#define PW_SPAWN_WAIT_H
/* Whether helper starts still in flight keep the title's end waiting (pw_ps5_wait_children, pw_wine_process.c).
 *
 * A start that never finishes held the title for good: a store demo in the title failed to load a DLL 0.13 s after the
 * store client asked for its overlay, the overlay's start never got its bootstrap (the console's loader had wedged),
 * and the title's end waited for that start with no limit (2026-11-01, owner-re9-noffx). Helpers that run are closed
 * after 15 s and waited for until gone; a start in flight for PW_SPAWN_WAIT_MS without a pause in between is let go,
 * named in the log. Its helper, if one came up, ends by itself once its bootstrap is refused (wine-child: 20 s).
 * shortcut: "in flight since" is the time starts were last all done, not each start's own; a start that keeps a
 * second one company longer than this is let go with it, which only an endless stream of starts does. Host test:
 * tests/test_pw_spawn_wait.c. */
#include <stdint.h>

#define PW_SPAWN_WAIT_MS 30000u

/* pending: starts in flight; since_ms: when the count last went from 0 to 1 (0: none); now_ms: the same clock. */
static inline int pw_spawn_wait_holds(unsigned pending, uint64_t since_ms, uint64_t now_ms)
{
    return pending && (!since_ms || now_ms < since_ms || now_ms - since_ms < PW_SPAWN_WAIT_MS);
}
#endif
