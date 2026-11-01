/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of patch 1127's fault hook: ps5_fault_served, extracted from the applied virtual.c by
 * tests/extract_fault_hook.py (tools/host-tests.sh). The grant retry, its bound and what resets it (another fault, a
 * conversion), guard and write-watch pages, read and write faults on code never converted, the conversion cluster's
 * window, view and protection limits, the view kinds converted, forced execute, a failed conversion, one that failed
 * beside a faulting page it converted, and Wine's host-protection cache after both. Wine's page tables are a fake
 * 2 MiB arena. Each expectation holds for any window of 1 to 16 host pages. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
typedef unsigned char BYTE; typedef int BOOL; typedef uintptr_t ULONG_PTR, UINT_PTR; typedef size_t SIZE_T;
typedef unsigned long long ULONG64;
#define TRUE 1
#define FALSE 0
#define EXCEPTION_WRITE_FAULT 1
#define EXCEPTION_EXECUTE_FAULT 8
#define VPROT_READ 0x01
#define VPROT_WRITE 0x02
#define VPROT_EXEC 0x04
#define VPROT_WRITECOPY 0x08
#define VPROT_GUARD 0x10
#define VPROT_COMMITTED 0x20
#define VPROT_WRITEWATCH 0x40
#define SEC_FILE 0x00800000
#define SEC_IMAGE 0x01000000
#define SEC_RESERVE 0x04000000
#define SEC_COMMIT 0x08000000
#define ROUND_ADDR(addr,mask) ((void *)((UINT_PTR)(addr) & ~(UINT_PTR)(mask)))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))
struct file_view { void *base; size_t size; unsigned int protect; };
struct ntdll_thread_data { struct { void *address; ULONG64 conversions; } ps5_granted; };

enum { PAGE = 4096, HOST = 16384, PAGES = 512 };
static char arena[PAGES * PAGE] __attribute__((aligned(1 << 21)));
static BYTE vprot[PAGES];
static int kernel[PAGES / 4];          /* what the host allows, per host page */
static struct file_view views[4];
static int force_exec_prot, convert_result, convert_partial, converts, queries;
static unsigned char cache[PAGES / 4];   /* Wine's host-protection cache, per host page */
static char *convert_start; static size_t convert_bytes; static int convert_prot;
static struct ntdll_thread_data thread_data;
static const size_t host_page_size = HOST;

static size_t index_of(const void *addr) { return (size_t)((const char *)addr - arena) / PAGE; }
static BYTE get_page_vprot(const void *addr)
{
    return (const char *)addr >= arena && (const char *)addr < arena + sizeof(arena) ? vprot[index_of(addr)] : 0;
}
static int get_unix_prot(BYTE vprot);
static int get_host_page_unix_prot(const void *addr, BYTE set, BYTE clear)
{
    const char *page = ROUND_ADDR(addr, HOST - 1);
    BYTE all = 0;
    for (int i = 0; i < 4; i++) all |= get_page_vprot(page + i * PAGE);
    return get_unix_prot((all & ~clear) | set);
}
static struct file_view *find_view(const void *addr, size_t size)
{
    (void)size;
    for (unsigned i = 0; i < sizeof(views) / sizeof(*views); i++)
        if (views[i].size && (const char *)addr >= (char *)views[i].base &&
            (const char *)addr < (char *)views[i].base + views[i].size) return &views[i];
    return NULL;
}
static struct ntdll_thread_data *ntdll_get_thread_data(void) { return &thread_data; }
int __wine_ps5_host_allows(const void *address, int access)
{
    queries++;
    return (kernel[index_of(address) / 4] & access) == access;
}
int __wine_ps5_exec_convert(void *address, size_t bytes, int unix_prot)
{
    converts++; convert_start = address; convert_bytes = bytes; convert_prot = unix_prot;
    /* A later piece failed: the first one was converted. */
    if (convert_result && convert_partial) kernel[index_of(address) / 4] = unix_prot;
    if (convert_result) return -1;
    for (size_t at = 0; at < bytes; at += HOST) kernel[index_of((char *)address + at) / 4] = unix_prot;
    return 0;
}
/* An image view's conversion, shared with other helpers (wine/ps5/pw_wine_codeshare.h): the same, counted apart. */
static unsigned image_converts;
int __wine_ps5_exec_convert_image(void *address, size_t bytes, int unix_prot)
{
    image_converts++;
    return __wine_ps5_exec_convert(address, bytes, unix_prot);
}
static void host_prot_record(const void *addr, size_t len, unsigned char value)
{
    for (size_t at = 0; at < len; at += HOST) cache[index_of((const char *)addr + at) / 4] = value;
}
/* Patch 2420's execute-fault note comes along with the handler it sits beside; what it reads, none of it here. */
typedef int LONG;
static const uintptr_t host_page_mask = HOST - 1;
static unsigned get_host_page_vprot(const void *page) { (void)page; return 0; }
static int __wine_ps5_kernel_prot(const void *address) { (void)address; return -1; }
#include "fault_hook.inc"
static void (*const exec_fault_note)(void *, const void *) __attribute__((unused)) = ps5_note_exec_fault;
/* The window is the patch's (256 KiB, or 16 KiB on trial: docs/MIHAWK_BROWSER.md); expectations follow it. */
enum { W = PS5_EXEC_CLUSTER / HOST };
_Static_assert(PS5_EXEC_CLUSTER % HOST == 0 && W >= 1 && W <= 16, "a window of 1 to 16 host pages");
static size_t window(size_t h) { return h - h % W; }

static void reset(void)
{
    memset(vprot, 0, sizeof(vprot)); memset(kernel, 0, sizeof(kernel)); memset(views, 0, sizeof(views));
    memset(cache, 0, sizeof(cache)); memset(&thread_data, 0, sizeof(thread_data));
    force_exec_prot = convert_result = convert_partial = converts = queries = image_converts = 0;
}
static void set(size_t first_page, size_t count, BYTE prot)
{
    for (size_t i = first_page; i < first_page + count; i++) vprot[i] = prot | VPROT_COMMITTED;
}
/* The kernel as a payload's: execute dropped from everything not converted. */
static void kernel_from_wine(void)
{
    for (size_t h = 0; h < PAGES / 4; h++) kernel[h] = get_host_page_unix_prot(arena + h * HOST, 0, 0) & ~PROT_EXEC;
}
static char *host(size_t h) { return arena + h * HOST; }
static BOOL fault(void *addr, ULONG_PTR err) { return ps5_fault_served(addr, ROUND_ADDR(addr, HOST - 1), err); }

int main(void)
{
    /* (a) A write the host allows runs again once; the same fault again takes the upstream path; then once more. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, PAGES, VPROT_READ | VPROT_WRITE);
    kernel_from_wine();
    assert(fault(host(3) + 8, EXCEPTION_WRITE_FAULT) && thread_data.ps5_granted.address == host(3) + 8);
    assert(!fault(host(3) + 8, EXCEPTION_WRITE_FAULT) && !thread_data.ps5_granted.address && !converts);
    assert(fault(host(3) + 8, EXCEPTION_WRITE_FAULT));
    /* A write the host refuses is not run again (the 4 KiB step path of patch 1122 takes it). */
    kernel[3] = PROT_READ; thread_data.ps5_granted.address = NULL;
    assert(!fault(host(3) + 8, EXCEPTION_WRITE_FAULT) && !converts);
    /* Guard and write-watch pages are never run again, even where the host allows the access. */
    queries = 0;
    vprot[20] |= VPROT_GUARD; assert(!fault(arena + 20 * PAGE, 0) && !queries);
    vprot[24] |= VPROT_WRITEWATCH; assert(!fault(arena + 24 * PAGE, EXCEPTION_WRITE_FAULT) && !queries);
    /* A read of a no-access page asks nothing of the kernel. */
    vprot[28] = VPROT_COMMITTED; assert(!fault(arena + 28 * PAGE, 0) && !queries);

    /* (b) Execute in a private view: the window around the fault, every host page RX. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, PAGES, VPROT_READ | VPROT_EXEC);
    kernel_from_wine();
    assert(fault(host(5) + 100, EXCEPTION_EXECUTE_FAULT) && converts == 1);
    assert(convert_start == host(window(5)) && convert_bytes == PS5_EXEC_CLUSTER &&
           convert_prot == (PROT_READ | PROT_EXEC));
    /* Converted: another thread's execute fault there runs again with no second conversion. The same fault again on
     * a page the host lets run is not a race: neither run again nor converted, so it cannot loop. */
    assert(fault(host(5) + 104, EXCEPTION_EXECUTE_FAULT) && converts == 1);
    assert(!fault(host(5) + 104, EXCEPTION_EXECUTE_FAULT) && converts == 1 && !thread_data.ps5_granted.address);
    /* The next window is its own conversion. */
    assert(fault(host(17), EXCEPTION_EXECUTE_FAULT) && converts == 2 && convert_start == host(window(17)) &&
           convert_bytes == PS5_EXEC_CLUSTER);

    /* The cluster stops at a host page with another protection: RW at host page 3, R at 9 (one RX page beside RW
     * pages makes host page 7 RWX, also another protection). */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, PAGES, VPROT_READ | VPROT_EXEC);
    set(3 * 4, 4, VPROT_READ | VPROT_WRITE); set(9 * 4, 4, VPROT_READ); set(7 * 4 + 1, 1, VPROT_READ | VPROT_WRITE);
    kernel_from_wine();
    assert(fault(host(5), EXCEPTION_EXECUTE_FAULT) && convert_start == host(max(4, window(5))) &&
           convert_bytes == (min(7, window(5) + W) - max(4, window(5))) * HOST);
    assert(fault(host(7) + 1, EXCEPTION_EXECUTE_FAULT) && convert_start == host(7) && convert_bytes == HOST &&
           convert_prot == (PROT_READ | PROT_WRITE | PROT_EXEC));

    /* The cluster stays in its view: host pages only partly inside are left out, but the faulting one is whole. Its
     * converted neighbours of the same protection are in the next cluster again (the glue keeps JIT runs as they are). */
    reset(); views[0] = (struct file_view){ arena + 0x2000, 0x3a000 - 0x2000, 0 }; set(2, 0x3a - 2, VPROT_READ | VPROT_EXEC);
    kernel_from_wine();
    assert(fault(host(2), EXCEPTION_EXECUTE_FAULT) && convert_start == host(max(1, window(2))) &&
           convert_bytes == (min(14, window(2) + W) - max(1, window(2))) * HOST);
    assert(fault(arena + 0x2000, EXCEPTION_EXECUTE_FAULT) && convert_start == host(0) &&
           convert_bytes == min(14, W) * HOST);
    assert(fault(arena + 0x39000, EXCEPTION_EXECUTE_FAULT) && convert_start == host(max(1, window(14))) &&
           convert_bytes == (15 - max(1, window(14))) * HOST);

    /* Image views convert; section views and pages Wine does not let run never do. */
    reset(); views[0] = (struct file_view){ arena, 0x40000, SEC_IMAGE | SEC_FILE };
    views[1] = (struct file_view){ arena + 0x40000, 0x40000, SEC_COMMIT };
    set(0, 0x80, VPROT_READ | VPROT_EXEC); set(0x50, 4, VPROT_READ);
    kernel_from_wine();
    assert(fault(host(1), EXCEPTION_EXECUTE_FAULT) && converts == 1 && image_converts == 1);
    assert(!fault(host(17), EXCEPTION_EXECUTE_FAULT) && converts == 1);
    assert(!fault(arena + 0x50 * PAGE, EXCEPTION_EXECUTE_FAULT) && converts == 1);

    /* Forced execute (a 32-bit program without DEP): read-write memory runs, converted read-write-execute. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 16, VPROT_READ | VPROT_WRITE);
    kernel_from_wine(); force_exec_prot = 1;
    assert(fault(host(2), EXCEPTION_EXECUTE_FAULT) && convert_start == host(window(2)) &&
           convert_bytes == (min(4, window(2) + W) - window(2)) * HOST &&
           convert_prot == (PROT_READ | PROT_WRITE | PROT_EXEC) && !image_converts);   /* private: never shared */

    /* A failed conversion is not served, and the next fault there tries again (it is refused, never looped). */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 4, VPROT_READ | VPROT_EXEC);
    kernel_from_wine(); convert_result = -1;
    assert(!fault(host(0), EXCEPTION_EXECUTE_FAULT) && converts == 1);
    assert(!fault(host(0), EXCEPTION_EXECUTE_FAULT) && converts == 2);

    /* Another piece of the cluster failed after the faulting page's own was converted: the instruction runs, and the
     * same fault again takes the upstream path. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 8, VPROT_READ | VPROT_EXEC);
    kernel_from_wine(); convert_result = -1; convert_partial = 1;
    assert(fault(host(0), EXCEPTION_EXECUTE_FAULT) && converts == 1 && thread_data.ps5_granted.address == host(0));
    assert(!fault(host(0), EXCEPTION_EXECUTE_FAULT) && converts == 1);

    /* The host-protection cache follows a conversion: a host page another thread opened for a 4 KiB step (cached
     * RWX) is cached as what the kernel now holds, so the step's reopen reaches the kernel; after a failure,
     * unknown. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 8, VPROT_READ | VPROT_EXEC);
    kernel_from_wine();
    {
        const size_t last = min(2, W) - 1;   /* the run's last host page: the neighbour, or the faulting page itself */
        cache[last] = PROT_READ | PROT_WRITE | PROT_EXEC | HOST_PROT_KNOWN;
        assert(fault(host(0), EXCEPTION_EXECUTE_FAULT) && convert_bytes == (last + 1) * HOST);
        assert(cache[0] == (PROT_READ | PROT_EXEC | HOST_PROT_KNOWN) && cache[last] == cache[0]);
        reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 8, VPROT_READ | VPROT_EXEC);
        kernel_from_wine(); convert_result = -1; cache[last] = PROT_READ | PROT_EXEC | HOST_PROT_KNOWN; cache[0] = cache[last];
        assert(!fault(host(0), EXCEPTION_EXECUTE_FAULT) && !cache[0] && !cache[last]);
    }

    /* A read or write fault on code is never converted: run again, it would fault forever. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 8, VPROT_READ | VPROT_EXEC);
    kernel_from_wine(); kernel[1] = 0;
    assert(!fault(host(0), EXCEPTION_WRITE_FAULT) && !fault(host(1), 0) && !converts);
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 8, VPROT_READ | VPROT_WRITE);
    kernel_from_wine(); force_exec_prot = 1; kernel[0] = PROT_READ;
    assert(!fault(host(0), EXCEPTION_WRITE_FAULT) && !converts);
    /* A fault not run again clears the last grant, so the next grant at the same address runs again. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 }; set(0, 8, VPROT_READ | VPROT_WRITE);
    kernel_from_wine(); kernel[1] = PROT_READ;
    assert(fault(host(0), EXCEPTION_WRITE_FAULT) && !fault(host(1), EXCEPTION_WRITE_FAULT) &&
           fault(host(0), EXCEPTION_WRITE_FAULT));
    /* A second race at the same address after another thread's conversion runs again; with none between it does not. */
    reset(); views[0] = (struct file_view){ arena, sizeof(arena), 0 };
    set(0, 16, VPROT_READ | VPROT_WRITE); set(16, 16, VPROT_READ | VPROT_EXEC);
    kernel_from_wine();
    assert(fault(host(0) + 8, EXCEPTION_WRITE_FAULT));
    {
        const struct ntdll_thread_data mine = thread_data;
        memset(&thread_data, 0, sizeof(thread_data));
        assert(fault(host(4), EXCEPTION_EXECUTE_FAULT) && converts == 1 && convert_start == host(4));
        thread_data = mine;
    }
    assert(fault(host(0) + 8, EXCEPTION_WRITE_FAULT) && !fault(host(0) + 8, EXCEPTION_WRITE_FAULT));
    puts("test_wine_fault_served: ok");
    return 0;
}
