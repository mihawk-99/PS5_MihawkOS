/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_NTSYNC_H
#define PW_NTSYNC_H
/*
 * Windows synchronization objects for Wine's in-process path, shared by the
 * title and every helper process.
 *
 * Wine keeps events, mutexes and semaphores in a synchronization device when
 * it has one (server/inproc_sync.c and dlls/ntdll/unix/sync.c, over Linux's
 * /dev/ntsync), so a wait, a SetEvent or a ReleaseMutex never goes to the
 * server: the server creates each object in the device and hands its number
 * to the threads that use it. A PS5 has no such device in its kernel. This is
 * that device, as a library: objects in a table, numbered as Wine numbers
 * descriptors, one lock over them all, and a word per waiting thread to sleep
 * on.
 *
 * The table lives in one region of shared memory that every process using
 * the device maps, each at its own address (so it holds indices, never
 * pointers): the title, whose server creates the objects, and each helper,
 * whose threads wait and signal. A sleeping thread is woken through its word
 * from any process: the kernel finds the sleeper by the memory object and
 * offset (umtx on the console, futex on a Linux host). Without it every
 * helper's wait and SetEvent was a round trip to the title's server thread:
 * A Direct3D 12 game in a helper made 40k to 634k server calls per 10 s, mostly
 * event_op and select, 63 to 1600 us each, about 40 to 50 thread-seconds of
 * waiting per 10 s, and drew 1 to 25 frames a second (2026-10-16; record
 * removed from the tree, in git history).
 * In one process the device took a SetEvent + ResetEvent from 29.3 us to
 * 0.20 us and a signalled wait from 16.6 us to 0.96 us (sync-probe, R3).
 *
 * A process can end in the middle of anything, so the device is told: before
 * the title kills a helper, and once it has ended, pw_ntsync_process_gone
 * takes its blocked waits out of the queues (a wake meant for a live thread
 * is never spent on a dead one), and any of its threads that enters the
 * device afterwards stays inside until the kill lands, so none is cut off
 * halfway through a change to the shared table.
 *
 * The semantics are those ntsync documents for its users
 * (Documentation/userspace-api/ntsync.rst in Linux), implemented here
 * independently:
 *   - a semaphore is signaled while its count is nonzero, and a wait takes
 *     one unit; a release past the maximum fails and changes nothing;
 *   - a mutex is signaled for a thread when it is unowned or owned by that
 *     thread (recursively, up to UINT32_MAX); a wait takes one level; a
 *     mutex whose owner died (kill) is abandoned, and the next wait that
 *     takes it reports that once;
 *   - an event is signaled while set; a wait resets an auto-reset event;
 *     pulse sets, satisfies whoever it can, and resets, atomically;
 *   - wait-any takes the first signaled object by index; wait-all takes all
 *     of them at once or none (the same object twice is invalid there); a
 *     signaled alert event ends a wait no object satisfied, with index equal
 *     to the object count;
 *   - waiters are served in the order they started waiting.
 *
 * Every function returns 0 or a negative errno value (-EINVAL for an unknown
 * object or bad arguments, -EOVERFLOW, -EPERM, -ETIMEDOUT, -EOWNERDEAD,
 * -ENOMEM), except the create and reference calls, which return an object
 * number (always positive) or a negative errno value.
 */
#include <stdint.h>
#include <stddef.h>

enum {
    PW_NTSYNC_MAX_WAIT = 64,
    /* What Wine keeps as the device's descriptor: never an object number. */
    PW_NTSYNC_DEVICE = 0x7ffffffe,
};

/* The bytes of shared memory the device needs (about 10 MiB: 131072 objects, 4096 blocked waits, 32768 queue
 * entries, 256 processes). */
size_t pw_ntsync_region_bytes(void);
/* The census counts a shared region in full in every process that maps it (docs/PLAN.md, K14): all 8.6 MiB of this
 * one in each helper, 214 against 153 MiB for an idle desktop's helpers (the Pro, 2026-10-20), 17 helpers in a
 * launcher's start, where the objects it used, 8558 at most, fill 0.5 MiB. So a helper maps only the fixed
 * tables (the first pw_ntsync_fixed_bytes()) and gives the device a mapper, which it calls, under its lock, the first
 * time the process reaches a 64 KiB chunk of 1024 objects: map(offset, bytes) maps that much of the region from offset
 * and returns where. The title maps the region whole and sets none. Set before pw_ntsync_attach. */
size_t pw_ntsync_fixed_bytes(void);
void pw_ntsync_set_chunk_mapper(void *(*map)(size_t offset, size_t bytes));
/* Uses region for this process's device: format it (the first process; region zeroed) or join it (format 0).
 * 0, or -EINVAL (too small, not formatted, another layout), -EBUSY (attached already), -ENOSPC (no room for another
 * process). A process that never attaches gets a region of its own on its first call. */
int pw_ntsync_attach(void *region, size_t bytes, int format);
/* The process pid will not use the device again (see above): before it is killed or while it ends (ended 0), or
 * once it has ended (ended 1, which also frees its place). Its blocked waits leave the queues at the first call;
 * their places are kept until ended 1, since its threads may still sleep on their words. A gone process that ended
 * holding the device's lock (killed on its way in) has it taken back by ended 1. The calling process cannot
 * name itself. It waits at most wait_ms for the device's lock, so a helper stuck holding it cannot stop the title's
 * hang breaker. Returns how many of its blocked waits this call dropped, or -ESRCH (no such process, or no device
 * here), -EBUSY (the lock stayed held), -EINVAL. */
int pw_ntsync_process_gone(int pid, int ended, uint32_t wait_ms);
/* Tests only: called inside the device before its lock is asked for; and holding it, on a gone process's thread's
 * way in, before it gives it back. */
extern void (*pw_ntsync_test_before_lock)(void);
extern void (*pw_ntsync_test_gone_holding)(void);
/* Tests only: the calling thread's number, when nonzero (a number the console gives again to a new thread). */
extern uint32_t (*pw_ntsync_test_thread_number)(void);

int pw_ntsync_create_semaphore(uint32_t count, uint32_t max);
/* owner and count are both zero (unowned) or both nonzero */
int pw_ntsync_create_mutex(uint32_t owner, uint32_t count);
int pw_ntsync_create_event(int manual, int signaled);
/* One more reference to an object (another holder of its number). */
int pw_ntsync_ref(int object);
/* Drops one reference; the object goes when the last is gone and no wait
 * uses it. */
int pw_ntsync_release(int object);

int pw_ntsync_semaphore_release(int object, uint32_t count, uint32_t *previous);
int pw_ntsync_semaphore_read(int object, uint32_t *count, uint32_t *max);
/* previous: the recursion count before the unlock */
int pw_ntsync_mutex_unlock(int object, uint32_t owner, uint32_t *previous);
/* The owner died: an owned mutex becomes unowned and abandoned. */
int pw_ntsync_mutex_kill(int object, uint32_t owner);
/* -EOWNERDEAD (owner and count 0) while abandoned and not taken since. */
int pw_ntsync_mutex_read(int object, uint32_t *owner, uint32_t *count);
int pw_ntsync_event_set(int object, uint32_t *previous);
int pw_ntsync_event_reset(int object, uint32_t *previous);
int pw_ntsync_event_pulse(int object, uint32_t *previous);
int pw_ntsync_event_read(int object, uint32_t *manual, uint32_t *signaled);

typedef struct PwNtsyncWait {
    uint64_t timeout;        /* absolute, ns; UINT64_MAX waits forever */
    int realtime;            /* timeout is on CLOCK_REALTIME, else CLOCK_MONOTONIC */
    int all;                 /* wait-all, else wait-any */
    const int *objects;
    uint32_t count;          /* 0..PW_NTSYNC_MAX_WAIT */
    uint32_t owner;          /* the waiting thread, for mutexes; nonzero if one is waited on */
    int alert;               /* an event that ends the wait, or 0 */
    uint32_t index;          /* out: the object that ended it (count: the alert) */
} PwNtsyncWait;
/* 0 or -EOWNERDEAD (index set), -ETIMEDOUT, -ECANCELED (a SIGQUIT reached the thread: Wine is ending it, and it
 * waits no more), or an error. */
int pw_ntsync_wait(PwNtsyncWait *wait);

/* Objects alive and threads blocked right now, for tests and diagnostics. A
 * blocked wait sleeps without the device's lock. */
void pw_ntsync_census(uint32_t *objects, uint32_t *waiters);

/* ---- diagnostics, for a game that hangs -------------------------------- */

/* Running totals, read without the lock (so a stuck device cannot stall the
 * reader): objects alive and threads blocked now; waits, those that slept,
 * those that timed out after sleeping, waiters served, signalling calls; and
 * signals deferred because they landed on a thread inside the device, and
 * raised again after. */
typedef struct PwNtsyncCounters {
    uint32_t objects, blocked;
    uint64_t waits, slept, timeouts, wakes, signals;
    uint32_t deferred, raised;      /* this process's threads */
    uint32_t processes;             /* using the device now */
    uint32_t exhausted;             /* creates and waits refused for want of room */
    uint32_t dropped;               /* blocked waits dropped with their process */
} PwNtsyncCounters;
void pw_ntsync_counters(PwNtsyncCounters *counters);

/* For a signal handler, first thing (async-signal-safe): 1 when the signal
 * landed on a thread inside the device, which then raises it again as soon as
 * it is out, and the handler returns at once. Wine calls the device as it
 * would a kernel driver, which no signal interrupts halfway: a handler that
 * ran here and needed the device, or a lock whose holder waits for the
 * device (the server's), would never return. Signals 1 to 31. A SIGQUIT (Wine ending the thread) on a thread
 * outside the device also ends its blocked wait and any later one, with -ECANCELED. */
int pw_ntsync_signal_deferred(int signal);
/* A critical section of another lock of this process's Unix side (Wine's heap): pw_ntsync_signal_deferred defers
 * a signal that lands between enter and leave, and the last leave raises it again. Calls nest. Never allocates. */
void pw_ntsync_critical_enter(void);
void pw_ntsync_critical_leave(void);
/* What an object is (the server's object type and id or handle), shown by the
 * dump. The first note stays until the object goes. */
int pw_ntsync_note(int object, const char *tag);
/* Keeps the last `capacity` operations (rounded up to a power of two, at most
 * 1 << 20), each with its time and kernel thread, for the dump; 0 stops and
 * frees them. Off by default. */
int pw_ntsync_trace(uint32_t capacity);
/* The device's state as lines of text: who holds its lock and since when,
 * counters, every blocked wait (thread, for how long, until when, and each
 * object's type, state, tag and queue), and with the trace on, the last
 * operations on those objects and the last `recent` operations overall.
 * Threads are Windows thread ids where a wait has shown which kernel thread
 * is which, else "u" and the kernel thread id. It waits up to wait_ms for the
 * lock; a lock held that long is reported, and the state read without it. */
void pw_ntsync_dump(void (*line)(void *context, const char *text), void *context, uint32_t recent,
                    uint32_t wait_ms);

#endif
