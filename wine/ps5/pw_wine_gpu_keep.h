/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host memory a program gave the GPU: Direct3D 12's OpenExistingHeapFromAddress, Vulkan's VK_EXT_external_memory_host.
 *
 * The GPU client in the title's own process gives the program's pages GPU read and write with one sceKernelMprotect
 * (PS5_Vulkan's tooling/gpu-broker/client.c, radv_ps5_memory_grant_gpu). The kernel keeps a page's GPU bits and its CPU
 * bits in one protection, so every later change Wine makes there would take the GPU's access away while the GPU still
 * uses the memory: a program's VirtualProtect, a write watch, a guard page. On Windows the GPU's access does not follow
 * the CPU's. So win32u records each import here for as long as it lasts (Wine patch 2425), and the PS5 memory layer
 * (pw_wine_dmem_ps5.c) keeps GPU read and write on these pages in every protection change, and takes them away with a
 * protection change of its own before a mapping there is replaced or unmapped: the kernel never tears down pages the
 * GPU holds through Wine's engine (a teardown of wired GPU memory under a reservation hung the console, "unwire
 * timeout").
 *
 * Ranges are whole host pages, [start, end). One import may repeat another's range (the same memory opened twice):
 * each is a record of its own, and a page is kept while any record covers it. The table is small and fixed: a program
 * imports a few large ranges (a recompiled console game's whole memory), not many small ones; a full table refuses
 * the import. Callers hold Wine's virtual mutex. */
#ifndef PW_WINE_GPU_KEEP_H
#define PW_WINE_GPU_KEEP_H

#include <stdint.h>

#define PW_WINE_GPU_KEEP_MAX 64

typedef struct {
    uintptr_t start[PW_WINE_GPU_KEEP_MAX], end[PW_WINE_GPU_KEEP_MAX];
    unsigned count;
} PwWineGpuKeep;

/* 0, or -1 for an empty range or a full table. */
static inline int pw_wine_gpu_keep_add(PwWineGpuKeep *keep, uintptr_t start, uintptr_t end)
{
    if (start >= end || keep->count == PW_WINE_GPU_KEEP_MAX) return -1;
    keep->start[keep->count] = start;
    keep->end[keep->count++] = end;
    return 0;
}

/* One record of exactly this range: 0, or -1 when there is none. */
static inline int pw_wine_gpu_keep_remove(PwWineGpuKeep *keep, uintptr_t start, uintptr_t end)
{
    for (unsigned i = 0; i < keep->count; i++)
        if (keep->start[i] == start && keep->end[i] == end) {
            keep->count--;
            keep->start[i] = keep->start[keep->count];
            keep->end[i] = keep->end[keep->count];
            return 0;
        }
    return -1;
}

/* The first piece of [at, end) that is wholly kept or wholly not: 1 kept, 0 not, its end in *piece_end. */
static inline int pw_wine_gpu_keep_piece(const PwWineGpuKeep *keep, uintptr_t at, uintptr_t end, uintptr_t *piece_end)
{
    uintptr_t kept_to = 0, next = end;

    for (unsigned i = 0; i < keep->count; i++) {
        if (keep->start[i] <= at && at < keep->end[i] && keep->end[i] > kept_to) kept_to = keep->end[i];
        else if (keep->start[i] > at && keep->start[i] < next) next = keep->start[i];
    }
    if (kept_to) {
        /* Records that overlap this one carry the piece on past its end. */
        for (int grew = 1; grew;) {
            grew = 0;
            for (unsigned i = 0; i < keep->count; i++)
                if (keep->start[i] <= kept_to && keep->end[i] > kept_to) { kept_to = keep->end[i]; grew = 1; }
        }
        *piece_end = kept_to < end ? kept_to : end;
        return 1;
    }
    *piece_end = next;
    return 0;
}

/* Whether any page of [at, end) is kept. */
static inline int pw_wine_gpu_keep_touches(const PwWineGpuKeep *keep, uintptr_t at, uintptr_t end)
{
    for (unsigned i = 0; i < keep->count; i++)
        if (keep->start[i] < end && at < keep->end[i]) return 1;
    return 0;
}

#endif
