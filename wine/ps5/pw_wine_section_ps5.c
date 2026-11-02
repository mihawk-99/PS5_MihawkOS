/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_section_ps5.h"
#include "pw_wine_fdv.h"
#include "pw_wine_blocks.h"
#include "pw_wine_dmap.h"
#include "pw_wine_perf.h"
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

extern int sceKernelReserveVirtualRange(void **, size_t, int, size_t);
extern int __wine_ps5_payload_memory_ready(void);
extern uint64_t pw_wine_dsection_min_bytes(void);
extern int __wine_ps5_dmem_file_region(void *, size_t);
extern int __wine_ps5_dmem_file_released(void *, size_t);
extern int __wine_ps5_mprotect(void *, size_t, int);
extern int __wine_ps5_foreign_in(void *, size_t);
extern int __wine_ps5_lay(void *, size_t);
extern int sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
/* wine/ps5/pw_wine_process.c, linked beside this in ntdll: the title's answer for a page of a direct-memory section. */
extern int pw_ps5_dsection_page(uint64_t dev, uint64_t ino, uint64_t offset, int64_t *dmem, uint64_t *chunk_offset,
                                uint64_t *chunk_bytes);
enum { MAX_PAGES = 65536, INDEX_BUCKETS = 131072, DEFAULT_PAGES = 4096 };
static PwWineSectionPage pages[MAX_PAGES];
static uint32_t buckets[INDEX_BUCKETS];
static unsigned cluster = 1;
enum { FLUSH_SYNC, FLUSH_ASYNC, FLUSH_NONE };
static unsigned flush_mode = FLUSH_SYNC;
static PwWineSection section;
static uintptr_t arena_base;
static size_t arena_bytes;
static unsigned (*protection_of)(uintptr_t);
static int assert_probe;
/* Mappings the kernel placed (made without MAP_FIXED) over the arena, which holds the Windows views of large
 * sections: a Windows program reads such a mapping as its own memory (Wine's host-protection leaves in a HashLink game's
 * pool, 2026-10-13; record removed from the tree; in git history). Counted from inside mmap, so lock-free: the arena is fixed once reserved. */
static uint64_t misplaced, misplaced_address, misplaced_bytes;
extern void (*pw_wine_mmap_placed)(void *, size_t);
static void placed(void *address, size_t bytes)
{
    const uintptr_t at = (uintptr_t)address;
    if (!bytes || at >= arena_base + arena_bytes || at + bytes <= arena_base) return;
    __atomic_store_n(&misplaced_address, (uint64_t)at, __ATOMIC_RELAXED);
    __atomic_store_n(&misplaced_bytes, (uint64_t)bytes, __ATOMIC_RELAXED);
    __atomic_add_fetch(&misplaced, 1, __ATOMIC_RELAXED);
}

/* What the cache costs, for the helper's census: every native call it makes counted and timed (callers hold Wine's virtual
 * mutex, so plain counters are enough). A HashLink game in an emulator faulted 1,400 pages a second through a 64 MiB window. */
enum { STAT_VIEWS, STAT_FAULT_CALLS, STAT_FAULT_NS, STAT_MAP_CALLS, STAT_MAP_NS, STAT_FLUSH_CALLS, STAT_FLUSH_NS,
       STAT_UNMAP_CALLS, STAT_UNMAP_NS, STAT_RESERVE_CALLS, STAT_RESERVE_NS, STAT_COUNT };
static uint64_t stats[STAT_COUNT];
static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void counted(int calls, int ns, uint64_t begin)
{
    stats[calls]++;
    stats[ns] += now_ns() - begin;
}

static int retain(void *context, int fd)
{
    uint32_t pin = 0;
    (void)context;
    /* Faults must never reopen a parked descriptor or lose a backing lease. */
    if (pw_wine_fdv && pw_wine_fdv->enter(fd, &pin))
        return -1;
    if (pin)
        pw_wine_fdv->keep(pin);
    int copy = dup(fd);
    if (pin)
        pw_wine_fdv->leave(pin);
    return copy;
}
static int closed(void *c, int fd)
{
    (void)c;
    return close(fd);
}
static int mapped(void *c, uintptr_t at, size_t n, unsigned prot, int fd, uint64_t off)
{
    (void)c;
    const uint64_t begin = now_ns();
    /* A large anonymous section in direct memory (wine/ps5/pw_wine_dsection.h): its RAM, the same in every alias. */
    const int direct = pw_wine_dmap_direct(fd);
    if (direct > 0) {
        const int status = pw_wine_dmap_map(fd, at, n, off);
        counted(STAT_MAP_CALLS, STAT_MAP_NS, begin);
        if (status) return -1;
        return (prot & 7) != (PROT_READ | PROT_WRITE) ? __wine_ps5_mprotect((void *)at, n, (int)prot) : 0;
    }
    if (direct < 0) return -1;
    /* Real blocks first: on /data a mapped write into a hole is not seen by the file's other mappings (pw_wine_blocks.h). */
    (void)pw_wine_blocks_ensure(fd, off, n);
    void *p = mmap((void *)at, n, (int)prot & ~PROT_EXEC, MAP_SHARED | MAP_FIXED, fd, (off_t)off);
    counted(STAT_MAP_CALLS, STAT_MAP_NS, begin);
    if (p != (void *)at)
        return -1;
    return prot & PROT_EXEC ? __wine_ps5_mprotect(p, n, (int)prot) : 0;
}
static int block_identity(void *c, int fd, uint64_t *id)
{
    struct stat st;
    (void)c;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) return -1;
    *id = ((uint64_t)st.st_dev << 40) ^ (uint64_t)st.st_ino;
    return 0;
}
static ssize_t block_read(void *c, int fd, void *b, size_t n, uint64_t off) { (void)c; return pread(fd, b, n, (off_t)off); }
static ssize_t block_write(void *c, int fd, const void *b, size_t n, uint64_t off)
{
    (void)c;
    return pwrite(fd, b, n, (off_t)off);
}
static int dmap_identity(void *c, int fd, uint64_t *dev, uint64_t *ino)
{
    struct stat st;
    (void)c;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) return -1;
    *dev = (uint64_t)st.st_dev;
    *ino = (uint64_t)st.st_ino;
    return 0;
}
static int dmap_page(void *c, uint64_t dev, uint64_t ino, uint64_t offset, int64_t *dmem, uint64_t *chunk_offset,
                     uint64_t *chunk_bytes)
{
    (void)c;
    return pw_ps5_dsection_page(dev, ino, offset, dmem, chunk_offset, chunk_bytes);
}
static int dmap_map(void *c, uintptr_t address, size_t bytes, int64_t dmem)
{
    const char *was = pw_wine_perf_vlock_doing("pager-map");   /* under the virtual mutex (pw_wine_perf.h) */
    void *at = (void *)address;
    (void)c;
    /* CPU read and write, fixed over the view's reservation (as the GPU client maps granted memory). */
    const int status = sceKernelMapDirectMemory(&at, bytes, 0x03, 0x10, dmem, 16384) || at != (void *)address ? -1 : 0;
    pw_wine_perf_vlock_doing(was);
    return status;
}
static int protected(void *c, uintptr_t at, size_t n, unsigned prot)
{
    (void)c;
    return __wine_ps5_mprotect((void *)at, n, (int)prot);
}
static int flushed(void *c, uintptr_t at, size_t n)
{
    (void)c;
    const uint64_t begin = now_ns();
    /* A shared mapping of a file keeps its bytes in the page cache after munmap, so nothing is lost without a flush; the
     * synchronous one writes the page and (on this system's kernel, as on FreeBSD's) may sync the whole file. Direct
     * memory never enters the cache (pw_wine_section_use_direct). */
    const int status = flush_mode == FLUSH_NONE ? 0 : msync((void *)at, n, flush_mode == FLUSH_ASYNC ? MS_ASYNC : MS_SYNC);
    counted(STAT_FLUSH_CALLS, STAT_FLUSH_NS, begin);
    return status;
}
static int unmapped(void *c, uintptr_t at, size_t n)
{
    (void)c;
    const uint64_t begin = now_ns();
    const int status = munmap((void *)at, n);
    counted(STAT_UNMAP_CALLS, STAT_UNMAP_NS, begin);
    return status;
}
static int reserved(void *c, uintptr_t at, size_t n)
{
    (void)c;
    void *p = (void *)at;
    const uint64_t begin = now_ns();
    const int status = sceKernelReserveVirtualRange(&p, n, 0x90, section.ops.page) || p != (void *)at;
    counted(STAT_RESERVE_CALLS, STAT_RESERVE_NS, begin);
    return status;
}

/* The guard laid over the page in one call (the GPU client releases its memory this way), as the engine lays its own;
 * the cache unmaps and reserves where the kernel refuses or laying is off. */
static int laid(void *c, uintptr_t at, size_t n)
{
    (void)c;
    const uint64_t begin = now_ns();
    const int status = __wine_ps5_lay((void *)at, n);
    counted(STAT_RESERVE_CALLS, STAT_RESERVE_NS, begin);
    return status;
}
static int foreign(void *c, uintptr_t at, size_t n)
{
    (void)c;
    return __wine_ps5_foreign_in((void *)at, n);
}

static int direct(void *c, int fd)
{
    (void)c;
    return pw_wine_dmap_direct(fd);
}
static unsigned host_protection(void *c, uintptr_t at)
{
    (void)c;
    return protection_of(at);
}

static unsigned setting(const char *name, unsigned fallback, unsigned low, unsigned high)
{
    const char *text = getenv(name);
    char *end = NULL;
    if (!text || !*text)
        return fallback;
    const unsigned long value = strtoul(text, &end, 10);
    if (*end)
        return fallback;
    return value < low ? low : value > high ? high : (unsigned)value;
}

void __wine_ps5_section_init(uintptr_t host_limit, unsigned (*host_protection_of)(uintptr_t))
{
    const size_t span = (size_t)64 << 30, align = 65536;
    PwWineSectionOps ops = {
        NULL,    (size_t)sysconf(_SC_PAGESIZE), retain, closed, mapped, protected, flushed, unmapped,
        reserved, laid, foreign};
    const unsigned capacity = setting("WINE_PS5_SECTION_PAGES", DEFAULT_PAGES, 64, MAX_PAGES);
    cluster = setting("WINE_PS5_SECTION_CLUSTER", 1, 1, PW_WINE_SECTION_MAX_RUN);
    {
        const char *mode = getenv("WINE_PS5_SECTION_FLUSH");
        flush_mode = mode && !strcmp(mode, "none") ? FLUSH_NONE : mode && !strcmp(mode, "async") ? FLUSH_ASYNC : FLUSH_SYNC;
    }
    static const PwWineBlocksOps block_ops = { NULL, block_identity, block_read, block_write };
    pw_wine_blocks_init(&block_ops);
    static const PwWineDmapOps dmap_ops = { NULL, dmap_identity, dmap_page, dmap_map };
    pw_wine_dmap_init(&dmap_ops);
    {
        const char *probe = getenv("WINE_PS5_ASSERT_PROBE");
        assert_probe = getenv("WINE_PS5_CHILD") && probe && !strcmp(probe, "1");
    }
    protection_of = host_protection_of;
    if (pw_wine_section_init(&section, &ops, pages, capacity) ||
        pw_wine_section_use_index(&section, buckets, INDEX_BUCKETS) ||
        (protection_of && pw_wine_section_use_direct(&section, direct, host_protection)))
        return;
    if (!__wine_ps5_payload_memory_ready() || host_limit < span * 2)
        return;
    void *base = (void *)((host_limit / 2 + align - 1) & ~(uintptr_t)(align - 1));
    if (sceKernelReserveVirtualRange(&base, span, 0x90, align))
        return;
    arena_base = (uintptr_t)base;
    arena_bytes = span;
    pw_wine_mmap_placed = placed;
    fprintf(stderr, "wine-ps5: shared-section arena=%p bytes=%zu mapped_cache_bytes=%zu cluster=%u flush=%s\n", base, span,
            section.capacity * ops.page, cluster, flush_mode == FLUSH_NONE ? "none" : flush_mode == FLUSH_ASYNC ? "async" : "sync");
}

uint64_t __wine_ps5_section_min_bytes(void)
{
    return __wine_ps5_payload_memory_ready() ? pw_wine_dsection_min_bytes() : (uint64_t)256 << 20;
}

int __wine_ps5_section_arena_contains(uintptr_t base, size_t bytes)
{
    return arena_bytes && base >= arena_base && bytes <= arena_bytes &&
           base - arena_base <= arena_bytes - bytes;
}

void *__wine_ps5_section_address(size_t bytes, uintptr_t low, uintptr_t high, size_t align_mask)
{
    if (!arena_bytes || !bytes || bytes > arena_bytes || align_mask == UINTPTR_MAX)
        return NULL;
    uintptr_t at = arena_base > low ? arena_base : low;
    uintptr_t end = arena_base + arena_bytes;
    if (high && high < end - 1)
        end = high + 1;
    for (;;)
    {
        if (at > UINTPTR_MAX - align_mask)
            return NULL;
        at = (at + align_mask) & ~align_mask;
        if (at >= end || bytes > end - at)
            return NULL;
        uintptr_t next = at;
        for (PwWineSectionView *v = section.views; v; v = v->next)
            if (at < v->base + v->bytes && v->base < at + bytes && v->base + v->bytes > next)
                next = v->base + v->bytes;
        if (next == at)
            return (void *)at;
        at = next;
    }
}

int __wine_ps5_section_add(PwWineSectionView *view, uintptr_t base, size_t bytes, int fd,
                           uint64_t offset)
{
    /* Console check of the assertion policy (pw_wine_dl_libc.c): an assertion under Wine's virtual mutex, as
     * create_view's in a 2026-10-14 run (record removed from the tree; in git history). Only a helper with WINE_PS5_ASSERT_PROBE=1. */
    assert(!assert_probe && "WINE_PS5_ASSERT_PROBE: an assertion inside Wine's virtual mutex");
    /* The arena is no region of the engine's, so its views are asked about here, as file_region asks for the rest. */
    int status = !section.capacity ||
                         (__wine_ps5_section_arena_contains(base, bytes) ? __wine_ps5_foreign_in((void *)base, bytes)
                                                                         : __wine_ps5_dmem_file_region((void *)base, bytes))
                     ? -1 : pw_wine_section_add(&section, view, base, bytes, fd, offset);
    /* Not the pager's: whatever the engine adopted for it is bare again. */
    if (status)
        __wine_ps5_dmem_file_released((void *)base, bytes);
    if (!status)
        stats[STAT_VIEWS]++;
    if (!status)
        fprintf(stderr, "wine-ps5: demand shared view=%p bytes=%zu offset=%llu\n", (void *)base, bytes,
                (unsigned long long)offset);
    return status;
}
int __wine_ps5_section_remove(PwWineSectionView *view)
{
    const uintptr_t base = view->base;
    const size_t bytes = view->bytes;
    const int status = pw_wine_section_remove(&section, view);
    /* Every page is a guard again: the range the engine adopted for the view is bare, and its release asks the kernel. */
    if (!status)
        __wine_ps5_dmem_file_released((void *)base, bytes);
    return status;
}
int __wine_ps5_section_protect(uintptr_t base, size_t bytes, unsigned prot)
{
    return section.capacity ? pw_wine_section_protect(&section, base, bytes, prot) : 0;
}
unsigned __wine_ps5_section_cluster(void)
{
    return cluster;
}
unsigned __wine_ps5_section_failed_stage(void)
{
    return section.failed_stage;
}
int __wine_ps5_section_fault(uintptr_t at, unsigned prot, unsigned access, PwWineSectionRetry *retry, unsigned run)
{
    if (!section.capacity)
        return 0;
    const uint64_t begin = now_ns();
    const int status = pw_wine_section_fault_run(&section, at, prot, access, 0, retry, run < cluster ? run : cluster);
    counted(STAT_FAULT_CALLS, STAT_FAULT_NS, begin);
    return status;
}
/* A direct section's memory under address, for a second mapping of it (pw_wine_alias.h): the chunk's direct memory at
 * address and the extent of the view that chunk backs contiguously, [*low, *high), clipped to the title's answer (the
 * section's last chunk may be shorter). 1, or 0. Under Wine's virtual mutex, as the pager. */
int __wine_ps5_section_backing(uintptr_t address, int64_t *dmem, uintptr_t *low, uintptr_t *high)
{
    uint64_t offset, chunk_offset, chunk_bytes;
    int64_t chunk;
    int fd;
    if (!section.capacity || !pw_wine_section_backing(&section, address, &fd, &offset, low, high) ||
        pw_wine_dmap_lookup(fd, offset, &chunk, &chunk_offset, &chunk_bytes))
        return 0;
    if (*low < address - (uintptr_t)(offset - chunk_offset))
        *low = address - (uintptr_t)(offset - chunk_offset);
    if (*high > address + (uintptr_t)(chunk_offset + chunk_bytes - offset))
        *high = address + (uintptr_t)(chunk_offset + chunk_bytes - offset);
    *dmem = chunk + (int64_t)(offset - chunk_offset);
    return 1;
}
/* Counters for the census: views added, faults (calls, ns), then pages mapped, evicted, failures, the most cached at once,
 * cached now, and the native calls (map, flush, unmap, reserve) as calls and ns; then direct chunks mapped, their bytes
 * mapped now, transient failures retried, the title asked, chunks remembered, failed asks; then mappings placed over
 * the arena, and the last one's address and bytes. Returns how many exist. */
unsigned __wine_ps5_section_stats(uint64_t *out, unsigned count)
{
    uint64_t requests, remembered, ask_failures;
    pw_wine_dmap_stats(&requests, &remembered, &ask_failures);
    uint64_t values[] = {stats[STAT_VIEWS], stats[STAT_FAULT_CALLS], stats[STAT_FAULT_NS], section.faults, section.evictions,
                         section.failures, section.peak, section.count, stats[STAT_MAP_CALLS], stats[STAT_MAP_NS],
                         stats[STAT_FLUSH_CALLS], stats[STAT_FLUSH_NS], stats[STAT_UNMAP_CALLS], stats[STAT_UNMAP_NS],
                         stats[STAT_RESERVE_CALLS], stats[STAT_RESERVE_NS], section.direct_maps, section.direct_bytes,
                         section.retries, requests, remembered, ask_failures,
                         __atomic_load_n(&misplaced, __ATOMIC_RELAXED),
                         __atomic_load_n(&misplaced_address, __ATOMIC_RELAXED),
                         __atomic_load_n(&misplaced_bytes, __ATOMIC_RELAXED)};
    const unsigned total = (unsigned)(sizeof(values) / sizeof(values[0]));
    for (unsigned i = 0; i < count && i < total; i++)
        out[i] = values[i];
    return total;
}
