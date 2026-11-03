/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_HEAP_H
#define PW_WINE_HEAP_H
#include <stddef.h>
#include <stdint.h>

/* Heap for Wine's Unix side on the PS5, where the title's libc malloc stops
 * near 13 MiB. Small blocks (up to 64 KiB) come from power-of-two size
 * classes carved out of 1 MiB anonymous mappings; larger blocks get their
 * own mapping and are unmapped on free. One mutex serialises every call.
 * A pointer this heap did not return is never touched: free counts it as
 * foreign and returns, so memory allocated inside libc cannot corrupt it. */
typedef struct PwWineHeapStats {
    uint64_t live_bytes,peak_live_bytes,mapped_bytes,peak_mapped_bytes;
    uint64_t allocations,frees,failures,foreign_frees,large_live;
    uint64_t contended;     /* calls that found the heap's mutex held by another thread */
} PwWineHeapStats;

void *pw_wine_heap_malloc(size_t bytes);
void *pw_wine_heap_calloc(size_t count,size_t bytes);
void *pw_wine_heap_realloc(void *pointer,size_t bytes);
void pw_wine_heap_free(void *pointer);
/* A block aligned to alignment, a power of two; free and realloc take it
 * like any other. NULL for a bad alignment or no memory. */
void *pw_wine_heap_memalign(size_t alignment,size_t bytes);
/* Usable bytes of a block this heap returned, 0 for anything else. */
size_t pw_wine_heap_usable_size(const void *pointer);
void pw_wine_heap_stats(PwWineHeapStats *stats);
/* Called around every hold of the heap's mutex, from before asking for it until after letting it go: ntdll's start
 * sets the synchronization device's critical section (pw_ntsync_critical_enter/leave, wine/ps5/pw_wine_dmem_ps5.c), so a
 * suspend that lands inside waits until the thread has let go. A thread suspended holding the mutex stopped every other
 * thread's next malloc, a minidump's own (a8-rel-base-2026-11-02 suspend-server64). The in-process server's copy has
 * none: its code runs with signals blocked. A leave without its enter (the guard set meanwhile) does nothing. */
void pw_wine_heap_set_guard(void (*enter)(void),void (*leave)(void));

/* Where the heap's address space comes from. By default it maps anonymous
 * memory itself, which on the console draws from about 440 MiB of flexible
 * memory shared with every thread stack and library, and runs out under a
 * big program (a JVM with a GL driver compiling shaders). A backing serves
 * the heap's spans and large blocks from elsewhere (the console's direct
 * memory): map returns zeroed read-write memory, bytes a multiple of 16 KiB,
 * or NULL, and the allocation then fails (the heap no longer maps by itself);
 * unmap takes back a block map returned, whole. Memory the heap took before
 * a backing was set is released the way it was taken. */
void pw_wine_heap_set_backing(void *(*map)(size_t bytes),void (*unmap)(void *address,size_t bytes));

#endif
