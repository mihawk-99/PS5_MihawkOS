/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_TITLE_CLAIM_H
#define PW_TITLE_CLAIM_H
/* Where a program's claim of the title goes (spawn_worker, pw_wine_process.c): the title's client took it, the title
 * refused it as a Steam play request (src/pw_title_play.h), a helper starts it, or it failed. The title's answer (its
 * result and errno) is read before the busy callback runs: that callback's own lookups set errno (a program with no
 * Steam app id left ENOENT, and the second program's start was refused instead of going to a helper:
 * a81final-base-2026-11-03-gate, launcher-render). Host test: tests/test_pw_title_claim.c. */
#include <errno.h>

enum PwTitleClaim { PW_TITLE_CLAIM_TITLE, PW_TITLE_CLAIM_REFUSED, PW_TITLE_CLAIM_HELPER, PW_TITLE_CLAIM_FAILED };

/* result, error: the title client's start and its errno, as it returned; busy (may be NULL): the title's say on a
 * refused claim, called only for EBUSY. */
static inline enum PwTitleClaim pw_title_claim(int result, int error, int (*busy)(const void *), const void *request)
{
    if (!result) return PW_TITLE_CLAIM_TITLE;
    if (error != EBUSY) return PW_TITLE_CLAIM_FAILED;
    return busy && busy(request) ? PW_TITLE_CLAIM_REFUSED : PW_TITLE_CLAIM_HELPER;
}
#endif
