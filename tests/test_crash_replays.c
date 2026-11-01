/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Every console crash of a helper's memory code, replayed as a permanent host test against the shared fake kernel
 * (tests/fake_ps5_kernel.h), with the real code: the direct-memory engine and its console glue, the shared pages, the
 * heap, the PRX unwiring, the GPU client (.deps/graphics-foundation with graphics patches 0013 and 0014), and code
 * extracted from the applied trees (tests/extract_crash_replays.py): virtual.c's, and the GPU broker owner's
 * shared_object (server.cpp). Each case runs in a process of its own (the glue's state is set once) and ends asserting
 * the fake recorded no hazard (done). Each was checked to fail on the code that crashed, the guard it names taken out
 * of a scratch copy. Callers outside these files are held by source checks: the loader's wiring and ntdll's unwiring
 * (tests/test_module_wiring.py), win32u's order (tests/test_placed_unmap_order.py). Console addresses are host
 * addresses here; the fake owns the window they lie in. */
#define _GNU_SOURCE
#define __PROSPERO__ 1
#define _WIN64 1
#include "fake_ps5_kernel.h"
#include "../wine/ps5/pw_wine_alias.h"
#include "../wine/ps5/pw_wine_codeshare.h"
#include "../wine/ps5/pw_wine_mmu4k.h"
#include "../wine/ps5/pw_wine_heap.h"
#include "../wine/ps5/pw_wine_pages.h"
#include "../wine/ps5/pw_wine_prx.h"
#include "../.deps/graphics-foundation/tooling/gpu-broker/radv_ps5_platform.h"
#include "../.deps/graphics-foundation/tooling/gpu-broker/client.h"
#include "../.deps/graphics-foundation/tooling/gpu-broker/protocol.h"
#define PW_MEMORY_HELPER_PHYSICAL
#include "../src/pw_shared_page.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ps5platform/kernel.h>
#include <ps5platform/memory.h>

pid_t __real_fork(void);
void *__real_mmap(void *, size_t, int, int, int, off_t);

/* The console glue's exports (wine/ps5/pw_wine_dmem_ps5.c). */
int __wine_ps5_set_payload_memory(void *(*)(void *, size_t, int, int, int, off_t), int (*)(const void *, size_t, int));
int __wine_ps5_set_payload_direct_memory(int (*)(size_t, size_t, int, int64_t *));
int __wine_ps5_dmem_region(void *, size_t);
void *__wine_ps5_mmap(void *, size_t, int, int, int, off_t);
int __wine_ps5_munmap(void *, size_t);
int __wine_ps5_mprotect(void *, size_t, int);
int __wine_ps5_exec_convert(void *, size_t, int);
int __wine_ps5_exec_convert_image(void *, size_t, int);
unsigned int __wine_ps5_code_cache_stats(uint64_t *, unsigned int);
unsigned int __wine_ps5_code_share_stats(uint64_t *, unsigned int);
int __wine_ps5_host_allows(const void *, int);
void __wine_ps5_exec_refused(const void *);
int __wine_ps5_dmem_keep(void *, size_t);
int __wine_ps5_native_hole(void **, size_t *);
int __wine_ps5_dmem_file_region(void *, size_t);
int __wine_ps5_dmem_file_released(void *, size_t);
int __wine_ps5_dmem_check(void *, size_t);
int __wine_ps5_vm_gap(const void *, const void *, int (*)(const void *, void **, void **), void **, void **);
int __wine_ps5_lay(void *, size_t);
int __wine_ps5_foreign_in(void *, size_t);
int __wine_ps5_dmem_backing(uintptr_t, int64_t *, uintptr_t *, uintptr_t *);
int pw_wine_pages_reserve(void);
int kernel_mprotect(pid_t, intptr_t, size_t, int);
bool ps5_memory_query(struct ps5_memory_stats *stats) { (void)stats; return false; }

enum { PAGE = FK_PAGE, RW = PROT_READ | PROT_WRITE, RX = PROT_READ | PROT_EXEC,
       FIXED_ANON = MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS };
#define MIB ((size_t)1 << 20)
#define GIB ((size_t)1 << 30)

/* ---- Wine's side, as virtual.c has it, for the extracted code ---- */
typedef unsigned char BYTE; typedef int BOOL; typedef unsigned int UINT, ULONG, NTSTATUS; typedef uintptr_t UINT_PTR;
typedef uintptr_t ULONG_PTR; typedef size_t SIZE_T; typedef unsigned long long ULONG64; typedef void *HANDLE, *PVOID;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER 0xc000000d
#define STATUS_NO_MEMORY 0xc0000017
#define STATUS_CONFLICTING_ADDRESSES 0xc0000018
#define STATUS_ACCESS_DENIED 0xc0000022
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_REPLACE_PLACEHOLDER 0x4000
#define VPROT_READ 0x01
#define VPROT_WRITE 0x02
#define VPROT_EXEC 0x04
#define VPROT_WRITECOPY 0x08
#define VPROT_GUARD 0x10
#define VPROT_COMMITTED 0x20
#define VPROT_WRITEWATCH 0x40
#define VPROT_SYSTEM 0x0200
#define VPROT_PLACEHOLDER 0x0400
#define VPROT_FREE_PLACEHOLDER 0x0800
#define SEC_FILE 0x00800000
#define SEC_IMAGE 0x01000000
#define SEC_RESERVE 0x04000000
#define SEC_COMMIT 0x08000000
#define EXCEPTION_WRITE_FAULT 1
#define EXCEPTION_EXECUTE_FAULT 8
#define ROUND_ADDR(addr,mask) ((void *)((UINT_PTR)(addr) & ~(UINT_PTR)(mask)))
#define ROUND_SIZE(addr,size,mask) (((SIZE_T)(size) + ((UINT_PTR)(addr) & (mask)) + (mask)) & ~(UINT_PTR)(mask))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define TRACE(...) ((void)0)
#define TRACE_(channel) TRACE
#define WARN(...) ((void)0)
#define ERR(...) fprintf(stderr, __VA_ARGS__)
#define VIRTUAL_DEBUG_DUMP_RANGES() ((void)0)
#define mutex_lock(m) ((void)(m))
#define mutex_unlock(m) ((void)(m))
#define host_mmap __wine_ps5_mmap
#define host_munmap __wine_ps5_munmap
#define host_mprotect __wine_ps5_mprotect
#define DECLSPEC_EXPORT
/* Wine's uninterrupted section: the virtual mutex, and its signals (SIGUSR1, a suspend, among them) blocked. */
static void uninterrupted(int enter, sigset_t *saved)
{
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    assert(!pthread_sigmask(enter ? SIG_BLOCK : SIG_SETMASK, enter ? &block : saved, enter ? saved : NULL));
}
#define server_enter_uninterrupted_section(mutex, sigset) uninterrupted(1, sigset)
#define server_leave_uninterrupted_section(mutex, sigset) uninterrupted(0, sigset)

static const UINT page_shift = 12;
static const UINT_PTR page_mask = 0xfff;
static const SIZE_T page_size = 0x1000;
static const UINT_PTR host_page_size = 0x4000, host_page_mask = 0x3fff;
static const UINT_PTR granularity_mask = 0xffff;
static const size_t pages_vprot_shift = 20;
static const size_t pages_vprot_mask = (1 << 20) - 1;
static size_t pages_vprot_size;
static BYTE **pages_vprot;
static int use_kernel_writewatch, enable_write_exceptions, force_exec_prot;
static const size_t view_block_size = 0x100000;
static int virtual_mutex;
enum { VM_STAT_MMAP, VM_STAT_MUNMAP, VM_STAT_MPROTECT, VM_STAT_MPROTECT_BYTES, VM_STAT_MPROTECT_SKIPPED };
static void vm_stat_add(int index, ULONG64 value) { (void)index; (void)value; }
static int __wine_ps5_section_protect(uintptr_t base, size_t bytes, int prot) { (void)base; (void)bytes; (void)prot; return 0; }
static int __wine_ps5_section_arena_contains(uintptr_t base, size_t bytes) { (void)base; (void)bytes; return 0; }
static int is_vprot_exec_write(BYTE vprot) { (void)vprot; return 0; }
static size_t unmap_area_above_user_limit(void *addr, size_t size) { (void)addr; return size; }

/* Wine's page table: directories of 2^20 pages made as a view needs them. */
static void vprot_ready(const void *base, size_t size)
{
    if (!pages_vprot) {
        pages_vprot_size = ((size_t)1 << 47 >> page_shift) >> pages_vprot_shift;
        assert((pages_vprot = calloc(pages_vprot_size, sizeof(*pages_vprot))));
    }
    for (size_t idx = (uintptr_t)base >> page_shift; idx < ((uintptr_t)base + size + page_mask) >> page_shift;
         idx += pages_vprot_mask + 1 - (idx & pages_vprot_mask))
        if (!pages_vprot[idx >> pages_vprot_shift])
            assert((pages_vprot[idx >> pages_vprot_shift] = calloc(pages_vprot_mask + 1, 1)));
}

/* Views, sorted, linked as Wine's tree would answer rb_prev and rb_next. */
struct rb_entry { struct rb_entry *prev, *next; };
#define RB_ENTRY_VALUE(element, type, field) ((type *)((char *)(element) - offsetof(type, field)))
static struct rb_entry *rb_prev(struct rb_entry *entry) { return entry->prev; }
static struct rb_entry *rb_next(struct rb_entry *entry) { return entry->next; }
struct file_view { struct rb_entry entry; void *base; size_t size; unsigned int protect; };
static struct file_view views[64];
static unsigned view_count;
static void link_views(void)
{
    for (unsigned i = 1; i < view_count; i++)
        for (unsigned j = i; j && views[j - 1].base > views[j].base; j--) {
            const struct file_view swap = views[j];
            views[j] = views[j - 1];
            views[j - 1] = swap;
        }
    for (unsigned i = 0; i < view_count; i++) {
        views[i].entry.prev = i ? &views[i - 1].entry : NULL;
        views[i].entry.next = i + 1 < view_count ? &views[i + 1].entry : NULL;
    }
}
static struct file_view *find_view(const void *addr, size_t size)
{
    (void)size;
    for (unsigned i = 0; i < view_count; i++)
        if ((const char *)addr >= (char *)views[i].base && (const char *)addr < (char *)views[i].base + views[i].size)
            return &views[i];
    return NULL;
}
static struct file_view *find_view_range(const void *addr, size_t size)
{
    for (unsigned i = 0; i < view_count; i++)
        if ((const char *)addr < (char *)views[i].base + views[i].size && (char *)views[i].base < (const char *)addr + size)
            return &views[i];
    return NULL;
}
static NTSTATUS create_view(struct file_view **ret, void *base, size_t size, unsigned int protect)
{
    void *at = base;
    assert(view_count < ARRAY_SIZE(views));
    views[view_count++] = (struct file_view){ { NULL, NULL }, base, size, protect };
    link_views();
    vprot_ready(base, size);
    *ret = find_view(at, 0);
    return STATUS_SUCCESS;
}
static void unmap_area(void *start, size_t size);
static void drop_view(struct file_view *view)
{
    *view = views[--view_count];
    link_views();
}
static void delete_view(struct file_view *view)
{
    if (!(view->protect & VPROT_SYSTEM)) unmap_area(view->base, view->size);
    drop_view(view);
}
static NTSTATUS remove_pages_from_view(struct file_view *view, char *base, size_t size)
{
    assert(base == view->base && size < view->size);   /* the pool gives its front back */
    view->base = base + size;
    view->size -= size;
    link_views();
    return STATUS_SUCCESS;
}
static void kernel_writewatch_register_range(struct file_view *view, void *base, size_t size)
{
    (void)view; (void)base; (void)size;
}
static void reset_write_watches(void *base, size_t size) { (void)base; (void)size; }

/* Reserved areas: the real list's walk, kept sorted, each an engine region as mmap_add_reserved_area makes it. */
struct list { struct list *next, *prev; };
#define LIST_ENTRY(elem, type, field) ((type *)((char *)(elem) - offsetof(type, field)))
#define LIST_FOR_EACH_ENTRY(elem, list, type, field) \
    for ((elem) = LIST_ENTRY((list)->next, type, field); &(elem)->field != (list); \
         (elem) = LIST_ENTRY((elem)->field.next, type, field))
#define LIST_FOR_EACH_ENTRY_REV(elem, list, type, field) \
    for ((elem) = LIST_ENTRY((list)->prev, type, field); &(elem)->field != (list); \
         (elem) = LIST_ENTRY((elem)->field.prev, type, field))
struct reserved_area { struct list entry; void *base; size_t size; };
static struct list reserved_areas = { &reserved_areas, &reserved_areas };
static struct reserved_area area_storage[64];
static unsigned area_count;
static void mmap_add_reserved_area(void *addr, SIZE_T size)
{
    struct reserved_area *area = &area_storage[area_count++], *next;
    struct list *before = &reserved_areas;

    __wine_ps5_dmem_region(addr, size);
    LIST_FOR_EACH_ENTRY(next, &reserved_areas, struct reserved_area, entry)
        if (next->base > addr) { before = &next->entry; break; }
    area->base = addr;
    area->size = size;
    area->entry.next = before;
    area->entry.prev = before->prev;
    before->prev->next = &area->entry;
    before->prev = &area->entry;
}
static int in_reserved_area(const void *addr, size_t size)
{
    struct reserved_area *area;
    LIST_FOR_EACH_ENTRY(area, &reserved_areas, struct reserved_area, entry)
        if ((const char *)addr >= (char *)area->base && (const char *)addr + size <= (char *)area->base + area->size)
            return 1;
    return 0;
}
/* Wine's own owned-range query for VirtualQuery's walk (patch 0959): the reserved area holding addr, or the next. */
static int owned_area(const void *addr, void **start, void **end)
{
    struct reserved_area *area;
    LIST_FOR_EACH_ENTRY(area, &reserved_areas, struct reserved_area, entry)
        if ((const char *)addr < (char *)area->base + area->size) {
            *start = area->base;
            *end = (char *)area->base + area->size;
            return 0;
        }
    return 1;
}

static inline void *counted_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
static void *anon_mmap_fixed(void *start, size_t size, int prot, int flags)
{
    return counted_mmap(start, size, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | flags, -1, 0);
}
struct alloc_area { size_t size; ptrdiff_t step; int unix_prot; BOOL top_down; UINT_PTR align_mask; char *native_mapped;
                    size_t native_mapped_size; };
static void *try_map_free_area_range(struct alloc_area *area, char *base, char *end)
{
    (void)area; (void)base; (void)end;
    return NULL;
}
struct range_entry { void *base; void *end; };
static struct range_entry range_storage[64], *free_ranges = range_storage, *free_ranges_end = range_storage + 1;

struct ntdll_thread_data { struct { void *address; ULONG64 conversions; } ps5_granted; void *ps5_step_page[2]; };
static struct ntdll_thread_data thread_data;
static struct ntdll_thread_data *ntdll_get_thread_data(void) { return &thread_data; }

#define NtCurrentProcess() ((HANDLE)~(uintptr_t)0)
enum { APC_VIRTUAL_LOCK = 1, APC_VIRTUAL_UNLOCK };
union apc_call { struct { int type; uint64_t addr, size; } virtual_lock, virtual_unlock; };
union apc_result { struct { NTSTATUS status; uint64_t addr, size; } virtual_lock, virtual_unlock; };
static NTSTATUS server_queue_process_apc(HANDLE process, const union apc_call *call, union apc_result *result)
{
    (void)process; (void)call; (void)result;
    return STATUS_ACCESS_DENIED;
}
#define wine_server_client_ptr(p) ((uint64_t)(uintptr_t)(p))
#define wine_server_get_ptr(p) ((void *)(uintptr_t)(p))

/* Patch 2420's execute-fault note: the kernel's protection of a page (wine/ps5/pw_wine_dmem_ps5.c), none here. */
typedef int LONG;
static int __wine_ps5_kernel_prot(const void *address) { (void)address; return -1; }

/* Patch 1134's ops repair a null %fs first (docs/MMU4K.md P5); counted here. */
static unsigned fs_repairs;
static void signal_ps5_fs_repair(void) { fs_repairs++; }

#include "crash_replays.inc"
#undef mmap
#undef munmap
#undef mprotect

/* ---- The helper's process ---- */

/* The payload SDK's mmap and mprotect: asked for execute, they grant it with their unlocked kernel write. */
static void *sdk_mmap(void *address, size_t bytes, int protection, int flags, int fd, off_t offset)
{
    void *mapped = mmap(address, bytes, protection & ~PROT_EXEC, flags, fd, offset);
    if (mapped != MAP_FAILED && (protection & PROT_EXEC)) kernel_mprotect(getpid(), (intptr_t)mapped, bytes, protection);
    return mapped;
}
static int sdk_mprotect(const void *address, size_t bytes, int protection)
{
    if (protection & PROT_EXEC) return kernel_mprotect(getpid(), (intptr_t)address, bytes, protection);
    return mprotect((void *)address, bytes, protection);
}
static int sdk_allocate(size_t bytes, size_t alignment, int type, int64_t *offset)
{
    (void)alignment; (void)type;
    return (*offset = fk_dmem_allocate(bytes)) < 0 ? -1 : 0;
}

/* A helper as the loader leaves it before Wine runs (native/proton_wine_child.c): its shared-page range, the payload
 * SDK's memory functions and the application's direct memory. */
static void payload(int direct)
{
    fk_init();
    assert(!pw_wine_pages_reserve());
    assert(!__wine_ps5_set_payload_memory(sdk_mmap, sdk_mprotect));
    if (direct) assert(!__wine_ps5_set_payload_direct_memory(sdk_allocate));
}
/* anon_mmap_tryfixed: a reservation of Wine's at exactly at, never over anything, made a region of the engine. */
static uintptr_t wine_reserve(uintptr_t at, size_t bytes)
{
    void *p = (void *)at;
    assert(!sceKernelReserveVirtualRange(&p, bytes, 0x90, PAGE) && p == (void *)at);
    assert(!__wine_ps5_dmem_region(p, bytes));
    return at;
}
/* A view of committed pages: NtAllocateVirtualMemory's MEM_COMMIT through set_vprot. */
static struct file_view *committed_view(uintptr_t at, size_t bytes, BYTE vprot)
{
    struct file_view *view;
    assert(create_view(&view, (void *)at, bytes, VPROT_READ | VPROT_WRITE | VPROT_EXEC) == STATUS_SUCCESS);
    assert(set_vprot(view, (void *)at, bytes, VPROT_COMMITTED | vprot));
    return view;
}
static unsigned kernel_protection(uintptr_t at)
{
    FkEntry entry;
    assert(fk_entry(at, &entry));
    return entry.protection;
}
static int entry_kind(uintptr_t at)
{
    FkEntry entry;
    return fk_entry(at, &entry) ? entry.kind : 0;
}
static void done(void)
{
    if (fk_hazards()) fputs(fk_report(), stderr);
    assert(!fk_hazards());
}

/* ---- The replays ---- */

/* 89e40ab (five runs of the earlier emulator experiment, 2026-10-13 and 2026-10-14; records removed from the tree, in
 * git history): at GPU start-up the emulator freed most of a 28 GiB placeholder with MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER, which maps it
 * PROT_NONE, and the engine unmapped the bare span and reserved it again over a wired GPU mapping inside: "unwire
 * timeout at 0xc80000000". Guard: pw_wine_dmem_replace with protection 0 tears down runs only. */
static void placeholder_free(void)
{
    const uintptr_t base = 0x900000000, end = 0xfe0000000, gpu = 0xc80000000;

    payload(1);
    wine_reserve(base, end - base);
    assert(__wine_ps5_mmap((void *)base, 16 * PAGE, RW, FIXED_ANON, -1, 0) == (void *)base);
    fk_gpu_started();
    fk_plant(FK_DIRECT, gpu, 4 * PAGE, 0x33, "gpu-client", 0);
    fk_clear_calls();
    assert(__wine_ps5_mmap((void *)(base + 16 * PAGE), end - base - 16 * PAGE, PROT_NONE, FIXED_ANON, -1, 0) ==
           (void *)(base + 16 * PAGE));
    assert(!fk_calls(FK_OP_MUNMAP) && !fk_calls(FK_OP_RESERVE_FIXED) && !fk_calls(FK_OP_RESERVE_NOOVERWRITE));
    assert(kernel_protection(gpu) == 0x33);
    done();
}

/* late-library (v3-jit-gatecases-2026-10-14-late-library): the probe reserved every free interval, loaded vulkan-1.dll
 * and released everything; seven "[wine-ps5] unmap" lines, then "unwire timeout at 0xc80000000 of pid 1098". Guard:
 * patch 1128's release kept reserved (unmap_area, __wine_ps5_dmem_keep), the mapping made a view of its own. */
static void late_library_release(void)
{
    const uintptr_t span = 0x900000000, gpu = 0xc80000000;
    struct file_view *view;
    void *start, *end;

    payload(1);
    wine_reserve(span, 0x700000000);
    assert(create_view(&view, (void *)span, 0x700000000, VPROT_PLACEHOLDER | VPROT_FREE_PLACEHOLDER) == STATUS_SUCCESS);
    for (unsigned i = 0; i < 15; i++) {
        const uintptr_t at = 0x1800000000 + (uintptr_t)i * 0x80000000;
        wine_reserve(at, GIB);
        assert(create_view(&view, (void *)at, GIB, VPROT_PLACEHOLDER | VPROT_FREE_PLACEHOLDER) == STATUS_SUCCESS);
    }
    fk_gpu_started();
    /* RADV's wired memory, placed where the kernel chose: inside Wine's reservation. */
    fk_place_in_reservations(1);
    fk_set_floor(gpu);
    fk_as("gpu-client");
    void *placed = NULL;
    assert(!sceKernelMapDirectMemory(&placed, 4 * PAGE, 0x33, 0, fk_dmem_allocate(4 * PAGE), PAGE) && placed == (void *)gpu);
    fk_as("wine");
    fk_place_in_reservations(0);
    fk_clear_calls();
    while (view_count) delete_view(&views[view_count - 1]);
    assert(!fk_touched(FK_OP_MUNMAP, gpu, gpu + 4 * PAGE) && fk_largest(FK_OP_MUNMAP) < GIB);
    assert(in_reserved_area((void *)span, 0x700000000) && strstr(fk_klog(), "[wine-ps5] keep"));
    /* The mapping is a system view; VirtualQuery's walk reads it as occupied, not free. */
    ps5_claim_native_holes();
    assert(view_count == 1 && views[0].base == (void *)gpu && views[0].size == 4 * PAGE &&
           views[0].protect == VPROT_SYSTEM);
    assert(__wine_ps5_vm_gap((void *)gpu, (void *)(span + 0x700000000), owned_area, &start, &end) == 1 &&
           start == (void *)gpu && end == (void *)(gpu + 4 * PAGE));
    assert(kernel_protection(gpu) == 0x33);
    done();
}

/* Patch 1128: a commit, a protection change, a placeholder replace, a reservation in a kept reserved area, a file view,
 * the heap, the pager and a conversion outside the regions, each over a mapping that is not Wine's. Guards: the
 * engine's foreign check (pw_wine_dmem.c refused), set_vprot's undo, ps5_placeholder_refused, reserved_area_mmap's
 * question, refuse_foreign, heap_map's taken pages, __wine_ps5_foreign_in, and the GPU-bit check outside the regions
 * (refused_gpu, pw_wine_dmem_ps5.c). */
static const unsigned char code[] = { 0xb8, 42, 0, 0, 0, 0xc3 };   /* mov eax, 42; ret */
static void commit_over_foreign(void)
{
    const uintptr_t region = 0x1000000000, f = region + GIB, heap = 0x5000000000;
    const int file = memfd_create("section", 0);
    struct file_view *view, *placeholder, *got = NULL;
    uintptr_t outside;
    void *block;

    payload(1);
    wine_reserve(region, (size_t)16 << 30);
    fk_plant(FK_DIRECT, f, 4 * PAGE, 0x33, "gpu-client", 0);
    memset((void *)f, 0x5a, 4 * PAGE);
    assert(file >= 0 && !ftruncate(file, 4 * PAGE));

    /* MEM_COMMIT and VirtualProtect over F: refused, every page byte as before, pages committed earlier included. */
    assert(create_view(&view, (void *)(f - 4 * PAGE), 16 * PAGE, VPROT_READ | VPROT_WRITE) == STATUS_SUCCESS);
    assert(set_vprot(view, (void *)(f - 4 * PAGE), 2 * PAGE, VPROT_COMMITTED | VPROT_READ));
    errno = 0;
    assert(!set_vprot(view, (void *)(f - 4 * PAGE), 16 * PAGE, VPROT_COMMITTED | VPROT_READ | VPROT_WRITE) &&
           errno == EEXIST);
    for (unsigned i = 0; i < 8; i++) assert(get_page_vprot((void *)(f - 4 * PAGE + i * 4096)) == (VPROT_COMMITTED | VPROT_READ));
    for (unsigned i = 8; i < 64; i++) assert(!get_page_vprot((void *)(f - 4 * PAGE + i * 4096)));
    assert(kernel_protection(f - 4 * PAGE) == PROT_READ);
    assert(__wine_ps5_mprotect((void *)f, PAGE, RW) == -1 && errno == EEXIST);
    drop_view(view);

    /* MEM_REPLACE_PLACEHOLDER over F: STATUS_CONFLICTING_ADDRESSES, the placeholder free again. */
    assert(create_view(&placeholder, (void *)(f - 2 * MIB), 4 * MIB, VPROT_PLACEHOLDER | VPROT_FREE_PLACEHOLDER) ==
           STATUS_SUCCESS);
    assert(map_view_replace(&got, (void *)(f - 2 * MIB), 4 * MIB, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                            VPROT_COMMITTED | VPROT_READ | VPROT_WRITE) == STATUS_CONFLICTING_ADDRESSES && !got);
    assert(placeholder->protect == (VPROT_PLACEHOLDER | VPROT_FREE_PLACEHOLDER));
    drop_view(placeholder);

    /* A reservation in a reserved area Wine kept: asked first, refused. A private file view read in: ENODEV, and the
     * protection change that reads it in is refused. A shared one: refused. */
    errno = 0;
    assert(reserved_area_mmap((void *)(f - MIB), 2 * MIB, PROT_NONE) == MAP_FAILED && errno == EEXIST);
    errno = 0;
    assert(__wine_ps5_mmap((void *)f, PAGE, PROT_READ, MAP_PRIVATE | MAP_FIXED, file, 0) == MAP_FAILED && errno == ENODEV);
    assert(__wine_ps5_mprotect((void *)f, PAGE, RW) == -1 && errno == EEXIST);
    errno = 0;
    assert(__wine_ps5_mmap((void *)f, 4 * PAGE, RW, MAP_SHARED | MAP_FIXED, file, 0) == MAP_FAILED && errno == EEXIST);

    /* The pager: a view over F is not adopted, and its fault is refused. */
    assert(__wine_ps5_dmem_file_region((void *)(f - MIB), 2 * MIB) == -1);
    errno = 0;
    assert(__wine_ps5_foreign_in((void *)(f - MIB), 2 * MIB) == -1 && errno == EEXIST);

    /* The heap: F2 at the first pages its area offers stays taken, and the span comes from past it. */
    fk_plant(FK_DIRECT, heap, 4 * PAGE, 0x33, "gpu-client", 0);
    assert((block = pw_wine_heap_malloc(100)) && ((uintptr_t)block < heap || (uintptr_t)block >= heap + 4 * PAGE));
    assert(kernel_protection(heap) == 0x33);

    /* A conversion outside the regions with F3 inside: refused before any change. */
    outside = (uintptr_t)__wine_ps5_mmap(NULL, 4 * PAGE, RW, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert((void *)outside != MAP_FAILED);
    memcpy((void *)outside, code, sizeof(code));
    fk_plant(FK_DIRECT, outside + 2 * PAGE, PAGE, 0x33, "gpu-client", 0);
    errno = 0;
    assert(__wine_ps5_exec_convert((void *)outside, 4 * PAGE, RX) == -1 && errno == EEXIST);
    assert(entry_kind(outside) == FK_ANON && !memcmp((void *)outside, code, sizeof(code)));
    /* A fixed mapping and an unmap outside the regions go around it too. */
    assert(__wine_ps5_mmap((void *)outside, 4 * PAGE, PROT_NONE, FIXED_ANON, -1, 0) == MAP_FAILED && errno == EEXIST);
    assert(__wine_ps5_munmap((void *)outside, 4 * PAGE) == -1);
    assert(kernel_protection(outside + 2 * PAGE) == 0x33 && kernel_protection(f) == 0x33);
    for (unsigned i = 0; i < 4 * PAGE; i++) assert(((unsigned char *)f)[i] == 0x5a);
    done();
}

/* The window page that vanished (2026-10-14, two runs; records removed from the tree): a physical shared mapping answered success
 * with nothing behind it, and its first write faulted inside a system call with Wine's USER lock held. Guard: the
 * mincore check (pw_wine_pages_ps5.c map), laid back. */
static void window_page_vanished(void)
{
    int fd;
    void *page, *again;

    payload(1);
    assert((fd = pw_shared_page_create(PAGE)) >= 0);
    fk_phantom_physhm(1);
    errno = 0;
    assert(pw_wine_pages_map(PAGE, RW, fd) == MAP_FAILED && errno == ENOMEM);
    fk_phantom_physhm(0);
    assert((page = pw_wine_pages_map(PAGE, RW, fd)) != MAP_FAILED);
    ((volatile char *)page)[0] = 1;
    pw_wine_pages_unmap(page, PAGE);
    assert((again = pw_wine_pages_map(PAGE, RW, fd)) == page);
    pw_wine_pages_unmap(again, PAGE);
    close(fd);
    done();
}
/* A page whose reservation could not be laid back is lost: something may take the hole, and no shared page is mapped
 * there again. Guard: pw_wine_pages.c keeps a page taken when lay_back fails. */
static uintptr_t lost_page;
static void take_lost_page(void) { assert(fk_place(FK_DIRECT, lost_page, PAGE, 0x33, "gpu-client") == lost_page); }
static void window_page_lost(void)
{
    int fd;
    void *pages[8];

    payload(1);
    assert((fd = pw_shared_page_create(PAGE)) >= 0);
    assert((pages[0] = pw_wine_pages_map(PAGE, RW, fd)) != MAP_FAILED);
    lost_page = (uintptr_t)pages[0];
    /* The lay-back refused: the page is unmapped, and a placement takes the hole. */
    fk_fail_next(FK_OP_RESERVE_FIXED);
    pw_wine_pages_unmap(pages[0], PAGE);
    take_lost_page();
    for (unsigned i = 1; i < 8; i++) {
        assert((pages[i] = pw_wine_pages_map(PAGE, RW, fd)) != MAP_FAILED && (uintptr_t)pages[i] != lost_page);
    }
    assert(kernel_protection(lost_page) == 0x33);
    close(fd);
    done();
}
/* A helper whose shared-page range could not be reserved maps no shared page where the kernel would put it (it came
 * back outside the band with nothing behind it). Guard: pw_wine_pages_no_range. */
static void window_page_no_range(void)
{
    static const uintptr_t hints[] = { 0xfbf8000000u, 0x7ff0000000u, 0x6ff0000000u };
    int fd;

    fk_init();
    for (unsigned i = 0; i < 3; i++) fk_plant(FK_ANON, hints[i] + 64 * MIB, PAGE, 3, "loader", 0);
    assert(pw_wine_pages_reserve() == -1);
    assert((fd = pw_shared_page_create(PAGE)) >= 0);
    fk_clear_calls();
    errno = 0;
    assert(pw_wine_pages_map(PAGE, RW, fd) == MAP_FAILED && errno == ENOSPC && !fk_calls(FK_OP_MMAP));
    close(fd);
    done();
}

/* Host-protection leaves inside guest memory (2026-10-13 and -14 emulator runs, records removed from the tree; a Unity game's 0x8080808080808080):
 * the engine backed and released whole views around a section view, leaving holes, and allocations made without an
 * address landed in them. Guards: __wine_ps5_dmem_file_region's replace before adopt (no hole), the heap never mapping
 * by itself once backed (pw_wine_heap.c), host-protection leaves from the heap (patch 1124). */
static void host_protection_leaf(void)
{
    const uintptr_t region = 0x900000000, view = region + 64 * MIB;
    unsigned mmaps;
    void *placed;

    payload(1);
    wine_reserve(region, GIB);
    assert(__wine_ps5_mmap((void *)view, 64 * MIB, RW, FIXED_ANON, -1, 0) == (void *)view);
    assert(!__wine_ps5_dmem_file_region((void *)view, 64 * MIB));
    assert(!fk_free_bytes(region, region + GIB));
    fk_set_floor(region);
    placed = mmap(NULL, 4 * PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(placed != MAP_FAILED && ((uintptr_t)placed >= region + GIB || (uintptr_t)placed + 4 * PAGE <= region));
    /* The heap's area has no room for the block (a mapping not Wine's halves it): no mapping of the heap's own. */
    fk_plant(FK_DIRECT, 0x5000000000 + ((size_t)3 << 30), PAGE, 0x33, "gpu-client", 0);
    mmaps = fk_calls(FK_OP_MMAP);
    assert(!pw_wine_heap_malloc((size_t)7 << 29) && fk_calls(FK_OP_MMAP) == mmaps);
    host_prot_record((void *)view, 64 * MIB, PROT_READ | HOST_PROT_KNOWN);
    assert(fk_calls(FK_OP_MMAP) == mmaps && host_prot_unchanged((void *)view, 64 * MIB, PROT_READ));
    done();
}

/* The heap holds the direct-memory engine's spin lock across its kernel calls (heap_map, heap_unmap; engine-mutex
 * audit hazard 1, a 2026-10-15 run): a thread suspended (SIGUSR1) or ended (SIGQUIT, SIGINT) inside one would
 * leave every fault that reaches the engine spinning under the virtual mutex. Guard: heap_mmap blocks all three, as
 * pages_enter does, and gives the mask back. */
static unsigned heap_calls_blocked;
static int blocked(int signal_number)
{
    sigset_t now;
    assert(!pthread_sigmask(SIG_BLOCK, NULL, &now));
    return sigismember(&now, signal_number);
}
static void heap_call_seen(void)
{
    heap_calls_blocked += blocked(SIGUSR1) && blocked(SIGQUIT) && blocked(SIGINT);
}
static void heap_signals_blocked(void)
{
    void *block;

    payload(1);
    wine_reserve(0x900000000, GIB);   /* the first region brings up the heap's area (0x5000000000) */
    fk_on_next(FK_OP_MAP_DIRECT, heap_call_seen);
    assert((block = pw_wine_heap_malloc(4 * MIB)) && (uintptr_t)block >= 0x5000000000);   /* a large block: heap_map */
    assert(heap_calls_blocked == 1 && !blocked(SIGUSR1) && !blocked(SIGQUIT) && !blocked(SIGINT));
    fk_on_next(FK_OP_RESERVE_FIXED, heap_call_seen);   /* heap_unmap lays the reservation back, then releases */
    pw_wine_heap_free(block);
    assert(heap_calls_blocked == 2 && !blocked(SIGUSR1) && !blocked(SIGQUIT) && !blocked(SIGINT));
    done();
}

/* The payload SDK's grant of execute (2026-10-14 freeze review, cause 2; exec-grant-measure-2026-10-14-01): it writes the
 * kernel's map entry without the map's lock. And a refused CPU-only map turned every later run into wired GPU memory.
 * Guards: no execute ever reaches the SDK (payload_map, payload_protect, the engine's exec_caller_only), execute from
 * JIT memory (__wine_ps5_exec_convert), no GPU bits in a payload (pw_wine_dmem_ps5.c map). */
#ifdef __clang__
__attribute__((no_sanitize("function")))
#endif
static int run(const void *at) { return ((int (*)(void))(uintptr_t)at)(); }
static void sdk_execute_grant(void)
{
    const uintptr_t region = 0x900000000, page = region + 4 * PAGE, other = region + 64 * PAGE;
    FkEntry entry;

    payload(1);
    wine_reserve(region, 64 * MIB);
    assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == (void *)page);
    memcpy((void *)page, code, sizeof(code));
    assert(!__wine_ps5_mprotect((void *)page, PAGE, RX) && kernel_protection(page) == PROT_READ);
    assert(!__wine_ps5_exec_convert((void *)page, PAGE, RX) && entry_kind(page) == FK_JIT &&
           kernel_protection(page) == RX && run((void *)page) == 42);
    /* An executable mapping outside the regions: mapped without execute, converted at its first execute. */
    void *outside = __wine_ps5_mmap(NULL, PAGE, RW | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(outside != MAP_FAILED && kernel_protection((uintptr_t)outside) == RW);
    memcpy(outside, code, sizeof(code));
    assert(!__wine_ps5_exec_convert(outside, PAGE, RW | PROT_EXEC) && run(outside) == 42);
    /* The CPU-only map refused once: the commit fails, nothing GPU-wired is made, the next commit maps 0x03. */
    fk_refuse_cpu_direct_once();
    assert(__wine_ps5_mmap((void *)other, PAGE, RW, FIXED_ANON, -1, 0) == MAP_FAILED);
    assert(__wine_ps5_mmap((void *)other, PAGE, RW, FIXED_ANON, -1, 0) == (void *)other && kernel_protection(other) == RW);
    for (uintptr_t at = FK_LOW; fk_next(at, &entry); at = entry.end) {
        assert(!(entry.protection & 0x30) || strcmp(entry.maker, "wine"));
        assert(!(entry.protection & PROT_EXEC) || entry.kind == FK_JIT);
    }
    done();
}

/* 4 KiB write tracking (patch 1122; a 2026-10-14 run, record removed from the tree) and the post-review fix: a host page whose
 * committed pages disagree on write is mapped read-only; a page opened for a step and then converted to JIT memory
 * stayed cached as open, so reopening it never reached the kernel and its store faulted forever. Guards:
 * get_host_page_unix_prot's strictest rule, ps5_fault_served's host_prot_record after a conversion. */
static void write_tracking(void)
{
    const uintptr_t region = 0x900000000, host = region + 16 * PAGE;
    struct file_view *view;
    unsigned char cached;

    payload(1);
    wine_reserve(region, 64 * MIB);
    assert(create_view(&view, (void *)region, 64 * MIB, VPROT_READ | VPROT_WRITE | VPROT_EXEC) == STATUS_SUCCESS);
    for (unsigned i = 0; i < 4; i++)
        assert(set_vprot(view, (void *)(host + i * 4096), 4096, VPROT_COMMITTED | (i % 2 ? VPROT_READ : VPROT_READ | VPROT_WRITE)));
    assert(kernel_protection(host) == PROT_READ);
    /* The write fault opens the host page for one instruction; the trap closes it: the cache follows the kernel. */
    thread_data.ps5_step_page[0] = (void *)host;
    assert(!mprotect_exec((void *)host, host_page_size, get_unix_prot(get_host_page_vprot((void *)host))));
    assert(kernel_protection(host) == RW);
    assert(virtual_ps5_step_done() && kernel_protection(host) == PROT_READ);
    cached = *host_prot_entry(host / host_prot_page_size(), 0);
    assert(cached == (PROT_READ | HOST_PROT_KNOWN));
    /* Opened again, then its code is converted (another thread's execute fault): the cache says what the kernel holds,
     * so the next opening reaches the kernel. */
    assert(!mprotect_exec((void *)host, host_page_size, RW) && kernel_protection(host) == RW);
    memcpy((void *)host, code, sizeof(code));
    set_page_vprot((void *)host, host_page_size, VPROT_COMMITTED | VPROT_READ | VPROT_EXEC);
    assert(ps5_fault_served((void *)host, (char *)host, EXCEPTION_EXECUTE_FAULT));
    assert(entry_kind(host) == FK_JIT && kernel_protection(host) == RX && run((void *)host) == 42);
    assert(*host_prot_entry(host / host_prot_page_size(), 0) == (RX | HOST_PROT_KNOWN));
    assert(!mprotect_exec((void *)host, host_page_size, RW) && kernel_protection(host) == RW);
    done();
}

/* A mapping made without an address inside a demand-paged view (2026-10-14; record removed from the tree): adopting the
 * view released its direct memory, which unmapped its pages. Guard: __wine_ps5_dmem_file_region's replace with
 * protection 0 before the adopt. */
static void noaddress_in_view(void)
{
    const uintptr_t region = 0x900000000, view = region + 16 * MIB;
    unsigned char info[0x160];
    uintptr_t text;
    void *placed;

    payload(1);
    wine_reserve(region, GIB);
    assert(__wine_ps5_mmap((void *)view, 64 * PAGE, RW, FIXED_ANON, -1, 0) == (void *)view);
    assert(!__wine_ps5_dmem_file_region((void *)view, 64 * PAGE));
    assert(!fk_free_bytes(view, view + 64 * PAGE) && entry_kind(view) == FK_RESERVED);
    fk_set_floor(region);
    text = fk_load_module(4 * PAGE, PAGE, 2 * PAGE, info);
    placed = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(text >= region + GIB && (uintptr_t)placed >= region + GIB);
    done();
}

/* An intruder between the guard's query and its fixed mapping (the GPU client's mapping placed into a hole a free of
 * its left in Wine's reservation). Guard: the hole is reserved again before it is trusted (pw_wine_vm_foreign's fill),
 * and a mapping that took it first is found and never mapped over. */
static uintptr_t hole;
static void intrude(void) { fk_place(FK_DIRECT, hole, PAGE, 0x33, "gpu-client"); }
static void make_hole(uintptr_t at)
{
    fk_plant(FK_DIRECT, at, PAGE, 0x33, "gpu-client", 0);
    fk_as("gpu-client");
    assert(!sceKernelMunmap((void *)at, PAGE));
    fk_as("wine");
}
static void intruder_refill(void)
{
    const uintptr_t region = 0x900000000;
    void *found;
    size_t bytes;

    payload(1);
    wine_reserve(region, 64 * MIB);
    fk_set_floor(region);
    /* The hole is refilled before the commit maps: an intruder at the fixed mapping lands elsewhere. */
    hole = region + 8 * PAGE;
    make_hole(hole);
    fk_on_next(FK_OP_MAP_DIRECT, intrude);
    assert(__wine_ps5_mmap((void *)hole, PAGE, RW, FIXED_ANON, -1, 0) == (void *)hole);
    assert(kernel_protection(hole) == RW);
    /* The intruder takes the hole between the query and the refill: found, refused, offered to Wine. */
    hole = region + 32 * PAGE;
    make_hole(hole);
    fk_on_next(FK_OP_RESERVE_NOOVERWRITE, intrude);
    errno = 0;
    assert(__wine_ps5_mmap((void *)hole, PAGE, RW, FIXED_ANON, -1, 0) == MAP_FAILED && errno == EEXIST);
    assert(kernel_protection(hole) == 0x33);
    assert(__wine_ps5_native_hole(&found, &bytes) == 1 && found == (void *)hole && bytes == PAGE);
    done();
}

/* A laid reservation that does not replace (check_laid turns laying off). The engine and the pager then unmap and
 * reserve, and a placement can take the page between the two calls: it is never mapped over and Wine is offered it.
 * Guards: check_laid, the engine's refused commit over what took the hole. (An anonymous PROT_NONE mapping would replace
 * in one call but is charged to flexible memory, 2026-10-06: not used.) */
static void laid_not_replacing(void)
{
    const uintptr_t region = 0x900000000, page = region + 8 * PAGE;
    void *found;
    size_t bytes;

    fk_init();
    fk_lay_ignored(1);
    assert(!pw_wine_pages_reserve());
    assert(!__wine_ps5_set_payload_memory(sdk_mmap, sdk_mprotect));
    assert(!__wine_ps5_set_payload_direct_memory(sdk_allocate));
    wine_reserve(region, 64 * MIB);
    assert(__wine_ps5_lay((void *)page, PAGE) == -1);   /* the pager is told too */
    assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == (void *)page);
    hole = page;
    fk_set_floor(region);
    fk_on_next(FK_OP_RESERVE_NOOVERWRITE, intrude);
    assert(__wine_ps5_mmap((void *)page, PAGE, PROT_NONE, FIXED_ANON, -1, 0) == (void *)page);
    assert(kernel_protection(page) == 0x33);
    errno = 0;
    assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == MAP_FAILED && errno == EEXIST);
    assert(__wine_ps5_native_hole(&found, &bytes) == 1 && found == (void *)page);
    done();
}

/* The GPU broker's owner, for the client: ALLOC answers the next address and direct memory the owner allocated and
 * granted, or with owner_physhm its shared memory (a descriptor, argument[2] 0: the memory helper refused direct
 * memory); FREE frees it, and runs owner_after_free once its answer is sent. The reply is made readable at once. */
static int owner_fd = -1, owner_physhm, reply_fd = -1;
static uint64_t owner_address, owner_frees;
static int64_t owner_dmem;
static void (*owner_after_free)(void);
static PwGpuMessage reply;
int ps5_gpu_packet_send(int fd, const void *data, size_t size, const int *fds, unsigned count)
{
    (void)fds; (void)count;
    assert(size == sizeof(reply));
    memcpy(&reply, data, sizeof(reply));
    reply.status = 0;
    if (reply.operation == PW_GPU_INIT) memset(reply.argument, 0, sizeof(reply.argument));
    if (reply.operation == PW_GPU_ALLOC) {
        reply.bytes = (reply.bytes + PAGE - 1) & ~(uint64_t)(PAGE - 1);
        reply.address = owner_address;
        reply.argument[0] = 1;
        if (owner_physhm) {
            assert((reply_fd = memfd_create("pw-physhm", 0)) >= 0 && !ftruncate(reply_fd, (off_t)reply.bytes));
            reply.argument[2] = 0;
        } else {
            assert((owner_dmem = fk_dmem_allocate(reply.bytes)) >= 0);
            fk_grant(owner_dmem, reply.bytes);
            reply.argument[2] = (uint64_t)owner_dmem + 1;
        }
    }
    if (reply.operation == PW_GPU_FREE) {
        owner_frees++;
        if (owner_after_free) owner_after_free();
    }
    (void)fd;
    return send(owner_fd, "r", 1, 0) == 1 ? 0 : -1;
}
ssize_t ps5_gpu_packet_recv(int fd, void *data, size_t size, int fds[2], unsigned *count)
{
    char byte;
    assert(recv(fd, &byte, 1, 0) == 1 && size == sizeof(reply));
    memcpy(data, &reply, sizeof(reply));
    *count = reply_fd >= 0;
    fds[0] = reply_fd;
    reply_fd = -1;
    return (ssize_t)size;
}
static void client(int reserve)
{
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair));
    owner_fd = pair[1];
    fk_as("gpu-client");
    if (reserve) assert(pw_gpu_reserve_host_ranges(sceKernelReserveVirtualRange, sceKernelMunmap));
    assert(!ps5_gpu_client_bind(pair[0], reserve));
    fk_as("wine");
}

/* What the code under test printed to stderr between capture and captured. */
static int capture_fd = -1, saved_stderr = -1;
static void capture(void)
{
    assert((saved_stderr = dup(2)) >= 0 && (capture_fd = memfd_create("stderr", 0)) >= 0 && dup2(capture_fd, 2) == 2);
}
static const char *captured(void)
{
    static char text[4096];
    const ssize_t got = pread(capture_fd, text, sizeof(text) - 1, 0);
    assert(dup2(saved_stderr, 2) == 2 && !close(saved_stderr) && !close(capture_fd));
    text[got > 0 ? got : 0] = 0;
    return text;
}

/* A placed GPU mapping (patch 0951, WoW64): win32u commits a range and the GPU client maps its wired memory over it.
 * Guards: the client refuses Wine's committed run there, and says so (graphics 0013); win32u gives the range back to a
 * bare reservation the engine forgets first (patch 1130), in ntdll under the virtual mutex with Wine's signals blocked,
 * since a thread suspended while it holds the engine's lock stops every other (__wine_ps5_placed_range_bare); the
 * client maps only over a bare reservation (0013), and the engine then never protects, lays or unmaps the mapping,
 * whatever order the program frees it in (its foreign check, patch 1128's kept release). */
static int suspend_blocked;
static void lay_under_section(void)
{
    sigset_t now;
    assert(!pthread_sigmask(SIG_BLOCK, NULL, &now));
    suspend_blocked = sigismember(&now, SIGUSR1) == 1;
}
static void placed_gpu_map(void)
{
    const uintptr_t region = 0x900000000, placed = region + 16 * MIB;
    struct radv_ps5_memory memory;
    struct file_view *view;
    void *found;
    size_t bytes;
    bool mapped;
    const char *printed;

    payload(1);
    wine_reserve(region, 256 * MIB);
    client(1);
    ps5_gpu_client_keep_sources();
    owner_address = PW_GPU_HOST_HIGH_BASE;
    fk_as("gpu-client");
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && memory.cpu == (void *)PW_GPU_HOST_HIGH_BASE);
    memset(memory.cpu, 0x77, 4 * PAGE);
    fk_as("wine");
    fk_gpu_started();
    /* win32u_vkMapMemory2KHR: MEM_COMMIT of the memory's size (4 KiB pages: here 60 KiB of the client's 64), then the
     * range made bare and forgotten by the host page (patch 1130), then the placed map. */
    view = committed_view(placed, 4 * PAGE, VPROT_READ | VPROT_WRITE);
    /* Without the hand-over the client maps nothing over Wine's run, and prints the entry it found. */
    fk_as("gpu-client");
    capture();
    mapped = radv_ps5_memory_map_at(&memory, (void *)placed);
    printed = captured();
    assert(!mapped && strstr(printed, "not bare at") && kernel_protection(placed) == RW && !fk_hazards());
    fk_as("wine");
    fk_on_next(FK_OP_RESERVE_FIXED, lay_under_section);
    assert(!__wine_ps5_placed_range_bare((void *)placed, 4 * PAGE - 4096) && suspend_blocked);
    fk_as("gpu-client");
    assert(radv_ps5_memory_map_at(&memory, (void *)placed) && ((unsigned char *)placed)[100] == 0x77);
    fk_as("wine");
    /* VirtualProtect: refused before it reaches the mapping. A release out of order: kept, the mapping a view. */
    errno = 0;
    assert(!set_vprot(view, (void *)placed, 4 * PAGE, VPROT_COMMITTED | VPROT_READ) && errno == EEXIST);
    delete_view(view);
    assert(kernel_protection(placed) == 0x33);
    assert(__wine_ps5_native_hole(&found, &bytes) == 1 && found == (void *)placed);
    /* The client's own unmap with VK_MEMORY_UNMAP_RESERVE_BIT_EXT lays the reservation back; then its free. */
    fk_as("gpu-client");
    radv_ps5_memory_unmap_at((void *)placed, 4 * PAGE, true);
    radv_ps5_memory_free(&memory);
    fk_as("wine");
    assert(entry_kind(placed) == FK_RESERVED && entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_RESERVED);
    done();
}

/* The GPU client's host ranges (graphics patch 0013): it maps a buffer only over its own bare reservation of a host
 * range. Without the ranges, or at an address outside them, no kernel call; over a mapping in the range, refused by the
 * query; a free lays the reservation back; a range whose lay-back failed is never mapped over. */
static void gpu_client_unreserved(void)
{
    struct radv_ps5_memory memory;

    payload(1);
    client(0);
    owner_address = PW_GPU_HOST_HIGH_BASE;
    fk_clear_calls();
    fk_as("gpu-client");
    assert(!radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && owner_frees == 1);
    assert(!fk_calls(FK_OP_RESERVE) && !fk_calls(FK_OP_MAP_DIRECT) && !fk_calls(FK_OP_MUNMAP) && !fk_calls(FK_OP_MMAP));
    done();
}
static void gpu_client_ranges(void)
{
    const uintptr_t at = PW_GPU_HOST_HIGH_BASE + 64 * PAGE;
    struct radv_ps5_memory memory;
    bool allocated;
    const char *printed;

    payload(1);
    client(1);
    fk_as("gpu-client");
    owner_address = 0x5100000000;   /* the owner's cursor, outside both ranges */
    fk_clear_calls();
    assert(!radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && owner_frees == 1);
    assert(!fk_calls(FK_OP_RESERVE) && !fk_calls(FK_OP_MAP_DIRECT) && !fk_calls(FK_OP_MUNMAP));
    /* A mapping inside the range (Wine's, say): refused by the query, never mapped over, and the refusal printed. */
    fk_plant(FK_ANON, at, PAGE, 3, "wine", 0);
    owner_address = at - PAGE;
    capture();
    allocated = radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory);
    printed = captured();
    assert(!allocated && strstr(printed, "not bare at") && owner_frees == 2 && entry_kind(at) == FK_ANON);
    /* A buffer, freed: laid back, no unmap. */
    owner_address = PW_GPU_HOST_HIGH_BASE;
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && kernel_protection(PW_GPU_HOST_HIGH_BASE) == 0x33);
    fk_clear_calls();
    radv_ps5_memory_free(&memory);
    assert(!fk_calls(FK_OP_MUNMAP) && entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_RESERVED);
    /* The lay-back refused: the client unmaps its own mapping, a placement takes the hole, and the owner's next buffer
     * there is refused. */
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory));
    fk_fail_next(FK_OP_RESERVE_FIXED);
    radv_ps5_memory_free(&memory);
    fk_set_floor(PW_GPU_HOST_HIGH_BASE);
    assert(fk_place(FK_ANON, PW_GPU_HOST_HIGH_BASE, PAGE, 3, "wine") == PW_GPU_HOST_HIGH_BASE);
    assert(!radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_ANON);
    done();
}

/* A client in the owner's own process (a program run inside the title, graphics patch 0031): the owner has the buffer
 * mapped at the address it answers, so the client maps nothing and reserves nothing there, and its free leaves the
 * owner's mapping alone; FREE still reaches the owner. */
static void gpu_client_in_owner(void)
{
    struct radv_ps5_memory memory;

    payload(1);
    ps5_gpu_client_in_owner();
    client(0);
    owner_address = PW_GPU_HOST_HIGH_BASE;
    fk_plant(FK_DIRECT, PW_GPU_HOST_HIGH_BASE, 4 * PAGE, 0x33, "gpu-owner", 0);
    fk_clear_calls();
    fk_as("gpu-client");
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && memory.cpu == (void *)PW_GPU_HOST_HIGH_BASE);
    assert(!fk_calls(FK_OP_RESERVE) && !fk_calls(FK_OP_RESERVE_FIXED) && !fk_calls(FK_OP_MAP_DIRECT) &&
           !fk_calls(FK_OP_MMAP) && !fk_calls(FK_OP_MUNMAP));
    radv_ps5_memory_free(&memory);
    assert(owner_frees == 1 && !fk_calls(FK_OP_RESERVE_FIXED) && !fk_calls(FK_OP_MUNMAP));
    assert(entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_DIRECT && kernel_protection(PW_GPU_HOST_HIGH_BASE) == 0x33);
    fk_as("wine");
    done();
}

/* A buffer of the owner's shared memory freed while another thread's ALLOC gets its address (graphics patch 0014):
 * the owner gives the range back as soon as it answers FREE, the other thread's query read the old shared mapping as
 * the bare reservation under it and mapped its wired buffer there, and the freeing thread then laid its reservation over
 * that live buffer. Guard: radv_ps5_memory_free gives the range back before FREE. */
static struct radv_ps5_memory other_buffer;
static void other_thread_alloc(void)
{
    if (other_buffer.cpu) return;
    owner_physhm = 0;
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &other_buffer) && other_buffer.cpu == (void *)PW_GPU_HOST_HIGH_BASE);
}
static void other_thread_next(void)
{
    fk_on_next(FK_OP_RESERVE_FIXED, other_thread_alloc);
    fk_on_next(FK_OP_MUNMAP, other_thread_alloc);
}
static void gpu_client_free_order(void)
{
    struct radv_ps5_memory memory;

    payload(1);
    client(1);
    ps5_gpu_client_keep_sources();
    owner_address = PW_GPU_HOST_HIGH_BASE;
    owner_physhm = 1;
    fk_as("gpu-client");
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_PHYSHM);
    owner_after_free = other_thread_next;
    radv_ps5_memory_free(&memory);
    owner_after_free = NULL;
    other_thread_alloc();   /* when the free made no kernel call after FREE */
    fk_on_next(FK_OP_RESERVE_FIXED, NULL);
    fk_on_next(FK_OP_MUNMAP, NULL);
    assert(entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_DIRECT && kernel_protection(PW_GPU_HOST_HIGH_BASE) == 0x33);
    fk_as("wine");
    done();
}

/* A placed map of the owner's shared memory (Wine patch 1130, graphics patches 0014 and 0016): over the bare range
 * win32u hands over it reads as that reservation, so the engine took the range for its own and a VirtualProtect there
 * mapped fresh direct memory over the GPU-visible pages; 0014 then refused to place it, and a 32-bit program's maps
 * failed once direct memory ran out (host-dmem-fallback64, v2-backend-gate-2026-10-15). Guard: the range is lent,
 * foreign by record, until Wine releases it. */
static void placed_shared_memory(void)
{
    const uintptr_t region = 0x900000000, placed = region + 16 * MIB;
    struct radv_ps5_memory memory;
    struct file_view *view;
    void *found;
    size_t bytes;

    payload(1);
    wine_reserve(region, 256 * MIB);
    client(1);
    ps5_gpu_client_keep_sources();
    owner_address = PW_GPU_HOST_HIGH_BASE;
    owner_physhm = 1;
    fk_as("gpu-client");
    assert(radv_ps5_memory_alloc(4 * PAGE, 0, false, &memory) && entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_PHYSHM);
    memset(memory.cpu, 0x55, 4 * PAGE);
    fk_as("wine");
    view = committed_view(placed, 4 * PAGE, VPROT_READ | VPROT_WRITE);
    assert(!__wine_ps5_placed_range_bare((void *)placed, 4 * PAGE - 4096));
    fk_as("gpu-client");
    assert(radv_ps5_memory_map_at(&memory, (void *)placed) && entry_kind(placed) == FK_PHYSHM &&
           ((unsigned char *)placed)[100] == 0x55);
    fk_as("wine");
    /* VirtualProtect: refused by the record, though the kernel reads the range as a reservation; the mapping kept. */
    errno = 0;
    assert(!set_vprot(view, (void *)placed, 4 * PAGE, VPROT_COMMITTED | VPROT_READ) && errno == EEXIST);
    assert(entry_kind(placed) == FK_PHYSHM && ((unsigned char *)placed)[100] == 0x55);
    /* Not a hole: the range lies in win32u's view, and a stale hole would fill the table and merge with real ones. */
    assert(!__wine_ps5_native_hole(&found, &bytes));
    /* win32u_vkFreeMemory's order: the client's unmap with VK_MEMORY_UNMAP_RESERVE_BIT_EXT lays the reservation back,
     * its free, then Wine's release, which takes the range back: a commit there is Wine's own again. */
    fk_as("gpu-client");
    radv_ps5_memory_unmap_at((void *)placed, 4 * PAGE, true);
    radv_ps5_memory_free(&memory);
    fk_as("wine");
    delete_view(view);
    assert(entry_kind(placed) == FK_RESERVED && entry_kind(PW_GPU_HOST_HIGH_BASE) == FK_RESERVED);
    view = committed_view(placed, 4 * PAGE, VPROT_READ | VPROT_WRITE);
    assert(kernel_protection(placed) != 0);
    delete_view(view);
    done();
}

/* The address pool's give-back (patch 1111): map_view's retry was given a chunk of max(16 MiB, the size asked), which
 * was unmapped, a bare reservation of GiBs after the GPU had started. Guard: patch 1129 keeps it reserved; the pool given
 * back at start-up still goes around a mapping that is not Wine's. */
static void pool_give_back(void)
{
    const uintptr_t pool = 0xf000000000, gpu = pool + GIB, rest = pool + ((size_t)2 << 30) + 64 * PAGE;
    const uintptr_t gpu2 = rest + 16 * MIB;
    struct file_view *view;

    payload(1);
    wine_reserve(pool, (size_t)4 << 30);
    assert(create_view(&view, (void *)pool, (size_t)4 << 30, VPROT_PLACEHOLDER | VPROT_FREE_PLACEHOLDER) ==
           STATUS_SUCCESS);
    ps5_pool_base = (char *)pool;
    ps5_pool_end = (char *)pool + ((size_t)4 << 30);
    fk_gpu_started();
    fk_plant(FK_DIRECT, gpu, 4 * PAGE, 0x33, "gpu-client", 0);
    fk_plant(FK_DIRECT, gpu2, 4 * PAGE, 0x33, "gpu-client", 0);
    fk_clear_calls();
    /* The pool releases its front... */
    assert(ps5_pool_release((void *)0x100000000, (void *)0x10000000000, (size_t)2 << 30, 0xffff));
    assert(!fk_calls(FK_OP_MUNMAP) && in_reserved_area((void *)pool, (size_t)2 << 30));
    assert(kernel_protection(gpu) == 0x33);
    /* ...and at start-up, when it cannot be made a view, it is unmapped around the mapping. */
    ps5_release_to_system = TRUE;
    unmap_area((void *)rest, 64 * MIB);
    ps5_release_to_system = FALSE;
    assert(fk_calls(FK_OP_MUNMAP) && !fk_touched(FK_OP_MUNMAP, gpu2, gpu2 + 4 * PAGE));
    assert(kernel_protection(gpu2) == 0x33);
    done();
}

/* A wired PRX module (2026-10-06, records removed from the tree: the kernel evicted module pages under memory pressure
 * and could not page them in again, "pager error", until the loader wired every segment, -03): the kernel places a
 * module in a hole of a range Wine holds. Guards: the loader wires text and read-only data alike (pw_prx_wired), the
 * engine's release goes around them, and the same segments are unwired before the unload (pw_prx_unwire; that
 * pw_wine_dl_libc.c's stop_unload calls it first, and the loader uses pw_prx_wired, tests/test_module_wiring.py). */
static void wired_prx(void)
{
    const uintptr_t region = 0x900000000;
    unsigned char info[0x160];
    PwPrxSegment segments[PW_PRX_MAX_SEGMENTS];
    uint32_t count;
    uintptr_t text;
    FkEntry entry;

    payload(1);
    wine_reserve(region, GIB);
    for (uintptr_t at = region + 32 * MIB; at < region + 32 * MIB + 8 * PAGE; at += PAGE) make_hole(at);
    fk_set_floor(region);
    text = fk_load_module(4 * PAGE, PAGE, 2 * PAGE, info);
    assert(text == region + 32 * MIB);
    fk_as("loader");
    assert(!pw_prx_parse_module_info(info, NULL, segments, &count) && count == 3);
    for (uint32_t i = 0; i < count; i++)
        if (pw_prx_wired(segments[i].protection)) assert(!mlock(segments[i].address, segments[i].size));
    assert(fk_entry(text + 4 * PAGE, &entry) && entry.protection == PROT_READ && entry.wired == 1);
    assert(fk_entry(text + 5 * PAGE, &entry) && entry.protection == RW && !entry.wired);
    fk_as("wine");
    fk_clear_calls();
    assert(!__wine_ps5_munmap((void *)region, GIB));
    assert(fk_calls(FK_OP_MUNMAP) == 2 && !fk_touched(FK_OP_MUNMAP, text, text + 7 * PAGE));
    assert(pw_prx_unwire(info, munlock) == 0);
    fk_unload_module(text);
    done();
}

/* VirtualLock over a run, then a release and a conversion: the engine tears down what was wired. Guard: patch 1129,
 * NtLockVirtualMemory wires nothing on PS5. */
static void lock_virtual_memory(void)
{
    const uintptr_t region = 0x900000000, view = region + 16 * MIB, code_page = view + 8 * PAGE;
    void *addr = (void *)view;
    SIZE_T size = 16 * PAGE;

    payload(1);
    wine_reserve(region, 64 * MIB);
    committed_view(view, 16 * PAGE, VPROT_READ | VPROT_WRITE);
    assert(NtLockVirtualMemory(NtCurrentProcess(), &addr, &size, 1) == STATUS_SUCCESS);
    memcpy((void *)code_page, code, sizeof(code));
    assert(!__wine_ps5_mprotect((void *)code_page, PAGE, RX) && !__wine_ps5_exec_convert((void *)code_page, PAGE, RX));
    assert(NtUnlockVirtualMemory(NtCurrentProcess(), &addr, &size, 1) == STATUS_SUCCESS);
    delete_view(&views[0]);
    done();
}

/* The free-range list (patch 1119): a freed view whose neighbour shares its 64 KiB granule went in front of the range
 * found for it, the list stopped being sorted, and create_view asserted (2026-10-14, four runs; records removed from the tree). And
 * alloc_free_area_in_range returned MAP_FAILED as an address when a reservation in a reserved area was refused (patch
 * 1128). */
static void free_range_order(void)
{
    const uintptr_t region = 0x900000000;
    struct file_view *a, *b;
    struct alloc_area area = { 64 * MIB, 0x10000, PROT_NONE, FALSE, 0xffff, NULL, 0 };

    payload(1);
    free_ranges[0].base = (void *)0;
    free_ranges[0].end = (void *)~(uintptr_t)0;
    free_ranges_end = free_ranges + 1;
    assert(create_view(&a, (void *)0x590000, 0x4000, VPROT_PLACEHOLDER) == STATUS_SUCCESS);
    free_ranges_insert_view(a);
    assert(create_view(&b, (void *)0x594000, 0x64940000 - 0x594000, VPROT_PLACEHOLDER) == STATUS_SUCCESS);
    free_ranges_insert_view(find_view((void *)0x594000, 0));
    free_ranges_remove_view(find_view((void *)0x594000, 0));
    for (struct range_entry *range = free_ranges; range + 1 < free_ranges_end; range++)
        assert(range->end <= range[1].base);
    view_count = 0;

    /* Patch 1133: a view beginning in a granule its neighbour keeps, running into the next free range (a HashLink game's
     * 20 MiB section view at 0x2002c4000 after a 16 KiB one at 0x2002c0000, 2026-10-15; record removed from the tree), takes that range. */
    free_ranges[0].base = (void *)0;
    free_ranges[0].end = (void *)~(uintptr_t)0;
    free_ranges_end = free_ranges + 1;
    assert(create_view(&a, (void *)0x2002c0000, 0x4000, VPROT_PLACEHOLDER) == STATUS_SUCCESS);
    free_ranges_insert_view(find_view((void *)0x2002c0000, 0));
    assert(create_view(&a, (void *)0x2016c4000, 0x4000, VPROT_PLACEHOLDER) == STATUS_SUCCESS);
    free_ranges_insert_view(find_view((void *)0x2016c4000, 0));
    assert(create_view(&b, (void *)0x2002c4000, 0x2016c4000 - 0x2002c4000, VPROT_COMMITTED) == STATUS_SUCCESS);
    free_ranges_insert_view(find_view((void *)0x2002c4000, 0));
    for (struct range_entry *range = free_ranges; range < free_ranges_end; range++)
        assert(range->end <= (void *)0x2002c0000 || range->base >= (void *)0x2016d0000);
    view_count = 0;

    wine_reserve(region, 128 * MIB);
    mmap_add_reserved_area((void *)region, 128 * MIB);
    fk_plant(FK_DIRECT, region + 4 * PAGE, PAGE, 0x33, "gpu-client", 0);
    assert(alloc_free_area_in_range(&area, (char *)region, (char *)region + 128 * MIB) == NULL);
    done();
}

/* Chromium's 32 GiB - 64 KiB reservation (2026-10-06, record removed from the tree: ENOMEM, 25 times in 240 s): a PROT_NONE anonymous
 * mapping of it was charged whole to flexible memory. Guard: a large reservation stays bare and is backed page by page
 * as it is committed (PAYLOAD_LAZY_BYTES, the flexible engine). */
static void chromium_reservation(void)
{
    const uintptr_t base = 0x1000000000;
    const size_t bytes = ((size_t)32 << 30) - 64 * 1024;
    void *p = (void *)base;

    payload(0);
    fk_flexible_budget(440 * MIB);
    assert(!sceKernelReserveVirtualRange(&p, bytes, 0x90, PAGE));
    assert(!__wine_ps5_dmem_region(p, bytes) && fk_charged() < MIB);
    assert(!__wine_ps5_mprotect((void *)(base + GIB), MIB, RW) && fk_charged() >= MIB && fk_charged() < 2 * MIB);
    ((volatile char *)(base + GIB))[MIB - 1] = 1;
    done();
}

/* The title's memory held by a helper (2026-10-06 "VM resource leak" panic; record removed from the tree): the GPU
 * broker's owner, in the title, made a client buffer's shared memory itself when the memory helper, the maker of what
 * helpers map, was gone or late; the helper mapped it. Guards: shared_object makes none then (graphics patch 0012, the
 * applied server.cpp's own code), and a helper's shared page is its own (pw_shared_page_create). */
#define nullptr NULL
#define SHM_ANON "/broker"
#define shm_open(name, flags, mode) physhm_open((name), (flags), (mode), 0x0c)
#include "broker_shared_object.inc"
#undef shm_open
static int maker_errno;
static int maker_fails(size_t bytes) { (void)bytes; errno = maker_errno; return -1; }
static void title_memory(void)
{
    static const int gone[] = { ENOTCONN, ETIMEDOUT };
    int own;
    void *page;

    payload(1);
    assert((own = pw_shared_page_create(PAGE)) >= 0);
    assert((page = pw_wine_pages_map(PAGE, RW, own)) != MAP_FAILED);
    pw_wine_pages_unmap(page, PAGE);
    close(own);
    shared_alloc = maker_fails;
    for (unsigned i = 0; i < 2; i++) {
        int fd, error;
        maker_errno = gone[i];
        fk_as("title");
        fd = shared_object(PAGE);
        error = errno;
        /* What the owner makes goes to a client, a helper, which maps it. */
        fk_as("wine");
        if (fd >= 0) pw_wine_pages_map(PAGE, RW, fd);
        assert(fd == -1 && error == gone[i]);
    }
    done();
}

/* An alias outliving its memory (docs/MMU4K.md, I4): not a console crash yet, but the worst the 4 KiB MMU could make. A
 * second mapping of a Windows view's direct memory, at a run of the helper's shared-page range, must be given back
 * before that memory is released: the release unmaps it (FK_MAPPED), and a store through the alias would then reach
 * whatever lands in the hole, or the alias would keep released memory. Each release path of the engine runs with an
 * alias live over the memory it releases: a free, a decommit and a fresh commit over it, an execute fault's conversion
 * to JIT memory, a demand-paged view taking the range, and win32u's placed GPU range (patch 1130); and an alias evicted
 * to make room goes back to the reservation. Guard: release() forgets the aliases first (pw_wine_alias_forget). The
 * alias ops are the console's own (__wine_ps5_alias_ops, wine/ps5/pw_wine_mmu4k_ps5.c, docs/MMU4K.md section 8): a
 * payload's direct run, the shared-page range, a fixed CPU read-write direct mapping. These replays have no direct
 * section, so its source answers nothing. */
int __wine_ps5_section_backing(uintptr_t address, int64_t *dmem, uintptr_t *low, uintptr_t *high)
{
    (void)address, (void)dmem, (void)low, (void)high;
    return 0;
}
extern const PwWineAliasOps __wine_ps5_alias_ops;
static const uintptr_t alias_region = 0x900000000, alias_page = 0x900000000 + 4 * PAGE;
static uintptr_t alias_of_page;
/* A store through one address is read at the other, both ways. */
static void alias_coherent(uintptr_t page, uintptr_t alias)
{
    static unsigned char value = 0x11;
    assert(alias && alias != page && entry_kind(alias) == FK_DIRECT);
    ((volatile unsigned char *)alias)[PAGE - 1] = value;
    assert(((volatile unsigned char *)page)[PAGE - 1] == value);
    ((volatile unsigned char *)page)[0] = (unsigned char)(value + 1);
    assert(((volatile unsigned char *)alias)[0] == (unsigned char)(value + 1));
    value += 2;
}
/* A helper with 16 pages committed at the region's start and an alias of the fifth, over the window of all 16. */
static void alias_live(void)
{
    payload(1);
    wine_reserve(alias_region, 64 * MIB);
    assert(__wine_ps5_mmap((void *)alias_region, 16 * PAGE, RW, FIXED_ANON, -1, 0) == (void *)alias_region);
    assert(pw_wine_alias_init(&__wine_ps5_alias_ops) == 0);
    alias_of_page = pw_wine_alias_get(alias_page);
    alias_coherent(alias_page, alias_of_page);
    assert(pw_wine_alias_get(alias_region) == alias_of_page - 4 * PAGE && !fk_hazards());
}
/* The release gave the alias back before the memory went (nothing released was still mapped): its range is the
 * helper's reservation again. */
static void alias_given_back(void)
{
    PwWineAliasStats stats;
    done();
    pw_wine_alias_stats(&stats);
    assert(stats.forgets == 1 && pw_wine_alias_generation() == 1 && entry_kind(alias_of_page) == FK_RESERVED);
}
static void alias_free(void)
{
    alias_live();
    /* NtFreeVirtualMemory of the first four pages: the window over all 16 goes; the rest gets a window of its own. */
    assert(!__wine_ps5_munmap((void *)alias_region, 4 * PAGE));
    alias_given_back();
    alias_coherent(alias_page, pw_wine_alias_get(alias_page));
    assert(!pw_wine_alias_get(alias_region));
    done();
}
static void alias_decommit(void)
{
    alias_live();
    assert(__wine_ps5_mmap((void *)alias_region, 16 * PAGE, PROT_NONE, FIXED_ANON, -1, 0) == (void *)alias_region);
    alias_given_back();
    assert(!pw_wine_alias_get(alias_page));
    done();
}
static void alias_recommit(void)
{
    alias_live();
    /* Fresh zeroed memory over the view: the old memory is released, the new one gets its own alias. */
    assert(__wine_ps5_mmap((void *)alias_region, 16 * PAGE, RW, FIXED_ANON, -1, 0) == (void *)alias_region);
    alias_given_back();
    const uintptr_t again = pw_wine_alias_get(alias_page);
    assert(((volatile unsigned char *)again)[PAGE - 1] == 0);
    alias_coherent(alias_page, again);
    done();
}
static void alias_convert(void)
{
    alias_live();
    /* Code stored through the alias, then its first execute: the page becomes JIT memory with those bytes. */
    memcpy((void *)alias_of_page, code, sizeof(code));
    assert(!__wine_ps5_mprotect((void *)alias_page, PAGE, RX) && !__wine_ps5_exec_convert((void *)alias_page, PAGE, RX));
    assert(entry_kind(alias_page) == FK_JIT && run((void *)alias_page) == 42);
    alias_given_back();
    assert(!pw_wine_alias_get(alias_page));
    alias_coherent(alias_region, pw_wine_alias_get(alias_region));
    done();
}
static void alias_file_region(void)
{
    alias_live();
    assert(!__wine_ps5_dmem_file_region((void *)alias_region, 16 * PAGE));
    alias_given_back();
    assert(!pw_wine_alias_get(alias_page));
    done();
}
static void alias_placed_range(void)
{
    alias_live();
    assert(!__wine_ps5_placed_range_bare((void *)alias_region, 4 * PAGE - 4096));
    alias_given_back();
    alias_coherent(alias_page, pw_wine_alias_get(alias_page));
    done();
}
static void alias_evicted(void)
{
    uintptr_t aliases[PW_WINE_ALIAS_WINDOWS + 1];
    PwWineAliasStats stats;

    payload(1);
    wine_reserve(alias_region, 64 * MIB);
    assert(pw_wine_alias_init(&__wine_ps5_alias_ops) == 0);
    /* Seventeen runs of their own memory, one a 2 MiB block: the seventeenth window evicts the first, whose range is
     * laid back and taken again (first-fit), so the stale address now maps the seventeenth's memory. */
    for (unsigned i = 0; i <= PW_WINE_ALIAS_WINDOWS; i++) {
        const uintptr_t at = alias_region + i * 2 * MIB;
        assert(__wine_ps5_mmap((void *)at, PAGE, RW, FIXED_ANON, -1, 0) == (void *)at);
        alias_coherent(at, aliases[i] = pw_wine_alias_get(at));
    }
    pw_wine_alias_stats(&stats);
    assert(stats.maps == PW_WINE_ALIAS_WINDOWS + 1 && stats.evictions == 1 && !stats.forgets);
    assert(aliases[PW_WINE_ALIAS_WINDOWS] == aliases[0] && pw_wine_alias_generation() == 1);
    /* The first block asks again and gets a window of its own memory, evicting the second, whose range it takes. */
    const uintptr_t again = pw_wine_alias_get(alias_region);
    assert(again == aliases[1]);
    alias_coherent(alias_region, again);
    pw_wine_alias_stats(&stats);
    assert(stats.maps == PW_WINE_ALIAS_WINDOWS + 2 && stats.evictions == 2);
    assert(!__wine_ps5_munmap((void *)alias_region, 64 * MIB));
    done();
    pw_wine_alias_stats(&stats);
    assert(stats.forgets == PW_WINE_ALIAS_WINDOWS);
}

/* What a run of the 4 KiB MMU may store into (patch 1134's ps5_mmu4k_classify, docs/MMU4K.md section 4.4), on the
 * shared fake kernel: a host page held read-only for its neighbours (1122) takes stores through an alias only into its
 * read-write pages, never a write-copy one (the alias would write the shared memory, not a private copy), a
 * write-watched one or a read-only one; a host page the kernel lets be written takes them at the program's address
 * wherever a native store would go through, write-copy included; a host page outside every view is not Wine's. exec
 * names the 4 KiB pages Windows lets execute, whatever the host page's union (a run fetches code only from them). */
static void mmu4k_classify(void)
{
    const uintptr_t region = 0x900000000, held = region + 4 * PAGE, open = held + PAGE;
    struct file_view *view;
    PwWineMmu4kPage page;

    payload(1);
    wine_reserve(region, 64 * MIB);
    assert(__wine_ps5_mmap((void *)region, 16 * PAGE, RW, FIXED_ANON, -1, 0) == (void *)region);
    assert(pw_wine_alias_init(&__wine_ps5_alias_ops) == 0);
    assert(create_view(&view, (void *)region, 16 * PAGE, VPROT_READ | VPROT_WRITE) == STATUS_SUCCESS);
    assert(set_vprot(view, (void *)region, 16 * PAGE, VPROT_COMMITTED | VPROT_READ | VPROT_WRITE));
    assert(set_vprot(view, (void *)(held + 4096), 4096, VPROT_COMMITTED | VPROT_READ));
    assert(set_vprot(view, (void *)(held + 2 * 4096), 4096, VPROT_COMMITTED | VPROT_READ | VPROT_WRITECOPY));
    assert(set_vprot(view, (void *)(held + 3 * 4096), 4096, VPROT_COMMITTED | VPROT_READ | VPROT_WRITE | VPROT_WRITEWATCH));
    assert(set_vprot(view, (void *)(open + 4096), 4096, VPROT_COMMITTED | VPROT_READ | VPROT_WRITECOPY));
    assert(kernel_protection(held) == PROT_READ && kernel_protection(open) == RW);
    assert(!ps5_mmu4k_classify(NULL, held, &page) && page.host_page == held);
    assert(!page.kernel_write && page.plain == 0x1 && !page.exec && page.alias_delta &&
           page.generation == pw_wine_alias_generation());
    /* the alias holds the same memory: a store through it is read at the program's (read-only) address */
    assert(entry_kind(held + page.alias_delta) == FK_DIRECT);
    ((volatile unsigned char *)(held + page.alias_delta))[100] = 0x5a;
    assert(((volatile unsigned char *)held)[100] == 0x5a);
    assert(!ps5_mmu4k_classify(NULL, open, &page) && page.kernel_write && page.plain == 0xf && !page.alias_delta &&
           !page.exec);
    set_page_vprot((void *)(region + 2 * 4096), 4096, VPROT_COMMITTED | VPROT_READ | VPROT_EXEC);   /* the table only */
    assert(!ps5_mmu4k_classify(NULL, region, &page) && page.exec == 0x4 && page.plain == 0xb);
    assert(ps5_mmu4k_classify(NULL, region + 32 * PAGE, &page) == -1);
    assert(fs_repairs == 4 && !fk_hazards());
    done();
}

/* Every case runs, so one run names every replay that fails. */
/* Shared code copies (wine/ps5/pw_wine_codeshare.h; docs/PLAN.md "Shared code copies"): not a past crash but the
 * guard against the one sharing could make, a helper's write reaching code another helper runs. The title's registry is
 * the real one, asked directly (the channel, pw_wine_process.c, is not run here); a second helper is a fork, which
 * inherits the first one's mapping of the object as another process would map it. */
static unsigned code_holder, code_asks, code_wrong;
static int code_wrong_fd = -1;
int pw_ps5_code_ask(uint64_t hash, uint64_t bytes, int *fd)
{
    code_asks++;
    if (code_wrong) {   /* an object whose bytes are not the asker's: a collision, or a page the asker patched */
        *fd = dup(code_wrong_fd);
        return 0;
    }
    return pw_wine_codeshare_find(hash, bytes, code_holder, fd);
}
int pw_ps5_code_offer(uint64_t hash, uint64_t bytes, int fd)
{
    (void)pw_wine_codeshare_offer(hash, bytes, code_holder, dup(fd));   /* as SCM_RIGHTS: the title's own descriptor */
    return 0;
}
enum { SHARE_HITS, SHARE_KIB, SHARE_OFFERS, SHARE_MISMATCHES, SHARE_FAILURES, PRIVATE_WRITES, PRIVATE_KIB };
static uint64_t share_stat(unsigned which)
{
    uint64_t values[7];
    assert(__wine_ps5_code_share_stats(values, 7) == 7);
    return values[which];
}
/* An image's code as Wine leaves it before its first execute: committed, written, read-execute (the kernel holds R). */
static void image_code(uintptr_t at, size_t bytes)
{
    assert(__wine_ps5_mmap((void *)at, bytes, RW, FIXED_ANON, -1, 0) == (void *)at);
    for (size_t off = 0; off < bytes; off += PAGE) memcpy((void *)(at + off), code, sizeof(code));
    assert(!__wine_ps5_mprotect((void *)at, bytes, RX) && kernel_protection(at) == PROT_READ);
}
/* The registry's object of [at, +bytes) as the helper's bytes hash, mapped read-write by the test where the fake puts it. */
static unsigned char *registry_view(uintptr_t at, size_t bytes)
{
    int fd;
    assert(!pw_wine_codeshare_find(pw_wine_codeshare_hash((const void *)at, bytes), bytes, 31, &fd));
    void *view = mmap(NULL, bytes, RW, MAP_SHARED, fd, 0);
    assert(view != MAP_FAILED);
    close(fd);
    return view;
}
static void wait_ok(pid_t pid)
{
    int status = 0;
    assert(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
}

/* Helper A runs an image's code first: no copy yet, so its own, given to the registry. Helper B, with the same bytes at
 * the same address, maps that object (no copy made) and runs it; a write grant gives B a copy of its own first, so A
 * never sees B's write; then the same for A. Private code (a JIT engine's) and writable image code never ask. */
static void shared_code_copy(void)
{
    const uintptr_t region = 0x900000000, page = region + 4 * PAGE, heap = region + 32 * PAGE;
    unsigned char *object;
    pid_t pid;

    payload(1);
    wine_reserve(region, 64 * MIB);
    image_code(page, PAGE);
    assert(!__wine_ps5_exec_convert_image((void *)page, PAGE, RX) && entry_kind(page) == FK_JIT &&
           kernel_protection(page) == RX && run((void *)page) == 42);
    assert(code_asks == 1 && share_stat(SHARE_OFFERS) == 1 && !share_stat(SHARE_HITS));
    object = registry_view(page, PAGE);
    assert(!memcmp(object, (void *)page, PAGE));
    object[PAGE - 1] = 0x11;   /* A maps that very object */
    assert(((volatile unsigned char *)page)[PAGE - 1] == 0x11);
    object[PAGE - 1] = 0;

    if (!(pid = __real_fork())) {
        code_holder = 1;
        image_code(page, PAGE);   /* B's own image at the same address: A's mapping is not B's */
        assert(!__wine_ps5_exec_convert_image((void *)page, PAGE, RX) && run((void *)page) == 42);
        assert(share_stat(SHARE_HITS) == 1 && share_stat(SHARE_KIB) == PAGE >> 10 && share_stat(SHARE_OFFERS) == 1);
        object[PAGE - 2] = 0x22;
        assert(((volatile unsigned char *)page)[PAGE - 2] == 0x22);   /* B maps the registry's object */
        object[PAGE - 2] = 0;
        /* VirtualProtect to read-write-execute, then a store: into a copy of B's own. */
        assert(!__wine_ps5_mprotect((void *)page, PAGE, RW | PROT_EXEC));
        ((volatile unsigned char *)page)[PAGE - 1] = 0x5a;
        assert(object[PAGE - 1] == 0 && run((void *)page) == 42 && share_stat(PRIVATE_WRITES) == 1);
        /* A second grant finds nothing shared there any more. */
        assert(!__wine_ps5_mprotect((void *)page, PAGE, RW) && share_stat(PRIVATE_WRITES) == 1);
        done();
        _exit(0);
    }
    wait_ok(pid);
    assert(((volatile unsigned char *)page)[PAGE - 1] == 0 && object[PAGE - 1] == 0);   /* B's store reached neither */
    assert(!__wine_ps5_mprotect((void *)page, PAGE, RW | PROT_EXEC));
    ((volatile unsigned char *)page)[PAGE - 1] = 0x77;
    assert(object[PAGE - 1] == 0 && share_stat(PRIVATE_WRITES) == 1);

    /* Private code and writable image code are copied as before and never offered or asked about. */
    code_asks = 0;
    image_code(heap, PAGE);
    assert(!__wine_ps5_exec_convert((void *)heap, PAGE, RX) && run((void *)heap) == 42);
    assert(__wine_ps5_mmap((void *)heap, PAGE, RW, FIXED_ANON, -1, 0) == (void *)heap);
    memcpy((void *)heap, code, sizeof(code));
    assert(!__wine_ps5_exec_convert_image((void *)heap, PAGE, RW | PROT_EXEC) && run((void *)heap) == 42);
    assert(!code_asks && share_stat(SHARE_OFFERS) == 1);
    done();
}

/* A copy is mapped only when its bytes are the helper's, whatever the registry answers; a range freed and committed again
 * is plain memory, which a write grant leaves alone; and a write grant whose copy fails is refused, the object untouched. */
/* The code cache (docs/FLEXIBLE_MEMORY.md, WINE_PS5_CODE_BUDGET_MIB): past the budget the oldest converted clusters go
 * back to direct memory holding the same bytes, without execute, and run again after their next conversion; no direct
 * memory is released while mapped (the fake kernel's hazards). */
static void code_cache(void)
{
    const uintptr_t region = 0x900000000;
    uint64_t stats[10];

    setenv("WINE_PS5_CODE_BUDGET_MIB", "1", 1);   /* 64 clusters of 16 KiB */
    payload(1);
    wine_reserve(region, 64 * MIB);
    for (unsigned i = 0; i < 80; i++) {
        const uintptr_t page = region + i * PAGE;
        assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == (void *)page);
        memcpy((void *)page, code, sizeof(code));
        ((volatile unsigned char *)page)[100] = (unsigned char)i;
        assert(!__wine_ps5_mprotect((void *)page, PAGE, RX) && !__wine_ps5_exec_convert((void *)page, PAGE, RX));
    }
    for (unsigned i = 0; i < 80; i++) {
        const uintptr_t page = region + i * PAGE;
        const int evicted = i < 16;
        assert((entry_kind(page) == FK_JIT) == !evicted);
        assert(kernel_protection(page) == (evicted ? PROT_READ : RX));
        assert(!memcmp((const void *)page, code, sizeof(code)) && ((const unsigned char *)page)[100] == i);
    }
    assert(run((void *)(region + 16 * PAGE)) == 42);
    /* The next execute of an evicted cluster converts it again; the oldest still held makes room. */
    assert(!__wine_ps5_exec_convert((void *)region, PAGE, RX) && entry_kind(region) == FK_JIT && run((void *)region) == 42);
    assert(entry_kind(region + 16 * PAGE) != FK_JIT && kernel_protection(region + 16 * PAGE) == PROT_READ);
    /* One freed since its conversion is found stale and dropped, not touched. */
    assert(!__wine_ps5_munmap((void *)(region + 17 * PAGE), PAGE));
    for (unsigned i = 80; i < 82; i++) {
        const uintptr_t page = region + i * PAGE;
        assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == (void *)page);
        memcpy((void *)page, code, sizeof(code));
        assert(!__wine_ps5_mprotect((void *)page, PAGE, RX) && !__wine_ps5_exec_convert((void *)page, PAGE, RX));
    }
    assert(__wine_ps5_code_cache_stats(stats, 10) == 10);
    /* The budget held, every eviction kept its bytes, the freed one was only dropped (an entry the kernel no longer
     * shows as executable code is never touched, so the count can only overstate what the cache holds). */
    assert(stats[0] == 1 && stats[1] <= 1024 && stats[2] >= 17 && stats[3] == stats[2] * 16 && stats[4] >= 1);
    assert(!stats[5] && !stats[6] && stats[9] == 1);   /* a budget the user set: never raised */
    assert(!stats[7]);
    done();
}

/* The code cache's default (the owner, 2026-10-25): on at 64 MiB unless WINE_PS5_CODE_BUDGET_MIB says otherwise; 0 is off,
 * and an off cache evicts nothing past any total. */
static void code_cache_default(void)
{
    const uintptr_t region = 0x900000000;
    uint64_t stats[10];

    unsetenv("WINE_PS5_CODE_BUDGET_MIB");
    payload(1);
    wine_reserve(region, 64 * MIB);
    assert(__wine_ps5_mmap((void *)region, PAGE, RW, FIXED_ANON, -1, 0) == (void *)region);
    memcpy((void *)region, code, sizeof(code));
    assert(!__wine_ps5_mprotect((void *)region, PAGE, RX) && !__wine_ps5_exec_convert((void *)region, PAGE, RX));
    assert(__wine_ps5_code_cache_stats(stats, 10) == 10);
    /* The engine's self-check converted a page of its own first: an entry the cache finds stale and skips. */
    assert(stats[0] == 64 && stats[1] >= 16 && !stats[2] && stats[9] == 3);   /* no program name in a host test: adaptive */
    done();
}
static void code_cache_off(void)
{
    const uintptr_t region = 0x900000000;
    uint64_t stats[10];

    setenv("WINE_PS5_CODE_BUDGET_MIB", "0", 1);
    payload(1);
    wine_reserve(region, 64 * MIB);
    for (unsigned i = 0; i < 80; i++) {
        const uintptr_t page = region + i * PAGE;
        assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == (void *)page);
        memcpy((void *)page, code, sizeof(code));
        assert(!__wine_ps5_mprotect((void *)page, PAGE, RX) && !__wine_ps5_exec_convert((void *)page, PAGE, RX));
    }
    for (unsigned i = 0; i < 80; i++) assert(entry_kind(region + i * PAGE) == FK_JIT);
    assert(__wine_ps5_code_cache_stats(stats, 10) == 10 && !stats[0] && !stats[1] && !stats[2] && !stats[9]);
    done();
}

/* The raise, through the real cache (pw_wine_code_budget.h): a program whose evicted clusters come back a second time, 1 MiB
 * within 10 s, gets 32 MiB more, once; code coming back once (a phase change) raises nothing. The evicted clusters come
 * back as JIT memory that runs. */
static void code_cache_raise(void)
{
    const uintptr_t region = 0x900000000;
    const unsigned pages = 64 * 64 + 64;   /* 65 MiB: the oldest 64 clusters go back to direct memory */
    uint64_t stats[10];

    unsetenv("WINE_PS5_CODE_BUDGET_MIB");
    payload(1);
    wine_reserve(region, 128 * MIB);
    for (unsigned i = 0; i < pages; i++) {
        const uintptr_t page = region + i * PAGE;
        assert(__wine_ps5_mmap((void *)page, PAGE, RW, FIXED_ANON, -1, 0) == (void *)page);
        memcpy((void *)page, code, sizeof(code));
        assert(!__wine_ps5_mprotect((void *)page, PAGE, RX) && !__wine_ps5_exec_convert((void *)page, PAGE, RX));
    }
    assert(__wine_ps5_code_cache_stats(stats, 10) == 10 && stats[0] == 64 && stats[2] >= 64 && !stats[7]);
    assert(entry_kind(region) != FK_JIT && kernel_protection(region) == PROT_READ);
    for (unsigned i = 0; i < 64; i++)
        assert(!__wine_ps5_exec_convert((void *)(region + i * PAGE), PAGE, RX) && run((void *)(region + i * PAGE)) == 42);
    assert(__wine_ps5_code_cache_stats(stats, 10) == 10 && stats[0] == 64 && !stats[7] && stats[8] >= 1024);
    /* In order, the 65 MiB cycles through the 64 MiB cache: the first pass brings every cluster back once (no raise),
     * the second brings them back again, which raises; it stops there, before the clusters still out come back in a
     * window of their own. A page that is JIT memory already is left as it is. */
    for (unsigned i = 0; i < pages; i++)
        assert(!__wine_ps5_exec_convert((void *)(region + i * PAGE), PAGE, RX) && run((void *)(region + i * PAGE)) == 42);
    assert(__wine_ps5_code_cache_stats(stats, 10) == 10 && stats[0] == 64 && !stats[7] && stats[8] >= 64 * 1024);
    for (unsigned i = 0; i < pages && !stats[7]; i++) {
        assert(!__wine_ps5_exec_convert((void *)(region + i * PAGE), PAGE, RX) && run((void *)(region + i * PAGE)) == 42);
        assert(__wine_ps5_code_cache_stats(stats, 10) == 10);
    }
    assert(stats[0] == 96 && stats[7] == 1 && !stats[5] && !stats[6] && stats[9] == 3);
    done();
}

static void shared_code_guards(void)
{
    const uintptr_t region = 0x900000000, page = region + 4 * PAGE, pair = region + 8 * PAGE;
    unsigned char *object;
    int other;

    payload(1);
    wine_reserve(region, 64 * MIB);
    assert((other = memfd_create("other-code", 0)) >= 0 && !ftruncate(other, PAGE));
    code_wrong = 1;
    code_wrong_fd = other;
    image_code(page, PAGE);
    assert(!__wine_ps5_exec_convert_image((void *)page, PAGE, RX) && run((void *)page) == 42);
    assert(share_stat(SHARE_MISMATCHES) == 1 && !share_stat(SHARE_HITS));
    code_wrong = 0;

    /* Freed (Wine's release maps the range PROT_NONE), committed again and run as private code (a JIT engine's): the
     * release forgot the shared copy, so a write grant there copies nothing. */
    assert(__wine_ps5_mmap((void *)page, PAGE, PROT_NONE, FIXED_ANON, -1, 0) == (void *)page);
    image_code(page, PAGE);
    assert(entry_kind(page) == FK_DIRECT);
    assert(!__wine_ps5_exec_convert((void *)page, PAGE, RX) && run((void *)page) == 42);
    assert(!__wine_ps5_mprotect((void *)page, PAGE, RW | PROT_EXEC) && !share_stat(PRIVATE_WRITES));

    /* Two pages, one object: a write grant on the second copies that page only; the first still maps the object. */
    image_code(pair, 2 * PAGE);
    assert(!__wine_ps5_exec_convert_image((void *)pair, 2 * PAGE, RX) && run((void *)(pair + PAGE)) == 42);
    object = registry_view(pair, 2 * PAGE);
    assert(!__wine_ps5_mprotect((void *)(pair + PAGE), PAGE, RW | PROT_EXEC));
    ((volatile unsigned char *)pair)[2 * PAGE - 1] = 0x5a;
    assert(object[2 * PAGE - 1] == 0 && share_stat(PRIVATE_WRITES) == 1 && share_stat(PRIVATE_KIB) == PAGE >> 10);
    object[PAGE - 1] = 0x33;
    assert(((volatile unsigned char *)pair)[PAGE - 1] == 0x33);
    object[PAGE - 1] = 0;

    /* The copy before a write grant fails (its scratch mapping): the grant is refused and the object never writable. */
    fk_fail_next(FK_OP_MMAP);
    assert(__wine_ps5_mprotect((void *)pair, PAGE, RW | PROT_EXEC) == -1);
    assert(!(kernel_protection(pair) & PROT_WRITE) && share_stat(PRIVATE_WRITES) == 1);
    assert(!__wine_ps5_mprotect((void *)pair, PAGE, RW | PROT_EXEC) && share_stat(PRIVATE_WRITES) == 2);
    ((volatile unsigned char *)pair)[PAGE - 1] = 0x44;
    assert(object[PAGE - 1] == 0);
    close(other);
    done();
}

static int failed;
static void in_child(void (*test)(void), const char *name)
{
    int status = 0;
    const pid_t pid = __real_fork();
    assert(pid >= 0);
    if (!pid) {
        test();
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "%s: FAILED (status %#x)\n", name, (unsigned)status);
        failed = 1;
        return;
    }
    printf("%s: ok\n", name);
}

int main(void)
{
    in_child(placeholder_free, "89e40ab placeholder free");
    in_child(late_library_release, "late-library release");
    in_child(commit_over_foreign, "commit over a foreign mapping");
    in_child(window_page_vanished, "window page that vanished");
    in_child(window_page_lost, "window page laid back lost");
    in_child(window_page_no_range, "window page with no range");
    in_child(host_protection_leaf, "host-protection leaf");
    in_child(heap_signals_blocked, "heap's kernel calls with suspend and kill blocked");
    in_child(sdk_execute_grant, "SDK execute grant");
    in_child(write_tracking, "4 KiB write tracking");
    in_child(noaddress_in_view, "no-address mapping in a demand-paged view");
    in_child(intruder_refill, "intruder between query and refill");
    in_child(laid_not_replacing, "laid reservation that does not replace");
    in_child(placed_gpu_map, "placed GPU map");
    in_child(gpu_client_unreserved, "GPU client without host ranges");
    in_child(gpu_client_in_owner, "GPU client in the owner's own process");
    in_child(gpu_client_ranges, "GPU client host ranges");
    in_child(gpu_client_free_order, "GPU client free order");
    in_child(placed_shared_memory, "placed shared memory");
    in_child(pool_give_back, "address pool give-back");
    in_child(wired_prx, "wired PRX module");
    in_child(lock_virtual_memory, "VirtualLock");
    in_child(free_range_order, "free-range order and a refused reserved-area mapping");
    in_child(chromium_reservation, "Chromium's 32 GiB reservation");
    in_child(title_memory, "title-made memory");
    in_child(alias_free, "alias outlives its memory: free");
    in_child(alias_decommit, "alias outlives its memory: decommit");
    in_child(alias_recommit, "alias outlives its memory: commit over it");
    in_child(alias_convert, "alias outlives its memory: execute conversion");
    in_child(alias_file_region, "alias outlives its memory: demand-paged view");
    in_child(alias_placed_range, "alias outlives its memory: placed GPU range");
    in_child(alias_evicted, "alias evicted");
    in_child(mmu4k_classify, "4 KiB MMU classify");
    in_child(shared_code_copy, "shared code copy: two helpers, one object, a write gets its own");
    in_child(shared_code_guards, "shared code copy: other bytes, a freed range, one page of two, a failed copy");
    in_child(code_cache, "code cache: evicted to direct memory, converted again");
    in_child(code_cache_default, "code cache: on at 64 MiB by default");
    in_child(code_cache_raise, "code cache: code back once raises nothing, 1 MiB back again in 10 s raises a program's budget 32 MiB");
    in_child(code_cache_off, "code cache: WINE_PS5_CODE_BUDGET_MIB=0 keeps every cluster");
    if (failed) return 1;
    puts("crash replays: every past console crash of a helper's memory code replayed against the fake kernel, no "
         "hazard, passed");
    return 0;
}
