/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PROTON_FLEX_PROBE_H
#define PROTON_FLEX_PROBE_H
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The flexible-memory probe (docs/FLEXIBLE_MEMORY.md): which pool a Sony local process draws on, and whether it runs
 * code from direct memory. Each figure is in bytes, -1 when its query failed; statuses are the kernel's. */
#define PROTON_FLEX_MAGIC 0x584c4650u /* "PFLX" */
#define PROTON_FLEX_BYTES (64ull << 20)
enum { FLEX_START, FLEX_DIRECT, FLEX_ANON, FLEX_JIT, FLEX_POINTS };
/* The ways to executable memory tried in each process, in this order (proton_flex_exec_try). */
enum { FLEX_EXEC_DIRECT0, FLEX_EXEC_DIRECT12, FLEX_EXEC_MAIN12, FLEX_EXEC_MAPX, FLEX_EXEC_ANON, FLEX_EXEC_JIT, FLEX_EXEC_WAYS };
struct ProtonFlexExec { int32_t made, mapped, protect, query, prot, value; };
struct ProtonFlexReply {
    uint32_t magic; int32_t pid;
    int32_t direct_alloc, direct_map, anon_map, jit_made;
    int64_t resident[FLEX_POINTS], mapped[FLEX_POINTS], available[FLEX_POINTS];
    struct ProtonFlexExec exec[FLEX_EXEC_WAYS];
    int32_t elevation, euid;
    int32_t pt_cpu_total[FLEX_POINTS], pt_cpu_free[FLEX_POINTS], pt_gpu_total, pt_gpu_free;
    int32_t jit80_made, jit80_mapped, jit80_protect, jit80_value;
    int32_t nofile_cur, nofile_max, nofile_raise_rc, nofile_raise_cur;
};
/* What the tester probe keeps of one flex run (native/proton_tester_probe.cpp): the spawn's answer, the helper's reply,
 * and the title's free flexible memory before, while the helper held its 64 MiB, and after it was closed. */
typedef struct ProtonFlexRun {
    int spawned, spawn_status, answered, closed, present;
    struct ProtonFlexReply reply;
    int64_t title_before, title_held, title_after;
} ProtonFlexRun;
extern int sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
extern int sceKernelAllocateMainDirectMemory(size_t, size_t, int, int64_t *);
extern int sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
extern int sceKernelMprotect(const void *, size_t, int);
extern int sceKernelQueryMemoryProtection(void *, void **, void **, int *);
extern int sceKernelJitCreateSharedMemory(const char *, size_t, int, int *);
extern int sceKernelGetPageTableStats(int *, int *, int *, int *);
extern int64_t sceKernelGetDirectMemorySize(void);
/* One way to a page of `mov eax, 42; ret`: run only where the kernel says execute is held (a payload's kernel answers
 * success and drops it). The memory is left mapped: the probe's processes end soon after. */
static inline void proton_flex_exec_try(unsigned way, struct ProtonFlexExec *out)
{
    static const unsigned char body[] = {0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3};
    const size_t page = 16384;
    void *code = 0;
    int64_t physical = -1;
    out->made = out->mapped = out->protect = out->query = 0; out->prot = out->value = -1;
    if (way == FLEX_EXEC_ANON || way == FLEX_EXEC_JIT) {
        int fd = -1;
        if (way == FLEX_EXEC_JIT) out->made = sceKernelJitCreateSharedMemory("pw-flex", page, 7, &fd);
        if (out->made) return;
        code = mmap(0, page, PROT_READ | PROT_WRITE, way == FLEX_EXEC_JIT ? MAP_SHARED : (MAP_ANON | MAP_PRIVATE), fd, 0);
        out->mapped = code == MAP_FAILED ? -1 : 0;
        if (fd >= 0) close(fd);
    } else {
        const int type = way == FLEX_EXEC_DIRECT0 ? 0 : 12;
        out->made = way == FLEX_EXEC_MAIN12 ? sceKernelAllocateMainDirectMemory(page, page, type, &physical)
                                            : sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), page, page, type, &physical);
        if (out->made) return;
        out->mapped = sceKernelMapDirectMemory(&code, page, way == FLEX_EXEC_MAPX ? 7 : 3, 0, physical, page);
    }
    if (out->mapped) return;
    for (unsigned i = 0; i < sizeof(body); ++i) ((volatile unsigned char *)code)[i] = body[i];
    if (way != FLEX_EXEC_MAPX) out->protect = sceKernelMprotect(code, page, PROT_READ | PROT_EXEC);
    void *start = 0, *end = 0;
    out->query = sceKernelQueryMemoryProtection(code, &start, &end, &out->prot);
    if (!out->protect && !out->query && (out->prot & PROT_EXEC)) out->value = ((int (*)(void))(uintptr_t)code)();
}

#ifdef __cplusplus
}
#include <string>
/* native/proton_service_preflight.cpp: one flex run for the tester probe; its lines go to log. 0, or nonzero. */
int proton_flex_run(ProtonFlexRun *run, std::string *log);
#endif
#endif
