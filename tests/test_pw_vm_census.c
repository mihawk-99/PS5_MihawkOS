/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Where a helper's system memory is (src/pw_vm_census.h): the record layout, the walk, grouping by name and flags,
 * residency, ordering and the log line, over a fake address space. */
#include "../src/pw_vm_census.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Fake {
    uint64_t start, end;
    int32_t protection;
    uint8_t flags;
    const char *name;
    uint64_t resident;
} Fake;

typedef struct Space {
    const Fake *maps;
    unsigned count;
    int fail_at;          /* query index that fails, -1 none */
    unsigned queries;
} Space;

enum { MIB = 1 << 20 };

static void encode(const Fake *f, uint8_t raw[PW_VM_RECORD])
{
    memset(raw, 0, PW_VM_RECORD);
    memcpy(raw, &f->start, 8);
    memcpy(raw + 8, &f->end, 8);
    memcpy(raw + 24, &f->protection, 4);
    raw[32] = f->flags;
    memcpy(raw + 33, f->name, strlen(f->name) < PW_VM_NAME ? strlen(f->name) : PW_VM_NAME);
}

static int query(void *context, uintptr_t address, uint8_t raw[PW_VM_RECORD])
{
    Space *space = context;
    if ((int)space->queries++ == space->fail_at) return -1;
    for (unsigned i = 0; i < space->count; i++)
        if (space->maps[i].end > address) { encode(&space->maps[i], raw); return 0; }
    return 1;
}

static unsigned classes_seen[PW_VM_CLASSES];

static uint64_t resident(void *context, uintptr_t start, uintptr_t end, unsigned klass)
{
    Space *space = context;
    classes_seen[klass]++;
    for (unsigned i = 0; i < space->count; i++)
        if (space->maps[i].start == start && space->maps[i].end == end) return space->maps[i].resident;
    return 0;
}

int main(void)
{
    /* The budget setting: one number is shared, F:D:R is per class, anything else is refused with 100 left. */
    long long ms[PW_VM_CLASSES];
    assert(pw_vm_budget_parse("250", ms) == 0 && ms[0] == 250 && ms[1] == 250 && ms[2] == 250);
    assert(pw_vm_budget_parse("400:100:300", ms) == 1 && ms[0] == 400 && ms[1] == 100 && ms[2] == 300);
    assert(pw_vm_budget_parse("0", ms) == 0 && ms[0] == 0);
    assert(pw_vm_budget_parse("0:0:500", ms) == 1 && ms[0] == 0 && ms[2] == 500);
    assert(pw_vm_budget_parse(NULL, ms) == -1 && ms[1] == 100);
    assert(pw_vm_budget_parse("", ms) == -1 && pw_vm_budget_parse("1:2", ms) == -1 && pw_vm_budget_parse("a", ms) == -1);
    assert(pw_vm_budget_parse("1:2:3:4", ms) == -1 && pw_vm_budget_parse("-5", ms) == -1 && pw_vm_budget_parse("99999", ms) == -1);

    /* A GPU process: Chromium's heap pages, two libc heap arenas, Wine's direct memory, a reservation, a stack. */
    const Fake maps[] = {
        {0x100000, 0x100000 + 64ull * MIB, 3, PW_VM_FLEXIBLE, "libc heap", 40ull * MIB},
        {0x200000000, 0x200000000 + 512ull * MIB, 3, PW_VM_FLEXIBLE | PW_VM_COMMITTED, "anon", 380ull * MIB},
        {0x300000000, 0x300000000 + 64ull * MIB, 3, PW_VM_FLEXIBLE, "libc heap", 20ull * MIB},
        {0x400000000, 0x400000000 + 600ull * MIB, 3, PW_VM_DIRECT, "wine", 600ull * MIB},
        {0x800000000, 0x800000000 + 10000ull * MIB, 0, 0, "reserved", 0},
        {0x2000000000, 0x2000000000 + 1ull * MIB, 3, PW_VM_FLEXIBLE | PW_VM_STACK, "stack", 1ull * MIB},
    };
    Space space = {maps, sizeof(maps) / sizeof(maps[0]), -1, 0};
    PwVmCensus census;

    /* The layout: fields at their offsets, the name terminated and printable. */
    uint8_t raw[PW_VM_RECORD];
    PwVmRecord record;
    Fake odd = {0x1000, 0x2000, 7, PW_VM_FLEXIBLE, "0123456789abcdef0123456789abcdefXYZ", 0};
    encode(&odd, raw);
    raw[34] = 0x01;
    pw_vm_record_decode(raw, &record);
    assert(record.start == 0x1000 && record.end == 0x2000 && record.protection == 7 && record.flags == PW_VM_FLEXIBLE);
    assert(strlen(record.name) == PW_VM_NAME && record.name[1] == '?' && record.name[0] == '0');

    /* The walk: every mapping, grouped, by kind; the reservation is not asked for residency, direct memory is
     * counted apart. */
    pw_vm_census_walk(&census, query, resident, &space);
    /* Every mapping is asked in its class: flexible, direct, and the protection-0 reservations apart. */
    assert(classes_seen[PW_VM_CLASS_FLEXIBLE] + classes_seen[PW_VM_CLASS_DIRECT] + classes_seen[PW_VM_CLASS_RESERVED] == census.mappings);
    assert(census.class_mappings[PW_VM_CLASS_FLEXIBLE] + census.class_mappings[PW_VM_CLASS_DIRECT] +
           census.class_mappings[PW_VM_CLASS_RESERVED] == census.mappings && census.class_mappings[PW_VM_CLASS_FLEXIBLE] >= 3);
    assert(census.mappings == 6 && !census.truncated && !census.failed && census.count == 5);
    assert(census.regions_2m > 0);
    {
        /* The page-table estimate: two mappings in one 2 MiB region count once, 12 MiB from 4 MiB six, a reservation
         * none, one page far away one more: 8. */
        static const Fake regions[] = {
            {0x200000, 0x201000, 3, 1, "a", 0}, {0x201000, 0x400000, 3, 1, "b", 0}, {0x400000, 0x1000000, 3, 1, "c", 0},
            {0x1000000, 0x80000000ull, 0, 0, "reserved", 0}, {0x80000000ull, 0x80001000ull, 3, 1, "far", 0},
        };
        Space space = {regions, 5, -1, 0};
        PwVmCensus counted;
        pw_vm_census_walk(&counted, query, NULL, &space);
        assert(counted.mappings == 5 && counted.regions_2m == 8);
        char text[256];
        pw_vm_census_format_regions(&counted, 8, text, sizeof(text));
        assert(!strcmp(text, "c:01=6/1 a:01=1/1 far:01=1/1"));
        /* By 1 GiB window, regions/MiB: 7 for 14 MiB in the first, 1 for a page at 2 GiB; one window shown leaves the other. */
        pw_vm_census_format_windows(&counted, 8, text, sizeof(text));
        assert(!strcmp(text, "0x0=7/14 0x2=1/1"));
        pw_vm_census_format_windows(&counted, 1, text, sizeof(text));
        assert(!strcmp(text, "0x0=7/14 +1"));
        /* A direct mapping aligned to 2 MiB at both ends still costs its regions, and is shown apart. */
        static const Fake direct[] = {
            {0x40000000ull, 0x40800000ull, 3, 2, "dmem", 0}, {0x40900000ull, 0x40a00000ull, 3, 2, "dmem-small", 0},
        };
        Space space2 = {direct, 2, -1, 0};
        pw_vm_census_walk(&counted, query, NULL, &space2);
        assert(counted.regions_2m_direct == 4 && counted.regions_2m == 5);
        pw_vm_census_format_regions(&counted, 8, text, sizeof(text));
        assert(!strcmp(text, "dmem:02=4 (2m-direct 4)/1 dmem-small:02=1/1"));   /* by page-table cost first */
    }
    assert(census.flexible == (64 + 512 + 64 + 1) * (uint64_t)MIB && census.direct == 600ull * MIB);
    assert(census.other == 10000ull * MIB);
    assert(census.resident == (40 + 380 + 20 + 1) * (uint64_t)MIB);
    assert(census.direct_resident == 600ull * MIB && !census.reserved_resident);
    /* Largest resident first: Wine's direct memory, the anonymous pages, then the two heap arenas together, then
     * the stack. */
    assert(!strcmp(census.groups[0].name, "wine") && census.groups[0].resident == 600ull * MIB);
    assert(!strcmp(census.groups[1].name, "anon") && census.groups[1].resident == 380ull * MIB);
    assert(!strcmp(census.groups[2].name, "libc heap") && census.groups[2].mappings == 2 &&
           census.groups[2].resident == 60ull * MIB && census.groups[2].bytes == 128ull * MIB);
    assert(!strcmp(census.groups[3].name, "stack"));

    /* The largest accessible mappings, largest first: the reservation (no access) is not one. */
    assert(census.big_count == 5 && census.big[0].start == 0x400000000 && census.big[0].bytes == 600ull * MIB);
    assert(census.big[1].start == 0x200000000 && census.big[4].start == 0x2000000000);
    char line[512];
    pw_vm_census_format_big(&census, line, sizeof(line));
    assert(!strncmp(line, "400000000+600MiB:02/3:wine 200000000+512MiB:11/3:anon ", 53));
    pw_vm_census_format(&census, 2, PW_VM_FLEXIBLE, line, sizeof(line));
    assert(!strcmp(line, "anon:11/3=380/512MiB/1 libc heap:01/3=60/128MiB/2"));
    pw_vm_census_format(&census, 9, PW_VM_DIRECT, line, sizeof(line));
    assert(!strcmp(line, "wine:02/3=600/600MiB/1"));
    assert(pw_vm_census_format(&census, 9, 0, line, 10) < 10 && strlen(line) < 10);

    /* The flexible mappings by protection and size: the heap arenas and the anonymous pages are read-write and
     * large (x: 8 MiB or more, two arenas of 64 MiB), the stack is read-write and l; direct memory and the reservation
     * are not flexible. */
    assert(census.flexible_kind[1][3].mappings == 3 && census.flexible_kind[1][3].bytes == (64 + 512 + 64) * (uint64_t)MIB);
    assert(census.flexible_kind[1][3].resident == (40 + 380 + 20) * (uint64_t)MIB);
    assert(census.flexible_kind[1][2].mappings == 1 && census.flexible_kind[1][2].bytes == 1ull * MIB);
    assert(!census.flexible_kind[0][0].mappings && !census.flexible_kind[2][0].mappings);
    pw_vm_census_format_flexible(&census, line, sizeof(line));
    assert(!strcmp(line, "rw.l=1/1.0/1.0 rw.x=3/640.0/440.0"));
    assert(census.flexbig_count == 4 && census.flexbig[0].start == 0x200000000 && census.flexbig[3].start == 0x2000000000);
    pw_vm_census_format_flexbig(&census, line, sizeof(line));
    assert(!strncmp(line, "200000000+512MiB:11/3:anon 100000+64MiB:01/3:libc heap ", 55));
    assert(pw_vm_census_format_flexible(&census, line, 8) < 8);

    /* A read-only 16 KiB mapping and a 2 MiB one with execute, residency tenths. */
    {
        const Fake small[] = {
            {0x1000, 0x1000 + 16384, 1, PW_VM_FLEXIBLE, "jit", 16384},
            {0x400000, 0x400000 + 2ull * MIB, 5, PW_VM_FLEXIBLE, "jit", 1ull * MIB + 512 * 1024},
        };
        Space tiny = {small, 2, -1, 0};
        pw_vm_census_walk(&census, query, resident, &tiny);
        pw_vm_census_format_flexible(&census, line, sizeof(line));
        assert(!strcmp(line, "r.s=1/0.0/0.0 rx.l=1/2.0/1.5"));
    }

    /* Without residency: ordered by mapped size. */
    space.queries = 0;
    pw_vm_census_walk(&census, query, NULL, &space);
    assert(!strcmp(census.groups[0].name, "reserved") && !census.resident);

    /* A failing query stops the walk and says so. */
    space.queries = 0;
    space.fail_at = 2;
    pw_vm_census_walk(&census, query, resident, &space);
    assert(census.failed && census.mappings == 2);
    puts("vm census: record layout, walk, groups by name and flags, residency, order, log line, flexible kinds, failure");
    return 0;
}
