/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_CODESHARE_H
#define PW_WINE_CODESHARE_H
#include <stddef.h>
#include <stdint.h>

/* Shared code copies (docs/PLAN.md, "Shared code copies"). A payload's kernel keeps execute only on JIT shared memory,
 * so patch 1127 copies each code cluster a helper runs into a JIT object charged to that helper's flexible memory, and
 * every helper pays again for the same DLL: CEF 150's browser, renderer and StorageService held 214 MiB at 16 KiB
 * clusters, 128 MiB of it copied code, mostly the same libcef.dll pages (evidence/cluster-ab-2026-10-16-ab.json). Here a
 * cluster of an image's code is copied once, and every other helper whose cluster has the same bytes maps that object.
 *
 * Keyed by content: the cluster's size and a 64-bit hash of its bytes, never its address or its file. A DLL loaded at the
 * same base has the same bytes in every helper; one loaded elsewhere differs only in the pages its relocations touch,
 * and its other pages share too. A helper given an object compares its bytes with its own before it maps it
 * (pw_wine_dmem_ps5.c), so a collision or a patched page costs a private copy, never wrong code.
 *
 * Not measured yet (docs/CONSOLE_QUEUE.md, "Shared code copies", K11-K17): whether the kernel passes a JIT object's
 * descriptor over a socket at all, whether a second process may map it executable, and whose resident flexible memory
 * then counts it (once, or once per process mapping it). Until K11-K17 pass, this saves nothing proven. */

static inline uint64_t pw_wine_codeshare_hash(const void *bytes, size_t size)
{
    const uint64_t *word = bytes;
    uint64_t hash = 0xcbf29ce484222325u ^ size;

    for (size_t i = 0; i < size / 8; i++) hash = (hash ^ word[i]) * 0x100000001b3u;
    hash ^= hash >> 33;   /* FNV-1a's high bits never reach its low ones: a finalizer mixes them */
    hash *= 0xff51afd7ed558ccdu;
    return hash ^ (hash >> 33);
}

/* The title's registry: the JIT objects helpers gave it (PW_CHILD_CODE_OFFER), each held until no helper that gave or
 * took it lives. A holder is the helper's slot in the title's child table (pw_wine_process.c, 32 slots), forgotten once
 * that helper has ended. ENTRIES bounds the descriptors the title holds: they share its descriptor table, 12,555 slots
 * whatever getrlimit's 13,952 says, with the in-process server's files and their parked placeholders. At 8,192 the
 * store client's helpers filled the registry in 20 s, its download's files took the rest, and a CreateFile failed with
 * "too many open files" (a tester's store-client download, 2026-10-31: 12,302 of 12,555). So 2,048, and a full table takes entries
 * back (pw_wine_codeshare_release): a lost entry only costs a helper a private copy later. CEF's three processes copied
 * 128,256 KiB at 16 KiB clusters (the cluster A/B), about 8,000 clusters, most of them the same ones. One mutex;
 * nothing under it waits but dup and close.
 * shortcut: an object stays until every helper that took it ends, also when one unloaded the DLL; drop per unmap if a
 * long-lived helper is seen loading and unloading large images. */
enum { PW_WINE_CODESHARE_ENTRIES = 2048, PW_WINE_CODESHARE_HOLDERS = 32 };

typedef struct PwWineCodeshareStats {
    uint64_t entries, bytes;             /* objects held now and their size */
    uint64_t finds, hits;                /* asks, and asks answered with an object */
    uint64_t offers, kept, closed;       /* objects given, kept, and closed when their last holder ended */
    uint64_t released;                   /* of those closed, taken back for a full descriptor table */
} PwWineCodeshareStats;

/* The object of a cluster of bytes whose content hashes to hash, for holder: 0 with *fd a new descriptor of it (the
 * caller's to close) and holder recorded, or -1 with errno ENOENT (none kept), EINVAL, or dup's error. */
int pw_wine_codeshare_find(uint64_t hash, uint64_t bytes, unsigned holder, int *fd);
/* Holder made fd, the JIT object of such a cluster: 0 kept, or -1 with errno EEXIST (one is kept already), ENOSPC
 * (ENTRIES are kept), EINVAL. fd becomes the registry's either way: it is closed when not kept. */
int pw_wine_codeshare_offer(uint64_t hash, uint64_t bytes, unsigned holder, int fd);
/* A file cluster's object, keyed by the file identity and the raw offset instead of the content: the first holder's
 * read is the only one. */
int pw_wine_codeshare_find_file(const uint64_t file[3], uint64_t bytes, unsigned holder, int *fd);
int pw_wine_codeshare_offer_file(const uint64_t file[3], uint64_t bytes, unsigned holder, int fd);
/* Holder has ended: it holds nothing now, and each object no live holder gave or took is closed. Returns how many. */
unsigned pw_wine_codeshare_forget(unsigned holder);
/* The descriptor table is full (EMFILE): close up to count entries, whoever holds them, so a file can open. A helper
 * that already mapped an object keeps its mapping; one that asks later makes a private copy. Returns how many. */
unsigned pw_wine_codeshare_release(unsigned count);
void pw_wine_codeshare_stats(PwWineCodeshareStats *stats);
/* Everything closed and forgotten (tests). */
void pw_wine_codeshare_reset(void);

/* A helper's shared ranges: where it maps an object the registry holds or may hand out (one it took, or one it made and
 * gave), so a protection change that adds write first gives that range a private copy (pw_wine_dmem_ps5.c). Sorted,
 * disjoint, touching ranges merged; no heap, no lock (the caller's). A range that cannot be split when part of it leaves
 * stays whole: a range marked that is no longer shared costs a needless private copy, never a write into a shared
 * object. RANGES bounds it at 128 KiB of the helper's memory; CEF's browser process made 4,451 conversions at 16 KiB,
 * many of them touching. */
enum { PW_WINE_CODESHARE_RANGES = 8192 };
typedef struct PwWineCodeshareRanges {
    unsigned count;
    struct { uintptr_t start, end; } range[PW_WINE_CODESHARE_RANGES];
} PwWineCodeshareRanges;

/* [start, end) marked: 0, or -1 with errno ENOSPC (full: nothing marked, so the caller must not share it) or EINVAL. */
int pw_wine_codeshare_mark(PwWineCodeshareRanges *set, uintptr_t start, uintptr_t end);
/* [start, end) no longer marked, where a split is possible (see above). */
void pw_wine_codeshare_unmark(PwWineCodeshareRanges *set, uintptr_t start, uintptr_t end);
/* The lowest marked piece of [start, end): 1 with [*low, *high), or 0. */
int pw_wine_codeshare_marked(const PwWineCodeshareRanges *set, uintptr_t start, uintptr_t end, uintptr_t *low,
                             uintptr_t *high);

/* Counting each shared object once in the census (docs/PLAN.md, K19).
 *
 * The census sums each process's resident flexible memory, which counts a shared object in full in every process that
 * maps it (K14). The standard PS5's kernel acts on physical pages, not on that sum (K19: 1,536 MiB of shared mappings
 * of one 64 MiB object took nothing from any process; 640 MiB of distinct objects did), so on it the census counts
 * shared code once: it subtracts sum(mappings of each object) - sum(objects), the copies beyond the first.
 *
 * A helper's shared pages: each object of the registry it maps now (taken, or made and kept), by start and size.
 * Exact, unlike the ranges above: an entry leaves with any overlap of a range that is unmapped, replaced or made
 * private. Each helper reports the total; the title subtracts the registry's bytes from the sum of the reports. Every
 * object a live helper maps is in the registry, so that difference is never more than the true copies beyond the
 * first: the census can stay too high, never too low. Full (PAGES, 128 KiB of the helper's memory at 16 B a page;
 * CEF's renderer converted 3,590 pages at 16 KiB clusters): a new one is not counted, which errs the same way. */
enum { PW_WINE_CODESHARE_PAGES = 8192 };
typedef struct PwWineCodesharePages {
    unsigned count;
    uint64_t bytes;   /* the pages' total, what the helper reports */
    struct { uintptr_t start; uint64_t bytes; } page[PW_WINE_CODESHARE_PAGES];
} PwWineCodesharePages;

/* [start, +bytes) counted, after anything it overlaps leaves: 0, or -1 with errno ENOSPC (full: not counted) or
 * EINVAL. No lock (the caller's). */
int pw_wine_codeshare_pages_add(PwWineCodesharePages *set, uintptr_t start, uint64_t bytes);
/* Every entry overlapping [start, end) leaves: the bytes that left. */
uint64_t pw_wine_codeshare_pages_drop(PwWineCodesharePages *set, uintptr_t start, uintptr_t end);
/* The title's side: mapped_bytes, the sum of the live helpers' reports, less the registry's objects: the census's copies
 * beyond the first of each object, 0 at least. */
uint64_t pw_wine_codeshare_duplicates(uint64_t mapped_bytes);

#endif
