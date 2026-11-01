/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sony owns native processes; Wine owns Windows process state and handles. */
#include "pw_wine_process.h"
#include "pw_wine_inherit.h"
#include "pw_wine_title_game.h"
#include "pw_wine_pe_imports.h"
#include "../../src/pw_environment.h"
#include "pw_wine_sink.h"
#include "pw_wine_process_io.h"
#include "pw_wine_threads.h"
#include "pw_wine_perf.h"
#include "pw_spawn_wait.h"
#include "../../src/pw_ntsync.h"
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/cpuset.h>
#include <time.h>
#include <unistd.h>

extern int sceKernelUsleep(unsigned);

#include "pw_payload_spawn.h"
#include "../../src/pw_memory_helper.h"
#include "../../src/pw_memory_service.h"
#include "pw_wine_codeshare.h"
#include "pw_wine_dsection.h"
#include "../../src/pw_warm_helper.h"
/* service is 1 while the slot holds a helper (-1 while one starts): every helper is a payload process. */
/* unloading: the root the title's desktop session ends on purpose (game mode, src/pw_desktop_session.h); its exit is
 * not the session's result. */
/* game_line: the program the title runs itself started this helper, or a helper it started did (a game's launcher in
 * the title starts the game): the title's end waits for it with no time limit (pw_ps5_wait_children). */
struct child { int fd, service, pid, closing, initial, unloading, gpu_cookie, memory, code_kib, game_line; void *input_page, *audio_ring; };
static struct child children[32];
static pthread_mutex_t child_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t child_closed = PTHREAD_COND_INITIALIZER;
static int child_control = -1;
static int (*gpu_open)(int *,int);
static void (*gpu_close)(int,int);
/* The GPU broker's open, one helper at a time. A whole start used to hold it, for the elevation service's one request
 * path; helpers no longer elevate (native/proton_wine_child.c), so starts overlap and only the broker's open waits. */
static pthread_mutex_t startup_lock=PTHREAD_MUTEX_INITIALIZER;
void pw_ps5_set_gpu_broker(int (*open)(int *,int),void (*close)(int,int))
{ gpu_open=open;gpu_close=close; }
static void (*frame_attach)(int,int);
static void (*frame_detach)(int);
static int (*window_attach)(int,int,size_t);
void pw_ps5_set_window_sink(int (*attach)(int,int,size_t)) { window_attach=attach; }
static int (*title_dns_config)(struct pw_dns_config *);
void pw_ps5_set_dns_config(int (*read)(struct pw_dns_config *)) { title_dns_config=read; }
static int (*title_resolve)(const char *,unsigned char[4]);
void pw_ps5_set_resolver(int (*resolve)(const char *,unsigned char[4])) { title_resolve=resolve; }
/* A helper's lookup, answered on its own thread: a name can take the resolver seconds, and the helper's monitor
 * must stay free for the helper's other messages. */
struct resolve_work { char name[256]; int reply; };
static void *resolve_worker(void *opaque)
{
    struct resolve_work *work=opaque;
    struct pw_child_resolved answer={EAI_FAIL,{0}};
    if(title_resolve)answer.status=title_resolve(work->name,answer.address);
    (void)pw_child_send(work->reply,&answer,sizeof(answer),NULL,0);
    close(work->reply);free(work);
    return NULL;
}
void pw_ps5_set_frame_sink(void (*attach)(int,int),void (*detach)(int))
{ frame_attach=attach;frame_detach=detach; }

static char initial_environment[4096];
static size_t initial_environment_bytes=2;
int pw_ps5_set_initial_environment(const char *block,size_t bytes)
{
    if(bytes>sizeof(initial_environment)) { errno=E2BIG;return -1; }
    if(pw_environment_validate(block,bytes))return -1;
    memcpy(initial_environment,block,bytes);initial_environment_bytes=bytes;
    return 0;
}
const char *pw_ps5_initial_environment(void) { return initial_environment; }
static unsigned pending_spawns;
/* When pending_spawns last went from 0 to 1, on payload_ms's clock; 0 while none is in flight (pw_spawn_wait.h). */
static long long pending_since_ms;
static int root_result=-1;
int pw_ps5_root_result(void) { pthread_mutex_lock(&child_lock);int result=root_result;pthread_mutex_unlock(&child_lock);return result; }
static int (*graphics_start)(int,const struct pw_child_bootstrap *);
void pw_ps5_set_graphics_start(int (*start)(int,const struct pw_child_bootstrap *)) { graphics_start=start; }
/* The one program the title runs itself beside the broker (docs/TITLE_CLIENT.md): the title's start of it, set only when
 * the title offers one (WINE_PS5_TITLE_PROCESS). */
static int (*title_client_start)(int,const struct pw_child_bootstrap *);
void pw_ps5_set_title_client_start(int (*start)(int,const struct pw_child_bootstrap *)) { title_client_start=start; }
static int in_title_client(void)
{
    const char *value=getenv("WINE_PS5_TITLE_CLIENT");
    return value && !strcmp(value,"1");
}
static int (*application_lookup)(const char *,const char *,char *,size_t,uint32_t *);
static void (*child_notice)(int,int,int);
void pw_ps5_set_child_notice(void (*notice)(int,int,int)) { child_notice=notice; }
void pw_ps5_set_application_lookup(int (*lookup)(const char *,const char *,char *,size_t,uint32_t *))
{ application_lookup=lookup; }
static int (*fixed_base_lookup)(const char *);
void pw_ps5_set_fixed_base_lookup(int (*lookup)(const char *)) { fixed_base_lookup=lookup; }
/* The image a helper runs, as Windows names it: a started process's, or the initial client's argv[1] (argv[0] is the
 * loader name); NULL when there is none. */
static const char *spawn_image(const struct pw_child_bootstrap *boot)
{
    if(boot->image[0]) return boot->image;
    if(!(boot->flags&PW_CHILD_INITIAL) || boot->argc<2) return NULL;
    const size_t first=strnlen(boot->initial_args,sizeof(boot->initial_args));
    if(first+1>=sizeof(boot->initial_args)) return NULL;
    const char *second=boot->initial_args+first+1;
    return memchr(second,0,sizeof(boot->initial_args)-first-1) && *second ? second : NULL;
}
static void descriptor_metrics(const char *stage)
{
    if (!getenv("WINE_PS5_CHILD_METRICS")) return;
    unsigned count=0;
    int limit=getdtablesize();
    if(limit>65536)limit=65536;
    for(int fd=0;fd<limit;++fd) if(fcntl(fd,F_GETFD)>=0) ++count;
    fprintf(stderr,"wine-ps5: child metrics stage=%s native_fds=%u limit=%d\n",stage,count,limit);
}

static int ready_for(int fd, short events, int milliseconds)
{
    struct pollfd p = {fd, events, 0};
    int ret;
    do ret = poll(&p, 1, milliseconds); while (ret < 0 && errno == EINTR);
    if (ret == 1 && (p.revents & events)) return 1;
    if (ret >= 0) errno = ret ? EPIPE : ETIMEDOUT;
    return 0;
}
static int ready(int fd, short events) { return ready_for(fd, events, 10000); }
static int copy_string(char *dest, size_t size, const char *src)
{
    if (!src) src = "";
    if (strlen(src) >= size) { errno = E2BIG; return -1; }
    strcpy(dest, src); return 0;
}
/* Set once the title ends the game (pw_ps5_end_spawns): no helper starts, and one that does not leave after its close
 * is sent SIGKILL and waited for until ESRCH, so the title is never replaced while it may run (src/pw_hang_breaker.h). */
static int spawns_ended;
/* The helper started ahead of need, under child_lock (src/pw_warm_helper.h); warm_changed wakes its keeper. */
static PwWarmHelper warm = PW_WARM_HELPER_INIT;
static pthread_cond_t warm_changed = PTHREAD_COND_INITIALIZER;
void pw_ps5_end_spawns(void)
{
    pthread_mutex_lock(&child_lock);
    spawns_ended = 1;
    pthread_cond_broadcast(&warm_changed);
    pthread_mutex_unlock(&child_lock);
}
static int close_until_gone(int pid, int control)
{
    const int status = payload_close(pid, control);
    pthread_mutex_lock(&child_lock);
    const int ending = spawns_ended;
    pthread_mutex_unlock(&child_lock);
    if (!status) return 0;
    /* A helper still there 10 s after its channel closed is hung or stopped, game ending or not: a native crash in a
     * 32-bit helper (i386 quartz avisplit, 2026-10-25) left it stopped by the kernel's debug subsystem, no close reached
     * it, nothing ended it, and the console guard refused every later launch until a restart. */
    fprintf(stderr, "wine-ps5: child native=%d stayed after its close%s: SIGKILL (errno %d)\n", pid,
            ending ? " while the game ends" : "", kill(pid, SIGKILL) ? errno : 0);
    for (unsigned tenths = 1; !payload_absent(pid); ++tenths) {
        if (!ending && tenths == 20)   /* a stopped process: continued, so that the SIGKILL is acted on */
            fprintf(stderr, "wine-ps5: child native=%d still there 2 s after SIGKILL: SIGCONT (errno %d)\n", pid,
                    kill(pid, SIGCONT) ? errno : 0);
        if (!ending && tenths == 100) {
            /* bounded outside a game's end, where the title's replacement waits for its own SIGKILLs (end_game) */
            fprintf(stderr, "wine-ps5: child native=%d still there 10 s after SIGKILL; left to the console guard\n", pid);
            return status;
        }
        if (tenths % 600 == 0) fprintf(stderr, "wine-ps5: still waiting for child native=%d (%u s)\n", pid, tenths / 10);
        usleep(100000);
    }
    fprintf(stderr, "wine-ps5: child native=%d gone after SIGKILL\n", pid);
    return 0;
}
/* Makes the next helper, parks it, and closes it PW_WARM_KEEP_MS after the last start unless a start took it. */
static void *warm_keeper(void *unused)
{
    (void)unused;
    int pair[2] = {-1, -1}, pid = 0;
    struct payload_spawn_times times;
    const long long began = payload_ms();
    if (pw_child_socketpair(pair, sizeof(struct pw_child_spawn)) || payload_spawn(pair[1], &pid, &times)) {
        const int saved = errno;
        if (pair[1] >= 0) close(pair[1]);
        if (pid > 0) close_until_gone(pid, pair[0]); else if (pair[0] >= 0) close(pair[0]);
        pthread_mutex_lock(&child_lock); warm.making = 0; pthread_mutex_unlock(&child_lock);
        fprintf(stderr, "wine-ps5: warm helper not made (errno %d); the next start is a cold one\n", saved);
        return NULL;
    }
    close(pair[1]);
    pthread_mutex_lock(&child_lock);
    if (!pw_warm_park(&warm, pair[0], pid, spawns_ended)) {
        pthread_mutex_unlock(&child_lock);
        close_until_gone(pid, pair[0]);
        return NULL;
    }
    fprintf(stderr, "wine-ps5: warm helper native=%d parked in %lld ms\n", pid, payload_ms() - began);
    long long left;
    while ((left = pw_warm_wait_ms(&warm, pid, payload_ms(), spawns_ended)) > 0) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += left / 1000;
        until.tv_nsec += (left % 1000) * 1000000;
        if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&warm_changed, &child_lock, &until);
    }
    int fd = -1, unused_pid = 0;
    const int still = !left && pw_warm_take(&warm, &fd, &unused_pid);
    pthread_mutex_unlock(&child_lock);
    if (still) {
        fprintf(stderr, "wine-ps5: warm helper native=%d closed unused\n", pid);
        close_until_gone(pid, fd);
    }
    return NULL;
}
/* After a start: the next helper is made while this one runs, unless one is parked or being made. */
static void warm_refill(void)
{
    pthread_mutex_lock(&child_lock);
    warm.last_start_ms = payload_ms();
    const int make = pw_warm_should_make(&warm, pw_warm_enabled(getenv("WINE_PS5_WARM_HELPER")), spawns_ended);
    if (make) warm.making = 1;
    pthread_cond_broadcast(&warm_changed);
    pthread_mutex_unlock(&child_lock);
    if (!make) return;
    pthread_t thread;
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    if (!error) error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (!error) error = pthread_create(&thread, &attributes, warm_keeper, NULL);
    pthread_attr_destroy(&attributes);
    if (error) { pthread_mutex_lock(&child_lock); warm.making = 0; pthread_mutex_unlock(&child_lock); }
}
/* The parked helper, if any and still there: its loader sends nothing before the bootstrap, so anything to read, or a
 * hangup, is a helper that is gone. */
static int warm_take_alive(int *fd, int *pid)
{
    pthread_mutex_lock(&child_lock);
    const int taken = pw_warm_take(&warm, fd, pid);
    if (taken) pthread_cond_broadcast(&warm_changed);
    pthread_mutex_unlock(&child_lock);
    if (!taken) return 0;
    struct pollfd probe = {*fd, POLLIN, 0};
    if (poll(&probe, 1, 0) == 0) return 1;
    fprintf(stderr, "wine-ps5: warm helper native=%d gone while parked (revents %#x); a cold start instead\n", *pid,
            (unsigned)probe.revents);
    close_until_gone(*pid, *fd);
    *fd = -1; *pid = 0;
    return 0;
}
static int spawn_native(int server_fd, const struct pw_child_bootstrap *boot, int *native_pid);
/* The title runs a program itself (title_client_start took it): what it starts, and what those start, are its game's. */
static int title_client_live;
/* The title runs a session's root itself (proton_wine.cpp, start_title_root): what it starts is its game's too. */
void pw_ps5_set_title_client_live(void) { title_client_live=1; }
static int game_line_of(int pid)
{
    int line = 0;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32; ++i) if (children[i].service > 0 && children[i].pid == pid) line = children[i].game_line;
    pthread_mutex_unlock(&child_lock);
    return line;
}
static void game_line_mark(int pid, const char *image)
{
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32; ++i) if (children[i].service > 0 && children[i].pid == pid) children[i].game_line = 1;
    pthread_mutex_unlock(&child_lock);
    fprintf(stderr, "wine-ps5: helper %d (%s) is the game's: the title's end waits for it\n", pid,
            image && *image ? pw_title_game_name(image) : "a program");
}
struct spawn_work { struct pw_child_spawn request; int fds[2]; long long received_ms; };
static void *spawn_worker(void *opaque)
{
    struct spawn_work *work=opaque;
    int native_pid=0;
    int result;
    /* Test only (missing-import64-title): WINE_PS5_TEST_SPAWN_STALL=<image> holds that program's start for 600 s (past the case's watchdog), as
     * the console's wedged loader held a store overlay's while the game in the title failed (pw_spawn_wait.h). */
    const char *stall=getenv("WINE_PS5_TEST_SPAWN_STALL");
    if(stall && *stall && work->request.bootstrap.image[0] &&
       !strcasecmp(pw_title_game_name(work->request.bootstrap.image),stall)) {
        fprintf(stderr,"wine-ps5: test: the start of %s is held 600 s\n",stall);
        sceKernelUsleep(600000000u);
    }
    /* Test only: force Chromium's single-process model on the CEF helper, so it cannot spawn a --type=renderer child
     * (the in-title design needs to know whether the CEF can be collapsed). */
    if (getenv("PROTON_CEF_SINGLE_PROCESS") && work->request.bootstrap.command_line[0] &&
        strstr(work->request.bootstrap.command_line, "steamwebhelper")) {
        const size_t used = strlen(work->request.bootstrap.command_line);
        if (used + sizeof(" --single-process") < sizeof(work->request.bootstrap.command_line))
            strcat(work->request.bootstrap.command_line, " --single-process");
    }
    if(work->request.bootstrap.flags==PW_CHILD_GRAPHICS && !gpu_open) {
        if(graphics_start) {
            result=graphics_start(work->fds[0],&work->request.bootstrap);
            if(!result)native_pid=getpid();
            /* The title runs one; another goes to a helper, as any process does. */
            else if(errno==EBUSY)result=spawn_native(work->fds[0],&work->request.bootstrap,&native_pid);
        } else { errno=ENOTSUP;result=-1; }
    } else if(work->request.bootstrap.flags==PW_CHILD_GRAPHICS && gpu_open && title_client_start) {
        /* Beside the broker: the title's own client, a broker client in this process; once it is taken, a helper. */
        result=title_client_start(work->fds[0],&work->request.bootstrap);
        if(!result) { native_pid=getpid();title_client_live=1; }
        else if(errno==EBUSY) {
            fprintf(stderr,"wine-ps5: %s: the title's place is taken; a helper\n",
                    work->request.bootstrap.image[0] ? pw_title_game_name(work->request.bootstrap.image) : "a program");
            result=spawn_native(work->fds[0],&work->request.bootstrap,&native_pid);
        }
    } else result=spawn_native(work->fds[0],&work->request.bootstrap,&native_pid);
    if(!result && native_pid>0 && native_pid!=getpid() && game_line_of(work->request.message.pid))
        game_line_mark(native_pid,work->request.bootstrap.image);
    struct pw_child_message reply={PW_CHILD_PROTOCOL,PW_CHILD_SPAWN_REPLY,work->request.message.pid,result ? errno : 0,native_pid};
    if(pw_child_send(work->fds[1],&reply,sizeof(reply),NULL,0) && !result && native_pid!=getpid()) pw_ps5_remote_close(native_pid);
    fprintf(stderr,"wine-ps5: broker requester=%d child=%d status=%d\n",reply.pid,native_pid,reply.value);
    /* A helper's request on the title's clock (tools/start_stages.py): read from its control socket, then answered. */
    fprintf(stderr,"wine-ps5: spawn served requester=%d native=%d received_ms=%lld replied_ms=%lld\n",reply.pid,native_pid,
            work->received_ms,payload_ms());
    close(work->fds[0]);close(work->fds[1]);free(work);
    pthread_mutex_lock(&child_lock);if(!--pending_spawns)pending_since_ms=0;pthread_mutex_unlock(&child_lock);
    return NULL;
}
static int accept_spawn(const struct pw_child_spawn *request, int fds[2])
{
    const struct pw_child_bootstrap *boot=&request->bootstrap;
    if(boot->protocol!=PW_CHILD_PROTOCOL || boot->size!=sizeof(*boot) || (boot->flags && boot->flags!=PW_CHILD_GRAPHICS) || boot->argc || boot->application_bytes ||
       !memchr(boot->runtime,0,sizeof(boot->runtime)) || !memchr(boot->prefix,0,sizeof(boot->prefix)) ||
       !memchr(boot->cwd,0,sizeof(boot->cwd)) || !memchr(boot->dllpath,0,sizeof(boot->dllpath)) ||
       !memchr(boot->overrides,0,sizeof(boot->overrides)) || !memchr(boot->debug,0,sizeof(boot->debug)) ||
       !memchr(boot->image,0,sizeof(boot->image)) || !memchr(boot->command_line,0,sizeof(boot->command_line))) {
        fprintf(stderr,"wine-ps5: spawn request refused: protocol %#x size %u flags %#x argc %u\n",
                boot->protocol,boot->size,boot->flags,boot->argc);
        return -1;
    }
    struct spawn_work *work=malloc(sizeof(*work));
    int error=ENOMEM;
    if(work) {
        work->request=*request;memcpy(work->fds,fds,sizeof(work->fds));work->received_ms=payload_ms();
        pthread_mutex_lock(&child_lock);if(!pending_spawns++)pending_since_ms=payload_ms();pthread_mutex_unlock(&child_lock);
        pthread_t thread;
        error=pthread_create(&thread,NULL,spawn_worker,work);
        if(!error) { pthread_detach(thread);return 0; }
        pthread_mutex_lock(&child_lock);if(!--pending_spawns)pending_since_ms=0;pthread_mutex_unlock(&child_lock);
        free(work);
    }
    struct pw_child_message reply={PW_CHILD_PROTOCOL,PW_CHILD_SPAWN_REPLY,request->message.pid,error,0};
    (void)pw_child_send(fds[1],&reply,sizeof(reply),NULL,0);
    close(fds[0]);close(fds[1]);return 0;
}
/* Shared code copies (pw_wine_codeshare.h): on unless the title's WINE_PS5_CODE_SHARE is 0. */
static int code_share_on(void)
{
    const char *value=getenv("WINE_PS5_CODE_SHARE");
    return !value || strcmp(value,"0");
}
/* The helpers' reports (PW_CHILD_CODE_MAPPED) and the registry's total change together under it, so the census never
 * reads an ended helper's report beside a registry that has let its objects go. */
static pthread_mutex_t code_census_lock = PTHREAD_MUTEX_INITIALIZER;
/* The bytes the census counts beyond the first copy of each shared object (pw_wine_codeshare.h, "Counting each shared
 * object once"): never more than the copies beyond the first that the helpers map, as of their last reports. */
uint64_t pw_ps5_code_share_duplicate_bytes(void)
{
    uint64_t mapped=0,duplicates;
    pthread_mutex_lock(&code_census_lock);
    for(unsigned i=0;i<32;++i)mapped+=(uint64_t)__atomic_load_n(&children[i].code_kib,__ATOMIC_RELAXED)<<10;
    duplicates=pw_wine_codeshare_duplicates(mapped);
    pthread_mutex_unlock(&code_census_lock);
    return duplicates;
}
/* A helper has ended (its slot is not reused before this): its report goes, the objects only it held are closed, said
 * with what is held. */
static void code_share_forget(int pid,struct child *child)
{
    PwWineCodeshareStats stats;
    pthread_mutex_lock(&code_census_lock);
    __atomic_store_n(&child->code_kib,0,__ATOMIC_RELAXED);
    const unsigned closed=pw_wine_codeshare_forget((unsigned)(child-children));
    pthread_mutex_unlock(&code_census_lock);
    pw_wine_codeshare_stats(&stats);
    if(closed || stats.finds)
        fprintf(stderr,"wine-ps5: shared code copies native=%d closed=%u held=%llu held_kib=%llu asks=%llu hits=%llu "
                "offers=%llu kept=%llu released=%llu\n",pid,closed,(unsigned long long)stats.entries,
                (unsigned long long)(stats.bytes>>10),(unsigned long long)stats.finds,(unsigned long long)stats.hits,
                (unsigned long long)stats.offers,(unsigned long long)stats.kept,(unsigned long long)stats.released);
}
static void *monitor_child(void *opaque)
{
    struct child *child = opaque;
    struct pw_child_spawn packet;
    for (;;) {
        int fds[2];unsigned count;
        ssize_t bytes=pw_child_recv(child->fd,&packet,sizeof(packet),fds,&count);
        const int receive_error=bytes<0 ? errno : 0;
        int valid=bytes>=(ssize_t)sizeof(packet.message) && packet.message.protocol==PW_CHILD_PROTOCOL && packet.message.pid==child->pid;
        if(valid && packet.message.kind==PW_CHILD_SPAWN && bytes==sizeof(packet) && count==2) {
            if(!accept_spawn(&packet,fds))continue;
        }
        if(valid && packet.message.kind==PW_CHILD_RESOLVE && bytes==sizeof(struct pw_child_resolve) && count==1) {
            const struct pw_child_resolve *request=(const struct pw_child_resolve *)&packet;
            struct resolve_work *work=malloc(sizeof(*work));
            pthread_t thread;pthread_attr_t attributes;
            if(work && memchr(request->name,0,sizeof(request->name))) {
                memcpy(work->name,request->name,sizeof(work->name));work->reply=fds[0];
                pthread_attr_init(&attributes);pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);
                pthread_attr_setstacksize(&attributes,256u<<10);
                if(!pthread_create(&thread,&attributes,resolve_worker,work)) { pthread_attr_destroy(&attributes);continue; }
                pthread_attr_destroy(&attributes);
            }
            free(work);close(fds[0]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_DNS_CONFIG && bytes==sizeof(packet.message) && count==1) {
            struct pw_child_dns_config answer={EAI_FAIL,{0}};
            if(title_dns_config)answer.status=title_dns_config(&answer.config);
            (void)pw_child_send(fds[0],&answer,sizeof(answer),NULL,0);
            close(fds[0]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_DSECTION && bytes==sizeof(struct pw_child_dsection) && count==1) {
            const struct pw_child_dsection *request=(const struct pw_child_dsection *)&packet;
            struct pw_child_dsection_answer answer={0,0,-1,0,0};
            if(pw_wine_dsection_page(request->dev,request->ino,request->offset,child->pid,&answer.dmem,
                                     &answer.chunk_offset,&answer.chunk_bytes))answer.status=errno ? errno : EIO;
            (void)pw_child_send(fds[0],&answer,sizeof(answer),NULL,0);
            close(fds[0]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_DOOMED && bytes==sizeof(packet.message) && !count) {
            fprintf(stderr,"wine-ps5: child native=%d doomed reason=%d\n",child->pid,packet.message.value);
            if(child_notice && packet.message.value==PW_CHILD_DOOM_GPU)child_notice(PW_CHILD_NOTICE_GPU,child->pid,0);
            continue;
        }
        if(valid && packet.message.kind==PW_CHILD_CODE_ASK && bytes==sizeof(struct pw_child_code) && count==1) {
            const struct pw_child_code *request=(const struct pw_child_code *)&packet;
            struct pw_child_code_answer answer={ENOSYS,0};
            int object=-1;
            if(code_share_on())
                answer.status=pw_wine_codeshare_find(request->hash,request->bytes,(unsigned)(child-children),&object) ?
                              (errno ? errno : EIO) : 0;
            (void)pw_child_send(fds[0],&answer,sizeof(answer),object>=0 ? &object : NULL,object>=0);
            if(object>=0)close(object);
            close(fds[0]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_CODE_OFFER && bytes==sizeof(struct pw_child_code) && count==2) {
            const struct pw_child_code *request=(const struct pw_child_code *)&packet;
            struct pw_child_code_answer answer={ENOSYS,0};
            if(!code_share_on())close(fds[0]);
            else answer.status=pw_wine_codeshare_offer(request->hash,request->bytes,(unsigned)(child-children),fds[0]) ?
                               (errno ? errno : EIO) : 0;   /* the object's descriptor is the registry's either way */
            (void)pw_child_send(fds[1],&answer,sizeof(answer),NULL,0);
            close(fds[1]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_CODE_ASK_FILE && bytes==sizeof(struct pw_child_code_file) && count==1) {
            const struct pw_child_code_file *request=(const struct pw_child_code_file *)&packet;
            struct pw_child_code_answer answer={ENOSYS,0};
            int object=-1;
            if(code_share_on())
                answer.status=pw_wine_codeshare_find_file(request->file,request->bytes,(unsigned)(child-children),&object) ?
                              (errno ? errno : EIO) : 0;
            (void)pw_child_send(fds[0],&answer,sizeof(answer),object>=0 ? &object : NULL,object>=0);
            if(object>=0)close(object);
            close(fds[0]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_CODE_OFFER_FILE && bytes==sizeof(struct pw_child_code_file) && count==2) {
            const struct pw_child_code_file *request=(const struct pw_child_code_file *)&packet;
            struct pw_child_code_answer answer={ENOSYS,0};
            if(!code_share_on())close(fds[0]);
            else answer.status=pw_wine_codeshare_offer_file(request->file,request->bytes,(unsigned)(child-children),fds[0]) ?
                               (errno ? errno : EIO) : 0;
            (void)pw_child_send(fds[1],&answer,sizeof(answer),NULL,0);
            close(fds[1]);continue;
        }
        if(valid && packet.message.kind==PW_CHILD_CODE_MAPPED && bytes==sizeof(packet.message) && !count) {
            __atomic_store_n(&child->code_kib,packet.message.value>0 ? packet.message.value : 0,__ATOMIC_RELAXED);
            continue;
        }
        if(valid && packet.message.kind==PW_CHILD_FRAME && bytes==sizeof(packet.message) && count==1) {
            if(frame_attach)frame_attach(child->pid,fds[0]);
            else close(fds[0]);
            continue;
        }
        if(valid && packet.message.kind==PW_CHILD_WINDOW && bytes==sizeof(packet.message) && count==2) {
            int status;
            if(window_attach)status=window_attach(child->pid,fds[0],(size_t)packet.message.value);
            else { close(fds[0]);status=ENOTSUP; }
            struct pw_child_message reply={PW_CHILD_PROTOCOL,PW_CHILD_WINDOW,child->pid,status,0};
            (void)pw_child_send(fds[1],&reply,sizeof(reply),NULL,0);
            close(fds[1]);continue;
        }
        for(unsigned i=0;i<count;++i)close(fds[i]);
        if(!valid || count || bytes!=sizeof(packet.message)) {
            pthread_mutex_lock(&child_lock);
            const int closing=child->closing;
            pthread_mutex_unlock(&child_lock);
            fprintf(stderr,"wine-ps5: child terminal receive native=%d bytes=%zd errno=%d protocol=%#x kind=%u descriptors=%u closing=%d\n",
                    child->pid,bytes,receive_error,bytes>=(ssize_t)sizeof(packet.message) ? packet.message.protocol : 0,
                    bytes>=(ssize_t)sizeof(packet.message) ? packet.message.kind : 0,count,closing);
            break;
        }
        if(packet.message.kind==PW_CHILD_EXIT) {
            payload_trace("exit-received",child->pid,child->fd,packet.message.value);
            /* Its threads end with it: none of its waits may take a wake meant for a live one (src/pw_ntsync.h). */
            (void)pw_ntsync_process_gone(child->pid,0,1000);
            pthread_mutex_lock(&child_lock);
            if(child->initial && !child->unloading)root_result=packet.message.value!=0;
            pthread_mutex_unlock(&child_lock);
            fprintf(stderr,"wine-ps5: child native=%d completion=%#x service=%#x\n",child->pid,(unsigned)packet.message.value,child->service);
            fprintf(stderr,"wine-ps5: child exit received native=%d ms=%lld\n",child->pid,payload_ms());
            break;
        }
    }
    pthread_mutex_lock(&child_lock);
    if(child->initial && !child->unloading && root_result<0)root_result=1; // Lost/malformed completion is terminal failure.
    const int service = child->service, pid = child->pid, gpu_cookie=child->gpu_cookie;
    while (child->closing == 2) pthread_cond_wait(&child_closed, &child_lock);
    child->closing = 1;
    const int control=child->fd; child->fd = -1;
    // Retain the slot until service close completes, so another spawn cannot
    // reuse its storage while this monitor still owns it.
    pthread_mutex_unlock(&child_lock);
    const int status = close_until_gone(pid,control);
    payload_trace("close-returned",pid,control,status);
    /* Gone, however it ended: its waits leave the synchronization device and its place is free (src/pw_ntsync.h). */
    const int dropped = pw_ntsync_process_gone(pid,1,1000);
    if(dropped>0 || dropped==-EBUSY) fprintf(stderr,"wine-ps5: child native=%d sync device waits dropped=%d\n",pid,dropped);
    fprintf(stderr, "wine-ps5: child native=%d service=%#x closed=%#x\n", pid, service, (unsigned)status);
    if(gpu_cookie && gpu_close)gpu_close(gpu_cookie,-pid);
    if(frame_detach)frame_detach(pid);
    code_share_forget(pid,child);
    pw_wine_input_page_release(child->input_page);
    pw_wine_audio_ring_release(child->audio_ring);
    pthread_mutex_lock(&child_lock);
    if(child->memory>=0) { close(child->memory);child->memory=-1; }
    child->gpu_cookie=0;child->input_page=NULL;child->audio_ring=NULL;
    child->service = 0; child->pid = 0; child->closing = 0; child->initial = 0; child->unloading = 0; child->game_line = 0;
    pthread_mutex_unlock(&child_lock);
    payload_trace("resources-released",pid,control,0);
    descriptor_metrics("closed");
    return NULL;
}
/* Another process's memory, for the title's server: the helper copies it on the socket it gave
 * (PW_CHILD_MEMORY_SERVICE, src/pw_memory_service.h). One copy at a time: the server's requests are short and few. */
static pthread_mutex_t remote_memory_lock = PTHREAD_MUTEX_INITIALIZER;
int pw_ps5_child_memory(int pid, int writing, unsigned long long address, void *buffer, unsigned int size,
                        unsigned int *done)
{
    int fd = -1, status;
    *done = 0;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < sizeof(children) / sizeof(children[0]); ++i)
        if (children[i].pid == pid && children[i].service > 0 && children[i].memory >= 0) {
            fd = dup(children[i].memory);
            break;
        }
    pthread_mutex_unlock(&child_lock);
    if (fd < 0) return -1;
    pthread_mutex_lock(&remote_memory_lock);
    status = pw_memory_service_copy(fd, writing, address, buffer, size, done, 10000);
    pthread_mutex_unlock(&remote_memory_lock);
    close(fd);
    return status;
}
/* A descriptor the child sent (kind), or -1. */
static int fd_from_child(int control, uint32_t kind)
{
    struct pw_child_message message = {0};
    int fds[2], fd = -1;
    unsigned count = 0;
    if (ready(control, POLLIN) && pw_child_recv(control, &message, sizeof(message), fds, &count) == sizeof(message) &&
        count == 1 && message.protocol == PW_CHILD_PROTOCOL && message.kind == kind) {
        fd = fds[0];
        count = 0;
    }
    for (unsigned i = 0; i < count; ++i) close(fds[i]);
    if (fd < 0) fprintf(stderr, "wine-ps5: child's memory service not received\n");
    return fd;
}
/* A page the child made and sent (kind), mapped by adopt; the descriptor is closed either way. */
static void *adopt_from_child(int control, uint32_t kind, void *(*adopt)(int))
{
    struct pw_child_message message = {0};
    int fds[2];
    unsigned count = 0;
    void *page = NULL;
    const int readable=ready(control,POLLIN);
    const ssize_t received=readable ? pw_child_recv(control,&message,sizeof(message),fds,&count) : -2;
    const int receive_error=received < 0 && received != -2 ? errno : 0;
    if (received == sizeof(message) && count == 1 && message.protocol == PW_CHILD_PROTOCOL && message.kind == kind)
    {
        page = adopt(fds[0]);
        if (!page)
            fprintf(stderr,"wine-ps5: child page mapping refused kind=%u fd=%d errno=%d\n",kind,fds[0],errno);
    }
    if (!page)
        fprintf(stderr,"wine-ps5: child page refusal readable=%d bytes=%zd errno=%d expected_kind=%u protocol=%#x kind=%u value=%d descriptors=%u\n",
                readable,received,receive_error,kind,message.protocol,message.kind,message.value,count);
    for (unsigned i = 0; i < count; ++i) close(fds[i]);
    if (!page) fprintf(stderr, "wine-ps5: child's %s page not adopted (%u descriptors)\n",
                       kind == PW_CHILD_INPUT_PAGE ? "input" : "audio", count);
    return page;
}
/* The memory helper's control socket, and the lock that keeps one request on it at a time. A request not answered in
 * time or answered out of turn marks the socket dead for the session (src/pw_memory_helper.h): every later request
 * fails at once, and the socket stays open, because the helper ends at its close and the kernel then frees direct
 * memory other processes still map (native/pw_dmem_share.h). One request holds the lock at most PW_MEMORY_REPLY_MS;
 * requests queued behind a helper that is slow but answers in time have no total bound. */
static int memory_control = -1;
static int memory_pid;
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;
/* The census page it writes (src/pw_memory_helper.h), mapped before the title's loop reads it. */
static const volatile struct pw_memory_census *census_page;
/* Each kind's slowest answer so far (ms, memory_lock held): the deadline's calibration, said at the first failure and
 * at the end of the session. */
enum { ASK_CENSUS, ASK_ALLOC, ASK_SHARED, ASK_TOUCH, ASK_DMEM, ASK_GRANT, ASK_FREE, ASK_STALL, ASK_KINDS };
static const char *const ask_names[ASK_KINDS] = {"census", "alloc", "shared", "touch", "dmem", "grant", "free", "stall"};
static uint64_t ask_slowest[ASK_KINDS];
static int memory_dead_said;
static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}
static void memory_slowest_say(const char *when)
{
    char line[256];
    int used = snprintf(line, sizeof(line), "wine-ps5: memory helper slowest answers %s (ms):", when);
    for (unsigned k = 0; k < ASK_KINDS && used > 0 && (size_t)used < sizeof(line); ++k)
        used += snprintf(line + used, sizeof(line) - (size_t)used, " %s=%llu", ask_names[k], (unsigned long long)ask_slowest[k]);
    fprintf(stderr, "%s\n", line);
}
/* memory_lock held, after a request of kind begun at started: keep its time, and say once that the helper was marked
 * dead, with what was asked and how long it waited. errno is kept, but is ENOTCONN for every failure of a dead helper
 * (the one that marked it included), so the server and the broker refuse their title-made fallbacks on one value. */
static void memory_asked(unsigned kind, uint64_t started, int failed, unsigned long long a, unsigned long long b)
{
    const int saved = errno, dead = failed && pw_memory_dead(memory_control);
    const uint64_t took = monotonic_ms() - started;
    if (took > ask_slowest[kind]) ask_slowest[kind] = took;
    if (dead && !memory_dead_said) {
        memory_dead_said = 1;
        fprintf(stderr, "wine-ps5: memory helper did not answer %s %#llx %#llx: errno %d, waited %llu ms; marked dead, "
                "nothing more is asked of it\n", ask_names[kind], a, b, saved, (unsigned long long)took);
        memory_slowest_say("before it");
    }
    errno = dead ? ENOTCONN : saved;
}
int pw_ps5_memory_lock_busy(void)
{
    const int error = pthread_mutex_trylock(&memory_lock);
    if (!error) pthread_mutex_unlock(&memory_lock);
    return error == EBUSY;
}
static void inherit_environment(char *out,size_t size);
int pw_ps5_start_memory_helper(void)
{
    int pair[2] = {-1, -1}, carrier[2] = {-1, -1}, pid = 0, result = -1, dead = 0;
    if (memory_control >= 0) return 0;
    if (pw_child_socketpair(pair, sizeof(struct pw_child_spawn))) return -1;
    if (payload_spawn(pair[1], &pid, NULL)) goto done;
    close(pair[1]); pair[1] = -1;
    /* The loader takes a bootstrap with one descriptor; this one carries nothing. */
    if (pipe(carrier)) goto done;
    struct pw_child_bootstrap boot;
    memset(&boot, 0, sizeof(boot));
    boot.protocol = PW_CHILD_PROTOCOL; boot.size = sizeof(boot); boot.flags = PW_CHILD_MEMORY;
    /* Only the helper scheduling settings are read from it there (native/proton_wine_child.c). */
    inherit_environment(boot.environment, sizeof(boot.environment));
    if (!ready(pair[0], POLLOUT) || pw_child_send(pair[0], &boot, sizeof(boot), &carrier[0], 1)) goto done;
    struct pw_child_message ack = {0};
    if (!ready(pair[0], POLLIN) || recv(pair[0], &ack, sizeof(ack), 0) != sizeof(ack) ||
        ack.protocol != PW_CHILD_PROTOCOL || ack.kind != PW_CHILD_READY || ack.pid != pid || ack.value) {
        errno = EPROTO; goto done;
    }
    pthread_mutex_lock(&memory_lock);
    memory_control = pair[0]; pair[0] = -1;
    pthread_mutex_unlock(&memory_lock);
    __atomic_store_n(&memory_pid, pid, __ATOMIC_RELAXED);
    fprintf(stderr, "wine-ps5: memory helper native=%d: the title's shared memory is made there\n", pid);
    /* And it takes the census of the console's system memory, into a page the title only reads. A helper that does not
     * answer this first request is marked dead: the title must not go on without it (see the header). */
    {
        pthread_mutex_lock(&memory_lock);
        const uint64_t started = monotonic_ms();
        const int page = pw_memory_census_page(memory_control, (int)getpid());
        memory_asked(ASK_CENSUS, started, page < 0, (unsigned long long)getpid(), 0);
        dead = page < 0 && pw_memory_dead(memory_control);
        pthread_mutex_unlock(&memory_lock);
        if (dead) { errno = ENOTCONN; goto done; }
        void *map = page >= 0 ? mmap(NULL, sizeof(struct pw_memory_census), PROT_READ, MAP_SHARED, page, 0) : MAP_FAILED;
        if (map != MAP_FAILED) census_page = map;
        else fprintf(stderr, "wine-ps5: no census of the console's system memory (errno %d)\n", errno);
        if (page >= 0) close(page);
    }
    result = 0;
 done:
    {
        const int saved = errno;
        if (result && !dead) fprintf(stderr, "wine-ps5: no memory helper (errno %d): the title makes its shared memory\n", saved);
        for (int i = 0; i < 2; ++i) { if (carrier[i] >= 0) close(carrier[i]); }
        if (pair[1] >= 0) close(pair[1]);
        if (pair[0] >= 0) { if (pid > 0) payload_close(pid, pair[0]); else close(pair[0]); }
        errno = saved;
    }
    return result;
}
int pw_ps5_shared_alloc(size_t bytes)
{
    int fd = -1;
    pthread_mutex_lock(&memory_lock);
    const uint64_t started = monotonic_ms();
    if (memory_control >= 0) fd = pw_memory_alloc(memory_control, bytes);
    else errno = ENOENT;
    memory_asked(ASK_ALLOC, started, fd < 0, bytes, 0);
    pthread_mutex_unlock(&memory_lock);
    return fd;
}
int pw_ps5_transport_alloc(size_t bytes)
{
    int fd = -1;
    pthread_mutex_lock(&memory_lock);
    const uint64_t started = monotonic_ms();
    if (memory_control >= 0) fd = pw_memory_alloc_shared(memory_control, bytes);
    else errno = ENOENT;
    memory_asked(ASK_SHARED, started, fd < 0, bytes, 0);
    pthread_mutex_unlock(&memory_lock);
    return fd;
}
static uint64_t start_first_ms, start_last_ms;
void pw_ps5_start_times(uint64_t *first_ms, uint64_t *last_ms)
{
    *first_ms = __atomic_load_n(&start_first_ms, __ATOMIC_RELAXED);
    *last_ms = __atomic_load_n(&start_last_ms, __ATOMIC_RELAXED);
}
int pw_ps5_memory_census(struct pw_memory_census *census)
{
    if (!census_page) {
        errno = ENOENT;
        return -1;
    }
    return pw_memory_census_read(census_page, census);
}
int pw_ps5_dmem_alloc(size_t bytes, int client_pid, int64_t *offset)
{
    int status = -1;
    pthread_mutex_lock(&memory_lock);
    const uint64_t started = monotonic_ms();
    if (memory_control >= 0) status = pw_memory_dmem_alloc(memory_control, bytes, getpid(), client_pid, offset);
    else errno = ENOENT;
    memory_asked(ASK_DMEM, started, status, bytes, (unsigned long long)client_pid);
    pthread_mutex_unlock(&memory_lock);
    return status;
}
int pw_ps5_dmem_grant(int64_t offset, size_t bytes, int pid)
{
    int status = -1;
    pthread_mutex_lock(&memory_lock);
    const uint64_t started = monotonic_ms();
    if (memory_control >= 0) status = pw_memory_dmem_grant(memory_control, offset, bytes, pid);
    else errno = ENOENT;
    memory_asked(ASK_GRANT, started, status, (unsigned long long)offset, (unsigned long long)pid);
    pthread_mutex_unlock(&memory_lock);
    return status;
}
/* The title's side of large anonymous sections in direct memory (wine/ps5/pw_wine_dsection.h). */
extern int sceKernelMapDirectMemory(void **address, size_t bytes, int protection, int flags, int64_t offset, size_t alignment);
extern int sceKernelMunmap(void *address, size_t bytes);
/* PROTON_MEMORY_TEST_STALL=n (a console test): the memory helper sleeps past the deadline before the n-th chunk, which
 * must mark it dead once and end the program, never wedge the title. */
static long dsection_stall_at;
static unsigned long dsection_chunks_asked;
static int dsection_allocate(void *context, uint64_t bytes, int pid, int64_t *offset)
{
    (void)context;
    if (dsection_stall_at > 0 &&
        __atomic_add_fetch(&dsection_chunks_asked, 1, __ATOMIC_RELAXED) == (unsigned long)dsection_stall_at) {
        fprintf(stderr, "wine-ps5: memory helper: test stall before direct-memory chunk %ld\n", dsection_stall_at);
        pthread_mutex_lock(&memory_lock);
        const uint64_t started = monotonic_ms();
        const int status = memory_control >= 0 ? pw_memory_stall(memory_control, PW_MEMORY_REPLY_MS + 2000) : -1;
        memory_asked(ASK_STALL, started, status, PW_MEMORY_REPLY_MS + 2000, 0);
        pthread_mutex_unlock(&memory_lock);
    }
    return pw_ps5_dmem_alloc((size_t)bytes, pid, offset);
}
static int dsection_grant(void *context, int64_t offset, uint64_t bytes, int pid)
{
    (void)context;
    return pw_ps5_dmem_grant(offset, (size_t)bytes, pid);
}
/* Direct memory comes back as its last user left it; Windows gives a section's pages zeroed. The title is granted
 * every chunk (pw_ps5_dmem_alloc), so it writes the zeros. */
static int dsection_zero(void *context, int64_t offset, uint64_t bytes)
{
    void *at = NULL;
    (void)context;
    if (sceKernelMapDirectMemory(&at, (size_t)bytes, 0x03, 0, offset, 16384)) { errno = EACCES; return -1; }
    memset(at, 0, (size_t)bytes);
    (void)sceKernelMunmap(at, (size_t)bytes);
    return 0;
}
static int dsection_release(void *context, int64_t offset, uint64_t bytes)
{
    (void)context;
    return pw_ps5_dmem_free(offset, (size_t)bytes);
}
static int dsection_alive(void *context, int pid)
{
    (void)context;
    return !payload_absent(pid);
}
void pw_ps5_dsection_collect_now(void)
{
    uint64_t given = 0, lost = 0;
    const unsigned waiting = pw_wine_dsection_collect(&given, &lost);
    if (given || lost)
        fprintf(stderr, "wine-ps5: direct-memory sections gave back %llu chunks (%llu lost: the memory helper did not "
                "take them); %u released sections wait for a process to end\n",
                (unsigned long long)given, (unsigned long long)lost, waiting);
}
/* The title's collector: every second, the memory of released sections whose processes have all ended goes back. */
static void *dsection_collector(void *unused)
{
    (void)unused;
    for (;;) {
        pw_ps5_dsection_collect_now();
        sceKernelUsleep(1000000);
    }
    return NULL;
}
static void dsection_init_once(void)
{
    static const PwWineDsectionOps ops = { NULL, dsection_allocate, dsection_grant, dsection_zero, dsection_release,
                                           dsection_alive, 0 };
    const char *stall = getenv("PROTON_MEMORY_TEST_STALL");
    pthread_attr_t attributes;
    pthread_t thread;
    dsection_stall_at = stall ? atol(stall) : 0;
    pw_wine_dsection_init(&ops);
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attributes, 256u << 10);
    if (pthread_create(&thread, &attributes, dsection_collector, NULL))
        fprintf(stderr, "wine-ps5: no direct-memory section collector: released sections keep their memory until the "
                "title ends\n");
    pthread_attr_destroy(&attributes);
}
static void dsection_start(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, dsection_init_once);
}
int pw_ps5_dsection_register(int fd, uint64_t dev, uint64_t ino, uint64_t size)
{
    dsection_start();
    const int status = pw_wine_dsection_register(fd, dev, ino, size);
    /* Every anonymous section of 1 MiB or more is named, kept or not: smaller ones left no trace, and what Chromium shares between
     * its processes is what the flexible pool holds (2026-10-24: only two sections of the browser's session were 8 MiB or more). */
    if (size >= (1u << 20) && status) fprintf(stderr, "wine-ps5: anonymous section of %llu MiB stays physical shared memory\n", (unsigned long long)(size >> 20));
    if (!status) fprintf(stderr, "wine-ps5: section of %llu MiB in direct memory\n", (unsigned long long)(size >> 20));
    else if (size >= pw_wine_dsection_min_bytes())
        fprintf(stderr, "wine-ps5: section of %llu MiB stays on a file: errno %d\n", (unsigned long long)(size >> 20), errno);
    return status;
}
void pw_ps5_dsection_release(uint64_t dev, uint64_t ino)
{
    pw_wine_dsection_release(dev, ino);
}
int pw_ps5_dmem_free(int64_t offset, size_t bytes)
{
    int status = -1;
    pthread_mutex_lock(&memory_lock);
    const uint64_t started = monotonic_ms();
    if (memory_control >= 0) status = pw_memory_dmem_free(memory_control, offset, bytes);
    else errno = ENOENT;
    memory_asked(ASK_FREE, started, status, (unsigned long long)offset, bytes);
    pthread_mutex_unlock(&memory_lock);
    return status;
}
int pw_ps5_shared_touch(int fd, size_t from, size_t to)
{
    int status = -1;
    pthread_mutex_lock(&memory_lock);
    const uint64_t started = monotonic_ms();
    if (memory_control >= 0) status = pw_memory_touch(memory_control, fd, from, to);
    else errno = ENOENT;
    memory_asked(ASK_TOUCH, started, status, from, to);
    pthread_mutex_unlock(&memory_lock);
    return status;
}
/* A new process's own Game Mode (PW_CHILD_NOTICE_GAME_MODE): its catalog settings', else what it inherited. */
static int child_game_mode(const struct pw_child_bootstrap *boot)
{
    const char *value=pw_wine_block_value(boot->application,boot->application_bytes<sizeof(boot->application) ?
                                  boot->application_bytes : sizeof(boot->application),"WINE_PS5_GAME_MODE");
    if(!value)value=pw_wine_block_value(boot->environment,sizeof(boot->environment),"WINE_PS5_GAME_MODE");
    return !value ? -1 : !strcmp(value,"1") ? 1 : !strcmp(value,"0") ? 0 : -1;
}
static int spawn_native(int server_fd, const struct pw_child_bootstrap *boot, int *native_pid)
{
    const long long requested = payload_ms();
    long long locked = 0, sent = 0;
    struct payload_spawn_times times = {0};
    pthread_mutex_lock(&child_lock);
    struct child *child = NULL;
    const int refused = spawns_ended;
    for (unsigned i = 0; i < 32 && !refused; ++i) if (!children[i].service) { child = &children[i]; child->service = -1; break; }
    pthread_mutex_unlock(&child_lock);
    if (refused) { errno = ESHUTDOWN; return -1; }
    if (!child) { errno = EAGAIN; return -1; }
    const int use_gpu=gpu_open && gpu_close;
    struct pw_child_bootstrap gpu_boot;
    int gpu_cookie=0;
    // Every child makes an input page and gives it to the title, which keeps the pad in it (pw_wine_sink.h), and,
    // with WINE_PS5_HELPER_AUDIO=1, an audio ring the title's mixer plays (docs/GRAPHICS_PROCESS_BROKER.md, "Audio
    // for helpers"). Both are the child's, never the title's: src/pw_shared_page.h says why.
    const char *helper_audio=getenv("WINE_PS5_HELPER_AUDIO");
    const int want_audio=helper_audio && !strcmp(helper_audio,"1");
    void *input_page=NULL,*audio_ring=NULL;
    int memory_fd=-1;
    gpu_boot=*boot;
    /* A process another one started gets the settings its executable has in the catalog (the title's lookup), on
     * both its sides: the loader sets them in its Unix environment and Wine in its Windows one. */
    if(!(boot->flags&PW_CHILD_INITIAL) && application_lookup && boot->image[0]) {
        uint32_t bytes=0;
        if(!application_lookup(boot->image,boot->command_line,gpu_boot.application,sizeof(gpu_boot.application),&bytes))
            gpu_boot.application_bytes=bytes;
    }
    if(use_gpu)gpu_boot.flags|=PW_CHILD_GPU;
    gpu_boot.flags|=PW_CHILD_INPUT;
    {
        const char *image=spawn_image(boot);
        if(image && fixed_base_lookup && fixed_base_lookup(image)) {
            gpu_boot.flags|=PW_CHILD_FIXED_BASE;
            fprintf(stderr,"wine-ps5: %s is fixed at 0x400000: its helper gives the carrier's image back\n",image);
        }
    }
    if(want_audio)gpu_boot.flags|=PW_CHILD_AUDIO;
    boot=&gpu_boot;
    descriptor_metrics("before_spawn");

    int payload_pid=0;
    int pair[2] = {-1,-1}, result = -1, service = -1, saved_errno, gpu_fd=-1;
    const int warm_start = warm_take_alive(&pair[0], &payload_pid);
    if (warm_start) service = 1;
    else {
        if (pw_child_socketpair(pair,sizeof(struct pw_child_spawn))) goto done;
        service = payload_spawn(pair[1],&payload_pid,&times) ? -1 : 1;
    }
    fprintf(stderr, "wine-ps5: child spawn service=%#x\n", (unsigned)service);
    if (service <= 0) goto done;
    /* Before it runs: its windows take the screen or not from its first frame */
    if(child_notice)child_notice(PW_CHILD_NOTICE_GAME_MODE,payload_pid,child_game_mode(boot));
    if (pair[1] >= 0) { close(pair[1]); pair[1] = -1; }
    const char *gpu_query=getenv("WINE_PS5_GPU_DEVICE_QUERY");
    if(use_gpu) {
        pthread_mutex_lock(&startup_lock);
        locked = payload_ms();
        gpu_fd=gpu_open(&gpu_cookie,payload_pid);
        pthread_mutex_unlock(&startup_lock);
        if(gpu_fd<0)goto done;
    }
    else if(gpu_query && !strcmp(gpu_query,"1")) {
        errno=0;gpu_fd=open("/dev/gc",O_RDWR);
        fprintf(stderr,"wine-ps5: gpu donor fd=%d errno=%d\n",gpu_fd,errno);
        if(gpu_fd<0)goto done;
        unsigned query=0;errno=0;
        int status=ioctl(gpu_fd,0xc004812eUL,&query);
        fprintf(stderr,"wine-ps5: gpu donor ioctl=%d errno=%d value=%#x\n",status,errno,query);
    }
    int boot_fds[2]={server_fd,gpu_fd};
    // gpu_fd, the child's GPU broker socket, stays open here until done, after the child's replies show it holds
    // its own copy: a unix socket in flight with no holder can arrive flushed (pw_payload_rendezvous.h).
    if (!ready(pair[0],POLLOUT) || pw_child_send(pair[0],boot,sizeof(*boot),boot_fds,gpu_fd>=0 ? 2 : 1)) goto done;
    sent = payload_ms();
    if(!(input_page=adopt_from_child(pair[0],PW_CHILD_INPUT_PAGE,pw_wine_input_page_adopt)))goto done;
    if(want_audio && !(audio_ring=adopt_from_child(pair[0],PW_CHILD_AUDIO_RING,pw_wine_audio_ring_adopt)))goto done;
    if((memory_fd=fd_from_child(pair[0],PW_CHILD_MEMORY_SERVICE))<0)goto done;
    struct pw_child_message ack = {0};
    if (!ready(pair[0], POLLIN) || recv(pair[0], &ack, sizeof(ack), 0) != sizeof(ack) ||
        ack.protocol != PW_CHILD_PROTOCOL || ack.kind != PW_CHILD_READY || ack.pid <= 0 || ack.pid == getpid() || ack.pid!=payload_pid) {
        errno = EPROTO; goto done;
    }
    if (ack.value) { errno = EIO; goto done; }
    pthread_mutex_lock(&child_lock);
    child->fd = pair[0]; child->pid = ack.pid; child->service = service; child->closing = 0; child->game_line = 0;
    child->memory = memory_fd; memory_fd = -1;
    child->input_page=input_page;input_page=NULL;
    child->audio_ring=audio_ring;audio_ring=NULL;
    child->initial=!!(boot->flags&PW_CHILD_INITIAL);child->unloading=0;child->gpu_cookie=gpu_cookie;
    pthread_mutex_unlock(&child_lock);
    pthread_t monitor;
    if (pthread_create(&monitor, NULL, monitor_child, child)) { errno = EAGAIN; goto done; }
    pthread_detach(monitor);
    pair[0] = -1; result = 0;
    if(native_pid)*native_pid=ack.pid;
    fprintf(stderr, "wine-ps5: child ready native=%d service=%#x\n", ack.pid, service);
    {
        /* One line per start, every step after the request: tools/start_stages.py lines it up with the helper's own. */
        const long long now = payload_ms();
        /* A cold start's time, request to ready, for the memory guard's slowdown rule (pw_memory_start_slowed). */
        if (!warm_start) {
            __atomic_store_n(&start_last_ms, (uint64_t)(now - requested), __ATOMIC_RELAXED);
            uint64_t none = 0;
            __atomic_compare_exchange_n(&start_first_ms, &none, (uint64_t)(now - requested), 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        }
#define PW_AFTER(t) ((t) ? (t) - requested : -1ll)
        fprintf(stderr, "wine-ps5: spawn native=%d request_ms=%lld lock=%lld read=%lld answered=%lld connected=%lld "
                "attempts=%u held=%lld sent=%lld ready=%lld warm=%d\n", ack.pid, requested, PW_AFTER(locked),
                PW_AFTER(times.read), PW_AFTER(times.answered), PW_AFTER(times.connected), times.attempts, PW_AFTER(times.held),
                PW_AFTER(sent), now - requested, warm_start);
#undef PW_AFTER
    }
 done:
    saved_errno = errno;
    if(gpu_fd>=0)close(gpu_fd);
    if(memory_fd>=0)close(memory_fd);
    pw_wine_input_page_release(input_page);
    pw_wine_audio_ring_release(audio_ring);
    if (result) {
        fprintf(stderr,"wine-ps5: child bootstrap failed errno=%d packet=%zu\n",saved_errno,sizeof(*boot));
        if(child_notice)child_notice(PW_CHILD_NOTICE_SPAWN,payload_pid,saved_errno);
        if(pair[1]>=0) { close(pair[1]);pair[1]=-1; }
        if(payload_pid>0) { close_until_gone(payload_pid,pair[0]);pair[0]=-1; }
        if(gpu_cookie && gpu_close)gpu_close(gpu_cookie,-payload_pid);
        pthread_mutex_lock(&child_lock); child->service = 0; child->pid = 0; child->game_line = 0;
        pthread_mutex_unlock(&child_lock);
    }
    for (unsigned i = 0; i < 2; ++i) if (pair[i] != -1) close(pair[i]);
    if (!result) warm_refill();
    errno = saved_errno;
    return result;
}
static int spawn_through_owner(int server_fd, const struct pw_child_bootstrap *boot)
{
    int pair[2];
    if(pw_child_socketpair(pair,sizeof(struct pw_child_spawn)))return -1;
    const struct pw_child_spawn request={{PW_CHILD_PROTOCOL,PW_CHILD_SPAWN,getpid(),0,0},*boot};
    int passed[]={server_fd,pair[1]},result=-1;
    struct pw_child_message reply={0};
    /* When this process asked the title for a child and when it had the answer (tools/start_stages.py): the first
     * step of a Windows CreateProcess here, and the time an installer's next custom action waits for its helper. */
    const long long asked=payload_ms();
    if(!ready(child_control,POLLOUT) || pw_child_send(child_control,&request,sizeof(request),passed,2))goto done;
    close(pair[1]);pair[1]=-1;
    // The title answers once the helper is ready or refused.
    if(!ready_for(pair[0],POLLIN,60000) || recv(pair[0],&reply,sizeof(reply),0)!=sizeof(reply) ||
       reply.protocol!=PW_CHILD_PROTOCOL || reply.kind!=PW_CHILD_SPAWN_REPLY || reply.pid!=getpid()) { errno=EPROTO;goto done; }
    if(reply.value) { errno=reply.value;goto done; }
    result=0;
 done:
    {
        int saved=errno;
        close(pair[0]);if(pair[1]>=0)close(pair[1]);
        fprintf(stderr,"wine-ps5: spawn asked ms=%lld replied_ms=%lld child=%lld status=%d\n",asked,payload_ms(),
                (long long)reply.tid,result ? saved : 0);
        errno=saved;
    }
    return result;
}
/* What a helper inherits of its spawner's environment: pw_wine_inherit.h, host-tested. */
static void inherit_environment(char *out,size_t size)
{
    extern char **environ;
    pw_wine_inherit_environment(environ,out,size);
}
static int prepare_boot(struct pw_child_bootstrap *boot,const char *runtime,const char *prefix,const char *cwd,const char *debug)
{
    memset(boot,0,sizeof(*boot));boot->protocol=PW_CHILD_PROTOCOL;boot->size=sizeof(*boot);
    inherit_environment(boot->environment,sizeof(boot->environment));
    return copy_string(boot->runtime, sizeof(boot->runtime), runtime) ||
        copy_string(boot->prefix, sizeof(boot->prefix), prefix) ||
        copy_string(boot->cwd, sizeof(boot->cwd), cwd) ||
        copy_string(boot->dllpath, sizeof(boot->dllpath), getenv("WINEDLLPATH")) ||
        copy_string(boot->overrides, sizeof(boot->overrides), getenv("WINEDLLOVERRIDES")) ||
        copy_string(boot->debug, sizeof(boot->debug), debug ? debug : getenv("WINEDEBUG")) ? -1 : 0;
}
int pw_ps5_spawn(int server_fd, const char *runtime, const char *prefix, const char *cwd, const char *debug, unsigned flags,
                 const char *image, const char *command_line)
{
    struct pw_child_bootstrap boot;
    if(prepare_boot(&boot,runtime,prefix,cwd,debug))return -1;
    /* Names that do not fit stay empty: the process then simply gets no catalog settings. */
    if(image && strlen(image)<sizeof(boot.image))strcpy(boot.image,image);
    if(command_line && strlen(command_line)<sizeof(boot.command_line))strcpy(boot.command_line,command_line);
    /* The one program the title runs itself, its code and memory in the title's partition (WINE_PS5_TITLE_PROCESS=<image
     * name>, docs/FLEXIBLE_MEMORY.md): the owner-graphics child (patch 0927), named rather than marked by the program. */
    const char *title_process=getenv("WINE_PS5_TITLE_PROCESS");
    if(!flags && title_process && *title_process && image) {
        const char *name=image;
        for(const char *at=image;*at;at++) if(*at=='\\' || *at=='/') name=at+1;
        if(!strcasecmp(name,title_process)) flags=PW_CHILD_GRAPHICS;
    }
    if(flags && (flags!=PW_CHILD_GRAPHICS || (child_control<0 && !gpu_open))) { errno=ENOTSUP;return -1; }
    boot.flags=flags;
    if(child_control>=0) return spawn_through_owner(server_fd,&boot);
    /* In the title: the program it runs itself starts this one, so it is its game's (a launcher starting the game). */
    int pid=0;
    const int result=spawn_native(server_fd,&boot,&pid);
    if(!result && title_client_live && pid>0) game_line_mark(pid,image);
    return result;
}
/* The image's own file and the DLLs beside it, for pw_pe_kind: one open file at a time. */
struct pe_folder { const char *exe; char dir[1024]; char name[PW_PE_IMPORT_NAME]; int fd; };
static int pe_folder_read(void *context, const char *name, uint64_t offset, void *into, size_t size)
{
    struct pe_folder *folder = context;
    if (folder->fd < 0 || strcasecmp(folder->name, name ? name : "")) {
        if (folder->fd >= 0) close(folder->fd);
        folder->fd = -1;
        copy_string(folder->name, sizeof(folder->name), name ? name : "");
        char path[1280];
        if (!name) folder->fd = open(folder->exe, O_RDONLY);
        else if ((size_t)snprintf(path, sizeof(path), "%s/%s", folder->dir, name) < sizeof(path)) {
            folder->fd = open(path, O_RDONLY);
            /* shortcut: the exact name, then all lower case; a DLL in another case is not found (the program stays a
             * helper, as in Alpha 6) until a case-blind lookup is needed */
            for (char *at = path + strlen(folder->dir) + 1; folder->fd < 0 && *at; ++at) *at = pw_pe_lower(*at);
            if (folder->fd < 0) folder->fd = open(path, O_RDONLY);
        }
        if (folder->fd < 0) return -1;
    }
    size_t have = 0;
    while (have < size) {
        const ssize_t got = pread(folder->fd, (char *)into + have, size - have, (off_t)(offset + have));
        if (got <= 0) return -1;
        have += (size_t)got;
    }
    return 0;
}
/* 1 when dir/name exists. */
static int folder_has(const char *dir, const char *name)
{
    char path[1280];
    return (size_t)snprintf(path, sizeof(path), "%s/%s", dir, name) < sizeof(path) && access(path, F_OK) == 0;
}
unsigned int pw_ps5_title_route(const char *unix_name, unsigned short machine, const char *image, const char *command_line)
{
    const char *setting = getenv("WINE_PS5_TITLE_GAMES");
    if (!setting || strcmp(setting, "1") || !image || !*image || (child_control < 0 && !gpu_open)) return 0;
    enum PwTitleRoute route = pw_title_game_route(image, machine == 0x8664, setting, getenv("WINE_PS5_TITLE_OPTOUT"),
                                                  getenv("WINE_PS5_TITLE_PICK"), getenv("WINE_PS5_TITLE_LISTED"));
    enum PwPeKind kind = PW_PE_KIND_UNREAD;
    char deepest[PW_PE_IMPORT_NAME] = "";
    if (route == PW_TITLE_ROUTE_CANDIDATE && unix_name) {
        struct pe_folder folder = { unix_name, "", "", -1 };
        copy_string(folder.dir, sizeof(folder.dir), unix_name);
        char *slash = strrchr(folder.dir, '/');
        if (slash) *slash = 0;
        /* A Chromium-based game first: its imports show no graphics API (it loads Direct3D at run time). */
        const int chromium = folder_has(folder.dir, "resources.pak") && folder_has(folder.dir, "icudtl.dat") &&
            (folder_has(folder.dir, "v8_context_snapshot.bin") || folder_has(folder.dir, "snapshot_blob.bin"));
        route = pw_title_game_chromium(route, chromium, folder_has(folder.dir, "libcef.dll"),
                                       command_line && strstr(command_line, "--type=") != NULL);
        if (route == PW_TITLE_ROUTE_CANDIDATE) kind = pw_pe_kind(pe_folder_read, &folder, deepest);
        if (folder.fd >= 0) close(folder.fd);
    }
    route = pw_title_game_decide(route, kind == PW_PE_KIND_GAME);
    if (route != PW_TITLE_ROUTE_OFF && route != PW_TITLE_ROUTE_NOT_LIBRARY)
        fprintf(stderr, "wine-ps5: %s: %s (%s%s%s%s%s)\n", pw_title_game_name(image),
                pw_title_route_in_title(route) ? "inside MihawkOS" : "a helper", pw_title_route_word(route),
                kind != PW_PE_KIND_UNREAD ? "; " : "", kind != PW_PE_KIND_UNREAD ? pw_pe_kind_word(kind) : "",
                deepest[0] ? " through " : "", deepest);
    return pw_title_route_in_title(route) ? PW_CHILD_GRAPHICS : 0;
}
int pw_ps5_start_root(int server_fd,const char *runtime,const char *prefix,const char *cwd,int argc,const char *const *argv)
{
    struct pw_child_bootstrap boot;
    if(argc<2 || argc>64 || prepare_boot(&boot,runtime,prefix,cwd,NULL)) { errno=EINVAL;return -1; }
    /* The Windows client now runs in a payload, so the initial client needs the run's stdout destination.
     * Do not put it in pw_wine_inherited(): each descendant would reopen/truncate the same file instead of using
     * its Windows inherited or redirected handles. Conformance logs disappeared at this boundary. */
    /* Only the first root: the desktop session's later roots (Explorer started again, src/pw_desktop_session.h) would
     * truncate the file the first root's descendants still write through their inherited handles. */
    static int output_given;
    const char *output=getenv("WINE_PS5_STDOUT");
    if(output && *output && !output_given) {
        output_given=1;
        char *end=boot.environment;
        while(*end)end+=strlen(end)+1;
        size_t available=sizeof(boot.environment)-(size_t)(end-boot.environment);
        int length=snprintf(end,available,"WINE_PS5_STDOUT=%s",output);
        if(length<0 || (size_t)length+2>available) { errno=E2BIG;return -1; }
        end[length+1]=0;  /* the environment is a double-NUL-terminated list */
    }
    size_t used=0;
    for(int i=0;i<argc;++i) {
        size_t size=strlen(argv[i])+1;
        if(size>sizeof(boot.initial_args)-used) { errno=E2BIG;return -1; }
        memcpy(boot.initial_args+used,argv[i],size);used+=size;
    }
    boot.application_bytes=initial_environment_bytes;
    memcpy(boot.application,initial_environment,initial_environment_bytes);
    boot.flags=PW_CHILD_INITIAL;boot.argc=argc;root_result=-1;
    return spawn_native(server_fd,&boot,NULL);
}
/* The title's desktop session (src/pw_desktop_session.h): whether its root, Explorer, runs, and ending it on purpose
 * (game mode), which the session survives: its exit is not the result pw_ps5_root_result reports. 0 when asked,
 * -1 (ESRCH) when there is no root to end. */
int pw_ps5_root_present(void)
{
    int present = 0;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32; ++i) if (children[i].initial && children[i].service > 0) { present = 1; break; }
    pthread_mutex_unlock(&child_lock);
    return present;
}
int pw_ps5_unload_root(void)
{
    pid_t pid = 0;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32; ++i)
        if (children[i].initial && children[i].service > 0 && !children[i].closing) {
            children[i].unloading = 1; pid = children[i].pid; break;
        }
    pthread_mutex_unlock(&child_lock);
    if (!pid) { errno = ESRCH; return -1; }
    fprintf(stderr, "wine-ps5: session root native=%d unloading\n", (int)pid);
    return pw_ps5_remote_close(pid);
}
unsigned pw_ps5_helpers(int *pids, unsigned capacity, int *memory)
{
    unsigned count = 0;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32 && count < capacity; ++i)
        if (children[i].service > 0 && children[i].pid > 0) pids[count++] = children[i].pid;
    pthread_mutex_unlock(&child_lock);
    if (memory) *memory = __atomic_load_n(&memory_pid, __ATOMIC_RELAXED);
    return count;
}
int pw_ps5_remote_signal(pid_t pid, long tid, int signal)
{
    struct pw_child_message message = {PW_CHILD_PROTOCOL, PW_CHILD_SIGNAL, pid, signal, tid};
    int result = -1;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32; ++i) if (children[i].pid == pid && children[i].service > 0) {
        result = send(children[i].fd, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT) == sizeof(message) ? 0 : -1;
        break;
    }
    pthread_mutex_unlock(&child_lock);
    if (result) errno = EAGAIN; // Transport failure is not proof the thread died.
    return result;
}
int pw_ps5_remote_close(pid_t pid)
{
    int service = -1, control=-1;
    pthread_mutex_lock(&child_lock);
    for (unsigned i=0; i<32; ++i) if (children[i].pid == pid && children[i].service > 0) {
        if (children[i].closing) { pthread_mutex_unlock(&child_lock); return 0; }
        children[i].closing = 2; service = children[i].service; control=children[i].fd;break;
    }
    pthread_mutex_unlock(&child_lock);
    if (service < 0) { errno = EAGAIN; return -1; }
    payload_trace("remote-shutdown-begin",pid,control,0);
    const int status = shutdown(control,SHUT_RDWR);
    payload_trace("remote-shutdown-end",pid,control,status ? errno : 0);
    fprintf(stderr, "wine-ps5: child native=%d forced_service_close=%#x status=%#x\n", pid, service, (unsigned)status);
    payload_trace("remote-close-logged",pid,control,0);
    pthread_mutex_lock(&child_lock);
    for (unsigned i=0; i<32; ++i) if (children[i].pid == pid && children[i].service == service)
        children[i].closing = status == 0 ? 1 : 0;
    pthread_cond_broadcast(&child_closed);
    pthread_mutex_unlock(&child_lock);
    payload_trace("remote-close-published",pid,control,status);
    return status == 0 ? 0 : -1;
}
/* Close the helpers still open: their control socket shut, a helper's loader sees the title gone and returns. */
static void close_remaining_children(void)
{
    pid_t pids[32];
    unsigned count = 0;
    pthread_mutex_lock(&child_lock);
    for (unsigned i = 0; i < 32; ++i)
        if (children[i].service > 0 && children[i].pid > 0 && !children[i].closing && !children[i].game_line)
            pids[count++] = children[i].pid;
    pthread_mutex_unlock(&child_lock);
    for (unsigned i = 0; i < count; ++i) {
        fprintf(stderr, "wine-ps5: child native=%d still open after the title's wait; closing it\n", (int)pids[i]);
        (void)pw_ps5_remote_close(pids[i]);
    }
}
/* Which helpers the title still waits for. A slot whose close returned (its "closed=" line) is in the GPU broker's close,
 * which takes the broker's client lock, or the frame detach: a wedged broker keeps that lock for good, and before this
 * line nothing said why the title was not replaced. */
static void report_open_children(unsigned seconds)
{
    int pids[32], closing[32];
    unsigned count = 0, pending;
    pthread_mutex_lock(&child_lock);
    pending = pending_spawns;
    for (unsigned i = 0; i < 32; ++i)
        if (children[i].service) { pids[count] = children[i].pid; closing[count++] = children[i].closing; }
    pthread_mutex_unlock(&child_lock);
    fprintf(stderr, "wine-ps5: the title still waits for %u helpers and %u spawns (%u s)\n", count, pending, seconds);
    for (unsigned i = 0; i < count; ++i)
        fprintf(stderr, "wine-ps5: still open: child native=%d closing=%d\n", pids[i], closing[i]);
}
void pw_ps5_wait_children(void)
{
    unsigned live, waited = 0;
    int let_go = 0;
    do {
        live = 0;
        pthread_mutex_lock(&child_lock);
        const unsigned pending=pending_spawns;
        const long long since=pending_since_ms;
        for (unsigned i=0; i<32; ++i) live += children[i].service != 0;
        pthread_mutex_unlock(&child_lock);
        const long long now=payload_ms();
        if (pw_spawn_wait_holds(pending, since > 0 ? (uint64_t)since : 0, now > 0 ? (uint64_t)now : 0)) live += pending;
        else if (pending && !let_go) {
            let_go = 1;
            fprintf(stderr, "wine-ps5: %u helper start(s) in flight for %lld s; the title's end goes on without them\n",
                    pending, (now - since) / 1000);
        }
        if (live) {
            sceKernelUsleep(10000);
            /* After 15 s, the helpers left are closed. A helper whose Windows program cannot end (one created
             * suspended whose parent is gone) kept the title waiting for good, and outlived it once the system
             * closed the title (2026-10-06, record removed from the tree; in git history: a launcher's browser process 365). */
            if (++waited == 1500) {
                unsigned games = 0;
                pthread_mutex_lock(&child_lock);
                for (unsigned i = 0; i < 32; ++i) games += children[i].service > 0 && children[i].game_line;
                pthread_mutex_unlock(&child_lock);
                if (games) fprintf(stderr, "wine-ps5: the title waits for the %u helper(s) of its game, as long as they run\n", games);
                close_remaining_children();
            }
            if (waited % 3000 == 0) report_open_children(waited / 100);
        }
    } while (live);
    descriptor_metrics("all_children_closed");
    pthread_mutex_lock(&memory_lock);
    if (memory_control >= 0) memory_slowest_say("this session");
    pthread_mutex_unlock(&memory_lock);
}
static void *child_agent(void *unused)
{
    (void)unused;
    struct pw_child_message message = {0};
    ssize_t bytes;
    int receive_error = 0;
    while ((bytes = recv(child_control, &message, sizeof(message), 0)) == sizeof(message)) {
        if (message.protocol != PW_CHILD_PROTOCOL || message.pid != getpid()) break;
        if (message.kind == PW_CHILD_SIGNAL) {
            int ret = pw_wine_thread_kill(getpid(), message.tid, message.value);
            if (ret) fprintf(stderr, "wine-ps5: child signal tid=%ld status=%d\n", (long)message.tid, errno);
        }
    }
    if (bytes < 0) receive_error = errno;
    fprintf(stderr, "wine-ps5: child agent terminal receive native=%d bytes=%zd errno=%d protocol=%#x kind=%u pid=%d\n",
            (int)getpid(), bytes, receive_error, message.protocol, message.kind, message.pid);
    // Loss of the owning parent means Sony will close the application. Never
    // call exit(), return from _start, or start a new independent wineserver.
    for (;;) sceKernelUsleep(100000);
}
int pw_wine_resolve(const char *name,unsigned char address[4])
{
    if(title_resolve)return title_resolve(name,address);
    if(child_control<0 || strlen(name)>=sizeof(((struct pw_child_resolve *)0)->name))return EAI_FAIL;
    struct pw_child_resolve request={{PW_CHILD_PROTOCOL,PW_CHILD_RESOLVE,getpid(),0,0},{0}};
    strcpy(request.name,name);
    int pair[2];
    if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair))return EAI_AGAIN;
    struct pw_child_resolved answer={EAI_AGAIN,{0}};
    const int sent=!pw_child_send(child_control,&request,sizeof(request),&pair[1],1);
    close(pair[1]); // The title's copy is the only one: a title that drops the request ends the wait.
    if(sent) {
        struct pollfd wait={pair[0],POLLIN,0};
        /* The title's resolver gives up after three tries of five seconds. */
        if(poll(&wait,1,20000)!=1 || recv(pair[0],&answer,sizeof(answer),0)!=(ssize_t)sizeof(answer))
            answer.status=EAI_AGAIN;
    }
    close(pair[0]);
    if(!answer.status)memcpy(address,answer.address,4);
    return answer.status;
}
int pw_wine_dns_config(struct pw_dns_config *config)
{
    memset(config,0,sizeof(*config));
    if(title_dns_config)return title_dns_config(config);
    if(child_control<0)return EAI_FAIL;
    struct pw_child_message request={PW_CHILD_PROTOCOL,PW_CHILD_DNS_CONFIG,getpid(),0,0};
    struct pw_child_dns_config answer={EAI_AGAIN,{0}};
    int pair[2];
    if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair))return EAI_AGAIN;
    int sent=!pw_child_send(child_control,&request,sizeof(request),&pair[1],1);
    close(pair[1]);
    if(sent && ready_for(pair[0],POLLIN,5000) &&
       recv(pair[0],&answer,sizeof(answer),0)==sizeof(answer) && answer.config.count<=4) {
        if(!answer.status) {
            for(unsigned i=0;i<answer.config.count;++i)
                if(answer.config.servers[i].family!=4 && answer.config.servers[i].family!=6)answer.status=EAI_FAIL;
            if(!answer.status)*config=answer.config;
        }
    } else answer.status=EAI_AGAIN;
    close(pair[0]);return answer.status;
}
/* The pager's answer channel, made with the agent so a fault opens no descriptor; remade after an ask that failed, so a
 * late answer dies with the old pair. Asks are serialized by Wine's virtual mutex (pw_wine_dmap.h): checked. */
static int dsection_reply[2] = {-1, -1};
static int dsection_asking;
int pw_ps5_dsection_page(uint64_t dev, uint64_t ino, uint64_t offset, int64_t *dmem, uint64_t *chunk_offset,
                         uint64_t *chunk_bytes)
{
    if(child_control<0) { errno=ENOTCONN;return -1; }
    if(__atomic_exchange_n(&dsection_asking,1,__ATOMIC_ACQUIRE)) { errno=EBUSY;return -1; }
    struct pw_child_dsection request={{PW_CHILD_PROTOCOL,PW_CHILD_DSECTION,getpid(),0,0},dev,ino,offset};
    struct pw_child_dsection_answer answer={EIO,0,-1,0,0};
    int error=0;
    const char *was=pw_wine_perf_vlock_doing(PW_CHILD_PAGER_ASK);   /* waits up to 10 s for the title, mutex held */
    /* The title allocates and zeroes a 2 MiB chunk on a first touch: a few milliseconds. */
    if(dsection_reply[0]<0 && socketpair(AF_UNIX,SOCK_SEQPACKET,0,dsection_reply)) { error=errno;dsection_reply[0]=dsection_reply[1]=-1; }
    else if(pw_child_send(child_control,&request,sizeof(request),&dsection_reply[1],1)) error=errno ? errno : EIO;
    else if(!ready_for(dsection_reply[0],POLLIN,10000)) error=errno;   /* ETIMEDOUT: the pager retries it */
    else if(recv(dsection_reply[0],&answer,sizeof(answer),MSG_DONTWAIT)!=(ssize_t)sizeof(answer)) error=EIO;
    if(error && dsection_reply[0]>=0) { close(dsection_reply[0]);close(dsection_reply[1]);dsection_reply[0]=dsection_reply[1]=-1; }
    __atomic_store_n(&dsection_asking,0,__ATOMIC_RELEASE);
    pw_wine_perf_vlock_doing(was);
    if(!error && answer.status) error=answer.status;
    if(error) { errno=error;return -1; }
    *dmem=answer.dmem;*chunk_offset=answer.chunk_offset;*chunk_bytes=answer.chunk_bytes;
    return 0;
}
/* Shared code copies: the answer channel made at the first ask and remade after a failed one, so a late answer dies with
 * the old pair. Asks and offers are serialized by Wine's virtual mutex (pw_wine_dmem_ps5.c, under the fault handler):
 * checked. An ask sends the answer socket and gets the object (fd); an offer sends the object (fd) and the socket. */
static int code_reply[2] = {-1, -1};
static int code_asking;
static int code_round(uint32_t kind, uint64_t hash, uint64_t bytes, int *fd)
{
    if(child_control<0) { errno=ENOTCONN;return -1; }
    if(__atomic_exchange_n(&code_asking,1,__ATOMIC_ACQUIRE)) { errno=EBUSY;return -1; }
    struct pw_child_code request={{PW_CHILD_PROTOCOL,kind,getpid(),0,0},hash,bytes};
    struct pw_child_code_answer answer={EIO,0};
    const int ask=kind==PW_CHILD_CODE_ASK;
    int fds[2]={-1,-1},error=0;
    unsigned count=0;
    /* The title's monitor answers from a table under one mutex: microseconds. 1 s is far past that, and under the
     * stall watch's 3 s hold of the virtual mutex. */
    if(code_reply[0]<0 && socketpair(AF_UNIX,SOCK_SEQPACKET,0,code_reply)) { error=errno;code_reply[0]=code_reply[1]=-1; }
    else {
        const int sent[2]={ask ? code_reply[1] : *fd,code_reply[1]};
        if(pw_child_send(child_control,&request,sizeof(request),sent,ask ? 1 : 2)) error=errno ? errno : EIO;
        else if(!ready_for(code_reply[0],POLLIN,1000)) error=errno ? errno : ETIMEDOUT;
        else if(pw_child_recv(code_reply[0],&answer,sizeof(answer),fds,&count)!=(ssize_t)sizeof(answer) ||
                count!=(ask && !answer.status ? 1u : 0u)) error=EIO;
    }
    if(error) {
        for(unsigned i=0;i<count;++i)close(fds[i]);
        if(code_reply[0]>=0) { close(code_reply[0]);close(code_reply[1]);code_reply[0]=code_reply[1]=-1; }
    }
    __atomic_store_n(&code_asking,0,__ATOMIC_RELEASE);
    if(!error && answer.status) error=answer.status;
    if(error) { errno=error;return -1; }
    if(ask)*fd=fds[0];
    return 0;
}
/* The round trips' calls and nanoseconds, lock-free (pw_ps5_code_round_timing, the helper's life summary). */
static uint64_t code_round_calls, code_round_ns;
static int code_round_timed(int (*round)(uint32_t,const void *,uint64_t,int *),uint32_t kind,const void *key,
                            uint64_t bytes,int *fd)
{
    struct timespec begin,end;
    clock_gettime(CLOCK_MONOTONIC,&begin);
    const int status=round(kind,key,bytes,fd);
    const int saved=errno;
    clock_gettime(CLOCK_MONOTONIC,&end);
    __atomic_add_fetch(&code_round_calls,1,__ATOMIC_RELAXED);
    __atomic_add_fetch(&code_round_ns,(uint64_t)((end.tv_sec-begin.tv_sec)*1000000000ll+(end.tv_nsec-begin.tv_nsec)),
                       __ATOMIC_RELAXED);
    errno=saved;
    return status;
}
static int code_round_hash(uint32_t kind,const void *key,uint64_t bytes,int *fd)
{ return code_round(kind,*(const uint64_t *)key,bytes,fd); }
unsigned pw_ps5_code_round_timing(uint64_t *out,unsigned count)
{
    const uint64_t values[2]={__atomic_load_n(&code_round_calls,__ATOMIC_RELAXED),__atomic_load_n(&code_round_ns,__ATOMIC_RELAXED)};
    for(unsigned i=0;i<count && i<2;++i)out[i]=values[i];
    return 2;
}
int pw_ps5_code_ask(uint64_t hash, uint64_t bytes, int *fd) { return code_round_timed(code_round_hash,PW_CHILD_CODE_ASK,&hash,bytes,fd); }
/* 0: the registry kept the object (fd stays the caller's to close). -1 EEXIST/ENOSPC: it did not; anything else: the
 * channel failed. */
int pw_ps5_code_offer(uint64_t hash, uint64_t bytes, int fd) { return code_round_timed(code_round_hash,PW_CHILD_CODE_OFFER,&hash,bytes,&fd); }
/* The file cluster's object: the first helper's read is the only one; every later helper maps it. */
static int code_round_file(unsigned kind, const uint64_t file[3], uint64_t bytes, int *fd)
{
    if(__atomic_exchange_n(&code_asking,1,__ATOMIC_ACQUIRE)) { errno=EBUSY;return -1; }
    struct pw_child_code_file request={{PW_CHILD_PROTOCOL,kind,getpid(),0,0},{file[0],file[1],file[2]},bytes};
    struct pw_child_code_answer answer={EIO,0};
    const int ask=kind==PW_CHILD_CODE_ASK_FILE;
    int fds[2]={-1,-1},error=0;
    unsigned count=0;
    if(code_reply[0]<0 && socketpair(AF_UNIX,SOCK_SEQPACKET,0,code_reply)) { error=errno;code_reply[0]=code_reply[1]=-1; }
    else {
        const int sent[2]={ask ? code_reply[1] : *fd,code_reply[1]};
        if(pw_child_send(child_control,&request,sizeof(request),sent,ask ? 1 : 2)) error=errno ? errno : EIO;
        else if(!ready_for(code_reply[0],POLLIN,1000)) error=errno ? errno : ETIMEDOUT;
        else if(pw_child_recv(code_reply[0],&answer,sizeof(answer),fds,&count)!=(ssize_t)sizeof(answer) ||
                count!=(ask && !answer.status ? 1u : 0u)) error=EIO;
    }
    if(error) {
        for(unsigned i=0;i<count;++i)close(fds[i]);
        if(code_reply[0]>=0) { close(code_reply[0]);close(code_reply[1]);code_reply[0]=code_reply[1]=-1; }
    }
    __atomic_store_n(&code_asking,0,__ATOMIC_RELEASE);
    if(!error && answer.status) error=answer.status;
    if(error) { errno=error;return -1; }
    if(ask)*fd=fds[0];
    return 0;
}
static int code_round_by_file(uint32_t kind,const void *key,uint64_t bytes,int *fd)
{ return code_round_file(kind,(const uint64_t *)key,bytes,fd); }
int pw_ps5_code_ask_file(const uint64_t file[3], uint64_t bytes, int *fd) { return code_round_timed(code_round_by_file,PW_CHILD_CODE_ASK_FILE,file,bytes,fd); }
int pw_ps5_code_offer_file(const uint64_t file[3], uint64_t bytes, int fd) { return code_round_timed(code_round_by_file,PW_CHILD_CODE_OFFER_FILE,file,bytes,&fd); }
int pw_ps5_child_frame(int fd)
{
    const struct pw_child_message message={PW_CHILD_PROTOCOL,PW_CHILD_FRAME,getpid(),0,0};
    /* A program in the title hands its page to the title's broker directly (it keeps fd, as a helper's send does). */
    if(child_control<0 && in_title_client() && frame_attach) {
        const int copy=dup(fd);
        if(copy<0)return -1;
        frame_attach(getpid(),copy);
        return 0;
    }
    if(child_control<0) { errno=ENOTCONN;return -1; }
    return pw_child_send(child_control,&message,sizeof(message),&fd,1);
}
int pw_ps5_child_window(int fd,size_t bytes)
{
    if(fd>=0 && bytes && bytes<=INT32_MAX && child_control<0 && in_title_client()) {
        /* A program in the title: the title's broker attaches its window page here, as for a helper's. */
        const int copy=window_attach ? dup(fd) : -1;
        if(!window_attach) { errno=ENOTSUP;return -1; }
        if(copy<0)return -1;
        const int status=window_attach(getpid(),copy,bytes);
        if(status) { errno=status;return -1; }
        return 0;
    }
    if(child_control<0 || fd<0 || !bytes || bytes>INT32_MAX) { errno=EINVAL;return -1; }
    int pair[2]={-1,-1},result=-1;
    if(pw_child_socketpair(pair,sizeof(struct pw_child_message)))return -1;
    const struct pw_child_message message={PW_CHILD_PROTOCOL,PW_CHILD_WINDOW,getpid(),(int32_t)bytes,0};
    const int passed[2]={fd,pair[1]};
    if(!ready(child_control,POLLOUT) || pw_child_send(child_control,&message,sizeof(message),passed,2))goto done;
    close(pair[1]);pair[1]=-1;
    struct pw_child_message reply;
    if(!ready_for(pair[0],POLLIN,5000) || recv(pair[0],&reply,sizeof(reply),0)!=sizeof(reply) ||
       reply.protocol!=PW_CHILD_PROTOCOL || reply.kind!=PW_CHILD_WINDOW || reply.pid!=getpid() || reply.tid)
        { errno=EPROTO;goto done; }
    if(reply.value) { errno=reply.value;goto done; }
    result=0;
done:
    { int saved=errno;close(pair[0]);if(pair[1]>=0)close(pair[1]);errno=saved; }
    return result;
}
/* The helper loader checks that the ntdll it loaded speaks its protocol (native/proton_wine_child.c). */
unsigned int pw_ps5_child_protocol(void) { return PW_CHILD_PROTOCOL; }
/* The CPUs at one level of this process's CPU sets, 0 when the kernel answers none (its set size is not this SDK's
 * cpuset_t, so each size is tried, as native/proton_wine.cpp does). */
static unsigned long long level_cpus(int level)
{
    static const size_t sizes[] = {8, 16, 24, 32, 64};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        _Alignas(8) unsigned char set[64] = {0};
        unsigned long long bits = 0;
        if (cpuset_getaffinity(level, CPU_WHICH_PID, -1, sizes[i], (cpuset_t *)(void *)set)) continue;
        memcpy(&bits, set, sizeof(bits));
        return bits;
    }
    return 0;
}
int pw_ps5_child_start(int fd)
{
    pthread_t thread;
    pthread_attr_t attributes;
    /* Where a helper may run, at each level, against the title's line ("cpu: this process may run on ..."): a game
     * helper had CPUs 9, 11, 13-15 while the title had 0-12 and most of them idle (docs/PLAN.md). Read only. */
    fprintf(stderr, "wine-ps5: cpus may run on %#llx, its CPU set holds %#llx, the root set above it %#llx\n",
            level_cpus(CPU_LEVEL_WHICH), level_cpus(CPU_LEVEL_CPUSET), level_cpus(CPU_LEVEL_ROOT));
    child_control = fd;
    if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,dsection_reply)) dsection_reply[0]=dsection_reply[1]=-1;
    pthread_attr_init(&attributes);
    /* The loader's scheduling tier (src/pw_helper_sched.h); a refusal keeps libkernel's default. */
    (void)pthread_attr_setinheritsched(&attributes, PTHREAD_INHERIT_SCHED);
    const int status = pthread_create(&thread, &attributes, child_agent, NULL);
    pthread_attr_destroy(&attributes);
    return status;
}
static int exit_sent;   /* PW_CHILD_EXIT went to the title: once per process */
void pw_ps5_child_exit_notice(unsigned int code)
{
    const struct pw_child_message message = {PW_CHILD_PROTOCOL, PW_CHILD_EXIT, getpid(), (int32_t)code, 0};
    const int saved = errno;
    if (child_control < 0 || __atomic_exchange_n(&exit_sent, 1, __ATOMIC_SEQ_CST)) return;
    /* send, not sendmsg or poll: ntdll wraps those (pw_wine_fdv_libc.c), and the wraps may take a lock. */
    for (int tries = 0; tries < 200; ++tries) {
        if (send(child_control, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)sizeof(message)) break;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ENOBUFS) break;
        sceKernelUsleep(10000);
    }
    errno = saved;
}
static int64_t doom_stamps[PW_CHILD_DOOMS];
void pw_ps5_child_doomed(unsigned int reason)
{
    struct timespec now;
    /* A reason may come again (an exit: NtTerminateProcess, pw_ps5_child_exit): one clock read, for the first. */
    if (reason < 1 || reason > PW_CHILD_DOOMS || __atomic_load_n(&doom_stamps[reason - 1], __ATOMIC_RELAXED) ||
        clock_gettime(CLOCK_MONOTONIC, &now))
        return;
    /* The title says on screen that this program lost the GPU (PW_CHILD_DOOMED): send, not sendmsg (see above). */
    if (pw_child_doom_stamp(doom_stamps, reason, (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000) &&
        reason == PW_CHILD_DOOM_GPU && child_control >= 0) {
        const struct pw_child_message message = {PW_CHILD_PROTOCOL, PW_CHILD_DOOMED, getpid(), (int32_t)reason, 0};
        const int saved = errno;
        (void)send(child_control, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT);
        errno = saved;
    }
}
unsigned int pw_ps5_child_doom(int64_t *stamps, unsigned int count) { return pw_child_doom_read(doom_stamps, stamps, count); }
void pw_ps5_child_exit(unsigned int status)
{
    struct pw_child_message message = {PW_CHILD_PROTOCOL, PW_CHILD_EXIT, getpid(), (int32_t)status, 0};
    /* A title whose monitor of this helper is stuck never reads the send below, and this thread parks for good. */
    pw_ps5_child_doomed(PW_CHILD_DOOM_EXIT);
    payload_trace("exit-send-begin",getpid(),child_control,(int)status);
    if (child_control >= 0 && !__atomic_exchange_n(&exit_sent, 1, __ATOMIC_SEQ_CST)) {
        ssize_t sent=send(child_control, &message, sizeof(message), MSG_NOSIGNAL);
        payload_trace("exit-send-end",getpid(),child_control,sent<0 ? errno : (int)sent);
    }
    for (;;) sceKernelUsleep(100000);
}
