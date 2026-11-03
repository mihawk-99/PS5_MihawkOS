/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The words and the first-faults table of a crash line (wine/ps5/pw_wine_fault_words.h, Wine patches 2450 and 2451). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../wine/ps5/pw_wine_fault_words.h"

int main(void)
{
    assert(!strcmp(pw_wine_fault_access_word(0), "read"));
    assert(!strcmp(pw_wine_fault_access_word(1), "write"));
    assert(!strcmp(pw_wine_fault_access_word(8), "execute"));
    assert(!strcmp(pw_wine_fault_access_word(2), "unknown access"));
    assert(!strcmp(pw_wine_fault_state_word(0x1000), "MEM_COMMIT") && !strcmp(pw_wine_fault_state_word(0x2000), "MEM_RESERVE"));
    assert(!strcmp(pw_wine_fault_state_word(0x10000), "MEM_FREE") && !strcmp(pw_wine_fault_state_word(7), "state?"));
    assert(!strcmp(pw_wine_fault_type_word(0x20000), "MEM_PRIVATE") && !strcmp(pw_wine_fault_type_word(0x40000), "MEM_MAPPED"));
    assert(!strcmp(pw_wine_fault_type_word(0x1000000), "MEM_IMAGE") && !strcmp(pw_wine_fault_type_word(0), ""));
    unsigned long long seen[3] = {0};
    /* A new instruction takes a slot, the same one again none (a Mono null check repeating), and a full table none. */
    assert(pw_wine_fault_first(seen, 3, 0x14333aa32ull) == 1);
    assert(pw_wine_fault_first(seen, 3, 0x14333aa32ull) == 0);
    assert(pw_wine_fault_first(seen, 3, 0x1805548f0ull) == 2);
    assert(pw_wine_fault_first(seen, 3, 0xdffff70b96ull) == 3);
    assert(pw_wine_fault_first(seen, 3, 0x140001000ull) == 0);
    assert(pw_wine_fault_first(seen, 3, 0x1805548f0ull) == 0);
    /* A jump to null faults at pc 0: logged once like any other. */
    unsigned long long null_seen[2] = {0};
    assert(pw_wine_fault_first(null_seen, 2, 0) == 1 && pw_wine_fault_first(null_seen, 2, 0) == 0);
    puts("fault words: PASS access, state and type words, one line per instruction, bounded");
    return 0;
}
