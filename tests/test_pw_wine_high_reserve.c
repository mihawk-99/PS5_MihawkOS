/* SPDX-License-Identifier: GPL-3.0-or-later */
/* When a fixed reservation above the console's address space is placed elsewhere (wine/ps5/pw_wine_high_reserve.h,
 * Wine patch 2430). */
#include <assert.h>
#include <stdio.h>
#include "../wine/ps5/pw_wine_high_reserve.h"

int main(void)
{
    const unsigned reserve = 0x2000, commit = 0x1000;
    const unsigned long long limit = 0xffffff0000ull, spelunky = 0x7017f800000ull, near = limit + (1ull << 30);

    /* The setting's words: only "0" and "1" force; unset or anything else keeps the default. */
    assert(pw_wine_high_reserve_forced(NULL) == -1 && pw_wine_high_reserve_forced("") == -1);
    assert(pw_wine_high_reserve_forced("1") == 1 && pw_wine_high_reserve_forced("0") == 0);
    assert(pw_wine_high_reserve_forced("10") == -1 && pw_wine_high_reserve_forced("yes") == -1);

    /* Default: a committed request far above the limit is moved; a reserve-only one is not. */
    assert(pw_wine_high_reserve_elsewhere(-1, reserve | commit, spelunky, limit));
    assert(!pw_wine_high_reserve_elsewhere(-1, reserve, spelunky, limit));
    /* Within a jump's reach of code below the limit (a hook's trampoline search) it is refused as before. */
    assert(!pw_wine_high_reserve_elsewhere(-1, reserve | commit, near, limit));
    assert(!pw_wine_high_reserve_elsewhere(-1, reserve | commit, limit - 0x10000, limit));

    /* The setting forces both ways, whatever the request. */
    assert(pw_wine_high_reserve_elsewhere(1, reserve, spelunky, limit) && pw_wine_high_reserve_elsewhere(1, reserve | commit, near, limit));
    assert(!pw_wine_high_reserve_elsewhere(0, reserve | commit, spelunky, limit));
    puts("high reserve: PASS committed far above moved, reserve-only and near kept, the setting forces both ways");
    return 0;
}
