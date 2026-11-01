/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2416: the size of a large heap block's region, with the expression cut from the pin's heap.c (counts.inc:
 * the macros REGION_ALIGN, BLOCK_ALIGN and ROUND_SIZE as heap.c defines them, and the declaration that sets total_size).
 * Wine asserts sizeof(ARENA_LARGE) == 4 * BLOCK_ALIGN, which stands in for the structure here. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef size_t SIZE_T;
#include "counts.inc"

static SIZE_T arena_size(void) { return 4 * BLOCK_ALIGN; }

/* The region's size for `size` bytes of data, or 0 when heap_allocate_large refuses (its overflow test). */
static SIZE_T region_for(SIZE_T size)
{
    const SIZE_T sizeof_arena = arena_size();
#define sizeof(x) sizeof_arena
    SIZE_T total_size = TOTAL_SIZE_EXPR;
#undef sizeof
    if (total_size < size) return 0;
    return total_size;
}

int main(void)
{
    int bad = 0;
    const SIZE_T header = arena_size();
    /* Data sizes that end exactly on a 64 KiB boundary of the region, and ones around them. */
    for (SIZE_T blocks = 1; blocks <= 4096; blocks += (blocks < 64 ? 1 : 97)) {
        for (long delta = -2; delta <= 2; delta++) {
            SIZE_T size = blocks * 0x10000 - header + delta;
            if (size > (SIZE_T)1 << 40) continue;
            SIZE_T region = region_for(size);
            SIZE_T slack = region - header - size;
            if (!region || region % 0x10000 || slack < 0x1000 || slack >= 0x1000 + 0x10000) {
                printf("size %zu: region %zu, slack %zu\n", size, region, slack);
                bad = 1;
            }
        }
    }
    if (region_for((SIZE_T)-1) != 0 || region_for((SIZE_T)-1 - 0xffff) != 0 || region_for((SIZE_T)-1 - header) != 0) {
        puts("a size near the top of the address space is not refused");
        bad = 1;
    }
    if (!bad) puts("pc5 heap room passed");
    return bad;
}
