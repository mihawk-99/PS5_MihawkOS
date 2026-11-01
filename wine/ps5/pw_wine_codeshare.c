/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shared code copies: the title's registry of JIT objects and a helper's shared ranges (pw_wine_codeshare.h). */
#include "pw_wine_codeshare.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

/* Open addressing, linear probing, at most half full; a removal shifts the entries after it back (no tombstones). */
enum { SLOT_BITS = 14, SLOTS = 1 << SLOT_BITS };
_Static_assert(SLOTS >= 2 * PW_WINE_CODESHARE_ENTRIES, "the table stays at most half full");
_Static_assert(PW_WINE_CODESHARE_HOLDERS <= 32, "holders are bits of a 32-bit mask");
typedef struct Entry { uint64_t hash, bytes; uint32_t holders; int fd; uint64_t file[3]; } Entry;   /* bytes 0: empty; file 0,0,0: content key */
static Entry table[SLOTS];
static PwWineCodeshareStats counted;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static size_t home(uint64_t hash) { return (size_t)((hash * 0x9e3779b97f4a7c15u) >> (64 - SLOT_BITS)); }

/* The entry of (hash, bytes), or the empty slot where it would go. Under the lock. */
static Entry *lookup(uint64_t hash, uint64_t bytes)
{
    for (size_t i = home(hash);; i = (i + 1) % SLOTS)
        if (!table[i].bytes || (table[i].hash == hash && table[i].bytes == bytes)) return &table[i];
}

/* The hash of a file identity and an offset: the entry's home. Collisions are resolved by the full comparison. */
static uint64_t file_hash(uint64_t dev, uint64_t ino, uint64_t off)
{
    uint64_t h = dev * 0x9e3779b97f4a7c15u ^ ino * 0xc2b2ae3d27d4eb4fu ^ off * 0x165667b19e3779f9u;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdu; h ^= h >> 33;
    return h;
}

/* The entry of (file, bytes), or the empty slot where it would go. Under the lock. */
static Entry *lookup_file(const uint64_t file[3], uint64_t bytes)
{
    const uint64_t hash = file_hash(file[0], file[1], file[2]);
    for (size_t i = home(hash);; i = (i + 1) % SLOTS)
        if (!table[i].bytes || (table[i].hash == hash && table[i].bytes == bytes &&
                                !memcmp(table[i].file, file, sizeof(table[i].file)))) return &table[i];
}

/* Empty slot hole, moving back each later entry of its run whose home is not after the hole. Under the lock. */
static void vacate(size_t hole)
{
    for (size_t next = (hole + 1) % SLOTS; table[next].bytes; next = (next + 1) % SLOTS)
        if ((next - home(table[next].hash)) % SLOTS >= (next - hole) % SLOTS) {
            table[hole] = table[next];
            hole = next;
        }
    table[hole].bytes = 0;
}

int pw_wine_codeshare_find(uint64_t hash, uint64_t bytes, unsigned holder, int *fd)
{
    int error = 0;
    Entry *entry;

    if (holder >= PW_WINE_CODESHARE_HOLDERS || !bytes) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&lock);
    counted.finds++;
    entry = lookup(hash, bytes);
    if (!entry->bytes) error = ENOENT;
    else if ((*fd = dup(entry->fd)) < 0) error = errno;
    else {
        entry->holders |= 1u << holder;
        counted.hits++;
    }
    pthread_mutex_unlock(&lock);
    if (error) errno = error;
    return error ? -1 : 0;
}

int pw_wine_codeshare_offer(uint64_t hash, uint64_t bytes, unsigned holder, int fd)
{
    int error = holder >= PW_WINE_CODESHARE_HOLDERS || !bytes || fd < 0 ? EINVAL : 0;
    Entry *entry;

    pthread_mutex_lock(&lock);
    counted.offers++;
    if (!error) {
        entry = lookup(hash, bytes);
        if (entry->bytes) error = EEXIST;
        else if (counted.entries >= PW_WINE_CODESHARE_ENTRIES) error = ENOSPC;
        else {
            *entry = (Entry){ hash, bytes, 1u << holder, fd, { 0, 0, 0 } };
            counted.entries++;
            counted.bytes += bytes;
            counted.kept++;
        }
    }
    pthread_mutex_unlock(&lock);
    if (error) {
        if (fd >= 0) close(fd);
        errno = error;
        return -1;
    }
    return 0;
}

/* The object a helper read from file[0]:file[1] at offset file[2] for bytes, under the file key: 0 with *fd a new
 * descriptor (the caller's to close) and holder recorded, or -1 with errno ENOENT, EINVAL, or dup's error. The read
 * stays the first holder's; every later one maps the object without reading the file. */
int pw_wine_codeshare_find_file(const uint64_t file[3], uint64_t bytes, unsigned holder, int *fd)
{
    int error = 0;
    Entry *entry;

    if (holder >= PW_WINE_CODESHARE_HOLDERS || !bytes || !file) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&lock);
    counted.finds++;
    entry = lookup_file(file, bytes);
    if (!entry->bytes) error = ENOENT;
    else if ((*fd = dup(entry->fd)) < 0) error = errno;
    else {
        entry->holders |= 1u << holder;
        counted.hits++;
    }
    pthread_mutex_unlock(&lock);
    if (error) errno = error;
    return error ? -1 : 0;
}

/* Holder made fd, the object of such a file cluster: 0 kept, or -1 with errno EEXIST/ENOSPC/EINVAL. fd becomes the
 * registry's either way: it is closed when not kept. */
int pw_wine_codeshare_offer_file(const uint64_t file[3], uint64_t bytes, unsigned holder, int fd)
{
    int error = holder >= PW_WINE_CODESHARE_HOLDERS || !bytes || !file || fd < 0 ? EINVAL : 0;
    Entry *entry;

    pthread_mutex_lock(&lock);
    counted.offers++;
    if (!error) {
        entry = lookup_file(file, bytes);
        if (entry->bytes) error = EEXIST;
        else if (counted.entries >= PW_WINE_CODESHARE_ENTRIES) error = ENOSPC;
        else {
            *entry = (Entry){ file_hash(file[0], file[1], file[2]), bytes, 1u << holder, fd,
                              { file[0], file[1], file[2] } };
            counted.entries++;
            counted.bytes += bytes;
            counted.kept++;
        }
    }
    pthread_mutex_unlock(&lock);
    if (error) {
        if (fd >= 0) close(fd);
        errno = error;
        return -1;
    }
    return 0;
}

unsigned pw_wine_codeshare_forget(unsigned holder)
{
    unsigned closed = 0;

    if (holder >= PW_WINE_CODESHARE_HOLDERS) return 0;
    pthread_mutex_lock(&lock);
    /* A removal may move a later entry into slot i, so i is looked at again; one moved from the start of a run that
     * wraps is looked at twice, which finds its bit already clear. */
    for (size_t i = 0; i < SLOTS;) {
        Entry *entry = &table[i];
        if (!entry->bytes || !(entry->holders & (1u << holder))) {
            i++;
            continue;
        }
        entry->holders &= ~(1u << holder);
        if (entry->holders) {
            i++;
            continue;
        }
        close(entry->fd);
        counted.entries--;
        counted.bytes -= entry->bytes;
        closed++;
        vacate(i);
    }
    counted.closed += closed;
    pthread_mutex_unlock(&lock);
    return closed;
}

unsigned pw_wine_codeshare_release(unsigned count)
{
    static size_t cursor;   /* where the last release stopped: entries go round, not always the same home slots */
    unsigned closed = 0;

    pthread_mutex_lock(&lock);
    for (size_t looked = 0; closed < count && counted.entries && looked < SLOTS;) {
        Entry *entry = &table[cursor];
        if (!entry->bytes) {
            cursor = (cursor + 1) % SLOTS;
            looked++;
            continue;
        }
        close(entry->fd);
        counted.entries--;
        counted.bytes -= entry->bytes;
        closed++;
        vacate(cursor);   /* a later entry may move into this slot: it is looked at next */
    }
    counted.closed += closed;
    counted.released += closed;
    pthread_mutex_unlock(&lock);
    return closed;
}

void pw_wine_codeshare_stats(PwWineCodeshareStats *stats)
{
    pthread_mutex_lock(&lock);
    *stats = counted;
    pthread_mutex_unlock(&lock);
}

void pw_wine_codeshare_reset(void)
{
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < SLOTS; i++)
        if (table[i].bytes) close(table[i].fd);
    memset(table, 0, sizeof(table));
    memset(&counted, 0, sizeof(counted));
    pthread_mutex_unlock(&lock);
}

/* The first range of set ending at or after at (touching counts), or count. */
static unsigned reaching(const PwWineCodeshareRanges *set, uintptr_t at)
{
    unsigned low = 0, high = set->count;

    while (low < high) {
        const unsigned mid = low + (high - low) / 2;
        if (set->range[mid].end < at) low = mid + 1;
        else high = mid;
    }
    return low;
}

int pw_wine_codeshare_mark(PwWineCodeshareRanges *set, uintptr_t start, uintptr_t end)
{
    const unsigned first = reaching(set, start);
    unsigned last = first;

    if (start >= end) {
        errno = EINVAL;
        return -1;
    }
    while (last < set->count && set->range[last].start <= end) last++;
    if (first == last) {
        if (set->count == PW_WINE_CODESHARE_RANGES) {
            errno = ENOSPC;
            return -1;
        }
        memmove(&set->range[first + 1], &set->range[first], (set->count - first) * sizeof(set->range[0]));
        set->range[first].start = start;
        set->range[first].end = end;
        set->count++;
        return 0;
    }
    if (set->range[first].start < start) start = set->range[first].start;
    if (set->range[last - 1].end > end) end = set->range[last - 1].end;
    set->range[first].start = start;
    set->range[first].end = end;
    memmove(&set->range[first + 1], &set->range[last], (set->count - last) * sizeof(set->range[0]));
    set->count -= last - first - 1;
    return 0;
}

void pw_wine_codeshare_unmark(PwWineCodeshareRanges *set, uintptr_t start, uintptr_t end)
{
    unsigned i = reaching(set, start + 1);   /* a range ending at start is untouched */

    while (start < end && i < set->count && set->range[i].start < end) {
        const uintptr_t low = set->range[i].start, high = set->range[i].end;
        if (low < start && high > end) {
            if (set->count == PW_WINE_CODESHARE_RANGES) return;   /* stays whole */
            memmove(&set->range[i + 2], &set->range[i + 1], (set->count - i - 1) * sizeof(set->range[0]));
            set->range[i].end = start;
            set->range[i + 1].start = end;
            set->range[i + 1].end = high;
            set->count++;
            return;
        }
        if (low < start) {
            set->range[i++].end = start;
        } else if (high > end) {
            set->range[i].start = end;
            return;
        } else {
            memmove(&set->range[i], &set->range[i + 1], (set->count - i - 1) * sizeof(set->range[0]));
            set->count--;
        }
    }
}

int pw_wine_codeshare_marked(const PwWineCodeshareRanges *set, uintptr_t start, uintptr_t end, uintptr_t *low,
                             uintptr_t *high)
{
    const unsigned i = reaching(set, start + 1);

    if (start >= end || i == set->count || set->range[i].start >= end) return 0;
    *low = set->range[i].start > start ? set->range[i].start : start;
    *high = set->range[i].end < end ? set->range[i].end : end;
    return 1;
}

/* The first entry of set ending after at, or count. */
static unsigned page_after(const PwWineCodesharePages *set, uintptr_t at)
{
    unsigned low = 0, high = set->count;
    while (low < high) {
        const unsigned mid = low + (high - low) / 2;
        if (set->page[mid].start + set->page[mid].bytes <= at) low = mid + 1;
        else high = mid;
    }
    return low;
}
uint64_t pw_wine_codeshare_pages_drop(PwWineCodesharePages *set, uintptr_t start, uintptr_t end)
{
    const unsigned first = page_after(set, start);
    unsigned last = first;
    uint64_t gone = 0;
    if (start >= end) return 0;
    while (last < set->count && set->page[last].start < end) gone += set->page[last++].bytes;
    if (last == first) return 0;
    memmove(&set->page[first], &set->page[last], (set->count - last) * sizeof(set->page[0]));
    set->count -= last - first;
    set->bytes -= gone;
    return gone;
}
int pw_wine_codeshare_pages_add(PwWineCodesharePages *set, uintptr_t start, uint64_t bytes)
{
    if (!bytes || start + bytes < start) {
        errno = EINVAL;
        return -1;
    }
    (void)pw_wine_codeshare_pages_drop(set, start, start + bytes);
    if (set->count == PW_WINE_CODESHARE_PAGES) {
        errno = ENOSPC;
        return -1;
    }
    const unsigned at = page_after(set, start);
    memmove(&set->page[at + 1], &set->page[at], (set->count - at) * sizeof(set->page[0]));
    set->page[at].start = start;
    set->page[at].bytes = bytes;
    set->count++;
    set->bytes += bytes;
    return 0;
}
uint64_t pw_wine_codeshare_duplicates(uint64_t mapped_bytes)
{
    uint64_t held;
    pthread_mutex_lock(&lock);
    held = counted.bytes;
    pthread_mutex_unlock(&lock);
    return mapped_bytes > held ? mapped_bytes - held : 0;
}
