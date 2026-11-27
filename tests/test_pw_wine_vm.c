/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../wine/ps5/pw_wine_vm.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

static int kernel_status;
static int transient_status;
static unsigned transient_left;
static uintptr_t kernel_start, kernel_end;
static uintptr_t owned_start, owned_end;
/* With entries set, the kernel's map as NEXT walks it (the entry holding the address or the next); otherwise the one
 * entry above, whatever is asked. Protection at +24, flags at +32, the name at +33 (src/pw_vm_census.h). */
typedef struct { uintptr_t start, end; int32_t protection; uint8_t flags; } FakeEntry;
static FakeEntry entries[8];
static unsigned entry_count, queries;
int sceKernelVirtualQuery(const void *address, int flags, void *record, size_t bytes)
{
    FakeEntry e = { kernel_start, kernel_end, 0, 0 };
    unsigned i = 0;
    assert(flags == 1 && bytes == 72);
    queries++;
    if (transient_left) {
        transient_left--;
        return transient_status;
    }
    if (entry_count) {
        while (i < entry_count && entries[i].end <= (uintptr_t)address) i++;
        if (i == entry_count) return (int)UINT32_C(0x8002000d);
        e = entries[i];
    }
    memset(record, 0xa5, bytes);
    memcpy(record, &e.start, sizeof(e.start));
    memcpy((char *)record + sizeof(void *), &e.end, sizeof(e.end));
    memcpy((char *)record + 24, &e.protection, sizeof(e.protection));
    memcpy((char *)record + 32, &e.flags, sizeof(e.flags));
    memcpy((char *)record + 33, "anon:0008004e9515", 18);
    return kernel_status;
}
/* The guard's refill of a hole: as the console's no-overwrite reservation, refused when something lies there now (a
 * mapping placed into the hole since the query, `racer`). */
static unsigned fills;
static uintptr_t filled_at, filled_bytes;
static FakeEntry racer;
static int fill(uintptr_t address, size_t bytes)
{
    unsigned i = 0;
    fills++;
    filled_at = address;
    filled_bytes = bytes;
    if (racer.end) {
        while (i < entry_count && entries[i].start < racer.start) i++;
        memmove(&entries[i + 1], &entries[i], (entry_count++ - i) * sizeof(entries[0]));
        entries[i] = racer;
        racer = (FakeEntry){ 0 };
        return -1;
    }
    if (bytes == 0x5000) return -1;   /* a hole no reservation takes, and nothing found there */
    while (i < entry_count && entries[i].start < address) i++;
    memmove(&entries[i + 1], &entries[i], (entry_count++ - i) * sizeof(entries[0]));
    entries[i] = (FakeEntry){ address, address + bytes, 0, 0 };
    return 0;
}
static int owned(const void *address, void **start, void **end)
{
    if ((uintptr_t)address >= owned_end) return 1;
    *start = (void *)owned_start;
    *end = (void *)owned_end;
    return 0;
}

int main(void)
{
    void *start, *end;
    kernel_start = 0x400000; kernel_end = 0x454000;
    assert(!pw_wine_vm_query((void *)0x10000, &start, &end));
    assert((uintptr_t)start == kernel_start && (uintptr_t)end == kernel_end);
    assert(!pw_wine_vm_query((void *)0x420000, &start, &end));
    assert(pw_wine_vm_query((void *)kernel_end, &start, &end) == -1);
    kernel_status = (int)UINT32_C(0x8002000d);
    assert(pw_wine_vm_query((void *)0x420000, &start, &end) == 1 && !start && !end);
    kernel_status = -2;
    assert(pw_wine_vm_query((void *)0x420000, &start, &end) == -1 && !start && !end);
    kernel_status = 0; kernel_start = kernel_end;
    assert(pw_wine_vm_query((void *)0x420000, &start, &end) == -1);
    assert(pw_wine_vm_query(NULL, NULL, &end) == -1);
    kernel_start = 0x400000; kernel_end = 0x454000;
    assert(!pw_wine_vm_gap((void *)0x10000, (void *)0x1000000, owned, NULL, &start, &end));
    assert((uintptr_t)end == 0x400000);
    assert(pw_wine_vm_gap((void *)0x420000, (void *)0x1000000, owned, NULL, &start, &end) == 1);
    assert((uintptr_t)start == 0x400000 && (uintptr_t)end == 0x454000);
    /* A kernel reservation owned by Wine is free for Windows to allocate. */
    owned_start = kernel_start; owned_end = kernel_end;
    kernel_status = (int)UINT32_C(0x8002000d);
    assert(!pw_wine_vm_gap((void *)owned_start, (void *)0x1000000, owned, NULL, &start, &end));
    assert((uintptr_t)start == owned_start && (uintptr_t)end == 0x1000000);
    /* The next mapping lies after an owned area: skip the reservation, then
     * report the native mapping, without shortening free space at its border. */
    kernel_status = 0; kernel_start = 0x600000; kernel_end = 0x610000;
    assert(!pw_wine_vm_gap((void *)0x10000, (void *)0x1000000, owned, NULL, &start, &end));
    assert((uintptr_t)end == kernel_start);
    /* One native interval can span Wine-owned and genuinely native space.
     * A query inside its owned middle is free up to the remaining native tail;
     * a query in the tail must not include the owned middle in AllocationBase. */
    kernel_start = 0x200000; kernel_end = 0x800000;
    owned_start = 0x400000; owned_end = 0x600000;
    assert(!pw_wine_vm_gap((void *)0x500000, (void *)0x1000000, owned, NULL, &start, &end));
    assert((uintptr_t)start == 0x500000 && (uintptr_t)end == 0x600000);
    assert(pw_wine_vm_gap((void *)0x700000, (void *)0x1000000, owned, NULL, &start, &end) == 1);
    assert((uintptr_t)start == 0x600000 && (uintptr_t)end == 0x800000);
    assert(pw_wine_vm_gap((void *)0x300000, (void *)0x1000000, owned, NULL, &start, &end) == 1);
    assert((uintptr_t)start == 0x200000 && (uintptr_t)end == 0x400000);
    /* Native map records change between queries; there is no stale view cache. */
    kernel_status = (int)UINT32_C(0x8002000d);
    assert(!pw_wine_vm_gap((void *)0x10000, (void *)0x1000000, owned, NULL, &start, &end));
    assert((uintptr_t)end == 0x1000000);
    kernel_status = -2;
    assert(pw_wine_vm_gap((void *)0x10000, (void *)0x1000000, owned, NULL, &start, &end) == -1);
    assert(pw_wine_vm_gap((void *)0x10000, (void *)0x10000, owned, NULL, &start, &end) == -1);
    /* The guard's walk (wine/ps5/pw_wine_dmem.h): like a bare reservation (protection 0, flags 0) or not. */
    {
        const PwWineVmEntry reservation = { 0, 0, 0, 0, 0, "" };
        PwWineVmEntry entry, found;
        kernel_status = 0;
        entries[0] = (FakeEntry){ 0x900000000, 0xc80000000, 0, 0 };
        entries[1] = (FakeEntry){ 0xc80000000, 0xc80010000, 0x33, 0x12 };
        entries[2] = (FakeEntry){ 0xc80010000, 0x1000000000, 0, 0 };
        entry_count = 3;
        assert(!pw_wine_vm_entry((void *)0xc80004000, &entry) && entry.start == 0xc80000000 &&
               entry.protection == 0x33 && entry.flags == 0x12 && !strcmp(entry.name, "anon:0008004e9515"));
        /* A reservation only: one query. */
        queries = 0;
        assert(!pw_wine_vm_foreign((void *)0x900000000, 0x100000000, &reservation, &found, NULL) && queries == 1);
        /* The wired entry inside the released span, found with its bounds. */
        assert(pw_wine_vm_foreign((void *)0x900000000, 0x700000000, &reservation, &found, NULL) == 1);
        assert(found.start == 0xc80000000 && found.end == 0xc80010000 && found.protection == 0x33);
        /* Protection 0 but not a reservation's flags (a stack's guard): foreign too. */
        entries[1] = (FakeEntry){ 0xc80000000, 0xc80010000, 0, 0x5 };
        assert(pw_wine_vm_foreign((void *)0xc00000000, 0x100000000, &reservation, &found, NULL) == 1 && found.flags == 0x5);
        /* A hole up to the end of the map, then no answer. */
        entry_count = 1;
        assert(!pw_wine_vm_foreign((void *)0xc80000000, 0x10000, &reservation, &found, NULL));
        entry_count = 3;
        kernel_status = -2;
        assert(pw_wine_vm_foreign((void *)0x900000000, 0x10000, &reservation, &found, NULL) == -1);
        kernel_status = 0;
        /* A hole inside the reservation (a mapping the kernel placed there and since freed) is reserved again before the
         * answer, so nothing lands in it between the query and a fixed mapping: one fill, then the walk goes on. */
        entries[0] = (FakeEntry){ 0x900000000, 0xa00000000, 0, 0 };
        entries[1] = (FakeEntry){ 0xa00100000, 0xb00000000, 0, 0 };
        entry_count = 2;
        fills = 0;
        assert(!pw_wine_vm_foreign((void *)0x900000000, 0x200000000, &reservation, &found, fill));
        assert(fills == 1 && filled_at == 0xa00000000 && filled_bytes == 0x100000 && entry_count == 3);
        assert(!pw_wine_vm_foreign((void *)0x900000000, 0x200000000, &reservation, &found, fill) && fills == 1);
        /* Without fill a hole counts as none, as before. */
        entries[1] = (FakeEntry){ 0xa00100000, 0xb00000000, 0, 0 };
        entry_count = 2;
        assert(!pw_wine_vm_foreign((void *)0x900000000, 0x200000000, &reservation, &found, NULL) && entry_count == 2);
        /* The race: the GPU client's next mapping takes the hole between the query and the refill. The refill is refused,
         * the hole asked about again, and the mapping found and never mapped over. */
        racer = (FakeEntry){ 0xa00000000, 0xa00010000, 0x33, 0x12 };
        assert(pw_wine_vm_foreign((void *)0x900000000, 0x200000000, &reservation, &found, fill) == 1);
        assert(found.start == 0xa00000000 && found.end == 0xa00010000 && found.protection == 0x33);
        /* A hole up to the end of the map is refilled too; one that no refill takes and stays empty: no answer. */
        entries[0] = (FakeEntry){ 0x900000000, 0xa00000000, 0, 0 };
        entry_count = 1;
        fills = 0;
        assert(!pw_wine_vm_foreign((void *)0x900000000, 0x100100000, &reservation, &found, fill));
        assert(fills == 1 && filled_at == 0xa00000000 && filled_bytes == 0x100000);
        entry_count = 1;
        assert(pw_wine_vm_foreign((void *)0x9ffffb000, 0xa000, &reservation, &found, fill) == -1 && entry_count == 1);
        /* VirtualQuery's gap with the signature: a wired entry inside Wine's reserved area is occupied, not free, and the
         * free space before it ends where it begins. Without the signature it read free (patch 0959's subtraction). */
        entries[0] = (FakeEntry){ 0x400000, 0x500000, 0, 0 };
        entries[1] = (FakeEntry){ 0x500000, 0x510000, 0x33, 0x12 };
        entries[2] = (FakeEntry){ 0x510000, 0x800000, 0, 0 };
        entry_count = 3;
        owned_start = 0x400000; owned_end = 0x800000;
        assert(!pw_wine_vm_gap((void *)0x500000, (void *)0x1000000, owned, NULL, &start, &end));
        assert((uintptr_t)start == 0x500000 && (uintptr_t)end == 0x1000000);
        assert(pw_wine_vm_gap((void *)0x504000, (void *)0x1000000, owned, &reservation, &start, &end) == 1);
        assert((uintptr_t)start == 0x500000 && (uintptr_t)end == 0x510000);
        assert(!pw_wine_vm_gap((void *)0x400000, (void *)0x1000000, owned, &reservation, &start, &end));
        assert((uintptr_t)start == 0x400000 && (uintptr_t)end == 0x500000);
        assert(!pw_wine_vm_gap((void *)0x510000, (void *)0x1000000, owned, &reservation, &start, &end));
        assert((uintptr_t)start == 0x510000 && (uintptr_t)end == 0x1000000);
        entry_count = 0;
    }
    /* Step slots: a re-fault on the page a slot holds reuses it; a third host page has none. */
    {
        void *slots[2] = { NULL, NULL };
        void *const p = (void *)0x10000, *const q = (void *)0x14000, *const r = (void *)0x18000;
        assert(pw_wine_vm_step_slot(slots, p) == 0);
        slots[0] = p;
        assert(pw_wine_vm_step_slot(slots, p) == 0);
        assert(pw_wine_vm_step_slot(slots, q) == 1);
        slots[1] = q;
        assert(pw_wine_vm_step_slot(slots, q) == 1 && pw_wine_vm_step_slot(slots, p) == 0);
        assert(pw_wine_vm_step_slot(slots, r) == -1);
    }
    /* Transient EINTR/EAGAIN retries and structured failure diagnostics (Issue #43). */
    {
        const PwWineVmEntry reservation = { 0, 0, 0, 0, 0, "" };
        PwWineVmEntry found;
        PwWineVmForeignDiag diag = {0};
        entries[0] = (FakeEntry){ 0x900000000, 0xa00000000, 0, 0 };
        entry_count = 1;
        kernel_status = 0;
        transient_status = (int)UINT32_C(0x80020004);
        transient_left = 2;
        assert(!pw_wine_vm_foreign_diag((void *)0x900000000, 0x10000, &reservation, &found, NULL, &diag));
        assert(transient_left == 0 && diag.kind == PW_WINE_VM_FAILURE_NONE);
        transient_status = (int)UINT32_C(0x80020023);
        transient_left = 3;
        assert(!pw_wine_vm_foreign_diag((void *)0x900000000, 0x10000, &reservation, &found, NULL, &diag));
        assert(transient_left == 0);
        /* Query failure diagnostic */
        kernel_status = (int)UINT32_C(0x80020016);
        assert(pw_wine_vm_foreign_diag((void *)0x900000000, 0x10000, &reservation, &found, NULL, &diag) == -1);
        assert(diag.kind == PW_WINE_VM_FAILURE_QUERY && (uint32_t)diag.kernel_status == UINT32_C(0x80020016) &&
               diag.cursor == 0x900000000 && !strcmp(pw_wine_vm_failure_name(diag.kind), "query"));
        /* Invalid/non-advancing entry diagnostic */
        kernel_status = 0;
        entry_count = 0;
        kernel_start = 0x900000000; kernel_end = 0x900000000;
        assert(pw_wine_vm_foreign_diag((void *)0x900000000, 0x10000, &reservation, &found, NULL, &diag) == -1);
        assert(diag.kind == PW_WINE_VM_FAILURE_ENTRY && diag.entry_start == 0x900000000 && diag.entry_end == 0x900000000);
        /* Refill failure diagnostic */
        entries[0] = (FakeEntry){ 0x900000000, 0xa00000000, 0, 0 };
        entry_count = 1;
        assert(pw_wine_vm_foreign_diag((void *)0x9ffffb000, 0xa000, &reservation, &found, fill, &diag) == -1);
        assert(diag.kind == PW_WINE_VM_FAILURE_REFILL && diag.entry_start == 0xa00000000 && diag.entry_end == 0xa00005000);
    }
    puts("pw_wine_vm: PASS");
}
