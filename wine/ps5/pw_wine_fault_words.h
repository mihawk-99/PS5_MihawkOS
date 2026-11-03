/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_FAULT_WORDS_H
#define PW_WINE_FAULT_WORDS_H
/* What a crash line says of an access violation or in-page error (Wine patches 2450 and 2451, copied into dlls/ntdll and
 * dlls/kernelbase by tools/build-wine-ps5.sh). A game in a PS5 title died with only "unhandled exception 0xc0000005 at
 * 000000014333AA32" (a tester's game crash report, 2026-11-01), and games with their own crash handler (a recompiled PC game,
 * NFS Heat) left no line at all: the touched address, the kind of access, the module and its offset, and
 * the thread are what a report needs. Pure and header-only: PE and host builds share it (tests/test_pw_wine_fault_words.c). */

/* ExceptionInformation[0] of an access violation or in-page error, in words. */
static inline const char *pw_wine_fault_access_word(unsigned long long access)
{
    return access == 0 ? "read" : access == 1 ? "write" : access == 8 ? "execute" : "unknown access";
}

/* MEMORY_BASIC_INFORMATION's State and Type, in words: what the touched page is to Wine (MEM_FREE: no view at all). */
static inline const char *pw_wine_fault_state_word(unsigned long state)
{
    return state == 0x1000 ? "MEM_COMMIT" : state == 0x2000 ? "MEM_RESERVE" : state == 0x10000 ? "MEM_FREE" : "state?";
}
static inline const char *pw_wine_fault_type_word(unsigned long type)
{
    return type == 0x20000 ? "MEM_PRIVATE" : type == 0x40000 ? "MEM_MAPPED" : type == 0x1000000 ? "MEM_IMAGE" : type ? "type?" : "";
}

/* 1-based slot when pc is new and a slot was free (the fault is logged), else 0: one line per faulting instruction, the
 * first `slots` of a session. Lock-free; a race only drops or doubles a line. Kept as pc + 1: a call through a null
 * pointer faults at pc 0, and 0 is an empty slot. */
static inline unsigned pw_wine_fault_first(unsigned long long *seen, unsigned slots, unsigned long long pc)
{
    ++pc;
    for (unsigned i = 0; i < slots; i++) {
        unsigned long long held = __atomic_load_n(&seen[i], __ATOMIC_RELAXED);
        if (held == pc) return 0;
        if (!held) {
            unsigned long long empty = 0;
            if (__atomic_compare_exchange_n(&seen[i], &empty, pc, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return i + 1;
            if (empty == pc) return 0;
        }
    }
    return 0;
}
#endif
