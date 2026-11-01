/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef __linux__
#define _GNU_SOURCE /* REG_RIP in the host test */
#endif
#include "pw_wine_threads.h"
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef __PROSPERO__
#include <machine/sysarch.h>
#ifndef AMD64_GET_GSBASE
#define AMD64_GET_GSBASE 130    /* FreeBSD's number, which the SDK's header lacks (Wine's signal_x86_64.c) */
#endif
#endif

typedef struct Entry { long tid; pthread_t thread; } Entry;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Entry entries[PW_WINE_THREADS_MAX];
static unsigned count;
static int (*remote_close)(pid_t);
static int (*remote_signal)(pid_t,long,int);
void pw_wine_threads_set_remote_signal(int (*callback)(pid_t,long,int), int (*close_callback)(pid_t))
{
    __atomic_store_n(&remote_signal,callback,__ATOMIC_RELEASE);
    __atomic_store_n(&remote_close,close_callback,__ATOMIC_RELEASE);
}

int pw_wine_process_close(pid_t pid)
{
    int (*callback)(pid_t) = __atomic_load_n(&remote_close,__ATOMIC_ACQUIRE);
    if (callback) return callback(pid);
    errno=ESRCH; return -1;
}
static Entry *find_locked(long tid)
{
    for(unsigned i=0;i<count;i++)
        if(entries[i].tid==tid)return &entries[i];
    return NULL;
}

static void (*thread_start)(void);
int __wine_ps5_set_payload_thread_start(void (*hook)(void))
{
    if(!hook)return -1;
    __atomic_store_n(&thread_start,hook,__ATOMIC_RELEASE);
    return 0;
}

int pw_wine_thread_register(long tid,pthread_t thread)
{
    int status=0;
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    if(entry)entry->thread=thread;
    else if(count<PW_WINE_THREADS_MAX)entries[count++]=(Entry){tid,thread};
    else status=-1;
    pthread_mutex_unlock(&lock);
    /* Every registration is a thread starting, a replaced entry too (the kernel reused its id); a full table does not
     * excuse it from its scheduling. Outside the lock: the hook may make system calls that block. */
    void (*hook)(void)=__atomic_load_n(&thread_start,__ATOMIC_ACQUIRE);
    if(hook)hook();
    return status;
}
void pw_wine_thread_started(void)
{
    void (*hook)(void)=__atomic_load_n(&thread_start,__ATOMIC_ACQUIRE);
    if(hook)hook();
}
void pw_wine_thread_unregister(long tid)
{
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    if(entry)*entry=entries[--count];
    pthread_mutex_unlock(&lock);
}
int pw_wine_thread_kill(pid_t pid,long tid,int signal)
{
    int status;
    if(pid!=getpid()) {
        int (*callback)(pid_t,long,int)=__atomic_load_n(&remote_signal,__ATOMIC_ACQUIRE);
        if(callback)return callback(pid,tid,signal);
        errno=ESRCH;return -1;
    }
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    status=entry?pthread_kill(entry->thread,signal):ESRCH;
    pthread_mutex_unlock(&lock);
    /* A kill that does not reach its thread leaves the process alive: say how each one went. */
    if(signal==SIGQUIT)fprintf(stderr,"wine-ps5: kill thread %04lx: %d\n",tid,status);
    if(status){errno=status;return -1;}
    return 0;
}
int pw_wine_thread_set_priority(long tid,int policy,int priority)
{
    int status;
    struct sched_param param={0};
    param.sched_priority=priority;
    pthread_mutex_lock(&lock);
    Entry *entry=find_locked(tid);
    /* Unregister runs before pthread_exit; keep the handle alive until the
     * scheduler call finishes, including when an exiting thread races us. */
    status=entry?pthread_setschedparam(entry->thread,policy,&param):ESRCH;
    pthread_mutex_unlock(&lock);
    return status;
}

/* The sampler. A slot belongs to a registry entry; the handler finds its own slot by thread, so no lock is taken in it. */
typedef struct Slot { volatile unsigned seq; volatile uint64_t rip, rsp, rax, top, teb; } Slot;
static Slot slots[PW_WINE_THREADS_MAX];
static pthread_mutex_t sample_lock=PTHREAD_MUTEX_INITIALIZER;
static int sample_installed;

static void sample_handler(int signal,siginfo_t *info,void *context)
{
    (void)signal;(void)info;
    const ucontext_t *uc=context;
    const pthread_t self=pthread_self();
    unsigned n=__atomic_load_n(&count,__ATOMIC_ACQUIRE);
    if(n>PW_WINE_THREADS_MAX)n=PW_WINE_THREADS_MAX;
    for(unsigned i=0;i<n;i++){
        if(!pthread_equal(entries[i].thread,self))continue;
#ifdef __linux__
        slots[i].rip=(uint64_t)uc->uc_mcontext.gregs[REG_RIP];
        slots[i].rsp=(uint64_t)uc->uc_mcontext.gregs[REG_RSP];
        slots[i].rax=(uint64_t)uc->uc_mcontext.gregs[REG_RAX];
#else
        slots[i].rip=(uint64_t)uc->uc_mcontext.mc_rip;
        slots[i].rsp=(uint64_t)uc->uc_mcontext.mc_rsp;
        slots[i].rax=(uint64_t)uc->uc_mcontext.mc_rax;
#endif
#ifdef __PROSPERO__
        /* A Wine thread's %gs base is its TEB, which holds its own address at 0x30. A thread registers before its first
         * server request, before Wine sets that base: it is still 0 then, and %gs:0x30 faulted at address 0x30 (a game's
         * helper with WINE_PS5_SAMPLE=1, 2026-11-01: its main thread, sampled in server_init_process). libkernel's
         * sysarch reads the base, as Wine's own signal handlers do; without one the thread has no TEB yet. */
        {
            void *base=NULL;
            uint64_t teb=0;
            if(!sysarch(AMD64_GET_GSBASE,&base)&&base)__asm__ volatile("movq %%gs:0x30,%0" : "=r"(teb));
            slots[i].teb=teb;
        }
#else
        slots[i].teb=0;
#endif
        /* The thread's own stack, which it can read: the word at the stack pointer is the return address in a call stub. */
        slots[i].top=slots[i].rsp&7?0:*(const volatile uint64_t *)(uintptr_t)slots[i].rsp;
        __atomic_store_n(&slots[i].seq,slots[i].seq+1,__ATOMIC_RELEASE);
        return;
    }
}

unsigned pw_wine_threads_sample(PwWineThreadSample *out,unsigned max,unsigned wait_us)
{
    unsigned seen[PW_WINE_THREADS_MAX];
    unsigned written=0;
    pthread_mutex_lock(&sample_lock);
    if(!sample_installed){
        struct sigaction action;
        memset(&action,0,sizeof(action));
        action.sa_sigaction=sample_handler;
        action.sa_flags=SA_SIGINFO|SA_RESTART|SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        if(!sigaction(SIGPROF,&action,NULL))sample_installed=1;
    }
    if(!sample_installed){pthread_mutex_unlock(&sample_lock);return 0;}
    pthread_mutex_lock(&lock);
    unsigned n=count<max?count:max;
    for(unsigned i=0;i<n;i++){
        seen[i]=__atomic_load_n(&slots[i].seq,__ATOMIC_ACQUIRE);
        if(pthread_kill(entries[i].thread,SIGPROF))seen[i]=~0u;
    }
    for(unsigned waited=0;waited<wait_us;waited+=100){
        unsigned pending=0;
        for(unsigned i=0;i<n;i++)
            if(seen[i]!=~0u&&__atomic_load_n(&slots[i].seq,__ATOMIC_ACQUIRE)==seen[i])pending++;
        if(!pending)break;
        usleep(100);
    }
    for(unsigned i=0;i<n;i++){
        PwWineThreadSample *sample=&out[written++];
        clockid_t clock;
        struct timespec cpu;
        sample->tid=entries[i].tid;
        sample->stale=seen[i]==~0u||__atomic_load_n(&slots[i].seq,__ATOMIC_ACQUIRE)==seen[i];
        sample->rip=sample->stale?0:slots[i].rip;
        sample->rsp=sample->stale?0:slots[i].rsp;
        sample->rax=sample->stale?0:slots[i].rax;
        sample->top=sample->stale?0:slots[i].top;
        sample->teb=sample->stale?0:slots[i].teb;
        sample->cpu_ns=0;
        if(!pthread_getcpuclockid(entries[i].thread,&clock)&&!clock_gettime(clock,&cpu))
            sample->cpu_ns=(uint64_t)cpu.tv_sec*1000000000ull+(uint64_t)cpu.tv_nsec;
    }
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&sample_lock);
    return written;
}
