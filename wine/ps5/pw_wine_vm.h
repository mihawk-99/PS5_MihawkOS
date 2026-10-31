/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native address-space queries for ntdll's Windows-view gaps.
 * Return 0 with the containing or next host mapping, 1 at the end of the map,
 * or -1 for a malformed/refused query. Never allocate or change a mapping.
 * The 72-byte query record and NEXT=1 were checked against the console's
 * protection query on 2026-10-08 (native-vquery-2026-10-08-02).
 * Wine owns the distinction between its reservations and native mappings.
 */
#ifndef PW_WINE_VM_H
#define PW_WINE_VM_H
#include <stddef.h>
#include <stdint.h>
int pw_wine_vm_query(const void *address, void **start, void **end);
/* One kernel entry: the record's fields (src/pw_vm_census.h). A record names its mapping after the code that made it
 * ("anon:0008004e9515" is ntdll's own direct memory, "anon:00fc0075420f" the GPU client's, census 2026-10-14), so two
 * reservations Wine made at different call sites differ in name: only protection and flags tell a kind apart. */
typedef struct PwWineVmEntry
{
    uintptr_t start, end;
    unsigned protection, flags;
    int type;
    char name[33];
} PwWineVmEntry;
/* The entry holding address, or the next one: 0, 1 at the end of the map, -1 on a refused or malformed query. */
int pw_wine_vm_entry(const void *address, PwWineVmEntry *entry);
/* The first entry in [address, +bytes) whose protection or flags differ from like's (a bare reservation of Wine's):
 * 1 with it, 0 when there is none, -1 on a failed query. One query per entry walked. A hole (no entry) is where the
 * kernel places a mapping made without an address, so fill, when given, reserves it again (no-overwrite; 0 when done)
 * before the walk goes on, and nothing lands there between this answer and the caller's fixed mapping; a hole fill
 * cannot take is asked about once more (what took it is judged), then -1. Without fill, holes count as none. */
int pw_wine_vm_foreign(const void *address, size_t bytes, const PwWineVmEntry *like, PwWineVmEntry *found,
                       int (*fill)(uintptr_t address, size_t bytes));
/* Optional failure details for the foreign-mapping guard; does not change its fail-closed result. */
enum
{
    PW_WINE_VM_FAILURE_NONE = 0,
    PW_WINE_VM_FAILURE_QUERY = 1,
    PW_WINE_VM_FAILURE_ENTRY = 2,
    PW_WINE_VM_FAILURE_REFILL = 3,
};
int pw_wine_vm_foreign_diagnostic(const void *address, size_t bytes, const PwWineVmEntry *like,
                                  PwWineVmEntry *found, int (*fill)(uintptr_t address, size_t bytes),
                                  int *failure_kind, int *kernel_status);
/* Wine's sorted reservation list remains owned and locked by ntdll. The
 * callback returns its containing/next reservation (0), or end of list (1).
 * In a gap between Windows views, return 0 for free space up to the next
 * native mapping, 1 for an occupied native range, or -1 on invalid input/query.
 * Reservations owned by Wine are available to Windows and are skipped.
 */
typedef int (*pw_wine_vm_owned_query)(const void *, void **, void **);
/* like, when given, is a bare reservation of Wine's (the guard's signature): an entry with another protection or flags
 * is never subtracted, so another owner's mapping inside Wine's reserved areas reads as occupied, not free. */
int pw_wine_vm_gap(const void *address, const void *limit, pw_wine_vm_owned_query owned, const PwWineVmEntry *like,
                   void **start, void **end);

/* Host-page write tracking (Wine patch 1122): the console's host page is 16 KiB and holds four Windows pages. A write
 * a Windows page allows, refused because read-only pages share its host page, opens the host page for that one
 * instruction under the trap flag; the trap closes it. This step is the fallback: in a helper such a write runs
 * through the 4 KiB MMU first (patch 1134, wine/ps5/pw_wine_mmu4k.h, docs/MMU4K.md), which steps only what it cannot
 * run. Wine keeps the step's slots here, under its virtual mutex, in per-thread state (tests/test_pw_wine_vm.c). */
/* The slot of a thread's two step slots to open page in: the one already holding page (a re-fault after another
 * thread closed it while the instruction had not run yet), else the first free one, else -1 (the instruction
 * writes a third host page: refused, it gets the access violation). Two suffice for any instruction that writes one
 * operand of at most 4 KiB; rep string instructions trap after each element. */
int pw_wine_vm_step_slot(void *const slots[2], const void *page);
#endif
