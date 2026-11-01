/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_THREADS_H
#define PW_WINE_THREADS_H
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

/* The in-process wineserver signals Wine's threads (suspend, APCs, kill)
 * with thr_kill2(pid, lwpid, signal), which the title libraries do not
 * provide; they only have pthread_kill. ntdll registers each thread's
 * kernel thread id with its pthread as the thread first talks to the
 * server, and wineserver.prx's thr_kill2 looks the id up. Registering an
 * id again replaces the entry, since the kernel reuses ids. */
enum { PW_WINE_THREADS_MAX = 1024 };

void pw_wine_threads_set_remote_signal(int (*signal_thread)(pid_t, long, int), int (*close_process)(pid_t));
int pw_wine_process_close(pid_t pid);
/* Called by the registering thread itself (ntdll's register_inprocess_thread, for the first thread and every later
 * one, before its first server request and before any Windows code). 0, or -1 when the table is full; either way the
 * thread start hook, when set, then runs on that thread, outside the registry's lock. */
int pw_wine_thread_register(long tid,pthread_t thread);
/* The thread start hook: a helper's payload loader sets it (native/proton_wine_child.c, its scheduling,
 * src/pw_helper_sched.h), and so does the title before it runs a game itself (native/proton_wine.cpp,
 * title_program_thread_start): the same plan for a game's threads wherever it runs. It cannot be unset. 0, or -1 for
 * NULL. */
int __wine_ps5_set_payload_thread_start(void (*hook)(void));
/* The hook, on the calling thread, when one is set: the title's Windows threads, which register with the in-process
 * server instead of this registry (patch 2405). */
void pw_wine_thread_started(void);
void pw_wine_thread_unregister(long tid);
/* thr_kill2 on the registered threads of this process: 0, or -1 with
 * errno ESRCH for another process or an unknown id. */
int pw_wine_thread_kill(pid_t pid,long tid,int signal);
/* pthread_setschedparam on a registered thread: 0, or an errno value (ESRCH
 * for an unknown id, or what the system refused). */
int pw_wine_thread_set_priority(long tid,int policy,int priority);

/* What each registered thread is running right now, for a profile built from many samples (the helper's telemetry takes
 * ten a second). Each thread is signalled with SIGPROF and its handler stores the interrupted instruction and stack
 * pointers; a thread that has not stored them within `wait_us` (it is inside a server call or another region that blocks
 * signals) is reported with `stale` set, which is itself a measure: that thread was not running Windows code. cpu_ns is the
 * thread's own CPU time since it began, user and system together. Nothing here allocates or takes a lock the signalled
 * thread could hold. Returns the number of threads written, at most `max`. */
typedef struct PwWineThreadSample {
    int64_t tid;
    uint64_t rip, rsp, cpu_ns;
    uint64_t rax;   /* a thread inside a system call: its number (the stub's `syscall` has rax set) */
    uint64_t top;   /* the word at rsp: for a thread in a call stub, where the stub returns to */
    uint64_t teb;   /* the thread's Windows TEB (its %gs base on the console), for ntdll's pw_wine_describe_thread */
    unsigned stale;
} PwWineThreadSample;
unsigned pw_wine_threads_sample(PwWineThreadSample *out, unsigned max, unsigned wait_us);

#endif
