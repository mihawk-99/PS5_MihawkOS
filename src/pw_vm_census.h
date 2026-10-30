/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_VM_CENSUS_H
#define PW_VM_CENSUS_H
/* Where a helper's system memory is: its mappings grouped by name and kind (B3, docs/PLAN.md).
 *
 * user-desktop-2026-10-10-02: a launcher's GPU process held 46, then 376, then 46, then 325 MiB of resident system memory
 * (sceKernelGetResidentFmemCount) while Wine's direct memory (523 MiB), Wine's Unix heap (71 MiB) and its mapped DLL
 * images (302 MiB, the same in every launcher helper that stayed near 50 MiB) did not move. No counter named the memory,
 * so the next step is the one that names it: a walk of the process's mappings (sceKernelVirtualQuery with NEXT),
 * grouped by the mapping's name and flags, largest first.
 *
 * The record, 72 bytes (libkernel; the public reimplementations agree, and pw_wine_vm.c reads its first two fields):
 *   void *start, *end; u64 offset; s32 protection; s32 memory type; u8 flags (bit 0 flexible, 1 direct, 2 stack,
 *   3 pooled, 4 committed); char name[32]; padding
 * The helper logs the first record in hex, so a layout the firmware does not share shows in the first run. */
#include <stddef.h>
#include <stdint.h>

enum {
    PW_VM_RECORD = 72,
    PW_VM_FLEXIBLE = 1u, PW_VM_DIRECT = 2u, PW_VM_STACK = 4u, PW_VM_POOLED = 8u, PW_VM_COMMITTED = 16u,
    PW_VM_NAME = 32, PW_VM_GROUPS = 48, PW_VM_BIG = 8, PW_VM_FLEXBIG = 8, PW_VM_WINDOWS = 32,
    PW_VM_PROTS = 4, PW_VM_SIZES = 4,   /* flexible_kind: r / rw / rx / other by <64 KiB / <1 MiB / <8 MiB / more */
    PW_VM_CLASS_FLEXIBLE = 0, PW_VM_CLASS_DIRECT = 1, PW_VM_CLASS_RESERVED = 2, PW_VM_CLASSES = 3,   /* a mapping's class, below */
    PW_VM_WALK_LIMIT = 65536,     /* mappings walked at most; a walk that reaches it says so */
};

/* One record's fields. */
typedef struct PwVmRecord {
    uintptr_t start, end;
    int32_t protection, type;
    uint8_t flags;
    char name[PW_VM_NAME + 1];
} PwVmRecord;

/* Mappings of one name and flags. */
typedef struct PwVmGroup {
    char name[PW_VM_NAME + 1];
    uint8_t flags;
    int32_t protection;          /* the first mapping's */
    uint32_t mappings;
    uint64_t bytes;
    uint64_t resident;           /* resident bytes, when the walk had a resident callback */
    uint64_t regions_2m;         /* the 2 MiB regions its accessible mappings brought first (PwVmCensus.regions_2m) */
    uint64_t regions_2m_direct;  /* of them, its 2 MiB-aligned direct ones (PwVmCensus.regions_2m_direct) */
} PwVmGroup;

/* One of the largest mappings, whole: the kernel's own map (tools/console-memory.py map=PID) lists the same ranges by
 * address, so the two are matched there (2026-10-11, record removed from the tree: 8 regions, 228 MiB, fully resident in the
 * kernel's count and absent from this walk's residency). */
typedef struct PwVmBig {
    uintptr_t start;
    uint64_t bytes;
    int32_t protection;
    uint8_t flags;
    char name[PW_VM_NAME + 1];
} PwVmBig;

/* The flexible mappings of one protection and size class: what the counted memory is made of (flexible memory is what
 * the census counts), when the group names do not say. */
typedef struct PwVmKind {
    uint32_t mappings;
    uint64_t bytes, resident;
} PwVmKind;

typedef struct PwVmCensus {
    PwVmGroup groups[PW_VM_GROUPS];
    PwVmBig big[PW_VM_BIG];      /* the largest accessible mappings, largest first */
    unsigned big_count;
    PwVmBig flexbig[PW_VM_FLEXBIG]; /* the largest accessible flexible mappings, largest first */
    unsigned flexbig_count;
    PwVmKind flexible_kind[PW_VM_PROTS][PW_VM_SIZES];
    unsigned count;              /* groups kept */
    unsigned mappings;           /* mappings walked */
    unsigned class_mappings[PW_VM_CLASSES]; /* of them, by class (flexible, direct, reserved) */
    uint64_t class_bytes[PW_VM_CLASSES];
    uint64_t flexible, direct, other; /* bytes, by kind (flexible wins when both bits are set) */
    uint64_t resident;           /* resident bytes of the accessible mappings that are not direct memory */
    uint64_t reserved_resident;  /* resident bytes of the inaccessible mappings (protection 0), asked from 2026-10-11 */
    uint64_t direct_resident;    /* resident bytes of the accessible direct mappings (asked from 2026-10-11) */
    uint64_t unkept;             /* bytes in groups that did not fit */
    uint64_t regions_2m_direct;  /* of regions_2m, those of direct mappings aligned to 2 MiB at both ends: in a game process each
                                  * still takes a page-table page (2026-10-29, the Pro, a 3D game: the pool fell by about the
                                  * census plus these), so they are counted in regions_2m and shown apart */
    uint64_t regions_2m;         /* distinct 2 MiB regions that accessible mappings touch: each holds a page-table page once a
                                  * page in it is used (the ceiling test: 8 page tables per 16 MiB filled), so about what the
                                  * process takes of the system's pool (docs/PLAN.md "Memory plan", Part 3) */
    struct { uint32_t gib, regions; uint64_t bytes; } windows[PW_VM_WINDOWS]; /* regions_2m and accessible bytes by the 1 GiB
                                                                            * window a mapping starts in */
    unsigned window_count;
    uint64_t window_other;       /* regions in windows past the PW_VM_WINDOWS kept */
    int truncated;               /* the walk stopped at PW_VM_WALK_LIMIT */
    int failed;                  /* the walk stopped at a query that failed */
} PwVmCensus;

/* The next mapping at or after address into raw (PW_VM_RECORD bytes): 0, 1 when there is none, < 0 on failure. */
typedef int (*PwVmQuery)(void *context, uintptr_t address, uint8_t raw[PW_VM_RECORD]);
/* Resident bytes in [start, end) (mincore on the console); called for each mapping,
 * direct memory and reservations too, whose pages are counted apart. NULL: not counted. */
typedef uint64_t (*PwVmResident)(void *context, uintptr_t start, uintptr_t end, unsigned klass);
/* The class of a mapping, passed to the residency callback so each class can have its own time budget: a walk in address order
 * with one budget spent its 100 ms on the PROT_NONE reservations (12,000 of 18,787 mappings) and measured 3,133 mappings of a
 * browser helper (2026-10-24, docs/PLAN.md): the flexible ones, which are what the console counts, came after it. */
/* WINE_PS5_CENSUS_MS: "N" is one budget shared by every class (the behaviour before), "F:D:R" a budget in ms for each class, flexible,
 * direct, reserved. Returns 1 for the per-class form, 0 for one number, -1 for text it does not read (ms keeps 100 for each). */
int pw_vm_budget_parse(const char *text, long long ms[PW_VM_CLASSES]);

#ifdef __cplusplus
extern "C" {
#endif
/* A record's fields from its bytes (the name always terminated). */
void pw_vm_record_decode(const uint8_t raw[PW_VM_RECORD], PwVmRecord *out);
/* Walks every mapping through query and groups them; groups sorted by resident bytes, then mapped bytes, largest
 * first. */
void pw_vm_census_walk(PwVmCensus *census, PwVmQuery query, PwVmResident resident, void *context);
/* "start+MiB:flags/protection:name ..." for the largest mappings (what PwVmCensus.big holds), into out. */
size_t pw_vm_census_format_big(const PwVmCensus *census, char *out, size_t size);
/* "start+MiB:flags/protection:name ..." for the largest flexible mappings (PwVmCensus.flexbig), into out. */
size_t pw_vm_census_format_flexbig(const PwVmCensus *census, char *out, size_t size);
/* "prot.size=mappings/mapped MiB/resident MiB ..." for the flexible mappings by protection (r rw rx o) and size
 * (s <64 KiB, m <1 MiB, l <8 MiB, x more), the classes that hold any, MiB to a tenth, into out. */
size_t pw_vm_census_format_flexible(const PwVmCensus *census, char *out, size_t size);
/* "name:flags=regions/mappings ..." for the n groups that brought the most 2 MiB regions, most first, into out. */
size_t pw_vm_census_format_regions(const PwVmCensus *census, unsigned n, char *out, size_t size);
/* "0xGIB=regions/MiB ..." for the n 1 GiB windows of the address space that brought the most 2 MiB regions, most first, then
 * "+N" for the rest, into out: whether a process's page tables come from a few dense areas or scattered ones. */
size_t pw_vm_census_format_windows(const PwVmCensus *census, unsigned n, char *out, size_t size);
/* "name:flags/protection=resident/mapped MiB/mappings ..." for the n first groups with flags & mask (0: every group),
 * into out. */
size_t pw_vm_census_format(const PwVmCensus *census, unsigned n, uint8_t mask, char *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
