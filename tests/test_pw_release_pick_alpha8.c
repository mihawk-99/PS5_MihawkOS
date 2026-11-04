/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What an Alpha 8 console (and a 7.1 one: the same src/pw_release.c since 7.0) is offered: its own pick, built from
 * 88f76993's src/pw_release.c by tools/host-tests.sh, over a listing as GitHub returns it. The 8.1 release's tag is one it
 * reads and takes as newer; "v8.1.0-alpha-8.1", the first name tried, it could not read, so no Alpha 8 console was offered
 * it (2026-11-04). */
#include "pw_release.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const PwReleaseEntry listing[] = {
        { "v8.1.0-alpha-8", 0, "https://example.invalid/v8.1.0-alpha-8/mihawkos-release.txt" },
        { "v8.0.0-alpha-8", 0, "https://example.invalid/v8.0.0-alpha-8/mihawkos-release.txt" },
        { "v7.1.0-alpha-7", 0, "https://example.invalid/v7.1.0-alpha-7/mihawkos-release.txt" },
    };
    assert(pw_release_pick(listing, 3, "8.0.0-alpha-8") == 0);   /* Alpha 8: 8.1 */
    assert(pw_release_pick(listing, 3, "7.1.0-alpha-7") == 0);   /* 7.1: 8.1, the newest */
    assert(pw_release_pick(listing, 3, "8.1.0-alpha-8") == -1);  /* 8.1 itself: nothing newer */
    const PwReleaseEntry unreadable[] = {
        { "v8.1.0-alpha-8.1", 0, "https://example.invalid/v8.1.0-alpha-8.1/mihawkos-release.txt" },
        { "v8.0.0-alpha-8", 0, "https://example.invalid/v8.0.0-alpha-8/mihawkos-release.txt" },
    };
    assert(pw_release_pick(unreadable, 2, "8.0.0-alpha-8") == -1);   /* the name that was never offered */
    puts("test_pw_release_pick_alpha8: ok");
    return 0;
}
