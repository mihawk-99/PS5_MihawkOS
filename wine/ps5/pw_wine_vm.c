/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_vm.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Sony's flags and name share a packed tail after the memory type, unlike some public SDK declarations. */
struct pw_vm_query_info
{
    void *start, *end;
    uint64_t offset;
    int32_t protection, type;
    uint8_t flags;
    char name[32];
    uint8_t padding[7];
};
_Static_assert(sizeof(struct pw_vm_query_info) == 72 && offsetof(struct pw_vm_query_info, flags) == 32,
               "libkernel query record");
extern int sceKernelVirtualQuery(const void *, int, void *, size_t);

static int pw_wine_vm_entry_impl(const void *address, PwWineVmEntry *entry, int *failure_kind,
                                 int *kernel_status)
{
    struct pw_vm_query_info info = {0};
    const int status = sceKernelVirtualQuery(address, 1, &info, sizeof(info));

    if (failure_kind) *failure_kind = PW_WINE_VM_FAILURE_NONE;
    if (kernel_status) *kernel_status = status;
    /* The kernel returns EACCES when NEXT has no remaining mapping. */
    if ((uint32_t)status == UINT32_C(0x8002000d)) return 1;
    if (status) {
        if (failure_kind) *failure_kind = PW_WINE_VM_FAILURE_QUERY;
        return -1;
    }
    if ((uintptr_t)info.end <= (uintptr_t)address || (uintptr_t)info.end <= (uintptr_t)info.start) {
        if (failure_kind) *failure_kind = PW_WINE_VM_FAILURE_ENTRY;
        return -1;
    }
    entry->start = (uintptr_t)info.start;
    entry->end = (uintptr_t)info.end;
    entry->protection = (unsigned)info.protection;
    entry->flags = info.flags;
    entry->type = info.type;
    memcpy(entry->name, info.name, sizeof(info.name));
    entry->name[sizeof(info.name)] = 0;
    return 0;
}

int pw_wine_vm_entry(const void *address, PwWineVmEntry *entry)
{
    return pw_wine_vm_entry_impl(address, entry, NULL, NULL);
}

int pw_wine_vm_query(const void *address, void **start, void **end)
{
    PwWineVmEntry entry;
    int status;

    if (!start || !end) return -1;
    *start = *end = NULL;
    if ((status = pw_wine_vm_entry(address, &entry))) return status;
    *start = (void *)entry.start;
    *end = (void *)entry.end;
    return 0;
}

static int pw_wine_vm_foreign_impl(const void *address, size_t bytes, const PwWineVmEntry *like,
                                   PwWineVmEntry *found, int (*fill)(uintptr_t address, size_t bytes),
                                   int *failure_kind, int *kernel_status)
{
    const uintptr_t high = (uintptr_t)address + bytes;
    int asked_again = 0;

    if (failure_kind) *failure_kind = PW_WINE_VM_FAILURE_NONE;
    if (kernel_status) *kernel_status = 0;
    for (uintptr_t cursor = (uintptr_t)address; cursor < high;)
    {
        int entry_failure = PW_WINE_VM_FAILURE_NONE, entry_kernel_status = 0;
        const int status = pw_wine_vm_entry_impl((const void *)cursor, found, &entry_failure,
                                                  &entry_kernel_status);
        if (status < 0) {
            if (failure_kind) *failure_kind = entry_failure;
            if (kernel_status) *kernel_status = entry_kernel_status;
            return -1;
        }
        const uintptr_t hole_end = status || found->start >= high ? high : found->start > cursor ? found->start : cursor;
        if (fill && hole_end > cursor)
        {
            if (fill(cursor, hole_end - cursor))
            {
                if (asked_again++) {
                    if (failure_kind) *failure_kind = PW_WINE_VM_FAILURE_REFILL;
                    return -1;
                }
                continue;
            }
            asked_again = 0;
        }
        if (status || found->start >= high) return 0;
        if (found->protection != like->protection || found->flags != like->flags) return 1;
        cursor = found->end;
    }
    return 0;
}

int pw_wine_vm_foreign(const void *address, size_t bytes, const PwWineVmEntry *like, PwWineVmEntry *found,
                       int (*fill)(uintptr_t address, size_t bytes))
{
    return pw_wine_vm_foreign_impl(address, bytes, like, found, fill, NULL, NULL);
}

int pw_wine_vm_foreign_diagnostic(const void *address, size_t bytes, const PwWineVmEntry *like,
                                  PwWineVmEntry *found, int (*fill)(uintptr_t address, size_t bytes),
                                  int *failure_kind, int *kernel_status)
{
    return pw_wine_vm_foreign_impl(address, bytes, like, found, fill, failure_kind, kernel_status);
}

int pw_wine_vm_gap(const void *address, const void *limit, pw_wine_vm_owned_query owned, const PwWineVmEntry *like,
                   void **start, void **end)
{
    const uintptr_t base = (uintptr_t)address, high = (uintptr_t)limit;
    uintptr_t cursor = base;

    if (!owned || !start || !end || high <= base) return -1;
    *start = (void *)base;
    *end = (void *)high;
    while (cursor < high) {
        PwWineVmEntry entry;
        int status = pw_wine_vm_entry((void *)cursor, &entry);
        if (status == 1) return 0;
        if (status < 0) return -1;
        uintptr_t low = entry.start;
        uintptr_t top = entry.end < high ? entry.end : high;
        const int other = like && (entry.protection != like->protection || entry.flags != like->flags);
        /* Subtract Wine's reservations from the complete native interval,
         * including any reservation before the queried address. Otherwise
         * AllocationBase could incorrectly extend across Wine-owned space. */
        while (low < top) {
            void *owned_start = NULL, *owned_end = NULL;
            uintptr_t piece_end = top;
            status = other ? 1 : owned((void *)low, &owned_start, &owned_end);
            if (status < 0 || status > 1) return -1;
            if (!status) {
                if ((uintptr_t)owned_end <= low || (uintptr_t)owned_end <= (uintptr_t)owned_start) return -1;
                if ((uintptr_t)owned_start <= low) {
                    low = (uintptr_t)owned_end;
                    continue;
                }
                if ((uintptr_t)owned_start < piece_end) piece_end = (uintptr_t)owned_start;
            }
            if (piece_end > base) {
                if (low > base) { *end = (void *)low; return 0; }
                *start = (void *)low;
                *end = (void *)piece_end;
                return 1;
            }
            /* This piece precedes the query, so advance over its following
             * owned reservation. With no reservation top > base guarantees
             * that the preceding return was taken. */
            if (status) return -1;
            low = (uintptr_t)owned_end;
        }
        cursor = entry.end;
    }
    return 0;
}

int pw_wine_vm_step_slot(void *const slots[2], const void *page)
{
    if (slots[0] == page) return 0;
    if (slots[1] == page) return 1;
    return !slots[0] ? 0 : !slots[1] ? 1 : -1;
}
