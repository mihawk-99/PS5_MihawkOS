/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_HELPER_SCHED_H
#define PW_HELPER_SCHED_H
/* How a helper's threads are scheduled (fix 1 of the 2026-10-14 review, klog/salvage-2026-10-14/review/plan.md).
 *
 * Why: on the console equal SCHED_FIFO threads never share a CPU. With 32 busy FIFO threads at 700 a periodic thread
 * ran 0 of 500 periods and 19 busy threads starved; under SCHED_RR the same load gave 497 of 500, p99 7 ms
 * (evidence/r4-sched-02.json; priority-probe's equal phase: r4-priority-off-01 0 periods, r5-priority-default-01 348).
 * No helper made a scheduling call: its threads keep what elfldr's spawn left them, and patch 0896's Windows priorities
 * reach only the title's own threads. So a Windows program whose threads spin on a lock (an emulator's spin guard, a pure
 * _mm_pause loop held across VirtualProtect) can keep the holder, and any payload at the same priority on its CPUs,
 * from running. The in-title Wine is left as it is (WINE_PS5_SCHED, patch 0896): this covers helpers only.
 *
 * The settings, read from the bootstrap's environment block (wine/ps5/pw_wine_process.c forwards them to every helper
 * and to the memory helper):
 *   WINE_PS5_HELPER_SCHED     rr (the default): the program's threads take SCHED_RR. keep: today's scheduling, with
 *                             only what measures it: two short threads that read their own, and the loader's
 *                             threads asking to inherit the main thread's.
 *   WINE_PS5_HELPER_PRIORITY  "S,P", 256 <= S <= P <= 767 (256 is the highest): the loader's main thread takes RR at S,
 *                             the program's threads RR at P, so Windows threads are never above the loader's. Unset:
 *                             700,767 (PW_HELPER_SCHED_SERVICE_DEFAULT, PW_HELPER_SCHED_PROGRAM_DEFAULT), measured: with
 *                             the program's threads at RR 700, level with the console's own, seven of seven runs of a
 *                             Unity game in the earlier emulator experiment (removed) stalled 31-83 s in, one thread 15-56 s inside a kernel mprotect of
 *                             its guest memory while the program burned two CPUs (2026-10-16; records removed from the tree;
 *                             docs/PLAN.md "Kernel mprotect stall"); at 700,767 it ran 463 s with none, 22-57 fps
 *                             (2026-10-16).
 *   WINE_PS5_HELPER_CPUS      a hex mask, nonzero, inside the helper's own CPU set: the program's threads keep to it.
 *   WINE_PS5_SCHED_CENSUS     1: the memory helper writes the scheduling census (native/proton_wine_child.c), once
 *                             about 5 s and once about 60 s after it starts. 0 or unset: none.
 * A refused value keeps its default and sets error to EINVAL; reading never fails.
 *
 * Who applies what (native/proton_wine_child.c): the loader's main thread reads its own scheduling and a plain new
 * thread's before any thread of the loader exists, and applies the service plan to itself; the threads it makes later
 * ask to inherit it. Each Windows thread applies the program plan to itself as it registers
 * (__wine_ps5_set_payload_thread_start, wine/ps5/pw_wine_threads.h). Mesa's and RADV's threads, made with no attributes
 * by Windows threads, get the program plan only if the kernel inherits it: the loader's "inherit=" line says.
 * Each thread is reported twice: as the program sees it (pthread_getschedparam, libthr's cached copy) and as the
 * kernel does (rtprio_thread, type/priority), since the first can say RR while the kernel kept FIFO.
 *
 * Portable: no system calls, no allocation, never aborts. */
#include <stddef.h>
#include <stdint.h>

enum {
    PW_HELPER_SCHED_HIGHEST = 256,  /* the console's scale: 256 is the highest priority, 767 the lowest */
    PW_HELPER_SCHED_LOWEST = 767,
    PW_HELPER_SCHED_FIFO = 1,       /* the only policies the console takes (policy 2 is EINVAL, r4-sched-02) */
    PW_HELPER_SCHED_RR = 3,
    PW_HELPER_SCHED_SERVICE_DEFAULT = 700,   /* WINE_PS5_HELPER_PRIORITY unset: see above */
    PW_HELPER_SCHED_PROGRAM_DEFAULT = 767,
};
enum { PW_HELPER_SCHED_SERVICE, PW_HELPER_SCHED_PROGRAM };

typedef struct PwHelperSched {
    int round_robin;    /* 1 for rr, 0 for keep */
    int service;        /* S, or 0 when unset */
    int program;        /* P, or 0 when unset */
    uint64_t cpus;      /* the mask, or 0 when unset */
    int census;         /* 1 when asked */
    int error;          /* 0, or EINVAL when a value was refused */
} PwHelperSched;

/* Parses NAME=VALUE entries, each NUL-terminated, up to an empty one or `bytes`; never reads past `bytes`, and an entry
 * not terminated within them is left out. block may be NULL when bytes is 0. */
void pw_helper_sched_read(const char *block, size_t bytes, PwHelperSched *out);

typedef struct PwHelperSchedPlan {
    int change;         /* 0: leave the thread as it is */
    int policy, priority;
    uint64_t cpus;      /* 0: leave its CPUs */
} PwHelperSchedPlan;

/* The plan for a tier, from the main thread's policy at entry, a plain new thread's policy and priority, and the CPUs
 * the helper may run on (0 when unknown). keep: no change. SERVICE: RR at S when S is set, else no change. PROGRAM: RR
 * at P, or at the plain thread's priority when P is unset; cpus as asked. A policy other than FIFO or RR, or a plain
 * priority outside 256..767, is a class nothing measured: no change, EINVAL. Asked cpus not inside entry_cpus are
 * left 0 (the policy part stands), EINVAL. Returns 0 or EINVAL; *out is always written. */
int pw_helper_sched_plan(const PwHelperSched *settings, int tier, int entry_policy, int plain_policy, int plain_priority,
                         uint64_t entry_cpus, PwHelperSchedPlan *out);

/* The census's choice of processes: those a helper's busy threads can take CPUs from. elfldr's children (klogsrv,
 * ftpsrv, kstuff, shadowmountplus, pldmgr, ...) and websrv's (ps5vkctl and the helpers), websrv and elfldr themselves,
 * the title (eboot.bin), SceShell* and SceSysCore*. websrv or elfldr may be -1 when unknown. 1 when wanted. */
int pw_helper_sched_wanted(const char *comm, int pid, int ppid, int websrv, int elfldr);

/* A process's threads counted by what the kernel says of each: rtprio type and priority, and its CPUs. A failed lookup
 * is counted as type 0 (no class is 0) with its errno as the priority. Adds one to the matching group, or starts a
 * group while fewer than cap exist: 0, or -1 when the thread fits no group. */
typedef struct PwHelperSchedGroup { unsigned type, priority; uint64_t cpus; unsigned threads; } PwHelperSchedGroup;
int pw_helper_sched_group(PwHelperSchedGroup *groups, unsigned cap, unsigned *count, unsigned type, unsigned priority,
                          uint64_t cpus);

/* A Windows thread's native priority in the game the title runs itself (Alpha 8, WINE_PS5_SCHED_TITLE=P; Wine patch
 * 2432). Every thread of that game runs SCHED_RR at P (767, the lowest: patch 2405), so no game thread can keep the
 * title's own (SCHED_FIFO 700: its loop, the server, the broker, input) off a CPU, which froze a tester's console. On
 * that floor alone Windows priorities did nothing: a time-critical thread woke as late as a normal one (p99 5.3 ms,
 * intitle-priority-title-base-2026-11-01-01). Above-normal bands now go one native step up each, as patch 0896 maps them
 * from 700, but from P and never above 701: still below every title thread. Normal and below stay at P, since the
 * console has no boost for a starved thread. effective: Wine's effective priority (class base plus the thread's). */
static inline int pw_helper_sched_windows_priority(int effective, int program)
{
    const int floor = PW_HELPER_SCHED_SERVICE_DEFAULT + 1;
    int step;
    if (program < floor || program > PW_HELPER_SCHED_LOWEST) program = PW_HELPER_SCHED_PROGRAM_DEFAULT;
    if (effective <= 8) step = 0;           /* normal and below */
    else if (effective == 9) step = 1;      /* above normal */
    else if (effective == 10) step = 2;     /* highest */
    else if (effective <= 12) step = 3;
    else if (effective <= 14) step = 4;     /* the high priority class */
    else if (effective == 15) step = 5;     /* time critical */
    else step = 6;                          /* the realtime class */
    return program - step < floor ? floor : program - step;
}

#endif
