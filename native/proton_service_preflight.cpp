// SPDX-License-Identifier: GPL-3.0-or-later
#include "proton_service_preflight.h"
#include "../ps5/src/proton_launch.h"
#include "../src/pw_fd_census.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(PS5_SERVICE_NATIVE)
#include "proton_gpu_broker_probe.h"
#include "proton_flex_probe.h"
#endif

extern "C" {
int sceSystemServiceGetAppStatus(void *);
int sceSystemServiceGetLocalProcessStatusList(void *, unsigned, unsigned *);
int sceSystemServiceAddLocalProcess(int, const char *, const char *const *, const void *);
int sceSystemServiceGetParentSocket(void);
int sceSystemServiceKillLocalProcess(int, int);
int sceKernelUsleep(unsigned);
int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAvailableDirectMemorySize(int64_t, int64_t, size_t, int64_t *, size_t *);
int sceKernelAvailableFlexibleMemorySize(size_t *);
}

#if defined(PS5_SERVICE_NATIVE)
namespace {
// Public layout mapped to exactly the internal defaults of Sony's JVM wrapper.
// That wrapper's exports belong to a separate import library on firmware 10.01.
struct SpawnOptions {
    uint32_t size;
    int32_t fd, crash_report;
    uint32_t other[15];
};
static_assert(sizeof(SpawnOptions) == 72);
struct Handshake { char identity[33]; int32_t pid, app, socket_type, allocation_status; int64_t physical; uint64_t allocation_bytes; };
bool ready(int fd, short events)
{
    pollfd descriptor{fd, events, 0};
    int result;
    do { result = poll(&descriptor, 1, 5000); } while (result < 0 && errno == EINTR);
    return result == 1 && (descriptor.revents & events);
}
int app_id()
{
    uint32_t status[4]{};
    return sceSystemServiceGetAppStatus(status) == 0 ? int(status[0]) : -1;
}
bool contains_process(int id, unsigned &count, int *first_id = nullptr)
{
    struct Entry { int32_t id; char name[32]; } entries[16]{};
    count = 0;
    if (sceSystemServiceGetLocalProcessStatusList(entries, 16, &count) != 0 || count > 16) {
        count = 17; return true; // Unknown state must never be mistaken for cleanup.
    }
    if (first_id && count) *first_id = entries[0].id;
    for (unsigned i = 0; i < count; ++i) if (entries[i].id == id) return true;
    return false;
}
int64_t title_flexible()
{
    size_t available = 0;
    return sceKernelAvailableFlexibleMemorySize(&available) ? -1 : int64_t(available);
}
// The flexible-memory probe (docs/FLEXIBLE_MEMORY.md, struct ProtonFlexReply): one Sony local process that maps
// direct and anonymous memory and runs code from direct memory, the title's own flexible figure around it.
int flex_probe(FILE *output, const char *identity, ProtonFlexRun *run = nullptr, unsigned hold_ms = 10000, bool title_ways = true)
{
    const int parent_app = app_id();
    unsigned before_count = 0, after_count = 0;
    int parent_service_id = -1;
    contains_process(getpid(), before_count, &parent_service_id);
    const int64_t title_before = title_flexible();
    int pt_cpu_t0 = -1, pt_cpu_f0 = -1, pt_gpu_t0 = -1, pt_gpu_f0 = -1;
    (void)sceKernelGetPageTableStats(&pt_cpu_t0, &pt_cpu_f0, &pt_gpu_t0, &pt_gpu_f0);
    if (run) run->title_before = title_before;
    fprintf(output, "flex title app=%d list_count=%u parent_service_id=%d title_flexible_before=%lld pt_before=%d/%d gpu_pt=%d/%d\n",
            parent_app, before_count, parent_service_id, (long long)title_before, pt_cpu_f0, pt_cpu_t0, pt_gpu_f0, pt_gpu_t0);
    int pair[2]{-1, -1};
    if (parent_app <= 0 || before_count != 1 || parent_service_id <= 0 || socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair)) {
        fprintf(output, "flex spawn=skipped\n");
        if (run) run->spawn_status = -1;
        return 1;
    }
    SpawnOptions options{};
    options.size = sizeof(options); options.fd = pair[1]; options.crash_report = 1;
    options.other[0] = uint32_t(-1); options.other[3] = 2; options.other[4] = 0x80000000; // as the normal mode
    const char *args[] = {"/app0/service-native-helper.bin", "--service-native-flex-helper", identity, nullptr};
    fsync(fileno(output));
    const int child = sceSystemServiceAddLocalProcess(parent_app, args[0], args, &options);
    close(pair[1]);
    fprintf(output, "flex spawn returned=%d status=%#x\n", child, unsigned(child));
    if (run) { run->spawn_status = child; run->spawned = child > 0 && child != parent_service_id; }
    if (child <= 0 || child == parent_service_id) { close(pair[0]); return 1; }
    ProtonFlexReply reply{};
    // The helper writes 128 MiB before it answers.
    bool answered = false;
    for (int wait = 0; wait < 6 && !answered; ++wait) answered = ready(pair[0], POLLIN);
    answered = answered && recv(pair[0], &reply, sizeof(reply), 0) == sizeof(reply) && reply.magic == PROTON_FLEX_MAGIC;
    const int64_t title_held = title_flexible();
    int pt_cpu_t1 = -1, pt_cpu_f1 = -1, pt_gpu_t1 = -1, pt_gpu_f1 = -1;
    (void)sceKernelGetPageTableStats(&pt_cpu_t1, &pt_cpu_f1, &pt_gpu_t1, &pt_gpu_f1);
    if (run) { run->answered = answered; run->reply = reply; run->title_held = title_held; }
    fprintf(output, "flex helper answered=%d pid=%d elevation=%d euid=%d direct_alloc=%#x direct_map=%#x anon_map=%d jit_made=%#x "
            "jit80_made=%#x jit80_mapped=%d jit80_protect=%#x jit80_value=%d nofile=%d/%d raise_rc=%d raise_cur=%d "
            "title_flexible_held=%lld title_pt_held=%d/%d\n",
            answered, reply.pid, reply.elevation, reply.euid, unsigned(reply.direct_alloc), unsigned(reply.direct_map),
            reply.anon_map, unsigned(reply.jit_made), unsigned(reply.jit80_made), reply.jit80_mapped,
            unsigned(reply.jit80_protect), reply.jit80_value, reply.nofile_cur, reply.nofile_max,
            reply.nofile_raise_rc, reply.nofile_raise_cur, (long long)title_held, pt_cpu_f1, pt_cpu_t1);
    static const char *const points[FLEX_POINTS] = {"start", "direct", "anon", "jit"};
    for (unsigned p = 0; answered && p < FLEX_POINTS; ++p)
        fprintf(output, "flex point=%s resident=%lld mapped=%lld available=%lld pt=%d/%d gpu_pt=%d/%d\n", points[p],
                (long long)reply.resident[p], (long long)reply.mapped[p], (long long)reply.available[p],
                reply.pt_cpu_free[p], reply.pt_cpu_total[p], reply.pt_gpu_free, reply.pt_gpu_total);
    static const char *const ways[FLEX_EXEC_WAYS] = {"direct0", "direct12", "main12", "mapx", "anon", "jit"};
    ProtonFlexExec title[FLEX_EXEC_WAYS]{};
    for (unsigned way = 0; title_ways && way < FLEX_EXEC_WAYS; ++way) proton_flex_exec_try(way, &title[way]);
    for (unsigned side = 0; side < (title_ways ? 2u : 1u); ++side)
        for (unsigned way = 0; way < FLEX_EXEC_WAYS && (side || answered); ++way) {
            const ProtonFlexExec &e = side ? title[way] : reply.exec[way];
            fprintf(output, "flex exec process=%s way=%s made=%#x mapped=%#x protect=%#x query=%#x prot=%d value=%d\n",
                    side ? "title" : "helper", ways[way], unsigned(e.made), unsigned(e.mapped), unsigned(e.protect), unsigned(e.query), e.prot, e.value);
        }
    fsync(fileno(output));
    // Hold it 10 s so SceShellCore's census of the app's processes can be read from the klog beside these figures.
    sceKernelUsleep(hold_ms * 1000u);
    const int32_t release = child;
    int32_t completed = -1;
    const bool done = answered && ready(pair[0], POLLOUT) && send(pair[0], &release, sizeof(release), MSG_NOSIGNAL) == sizeof(release) &&
                      ready(pair[0], POLLIN) && recv(pair[0], &completed, sizeof(completed), 0) == sizeof(completed) && completed == 37;
    const int closed = sceSystemServiceKillLocalProcess(parent_app, child);
    bool present = true;
    for (int attempt = 0; attempt < 200 && present; ++attempt) {
        present = contains_process(child, after_count);
        if (present) sceKernelUsleep(10000);
    }
    sceKernelUsleep(500000);
    const int64_t title_after = title_flexible();
    int pt_cpu_t2 = -1, pt_cpu_f2 = -1, pt_gpu_t2 = -1, pt_gpu_f2 = -1;
    (void)sceKernelGetPageTableStats(&pt_cpu_t2, &pt_cpu_f2, &pt_gpu_t2, &pt_gpu_f2);
    fprintf(output, "flex close completion=%d closed=%#x present=%d list_after=%u title_flexible_after=%lld title_pt_after=%d/%d\n",
            done, unsigned(closed), present, after_count, (long long)title_after, pt_cpu_f2, pt_cpu_t2);
    if (run) { run->title_after = title_after; run->closed = closed; run->present = present; }
    close(pair[0]);
    // The verdict asks only that the probe ran and cleaned up; the figures are the answer, read by tools/run-child-probe.py.
    return !(answered && done && closed == 0 && !present && after_count == before_count);
}
}

// The tester probe's measurement (native/proton_tester_probe.cpp): the flex probe's Sony local process, with the title's own
// execute attempts left to the caller (which guards each), held 2 s, its lines captured into log.
int proton_flex_run(ProtonFlexRun *run, std::string *log)
{
    static const char hex[] = "0123456789abcdef";
    char identity[33];
    for (int i = 0; i < 32; ++i) identity[i] = hex[(unsigned(getpid()) * 2654435761u >> (i % 8)) & 15 ^ (i & 15)];
    identity[32] = 0;
    char *text = nullptr;
    size_t size = 0;
    FILE *stream = open_memstream(&text, &size);
    if (!stream) return -1;
    const int status = flex_probe(stream, identity, run, 2000, false);
    fclose(stream);
    if (text) { log->assign(text, size); free(text); }
    return status;
}

bool proton_service_native(bool elevated, int &status)
{
    const auto hook = proton_title_path("/app0/service-native.txt");
    FILE *input = fopen(hook.c_str(), "r");
    if (!input) return false;
    char identity[65]{};
    const bool identity_valid = fscanf(input, "%64s", identity) == 1 && strlen(identity) == 32 &&
                                strspn(identity, "0123456789abcdef") == 32;
    char mode[16]{};
    (void)fscanf(input, "%15s", mode);
    const bool missing_image = strcmp(mode, "missing-image") == 0;
    const bool repeat = strcmp(mode, "repeat") == 0;
    const bool parent_close = strcmp(mode, "parent-close") == 0;
    const bool gpu_present=strcmp(mode,"gpu-present")==0;
    const bool gpu_draw=strcmp(mode,"gpu-draw")==0 || gpu_present;
    const bool gpu_vulkan = strcmp(mode,"gpu-vulkan")==0 || gpu_draw;
    const bool gpu_shared = strcmp(mode, "gpu-broker-shm") == 0;
    const bool gpu_memory = strcmp(mode, "gpu-memory") == 0;
    const bool fd_budget = strcmp(mode, "fd-budget") == 0;
    const bool channel = strcmp(mode, "channel") == 0;
    const bool payload_start = strcmp(mode, "payload-start") == 0;
    const bool flex = strcmp(mode, "flex") == 0;
    const bool gpu_broker = strcmp(mode, "gpu-broker") == 0 || gpu_shared;
    const bool valid = identity_valid && (mode[0] == 0 || missing_image || repeat || parent_close || gpu_broker || gpu_vulkan ||
                                          gpu_memory || fd_budget || channel || payload_start || flex);
    fclose(input); unlink(hook.c_str());
    status = 1;
    FILE *output = fopen(proton_title_path("/app0/service-native-results.txt").c_str(), "w");
    if (!output) return true;
    setvbuf(output, nullptr, _IONBF, 0);
    if (valid && (channel || payload_start)) {
        status = channel ? proton_channel_probe(output, identity) : proton_payload_start_probe(output, identity);
        fprintf(output, "result=%s\n", status == 0 ? "PASS" : "FAIL");
        fsync(fileno(output)); fclose(output); return true;
    }
    if (valid && flex) {
        fprintf(output, "service-native identity=%s revision=6 parent=%d elevated=%d\n", identity, int(getpid()), elevated);
        status = flex_probe(output, identity);
        fprintf(output, "result=%s\n", status == 0 ? "PASS" : "FAIL");
        fsync(fileno(output)); fclose(output); return true;
    }
    if (valid && fd_budget) {
        fprintf(output, "service-native identity=%s revision=5 parent=%d elevated=%d\n", identity, int(getpid()), elevated);
        status = proton_fd_budget_probe(output, identity);
        fprintf(output, "result=%s\n", status == 0 ? "PASS" : "FAIL");
        fsync(fileno(output)); fclose(output); return true;
    }
    if (valid && gpu_memory) {
        fprintf(output, "service-native identity=%s revision=4 parent=%d elevated=%d\n", identity, int(getpid()), elevated);
        unsigned owner_touch = 1, direct_arm = 1, alias_arm = 0;
        FILE *settings = fopen(proton_title_path("/app0/gpu-memory-settings.txt").c_str(), "r");
        if (settings) {
            if (fscanf(settings, "%u %u %u", &owner_touch, &direct_arm, &alias_arm) != 3 || owner_touch > 1 ||
                direct_arm > 1 || alias_arm > 2) {
                fprintf(output, "result=FAIL invalid_memory_settings=1\n");
                fclose(settings); fsync(fileno(output)); fclose(output); return true;
            }
            fclose(settings);
            unlink(proton_title_path("/app0/gpu-memory-settings.txt").c_str());
        }
        status = proton_gpu_memory_probe(output, identity, owner_touch != 0, direct_arm != 0, alias_arm);
        fprintf(output, "result=%s\n", status == 0 ? "PASS" : "FAIL");
        fsync(fileno(output)); fclose(output); return true;
    }
    if(valid && (gpu_broker || gpu_vulkan)) {
        fprintf(output,"service-native identity=%s revision=3 parent=%d elevated=%d\n",identity,int(getpid()),elevated);
        uint64_t shared_bytes=16384;unsigned window32=0,local=0;
        FILE *settings=fopen(proton_title_path("/app0/gpu-broker-settings.txt").c_str(),"r");
        if(settings) {
            unsigned long long bytes=0;
            const int fields=fscanf(settings,"%llu %u %u",&bytes,&window32,&local);
            const bool parsed=fields==2 || fields==3;
            fclose(settings);unlink(proton_title_path("/app0/gpu-broker-settings.txt").c_str());
            if(!parsed || bytes<16384 || bytes>33554432 || (bytes&16383) || window32>1 || local>1) {
                fprintf(output,"result=FAIL invalid_shared_settings=1\n");fsync(fileno(output));fclose(output);return true;
            }
            shared_bytes=bytes;
        }
        status=gpu_vulkan ? proton_gpu_server_probe(output,identity,gpu_draw,gpu_present,local!=0) : proton_gpu_broker_probe(output,identity,gpu_shared,shared_bytes,window32);
        fprintf(output,"result=%s\n",status==0 ? "PASS" : "FAIL");
        fsync(fileno(output));fclose(output);return true;
    }
    fprintf(output, "service-native identity=%s revision=2 parent=%d elevated=%d\n",
            valid ? identity : "invalid", int(getpid()), elevated);
    auto volatile imported_spawn = &sceSystemServiceAddLocalProcess;
    auto volatile imported_kill = &sceSystemServiceKillLocalProcess;
    auto spawn = imported_spawn; auto kill_local = imported_kill;
    fprintf(output, "bindings spawn=%d kill=%d\n", spawn != nullptr, kill_local != nullptr);
    fsync(fileno(output));
    if (valid && spawn && kill_local) {
        const int parent_app = app_id();
        unsigned before_count = 0, after_count = 0;
        int parent_service_id = -1;
        const bool parent_listed = contains_process(getpid(), before_count, &parent_service_id);
        fprintf(output, "app id=%d list_count=%u native_parent_listed=%d parent_service_id=%d\n",
                parent_app, before_count, parent_listed, parent_service_id);
        PwFdCensus before{}, after{}; pw_fd_census(&before);
        int64_t memory_start = -1;
        size_t available_before = 0;
        const int memory_query = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 65536,
                                                                   &memory_start, &available_before);
        fprintf(output, "memory baseline_status=%#x available=%zu start=%lld\n", unsigned(memory_query), available_before, (long long)memory_start);
        status = 0;
        const int cycles = repeat ? 4 : 1;
        for (int cycle = 0; cycle < cycles && status == 0; ++cycle) {
        fprintf(output, "cycle=%d mode=%s\n", cycle + 1, mode[0] ? mode : "normal");
        status = 1;
        int pair[2]{-1, -1};
        if (parent_app > 0 && before_count == 1 && parent_service_id > 0 && socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0) {
            SpawnOptions options{};
            options.size = sizeof(options); options.fd = pair[1]; options.crash_report = 1;
            options.other[0] = uint32_t(-1); // public 0xc -> internal 0x10 default -1.
            // Explicit headless preload policy. Zero selects Sony's full startup
            // defaults, including VideoOut, even for a kernel-only image.
            options.other[3] = 2; options.other[4] = 0x80000000;
            // public 0x18 -> process_param.preload_prxflag: selective libc only.
            const char *args[] = {missing_image ? "/app0/service-native-never-exists.bin" : "/app0/service-native-helper.bin", "--service-native-helper", identity, nullptr};
            fprintf(output, "stage=service_spawn source_fd=%d options_size=%zu preload=0x8000000000000002\n", pair[1], sizeof(options));
            fsync(fileno(output));
            fprintf(output, "expected_missing_image=%d\n", missing_image); fsync(fileno(output));
            const int child = spawn(parent_app, args[0], args, &options);
            fprintf(output, "spawn returned=%d status=%#x\n", child, unsigned(child));
            close(pair[1]); pair[1] = -1;
            if (missing_image && child < 0) {
                const bool parent_present = contains_process(parent_service_id, after_count);
                fprintf(output, "failed_spawn parent_present=%d list_before=%u list_after=%u\n",
                        parent_present, before_count, after_count);
                status = !(parent_present && after_count == before_count);
            } else if (child > 0 && child != parent_service_id) {
                Handshake reply{};
                const bool acknowledged = ready(pair[0], POLLIN) &&
                    recv(pair[0], &reply, sizeof(reply), 0) == sizeof(reply);
                const bool matches = acknowledged && memcmp(reply.identity, identity, 33) == 0 &&
                    reply.pid > 0 && reply.pid != getpid() && reply.app == -1 && reply.socket_type == SOCK_SEQPACKET && reply.allocation_status == 0 && reply.allocation_bytes == 1024 * 1024 &&
                    contains_process(child, after_count) && after_count <= 16;
                size_t available_during = 0;
                const int during_query = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 65536,
                                                                           &memory_start, &available_during);
                fprintf(output, "memory during_status=%#x available=%zu child_allocation_status=%#x bytes=%llu physical=%lld\n",
                        unsigned(during_query), available_during, unsigned(reply.allocation_status),
                        (unsigned long long)reply.allocation_bytes, (long long)reply.physical);
                const int32_t release = child;
                const bool released = matches && ready(pair[0], POLLOUT) &&
                                      send(pair[0], &release, sizeof(release), MSG_NOSIGNAL) == sizeof(release);
                int32_t completed = -1;
                const bool done = released && ready(pair[0], POLLIN) &&
                    recv(pair[0], &completed, sizeof(completed), 0) == sizeof(completed) && completed == 37;
                fprintf(output, "handshake ack=%d matches=%d service_id=%d pid=%d app=%d socket_type=%d completion=%d\n",
                        acknowledged, matches, child, reply.pid, reply.app, reply.socket_type, done);
                // Completion is reported over the socket; native closure belongs
                // to Sony. Never use a service identifier as a waitpid/kill PID.
                if (parent_close && matches && done) {
                    fprintf(output, "parent_close live_service_id=%d live_native_pid=%d list_count=%u\n", child, reply.pid, after_count);
                    // Return through the title's own shell-close path, leaving
                    // the owned helper and its allocation live for Sony cleanup.
                    close(pair[0]); pair[0] = -1;
                    fprintf(output, "result=PASS\n"); fsync(fileno(output)); fclose(output);
                    status = 0; return true;
                }
                fprintf(output, "service_close id=%d watchdog=%d\n", child, !done); fsync(fileno(output));
                const int closed = kill_local(parent_app, child);
                fprintf(output, "service_close status=%#x\n", unsigned(closed));
                bool present = true;
                for (int attempt = 0; attempt < 200 && present; ++attempt) {
                    present = contains_process(child, after_count);
                    if (present) sceKernelUsleep(10000);
                }
                fprintf(output, "cleanup child_present=%d list_before=%u list_after=%u\n",
                        present, before_count, after_count);
                size_t available_after = 0;
                const int after_query = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 65536,
                                                                          &memory_start, &available_after);
                fprintf(output, "memory after_status=%#x available=%zu baseline=%zu\n", unsigned(after_query), available_after, available_before);
                status = !(!missing_image && matches && done && closed == 0 && !present && after_count == before_count &&
                           memory_query == 0 && during_query == 0 && after_query == 0 && available_after == available_before);
            }
        }
        for (int fd : pair) if (fd >= 0) close(fd);
        pw_fd_census(&after);
        fprintf(output, "descriptors before=%u after=%u\n", before.open, after.open);
        if (before.open != after.open) status = 1;
        }
    }
    fprintf(output, "result=%s\n", status == 0 ? "PASS" : "FAIL");
    fsync(fileno(output)); fclose(output);
    return true;
}
#endif

namespace {
constexpr uint32_t InvalidArgument = 0x80940005;
// Reconstructed public PS5 layout, used only with oversized argv that the
// client rejects before IPC. This is NOT a validated spawn initializer.
struct Options {
    uint32_t size;
    int32_t fd, crash_report;
    uint32_t other[15];
};
static_assert(sizeof(Options) == 72);
bool guard_intact(const unsigned char *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (bytes[i] != 0xa5) return false;
    return true;
}
}

bool proton_service_preflight(bool elevated, int &status)
{
    const auto hook = proton_title_path("/app0/service-preflight.txt");
    FILE *input = fopen(hook.c_str(), "r");
    if (!input) return false;
    char identity[65]{};
    const bool valid = fscanf(input, "%64s", identity) == 1 && strlen(identity) == 32 &&
        strspn(identity, "0123456789abcdef") == 32;
    fclose(input);
    unlink(hook.c_str());
    status = 1;
    FILE *output = fopen(proton_title_path("/app0/service-preflight-results.txt").c_str(), "w");
    if (!output) return true;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "service-preflight identity=%s revision=2 parent=%d elevated=%d\n",
            valid ? identity : "invalid", int(getpid()), elevated);
    auto volatile imported_app = &sceSystemServiceGetAppStatus;
    auto volatile imported_list = &sceSystemServiceGetLocalProcessStatusList;
    auto volatile imported_add = &sceSystemServiceAddLocalProcess;
    auto volatile imported_socket = &sceSystemServiceGetParentSocket;
    auto app = imported_app;
    auto list = imported_list;
    auto add = imported_add;
    auto parent_socket = imported_socket;
    fprintf(output, "bindings app=%d list=%d add=%d parent_socket=%d\n",
            app != nullptr, list != nullptr, add != nullptr, parent_socket != nullptr);
    fsync(fileno(output));
    if (valid && app && list && add && parent_socket) {
        PwFdCensus before{}, after{};
        pw_fd_census(&before);
        // Public app status writes 16 bytes; leave extra room and a guard to
        // observe ABI disagreement without printing opaque status contents.
        alignas(8) unsigned char app_status[80];
        memset(app_status, 0xa5, sizeof(app_status));
        fprintf(output, "stage=app_status\n"); fsync(fileno(output));
        const int app_result = app(app_status);
        int32_t app_id = -1;
        if (app_result == 0) memcpy(&app_id, app_status, sizeof(app_id));
        const bool app_guard = guard_intact(app_status + 16, sizeof(app_status) - 16);
        fprintf(output, "app status=%#x id=%d guard=%d\n", unsigned(app_result), app_id, app_guard);
        alignas(8) unsigned char processes[16 * 36 + 16];
        memset(processes, 0xa5, sizeof(processes));
        unsigned count = 0;
        fprintf(output, "stage=local_process_list\n"); fsync(fileno(output));
        const int list_result = list(processes, 16, &count);
        const bool list_guard = guard_intact(processes + 16 * 36, 16);
        fprintf(output, "local_processes status=%#x count=%u guard=%d\n",
                unsigned(list_result), count, list_guard);
        const int descriptor = parent_socket();
        fprintf(output, "parent_socket descriptor=%d validated_socket=0\n", descriptor);
        // Null options are rejected before client initialization and IPC.
        fprintf(output, "stage=null_options_rejection\n"); fsync(fileno(output));
        const int null_result = add(-1, "/app0/preflight-never-exists.self", nullptr, nullptr);
        fprintf(output, "invalid_null_options status=%#x expected=%#x\n",
                unsigned(null_result), InvalidArgument);
        // 4096 characters + NUL = 4097 serialized bytes. No executable image
        // exists at this synthetic path; never shorten this argv into a valid request.
        char oversized[4097];
        memset(oversized, 'X', sizeof(oversized) - 1);
        oversized[sizeof(oversized) - 1] = '\0';
        const char *argv[] = {oversized, nullptr};
        Options options{};
        options.size = sizeof(options); options.fd = -1; options.crash_report = -1;
        fprintf(output, "stage=oversized_argv_rejection bytes=%zu\n", sizeof(oversized));
        fsync(fileno(output));
        const int oversized_result = add(-1, "/app0/preflight-never-exists.self", argv, &options);
        fprintf(output, "invalid_oversized_argv status=%#x expected=%#x\n",
                unsigned(oversized_result), InvalidArgument);
        pw_fd_census(&after);
        fprintf(output, "descriptors before=%u after=%u spawn_requests=0\n", before.open, after.open);
        status = !(app_result == 0 && app_id > 0 && app_guard && list_result == 0 && count <= 16 &&
                   list_guard && descriptor == 3 && uint32_t(null_result) == InvalidArgument &&
                   uint32_t(oversized_result) == InvalidArgument && before.open == after.open);
    }
    fprintf(output, "result=%s\n", status == 0 ? "PASS" : "FAIL");
    fsync(fileno(output));
    fclose(output);
    return true;
}
