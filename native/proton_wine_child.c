/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Headless service image: no graphics, independent server, or native exit. */
#include "../wine/ps5/pw_wine_process.h"
#include "../wine/ps5/pw_wine_process_io.h"
#include "../wine/ps5/pw_wine_sink.h"
#include "../wine/ps5/pw_wine_perf.h"
#include "../wine/ps5/pw_wine_threads.h"
#include "../src/pw_shared_page.h"
#include "../src/pw_memory_helper.h"
#include "../src/pw_memory_service.h"
#include "../src/pw_wine_start.h"
#include "../src/pw_environment.h"
#include "../src/pw_gpu_broker.h"
#include "../src/pw_control_watch.h"
#include "../src/pw_suspend.h"
#include "../src/pw_vm_census.h"
#include "../src/pw_helper_sched.h"
#include "../src/pw_cpu_placement.h"
#include "../src/pw_log_sessions.h"
#include "../src/pw_fd_census.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/event.h>
#ifdef PROTON_CHILD_GPU_DEVICE_QUERY
#include <sys/ioctl.h>
#include "proton_gpu_identity.h"
#endif
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/rtprio.h>
#include <ps5platform/kernel.h>
#include "pw_prx_elf_ps5.h"
#include "../src/pw_firmware.h"
#include "../src/pw_warm_helper.h"
#include "../wine/ps5/pw_wine_dl.h"
#include <sys/sysctl.h>
#include <sys/thr.h>
#include <sys/user.h>
#include <ps5platform/elevation.h>

extern int sceKernelUsleep(unsigned);
extern int sceKernelLoadStartModule(const char *, size_t, const void *, unsigned, const void *, int *);
extern int sceKernelReserveVirtualRange(void **, size_t, int, size_t);
extern int sceKernelMunmap(void *, size_t);
extern int sceKernelGetModuleInfo(int, void *);
#ifdef PROTON_CHILD_GPU_PREFLIGHT
extern int sceKernelDlsym(int,const char *,void **);
extern int sceSysmoduleLoadModuleByNameInternal(const char *,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long);
#endif
static int output_fd = -1;
static unsigned child_firmware;
static void output(const char *text, size_t size)
{
    while (size && output_fd >= 0) {
        ssize_t count = write(output_fd, text, size);
        if (count <= 0) { if (errno == EINTR) continue; break; }
        text += count; size -= count;
    }
}
/* Names the loader sets itself, from its descriptors and paths: an application's settings never replace them.
 * WINE_PS5_NTSYNC is not here: the loader no longer sets it (WINE_PS5_CHILDREN keeps the device off), and
 * pw_app_environment.c keeps a catalog setting of that name out of the block in the first place. */
static int loader_owned(const char *name)
{
    static const char *const owned[]={"WINESERVERSOCKET","WINE_PS5_CHILD","WINE_PS5_CHILDREN",
        "WINEDLLPATH","WINEDLLOVERRIDES","WINEDEBUG","WINE_PS5_PAYLOAD_CHILD","WINE_PS5_INITIAL_CLIENT",
        "WINE_PS5_GPU_FD","WINE_PS5_GPU_BROKER","WINE_PS5_GPU_RESERVED","WINEPREFIX","WINE_PS5_NTDLL_DIR"};
    for(size_t i=0;i<sizeof(owned)/sizeof(owned[0]);++i) if(!strcmp(name,owned[i]))return 1;
    return 0;
}
static void stage(const char *text) { output(text, strlen(text)); }
static long long payload_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return now.tv_sec * 1000ll + now.tv_nsec / 1000000;
}
/* A helper's start, step by step, on the monotonic clock the title's spawn line and Wine's +timestamp share
 * (tools/start_stages.py reads them): main and control are taken before the log exists and written after it opens. */
static long long start_main_ms = -1, start_control_ms = -1;
static void start_stamp(const char *at, long long ms)
{
    char line[96];
    snprintf(line, sizeof(line), "wine-child: start at=%s ms=%lld\n", at, ms >= 0 ? ms : payload_monotonic_ms());
    stage(line);
}
static int set_env(const char *key, const char *value) { return setenv(key, value, 1); }
static PwWineStart wine;
static int wine_argc=2;
static char *wine_argv[65]={"wine","child.exe",NULL};
#ifdef PROTON_CHILD_GPU_DEVICE_QUERY
static void gpu_device_query(const char *phase,int inherited)
{
    char identity[256];
    proton_gpu_identity(identity,sizeof(identity),phase);stage(identity);fsync(output_fd);
    errno=0;
    int fd=inherited>=0 ? inherited : open("/dev/gc",O_RDWR),error=errno;
    char line[160];
    snprintf(line,sizeof(line),"wine-child: gpu device %s uid=%u euid=%u fd=%d errno=%d\n",
             phase,(unsigned)getuid(),(unsigned)geteuid(),fd,error);
    stage(line);fsync(output_fd);
    if(inherited>=0) {
        unsigned value=0;errno=0;
        int status=ioctl(fd,0xc004812eUL,&value);error=errno;
        snprintf(line,sizeof(line),"wine-child: gpu inherited %s ioctl=%d errno=%d value=%#x\n",phase,status,error,value);
        stage(line);fsync(output_fd);
    } else if(fd>=0)close(fd);
}
#endif
#ifdef PROTON_CHILD_GPU_PREFLIGHT
static int gpu_preflight(void)
{
    /* ByName does not resolve the initializer's dependencies for us. VideoOut
     * remains disabled: this probe checks AGC initialization before display. */
    const char *modules[]={"libSceRegMgr","libSceAgcDriver","libSceAgc"};
    int status=0;char line[160];
    for(unsigned i=0;i<3;++i) {
        snprintf(line,sizeof(line),"wine-child: gpu preflight before Sysmodule %s load\n",modules[i]);stage(line);fsync(output_fd);
        status=sceSysmoduleLoadModuleByNameInternal(modules[i],0,0,0,0,0);
        snprintf(line,sizeof(line),"wine-child: gpu preflight Sysmodule %s status=%#x\n",modules[i],status);stage(line);fsync(output_fd);
        if(status<0)return -1;
    }
    /* ByName returns the handle (RegMgr returned 0x14), not a zero status. */
    int agc=status;
    int (*init)(unsigned)=NULL;
    if(sceKernelDlsym(agc,"sceAgcInit",(void **)&init) || !init)
        (void)sceKernelDlsym(agc,"kW3GLb7QfPg",(void **)&init);
    if(!init) { stage("wine-child: gpu preflight missing init\n");return -1; }
    stage("wine-child: gpu preflight before AGC init\n");fsync(output_fd);
    status=init(8);
    snprintf(line,sizeof(line),"wine-child: gpu preflight AGC init status=%#x\n",status);stage(line);fsync(output_fd);
    return status ? -1 : 0;
}
#endif
/* Helper scheduling (src/pw_helper_sched.h): the settings, the program's plan and what became of it. The lines
 * written here are read by tools/helper_sched.py and checked against these formats by tests/test_sched_watch.py. */
static PwHelperSched helper_sched;
static PwHelperSchedPlan program_plan;
static size_t cpuset_bytes;   /* the CPU set size the kernel answers to, found before any thread of ours */
static long main_tid;
static unsigned program_set, program_failed;
static int program_error;
/* A thread as the program sees it and as the kernel does; a refused read is -errno, unknown CPUs 0. */
typedef struct SchedView { int policy, priority, kernel_type, kernel_priority; uint64_t cpus; } SchedView;

/* The CPU set size the kernel answers to, found once before any thread of ours: this SDK's cpuset_t is not its size
 * (native/proton_wine.cpp). */
static size_t sched_cpuset_size(void)
{
    static const size_t sizes[] = {8, 16, 24, 32, 64};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && !cpuset_bytes; ++i) {
        _Alignas(8) unsigned char set[64] = {0};
        if (!cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizes[i], (cpuset_t *)(void *)set)) cpuset_bytes = sizes[i];
    }
    return cpuset_bytes;
}

/* CPUs 0-63 of a thread or process (CPU_WHICH_TID or CPU_WHICH_PID; -1 for the caller), 0 when refused. */
static uint64_t sched_cpus(int which, long id)
{
    _Alignas(8) unsigned char set[64] = {0};
    uint64_t bits = 0;
    if (!cpuset_bytes || cpuset_getaffinity(CPU_LEVEL_WHICH, which, (id_t)id, cpuset_bytes, (cpuset_t *)(void *)set)) return 0;
    memcpy(&bits, set, sizeof(bits));
    return bits;
}
static void sched_self(SchedView *view)
{
    struct sched_param param = {0};
    struct rtprio kernel = {0};
    const int error = pthread_getschedparam(pthread_self(), &view->policy, &param);
    if (error) view->policy = -error;
    view->priority = error ? -error : param.sched_priority;
    if (rtprio_thread(RTP_LOOKUP, 0, &kernel)) view->kernel_type = view->kernel_priority = -errno;
    else { view->kernel_type = kernel.type; view->kernel_priority = kernel.prio; }
    view->cpus = sched_cpus(CPU_WHICH_TID, -1);
}
static void *sched_plain_thread(void *view) { sched_self(view); return NULL; }
/* What a thread made with no attributes gets, as Mesa's, RADV's and Wine's own threads are made. Every signal stays
 * blocked in it, so no Wine handler runs on a thread without a TEB. */
static void sched_plain(SchedView *view)
{
    sigset_t all, before;
    pthread_t thread;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &before);
    int error = pthread_create(&thread, NULL, sched_plain_thread, view);
    pthread_sigmask(SIG_SETMASK, &before, NULL);
    if (!error) error = pthread_join(thread, NULL);
    if (error) *view = (SchedView){-error, -error, -error, -error, 0};
}
/* The plan on the calling thread: 0 or an errno. */
static int sched_apply_self(const PwHelperSchedPlan *plan)
{
    const struct sched_param param = {.sched_priority = plan->priority};
    int error = pthread_setschedparam(pthread_self(), plan->policy, &param);
    if (!error && plan->cpus) {
        _Alignas(8) unsigned char set[64] = {0};
        memcpy(set, &plan->cpus, sizeof(plan->cpus));
        if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, cpuset_bytes, (const cpuset_t *)(const void *)set)) error = errno;
    }
    return error;
}
/* Each Windows thread on itself as it registers (wine/ps5/pw_wine_threads.h): no lock, no allocation. */
static void program_thread_start(void)
{
    const int error = sched_apply_self(&program_plan);
    if (!error) { __atomic_add_fetch(&program_set, 1, __ATOMIC_RELAXED); return; }
    __atomic_store_n(&program_error, error, __ATOMIC_RELAXED);
    if (__atomic_fetch_add(&program_failed, 1, __ATOMIC_RELAXED)) return;
    char line[96];
    snprintf(line, sizeof(line), "wine-child: sched a Windows thread kept its scheduling: error %d\n", error);
    stage(line);
}
static void sched_tally(void)
{
    char line[112];
    snprintf(line, sizeof(line), "wine-child: sched program threads set=%u failed=%u last_error=%d\n",
             __atomic_load_n(&program_set, __ATOMIC_RELAXED), __atomic_load_n(&program_failed, __ATOMIC_RELAXED),
             __atomic_load_n(&program_error, __ATOMIC_RELAXED));
    stage(line);
}
/* src/pw_cpu_placement.h with the console's calls: read the main thread's affinity and the process's CPU set, make a
 * set under the root set, narrow it if the kernel permits, move the process into it, and narrow this thread to the
 * title's CPUs. Every helper's own set is 0xea00 while the title has 0-12; plain affinity cannot leave a set,
 * cpuset()/cpuset_setid() can (docs/PLAN.md). A process-wide cpuset_setaffinity is EPERM in a payload, so WHICH is the
 * calling (main) thread, and the set-wide mask is attempted through CPU_WHICH_CPUSET and may be refused. */
static int placement_affinity_which;   /* CPU_WHICH_TID or CPU_WHICH_PID, the call that finally narrowed it */
static int placement_read(int level, uint64_t *mask, int *error)
{
    static const cpulevel_t levels[] = {CPU_LEVEL_WHICH, CPU_LEVEL_CPUSET, CPU_LEVEL_ROOT};
    _Alignas(8) unsigned char set[64] = {0};
    *mask = 0;
    if (level < 0 || level > 2) { *error = EINVAL; return -1; }
    const size_t bytes = sched_cpuset_size();
    if (!bytes) { *error = errno ? errno : ENOTSUP; return -1; }
    const cpuwhich_t which = level == PW_CPU_PLACEMENT_WHICH ? CPU_WHICH_TID : CPU_WHICH_PID;
    if (cpuset_getaffinity(levels[level], which, -1, bytes, (cpuset_t *)(void *)set)) { *error = errno; return -1; }
    memcpy(mask, set, sizeof(*mask));
    return 0;
}
static int placement_create(int *set_id, int *error)
{
    cpusetid_t id;
    if (cpuset(&id)) { *error = errno; return -1; }
    *set_id = (int)id;
    return 0;
}
static int placement_get(int *set_id, int *error)
{
    cpusetid_t id;
    if (cpuset_getid(CPU_LEVEL_CPUSET, CPU_WHICH_PID, -1, &id)) { *error = errno; return -1; }
    *set_id = (int)id;
    return 0;
}
static int placement_set_mask(int set_id, uint64_t mask, int *error)
{
    _Alignas(8) unsigned char set[64] = {0};
    const size_t bytes = sched_cpuset_size();
    if (!bytes) { *error = ENOTSUP; return -1; }
    memcpy(set, &mask, sizeof(mask));
    if (cpuset_setaffinity(CPU_LEVEL_CPUSET, CPU_WHICH_CPUSET, (id_t)set_id, bytes,
                          (const cpuset_t *)(const void *)set)) {
        *error = errno;
        return -1;
    }
    return 0;
}
static int placement_move(int set_id, int *error)
{
    if (cpuset_setid(CPU_WHICH_PID, -1, (cpusetid_t)set_id)) { *error = errno; return -1; }
    return 0;
}
static int placement_affinity(uint64_t mask, int *error)
{
    static const cpuwhich_t order[] = {CPU_WHICH_TID, CPU_WHICH_PID};
    _Alignas(8) unsigned char set[64] = {0};
    const size_t bytes = sched_cpuset_size();
    if (!bytes) { *error = ENOTSUP; return -1; }
    memcpy(set, &mask, sizeof(mask));
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if (!cpuset_setaffinity(CPU_LEVEL_WHICH, order[i], -1, bytes, (const cpuset_t *)(const void *)set)) {
            placement_affinity_which = order[i];
            return 0;
        }
        *error = errno;
    }
    return -1;
}
static const PwCpuPlacementOps placement_ops = {placement_read, placement_create, placement_get, placement_set_mask,
                                               placement_move, placement_affinity};
/* The switch is WINE_PS5_HELPER_CPUS_WIDEN=1 in the bootstrap's settings block, off by default. The memory helper is not
 * a program: it keeps its service CPUs and leaves the title's to the game. The line says what happened; the census in
 * pw_ps5_child_start reads the result once Wine runs. */
static void sched_place_cpus(unsigned flags, const char *block, size_t bytes)
{
    static const char *const names[] = {"off", "already", "moved", "failed"};
    PwCpuPlacement placement;
    const int enabled = !(flags & PW_CHILD_MEMORY) && pw_cpu_placement_enabled(block, bytes);
    placement_affinity_which = 0;
    const int status = pw_cpu_placement_apply(&placement_ops, enabled, PW_CPU_PLACEMENT_TITLE_CPUS, &placement);
    char line[448];
    snprintf(line, sizeof(line),
             "wine-child: cpus placement status=%s target=%#llx before=%#llx/%#llx root=%#llx after=%#llx/%#llx/%#llx "
             "old_set=%d new_set=%d set_mask_errno=%d narrow_errno=%d affinity_which=%d restored=%d error=%d errno=%d\n",
             names[status ? 3 : placement.status], (unsigned long long)PW_CPU_PLACEMENT_TITLE_CPUS,
             (unsigned long long)placement.affinity, (unsigned long long)placement.set, (unsigned long long)placement.root,
             (unsigned long long)placement.after_affinity, (unsigned long long)placement.after_set,
             (unsigned long long)placement.after_root, placement.old_set, placement.new_set, placement.set_mask_error,
             placement.narrow_error, placement_affinity_which, placement.restore_failed ? 1 : 0, placement.error,
             placement.errno_value);
    stage(line);
}

/* Before any thread of this loader: the CPU placement's own line (src/pw_cpu_placement.h), then the settings, this
 * thread's scheduling at entry and a plain new thread's, both plans, and the service plan on this thread, which the
 * loader's later threads ask to inherit. */
static void sched_entry(const char *kind, const char *block, size_t bytes)
{
    SchedView entry, plain, now;
    PwHelperSchedPlan service;
    char line[512];
    pw_helper_sched_read(block, bytes, &helper_sched);
    (void)sched_cpuset_size();
    (void)thr_self(&main_tid);
    sched_self(&entry);
    sched_plain(&plain);
    const int service_error = pw_helper_sched_plan(&helper_sched, PW_HELPER_SCHED_SERVICE, entry.policy, plain.policy,
                                                   plain.priority, entry.cpus, &service);
    const int plan_error = pw_helper_sched_plan(&helper_sched, PW_HELPER_SCHED_PROGRAM, entry.policy, plain.policy,
                                                plain.priority, entry.cpus, &program_plan);
    const int error = helper_sched.error ? helper_sched.error : service_error ? service_error : plan_error;
    const int service_status = service.change ? sched_apply_self(&service) : 0;
    sched_self(&now);
    snprintf(line, sizeof(line), "wine-child: sched kind=%s mode=%s main_tid=%ld entry=%d/%d entry_kernel=%d/%d "
             "entry_cpus=%#llx plain=%d/%d plain_kernel=%d/%d plain_cpus=%#llx service=%d program=%d program_cpus=%#llx "
             "census=%d error=%d service_status=%d now=%d/%d now_kernel=%d/%d\n",
             kind, helper_sched.round_robin ? "rr" : "keep", main_tid, entry.policy, entry.priority, entry.kernel_type,
             entry.kernel_priority, (unsigned long long)entry.cpus, plain.policy, plain.priority, plain.kernel_type,
             plain.kernel_priority, (unsigned long long)plain.cpus, service.change ? service.priority : 0,
             program_plan.change ? program_plan.priority : 0, (unsigned long long)program_plan.cpus, helper_sched.census,
             error, service_status, now.policy, now.priority, now.kernel_type, now.kernel_priority);
    stage(line);
}
/* A detached native thread of the loader that inherits the main thread's scheduling (a refused inherit leaves
 * libkernel's default), with every signal blocked in it. 0 or an errno. */
static int native_thread(void *(*run)(void *), size_t stack)
{
    pthread_attr_t attributes;
    pthread_t thread;
    sigset_t all, before;
    int status = pthread_attr_init(&attributes);
    if (status) return status;
    status = pthread_attr_setstacksize(&attributes, stack);
    if (!status) status = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    (void)pthread_attr_setinheritsched(&attributes, PTHREAD_INHERIT_SCHED);
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &before);
    if (!status) status = pthread_create(&thread, &attributes, run, NULL);
    pthread_sigmask(SIG_SETMASK, &before, NULL);
    pthread_attr_destroy(&attributes);
    return status;
}
/* What the stall watch, the census and the backstop read of ntdll and the stall watch stamps: lock-free exports only
 * (wine/ps5/pw_wine_perf.h, __wine_ps5_dmem_holder, wine/ps5/pw_wine_process.h), published once ntdll is loaded;
 * log_sync is older than that. doom_names in PW_CHILD_DOOM_* order. */
static void (*watch_vlock)(PwWinePerfVlock *);
static unsigned (*watch_dmem)(uint64_t *, unsigned);
static unsigned (*watch_doom)(int64_t *, unsigned);
static void (*watch_doomed)(unsigned);
static const char *const doom_names[PW_CHILD_DOOMS] = {"gpu", "exit", "engine", "test", "stall"};
/* The STALL stamp's owners (the virtual mutex's, the engine lock's) and when the next thread listing is due (0: none):
 * the log thread's own, set by stall_watch and read by stall_threads. */
static uint64_t stall_owners[2];
static long long stall_list_ms;
/* The app's time away (src/pw_suspend.h), as this helper sees it: the title's word and beat in this helper's input page
 * (mapped read-only once made), and the control thread's own clock, whose gap shows a freeze nobody announced. While the
 * app is away or just back, nothing here ends the helper: its direct memory may still be under GPU work the system
 * froze with the app. The control thread writes; the log thread reads the two words. */
static const PwSuspendPage *title_word;
static PwSuspendClock helper_suspend;
static uint32_t title_beat_seen;
static uint64_t title_beat_at;
static long long suspend_resumed_at;
static int suspend_away_now;
static int helper_suspend_tick(long long now)
{
    int away = -1;
    if (title_word) away = pw_suspend_page_away(title_word, &title_beat_seen, &title_beat_at, (uint64_t)now);
    const long long before = (long long)helper_suspend.last_ms;
    const int change = pw_suspend_tick(&helper_suspend, (uint64_t)now, away);
    if (change != PW_SUSPEND_NONE) {
        char line[160];
        if (change == PW_SUSPEND_GAP)
            snprintf(line, sizeof(line), "wine-child: suspend gap: this helper was frozen %lld ms; nothing is ended until "
                     "%u ms after the return\n", now - before, (unsigned)PW_SUSPEND_GRACE_MS);
        else
            snprintf(line, sizeof(line), "wine-child: suspend %s at %lld ms\n", change == PW_SUSPEND_LEFT ?
                     "left: the title says the app is away, or its beat stopped" : "back", now);
        stage(line);
    }
    __atomic_store_n(&suspend_resumed_at, (long long)helper_suspend.resumed_ms, __ATOMIC_RELAXED);
    __atomic_store_n(&suspend_away_now, (int)helper_suspend.away, __ATOMIC_RELAXED);
    return pw_suspend_hold(&helper_suspend, (uint64_t)now);
}
/* The log thread's view: 1 while held, and *since_us, a hold may count no further back than the last return. */
static int helper_suspend_held(long long now, uint64_t *since_us)
{
    const long long resumed = __atomic_load_n(&suspend_resumed_at, __ATOMIC_RELAXED);
    *since_us = resumed && now > resumed ? (uint64_t)(now - resumed) * 1000 : resumed ? 0 : UINT64_MAX;
    return __atomic_load_n(&suspend_away_now, __ATOMIC_RELAXED) || (resumed && now < resumed + PW_SUSPEND_GRACE_MS);
}
/* One line, each second it holds, when Wine's virtual mutex has been held 2 s or more, the direct-memory engine's lock
 * 2 s or more, or threads waited on the mutex at three looks in a row: who holds what, doing what, since when. In
 * a 2026-10-15 run (record removed from the tree) the census waited 160 s on that mutex and no record named its holder. A look counts as waited
 * only when a waiter also got through since the last one: a thread ended while it waits stays in waiters for good
 * (pw_wine_perf.h). At most 300 lines a process. 1 when it wrote one. The first look at a hold of 3 s
 * (pw_control_stalled: the pager's ask and holds after EXIT left out of the mutex's) stamps STALL, before its line,
 * and asks stall_threads for its listings. */
static int stall_watch(void)
{
    static PwWinePerfVlock before;
    static unsigned waited, lines;
    void (*vlock)(PwWinePerfVlock *) = __atomic_load_n(&watch_vlock, __ATOMIC_ACQUIRE);
    unsigned (*dmem)(uint64_t *, unsigned) = __atomic_load_n(&watch_dmem, __ATOMIC_ACQUIRE);
    unsigned (*doom)(int64_t *, unsigned) = __atomic_load_n(&watch_doom, __ATOMIC_ACQUIRE);
    PwWinePerfVlock now;
    static const char *const doing_names[] = {"-", "allocate", "map", "protect", "release", "unmap", "reserve", "lay",
                                              "foreign"};
    uint64_t holder[7] = {0, 0, 0, 0, 0, 0, (uint64_t)-1};
    int64_t stamps[PW_CHILD_DOOMS] = {0};
    char line[480], dooms[32] = "-";
    if (!vlock) return 0;
    vlock(&now);
    if (dmem) (void)dmem(holder, 7);
    if (doom) (void)doom(stamps, PW_CHILD_DOOMS);   /* EXIT's, for the rule; read again for the line */
    /* A hold that began before a freeze counts from the return (src/pw_suspend.h): a suspend is not a stall. */
    uint64_t since_us;
    const int held = helper_suspend_held(payload_monotonic_ms(), &since_us);
    const uint64_t vlock_us = now.held_us < since_us ? now.held_us : since_us,
                   dmem_us = holder[1] < since_us ? holder[1] : since_us;
    if (!stall_list_ms && !held && pw_control_stalled(now.owner, vlock_us, now.doing, stamps[PW_CHILD_DOOM_EXIT - 1],
                                                       holder[0], dmem_us)) {
        void (*doomed)(unsigned) = __atomic_load_n(&watch_doomed, __ATOMIC_ACQUIRE);
        if (doomed) doomed(PW_CHILD_DOOM_STALL);
        stall_owners[0] = now.owner;
        stall_owners[1] = holder[0];
        stall_list_ms = payload_monotonic_ms();
    }
    waited = now.waiters && now.contended != before.contended ? waited + 1 : 0;
    const int wrote = (now.held_us >= 2000000 || holder[1] >= 2000000 || waited >= 3) && lines < 300;
    if (wrote) {
        int used = 0;
        if (doom) (void)doom(stamps, PW_CHILD_DOOMS);
        for (unsigned i = 0; i < PW_CHILD_DOOMS; ++i)
            if (stamps[i]) used += snprintf(dooms + used, sizeof(dooms) - used, "%s%s", used ? "," : "", doom_names[i]);
        snprintf(line, sizeof(line), "wine-child: stall vlock owner=%llu held_ms=%llu site=%#llx doing=%s:%llu "
                 "waiters=%llu acquires=+%llu contended=+%llu changes=+%llu dmem_lock=%llu:%llu "
                 "dmem_doing=%s:%#llx+%#llx:%llu:%#llx doom=%s\n",
                 (unsigned long long)now.owner, (unsigned long long)now.held_us / 1000, (unsigned long long)now.site,
                 now.doing ? now.doing : "-", (unsigned long long)now.doing_us / 1000, (unsigned long long)now.waiters,
                 (unsigned long long)(now.acquires - before.acquires),
                 (unsigned long long)(now.contended - before.contended),
                 (unsigned long long)(now.changes - before.changes), (unsigned long long)holder[0],
                 (unsigned long long)holder[1] / 1000,
                 holder[2] < sizeof(doing_names) / sizeof(doing_names[0]) ? doing_names[holder[2]] : "?",
                 (unsigned long long)holder[3], (unsigned long long)holder[4], (unsigned long long)holder[5],
                 (unsigned long long)holder[6], dooms);
        stage(line);
        lines++;
    }
    before = now;
    return wrote;
}
/* The scheduling census (WINE_PS5_SCHED_CENSUS=1, only in the memory helper, as the memory census): the policy,
 * priority and CPUs of the processes a helper's busy threads could starve (pw_helper_sched_wanted), once about 5 s and
 * once about 60 s after the helper starts, on a thread of its own, so the memory census keeps its half-second stamp.
 * Read-only calls; a refused one prints -errno. Per process: its first thread's policy and priority
 * (sched_getscheduler, sched_getparam), the best of its threads (rtprio) and its CPUs; then its threads counted by what
 * the kernel says of each (rtprio_thread, cpuset by thread), when KERN_PROC_INC_THREAD lists them. Not verified on the
 * console's 1096-byte records: ki_tid at the SDK's offset 600 (the header's tids are this process's own as the listing
 * reads them, to compare with self_tid and main_tid) and ki_pri at 604, four u_chars too small for 256-767, so raw600
 * carries those bytes as read. census_next and census_word serve the stall watch's thread records too, in every
 * helper. */
#ifdef PW_MEMORY_HELPER_CENSUS
static char *census_list(int what, int pid, size_t *bytes, int *error)
{
    int mib[4] = {CTL_KERN, KERN_PROC, what, pid};
    size_t size = 0;
    char *raw;
    *bytes = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0)) { *error = errno; return NULL; }
    size += size / 4 + 4096;   /* room for what started since */
    if (!(raw = malloc(size))) { *error = ENOMEM; return NULL; }
    if (sysctl(mib, 4, raw, &size, NULL, 0)) { *error = errno; free(raw); return NULL; }
    *error = 0;
    *bytes = size;
    return raw;
}
#endif
/* The next record, ki_structsize apart (1096 bytes on the console, the SDK's 1088; the fields read come first). */
static const unsigned char *census_next(const char *raw, size_t bytes, size_t *at, struct kinfo_proc *record, int *size)
{
    if (!raw || *at + sizeof(int) > bytes) return NULL;
    memcpy(size, raw + *at, sizeof(int));
    if (*size <= 0 || *at + (size_t)*size > bytes) return NULL;
    const unsigned char *start = (const unsigned char *)raw + *at;
    memset(record, 0, sizeof(*record));
    memcpy(record, start, (size_t)*size < sizeof(*record) ? (size_t)*size : sizeof(*record));
    *at += (size_t)*size;
    return start;
}
static void census_word(char *word, const char *from, size_t size)
{
    memcpy(word, from, size);
    word[size - 1] = 0;
    for (char *c = word; *c; ++c) if (*c <= ' ' || *c > '~') *c = '_';
    if (!*word) strcpy(word, "-");
}
/* The stall watch's thread records (docs/PLAN.md, "Kernel mprotect stall"): every thread of this process as the kernel
 * lists it (KERN_PROC_PID | KERN_PROC_INC_THREAD), at the STALL stamp's look and once more 2 s later: what the owner
 * and every other thread wait on (wmesg, wchan, lock), whether they run (stat: 2 running, 3 asleep, 6 waiting, 7 on a
 * lock; the CPU each last ran on; its CPU time, and in pass 2 what it used since pass 1) and at what priority
 * (rtprio_thread's answer for the tid). The owners are marked holds=vlock, dmem or vlock+dmem. The fields are the
 * SDK's layout over the console's records, as the flight recorder reads stat, wmesg, lastcpu and runtime of a process;
 * ki_tid is not verified there (the summary's self_tid is this thread's, to find among them) and rtprio_thread refuses
 * a wrong one. A payload got no thread records for another process (tools/console-processes.c): then the summary says
 * records=0 with the sysctl's errno and byte count. One static buffer, so nothing is allocated and no Wine lock is
 * taken; it runs after the stall line is synced, since the sysctl may wait in the stall as the memory census's
 * kern.proc walk did (2026-10-14; record removed from the tree), and the control thread ends the helper regardless. 1 when it
 * wrote a pass. */
static int stall_threads(void)
{
    static char raw[256u << 10];   /* 239 of the console's 1096-byte records; ENOMEM past that, with what fit */
    static int first_tid[256];
    static uint64_t first_runtime[256];
    static unsigned passes, firsts;
    const long long now = payload_monotonic_ms();
    if (!stall_list_ms || passes >= 2 || now < stall_list_ms) return 0;
    passes++;
    stall_list_ms = now + 2000;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, (int)getpid()};
    size_t bytes = sizeof(raw), at = 0;
    const int error = sysctl(mib, 4, raw, &bytes, NULL, 0) ? errno : 0;
    if (error && error != ENOMEM) bytes = 0;
    struct kinfo_proc thread;
    unsigned records = 0;
    int size;
    long self = 0;
    char line[384], name[sizeof(thread.ki_tdname)], wmesg[sizeof(thread.ki_wmesg)], lock[sizeof(thread.ki_lockname)];
    (void)thr_self(&self);
    while (census_next(raw, bytes, &at, &thread, &size)) {
        struct rtprio kernel = {0};
        const int refused = rtprio_thread(RTP_LOOKUP, thread.ki_tid, &kernel) ? errno : 0;
        const uint64_t owner = (uint64_t)(unsigned)thread.ki_tid;
        long long delta = -1;
        records++;
        if (passes == 1 && firsts < 256) {
            first_tid[firsts] = thread.ki_tid;
            first_runtime[firsts++] = thread.ki_runtime;
        }
        for (unsigned i = 0; passes == 2 && i < firsts; ++i)
            if (first_tid[i] == thread.ki_tid) delta = (long long)(thread.ki_runtime - first_runtime[i]);
        census_word(name, thread.ki_tdname, sizeof(name));
        census_word(wmesg, thread.ki_wmesg, sizeof(wmesg));
        census_word(lock, thread.ki_lockname, sizeof(lock));
        snprintf(line, sizeof(line), "wine-child: stall thread tid=%d stat=%d wmesg=%s wchan=%#llx lock=%s cpu=%d "
                 "runtime_us=%llu delta_us=%lld pri=%d/%d name=%s pass=%u holds=%s\n", thread.ki_tid,
                 thread.ki_stat, wmesg, (unsigned long long)(uintptr_t)thread.ki_wchan, lock, thread.ki_lastcpu,
                 (unsigned long long)thread.ki_runtime, delta, refused ? -refused : kernel.type,
                 refused ? -refused : kernel.prio, name, passes,
                 owner == stall_owners[0] && owner == stall_owners[1] ? "vlock+dmem"
                 : owner == stall_owners[0] ? "vlock" : owner == stall_owners[1] ? "dmem" : "-");
        stage(line);
    }
    snprintf(line, sizeof(line), "wine-child: stall threads pass=%u ms=%lld records=%u sysctl_errno=%d bytes=%llu "
             "self_tid=%ld vlock_owner=%llu dmem_owner=%llu\n", passes, now, records, error, (unsigned long long)bytes,
             self, (unsigned long long)stall_owners[0], (unsigned long long)stall_owners[1]);
    stage(line);
    return 1;
}
/* The log reaches the disk every 10 s from a native thread that takes no Wine lock, so a wedged program cannot stop
 * it, and a run that froze the console leaves all but its last seconds; a stall line is synced at once, and the stall's
 * thread records after it. Not the control loop: a slow fsync there would delay noticing that the owner closed. */
static unsigned log_syncs;
static void *log_sync(void *unused)
{
    (void)unused;
    for (unsigned tick = 1;; ++tick) {
        const struct timespec period = {1, 0};
        nanosleep(&period, NULL);
        if ((stall_watch() || tick % 10 == 0) && output_fd >= 0 && !fsync(output_fd))
            __atomic_add_fetch(&log_syncs, 1, __ATOMIC_RELAXED);
        if (stall_threads() && output_fd >= 0 && !fsync(output_fd))
            __atomic_add_fetch(&log_syncs, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}
#ifdef PW_MEMORY_HELPER_CENSUS
static void census_process(long long ms, const struct kinfo_proc *process, const unsigned char *raw, int size)
{
    const int pid = process->ki_pid;
    struct sched_param param = {0};
    struct rtprio best = {0};
    int policy = sched_getscheduler(pid);
    if (policy < 0) policy = -errno;
    const int priority = sched_getparam(pid, &param) ? -errno : param.sched_priority;
    const int best_error = rtprio(RTP_LOOKUP, pid, &best) ? errno : 0;
    PwHelperSchedGroup groups[16];
    unsigned count = 0, records = 0, dropped = 0;
    size_t bytes = 0, at = 0;
    int error, thread_size;
    struct kinfo_proc thread;
    char *threads = census_list(KERN_PROC_PID | KERN_PROC_INC_THREAD, pid, &bytes, &error);
    while (census_next(threads, bytes, &at, &thread, &thread_size)) {
        struct rtprio kernel = {0};
        const int failed = rtprio_thread(RTP_LOOKUP, thread.ki_tid, &kernel) ? errno : 0;
        records++;
        if (pw_helper_sched_group(groups, 16, &count, failed ? 0 : kernel.type, failed ? (unsigned)failed : kernel.prio,
                                  sched_cpus(CPU_WHICH_TID, thread.ki_tid)))
            dropped++;
    }
    free(threads);
    char comm[sizeof(process->ki_comm)], wmesg[sizeof(process->ki_wmesg)], raw600[33] = "-", line[512];
    census_word(comm, process->ki_comm, sizeof(comm));
    census_word(wmesg, process->ki_wmesg, sizeof(wmesg));
    if (size >= 616) for (int i = 0; i < 16; ++i) snprintf(raw600 + 2 * i, 3, "%02x", raw[600 + i]);
    snprintf(line, sizeof(line), "sched-census: ms=%lld pid=%d ppid=%d comm=%s stat=%d wmesg=%s threads=%d first_policy=%d "
             "first_prio=%d best_rtprio=%d/%d cpus=%#llx oncpu=%d lastcpu=%d thread_records=%u thread_errno=%d groups=%u "
             "dropped=%u raw600=%s\n", ms, pid, process->ki_ppid, comm, process->ki_stat, wmesg, process->ki_numthreads,
             policy, priority, best_error ? -best_error : best.type, best_error ? -best_error : best.prio,
             (unsigned long long)sched_cpus(CPU_WHICH_PID, pid), process->ki_oncpu, process->ki_lastcpu, records, error,
             count, dropped, raw600);
    stage(line);
    for (unsigned i = 0; i < count; ++i) {
        snprintf(line, sizeof(line), "sched-census: ms=%lld pid=%d group rtprio=%u/%u cpus=%#llx threads=%u\n", ms, pid,
                 groups[i].type, groups[i].priority, (unsigned long long)groups[i].cpus, groups[i].threads);
        stage(line);
    }
}
static void census_take(void)
{
    const long long ms = payload_monotonic_ms();
    const int websrv = (int)getppid();
    char tids[160] = "-", line[320];
    long self = 0;
    size_t bytes = 0, at = 0, used = 0;
    int error, size, elfldr = -1;
    unsigned records = 0;
    struct kinfo_proc record;
    const unsigned char *start;
    (void)thr_self(&self);
    char *raw = census_list(KERN_PROC_PID | KERN_PROC_INC_THREAD, getpid(), &bytes, &error);
    while (census_next(raw, bytes, &at, &record, &size))
        if (records++ < 16 && used < sizeof(tids) - 12)
            used += (size_t)snprintf(tids + used, sizeof(tids) - used, "%s%d", used ? "," : "", (int)record.ki_tid);
    free(raw);
    snprintf(line, sizeof(line), "sched-census: ms=%lld self_cpu=%d self_tid=%ld main_tid=%ld inc_thread_records=%u "
             "inc_thread_errno=%d tids=%s\n", ms, (int)sceKernelGetCurrentCpu(), self, main_tid, records, error, tids);
    stage(line);
    raw = census_list(KERN_PROC_PROC, 0, &bytes, &error);
    for (at = 0; census_next(raw, bytes, &at, &record, &size);)
        if (!strcmp(record.ki_comm, "elfldr.elf")) elfldr = record.ki_pid;
    for (at = 0; (start = census_next(raw, bytes, &at, &record, &size));)
        if (pw_helper_sched_wanted(record.ki_comm, record.ki_pid, record.ki_ppid, websrv, elfldr))
            census_process(ms, &record, start, size);
    free(raw);
    snprintf(line, sizeof(line), "sched-census: ms=%lld done processes_errno=%d websrv=%d elfldr=%d\n",
             payload_monotonic_ms(), error, websrv, elfldr);
    stage(line);
}
static void *census_thread(void *unused)
{
    const struct timespec first = {5, 0}, second = {55, 0};
    (void)unused;
    nanosleep(&first, NULL);
    census_take();
    nanosleep(&second, NULL);
    census_take();
    return NULL;
}
#endif
static void *wine_worker(void *unused)
{
    (void)unused;
    /* The program's plan on this thread, then what a plain thread made from it gets: Mesa's and RADV's threads are
     * made so by Windows threads. This thread takes the plan again as it registers, in wine_main. */
    SchedView worker, inherit;
    const int status = program_plan.change ? sched_apply_self(&program_plan) : 0;
    sched_self(&worker);
    sched_plain(&inherit);
    char line[320];
    snprintf(line, sizeof(line), "wine-child: sched worker=%d/%d worker_kernel=%d/%d worker_cpus=%#llx status=%d "
             "inherit=%d/%d inherit_kernel=%d/%d inherit_cpus=%#llx\n", worker.policy, worker.priority,
             worker.kernel_type, worker.kernel_priority, (unsigned long long)worker.cpus, status, inherit.policy,
             inherit.priority, inherit.kernel_type, inherit.kernel_priority, (unsigned long long)inherit.cpus);
    stage(line);
    start_stamp("wine-main", -1);
    wine.wine_main(wine_argc,wine_argv); // Created children get parameters from the server.
    for (;;) sceKernelUsleep(100000);
}
/* Two counts in 16 KiB pages, resident and mapped; both pointers are needed (src/pw_memory_helper.h). */
int sceKernelGetResidentFmemCount(int pid, uint64_t *resident, uint64_t *mapped);
int sceKernelVirtualQuery(const void *address, int flags, void *info, size_t size);
void pw_malloc_trace_start(const char *directory);
extern int malloc_stats_fast(void *) __attribute__((weak));
int mincore(const void *address, size_t length, char *vector);

static long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000ll + t.tv_nsec;
}

/* B3 (src/pw_vm_census.h): which mappings hold this helper's resident system memory. */
static int vm_census_query(void *context, uintptr_t address, uint8_t raw[PW_VM_RECORD])
{
    (void)context;
    const int status = sceKernelVirtualQuery((const void *)address, 1, raw, PW_VM_RECORD);
    if ((uint32_t)status == UINT32_C(0x8002000d)) return 1;  /* no mapping after address */
    return status ? -1 : 0;
}

/* The walk asks the kernel which pages of each mapping are resident (mincore), which holds the process's address-space
 * lock while it counts: for the mappings of a Windows emulator (a 14 GiB shared section, a 16 GiB host range, 1.6 TiB of
 * placeholders) one census took 9 to 16 s of every 21 s (2026-10-13; record removed from the tree), and the page faults and protection changes of
 * every other thread queued behind it. The walk now has a time budget, 100 ms unless WINE_PS5_CENSUS_MS says otherwise
 * (0 skips the resident count), and asks in 64 MiB steps; what is past the budget counts as not resident and is said so. */
static long long vm_census_deadline_ns[PW_VM_CLASSES];
static unsigned vm_census_cut, vm_census_cut_class[PW_VM_CLASSES];
static uint64_t vm_census_bytes_class[PW_VM_CLASSES];   /* resident bytes found, by class */

static uint64_t vm_census_resident(void *context, uintptr_t start, uintptr_t end, unsigned klass)
{
    static char vector[4096];
    const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uint64_t bytes = 0;
    (void)context;
    for (uintptr_t at = start; at < end;) {
        if (now_ns() >= vm_census_deadline_ns[klass]) { vm_census_cut++; vm_census_cut_class[klass]++; vm_census_bytes_class[klass] += bytes; return bytes; }
        const uintptr_t chunk = end - at < sizeof(vector) * page ? end - at : sizeof(vector) * page;
        const size_t pages = (size_t)((chunk + page - 1) / page);
        if (mincore((const void *)at, (size_t)chunk, vector)) return bytes;
        for (size_t i = 0; i < pages; i++) bytes += (vector[i] & 1) ? page : 0;
        at += chunk;
    }
    vm_census_bytes_class[klass] += bytes;
    return bytes;
}

/* The kernel's own map of this process (kern.proc.vmmap, as tools/console-memory.c map=PID reads it): its resident pages by region type
 * and protection, and the largest regions with their ranges and paths. The walk of our own mappings found 162 MiB of the 602 MiB the kernel
 * counted at the start-up jump of the signed-in client's browser (2026-10-24), none of it in direct or PROT_NONE mappings; this is the kernel's
 * own view of the rest. WINE_PS5_VMMAP=1; one sysctl of about 25 MiB of records, freed at once. */
static void vmmap_report(void (*say_line)(const char *))
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, (int)getpid()};
    size_t bytes = 0;
    char line[512];
    if (sysctl(mib, 4, NULL, &bytes, NULL, 0) || !bytes) {
        snprintf(line, sizeof(line), "wine-child: vmmap: size query failed, errno %d\n", errno);
        say_line(line);
        return;
    }
    bytes += bytes / 4 + (1u << 20);
    char *raw = malloc(bytes);
    if (!raw) { say_line("wine-child: vmmap: no memory for the records\n"); return; }
    if (sysctl(mib, 4, raw, &bytes, NULL, 0)) {
        snprintf(line, sizeof(line), "wine-child: vmmap: read failed, errno %d\n", errno);
        say_line(line);
        free(raw);
        return;
    }
    const unsigned long long page = (unsigned long long)sysconf(_SC_PAGESIZE);
    const size_t bytes_total = bytes;
    struct { unsigned long regions, resident, priv; unsigned long long span; } by_type[16] = {{0}};
    struct { unsigned long long start, end; int type, prot, resident; char path[48]; } top[8] = {{0}};
    unsigned count = 0, big_seen = 0;
    for (const char *cursor = raw; cursor + sizeof(int) <= raw + bytes; count++) {
        const struct kinfo_vmentry *entry = (const struct kinfo_vmentry *)cursor;
        const int size = entry->kve_structsize;
        if (size <= 0 || cursor + size > raw + bytes) break;
        cursor += size;
        const int kind = entry->kve_type >= 0 && entry->kve_type < 15 ? entry->kve_type : 15;
        by_type[kind].regions++;
        by_type[kind].resident += (unsigned long)entry->kve_resident;
        by_type[kind].priv += (unsigned long)entry->kve_private_resident;
        by_type[kind].span += entry->kve_end - entry->kve_start;
        unsigned at = 8;
        while (at > 0 && top[at - 1].resident < entry->kve_resident) at--;
        if (at < 8) {
            for (unsigned i = 7; i > at; i--) top[i] = top[i - 1];
            top[at].start = entry->kve_start;
            top[at].end = entry->kve_end;
            top[at].type = entry->kve_type;
            top[at].prot = entry->kve_protection;
            top[at].resident = entry->kve_resident;
            snprintf(top[at].path, sizeof(top[at].path), "%s", entry->kve_path);
            big_seen++;
        }
    }
    snprintf(line, sizeof(line), "wine-child: vmmap %u records of %zu bytes (the SDK's struct is %zu)\n", count, bytes, sizeof(struct kinfo_vmentry));
    say_line(line);
    for (unsigned k = 0; k < 16; k++) {
        if (!by_type[k].regions) continue;
        snprintf(line, sizeof(line), "wine-child: vmmap type %u: %lu regions, %llu MiB span, %llu MiB resident, %llu MiB private\n", k,
                 by_type[k].regions, by_type[k].span >> 20, (unsigned long long)by_type[k].resident * page >> 20,
                 (unsigned long long)by_type[k].priv * page >> 20);
        say_line(line);
    }
    for (unsigned i = 0; i < 8 && top[i].resident > 0; i++) {
        snprintf(line, sizeof(line), "wine-child: vmmap top %u: %#llx-%#llx type=%d prot=%#x resident=%llu MiB path=%s\n", i, top[i].start,
                 top[i].end, top[i].type, top[i].prot, (unsigned long long)top[i].resident * page >> 20, top[i].path);
        say_line(line);
    }
    /* What the libkernel names each of them (the walk above never listed the two largest): the exact query, then the next-mapping query. */
    for (unsigned i = 0; i < 3 && top[i].resident > 0; i++)
        for (int mode = 0; mode < 2; mode++) {
            uint8_t record[PW_VM_RECORD] = {0};
            PwVmRecord decoded;
            const int status = sceKernelVirtualQuery((const void *)(uintptr_t)top[i].start, mode, record, sizeof(record));
            if (status) {
                snprintf(line, sizeof(line), "wine-child: vmmap top %u query(%s) failed, status %#x\n", i, mode ? "next" : "exact", (unsigned)status);
            } else {
                pw_vm_record_decode(record, &decoded);
                snprintf(line, sizeof(line), "wine-child: vmmap top %u query(%s): %#lx-%#lx flags %#x prot %d type %d name \"%s\"\n", i,
                         mode ? "next" : "exact", (unsigned long)decoded.start, (unsigned long)decoded.end, decoded.flags, decoded.protection,
                         decoded.type, decoded.name);
            }
            say_line(line);
        }
    /* Sony's libc keeps its own account of the heap when a program asks for it (malloc_stats_fast fills a managed-size record, or
     * refuses when the program did not declare SCE_LIBC_INIT_MALLOC_MANAGED_SIZE): if the two big regions are its heap, its
     * current system size says so. Weak, so a link whose stub lacks it still builds. */
    {
        struct { unsigned short size, version; unsigned reserved; size_t max_system, current_system, max_inuse, current_inuse; } managed = {0};
        managed.size = sizeof(managed);
        managed.version = 1;
        if (malloc_stats_fast) {
            const int status = malloc_stats_fast(&managed);
            snprintf(line, sizeof(line), "wine-child: libc heap: status %#x system %zu MiB (max %zu) in use %zu MiB (max %zu)\n", (unsigned)status,
                     managed.current_system >> 20, managed.max_system >> 20, managed.current_inuse >> 20, managed.max_inuse >> 20);
        } else {
            snprintf(line, sizeof(line), "wine-child: libc heap: malloc_stats_fast is not in this link\n");
        }
        say_line(line);
    }
    /* The first resident pages of the two largest regions, read where mincore says they are in memory (a read of an absent page would
     * make one): a heap's header, bitmaps or pixel data tell what the memory is. */
    for (unsigned i = 0; i < 2 && top[i].resident > 0; i++) {
        unsigned char vec[64];
        const unsigned long long step = 16384;
        if (mincore((const void *)(uintptr_t)top[i].start, step * sizeof(vec), (char *)vec)) {
            snprintf(line, sizeof(line), "wine-child: vmmap top %u peek: mincore failed, errno %d\n", i, errno);
            say_line(line);
            continue;
        }
        unsigned shown = 0;
        for (unsigned k = 0; k < sizeof(vec) && shown < 3; k++) {
            if (!(vec[k] & 1)) continue;
            const unsigned char *at = (const unsigned char *)(uintptr_t)(top[i].start + step * k);
            int n = snprintf(line, sizeof(line), "wine-child: vmmap top %u peek +%#llx:", i, step * k);
            for (unsigned b = 0; b < 32; b++) n += snprintf(line + n, sizeof(line) - (size_t)n, " %02x", at[b]);
            snprintf(line + n, sizeof(line) - (size_t)n, "\n");
            say_line(line);
            shown++;
        }
        if (!shown) {
            snprintf(line, sizeof(line), "wine-child: vmmap top %u peek: none of the first %zu pages resident\n", i, sizeof(vec));
            say_line(line);
        }
    }
    /* The platform heap's range (PS5_PayloadSDK ps5platform/heap.h, from 0x2000000000): its segments, mapped direct memory, and what is resident in them. */
    {
        unsigned long long segments = 0, bytes = 0, resident = 0, largest = 0;
        for (const char *cursor = raw; cursor + sizeof(int) <= raw + bytes_total; ) {
            const struct kinfo_vmentry *entry = (const struct kinfo_vmentry *)cursor;
            const int size = entry->kve_structsize;
            if (size <= 0 || cursor + size > raw + bytes_total) break;
            cursor += size;
            if (entry->kve_start < 0x2000000000ull || entry->kve_start >= 0x4000000000ull) continue;
            if (!entry->kve_resident) continue;
            segments++;
            bytes += entry->kve_end - entry->kve_start;
            resident += (unsigned long long)entry->kve_resident * page;
            if (entry->kve_end - entry->kve_start > largest) largest = entry->kve_end - entry->kve_start;
        }
        snprintf(line, sizeof(line), "wine-child: vmmap platform heap range: %llu mappings with pages, %llu MiB mapped, %llu MiB resident, largest %llu MiB\n",
                 segments, bytes >> 20, resident >> 20, largest >> 20);
        say_line(line);
    }
    /* Where Sony's malloc puts new blocks, against the two largest regions: if they land inside, that heap is those regions. */
    {
        static const size_t sizes[] = {64, 4096, 65536, 1u << 20};
        void *blocks[4];
        int n = snprintf(line, sizeof(line), "wine-child: malloc probe:");
        for (unsigned i = 0; i < 4; i++) { blocks[i] = malloc(sizes[i]); n += snprintf(line + n, sizeof(line) - (size_t)n, " %zu->%p", sizes[i], blocks[i]); }
        snprintf(line + n, sizeof(line) - (size_t)n, "\n");
        say_line(line);
        for (unsigned i = 0; i < 4; i++) free(blocks[i]);
    }
    (void)big_seen;
    free(raw);
}

static void vm_census_report(void (*say_line)(const char *))
{
    static int layout_logged;
    static PwVmCensus census;
    char line[1024];
    if (!layout_logged) {
        uint8_t raw[PW_VM_RECORD] = {0};
        layout_logged = 1;
        if (!sceKernelVirtualQuery((const void *)0, 1, raw, sizeof(raw))) {
            size_t at = (size_t)snprintf(line, sizeof(line), "wine-child: vm record ");
            for (size_t i = 0; i < sizeof(raw) && at + 3 < sizeof(line); i++)
                at += (size_t)snprintf(line + at, sizeof(line) - at, "%02x", raw[i]);
            snprintf(line + at, sizeof(line) - at, "\n");
            say_line(line);
        }
    }
    /* WINE_PS5_CENSUS_MS: one number is one budget for the whole walk (100 ms before), "F:D:R" a budget for each class: flexible, direct,
     * reserved (src/pw_vm_census.h). 0 skips the resident count. */
    long long budget_ms[PW_VM_CLASSES];
    const int per_class = pw_vm_budget_parse(getenv("WINE_PS5_CENSUS_MS"), budget_ms);
    const long long walk_begin = now_ns();
    vm_census_cut = 0;
    memset(vm_census_cut_class, 0, sizeof(vm_census_cut_class));
    memset(vm_census_bytes_class, 0, sizeof(vm_census_bytes_class));
    for (unsigned c = 0; c < PW_VM_CLASSES; c++) vm_census_deadline_ns[c] = walk_begin + budget_ms[c] * 1000000ll;
    pw_vm_census_walk(&census, vm_census_query, (budget_ms[0] > 0 || budget_ms[1] > 0 || budget_ms[2] > 0) ? vm_census_resident : NULL, NULL);
    if (vm_census_cut) {
        snprintf(line, sizeof(line), "wine-child: vm census resident count cut after %lld ms in %u mappings\n", budget_ms[0], vm_census_cut);
        say_line(line);
    }
    /* What was measured, by class: mappings measured/all and resident MiB found, so a cut shows where it fell. */
    snprintf(line, sizeof(line), "wine-child: vm measured flexible=%u/%u resident %llu MiB direct=%u/%u resident %llu MiB reserved=%u/%u resident %llu MiB walk=%lld ms%s\n",
             census.class_mappings[0] - vm_census_cut_class[0], census.class_mappings[0], (unsigned long long)(vm_census_bytes_class[0] >> 20),
             census.class_mappings[1] - vm_census_cut_class[1], census.class_mappings[1], (unsigned long long)(vm_census_bytes_class[1] >> 20),
             census.class_mappings[2] - vm_census_cut_class[2], census.class_mappings[2], (unsigned long long)(vm_census_bytes_class[2] >> 20),
             (now_ns() - walk_begin) / 1000000, per_class > 0 ? " (budgets per class)" : "");
    say_line(line);
    /* About what this helper takes of the system's page-table pool (src/pw_vm_census.h, regions_2m). */
    {
        int at = snprintf(line, sizeof(line), "wine-child: vm page tables ~%llu (2 MiB regions its accessible mappings touch; "
                          "of them 2m-direct %llu) by group ", (unsigned long long)census.regions_2m,
                          (unsigned long long)census.regions_2m_direct);
        if (at > 0 && (size_t)at < sizeof(line) - 2) {
            size_t used = (size_t)at + pw_vm_census_format_regions(&census, 8, line + at, sizeof(line) - (size_t)at - 2);
            line[used++] = '\n';
            line[used] = 0;
        }
        say_line(line);
        /* Where those regions lie, by 1 GiB window of the address space (Alpha 6, MihawkOS uses fewer page tables): a few
         * dense windows cannot shrink by packing, scattered ones can. */
        at = snprintf(line, sizeof(line), "wine-child: vm page-table windows ");
        if (at > 0 && (size_t)at < sizeof(line) - 2) {
            size_t used = (size_t)at + pw_vm_census_format_windows(&census, 12, line + at, sizeof(line) - (size_t)at - 2);
            line[used++] = '\n';
            line[used] = 0;
            say_line(line);
        }
    }
    int at = snprintf(line, sizeof(line), "wine-child: vm resident=%llu MiB direct_resident=%llu reserved_resident=%llu flexible=%llu direct=%llu other=%llu MiB "
                      "mappings=%u groups=%u%s%s top ", (unsigned long long)(census.resident >> 20),
                      (unsigned long long)(census.direct_resident >> 20), (unsigned long long)(census.reserved_resident >> 20),
                      (unsigned long long)(census.flexible >> 20), (unsigned long long)(census.direct >> 20),
                      (unsigned long long)(census.other >> 20), census.mappings, census.count,
                      census.truncated ? " (truncated)" : "", census.failed ? " (walk failed)" : "");
    if (at > 0 && (size_t)at < sizeof(line) - 2) {
        size_t used = (size_t)at;
        used += pw_vm_census_format(&census, 6, 0, line + used, sizeof(line) - used - 2);
        line[used++] = '\n';
        line[used] = 0;
        say_line(line);
        char big[512];
        pw_vm_census_format_big(&census, big, sizeof(big));
        snprintf(line, sizeof(line), "wine-child: vm big %s\n", big);
        say_line(line);
        /* The counted memory is flexible memory: by protection and size, and its largest mappings with their addresses
         * (tools/memory_breakdown.py reads both), because the group names do not say what it is. */
        char kinds[512];
        pw_vm_census_format_flexible(&census, kinds, sizeof(kinds));
        snprintf(line, sizeof(line), "wine-child: vm flexible-kinds %s\n", kinds);
        say_line(line);
        pw_vm_census_format_flexbig(&census, big, sizeof(big));
        snprintf(line, sizeof(line), "wine-child: vm flexible-big %s\n", big);
        say_line(line);
        const char *vmmap_setting = getenv("WINE_PS5_VMMAP");
        if (vmmap_setting && !strcmp(vmmap_setting, "1")) vmmap_report(say_line);
    }
}
static int payload_prepare_module(int32_t);
/* The payload SDK's mmap and mprotect grant execute by writing the kernel's map entry without the map's lock
 * (kernel_set_vmem_protection, PS5_PayloadSDK crt/kernel.c:1492), racing every other thread's map change. ntdll takes
 * them for everything else and gets execute from JIT memory (wine/ps5/pw_wine_dmem_ps5.c), so an executable request
 * reaching them is refused before it maps anything, and counted (sdk_refused in the tally; a run with any fails). */
static unsigned payload_modules;   /* modules whose unwritable segments the loader wired (proton_payload_child.c) */
static unsigned long long sdk_exec_refused;
static void *sdk_mmap(void *address, size_t bytes, int protection, int flags, int fd, off_t offset)
{
    if (protection & PROT_EXEC) {
        __atomic_add_fetch(&sdk_exec_refused, 1, __ATOMIC_RELAXED);
        errno = EPERM;
        return MAP_FAILED;
    }
    return mmap(address, bytes, protection, flags, fd, offset);
}
static int sdk_mprotect(const void *address, size_t bytes, int protection)
{
    if (protection & PROT_EXEC) {
        __atomic_add_fetch(&sdk_exec_refused, 1, __ATOMIC_RELAXED);
        errno = EPERM;
        return -1;
    }
    return mprotect(address, bytes, protection);
}
/* Executable protection in this process: ntdll's lock-free counters (__wine_ps5_exec_stats: kernel refusals, JIT
 * conversions and the KiB they copied, failed conversions, execute faults nobody could serve, faults retried once the
 * kernel allowed them) and the loader's own, so the terminal branch may print it wherever Wine's threads stopped. A
 * probe ends before the first census, so its terminal line is its only tally. */
static unsigned (*exec_stats)(uint64_t *, unsigned);
/* What this process's life spent in the server, in files and in section mapping, from ntdll's lock-free totals: the
 * census prints them every 10 s, which a helper that lives under a second never reaches (installers' custom actions,
 * docs/PLAN.md). One line, at the terminal branch, after Wine's threads stopped. */
static void life_summary(void)
{
    void (*perf_snapshot)(PwWinePerf *) = (void *)pw_prx_lookup(wine.descriptor, "pw_wine_perf_snapshot");
    unsigned (*file_stats)(unsigned long long *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "__wine_file_call_stats");
    unsigned (*section_stats)(uint64_t *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_section_stats");
    /* Where module loading goes (P3): execute conversions and image reads, code-share round trips, module opens. */
    unsigned (*load_timing)(uint64_t *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_load_timing");
    unsigned (*code_round_timing)(uint64_t *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "pw_ps5_code_round_timing");
    unsigned (*dl_timing)(uint64_t *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "pw_wine_dl_timing");
    PwWinePerf perf = {0};
    unsigned long long files[24] = {0};
    uint64_t section[16] = {0}, load[4] = {0}, rounds[2] = {0}, opens[4] = {0};
    if (perf_snapshot) perf_snapshot(&perf);
    if (!file_stats || file_stats(files, 24) < 24) memset(files, 0, sizeof(files));
    if (!section_stats || section_stats(section, 16) < 16) memset(section, 0, sizeof(section));
    if (!load_timing || load_timing(load, 4) < 4) memset(load, 0, sizeof(load));
    if (!code_round_timing || code_round_timing(rounds, 2) < 2) memset(rounds, 0, sizeof(rounds));
    if (!dl_timing || dl_timing(opens, 4) < 4) memset(opens, 0, sizeof(opens));
    /* The program's own exit, on the monotonic clock: its EXIT doom stamp (pw_ps5_child_exit), 0 when it never exited. */
    int64_t dooms[PW_CHILD_DOOMS] = {0};
    unsigned (*doom)(int64_t *, unsigned) = __atomic_load_n(&watch_doom, __ATOMIC_ACQUIRE);
    if (doom) (void)doom(dooms, PW_CHILD_DOOMS);
    char line[640];
    /* file kinds in __wine_file_call_stats' order: read, create, open, attributes, directory, information */
    snprintf(line, sizeof(line), "wine-child: life summary ms=%lld server_calls=%llu server_ms=%llu file_read_calls=%llu "
             "file_read_ms=%llu file_open_calls=%llu file_open_ms=%llu file_attributes_ms=%llu file_information_ms=%llu "
             "section_map_calls=%llu section_map_ms=%llu section_fault_calls=%llu section_fault_ms=%llu\n",
             payload_monotonic_ms(), (unsigned long long)perf.server_calls,
             perf.tsc_hz ? (unsigned long long)(perf.server_ticks * 1000 / perf.tsc_hz) : 0ull,
             files[0], files[1] / 1000000ull, files[8], files[9] / 1000000ull, files[13] / 1000000ull, files[21] / 1000000ull,
             (unsigned long long)section[8], (unsigned long long)(section[9] / 1000000ull),
             (unsigned long long)section[1], (unsigned long long)(section[2] / 1000000ull));
    const size_t used = strlen(line) - 1;   /* before its newline */
    snprintf(line + used, sizeof(line) - used, " convert_calls=%llu convert_ms=%llu image_read_calls=%llu image_read_ms=%llu "
             "code_round_calls=%llu code_round_ms=%llu dl_found_calls=%llu dl_found_ms=%llu dl_missed_calls=%llu dl_missed_ms=%llu "
             "exit_ms=%lld\n",
             (unsigned long long)load[0], (unsigned long long)(load[1] / 1000000ull), (unsigned long long)load[2],
             (unsigned long long)(load[3] / 1000000ull), (unsigned long long)rounds[0], (unsigned long long)(rounds[1] / 1000000ull),
             (unsigned long long)opens[0], (unsigned long long)(opens[1] / 1000000ull), (unsigned long long)opens[2],
             (unsigned long long)(opens[3] / 1000000ull), (long long)dooms[PW_CHILD_DOOM_EXIT - 1]);
    stage(line);
}
static void exec_tally(void)
{
    uint64_t v[7] = {0};
    char line[320];
    if (exec_stats) (void)exec_stats(v, 7);
    snprintf(line, sizeof(line), "wine-child: executable protection refused=%llu conversions=%llu converted_kib=%llu "
             "convert_failed=%llu exec_refused=%llu grant_retries=%llu modules=%u sdk_refused=%llu\n",
             (unsigned long long)v[0], (unsigned long long)v[1], (unsigned long long)v[2], (unsigned long long)v[3],
             (unsigned long long)v[4], (unsigned long long)v[5], __atomic_load_n(&payload_modules, __ATOMIC_RELAXED),
             __atomic_load_n(&sdk_exec_refused, __ATOMIC_RELAXED));
    stage(line);
}
/* Shared code copies in this process (__wine_ps5_code_share_stats, wine/ps5/pw_wine_codeshare.h): clusters mapped from
 * the title's registry and their KiB, copies given to it, copies whose bytes were not this helper's, asks or offers that
 * failed, and shared copies made private before a write and their KiB. Lock-free, printed beside the tally above. */
static unsigned (*code_share_stats)(uint64_t *, unsigned);
/* The KiB of the registry's objects this process maps (__wine_ps5_code_share_mapped_kib), told to the title when it
 * changes (PW_CHILD_CODE_MAPPED): its census counts each shared object once from these. Read each telemetry tick. */
static uint64_t (*code_share_mapped)(void);
static void code_share_report(void)
{
    static int64_t told = -1;
    const uint64_t kib = code_share_mapped ? code_share_mapped() : 0;
    if ((int64_t)kib == told || kib > INT32_MAX) return;
    const struct pw_child_message report = {PW_CHILD_PROTOCOL, PW_CHILD_CODE_MAPPED, getpid(), (int32_t)kib, 0};
    if (!pw_child_send(3, &report, sizeof(report), NULL, 0)) told = (int64_t)kib;   /* failed: told again next tick */
}
static void code_share_tally(void)
{
    uint64_t v[7] = {0};
    char line[320];
    if (!code_share_stats || code_share_stats(v, 7) < 7) return;
    /* shared_kib and offered count events since the start; mapped_kib is what this process maps of the registry's
     * objects now (taken and kept), the figure the title's census counts once. */
    snprintf(line, sizeof(line), "wine-child: shared code copies shared=%llu shared_kib=%llu offered=%llu mismatched=%llu "
             "failed=%llu private_writes=%llu private_kib=%llu mapped_kib=%llu\n", (unsigned long long)v[0],
             (unsigned long long)v[1], (unsigned long long)v[2], (unsigned long long)v[3], (unsigned long long)v[4],
             (unsigned long long)v[5], (unsigned long long)v[6],
             (unsigned long long)(code_share_mapped ? code_share_mapped() : 0));
    stage(line);
}
/* The code cache (__wine_ps5_code_cache_stats, docs/FLEXIBLE_MEMORY.md): its budget now and its policy (off, set by
 * the user, a listed client, adaptive), the KiB it holds, clusters given back to direct memory and their KiB, entries
 * found stale, evictions refused, ranges lost, the budget's raises and the KiB of evicted code converted again. */
static unsigned (*code_cache_stats)(uint64_t *, unsigned);
static void code_cache_tally(void)
{
    static const char *const policies[] = {"off", "set", "listed", "adaptive"};
    uint64_t v[10] = {0};
    char line[320];
    if (!code_cache_stats || code_cache_stats(v, 10) < 10) return;
    snprintf(line, sizeof(line), "wine-child: code cache budget_mib=%llu policy=%s held_kib=%llu evicted=%llu evicted_kib=%llu "
             "stale=%llu refused=%llu lost=%llu raises=%llu reconverted_kib=%llu\n", (unsigned long long)v[0],
             v[9] < 4 ? policies[v[9]] : "?", (unsigned long long)v[1], (unsigned long long)v[2], (unsigned long long)v[3],
             (unsigned long long)v[4], (unsigned long long)v[5], (unsigned long long)v[6], (unsigned long long)v[7],
             (unsigned long long)v[8]);
    stage(line);
}
/* Mappings the kernel shows inside Wine's own space that are not Wine's (wine/ps5/pw_wine_dmem.h): ntdll's lock-free
 * counters (__wine_ps5_foreign_stats: found, unmaps that went around one, mappings refused, queries not answered,
 * holes) and the first four holes, each named by the kernel's record at its address now. A skip or a refusal is the
 * guard working; this says where and what it was. */
static unsigned (*foreign_stats)(uint64_t *, unsigned);
static void foreign_tally(void)
{
    uint64_t v[25] = {0};
    char line[1024];
    size_t at;
    if (!foreign_stats || foreign_stats(v, 25) < 25) return;
    at = (size_t)snprintf(line, sizeof(line), "wine-child: foreign mappings found=%llu skipped_unmaps=%llu "
                          "refused_maps=%llu query_failed=%llu holes=%llu", (unsigned long long)v[0],
                          (unsigned long long)v[1], (unsigned long long)v[2], (unsigned long long)v[3],
                          (unsigned long long)v[4]);
    for (unsigned i = 0; i < 4 && i < v[4] && at < sizeof(line) - 2; i++) {
        const uint64_t *hole = v + 5 + i * 5;
        uint8_t raw[PW_VM_RECORD] = {0};
        PwVmRecord record = {0};
        int named = 0;
        if (!sceKernelVirtualQuery((const void *)(uintptr_t)hole[0], 1, raw, sizeof(raw))) {
            pw_vm_record_decode(raw, &record);
            named = record.start <= hole[0] && hole[0] < record.end;
        }
        at += (size_t)snprintf(line + at, sizeof(line) - at, " at=%#llx+%#llx/%llx skips=%llu refusals=%llu kind=%s%02x:%s",
                               (unsigned long long)hole[0], (unsigned long long)hole[1], (unsigned long long)hole[2],
                               (unsigned long long)hole[3], (unsigned long long)hole[4], named ? "" : "gone-",
                               named ? record.flags : 0, named && record.name[0] ? record.name : "-");
    }
    if (at > sizeof(line) - 2) at = sizeof(line) - 2;
    line[at++] = '\n';
    line[at] = 0;
    stage(line);
}
/* Direct-memory sections mapped a chunk at a time, and mappings the kernel placed over the section arena
 * (wine/ps5/pw_wine_section_ps5.c): plain counters, so the terminal branch prints them too; a probe's section lives
 * shorter than the first census. */
static unsigned (*direct_section_stats)(uint64_t *, unsigned);
static void direct_section_tally(void)
{
    uint64_t v[25] = {0};
    char line[256];
    if (!direct_section_stats || direct_section_stats(v, 25) < 25 || !(v[16] || v[18] || v[21] || v[22])) return;
    snprintf(line, sizeof(line), "wine-child: direct sections chunk_maps=%llu mapped_mib=%llu retries=%llu asks=%llu "
             "remembered=%llu ask_failures=%llu misplaced=%llu last=%#llx+%#llx\n", (unsigned long long)v[16],
             (unsigned long long)(v[17] >> 20), (unsigned long long)v[18], (unsigned long long)v[19],
             (unsigned long long)v[20], (unsigned long long)v[21], (unsigned long long)v[22], (unsigned long long)v[23],
             (unsigned long long)v[24]);
    stage(line);
}
/* sigaction(2) itself, system call 416, without libkernel's wrapper (patch 0945). */
static int payload_raw_sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    return (int)syscall(416, sig, act, old);
}
/* A native SDK thread has GS base zero. Wine's process-wide signal routing
 * must preserve this handler while still catching PE32 faults in Wine threads. */
static volatile sig_atomic_t native_signals_seen;
static void native_signal_probe(int sig) { if (sig == SIGUSR1) ++native_signals_seen; }

/* A profile of what this process's threads run (wine/ps5/pw_wine_threads.h), taken ten times a second when
 * WINE_PS5_SAMPLE=1 and printed with each 10 s census: per thread the CPU it used and how many samples found it in
 * Windows or native code, inside a server call or another signal-blocked region ("stale"), and the most-hit
 * instruction addresses in 16-byte buckets. A native 32-bit game loaded in 37 s where a PC takes 2 and no record said
 * where the time went (2026-10-07; record removed from the tree); a HashLink game under an emulator took minutes to reach its menu. */
enum { PROFILE_THREADS = 256, PROFILE_HOT = 8192 };
typedef struct ProfileThread { int64_t tid; uint64_t cpu_before, cpu_now; unsigned run, stale, stub; } ProfileThread;
typedef struct ProfileHot { uint64_t bucket, top, rax; int64_t tid; unsigned n; } ProfileHot;
static ProfileThread profile_threads[PROFILE_THREADS];
static ProfileHot profile_hot[PROFILE_HOT];
typedef struct ProfileCall { int64_t tid; unsigned call, n; } ProfileCall;
enum { PROFILE_CALLS = 1024 };
static ProfileCall profile_calls[PROFILE_CALLS];
static unsigned profile_call_count;
static unsigned profile_thread_count;

static void profile_take(unsigned (*sample)(PwWineThreadSample *, unsigned, unsigned), unsigned (*describe)(uint64_t, uint64_t))
{
    PwWineThreadSample out[PROFILE_THREADS];
    const unsigned n = sample(out, PROFILE_THREADS, 3000);
    for (unsigned i = 0; i < n; i++) {
        unsigned t = 0;
        while (t < profile_thread_count && profile_threads[t].tid != out[i].tid) t++;
        if (t == profile_thread_count) {
            if (t == PROFILE_THREADS) continue;
            profile_threads[t] = (ProfileThread){.tid = out[i].tid, .cpu_before = out[i].cpu_ns};
            profile_thread_count++;
        }
        profile_threads[t].cpu_now = out[i].cpu_ns;
        if (out[i].stale) { profile_threads[t].stale++; continue; }
        profile_threads[t].run++;
        if (out[i].rip < 0x800020000ull) profile_threads[t].stub++;
        /* What the thread is in: a Windows system call (its number, from ntdll's table), or running Windows code. */
        const unsigned call = describe ? describe(out[i].teb, out[i].rsp) : 0;
        if (call) {
            unsigned c = 0;
            while (c < profile_call_count && (profile_calls[c].tid != out[i].tid || profile_calls[c].call != call)) c++;
            if (c == profile_call_count && c < PROFILE_CALLS) profile_calls[profile_call_count++] = (ProfileCall){.tid = out[i].tid, .call = call};
            if (c < profile_call_count) profile_calls[c].n++;
        }
        /* A sample inside the call stubs near the image's start is a wait: those are counted across threads by syscall
         * number and return address. Any other sample is code running: counted per thread, in 64-byte buckets. */
        const int stub = out[i].rip < 0x800020000ull;
        const int64_t who = stub ? 0 : out[i].tid;
        const uint64_t bucket = stub ? out[i].rip >> 4 : out[i].rip >> 6;
        const uint64_t rax = stub ? out[i].rax & 0xffff : 0, top = stub ? out[i].top : 0;
        const uint64_t key = bucket * 0x9e3779b97f4a7c15ull ^ (uint64_t)who * 0xc2b2ae3d27d4eb4full ^ top * 0x85ebca77c2b2ae63ull ^ rax;
        unsigned slot = (unsigned)(key >> 51) & (PROFILE_HOT - 1);
        for (unsigned probe = 0; probe < 64; probe++, slot = (slot + 1) & (PROFILE_HOT - 1)) {
            ProfileHot *hot = &profile_hot[slot];
            if (hot->n && (hot->bucket != bucket || hot->tid != who || hot->top != top || hot->rax != rax)) continue;
            if (!hot->n) *hot = (ProfileHot){.bucket = bucket, .top = top, .rax = rax, .tid = who};
            hot->n++;
            break;
        }
    }
}

static void profile_report(void)
{
    char line[200];
    for (unsigned t = 0; t < profile_thread_count; t++) {
        ProfileThread *thread = &profile_threads[t];
        if (thread->run || thread->stale)
            {
            snprintf(line, sizeof(line), "wine-child: thread tid=%lld samples=%u in_call_stub=%u signal_blocked=%u\n",
                     (long long)thread->tid, thread->run, thread->stub, thread->stale);
            stage(line);
        }
        thread->cpu_before = thread->cpu_now;
        thread->run = thread->stale = thread->stub = 0;
    }
    for (unsigned top = 0; top < 160; top++) {
        unsigned best = PROFILE_HOT;
        for (unsigned i = 0; i < PROFILE_HOT; i++)
            if (profile_hot[i].n && (best == PROFILE_HOT || profile_hot[i].n > profile_hot[best].n)) best = i;
        if (best == PROFILE_HOT) break;
        const ProfileHot *hot = &profile_hot[best];
        if (hot->tid)
            snprintf(line, sizeof(line), "wine-child: hot rip=%#llx n=%u tid=%lld\n", (unsigned long long)hot->bucket << 6, hot->n,
                     (long long)hot->tid);
        else
            snprintf(line, sizeof(line), "wine-child: wait rip=%#llx n=%u rax=%llu top=%#llx\n", (unsigned long long)hot->bucket << 4,
                     hot->n, (unsigned long long)hot->rax, (unsigned long long)hot->top);
        stage(line);
        profile_hot[best].n = 0;
    }
    for (unsigned top = 0; top < 120; top++) {
        unsigned best = PROFILE_CALLS;
        for (unsigned i = 0; i < profile_call_count; i++)
            if (profile_calls[i].n && (best == PROFILE_CALLS || profile_calls[i].n > profile_calls[best].n)) best = i;
        if (best == PROFILE_CALLS || profile_calls[best].n < 2) break;
        snprintf(line, sizeof(line), "wine-child: nt tid=%lld call=%#x n=%u\n", (long long)profile_calls[best].tid,
                 profile_calls[best].call & 0xffff, profile_calls[best].n);
        stage(line);
        profile_calls[best].n = 0;
    }
    profile_call_count = 0;
    memset(profile_hot, 0, sizeof(profile_hot));
}

/* The 4 KiB MMU's census line (docs/MMU4K.md section 10): differences over the 10 s window, but top= (the instructions
 * it stepped, map << 8 | opcode, with a space-saving count) as counted since the start. With total, once as the helper
 * ends, the counts since the start: a probe that ends inside its first window had no line (l4-payload-vm-contention-
 * 2026-10-16-01). Lock-free (relaxed loads). Nothing without the export. */
static void mmu4k_census(unsigned (*stats)(uint64_t *, unsigned), int total)
{
    /* __wine_ps5_mmu4k_stats' order: runs, insns, the exits (pw_wine_mmu4k.h's), stepfall, stack_full, stack_damage,
     * top[4], then the alias module's maps, map failures, forgets, evictions, noalias. */
    static const char *const exits[] = {"idle",  "budget", "form",    "protected", "straddle",
                                        "fault", "fetch",  "noalias", "yield"};
    enum { EXITS = sizeof(exits) / sizeof(exits[0]), EXIT = 2, STEPFALL = EXIT + EXITS, TOP = STEPFALL + 3,
           ALIAS = TOP + 4, COUNT = ALIAS + 5 };
    static uint64_t before[COUNT];
    uint64_t now[COUNT] = {0}, d[COUNT];
    char line[384];
    if (!stats || stats(now, COUNT) != COUNT) return;   /* another order: no line rather than misnamed numbers */
    for (unsigned i = 0; i < COUNT; ++i) {
        d[i] = total ? now[i] : now[i] - before[i];
        if (!total) before[i] = now[i];
    }
    int used = snprintf(line, sizeof(line), "wine-child: mmu4k %sruns=%llu insns=%llu exits=", total ? "total " : "",
                        (unsigned long long)d[0], (unsigned long long)d[1]);
    for (unsigned i = 0; i < EXITS && used > 0 && used < (int)sizeof(line); ++i)
        used += snprintf(line + used, sizeof(line) - used, "%s%s:%llu", i ? "," : "", exits[i],
                         (unsigned long long)d[EXIT + i]);
    if (used > 0 && used < (int)sizeof(line))
        used += snprintf(line + used, sizeof(line) - used, " stepfall=%llu stack_full=%llu stack_damage=%llu top=",
                         (unsigned long long)d[STEPFALL], (unsigned long long)d[STEPFALL + 1],
                         (unsigned long long)d[STEPFALL + 2]);
    for (unsigned i = 0; i < 4 && used > 0 && used < (int)sizeof(line); ++i)
        used += snprintf(line + used, sizeof(line) - used, "%s%04x:%u", i ? "," : "", (unsigned)(now[TOP + i] >> 32),
                         (unsigned)now[TOP + i]);
    if (used > 0 && used < (int)sizeof(line))
        used += snprintf(line + used, sizeof(line) - used, " alias=%llu/%llu/%llu evictions=%llu noalias=%llu\n",
                         (unsigned long long)d[ALIAS], (unsigned long long)d[ALIAS + 1],
                         (unsigned long long)d[ALIAS + 2], (unsigned long long)d[ALIAS + 3],
                         (unsigned long long)d[ALIAS + 4]);
    if (used > 0 && used < (int)sizeof(line)) stage(line);
}

/* The census's lock line: Wine's virtual mutex over the 10 s window (acquisitions, those that waited, owner changes),
 * its holder now and its longest hold since the start; the engine lock's holder now. Lock-free (pw_wine_perf.h), so it
 * prints while the mutex is wedged, and every census has one (proof on the console that patch 1135's hook runs). */
static void vlock_census(void)
{
    static PwWinePerfVlock before;
    void (*vlock)(PwWinePerfVlock *) = __atomic_load_n(&watch_vlock, __ATOMIC_ACQUIRE);
    unsigned (*dmem)(uint64_t *, unsigned) = __atomic_load_n(&watch_dmem, __ATOMIC_ACQUIRE);
    PwWinePerfVlock now;
    uint64_t holder[2] = {0, 0};
    char line[384];
    if (!vlock) return;
    vlock(&now);
    if (dmem) (void)dmem(holder, 2);
    snprintf(line, sizeof(line), "wine-child: vlock acquires=+%llu contended=+%llu changes=+%llu waiters=%llu owner=%llu "
             "held_ms=%llu doing=%s longest_ms=%llu longest_owner=%llu longest_site=%#llx longest_doing=%s "
             "dmem_lock=%llu:%llu\n", (unsigned long long)(now.acquires - before.acquires),
             (unsigned long long)(now.contended - before.contended), (unsigned long long)(now.changes - before.changes),
             (unsigned long long)now.waiters, (unsigned long long)now.owner, (unsigned long long)now.held_us / 1000,
             now.doing ? now.doing : "-", (unsigned long long)now.longest_us / 1000,
             (unsigned long long)now.longest_owner, (unsigned long long)now.longest_site,
             now.longest_doing ? now.longest_doing : "-", (unsigned long long)holder[0],
             (unsigned long long)holder[1] / 1000);
    stage(line);
    before = now;
}

/* Optional statistics can take Wine locks that are contended during teardown.
 * Keep them off the loader thread that must observe owner EOF and return from
 * payload main. This detached worker has process lifetime; never join it during
 * shutdown. Its inputs are immutable PRX exports, not a caller's stack. */
static void *payload_telemetry(void *unused)
{
    (void)unused;
    /* Opt-in regression: park only this worker, with a finite fallback. The
     * runner observes entry before asking the real window to close and proves
     * native absence before the deadline. No Wine lock is acquired here. */
    const char *hold = getenv("WINETEST_TELEMETRY_HOLD");
    if (hold && !strcmp(hold, "1")) {
        struct timespec begin, now;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        char line[128];
        snprintf(line, sizeof(line), "wine-child: telemetry hold begin pid=%d duration_ms=60000\n", (int)getpid());
        stage(line);
        do {
            sceKernelUsleep(100000);
            clock_gettime(CLOCK_MONOTONIC, &now);
        } while ((now.tv_sec - begin.tv_sec) * 1000ll +
                 (now.tv_nsec - begin.tv_nsec) / 1000000 < 60000);
        stage("wine-child: telemetry hold expired\n");
    }
    const char *native_signals = getenv("WINE_PS5_NATIVE_SIGNALS");
    void (*input_stats)(PwWineInputStats *) = (void *)pw_prx_lookup(wine.descriptor, "pw_wine_input_stats");
    // What this process holds of the console's system memory, and how much of that is images (patch 0944): every
    // image is read into a process's own memory, so another process with the same DLLs holds them again.
    unsigned (*virtual_stats)(unsigned long long *, unsigned) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_virtual_stats");
    unsigned (*memory_stats)(uint64_t *, unsigned) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_memory_stats");
    void (*image_stats)(unsigned long long *, unsigned long long *) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_virtual_image_stats");
    unsigned (*image_read_stats)(uint64_t *, unsigned) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_image_read_stats");
    // What this process wrote to regular files, by call size (patch 1006): the console's disk is slow below 1 MiB writes.
    unsigned (*file_write_stats)(unsigned long long *, unsigned) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_file_write_stats");
    // The demand-mapped shared sections' cache: pages faulted, evicted, and what its native calls cost.
    unsigned (*section_stats)(uint64_t *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_section_stats");
    // The 4 KiB MMU (docs/MMU4K.md section 10): runs, instructions, exits, steps it fell back to, its aliases.
    unsigned (*mmu4k_stats)(uint64_t *, unsigned) = (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_mmu4k_stats");
    // What the process spent in file reads, opens and queries (patch 1112), kind by kind: calls, ns, slowest ns, bytes.
    unsigned (*file_call_stats)(unsigned long long *, unsigned) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_file_call_stats");
    // ntdll's own counters (wine/ps5/pw_wine_perf.h), which the title prints only for its own process.
    void (*perf_snapshot)(PwWinePerf *) = (void *)pw_prx_lookup(wine.descriptor, "pw_wine_perf_snapshot");
    const char *sample_env = getenv("WINE_PS5_SAMPLE");
    unsigned (*thread_sample)(PwWineThreadSample *, unsigned, unsigned) =
        sample_env && !strcmp(sample_env, "1") ? (void *)pw_prx_lookup(wine.descriptor, "pw_wine_threads_sample") : NULL;
    unsigned (*thread_describe)(uint64_t, uint64_t) =
        thread_sample ? (void *)pw_prx_lookup(wine.descriptor, "pw_wine_describe_thread") : NULL;
    // The files and directories this process holds, every 10 s: its share of the title's budget of about 250
    // (fd-budget-01), which the server's own figures do not show.
    struct timespec census_started;
    clock_gettime(CLOCK_MONOTONIC, &census_started);
    unsigned census_period = 60;
    {
        const char *text = getenv("WINE_PS5_CENSUS_SECONDS");
        const unsigned long value = text && *text ? strtoul(text, NULL, 10) : 0;
        if (value >= 10) census_period = (unsigned)(value / 10 * 10);
    }
    // Where each 10 s period of this loop goes: asleep (100 ms asked each tick), in the sampler, or in the census itself.
    // An emulator's periods took 20 to 25 s (2026-10-13; record removed from the tree), and no record said whether the thread slept late or worked long.
    long long slept_ns = 0, sampled_ns = 0, worked_ns = 0;
    struct timespec lap;
    clock_gettime(CLOCK_MONOTONIC, &lap);
    const char *early_setting = getenv("WINE_PS5_EARLY");
    const int early_every_second = early_setting && !strcmp(early_setting, "1");
    for (unsigned tick = 0;; ++tick) {
        struct timespec woke, slept, sampled;
        {
            struct timespec top;
            clock_gettime(CLOCK_MONOTONIC, &top);
            worked_ns += (top.tv_sec - lap.tv_sec) * 1000000000ll + (top.tv_nsec - lap.tv_nsec);
        }
        clock_gettime(CLOCK_MONOTONIC, &woke);
        sceKernelUsleep(100000);
        clock_gettime(CLOCK_MONOTONIC, &slept);
        slept_ns += (slept.tv_sec - woke.tv_sec) * 1000000000ll + (slept.tv_nsec - woke.tv_nsec);
        code_share_report();
        if (thread_sample) profile_take(thread_sample, thread_describe);
        clock_gettime(CLOCK_MONOTONIC, &sampled);
        sampled_ns += (sampled.tv_sec - slept.tv_sec) * 1000000000ll + (sampled.tv_nsec - slept.tv_nsec);
        lap = sampled;
        /* A fresh Chromium GPU process holds 300 to 345 MiB of the console's resident memory within 10 s, and the memory guard ends
         * it before the first census of 10 s has walked its mappings (2026-10-12; record removed from the tree): what holds it is read at 2, 4, 6, 8, 9, 11,
         * 12, 13 and 15 s as well (its flexible memory goes from 44 to 345 MiB at about 10 s). */
        static unsigned long long census_resident_mib, census_tick;
        const int early_tick = tick == 19 || tick == 39 || tick == 59 || tick == 79 || tick == 89 || tick == 109 || tick == 119 || tick == 129 || tick == 149;
        /* Every second to 60 s (the climb of 2026-10-24 was 255 to 469 MiB between two of the sparse samples), and a full census when the
         * count has risen 50 MiB since the last one, at most once in 5 s. WINE_PS5_EARLY=1 turns this on; it costs a line a second. */
        const int dense = early_every_second && tick < 600 && tick % 10 == 9;
        if (early_tick || dense) {
            uint64_t resident = 0, mapped = 0;
            const unsigned long long page = (unsigned long long)sysconf(_SC_PAGESIZE);
            char early[160];
            const int status = sceKernelGetResidentFmemCount(getpid(), &resident, &mapped);
            snprintf(early, sizeof(early), "wine-child: early memory at %u s: resident=%llu MiB (status %#x)\n", (tick + 1) / 10,
                     resident * page >> 20, (unsigned)status);
            stage(early);
            /* The sampler's report every second for the first minute, not only at the 10 s census (which the browser helper never reaches in
             * time): who was running while the libc heap grew, and where a slow start's seconds go. */
            if (thread_sample) profile_report();
            const unsigned long long now_mib = resident * page >> 20;
            if (now_mib >= 100 && (early_tick || (now_mib >= census_resident_mib + 50 && tick >= census_tick + 50))) {
                census_resident_mib = now_mib;
                census_tick = tick;
                vm_census_report(stage);
            }
            continue;
        }
        if (tick % 100 != 99) continue;
        if (tick == 99 && native_signals && !strcmp(native_signals, "1")) {
            int sent = pthread_kill(pthread_self(), SIGUSR1);
            stage(!sent && native_signals_seen == 1
                  ? "PS5_PROTON_NATIVE_SIGNAL_PROBE PASS: prior native handler called once\n"
                  : "PS5_PROTON_NATIVE_SIGNAL_PROBE FAIL: prior native handler not called once\n");
        }
        /* The heavy part of the census (every descriptor's fstat, 190 ms with an emulator's 3,000 files, the images and the mapping
         * walk) runs every 10 s for the first minute and then every WINE_PS5_CENSUS_SECONDS (60): on every 10 s it was a
         * 300 to 660 ms gap in the frames of a game that otherwise held 58 fps (2026-10-13; record removed from the tree). The cheap counters keep their
         * 10 s period. */
        static unsigned files, dirs;
        const unsigned seconds = (tick + 1) / 10;
        const int heavy = seconds <= 60 || seconds % census_period == 0;
        const long long census_begin = now_ns();
        long long fds_ns = 0, image_ns = 0, vm_ns = 0;
        if (heavy) {
            files = dirs = 0;
            for (int fd = 0; fd < 4096; ++fd) {
                struct stat status;
                if (fstat(fd, &status)) continue;
                files += S_ISREG(status.st_mode);
                dirs += S_ISDIR(status.st_mode);
            }
        }
        fds_ns = now_ns() - census_begin;
        char line[384];
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        const long long elapsed_ms = (now.tv_sec - census_started.tv_sec) * 1000ll +
                                     (now.tv_nsec - census_started.tv_nsec) / 1000000;
        snprintf(line, sizeof(line), "wine-child: census seconds=%u elapsed_ms=%lld files=%u dirs=%u slept_ms=%lld "
                 "sampler_ms=%lld other_ms=%lld\n", (tick + 1) / 10, elapsed_ms, files, dirs, slept_ns / 1000000,
                 sampled_ns / 1000000, worked_ns / 1000000);
        slept_ns = sampled_ns = worked_ns = 0;
        stage(line);
        /* Lock-free lines first: what follows can wait on a lock the stall holds. */
        vlock_census();
        mmu4k_census(mmu4k_stats, 0);
        if (thread_sample) profile_report();
        if (section_stats) {
            uint64_t v[16] = {0};
            if (section_stats(v, 16) >= 16 && v[0])
                {
                snprintf(line, sizeof(line), "wine-child: section views=%llu fault_calls=%llu fault_ms=%llu mapped=%llu evicted=%llu failures=%llu "
                         "peak=%llu cached=%llu map_calls=%llu map_ms=%llu flush_calls=%llu flush_ms=%llu unmap_calls=%llu unmap_ms=%llu "
                         "reserve_calls=%llu reserve_ms=%llu\n", (unsigned long long)v[0], (unsigned long long)v[1],
                         (unsigned long long)(v[2] / 1000000ull), (unsigned long long)v[3], (unsigned long long)v[4],
                         (unsigned long long)v[5], (unsigned long long)v[6], (unsigned long long)v[7], (unsigned long long)v[8],
                         (unsigned long long)(v[9] / 1000000ull), (unsigned long long)v[10], (unsigned long long)(v[11] / 1000000ull),
                         (unsigned long long)v[12], (unsigned long long)(v[13] / 1000000ull), (unsigned long long)v[14],
                         (unsigned long long)(v[15] / 1000000ull));
                stage(line);
            }
            direct_section_tally();
        }
        if (file_call_stats) {
            // Totals since the process began; the log's reader subtracts.
            static const char *const kinds[] = {"read", "create", "open", "attributes", "directory", "information"};
            unsigned long long c[24] = {0};
            if (file_call_stats(c, 24) >= 24)
                for (unsigned k = 0; k < 6; k++) {
                    if (!c[k * 4]) continue;
                    snprintf(line, sizeof(line), "wine-child: file %s calls=%llu ms=%llu slowest_ms=%llu KiB=%llu\n", kinds[k], c[k * 4],
                             c[k * 4 + 1] / 1000000ull, c[k * 4 + 2] / 1000000ull, c[k * 4 + 3] >> 10);
                    stage(line);
                }
        }
        if (heavy && image_stats && virtual_stats) {
            unsigned long long class_counters[10] = {0};
            const unsigned class_kept = virtual_stats(class_counters, 10);
            if (memory_stats) {
                uint64_t m[20] = {0};
                if (memory_stats(m, 20) >= 20) {
                    snprintf(line, sizeof(line), "wine-child: memory classes dmem_backed_mib=%llu heap_mapped_mib=%llu "
                             "heap_live_mib=%llu allocs=%llu file_private_mib=%llu mmap=%llu mprotect_bytes_mib=%llu\n",
                             (unsigned long long)(m[0] >> 20), (unsigned long long)(m[4] >> 20),
                             (unsigned long long)(m[8] >> 20), (unsigned long long)m[9],
                             class_kept > 9 ? (unsigned long long)(class_counters[9] >> 20) : 0ull,
                             (unsigned long long)class_counters[0],
                             (unsigned long long)(class_counters[3] >> 20));
                    snprintf(line + strlen(line), sizeof(line) - strlen(line),
                             "wine-child: memory claims attempts=%llu ok=%llu failed=%llu align_refused=%llu region_refused=%llu\n",
                             (unsigned long long)m[15], (unsigned long long)m[16], (unsigned long long)m[17],
                             (unsigned long long)m[18], (unsigned long long)m[19]);
                    { uint64_t r[5] = {0};
                      if (image_read_stats && image_read_stats(r, 5) >= 5)
                          snprintf(line + strlen(line), sizeof(line) - strlen(line),
                                   "wine-child: image read asks=%llu hits=%llu offers=%llu kept=%llu refused=%llu\n",
                                   (unsigned long long)r[0], (unsigned long long)r[1], (unsigned long long)r[2],
                                   (unsigned long long)r[3], (unsigned long long)r[4]); }
                    stage(line);
                }
            }
            const unsigned long long page = (unsigned long long)sysconf(_SC_PAGESIZE);
            unsigned long long views = 0, image_bytes = 0, counters[10] = {0};
            uint64_t resident = 0, mapped = 0;
            /* image_stats takes the virtual mutex: in a 2026-10-15 run it waited 160 s and every later line was lost. */
            void (*vlock)(PwWinePerfVlock *) = __atomic_load_n(&watch_vlock, __ATOMIC_ACQUIRE);
            PwWinePerfVlock held = {0};
            if (vlock) vlock(&held);
            if (held.owner && held.held_us >= 1000000) {
                snprintf(line, sizeof(line), "wine-child: telemetry phase=image-stats skipped owner=%llu held_ms=%llu "
                         "doing=%s\n", (unsigned long long)held.owner, (unsigned long long)held.held_us / 1000,
                         held.doing ? held.doing : "-");
                stage(line);
            } else {
                stage("wine-child: telemetry phase=image-stats begin\n");
                const long long image_begin = now_ns();
                image_stats(&views, &image_bytes);
                image_ns = now_ns() - image_begin;
                stage("wine-child: telemetry phase=image-stats end\n");
            }
            const unsigned kept = virtual_stats(counters, 10);
            const int status = sceKernelGetResidentFmemCount(getpid(), &resident, &mapped);
            snprintf(line, sizeof(line), "wine-child: memory resident=%llu MiB mapped=%llu MiB (status %#x) images=%llu "
                     "holding %llu MiB, files read in %llu MiB page_bytes=%llu resident_pages=%llu\n",
                     resident * page >> 20, mapped * page >> 20, (unsigned)status, views, image_bytes >> 20,
                     kept > 9 ? counters[9] >> 20 : 0, page, (unsigned long long)resident);
            stage(line);
            const long long vm_begin = now_ns();
            vm_census_report(stage);
            vm_ns = now_ns() - vm_begin;
        }
        snprintf(line, sizeof(line), "wine-child: census_ms fds=%lld image_stats=%lld vm_census=%lld so_far=%lld\n", fds_ns / 1000000,
                 image_ns / 1000000, vm_ns / 1000000, (now_ns() - census_begin) / 1000000);
        stage(line);
        if (file_write_stats) {
            // Totals since the process began; the log's reader subtracts. Slowest is one call in milliseconds.
            unsigned long long w[13] = {0};
            if (file_write_stats(w, 13) >= 13 && w[0])
                {
                snprintf(line, sizeof(line), "wine-child: file writes calls=%llu bytes=%llu KiB write_ms=%llu "
                         "le4k=%llu le16k=%llu le64k=%llu le256k=%llu bigger=%llu slowest_ms=%llu merged_calls=%llu "
                         "merged_KiB=%llu flushes=%llu flush_ms=%llu\n", w[0], w[1] >> 10, w[2] / 1000000ull, w[3], w[4], w[5],
                         w[6], w[7], w[8] / 1000000ull, w[9], w[10] >> 10, w[11], w[12] / 1000000ull);
                stage(line);
            }
        }
        if (memory_stats) {
            /* The last refused call too (PW_WINE_DMEM_FAILED_*: release, allocate, map, protect, unmap, reserve,
             * outside, foreign), which the count alone never said (2026-10-07; record removed from the tree). */
            static const char *const refused[] = {"-", "release", "allocate", "map", "protect", "unmap", "reserve",
                                                  "outside", "foreign"};
            uint64_t counters[15] = {0};
            const unsigned kept = memory_stats(counters, 15);
            if (kept >= 12) {
                const uint64_t what = kept >= 15 ? counters[12] : 0;
                snprintf(line, sizeof(line), "wine-child: direct memory current=%llu MiB peak=%llu MiB "
                         "runs=%llu failures=%llu unix_heap=%llu MiB last_failure=%s@%#llx+%#llx\n",
                         (unsigned long long)(counters[0] >> 20), (unsigned long long)(counters[1] >> 20),
                         (unsigned long long)counters[2], (unsigned long long)counters[3],
                         (unsigned long long)(counters[4] >> 20), what < 9 ? refused[what] : "?",
                         (unsigned long long)counters[13], (unsigned long long)counters[14]);
                stage(line);
            }
        }
        exec_tally();
        code_share_tally();
        code_cache_tally();
        foreign_tally();
        sched_tally();
        /* What this process's Wine did in the last 10 s: its server round trips and the five requests that took the
         * most time, its sleeps, and the host VM calls. A native 32-bit game loaded its archives in 37 s where a PC takes
         * about 2 (2026-10-07), and no record said where the time went. */
        if (perf_snapshot) {
            static PwWinePerf perf[2];
            static unsigned long long vm_before[12];
            static unsigned primed;
            PwWinePerf *now = &perf[tick / 100 % 2], *before = &perf[(tick / 100 + 1) % 2];
            perf_snapshot(now);
            unsigned long long vm[12] = {0};
            const unsigned vm_kept = virtual_stats ? virtual_stats(vm, 12) : 0;
            if (primed && now->tsc_hz) {
                const double us = 1e6 / (double)now->tsc_hz;
                const uint64_t calls = now->server_calls - before->server_calls;
                const uint64_t blocked = now->selects_blocked - before->selects_blocked;
                int used = snprintf(line, sizeof(line), "wine-child: server calls=%llu avg_us=%.1f selects=%llu blocked=%llu "
                    "block_avg_us=%.0f delays=%llu yields=%llu delay_asked_ms=%llu",
                    (unsigned long long)calls, calls ? (now->server_ticks - before->server_ticks) * us / calls : 0.0,
                    (unsigned long long)(now->selects - before->selects), (unsigned long long)blocked,
                    blocked ? (now->select_block_ticks - before->select_block_ticks) * us / blocked : 0.0,
                    (unsigned long long)(now->delays - before->delays),
                    (unsigned long long)(now->delay_yields - before->delay_yields),
                    (unsigned long long)((now->delay_requested_us - before->delay_requested_us) / 1000));
                if (vm_kept > 5 && used > 0 && used < (int)sizeof(line))
                    used += snprintf(line + used, sizeof(line) - used, " vm mmap=%llu munmap=%llu mprotect=%llu faults=%llu",
                        vm[0] - vm_before[0], vm[1] - vm_before[1], vm[2] - vm_before[2], vm[5] - vm_before[5]);
                /* Host pages opened for a write a 4 KiB page allows beside read-only ones (1122's step and system calls). */
                if (vm_kept > 10 && used > 0 && used < (int)sizeof(line))
                    used += snprintf(line + used, sizeof(line) - used, " opened=%llu", vm[10] - vm_before[10]);
                if (used > 0 && used < (int)sizeof(line) - 1) { line[used] = '\n'; line[used + 1] = 0; stage(line); }
                /* The 4 KiB pages that fault most (patch 0545), so a fault storm names its pages: count@page pc info
                 * (info: kind << 24 | page protection << 16 | host page protection << 8 | 1 resolved, 2 violation, 3 other). */
                if (wine.fault_top) {
                    uint64_t entries[6 * 4];
                    const unsigned got = wine.fault_top(entries, 6);
                    int at = snprintf(line, sizeof(line), "wine-child: vm fault top");
                    for (unsigned i = 0; i < got && at > 0 && at < (int)sizeof(line) - 80; ++i)
                        at += snprintf(line + at, sizeof(line) - at, " %llu@%#llx pc=%#llx info=%#llx", (unsigned long long)entries[4 * i],
                                       (unsigned long long)entries[4 * i + 1], (unsigned long long)entries[4 * i + 2], (unsigned long long)entries[4 * i + 3]);
                    if (got && at > 0 && at < (int)sizeof(line) - 1) { line[at] = '\n'; line[at + 1] = 0; stage(line); }
                }
                unsigned top[5], found = 0;
                for (unsigned code = 0; code < PW_PERF_REQUESTS; ++code) {
                    const uint64_t ticks = now->request_ticks[code] - before->request_ticks[code];
                    if (!ticks) continue;
                    unsigned at = found < 5 ? found++ : 5;
                    while (at > 0 && ticks > now->request_ticks[top[at - 1]] - before->request_ticks[top[at - 1]]) {
                        if (at < 5) top[at] = top[at - 1];
                        --at;
                    }
                    if (at < 5) top[at] = code;
                }
                used = snprintf(line, sizeof(line), "wine-child: server top");
                for (unsigned i = 0; i < found && used > 0 && used < (int)sizeof(line) - 2; ++i) {
                    const unsigned code = top[i];
                    const uint64_t count = now->request_calls[code] - before->request_calls[code];
                    used += snprintf(line + used, sizeof(line) - used - 1, " %s=%llu/%.1fus",
                        now->request_names[code] ? now->request_names[code] : "?", (unsigned long long)count,
                        count ? (now->request_ticks[code] - before->request_ticks[code]) * us / count : 0.0);
                }
                if (found && used > 0 && used < (int)sizeof(line) - 1) { line[used] = '\n'; line[used + 1] = 0; stage(line); }
                /* What its media demuxers did (winedmo, pw_wine_perf.h), in a 10 s that had any. */
                uint64_t media[PW_PERF_MEDIA_COUNTERS], any = 0;
                for (unsigned i = 0; i < PW_PERF_MEDIA_COUNTERS; ++i) any |= media[i] = now->media[i] - before->media[i];
                if (any) {
                    snprintf(line, sizeof(line), "wine-child: media opens=%llu seeks=%llu to_start=%llu rewinds=%llu "
                             "reads=%llu read_KiB=%llu\n", (unsigned long long)media[PW_PERF_MEDIA_OPENS],
                             (unsigned long long)media[PW_PERF_MEDIA_SEEKS],
                             (unsigned long long)media[PW_PERF_MEDIA_SEEKS_TO_START],
                             (unsigned long long)media[PW_PERF_MEDIA_REWINDS],
                             (unsigned long long)media[PW_PERF_MEDIA_READS],
                             (unsigned long long)(media[PW_PERF_MEDIA_BYTES] >> 10));
                    stage(line);
                }
            }
            for (unsigned i = 0; i < 12; ++i) vm_before[i] = vm[i];
            primed = 1;
        }
        // Where the pad's presses went (wine/ps5/pw_wine_sink.h): read from the title's page, then queued by the PS5
        // bus to Wine's HID stack, or held back while its device was not started.
        if (input_stats) {
            PwWineInputStats in = {0};
            input_stats(&in);
            snprintf(line, sizeof(line), "wine-child: pad reads=%llu connected=%llu pressed=%llu queued=%llu unstarted=%llu "
                     "created=%llu removed=%llu\n", (unsigned long long)in.reads, (unsigned long long)in.connected,
                     (unsigned long long)in.pressed, (unsigned long long)in.queued, (unsigned long long)in.unstarted,
                     (unsigned long long)in.created, (unsigned long long)in.removed);
            stage(line);
        }
    }
    return NULL;
}

/* The backstop (src/pw_control_watch.h): ntdll's doom stamps, read every 100 ms with no Wine lock. A "doom seen" line
 * for each reason as it appears; once the earliest grace is up, "payload doomed" and 1: the caller returns straight
 * into payload main's _exit, with no tallies and no fsync, since a doomed helper's Wine may hold whatever they would
 * wait on. */
static int doom_tick(PwControlDoom *watch, long long now, int held)
{
    static int deferred_said;
    unsigned (*doom)(int64_t *, unsigned) = __atomic_load_n(&watch_doom, __ATOMIC_ACQUIRE);
    int64_t stamps[PW_CHILD_DOOMS] = {0};
    unsigned reason = 0;
    int step;
    char line[192];
    if (!doom) return 0;
    (void)doom(stamps, PW_CHILD_DOOMS);
    while ((step = pw_control_doom_step(watch, stamps, now, &reason)) == PW_CONTROL_DOOM_SEEN) {
        snprintf(line, sizeof(line), "wine-child: payload doom seen reason=%s stamped_ms_ago=%lld due_in_ms=%lld; "
                 "ending the process then unless the title closes it first\n", doom_names[reason - 1],
                 now - (long long)stamps[reason - 1], watch->due_ms - now);
        stage(line);
    }
    if (step != PW_CONTROL_DOOM_DUE) return 0;
    /* Not while the app is away or just back (src/pw_suspend.h): ending would free direct memory GPU work may still use. */
    if (held) {
        if (!deferred_said) {
            deferred_said = 1;
            snprintf(line, sizeof(line), "wine-child: payload doom reason=%s due; deferred while the app is away or just "
                     "back\n", doom_names[reason - 1]);
            stage(line);
        }
        return 0;
    }
    snprintf(line, sizeof(line), "wine-child: payload doomed reason=%s stamped_ms_ago=%lld; ending without Wine\n",
             doom_names[reason - 1], now - (long long)stamps[reason - 1]);
    stage(line);
    return 1;
}

/* The staged unsigned ELF beside the fake-signed SELF: load it in this helper's address space (13.59 and later
 * cannot load the SELF through the kernel). The same fields pw_wine_start_load fills, without a kernel handle or
 * the ntdll adopt; the caller goes on to the protocol check and the Wine entry. */
static int wine_start_elf(const char *prx_path)
{
    char elf_path[1300], error[192], line[1500];
    PwPrxElfImage image;
    int status;
    snprintf(elf_path, sizeof(elf_path), "%s.elf", prx_path);
    status = pw_prx_elf_ps5_load(elf_path, &image, error, sizeof(error));
    if (status) {
        snprintf(line, sizeof(line), "wine-child: elf load %s status=%d %s\n", elf_path, status, error);
        stage(line); fsync(output_fd);
        return status;
    }
    wine.module = -1;
    wine.stage = PW_WINE_START_LOAD;
    wine.segment_count = image.segment_count < PW_PRX_MAX_SEGMENTS ? image.segment_count : PW_PRX_MAX_SEGMENTS;
    memcpy(wine.segments, image.segments, sizeof(wine.segments[0]) * wine.segment_count);
    wine.descriptor = image.descriptor;
    wine.wine_main = (void (*)(int, char **))pw_prx_lookup(image.descriptor, "__wine_main");
    wine.virtual_stats = (unsigned int (*)(uint64_t *, unsigned int))pw_prx_lookup(image.descriptor, "__wine_virtual_stats");
    wine.memory_stats = (unsigned int (*)(uint64_t *, unsigned int))pw_prx_lookup(image.descriptor, "__wine_ps5_memory_stats");
    wine.fault_top = (unsigned int (*)(uint64_t *, unsigned int))pw_prx_lookup(image.descriptor, "__wine_virtual_fault_top");
    snprintf(line, sizeof(line), "wine-child: elf load %s base=%#lx segments=%u descriptor=%p module_start=%p\n",
             elf_path, (unsigned long)image.base, image.segment_count, (const void *)image.descriptor,
             (void *)image.module_start);
    stage(line); fsync(output_fd);
    if (!wine.wine_main) return PW_ERR_NOT_FOUND;
    if (image.module_start && (wine.module_start_result = image.module_start(0, NULL))) return wine.module_start_result;
    return 0;
}

/* The title's own exit. A system close from the home screen kills the title, and about 100 ms later the system deletes
 * the application's direct-memory container, to which every helper's memory is charged (sceKernelAllocateDirectMemoryForApp).
 * Helpers learned of the death through their control sockets, one after another over about 110 ms, and the kernel found a
 * helper's ranges still mapped by later helpers ("DMEM leak is detected by dmem_remove_segment()", owner the helper that had
 * offered shared code copies; the Pro, 2026-10-27, twice). This thread waits on the title's pid itself (kqueue NOTE_EXIT), so
 * every helper sees the death in the same instant, and ends the process at once. That was not enough for the warning:
 * the kernel still found a helper's ranges (proof-base-2026-10-28), every helper having begun to exit before the
 * container went, but an exit gives direct memory back only after tearing down descriptors and the address space. A
 * helper that released its ranges itself first ended in the kernel and outlived SIGKILL (the gate of 2026-10-28-04,
 * likely wired module data; docs/PLAN.md "Memory plan"), so it does not: the kernel takes those ranges back at the
 * container's deletion, as the warning says. The line says when, and what descriptors were still open. */
static int title_pid;
static void *title_watch(void *unused)
{
    (void)unused;
    char line[256];
    struct kevent change, event;
    const int queue = kqueue();
    EV_SET(&change, title_pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
    const int armed = queue >= 0 ? kevent(queue, &change, 1, NULL, 0, NULL) : -1;
    const int arm_error = armed < 0 ? errno : 0;
    if (armed < 0 && arm_error != ESRCH) {
        snprintf(line, sizeof(line), "wine-child: title watch pid=%d unavailable errno=%d; the control socket alone ends "
                 "this helper\n", title_pid, arm_error);
        stage(line);
        return NULL;
    }
    if (armed >= 0) {
        snprintf(line, sizeof(line), "wine-child: title watch pid=%d armed\n", title_pid);
        stage(line);
        for (;;) {
            const int got = kevent(queue, NULL, 0, &event, 1, NULL);
            if (got == 1 && (event.fflags & NOTE_EXIT)) break;
            if (got < 0 && errno != EINTR) {
                snprintf(line, sizeof(line), "wine-child: title watch wait failed errno=%d\n", errno);
                stage(line);
                return NULL;
            }
        }
    }
    const long long seen = payload_monotonic_ms();
    struct PwFdCensus fds;
    pw_fd_census(&fds);
    snprintf(line, sizeof(line), "wine-child: title %d exited, seen at %lld ms%s; ending at once (descriptors open=%u "
             "sockets=%u fifos=%u files=%u devices=%u other=%u)\n", title_pid, seen,
             armed < 0 ? " (already gone when armed)" : "", fds.open, fds.sockets, fds.fifos, fds.files, fds.devices, fds.other);
    stage(line);
    fsync(output_fd);
    _exit(0);
}

static void wine_child_run(void)
{
    unsigned mxcsr = 0x1f80;
    __asm__ volatile("ldmxcsr %0; fninit" : : "m"(mxcsr));
    char log_path[96];
    snprintf(log_path, sizeof(log_path), PW_LOG_SESSIONS_ROOT "/0/wine-child-%d.log", (int)getpid());
    output_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    stage("wine-child: entry\n");
#ifndef PW_HELPER_BUILD
#define PW_HELPER_BUILD "unknown"   /* a build outside tools/build-payload-loader.sh */
#endif
    /* Which build this helper is (tools/build-payload-loader.sh's source digest): a tester's bundle names it at once. */
    stage("wine-child: build " PW_HELPER_BUILD "\n");
    start_stamp("main", start_main_ms);
    start_stamp("control", start_control_ms);
    start_stamp("entry", -1);
    {
        /* Which firmware this helper runs on, once per helper, for the title's notice when a module load stops it
         * (errno 92, src/pw_start_failure.h). The INSTALLED firmware, not the word an enabler reports (src/pw_firmware.h).
         * It no longer picks the module loader: that is pw_prx_elf_wanted's, the same on every firmware. */
        unsigned reported = 0;
        child_firmware = pw_firmware_installed(&reported);
        char firmware_line[128];
        snprintf(firmware_line, sizeof(firmware_line), "wine-child: firmware status=%d version=%#08x reported=%#08x\n",
                 child_firmware ? 0 : -1, child_firmware, reported);
        stage(firmware_line);
    }
    {
        /* What the kernel lets this process map, as the process starts: a fixed reservation that Wine asks for later is
         * refused with ENOMEM once the total reaches a limit (late-library, alpha-3), and which one it is decides the fix. */
        static const struct { int resource; const char *name; } limits[] = {
            {RLIMIT_DATA, "data"}, {RLIMIT_STACK, "stack"}, {RLIMIT_RSS, "rss"}, {RLIMIT_MEMLOCK, "memlock"},
#ifdef RLIMIT_VMEM
            {RLIMIT_VMEM, "vmem"},
#endif
            {RLIMIT_AS, "as"}};
        char limit_line[256];
        size_t at = (size_t)snprintf(limit_line, sizeof(limit_line), "wine-child: limits");
        for (unsigned i = 0; i < sizeof(limits) / sizeof(limits[0]) && at < sizeof(limit_line); i++) {
            struct rlimit value;
            if (getrlimit(limits[i].resource, &value)) at += (size_t)snprintf(limit_line + at, sizeof(limit_line) - at, " %s=err", limits[i].name);
            else at += (size_t)snprintf(limit_line + at, sizeof(limit_line) - at, " %s=%lld/%lld", limits[i].name,
                                        value.rlim_cur == RLIM_INFINITY ? -1LL : (long long)value.rlim_cur,
                                        value.rlim_max == RLIM_INFINITY ? -1LL : (long long)value.rlim_max);
        }
        snprintf(limit_line + at, sizeof(limit_line) - at, "\n");
        stage(limit_line);
    }
    {
        /* native/proton_payload_child.c released the console's TTY before the rendezvous; say what fd 0 was and what
         * became of it (its fstat: what the kernel's TTY(STDIN) slot was registered for). */
        char tty_line[192];
        snprintf(tty_line, sizeof(tty_line),
                 "wine-child: console tty stat=%d fstat_errno=%d dev=%#llx rdev=%#llx mode=%#o stdin_null=%d "
                 "stdout_null=%d stderr_null=%d error=%d\n",
                 tty_fstat_status, tty_fstat_error, tty_dev, tty_rdev, tty_mode, tty_stdin_null, tty_stdout_null,
                 tty_stderr_null, tty_error);
        stage(tty_line);
    }
    /* Native allocator diagnostics precede Wine's own logging setup. Keep
     * stderr in this process's saved log too, rather than elfldr's uncaptured
     * stream. The duplicate shares the stage writer's file offset. */
    int diagnostic_fd = output_fd >= 0 ? dup(output_fd) : -1;
    FILE *diagnostics = diagnostic_fd >= 0 ? fdopen(diagnostic_fd, "w") : NULL;
    if (diagnostics) {
        /* Line-buffered, so a line is one write: unbuffered, fprintf wrote each piece apart and Wine's records from
         * other threads landed between them (564 spliced lines in gate-release-2026-10-19-01's helper logs). The
         * buffer is static: allocator diagnostics print through stderr. */
        static char diagnostics_buffer[4096];
        setvbuf(diagnostics, diagnostics_buffer, _IOLBF, sizeof(diagnostics_buffer));
        stderr = diagnostics;
        stage("wine-child: native stderr captured\n");
    } else {
        if (diagnostic_fd >= 0) close(diagnostic_fd);
        stage("wine-child: native stderr capture unavailable\n");
    }
    const payload_args_t *runtime_args=payload_get_args();
    char runtime_line[128];
    snprintf(runtime_line,sizeof(runtime_line),"wine-child: payload runtime pipe=%d/%d pair=%d/%d control=3\n",
        runtime_args->rwpipe[0],runtime_args->rwpipe[1],runtime_args->rwpair[0],runtime_args->rwpair[1]);
    stage(runtime_line);
    struct pw_child_bootstrap boot = {0};
    struct pollfd pollfd = {3, POLLIN, 0};
    int server_fd = -1, error = 1, acknowledged = 0, fds[2];unsigned count=0;
    struct stat control_identity={0};
    const int control_stat=fstat(3,&control_identity);
    int control_type=-1;
    socklen_t control_type_bytes=sizeof(control_type);
    const int control_type_status=getsockopt(3,SOL_SOCKET,SO_TYPE,&control_type,&control_type_bytes);
    char identity_line[128];
    snprintf(identity_line,sizeof(identity_line),"wine-child: control descriptor stat=%d socket_status=%d socket_type=%d\n",
             control_stat,control_type_status,control_type);
    stage(identity_line);
#ifdef PROTON_CHILD_GPU_DEVICE_QUERY
    int gpu_fd=-1;
#endif
#ifdef PROTON_CHILD_GPU_BROKER
    int broker_fd=-1;
#endif
    /* A helper the title started ahead of need waits here until a start takes it (src/pw_warm_helper.h); the title closes
     * one it does not use, which ends this wait with a hangup. */
    const int boot_polled = poll(&pollfd, 1, PW_WARM_BOOTSTRAP_MS);
    const int boot_poll_error = boot_polled < 0 ? errno : 0;
    const ssize_t boot_received = boot_polled == 1 && (pollfd.revents & POLLIN)
                                ? pw_child_recv(3, &boot, sizeof(boot), fds, &count) : -2;
    const int boot_receive_error = boot_received < 0 && boot_received != -2 ? errno : 0;
    char boot_line[256];
    snprintf(boot_line, sizeof(boot_line),
             "wine-child: bootstrap receive poll=%d errno=%d revents=%#x bytes=%zd receive_errno=%d expected=%zu descriptors=%u protocol=%#x size=%u flags=%#x argc=%u\n",
             boot_polled, boot_poll_error, (unsigned)pollfd.revents, boot_received, boot_receive_error,
             sizeof(boot), count, boot.protocol, boot.size, boot.flags, boot.argc);
    stage(boot_line);
    start_stamp("bootstrap", -1);
    if (boot_polled != 1 || !(pollfd.revents & POLLIN) || boot_received != sizeof(boot) ||
#if defined(PROTON_CHILD_GPU_DEVICE_QUERY) || defined(PROTON_CHILD_GPU_BROKER)
        (count!=1 && count!=2) ||
#else
        count!=1 ||
#endif
        boot.protocol != PW_CHILD_PROTOCOL || boot.size != sizeof(boot)) {
        for(unsigned i=0;i<count;++i)close(fds[i]);goto done;
    }
    {
        /* Only a fixed-base 32-bit image's helper gives the carrier's image back (native/proton_payload_child.c). */
        char carrier_line[160];
        if (boot.flags & PW_CHILD_FIXED_BASE) {
            carrier_release();
            boot.flags &= ~PW_CHILD_FIXED_BASE;
            snprintf(carrier_line, sizeof(carrier_line), "wine-child: carrier image released for a fixed-base image "
                     "refused=%d end=%#lx errno=%d\n", carrier_refused, (unsigned long)carrier_end, carrier_errno);
        } else {
            snprintf(carrier_line, sizeof(carrier_line), "wine-child: carrier image kept\n");
        }
        stage(carrier_line);
    }
    sched_place_cpus(boot.flags, boot.environment, sizeof(boot.environment));
    sched_entry(boot.flags==PW_CHILD_MEMORY ? "memory" : "program", boot.environment, sizeof(boot.environment));
    // The title's exit, watched directly, for every helper the memory helper included (title_watch above).
    if ((title_pid = pw_control_title_pid(boot.environment, sizeof(boot.environment))) > 0) {
        char watch_line[96];
        snprintf(watch_line, sizeof(watch_line), "wine-child: title watch thread status=%d\n",
                 native_thread(title_watch, 64u << 10));
        stage(watch_line);
    } else stage("wine-child: title watch: no WINE_PS5_TITLE_PID in the bootstrap\n");
    {
        char sync_line[96];
        snprintf(sync_line, sizeof(sync_line), "wine-child: log sync every 10 s, stall watch every 1 s status=%d\n",
                 native_thread(log_sync, 64u << 10));
        stage(sync_line);
    }
    if(boot.flags==PW_CHILD_MEMORY) {
        // The memory helper (src/pw_memory_helper.h): no Windows program here. It makes the shared memory the title's
        // server and GPU broker would otherwise make, until the title is gone.
        for(unsigned i=0;i<count;++i)close(fds[i]);
        const struct pw_child_message ready={PW_CHILD_PROTOCOL,PW_CHILD_READY,getpid(),0,0};
        if(send(3,&ready,sizeof(ready),MSG_NOSIGNAL)!=sizeof(ready))return;
        stage("wine-child: memory helper: the title's shared memory is made here\n");
#ifdef PW_MEMORY_HELPER_CENSUS
        if(helper_sched.census) {
            char census_line[64];
            snprintf(census_line,sizeof(census_line),"wine-child: sched census thread status=%d\n",native_thread(census_thread,256u << 10));
            stage(census_line);
        }
#endif
        const unsigned long served=pw_memory_helper_serve(3);
        char memory_line[128];
        snprintf(memory_line,sizeof(memory_line),"wine-child: memory helper: %lu requests answered; the title is gone\n",served);
        stage(memory_line);
        fsync(output_fd);
        return;
    }
    server_fd=fds[0];
    if(boot.flags&PW_CHILD_INPUT) {
        // This child's input page (pw_wine_sink.h): made here and given to the title, which keeps the pad in it, so
        // the page is this process's, not the title's (src/pw_shared_page.h). Its xinput reads the pad from it.
        boot.flags&=~PW_CHILD_INPUT;
        const int page_fd=pw_shared_page_create(PW_WINE_INPUT_PAGE_BYTES);
        const int page_error=page_fd < 0 ? errno : 0;
        const struct pw_child_message page={PW_CHILD_PROTOCOL,PW_CHILD_INPUT_PAGE,getpid(),0,0};
        const int page_sent=page_fd >= 0 ? pw_child_send(3,&page,sizeof(page),&page_fd,1) : -2;
        const int send_error=page_sent == -1 ? errno : 0;
        char page_line[192];
        snprintf(page_line,sizeof(page_line),"wine-child: input bootstrap fd=%d allocation_errno=%d send=%d send_errno=%d bytes=%u\n",
                 page_fd,page_error,page_sent,send_error,PW_WINE_INPUT_PAGE_BYTES);
        stage(page_line);
        if(page_fd<0 || page_sent)goto done;
        char page_value[24];snprintf(page_value,sizeof(page_value),"%d",page_fd);
        if(set_env("WINE_PS5_INPUT_FD",page_value))goto done;
        // The title's suspend word in it (src/pw_suspend.h), read by this loader's threads.
        void *word_page=mmap(NULL,PW_WINE_INPUT_PAGE_BYTES,PROT_READ,MAP_SHARED,page_fd,0);
        if(word_page!=MAP_FAILED)title_word=(const PwSuspendPage *)((const char *)word_page+PW_SUSPEND_PAGE_OFFSET);
        stage("wine-child: input page made and given to the title\n");
    }
    if(boot.flags&PW_CHILD_AUDIO) {
        // This helper's audio ring (src/pw_audio_ring.h), made here the same way: its audio driver plays into it,
        // and the title, which starts it empty, mixes it.
        boot.flags&=~PW_CHILD_AUDIO;
        const int ring_fd=pw_shared_page_create(PW_WINE_AUDIO_RING_BYTES);
        const struct pw_child_message ring={PW_CHILD_PROTOCOL,PW_CHILD_AUDIO_RING,getpid(),0,0};
        if(ring_fd<0 || pw_child_send(3,&ring,sizeof(ring),&ring_fd,1))goto done;
        char ring_value[24];snprintf(ring_value,sizeof(ring_value),"%d",ring_fd);
        if(set_env("WINE_PS5_AUDIO_FD",ring_value))goto done;
        stage("wine-child: audio ring made and given to the title\n");
    }
    {
        // This process's memory, served to the title's server (src/pw_memory_service.h): another process's
        // ReadProcessMemory and WriteProcessMemory into this one, and this one's own, go through it. Given before
        // this process says it is ready, so no process can ask before the title has it.
        int memory_end=-1;
        const struct pw_child_message service={PW_CHILD_PROTOCOL,PW_CHILD_MEMORY_SERVICE,getpid(),0,0};
        if(pw_memory_service_start(&memory_end) || pw_child_send(3,&service,sizeof(service),&memory_end,1)) {
            if(memory_end>=0)close(memory_end);
            goto done;
        }
        // Not closed: nothing here proves the title has taken its copy yet, and a unix socket in flight with no
        // holder can arrive flushed (wine/ps5/pw_payload_rendezvous.h). Kept unread for this process's life.
        stage("wine-child: memory served to the title\n");
    }
#ifdef PROTON_CHILD_GPU_BROKER
    if(boot.flags&PW_CHILD_GPU) { if(count!=2)goto done;broker_fd=fds[1];boot.flags&=~PW_CHILD_GPU; }
    else if(count!=1)goto done;
    if(boot.flags==PW_CHILD_GRAPHICS)boot.flags=0;
#endif
    if(boot.flags&PW_CHILD_INITIAL) {
        if(boot.flags!=PW_CHILD_INITIAL || boot.argc<2 || boot.argc>64)goto done;
        size_t offset=0;
        for(unsigned i=0;i<boot.argc;++i) {
            const char *end=memchr(boot.initial_args+offset,0,sizeof(boot.initial_args)-offset);
            if(!end)goto done;
            wine_argv[i]=boot.initial_args+offset;offset=(size_t)(end-boot.initial_args)+1;
        }
        wine_argc=(int)boot.argc;wine_argv[wine_argc]=NULL;
        if(set_env("WINE_PS5_INITIAL_CLIENT","1"))goto done;
        stage("wine-child: initial Windows client\n");
    } else if(boot.flags || boot.argc)goto done;
#ifdef PROTON_CHILD_GPU_DEVICE_QUERY
    if(count==2)gpu_fd=fds[1];
#endif
    if (server_fd < 0 || server_fd == 3 ||
        !memchr(boot.runtime, 0, sizeof(boot.runtime)) || !memchr(boot.prefix, 0, sizeof(boot.prefix)) ||
        !memchr(boot.cwd, 0, sizeof(boot.cwd)) || !memchr(boot.dllpath, 0, sizeof(boot.dllpath)) ||
        !memchr(boot.overrides, 0, sizeof(boot.overrides)) || !memchr(boot.debug, 0, sizeof(boot.debug)) ||
        boot.environment[sizeof(boot.environment)-1]) goto done;
    // The application's settings: the initial application's catalog entry, or the ones the title found in the
    // catalog for this process's executable (pw_wine_process.h). Only the title fills them.
    if(boot.application_bytes>sizeof(boot.application) ||
       (boot.application_bytes && pw_environment_validate(boot.application,boot.application_bytes)))goto done;
    stage("wine-child: bootstrap socket received\n");
#ifdef PROTON_CHILD_GPU_DEVICE_QUERY
    gpu_device_query("before_elevation",gpu_fd);
#endif
    if(set_env("WINE_PS5_PAYLOAD_CHILD", "1"))goto done;
    stage("wine-child: payload entry, control fd 3; no elevation needed\n");
    start_stamp("payload-entry", -1);
    // The title and its children share one budget of about 250 open files (fd-budget-01). The server, in the
    // title, opens every Windows file and parks the idle ones (patch 0929); a child's own files are few, and it
    // parks them early so they never crowd the server's out.
    if(!getenv("PW_FDV_HIGH_WATER") && set_env("PW_FDV_HIGH_WATER","24"))goto done;
#ifdef PROTON_CHILD_GPU_BROKER
    if(broker_fd>=0) {
        char gpu_value[24];snprintf(gpu_value,sizeof(gpu_value),"%d",broker_fd);
        if(set_env("WINE_PS5_GPU_FD",gpu_value) || set_env("WINE_PS5_GPU_BROKER","1"))goto done;
        stage("wine-child: GPU broker descriptor ready\n");
        // Before Wine maps anything: the ranges the owner places host-visible GPU buffers in (src/pw_gpu_broker.h).
        if(pw_gpu_reserve_host_ranges(sceKernelReserveVirtualRange,sceKernelMunmap)) {
            if(set_env("WINE_PS5_GPU_RESERVED","1"))goto done;
            stage("wine-child: GPU broker host ranges reserved\n");
        } else stage("wine-child: GPU broker host ranges not reserved\n");
    }
#endif
#ifdef PROTON_CHILD_GPU_DEVICE_QUERY
    gpu_device_query("after_elevation",gpu_fd);
    if(gpu_fd>=0) { close(gpu_fd);gpu_fd=-1; }
#endif
#ifdef PROTON_CHILD_GPU_PREFLIGHT
    if(gpu_preflight())goto done;
#endif
    struct rlimit files;
    if (!getrlimit(RLIMIT_NOFILE, &files)) { files.rlim_cur = files.rlim_max; (void)setrlimit(RLIMIT_NOFILE, &files); }
    char ntdll[1200], fd_value[24];
    snprintf(ntdll, sizeof(ntdll), "%s/ntdll.prx", boot.runtime);
    snprintf(fd_value, sizeof(fd_value), "%d", server_fd);
    const PwWineStartConfig config = {.prefix=boot.prefix, .ntdll_dir=boot.runtime, .ntdll_path=ntdll};
    const PwWineStartOps ops = {.load_start=sceKernelLoadStartModule, .module_info=sceKernelGetModuleInfo, .set_env=set_env};
    // The spawner's settings for Wine and its graphics (pw_wine_process.c), before the loader's own.
    for (const char *entry = boot.environment; *entry && entry < boot.environment + sizeof(boot.environment);
         entry += strlen(entry) + 1) {
        const char *equals = strchr(entry, '=');
        char name[128];
        if (!equals || equals == entry || (size_t)(equals - entry) >= sizeof(name)) continue;
        memcpy(name, entry, (size_t)(equals - entry)); name[equals - entry] = 0;
        if (set_env(name, equals + 1)) goto done;
    }
    // The application's own settings win over the spawner's (a game started from the desktop has its game.json
    // "env", not Explorer's), and lose only to what the loader derives next: its descriptors and paths.
    for(const char *entry=boot.application; boot.application_bytes && *entry; entry+=strlen(entry)+1) {
        const char *equals=strchr(entry,'=');char name[sizeof(boot.application)];
        memcpy(name,entry,(size_t)(equals-entry));name[equals-entry]=0;
        if(loader_owned(name)) { stage("wine-child: an application setting names the loader's own; left out\n");continue; }
        if(set_env(name,equals+1))goto done;
    }
    if(boot.application_bytes && !(boot.flags&PW_CHILD_INITIAL))stage("wine-child: application settings from the catalog\n");
    /* WINE_PS5_CHILDREN is what keeps the in-process sync device off in a helper: ntdll refuses it when that is set
     * (wine/patches/0925, "!getenv(\"WINE_PS5_CHILDREN\")"), and it is set before ntdll loads. The old forced
     * WINE_PS5_NTSYNC=0 said the same thing a second time and nothing read it (docs/PLAN.md). */
    if (pw_wine_start_environment(&wine, &config, &ops) ||
        set_env("WINESERVERSOCKET", fd_value) || set_env("WINE_PS5_CHILD", "1") ||
        set_env("WINE_PS5_CHILDREN", "1") ||
        set_env("WINEDLLPATH", boot.dllpath) || set_env("WINEDLLOVERRIDES", boot.overrides) ||
        set_env("WINEDEBUG", boot.debug)) {
        stage("wine-child: environment failed\n"); goto done;
    }
    /* Wine's modules load through this helper's own loader on every firmware (src/pw_prx_elf.h says why), unless the run
     * asks for the kernel's with WINE_PS5_PRX_LOADER=0. */
    const char *loader_reason = "default";
    const int user_space_loader = pw_prx_elf_wanted(boot.environment, sizeof(boot.environment), &loader_reason);
    {
        /* Before the kernel's module loader, which can stop this process inside the call (the pager error at
         * ntdll.prx's text, errno 92): the line must be on disk first. */
        char load_line[1300], loader_line[160];
        snprintf(loader_line, sizeof(loader_line), "wine-child: module loader=%s (%s)\n",
                 user_space_loader ? "user-space" : "kernel", loader_reason);
        snprintf(load_line, sizeof(load_line), "wine-child: loading ntdll path=%s\n", ntdll);
        { char directory[256]; snprintf(directory, sizeof(directory), "%s", ntdll); char *slash = strrchr(directory, '/'); if (slash) { *slash = 0; pw_malloc_trace_start(directory); } }
        stage(loader_line);
        stage(load_line);
        start_stamp("ntdll-loading", -1);
    }
    int elf_ntdll_handle = -1;
    if (user_space_loader) {
        const int elf_status = wine_start_elf(ntdll);
        elf_ntdll_handle = pw_prx_elf_ps5_last_handle();
        if (elf_status) {
            char fail_line[128];
            snprintf(fail_line, sizeof(fail_line), "wine-child: ntdll elf load failed status=%d\n", elf_status);
            stage(fail_line); goto done;
        }
    } else if (pw_wine_start_load(&wine, &config, &ops)) {
        stage("wine-child: ntdll load failed\n"); goto done;
    }
    stage("wine-child: ntdll loaded\n");
    start_stamp("ntdll-loaded", -1);
    {
        // The loader and the ntdll it loaded must speak one protocol, or the title refuses what this process sends
        // (a spawn request, its exit) and nothing says why.
        unsigned int (*protocol)(void) = (void *)pw_prx_lookup(wine.descriptor, "pw_ps5_child_protocol");
        char protocol_line[1400];
        snprintf(protocol_line, sizeof(protocol_line), "wine-child: %s speaks protocol %#x, the loader %#x\n", ntdll,
                 protocol ? protocol() : 0u, PW_CHILD_PROTOCOL);
        stage(protocol_line);
        if (!protocol || protocol() != PW_CHILD_PROTOCOL) {
            stage("wine-child: ntdll and loader differ; deploy the title and the runtime together\n");
            goto done;
        }
    }
    if (user_space_loader) {
        static PwWineDlOps elf_dl_ops;
        elf_dl_ops.load_start = pw_prx_elf_ps5_load_start;
        elf_dl_ops.module_info = pw_prx_elf_ps5_module_info;
        void (*dl_configure)(const PwWineDlOps *, const char *) =
            (void *)pw_prx_lookup(wine.descriptor, "pw_wine_dl_configure");
        void *(*dl_adopt)(const char *, int32_t) = (void *)pw_prx_lookup(wine.descriptor, "pw_wine_dl_adopt");
        if (dl_configure) dl_configure(&elf_dl_ops, config.ntdll_dir);
        if (dl_adopt && elf_ntdll_handle > 0) dl_adopt(ntdll, elf_ntdll_handle);
        stage(dl_configure ? "wine-child: sibling modules load through the user-space loader\n"
                           : "wine-child: ntdll has no dl configure; siblings use the kernel\n");
    }
    int (*set_application)(const char *,size_t)=
        (void *)pw_prx_lookup(wine.descriptor,"pw_ps5_set_initial_environment");
    if(!set_application || (boot.application_bytes &&
       set_application(boot.application,boot.application_bytes))) {
        stage("wine-child: application environment refused\n");goto done;
    }
    int (*adopt_memory)(void *(*)(void *,size_t,int,int,int,off_t),int (*)(const void *,size_t,int)) =
        (void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_set_payload_memory");
    if(!adopt_memory || adopt_memory(sdk_mmap,sdk_mprotect))goto done;
    /* The carrier image this loader gave back (native/proton_payload_child.c): ntdll converts an executable range the
     * kernel still names as that image to JIT memory before it can fault there (SYSTEM_XO_VIOLATION; the fixed-base
     * 32-bit installer's first execute at 0x400000). Nothing low was released when the module was not found. */
    if(carrier_refused==0 && carrier_end>0x400000) {
        int (*set_low)(uintptr_t,uintptr_t)=(void *)pw_prx_lookup(wine.descriptor,
                                                                  "__wine_ps5_set_payload_low_image");
        char low_line[140];
        const int status=set_low?set_low(0x400000,(uintptr_t)carrier_end):-1;
        snprintf(low_line,sizeof(low_line),"wine-child: low image base=0x400000 end=%#lx set=%d\n",
                 (unsigned long)carrier_end,status);
        stage(low_line);
    }
    exec_stats=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_exec_stats");
    foreign_stats=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_foreign_stats");
    code_share_stats=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_code_share_stats");
    code_cache_stats=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_code_cache_stats");
    code_share_mapped=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_code_share_mapped_kib");
    direct_section_stats=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_section_stats");
    {
        void (*vlock)(PwWinePerfVlock *)=(void *)pw_prx_lookup(wine.descriptor,"pw_wine_perf_vlock_read");
        unsigned (*dmem)(uint64_t *,unsigned)=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_dmem_holder");
        unsigned (*doom)(int64_t *,unsigned)=(void *)pw_prx_lookup(wine.descriptor,"pw_ps5_child_doom");
        void (*doomed)(unsigned)=(void *)pw_prx_lookup(wine.descriptor,"pw_ps5_child_doomed");
        __atomic_store_n(&watch_doomed,doomed,__ATOMIC_RELEASE);
        __atomic_store_n(&watch_vlock,vlock,__ATOMIC_RELEASE);
        __atomic_store_n(&watch_dmem,dmem,__ATOMIC_RELEASE);
        __atomic_store_n(&watch_doom,doom,__ATOMIC_RELEASE);
        char watch_line[96];
        snprintf(watch_line,sizeof(watch_line),"wine-child: watch vlock=%d dmem_lock=%d doom=%d stall_stamp=%d\n",
                 !!vlock,!!dmem,!!doom,!!doomed);
        stage(watch_line);
    }
    stage("wine-child: payload SDK memory adopted; execute through JIT memory\n");
    void (*set_frame_allocator)(int (*)(size_t)) =
        (void *)pw_prx_lookup(wine.descriptor, "pw_wine_set_frame_allocator");
    if (!set_frame_allocator) goto done;
    set_frame_allocator(pw_shared_page_create);
    stage("wine-child: memory-only frame allocator adopted\n");
    const char *direct = getenv("WINE_PS5_PAYLOAD_DMEM");
    if (!direct || strcmp(direct, "0")) {
        int (*adopt_direct)(int (*)(size_t,size_t,int,int64_t *)) =
            (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_set_payload_direct_memory");
        if (!adopt_direct || adopt_direct(payload_allocate_direct)) {
            stage("wine-child: application allocator adoption failed; deploy the matching runtime\n");
            goto done;
        }
        stage("wine-child: application direct-memory allocator adopted\n");
    }
    /* The kernel's own sigaction for ntdll (patch 0945): in a WoW64 process the signal handlers must be entered
     * before anything reads %fs, which then holds the 32-bit TEB; libkernel's sigaction wraps them in code that
     * does (docs/IA32_DESIGN.md, M0). A payload may make the system call; a PRX module may not. */
    int (*adopt_sigaction)(int (*)(int, const struct sigaction *, struct sigaction *)) =
        (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_set_payload_sigaction");
    if (adopt_sigaction && !adopt_sigaction(payload_raw_sigaction))
        stage("wine-child: the kernel's sigaction adopted\n");
    const char *native_signals = getenv("WINE_PS5_NATIVE_SIGNALS");
    if (native_signals && !strcmp(native_signals, "1")) {
        struct sigaction action = {0};
        action.sa_handler = native_signal_probe;
        sigemptyset(&action.sa_mask);
        if (payload_raw_sigaction(SIGUSR1, &action, NULL)) goto done;
    }
    int (*adopt_module)(int (*)(int32_t)) = (void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_set_payload_module");
    /* A user-space module (wine.module < 0) has no kernel handle to wire: its text is JIT memory and its data is
     * anonymous memory this loader owns. The hook still goes to ntdll for the kernel-loaded modules of a title. */
    if(!adopt_module || adopt_module(payload_prepare_module) ||
       (wine.module >= 0 && payload_prepare_module(wine.module)))goto done;
    if(program_plan.change) {
        int (*thread_start)(void (*)(void))=(void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_set_payload_thread_start");
        if(!thread_start || thread_start(program_thread_start)) {
            stage("wine-child: no thread start hook in this ntdll; deploy the title and the runtime together\n");goto done;
        }
    }
    void (*set_output)(void (*)(const char *,size_t)) = (void *)pw_prx_lookup(wine.descriptor, "__wine_ps5_set_output_sink");
    int (*set_cwd)(const char *) = (void *)pw_prx_lookup(wine.descriptor, "pw_cwd_set");
    int (*agent)(int) = (void *)pw_prx_lookup(wine.descriptor, "pw_ps5_child_start");
    if (!set_output || !set_cwd || !agent || set_cwd(boot.cwd) || agent(3)) goto done;
    set_output(output);
    const struct pw_child_message ready = {PW_CHILD_PROTOCOL, PW_CHILD_READY, getpid(), 0, 0};
    if (send(3, &ready, sizeof(ready), MSG_NOSIGNAL) != sizeof(ready)) goto done;
    acknowledged = 1;
    pthread_attr_t attr;
    pthread_t thread;
    if (pthread_attr_init(&attr)) goto done;
    /* Wine switches to its own Windows/Unix thread stacks. This native stack
     * is for bootstrap; keep concurrent broker helpers within flexible RAM. */
#ifdef PROTON_CHILD_GPU_BROKER
    const size_t stack_bytes=4 * 1024 * 1024;
#else
    const size_t stack_bytes=16 * 1024 * 1024;
#endif
    int started = pthread_attr_setstacksize(&attr, stack_bytes);
    (void)pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
    if (!started) started = pthread_create(&thread, &attr, wine_worker, NULL);
    pthread_attr_destroy(&attr);
    char thread_result[128];snprintf(thread_result,sizeof(thread_result),"wine-child: bootstrap stack_bytes=%zu thread_status=%d\n",stack_bytes,started);
    stage(thread_result);
    if (started) goto done;
    error = 0;
 done:
    {
        const struct pw_child_message ack = {PW_CHILD_PROTOCOL, PW_CHILD_READY, getpid(), error, 0};
        if (!acknowledged) (void)send(3, &ack, sizeof(ack), MSG_NOSIGNAL);
        else if (error) {
            if (server_fd >= 0) close(server_fd);
            const struct pw_child_message failed = {PW_CHILD_PROTOCOL, PW_CHILD_EXIT, getpid(), error, 0};
            (void)send(3, &failed, sizeof(failed), MSG_NOSIGNAL);
        }
    }
    stage(error ? "wine-child: failed; awaiting service close\n" : "wine-child: worker started\n");
    fsync(output_fd);
    if (!error) {
        pthread_attr_t attributes;
        pthread_t telemetry;
        int status = pthread_attr_init(&attributes);
        if (!status) {
            status = pthread_attr_setstacksize(&attributes, 256u << 10);
            if (!status) status = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
            (void)pthread_attr_setinheritsched(&attributes, PTHREAD_INHERIT_SCHED);
            if (!status) status = pthread_create(&telemetry, &attributes, payload_telemetry, NULL);
            pthread_attr_destroy(&attributes);
        }
        char line[96];
        snprintf(line,sizeof(line),"wine-child: telemetry worker status=%d\n",status);
        stage(line);
    }
    // The backstop's test: stamp TEST this long after the loop starts (WINETEST_DOOM_AFTER_MS).
    void (*doom_test)(unsigned)=NULL;
    long long doom_test_at=0;
    if (!error) {
        // Above the program's threads (RR 700 unless WINE_PS5_HELPER_PRIORITY says, src/pw_helper_sched.h; a lower
        // number runs first), so a busy program cannot starve the backstop, and never below where that setting put
        // this thread; after every thread that inherits this one's scheduling is made.
        SchedView self;
        sched_self(&self);
        int priority=program_plan.change && program_plan.priority<=690 ? program_plan.priority-1 : 690;
        if(self.priority>=PW_HELPER_SCHED_HIGHEST && self.priority<priority)priority=self.priority;
        if(priority<PW_HELPER_SCHED_HIGHEST)priority=PW_HELPER_SCHED_HIGHEST;
        const PwHelperSchedPlan backstop={1,self.policy,priority,0};
        char line[96];
        snprintf(line,sizeof(line),"wine-child: control thread priority=%d policy=%d status=%d\n",priority,self.policy,
                 sched_apply_self(&backstop));
        stage(line);
        // The run's own program only (tools/run-battery.py's doom-backstop): wineboot --init, services and the drivers
        // inherit WINETEST_ too, and a wineboot ended mid-update would leave the shared probe prefix half made.
        const char *after=getenv("WINETEST_DOOM_AFTER_MS");
        if (after && *after && getenv("WINE_PS5_INITIAL_CLIENT")) {
            doom_test=(void *)pw_prx_lookup(wine.descriptor,"pw_ps5_child_doomed");
            doom_test_at=payload_monotonic_ms()+strtoll(after,NULL,10);
        }
    }
    PwControlDoom doom_watch={0};
    /* Only native control observation here: no guest locks, and of the statistics only the lock-free exec, scheduling and direct-section tallies. */
    for (;;) {
        short events=0;
        const int polled = pw_control_watch(3,100,&events);
        const int poll_error=polled<0 ? errno : 0;
        const long long now=payload_monotonic_ms();
        if (doom_test && now>=doom_test_at) { doom_test(PW_CHILD_DOOM_TEST);doom_test=NULL; }
        if (doom_tick(&doom_watch,now,helper_suspend_tick(now))) return;
        if(error || (polled > 0 && (events & (POLLHUP|POLLERR|POLLNVAL)))) {
            const long long terminal_started = payload_monotonic_ms();
            struct stat current={0};
            const int final_stat=fstat(3,&current);
            int final_type=-1;socklen_t type_bytes=sizeof(final_type);
            const int type_status=getsockopt(3,SOL_SOCKET,SO_TYPE,&final_type,&type_bytes);
            const int same=!control_stat && !final_stat && current.st_dev==control_identity.st_dev &&
                           current.st_ino==control_identity.st_ino && current.st_mode==control_identity.st_mode;
            char terminal[320];
            snprintf(terminal,sizeof(terminal),"wine-child: payload terminal reason=%s bootstrap_error=%d poll=%d errno=%d revents=%#x same_metadata=%d stat=%d socket_status=%d socket_type=%d; returning to payload main\n",
                     error ? "bootstrap-refused" : "control-ended",error,polled,poll_error,(unsigned)events,same,final_stat,type_status,final_type);
            stage(terminal);
            if (!error) life_summary();
            exec_tally();
            code_share_tally();
        code_cache_tally();
            foreign_tally();
            sched_tally();
            direct_section_tally();
            if (!error) mmu4k_census((void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_mmu4k_stats"),1);
            const int flushed = fsync(output_fd);
            const int flush_error = flushed ? errno : 0;
            snprintf(terminal,sizeof(terminal),"wine-child: terminal flush status=%d errno=%d begin_ms=%lld end_ms=%lld syncs=%u\n",
                     flushed,flush_error,terminal_started,payload_monotonic_ms(),__atomic_load_n(&log_syncs,__ATOMIC_RELAXED));
            stage(terminal);
            return;
        }
    }
}
