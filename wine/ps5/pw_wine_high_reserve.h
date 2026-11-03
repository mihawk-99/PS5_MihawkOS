/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_HIGH_RESERVE_H
#define PW_WINE_HIGH_RESERVE_H
/* Whether a fixed reservation refused only for lying above the console's address space is placed elsewhere (Wine patch
 * 2430, copied into dlls/ntdll/unix by tools/build-wine-ps5.sh). The console's app area ends near 1 TiB, a Windows
 * program's reaches 128 TiB: a game that asked only for 32 MiB committed at 7 TiB and on looped on the refusals and never
 * drew (Spelunky 2, 2026-11-01). Pure and header-only: Wine and the host tests share it (tests/test_pw_wine_high_reserve.c). */

/* WINE_PS5_HIGH_RESERVE_ELSEWHERE as a forced answer: 1 or 0, else -1 (unset or another value: the default below). */
static inline int pw_wine_high_reserve_forced(const char *value)
{
    return value && (value[0] == '0' || value[0] == '1') && !value[1] ? value[0] - '0' : -1;
}

/* Moved by default only when the request also commits (MEM_COMMIT, 0x1000: memory the program will use at once, which
 * can never be served up there) and starts more than 4 GiB above the limit, out of a rel32 jump's reach of any code below
 * it: a hook looking for room near its own code is refused as before and tries lower, and reserve-only requests (V8 and
 * other allocators that fall back by themselves) are left alone. */
static inline int pw_wine_high_reserve_elsewhere(int forced, unsigned type, unsigned long long base, unsigned long long limit)
{
    if (forced >= 0) return forced;
    return (type & 0x1000) && base >= limit && base - limit > (4ull << 30);
}

#endif
