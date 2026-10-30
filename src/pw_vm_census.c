/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_vm_census.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pw_vm_record_decode(const uint8_t raw[PW_VM_RECORD], PwVmRecord *out)
{
    uint64_t start, end;
    memset(out, 0, sizeof(*out));
    memcpy(&start, raw, 8);
    memcpy(&end, raw + 8, 8);
    out->start = (uintptr_t)start;
    out->end = (uintptr_t)end;
    memcpy(&out->protection, raw + 24, 4);
    memcpy(&out->type, raw + 28, 4);
    out->flags = raw[32];
    memcpy(out->name, raw + 33, PW_VM_NAME);
    out->name[PW_VM_NAME] = 0;
    for (size_t i = 0; out->name[i]; i++)
        if ((unsigned char)out->name[i] < 0x20 || (unsigned char)out->name[i] > 0x7e) out->name[i] = '?';
}

static PwVmGroup *group_of(PwVmCensus *census, const PwVmRecord *record)
{
    for (unsigned i = 0; i < census->count; i++)
        if (census->groups[i].flags == record->flags && !strcmp(census->groups[i].name, record->name))
            return &census->groups[i];
    if (census->count == PW_VM_GROUPS) return NULL;
    PwVmGroup *group = &census->groups[census->count++];
    memcpy(group->name, record->name, sizeof(group->name));
    group->flags = record->flags;
    group->protection = record->protection;
    return group;
}

/* record (bytes) into a list of the cap largest, largest first. */
static void insert_big(PwVmBig *list, unsigned *count, unsigned cap, const PwVmRecord *record, uint64_t bytes)
{
    unsigned at = *count < cap ? *count : cap;
    while (at > 0 && list[at - 1].bytes < bytes) at--;
    if (at >= cap) return;
    const unsigned last = *count < cap ? *count : cap - 1;
    for (unsigned i = last; i > at; i--) list[i] = list[i - 1];
    list[at].start = record->start;
    list[at].bytes = bytes;
    list[at].protection = record->protection;
    list[at].flags = record->flags;
    memcpy(list[at].name, record->name, sizeof(list[at].name));
    if (*count < cap) (*count)++;
}

static unsigned prot_class(int32_t protection)
{
    if (protection & 4) return 2;
    if (protection & 2) return 1;
    return (protection & 1) ? 0 : 3;
}

static unsigned size_class(uint64_t bytes)
{
    return bytes < (64u << 10) ? 0 : bytes < (1u << 20) ? 1 : bytes < (8u << 20) ? 2 : 3;
}

int pw_vm_budget_parse(const char *text, long long ms[PW_VM_CLASSES])
{
    long long value[PW_VM_CLASSES];
    unsigned n = 0;
    const char *at = text;
    for (unsigned i = 0; i < PW_VM_CLASSES; i++) ms[i] = 100;
    if (!text || !*text) return -1;
    while (*at && n < PW_VM_CLASSES) {
        char *end;
        long long v;
        if (*at < '0' || *at > '9') return -1;
        v = strtoll(at, &end, 10);
        if (v > 60000) return -1;
        value[n++] = v;
        at = end;
        if (*at == ':') at++;
        else if (*at) return -1;
    }
    if (*at || (n != 1 && n != PW_VM_CLASSES)) return -1;
    for (unsigned i = 0; i < PW_VM_CLASSES; i++) ms[i] = value[n == 1 ? 0 : i];
    return n == PW_VM_CLASSES;
}

/* A mapping's regions go to the window it starts in (one that crosses a 1 GiB line is rare and counted once). */
static void count_window(PwVmCensus *census, uint32_t gib, uint64_t regions, uint64_t bytes)
{
    for (unsigned i = 0; i < census->window_count; i++)
        if (census->windows[i].gib == gib) { census->windows[i].regions += (uint32_t)regions; census->windows[i].bytes += bytes; return; }
    if (census->window_count == PW_VM_WINDOWS) { census->window_other += regions; return; }
    census->windows[census->window_count].gib = gib;
    census->windows[census->window_count].bytes = bytes;
    census->windows[census->window_count++].regions = (uint32_t)regions;
}

void pw_vm_census_walk(PwVmCensus *census, PwVmQuery query, PwVmResident resident, void *context)
{
    uintptr_t address = 0;
    uint64_t last_region = 0;
    memset(census, 0, sizeof(*census));
    for (;;) {
        uint8_t raw[PW_VM_RECORD];
        PwVmRecord record;
        if (census->mappings == PW_VM_WALK_LIMIT) { census->truncated = 1; break; }
        const int status = query(context, address, raw);
        if (status == 1) break;
        if (status < 0) { census->failed = 1; break; }
        pw_vm_record_decode(raw, &record);
        if (record.end <= record.start || record.end <= address) { census->failed = 1; break; }
        const uint64_t bytes = record.end - record.start;
        census->mappings++;
        /* resident pages of every accessible mapping, direct memory apart: a 2026-10-11 run (record removed from the tree) showed the
         * kernel's resident count of a launcher's GPU process swing by 390 MiB while the mappings walked here, direct
         * ones excluded, held 90 to 130 MiB; the flags are the part of the layout least agreed on, so they do
         * not decide what is asked */
        const unsigned klass = !record.protection ? PW_VM_CLASS_RESERVED
            : (record.flags & PW_VM_DIRECT && !(record.flags & PW_VM_FLEXIBLE)) ? PW_VM_CLASS_DIRECT : PW_VM_CLASS_FLEXIBLE;
        census->class_mappings[klass]++;
        census->class_bytes[klass] += bytes;
        uint64_t regions = 0, aligned_direct = 0;
        const int direct_mapping = record.flags & PW_VM_DIRECT && !(record.flags & PW_VM_FLEXIBLE);
        if (record.protection && direct_mapping && !(record.start & 0x1fffff) && !(record.end & 0x1fffff)) {
            aligned_direct = bytes >> 21;
            census->regions_2m_direct += aligned_direct;
        }
        if (record.protection) {
            /* in address order, so a region shared with the previous accessible mapping is counted once */
            const uint64_t first = record.start >> 21, last = (record.end - 1) >> 21;
            regions = last - first + 1 - (census->regions_2m && first == last_region ? 1 : 0);
            census->regions_2m += regions;
            last_region = last;
            count_window(census, (uint32_t)(record.start >> 30), regions, bytes);
        }
        const uint64_t in = resident ? resident(context, record.start, record.end, klass) : 0;
        if (!record.protection) census->reserved_resident += in;
        else if (record.flags & PW_VM_DIRECT && !(record.flags & PW_VM_FLEXIBLE)) census->direct_resident += in;
        else census->resident += in;
        if (record.flags & PW_VM_FLEXIBLE) census->flexible += bytes;
        else if (record.flags & PW_VM_DIRECT) census->direct += bytes;
        else census->other += bytes;
        if (record.protection) {
            insert_big(census->big, &census->big_count, PW_VM_BIG, &record, bytes);
            if (record.flags & PW_VM_FLEXIBLE) {
                PwVmKind *kind = &census->flexible_kind[prot_class(record.protection)][size_class(bytes)];
                kind->mappings++;
                kind->bytes += bytes;
                kind->resident += in;
                insert_big(census->flexbig, &census->flexbig_count, PW_VM_FLEXBIG, &record, bytes);
            }
        }
        PwVmGroup *group = group_of(census, &record);
        if (group) {
            group->mappings++; group->bytes += bytes; group->resident += in;
            group->regions_2m += regions; group->regions_2m_direct += aligned_direct;
        }
        else census->unkept += bytes;
        address = record.end;
    }
    /* largest first */
    for (unsigned i = 1; i < census->count; i++)
        for (unsigned j = i; j > 0 && (census->groups[j].resident > census->groups[j - 1].resident ||
                                       (census->groups[j].resident == census->groups[j - 1].resident &&
                                        census->groups[j].bytes > census->groups[j - 1].bytes)); j--) {
            PwVmGroup swap = census->groups[j];
            census->groups[j] = census->groups[j - 1];
            census->groups[j - 1] = swap;
        }
}

size_t pw_vm_census_format(const PwVmCensus *census, unsigned n, uint8_t mask, char *out, size_t size)
{
    size_t used = 0;
    unsigned shown = 0;
    if (!size) return 0;
    out[0] = 0;
    for (unsigned i = 0; i < census->count && shown < n; i++) {
        const PwVmGroup *g = &census->groups[i];
        if (mask && !(g->flags & mask)) continue;
        const int wrote = snprintf(out + used, size - used, "%s%s:%02x/%x=%llu/%lluMiB/%u", shown ? " " : "",
                                   g->name[0] ? g->name : "-", g->flags, (unsigned)g->protection,
                                   (unsigned long long)(g->resident >> 20), (unsigned long long)(g->bytes >> 20),
                                   g->mappings);
        if (wrote < 0 || (size_t)wrote >= size - used) break;
        used += (size_t)wrote;
        shown++;
    }
    return used;
}

static size_t format_bigs(const PwVmBig *list, unsigned count, char *out, size_t size)
{
    size_t used = 0;
    if (!size) return 0;
    out[0] = 0;
    for (unsigned i = 0; i < count; i++) {
        const PwVmBig *b = &list[i];
        const int wrote = snprintf(out + used, size - used, "%s%llx+%lluMiB:%02x/%x:%s", i ? " " : "",
                                   (unsigned long long)b->start, (unsigned long long)(b->bytes >> 20), b->flags,
                                   (unsigned)b->protection, b->name[0] ? b->name : "-");
        if (wrote < 0 || (size_t)wrote >= size - used) break;
        used += (size_t)wrote;
    }
    return used;
}

size_t pw_vm_census_format_big(const PwVmCensus *census, char *out, size_t size)
{
    return format_bigs(census->big, census->big_count, out, size);
}

size_t pw_vm_census_format_flexbig(const PwVmCensus *census, char *out, size_t size)
{
    return format_bigs(census->flexbig, census->flexbig_count, out, size);
}

size_t pw_vm_census_format_flexible(const PwVmCensus *census, char *out, size_t size)
{
    static const char *const prots[PW_VM_PROTS] = {"r", "rw", "rx", "o"};
    static const char sizes[PW_VM_SIZES] = {'s', 'm', 'l', 'x'};
    size_t used = 0;
    unsigned shown = 0;
    if (!size) return 0;
    out[0] = 0;
    for (unsigned p = 0; p < PW_VM_PROTS; p++)
        for (unsigned s = 0; s < PW_VM_SIZES; s++) {
            const PwVmKind *k = &census->flexible_kind[p][s];
            if (!k->mappings) continue;
            const int wrote = snprintf(out + used, size - used, "%s%s.%c=%u/%llu.%llu/%llu.%llu", shown ? " " : "",
                                       prots[p], sizes[s], k->mappings,
                                       (unsigned long long)(k->bytes >> 20), (unsigned long long)((k->bytes * 10 >> 20) % 10),
                                       (unsigned long long)(k->resident >> 20),
                                       (unsigned long long)((k->resident * 10 >> 20) % 10));
            if (wrote < 0 || (size_t)wrote >= size - used) return used;
            used += (size_t)wrote;
            shown++;
        }
    return used;
}

size_t pw_vm_census_format_regions(const PwVmCensus *census, unsigned n, char *out, size_t size)
{
    size_t used = 0;
    if (!size) return 0;
    out[0] = 0;
    uint8_t shown[PW_VM_GROUPS] = {0};
    for (unsigned k = 0; k < n; ++k) {
        int best = -1;
        for (unsigned i = 0; i < census->count; ++i)
            if (!shown[i] && (census->groups[i].regions_2m || census->groups[i].regions_2m_direct) &&
                (best < 0 || census->groups[i].regions_2m > census->groups[best].regions_2m))
                best = (int)i;
        if (best < 0) break;
        shown[best] = 1;
        const PwVmGroup *g = &census->groups[best];
        const int wrote = g->regions_2m_direct
            ? snprintf(out + used, size - used, "%s%s:%02x=%llu (2m-direct %llu)/%u", k ? " " : "", g->name[0] ? g->name : "-",
                       g->flags, (unsigned long long)g->regions_2m, (unsigned long long)g->regions_2m_direct, g->mappings)
            : snprintf(out + used, size - used, "%s%s:%02x=%llu/%u", k ? " " : "", g->name[0] ? g->name : "-",
                       g->flags, (unsigned long long)g->regions_2m, g->mappings);
        if (wrote < 0 || (size_t)wrote >= size - used) { out[used] = 0; break; }
        used += (size_t)wrote;
    }
    return used;
}

size_t pw_vm_census_format_windows(const PwVmCensus *census, unsigned n, char *out, size_t size)
{
    size_t used = 0;
    uint64_t rest = census->window_other;
    uint8_t shown[PW_VM_WINDOWS] = {0};
    if (!size) return 0;
    out[0] = 0;
    for (unsigned k = 0; k < census->window_count; ++k) {
        int best = -1;
        for (unsigned i = 0; i < census->window_count; ++i)
            if (!shown[i] && (best < 0 || census->windows[i].regions > census->windows[best].regions)) best = (int)i;
        shown[best] = 1;
        if (k >= n) { rest += census->windows[best].regions; continue; }
        const int wrote = snprintf(out + used, size - used, "%s0x%x=%u/%llu", k ? " " : "", (unsigned)census->windows[best].gib,
                                   (unsigned)census->windows[best].regions,
                                   (unsigned long long)((census->windows[best].bytes + (1u << 20) - 1) >> 20));
        if (wrote < 0 || (size_t)wrote >= size - used) { out[used] = 0; return used; }
        used += (size_t)wrote;
    }
    if (rest) {
        const int wrote = snprintf(out + used, size - used, "%s+%llu", used ? " " : "", (unsigned long long)rest);
        if (wrote > 0 && (size_t)wrote < size - used) used += (size_t)wrote;
        else out[used] = 0;
    }
    return used;
}
