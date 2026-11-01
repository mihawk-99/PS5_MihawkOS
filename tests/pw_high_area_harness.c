/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2421: the high-area sizing functions, cut from the patch (high_area.inc: its added and context lines
 * from the PS5_HIGH_* definitions to ps5_reserve_high_area, which is the console call and is replaced here). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#include "high_area.inc"

static size_t kernel_limit;   /* the kernel grants a reservation of at most this many bytes */
static unsigned tries;
static int fake_reserve(size_t size, void *context)
{
    (void)context;
    ++tries;
    return size <= kernel_limit ? 0 : -1;
}

static int bad;
#define CHECK(cond, ...) do { if (!(cond)) { printf(__VA_ARGS__); printf("\n"); bad = 1; } } while (0)

int main(void)
{
    const size_t GiB = (size_t)1 << 30;
    /* The size granted is the largest of first, first/2, first/4 ... (floored at 16 GiB) the kernel allows. */
    struct { size_t first_gib, limit_gib, expect_gib; } cases[] = {
        {512, 1024, 512}, {512, 512, 512}, {512, 300, 256}, {512, 100, 64}, {512, 33, 32}, {512, 16, 16}, {512, 15, 0},
        {28, 100, 28}, {28, 20, 16}, {28, 15, 0}, {16, 16, 16}, {16, 17, 16}, {4, 100, 16}, {0, 100, 16}, {0, 10, 0},
        {300, 200, 150}, {296, 74, 74}, {300, 18, 16},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        kernel_limit = cases[i].limit_gib * GiB;
        tries = 0;
        size_t got = ps5_halving_reserve(cases[i].first_gib * GiB, fake_reserve, NULL);
        CHECK(got == cases[i].expect_gib * GiB, "first %zu limit %zu GiB: got %zu GiB, expected %zu", cases[i].first_gib,
              cases[i].limit_gib, got / GiB, cases[i].expect_gib);
        CHECK(tries <= 8, "first %zu limit %zu: %u tries", cases[i].first_gib, cases[i].limit_gib, tries);
    }
    /* Every answer is granted, and the size above it (when it was tried) is not. */
    for (size_t first = 0; first <= 512; first += 7)
        for (size_t limit = 0; limit <= 600; limit += 5) {
            kernel_limit = limit * GiB;
            size_t got = ps5_halving_reserve(first * GiB, fake_reserve, NULL);
            CHECK(!got || (got <= kernel_limit && got >= PS5_HIGH_RESERVE), "first %zu limit %zu: %zu", first, limit, got / GiB);
            CHECK(got || kernel_limit < PS5_HIGH_RESERVE, "first %zu limit %zu: refused although 16 GiB is granted", first, limit);
            CHECK(!got || got == (first * GiB < PS5_HIGH_RESERVE ? PS5_HIGH_RESERVE : first * GiB) || (got * 2 > kernel_limit),
                  "first %zu limit %zu: got %zu GiB but %zu GiB fits", first, limit, got / GiB, got * 2 / GiB);
        }
    /* The first size: the environment, or the maximum. */
    unsetenv("PW_PS5_HIGH_RESERVE_GB");
    CHECK(ps5_high_reserve_first() == PS5_HIGH_RESERVE_MAX, "default is not the maximum");
    struct { const char *value; size_t expect_gib; } env[] = {{"", 512}, {"0", 0}, {"28", 28}, {"512", 512}, {"513", 512},
                                                              {"banana", 512}, {"28x", 512}, {"-1", 512}};
    for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); i++) {
        setenv("PW_PS5_HIGH_RESERVE_GB", env[i].value, 1);
        CHECK(ps5_high_reserve_first() == env[i].expect_gib * GiB, "PW_PS5_HIGH_RESERVE_GB=%s: %zu GiB", env[i].value,
              ps5_high_reserve_first() / GiB);
    }
    if (!bad) puts("pw high area passed");
    return bad;
}
