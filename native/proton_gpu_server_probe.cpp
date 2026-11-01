// SPDX-License-Identifier: GPL-3.0-or-later
// Wine lifecycle adapter; GPU allocation/submission/display belongs to PS5_Vulkan.
#include "proton_gpu_server.h"
#include "proton_gpu_display.h"
#include "../ps5/src/proton_launch.h"
#ifdef PS5_SERVICE_NATIVE
#include "../.deps/graphics-foundation/tooling/gpu-broker/server.cpp"
#else
#include "../.deps/graphics-foundation/tooling/gpu-broker/server.h"
#include <cerrno>
#include <cstdint>
#include <signal.h>
#include <unistd.h>
#endif
#include "../src/pw_gpu_identity.h"
#include "../src/pw_memory_guard.h"
#include <cstdio>
#include <cstdlib>
#include <ps5platform/kernel.h>
extern "C" int sceSystemServiceGetLocalProcessStatusList(void *,unsigned,unsigned *);
namespace {
bool proton_native_absent(int token)
{
    if(token<0) { errno=0;return kill(-token,0)<0 && errno==ESRCH; }
    struct Entry { int32_t id;char name[32]; } entries[16]{};unsigned count=0;
    if(sceSystemServiceGetLocalProcessStatusList(entries,16,&count) || count>16)return false;
    for(unsigned i=0;i<count;++i)if(entries[i].id==token)return false;
    return true;
}
}
/* The host-visible GPU memory each program may hold: the title's choice, PROTON_GPU_HOST_MIB (a game.json can set it),
 * 1 GiB by default. The broker's own default, 128 MiB, stopped a 32-bit game whose 64 MiB textures upload through
 * Zink's host-visible staging (2026-10-07; record removed from the tree, in git history); the memory comes from the application pool. */
static unsigned long long proton_gpu_host_limit()
{
    const char *text=getenv("PROTON_GPU_HOST_MIB");
    const unsigned long long mib=text && *text ? strtoull(text,nullptr,10) : 1024;
    return mib<<20;
}
void proton_gpu_broker_set_local_refused(void (*hook)(int pid,unsigned long long bytes))
{
    ps5_gpu_broker_set_local_refused(hook);
}
bool proton_gpu_broker_start(bool capture,bool local)
{
    const unsigned long long host=proton_gpu_host_limit();
    if(!ps5_gpu_broker_set_host_limit(host))
        fprintf(stderr,"[PS5 Proton] GPU broker: host-visible limit of %llu MiB refused; the broker's default stays\n",host>>20);
    // The console's model, which every GPU client is told at INIT and gives games as its GPU's identity
    // (src/pw_gpu_identity.h): the generation from the application's direct pool (src/pw_memory_guard.h), none when its
    // size is not known.
    const uint64_t direct=(uint64_t)sceKernelGetDirectMemorySize();
    const unsigned model=!direct ? PW_GPU_MODEL_UNKNOWN :
        pw_console_kind_of(direct)==PW_CONSOLE_PRO ? PW_GPU_MODEL_PRO : PW_GPU_MODEL_STANDARD;
    if(!ps5_gpu_broker_set_console_model(model))
        fprintf(stderr,"[PS5 Proton] GPU broker: console model %u refused; clients keep RADV's own GPU identity\n",model);
    return ps5_gpu_broker_start(proton_app_root(),capture,local,proton_native_absent);
}
int proton_gpu_broker_open(int *cookie,int pid) { return ps5_gpu_broker_open_for_pid(cookie,pid); }
int proton_gpu_broker_open_in_owner(int *cookie) { return ps5_gpu_broker_open_in_owner(cookie); }
void proton_gpu_broker_close(int cookie,int token) { ps5_gpu_broker_close(cookie,token); }
bool proton_gpu_broker_stop() { return ps5_gpu_broker_stop(); }
bool proton_gpu_broker_warm(FILE *out) { return ps5_gpu_broker_warm(out); }
void proton_gpu_broker_set_desktop(int (*foreground)(),void (*cursor)(int *,int *))
{ ps5_gpu_broker_set_desktop(foreground,cursor); }
void proton_gpu_broker_set_windows(int (*snapshot)(void *,unsigned)) { ps5_gpu_broker_set_windows(snapshot); }
int proton_gpu_broker_window_attach(int pid,int fd,size_t bytes) { return ps5_gpu_broker_window_attach(pid,fd,bytes); }
void proton_gpu_broker_frame_attach(int pid,int fd) { ps5_gpu_broker_frame_attach(pid,fd); }
void proton_gpu_broker_frame_detach(int pid) { ps5_gpu_broker_frame_detach(pid); }
unsigned long long proton_gpu_broker_flips() { return ps5_gpu_broker_flips(); }
unsigned proton_gpu_broker_display_hz() { return ps5_gpu_broker_display_hz(); }
int proton_gpu_broker_game_mode() { return ps5_gpu_broker_game_mode(); }
bool proton_gpu_broker_capture_next(const char *path) { return ps5_gpu_broker_capture_next(path); }
bool proton_gpu_broker_set_cover(const void *pixels) { return ps5_gpu_broker_set_cover(pixels); }
void proton_gpu_broker_health(ProtonGpuHealth *health)
{
    Ps5GpuBrokerHealth now{};
    ps5_gpu_broker_health(&now);
    *health={now.lock_busy,now.display_busy,now.fences_issued,now.fences_completed,now.phase,now.phase_ms,now.holder_tid};
}
void proton_gpu_broker_set_away(bool away) { ps5_gpu_broker_set_away(away); }
void proton_gpu_broker_set_shared_memory(int (*alloc)(size_t),int (*touch)(int,size_t,size_t)) { ps5_gpu_broker_set_shared_memory(alloc,touch); }
void proton_gpu_broker_set_dmem_memory(int (*alloc)(size_t,int,int64_t *),int (*release)(int64_t,size_t)) { ps5_gpu_broker_set_dmem_memory(alloc,release); }

#ifdef PS5_SERVICE_NATIVE
#include "proton_gpu_broker_probe.h"
#include "../src/pw_fd_census.h"
#include <functional>
extern "C" {
int sceSystemServiceGetAppStatus(void *);
int sceSystemServiceAddLocalProcess(int,const char *,const char *const *,const void *);
int sceSystemServiceKillLocalProcess(int,int);
#include "../wine/ps5/pw_wine_process_io.h"
}
namespace {
int serve_go;
std::function<void(unsigned)> *serve_function;
struct Options { uint32_t size;int32_t fd,crash;uint32_t other[15]; };
static_assert(sizeof(Options)==72);
uint64_t expected_hash(unsigned client,unsigned cycle)
{
    const uint32_t pixel=0xff000000u | (client ? 0xff00u : 0xffu) | (cycle ? 0xff0000u : 0);
    uint64_t hash=1469598103934665603ull;for(unsigned i=0;i<4096;++i)hash=(hash^pixel)*1099511628211ull;
    return hash;
}
}
int proton_gpu_server_probe(FILE *out,const char *identity,bool draw,bool present,bool local)
{
    client_absent=proton_native_absent;
    retain_on_failure=true;wedged=false;test_fail_submit=0;
    ProtonGpuDisplay display;
    // Static: two clients' buffer tables do not belong on the main thread's stack.
    static Client clients[2];for(auto &c:clients)reset(c);
    uint32_t app[4]{};PwFdCensus before{},after{};
    bool okay=!sceSystemServiceGetAppStatus(app) && app[0] && radv_ps5_platform_init() && proton_gpu_broker_warm(out);
    if(present && okay) {
        PwFdCensus cold{},warm{};pw_fd_census(&cold);
        ProtonGpuDisplay warmup;uint32_t black[64*64]{};
        okay=!warmup.present(black,64,64,256,out,nullptr);
        if(!warmup.close(out))retain(out,"display_warmup_teardown_failed");
        pw_fd_census(&warm);
        fprintf(out,"gpu-display warmup=1 descriptors_cold=%u descriptors_warm=%u verdict=%d\n",cold.open,warm.open,okay);fsync(fileno(out));
    }
    {
        const uint64_t t0=radv_ps5_now_ns(),c0=__builtin_ia32_rdtsc();sceKernelUsleep(20000);
        const uint64_t t1=radv_ps5_now_ns(),c1=__builtin_ia32_rdtsc();
        fprintf(out,"gpu-vulkan tsc ticks=%llu ns=%llu\n",(unsigned long long)(c1-c0),(unsigned long long)(t1-t0));
    }
    // The serving threads start before the baseline and take their heap arena first: the title heap keeps one per
    // allocating thread for the process's life, which otherwise shows as direct memory not returned (async-12).
    // Lines the serving threads write go through a pipe to the results file (LogPipe); opened before the baseline.
    LogPipe serving_pipe;FILE *const serving_log=serving_pipe.open(out) ? serving_pipe.front : out;
    fprintf(serving_log,"gpu-vulkan serving_log pipe=%d\n",serving_log!=out);  // its buffer is taken before the baseline
    pthread_t threads[2];bool running[2]{};
    struct Start { unsigned index; } starts[2]{{0},{1}};
    serve_go=0;serve_function=nullptr;
    {
        pthread_attr_t attributes;pthread_attr_init(&attributes);pthread_attr_setstacksize(&attributes,1u<<20);
        for(unsigned i=0;i<2 && okay;++i)
            running[i]=!pthread_create(&threads[i],&attributes,[](void *argument) -> void * {
                free(malloc(64));
                const unsigned index=static_cast<Start *>(argument)->index;
                while(!__atomic_load_n(&serve_go,__ATOMIC_ACQUIRE))sceKernelUsleep(1000);
                if(serve_function)(*serve_function)(index);
                return nullptr;
            },&starts[i]);
        pthread_attr_destroy(&attributes);
        sceKernelUsleep(20000);
    }
    host_ranges_reserve(out);
    pw_fd_census(&before);const uint64_t direct_before=radv_ps5_memory_available_bytes();
    size_t flexible_before=0,flexible_after=0;
    const int flexible_status=sceKernelAvailableFlexibleMemorySize(&flexible_before);
    uint64_t next_window=0x220000000ull,next_high=0x5000000000ull;
    for(unsigned i=0;i<2 && okay;++i) {
        int pair[2]{-1,-1};okay=pw_child_socketpair(pair,sizeof(PwGpuMessage))==0;
        if(!okay)break;
        Options options{};options.size=sizeof(options);options.fd=pair[1];options.crash=1;options.other[0]=uint32_t(-1);
        options.other[3]=2;options.other[4]=0x80000000;
        const char *args[]{"/app0/service-native-helper.bin","--service-native-vulkan-helper",identity,i ? "1" : "0",nullptr};
        clients[i].service=sceSystemServiceAddLocalProcess(app[0],args[0],args,&options);
        close(pair[1]);clients[i].socket=pair[0];okay=clients[i].service>0;
        // The existing elevation contract has one shared request pathname.
        // Complete each single-threaded startup before spawning the next client.
        if(okay) {
            pollfd initial{clients[i].socket,POLLIN,0};PwGpuMessage hello{};int fds[2];unsigned count=0;
            okay=poll(&initial,1,15000)==1 && (initial.revents&POLLIN);
            if(okay) {
                const ssize_t received=pw_child_recv(clients[i].socket,&hello,sizeof(hello),fds,&count);
                for(unsigned n=0;n<count;++n)close(fds[n]);
                okay=received==sizeof(hello) && !count && hello.magic==PW_GPU_MAGIC && hello.operation==PW_GPU_INIT &&
                    hello.sequence==1 && !hello.status && hello.argument[0]>0 && hello.argument[0]!=uint64_t(getpid()) && !hello.argument[1];
                fprintf(out,"gpu-vulkan client=%u bootstrap_stage=%llu bootstrap_error=%#llx handshake=%d\n",i,
                    (unsigned long long)hello.argument[1],(unsigned long long)hello.argument[2],okay);fsync(fileno(out));
                if(okay) {
                    clients[i].pid=(int)hello.argument[0];clients[i].sequence=1;
                    fprintf(out,"gpu-vulkan client=%u ranges_reserved=%llu\n",i,(unsigned long long)hello.argument[2]);
                    hello.argument[2]=hello.argument[3]=0;
                    if(local) { hello.argument[2]=local_offer();hello.argument[3]=PW_GPU_LIMIT; }
                    okay=pw_child_send(clients[i].socket,&hello,sizeof(hello),nullptr,0)==0;
                    fprintf(out,"gpu-vulkan client=%u pid=%d initialized=1 local_offer=%llu\n",i,clients[i].pid,
                        (unsigned long long)hello.argument[2]);
                }
            }
        }
        fprintf(out,"gpu-vulkan client=%u service=%d spawned=%d\n",i,clients[i].service,okay);fsync(fileno(out));
    }
    const uint64_t deadline=radv_ps5_now_ns()+150000000000ull;
    // One thread per client, each waiting on its own socket alone (await_message), the broker's work serialized by one
    // lock, as the Wine broker serves its clients.
    // A spinning lock: the work under it is tens of microseconds, and a thread that slept on a contended mutex was
    // woken milliseconds late (graphics-child-gpu-async-14).
    int serving=0;
    auto lock_serving=[&] { while(__atomic_exchange_n(&serving,1,__ATOMIC_ACQUIRE))sched_yield(); };
    auto unlock_serving=[&] { __atomic_store_n(&serving,0,__ATOMIC_RELEASE); };
    auto serve=[&](unsigned i) {
        FILE *const serve_log=serving_log;
        auto &c=clients[i];uint64_t spin_until=0,waiting_since=radv_ps5_now_ns();
        while(__atomic_load_n(&okay,__ATOMIC_RELAXED) && !c.done && radv_ps5_now_ns()<deadline) {
            bool spun=true;
            const int found=await_message(c.socket,spin_until,&spun);
            if(found<0) { __atomic_store_n(&okay,false,__ATOMIC_RELAXED);break; }
            if(!found)continue;
            const uint64_t seen=radv_ps5_now_ns();
            lock_serving();
            const uint64_t locked=radv_ps5_now_ns();unsigned handled_operation=0;
            // Per message: the wait before it appeared (bucketed), how it was found, the lock wait, the handling.
            const uint64_t waited=seen-waiting_since;
            ++c.found[spun ? 0 : 1];++c.waited[waited<100000 ? 0 : waited<1000000 ? 1 : waited<2000000 ? 2 : 3];
            c.lock_ns+=locked-seen;
            do {
            PwGpuMessage message{};int fds[2];unsigned count=0;
            const ssize_t received=pw_child_recv(c.socket,&message,sizeof(message),fds,&count);
            for(unsigned n=0;n<count;++n)close(fds[n]);
            handled_operation=message.operation;
            if(received!=sizeof(message) || count || message.magic!=PW_GPU_MAGIC || message.status || message.sequence!=c.sequence+1) { okay=false;break; }
            c.sequence=message.sequence;int fd=-1;message.status=EINVAL;uint64_t submitted_at=0;
            switch(message.operation) {
            case PW_GPU_INIT:
                if(c.pid<0 && message.argument[0]>0 && message.argument[0]!=uint64_t(getpid())) {
                    c.pid=(int)message.argument[0];message.status=0;
                    fprintf(serve_log,"gpu-vulkan client=%u pid=%d initialized=1\n",i,c.pid);
                }break;
            case PW_GPU_ALLOC:
                if(c.pid>0)message.status=allocate(c,message,next_window,next_high,fd);break;
            case PW_GPU_FREE:
                for(auto &b:c.buffers)if(b.token && b.token==message.argument[0] && b.address==message.address && b.bytes==message.bytes) {
                    if(c.tess && pw_gpu_contains(b.address,b.bytes,c.tess,c.tess_bytes)){c.tess=0;c.tess_bytes=0;}
                    free_buffer(c,b,serve_log);message.status=0;break;
                }break;
            case PW_GPU_SUBMIT: {
                const uint64_t received=__builtin_ia32_rdtsc();
                if(message.argument[2] && message.argument[2]<=received) { c.time_recv+=received-message.argument[2];++c.recv_samples; }
                if(c.pid>0)message.status=submit(c,message,serve_log,i);submitted_at=radv_ps5_now_ns();
                message.argument[3]=0;break;
            }
            case PW_GPU_TESS:
                if(owned(c,message.address,message.bytes)) { c.tess=message.address;c.tess_bytes=(uint32_t)message.bytes;message.status=0; }break;
            case PW_GPU_OFFCHIP:
                if(message.argument[0]<=3 && message.argument[1]<=511) {
                    c.granularity=(uint32_t)message.argument[0];c.buffering=(uint32_t)message.argument[1];c.offchip=true;message.status=0;
                }break;
            case PW_GPU_PRESENT:
                if(present && message.argument[0]==64 && message.argument[1]==64 && message.argument[2]==256 &&
                   message.argument[3]<2 && message.bytes==16384 && !(message.address&3) && owned(c,message.address,message.bytes) &&
                   !(c.presentations&(1u<<message.argument[3]))) {
                    radv_ps5_cpu_flush((void *)message.address,message.bytes);
                    uint64_t hash=1469598103934665603ull;
                    for(unsigned n=0;n<4096;++n)hash=(hash^((uint32_t *)message.address)[n])*1099511628211ull;
                    if(hash==expected_hash(i,(unsigned)message.argument[3])) {
                        char path[160];snprintf(path,sizeof(path),"/data/homebrew/PPSA99170/gpu-present-%u-%llu.ppm",i,(unsigned long long)message.argument[3]);
                        const uint64_t present_start=radv_ps5_now_ns();
                        message.status=display.present((void *)message.address,64,64,256,serve_log,nullptr);
                        const uint64_t presented=radv_ps5_now_ns();
                        // The capture is a separate step, after the timed present: it writes a picture to disk.
                        if(!message.status && !display.capture_last(path))message.status=EIO;
                        fprintf(serve_log,"gpu-present client=%u present_us=%llu copy_us=%llu\n",i,(unsigned long long)(presented-present_start)/1000,
                            (unsigned long long)display.last_copy_ns/1000);
                        if(!message.status)c.presentations|=1u<<message.argument[3];
                        fprintf(serve_log,"gpu-present client=%u cycle=%llu hash=%#llx flip=%llu verdict=%d\n",i,
                            (unsigned long long)message.argument[3],(unsigned long long)hash,(unsigned long long)display.flips,!message.status);
                        sceKernelUsleep(300000);
                    }
                }break;
            case PW_GPU_IMAGE:
                if(message.argument[3]==4) { message.status=0;break; }  // a no-op round trip
                if(message.argument[3]==3 && message.argument[0]<16 && message.argument[0]%8==6) {
                    message.status=0;++c.timings;
                    fprintf(serve_log,"gpu-vulkan client=%u cpus cycle=%llu allowed=%#llx seen=%#llx\n",i,(unsigned long long)message.argument[0]/8,
                        (unsigned long long)message.argument[1],(unsigned long long)message.argument[2]);
                    break;
                }
                if(message.argument[3]==3 && message.argument[0]>=18 && message.argument[0]<24 && message.argument[1]>0) {
                    message.status=0;++c.timings;
                    fprintf(serve_log,"gpu-vulkan client=%u rpc_split cycle=%llu part=%s submissions=%llu ns=%llu per_submission_us=%llu\n",i,
                        (unsigned long long)(message.argument[0]-18)/3,(const char *[]){"lock","send","wait"}[(message.argument[0]-18)%3],
                        (unsigned long long)message.argument[1],(unsigned long long)message.argument[2],
                        (unsigned long long)(message.argument[2]/message.argument[1]/1000));
                    break;
                }
                if(message.argument[3]==3 && (message.argument[0]==16 || message.argument[0]==17) && message.argument[1]>0) {
                    message.status=0;++c.timings;
                    fprintf(serve_log,"gpu-vulkan client=%u reply_transit cycle=%llu submissions=%llu ns=%llu per_submission_us=%llu\n",i,
                        (unsigned long long)message.argument[0]-16,(unsigned long long)message.argument[1],(unsigned long long)message.argument[2],
                        (unsigned long long)(message.argument[2]/message.argument[1]/1000));
                    break;
                }
                if(message.argument[3]==3 && message.argument[0]<16 && (message.argument[0]%8<6 || message.argument[0]%8==7) && message.argument[1]>0) {
                    message.status=0;++c.timings;
                    fprintf(serve_log,"gpu-vulkan client=%u timing cycle=%llu phase=%llu submissions=%llu ns=%llu per_submission_us=%llu\n",i,
                        (unsigned long long)message.argument[0]/8,(unsigned long long)message.argument[0]%8,(unsigned long long)message.argument[1],
                        (unsigned long long)message.argument[2],(unsigned long long)(message.argument[2]/message.argument[1]/1000));
                    break;
                }
                if(message.argument[3]==2 && message.argument[0]<4 && !(c.coherence&(1u<<message.argument[0]))) {
                    // Risk 5: a coherence check with no cache maintenance; 4,096 words must have matched.
                    c.coherence|=1u<<message.argument[0];c.coherent+=message.argument[1]==4096;message.status=0;
                    fprintf(serve_log,"gpu-vulkan client=%u coherence slot=%llu cycle=%llu kind=%llu words=%llu hash=%#llx verdict=%d\n",i,
                        (unsigned long long)message.argument[0],(unsigned long long)message.argument[0]/2,(unsigned long long)message.argument[0]%2,
                        (unsigned long long)message.argument[1],(unsigned long long)message.argument[2],message.argument[1]==4096);
                    break;
                }
                if(message.argument[3]==uint64_t(draw) && message.argument[0]<2 && message.argument[1]==4096 && message.argument[2]==expected_hash(i,(unsigned)message.argument[0]) &&
                   !(c.images&(1u<<message.argument[0]))) {
                    c.images|=1u<<message.argument[0];message.status=0;
                    fprintf(serve_log,"gpu-vulkan client=%u cycle=%llu pixels=4096 hash=%#llx device_created=1 draw=%d verdict=1\n",i,
                        (unsigned long long)message.argument[0],(unsigned long long)message.argument[2],draw);
                }break;
            case PW_GPU_DONE:
                c.done=true;c.passed=c.pid>0 && message.argument[0]==0 && message.argument[1]==2 &&
                    c.images==3 && (!present || c.presentations==3) && message.argument[2]==expected_hash(i,1) && message.argument[3]==c.allocated && c.submissions>=2;
                message.status=0;
                // With device-local memory offered, the client must have used it; and all four coherence checks pass.
                if(local && !c.local_peak)c.passed=false;
                if(c.coherence!=15 || c.coherent!=4 || c.timings!=24)c.passed=false;
                fprintf(serve_log,"gpu-vulkan client=%u submit_time submissions=%u waits=%llu stage_us=%llu submit_us=%llu wait_us=%llu release_us=%llu recv_ticks=%llu recv_samples=%llu poll_missed=%llu\n",i,
                    c.submissions,(unsigned long long)c.waits,(unsigned long long)c.time_stage/1000,(unsigned long long)c.time_submit/1000,
                    (unsigned long long)c.time_wait/1000,(unsigned long long)c.time_release/1000,(unsigned long long)c.time_recv,
                    (unsigned long long)c.recv_samples,(unsigned long long)c.poll_missed);
                fprintf(serve_log,"gpu-vulkan client=%u serving handled=%llu found_spinning=%llu found_after_poll=%llu waited_lt100us=%llu "
                    "waited_lt1ms=%llu waited_lt2ms=%llu waited_ge2ms=%llu lock_us=%llu handle_us=%llu\n",i,(unsigned long long)c.handled,
                    (unsigned long long)c.found[0],(unsigned long long)c.found[1],(unsigned long long)c.waited[0],(unsigned long long)c.waited[1],
                    (unsigned long long)c.waited[2],(unsigned long long)c.waited[3],(unsigned long long)c.lock_ns/1000,(unsigned long long)c.handle_ns/1000);
                for(unsigned op=1;op<16;++op)if(c.op_count[op])
                    fprintf(serve_log,"gpu-vulkan client=%u operation_time op=%u count=%llu total_us=%llu max_us=%llu\n",i,op,
                        (unsigned long long)c.op_count[op],(unsigned long long)c.op_ns[op]/1000,(unsigned long long)c.op_max_ns[op]/1000);
                fprintf(serve_log,"gpu-vulkan client=%u pid=%d images=%llu hash=%#llx live_bytes=%llu verdict=%d local_bytes=%llu local_peak=%llu\n",i,c.pid,
                    (unsigned long long)message.argument[1],(unsigned long long)message.argument[2],(unsigned long long)c.allocated,c.passed,
                    (unsigned long long)c.local,(unsigned long long)c.local_peak);
                break;
            }
            if((trace || message.status) && !(message.operation==PW_GPU_IMAGE && message.argument[3]==4))
                fprintf(serve_log,"gpu-vulkan client=%u operation=%u status=%#x allocated=%llu local=%llu\n",i,message.operation,unsigned(message.status),(unsigned long long)c.allocated,(unsigned long long)c.local);
            if(submitted_at)message.argument[3]=__builtin_ia32_rdtsc();
            if(pw_child_send(c.socket,&message,sizeof(message),fd>=0 ? &fd : nullptr,fd>=0 ? 1 : 0))okay=false;
            // From the native submit's return to the reply having gone: what the owner's own threads cost a submission.
            if(submitted_at) { c.time_release+=radv_ps5_now_ns()-submitted_at;submitted_at=0; }
            kick();
            } while(false);
            {
                const uint64_t spent=radv_ps5_now_ns()-locked;c.handle_ns+=spent;++c.handled;
                const unsigned op=handled_operation<16 ? handled_operation : 0;c.op_ns[op]+=spent;++c.op_count[op];
                if(spent>c.op_max_ns[op])c.op_max_ns[op]=spent;
            }
            unlock_serving();
            spin_until=radv_ps5_now_ns()+kServeSpinNs;waiting_since=radv_ps5_now_ns();
        }
    };
    std::function<void(unsigned)> serving_function=serve;serve_function=&serving_function;
    __atomic_store_n(&serve_go,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<2;++i)if(running[i])pthread_join(threads[i],nullptr);
    okay &= running[0] && running[1];
    okay &= clients[0].passed && clients[1].passed && clients[0].pid!=clients[1].pid;
    for(unsigned i=0;i<2;++i) {
        auto &c=clients[i];
        if(c.service>0) {
            const int status=sceSystemServiceKillLocalProcess(app[0],c.service);
            for(unsigned n=0;n<200 && !absent(c.service);++n)sceKernelUsleep(10000);
            if(!absent(c.service))retain(serving_log,"live_vulkan_helper");
            fprintf(serving_log,"gpu-vulkan client=%u closed=%#x absent=1\n",i,unsigned(status));okay &= status==0;
        }
    }
    // Nothing a client used is released before the GPU has finished every submission.
    if(!drain(5000000000ull))retain(serving_log,"pending_vulkan_submission");
    fprintf(serving_log,"gpu-broker fences issued=%u completed=%u drains=%llu state_drains=%llu kicks=%llu kick_us=%llu\n",fences.issued,
        fences.page.cpu ? completed() : 0,(unsigned long long)fences.drains,(unsigned long long)fences.state_drains,
        (unsigned long long)fences.kicks,(unsigned long long)fences.kick_ns/1000);
    for(unsigned i=0;i<2;++i) {
        auto &c=clients[i];
        for(auto &b:c.buffers)if(b.token)release(b);
        if(c.socket>=0)close(c.socket);
    }
    fences_reset();
    host_ranges_release(serving_log);
    if(present && !display.close(serving_log))retain(serving_log,"display_teardown_failed");
    pw_fd_census(&after);const uint64_t direct_after=radv_ps5_memory_available_bytes();
    serving_pipe.close();
    const int flexible_after_status=sceKernelAvailableFlexibleMemorySize(&flexible_after);
    fprintf(out,"gpu-vulkan descriptors before=%u after=%u direct_before=%llu direct_after=%llu flexible_status=%#x flexible_after_status=%#x flexible_before=%zu flexible_after=%zu\n",
        before.open,after.open,(unsigned long long)direct_before,(unsigned long long)direct_after,
        unsigned(flexible_status),unsigned(flexible_after_status),flexible_before,flexible_after);fsync(fileno(out));
    okay &= before.limit>0 && before.open>0 && before.open==after.open && direct_before==direct_after && !flexible_status && !flexible_after_status && flexible_before==flexible_after;
    return okay ? 0 : 1;
}

#endif
