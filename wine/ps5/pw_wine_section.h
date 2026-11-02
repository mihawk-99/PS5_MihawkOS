/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_SECTION_H
#define PW_WINE_SECTION_H
#include <stddef.h>
#include <stdint.h>
/* Demand-mapped shared file views. Caller owns the virtual reservation and
 * serializes every operation with Wine's virtual mutex, including faults.
 * One retained descriptor per view survives CloseHandle; aliases use the real
 * shared file pages, never private copies. Windows protection metadata stays
 * authoritative: the fault caller supplies the current effective protection.
 * Cache capacity bounds native file-mapping accounting, not Windows view size.
 * Eviction flushes writable pages and restores a no-overwrite virtual guard.
 * Fault handling allocates no memory. No operation may replace an unowned page.
 */
typedef struct PwWineSectionView
{
    uintptr_t base;
    size_t bytes;
    uint64_t offset;
    int fd, failed;
    uintptr_t lost_guard;
    struct PwWineSectionView *next;
    uint32_t head;    /* the first cached page of this view (page index + 1), 0 when none */
    size_t cached;    /* pages of this view in the cache */
    int direct;       /* the section's direct() answer for fd: 0 not asked yet, 1 direct memory, -1 a file */
    unsigned retries; /* transient failures in a row (PW_WINE_SECTION_RETRY) */
    uint64_t *chunks; /* direct views: one bit per PW_WINE_SECTION_CHUNK the view touches, set while mapped */
} PwWineSectionView;
typedef struct PwWineSectionPage
{
    uintptr_t address;
    PwWineSectionView *view;
    unsigned protection, needs_flush;
    uint64_t generation;
    uint32_t hnext;   /* next page of this address's hash bucket (index + 1), 0 at the end */
    uint32_t vnext, vprev; /* neighbours in the view's list of cached pages (index + 1) */
} PwWineSectionPage;
typedef struct PwWineSectionRetry
{
    uintptr_t address;
    uint64_t generation;
} PwWineSectionRetry;
typedef struct PwWineSectionOps
{
    void *context;
    size_t page;
    int (*retain)(void *, int);
    int (*close)(void *, int);
    int (*map)(void *, uintptr_t, size_t, unsigned, int, uint64_t);
    int (*protect)(void *, uintptr_t, size_t, unsigned);
    int (*flush)(void *, uintptr_t, size_t);
    int (*unmap)(void *, uintptr_t, size_t);
    int (*reserve)(void *, uintptr_t, size_t);
    /* Optional, NULL: unmap, then reserve. A guard laid over a mapped page in one call, so the page is never a hole
     * another thread's mapping without an address lands in (60,000 to 77,000 evictions in a Unity game run). */
    int (*lay)(void *, uintptr_t, size_t);
    /* Optional, NULL: no check. Before a fault maps into [address, +bytes), which holds only guards of the view's:
     * nonzero when a mapping that is not Wine's lies there, or the kernel was not asked. The kernel places a mapping
     * made without an address inside a reservation (the GPU client's, "unwire timeout at 0xc80000000", late-library
     * 2026-10-15), and a fixed mapping over wired memory hangs the console: the fault fails instead (stage 9), and the
     * view stays usable. One call per fault (a whole direct chunk, or the cache's run). */
    int (*foreign)(void *, uintptr_t, size_t);
} PwWineSectionOps;
typedef struct PwWineSection
{
    PwWineSectionOps ops;
    PwWineSectionView *views;
    PwWineSectionPage *pages;
    size_t capacity, hand, count, peak;
    uint64_t generation, faults, evictions, failures, clustered;
    uint64_t direct_maps, direct_bytes, retries;  /* direct chunks mapped, their bytes mapped now, answers retried */
    int (*direct)(void *, int);                    /* pw_wine_section_use_direct */
    unsigned (*protection)(void *, uintptr_t);
    unsigned failed_stage;
    uint32_t *buckets;  /* optional address index: bucket_mask + 1 chains of pages, see pw_wine_section_use_index */
    size_t bucket_mask;
} PwWineSection;
enum
{
    PW_WINE_SECTION_READ = 1,
    PW_WINE_SECTION_WRITE = 2,
    PW_WINE_SECTION_EXEC = 4
};
enum { PW_WINE_SECTION_CHUNK = 2u << 20, PW_WINE_SECTION_RETRY = 2, PW_WINE_SECTION_RETRIES = 3 };
int pw_wine_section_init(PwWineSection *, const PwWineSectionOps *, PwWineSectionPage *, size_t);
/* Find cached pages by address in constant time instead of scanning the whole cache (a fault's first question): `count`
 * buckets, a power of two, zeroed here; call right after init, before any fault. 0, or -1 for a bad count. */
int pw_wine_section_use_index(PwWineSection *, uint32_t *buckets, size_t count);
/* Map the views of direct-memory sections a chunk at a time instead of through the cache (see the fault contract):
 * direct(context, fd) is 1 when fd's section is in direct memory, 0 for a file, -1 with errno when that cannot be
 * learnt now; protection(context, address) is the protection Windows gives that page now, asked under the caller's
 * lock. Call right after init, before any view is added (add then makes each view's chunk bitmap and asks direct(),
 * which admits the process to a direct section while its handle is held). 0, or -1. */
int pw_wine_section_use_direct(PwWineSection *, int (*direct)(void *, int), unsigned (*protection)(void *, uintptr_t));
enum { PW_WINE_SECTION_MAX_RUN = 64 };
int pw_wine_section_add(PwWineSection *, PwWineSectionView *, uintptr_t, size_t, int, uint64_t);
PwWineSectionView *pw_wine_section_find(PwWineSection *, uintptr_t);
/* 1 resolved, 0 not this view/invalid access/repeated fault, -1 backing failure.
 * retry is per thread and prevents an unchanged mapping from hiding a real fault.
 * SIGBUS on an already mapped page must be reported to Wine, not retried.
 * PW_WINE_SECTION_RETRY: the title's answer for a direct chunk failed for now (EINTR, EAGAIN, ENOMEM, ETIMEDOUT):
 * nothing changed and the caller runs the access again; after PW_WINE_SECTION_RETRIES of them in a row on one view
 * the result is -1, and the view stays usable. A failed answer never sets view->failed, which means only that a
 * guard may be gone (a refused kernel mapping, ENXIO, or a failed reserve).
 * A direct view (pw_wine_section_use_direct) bypasses the cache: an allowed access maps the whole chunk around it,
 * clipped to the view, and each host page takes protection(); the chunk stays mapped until remove (no cache record,
 * no eviction, no flush). A run of host pages that is not read-write is mapped read-write and protected at once: between the two
 * another thread may write it without a fault, as on the cache path (one run, not the whole chunk). A fault in a
 * mapped chunk whose protection allows the access protects that host page again and returns 1; the same thread
 * faulting again at the same address with no success between gets 0. */
int pw_wine_section_fault(PwWineSection *, uintptr_t, unsigned protection, unsigned access, int bus,
                          PwWineSectionRetry *);
/* The same, mapping up to `run` pages from the faulting one in one native call when the caller knows they share the
 * protection (a Windows program's first touch of a large buffer faults every 16 KiB otherwise: a HashLink game in an emulator took 1,400
 * faults a second). The run is cut at the end of the view, at the first page already cached, and at a quarter of the cache.
 * Caller guarantees every page of the run has `protection`. */
/* What failed_stage says, for a log line a person reads (patch 2429). */
static inline const char *pw_wine_section_stage_word(unsigned stage)
{
    static const char *const words[] = {
        "none", "adding the view", "flushing an evicted page", "unmapping an evicted page",
        "restoring a guard (the view is refused from now on)", "the kernel refused the mapping", "changing protection",
        "closing the view", "the title's answer failed", "a mapping that is not Wine's lies in the range"};
    return stage < sizeof(words) / sizeof(words[0]) ? words[stage] : "unknown";
}
int pw_wine_section_fault_run(PwWineSection *, uintptr_t, unsigned protection, unsigned access, int bus,
                              PwWineSectionRetry *, size_t run);
/* For a second mapping of the memory under address (pw_wine_alias.h): a direct view whose chunk holding address is
 * mapped answers its retained descriptor, address's offset in the section and the chunk's extent in the view,
 * [*low, *high). 1, or 0: no view, a view of the cache (its pages are file memory, which has no alias), or a chunk not
 * mapped. Under the caller's lock; the answer holds until the view is removed. */
int pw_wine_section_backing(PwWineSection *, uintptr_t address, int *fd, uint64_t *offset, uintptr_t *low,
                            uintptr_t *high);
/* 1 owned and protected, 0 outside, -1 failure. Unmapped pages stay guards; mapped direct chunks are protected. */
int pw_wine_section_protect(PwWineSection *, uintptr_t, size_t, unsigned);
/* Flush and evict every cached page and unmap every mapped direct chunk, restoring its virtual guard, then close the
 * retained descriptor and detach. Caller releases the full virtual reservation.
 * Failure leaves the view attached; a lost guard must never be overwritten. */
int pw_wine_section_remove(PwWineSection *, PwWineSectionView *);
#endif
