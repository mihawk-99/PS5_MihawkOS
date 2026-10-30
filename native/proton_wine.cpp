// SPDX-License-Identifier: GPL-3.0-or-later
// The title owns input/audio; Wine and RADV own the game's display.
#include "proton_wine.h"
#include "proton_sched_probe.h"
#include "proton_loading.h"
#include "proton_limits_probe.h"
extern "C" {
#include "pw_dmem_share.h"
#include <sys/mman.h>
}

#include "proton_gpu_identity.h"
#include "proton_flight_recorder.h"
#ifdef PS5_GPU_BROKER
#include "proton_gpu_server.h"
#endif
#include "../src/proton_touchpad.h"
#include "../src/pw_controls.h"
#include "../src/pw_setup.h"
#include "../src/pw_runtime_check.h"
#include "../src/pw_sha256.h"
#include "../src/proton_dualsense.h"
#include "../ps5/src/proton_launch.h"
#include "../ps5/src/platform.h"
#include <ps5platform/memory.h>
#include <ps5platform/kernel.h>
#include <sys/resource.h>
extern "C" {
#include "../src/pw_wine_start.h"
#include "../src/pw_environment.h"
#include "../src/pw_window_snapshot.h"
#include "../src/pw_shader_cache.h"
#include "../src/pw_vm_census.h"
#include "../src/pw_fd_census.h"
#include "../src/pw_pipe_memory.h"
#include "../src/pw_pad_script.h"
#include "../src/pw_memory_guard.h"
#include "../src/pw_firmware.h"
#include "../src/pw_hang_breaker.h"
#include "../src/pw_log_sessions.h"
#include "../src/pw_start_failure.h"
#include "../src/pw_desktop_session.h"
#include "../src/pw_app_environment.h"
#include "../src/pw_shell_link.h"
#include "../src/pw_game_folders.h"
#include "../src/pw_wine_profile.h"
#include "../src/pw_pe_arch.h"
#include "../src/pw_memory_helper.h"
#include "../src/pw_aurelia.h"
#include "../src/pw_steam.h"
#include "../src/pw_steam_guide.h"
#include "../src/pw_path_nocase.h"
#include "../src/pw_steamcmd.h"
#include "../src/pw_steamcmd_setup.h"
#include "../src/pw_ntsync.h"
#include "../src/pw_speed_defaults.h"
#include "../src/pw_display_rate.h"
#include "../src/pw_suspend.h"
#include "../wine/ps5/pw_wine_sink.h"
#include "../wine/ps5/pw_wine_perf.h"
#include "../wine/ps5/pw_wine_fdv.h"
#include "../wine/ps5/pw_wine_process.h"
#include "pw_audio_ps5.h"
#include "pw_usb_input_ps5.h"
#include "../src/pw_game_mode.h"
#include "../src/pw_high_refresh.h"
#include "../src/pw_controller.h"
#include "../src/pw_detailed_logs.h"
#ifdef PS5_GPU_BROKER
#include "../src/pw_window_direct.h"
#endif
int sceUserServiceGetInitialUser(int32_t *user_id);
int32_t sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, const void *, int *);
int sceKernelGetModuleInfo(int32_t, void *);
uint64_t sceKernelGetTscFrequency(void);
uint64_t sceKernelGetProcessTime(void);
int scePthreadSetprio(pthread_t thread, int priority);
int sceKernelGetCurrentCpu(void);
int sceKernelSendNotificationRequest(int, void *, size_t, int);
void catchReturnFromMain(int);
}
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <thread>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <ctime>

constexpr bool same_button(unsigned a, unsigned b) { return a == b; }
static_assert(same_button(PW_PAD_SCRIPT_CROSS, PAD_CROSS) && same_button(PW_PAD_SCRIPT_CIRCLE, PAD_CIRCLE) && same_button(PW_PAD_SCRIPT_SQUARE, PAD_SQUARE) &&
    same_button(PW_PAD_SCRIPT_TRIANGLE, PAD_TRIANGLE) && same_button(PW_PAD_SCRIPT_UP, PAD_UP) && same_button(PW_PAD_SCRIPT_DOWN, PAD_DOWN) &&
    same_button(PW_PAD_SCRIPT_LEFT, PAD_LEFT) && same_button(PW_PAD_SCRIPT_RIGHT, PAD_RIGHT) && same_button(PW_PAD_SCRIPT_OPTIONS, PAD_OPTIONS) &&
    same_button(PW_PAD_SCRIPT_L1, PAD_L1) && same_button(PW_PAD_SCRIPT_R1, PAD_R1) && same_button(PW_PAD_SCRIPT_L3, PAD_L3) &&
    same_button(PW_PAD_SCRIPT_R3, PAD_R3) && same_button(PW_PAD_SCRIPT_TOUCH_PAD, PAD_TOUCH_PAD),
    "the pad script names the console's buttons");

namespace {
PwWineStart wine;
PwAudioPs5Ops audio;
int audio_port = -1;
int log_fd = -1;
int title_log_fd = -1;   // /data/ps5proton/logs/title-last.log, the say() mirror (ps5/src/platform.c)
void log_line(const std::string &line);

int (*flush_registry)(unsigned) = nullptr;
int (*terminate_game)() = nullptr;
// Every Windows program is in a helper (the launcher layout): a game that has to be ended is ended by SIGKILL to its
// helpers (src/pw_hang_breaker.h), listed by pw_ps5_helpers.
bool programs_in_helpers = false;
unsigned (*list_helpers)(int *, unsigned, int *) = nullptr;
// Before the SIGKILL: no helper starts after it, and one mid-spawn is waited for until gone (pw_ps5_end_spawns).
void (*end_spawns)() = nullptr;
// One pass of the direct-section collector on the way out, so the last 'gave back' line reaches klog.
void (*collect_sections)() = nullptr;
// The helpers sent SIGKILL, once per session: back_to_launcher replaces the title only once each is gone. The first
// exchange of ending owns the ending: end_helpers on the title's loop or the flight recorder's thread, or
// back_to_launcher's last check, after which nothing is killed. ended and ended_flips are read once ended_ready is set.
std::atomic<bool> ending{false}, ended_ready{false};
PwHangEnd ended{};
std::string ended_reason;   // why end_helpers ended the game, for the launcher; read once ended_ready is set
unsigned long long ended_flips = 0;
int (*stop_audio)(unsigned) = nullptr;
void (*fdv_stats)(PwWineFdvStats *) = nullptr;
int (*server_snapshot)(unsigned) = nullptr;
int (*server_user_processes)() = nullptr;   // the server's count of user processes (patch 2073)
void (*device_dump)(void (*)(void *, const char *), void *, uint32_t, uint32_t) = nullptr;
void (*device_counters)(PwNtsyncCounters *) = nullptr;
int (*device_gone)(int, int, uint32_t) = nullptr;   // pw_ntsync_process_gone
// The census of the console's system memory the memory helper takes (src/pw_memory_helper.h).
int (*memory_census)(pw_memory_census *) = nullptr;
extern "C" int sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
extern "C" int sceKernelVirtualQuery(const void *, int, void *, size_t);

// The title's own mappings, every 10 s of a session (Alpha 6, docs/PLAN.md Part 3): the GPU broker maps a game's
// graphics memory here, which no helper's census counts (a Direct3D 12 demo: ~1,000 system page tables in game in no helper).
static int title_vm_query(void *, uintptr_t address, uint8_t raw[PW_VM_RECORD])
{
    const int status = sceKernelVirtualQuery((const void *)address, 1, raw, PW_VM_RECORD);
    if ((uint32_t)status == UINT32_C(0x8002000d)) return 1;  /* no mapping after address */
    return status ? -1 : 0;
}

extern "C" int sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
extern "C" int sceKernelReleaseDirectMemory(int64_t, size_t);
extern "C" int sceKernelMunmap(void *, size_t);

// PROTON_PT2M_TITLE_PROBE=1 (a probe, never a default; docs/PLAN.md Part 3): does the kernel map direct memory that is
// 2 MiB-aligned at both ends without page-table pages? The title maps 64 MiB of CPU+GPU direct memory as a game's
// graphics memory is mapped (RADV: type 12, 2 MiB-aligned start), once ending on a 2 MiB line and once 16 KiB past
// it, touches every page and reads the system's free page tables around it, five rounds each. Then frees it all.
static void title_pt2m_probe()
{
    const size_t mib2 = size_t(2) << 20, base = size_t(64) << 20;
    auto free_tables = []() -> int {
        usleep(400000);   // the memory helper's census is read every 100 ms
        pw_memory_census fresh{};
        return memory_census && !memory_census(&fresh) ? int(fresh.page_tables_free) : -1;
    };
    for (int shape = 0; shape < 2; ++shape)
        for (int round = 0; round < 5; ++round) {
            const size_t bytes = shape ? base + 16384 : base;
            int64_t physical = -1;
            void *at = nullptr;
            const int before = free_tables();
            int status = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes, mib2, 12, &physical);
            if (!status) status = sceKernelMapDirectMemory(&at, bytes, 0x33, 0, physical, mib2);
            if (!status)
                for (size_t off = 0; off < bytes; off += 16384) ((volatile uint8_t *)at)[off] = 1;
            const int mapped = free_tables();
            if (!status) sceKernelMunmap(at, bytes);
            if (physical >= 0) sceKernelReleaseDirectMemory(physical, bytes);
            const int after = free_tables();
            say("Wine: pt2m title probe shape=%s round=%d status=%#x at=%p aligned_start=%d free before=%d mapped=%d after=%d "
                "cost=%d", shape ? "tail-16k" : "aligned", round, (unsigned)status, at, at && !((uintptr_t)at & (mib2 - 1)),
                before, mapped, after, before - mapped);
        }
}

static void title_vm_census_say()
{
    static PwVmCensus census;
    char groups[640], windows[384];
    pw_vm_census_walk(&census, title_vm_query, nullptr, nullptr);
    pw_vm_census_format_regions(&census, 6, groups, sizeof(groups));
    pw_vm_census_format_windows(&census, 8, windows, sizeof(windows));
    say("Wine: title vm page tables ~%llu (2m-direct %llu) mappings=%u%s by group %s; windows %s",
        (unsigned long long)census.regions_2m, (unsigned long long)census.regions_2m_direct, census.mappings,
        census.truncated ? " (truncated)" : census.failed ? " (walk failed)" : "", groups, windows);
}
// The first and the latest cold helper start (wine/ps5/pw_wine_process.h), for STOP's slowdown trigger.
void (*start_times)(uint64_t *, uint64_t *) = nullptr;
// The helpers' mappings of shared code copies beyond the first of each (wine/ps5/pw_wine_codeshare.h), which the census
// counts per mapping and the kernel of firmware 13 once.
uint64_t (*code_share_duplicates)() = nullptr;
unsigned (*code_cache_stats)(uint64_t *, unsigned) = nullptr;   // the program in the title's code budget (__wine_ps5_code_cache_stats)
std::atomic<bool> leaving{false};
std::atomic<bool> graphics_claimed{false};
// The one program the title runs itself beside the broker (start_title_client).
std::atomic<bool> title_client_claimed{false};
int (*owner_result)() = nullptr;
// The Mihawk OS desktop session, supervised here (src/pw_desktop_session.h): the launch named its entry, Explorer is
// the session root, and game mode unloads and restarts it. session_result is the session's own verdict when it ends
// with Explorer unloaded (no root left to report one).
bool desktop_session = false;
int session_result = -1;
int (*session_connect)(const char *) = nullptr;
int (*session_start_root)(int,const char *,const char *,const char *,int,const char *const *) = nullptr;
int (*session_root_present)() = nullptr;
int (*session_unload_root)() = nullptr;
// The owner's GPU broker, stopped on the way back to the launcher once every child has gone.
bool owner_broker_running = false;
void (*owner_wait)() = nullptr;

// What goes wrong during a session, said on screen (docs/SELF_INSTALL.md, phase 3): a PS5 system notification, which
// the system shows over the title (the owner saw one, phase 0's probe 5), with no shell code. Each notice is also kept
// for the launcher, which shows it after the session (PROTON_SESSION_NOTICES), in case it was missed. Each kind is said
// once a session, from the title's loop or on the way back to the launcher; any other thread only fills its slot.
enum NoticeKind { NOTICE_MEMORY_CLOSE, NOTICE_MEMORY_END, NOTICE_SPAWN, NOTICE_GPU, NOTICE_FOLDER,
                  NOTICE_MEMORY_SWAP, NOTICE_TEST, NOTICE_PAYLOADS, NOTICE_GPU_MEMORY, NOTICE_KINDS };
// 0 empty, 1 being written, 2 ready, 3 kept for the launcher and posted
struct Notice { std::atomic<int> state{0}; char text[512]; };
Notice notices[NOTICE_KINDS];
std::atomic<int> spawn_errno{0}, spawn_pid{0}, gpu_lost_pid{0};   // from Wine's helpers (pw_ps5_set_child_notice), for the loop
// The first program whose graphics memory the broker refused for want of direct memory, and its request (graphics patch
// 0042): a game then stops on its own "out of memory", which names no cause.
std::atomic<int> gpu_memory_pid{0};
std::atomic<unsigned long long> gpu_memory_bytes{0};
void gpu_memory_refused(int pid, unsigned long long bytes)
{
    int none = 0;
    if (gpu_memory_pid.compare_exchange_strong(none, pid > 0 ? pid : -1)) gpu_memory_bytes.store(bytes);
}
std::atomic<bool> spawn_said{false};
std::string session_prefix;                         // the session's prefix, set before Wine starts

__attribute__((format(printf, 2, 3))) void notice_set(NoticeKind kind, const char *format, ...)
{
    int empty = 0;
    if (!notices[kind].state.compare_exchange_strong(empty, 1)) return;
    va_list args;
    va_start(args, format);
    vsnprintf(notices[kind].text, sizeof(notices[kind].text), format, args);
    va_end(args);
    notices[kind].state.store(2, std::memory_order_release);
}

// pw_ps5_set_child_notice's: on Wine's spawning and monitoring threads, so it only records.
void child_notice(int kind, int pid, int value)
{
    int none = 0;
    if (kind == PW_CHILD_NOTICE_SPAWN) {
        // The pid of the failure whose errno is kept, so the notice reads that helper's log.
        if (spawn_errno.compare_exchange_strong(none, value ? value : -1) && pid > 0)
            spawn_pid.store(pid, std::memory_order_relaxed);
    }
    else if (kind == PW_CHILD_NOTICE_GPU) gpu_lost_pid.compare_exchange_strong(none, pid > 0 ? pid : -1);
#ifdef PS5_GPU_BROKER
    else if (kind == PW_CHILD_NOTICE_GAME_MODE) pw_window_direct_set_process(pid, value);   // a game's own Game Mode
#endif
}

void notices_post()
{
    if (const int error = spawn_errno.load(std::memory_order_relaxed); error && !spawn_said.exchange(true)) {
        // The per-boot class, freed only by a restart: errno 5, or the boot's pipe memory more than half used, as a
        // fresh pipe's buffer shows it (src/pw_pipe_memory.h; the kernel's own totals are refused to the title).
        const long buffer = pw_pipe_buffer_probe();
        const bool pipes = pw_pipe_memory_low(buffer);
        // A helper that died inside a module load (errno 92: the kernel's pager error at a fake-signed module's text)
        // leaves nothing but its own log, which names the module and the firmware (src/pw_start_failure.h).
        PwStartFailure failure{};
        char helper_log[96], described[512];
        const int pid = spawn_pid.load(std::memory_order_relaxed);
        bool named = false;
        if (pid > 0) {
            snprintf(helper_log, sizeof(helper_log), PW_LOG_SESSIONS_ROOT "/0/wine-child-%d.log", pid);
            named = pw_start_failure_read(helper_log, &failure) == 0 && failure.module[0];
            if (named) pw_start_failure_describe(&failure, error, described, sizeof(described));
        }
        say("Wine: a helper could not start (errno %d); a fresh pipe takes %ld bytes%s%s%s", error, buffer,
            pipes ? " (low: more than half the console's pipe memory is used)" : "",
            named ? "; its log says: " : "", named ? described : "");
        if (named)
            notice_set(NOTICE_SPAWN, "%s", described);
        else if (error == EIO || pipes)
            notice_set(NOTICE_SPAWN, "A program could not start (errno %d)%s. Restart the PS5.", error,
                       pipes ? ": the PS5's pipe memory is running out" : "");
        else
            notice_set(NOTICE_SPAWN, "A program could not start (errno %d).", error);
    }
    if (gpu_lost_pid.load(std::memory_order_relaxed))
        notice_set(NOTICE_GPU, "A program stopped: the GPU did not answer it.");
    if (const int pid = gpu_memory_pid.load(std::memory_order_relaxed)) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true))
            say("Wine: process %d asked for %llu MiB more graphics memory than the console had free", pid,
                gpu_memory_bytes.load() >> 20);
        notice_set(NOTICE_GPU_MEMORY, "The game ran out of graphics memory: the PS5 shares its memory between the game "
                   "and its graphics. Lower its texture quality or other graphics settings.");
    }
    // Kept, then posted, each once (2 to 3), whichever thread gets there. The payload tip is only kept, for the
    // launcher: a system notification draws over the game's top right (the gate's window captures on the Pro,
    // memplan-pro-gate-2026-10-28-01: host-dmem64's pixel at 1801,102 read the notification's blue).
    for (int kind = 0; kind < NOTICE_KINDS; ++kind) {
        Notice &notice = notices[kind];
        int ready = 2;
        if (!notice.state.compare_exchange_strong(ready, 3, std::memory_order_acquire)) continue;
        if (FILE *file = fopen(proton_title_path(PROTON_SESSION_NOTICES).c_str(), "a")) {
            fprintf(file, "%s\n", notice.text);
            fclose(file);
        }
        if (kind == NOTICE_PAYLOADS) {
            say("notice: %s (kept for the launcher)", notice.text);
            continue;
        }
        struct { char reserved[45]; char message[3075]; } request{};   // the payload SDK's samples/hello_world
        snprintf(request.message, sizeof(request.message), "%s: %s", PS5_TITLE_NAME, notice.text);
        const int posted = sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
        say("notice: %s (posted %d)", notice.text, posted);
    }
}

// 1 for a 32-bit image fixed at 0x400000 (src/pw_pe_arch.h), by its Windows path in the session's prefix: its helper
// gives the carrier's image there back (pw_ps5_set_fixed_base_lookup). Called on Wine's spawning threads; only reads.
int title_fixed_base(const char *windows_path)
{
    char host[1024];
    struct PwPeInfo pe;
    if (windows_path && (!strncmp(windows_path, "\\??\\", 4) || !strncmp(windows_path, "\\\\?\\", 4))) windows_path += 4;
    return windows_path && !pw_wine_launch_host_dir(session_prefix.c_str(), windows_path, host, sizeof(host)) &&
           pw_pe_arch_file(host, &pe) == PW_PE_X86 && pw_pe_arch_fixed_base32(&pe);
}

// A game that has not ended after its close request: what every thread waits
// on, in the server (each thread's wait, queue, last request, and who holds
// the server lock) and in the synchronization device (who holds its lock and
// its blocked waits; with WINE_PS5_NTSYNC_TRACE=1 also its last operations).
// Read from this thread with time limits, so a stuck lock is reported, never
// waited for.
// What the console lets this process run on, against what sysconf says: Wine tells a game
// sysconf's count, and Java sizes its thread pools by it. With PROTON_CPU_REPORT=1, 16
// threads also record for about 200 ms which CPUs they ran on. They run at the lowest
// priority and sleep between samples: the console's FIFO scheduling never takes a CPU from a
// thread that does not give it up, and 16 spinning threads at the title's own priority starved
// the title for good (2026-10; record removed from the tree, in git history).
unsigned long long cpu_bits(const unsigned char *set, size_t bytes)
{
    unsigned long long bits = 0;
    for (size_t i = 0; i < 64 && i / 8 < bytes; ++i)
        if (set[i / 8] & (1u << (i % 8))) bits |= 1ull << i;
    return bits;
}
std::atomic<unsigned long long> cpus_seen{0};
void *cpu_probe_thread(void *)
{
    scePthreadSetprio(pthread_self(), 767);
    for (int i = 0; i < 2000; ++i) {
        const int cpu = sceKernelGetCurrentCpu();
        if (cpu >= 0 && cpu < 64) cpus_seen.fetch_or(1ull << cpu, std::memory_order_relaxed);
        usleep(100);
    }
    return nullptr;
}
// The CPUs this process may run on (CPU_LEVEL_WHICH), or at another level of its CPU sets (CPU_LEVEL_CPUSET: its
// set; CPU_LEVEL_ROOT: the root set above it), from the kernel: the size its CPU set takes is not this SDK's cpuset_t
// (ERANGE for 32 and 128 bytes), so each size is tried. 0 when none answers.
unsigned long long cpus_allowed(size_t *bytes_used, int level = CPU_LEVEL_WHICH)
{
    for (size_t bytes : {size_t(8), size_t(16), size_t(24), size_t(32), size_t(64)}) {
        alignas(8) unsigned char set[64] = {};
        if (!cpuset_getaffinity(level, CPU_WHICH_PID, -1, bytes, reinterpret_cast<cpuset_t *>(set))) {
            *bytes_used = bytes;
            return cpu_bits(set, bytes);
        }
    }
    return 0;
}

unsigned long long cpus_sampled()
{
    pthread_t threads[16];
    int started = 0;
    for (auto &t : threads) started += !pthread_create(&t, nullptr, cpu_probe_thread, nullptr);
    for (int i = 0; i < started; ++i) pthread_join(threads[i], nullptr);
    return cpus_seen.load();
}

// Wine shows a game sysconf's 16 CPUs; a game process runs on 13 (a Java game, 2026-10; record removed from the tree: CPUs 0-12),
// and Java sizes its collector and a Java game its chunk workers by the count, so 16 threads
// share 13 CPUs with the render thread. WINE_CPU_TOPOLOGY (Proton's, given the CPUs by number,
// as the console has no /sys topology) makes Wine show the ones the process has; a game that
// sets it keeps its own. PROTON_CPU_REPORT=1 also samples where 16 threads run.
void cpu_topology()
{
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    size_t bytes = 0;
    unsigned long long mask = cpus_allowed(&bytes);
    const char *how = "the kernel";
    const char *report = getenv("PROTON_CPU_REPORT");
    if (report && !strcmp(report, "1")) {
        const unsigned long long seen = cpus_sampled();
        say("cpu: sysconf %ld online; the kernel's set %#llx (%zu bytes); 16 sampling threads ran on %#llx (%d CPUs)",
            online, mask, bytes, seen, __builtin_popcountll(seen));
    }
    if (!mask) {
        mask = cpus_sampled();
        how = "sampling";
    }
    // Each helper logs the same three (wine/ps5/pw_wine_process.c): whether a helper could join this set at all.
    size_t level_bytes = 0;
    say("cpu: this process may run on %#llx, its CPU set holds %#llx, the root set above it %#llx", mask,
        cpus_allowed(&level_bytes, CPU_LEVEL_CPUSET), cpus_allowed(&level_bytes, CPU_LEVEL_ROOT));
    const int count = __builtin_popcountll(mask);
    if (getenv("WINE_CPU_TOPOLOGY") || !count || count >= online) {
        say("cpu: %d of %ld CPUs by %s; Wine's topology left as it is", count, online, how);
        return;
    }
    std::string topology = std::to_string(count) + ":";
    for (int i = 0; i < 64; ++i)
        if (mask & (1ull << i)) topology += (topology.back() == ':' ? "" : ",") + std::to_string(i);
    setenv("WINE_CPU_TOPOLOGY", topology.c_str(), 1);
    say("cpu: %d of %ld CPUs by %s: WINE_CPU_TOPOLOGY=%s", count, online, how, topology.c_str());
}

void output(const char *text, size_t length);
// The Aurelia client's --json lines (src/pw_aurelia.h): a session that runs its binary has its library, sign-in
// challenge and results read from the session's own output, for the title's overlay. list --json prints its array as
// one long line, which is why output()'s line buffer holds 16 KiB for every session.
bool aurelia_session()
{
    static int on = -1;
    if (on < 0) {
        // A launcher launch names the game by profile (the launcher's Aurelia entry), a test run by path; either one
        // that names the client turns the capture and the overlay on (2026-10-22: only the path was checked, so a
        // launcher launch ran headless with the broker on the display and showed black).
        const std::string &path = proton_launch().path;
        const std::string &profile = proton_launch().profile;
        on = proton_launch().aurelia || profile == "aurelia" || path.find("aurelia_windows") != std::string::npos ? 1 : 0;
    }
    return on == 1;
}
// The Steam runtime session (docs/aegis/plans/2026-10-23-steamcmd-runtime.md): Valve's headless
// SteamCMD started as the program, its console output read for the overlay. The profile "steamcmd"
// is the diagnostic entry; a game's prelaunch reuses the same reader once the runtime is signed in.
bool steamcmd_session()
{
    static int on = -1;
    if (on < 0) {
        const std::string &path = proton_launch().path;
        const std::string &profile = proton_launch().profile;
        on = profile == "steamcmd" || path.find("steamcmd") != std::string::npos ? 1 : 0;
    }
    return on == 1;
}
void aurelia_line(const char *line);
uint64_t monotonic_ms();
// When the sign-in was confirmed: the session ends this many ms later (0: none). Read and written under the same lock.
static volatile uint64_t aurelia_end_ms;
// A list session parsed a library: the shelf is the screen until a game is chosen.
static bool aurelia_library_ready;
// What the session hands over to: the sign-in's library list, or the game the shelf chose. return_to_launcher gives
// this to the launcher in its own final restart, which otherwise writes a bare launcher request and overwrites
// anything wine-run.txt held (2026-10-22: the shelf's play request was lost exactly that way).
static bool aurelia_next_set;
static ProtonGame aurelia_next;
// The install target's name, from the library snapshot (or the app id when the snapshot does not know it): the install
// session's command line carries the app id, not a name, and the screen says "Installing <name> · N%".
static std::string aurelia_install_name;

// The phase the loading screen starts from, known from the client's own command line (the launcher's profile arguments
// or a test run's args= override, both already applied to game.arguments): login, list or install <app id>.
void aurelia_phase_from_arguments(const std::string &command)
{
    size_t at = command.find_first_not_of(" \t");
    if (at == std::string::npos) return;
    const size_t end = command.find_first_of(" \t", at);
    const std::string word = command.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (word == "install") {
        size_t start = command.find_first_of("0123456789", at);
        std::string id;
        if (start != std::string::npos)
            for (size_t i = start; i < command.size() && command[i] >= '0' && command[i] <= '9'; ++i) id += command[i];
        std::string name = id.empty() ? std::string("the game") : id;
        if (!id.empty()) {
            if (FILE *file = fopen(PROTON_DATA_ROOT "/aurelia-library.txt", "r")) {
                char line[512];
                while (fgets(line, sizeof(line), file)) {
                    unsigned app = 0;
                    char game[160] = {};
                    if (sscanf(line, "%u\t%159[^\t]", &app, game) >= 2 && std::to_string(app) == id) { name = game; break; }
                }
                fclose(file);
            }
        }
        aurelia_install_name = name;
        proton_aurelia_phase(("Installing " + name).c_str());
        proton_aurelia_status("Waiting for the download to start");
    } else if (word == "list") {
        proton_aurelia_phase("Listing your library");
        proton_aurelia_status("Waiting for the client to finish");
    } else if (word == "login") {
        proton_aurelia_phase("Signing in");
        proton_aurelia_status("Waiting for the sign-in code");
    }
}

// One JSON document, however it is printed: a signed-in list --json is pretty-printed over many lines, the login
// result over four. Lines accumulate until the brackets balance (quotes respected), then the whole document is parsed.
void aurelia_feed(const char *line)
{
    static std::string document;
    static int depth;
    static bool in_string, escaped;

    if (document.empty()) {
        const char *at = line;
        while (*at == ' ' || *at == '\t') ++at;
        if (*at != '{' && *at != '[') return;
    }
    for (const char *at = line; *at; ++at) {
        const char c = *at;
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') in_string = true;
        else if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
    }
    document += line;
    document += '\n';
    if (depth <= 0) {
        aurelia_line(document.c_str());
        document.clear();
        depth = 0;
        in_string = escaped = false;
    }
}

// What the launcher's Aurelia page reads: the library a session parsed, and the account a sign-in named, as small
// text files under /data (rewritten whole, so a reader never sees a half line).
void aurelia_save_library(const PwAureliaGame *games, int count)
{
    const std::string path = PROTON_DATA_ROOT "/aurelia-library.txt", temporary = path + ".tmp";
    FILE *file = fopen(temporary.c_str(), "w");
    if (!file) return;
    for (int i = 0; i < count; i++)
        fprintf(file, "%u\t%s\t%u\t%d\n", games[i].app_id, games[i].name, games[i].playtime_minutes,
                games[i].update_available);
    fclose(file);
    rename(temporary.c_str(), path.c_str());
}

void aurelia_save_account(const char *account)
{
    const std::string path = PROTON_DATA_ROOT "/aurelia-account.txt", temporary = path + ".tmp";
    FILE *file = fopen(temporary.c_str(), "w");
    if (!file) return;
    fprintf(file, "%s\n", account && *account ? account : "signed in");
    fclose(file);
    rename(temporary.c_str(), path.c_str());
}

// ponytail: 128 games and 16 KiB lines, the prototype's ceiling; a library past it logs its first games and says so.
void aurelia_line(const char *line)
{
    static std::mutex lock;
    PwAureliaEvent event;
    static PwAureliaGame games[1024];   // a library of any size the client lists, up to 1024 (the page held 128 before)
    int count, libraries;

    if (line[0] != '{' && line[0] != '[') return;
    std::lock_guard<std::mutex> held(lock);
    if (pw_aurelia_parse_event(line, &event)) {
        switch (event.kind) {
        case PW_AURELIA_EVENT_QR_CHALLENGE:
            say("Aurelia: challenge %s", event.url);
            proton_aurelia_challenge(event.url);
            break;
        case PW_AURELIA_EVENT_QR_SCANNED:
            // The client says the phone read the code; the account still has to approve it. Until now this line was
            // "a JSON line the client does not know" (the Pro, 2026-10-22).
            say("Aurelia: the code was scanned; the account confirms it next");
            proton_aurelia_phase("Code scanned");
            proton_aurelia_status("Confirm the sign-in in the Steam app");
            break;
        case PW_AURELIA_EVENT_AUTH_REQUIRED:
            say("Aurelia: not signed in (%s)", event.hint);
            proton_aurelia_status("Not signed in");
            break;
        case PW_AURELIA_EVENT_RESULT:
            say("Aurelia: command finished (%s)%s%s", event.result_ok ? "ok" : "failed",
                event.status[0] ? " status=" : "", event.status);
            if (event.result_ok) proton_aurelia_status(event.status[0] ? event.status : "Finished");
            else proton_aurelia_error("The command failed");
            break;
        case PW_AURELIA_EVENT_ERROR:
            say("Aurelia: error %s", event.error);
            proton_aurelia_error(event.error);
            break;
        case PW_AURELIA_EVENT_PROGRESS: {
            static int last_tenth = -100;
            std::string status = event.state[0] ? std::string(event.state) : std::string("working");
            if (status == "downloading") status = "Downloading";
            // The phase carries the name and the percent the client is sure of; the status is the state's own word.
            if (!aurelia_install_name.empty()) {
                std::string phase = "Installing " + aurelia_install_name;
                if (event.percent >= 0) phase += " \xc2\xb7 " + std::to_string(event.percent) + "%";
                proton_aurelia_phase(phase.c_str());
            }
            proton_aurelia_progress(event.percent, status.c_str());
            if (event.percent >= 0 && event.percent / 10 != last_tenth / 10) {
                last_tenth = event.percent;
                say("Aurelia: %s %d%%", status.c_str(), event.percent);
            }
            break;
        }
        case PW_AURELIA_EVENT_SIGNED_IN: {
            std::string signed_in = event.account[0] ? std::string("Signed in as ") + event.account : "Signed in";
            say("Aurelia: %s", signed_in.c_str());
            aurelia_save_account(event.account);
            // The confirmation is followed by the library, on this same screen: the session hands over to a list
            // session through its own final restart (see aurelia_next).
            aurelia_next = ProtonGame{};
            aurelia_next.id = "aurelia";
            aurelia_next.arguments = "list --json";
            aurelia_next_set = true;
            proton_aurelia_phase(signed_in.c_str());
            proton_aurelia_status("Opening your library");
            // The client keeps its daemon after the login: end the session itself once the confirmation has shown, so
            // the launcher comes back instead of the overlay waiting for a code that was already approved.
            aurelia_end_ms = monotonic_ms() + 4000;
            break;
        }
        default: break;
        }
        return;
    }
    count = pw_aurelia_parse_library(line, games, sizeof(games) / sizeof(games[0]));
    if (count >= 0) {
        say("Aurelia: the library lists %d game%s", count, count == 1 ? "" : "s");
        aurelia_save_library(games, count);
        {
            // The session's own screen becomes the library: names and ids to the overlay, the account from the file.
            std::vector<const char *> names;
            std::vector<unsigned> ids;
            for (int i = 0; i < count; ++i) {
                names.push_back(games[i].name);
                ids.push_back(games[i].app_id);
            }
            std::string account;
            if (FILE *file = fopen(PROTON_DATA_ROOT "/aurelia-account.txt", "r")) {
                char line[80] = {};
                if (fgets(line, sizeof(line), file)) {
                    account = line;
                    while (!account.empty() && (account.back() == '\n' || account.back() == '\r')) account.pop_back();
                }
                fclose(file);
            }
            proton_aurelia_library(account.empty() ? nullptr : account.c_str(), names.data(), ids.data(), count);
            aurelia_library_ready = count > 0;
        }
        proton_aurelia_status(count ? "Your library is ready" : "Your library is empty");
        for (int i = 0; i < count && i < 3; i++) say("Aurelia:   %u %s", games[i].app_id, games[i].name);
        return;
    }
    libraries = pw_aurelia_parse_libraries(line);
    if (libraries >= 0) {
        say("Aurelia: %d library folder%s", libraries, libraries == 1 ? "" : "s");
        return;
    }
    say("Aurelia: a JSON line the client does not know: %.160s", line);
}
// Where the client's --json lines land: WINE_PS5_STDOUT when the launch set it, else the session's fallback file.
std::string aurelia_output_path()
{
    const char *configured = getenv("WINE_PS5_STDOUT");
    return configured && *configured ? configured : PROTON_DATA_ROOT "/logs/wine-stdout.txt";
}

// The program's standard output (WINE_PS5_STDOUT, which every session sets) is where the client's --json lines land:
// the program runs as a payload helper, so the title's own output sink never sees them. Polled from the session loop.
void aurelia_poll()
{
    static off_t offset;
    static char pending[16384];
    static size_t used;
    const std::string path = aurelia_output_path();
    struct stat status;
    char buffer[4096];
    ssize_t got;
    int fd = open(path.c_str(), O_RDONLY);

    if (fd < 0) return;
    static int watching;
    if (!watching++) say("Aurelia: watching %s", path.c_str());
    if (fstat(fd, &status) == 0 && status.st_size < offset) offset = 0; // a new session truncated it
    if (lseek(fd, offset, SEEK_SET) >= 0) {
        while ((got = read(fd, buffer, sizeof(buffer))) > 0) {
            for (ssize_t at = 0; at < got; at++) {
                if (buffer[at] == '\n' || used == sizeof(pending) - 1) {
                    pending[used] = 0;
                    aurelia_feed(pending);
                    used = 0;
                } else pending[used++] = buffer[at];
            }
            offset += got;
        }
    }
    close(fd);
}
// The Steam runtime's first-run page state (defined below; steamcmd_line needs it first).
static PwSteamcmdSetup steamcmd_setup;
static int steamcmd_stdin = -1;
static void steamcmd_setup_show();

// The SteamCMD console lines: the same program output the Aurelia client's JSON lands in, mapped to the
// overlay's phase/status/progress. Slice 1 only reports; slice 2 adds the setup page that writes stdin.
void steamcmd_line(const char *line)
{
    static std::mutex lock;
    static PwSteamcmdStatus status;
    if (!line || !*line) return;
    std::lock_guard<std::mutex> held(lock);
    if (!pw_steamcmd_line(&status, line)) return;
    say("SteamCMD: %s %.120s", pw_steamcmd_state_name(status.state), status.detail);
    if (pw_steamcmd_setup_event(&steamcmd_setup, &status)) steamcmd_setup_show();
    switch (status.state) {
    case PW_STEAMCMD_UPDATE: {
        std::string phase = "Updating the Steam runtime";
        if (status.percent >= 0) phase += " \xc2\xb7 " + std::to_string(status.percent) + "%";
        proton_aurelia_phase(phase.c_str());
        proton_aurelia_progress(status.percent, "Updating");
        break;
    }
    case PW_STEAMCMD_SIGNING_IN:
        proton_aurelia_phase("Signing in to Steam");
        proton_aurelia_status(status.account[0] ? (std::string("Signing in as ") + status.account).c_str()
                                                 : "Signing in to Steam");
        break;
    case PW_STEAMCMD_GUARD:
        proton_aurelia_phase("Signing in to Steam");
        proton_aurelia_status("Steam Guard code needed");
        break;
    case PW_STEAMCMD_SIGNED_IN:
        proton_aurelia_status("Steam runtime signed in");
        break;
    case PW_STEAMCMD_READY:
        proton_aurelia_phase("Steam runtime");
        proton_aurelia_status("Steam runtime ready");
        break;
    case PW_STEAMCMD_FAILED:
        proton_aurelia_error("Steam did not accept the sign-in");
        break;
    default:
        break;
    }
}

// The Steam runtime's first-run page: the setup state feeds the overlay, and the login/Guard lines go to the
// SteamCMD's stdin (the pipe the session's program was started with). The password lives only in this struct and
// the one write; the SteamCMD's own machine token (ssfn) is what persists.
static void steamcmd_setup_show()
{
    proton_aurelia_setup(true, steamcmd_setup.account, steamcmd_setup.password, steamcmd_setup.guard,
                         steamcmd_setup.focus, steamcmd_setup.status, steamcmd_setup.error,
                         steamcmd_setup.state == PW_STEAMCMD_SETUP_SIGNED_IN);
}

static void steamcmd_setup_begin()
{
    static bool begun;
    if (begun) return;
    begun = true;
    proton_loading_keep_display(true);
    pw_steamcmd_setup_init(&steamcmd_setup);
    steamcmd_setup_show();
}

static void steamcmd_key(int key)
{
    char line[256];
    FILE *file;
    if (!pw_steamcmd_setup_key(&steamcmd_setup, key)) return;
    steamcmd_setup_show();
    if (!pw_steamcmd_setup_command(&steamcmd_setup, line, sizeof line)) return;
    /* SteamCMD's interactive console does not read the title's stdin pipe; +runscript does read a file. The wrapper
     * watches this file and starts SteamCMD with it. It is removed after use and never logged. */
    file = fopen(PROTON_DATA_ROOT "/logs/steamcmd-login.txt", "w");
    if (file) {
        fputs(line, file);
        fclose(file);
        chmod(PROTON_DATA_ROOT "/logs/steamcmd-login.txt", 0600);
    }
    memset(line, 0, sizeof line);
    say(file ? "SteamCMD: login file written" : "SteamCMD: login file not written");
}

// The USB keyboard's event code is the Windows virtual key in its low byte (src/pw_usb_input.h); Shift comes from the
// merged keyboard state. Only what a Steam account or password can carry is mapped; the rest is ignored.
static void steamcmd_handle_key(uint32_t code, bool shift)
{
    const unsigned vk = code & 0xff;
    int key = 0;
    static const char shifted_digits[] = ")!@#$%^&*(";
    static const char numpad[] = "0123456789";
    if (vk >= 'A' && vk <= 'Z') key = shift ? int(vk) : int(vk - 'A' + 'a');
    else if (vk >= '0' && vk <= '9') key = shift ? shifted_digits[vk - '0'] : int(vk);
    else if (vk >= 0x60 && vk <= 0x69) key = numpad[vk - 0x60];
    else if (vk == 0x6a) key = '*';
    else if (vk == 0x6b) key = '+';
    else if (vk == 0x6d) key = '-';
    else if (vk == 0x6e) key = '.';
    else if (vk == 0x6f) key = '/';
    else if (vk == 0x0d) key = PW_STEAMCMD_KEY_ENTER;
    else if (vk == 0x09) key = PW_STEAMCMD_KEY_TAB;
    else if (vk == 0x08) key = PW_STEAMCMD_KEY_BACKSPACE;
    else if (vk == 0x1b) key = PW_STEAMCMD_KEY_ESCAPE;
    else if (vk == 0x20) key = ' ';
    else if (vk == 0xbd) key = shift ? '_' : '-';
    else if (vk == 0xbb) key = shift ? '+' : '=';
    else if (vk == 0xba) key = shift ? ':' : ';';
    else if (vk == 0xbc) key = shift ? '<' : ',';
    else if (vk == 0xbe) key = shift ? '>' : '.';
    else if (vk == 0xbf) key = shift ? '?' : '/';
    else if (vk == 0xde) key = shift ? '"' : '\'';
    else if (vk == 0xdb) key = shift ? '{' : '[';
    else if (vk == 0xdd) key = shift ? '}' : ']';
    else if (vk == 0xdc) key = shift ? '|' : '\\';
    else if (vk == 0xc0) key = shift ? '~' : '`';
    if (key) steamcmd_key(key);
}

// The SteamCMD console output is the session's program stdout (WINE_PS5_STDOUT, the path every session sets):
// polled from the session loop exactly like the Aurelia client's JSON. Offsets survive a truncated file.
void steamcmd_poll()
{
    static off_t offset;
    static char pending[4096];
    static size_t used;
    static bool started;
    const std::string path = aurelia_output_path();

    if (!started) {
        started = true;
        steamcmd_setup_begin();
        say("SteamCMD: watching %s", path.c_str());
    }
    static off_t log_offset;
    static char log_pending[4096];
    static size_t log_used;
    static bool log_started;
    const auto feed = [](const char *path, off_t &offset, char *pending, size_t &used) {
        int fd = open(path, O_RDONLY);
        struct stat status;
        char buffer[2048];
        ssize_t got;
        if (fd < 0) return;
        if (fstat(fd, &status) == 0 && status.st_size < offset) offset = 0;
        if (lseek(fd, offset, SEEK_SET) >= 0) {
            while ((got = read(fd, buffer, sizeof(buffer))) > 0) {
                for (ssize_t at = 0; at < got; at++) {
                    if (buffer[at] == '\n' || used == 4095) {
                        pending[used] = 0;
                        steamcmd_line(pending);
                        used = 0;
                    } else {
                        pending[used++] = buffer[at];
                    }
                }
                offset += got;
            }
        }
        close(fd);
    };
    if (!log_started) {
        log_started = true;
        struct stat old;
        if (stat(PROTON_DATA_ROOT "/prefixes/mihawk/drive_c/Games/SteamCMD/logs/connection_log.txt", &old) == 0)
            log_offset = old.st_size;
    }
    feed(path.c_str(), offset, pending, used);
    /* SteamCMD's console text is not the redirected stdout; its log carries the sign-in verdict. */
    feed(PROTON_DATA_ROOT "/prefixes/mihawk/drive_c/Games/SteamCMD/logs/connection_log.txt", log_offset, log_pending,
         log_used);
}

// Through the Wine log as well as klog: the log file is written as it goes.
void dump_line(void *, const char *text)
{
    char line[600];
    const int length = snprintf(line, sizeof(line), "ntsync: %s\n", text);
    if (length > 0) output(line, std::min<size_t>(static_cast<size_t>(length), sizeof(line) - 1));
}
void dump_hang(unsigned seconds)
{
    say("Wine: hang snapshot %u s after the close request", seconds);
    const int status = server_snapshot ? server_snapshot(1000) : -1;
    say("Wine: server snapshot status=%d", status);
    if (device_dump) device_dump(dump_line, nullptr, 400, 500);
}

// Each catalog game's own settings for a process another one started, in this session's prefix
// (src/pw_app_environment.h): a game launched from its desktop shortcut gets its game.json "env". Built before Wine
// starts and only read afterwards, on the spawning threads.
struct ApplicationTable {
    std::vector<ProtonGame> games;
    std::vector<std::vector<const char *>> words;
    std::vector<PwAppEntry> entries;
};
ApplicationTable applications;

void build_application_table(const std::vector<ProtonGame> &catalog, const std::string &prefix)
{
    for (const auto &game : catalog)
        if (!game.executable.empty() && game.complaint.empty() && proton_game_prefix(game) == prefix &&
            !pw_desktop_session_entry(game.executable.c_str()))
            applications.games.push_back(game);
    applications.words.resize(applications.games.size());
    for (size_t i = 0; i < applications.games.size(); ++i) {
        const ProtonGame &game = applications.games[i];
        for (const auto &word : game.env) applications.words[i].push_back(word.c_str());
        applications.entries.push_back(PwAppEntry{game.name.c_str(), game.executable.c_str(), game.arguments.c_str(),
                                                  applications.words[i].data(), applications.words[i].size()});
    }
}

// The Steam guide's sign-in session (src/pw_steam_guide.h) counts the processes the client starts (each of its updates
// restarts it) and sees its browser start (steamwebhelper), which opens the sign-in page about 20 s later (docs/STEAM.md).
static std::atomic<unsigned> guide_processes{0};
static std::atomic<bool> guide_browser{false};
static void guide_process(const char *image, const char *command_line)
{
    guide_processes.fetch_add(1, std::memory_order_relaxed);
    for (const char *text : {image, command_line}) {
        char lower[1024] = {};
        for (size_t i = 0; text && text[i] && i + 1 < sizeof(lower); ++i) lower[i] = (char)tolower((unsigned char)text[i]);
        if (strstr(lower, "steamwebhelper")) guide_browser.store(true, std::memory_order_relaxed);
    }
}

int title_application_lookup(const char *image, const char *command_line, char *block, size_t capacity, uint32_t *bytes)
{
    size_t index = 0;
    const PwAppMatch match = pw_app_environment_find(applications.entries.data(), applications.entries.size(), image,
                                                     command_line, &index);
    const char *slash = image ? strrchr(image, '\\') : nullptr;
    const char *shown = slash ? slash + 1 : image ? image : "";
    // Every child process's whole command line, in 800-character parts: Wine's own trace cuts strings at about
    // 300 characters, which hid the switches of a launcher's Chromium GPU process (2026-10-11; record removed from the tree).
    if (command_line) {
        const size_t length = strlen(command_line), part = 800;
        for (size_t at = 0, n = 1; at < length || at == 0; at += part, ++n) {
            say("Wine: process %s command line %zu/%zu: %.*s", shown, n, (length + part - 1) / part ? (length + part - 1) / part : 1,
                (int)(length - at < part ? length - at : part), command_line + at);
            if (!length) break;
        }
    }
    proton_cover_process(command_line);
    guide_process(image, command_line);
    if (match == PW_APP_AMBIGUOUS) {
        say("Mihawk: %s is in the library more than once and its command line does not say which; no settings", shown);
        return -1;
    }
    if (match != PW_APP_FOUND) return -1;
    size_t kept = 0;
    const int error = pw_app_environment_block(&applications.entries[index], block, capacity, bytes, &kept);
    if (error) {
        say("Mihawk: \"%s\" (%s): its library settings could not be given (%d)", applications.entries[index].name, shown, error);
        return -1;
    }
    say("Mihawk: \"%s\" (%s) gets its library settings: %zu", applications.entries[index].name, shown, kept);
    return 0;
}

// The desktop's shortcuts, one per library game in the session's prefix, one per game folder in its C:\Games
// (src/pw_game_folders.h) and one for MihawkBrowser, made before Explorer starts (src/pw_shell_link.h): Public
// Desktop, owned through a manifest, so the user's own shortcuts are never touched. Start lists the Public Desktop
// too, so this is also MihawkBrowser's Start entry (docs/MIHAWK_BROWSER.md §3.7).
void sync_desktop_shortcuts(const std::vector<ProtonGame> &catalog, const std::string &prefix)
{
    std::vector<PwGameFolder> folders;
    const int scanned = pw_game_folders_scan((prefix + "/drive_c/Games").c_str(),
        [](void *into, const PwGameFolder *folder) { static_cast<std::vector<PwGameFolder> *>(into)->push_back(*folder); },
        &folders);
    // The entries point into these: reserved whole, so none moves.
    std::vector<std::string> names, directories, descriptions, targets;
    std::vector<PwShellLinkEntry> entries;
    const size_t most = catalog.size() + folders.size();
    names.reserve(most); directories.reserve(most); descriptions.reserve(most); targets.reserve(most);
    for (const auto &game : catalog) {
        // An entry with a prefix of its own gets no shortcut: a session serves one prefix (one wineserver), so the
        // launcher lists it and starts it in a session of its own (examples/ps5proton/ps5proton.cpp).
        if (!game.desktop || !game.complaint.empty() || game.executable.empty() ||
            pw_desktop_session_entry(game.executable.c_str()) || proton_game_prefix(game) != prefix)
            continue;
        const size_t slash = game.executable.find_last_of("\\/");
        names.push_back(!game.name.empty() ? game.name : game.executable.substr(slash + 1));
        directories.push_back(!game.directory.empty() ? game.directory : game.executable.substr(0, slash));
        descriptions.push_back("Mihawk OS library: " + game.id);
        PwShellLinkEntry entry{};
        entry.display_name = names.back().c_str();
        entry.link.target = game.executable.c_str();
        entry.link.arguments = game.arguments.c_str();
        entry.link.working_directory = directories.back().c_str();
        entry.link.description = descriptions.back().c_str();
        entries.push_back(entry);
    }
    // The runtime's one read-only copy, outside every prefix, with MihawkBrowser's approved icon B3 (decision 11).
    std::string browser = "Z:" PROTON_DATA_ROOT "/runtime/ge11-7/lib/mihawk-browser/MihawkBrowser.exe";
    for (char &c : browser) if (c == '/') c = '\\';
    const std::string browser_directory = browser.substr(0, browser.find_last_of('\\'));
    PwShellLinkEntry browser_entry{};
    browser_entry.display_name = "MihawkBrowser";
    browser_entry.link.target = browser.c_str();
    browser_entry.link.working_directory = browser_directory.c_str();
    browser_entry.link.icon_path = "C:\\windows\\resources\\mihawk\\icons.dll";
    // A negative index names a resource ID: 1014 is PW_ICON_ORIGINAL_APP_BROWSER (.deps/mihawk-icons/icons.h; IDs
    // follow themes/windows11/icons/manifest.json's order, and app-browser follows the fourteen original icons).
    browser_entry.link.icon_index = -1014;
    browser_entry.link.description = "Browse the web and get game launchers";
    entries.push_back(browser_entry);
    // A folder with one program is a game; a library entry whose executable is in it covers it with its own shortcut.
    unsigned shown = 0, several = 0, covered = 0;
    const char *unchosen = nullptr;   // the first folder with several programs, named on screen (phase 3)
    for (const auto &folder : folders) {
        const ProtonGame *entry_of = nullptr;
        for (const auto &game : catalog)
            if (proton_game_prefix(game) == prefix && pw_game_folder_covers(folder.name, game.executable.c_str()))
                entry_of = &game;
        if (entry_of || folder.programs != 1) {
            covered += entry_of != nullptr;
            several += !entry_of && folder.programs > 1;
            if (!entry_of && folder.programs > 1 && !unchosen) unchosen = folder.name;
            say("Mihawk: games folder %s: %s", folder.name, entry_of ? ("library entry " + entry_of->id).c_str() :
                folder.programs ? "several programs at its top; a game.json names the game's" :
                "no program at its top (a game.json names one in a subfolder)");
            continue;
        }
        names.push_back(folder.name);
        directories.push_back(std::string("C:\\Games\\") + folder.name);
        targets.push_back(directories.back() + "\\" + folder.program);
        descriptions.push_back("Mihawk OS games folder: " + names.back());
        PwShellLinkEntry entry{};
        entry.display_name = names.back().c_str();
        entry.link.target = targets.back().c_str();
        entry.link.arguments = "";
        entry.link.working_directory = directories.back().c_str();
        entry.link.description = descriptions.back().c_str();
        entries.push_back(entry);
        ++shown;
        say("Mihawk: games folder %s: shortcut to %s", folder.name, targets.back().c_str());
    }
    say("Mihawk: games folders in C:\\Games: %zu (scan %d): %u shortcuts, %u with several programs, %u covered by the "
        "library", folders.size(), scanned, shown, several, covered);
    if (unchosen)
        notice_set(NOTICE_FOLDER, "C:\\Games\\%s holds several programs, so it has no shortcut: a game.json names the "
                   "game's.%s", unchosen, several > 1 ? " Other folders too: see the title's log." : "");
    PwShellLinkSyncResult result{};
    const std::string folder = prefix + "/drive_c/users/Public/Desktop", manifest = prefix + "/.mihawk-desktop-shortcuts";
    const int error = pw_shell_link_sync(folder.c_str(), manifest.c_str(), entries.data(), entries.size(), &result);
    say("Mihawk: desktop shortcuts for %zu library games and MihawkBrowser: written %u, unchanged %u, removed %u, failed %u (%d%s%s)",
        entries.size() - 1, result.written, result.unchanged, result.removed, result.failed, error,
        error ? " at " : "", error && result.first_error_step ? result.first_error_step : "");
}

uint64_t monotonic_ms()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec) * 1000 + uint64_t(now.tv_nsec) / 1000000;
}

int signal_process(int pid, int signal) { return kill(pid, signal) ? errno : 0; }

// PROTON_STOP_TERMINATE=1: the server may end a game whose helpers the kernel would not let the title SIGKILL. Opt-in:
// that ending preceded the console's power-offs (2026-10-03, and a 2026-10-14 run's stop; record removed from the tree).
bool server_terminate_asked()
{
    const char *setting = getenv("PROTON_STOP_TERMINATE");
    return setting && !strcmp(setting, "1");
}

// Only ESRCH proves a helper sent SIGKILL gone (src/pw_hang_breaker.h); until each has, the title is not replaced.
void wait_for_ended_helpers()
{
    for (unsigned second = 0; pw_hang_breaker_gone(&ended, signal_process, monotonic_ms()); ++second) {
        for (unsigned i = 0; i < ended.count && second % 30 == 0; ++i) {
            if (!(ended.waiting & (1u << i))) continue;
            if (!second)
                say("Wine: process %d is still there %llu ms after SIGKILL (kill(pid, 0): errno %d); the title waits for it "
                    "and is not replaced: close the title from the home screen if it stays", ended.pids[i],
                    (unsigned long long)(monotonic_ms() - ended.sent_ms), ended.last[i]);
            else
                say("Wine: still waiting for process %d (%u s)", ended.pids[i], second);
        }
        if (second % 30 == 0 && title_log_fd >= 0) fsync(title_log_fd);
        usleep(1000000);
    }
    for (unsigned i = 0; i < ended.count; ++i)
        if (ended.sent[i] != PW_HANG_SKIPPED)
            say("Wine: helper %d gone %llu ms after SIGKILL", ended.pids[i], (unsigned long long)ended.gone_ms[i]);
    // Whether the console's payload processes came back once the game was gone: the memory helper's census is one.
    pw_memory_census census{};
    const bool read = memory_census && !memory_census(&census);
    unsigned long long frames = 0;
#ifdef PS5_GPU_BROKER
    frames = proton_gpu_broker_flips() - ended_flips;
#endif
    say("Wine: after the kill: census %lld ms old, frames +%llu in %llu ms",
        read ? (long long)(monotonic_ms() - census.taken_ms) : -1ll, frames,
        (unsigned long long)(monotonic_ms() - ended.sent_ms));
    if (title_log_fd >= 0) fsync(title_log_fd);
}

// reason: why the session failed, shown by the launcher (ProtonLaunch::ended); null for an ordinary end.
void return_to_launcher(const char *reason)
{
    // The game's exit (atexit, on a Windows thread) and the title's own stop
    // can both get here: the first one leaves, the other waits to be replaced.
    if (leaving.exchange(true)) for (;;) usleep(100000);
    // A launcher can exit without claiming graphics while headless children
    // remain. Keep their server host alive until every helper has ended.
    // After a SIGKILL ending each helper's monitor still closes its GPU client, which takes the broker's client lock:
    // a wedged broker keeps the title here (pw_ps5_wait_children names the open slots every 30 s).
    if (owner_wait && ending.load(std::memory_order_acquire)) {
        say("Wine: waiting for the ended helpers' services to close");
        if (title_log_fd >= 0) fsync(title_log_fd);
    }
    if(owner_wait)owner_wait();
    // The client's standard output is flushed at its exit, which has just happened: the session's final read.
    if (aurelia_session()) aurelia_poll();
    if (steamcmd_session()) steamcmd_poll();
    log_line(proton_cover_stop("the session ended"));
    log_line(proton_guide_stop("the session ended"));
    bool broker_stopped = true;
#ifdef PS5_GPU_BROKER
    // A program the title ran itself may still be drawing: its broker slot is never closed under it (closing it freed
    // buffers the Unity game's render thread still used, and the title died: fmem-bd-title-2026-10-26-01). The broker
    // stays as it is and ends with the process (docs/TITLE_CLIENT.md, "Stop and end").
    if (owner_broker_running) broker_stopped = !title_client_claimed && proton_gpu_broker_stop();
    owner_broker_running = false;
#endif
    // No presenter may be live when the process is replaced (R4): a game that ends before it
    // presents still has the loading screen.
    // A list's games are the screen until something is chosen, so the loading screen stays up for them.
    if (!(aurelia_session() && aurelia_library_ready)) log_line(proton_loading_stop("the game ended"));
    say("Wine: process completed; returning to launcher");
    if (aurelia_session() && aurelia_library_ready) {
        long choice = 0;
        struct pad browse{};
        while (!choice) {
            pad_poll(&browse);
            const uint32_t pressed = browse.pressed;
            choice = proton_aurelia_browse((pressed & PAD_LEFT) != 0, (pressed & PAD_RIGHT) != 0,
                                           (pressed & PAD_UP) != 0, (pressed & PAD_DOWN) != 0,
                                           (pressed & PAD_CROSS) != 0, (pressed & PAD_CIRCLE) != 0);
            if (!choice) usleep(33000);
        }
        say("Aurelia: the shelf chose %ld", choice);
        if (choice > 0) {
            // A harness timeout leaves stop-game behind while the shelf keeps the title up. The choice is the user
            // asking for that game, not for the test to end: a new game session that found the stale file closed at
            // 0 s (the Pro, 2026-10-22, title-last.log: "stop requested; asking the game to close (0)").
            unlink(PROTON_DATA_ROOT "/stop-game");
            const std::string id = std::to_string(choice);
            const bool installed = access((std::string(PROTON_DATA_ROOT "/games/") + id + "/game.json").c_str(), F_OK) == 0;
            aurelia_next = ProtonGame{};
            if (installed) {
                aurelia_next.id = id;
            } else {
                aurelia_next.id = "aurelia";
                aurelia_next.arguments = "install " + id + " --json";
            }
            aurelia_next_set = true;
        }
        aurelia_library_ready = false;
        log_line(proton_loading_stop(choice > 0 ? "a game was chosen from the shelf" : "the shelf was left"));
    }
    if(owner_result) {
        const int code=session_result>=0 ? session_result : owner_result();
        say("PS5_PROTON_OWNER_BOOTSTRAP %s initial_status=%d graphics_claimed=%d broker_stopped=%d title_client=%d",
            code==0 && (broker_stopped || title_client_claimed) ? "PASS" : "FAIL",code,graphics_claimed.load(),broker_stopped,
            title_client_claimed.load());
    }
    log_line("Wine: process completed; returning to launcher");
    if (fdv_stats) {
        PwWineFdvStats files{};
        fdv_stats(&files);
        say("Wine: open files at the end: live=%u parked=%u parks=%lu unparks=%lu limit hits=%lu failed=%lu%s",
            files.live, files.parked, files.parks, files.unparks, files.limit_hits, files.failures,
            files.disabled ? " (budget off)" : "");
    }
    // No grain is in flight once the sink is stopped; then the port closes,
    // rather than going with the process.
    if (stop_audio && audio_port >= 0) {
        if (!stop_audio(200)) {
            say("Wine: audio stopped, port closed: %d", audio.close(audio_port));
            audio_port = -1;
        } else {
            say("Wine: a grain still held the audio sink; the port goes with the process");
        }
    }
    // The prefix's registry goes to disk before LoadExec replaces the process,
    // whether the game ended or the title stops it (patch 0886).
    if (flush_registry) {
        const uint64_t start = sceKernelGetProcessTime();
        const int status = flush_registry(3000);
        say("Wine: registry %s in %llu ms", status ? "NOT saved" : "saved",
            (unsigned long long)((sceKernelGetProcessTime() - start) / 1000));
    }
    // Last, after every bounded step above: replacing the title while a program's threads still ran powered the
    // console off (2026-10-03), so a helper sent SIGKILL is waited for here, for as long as it takes. Claimed here if no
    // one has: a SIGKILL sent after this check would not be waited for.
    if (ending.exchange(true)) {
        while (!ended_ready.load(std::memory_order_acquire)) usleep(10000);
        wait_for_ended_helpers();
    }
    // Every helper is gone now: a section released in the last second gives its memory back before the title goes.
    if (collect_sections) collect_sections();
    notices_post();
    // stderr is drained by the title's klog thread. Preserve the last Wine
    // messages before LoadExec replaces this process.
    usleep(500000);
    if (!reason && ended_ready.load(std::memory_order_acquire) && !ended_reason.empty()) reason = ended_reason.c_str();
    // The session came back through the title: the launcher is told why only when it failed.
    remove(proton_title_path(PROTON_SESSION_MARKER).c_str());
    if (aurelia_next_set) {
        aurelia_next_set = false;
        say("Wine: the session hands over to %s", aurelia_next.id.c_str());
        proton_restart(&aurelia_next, proton_launch().cycle + 1, "aurelia chosen", reason);
    }
    proton_restart(nullptr, proton_launch().cycle + 1, "wine-ended", reason);
    catchReturnFromMain(1);
}

void back_to_launcher() { return_to_launcher(nullptr); }

// A session that cannot go on: the reason is logged and carried to the launcher, which shows it.
__attribute__((format(printf, 1, 2))) void fail(const char *format, ...)
{
    char reason[512];
    va_list args;
    va_start(args, format);
    vsnprintf(reason, sizeof(reason), format, args);
    va_end(args);
    say("%s", reason);
    return_to_launcher(reason);
}

// Suspend and resume (src/pw_suspend.h): this loop ticks the clock; the flight recorder's thread reads the hold.
static PwSuspendClock title_suspend;
static std::atomic<uint64_t> suspend_resumed_ms{0};
static std::atomic<uint32_t> suspend_away{0};
static int suspend_hold(uint64_t now_ms)
{
    PwSuspendClock seen{};
    seen.away = suspend_away.load(std::memory_order_relaxed);
    seen.resumed_ms = suspend_resumed_ms.load(std::memory_order_relaxed);
    return pw_suspend_hold(&seen, now_ms);
}
// The system's word on the app (sceSystemServiceGetStatus, its first fields; the rest, reserved, is kept room for):
// 1 while it runs in the background, 0 in the foreground, -1 when it cannot be read. The system's overlay over a running
// app is only reported (overlaid): notifications raise it too, and a game is not paused for one. The system's events
// are taken and named, so a resume event shows in the log next to the clock's own evidence.
struct SystemServiceStatus { int32_t events; uint8_t overlaid, background, cpu7, streaming, vr_out, reserved[251]; };
extern "C" int sceSystemServiceGetStatus(SystemServiceStatus *status);
extern "C" int sceSystemServiceReceiveEvent(void *event);
static int system_away(bool *overlaid)
{
    SystemServiceStatus status{};
    if (sceSystemServiceGetStatus(&status)) return -1;
    *overlaid = status.overlaid != 0;
    static uint8_t event[16384];   // an event's type, then up to 8 KiB of its data
    for (int i = 0; i < status.events && i < 8; ++i) {
        if (sceSystemServiceReceiveEvent(event)) break;
        int32_t type = 0;
        memcpy(&type, event, sizeof(type));
        say("system: event %#x", type);
    }
    return status.background ? 1 : 0;
}

// SIGKILL to every helper that runs a Windows program, straight from the title (src/pw_hang_breaker.h): nothing in a
// helper or in the server has to answer, and the memory helper is not among them. Once per session, from any thread:
// what it calls takes only child_lock (pw_ps5_end_spawns, pw_ps5_helpers), never held across a wait.
bool end_helpers(const char *why)
{
    if (!list_helpers || ending.exchange(true)) return false;
    if (end_spawns) end_spawns();
    int pids[PW_HANG_MAX_HELPERS];
    const unsigned count = list_helpers(pids, PW_HANG_MAX_HELPERS, nullptr);
#ifdef PS5_GPU_BROKER
    ended_flips = proton_gpu_broker_flips();
#endif
    say("Wine: %s: ending %u helpers by SIGKILL, not through the server", why, count);
    ended_reason = std::string("the title ended the game (") + why + ")";
    // Each is gone from the synchronization device first, so none is cut off halfway through a change to the objects
    // every process shares (src/pw_ntsync.h); a lock a stuck helper keeps does not hold this up past 200 ms.
    if (device_gone)
        for (unsigned i = 0; i < count; ++i) {
            const int status = device_gone(pids[i], 0, 200);
            if (status < 0 && status != -ESRCH) say("Wine: helper %d: not taken out of the sync device (%d)", pids[i], status);
        }
    pw_hang_breaker_end(pids, count, getpid(), signal_process, monotonic_ms(), &ended);
    for (unsigned i = 0; i < ended.count; ++i)
        say("Wine: helper %d: SIGKILL %s (errno %d)", ended.pids[i],
            ended.sent[i] == PW_HANG_SKIPPED ? "not sent: not a helper's pid" : !ended.sent[i] ? "sent" :
            ended.sent[i] == ESRCH ? "found it gone already" : "refused", ended.sent[i] == PW_HANG_SKIPPED ? 0 : ended.sent[i]);
    if (title_log_fd >= 0) fsync(title_log_fd);
    ended_ready.store(true, std::memory_order_release);
    return true;
}

// The one way a game is ended from here: its helpers by SIGKILL, then back to the launcher, which waits for each to be
// gone. Only when the kernel refused a SIGKILL, and only with PROTON_STOP_TERMINATE=1, does the server end it as well.
void end_game(const char *why, bool server_fallback)
{
    if (!end_helpers(why)) return;
    bool refused = false;
    for (unsigned i = 0; i < ended.count; ++i) refused |= ended.sent[i] == EPERM;
    if (refused && server_fallback && server_terminate_asked() && terminate_game)
        say("Wine: SIGKILL was refused; ending the game through the server as well (%d)", terminate_game());
    // The client's standard output is flushed when its process exits, which is now: one last read before the session
    // ends (the loop's polls saw nothing while it was still buffered).
    if (aurelia_session()) aurelia_poll();
    if (steamcmd_session()) steamcmd_poll();
    back_to_launcher();
}

void output(const char *text, size_t length)
{
    // Each Wine thread assembles its own lines: no lock is shared between
    // them, so one thread's output never waits for another's, and a thread
    // that ends mid-line holds nothing up. The log is opened O_APPEND, so
    // every chunk lands whole at its end.
    static thread_local char line[16384];
    static thread_local size_t used;
    // Keep exception diagnostics even if the process dies before klog drains.
    if (log_fd >= 0) {
        size_t offset = 0;
        while (offset < length) {
            const ssize_t written = write(log_fd, text + offset, length - offset);
            if (written > 0) offset += written;
            else if (written < 0 && errno == EINTR) continue;
            else break;
        }
    }
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\n' || used == sizeof(line) - 1) {
            line[used] = 0;
            if (aurelia_session()) aurelia_line(line);
            say("Wine: %s", line);
            used = 0;
        }
        if (text[i] != '\n') line[used++] = text[i];
    }
}

int set_env(const char *key, const char *value) { return setenv(key, value, 1); }

// Names through the console's resolver (libSceNet, as the ProsperoLight title asks it: five seconds, two retries),
// for every Windows process: Winsock asks ntdll, which asks this from the title's own client and over the control
// socket from a helper (pw_wine_process.h). Only the title loads libSceNet: a helper that did took the title down.
} // namespace
extern "C" {
int sceNetPoolCreate(const char *, int, int);
int sceNetPoolDestroy(int);
int sceNetGetDnsInfo(void *, int);
int sceNetGetDns6Info(void *, int);
int sceNetResolverCreate(const char *, int, int);
int sceNetResolverDestroy(int);
int sceNetResolverStartNtoa(int, const char *, uint32_t *, int, int, int);
}
namespace {
int title_resolve(const char *name, unsigned char address[4])
{
    uint32_t found = 0;
    const int pool = sceNetPoolCreate("wine-dns", 0x4000, 0);
    if (pool < 0) return EAI_AGAIN;
    const int resolver = sceNetResolverCreate("wine-dns", pool, 0);
    if (resolver < 0) { sceNetPoolDestroy(pool); return EAI_AGAIN; }
    const int result = sceNetResolverStartNtoa(resolver, name, &found, 5000000, 2, 0);
    sceNetResolverDestroy(resolver);
    sceNetPoolDestroy(pool);
    if (result < 0 || !found) return EAI_NONAME;
    memcpy(address, &found, sizeof(found));
    return 0;
}
int title_dns_config(struct pw_dns_config *config)
{
    *config = {};
    const int pool = sceNetPoolCreate("wine-dns-config", 0x4000, 0);
    if (pool < 0) return EAI_AGAIN;
    unsigned char ipv4[8] = {}, ipv6[32] = {};
    const int n4 = sceNetGetDnsInfo(ipv4, 0), n6 = sceNetGetDns6Info(ipv6, 0);
    sceNetPoolDestroy(pool);
    if (n4 < 0 || n4 > 2 || n6 < 0 || n6 > 2) return EAI_FAIL;
    for (int i = 0; i < n4; ++i) {
        auto &server = config->servers[config->count++];
        server.family = 4; memcpy(server.address, ipv4 + 4*i, 4);
    }
    for (int i = 0; i < n6; ++i) {
        auto &server = config->servers[config->count++];
        server.family = 6; memcpy(server.address, ipv6 + 16*i, 16);
    }
    return 0;
}
struct ThreadStart { void (*entry)(void *); void *arg; };
void *thread_entry(void *opaque)
{
    const ThreadStart start = *static_cast<ThreadStart *>(opaque);
    delete static_cast<ThreadStart *>(opaque);
    say("Wine: entering __wine_main on 16 MiB stack");
    start.entry(start.arg);
    back_to_launcher();
    return nullptr;
}
int start_thread(void (*entry)(void *), void *arg, size_t bytes)
{
    auto *start = new ThreadStart{entry, arg};
    pthread_attr_t attr;
    pthread_t thread;
    int status = pthread_attr_init(&attr);
    if (!status) {
        status = pthread_attr_setstacksize(&attr, bytes);
        if (!status) status = pthread_create(&thread, &attr, thread_entry, start);
        pthread_attr_destroy(&attr);
    }
    if (status) delete start;
    else pthread_detach(thread);
    return status;
}
template<class T> T symbol(const char *name)
{
    return reinterpret_cast<T>(const_cast<void *>(pw_prx_lookup(wine.descriptor, name)));
}

int start_owner_graphics(int server_fd,const struct pw_child_bootstrap *boot)
{
    // ponytail: one graphics client per native title; relaunch the title for
    // another game, as the normal launcher does. Wine globals are never reused.
    if(graphics_claimed.exchange(true)) { errno=EBUSY;return -1; }
    const int socket=dup(server_fd);
    if(socket<0) { graphics_claimed=false;return -1; }
    char number[24];snprintf(number,sizeof(number),"%d",socket);
    auto set_cwd=symbol<int (*)(const char *)>("pw_cwd_set");
    if(!set_cwd || set_cwd(boot->cwd) || setenv("WINESERVERSOCKET",number,1) ||
       setenv("WINE_PS5_GRAPHICS_OWNER","1",1)) { close(socket);graphics_claimed=false;return -1; }
    const char *const argv[]={"wine","graphics-child.exe",nullptr};
    const PwWineStartConfig config={.argc=2,.argv=argv,.stack_bytes=16u<<20};
    const PwWineStartOps ops={.start_thread=start_thread};
    const int status=pw_wine_start_run(&wine,&config,&ops);
    if(status) { close(socket);graphics_claimed=false;errno=EIO;return -1; }
    say("Wine: graphics Windows child uses the original native title");
    return 0;
}

#ifdef PS5_GPU_BROKER
/* The one Windows program the title runs itself beside the broker (docs/TITLE_CLIENT.md, WINE_PS5_TITLE_PROCESS): its
 * code and memory come from the title's partition, not the system pool. Its graphics are a helper's: a broker client in
 * this process (graphics patch 0031, PROTON_GPU_IN_OWNER), the helpers' win32u and Vulkan builds (WINE_PS5_TITLE_CLIENT:
 * wine/ps5/pw_wine_dl.c, patch 2301), its windows attached here as a helper's are. Its end ends the session, as a
 * title's program's does (the first version, docs/TITLE_CLIENT.md). */
int start_title_client(int server_fd,const struct pw_child_bootstrap *boot)
{
    if(title_client_claimed.exchange(true)) { errno=EBUSY;return -1; }
    int cookie=0;
    const int gpu=proton_gpu_broker_open_in_owner(&cookie);
    const int socket=gpu>=0 ? dup(server_fd) : -1;
    char server[24],broker[24];
    snprintf(server,sizeof(server),"%d",socket);snprintf(broker,sizeof(broker),"%d",gpu);
    auto set_cwd=symbol<int (*)(const char *)>("pw_cwd_set");
    if(socket<0 || !set_cwd || set_cwd(boot->cwd) || setenv("WINESERVERSOCKET",server,1) ||
       setenv("WINE_PS5_GPU_FD",broker,1) || setenv("WINE_PS5_GPU_BROKER","1",1) || setenv("PROTON_GPU_IN_OWNER","1",1) ||
       setenv("WINE_PS5_TITLE_CLIENT","1",1) ||
       /* What any client in the title needs (patch 0927): the in-process server's callbacks bound before the socket
        * is attached, and the routing request taken out of the program's environment so its children go to helpers. */
       setenv("WINE_PS5_GRAPHICS_OWNER","1",1)) {
        const int error=errno ? errno : EIO;
        if(socket>=0)close(socket);
        if(gpu>=0) { close(gpu);proton_gpu_broker_close(cookie,0); }
        title_client_claimed=false;errno=error;return -1;
    }
    const char *const argv[]={"wine","title-client.exe",nullptr};
    const PwWineStartConfig config={.argc=2,.argv=argv,.stack_bytes=16u<<20};
    const PwWineStartOps ops={.start_thread=start_thread};
    if(pw_wine_start_run(&wine,&config,&ops)) { errno=EIO;return -1; }   // claimed for good: the environment is set
    say("Wine: %s runs in the title beside the broker",boot->image[0] ? boot->image : "a program");
    return 0;
}
#endif

/* ntdll's runtime counters (wine/ps5/pw_wine_perf.h), printed as the difference
 * between two snapshots from this thread, never from a game thread. */
struct PerfReport {
    void (*snapshot)(PwWinePerf *) = nullptr;
    uint32_t (*percentile)(const PwWinePerfFrames *, const PwWinePerfFrames *, unsigned) = nullptr;
    unsigned int (*memory)(uint64_t *, unsigned int) = nullptr;
    unsigned int (*virtual_memory)(uint64_t *, unsigned int) = nullptr;
    PwWinePerf before{}, now{};
    uint64_t memory_before[12]{}, memory_now[12]{}, vm_before[9]{}, vm_now[9]{};
    uint64_t wine_started = 0;     // tick when Wine's thread was started
    bool first_reported[PW_PERF_APIS]{};
    bool primed = false;

    double ms(uint64_t ticks) const { return now.tsc_hz ? ticks * 1000.0 / now.tsc_hz : 0; }
    void report()
    {
        if (!snapshot || !percentile) return;
        snapshot(&now);
        const bool have_memory = memory && memory(memory_now, 12) >= 12 &&
            virtual_memory && virtual_memory(vm_now, 9) >= 9;
        // Time to the first frame of each API: shader and pipeline caches show here.
        static const char *const names[] = {"opengl", "vulkan"};
        for (unsigned api = 0; api < PW_PERF_APIS; ++api)
            if (!first_reported[api] && now.frames[api].first && wine_started && now.tsc_hz) {
                say("perf: first %s frame %.0f ms after Wine started", names[api],
                    ms(now.frames[api].first - wine_started));
                first_reported[api] = true;
            }
        if (primed && now.tsc_hz && now.now > before.now) {
            const double seconds = (now.now - before.now) / double(now.tsc_hz);
            static const char *const apis[] = {"opengl", "vulkan"};
            for (unsigned api = 0; api < PW_PERF_APIS; ++api) {
                const PwWinePerfFrames &a = now.frames[api], &b = before.frames[api];
                const uint64_t frames = a.frames - b.frames;
                if (!frames) continue;
                say("perf: %s frames=%llu fps=%.1f p50=%.2f p95=%.2f p99=%.2f max=%.2f ms present_avg=%.3f present_max_all=%.3f ms",
                    apis[api], (unsigned long long)frames, frames / seconds,
                    percentile(&a, &b, 50) / 1000.0, percentile(&a, &b, 95) / 1000.0,
                    percentile(&a, &b, 99) / 1000.0, percentile(&a, &b, 100) / 1000.0,
                    ms(a.call_ticks - b.call_ticks) / frames, ms(a.call_max));
            }
            const uint64_t calls = now.server_calls - before.server_calls;
            const uint64_t selects = now.selects - before.selects;
            const uint64_t blocked = now.selects_blocked - before.selects_blocked;
            const uint64_t waits = now.waits - before.waits;
            say("perf: server calls=%llu per_s=%.0f avg_us=%.1f selects=%llu blocked=%llu block_avg_us=%.1f "
                "waits=%llu handles_avg=%.2f all=%llu polled=%llu delays=%llu yields=%llu",
                (unsigned long long)calls, calls / seconds,
                calls ? ms(now.server_ticks - before.server_ticks) * 1000.0 / calls : 0.0,
                (unsigned long long)selects, (unsigned long long)blocked,
                blocked ? ms(now.select_block_ticks - before.select_block_ticks) * 1000.0 / blocked : 0.0,
                (unsigned long long)waits,
                waits ? double(now.wait_handles - before.wait_handles) / waits : 0.0,
                (unsigned long long)(now.waits_all - before.waits_all),
                (unsigned long long)(now.waits_polled - before.waits_polled),
                (unsigned long long)(now.delays - before.delays),
                (unsigned long long)(now.delay_yields - before.delay_yields));
            // The five requests that took the most time in this interval.
            unsigned top[5];
            unsigned found = 0;
            for (unsigned code = 0; code < PW_PERF_REQUESTS; ++code) {
                const uint64_t ticks = now.request_ticks[code] - before.request_ticks[code];
                if (!ticks) continue;
                unsigned at = found < 5 ? found++ : 5;
                while (at > 0 && ticks > now.request_ticks[top[at - 1]] - before.request_ticks[top[at - 1]]) {
                    if (at < 5) top[at] = top[at - 1];
                    --at;
                }
                if (at < 5) top[at] = code;
            }
            char line[512];
            int used = snprintf(line, sizeof(line), "perf: top");
            for (unsigned i = 0; i < found && used > 0 && used < (int)sizeof(line); ++i) {
                const unsigned code = top[i];
                const uint64_t count = now.request_calls[code] - before.request_calls[code];
                used += snprintf(line + used, sizeof(line) - used, " %s=%llu/%.1fus",
                    now.request_names[code] ? now.request_names[code] : "?", (unsigned long long)count,
                    count ? ms(now.request_ticks[code] - before.request_ticks[code]) * 1000.0 / count : 0.0);
            }
            if (found) say("%s", line);
            // Memory: direct memory and the Unix-side heap now, allocation
            // traffic and contention on the heap's mutex in this interval,
            // and the host VM calls (patch 0540's counters).
            if (have_memory) {
                say("perf: memory dmem_mib=%.1f dmem_peak_mib=%.1f heap_live_mib=%.1f heap_mapped_mib=%.1f "
                    "allocs=%llu frees=%llu contended=%llu mmap=%llu munmap=%llu mprotect=%llu mprotect_skipped=%llu faults=%llu",
                    memory_now[0] / 1048576.0, memory_now[1] / 1048576.0, memory_now[8] / 1048576.0,
                    memory_now[4] / 1048576.0,
                    (unsigned long long)(memory_now[9] - memory_before[9]),
                    (unsigned long long)(memory_now[10] - memory_before[10]),
                    (unsigned long long)(memory_now[11] - memory_before[11]),
                    (unsigned long long)(vm_now[0] - vm_before[0]), (unsigned long long)(vm_now[1] - vm_before[1]),
                    (unsigned long long)(vm_now[2] - vm_before[2]), (unsigned long long)(vm_now[4] - vm_before[4]),
                    (unsigned long long)(vm_now[5] - vm_before[5]));
                // The direct-memory map, absolute: runs (kernel mappings), refused calls, what is backed below
                // 4 GiB, and the VM call totals, for a process that ran out of memory (a JVM's malloc failed).
                struct ps5_memory_stats system;
                if (ps5_memory_query(&system))
                    say("perf: system direct memory %llu MiB of %llu free in %u ranges, largest %llu MiB; flexible %llu MiB free",
                        (unsigned long long)(system.free_bytes >> 20), (unsigned long long)(system.total_bytes >> 20),
                        system.free_ranges, (unsigned long long)(system.largest_bytes >> 20),
                        (unsigned long long)(system.flexible_bytes >> 20));
                struct PwFdCensus descriptors;
                pw_fd_census(&descriptors);
                say("perf: descriptors open=%u of %u sockets=%u fifos=%u files=%u dirs=%u devices=%u other=%u",
                    descriptors.open, descriptors.limit, descriptors.sockets, descriptors.fifos, descriptors.files,
                    descriptors.dirs, descriptors.devices, descriptors.other);
                say("perf: dmem runs=%llu refused=%llu low_mib=%.1f vm_total mmap=%llu munmap=%llu mprotect=%llu v3=%llu skipped=%llu faults=%llu",
                    (unsigned long long)memory_now[2], (unsigned long long)memory_now[3], memory_now[6] / 1048576.0,
                    (unsigned long long)vm_now[0], (unsigned long long)vm_now[1], (unsigned long long)vm_now[2],
                    (unsigned long long)vm_now[3], (unsigned long long)vm_now[4], (unsigned long long)vm_now[5]);
            }
        }
        if (have_memory) {
            std::copy(std::begin(memory_now), std::end(memory_now), memory_before);
            std::copy(std::begin(vm_now), std::end(vm_now), vm_before);
        }
        before = now;
        primed = true;
    }
};
PerfReport perf;

/* A one-shot pad script (src/pw_pad_script.h) for a run nobody holds a controller for: read and
 * removed at the game's start, applied over the real pad every tick, its captures and stop fired as
 * their times pass. Every change of what it holds is logged, so a run says what it pressed when. */
struct PadScript {
    PwPadScript script{};
    bool active = false;
    uint64_t started_ms = 0, origin_ms = 0;
    unsigned last_ms = 0, captures = 0, capture_index = 0;
    uint64_t capture_asked_ms = 0;
    // A picture asked of the GPU broker (the launcher layout), written straight to its numbered name.
    std::string broker_capture;
#ifdef PS5_GPU_BROKER
    // A request the broker declined because its lock was busy (graphics patch 0012 takes it with trylock): asked
    // again at each collect for up to 5 s; dropped, four of six gate captures failed (v3-jit-gatecases-2026-10-14).
    std::string broker_retry;
    uint64_t broker_retry_until = 0;
#endif
    uint32_t last_buttons = 0;
    bool last_axes[PW_PAD_SCRIPT_AXES]{};

    void load()
    {
        const char *path = PROTON_DATA_ROOT "/pad-script.txt";
        FILE *file = fopen(path, "rb");
        if (!file) return;
        std::string text;
        char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, read);
        fclose(file);
        unlink(path);   // one-shot, as wine-run.txt: it never replays on the next launch
        const unsigned bad = pw_pad_script_parse(text.c_str(), &script);
        if (bad) {
            say("Wine: pad script refused at line %u; no scripted input", bad);
            return;
        }
        active = script.count > 0;
        started_ms = monotonic_ms();
        origin_ms = script.from_frame ? 0 : started_ms;
        say("Wine: pad script: %u steps, times from %s", script.count, script.from_frame ? "the first frame" : "the start");
    }

    /* Over the pad the console read: the script's buttons are added, its axes replace the stick's. */
    void apply(struct pad &pad)
    {
        if (!active) return;
        const uint64_t now = monotonic_ms();
        if (!origin_ms) {
            static PwWinePerf frames;
            bool presented = false;
            if (perf.snapshot) {
                perf.snapshot(&frames);
                for (unsigned api = 0; api < PW_PERF_APIS; ++api) presented |= frames.frames[api].frames > 0;
            }
#ifdef PS5_GPU_BROKER
            // In the launcher layout the program presents from a helper, through the title's GPU broker: its first
            // frame is the broker's first shown (a 2026-10-06 run waited for a frame never counted; record removed from the tree).
            presented |= owner_broker_running && proton_gpu_broker_flips() > 0;
#endif
            if (!presented) return;
            origin_ms = now;
            say("Wine: pad script: first frame %.2f s after the start; script time begins",
                (now - started_ms) / 1000.0);
        }
        const unsigned ms = unsigned(now - origin_ms);
        PwPadScriptState state;
        pw_pad_script_state(&script, ms, &state);
        pad.connected = true;
        pad.pressed |= state.buttons & ~last_buttons & ~pad.held;
        pad.held |= state.buttons;
        float *const floats[PW_PAD_SCRIPT_AXES] = {&pad.left_x, &pad.left_y, &pad.right_x, &pad.right_y, &pad.l2, &pad.r2};
        bool axes_changed = false;
        for (int axis = 0; axis < PW_PAD_SCRIPT_AXES; ++axis) {
            axes_changed |= state.axis_set[axis] != last_axes[axis];
            last_axes[axis] = state.axis_set[axis];
            if (!state.axis_set[axis]) continue;
            *floats[axis] = state.axis[axis];
            pad.raw_axes[axis] = pw_pad_script_raw(axis, state.axis[axis]);
        }
        if (state.touching) {
            pad.touch_count = 1;pad.touch_id = 0;pad.touch_x = state.touch_x;pad.touch_y = state.touch_y;
        }
        if (state.buttons != last_buttons || axes_changed)
            say("Wine: pad script %.2f s: buttons=%#x lx=%.2f ly=%.2f rx=%.2f ry=%.2f", ms / 1000.0, state.buttons,
                pad.left_x, pad.left_y, pad.right_x, pad.right_y);
        last_buttons = state.buttons;
        const PwPadScriptStep *events[8];
        const unsigned count = pw_pad_script_events(&script, last_ms, ms, events, 8);
        for (unsigned i = 0; i < count; ++i) {
            if (events[i]->kind == PW_PAD_SCRIPT_STOP) {
                const int fd = open(PROTON_DATA_ROOT "/stop-game", O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd >= 0) close(fd);
                say("Wine: pad script %.2f s: stop", events[i]->start_ms / 1000.0);
            } else {
                capture(now, events[i]->start_ms);
            }
        }
        last_ms = ms;
        collect(now);
    }

    /* win32u (patches 0780, 0830) saves the next presented frame to WINE_PS5_CAPTURE when its
     * ".request" file exists; each picture is then renamed to <name>-NN.ppm so the next one cannot
     * overwrite it. */
    void capture(uint64_t now, unsigned at_ms)
    {
        const char *path = getenv("WINE_PS5_CAPTURE");
        if (!path) return;
        if (capture_asked_ms) collect(now + 60000);   // the previous one never came: say so first
#ifdef PS5_GPU_BROKER
        if (owner_broker_running) {
            // The program's frames reach the screen through the broker, not this process's win32u: the broker
            // writes the next one it shows, under the name the request would have been renamed to.
            std::string name(path);
            const size_t dot = name.rfind(".ppm");
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "-%02u.ppm", captures + 1);
            name = (dot == std::string::npos ? name : name.substr(0, dot)) + suffix;
            unlink(name.c_str());
            const bool asked = proton_gpu_broker_capture_next(name.c_str());
            capture_index = ++captures;
            capture_asked_ms = asked ? now : 0;
            broker_capture = asked ? name : std::string();
            broker_retry = asked ? std::string() : name;
            broker_retry_until = now + 5000;
            say("Wine: pad script %.2f s: capture %02u asked of the GPU broker (%d)", at_ms / 1000.0, capture_index, asked);
            return;
        }
#endif
        unlink(path);
        const std::string request = std::string(path) + ".request";
        const int fd = open(request.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) close(fd);
        capture_index = ++captures;
        capture_asked_ms = now;
        say("Wine: pad script %.2f s: capture %02u asked (%d)", at_ms / 1000.0, capture_index, fd >= 0);
    }

    void collect(uint64_t now)
    {
        const char *path = getenv("WINE_PS5_CAPTURE");
#ifdef PS5_GPU_BROKER
        if (!broker_retry.empty()) {
            if (proton_gpu_broker_capture_next(broker_retry.c_str())) {
                say("Wine: capture %02u asked of the GPU broker again (1)", capture_index);
                capture_asked_ms = now;
                broker_capture = broker_retry;
                broker_retry.clear();
            } else if (now >= broker_retry_until) {
                say("Wine: capture %02u: the GPU broker stayed busy for 5 s", capture_index);
                broker_retry.clear();
            }
            return;
        }
#endif
        if (!capture_asked_ms || !path) return;
        struct stat status{};
        if (!broker_capture.empty()) {
            // Written in one pass under the broker's display lock; a second's wait covers it, as below.
            if (stat(broker_capture.c_str(), &status) == 0 && status.st_size > 0 && now - capture_asked_ms >= 1000) {
                say("Wine: capture %02u saved as %s by the GPU broker", capture_index, broker_capture.c_str());
                capture_asked_ms = 0;
                broker_capture.clear();
            } else if (now - capture_asked_ms >= 10000) {
                say("Wine: capture %02u: the GPU broker showed no frame in 10 s", capture_index);
                capture_asked_ms = 0;
                broker_capture.clear();
            }
            return;
        }
        // Saved in one pass by the presenting thread; a second's wait covers its write.
        if (stat(path, &status) == 0 && status.st_size > 0 && now - capture_asked_ms >= 1000) {
            std::string name(path);
            const size_t dot = name.rfind(".ppm");
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "-%02u.ppm", capture_index);
            name = (dot == std::string::npos ? name : name.substr(0, dot)) + suffix;
            say("Wine: capture %02u saved as %s (%d)", capture_index, name.c_str(), rename(path, name.c_str()));
            capture_asked_ms = 0;
        } else if (now - capture_asked_ms >= 10000) {
            say("Wine: capture %02u: no frame presented in 10 s", capture_index);
            unlink((std::string(path) + ".request").c_str());
            capture_asked_ms = 0;
        }
    }
};
PadScript pad_script;
// The game's presenter (Vulkan, patch 0460, or OpenGL, 0820) asks for the display before it
// opens it: the loading screen gives it up first.
// A line in the Wine log as well as klog: the log file is written as it goes, and klog can lose a
// process's last lines when the title replaces itself.
void log_line(const std::string &line)
{
    if (line.empty() || log_fd < 0) return;
    const std::string text = "title: " + line + "\n";
    (void)!write(log_fd, text.data(), text.size());
}
int release_display(void *)
{
    // The SteamCMD session has no game presenter: its console window must not take the setup page's display.
    if (!steamcmd_session()) log_line(proton_loading_stop("the game's presenter takes the display"));
    say("Wine: display handed to the game's presenter");
    return 0;
}
int play_audio(void *, const int16_t *frames)
{
    static bool reported_signal, reported_error;
    const int result = audio.output(audio_port, frames);
    if (result < 0 && !reported_error) {
        say("Wine: AudioOut submission failed: %d", result);
        reported_error = true;
    }
    if (result >= 0 && !reported_signal) {
        for (unsigned i = 0; i < PW_AUDIO_PS5_GRAIN * PW_AUDIO_PS5_CHANNELS; ++i) {
            if (!frames[i]) continue;
            say("Wine: non-silent stereo audio submitted at %d Hz", PW_AUDIO_PS5_RATE);
            reported_signal = true;
            break;
        }
    }
    // AudioOut returns the accepted frame count on success; Wine's sink uses 0.
    return result < 0 ? result : 0;
}

// Settings > Logging (src/pw_detailed_logs.h): this session's folder, with this process's title-early.log (its lines
// before the session; the next title start empties it) and the kernel log. The rest is copied at the next title start
// (ps5/src/main.cpp). The kernel log comes only from klogsrv, when the PC side loaded it: it serves every client at
// once and opens /dev/klog only while one is connected, so this takes nothing from the PC's client. Nothing here opens
// /dev/klog.
void detailed_logs_begin()
{
    char folder[PW_DETAILED_LOGS_PATH] = "";
    unsigned removed = 0, copied = 0, left_out = 0;
    const int status = pw_detailed_logs_begin(PW_DETAILED_LOGS_ROOT, PW_DETAILED_LOGS_KEEP, time(nullptr), folder,
                                              sizeof(folder), &removed);
    const std::string early = proton_title_path("/app0/title-early.log");
    const char *sources[] = {early.c_str()};
    const int filled = folder[0] ? pw_detailed_logs_fill(folder, sources, 1, &copied, &left_out) : -1;
    say("Logging: on; folder %s status=%d removed=%u title-early=%d", folder[0] ? folder : "(none)", status, removed,
        filled);
    if (!folder[0]) return;
    const int klog = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(3232);   // klogsrv's port (ps5-payload-dev/klogsrv)
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (klog < 0 || connect(klog, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
        say("Logging: no kernel log: klogsrv is not loaded (errno %d)", errno);
        if (klog >= 0) close(klog);
        return;
    }
    const int file = open((std::string(folder) + "/klog.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (file < 0) {
        say("Logging: no kernel log: klog.txt could not be made (errno %d)", errno);
        close(klog);
        return;
    }
    std::thread([klog, file] {
        unsigned long long written = 0;
        const int error = pw_detailed_logs_stream(klog, file, PW_DETAILED_LOGS_KLOG_BYTES, &written);
        close(klog);
        close(file);
        say("Logging: the kernel log stopped after %llu bytes (status %d)", written, error);
    }).detach();
    say("Logging: the kernel log comes from klogsrv on the loopback");
}
}

/* A test run can hold N MiB of the title's direct pool under the load, to measure whether the in-title memory model
 * has the headroom a program such as the Steam client needs (tools/run-console.py with PROTON_DMEM_TITLE_TEST=<MiB>
 * in the profile's environment). The pool stays held until the title ends; the pattern is verified once. */
static void proton_dmem_title_test(long mib)
{
    const size_t bytes = ((size_t)mib << 20) & ~(size_t)(PW_DMEM_SHARE_PAGE - 1);
    int64_t offset = -1;
    void *at = nullptr;

    if (mib > 0 && !pw_dmem_share_alloc(bytes, getpid(), 0, &offset)) {
        const int status = sceKernelMapDirectMemory(&at, bytes, PW_DMEM_SHARE_PROT, 0, offset, PW_DMEM_SHARE_PAGE);
        if (!status && at) {
            volatile unsigned char *p = static_cast<volatile unsigned char *>(at);
            size_t bad = 0;
            for (size_t i = 0; i < bytes; i += 4096) p[i] = static_cast<unsigned char>(i >> 12);
            for (size_t i = 0; i < bytes; i += 4096) if (p[i] != static_cast<unsigned char>(i >> 12)) ++bad;
            say("dmem-title: held %ld MiB at %p touched_bad=%zu", mib, at, bad);
            return;
        }
        say("dmem-title: map %ld MiB failed: %#x", mib, status);
        (void)pw_dmem_share_free(offset, bytes);
        return;
    }
    say("dmem-title: alloc %ld MiB failed: %s", mib, strerror(errno));
}

void proton_run_wine()
{
    const auto &launch = proton_launch();
    // The Steam runtime's setup page owns the display from the first moment: the runtime's own display init opens
    // VideoOut within 0.1 s, before the SteamCMD even starts, and used to close the page.
    if (steamcmd_session()) proton_loading_keep_display(true);
    // A test run can measure the scheduler instead of starting a game.
    for (const std::string &entry : launch.env)
        if (entry.rfind("PROTON_DMEM_TITLE_TEST=", 0) == 0) { proton_dmem_title_test(atol(entry.c_str() + 23)); break; }
    if (std::find(launch.env.begin(), launch.env.end(), "PROTON_LIMITS_PROBE=1") != launch.env.end()) {
        proton_limits_probe(PROTON_DATA_ROOT "/limits-probe");
        back_to_launcher();
    }
    if (std::find(launch.env.begin(), launch.env.end(), "PROTON_SCHED_PROBE=1") != launch.env.end()) {
        proton_sched_probe();
        back_to_launcher();
    }
    // The last sessions' logs are kept (src/pw_log_sessions.h): this session's helpers write into sessions/0, and the
    // previous session's title and Wine logs join its helpers' in sessions/1 before new ones are begun.
    unsigned session_removed = 0;
    const int session_status = pw_log_sessions_rotate(PW_LOG_SESSIONS_ROOT, PW_LOG_SESSIONS_KEEP, &session_removed);
    const int kept_title = session_status ? -1 :
        rename(PROTON_DATA_ROOT "/logs/title-last.log", PW_LOG_SESSIONS_ROOT "/1/title-last.log") ? errno : 0;
    const int kept_wine = session_status ? -1 :
        rename(PROTON_DATA_ROOT "/logs/wine-last.log", PW_LOG_SESSIONS_ROOT "/1/wine-last.log") ? errno : 0;
    // The GPU broker empties its results file only when it starts: a run whose broker never started was read with the
    // previous run's (a 2026-10-14 run fetched another game's numbers; record removed from the tree).
    const int broker_results = unlink((std::string(proton_app_root()) + "/gpu-broker-results.txt").c_str()) ? errno : 0;
    say("Aurelia: capture=%s path=%s stdout=%s", aurelia_session() ? "on" : "off", launch.path.c_str(),
        getenv("WINE_PS5_STDOUT") ? getenv("WINE_PS5_STDOUT") : "(unset, the fallback)");
    log_fd = open(PROTON_DATA_ROOT "/logs/wine-last.log", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    // Every say() line also lands on /data, flushed every 2 s by the loop below: a run that freezes the console
    // keeps the title's last seconds (a 2026-10-10 launcher run kept nothing; record removed from the tree).
    title_log_fd = open(PROTON_DATA_ROOT "/logs/title-last.log", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (title_log_fd >= 0) platform_say_file(title_log_fd);
    // The contract with tools/run-console.py, which fails a run whose status is not 0.
    say("Wine: session logs status=%d removed=%u title=%d wine=%d broker-results=%d", session_status, session_removed,
        kept_title, kept_wine, broker_results);
    // Which build this session is, at the top of its log: the title, the runtime it pins and the one installed. A runtime
    // that is not this build's starts no helper: every start comes here (a tile, a shortcut, profile=, a restart), and
    // the launcher it returns to opens the setup's install (src/pw_runtime_check.h, a tester's errno 92 of 2026-10-29).
    say("Build: %s", proton_build_identity().c_str());
    if (proton_runtime_state() != PW_RUNTIME_THIS_BUILD) fail("%s", proton_runtime_problem().c_str());
    // The payload checklist, on every start too: the launcher it returns to shows what to load or close (pw_payload_check.h).
    if (!proton_payloads_check().pass) fail("%s", proton_payloads_problem().c_str());
    {
        // The helper this title was built with, from its own folder (tools/build-production.sh): an older install's copy
        // in /data/ps5proton/run can never be started by a newer title. A test may still name another image.
        const std::string own = proton_title_path("/app0/proton-payload-child.elf");
        if (!getenv("WINE_PS5_PAYLOAD_IMAGE") && access(own.c_str(), R_OK) == 0)
            setenv("WINE_PS5_PAYLOAD_IMAGE", own.c_str(), 1);
        const char *image = getenv("WINE_PS5_PAYLOAD_IMAGE");
        const std::string path = image ? image : PROTON_DATA_ROOT "/run/proton-payload-child.elf";
        char hex[65] = "-";
        if (FILE *file = fopen(path.c_str(), "rb")) {
            PwSha256 hash;
            uint8_t buffer[65536], digest[32];
            size_t got;
            pw_sha256_init(&hash);
            while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) pw_sha256_update(&hash, buffer, got);
            fclose(file);
            pw_sha256_final(&hash, digest);
            pw_sha256_hex(digest, hex);
        }
        say("Build: helper %s sha256 %.16s%s", path.c_str(), hex, image ? "" : " (the runtime's copy: this title has none)");
    }
    int detailed = 0;
    const int detailed_setting = pw_detailed_logs_load(&detailed, proton_logging_path().c_str());
    if (detailed) detailed_logs_begin();
    else say("Logging: off%s", detailed_setting == ENOENT ? " (default)" : detailed_setting ? " (setting unreadable)" : "");
    ProtonGame game;
    const std::vector<ProtonGame> catalog = proton_read_games(nullptr);
    for (const auto &candidate : catalog)
        if (candidate.id == launch.profile) game = candidate;
    // A profile that names no entry would run --program in the probe's prefix (a 2026-10-14 run, record removed from
    // the tree, ran an emulator under profile 10). The built-in mihawk-os entry is in the catalog (proton_read_games).
    if (!launch.profile.empty() && game.id != launch.profile) {
        fail("Wine: requested profile %s is not in the library", launch.profile.c_str());
    }
    if (!launch.path.empty()) {
        // Another program run with this entry's prefix and settings (tools/run-console.py --program) starts where
        // Explorer would start it, in its own folder, and without the entry's arguments: a Go game ran in a launcher's
        // folder with the launcher's switches and could not open external/script/main.lua (2026-10-11; record removed from the tree).
        game.executable = launch.path;
        game.directory.clear();
        game.arguments.clear();
    }
    if (launch.arguments_override) game.arguments = launch.arguments;
    if (game.executable.empty() || !game.complaint.empty()) {
        fail("Wine: requested profile is missing or invalid%s%s", game.complaint.empty() ? "" : ": ",
             game.complaint.c_str());
    }
    // On disk before anything can crash: the launcher finds it if this session never comes back (PROTON_SESSION_MARKER).
    remove(proton_title_path(PROTON_SESSION_NOTICES).c_str());
    {
        const std::string marker = proton_title_path(PROTON_SESSION_MARKER);
        const int fd = open(marker.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        const bool written = fd >= 0 && write(fd, game.name.data(), game.name.size()) == (ssize_t)game.name.size() &&
            !fsync(fd);
        if (fd >= 0) close(fd);
        if (!written) say("Wine: the session marker %s could not be written (errno %d)", marker.c_str(), errno);
    }
    // Every Wine thread holds socket pairs to the in-process server, a JVM opens
    // a hundred jars, and the console starts a process at a low open-files
    // limit: a Java game's saves failed with "Too many open files" thousands of
    // times. Take the limit the system allows.
    {
        struct rlimit files{};
        if (!getrlimit(RLIMIT_NOFILE, &files)) {
            const unsigned long long before = (unsigned long long)files.rlim_cur;
            files.rlim_cur = files.rlim_max;
            const int status = setrlimit(RLIMIT_NOFILE, &files);
            say("Wine: open files limit %llu -> %llu (maximum %llu) status=%d", before,
                (unsigned long long)files.rlim_cur, (unsigned long long)files.rlim_max, status);
        } else {
            say("Wine: open files limit unknown");
        }
    }
    // The controller reaches programs as the player chose (below, after the game's own environment); the rest of a
    // program's environment (WINE_PS5_OPENGL for OpenGL, say) is its game.json "env".
    {
        // The installed firmware ("13.40") for Windows programs that need a per-console figure: tests/late-library-probe.c's
        // floor of reservable address space is one (src/pw_firmware.h: the system's own word is spoofed by enablers).
        char text[8];
        if (pw_firmware_text(pw_firmware_installed(nullptr), text, sizeof(text))) setenv("WINE_PS5_FIRMWARE", text, 1);
        // Every helper watches the title's own exit by this pid (native/proton_wine_child.c's title_watch).
        snprintf(text, sizeof(text), "%d", (int)getpid());
        setenv("WINE_PS5_TITLE_PID", text, 1);
    }
    const std::string prefix = proton_game_prefix(game);
    if (prefix.empty()) {
        fail("Wine: invalid prefix ID");
    }
    session_prefix = prefix;
    const std::string runtime = PROTON_DATA_ROOT "/runtime/ge11-7";
    const std::string modules = runtime + "/lib/wine/x86_64-unix";
    const std::string ntdll = modules + "/ntdll.prx";
    if (access(ntdll.c_str(), R_OK)) {
        fail("Wine: runtime missing: %s is not there", ntdll.c_str());
    }
    // A game without a prefix gets one made here, on the console: Wine lays out the folder and its drive links, and
    // wineboot (a helper process, patch 0933) fills it from wine.inf, as on a PC. A copy from the PC is no longer
    // needed (tools/deploy-wine.py --prefix still makes one).
    // The drive links (C: to drive_c, Z: to /) are laid down here: Wine makes them only where a client connects
    // to the server itself, and in the launcher layout the first program is handed its connection (prefix-01: no
    // C:, so neither wineboot nor the program was found). A title cannot make symbolic links; the links' table is
    // the file the Unix side reads in their folder (wine/ps5/pw_wine_cwd.h).
    if (access((prefix + "/system.reg").c_str(), R_OK)) {
        const int made = mkdir(prefix.c_str(), 0777);
        say("Wine: no prefix at %s yet; Wine and wineboot make it (folder %s)", prefix.c_str(),
            !made ? "made" : errno == EEXIST ? "already there" : strerror(errno));
        mkdir((prefix + "/drive_c").c_str(), 0777);
        if (!mkdir((prefix + "/dosdevices").c_str(), 0777) || errno == EEXIST) {
            const std::string table = prefix + "/dosdevices/.pw-symlinks";
            if (access(table.c_str(), F_OK)) {
                FILE *links = fopen(table.c_str(), "w");
                const bool written = links && fputs("c:\t../drive_c\nz:\t/\n", links) >= 0;
                if (links) fclose(links);
                say("Wine: drive links C: and Z: %s", written ? "written" : "could not be written");
            }
        }
        // The language the guided setup chose is this new prefix's, for good (docs/GUIDED_SETUP.md)
        PwSetupRecord record;
        if (!pw_setup_record_load(&record, PROTON_DATA_ROOT "/setup.txt") && pw_setup_language(record.language)) {
            FILE *pin = fopen((prefix + "/.pw-language").c_str(), "w");
            const bool pinned = pin && fprintf(pin, "%s\n", record.language) > 0;
            if (pin) fclose(pin);
            say("Wine: the new prefix's language: %s%s", record.language, pinned ? "" : " (not pinned)");
        }
    }
    // Every Windows program but the shell's Explorer takes the prefix's language (patch 1230; helpers inherit the
    // variable, wine/ps5/pw_wine_inherit.h): its locale, UI language and keyboard layout.
    {
        char tag[16] = {};
        FILE *pin = fopen((prefix + "/.pw-language").c_str(), "r");
        if (pin && fgets(tag, sizeof(tag), pin)) tag[strcspn(tag, "\r\n")] = 0;
        if (pin) fclose(pin);
        if (const PwSetupLanguage *language = pw_setup_language(tag)) {
            setenv("WINE_PS5_LOCALE", language->locale, 1);
            say("Wine: Windows programs speak %s (%s); the shell stays English", language->tag, language->locale);
        }
    }
    // The Windows user is Mihawk (patch 1240); a prefix made before v2.0.0-alpha-3 has Proton's C:\users\steamuser,
    // which moves once, with its registry's paths and a link from the old name, before Wine starts on it
    // (src/pw_wine_profile.h). Before the user folders below, which would otherwise make an empty Mihawk beside it.
    PwWineProfileMove profile_move;
    if (pw_wine_profile_migrate(prefix.c_str(), &profile_move)) {
        say("Wine: the profile's move to %s stopped at its %s step (%s); Wine starts, the next start goes on",
            PW_WINE_PROFILE_USER, profile_move.step, strerror(errno));
    } else if (!profile_move.already) {
        say("Wine: profile %s: %u renames, %u kept in %s, link %s, %u registry files rewritten (%zu references)",
            PW_WINE_PROFILE_USER, profile_move.moved, profile_move.kept, PW_WINE_PROFILE_FORMER,
            profile_move.linked ? "from " PW_WINE_PROFILE_FORMER : "none", profile_move.files, profile_move.replaced);
    }
    // wineboot makes the user folders, but not in the single layout (no child processes) nor in a prefix copied
    // from a PC; without AppData\Roaming a .NET program gets an empty application-data folder (src/pw_wine_launch.h).
    int folders_made = 0;
    const int folder_failures = pw_wine_launch_profile_dirs(prefix.c_str(), [](const char *path, void *made) {
        if (mkdir(path, 0777) == 0) { ++*static_cast<int *>(made); return 0; }
        return errno == EEXIST ? 0 : -1;
    }, &folders_made);
    if (folders_made || folder_failures) say("Wine: user folders: %d made, %d failed", folders_made, folder_failures);
    // Proton's default native set (DXVK: d3d11 dxgi d3d10core d3d9; VKD3D-Proton: d3d12 d3d12core), as the conformance
    // runners load it. Wine's own d3d12 needs Wine's own DXGI, so with DXVK's no Direct3D 12 program gets a device.
    // tests/test_dll_overrides.py
    static const char default_overrides[] = "d3d11,dxgi,d3d10core,d3d9,d3d12,d3d12core=n;bcrypt=b";
    const char *args[PW_WINE_START_MAX_ARGS + 1] = {"wine", game.executable.c_str()};
    char words[4096];
    // The desktop session's entry runs no Windows program: Explorer is the root, and the entry's arguments are what
    // Explorer runs once its desktop is up (src/pw_desktop_session.h).
    desktop_session = pw_desktop_session_entry(game.executable.c_str());
    const char *split[PW_WINE_LAUNCH_WORDS + 1] = {};
    int count = pw_wine_launch_split(game.arguments.c_str(), words, sizeof(words), split,
                                     desktop_session ? PW_WINE_LAUNCH_WORDS - 1 : PW_WINE_LAUNCH_WORDS);
    if (count < 0) fail("Wine: invalid argument list");
    if (desktop_session) {
        const int shell = pw_desktop_session_argv(args, sizeof(args) / sizeof(args[0]), "wine", split, (size_t)count);
        if (shell < 0) fail("Wine: invalid argument list");
        count = shell - 2;
        say("Mihawk: desktop session; Explorer is its root, the title supervises it");
    } else {
        for (int i = 0; i < count; ++i) args[2 + i] = split[i];
    }
    // Settings > Logging adds process starts and ends and every channel's warnings; warn-seh stays last, so a game's own
    // exceptions are still not logged.
    const char *winedebug = game.id.empty() ?
        (detailed ? "+timestamp,+pid,+tid,+seh,+debugstr,+loaddll,+process,+thread,+plugplay,+winebus,warn+all" :
                    "+timestamp,+pid,+tid,+seh,+debugstr,+loaddll,+process,+thread,+plugplay,+winebus,warn+explorer") :
        // warn-seh: a game's exceptions are its own control flow (an emulator's write tracker takes thousands), and
        // each one was symbolized and logged twice (WINE_BACKTRACE_LOG_ON is WARN_ON(seh)); crashes still log as err.
        detailed ? "+timestamp,+pid,+tid,+debugstr,+loaddll,+process,+ps5audio,warn+all,warn-seh" :
                   "+timestamp,+pid,+tid,+debugstr,+loaddll,+ps5audio,warn+explorer,warn-seh";
    // A test run's own channels (tools/run-console.py --env PROTON_WINEDEBUG=...): what a failure needs traced, for that launch only.
    // Read from this launch's own entries: they are applied to the environment only further down.
    std::string launch_winedebug;
    for (const auto &word : launch.env)
        if (word.rfind("PROTON_WINEDEBUG=", 0) == 0 && word.size() > 17) launch_winedebug = word.substr(17);
    if (!launch_winedebug.empty()) winedebug = launch_winedebug.c_str();
    // Logging also keeps the synchronization device's last 8192 operations (256 KiB a process) for a hang's dump.
    if (detailed) setenv("WINE_PS5_NTSYNC_TRACE", "8192", 0);
    const PwWineStartEnv env[] = {
        {"WINEDEBUG", winedebug}, {"WINEARCH", "wow64"},
        {"HOME", prefix.c_str()}, {"USER", PW_WINE_PROFILE_USER}, {"WINE_PS5_TRACE_STARTUP", "1"},
        {"WINE_PS5_VIEW", "desktop"}, {"WINEDLLOVERRIDES", default_overrides},
        // Manual launches need usable standard handles too; only the initial
        // client opens this file, and children retain inherited/redirected pipes.
        {"WINE_PS5_STDOUT", PROTON_DATA_ROOT "/logs/wine-stdout.txt"},
        {"WINE_PS5_DESKTOP", "1920x1080"},
        {"WINE_PS5_CAPTURE", PROTON_DATA_ROOT "/logs/game-frame.ppm"},
        // wined3d's OpenGL adapter cannot start here ("Failed to get a GL context for adapter"), so DXCore, which
        // enumerates adapters through wined3d, found none; with its Vulkan renderer it finds RADV (d3d12.c:955).
        {"WINE_D3D_CONFIG", "renderer=vulkan"},
        {"DOTNET_SYSTEM_GLOBALIZATION_USENLS", "1"},
        // The certificate authorities a Windows program's TLS trusts: crypt32's root store, from the runtime's bundle.
        {"WINE_ADDITIONAL_CERTS_DIR", PROTON_DATA_ROOT "/runtime/ge11-7/share/wine/ca-certificates.crt"},
        {"DXVK_LOG_PATH", PROTON_DATA_ROOT "/logs"}, {"WINEESYNC", "0"}, {"WINEFSYNC", "0"}
    };
    const PwWineStartOps ops = {sceKernelLoadStartModule, sceKernelGetModuleInfo, set_env, start_thread};
    const PwWineStartConfig config = {ntdll.c_str(), modules.c_str(), prefix.c_str(), env,
        sizeof(env) / sizeof(env[0]), count + 2, args, 16u << 20};
    int status = pw_wine_start_environment(&wine, &config, &ops);
    // The game's own environment (game.json "env") wins over the title's defaults,
    // and this launch's test overrides win over both.
    for (const auto &word : game.env) {
        const size_t equals = word.find('=');
        setenv(word.substr(0, equals).c_str(), word.c_str() + equals + 1, 1);
        say("Wine: game environment entry applied");
    }
    // One-shot test overrides (env=NAME=VALUE in wine-run.txt) win over defaults.
    for (const auto &word : launch.env) {
        const size_t equals = word.find('=');
        setenv(word.substr(0, equals).c_str(), word.c_str() + equals + 1, 1);
        say("Wine: launch environment entry applied");
    }
    {
        // The player's Game Mode setting (src/pw_game_mode.h), or the game's or a test's own WINE_PS5_GAME_MODE: off
        // composes every frame, so the desktop never unloads. A program started in the session has its own the same
        // way (its entry's, or inherited: PW_CHILD_NOTICE_GAME_MODE, src/pw_window_direct.h).
        const char *own = getenv(PW_GAME_MODE_ENV);
        int game_mode = 1, game_mode_error = 0;
        if (own && (!strcmp(own, "1") || !strcmp(own, "0"))) game_mode = own[0] == '1';
        else game_mode_error = pw_game_mode_load(&game_mode, proton_game_mode_path().c_str());
#ifdef PS5_GPU_BROKER
        pw_window_direct_set_enabled(game_mode);
#endif
        say("Game Mode: %s%s", game_mode ? "on" : "off",
            own && !game_mode_error && (!strcmp(own, "1") || !strcmp(own, "0")) ? " (this game's or launch's own)" :
            game_mode_error == ENOENT ? " (default)" : game_mode_error ? " (setting unreadable; default)" : "");
    }
    {
        // The controller programs see (src/pw_controller.h): the player's setting, or a game's or a test's own
        // WINE_PS5_CONTROLLER. Only the DualSense has the HID device; xinput reads WINE_PS5_CONTROLLER (patch 2400).
        // A game's SDL_XINPUT_ENABLED=0 chose the DualSense over HID when both were there; with an Xbox controller,
        // XInput is the only one, so it goes.
        const char *own = getenv("WINE_PS5_CONTROLLER");
        int controller = pw_controller_kind(own), error = 0;
        if (controller < 0) error = own ? EINVAL : pw_controller_load(&controller, proton_controller_path().c_str());
        if (controller < 0) controller = PW_CONTROLLER_XBOX_360;
        setenv("WINE_PS5_CONTROLLER", pw_controller_word(controller), 1);
        if (controller == PW_CONTROLLER_DUALSENSE) setenv("WINE_PS5_HID", "1", 1);
        else unsetenv("WINE_PS5_HID");
        const char *sdl = getenv("SDL_XINPUT_ENABLED");
        const bool sdl_dropped = controller != PW_CONTROLLER_DUALSENSE && sdl && !strcmp(sdl, "0");
        if (sdl_dropped) unsetenv("SDL_XINPUT_ENABLED");
        say("Controller: %s%s%s", pw_controller_word(controller),
            own && !error ? " (this game's or launch's own)" : own ? " (WINE_PS5_CONTROLLER not understood; default)" :
            error == ENOENT ? " (default)" : error ? " (setting unreadable; default)" : "",
            sdl_dropped ? "; the game's SDL_XINPUT_ENABLED=0 dropped: XInput is its only controller" : "");
    }
    {
        // The player's 120 Hz setting (src/pw_high_refresh.h): off asks for 60 Hz, unless the game or the launch
        // named PROTON_120HZ itself.
        const char *own = getenv("PROTON_120HZ");
        int high = 1, error = 0;
        if (!own) error = pw_high_refresh_load(&high, proton_high_refresh_path().c_str());
        if (!high) setenv("PROTON_120HZ", "0", 1);
        say("120 Hz: %s%s", own ? "the game's or launch's own" : high ? "on" : "off",
            own ? "" : error == ENOENT ? " (default)" : error ? " (setting unreadable; default)" : "");
    }
    // The speed switches on by default, after the game's and this launch's own, so either can still turn one off
    // with NAME=0 (src/pw_speed_defaults.h: the sync device, the helpers' CPUs, the asynchronous present, the present
    // link, 120 Hz).
    pw_speed_defaults_apply([](const char *name) -> const char * { return getenv(name); }, setenv);
    say("Wine: speed switches WINE_PS5_NTSYNC=%s WINE_PS5_HELPER_CPUS_WIDEN=%s WINE_PS5_ASYNC_PRESENT=%s "
        "WINE_PS5_PRESENT_LINK=%s PROTON_120HZ=%s", getenv("WINE_PS5_NTSYNC"), getenv("WINE_PS5_HELPER_CPUS_WIDEN"),
        getenv("WINE_PS5_ASYNC_PRESENT"), getenv("WINE_PS5_PRESENT_LINK"), getenv("PROTON_120HZ"));
    {
        // The console's pipe memory as this launch finds it (src/pw_pipe_memory.h): a fresh pipe's buffer, small once
        // more than half is used, the warning before the boot's pipes run out.
        const long buffer = pw_pipe_buffer_probe();
        say("Wine: pipe memory at start: a fresh pipe takes %ld bytes%s", buffer,
            pw_pipe_memory_low(buffer) ? " (low: more than half the console's pipe memory is used)" : "");
    }
    // 120 Hz output (on by default; game.json "env": PROTON_120HZ=0 turns it off for a game that counts vblanks for its
    // timing, which would run twice as fast): RADV's display runs at 119.88 Hz where the title declares high frame rate
    // (param.json attribute3 0x40) and the TV takes it. An elevated title has no /app0, so RADV is told where
    // param.json is. What Wine offers (WINE_PS5_REFRESH) waits for the broker's display, below.
    if (const char *high = getenv("PROTON_120HZ"); high && !strcmp(high, "1")) {
        const std::string param = std::string(proton_app_root()) + "/sce_sys/param.json";
        setenv("PS5_VIDEOOUT_PARAM_JSON", param.c_str(), 1);
        say("Wine: 120 Hz output asked for (%s)", param.c_str());
    }
    if (const char *query = getenv("WINE_PS5_GPU_DEVICE_QUERY"); query && !strcmp(query,"1")) {
        char identity[256];
        proton_gpu_identity(identity,sizeof(identity),"owner");
        say("%s",identity);
    }
    cpu_topology();
    // The guided setup's first session (its marker; examples/ps5proton/setup_tour.hpp) shows the setup's cover in place
    // of the desktop once the broker runs (src/pw_setup.h), so not the loading screen before it.
    const bool setup_preparing = access(PROTON_DATA_ROOT "/setup-preparing", F_OK) == 0;
    // The Steam guide's sessions (src/pw_steam_guide.h; their entries say which) show its screens the same way.
    const char *guide_env = getenv("PROTON_STEAM_GUIDE");
    const int steam_guide = !guide_env ? 0 : !strcmp(guide_env, "install") ? 1 : !strcmp(guide_env, "signin") ? 2 : 0;
    bool guide_screens = false;   // they are up: without them (no faces) the client's windows show and the pad is theirs
    // A test's pictures of those screens on the console (tools/run-console.py --env PROTON_STEAM_GUIDE_SHOWCASE=1, or one
    // language's tag): each screen in each language for three seconds and a broker capture of each; then the session ends.
    std::vector<const PwSetupLanguage *> showcase;
    if (const char *word = steam_guide ? nullptr : getenv("PROTON_STEAM_GUIDE_SHOWCASE")) {
        size_t count = 0;
        const PwSetupLanguage *all = pw_setup_languages(&count);
        for (size_t i = 0; i < count; ++i)
            if (all[i].text && (!strcmp(word, "1") || !strcmp(word, all[i].tag))) showcase.push_back(&all[i]);
        say("steam guide showcase: %zu languages, 5 screens each", showcase.size());
    }
    std::vector<unsigned> showcase_captures;   // the pad script's capture numbers the showcase asked for
    if (!setup_preparing && !steam_guide) {
        const size_t slash = game.executable.find_last_of("\\/");
        const std::string shown = !game.name.empty() ? game.name
            : slash == std::string::npos ? game.executable : game.executable.substr(slash + 1);
        // A session that runs the client shows its screen: the sign-in QR and what the client last said.
        // A hand-off restarts the title in place, so the previous session's captured output is still on disk; the poller
        // starts at byte 0 and would replay it as this session's events (2026-10-22: the sign-in's list hand-off re-ran
        // the login from the stale file and looped, cycle after cycle). Empty it before this session's client starts.
        if (aurelia_session()) {
            const std::string output = aurelia_output_path();
            const int cleared = remove(output.c_str());
            say("Aurelia: previous output %s (%s)", cleared == 0 ? "cleared" : "absent", output.c_str());
        }
        proton_aurelia_overlay(aurelia_session());
        proton_loading_start(shown.c_str(), (std::string(proton_app_root()) + "/assets/hui/fonts").c_str());
    }
    const char *cache_setting = getenv("PROTON_SHADER_CACHE");
    const bool cache_enabled = !cache_setting || strcmp(cache_setting, "0");
    const int cache_status = pw_shader_cache_configure(PROTON_DATA_ROOT "/caches",
        game.id.empty() ? "probe" : game.id.c_str(), (runtime + "/cache-version").c_str(), cache_enabled);
    say("Wine: shader caches %s status=%d", cache_enabled && !cache_status ? "enabled" : "disabled", cache_status);
    if (cache_enabled && !cache_status)
        say("Wine: shader cache namespace %s", getenv("PS5_SHADER_CACHE_DIR"));
    // Where a program's processes run (docs/WINDOWS_PROCESSES.md, "Layouts"), from PROTON_LAYOUT in game.json's
    // env or else chosen here:
    //   title     the program runs in the title, with the title's own graphics (OpenGL, Vulkan, 120 Hz), and the
    //             processes it starts run in helpers, without graphics;
    //   launcher  every process runs in a helper and draws through the title's GPU broker: a launcher whose
    //             window and games are processes it starts (the launcher's browser (CEF), the games of its library);
    //   single    no child processes: CreateProcess fails as it did before 2026-10-05.
    // Every program uses the owner/broker layout by default, regardless of its
    // executable name. The older layouts remain explicit diagnostic options.
    const char *layout = getenv("PROTON_LAYOUT");
    if (!layout || (strcmp(layout, "title") && strcmp(layout, "launcher") && strcmp(layout, "single"))) {
        if (layout) say("Wine: unknown PROTON_LAYOUT=%s; choosing", layout);
        layout = "launcher";
    }
#ifndef PS5_GPU_BROKER
    if (!strcmp(layout, "launcher")) {
        say("Wine: this title has no GPU broker; the launcher layout runs as the title layout");
        layout = "title";
    }
#endif
    // A variable a game.json or a test run set wins over the layout's default.
    auto layout_env = [](const char *name, const char *value) { setenv(name, value, 0); };
    if (!strcmp(layout, "launcher")) {
        layout_env("WINE_PS5_OWNER_BOOTSTRAP", "1");
        layout_env("WINE_PS5_GPU_BROKER", "1");
        // Helpers send audio to the owning title's mixer, just as they send graphics to its broker.
        layout_env("WINE_PS5_HELPER_AUDIO", "1");
        layout_env("PROTON_GPU_LOCAL", "1");
        layout_env("WINE_PS5_CHILDREN", "1");
        // The initial client's ntdll installs the complete bundle after wineboot,
        // before DLL imports: first-launch and upgraded prefixes use the same path.
        const char *zink = getenv("PROTON_ZINK");
        const bool use_zink = !zink || strcmp(zink, "0");
        const char *overrides = getenv("WINEDLLOVERRIDES");
        if (overrides && !strcmp(overrides, default_overrides))
            setenv("WINEDLLOVERRIDES", (std::string(use_zink ? "opengl32=n,b;" : "opengl32=b;") + default_overrides).c_str(), 1);
        if (use_zink) layout_env("GALLIUM_DRIVER", "zink");
    } else if (!strcmp(layout, "title")) {
        layout_env("WINE_PS5_CHILDREN", "1");
    }
    // Payload clients no longer have Sony's seven-helper limit. Wine's service
    // manager owns shared Windows drivers (including NSI), so normal launches
    // need it as well as prefix setup. An explicit zero remains a diagnostic.
    if (getenv("WINE_PS5_CHILDREN")) setenv("WINE_PS5_SERVICES", "1", 0);
    say("Wine: layout %s (children %s, services %s)", layout, getenv("WINE_PS5_CHILDREN") ? getenv("WINE_PS5_CHILDREN") : "off",
        getenv("WINE_PS5_SERVICES") ? getenv("WINE_PS5_SERVICES") : "off");
    // The title and its child processes share one budget of about 250 open files (fd-budget-01), and with
    // children the server opens every Windows file of every process (patch 0929): it parks idle files above
    // 150 open rather than 192, so the title's other files and each child's own fit beside them.
    if (const char *owner = getenv("WINE_PS5_OWNER_BOOTSTRAP"), *children = getenv("WINE_PS5_CHILDREN");
        ((owner && !strcmp(owner, "1")) || (children && !strcmp(children, "1"))) && !getenv("PW_FDV_HIGH_WATER"))
        setenv("PW_FDV_HIGH_WATER", "150", 1);
    if (!status) status = pw_wine_start_load(&wine, &config, &ops);
    say("Wine: load status=%d stage=%d module=%d", status, wine.stage, wine.module);
    if (status) fail("Wine: the runtime did not load (status %d at stage %d)", status, wine.stage);
    atexit(back_to_launcher);
    if (auto set_tsc = symbol<void (*)(uint64_t)>("pw_wine_perf_set_tsc_hz")) set_tsc(sceKernelGetTscFrequency());
    perf.snapshot = symbol<void (*)(PwWinePerf *)>("pw_wine_perf_snapshot");
    perf.percentile = symbol<uint32_t (*)(const PwWinePerfFrames *, const PwWinePerfFrames *, unsigned)>(
        "pw_wine_perf_frame_percentile");
    flush_registry = symbol<int (*)(unsigned)>("pw_wine_server_flush");
    code_cache_stats = symbol<unsigned (*)(uint64_t *, unsigned)>("__wine_ps5_code_cache_stats");
    terminate_game = symbol<int (*)()>("pw_wine_server_terminate");
    stop_audio = symbol<int (*)(unsigned)>("pw_wine_stop_audio");
    server_snapshot = symbol<int (*)(unsigned)>("pw_wine_server_snapshot");
    device_dump = symbol<void (*)(void (*)(void *, const char *), void *, uint32_t, uint32_t)>("pw_ntsync_dump");
    device_counters = symbol<void (*)(PwNtsyncCounters *)>("pw_ntsync_counters");
    device_gone = symbol<int (*)(int, int, uint32_t)>("pw_ntsync_process_gone");
    perf.memory = wine.memory_stats;
    perf.virtual_memory = wine.virtual_stats;
    auto set_output = symbol<void (*)(void (*)(const char *, size_t))>("__wine_ps5_set_output_sink");
    if (set_output) set_output(output);
    auto set_release = symbol<void (*)(PwWineDisplayRelease, void *)>("pw_wine_set_display_release");
    if (set_release) set_release(release_display, nullptr);
    auto set_audio = symbol<void (*)(PwWineAudioSink, void *)>("pw_wine_set_audio_sink");
    if (set_audio && !pw_audio_ps5_platform_ops(&audio) && !pw_audio_ps5_open_port(&audio, &audio_port))
        set_audio(play_audio, nullptr);
    auto post_input = symbol<int (*)(const PwWineInput *)>("pw_wine_post_input");
    // With every program in a helper (the launcher layout) the server takes the input itself (patch 0931).
    int (*server_input)(const void *, unsigned) = nullptr;
    int (*server_foreground)() = nullptr; // the native process of the foreground window, for the log
    int (*server_windows)(void *, unsigned) = nullptr;
    auto deliver = [&](const PwWineInput *event) {
        return server_input ? server_input(event, 1) : post_input ? post_input(event) : -1;
    };
    auto sink_stats = symbol<void (*)(PwWineSinkStats *)>("pw_wine_sink_stats");
    fdv_stats = symbol<void (*)(PwWineFdvStats *)>("pw_wine_fdv_stats");
    if (auto set_resolver = symbol<void (*)(int (*)(const char *, unsigned char *))>("pw_ps5_set_resolver"))
        set_resolver(title_resolve);
    // A process another one starts gets its library entry's settings (src/pw_app_environment.h).
    build_application_table(catalog, prefix);
    if (auto set_lookup = symbol<void (*)(int (*)(const char *, const char *, char *, size_t, uint32_t *))>(
            "pw_ps5_set_application_lookup"))
        set_lookup(title_application_lookup);
    else
        say("Mihawk: this runtime has no application lookup; programs started from others keep their parent's settings");
    if (auto set_fixed_base = symbol<void (*)(int (*)(const char *))>("pw_ps5_set_fixed_base_lookup"))
        set_fixed_base(title_fixed_base);
    else
        say("Mihawk: this runtime has no fixed-base lookup; a 32-bit program fixed at 0x400000 cannot load");
    if (auto set_notice = symbol<void (*)(void (*)(int, int, int))>("pw_ps5_set_child_notice")) set_notice(child_notice);
    if (desktop_session) sync_desktop_shortcuts(catalog, prefix);
    if (auto set_dns_config = symbol<void (*)(int (*)(struct pw_dns_config *))>("pw_ps5_set_dns_config"))
        set_dns_config(title_dns_config);
    auto set_dualsense = symbol<void (*)(const uint8_t *)>("pw_wine_set_dualsense");
    auto set_pad = symbol<void (*)(const PwWinePad *)>("pw_wine_set_pad");
    auto set_cwd = symbol<int (*)(const char *)>("pw_cwd_set");
    char cwd[1024]={0};
    // The program starts in game.json's "directory", or in its executable's.
    std::string windows_dir = game.directory.empty() ?
        game.executable.substr(0, game.executable.find_last_of("\\/")) : game.directory;
    // A program at the root of a drive (C:\a.exe) starts in C:\, which the cut above leaves as "C:":
    // without the separator the directory is refused and the program starts in Z:\, the console's read-only root.
    if (windows_dir.size() == 2 && windows_dir[1] == ':') windows_dir += '\\';
    if (set_cwd && !pw_wine_launch_host_dir(prefix.c_str(), windows_dir.c_str(), cwd, sizeof(cwd)))
        say("Wine: working directory status=%d", set_cwd(cwd));
    say("Wine: output=%d audio=%d xinput=%d", set_output != nullptr, audio_port >= 0, set_pad != nullptr);
    pad_script.load();
    if (auto ticks = symbol<uint64_t (*)()>("pw_wine_perf_ticks")) perf.wine_started = ticks();
    const char *owner=getenv("WINE_PS5_OWNER_BOOTSTRAP");
    const bool owner_bootstrap=owner && !strcmp(owner,"1");
    const char *graphics_host=getenv("WINE_PS5_GRAPHICS_HOST");
    const char *gpu_setting=getenv("WINE_PS5_GPU_BROKER");
    // The SteamCMD session is headless: it is a console program, the loading screen owns the display (the setup
    // page), and the runtime's GPU broker/graphics owner would take VideoOut away from the page.
    const bool steam_runtime=steamcmd_session();
    const bool owner_gpu=!steam_runtime && owner_bootstrap && gpu_setting && !strcmp(gpu_setting,"1");
    const bool owner_graphics=!steam_runtime && !owner_gpu && owner_bootstrap && graphics_host && !strcmp(graphics_host,"1");
    // Explicit caller overrides also belong to the initial Windows process.
    // Catalog first, launch last; Wine applies Unicode/case-insensitive precedence.
    std::vector<const char *> application_entries;
    // Explorer enables its shell and sets up the desktop itself in the title's session (Wine patch 0996).
    if(desktop_session)application_entries.push_back("MIHAWK_SESSION=1");
    for(const auto &entry : game.env)application_entries.push_back(entry.c_str());
    for(const auto &entry : launch.env)application_entries.push_back(entry.c_str());
    char application[sizeof(pw_child_bootstrap::application)];size_t application_bytes=0;
    auto set_application=symbol<int (*)(const char *,size_t)>("pw_ps5_set_initial_environment");
    if(!set_application || pw_environment_pack(application,sizeof(application),application_entries.data(),
                                               application_entries.size(),&application_bytes) ||
       set_application(application,application_bytes)) {
        fail("Wine: initial application environment refused errno=%d",errno);
    }
    if(owner_bootstrap) {
        /* Initialize only the server in the GPU-capable title. The first
         * Windows program gets its own helper process, headless. */
        // A helper that dies leaves the server's and the broker's sockets to it broken, and a write to one must fail
        // (EPIPE) rather than end the title. wineserver's main() and every Wine client ignore SIGPIPE; this title
        // runs neither (2026-10-06, record removed from the tree: the game's helper crashed, then the title died of SIGPIPE).
        {
            struct sigaction ignore{};
            ignore.sa_handler = SIG_IGN;
            sigemptyset(&ignore.sa_mask);
            say("Wine: SIGPIPE ignored status=%d", sigaction(SIGPIPE, &ignore, nullptr));
        }
        auto open_module=symbol<void *(*)(const char *,int)>("dlopen");
        auto module_symbol=symbol<void *(*)(void *,const char *)>("dlsym");
        auto start_root=symbol<int (*)(int,const char *,const char *,const char *,int,const char *const *)>("pw_ps5_start_root");
        auto wait_children=symbol<void (*)()>("pw_ps5_wait_children");
        auto root_result=symbol<int (*)()>("pw_ps5_root_result");
        auto remote_signal=symbol<int (*)(pid_t,long,int)>("pw_ps5_remote_signal");
        auto remote_close=symbol<int (*)(pid_t)>("pw_ps5_remote_close");
        auto set_graphics=symbol<void (*)(int (*)(int,const struct pw_child_bootstrap *))>("pw_ps5_set_graphics_start");
        void *server=open_module ? open_module((modules+"/wineserver.so").c_str(),2) : nullptr;
        auto connect_server=server && module_symbol ? (int (*)(const char *))module_symbol(server,"pw_wineserver_connect") : nullptr;
        auto server_flush=server && module_symbol ? (int (*)(unsigned))module_symbol(server,"pw_wineserver_flush") : nullptr;
        auto set_remote=server && module_symbol ? (void (*)(int (*)(pid_t,long,int),int (*)(pid_t)))module_symbol(server,"pw_wine_threads_set_remote_signal") : nullptr;
        if(!connect_server || !server_flush || !start_root || !wait_children || !root_result || !set_remote || !remote_signal || !remote_close) {
            fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL missing bootstrap exports");
        }
        set_remote(remote_signal,remote_close);
        server_windows=module_symbol ? (int (*)(void *,unsigned))module_symbol(server,"pw_wineserver_windows") : nullptr;
        // An Aurelia session runs a headless client: the sign-in overlay owns the display, and the broker's own
        // presenter would take it a tenth of a second in and close the screen (2026-10-22). No window is shown, so the
        // broker stays off for it.
        if (aurelia_session())
            say("Aurelia: no GPU broker: the sign-in screen keeps the display");
        else if(owner_gpu) {
#ifdef PS5_GPU_BROKER
            auto set_gpu=symbol<void (*)(int (*)(int *,int),void (*)(int,int))>("pw_ps5_set_gpu_broker");
            const char *capture=getenv("PROTON_GPU_CAPTURE"),*local=getenv("PROTON_GPU_LOCAL");
            const bool window_surfaces=getenv("WINE_PS5_WINDOW_SURFACES") &&
                !strcmp(getenv("WINE_PS5_WINDOW_SURFACES"),"1");
            auto set_windows=symbol<void (*)(int (*)(int,int,size_t))>("pw_ps5_set_window_sink");
            if(window_surfaces && (!server_windows || !set_windows)) {
                fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL window surface exports");
            }
            proton_gpu_broker_set_windows(window_surfaces ? server_windows : nullptr);
            if(set_windows)set_windows(window_surfaces ? proton_gpu_broker_window_attach : nullptr);
            proton_gpu_broker_set_local_refused(gpu_memory_refused);
            if(!set_gpu || !proton_gpu_broker_start(capture && !strcmp(capture,"1"),local && !strcmp(local,"1"))) {
                fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL GPU broker startup");
            }
            set_gpu(proton_gpu_broker_open,proton_gpu_broker_close);
            owner_broker_running=true;
            if(const char *named=getenv("WINE_PS5_TITLE_PROCESS"); named && *named) {
                auto set_title_client=symbol<void (*)(int (*)(int,const struct pw_child_bootstrap *))>("pw_ps5_set_title_client_start");
                if(set_title_client)set_title_client(start_title_client);
                say("Wine: %s runs in the title beside the broker when started (%s)",named,
                    set_title_client ? "offered" : "an older runtime: in a helper");
            }
            // Wine offers 120 Hz modes (patches 0924, 1188) only when the broker's display kept 119.88 Hz, asked here,
            // before any helper starts (graphics patch 0029); on a TV that refuses it every program sees 60 Hz alone.
            if(const char *high=getenv("PROTON_120HZ"); high && !strcmp(high,"1")) {
                const unsigned hz=proton_gpu_broker_display_hz();
                if(const char *refresh=pw_display_refresh_env(hz))setenv("WINE_PS5_REFRESH",refresh,1);
                say("Wine: the display kept %u Hz; Wine offers %s",hz,hz==120 ? "60 and 120 Hz" : "60 Hz alone");
            }
            // The desktop: helpers' frames (their user drivers compose their windows, the broker shows them), the
            // foreground window's process, and the cursor the touchpad moves, drawn by the title (patches 0931, 0932).
            if(auto set_frames=symbol<void (*)(void (*)(int,int),void (*)(int))>("pw_ps5_set_frame_sink"))
                set_frames(proton_gpu_broker_frame_attach,proton_gpu_broker_frame_detach);
            proton_gpu_broker_set_desktop(
                module_symbol ? (int (*)())module_symbol(server,"pw_wineserver_foreground") : nullptr,
                module_symbol ? (void (*)(int *,int *))module_symbol(server,"pw_wineserver_cursor") : nullptr);
            if(setup_preparing)proton_cover_start(PROTON_DATA_ROOT "/" PW_SETUP_COVER_NAME,proton_gpu_broker_set_cover);
            if(steam_guide) {
                PwSetupRecord setup_record;
                pw_setup_record_load(&setup_record,PROTON_DATA_ROOT "/setup.txt");
                guide_screens=proton_guide_start(setup_record.language,proton_app_root(),proton_gpu_broker_set_cover);
                if(guide_screens)
                    proton_guide_show(steam_guide==2 ? PW_STEAM_SCREEN_UPDATING : PW_STEAM_SCREEN_INSTALLING,0,0,nullptr);
            }
            if(!showcase.empty())proton_guide_start(showcase[0]->tag,proton_app_root(),proton_gpu_broker_set_cover);
#else
            fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL GPU broker absent from this build");
#endif
        }
        flush_registry=server_flush; // The owner can have no Wine client to bind this wrapper.
        // Nor these: the server's snapshot of every thread's wait for dump_hang, and its terminate (patch 0899), now
        // only for a game whose helpers the kernel would not let the title SIGKILL (end_game).
        server_snapshot=module_symbol ? (int (*)(unsigned))module_symbol(server,"pw_wineserver_snapshot") : nullptr;
        terminate_game=module_symbol ? (int (*)())module_symbol(server,"pw_wineserver_terminate") : nullptr;
        server_user_processes=module_symbol ? (int (*)())module_symbol(server,"pw_wineserver_user_processes") : nullptr;
        // With every program in a helper, a game that does not close is ended by SIGKILL to its helpers.
        programs_in_helpers=!owner_graphics;
        list_helpers=symbol<unsigned (*)(int *,unsigned,int *)>("pw_ps5_helpers");
        end_spawns=symbol<void (*)()>("pw_ps5_end_spawns");
        collect_sections=symbol<void (*)()>("pw_ps5_dsection_collect_now");
        // And the touchpad's pointer and the close request go to the desktop, wherever its windows are.
        server_input=module_symbol ? (int (*)(const void *,unsigned))module_symbol(server,"pw_wineserver_input") : nullptr;
        server_foreground=module_symbol ? (int (*)())module_symbol(server,"pw_wineserver_foreground") : nullptr;
        server_windows=module_symbol ? (int (*)(void *,unsigned))module_symbol(server,"pw_wineserver_windows") : nullptr;
        if(owner_graphics) {
            if(!set_graphics) fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL graphics export");
            owner_result=root_result;owner_wait=wait_children;set_graphics(start_owner_graphics);
        }
        // The memory the title's server and GPU broker hand to helpers is made by the memory helper, a helper process,
        // so none of it is the title's: memory the title made and a helper still mapped when the system closed the
        // title panicked the console (src/pw_shared_page.h). It starts before the server makes any.
        {
            auto start_memory=symbol<int (*)()>("pw_ps5_start_memory_helper");
            auto shared_alloc=symbol<int (*)(size_t)>("pw_ps5_shared_alloc");
            auto transport_alloc=symbol<int (*)(size_t)>("pw_ps5_transport_alloc");
            auto shared_touch=symbol<int (*)(int,size_t,size_t)>("pw_ps5_shared_touch");
            auto set_server_memory=module_symbol ?
                (void (*)(int (*)(size_t),int (*)(int,size_t,size_t)))module_symbol(server,"pw_wineserver_set_shared_memory") : nullptr;
            const bool exports=start_memory && shared_alloc && transport_alloc && shared_touch && set_server_memory;
            const bool memory=exports && !start_memory();
            // Without its helper the title would make that memory itself (the console panicked on that), or install
            // hooks that always fail (ENOTCONN: started, then did not answer; wine/ps5/pw_wine_process.h).
            if(exports && !memory) {
                const int error=errno;
                fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL memory helper %s (errno %d)",
                     error==ENOTCONN ? "did not answer" : "did not start", error);
            }
            if(memory) {
                set_server_memory(shared_alloc,shared_touch);
                // The synchronization device's objects are in memory the title and every helper map (src/pw_ntsync.h),
                // made here by the memory helper; ntdll formats it before the server starts, and the server sends it to
                // each helper (patch 1180). On by default (src/pw_speed_defaults.h); WINE_PS5_NTSYNC=0 turns it off, and
                // so does a region that cannot be made: every wait then goes to the server.
                if(const char *sync=getenv("WINE_PS5_NTSYNC"); sync && !strcmp(sync,"1")) {
                    auto region_bytes=symbol<size_t (*)()>("pw_ntsync_region_bytes");
                    const size_t bytes=region_bytes ? region_bytes() : 0;
                    const int region=bytes ? shared_alloc(bytes) : -1;
                    char number[24];
                    snprintf(number,sizeof(number),"%d",region);
                    if(region>=0 && !shared_touch(region,0,bytes) && !setenv("WINE_PS5_NTSYNC_REGION",number,1)) {
                        // The owner starts the server itself (connect_server below), so it hands the server the device
                        // here, before the server makes its first object; ntdll's own connect does it in the title
                        // layout (patch 1180).
                        auto ntsync_start=symbol<const void *(*)()>("pw_wine_ntsync_start");
                        auto set_ntsync=module_symbol ? (void (*)(const void *))module_symbol(server,"pw_wineserver_set_ntsync") : nullptr;
                        auto set_region=module_symbol ? (void (*)(int))module_symbol(server,"pw_wineserver_set_ntsync_region") : nullptr;
                        const void *ops=ntsync_start && set_ntsync && set_region ? ntsync_start() : nullptr;
                        if(ops) { set_region(region);set_ntsync(ops); }
                        say("Wine: one synchronization device for the title and its helpers, %zu KiB: %s",bytes>>10,
                            ops ? "the server's objects are in it" : "not attached, every wait goes to the server");
                    } else {
                        say("Wine: synchronization device off: its region was not made (errno %d)",errno);
                        if(region>=0) close(region);
                    }
                }
                // Large anonymous sections (256 MiB and more) in direct memory, not a file on /data paged through a 64 MiB cache
                // (patch 1121, wine/ps5/pw_wine_dsection.h): an emulated game's guest memory thrashed the disk until the console went
                // down (2026-10-14; record removed from the tree). PROTON_SECTION_DMEM=0 keeps the file backing.
                if(const char *dsection=getenv("PROTON_SECTION_DMEM"); !dsection || strcmp(dsection,"0")) {
                    // The registration carries the backing file (patch 1126); a runtime from another build has no such hook.
                    auto set_dsection=module_symbol ?
                        (void (*)(int (*)(int,uint64_t,uint64_t,uint64_t),void (*)(uint64_t,uint64_t)))module_symbol(server,"pw_wineserver_set_dsection_fd") : nullptr;
                    auto dsection_register=symbol<int (*)(int,uint64_t,uint64_t,uint64_t)>("pw_ps5_dsection_register");
                    auto dsection_release=symbol<void (*)(uint64_t,uint64_t)>("pw_ps5_dsection_release");
                    if(set_dsection && dsection_register && dsection_release) {
                        set_dsection(dsection_register,dsection_release);
                        say("Wine: large anonymous sections in direct memory");
                    } else say("Wine: large anonymous sections stay on files (no hooks)");
                }
#ifdef PS5_GPU_BROKER
                proton_gpu_broker_set_shared_memory(transport_alloc,shared_touch);
                // The broker's host-visible buffers come from direct memory the memory helper owns and grants to the title and the
                // client (patch 0009; docs/PLAN.md, "Direct memory shared between processes"), not from shared memory, which is
                // charged to the title's 1 GiB flexible pool. PROTON_GPU_HOST_DMEM=0 returns to shared memory.
                if(const char *host_dmem=getenv("PROTON_GPU_HOST_DMEM"); !host_dmem || strcmp(host_dmem,"0")) {
                    static int (*real_dmem_alloc)(size_t,int,int64_t *);
                    real_dmem_alloc=symbol<int (*)(size_t,int,int64_t *)>("pw_ps5_dmem_alloc");
                    // PROTON_GPU_TEST_DMEM_REFUSE=n (a test): the helper "refuses" every range after the first n, to show the broker
                    // falling back to shared memory.
                    static long refuse_after=getenv("PROTON_GPU_TEST_DMEM_REFUSE") ? atol(getenv("PROTON_GPU_TEST_DMEM_REFUSE")) : -1;
                    int (*dmem_alloc)(size_t,int,int64_t *)=real_dmem_alloc;
                    if(real_dmem_alloc && refuse_after>=0) dmem_alloc=[](size_t bytes,int pid,int64_t *offset)->int {
                        static long made=0;
                        if(made>=refuse_after) { errno=ENOMEM;return -1; }
                        ++made;return real_dmem_alloc(bytes,pid,offset);
                    };
                    auto dmem_release=symbol<int (*)(int64_t,size_t)>("pw_ps5_dmem_free");
                    if(dmem_alloc && dmem_release) proton_gpu_broker_set_dmem_memory(dmem_alloc,dmem_release);
                    say("Wine: GPU host-visible memory from direct memory: %s", dmem_alloc && dmem_release ? "on" : "missing in this runtime");
                }
#endif
                // PROTON_PT_PROBE (a probe, never a default; docs/PLAN.md "Memory plan", Part 2): memory the memory helper
                // makes, mapped and touched by the title and held until it ends, to measure whether the system's page-table
                // count drifts with it at the container's deletion. "none" maps nothing (the baseline), "shm:N" N MiB of the
                // helper's shared memory, "dmem:N" N MiB of its direct memory granted to the title.
                if(const char *probe=getenv("PROTON_PT_PROBE"); probe && *probe) {
                    const bool dmem=!strncmp(probe,"dmem:",5), shm=!strncmp(probe,"shm:",4);
                    const size_t mib=(dmem||shm) ? (size_t)strtoul(strchr(probe,':')+1,nullptr,10) : 0;
                    const size_t bytes=(mib>0 && mib<=1024 ? mib : 0)<<20;
                    void *at=nullptr;
                    int status=0;
                    if(bytes && shm) {
                        const int object=shared_alloc(bytes);
                        status=object<0 || shared_touch(object,0,bytes) ? -1 : 0;
                        if(!status) at=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,object,0);
                        if(at==MAP_FAILED) { at=nullptr;status=-1; }
                    } else if(bytes && dmem) {
                        auto probe_dmem=symbol<int (*)(size_t,int,int64_t *)>("pw_ps5_dmem_alloc");
                        int64_t offset=-1;
                        status=probe_dmem && !probe_dmem(bytes,0,&offset) ? sceKernelMapDirectMemory(&at,bytes,3,0,offset,16384) : -1;
                        if(status) at=nullptr;
                    }
                    if(at) for(size_t page=0;page<bytes;page+=16384) static_cast<volatile char *>(at)[page]=1;
                    say("Wine: page-table probe %s: %zu MiB %s (status %d errno %d); held until the title ends",probe,bytes>>20,
                        at ? "mapped and touched" : "not mapped",status,status ? errno : 0);
                }
            }
            say("Wine: memory helper %s", memory ? "started: the server's and the broker's shared memory is made there" :
                "missing: the title makes its shared memory");
            if (memory) memory_census = symbol<int (*)(pw_memory_census *)>("pw_ps5_memory_census");
            start_times = symbol<void (*)(uint64_t *, uint64_t *)>("pw_ps5_start_times");
            code_share_duplicates = symbol<uint64_t (*)()>("pw_ps5_code_share_duplicate_bytes");
        }
        // Another process's memory: each helper serves its own (src/pw_memory_service.h), and the server copies
        // through it for ReadProcessMemory and WriteProcessMemory, which it refused for every helper before.
        {
            auto child_memory=symbol<int (*)(int,int,unsigned long long,void *,unsigned int,unsigned int *)>(
                "pw_ps5_child_memory");
            auto set_remote_memory=module_symbol ?
                (void (*)(int (*)(int,int,unsigned long long,void *,unsigned int,unsigned int *)))
                    module_symbol(server,"pw_wineserver_set_remote_memory") : nullptr;
            if(child_memory && set_remote_memory) set_remote_memory(child_memory);
            say("Wine: other processes' memory %s", child_memory && set_remote_memory ?
                "reached through each helper" : "unreachable (an older runtime)");
        }
        // The desktop session restarts its root later through the same means (src/pw_desktop_session.h).
        session_connect=connect_server;session_start_root=start_root;
        session_root_present=symbol<int (*)()>("pw_ps5_root_present");
        session_unload_root=symbol<int (*)()>("pw_ps5_unload_root");
        if(desktop_session && (!session_root_present || !session_unload_root))
            say("Mihawk: this runtime cannot unload the desktop; game mode keeps it loaded");
        const int socket=connect_server((runtime+"/share/wine/nls").c_str());
        if(socket<0) fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL server connection");
        const int launched=start_root(socket,modules.c_str(),prefix.c_str(),cwd,config.argc,config.argv);
        close(socket);
        say("Wine: owner-only server, initial Windows client launch=%d",launched);
        if(launched) fail("PS5_PROTON_OWNER_BOOTSTRAP FAIL initial client");
        if(!owner_graphics) {
            // The pad loop below runs while the Windows children do: they read the title's pad from the input page
            // the title's sink shares with them (wine/ps5/pw_wine_sink.c). Blocking here for the children left that
            // page unwritten (graphics-child-input-dxvk-01). The loop goes back to the launcher once the initial
            // client has ended; back_to_launcher waits for every child and stops the broker.
            owner_result=root_result;owner_wait=wait_children;
        }
    }
    if (steamcmd_session()) {
        int input[2];
        // Before the SteamCMD starts: its console window must never take the display the setup page is drawn on.
        proton_loading_keep_display(true);
        // A login file left by an earlier attempt would sign in before the page; start from a clean page.
        unlink(PROTON_DATA_ROOT "/logs/steamcmd-login.txt");
        if (pipe(input) == 0) {
            steamcmd_stdin = input[1];
            dup2(input[0], STDIN_FILENO);
            say("SteamCMD: the console's input is a pipe");
        } else {
            say("SteamCMD: no input pipe (%s)", strerror(errno));
        }
    }
    if(!owner_bootstrap) status = pw_wine_start_run(&wine, &config, &ops);
    if (status) fail("Wine: thread failed=%d", status);
    // This loop feeds the pad, watches for stop-game and reports: above every
    // Windows thread (they stop at 695, patch 0896), so a game that keeps all
    // CPUs busy never holds up input or the way back to the launcher. It
    // sleeps between ticks, so it takes almost no CPU from the game.
    scePthreadSetprio(pthread_self(), 690);
    struct pad pad{};
    pw_controls controls{};
    const auto controls_path = proton_controls_path();
    const int controls_error = pw_controls_load(&controls, controls_path.c_str());
    if (controls_error && controls_error != ENOENT) say("Controls: could not read saved mappings (%d); using defaults", controls_error);
    say("Controls: left=%s right=%s middle=%s", pw_controls_button_id(controls.button[0]),
        pw_controls_button_id(controls.button[1]), pw_controls_button_id(controls.button[2]));
    pw_controls_mouse mouse{};
    // A USB keyboard and mouse (native/pw_usb_input_ps5.h): loaded here, after the /data grant, which needs the
    // title single-threaded; each load and open is logged, and a device plugged in later is found within 3 s.
    PwUsbInputPs5 usb{};
    {
        int32_t user = -1;
        const int user_status = sceUserServiceGetInitialUser(&user);
        if (user_status < 0) say("usb: no initial user (%#x)", (unsigned)user_status);
        pw_usb_input_ps5_open(&usb, user, [](const char *line) { say("%s", line); });
    }
    bool was_connected = false;
    unsigned stop_tick = 0;
    bool stop_escalated = false;
    // The guided setup's first session (its marker; examples/ps5proton/setup_tour.hpp) ends itself once wineboot has made
    // the prefix: in a new prefix's first session games never get the DualSense (README, "The Mihawk OS prefix"), so
    // no game is opened in it. The launcher then shows the setup's Done step.
    unsigned prefix_made_tick = 0;
    if (setup_preparing) say("setup: the first session; it ends 5 s after the prefix is made");
    // The store-client option's sign-in session (src/pw_steam.h): the client's own window is more than the console's memory
    // holds, so once the sign-in is saved (a remembered user in the client's loginusers.vdf that was not there at the
    // start) the session ends 20 s later, when the client has written the rest of its state, and the launcher shows the
    // client signed in. A session that starts already signed in is left alone.
    // A program that restarts itself ends its first process after starting its second (a store client's first update does,
    // twice): with PROTON_SESSION_FOLLOWS=processes the root's end is the session's end only once the server counts no
    // user process for 5 s, the way Proton waits for the wineserver. Off by default: a game's stray process would hold
    // a session that used to end with its game.
    const char *follow_env = getenv("PROTON_SESSION_FOLLOWS");
    const bool follow_processes = follow_env && !strcmp(follow_env, "processes") && server_user_processes;
    if (follow_env && !follow_processes) say("Wine: PROTON_SESSION_FOLLOWS=%s is not available: the server does not export its user process count", follow_env);
    uint64_t no_processes_since_ms = 0;
    bool follow_said = false;
    auto programs_remain = [&]() {
        if (!follow_processes) return false;
        const uint64_t now = monotonic_ms();
        const int count = server_user_processes();
        if (count > 0) {
            no_processes_since_ms = 0;
            if (!follow_said) {
                follow_said = true;
                say("Wine: the first program ended; the session follows the %d user process(es) that remain", count);
            }
            return true;
        }
        if (!no_processes_since_ms) no_processes_since_ms = now;
        return now - no_processes_since_ms < 5000;
    };
    const char *client_watch_env = getenv("PROTON_STEAM_SIGNIN_WATCH");
    const bool client_watch = client_watch_env && !strcmp(client_watch_env, "1");
    const std::string client_users = prefix + PW_STEAM_LOGINUSERS_REL;
    auto client_signed_in = [&client_users]() {
        char text[65536];
        size_t got = 0;
        if (FILE *file = fopen(client_users.c_str(), "rb")) {
            got = fread(text, 1, sizeof(text), file);
            fclose(file);
        }
        return pw_steam_remembered_user(text, got, nullptr, 0) != 0;
    };
    const bool client_signed_at_start = client_watch && client_signed_in();
    unsigned client_signed_tick = 0;
    // When the sign-in was first seen; then the client is ended once its token is written after it (the stop below).
    time_t client_signed_wall = 0;
    const std::string client_token = prefix + PW_STEAM_LOCAL_VDF_REL;
    auto client_token_written = [&client_token, &client_signed_wall]() {
        struct stat info;
        return stat(client_token.c_str(), &info) == 0 && info.st_mtime + 2 >= client_signed_wall;
    };
    // What the client's own connection log says about the logon, read only from where it stood when the session began (the
    // file keeps earlier sessions). Observation for now: the session still ends 20 s after the sign-in is saved, and the
    // title log says when the logon line came, so the next real sign-in shows which of the two is the better end.
    const std::string client_conn = prefix + PW_STEAM_CONNECTION_LOG_REL;
    long client_conn_start = -1;
    if (client_watch) {
        struct stat info;
        client_conn_start = stat(client_conn.c_str(), &info) == 0 ? (long)info.st_size : 0;
    }
    auto client_logged_on = [&client_conn, &client_conn_start]() {
        char text[16384];
        size_t got = 0;
        if (client_conn_start < 0) return false;
        if (FILE *file = fopen(client_conn.c_str(), "rb")) {
            if (fseek(file, 0, SEEK_END) == 0) {
                const long size = ftell(file);
                const long from = std::max<long>(client_conn_start, size - (long)sizeof(text) + 1);
                if (size > from && fseek(file, from, SEEK_SET) == 0) got = fread(text, 1, sizeof(text) - 1, file);
            }
            fclose(file);
        }
        text[got] = 0;
        return strstr(text, "RecvMsgClientLogOnResponse(): OK") != nullptr || strstr(text, "[Logged On]") != nullptr;
    };
    bool client_logon_said = false, client_cancel_said = false;
    unsigned client_options_tick = 0;
    // The Steam guide's sign-in session: its screens follow the client's update (the package folder's bytes, the processes
    // it starts), its browser, the sign-in and a Steam Guard request; a CROSS on a screen is the guide's, not the client's.
    PwSteamSession guide_session;
    pw_steam_session_begin(&guide_session, steam_guide == 2);
    const std::string guide_package = prefix + "/drive_c/Program Files (x86)/Steam/package";
    unsigned long long guide_bytes = 0;
    std::string guide_persona;
    bool guide_cross = false, guide_guard = false, guide_ended = false;
    uint32_t guide_buttons = 0;   // buttons first pressed over a guide screen: kept from the client until released
    int guide_said = -1;          // the screen the log last named
    // How the session ended, for the launcher's return (the record's result); later results replace earlier ones
    auto guide_record = [&](PwSteamResult result) {
        PwSteamGuideRecord record;
        pw_steam_guide_record_load(&record, PW_STEAM_GUIDE_RECORD);
        record.stage = PW_STEAM_STAGE_SIGNIN;
        record.result = result;
        const int error = pw_steam_guide_record_save(&record, PW_STEAM_GUIDE_RECORD);
        say("steam guide: the session's result is %s%s", pw_steam_guide_result_word(result), error ? " (not saved)" : "");
    };
    // The bytes the client's update has written into its package folder, to two levels
    auto guide_package_bytes = [&guide_package]() {
        unsigned long long total = 0;
        char folder[1024];
        if (!pw_path_nocase(guide_package.c_str(), folder, sizeof(folder))) return total;
        std::vector<std::string> folders{folder};
        for (size_t at = 0; at < folders.size() && at < 64; ++at) {
            DIR *dir = opendir(folders[at].c_str());
            if (!dir) continue;
            while (const struct dirent *entry = readdir(dir)) {
                if (entry->d_name[0] == '.') continue;
                const std::string path = folders[at] + "/" + entry->d_name;
                struct stat info;
                if (stat(path.c_str(), &info) != 0) continue;
                if (S_ISREG(info.st_mode)) total += (unsigned long long)info.st_size;
                else if (S_ISDIR(info.st_mode) && at == 0) folders.push_back(path);
            }
            closedir(dir);
        }
        return total;
    };
    // The client's own connection log, since the session began: a sign-in that needs a Steam Guard code
    auto guide_guard_asked = [&client_conn, &client_conn_start]() {
        char text[16384];
        size_t got = 0;
        if (client_conn_start < 0) return false;
        if (FILE *file = fopen(client_conn.c_str(), "rb")) {
            if (fseek(file, 0, SEEK_END) == 0) {
                const long size = ftell(file);
                const long from = std::max<long>(client_conn_start, size - (long)sizeof(text) + 1);
                if (size > from && fseek(file, from, SEEK_SET) == 0) got = fread(text, 1, sizeof(text) - 1, file);
            }
            fclose(file);
        }
        text[got] = 0;
        return strstr(text, "Logon Denied") || strstr(text, "LogonDenied") || strstr(text, "TwoFactor") ||
               strstr(text, "Two Factor");
    };
    auto client_persona = [&client_users]() {
        char text[65536], persona[96] = {};
        size_t got = 0;
        if (FILE *file = fopen(client_users.c_str(), "rb")) {
            got = fread(text, 1, sizeof(text), file);
            fclose(file);
        }
        return pw_steam_remembered_user(text, got, persona, sizeof(persona)) ? std::string(persona) : std::string();
    };
    if (client_watch) say("Client sign-in: watching for a saved sign-in (%s)", client_signed_at_start ? "already signed in" : "none yet");
    PwDesktopSession session{};
    bool session_begun = false;
    uint64_t session_unload_ms = 0;  // when the shell was asked to end
    // A start of the session's shell, run off this loop (PW_SESSION_STARTING): a spawn takes the startup lock, posts to
    // the payload loader and waits on its rendezvous, and the running game keeps its controller meanwhile only if the
    // loop goes on (a restore took 225-614 ms, step3-focus-2026-10-10-04). The worker owns copies of what it reads.
    struct SessionStart { std::atomic<int> done{0}; int launched = -1; uint64_t begin_ms = 0, took_ms = 0; bool game_alive = false; };
    std::shared_ptr<SessionStart> session_start;
    const unsigned ps_buttons[] = {PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT,PAD_OPTIONS,
        PAD_L3,PAD_R3,PAD_L1,PAD_R1,PAD_CROSS,PAD_CIRCLE,PAD_SQUARE,PAD_TRIANGLE};
#ifdef PS5_GPU_BROKER
    // The program's frames as the console shows them, from the broker's flips (the launcher layout): how many in each
    // 10 s and the longest wait between two, seen from this loop's 8 ms ticks. A freeze of the picture alone, with the
    // sound going on, shows as a long gap.
    unsigned long long shown_flips = 0, shown_at_report = 0;
    uint64_t shown_last_ms = 0, shown_gap_ms = 0;
    unsigned shown_gaps_100 = 0;
#endif
    const unsigned xi_buttons[] = {1,2,4,8,16,64,128,256,512,4096,8192,16384,32768};
    // The console's system memory, which helpers draw on and the title does not (src/pw_memory_guard.h).
    // PROTON_SYSTEM_MEMORY_LIMIT, in MiB, lowers the limit (0 turns the guard off), and PROTON_SYSTEM_MEMORY_END the
    // mark past which a game is ended at once.
    // The marks of the memory plan (src/pw_memory_guard.h): the console's generation from its direct pool and the
    // INSTALLED firmware, not the word an enabler reports (the Pro reported 12.09; volunteers read 99.99 and 11.59).
    // WARN is a notice on the resident census and STOP, the limit, closes on it too; a limit the run sets itself is held to
    // it. A helper's mapped figure is its address space, not memory asked of the kernel (memplan-gate-2026-10-28-01 read
    // 48 GiB), so it is only logged.
    unsigned firmware_reported = 0;
    const unsigned firmware_installed = pw_firmware_installed(&firmware_reported);
    const uint64_t direct_pool = (uint64_t)sceKernelGetDirectMemorySize();
    const pw_console_kind console_kind = pw_console_kind_of(direct_pool);
    const pw_memory_marks marks = pw_memory_marks_for(console_kind, firmware_installed);
    const unsigned default_limit = marks.stop_mib;
    const unsigned swap_notice = marks.warn_mib;
    {
        char installed_text[8] = "?", reported_text[8] = "?";
        pw_firmware_text(firmware_installed, installed_text, sizeof(installed_text));
        pw_firmware_text(firmware_reported, reported_text, sizeof(reported_text));
        say("Wine: firmware %s installed (the system reports %s), a %s (direct pool %llu MiB); the memory guard warns at "
            "%u MiB resident and stops at %u MiB resident (0: no mark), a slow helper start, or a plateau with frame gaps",
            installed_text, reported_text, console_kind == PW_CONSOLE_PRO ? "Pro" : "standard PS5 or Slim",
            (unsigned long long)(direct_pool >> 20), swap_notice, default_limit);
    }
    const char *memory_limit_setting = getenv("PROTON_SYSTEM_MEMORY_LIMIT");
    const char *memory_end_setting = getenv("PROTON_SYSTEM_MEMORY_END");
    const unsigned memory_limit_asked = memory_limit_setting ? (unsigned)strtoul(memory_limit_setting, nullptr, 10)
                                                             : default_limit;
    // PROTON_SYSTEM_MEMORY_LIMIT=0 turns every rule off; a console with no resident mark (default 0) keeps the others.
    const bool guard_on = !(memory_limit_setting && memory_limit_asked == 0);
    // Helpers' code is JIT memory, charged to their resident flexible memory with no cap but this guard, and a limit
    // past the console's ~989 MiB (the earlier emulator experiment's runs asked 1250) is never reached before it swaps: a guard that never acts.
    unsigned memory_limit = default_limit ? std::min(memory_limit_asked, default_limit) : memory_limit_asked;
    if (memory_limit != memory_limit_asked)
        say("Wine: PROTON_SYSTEM_MEMORY_LIMIT=%u MiB is past the guard's %u MiB; using %u MiB", memory_limit_asked,
            default_limit, memory_limit);
    const unsigned memory_end_asked = memory_end_setting ? (unsigned)strtoul(memory_end_setting, nullptr, 10)
                                                         : memory_limit ? pw_memory_guard_default_end_mib(memory_limit) : 0;
    // An end mark past the guard's own is past the console's ~989 MiB before it is reached, and turns the steep-climb
    // rule off. Not the lowered limit plus the margin: the gate's memory-guard case keeps its end out of reach.
    unsigned memory_end = default_limit ? std::min(memory_end_asked, pw_memory_guard_default_end_mib(default_limit))
                                        : memory_end_asked;
    if (memory_end != memory_end_asked)
        say("Wine: PROTON_SYSTEM_MEMORY_END=%u MiB is past the guard's %u MiB; using %u MiB", memory_end_asked,
            pw_memory_guard_default_end_mib(default_limit), memory_end);
    // A shared code copy is counted once: the kernel charges a shared object's physical pages once and leaves 1.5 GiB of extra
    // mappings alone (K19, docs/PLAN.md, the standard PS5 at 13.40, and the Pro at 10.01 on 2026-10-24, below).
    const char *shared_once_setting = getenv("PROTON_CENSUS_SHARED_ONCE");
    // Counted once on every console now (2026-10-24): the Pro's kernel too acts on physical pages. One 64 MiB object mapped again
    // and again, to a per-mapping total of 1175 MiB, moved nothing there (pro-charge-shared-2026-10-24-03), where 64 MiB steps of
    // new pages made it trim other processes and swap (pro-charge-distinct-2026-10-24-05). PROTON_CENSUS_SHARED_ONCE=0 counts every mapping.
    const bool census_shared_once = code_share_duplicates &&
        (shared_once_setting && *shared_once_setting ? strcmp(shared_once_setting, "0") != 0 : true);
    say("Wine: the memory census counts a shared code copy %s", census_shared_once ? "once" : "in every helper that maps it");
    uint64_t census_shared = 0, census_raw_peak = 0;   // what the last census left out; the peak before leaving it out
    // PROTON_MEMORY_OBSERVE=1 (a test's env, never a default): the census runs and logs but no verdict can fire (limit 4000 MiB). The
    // owner asked for a run with no guard at all (2026-10-24); the harness's own brake on the kernel's figures is the only one.
    bool memory_observe = false;   // and the triggers below act on nothing either
    if (const char *observe = getenv("PROTON_MEMORY_OBSERVE"); observe && !strcmp(observe, "1")) {
        memory_observe = true;
        say("Wine: PROTON_MEMORY_OBSERVE=1: the memory guard only observes (it was %u/%u MiB)", memory_limit, memory_end);
        memory_limit = 4000;
        memory_end = 4070;
    }
    // A measurement, never a default: a client sign-in session reads /data/ps5proton/memory-raise ("<limit>:<end>" in MiB) and
    // runs its guard there. The window the client opens after a sign-in climbs past the shipping 870 MiB within seconds and
    // the guard's kill came before the client had saved the sign-in (2026-10-24). Bounds: limit 870..990, end limit..1010, so
    // the steep-climb rule stays armed and the kernel's ~1039 MiB limit stays out of reach of a decision's overshoot.
    if (client_watch) {
        unsigned raise_limit = 0, raise_end = 0;
        if (FILE *raise = fopen(PROTON_DATA_ROOT "/memory-raise", "r")) {
            if (fscanf(raise, "%u:%u", &raise_limit, &raise_end) != 2) raise_limit = raise_end = 0;
            fclose(raise);
            if (raise_limit >= 870 && raise_limit <= 990 && raise_end >= raise_limit && raise_end <= 1010 &&
                raise_end <= raise_limit + PW_MEMORY_GUARD_END_MARGIN_MIB) {
                say("Wine: memory-raise: the guard runs at %u/%u MiB for this client sign-in session (shipping %u/%u)",
                    raise_limit, raise_end, memory_limit, memory_end);
                memory_limit = raise_limit;
                memory_end = raise_end;
            } else {
                say("Wine: memory-raise refused (limit 870..990, end limit..1010, at most +%u): keeping %u/%u MiB",
                    PW_MEMORY_GUARD_END_MARGIN_MIB, memory_limit, memory_end);
            }
        }
    }
    pw_memory_guard memory_guard{};
    pw_memory_guard_init(&memory_guard, memory_limit, memory_end);
    pw_page_table_guard page_table_guard{};   // the system's page tables, a pool apart (src/pw_memory_guard.h)
    pw_memory_census census{}, census_peak{};
    uint64_t census_address_space = 0;   // the helpers' mapped bytes, their address space: logged, never acted on
    bool start_slowed_said = false, plateau_said = false;
    pw_plateau memory_plateau{};   // STOP's plateau trigger (src/pw_memory_guard.h)
    bool census_seen = false, census_missing_said = false, census_stale_said = false, swap_notice_said = false;
    // The hang breaker (src/pw_hang_breaker.h): once a second while the GPU broker shows a game whose processes are all
    // helpers. Its census time is that of the newest census read whole. PROTON_HANG_BREAKER=0 makes it only report.
    // For the console proof of detection, never a game: PROTON_HANG_STALL_MS lowers its 45 s, and
    // PROTON_HANG_BREAKER_TEST_CENSUS_FREEZE_S=N takes the census as frozen N s after it was armed.
    const char *breaker_setting = getenv("PROTON_HANG_BREAKER");
    const bool breaker_acts = !(breaker_setting && !strcmp(breaker_setting, "0"));
    const char *stall_setting = getenv("PROTON_HANG_STALL_MS"), *freeze_setting = getenv("PROTON_HANG_BREAKER_TEST_CENSUS_FREEZE_S");
    const uint32_t breaker_stall_ms = stall_setting ? (uint32_t)strtoul(stall_setting, nullptr, 10) : 0;
    const uint64_t breaker_freeze_ms = freeze_setting ? strtoull(freeze_setting, nullptr, 10) * 1000 : 0;
    uint64_t breaker_census_ms = 0;
    const uint64_t breaker_armed_ms = monotonic_ms();
    bool breaker_said = false;
#ifdef PS5_GPU_BROKER
    const bool breaker_armed = owner_broker_running && programs_in_helpers;
    if (breaker_armed)
        say("Wine: hang breaker armed: a game is ended when the screen shows no new frame for %u ms while the system memory "
            "census is as old%s%s", breaker_stall_ms && breaker_stall_ms < PW_HANG_STALL_MS ? breaker_stall_ms : PW_HANG_STALL_MS,
            !list_helpers ? "; this runtime lists no helpers, so it only reports" : breaker_acts ? "" :
            "; PROTON_HANG_BREAKER=0, so it only reports", breaker_freeze_ms ? "; test: the census is taken as frozen" : "");
#endif
    // The flight recorder (native/proton_flight_recorder.h), for every session of the owner layout.
    if (owner_bootstrap) {
        ProtonFlightSources flight{};
        flight.helpers = list_helpers;
        flight.memory_lock_busy = symbol<int (*)()>("pw_ps5_memory_lock_busy");
        flight.foreground = server_foreground;
        flight.census = memory_census;
        // A wedged broker ends the game from the recorder's thread, which kept running when this loop did not.
        if (programs_in_helpers && breaker_acts) flight.end_helpers = end_helpers;
        flight.suspend_hold = suspend_hold;
        proton_flight_recorder_start(flight);
    }
    // Marked test only: a real PE reads the server-authored snapshot through
    // its own C drive. The production broker will use this copied record too.
    const bool windows_probe = getenv("PROTON_WINDOW_SNAPSHOT_PROBE") &&
        !strcmp(getenv("PROTON_WINDOW_SNAPSHOT_PROBE"), "1");
    const std::string windows_request = prefix + "/drive_c/.pw-window-request";
    const std::string windows_reply = prefix + "/drive_c/.pw-window-reply";
    const std::string windows_temp = windows_reply + ".tmp";
    uint32_t windows_nonce = 0;
    if (windows_probe) { unlink(windows_request.c_str());unlink(windows_reply.c_str());unlink(windows_temp.c_str()); }
    auto set_suspend = symbol<void (*)(uint32_t, uint32_t)>("pw_wine_set_suspend");
    bool system_overlaid = false;
    for (unsigned tick = 0;; ++tick) {
        // Suspend and resume (src/pw_suspend.h): the system's word, every fourth tick, and the clock's gap between two
        // ticks for a suspend nobody announced. The helpers read away and the beat from their input pages, the broker
        // holds new GPU work while away, and nothing is ended while away or for the grace after.
        {
            bool overlaid = system_overlaid;
            const int away = tick % 4 == 0 ? system_away(&overlaid) : -1;
            if (overlaid != system_overlaid) {
                system_overlaid = overlaid;
                say("system: the system's overlay is %s", overlaid ? "up" : "down");
            }
            const uint64_t at = monotonic_ms(), before = title_suspend.last_ms;
            const bool was_away = title_suspend.away;
            const int change = pw_suspend_tick(&title_suspend, at, away);
            if (change != PW_SUSPEND_NONE) {
                char line[192];
                if (change == PW_SUSPEND_LEFT)
                    snprintf(line, sizeof(line), "suspend left: the app is in the background; GPU work is held and drained, "
                             "nothing is ended until %u ms after it returns", (unsigned)PW_SUSPEND_GRACE_MS);
                else if (change == PW_SUSPEND_BACK)
                    snprintf(line, sizeof(line), "suspend back: the app is in the foreground after %llu ms",
                             (unsigned long long)(at - title_suspend.left_ms));
                else
                    snprintf(line, sizeof(line), "suspend %s: the clock jumped %llu ms between two ticks",
                             was_away ? "announced" : "unannounced", (unsigned long long)(at - before));
                say("%s (suspends %u, unannounced %u)", line, title_suspend.suspends, title_suspend.unannounced);
                proton_flight_note(line);
                if (title_log_fd >= 0) fsync(title_log_fd);
            }
            suspend_away.store(title_suspend.away, std::memory_order_relaxed);
            suspend_resumed_ms.store(title_suspend.resumed_ms, std::memory_order_relaxed);
            if (set_suspend) set_suspend(title_suspend.away, tick + 1);
#ifdef PS5_GPU_BROKER
            proton_gpu_broker_set_away(title_suspend.away);
#endif
        }
        // The client has been started (start_root above): the phase it was launched for is now real, not "Starting Wine".
        static bool aurelia_phase_set;
        if (aurelia_session() && !aurelia_phase_set) {
            aurelia_phase_set = true;
            aurelia_phase_from_arguments(game.arguments);
        }
        if (aurelia_session() && tick % 30 == 0) aurelia_poll(); // the client's lines, four times a second
        if (steamcmd_session() && tick % 30 == 0) steamcmd_poll(); // the runtime's console lines, four times a second
        // The sign-in overlay's session ends when the login was confirmed (aurelia_line sets the deadline).
        if (aurelia_session() && aurelia_end_ms && monotonic_ms() >= aurelia_end_ms) {
            aurelia_end_ms = 0;
            say("Aurelia: signed in; ending the session");
            end_game("the Aurelia sign-in finished", false);
        }
        // Another thread (the flight recorder) ended the game's helpers: the ordered way back, as end_game takes.
        if (ending.load(std::memory_order_acquire)) back_to_launcher();
        // A test run's notice, to see whether a notification shows over a full-screen game: PROTON_TEST_NOTICE=game-mode
        // as soon as the broker flips a game's frames directly, or =<s> that many seconds into the loop.
        if (const char *test = getenv("PROTON_TEST_NOTICE")) {
            bool due = tick && tick == 120u * strtoul(test, nullptr, 10);
#ifdef PS5_GPU_BROKER
            due = due || (!strcmp(test, "game-mode") && proton_gpu_broker_game_mode() > 0);
#endif
            if (due) notice_set(NOTICE_TEST, "A test notice: notices show here.");
        }
        notices_post();
        if (windows_probe && tick % 6 == 0) {
            uint32_t nonce = 0;
            if (FILE *request = fopen(windows_request.c_str(), "rb")) {
                if (fread(&nonce, sizeof(nonce), 1, request) != 1) nonce = 0;
                fclose(request);
            }
            if (nonce && nonce != windows_nonce) {
                PwWindowProbeReply reply{};reply.nonce = nonce;
                reply.status = server_windows ? (uint32_t)server_windows(&reply.snapshot,sizeof(reply.snapshot)) : 1;
                if (FILE *file = fopen(windows_temp.c_str(), "wb")) {
                    const bool written = fwrite(&reply,sizeof(reply),1,file) == 1;
                    const bool closed = fclose(file) == 0;
                    if (written && closed && !rename(windows_temp.c_str(),windows_reply.c_str())) {
                        windows_nonce = nonce;
                        const PwWindowRow *root = reply.snapshot.count && reply.snapshot.count <= PW_WINDOW_LIMIT ?
                            &reply.snapshot.windows[reply.snapshot.count-1] : nullptr;
                        say("Wine: window snapshot nonce=%u sequence=%llu count=%u overflow=%u status=%u root_hwnd=%#x root_pid=%d root_winpid=%#x",
                            nonce,(unsigned long long)reply.snapshot.sequence,reply.snapshot.count,reply.snapshot.overflow,reply.status,
                            root ? root->hwnd : 0,root ? root->pid : 0,root ? root->windows_pid : 0);
                    }
                }
            }
        }

        // Bounded headless lifecycle probe: replace this owner while eight Windows
        // children still wait on events. This deliberately skips owner_wait, so
        // descriptor EOF must reap the payloads.
        const char *owner_close=getenv("PROTON_PAYLOAD_TEST_OWNER_CLOSE");
        if(owner_bootstrap && !owner_broker_running && owner_close && !strcmp(owner_close,"1") && tick%120==0) {
            const char *marker="/data/ps5proton/prefixes/probe/drive_c/payload-owner-ready";
            if(access(marker,F_OK)==0) {
                unlink(marker);
                say("PS5_PROTON_PAYLOAD_OWNER_CLOSE PASS children_ready=8");
                log_line("payload owner-close: eight children alive; replacing the owner");
                if(log_fd>=0)fsync(log_fd);
                proton_restart(nullptr,proton_launch().cycle+1,"payload-owner-close");
                catchReturnFromMain(1);
            }
        }
        if(owner_result && !graphics_claimed.load() && owner_result()>=0 && !programs_remain()) back_to_launcher();
#ifdef PS5_GPU_BROKER
        if (owner_broker_running) {
            const unsigned long long flips = proton_gpu_broker_flips();
            if (flips != shown_flips) {
                const uint64_t now = monotonic_ms();
                if (shown_last_ms) {
                    shown_gap_ms = std::max<uint64_t>(shown_gap_ms, now - shown_last_ms);
                    shown_gaps_100 += now - shown_last_ms >= 100;
                }
                shown_flips = flips;shown_last_ms = now;
            }
        }
#endif
        pad_poll(&pad);
        pad_script.apply(pad);
        // Over a guide screen the pad drives the screen alone: CROSS is the guide's, and every button pressed while the
        // screen shows stays away from the client until it is let go (a press must not click what cannot be seen)
        struct pad shown_pad = pad;
        if (steam_guide) {
            const bool covering = guide_screens && guide_session.screen != PW_STEAM_SCREEN_NONE && !guide_ended;
            if (covering && (pad.pressed & PAD_CROSS)) guide_cross = true;
            if (covering) guide_buttons |= (pad.held | pad.pressed) & ~PAD_INTERCEPTED;   // the shell's mark stays visible
            guide_buttons &= pad.held;
            shown_pad.held &= ~guide_buttons;
            shown_pad.pressed &= ~guide_buttons;
            if (covering) {
                shown_pad.left_x = shown_pad.left_y = shown_pad.right_x = shown_pad.right_y = 0.0f;
                shown_pad.l2 = shown_pad.r2 = 0.0f;
                shown_pad.touch_count = 0;
            }
        }
        if (steamcmd_session() && pad.pressed) {
            const uint32_t pressed = pad.pressed;
            if (pressed & PAD_UP) steamcmd_key(PW_STEAMCMD_KEY_UP);
            if (pressed & PAD_DOWN) steamcmd_key(PW_STEAMCMD_KEY_DOWN);
            if (pressed & PAD_CROSS) steamcmd_key(PW_STEAMCMD_KEY_ENTER);
            if (pressed & PAD_CIRCLE) steamcmd_key(PW_STEAMCMD_KEY_ESCAPE);
        }
        struct pad gamepad{};
        pw_controls_gamepad(&controls, &shown_pad, &gamepad);
        PwWinePad state{};
        state.connected = gamepad.connected;
        for (size_t i = 0; i < sizeof(ps_buttons)/sizeof(ps_buttons[0]); ++i)
            if (gamepad.held & ps_buttons[i]) state.buttons |= xi_buttons[i];
        state.left_trigger = std::clamp(gamepad.l2, 0.f, 1.f) * 255;
        state.right_trigger = std::clamp(gamepad.r2, 0.f, 1.f) * 255;
        state.thumb_lx = std::clamp(shown_pad.left_x, -1.f, 1.f) * 32767;
        state.thumb_ly = -std::clamp(shown_pad.left_y, -1.f, 1.f) * 32767;
        state.thumb_rx = std::clamp(shown_pad.right_x, -1.f, 1.f) * 32767;
        state.thumb_ry = -std::clamp(shown_pad.right_y, -1.f, 1.f) * 32767;
        if (set_pad) set_pad(&state);
        uint8_t report[64];
        proton_dualsense_report(&gamepad, tick, report);
        if (set_dualsense) set_dualsense(pad.connected ? report : nullptr);
        if (tick == 0 || pad.pressed || (pad.connected != was_connected))
            say("Wine: pad connected=%d buttons=%#x touch=%u", pad.connected, pad.held, pad.touch_count);
        was_connected = pad.connected;
        PwWineInput events[4];
        const unsigned event_count = pw_controls_update(&controls, &mouse, &shown_pad, events);
        for (unsigned i = 0; i < event_count; ++i) deliver(&events[i]);
        PwWineInput usb_events[PW_USB_KEYBOARD_EVENTS + PW_USB_MOUSE_EVENTS];
        const unsigned usb_count = pw_usb_input_ps5_poll(&usb, tick, usb_events,
                                                         sizeof(usb_events) / sizeof(usb_events[0]));
        for (unsigned i = 0; i < usb_count; ++i) {
            deliver(&usb_events[i]);
            if (steamcmd_session() && usb_events[i].type == PW_WINE_INPUT_KEY && usb_events[i].down)
                steamcmd_handle_key(usb_events[i].code, (usb.keys.modifiers & 0x22) != 0);
        }
        // Diagnostics request a displayed image after their pixels are ready.
        // Reuse numbered capture ownership, independently of timed pad scripts.
        if (tick % 120 == 0) {
            const char *path=getenv("WINE_PS5_CAPTURE");
            if (path) {
                const std::string request=std::string(path)+".broker-request";
                if (unlink(request.c_str())==0) pad_script.capture(monotonic_ms(),0);
            }
            pad_script.collect(monotonic_ms());
        }
        // stop-game asks the game to end as a user would, and the game ends
        // through its own exit, which closes its presenter (patch 0892) and
        // returns to the launcher. Only a game that does not close is
        // ended, and only one that cannot be ended either is left. The
        // desktop session logs off as Start's Power does (patch 1214): a
        // close to the foreground window reached one of Explorer's own and
        // the session went on (2026-10-18; record removed from the tree).
        if (tick % 240 == 0 && title_log_fd >= 0) fsync(title_log_fd);
        if (setup_preparing && tick % 120 == 0 && !stop_tick) {
            char stamp[128] = {};
            if (FILE *file = !prefix_made_tick ? fopen((prefix + "/.update-timestamp").c_str(), "r") : nullptr) {
                fread(stamp, 1, sizeof(stamp) - 1, file);
                fclose(file);
                if (pw_setup_prefix_made(stamp)) {
                    prefix_made_tick = tick;
                    say("setup: the prefix is made (%s)", stamp);
                    proton_cover_ready();
                }
            }
            if (prefix_made_tick && tick - prefix_made_tick >= 600)
                if (FILE *stop = fopen(PROTON_DATA_ROOT "/stop-game", "w")) fputs("setup-prepared\n", stop), fclose(stop);
        }
        // A client sign-in session can be left with the pad: hold OPTIONS for two seconds. The window is the program's, so no
        // screen of ours carries a button for it (the Accounts row's text says so).
        if (client_watch && !client_signed_at_start && !stop_tick && !client_cancel_said) {
            if (pad.held & PAD_OPTIONS) {
                if (!client_options_tick) client_options_tick = tick ? tick : 1;
                if (tick - client_options_tick >= 240) {
                    client_cancel_said = true;
                    say("Client sign-in: cancelled with OPTIONS held for 2 s");
                    if (steam_guide == 2) guide_ended = true, guide_record(pw_steam_session_ended(&guide_session, PW_STEAM_RESULT_CANCELLED));
                    if (FILE *stop = fopen(PROTON_DATA_ROOT "/stop-game", "w")) fputs("client-sign-in-cancelled\n", stop), fclose(stop);
                }
            } else {
                client_options_tick = 0;
            }
        }
        if (client_watch && !client_signed_at_start && tick % 120 == 0 && !stop_tick) {
            // Nobody there: the window costs the memory of the sign-in page and is of no use unattended. 30 minutes covers the
            // first start's updates (about 5.5 minutes) and a slow scan.
            if (tick >= 30u * 60u * 120u) {
                say("Client sign-in: no sign-in after 30 minutes; ending the session");
                if (steam_guide == 2) guide_ended = true, guide_record(pw_steam_session_ended(&guide_session, PW_STEAM_RESULT_TIMEOUT));
                if (FILE *stop = fopen(PROTON_DATA_ROOT "/stop-game", "w")) fputs("client-sign-in-timeout\n", stop), fclose(stop);
            }
            if (!client_logon_said && client_logged_on()) {
                client_logon_said = true;
                say("Client sign-in: the client's connection log shows a logon, %u s into the session", tick / 120);
            }
            if (!client_signed_tick && client_signed_in()) {
                // The session ends at once (the owner, 2026-10-26: Steam kept running 48 s under the guide's last screen).
                // In the owner's run the token was written 26 s or more after loginusers.vdf, so the stop below waits for it.
                client_signed_tick = tick ? tick : 1;
                client_signed_wall = time(nullptr);
                say("Client sign-in: saved, %u s into the session; closing the client", tick / 120);
                if (steam_guide != 2)   // a guide session ends from its own tick, which shows its screen first
                    if (FILE *stop = fopen(PROTON_DATA_ROOT "/stop-game", "w")) fputs("client-signed-in\n", stop), fclose(stop);
            }
        }
        if (!showcase.empty() && tick % 120 == 0 && !stop_tick) {
            static const PwSteamScreen screens[] = {PW_STEAM_SCREEN_INSTALLING, PW_STEAM_SCREEN_UPDATING, PW_STEAM_SCREEN_SLOW,
                                                    PW_STEAM_SCREEN_SCAN, PW_STEAM_SCREEN_SIGNED_IN};
            static const char *const names[] = {"installing", "updating", "slow", "scan", "signed-in"};
            const unsigned item = tick / 120 / 3, phase = tick / 120 % 3;
            if (item >= showcase.size() * 5) {
                // Three seconds after the last capture: each picture asked for is on disk, or the verdict says how many are not
                if (item == showcase.size() * 5 && phase == 2) {
                    const char *path = getenv("WINE_PS5_CAPTURE");
                    const std::string base = path ? std::string(path).substr(0, std::string(path).rfind(".ppm")) : std::string();
                    unsigned saved = 0;
                    for (const unsigned index : showcase_captures) {
                        char suffix[16];
                        snprintf(suffix, sizeof(suffix), "-%02u.ppm", index);
                        struct stat info;
                        saved += path && stat((base + suffix).c_str(), &info) == 0 && info.st_size > 0;
                    }
                    say("PS5_PROTON_STEAM_GUIDE_SHOWCASE %s captures=%u of %zu languages=%zu", saved == showcase.size() * 5 ? "PASS" : "FAIL",
                        saved, showcase.size() * 5, showcase.size());
                }
                if (item > showcase.size() * 5 && phase == 0)
                    if (FILE *stop = fopen(PROTON_DATA_ROOT "/stop-game", "w")) fputs("steam-guide-showcase\n", stop), fclose(stop);
            } else if (phase == 0) {
                if (item && item % 5 == 0) {
                    log_line(proton_guide_stop("the showcase's next language"));
                    proton_guide_start(showcase[item / 5]->tag, proton_app_root(), proton_gpu_broker_set_cover);
                }
                proton_guide_show(screens[item % 5], 134, 212000000ull, "Mihawk");
            } else if (phase == 2) {
                pad_script.capture(monotonic_ms(), 0);
                showcase_captures.push_back(pad_script.capture_index);
                say("steam guide showcase: capture %02u is %s in %s", pad_script.capture_index, names[item % 5], showcase[item / 5]->tag);
            }
        }
        if (steam_guide && !stop_tick && !guide_ended && (tick % 120 == 0 || guide_cross)) {
            PwSteamSessionInput input{};
            input.seconds = tick / 120;
            if (steam_guide == 2 && tick % 600 == 0) guide_bytes = guide_package_bytes();
            if (steam_guide == 2 && tick % 600 == 0 && !guide_guard && guide_guard_asked()) {
                guide_guard = true;
                say("steam guide: the client's connection log asks for a Steam Guard code");
                guide_record(PW_STEAM_RESULT_GUARD);   // the launcher explains the code, unless a sign-in comes after
            }
            input.package_bytes = guide_bytes;
            input.processes = guide_processes.load(std::memory_order_relaxed);
            input.browser = guide_browser.load(std::memory_order_relaxed);
            input.signed_in = steam_guide == 2 && !client_signed_at_start && client_signed_in();
            input.guard = guide_guard;
            input.cross = guide_cross;
            guide_cross = false;
            const int result = pw_steam_session_tick(&guide_session, &input);
            if (input.signed_in && guide_persona.empty()) guide_persona = client_persona();
            if (int(guide_session.screen) != guide_said) {
                guide_said = int(guide_session.screen);
                say("steam guide: screen %d at %u s (%llu package bytes, %u processes, browser %d)", guide_said, input.seconds,
                    guide_bytes, input.processes, input.browser);
            }
            proton_guide_show(guide_session.screen, input.seconds, guide_bytes, guide_persona.c_str());
            if (result >= 0) {
                guide_ended = true;
                if (result == PW_STEAM_RESULT_SIGNED_IN && !client_signed_wall) client_signed_wall = time(nullptr);
                guide_record((PwSteamResult)result);
                if (FILE *stop = fopen(PROTON_DATA_ROOT "/stop-game", "w"))
                    fprintf(stop, "steam-guide-%s\n", pw_steam_guide_result_word((PwSteamResult)result)), fclose(stop);
            }
        }
        if (tick % 120 == 0 && !stop_tick && access(PROTON_DATA_ROOT "/stop-game", F_OK) == 0) {
            unlink(PROTON_DATA_ROOT "/stop-game");
            stop_tick = tick;
            PwWineInput close{};
            const bool log_off = desktop_session && server_input;  // the in-title sink knows no LOG_OFF
            close.type = log_off ? PW_WINE_INPUT_LOG_OFF : PW_WINE_INPUT_CLOSE;
            say("Wine: stop requested; asking the game to close%s (%d)",
                log_off ? ": the desktop session logs off" : "", deliver(&close));
        }
        // After a sign-in the client ignores the close and is not hung: no snapshot (16 s of klog on 2026-10-26), and it is
        // ended once its token is on disk, checked each second from the close, or 25 s after it.
        const unsigned stop_age = stop_tick ? tick - stop_tick : 0;
        if (stop_tick && !stop_escalated && stop_age % 120 == 0 &&
            (client_signed_wall ? stop_age >= 3000 || (stop_age >= 120 && client_token_written()) : stop_age >= 1200)) {
            stop_escalated = true;
            // The close request reaches only a thread that pumps messages. What every thread waits on is recorded first,
            // then a game in helpers is ended by SIGKILL to them (end_game), which needs nothing of the game's threads or
            // the server. A game in the title is ended by the server only when asked (PROTON_STOP_TERMINATE=1): the
            // server kills every thread, but the HID bus thread does not end, so the game stays up without its threads
            // (a state the console did not survive on 2026-10-03).
            // The sink counts only what the in-title sink posted; the launcher layout's input goes to the server.
            PwWineSinkStats stats{};
            if (sink_stats) sink_stats(&stats);
            char inputs[80];
            if (server_input) snprintf(inputs, sizeof(inputs), "the close went to the server");
            else snprintf(inputs, sizeof(inputs), "inputs delivered %llu of %llu posted",
                          (unsigned long long)stats.inputs_delivered, (unsigned long long)stats.inputs_posted);
            if (client_signed_wall)
                say("Client sign-in: the client's token %s, %u s after the close", client_token_written() ? "is written" :
                    "was not written", stop_age / 120);
            else dump_hang(10);
            if (programs_in_helpers && list_helpers) {
                say("Wine: the game did not close in %u s (%s); ending its helpers by SIGKILL, not through the server",
                    stop_age / 120, inputs);
                end_game("stop", true);
            } else if (server_terminate_asked()) {
                const int asked = terminate_game ? terminate_game() : -1;
                say("Wine: the game did not close in 10 s (%s); ending it through the server (%d)", inputs, asked);
            } else {
                say("Wine: the game did not close in 10 s (%s); waiting for it (PROTON_STOP_TERMINATE=1 ends it "
                    "through the server)", inputs);
            }
        }
#ifdef PS5_GPU_BROKER
        // The desktop session (src/pw_desktop_session.h), every 100 ms: game mode step 3 unloads Explorer while a
        // full-screen game holds the screen, and starts it again afterwards.
        if (desktop_session && owner_broker_running && session_root_present && session_unload_root &&
            session_connect && session_start_root && tick % 12 == 0) {
            const uint64_t now = monotonic_ms();
            if (!session_begun) { pw_desktop_session_begin(&session, now); session_begun = true; }
            PwSessionState state{};
            state.now_ms = now;
            const int game = proton_gpu_broker_game_mode();
            state.game_mode = game > 0;
            state.game_pid = game;
            const int watched = game > 0 ? game : session.game_pid;
            state.game_alive = watched > 0 && !(kill(watched, 0) < 0 && errno == ESRCH);
            state.shell_present = session_root_present();
            state.stopping = stop_tick != 0;
            // A start that finished since the last tick.
            if (session_start && session_start->done.load(std::memory_order_acquire)) {
                const int launched = session_start->launched;
                pw_desktop_session_started(&session, !launched, now);
                say("Mihawk: desktop restored (%d) in %llu ms, after %s; unloads=%u starts=%u", launched,
                    (unsigned long long)session_start->took_ms,
                    session_start->game_alive ? "the game left game mode" : "the game's process ended", session.unloads,
                    session.starts);
                if (launched)
                    say("Mihawk: the desktop could not start again (%u in a row); trying again in %u ms",
                        session.failed_starts, (unsigned)PW_SESSION_RETRY_MS);
                session_start.reset();
            }
            const PwSessionPhase before = session.phase;
            const unsigned kept_before = session.kept;
            const PwSessionAction action = pw_desktop_session_tick(&session, &state);
            // What the shell did since the last tick, before acting on this one (a start can follow at once).
            if (before == PW_SESSION_UNLOADING && session.kept != kept_before)
                say("Mihawk: the desktop did not end in %u ms and stays until game mode has a break (kept=%u)",
                    (unsigned)PW_SESSION_UNLOAD_LIMIT_MS, session.kept);
            else if (before == PW_SESSION_UNLOADING && session.phase != PW_SESSION_UNLOADING)
                say("Mihawk: desktop unloaded in %llu ms; unloads=%u",
                    (unsigned long long)(now - session_unload_ms), session.unloads);
            switch (action) {
            case PW_SESSION_UNLOAD:
                session_unload_ms = now;
                say("Mihawk: desktop unloading: the full-screen game in process %d held game mode for %u ms (%d)",
                    game, (unsigned)PW_SESSION_UNLOAD_MS, session_unload_root());
                break;
            case PW_SESSION_START: {
                auto job = std::make_shared<SessionStart>();
                job->begin_ms = now;
                job->game_alive = state.game_alive;
                session_start = job;
                std::thread([job, nls = runtime + "/share/wine/nls", loaded = modules, root = prefix,
                             directory = std::string(cwd)]() {
                    const char *shell[PW_WINE_START_MAX_ARGS + 1];
                    const int words = pw_desktop_session_argv(shell, sizeof(shell) / sizeof(shell[0]), "wine", nullptr, 0);
                    const int socket = session_connect(nls.c_str());
                    job->launched = socket >= 0 && words > 0 ?
                        session_start_root(socket, loaded.c_str(), root.c_str(), directory.c_str(), words, shell) : -1;
                    if (socket >= 0) close(socket);
                    job->took_ms = monotonic_ms() - job->begin_ms;
                    job->done.store(1, std::memory_order_release);
                }).detach();
                break;
            }
            case PW_SESSION_END:
                session_result = session.end_status ? 1 : 0;
                if (session.end_status)
                    fail("Mihawk: the session ends: the desktop could not start again (%u in a row) and the game has closed",
                         session.failed_starts);
                say("Mihawk: the session ends: stopped with the desktop unloaded, and the game has closed");
                back_to_launcher();
                break;
            default:
                break;
            }
        }
#endif
        // The registry goes to disk every 30 s, as it would on Windows, where a program's setup is kept even when the
        // session is cut short. The wineserver saves only when its last process ends (patch 0886 adds the title's
        // request), so a session that was ended from outside (the memory guard, the PS button, a crash) lost
        // everything it wrote: a launcher ran its DirectX and Visual C++ install scripts again in the next session
        // because their markers had never reached the disk (runprocess_log.txt: two full runs, 2026-10-10 21:00 and
        // 21:22). A clean registry is skipped by the server, so the request costs nothing when nothing changed.
        if (tick && tick % 1250 == 0) title_vm_census_say();
        if (tick == 2500 && getenv("PROTON_PT2M_TITLE_PROBE") && !strcmp(getenv("PROTON_PT2M_TITLE_PROBE"), "1"))
            title_pt2m_probe();
        if (tick && tick % 3750 == 0 && flush_registry) {
            const uint64_t begin = sceKernelGetProcessTime();
            const int status = flush_registry(3000);
            const uint64_t took_ms = (sceKernelGetProcessTime() - begin) / 1000;
            if (status || took_ms >= 250)
                say("Wine: registry %s in %llu ms (every 30 s)", status ? "NOT saved" : "saved",
                    (unsigned long long)took_ms);
        }
        // The census is read every 100 ms, guard or not (its page tables change that often; its processes every half
        // second): the hang breaker goes by its age. A read that fails (the
        // helper was writing it) keeps the last whole one; the copy it was given may be torn (src/pw_memory_helper.c).
        bool census_read = false;
        if (tick % 12 == 0) {
            pw_memory_census fresh{};
            census_read = memory_census && !memory_census(&fresh);
            if (census_read) {
                census = fresh;
                census_raw_peak = std::max<uint64_t>(census_raw_peak, census.system);
                // Never more than the helpers' own figures: what is left out is a helper's mapping.
                census_shared = census_shared_once ? std::min<uint64_t>(code_share_duplicates(), census.helpers) : 0;
                census.system -= census_shared;
                census.helpers -= census_shared;
                census.helpers_mapped -= std::min<uint64_t>(census_shared, census.helpers_mapped);
                census_address_space = census.helpers_mapped;
                if (!breaker_freeze_ms || monotonic_ms() - breaker_armed_ms < breaker_freeze_ms) breaker_census_ms = census.taken_ms;
            }
        }
        // A game whose helpers hold the console's system memory past the limit swaps the console until it stops
        // answering, so it is asked to close as a user would, and ended at once if it climbs on (src/pw_memory_guard.h).
        if (tick % 12 == 0 && guard_on) {
            if (!census_read) {
                if (!census_missing_said && tick >= 1200 && !census_seen) {
                    census_missing_said = true;
                    say("Wine: no census of the console's system memory (errno %d): its guard is off",
                        memory_census ? errno : ENOENT);
                }
            } else {
                const uint64_t age = monotonic_ms() - census.taken_ms;
                if (!census_seen) {
                    say("Wine: system memory %llu MiB in %u processes at the start; a game is asked to close when its "
                        "helpers hold it at %u MiB for %u of %u s, and ended when they hold it past %u MiB for %u s; "
                        "page tables %u free of %u: asked to close at %u free, ended at %u",
                        (unsigned long long)(census.system >> 20), census.processes, memory_limit,
                        PW_MEMORY_GUARD_CLOSE_MS / 1000, PW_MEMORY_GUARD_WINDOW_MS / 1000, memory_end,
                        PW_MEMORY_GUARD_END_MS / 1000, census.page_tables_free, census.page_tables,
                        PW_PAGE_TABLE_CLOSE_FREE, PW_PAGE_TABLE_END_FREE);
                }
                // Part 4 of the memory plan: the user's payloads take the same memory as a game's helpers (the volunteers' 63-267
                // MiB, one console's single payload.elf 158 MiB), so the session starts by saying what closing them would free.
                if (!census_seen) {
                    say("Wine: payloads use %llu MiB in %u processes at the start", (unsigned long long)(census.payloads >> 20),
                        census.payload_count);
                    if (FILE *file = fopen(PROTON_PAYLOADS_FILE, "w")) {
                        fprintf(file, "%llu\n", (unsigned long long)(census.payloads >> 20));
                        fclose(file);
                    }
                    if ((census.payloads >> 20) >= 64)
                        notice_set(NOTICE_PAYLOADS, "Payloads are using %llu MiB; closing them frees room.",
                                   (unsigned long long)(census.payloads >> 20));
                }
                census_seen = true;
                if (!swap_notice_said && (census.system >> 20) >= swap_notice) {
                    swap_notice_said = true;
                    notice_set(NOTICE_MEMORY_SWAP, "The PS5's memory is getting full: games may slow down as it starts to swap.");
                    say("Wine: the console's system memory reached %u MiB (now %llu MiB; helpers %llu MiB in %u; their address "
                        "space %llu MiB): it starts to swap from here; only a notice, nothing is closed below %u MiB", swap_notice,
                        (unsigned long long)(census.system >> 20), (unsigned long long)(census.helpers >> 20),
                        census.helper_count, (unsigned long long)(census_address_space >> 20), memory_limit);
                }
                if (census.system > census_peak.system) census_peak = census;
                if (age > 5000 && !census_stale_said)
                    say("Wine: the system memory census is %llu ms old (status %d)", (unsigned long long)age,
                        census.status);
                census_stale_said = age > 5000;
                // A suspend is not time at the limit (src/pw_suspend.h): the guard's windows start again at each return,
                // and it decides nothing while the app is away or just back.
                static uint64_t guard_rearmed_for;
                if (title_suspend.resumed_ms != guard_rearmed_for) {
                    guard_rearmed_for = title_suspend.resumed_ms;
                    pw_memory_guard_rearm(&memory_guard);
                }
                const bool held = pw_suspend_hold(&title_suspend, monotonic_ms());
                pw_memory_verdict verdict = held ? PW_MEMORY_FINE : pw_memory_guard_sample(&memory_guard, pw_memory_census_guarded(&census), census.taken_ms);
                // STOP's second trigger: at WARN or past it, a helper start 3 times slower than the session's first.
                uint64_t start_first = 0, start_last = 0;
                if (start_times) start_times(&start_first, &start_last);
                bool by_start = false, by_plateau = false;
                // STOP's third trigger: resident held at its plateau, past WARN, for 60 s with a frame gap over 1 s in it.
                const uint64_t frame_gap = shown_last_ms ? monotonic_ms() - shown_last_ms : 0;
                if (!held && !memory_observe && pw_plateau_sample(&memory_plateau, census.system >> 20, census.taken_ms, frame_gap, swap_notice) &&
                    verdict == PW_MEMORY_FINE && !plateau_said) {
                    plateau_said = by_plateau = true;
                    verdict = PW_MEMORY_CLOSE;
                    say("Wine: resident memory held at %llu MiB (within %u of its plateau, past %u MiB) for %u s with frame gaps "
                        "over %u ms: stopping as at the memory mark", (unsigned long long)(census.system >> 20),
                        PW_PLATEAU_BAND_MIB, swap_notice, PW_PLATEAU_HOLD_MS / 1000, PW_PLATEAU_GAP_MS);
                }
                if (!held && !memory_observe && verdict == PW_MEMORY_FINE && !start_slowed_said && (census.system >> 20) >= swap_notice &&
                    pw_memory_start_slowed(start_first, start_last)) {
                    start_slowed_said = by_start = true;
                    verdict = PW_MEMORY_CLOSE;
                    say("Wine: a helper start took %llu ms against the session's first %llu ms at %llu MiB resident: the "
                        "console is swapping hard; stopping as at the memory mark", (unsigned long long)start_last,
                        (unsigned long long)start_first, (unsigned long long)(census.system >> 20));
                }
                if (verdict != PW_MEMORY_FINE)
                    say("Wine: memory guard %s: resident %llu MiB, helpers' address space %llu MiB (warn %u MiB, stop %u MiB "
                        "resident)", verdict == PW_MEMORY_CLOSE ? "stop: closing" : "stop: ending",
                        (unsigned long long)(census.system >> 20), (unsigned long long)(census_address_space >> 20), swap_notice,
                        memory_limit);
                // The page tables run out under the memory limit (Steam's browser in many processes, 2026-10-28): the
                // stronger verdict of the two is acted on, and the log names the page tables when they gave it.
                // A reading the guard cannot use is said once (src/pw_memory_guard.h, pw_page_table_known).
                static bool page_tables_unknown_said;
                if (!page_tables_unknown_said && !pw_page_table_known(census.page_tables, census.page_tables_free)) {
                    page_tables_unknown_said = true;
                    say("Wine: page tables unknown (free %u of total %u): the page-table guard does nothing until they read "
                        "sound", census.page_tables_free, census.page_tables);
                }
                const pw_memory_verdict tables = held ? PW_MEMORY_FINE :
                    pw_page_table_sample(&page_table_guard, census.page_tables, census.page_tables_free, monotonic_ms());
                const bool by_tables = tables > verdict;
                if (by_tables) verdict = tables;
                char tables_why[96];
                snprintf(tables_why, sizeof(tables_why), "page tables were down to %u free of %u%s", census.page_tables_free,
                         census.page_tables, page_table_guard.steep ? ", on course for 512 within 2 s" : "");
                if (verdict != PW_MEMORY_FINE && desktop_session && census.largest_pid > 0) {
                    // The desktop session is the user's whole Windows: the guard acts on the process that holds the
                    // memory, not the session (user-desktop-2026-10-10-01: a launcher's GPU process reached 462 MiB and
                    // the session ended). Close asks its windows to close, or ends a process with none (Chromium
                    // starts a new GPU process); end ends it. Then the guard starts over for the next one.
                    PwWineInput end{};
                    end.type = PW_WINE_INPUT_END_PROCESS;
                    end.code = (uint32_t)census.largest_pid;
                    end.down = verdict == PW_MEMORY_CLOSE;
                    const int delivered = deliver(&end);
                    notice_set(verdict == PW_MEMORY_CLOSE ? NOTICE_MEMORY_CLOSE : NOTICE_MEMORY_END,
                               "The PS5's memory is nearly full: the program using the most was %s.",
                               verdict == PW_MEMORY_CLOSE ? "asked to close" : "ended");
                    // The END line names the rule that fired: the steep climb ends below the end mark (a launcher's run
                    // on the Pro, 2026-10-20, read "stood past 940 MiB" at 872-900).
                    char why[96];
                    if (by_tables) snprintf(why, sizeof(why), "was fine but its %s", tables_why);
                    else if (by_start) snprintf(why, sizeof(why), "slowed a helper start to %llu ms", (unsigned long long)start_last);
                    else if (by_plateau) snprintf(why, sizeof(why), "held its plateau with frame gaps for %u s", PW_PLATEAU_HOLD_MS / 1000);
                    else if (verdict == PW_MEMORY_CLOSE) snprintf(why, sizeof(why), "stood at %u MiB", memory_limit);
                    else if (memory_guard.steep)
                        snprintf(why, sizeof(why), "rose %u MiB or more in %u s to within %u MiB of %u MiB",
                                 PW_MEMORY_GUARD_STEEP_RISE_MIB, PW_MEMORY_GUARD_STEEP_WINDOW_MS / 1000,
                                 PW_MEMORY_GUARD_STEEP_NEAR_MIB, memory_limit);
                    else snprintf(why, sizeof(why), "stood past %u MiB for %u s", memory_end, PW_MEMORY_GUARD_END_MS / 1000);
                    say("Wine: the console's system memory %s (now %llu MiB; helpers %llu MiB in %u, "
                        "the largest %llu MiB in process %d): %s that process, not the desktop session (%d)",
                        why, (unsigned long long)(census.system >> 20),
                        (unsigned long long)(census.helpers >> 20), census.helper_count,
                        (unsigned long long)(census.largest >> 20), census.largest_pid,
                        verdict == PW_MEMORY_CLOSE ? "closing" : "ending", delivered);
                    pw_memory_guard_rearm(&memory_guard);
                } else if (verdict == PW_MEMORY_CLOSE) {
                    PwWineInput close{};
                    close.type = PW_WINE_INPUT_CLOSE;
                    const int delivered = stop_tick ? 0 : deliver(&close);
                    notice_set(NOTICE_MEMORY_CLOSE, "The PS5's memory is nearly full: the game was asked to close.");
                    if (by_tables) say("Wine: the console's %s (system memory %llu MiB; helpers in %u): %s (%d)", tables_why,
                                       (unsigned long long)(census.system >> 20), census.helper_count,
                                       stop_tick ? "the game is already closing" : "asking the game to close before they run out",
                                       delivered);
                    else if (by_start || by_plateau) say("Wine: asking the game to close before the console swaps further (%d)", delivered);
                    else say("Wine: the console's system memory stood at %u MiB or more for %llu of the last %u s (now %llu MiB; "
                        "helpers %llu MiB in %u, the largest %llu MiB in process %d): %s (%d)", memory_limit,
                        (unsigned long long)(pw_memory_guard_pressure_ms(&memory_guard) / 1000),
                        PW_MEMORY_GUARD_WINDOW_MS / 1000, (unsigned long long)(census.system >> 20),
                        (unsigned long long)(census.helpers >> 20), census.helper_count,
                        (unsigned long long)(census.largest >> 20), census.largest_pid,
                        stop_tick ? "the game is already closing" : "asking the game to close before the console swaps",
                        delivered);
                    if (!stop_tick) stop_tick = tick;
                } else if (verdict == PW_MEMORY_END) {
                    const bool can_end = programs_in_helpers && list_helpers;
                    notice_set(NOTICE_MEMORY_END, "The PS5's memory stayed full: %s", can_end ? "the game was ended." :
                               "close " PS5_TITLE_NAME " from the home screen.");
                    char why[96];
                    if (by_tables) snprintf(why, sizeof(why), "was fine but its %s", tables_why);
                    else if (memory_guard.steep)
                        snprintf(why, sizeof(why), "rose %u MiB or more in %u s to within %u MiB of %u MiB",
                                 PW_MEMORY_GUARD_STEEP_RISE_MIB, PW_MEMORY_GUARD_STEEP_WINDOW_MS / 1000,
                                 PW_MEMORY_GUARD_STEEP_NEAR_MIB, memory_limit);
                    else snprintf(why, sizeof(why), "stood past %u MiB for %u s", memory_end, PW_MEMORY_GUARD_END_MS / 1000);
                    say("Wine: the console's system memory %s (now %llu MiB; helpers %llu MiB in %u, "
                        "the largest %llu MiB in process %d): %s", why, (unsigned long long)(census.system >> 20),
                        (unsigned long long)(census.helpers >> 20), census.helper_count,
                        (unsigned long long)(census.largest >> 20), census.largest_pid,
                        can_end ? "ending the game now: its helpers by SIGKILL" :
                                  "the game cannot be ended from here");
                    if (!stop_tick) stop_tick = tick;
                    if (can_end) end_game("the memory guard", true);
                }
            }
        }
#ifdef PS5_GPU_BROKER
        // The hang breaker (src/pw_hang_breaker.h), once a second; the flight recorder prints what it saw.
        if (breaker_armed && tick % 120 == 60) {
            const uint64_t now = monotonic_ms();
            PwHangSample sample{};
            // Ages count from the app's last return (src/pw_suspend.h): a suspend is not a stall.
            sample.present_age_ms = now - pw_suspend_since(&title_suspend, shown_last_ms ? shown_last_ms : breaker_armed_ms);
            sample.census_known = breaker_census_ms != 0;
            const uint64_t census_from = pw_suspend_since(&title_suspend, breaker_census_ms);
            sample.census_age_ms = breaker_census_ms && now > census_from ? now - census_from : 0;
            sample.stall_ms = breaker_stall_ms;
            PwHangVerdict verdict = PW_HANG_FINE;
            pw_hang_breaker_decide(&sample, &verdict);
            proton_flight_publish(sample.present_age_ms, sample.census_age_ms, sample.census_known, verdict == PW_HANG_STALLED);
            if (verdict == PW_HANG_FINE) {
                breaker_said = false;
            } else if (!breaker_said && pw_suspend_hold(&title_suspend, now)) {
                // Away or just back: decided again once the grace is over (the ages count from the return anyway).
            } else if (!breaker_said) {
                breaker_said = true;
                say("Wine: hang breaker: stalled: no frame for %llu ms and the system memory census %llu ms old%s",
                    (unsigned long long)sample.present_age_ms, (unsigned long long)sample.census_age_ms,
                    breaker_acts && list_helpers ? "" : "; reporting only");
                if (title_log_fd >= 0) fsync(title_log_fd);
                if (breaker_acts) end_game("hang breaker", false);
            }
        }
#endif
        // A second look shows whether anything moved since the first.
        if (stop_tick && tick - stop_tick == 3600) dump_hang(30);
        // Replacing a process that is still running is what powers the console
        // off (2026-10-03: with a live presenter, and with a probe's threads
        // after the server had killed the game). So a game that cannot be ended
        // is left running and said so: the user closes the title from the home
        // screen, which the system does on its own terms.
        if (stop_tick && tick - stop_tick == 7200)
            say("Wine: the game did not end in 60 s; leaving it running: close the title from the home screen");
        if (tick % 600 == 0) perf.report();
        // The synchronization device's totals, for every process that uses it: read
        // without its lock, so a stuck device cannot hold up this loop.
        if (tick % 600 == 0 && device_counters) {
            PwNtsyncCounters device{};
            device_counters(&device);
            if (device.waits)
                say("perf: sync device objects=%u blocked=%u waits=%llu slept=%llu timeouts=%llu wakes=%llu "
                    "signals=%llu deferred=%u raised=%u processes=%u dropped=%u refused=%u", device.objects,
                    device.blocked, (unsigned long long)device.waits, (unsigned long long)device.slept,
                    (unsigned long long)device.timeouts, (unsigned long long)device.wakes,
                    (unsigned long long)device.signals, device.deferred, device.raised, device.processes,
                    device.dropped, device.exhausted);
        }
        if (tick % 1200 == 0) {
            PwWineSinkStats stats{};
            if (sink_stats) sink_stats(&stats);
            PwWineFdvStats files{};
            if (fdv_stats) fdv_stats(&files);
            say("Wine: alive stage=%d seconds=%u pad=%d mouse=%llu/%llu dropped=%llu files live=%u parked=%u parks=%lu unparks=%lu limit=%lu failed=%lu%s",
                wine.stage, tick / 120, pad.connected, (unsigned long long)stats.inputs_delivered,
                (unsigned long long)stats.inputs_posted, (unsigned long long)stats.inputs_dropped, files.live,
                files.parked, files.parks, files.unparks, files.limit_hits, files.failures,
                files.disabled ? " (budget off)" : "");
            if (usb.keyboards || usb.mouse >= 0)
                say("usb: keyboards=%u mouse=%s key events=%llu mouse events=%llu", usb.keyboards,
                    usb.mouse >= 0 ? "open" : "none", (unsigned long long)usb.keys_sent,
                    (unsigned long long)usb.mouse_sent);
            if (census_seen)
                say("Wine: system memory %llu MiB (helpers %llu MiB in %u, the largest %llu MiB in process %d), peak %llu MiB; "
                    "%llu s of the last %u at the %u MiB limit; shared code copies beyond the first %llu MiB (what counting "
                    "them once would take off); helpers' address space %llu MiB", (unsigned long long)(census.system >> 20),
                    (unsigned long long)(census.helpers >> 20), census.helper_count,
                    (unsigned long long)(census.largest >> 20), census.largest_pid,
                    (unsigned long long)(census_peak.system >> 20),
                    (unsigned long long)(pw_memory_guard_pressure_ms(&memory_guard) / 1000),
                    PW_MEMORY_GUARD_WINDOW_MS / 1000, memory_limit,
                    (unsigned long long)((code_share_duplicates ? code_share_duplicates() : 0) >> 20),
                    (unsigned long long)(census_address_space >> 20));
            // A program in the title converts its code in the title's partition under a helper's budget; the same figures
            // as a helper's line (native/proton_wine_child.c, code_cache_tally).
            uint64_t cache[10] = {};
            if (title_client_claimed && code_cache_stats && code_cache_stats(cache, 10) >= 10) {
                static const char *const policies[] = {"off", "set", "listed", "adaptive"};
                say("Wine: title code cache budget_mib=%llu policy=%s held_kib=%llu evicted=%llu evicted_kib=%llu stale=%llu "
                    "refused=%llu lost=%llu raises=%llu reconverted_kib=%llu", (unsigned long long)cache[0],
                    cache[9] < 4 ? policies[cache[9]] : "?", (unsigned long long)cache[1], (unsigned long long)cache[2],
                    (unsigned long long)cache[3], (unsigned long long)cache[4], (unsigned long long)cache[5],
                    (unsigned long long)cache[6], (unsigned long long)cache[7], (unsigned long long)cache[8]);
            }
            if (census_seen && census_shared_once)
                say("Wine: shared code copies beyond the first %llu MiB, left out of the census; peak counting every "
                    "mapping %llu MiB", (unsigned long long)(census_shared >> 20),
                    (unsigned long long)(census_raw_peak >> 20));
#ifdef PS5_GPU_BROKER
            if (shown_last_ms)
                say("Wine: frames shown %llu in 10 s, longest gap %llu ms, %u gaps of 100 ms or more; foreground process %d",
                    shown_flips - shown_at_report, (unsigned long long)shown_gap_ms, shown_gaps_100,
                    server_foreground ? server_foreground() : -1);
            shown_at_report = shown_flips;shown_gap_ms = 0;shown_gaps_100 = 0;
#endif
        }
        usleep(8333);
    }
}
