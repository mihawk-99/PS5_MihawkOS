/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_SECTION_PS5_H
#define PW_WINE_SECTION_PS5_H
#include "pw_wine_section.h"
/* The smallest section whose view the pager takes: the direct-memory threshold (wine/ps5/pw_wine_dsection.h) in a helper
 * and in the program in the title, which asks the title's own table for the chunks (pw_ps5_dsection_page), so both map
 * the same memory; 256 MiB in a title with no program of its own (fmem-intitle-p3-2026-10-26-01: before the title asked
 * its own table, a 14 MiB view there faulted c0000017). */
uint64_t __wine_ps5_section_min_bytes(void);
/* Called under Wine's virtual mutex. Only payload clients reserve the internal
 * arena; it stays outside Wine's generic free reservations so Windows scanners
 * see the occupied native range. Existing Windows views remain authoritative.
 * Large shared sections use real file pages on faults, capped at 64 MiB mapped
 * per client unless WINE_PS5_SECTION_PAGES says otherwise (64 to 65,536 pages of 16 KiB). This bounds Sony's virtual mapping accounting, not physical RAM.
 * Fixed views retain their requested address; this is not relocation policy. */
/* host_protection: the protection Windows gives the host page at an address now (Wine's get_host_page_unix_prot,
 * patch 1125), which a direct-memory section's chunk takes when it is mapped whole; NULL keeps such sections on the
 * cache. In a payload with an arena, a mapping the kernel places over it is counted (__wine_ps5_section_stats). */
void __wine_ps5_section_init(uintptr_t host_limit, unsigned (*host_protection)(uintptr_t));
void *__wine_ps5_section_address(size_t bytes, uintptr_t low, uintptr_t high, size_t align_mask);
int __wine_ps5_section_arena_contains(uintptr_t base, size_t bytes);
int __wine_ps5_section_add(PwWineSectionView *, uintptr_t, size_t, int, uint64_t);
int __wine_ps5_section_remove(PwWineSectionView *);
int __wine_ps5_section_protect(uintptr_t, size_t, unsigned);
/* `run`: how many host pages from the faulting one, at most, share its protection (at least 1); the cache maps up to
 * min(run, __wine_ps5_section_cluster()) of them in one call. */
int __wine_ps5_section_fault(uintptr_t, unsigned, unsigned, PwWineSectionRetry *, unsigned run);
/* Pages mapped per fault at most: WINE_PS5_SECTION_CLUSTER, 1 to 64 (1 maps a page at a time). */
unsigned __wine_ps5_section_cluster(void);
unsigned __wine_ps5_section_stats(uint64_t *, unsigned);
/* The stage of the cache's last failure (pw_wine_section_stage_word), for the fault that raised it (patch 2429). */
unsigned __wine_ps5_section_failed_stage(void);
/* The direct memory of a direct section's mapped chunk under address, and the range of the view it backs contiguously,
 * for a second mapping (pw_wine_alias.h): 1, or 0. */
int __wine_ps5_section_backing(uintptr_t address, int64_t *dmem, uintptr_t *low, uintptr_t *high);
#endif
