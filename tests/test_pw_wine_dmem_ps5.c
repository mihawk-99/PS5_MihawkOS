/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The console glue of the direct-memory engine (wine/ps5/pw_wine_dmem_ps5.c) against a model of libkernel as the
 * console answered it (exec-kernel-2026-10-14-0{1,2,3}): a payload's kernel keeps execute only on JIT memory, here a
 * memfd named pw-jit, and drops it silently everywhere else; a JIT object maps over a live mapping only at exactly its
 * size (K4); pwrite on it fails with ESPIPE (K5); execute asked at map time is dropped (K8). A payload's executable
 * memory is made at its first execute by __wine_ps5_exec_convert, and the payload SDK never sees execute. Each case
 * runs in its own process: the glue's state is set once.
 * The guard against mappings that are not Wine's runs against the same model: sceKernelVirtualQuery reads
 * /proc/self/maps (a PROT_NONE anonymous entry is a bare reservation, protection 0 and flags 0).
 *   cc -std=gnu11 -Itests/include tests/test_pw_wine_dmem_ps5.c wine/ps5/pw_wine_dmem_ps5.c wine/ps5/pw_wine_dmem.c \
 *      wine/ps5/pw_wine_heap.c wine/ps5/pw_wine_pages.c wine/ps5/pw_wine_vm.c -lpthread */
#define _GNU_SOURCE
#include "../wine/ps5/pw_wine_pages.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <ps5platform/memory.h>

int __wine_ps5_set_payload_memory(void *(*)(void *, size_t, int, int, int, off_t), int (*)(const void *, size_t, int));
int __wine_ps5_set_payload_direct_memory(int (*)(size_t, size_t, int, int64_t *));
int __wine_ps5_dmem_region(void *, size_t);
void *__wine_ps5_mmap(void *, size_t, int, int, int, off_t);
int __wine_ps5_munmap(void *, size_t);
int __wine_ps5_mprotect(void *, size_t, int);
int __wine_ps5_exec_convert(void *, size_t, int);
int __wine_ps5_host_allows(const void *, int);
void __wine_ps5_exec_refused(const void *);
int __wine_ps5_kernel_prot(const void *);
int __wine_ps5_dmem_gpu_keep(void *, size_t, int);
unsigned int __wine_ps5_exec_stats(uint64_t *, unsigned int);
unsigned int __wine_ps5_memory_stats(uint64_t *, unsigned int);
int __wine_ps5_dmem_keep(void *, size_t);
int __wine_ps5_native_hole(void **, size_t *);
int __wine_ps5_dmem_file_region(void *, size_t);
unsigned int __wine_ps5_foreign_stats(uint64_t *, unsigned int);
int __wine_ps5_foreign_in(void *, size_t);
int __wine_ps5_dmem_check(void *, size_t);
int __wine_ps5_vm_gap(const void *, const void *, int (*)(const void *, void **, void **), void **, void **);
int __wine_ps5_lay(void *, size_t);
int __wine_ps5_dmem_backing(uintptr_t, int64_t *, uintptr_t *, uintptr_t *);

enum { PAGE = 16384, RW = PROT_READ | PROT_WRITE, RX = PROT_READ | PROT_EXEC, RWX = RW | PROT_EXEC,
       FIXED_ANON = MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS };
/* __wine_ps5_exec_stats, in order. */
enum { REFUSED, CONVERSIONS, CONVERTED_KIB, CONVERT_FAILED, EXEC_REFUSED, GRANT_RETRIES, STATS };

/* What the kernel keeps execute on: 0 everywhere (a title), 1 JIT memory only (a payload, measured), 2 nowhere, 3 JIT
 * memory only and never with write (no RWX). */
static int exec_model = 1;

/* The /proc/self/maps entry holding address: its range, protection, inode, and whether it maps a JIT object. */
typedef struct Entry { uintptr_t low, high; unsigned protection; unsigned long inode; int jit; } Entry;
static int entry_at(uintptr_t address, Entry *entry)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    int found = 0;

    assert(maps);
    while (!found && fgets(line, sizeof(line), maps)) {
        unsigned long low, high, inode;
        char perms[8];
        int path = 0;
        if (sscanf(line, "%lx-%lx %7s %*s %*s %lu %n", &low, &high, perms, &inode, &path) < 4 || address < low ||
            address >= high)
            continue;
        *entry = (Entry){ low, high, (perms[0] == 'r') | (perms[1] == 'w') << 1 | (perms[2] == 'x') << 2, inode,
                          path && !strncmp(line + path, "/memfd:pw-jit", 13) };
        found = 1;
    }
    fclose(maps);
    return found;
}
static int all_jit(uintptr_t at, size_t bytes)
{
    Entry entry;
    for (uintptr_t page = at; page < at + bytes; page = entry.high)
        if (!entry_at(page, &entry) || !entry.jit) return 0;
    return 1;
}
static int jit_mapped(void)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    int found = 0;
    while (!found && fgets(line, sizeof(line), maps)) found = strstr(line, "/memfd:pw-jit") != NULL;
    fclose(maps);
    return found;
}
static int is_jit_fd(int fd)
{
    char path[64], target[128];
    ssize_t length;
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    length = readlink(path, target, sizeof(target) - 1);
    if (length <= 0) return 0;
    target[length] = 0;
    return !strncmp(target, "/memfd:pw-jit", 13);
}
static unsigned jit_fds(void)
{
    unsigned open_objects = 0;
    for (int fd = 0; fd < 1024; fd++) open_objects += is_jit_fd(fd);
    return open_objects;
}

/* libkernel, modelled. Every sceKernelMprotect is recorded with the protection asked; a range can refuse every
 * change (EACCES, as the console's 0x8002000d). Execute is kept or dropped per mapping. */
typedef struct Call { uintptr_t address; size_t bytes; int protection; } Call;
static Call calls[64];
static unsigned call_count, releases;
static size_t released_bytes;
static uintptr_t refuse_low, refuse_high;
static int refuse_jit, refuse_scratch, wait_for_writer;
static volatile int writer_faulted;
/* The GPU bits the kernel keeps beside the CPU's, per page of one range from gpu_low (64 pages). */
enum { GPU = 0x30 };
static uintptr_t gpu_low;
static unsigned char gpu_pages[64];
static unsigned gpu_bits_at(const void *address)
{
    const uintptr_t at = (uintptr_t)address;
    return gpu_low && at >= gpu_low && at < gpu_low + 64 * (uintptr_t)PAGE ? gpu_pages[(at - gpu_low) / PAGE] : 0;
}

int sceKernelMprotect(const void *address, size_t bytes, int protection)
{
    const uintptr_t at = (uintptr_t)address;
    const unsigned gpu = (unsigned)protection & 0xf0;
    if (call_count < 64) calls[call_count] = (Call){ at, bytes, protection };
    call_count++;
    if (at < refuse_high && at + bytes > refuse_low) return (int)0x8002000d;
    if ((protection & PROT_EXEC) && (exec_model == 2 || (exec_model == 3 && (protection & PROT_WRITE))))
        protection &= ~PROT_EXEC;
    for (uintptr_t page = at, high; page < at + bytes; page = high) {
        Entry entry;
        if (!entry_at(page, &entry)) return (int)0x8002000c;
        high = entry.high < at + bytes ? entry.high : at + bytes;
        if (mprotect((void *)page, high - page, !exec_model || entry.jit ? protection & 7 : protection & 3))
            return (int)(0x80020000u | (unsigned)errno);
    }
    for (uintptr_t page = at; page < at + bytes; page += PAGE)
        if (gpu_low && page >= gpu_low && page < gpu_low + 64 * (uintptr_t)PAGE)
            gpu_pages[(page - gpu_low) / PAGE] = (unsigned char)gpu;
    return 0;
}
int sceKernelQueryMemoryProtection(void *address, void **start, void **end, uint32_t *protection)
{
    Entry entry;
    if (!entry_at((uintptr_t)address, &entry)) return (int)0x80020016;
    *start = (void *)entry.low;
    *end = (void *)entry.high;
    *protection = entry.protection | gpu_bits_at(address);
    return 0;
}
int sceKernelJitCreateSharedMemory(const char *name, size_t bytes, int max_protection, int *fd)
{
    const time_t give_up = time(NULL) + 5;
    (void)max_protection;
    if (refuse_jit) return (int)0x8002000c;
    /* The writer of the lost-writes case faults on the read-only range before anything is copied. */
    while (wait_for_writer && !writer_faulted && time(NULL) < give_up) sched_yield();
    *fd = memfd_create(name, 0);
    if (*fd < 0 || ftruncate(*fd, (off_t)bytes)) return (int)0x8002000c;
    return 0;
}
/* K4: a JIT object maps over a live mapping only at exactly its size. K8: execute asked at map time is dropped. */
int sceKernelMmap(void *address, size_t bytes, int protection, int flags, int fd, off_t offset, void **out)
{
    struct stat object;
    void *mapped;
    if ((flags & MAP_FIXED) && is_jit_fd(fd) && (fstat(fd, &object) || (size_t)object.st_size != bytes))
        return (int)0x80020016;
    mapped = mmap(address, bytes, protection & ~PROT_EXEC, flags, fd, offset);
    if (mapped == MAP_FAILED) return (int)(0x80020000u | (unsigned)errno);
    *out = mapped;
    return 0;
}
/* K5: a JIT object cannot be written by pwrite; the glue must copy through a mapping. */
ssize_t pwrite(int fd, const void *buffer, size_t bytes, off_t offset)
{
    if (is_jit_fd(fd)) {
        errno = ESPIPE;
        return -1;
    }
    return syscall(SYS_pwrite64, fd, buffer, bytes, offset);
}
/* Only the fixed reservations the engine makes inside the test's own mappings: the heap area's address hints lie in
 * ASan's shadow gap. */
static int lay_ignored;   /* a reservation laid over a mapping answers success and leaves the mapping */
/* No-overwrite (0x80) is refused where anything is mapped. intruder: a mapping of this object takes the range just
 * before the next no-overwrite reservation (the GPU client's, placed into a hole between the guard's query and its
 * refill). */
static unsigned reserve_count;
static int intruder = -1;
int sceKernelReserveVirtualRange(void **address, size_t bytes, int flags, size_t alignment)
{
    (void)alignment;
    if (!(flags & 0x10)) return (int)0x8002000c;
    reserve_count++;
    if (lay_ignored && !(flags & 0x80)) return 0;
    if (intruder >= 0 && (flags & 0x80)) {
        assert(mmap(*address, PAGE, RW, MAP_SHARED | MAP_FIXED, intruder, 0) == *address);
        intruder = -1;
    }
    return mmap(*address, bytes, PROT_NONE, (flags & 0x80 ? MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS
                                                          : FIXED_ANON) | MAP_NORESERVE, -1, 0) == MAP_FAILED
               ? (int)0x8002000c : 0;
}
int sceKernelAllocateMainDirectMemory(size_t bytes, size_t alignment, int type, int64_t *offset)
{
    (void)bytes; (void)alignment; (void)type;
    *offset = 0;
    return 0;
}
/* Direct memory is refused execute at map time, as on the console. */
int sceKernelMapDirectMemory(void **address, size_t bytes, int protection, int flags, int64_t offset, size_t alignment)
{
    (void)flags; (void)offset; (void)alignment;
    if (protection & PROT_EXEC) return (int)0x80020016;
    return mmap(*address, bytes, RW, FIXED_ANON, -1, 0) == MAP_FAILED ? (int)0x8002000c : 0;
}
/* Every unmap is logged: the guard is judged by which ranges reached the kernel. */
static struct { uintptr_t address; size_t bytes; } unmaps[64];
static unsigned unmap_count, query_count;
int sceKernelMunmap(void *address, size_t bytes)
{
    if (unmap_count < 64) unmaps[unmap_count].address = (uintptr_t)address, unmaps[unmap_count].bytes = bytes;
    unmap_count++;
    return munmap(address, bytes) ? (int)0x80020016 : 0;
}
/* NEXT: the entry holding address, or the next one; EACCES past the last. Record: start, end, protection at +24, flags
 * at +32 (direct and committed for an accessible mapping, as the GPU client's 0x33 memory reads), name at +33. */
int sceKernelVirtualQuery(const void *address, int flags, void *record, size_t bytes)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    int status = (int)0x8002000d;

    assert(maps && flags == 1 && bytes == 72);
    query_count++;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long low, high;
        char perms[8];
        int path = 0;
        if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %n", &low, &high, perms, &path) < 3 || high <= (uintptr_t)address)
            continue;
        const int32_t protection = (perms[0] == 'r') | (perms[1] == 'w') << 1 | (perms[2] == 'x') << 2;
        const uint8_t kind = protection ? 0x12 : 0;
        memset(record, 0, bytes);
        memcpy(record, &low, 8);
        memcpy((char *)record + 8, &high, 8);
        memcpy((char *)record + 24, &protection, 4);
        memcpy((char *)record + 32, &kind, 1);
        if (path) strncpy((char *)record + 33, line + path, strcspn(line + path, "\n") < 31 ? strcspn(line + path, "\n") : 31);
        status = 0;
        break;
    }
    fclose(maps);
    return status;
}
int sceKernelReleaseDirectMemory(int64_t offset, size_t bytes)
{
    (void)offset;
    releases++;
    released_bytes = bytes;
    return 0;
}
int sceKernelDebugOutText(int channel, const char *text) { (void)channel; (void)text; return 0; }
bool ps5_memory_query(struct ps5_memory_stats *stats) { (void)stats; return false; }

/* The payload SDK's functions, as the loader hands them over: they must never see execute. */
static unsigned sdk_exec;
static void *sdk_mmap(void *address, size_t bytes, int protection, int flags, int fd, off_t offset)
{
    sdk_exec += !!(protection & PROT_EXEC);
    return mmap(address, bytes, protection, flags, fd, offset);
}
static int sdk_mprotect(const void *address, size_t bytes, int protection)
{
    sdk_exec += !!(protection & PROT_EXEC);
    return mprotect((void *)address, bytes, protection);
}
static int64_t next_offset, offset_gap;   /* a gap keeps neighbouring commits separate runs */
static int sdk_allocate(size_t bytes, size_t alignment, int type, int64_t *offset)
{
    (void)alignment; (void)type;
    *offset = next_offset;
    next_offset += (int64_t)bytes + offset_gap;
    return 0;
}

static const unsigned char code[] = { 0xb8, 42, 0, 0, 0, 0xc3 };   /* mov eax, 42; ret */
#ifdef __clang__
__attribute__((no_sanitize("function")))
#endif
static int run(const void *at) { return ((int (*)(void))(uintptr_t)at)(); }

/* A 16 KiB-aligned PROT_NONE range of the test's own. */
static uintptr_t region(size_t bytes)
{
    void *at = mmap(NULL, bytes + PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    assert(at != MAP_FAILED);
    return ((uintptr_t)at + PAGE - 1) & ~(uintptr_t)(PAGE - 1);
}
static void stats(uint64_t v[STATS]) { assert(__wine_ps5_exec_stats(v, STATS) == 7); }
static unsigned kernel_protection(const void *address)
{
    Entry entry;
    assert(entry_at((uintptr_t)address, &entry));
    return entry.protection;
}
static uint64_t backed_bytes(void)
{
    uint64_t v[20];
    assert(__wine_ps5_memory_stats(v, 20) == 20);
    return v[0];
}

/* The helper's shared-page range (pw_wine_pages), where the glue makes its temporary mapping of a new object. */
static int scratch_map(void *context, uintptr_t address, size_t bytes, int protection, int fd)
{
    void *out = NULL;
    (void)context;
    if (refuse_scratch) {
        errno = ENOMEM;
        return -1;
    }
    return sceKernelMmap((void *)address, bytes, protection, MAP_SHARED | MAP_FIXED, fd, 0, &out) ||
           out != (void *)address ? -1 : 0;
}
static int scratch_lay_back(void *context, uintptr_t address, size_t bytes)
{
    (void)context;
    assert(mmap((void *)address, bytes, PROT_NONE, FIXED_ANON | MAP_NORESERVE, -1, 0) == (void *)address);
    return 0;
}
static void payload(int direct, int scratch)
{
    static const PwWinePagesOps ops = { NULL, scratch_map, scratch_lay_back };
    if (scratch) assert(!pw_wine_pages_init(&ops, region(64 * PAGE), 64 * PAGE));
    assert(!__wine_ps5_set_payload_memory(sdk_mmap, sdk_mprotect));
    if (direct) assert(!__wine_ps5_set_payload_direct_memory(sdk_allocate));
}

/* The model itself: an object larger than the mapping does not map over it (K4), and pwrite is refused (K5). */
static void model_rules(void)
{
    const uintptr_t at = region(2 * PAGE);
    int fd = -1;
    void *out = NULL;
    assert(!sceKernelJitCreateSharedMemory("pw-jit", 2 * PAGE, 7, &fd) && is_jit_fd(fd));
    assert(sceKernelMmap((void *)at, PAGE, RW, MAP_SHARED | MAP_FIXED, fd, 0, &out) == (int)0x80020016);
    errno = 0;
    assert(pwrite(fd, code, sizeof(code), 0) == -1 && errno == ESPIPE);
    close(fd);
}

/* The default helper: the application's direct memory. (a) a direct page's first execute, (b) protection changes on
 * JIT memory, (h) decommitted and freed, nothing left, plus the self-check, refusals and memory outside the regions. */
static void direct_payload(void)
{
    uint64_t before[STATS], after[STATS];
    const uintptr_t base = region(64 << 20);
    unsigned char *page = (unsigned char *)base + PAGE, *next = page + PAGE;
    uint64_t backed;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    /* The self-check made one page of JIT memory and freed it. */
    stats(before);
    assert(before[CONVERSIONS] == 1 && before[CONVERTED_KIB] == 16 && !before[CONVERT_FAILED]);
    assert(!jit_mapped() && !jit_fds());

    assert(__wine_ps5_mmap(page, 2 * PAGE, RW, FIXED_ANON, -1, 0) == page);
    memcpy(page, code, sizeof(code));
    memset(next, 0x77, PAGE);
    /* RX on direct memory: the kernel is asked for read only. */
    call_count = 0;
    assert(!__wine_ps5_mprotect(page, PAGE, RX));
    assert(call_count == 1 && calls[0].address == (uintptr_t)page && calls[0].protection == PROT_READ);
    assert(kernel_protection(page) == PROT_READ);

    /* (a) The first execute: one JIT object of exactly the page, its direct memory released, the code runs, the page
     * beside it untouched. */
    backed = backed_bytes();
    releases = 0;
    assert(!__wine_ps5_exec_convert(page, PAGE, RX));
    assert(all_jit((uintptr_t)page, PAGE) && kernel_protection(page) == RX && run(page) == 42);
    assert(releases == 1 && released_bytes == PAGE && backed_bytes() == backed - PAGE);
    assert(!all_jit((uintptr_t)next, PAGE) && kernel_protection(next) == RW);
    assert(next[0] == 0x77 && next[PAGE - 1] == 0x77);
    next[0] = 1;
    stats(after);
    assert(after[CONVERSIONS] == before[CONVERSIONS] + 1 && after[CONVERTED_KIB] == before[CONVERTED_KIB] + 16);
    assert(!after[CONVERT_FAILED] && !jit_fds());

    /* (b) RW, RX, RWX: one kernel call each with the whole protection; code patched in place runs. */
    call_count = 0;
    assert(!__wine_ps5_mprotect(page, PAGE, RW) && kernel_protection(page) == RW);
    page[1] = 43;
    assert(!__wine_ps5_mprotect(page, PAGE, RX) && kernel_protection(page) == RX && run(page) == 43);
    assert(!__wine_ps5_mprotect(page, PAGE, RWX) && kernel_protection(page) == RWX);
    page[1] = 44;
    assert(run(page) == 44);
    assert(call_count == 3 && calls[0].protection == RW && calls[1].protection == RX && calls[2].protection == RWX);

    /* A fault the kernel already allows runs again, counted; one it does not is not. */
    stats(before);
    assert(__wine_ps5_host_allows(page + 100, PROT_EXEC) && __wine_ps5_host_allows(page, PROT_WRITE));
    assert(!__wine_ps5_host_allows(next, PROT_EXEC));
    stats(after);
    assert(after[GRANT_RETRIES] == before[GRANT_RETRIES] + 2);
    /* An execute fault's record (virtual.c, ps5_note_exec_fault) reads the kernel's protection, counting nothing, and
     * -1 where nothing is mapped. */
    assert(__wine_ps5_kernel_prot(page + 100) == RWX && __wine_ps5_kernel_prot(next) == (int)kernel_protection(next));
    assert(__wine_ps5_kernel_prot((void *)(uintptr_t)16) == -1);
    stats(before);
    (void)__wine_ps5_kernel_prot(page);
    stats(after);
    assert(after[GRANT_RETRIES] == before[GRANT_RETRIES]);

    /* A refusal: one call, no retry, counted; the page keeps its access. */
    refuse_low = (uintptr_t)next;
    refuse_high = refuse_low + PAGE;
    call_count = 0;
    assert(__wine_ps5_mprotect(next, PAGE, RX) == -1 && call_count == 1);
    refuse_low = refuse_high = 0;
    stats(before);
    assert(before[REFUSED] == after[REFUSED] + 1);
    next[0] = 2;

    /* (h) Decommitted, then freed: no JIT mapping is left. */
    assert(__wine_ps5_mmap(page, PAGE, PROT_NONE, FIXED_ANON, -1, 0) == page);
    assert(!jit_mapped());
    assert(__wine_ps5_mmap(page, PAGE, RW, FIXED_ANON, -1, 0) == page);
    memcpy(page, code, sizeof(code));
    assert(!__wine_ps5_exec_convert(page, PAGE, RX) && run(page) == 42 && jit_mapped());
    assert(!__wine_ps5_munmap(page, PAGE) && !jit_mapped() && !jit_fds());

    /* Executable anonymous memory outside the regions: mapped without execute, converted whole at its first execute. */
    unsigned char *outside = (unsigned char *)region(4 * PAGE);   /* 16 KiB-aligned, as the console's */
    assert(__wine_ps5_mmap(outside, 4 * PAGE, RWX, FIXED_ANON, -1, 0) == outside && kernel_protection(outside) == RW);
    memcpy(outside + 3 * PAGE, code, sizeof(code));
    assert(!__wine_ps5_exec_convert(outside, 4 * PAGE, RWX));
    assert(all_jit((uintptr_t)outside, 4 * PAGE) && kernel_protection(outside) == RWX && run(outside + 3 * PAGE) == 42);
    assert(!sdk_exec);
}

/* (c) WINE_PS5_PAYLOAD_DMEM=0: flexible memory, in a small reservation (outside the engine: MAP_FIXED alone) and in
 * a large one (the engine's flexible runs). */
static void flexible_payload(void)
{
    const uintptr_t small = region(64 * PAGE), large = region(512 << 20);
    unsigned char *page = (unsigned char *)small + PAGE, *inside = (unsigned char *)large + PAGE;

    payload(0, 1);
    assert(!__wine_ps5_dmem_region((void *)small, 64 * PAGE));
    assert(!__wine_ps5_mprotect(page, PAGE, RW));
    memcpy(page, code, sizeof(code));
    assert(!__wine_ps5_mprotect(page, PAGE, RX) && kernel_protection(page) == PROT_READ);
    assert(!__wine_ps5_exec_convert(page, PAGE, RX));
    assert(all_jit((uintptr_t)page, PAGE) && run(page) == 42 && kernel_protection(page + PAGE) == PROT_NONE);
    /* The page beside it, then: only that one is copied, and the JIT page is neither made read-only again nor
     * replaced. When the next one fails, both JIT pages keep running. */
    unsigned char *beside = page + PAGE, *third = page + 2 * PAGE;
    Entry entry;
    unsigned long object;
    assert(entry_at((uintptr_t)page, &entry) && entry.jit);
    object = entry.inode;
    assert(!__wine_ps5_mprotect(beside, 2 * PAGE, RW));
    memcpy(beside, code, sizeof(code));
    memcpy(third, code, sizeof(code));
    assert(!__wine_ps5_mprotect(beside, 2 * PAGE, RX) && kernel_protection(beside) == PROT_READ);
    call_count = 0;
    assert(!__wine_ps5_exec_convert(page, 2 * PAGE, RX));
    for (unsigned i = 0; i < call_count && i < 64; i++)
        assert(calls[i].address != (uintptr_t)page || calls[i].protection != PROT_READ);
    assert(entry_at((uintptr_t)page, &entry) && entry.jit && entry.inode == object);
    assert(entry_at((uintptr_t)beside, &entry) && entry.jit && entry.inode != object && run(beside) == 42);
    refuse_jit = 1;
    assert(__wine_ps5_exec_convert(page, 3 * PAGE, RX) == -1);
    refuse_jit = 0;
    assert(kernel_protection(page) == RX && kernel_protection(beside) == RX && run(page) == 42 && run(beside) == 42);
    assert(kernel_protection(third) == PROT_READ && !memcmp(third, code, sizeof(code)));

    assert(!__wine_ps5_dmem_region((void *)large, 512 << 20));
    assert(__wine_ps5_mmap(inside, PAGE, RW, FIXED_ANON, -1, 0) == inside);
    memcpy(inside, code, sizeof(code));
    call_count = 0;
    assert(!__wine_ps5_mprotect(inside, PAGE, RX) && call_count == 1 && calls[0].protection == PROT_READ);
    assert(!__wine_ps5_exec_convert(inside, PAGE, RX));
    assert(all_jit((uintptr_t)inside, PAGE) && kernel_protection(inside) == RX && run(inside) == 42);
    assert(!sdk_exec && !jit_fds());
}

/* (d) Failures: the JIT object refused, the temporary mapping refused (after the read-only step and the object
 * made), and execute dropped on JIT memory too. Each restores what the kernel held, keeps the contents and the
 * engine's runs, closes the object and is counted. (f) A range across a region's edge is refused untouched. */
static void failures(void)
{
    uint64_t before[STATS], after[STATS];
    const uintptr_t base = region(64 << 20);
    unsigned char *page = (unsigned char *)base + PAGE;
    uint64_t backed;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    assert(__wine_ps5_mmap(page, PAGE, RW, FIXED_ANON, -1, 0) == page);
    memcpy(page, code, sizeof(code));
    assert(!__wine_ps5_mprotect(page, PAGE, RWX) && kernel_protection(page) == RW);
    backed = backed_bytes();
    stats(before);

    refuse_jit = 1;
    errno = 0;
    assert(__wine_ps5_exec_convert(page, PAGE, RWX) == -1 && errno == ENOMEM);
    refuse_jit = 0;
    assert(kernel_protection(page) == RW && !memcmp(page, code, sizeof(code)) && backed_bytes() == backed);
    page[100] = 1;

    refuse_scratch = 1;
    assert(__wine_ps5_exec_convert(page, PAGE, RWX) == -1);
    refuse_scratch = 0;
    assert(kernel_protection(page) == RW && !memcmp(page, code, sizeof(code)) && backed_bytes() == backed);
    assert(!jit_fds() && !jit_mapped());
    page[100] = 2;

    exec_model = 2;
    assert(__wine_ps5_exec_convert(page, PAGE, RWX) == -1 && errno == EACCES);
    exec_model = 1;
    assert(kernel_protection(page) == RW && !memcmp(page, code, sizeof(code)) && page[100] == 2 && !jit_fds());
    stats(after);
    assert(after[CONVERT_FAILED] == before[CONVERT_FAILED] + 3 && after[CONVERSIONS] == before[CONVERSIONS] + 1);

    /* Converted again once the kernel keeps execute: it runs. */
    assert(!__wine_ps5_exec_convert(page, PAGE, RWX) && run(page) == 42);

    /* Not one view: across the region's end. */
    call_count = 0;
    errno = 0;
    assert(__wine_ps5_exec_convert((void *)(base + (64 << 20) - PAGE), 2 * PAGE, RX) == -1 && errno == EINVAL);
    assert(!call_count);

    /* An execute fault Wine allows that nothing serves is counted. */
    __wine_ps5_exec_refused(page);
    stats(before);
    assert(before[EXEC_REFUSED] == after[EXEC_REFUSED] + 1);
}

/* (f) A page that is JIT memory already stays the same object; only its direct neighbour is converted. */
static void caller_runs_kept(void)
{
    uint64_t before[STATS], after[STATS];
    const uintptr_t base = region(64 << 20);
    unsigned char *first = (unsigned char *)base + PAGE, *second = first + PAGE;
    Entry entry;
    unsigned long object;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    assert(__wine_ps5_mmap(first, 2 * PAGE, RW, FIXED_ANON, -1, 0) == first);
    memcpy(first, code, sizeof(code));
    memcpy(second, code, sizeof(code));
    assert(!__wine_ps5_mprotect(first, 2 * PAGE, RX));
    assert(!__wine_ps5_exec_convert(first, PAGE, RX));
    assert(entry_at((uintptr_t)first, &entry) && entry.jit);
    object = entry.inode;
    stats(before);
    assert(!__wine_ps5_exec_convert(first, 2 * PAGE, RX));
    stats(after);
    assert(after[CONVERSIONS] == before[CONVERSIONS] + 1 && after[CONVERTED_KIB] == before[CONVERTED_KIB] + 16);
    assert(entry_at((uintptr_t)first, &entry) && entry.jit && entry.inode == object);
    assert(entry_at((uintptr_t)second, &entry) && entry.jit && entry.inode != object);
    assert(run(first) == 42 && run(second) == 42);
}

/* (e) No write is lost: a thread keeps incrementing a counter in the page while it is converted. Its write faults on
 * the read-only page before the copy (the model's JIT creation waits for that fault); the handler models patch 1127:
 * it waits for virtual_mutex, then runs the write again because the kernel now allows it. */
static pthread_mutex_t virtual_mutex = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t watched_low, watched_high;
static volatile int stop_writing;
static void on_fault(int signal_number, siginfo_t *info, void *context)
{
    const uintptr_t at = (uintptr_t)info->si_addr;
    int again;
    (void)context;
    if (at < watched_low || at >= watched_high) {
        signal(signal_number, SIG_DFL);
        return;
    }
    writer_faulted = 1;
    pthread_mutex_lock(&virtual_mutex);
    again = __wine_ps5_host_allows(info->si_addr, PROT_WRITE);
    pthread_mutex_unlock(&virtual_mutex);
    if (!again) signal(signal_number, SIG_DFL);
}
static void *writer(void *counter)
{
    uintptr_t written = 0;
    while (!stop_writing) {
        __atomic_fetch_add((uint64_t *)counter, 1, __ATOMIC_RELAXED);
        written++;
    }
    return (void *)written;
}
static void lost_writes(void)
{
    const uintptr_t base = region(64 << 20);
    unsigned char *page = (unsigned char *)base + PAGE;
    uint64_t *counter = (uint64_t *)(page + 64), v[STATS];
    struct sigaction action = { 0 };
    pthread_t thread;
    void *written;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    assert(__wine_ps5_mmap(page, PAGE, RW, FIXED_ANON, -1, 0) == page);
    assert(!__wine_ps5_mprotect(page, PAGE, RWX) && kernel_protection(page) == RW);
    action.sa_sigaction = on_fault;
    action.sa_flags = SA_SIGINFO;
    assert(!sigaction(SIGSEGV, &action, NULL));
    watched_low = (uintptr_t)page;
    watched_high = watched_low + PAGE;
    wait_for_writer = 1;
    assert(!pthread_create(&thread, NULL, writer, counter));
    while (!__atomic_load_n(counter, __ATOMIC_RELAXED)) sched_yield();
    pthread_mutex_lock(&virtual_mutex);
    assert(!__wine_ps5_exec_convert(page, PAGE, RWX));
    pthread_mutex_unlock(&virtual_mutex);
    /* The writer goes on, on the new mapping. */
    const uint64_t converted_at = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (__atomic_load_n(counter, __ATOMIC_RELAXED) == converted_at) sched_yield();
    stop_writing = 1;
    assert(!pthread_join(thread, &written));
    assert(writer_faulted && *counter == (uint64_t)(uintptr_t)written && all_jit((uintptr_t)page, PAGE));
    stats(v);
    assert(v[GRANT_RETRIES] >= 1);
}

/* (g) The self-check refuses a kernel that drops execute on JIT memory too, and a helper with no range for the
 * temporary mapping: the regions stay plain reservations. */
static void execute_not_kept(void)
{
    exec_model = 2;
    payload(1, 1);
    assert(__wine_ps5_dmem_region((void *)region(64 << 20), 64 << 20) == -1);
}
static void no_scratch(void)
{
    payload(1, 0);
    assert(__wine_ps5_dmem_region((void *)region(64 << 20), 64 << 20) == -1);
}

/* (g) A title, unchanged: its direct memory takes execute from the kernel's mprotect; it has no JIT memory. */
static void title(void)
{
    const uintptr_t base = region(64 << 20);
    unsigned char *page = (unsigned char *)base + PAGE;

    exec_model = 0;
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    assert(__wine_ps5_mmap(page, PAGE, RW, FIXED_ANON, -1, 0) == page);
    memcpy(page, code, sizeof(code));
    call_count = 0;
    assert(!__wine_ps5_mprotect(page, PAGE, RX) && call_count == 1 && calls[0].protection == RX && run(page) == 42);
    errno = 0;
    assert(__wine_ps5_exec_convert(page, PAGE, RX) == -1 && errno == ENOTSUP);
}

/* (h) Host memory a program gave the GPU (pw_wine_gpu_keep.h, Wine patch 2425), in a title: every protection change keeps
 * GPU read and write on those pages and on nothing else; a repeated import keeps them until both end; the import's
 * end takes them away; a mapping unmapped or replaced there loses them first. */
static void gpu_kept(void)
{
    const uintptr_t base = region(64 << 20);
    unsigned char *heap = (unsigned char *)base + PAGE, *next = heap + 2 * PAGE;

    exec_model = 0;
    gpu_low = (uintptr_t)heap;
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    assert(__wine_ps5_mmap(heap, 3 * PAGE, RW, FIXED_ANON, -1, 0) == heap);
    /* Recorded before the GPU client's grant, as win32u does; an end with no record is refused. */
    assert(!__wine_ps5_dmem_gpu_keep(heap + 100, 2 * PAGE - 100, 1));
    assert(__wine_ps5_dmem_gpu_keep(next, PAGE, 0) == -1);
    assert(!sceKernelMprotect(heap, 2 * PAGE, RW | GPU));
    call_count = 0;
    assert(!__wine_ps5_mprotect(heap, 3 * PAGE, PROT_READ));
    assert(call_count == 2 && calls[0].address == (uintptr_t)heap && calls[0].bytes == 2 * PAGE &&
           calls[0].protection == (PROT_READ | GPU) && calls[1].address == (uintptr_t)next &&
           calls[1].protection == PROT_READ);
    assert(gpu_bits_at(heap) == GPU && gpu_bits_at(heap + PAGE) == GPU && !gpu_bits_at(next));
    assert(kernel_protection(heap) == PROT_READ && kernel_protection(next) == PROT_READ);

    /* The same memory imported again: kept until both imports end; the end gives the CPU's protection back. */
    assert(!__wine_ps5_dmem_gpu_keep(heap, 2 * PAGE, 1));
    assert(!__wine_ps5_dmem_gpu_keep(heap, 2 * PAGE, 0));
    assert(!__wine_ps5_mprotect(heap, PAGE, RW) && gpu_bits_at(heap) == GPU);
    call_count = 0;
    assert(!__wine_ps5_dmem_gpu_keep(heap, 2 * PAGE, 0));
    assert(!gpu_bits_at(heap) && !gpu_bits_at(heap + PAGE) && kernel_protection(heap) == RW && call_count >= 1);
    call_count = 0;
    assert(!__wine_ps5_mprotect(heap, 2 * PAGE, RW) && call_count == 1 && calls[0].protection == RW);

    /* Unmapped while imported (the program freed it first): the GPU's access goes before the mapping; the record ends
     * later, over nothing. */
    assert(!__wine_ps5_dmem_gpu_keep(heap, PAGE, 1) && !sceKernelMprotect(heap, PAGE, RW | GPU));
    assert(!__wine_ps5_munmap(heap, 3 * PAGE) && !gpu_bits_at(heap));
    assert(!__wine_ps5_dmem_gpu_keep(heap, PAGE, 0));

    /* Replaced by a fixed mapping while imported: the same. */
    assert(__wine_ps5_mmap(heap, PAGE, RW, FIXED_ANON, -1, 0) == heap);
    assert(!__wine_ps5_dmem_gpu_keep(heap, PAGE, 1) && !sceKernelMprotect(heap, PAGE, RW | GPU));
    assert(__wine_ps5_mmap(heap, PAGE, RW, FIXED_ANON, -1, 0) == heap && !gpu_bits_at(heap));
    assert(!__wine_ps5_dmem_gpu_keep(heap, PAGE, 0));
    gpu_low = 0;
}

/* (g) The self-check's RWX step: a kernel that keeps RX but not RWX on JIT memory is refused. */
static void rwx_not_kept(void)
{
    exec_model = 3;
    payload(1, 1);
    assert(__wine_ps5_dmem_region((void *)region(64 << 20), 64 << 20) == -1);
}

/* (f) A range whose later page is a shared file view (a caller run, not JIT): the query of every page refuses it. */
static void caller_not_jit(void)
{
    uint64_t before[STATS], after[STATS];
    const uintptr_t base = region(64 << 20);
    unsigned char *first = (unsigned char *)base + PAGE, *second = first + PAGE;
    const int fd = memfd_create("section", 0);

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20));
    assert(fd >= 0 && !ftruncate(fd, PAGE));
    assert(__wine_ps5_mmap(first, PAGE, RW, FIXED_ANON, -1, 0) == first);
    assert(__wine_ps5_mmap(second, PAGE, RW, MAP_SHARED | MAP_FIXED, fd, 0) == second);
    memcpy(first, code, sizeof(code));
    assert(!__wine_ps5_mprotect(first, 2 * PAGE, RX));
    stats(before);
    errno = 0;
    assert(__wine_ps5_exec_convert(first, 2 * PAGE, RX) == -1 && errno == EACCES);
    stats(after);
    assert(after[CONVERT_FAILED] == before[CONVERT_FAILED] + 1);
}

/* (d) A full run table: refused before anything is replaced, the direct memory kept. */
static void table_full(void)
{
    const uintptr_t base = region(1024 << 20);
    unsigned char *page = (unsigned char *)base + PAGE;
    unsigned runs;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 1024 << 20));
    offset_gap = PAGE;
    for (runs = 0; __wine_ps5_mmap(page + (size_t)runs * PAGE, PAGE, RW, FIXED_ANON, -1, 0) != MAP_FAILED; runs++) {}
    assert(runs > 1000);
    memcpy(page, code, sizeof(code));
    assert(!__wine_ps5_mprotect(page, PAGE, RX));
    releases = 0;
    errno = 0;
    assert(__wine_ps5_exec_convert(page, PAGE, RX) == -1 && errno == ENOMEM);
    assert(!releases && !all_jit((uintptr_t)page, PAGE) && !jit_fds());
}

/* A shared file mapped over the middle of a run when the table has no room to split it: the adopt fails and the run
 * still names direct memory no longer mapped there. No alias of any run after that (the 4 KiB MMU's stores step), or
 * a store through one is lost (review finding 4, 2026-10-15). */
static void stale_backing(void)
{
    const uintptr_t base = region(1024 << 20);
    unsigned char *three = (unsigned char *)base + PAGE, *page = three + 4 * PAGE;
    const int file = memfd_create("section", 0);
    int64_t offset;
    uintptr_t low, high;
    unsigned runs;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 1024 << 20));
    offset_gap = PAGE;
    assert(__wine_ps5_mmap(three, 3 * PAGE, RW, FIXED_ANON, -1, 0) == three);
    assert(__wine_ps5_dmem_backing((uintptr_t)three + PAGE, &offset, &low, &high) && low == (uintptr_t)three &&
           high == (uintptr_t)three + 3 * PAGE);
    for (runs = 0; __wine_ps5_mmap(page + (size_t)runs * PAGE, PAGE, RW, FIXED_ANON, -1, 0) != MAP_FAILED; runs++) {}
    assert(runs > 1000 && __wine_ps5_dmem_backing((uintptr_t)page, &offset, &low, &high));
    assert(file >= 0 && !ftruncate(file, PAGE));
    assert(__wine_ps5_mmap(three + PAGE, PAGE, RW, MAP_SHARED | MAP_FIXED, file, 0) == three + PAGE);
    assert(!__wine_ps5_dmem_backing((uintptr_t)three + PAGE, &offset, &low, &high));
    assert(!__wine_ps5_dmem_backing((uintptr_t)page, &offset, &low, &high));
}

/* The crash of 2026-10-15 (late-library): a helper's GPU started, and the kernel held a wired mapping inside a span
 * Wine had reserved and believed bare ("unwire timeout at 0xc80000000"). Here: a region Wine reserved, one run of
 * Wine's at P, and after it a foreign entry F (a shared memfd, 4 pages, read-write) placed inside the region. No commit
 * maps over F, no unmap touches it, a release goes around it, and Wine is offered it as a hole. */
static int overlaps(uintptr_t low, uintptr_t high)
{
    for (unsigned i = 0; i < unmap_count && i < 64; i++)
        if (unmaps[i].address < high && low < unmaps[i].address + unmaps[i].bytes) return 1;
    return 0;
}
static void foreign_inside_reservation(void)
{
    enum { MIB = 1 << 20 };
    const uintptr_t base = region(256 * MIB), p = base + 16 * MIB, f = p + 64 * MIB, f_end = f + 4 * PAGE;
    const int radv = memfd_create("radv", 0), file = memfd_create("section", 0);
    uint64_t v[25];
    Entry entry;
    void *hole;
    size_t hole_bytes;

    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 256 * MIB));
    assert(__wine_ps5_mmap((void *)p, PAGE, RW, FIXED_ANON, -1, 0) == (void *)p);
    assert(radv >= 0 && !ftruncate(radv, 4 * PAGE) && file >= 0 && !ftruncate(file, 4 * PAGE));
    assert(mmap((void *)f, 4 * PAGE, RW, MAP_SHARED | MAP_FIXED, radv, 0) == (void *)f);
    memset((void *)f, 0x5a, 4 * PAGE);

    /* Commits over F, by mapping, by protection, a shared file view and a pager view: each refused, F intact. */
    errno = 0;
    assert(__wine_ps5_mmap((void *)(f - MIB), 2 * MIB, RW, FIXED_ANON, -1, 0) == MAP_FAILED && errno == EEXIST);
    errno = 0;
    assert(__wine_ps5_mprotect((void *)(f - MIB), 2 * MIB, RW) == -1 && errno == EEXIST);
    errno = 0;
    assert(__wine_ps5_mmap((void *)f, 4 * PAGE, RW, MAP_SHARED | MAP_FIXED, file, 0) == MAP_FAILED && errno == EEXIST);
    assert(__wine_ps5_dmem_file_region((void *)p, 128 * MIB) == -1);
    assert(entry_at(f, &entry) && entry.low == f && entry.high == f_end && entry.protection == RW);
    for (unsigned i = 0; i < 4 * PAGE; i++) assert(((unsigned char *)f)[i] == 0x5a);
    assert(kernel_protection((void *)(f - MIB)) == PROT_NONE);

    /* Decommit over Wine's run and F: the run is a reservation again, nothing is unmapped. */
    unmap_count = 0;
    assert(__wine_ps5_mmap((void *)p, 128 * MIB, PROT_NONE, FIXED_ANON, -1, 0) == (void *)p);
    assert(kernel_protection((void *)p) == PROT_NONE && !unmap_count && entry_at(f, &entry) && entry.high == f_end);

    /* Patch 1128's release: kept reserved, the kernel asked about each entry in it, no unmap at all. */
    query_count = 0;
    assert(!__wine_ps5_dmem_keep((void *)p, 128 * MIB) && query_count == 3 && !unmap_count);   /* one an entry */
    assert(__wine_ps5_mmap((void *)p, 128 * MIB, PROT_NONE, FIXED_ANON, -1, 0) == (void *)p && !unmap_count);

    /* The other releases unmap around F: exactly [P, F) and [F + 4 pages, P + 128 MiB), F's pages still owned. */
    releases = 0;
    assert(__wine_ps5_mmap((void *)p, PAGE, RW, FIXED_ANON, -1, 0) == (void *)p);
    assert(!__wine_ps5_munmap((void *)p, 128 * MIB));
    assert(unmap_count == 2 && unmaps[0].address == p && unmaps[0].bytes == f - p);
    assert(unmaps[1].address == f_end && unmaps[1].bytes == p + 128 * MIB - f_end && !overlaps(f, f_end));
    assert(releases == 1 && !entry_at(p, &entry) && !entry_at(f_end, &entry));
    assert(entry_at(f, &entry) && entry.high == f_end && ((unsigned char *)f)[4 * PAGE - 1] == 0x5a);
    errno = 0;
    assert(__wine_ps5_mmap((void *)f, PAGE, RW, FIXED_ANON, -1, 0) == MAP_FAILED && errno == EEXIST);

    /* Wine is offered F once, and again after the next refusal there. */
    assert(__wine_ps5_native_hole(&hole, &hole_bytes) == 1 && hole == (void *)f && hole_bytes == 4 * PAGE);
    assert(!__wine_ps5_native_hole(&hole, &hole_bytes));
    assert(__wine_ps5_mmap((void *)f, PAGE, RW, FIXED_ANON, -1, 0) == MAP_FAILED);
    assert(__wine_ps5_native_hole(&hole, &hole_bytes) == 1 && hole == (void *)f);
    assert(__wine_ps5_foreign_stats(v, 25) == 25);
    assert(v[0] == 1 && v[1] == 1 && v[2] >= 6 && !v[3] && v[4] == 1);
    assert(v[5] == f && v[6] == 4 * PAGE && v[7] == RW && v[8] == 1 && v[9] >= 6);

    /* Cost: a bare 64 MiB release, one query and one unmap; a fresh bare commit, one query and no unmap. */
    const uintptr_t bare = base + 160 * MIB;
    query_count = unmap_count = 0;
    assert(!__wine_ps5_munmap((void *)bare, 64 * MIB) && query_count == 1 && unmap_count == 1);
    query_count = unmap_count = 0;
    assert(__wine_ps5_mmap((void *)(base + 240 * MIB), PAGE, RW, FIXED_ANON, -1, 0) == (void *)(base + 240 * MIB));
    assert(query_count == 1 && !unmap_count);
}

/* The exports patch 1128 and the pager use, on the same model. G is the GPU client's mapping placed by the kernel after
 * Wine reserved the space: outside the regions (the section arena), inside a view the engine adopted, and in a bare
 * part of a region (a reserved area Wine kept). Then a hole in a reservation of Wine's (the client freed what it had
 * placed there): refilled before a commit trusts it, and when the client's next mapping takes it first, found. */
static uintptr_t owned_low, owned_high;
static int owned_area(const void *address, void **start, void **end)
{
    if ((uintptr_t)address >= owned_high) return 1;
    *start = (void *)owned_low;
    *end = (void *)owned_high;
    return 0;
}
static void guard_exports(void)
{
    enum { MIB = 1 << 20 };
    const uintptr_t base = region(64 * MIB), outside = region(8 * MIB), view = base + 16 * MIB, g = outside + 2 * MIB;
    const uintptr_t in_view = view + 8 * MIB, bare = base + 48 * MIB, hole = base + 60 * MIB;
    const int radv = memfd_create("radv", 0);
    uint64_t v[25];
    void *start, *end;

    payload(1, 1);
    assert(radv >= 0 && !ftruncate(radv, PAGE));
    assert(!__wine_ps5_dmem_region((void *)base, 64 * MIB));
    assert(!__wine_ps5_dmem_file_region((void *)view, 16 * MIB));
    assert(mmap((void *)g, PAGE, RW, MAP_SHARED | MAP_FIXED, radv, 0) == (void *)g);
    assert(mmap((void *)in_view, PAGE, RW, MAP_SHARED | MAP_FIXED, radv, 0) == (void *)in_view);
    assert(mmap((void *)bare, PAGE, RW, MAP_SHARED | MAP_FIXED, radv, 0) == (void *)bare);

    /* The pager, before a fixed mapping into its guards: outside the regions and inside the adopted view. */
    errno = 0;
    assert(__wine_ps5_foreign_in((void *)outside, 4 * MIB) == -1 && errno == EEXIST);
    assert(!__wine_ps5_foreign_in((void *)(outside + 4 * MIB), 4 * MIB));
    errno = 0;
    assert(__wine_ps5_foreign_in((void *)view, 16 * MIB) == -1 && errno == EEXIST);
    assert(!__wine_ps5_foreign_in((void *)view, 8 * MIB));
    /* A reservation in a reserved area Wine kept: asked first, refused over G, and the rest goes ahead. */
    errno = 0;
    assert(__wine_ps5_dmem_check((void *)(bare - MIB), 2 * MIB) == -1 && errno == EEXIST);
    assert(!__wine_ps5_dmem_check((void *)(bare + MIB), 2 * MIB));
    assert(__wine_ps5_foreign_stats(v, 25) == 25 && v[2] == 3 && v[4] == 3);
    /* VirtualQuery between views: free up to G, G itself occupied, free after it. */
    owned_low = base;
    owned_high = base + 64 * MIB;
    assert(!__wine_ps5_vm_gap((void *)(bare - MIB), (void *)owned_high, owned_area, &start, &end));
    assert(start == (void *)(bare - MIB) && end == (void *)bare);
    assert(__wine_ps5_vm_gap((void *)bare, (void *)owned_high, owned_area, &start, &end) == 1);
    assert(start == (void *)bare && end == (void *)(bare + PAGE));

    /* A hole in a bare part: the commit refills it (one no-overwrite reservation) and then maps. */
    assert(!munmap((void *)hole, PAGE));
    reserve_count = 0;
    assert(__wine_ps5_mmap((void *)hole, PAGE, RW, FIXED_ANON, -1, 0) == (void *)hole && reserve_count == 1);
    assert(kernel_protection((void *)hole) == RW);
    /* The client's next mapping takes the hole between the query and the refill: found, never mapped over. */
    assert(!munmap((void *)(hole + 2 * PAGE), PAGE));
    memset((void *)g, 0x77, PAGE);
    intruder = radv;
    errno = 0;
    assert(__wine_ps5_mmap((void *)(hole + 2 * PAGE), PAGE, RW, FIXED_ANON, -1, 0) == MAP_FAILED && errno == EEXIST);
    assert(intruder < 0 && ((unsigned char *)(hole + 2 * PAGE))[0] == 0x77);
    /* The pager lays its guards as the engine does. */
    assert(!__wine_ps5_lay((void *)(base + 8 * MIB), PAGE) && kernel_protection((void *)(base + 8 * MIB)) == 0);
}

/* Laying is measured by the self-check: a kernel that answers success and keeps the mapping turns it off, and the engine
 * unmaps and reserves from then on. */
static void lay_measured(void)
{
    const uintptr_t base = region(64 << 20);
    unsigned char *page = (unsigned char *)base + PAGE;

    lay_ignored = 1;
    payload(1, 1);
    assert(!__wine_ps5_dmem_region((void *)base, 64 << 20) && kernel_protection((void *)base) == PROT_NONE);
    assert(__wine_ps5_mmap(page, PAGE, RW, FIXED_ANON, -1, 0) == page);
    unmap_count = 0;
    assert(__wine_ps5_mmap(page, PAGE, PROT_NONE, FIXED_ANON, -1, 0) == page);
    assert(unmap_count == 1 && unmaps[0].address == (uintptr_t)page && kernel_protection(page) == PROT_NONE);
    /* The pager's guards are not laid either: they would not read as bare reservations to the guard. */
    assert(__wine_ps5_lay(page, PAGE) == -1);
}

static void in_child(void (*test)(void), const char *name)
{
    int status = 0;
    const pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        test();
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "%s: FAILED (status %#x)\n", name, (unsigned)status);
        exit(1);
    }
}

int main(void)
{
    in_child(model_rules, "model rules");
    in_child(direct_payload, "direct payload");
    in_child(flexible_payload, "flexible payload");
    in_child(failures, "failures");
    in_child(caller_runs_kept, "caller runs kept");
    in_child(lost_writes, "lost writes");
    in_child(execute_not_kept, "execute not kept");
    in_child(no_scratch, "no scratch range");
    in_child(title, "title");
    in_child(gpu_kept, "gpu kept");
    in_child(rwx_not_kept, "rwx not kept");
    in_child(caller_not_jit, "caller not jit");
    in_child(table_full, "table full");
    in_child(stale_backing, "stale backing");
    in_child(foreign_inside_reservation, "foreign inside a reservation");
    in_child(guard_exports, "guard exports");
    in_child(lay_measured, "lay measured");
    puts("payload executable memory: direct and flexible memory never asked for execute, converted to JIT memory of "
         "exactly the range at the first execute through a temporary mapping in the helper's own range, contents "
         "and neighbours kept, direct memory released, then one kernel call per change; failures restored and "
         "counted, JIT pages kept and never made read-only again, no write lost, nothing left after decommit and "
         "free, the self-check refusing a kernel without JIT execute or RWX or a helper without a range, a file page "
         "in the range and a full run table refused, no alias after an adopt failed, the payload SDK never asked for execute, a title unchanged, host memory a "
         "program gave the GPU keeping its access through protection changes until the import ends or it is unmapped; a "
         "foreign mapping inside a reservation never mapped over or unmapped, a release kept or made around it, Wine "
         "offered it as a hole, passed");
    return 0;
}
