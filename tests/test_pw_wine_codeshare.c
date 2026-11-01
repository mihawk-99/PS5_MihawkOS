/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shared code copies (wine/ps5/pw_wine_codeshare.h): the content hash, the title's registry (keeping, handing out and
 * closing descriptors; removal from its table against a brute-force model, with collisions forced into runs that wrap)
 * and a helper's shared ranges (against a page bitmap, and at capacity). */
#include "../wine/ps5/pw_wine_codeshare.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static int is_open(int fd) { return fcntl(fd, F_GETFD) != -1; }
static int null_fd(void)
{
    const int fd = open("/dev/null", O_RDONLY);
    assert(fd >= 0);
    return fd;
}

static void hash(void)
{
    static uint64_t a[2048], b[2048];
    for (unsigned i = 0; i < 2048; i++) a[i] = b[i] = i * 0x9e3779b97f4a7c15u;
    assert(pw_wine_codeshare_hash(a, sizeof(a)) == pw_wine_codeshare_hash(b, sizeof(b)));
    ((unsigned char *)b)[16383] ^= 0x80;   /* the last byte's top bit */
    assert(pw_wine_codeshare_hash(a, sizeof(a)) != pw_wine_codeshare_hash(b, sizeof(b)));
    assert(pw_wine_codeshare_hash(a, 8192) != pw_wine_codeshare_hash(a, 16384));
    memset(a, 0, sizeof(a));
    assert(pw_wine_codeshare_hash(a, 8192) != pw_wine_codeshare_hash(a, 16384));   /* the size is in it */
}

static void registry(void)
{
    PwWineCodeshareStats stats;
    int got = -1, kept = null_fd(), other = null_fd();

    pw_wine_codeshare_reset();
    assert(pw_wine_codeshare_find(7, 16384, 0, &got) == -1 && errno == ENOENT);
    assert(!pw_wine_codeshare_offer(7, 16384, 0, kept));
    /* A second object of the same content is refused and closed; the first stays. */
    assert(pw_wine_codeshare_offer(7, 16384, 1, other) == -1 && errno == EEXIST && !is_open(other));
    /* Same hash, another size: another cluster. */
    assert(pw_wine_codeshare_find(7, 32768, 1, &got) == -1 && errno == ENOENT);
    assert(!pw_wine_codeshare_find(7, 16384, 1, &got) && got != kept && is_open(got));
    close(got);
    assert(pw_wine_codeshare_find(7, 16384, 32, &got) == -1 && errno == EINVAL);
    assert(pw_wine_codeshare_offer(8, 16384, 32, null_fd()) == -1 && errno == EINVAL);
    assert(pw_wine_codeshare_offer(8, 0, 0, null_fd()) == -1 && errno == EINVAL);
    /* Holder 0 gave it, holder 1 took it: it stays until both have ended. A holder that held nothing changes nothing. */
    assert(!pw_wine_codeshare_forget(5));
    assert(!pw_wine_codeshare_forget(0) && is_open(kept));
    assert(pw_wine_codeshare_forget(1) == 1 && !is_open(kept));
    assert(pw_wine_codeshare_find(7, 16384, 2, &got) == -1 && errno == ENOENT);
    pw_wine_codeshare_stats(&stats);
    assert(!stats.entries && !stats.bytes && stats.finds == 4 && stats.hits == 1 && stats.offers == 4 &&
           stats.kept == 1 && stats.closed == 1);

    /* Full: the next is refused and closed. */
    pw_wine_codeshare_reset();
    const int base = null_fd();
    for (unsigned i = 0; i < PW_WINE_CODESHARE_ENTRIES; i++) assert(!pw_wine_codeshare_offer(i + 1, 16384, i % 32, dup(base)));
    other = dup(base);
    assert(pw_wine_codeshare_offer(0, 16384, 0, other) == -1 && errno == ENOSPC && !is_open(other));
    pw_wine_codeshare_stats(&stats);
    assert(stats.entries == PW_WINE_CODESHARE_ENTRIES && stats.bytes == (uint64_t)PW_WINE_CODESHARE_ENTRIES * 16384);
    unsigned closed = 0;
    for (unsigned holder = 0; holder < 32; holder++) closed += pw_wine_codeshare_forget(holder);
    assert(closed == PW_WINE_CODESHARE_ENTRIES);
    pw_wine_codeshare_stats(&stats);
    assert(!stats.entries && !stats.bytes);

    /* A full descriptor table takes entries back whoever holds them (files first): each released descriptor is closed,
     * a later find misses, an offer is kept again, and a release past the last entry stops. */
    pw_wine_codeshare_reset();
    int fds[PW_WINE_CODESHARE_ENTRIES];
    for (unsigned i = 0; i < PW_WINE_CODESHARE_ENTRIES; i++) {
        fds[i] = dup(base);
        assert(!pw_wine_codeshare_offer(i + 1, 16384, i % 32, fds[i]));
    }
    assert(pw_wine_codeshare_release(64) == 64);
    unsigned open_now = 0;
    for (unsigned i = 0; i < PW_WINE_CODESHARE_ENTRIES; i++) open_now += is_open(fds[i]);
    assert(open_now == PW_WINE_CODESHARE_ENTRIES - 64);
    pw_wine_codeshare_stats(&stats);
    assert(stats.entries == PW_WINE_CODESHARE_ENTRIES - 64 && stats.released == 64);
    other = dup(base);
    assert(!pw_wine_codeshare_offer(PW_WINE_CODESHARE_ENTRIES + 1, 16384, 0, other));   /* room again */
    unsigned misses = 0;
    for (unsigned i = 0; i < PW_WINE_CODESHARE_ENTRIES; i++) {
        int got_fd;
        if (pw_wine_codeshare_find(i + 1, 16384, 1, &got_fd)) misses += errno == ENOENT;
        else close(got_fd);
    }
    assert(misses == 64);
    assert(pw_wine_codeshare_release(100000) == PW_WINE_CODESHARE_ENTRIES - 64 + 1);
    assert(!pw_wine_codeshare_release(1));
    pw_wine_codeshare_stats(&stats);
    assert(!stats.entries && !stats.bytes && stats.released == PW_WINE_CODESHARE_ENTRIES + 1);
    close(base);
}

/* Hashes whose home slot is chosen: home(h) is the top 14 bits of h * golden, so h = (slot << 50 | low) * golden^-1. */
static uint64_t golden_inverse(void)
{
    const uint64_t golden = 0x9e3779b97f4a7c15u;
    uint64_t x = golden;   /* Newton: each step doubles the correct low bits */
    for (int i = 0; i < 6; i++) x *= 2 - golden * x;
    assert(x * golden == 1);
    return x;
}

/* Random offers, finds and forgets over a few home slots (runs that collide and wrap the table's end) against a list. */
static void registry_model(void)
{
    enum { KEYS = 600, ROUNDS = 40000 };
    static struct { uint64_t hash; int kept, fd; uint32_t holders; } model[KEYS];
    const uint64_t inverse = golden_inverse();
    const unsigned slots[] = { 16382, 16383, 0, 1, 9000 };
    PwWineCodeshareStats stats;
    unsigned live = 0;

    pw_wine_codeshare_reset();
    srand(12345);
    for (unsigned k = 0; k < KEYS; k++) {
        model[k].hash = (((uint64_t)slots[k % 5] << 50) | (uint64_t)(k + 1)) * inverse;
        model[k].kept = 0;
    }
    for (unsigned round = 0; round < ROUNDS; round++) {
        const unsigned k = (unsigned)rand() % KEYS, holder = (unsigned)rand() % 32;
        int fd;
        const int op = rand() % 16;   /* offer 8, find 7, forget 1 in 16 */
        switch (op < 8 ? 0 : op < 15 ? 2 : 3) {
        case 0:
            fd = null_fd();
            if (model[k].kept) {
                assert(pw_wine_codeshare_offer(model[k].hash, 16384, holder, fd) == -1 && errno == EEXIST);
            } else {
                assert(!pw_wine_codeshare_offer(model[k].hash, 16384, holder, fd));
                model[k].kept = 1;
                model[k].fd = fd;
                model[k].holders = 1u << holder;
                live++;
            }
            break;
        case 2:
            if (!model[k].kept) {
                assert(pw_wine_codeshare_find(model[k].hash, 16384, holder, &fd) == -1 && errno == ENOENT);
            } else {
                assert(!pw_wine_codeshare_find(model[k].hash, 16384, holder, &fd));
                close(fd);
                model[k].holders |= 1u << holder;
            }
            break;
        default: {
            unsigned expect = 0;
            for (unsigned j = 0; j < KEYS; j++)
                if (model[j].kept && (model[j].holders &= ~(1u << holder)) == 0) {
                    model[j].kept = 0;
                    expect++;
                }
            assert(pw_wine_codeshare_forget(holder) == expect);
            live -= expect;
            for (unsigned j = 0; j < KEYS; j++)
                if (model[j].kept) assert(is_open(model[j].fd));
        }
        }
    }
    pw_wine_codeshare_stats(&stats);
    assert(stats.entries == live && live > 100);
    for (unsigned k = 0; k < KEYS; k++) {
        int fd;
        assert((pw_wine_codeshare_find(model[k].hash, 16384, 0, &fd) == 0) == model[k].kept);
        if (model[k].kept) close(fd);
    }
    pw_wine_codeshare_reset();
}

/* The shared ranges, as pages 0..PAGES of 16 KiB, against a bitmap. */
enum { PAGES = 512, UNIT = 16384 };
static PwWineCodeshareRanges set;
static unsigned char truth[PAGES];
static void check_set(void)
{
    for (unsigned i = 0; i < set.count; i++) {
        assert(set.range[i].start < set.range[i].end);
        assert(!i || set.range[i - 1].end < set.range[i].start);   /* disjoint, touching ones merged */
    }
    for (unsigned page = 0; page < PAGES; page++) {
        uintptr_t low, high;
        const int marked = pw_wine_codeshare_marked(&set, (uintptr_t)page * UNIT, (uintptr_t)(page + 1) * UNIT, &low, &high);
        assert(marked == truth[page]);
        if (marked) assert(low == (uintptr_t)page * UNIT && high == (uintptr_t)(page + 1) * UNIT);
    }
}
static void ranges(void)
{
    uintptr_t low, high;

    srand(777);
    for (unsigned round = 0; round < 20000; round++) {
        const unsigned a = (unsigned)rand() % PAGES, n = 1 + (unsigned)rand() % 24, b = a + n > PAGES ? PAGES : a + n;
        if (rand() % 2) {
            assert(!pw_wine_codeshare_mark(&set, (uintptr_t)a * UNIT, (uintptr_t)b * UNIT));
            memset(truth + a, 1, b - a);
        } else {
            pw_wine_codeshare_unmark(&set, (uintptr_t)a * UNIT, (uintptr_t)b * UNIT);
            memset(truth + a, 0, b - a);
        }
        if (!(round % 50)) check_set();
    }
    check_set();
    /* The lowest piece of a wide query, clipped to it. */
    memset(&set, 0, sizeof(set));
    assert(!pw_wine_codeshare_mark(&set, 4 * UNIT, 8 * UNIT) && !pw_wine_codeshare_mark(&set, 10 * UNIT, 12 * UNIT));
    assert(pw_wine_codeshare_marked(&set, 6 * UNIT, 11 * UNIT, &low, &high) && low == 6 * UNIT && high == 8 * UNIT);
    assert(pw_wine_codeshare_marked(&set, 8 * UNIT, 20 * UNIT, &low, &high) && low == 10 * UNIT && high == 12 * UNIT);
    assert(!pw_wine_codeshare_marked(&set, 8 * UNIT, 10 * UNIT, &low, &high));
    assert(pw_wine_codeshare_mark(&set, 3 * UNIT, 3 * UNIT) == -1 && errno == EINVAL);

    /* Full: a new range is refused (nothing marked), one touching another still merges, and a split keeps it whole. */
    memset(&set, 0, sizeof(set));
    for (unsigned i = 0; i < PW_WINE_CODESHARE_RANGES; i++)
        assert(!pw_wine_codeshare_mark(&set, (uintptr_t)(4 * i) * UNIT, (uintptr_t)(4 * i + 2) * UNIT));
    const uintptr_t beyond = (uintptr_t)(4 * PW_WINE_CODESHARE_RANGES) * UNIT;
    assert(pw_wine_codeshare_mark(&set, beyond, beyond + UNIT) == -1 && errno == ENOSPC);
    assert(!pw_wine_codeshare_marked(&set, beyond, beyond + UNIT, &low, &high));
    assert(!pw_wine_codeshare_mark(&set, 2 * UNIT, 3 * UNIT) && set.count == PW_WINE_CODESHARE_RANGES);
    pw_wine_codeshare_unmark(&set, 1 * UNIT, 2 * UNIT);   /* inside [0, 3): cannot split */
    assert(pw_wine_codeshare_marked(&set, 1 * UNIT, 2 * UNIT, &low, &high));
    pw_wine_codeshare_unmark(&set, 0, 1 * UNIT);           /* its front: no split needed */
    assert(!pw_wine_codeshare_marked(&set, 0, 1 * UNIT, &low, &high) && set.range[0].start == 1 * UNIT);
}

/* The exact page set against a model: a byte map of which 16 KiB pages are counted, entries one to three pages. */
static void pages(void)
{
    enum { PAGE = 16384, SPAN = 512 };
    static PwWineCodesharePages set;
    static unsigned model[SPAN];   /* the id of the entry covering each page, 0 none (ids never repeat) */
    unsigned id = 0;
    srand(7);
    memset(&set, 0, sizeof(set));
    for (int round = 0; round < 20000; ++round) {
        const unsigned at = (unsigned)rand() % SPAN, length = 1 + (unsigned)rand() % 3;
        if (rand() % 3) {
            if (at + length > SPAN) continue;
            assert(!pw_wine_codeshare_pages_add(&set, (uintptr_t)at * PAGE, (uint64_t)length * PAGE));
            /* any entry it overlaps leaves whole */
            for (unsigned p = 0; p < SPAN; ++p)
                for (unsigned q = at; q < at + length; ++q)
                    if (model[q] && model[p] == model[q] && (p < at || p >= at + length)) model[p] = 0;
            ++id;
            for (unsigned q = at; q < at + length; ++q) model[q] = id;
        } else {
            const unsigned end = at + length > SPAN ? SPAN : at + length;
            uint64_t expect = 0;
            unsigned hit[3] = {0, 0, 0}, hits = 0;
            for (unsigned q = at; q < end; ++q)
                if (model[q] && (!hits || hit[hits - 1] != model[q])) hit[hits++] = model[q];
            for (unsigned p = 0; p < SPAN; ++p)
                for (unsigned h = 0; h < hits; ++h)
                    if (model[p] == hit[h]) { expect += PAGE; model[p] = 0; break; }
            assert(pw_wine_codeshare_pages_drop(&set, (uintptr_t)at * PAGE, (uintptr_t)end * PAGE) == expect);
        }
        uint64_t total = 0;
        for (unsigned p = 0; p < SPAN; ++p) total += model[p] ? PAGE : 0;
        assert(set.bytes == total);
        for (unsigned i = 1; i < set.count; ++i)
            assert(set.page[i - 1].start + set.page[i - 1].bytes <= set.page[i].start);
    }
    /* Full: a new page is not counted, and an add that replaces one still fits. */
    memset(&set, 0, sizeof(set));
    for (unsigned i = 0; i < PW_WINE_CODESHARE_PAGES; ++i)
        assert(!pw_wine_codeshare_pages_add(&set, (uintptr_t)i * 2 * PAGE, PAGE));
    assert(pw_wine_codeshare_pages_add(&set, (uintptr_t)1 * PAGE, PAGE) == -1 && errno == ENOSPC);
    assert(set.bytes == (uint64_t)PW_WINE_CODESHARE_PAGES * PAGE);
    assert(!pw_wine_codeshare_pages_add(&set, 0, PAGE) && set.count == PW_WINE_CODESHARE_PAGES);
    assert(pw_wine_codeshare_pages_add(&set, 0, 0) == -1 && errno == EINVAL);
    assert(pw_wine_codeshare_pages_drop(&set, 5, 5) == 0);
}

/* The census's copies beyond the first: the reports' sum less the registry's objects, never below 0, never more
 * than the true copies when every object a live helper maps is in the registry. */
static void duplicates(void)
{
    pw_wine_codeshare_reset();
    assert(pw_wine_codeshare_duplicates(0) == 0 && pw_wine_codeshare_duplicates(1 << 20) == 1 << 20);
    /* Three objects of 16 KiB: one made by holder 0 and taken by 1 and 2, one made by 0 alone, one made by 1. */
    assert(!pw_wine_codeshare_offer(1, 16384, 0, open("/dev/null", O_RDONLY)));
    assert(!pw_wine_codeshare_offer(2, 16384, 0, open("/dev/null", O_RDONLY)));
    assert(!pw_wine_codeshare_offer(3, 16384, 1, open("/dev/null", O_RDONLY)));
    int fd;
    assert(!pw_wine_codeshare_find(1, 16384, 1, &fd)); close(fd);
    assert(!pw_wine_codeshare_find(1, 16384, 2, &fd)); close(fd);
    /* reports: 0 maps objects 1 and 2, 1 maps 1 and 3, 2 maps 1: 5 mappings of 3 objects, 2 copies beyond the first */
    assert(pw_wine_codeshare_duplicates(5 * 16384) == 2 * 16384);
    /* 2 has unmapped its copy but not ended: its report drops, the registry keeps the object: 1 copy */
    assert(pw_wine_codeshare_duplicates(4 * 16384) == 1 * 16384);
    /* 0 also unmapped object 2 (its own) while alive: the true copies stay 1, the estimate falls to 0 (too high a
     * census, never too low) */
    assert(pw_wine_codeshare_duplicates(3 * 16384) == 0);
    /* 0 ends: its report goes first, then its holds; object 2 had only it */
    assert(pw_wine_codeshare_forget(0) == 1);
    assert(pw_wine_codeshare_duplicates(2 * 16384) == 0);   /* 1 maps 1 and 3: two objects, no copy */
    pw_wine_codeshare_reset();
}

int main(void)
{
    struct rlimit files;
    /* The full registry holds 2,048 descriptors. */
    assert(!getrlimit(RLIMIT_NOFILE, &files) && files.rlim_max >= 3000);
    files.rlim_cur = files.rlim_max;
    assert(!setrlimit(RLIMIT_NOFILE, &files));
    hash();
    registry();
    registry_model();
    ranges();
    pages();
    duplicates();
    puts("codeshare: hash, registry (model with wrapping runs), shared ranges (bitmap, capacity), shared pages (model, "
         "capacity), copies beyond the first, release for a full table: passed");
    return 0;
}
