/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_ntsync.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__PROSPERO__) || defined(__FreeBSD__)
#include <pthread_np.h>
#include <sys/types.h>
#include <sys/umtx.h>
#elif defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

enum { SEMAPHORE = 1, MUTEX, EVENT };
/* object number: generation (10 bits) above the slot number plus one (20 bits) */
enum { SLOT_BITS = 20, SLOT_MASK = (1 << SLOT_BITS) - 1, GENERATION_MASK = 0x3ff };
/* What the region holds. Every reference in it is an index plus one (0: none), never a pointer: each process maps
 * it at its own address. */
enum {
    OBJECTS = 1 << 17, WAITERS = 4096, ENTRIES = 1 << 15, PROCESSES = 256,
    MAGIC = 0x504e5453,     /* "PNTS" */
    LAYOUT = 3,                 /* 2: the lock word names its holder's process; 3: objects name their slot, in chunks */
    CHUNK_OBJECTS = 1024,       /* a helper maps the objects table this many at a time (pw_ntsync_set_chunk_mapper) */
};
enum { PROCESS_FREE, PROCESS_USING, PROCESS_GONE };
/* A waiter's place: free, used by a wait, or kept for a wait of a process told gone that has not ended yet, whose
 * thread may still sleep on its word (pw_ntsync_process_gone). */
enum { WAITER_FREE, WAITER_USED, WAITER_KEPT };
enum { WAKE_ENDING = 2 };

typedef struct Entry {          /* one waiter on one object, in the object's queue */
    uint32_t prev, next;        /* in the object's queue */
    uint32_t waiter, object;
    uint32_t index;             /* in the wait's objects; count for the alert */
    uint32_t next_in_wait;      /* the wait's next entry (its objects in order, the alert last); the free list */
} Entry;

typedef struct Object {
    uint32_t type;
    uint32_t references;        /* holders of the number, and waits using it */
    uint32_t generation;
    uint32_t next_free;
    union {
        struct { uint32_t count, max; } semaphore;
        struct { uint32_t owner, count, abandoned; } mutex;
        struct { uint32_t manual, signaled; } event;
    } u;
    uint32_t head, tail, queued;   /* the FIFO of waiters */
    char tag[20];               /* what it is, for the dump (pw_ntsync_note) */
    uint32_t slot;              /* its own place in the table, which a process that maps it in chunks cannot compute */
} Object;
_Static_assert(sizeof(Object) == 64, "a chunk of the objects table is 64 KiB");

typedef struct Waiter {
    uint32_t wake;              /* the word its thread sleeps on: 0 until served, then 1; WAKE_ENDING from a SIGQUIT */
    uint32_t used, all, done;   /* used: WAITER_* */
    int32_t result;
    uint32_t owner, count, index;
    uint32_t process;           /* its thread's process slot */
    uint32_t thread;            /* kernel thread */
    uint32_t first;             /* its entries */
    uint32_t queued;            /* its entries are in the queues, each holding a reference */
    uint32_t next_free;
    uint32_t blocked_prev, blocked_next;   /* while blocked, for the dump */
    int32_t realtime;
    uint64_t since, timeout;    /* a time stamp; the wait's own deadline */
} Waiter;

typedef struct Process {
    int32_t pid;
    uint32_t state;
} Process;

typedef struct Shared {
    uint32_t magic, layout;
    uint64_t bytes;
    uint32_t lock;              /* 0 free; held: the holder's process slot << 2 | 1, | 2 with threads asleep on it */
    int32_t holder_pid;
    uint64_t holder_state;      /* thread | phase << 32 | call << 40 */
    uint64_t held_since;
    uint32_t object_count, live_objects, free_objects;
    uint32_t waiter_count, free_waiters, entry_count, free_entries;
    uint32_t blocked, blocked_first, blocked_last;
    uint32_t processes_using, exhausted, dropped;
    uint32_t reentries, unlock_errors;
    uint32_t locks_taken_back;  /* from gone processes that ended holding it */
    uint64_t count_waits, count_blocks, count_timeouts, count_wakes, count_signals;
    uint64_t base_stamp, base_ns;   /* to turn time stamps into time */
    Process processes[PROCESSES];
    Waiter waiters[WAITERS];
    Entry entries[ENTRIES];
    Object objects[OBJECTS] __attribute__((aligned(CHUNK_OBJECTS * 64)));   /* each chunk at a mappable offset */
} Shared;

static Shared *shared;
/* This process's view of the objects table: the title maps the region whole; a helper maps the fixed tables and then
 * each 64 KiB chunk of objects the first time it reaches one, under the device's lock (pw_ntsync_set_chunk_mapper). */
static Object *chunks[OBJECTS / CHUNK_OBJECTS];
static void *(*map_chunk)(size_t offset, size_t bytes);
static uint32_t me_process;     /* this process's slot */
static int32_t me_pid;          /* getpid() once, not a system call per operation */
static pthread_once_t private_once = PTHREAD_ONCE_INIT;
void (*pw_ntsync_test_before_lock)(void);
void (*pw_ntsync_test_gone_holding)(void);
uint32_t (*pw_ntsync_test_thread_number)(void);

static Object *slot_object(uint32_t slot)
{
    const uint32_t chunk = slot / CHUNK_OBJECTS;
    if (slot >= OBJECTS) return NULL;
    if (!chunks[chunk])
        chunks[chunk] = map_chunk ? map_chunk(offsetof(Shared, objects) + (size_t)chunk * sizeof(Object) * CHUNK_OBJECTS,
                                              sizeof(Object) * CHUNK_OBJECTS)
                                  : &shared->objects[(size_t)chunk * CHUNK_OBJECTS];
    return chunks[chunk] ? &chunks[chunk][slot % CHUNK_OBJECTS] : NULL;
}
static Object *object_at(uint32_t ref) { return ref ? slot_object(ref - 1) : NULL; }
static Waiter *waiter_at(uint32_t ref) { return ref ? &shared->waiters[ref - 1] : NULL; }
static Entry *entry_at(uint32_t ref) { return ref ? &shared->entries[ref - 1] : NULL; }
static uint32_t object_ref(const Object *object) { return object->slot + 1; }
static uint32_t waiter_ref(const Waiter *waiter) { return (uint32_t)(waiter - shared->waiters) + 1; }
static uint32_t entry_ref(const Entry *entry) { return (uint32_t)(entry - shared->entries) + 1; }

/* ---- the words threads sleep on, in any process -------------------------
 * The kernel finds a sleeper by the memory object and offset of its word, so a
 * wake reaches a thread of another process that maps the same region. */
static int sleep_word(uint32_t *word, uint32_t expected, const uint64_t *deadline, int realtime)
{
#if defined(__PROSPERO__) || defined(__FreeBSD__)
    struct _umtx_time time;
    if (!deadline) return _umtx_op(word, UMTX_OP_WAIT_UINT, expected, NULL, NULL) ? errno : 0;
    memset(&time, 0, sizeof(time));
    time._timeout.tv_sec = (time_t)(*deadline / 1000000000u);
    time._timeout.tv_nsec = (long)(*deadline % 1000000000u);
    time._flags = UMTX_ABSTIME;
    time._clockid = realtime ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    return _umtx_op(word, UMTX_OP_WAIT_UINT, expected, (void *)sizeof(time), &time) ? errno : 0;
#elif defined(__linux__)
    struct timespec time;
    if (deadline) {
        time.tv_sec = (time_t)(*deadline / 1000000000u);
        time.tv_nsec = (long)(*deadline % 1000000000u);
    }
    return syscall(SYS_futex, word, FUTEX_WAIT_BITSET | (realtime ? FUTEX_CLOCK_REALTIME : 0), expected,
                   deadline ? &time : NULL, NULL, FUTEX_BITSET_MATCH_ANY) ? errno : 0;
#else
#error "no wait on a shared word for this system"
#endif
}

static void wake_word(uint32_t *word)
{
#if defined(__PROSPERO__) || defined(__FreeBSD__)
    _umtx_op(word, UMTX_OP_WAKE, 1, NULL, NULL);
#else
    syscall(SYS_futex, word, FUTEX_WAKE, 1, NULL, NULL, 0);
#endif
}

/* The device's lock, a word in the region: taken with a compare and swap, slept on when it is busy. It names the
 * holder's process, so the title can take it back from a gone process that ended holding it
 * (pw_ntsync_process_gone). */
enum { LOCK_HELD = 1, LOCK_SLEEPERS = 2, LOCK_SLOT_SHIFT = 2 };
static uint32_t lock_mine(void) { return me_process << LOCK_SLOT_SHIFT | LOCK_HELD; }

static void lock_take(void)
{
    uint32_t seen = 0;
    if (__atomic_compare_exchange_n(&shared->lock, &seen, lock_mine(), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    for (int spin = 0; spin < 100; spin++) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
        seen = 0;
        if (__atomic_load_n(&shared->lock, __ATOMIC_RELAXED) == 0 &&
            __atomic_compare_exchange_n(&shared->lock, &seen, lock_mine(), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
    }
    for (;;) {
        seen = __atomic_load_n(&shared->lock, __ATOMIC_RELAXED);
        if (!seen) {
            /* threads may still sleep on it: the give wakes one */
            if (__atomic_compare_exchange_n(&shared->lock, &seen, lock_mine() | LOCK_SLEEPERS, 0, __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED))
                return;
            continue;
        }
        if (!(seen & LOCK_SLEEPERS) &&
            !__atomic_compare_exchange_n(&shared->lock, &seen, seen | LOCK_SLEEPERS, 0, __ATOMIC_RELAXED,
                                         __ATOMIC_RELAXED))
            continue;
        sleep_word(&shared->lock, seen | LOCK_SLEEPERS, NULL, 0);
    }
}

static int lock_try(void)
{
    uint32_t seen = 0;
    return __atomic_compare_exchange_n(&shared->lock, &seen, lock_mine(), 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void lock_give(void)
{
    if (__atomic_exchange_n(&shared->lock, 0, __ATOMIC_RELEASE) & LOCK_SLEEPERS) wake_word(&shared->lock);
}

/* ---- diagnostics ----------------------------------------------------------
 * Who holds the lock and since when (read without it by the dump), the
 * blocked waits, counters, and an optional ring of the last operations of
 * this process's threads. A thread is its kernel id; waits teach which
 * Windows thread each one is. */
enum { OP_CREATE = 1, OP_FREE, OP_WAIT, OP_BLOCK, OP_WAKE, OP_RETURN, OP_SET, OP_RESET, OP_PULSE,
       OP_SEM_RELEASE, OP_UNLOCK, OP_KILL, OP_REENTER };
typedef struct Record {
    uint64_t stamp;
    int object;
    uint32_t thread;            /* the kernel thread that did it */
    uint32_t a, b;              /* what it did, per operation */
    uint8_t op, c;
} Record;
/* Who holds the lock: the kernel thread, where it is (a phase) and in which
 * call, in one word stored and read at once. It stays set from the moment the
 * lock is taken until it is released for good, so the dump names a thread
 * stuck while it unlocks. */
enum { PHASE_INSIDE = 1, PHASE_LEAVING };
enum { CALL_WAIT = 1, CALL_CREATE, CALL_REF, CALL_RELEASE, CALL_SEM_RELEASE, CALL_SEM_READ, CALL_MUTEX_UNLOCK,
       CALL_MUTEX_KILL, CALL_MUTEX_READ, CALL_EVENT_SET, CALL_EVENT_RESET, CALL_EVENT_PULSE, CALL_EVENT_READ,
       CALL_NOTE, CALL_CENSUS, CALL_TRACE, CALL_PROCESS, CALL_COUNT };
static const char *const call_names[CALL_COUNT] = {
    "-", "wait", "create", "ref", "release", "semaphore release", "semaphore read", "mutex unlock", "mutex kill",
    "mutex read", "event set", "event reset", "event pulse", "event read", "note", "census", "trace", "process"};
static const char *const phase_names[] = {"-", "inside", "unlocking"};
/* Signals that landed on a thread of this process inside the device, which
 * its handler left for later (pw_ntsync_signal_deferred), those raised again
 * once the thread was out, and the last one deferred. */
static uint32_t signals_deferred, signals_raised;
static uint64_t signal_last;            /* thread | call << 40 | signal << 48 */
static Record *ring;
static uint32_t ring_mask;
static uint64_t ring_count;

/* Every kernel thread of this process that has used the device: its Windows
 * thread (from its waits), whether it is inside the device (from before it
 * asks for the lock until it has let it go), and the signals that landed
 * meanwhile. A slot is claimed once and kept, and a signal handler reaches its
 * own with atomics only. Wine calls the device as it would a kernel driver,
 * which no signal interrupts halfway; inside, a handler only notes its signal,
 * and the thread raises it again when it leaves. Then nothing a handler does
 * (a suspend's server call, say) can wait for a lock its own thread holds. */
typedef struct ThreadSlot {
    uint32_t thread, owner, inside, signals, deferrals;
    uint32_t critical;          /* depth of other critical sections (pw_ntsync_critical_enter): signals wait for them too */
    uint32_t ending;            /* a SIGQUIT reached it: Wine is ending it, and its waits end (pw_ntsync_wait) */
    uint32_t *asleep_on;        /* its blocked wait's word, while it has one */
    uint32_t exit_hook;         /* its thread's exit resets the slot (slot_reset) */
} ThreadSlot;
enum { THREAD_SLOTS = 4096 };
static ThreadSlot thread_slots[THREAD_SLOTS];
static uint32_t without_slot;           /* entries by threads the table had no room for */

static uint32_t thread_number(void)
{
    if (pw_ntsync_test_thread_number) {
        const uint32_t number = pw_ntsync_test_thread_number();
        if (number) return number;
    }
#if defined(__PROSPERO__) || defined(__FreeBSD__)
    return (uint32_t)pthread_getthreadid_np();
#elif defined(__linux__)
    return (uint32_t)syscall(SYS_gettid);
#else
    return (uint32_t)(uintptr_t)pthread_self();
#endif
}

static uint64_t now_ns(clockid_t clock)
{
    struct timespec now;
    clock_gettime(clock, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

/* A time stamp: the TSC where there is one, for it costs no system call. */
static uint64_t stamp(void)
{
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    return now_ns(CLOCK_MONOTONIC);
#endif
}

/* A slot belongs to a kernel thread number, which the console gives to a new thread once the old one has ended. So the
 * thread's exit resets what a later thread must not inherit: its mark as ending, which would end every wait of the
 * new thread at once, and its wait's word. A pthread key's destructor runs at the exit; it is set at the thread's
 * first entry into the device, never in a signal handler (which only calls pw_ntsync_signal_deferred). */
static pthread_key_t slot_key;
static pthread_once_t slot_key_once = PTHREAD_ONCE_INIT;
static int slot_key_made;

static void slot_reset(void *value)
{
    ThreadSlot *slot = value;
    __atomic_store_n(&slot->ending, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&slot->asleep_on, NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&slot->exit_hook, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&slot->critical, 0, __ATOMIC_SEQ_CST);
}

static void slot_key_make(void)
{
    slot_key_made = !pthread_key_create(&slot_key, slot_reset);
}

static void slot_hook_exit(ThreadSlot *slot)
{
    if (__atomic_load_n(&slot->exit_hook, __ATOMIC_RELAXED)) return;
    pthread_once(&slot_key_once, slot_key_make);
    if (slot_key_made && !pthread_setspecific(slot_key, slot)) __atomic_store_n(&slot->exit_hook, 1, __ATOMIC_SEQ_CST);
}

static ThreadSlot *thread_slot(uint32_t thread, int claim)
{
    uint32_t slot = (thread * 2654435761u) >> 20;
    for (uint32_t i = 0; thread && i < THREAD_SLOTS; i++, slot = (slot + 1) & (THREAD_SLOTS - 1)) {
        uint32_t seen = __atomic_load_n(&thread_slots[slot].thread, __ATOMIC_ACQUIRE);
        if (!seen) {
            if (!claim) return NULL;
            if (__atomic_compare_exchange_n(&thread_slots[slot].thread, &seen, thread, 0, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                return &thread_slots[slot];
        }
        if (seen == thread) return &thread_slots[slot];
    }
    return NULL;
}

static uint64_t holding(uint32_t thread, uint32_t phase, uint32_t call)
{
    return thread | (uint64_t)phase << 32 | (uint64_t)call << 40;
}
static uint32_t state_thread(uint64_t state) { return (uint32_t)state; }
static uint32_t state_phase(uint64_t state) { return (uint32_t)(state >> 32) & 0xff; }
static uint32_t state_call(uint64_t state) { return (uint32_t)(state >> 40) & 0xff; }

static void record(int op, int object, uint32_t a, uint32_t b, uint32_t c)
{
    Record *entry;
    if (!ring) return;
    entry = &ring[ring_count++ & ring_mask];
    entry->stamp = stamp();
    entry->object = object;
    entry->thread = state_thread(__atomic_load_n(&shared->holder_state, __ATOMIC_RELAXED));
    entry->a = a;
    entry->b = b;
    entry->op = (uint8_t)op;
    entry->c = (uint8_t)c;
}

/* ---- the region ----------------------------------------------------------- */

size_t pw_ntsync_region_bytes(void)
{
    return sizeof(Shared);
}

size_t pw_ntsync_fixed_bytes(void)
{
    return offsetof(Shared, objects);
}

void pw_ntsync_set_chunk_mapper(void *(*map)(size_t offset, size_t bytes))
{
    map_chunk = map;
}

static int claim_process(void)
{
    const int32_t pid = me_pid = (int32_t)getpid();
    for (uint32_t i = 0; i < PROCESSES; i++) {
        Process *process = &shared->processes[i];
        if (process->state != PROCESS_FREE) continue;
        process->pid = pid;
        process->state = PROCESS_USING;
        shared->processes_using++;
        me_process = i + 1;
        return 0;
    }
    return -ENOSPC;
}

int pw_ntsync_attach(void *region, size_t bytes, int format)
{
    Shared *candidate = region;
    int status;
    if (!region || bytes < (map_chunk ? offsetof(Shared, objects) : sizeof(Shared))) return -EINVAL;
    if (shared) return -EBUSY;
    if (format) {
        candidate->layout = LAYOUT;
        candidate->bytes = sizeof(Shared);
        candidate->base_stamp = stamp();
        candidate->base_ns = now_ns(CLOCK_MONOTONIC);
        __atomic_store_n(&candidate->magic, MAGIC, __ATOMIC_RELEASE);
    } else if (__atomic_load_n(&candidate->magic, __ATOMIC_ACQUIRE) != MAGIC || candidate->layout != LAYOUT ||
               candidate->bytes != sizeof(Shared)) {
        return -EINVAL;
    }
    shared = candidate;
    lock_take();
    status = claim_process();
    lock_give();
    if (status) shared = NULL;
    return status;
}

/* A program that never attached (a host test, or one process alone) gets a region of its own. */
static void attach_private(void)
{
    void *region = calloc(1, sizeof(Shared));
    if (region && pw_ntsync_attach(region, sizeof(Shared), 1)) free(region);
}

static ThreadSlot *enter(uint32_t call);
static void leave(ThreadSlot *me);

/* ---- objects -------------------------------------------------------------- */

static Object *lookup(int number)
{
    uint32_t slot, generation;
    Object *object;
    if (number <= 0 || number == PW_NTSYNC_DEVICE) return NULL;
    slot = ((uint32_t)number & SLOT_MASK) - 1;
    generation = (uint32_t)number >> SLOT_BITS;
    if (slot >= shared->object_count) return NULL;
    object = slot_object(slot);
    if (!object || !object->type || object->generation != generation) return NULL;
    return object;
}

static int number_of(const Object *object)
{
    return (int)(((object->generation & GENERATION_MASK) << SLOT_BITS) | (object->slot + 1));
}

static Object *allocate(int type)
{
    Object *object = object_at(shared->free_objects);
    if (object) {
        shared->free_objects = object->next_free;
    } else {
        if (shared->object_count == OBJECTS) {
            shared->exhausted++;
            return NULL;
        }
        if (!(object = slot_object(shared->object_count))) {
            shared->exhausted++;
            return NULL;
        }
        object->slot = shared->object_count++;
    }
    object->type = (uint32_t)type;
    object->references = 1;
    object->head = object->tail = object->queued = 0;
    memset(&object->u, 0, sizeof(object->u));
    object->tag[0] = 0;
    shared->live_objects++;
    return object;
}

static void unreference(Object *object)
{
    if (--object->references) return;
    record(OP_FREE, number_of(object), 0, 0, 0);
    object->type = 0;
    object->generation = (object->generation + 1) & GENERATION_MASK;
    object->next_free = shared->free_objects;
    shared->free_objects = object_ref(object);
    shared->live_objects--;
}

/* Can owner take this object now? */
static int signaled(const Object *object, uint32_t owner)
{
    switch (object->type) {
    case SEMAPHORE: return object->u.semaphore.count != 0;
    case MUTEX:
        return (!object->u.mutex.owner || object->u.mutex.owner == owner) &&
               object->u.mutex.count != UINT32_MAX;
    case EVENT: return object->u.event.signaled;
    }
    return 0;
}

/* Takes it; 1 when it was an abandoned mutex. */
static int take(Object *object, uint32_t owner)
{
    int abandoned = 0;
    switch (object->type) {
    case SEMAPHORE: object->u.semaphore.count--; break;
    case MUTEX:
        abandoned = (int)object->u.mutex.abandoned;
        object->u.mutex.abandoned = 0;
        object->u.mutex.owner = owner;
        object->u.mutex.count++;
        break;
    case EVENT: if (!object->u.event.manual) object->u.event.signaled = 0; break;
    }
    return abandoned;
}

/* ---- waits ---------------------------------------------------------------- */

/* Waiters served under the lock, woken once it is given back: a woken thread
 * then finds it free. More than fit are woken at once. */
typedef struct Wakes {
    uint32_t count;
    uint32_t waiters[16];
} Wakes;

static void wake_later(Wakes *wakes, Waiter *waiter)
{
    if (wakes && wakes->count < sizeof(wakes->waiters) / sizeof(wakes->waiters[0]))
        wakes->waiters[wakes->count++] = waiter_ref(waiter);
    else wake_word(&waiter->wake);
}

/* After the lock: a waiter served and gone since gets a wake it ignores. */
static void wake_now(const Wakes *wakes)
{
    for (uint32_t i = 0; i < wakes->count; i++) wake_word(&waiter_at(wakes->waiters[i])->wake);
}

/* Satisfies the wait now if it can: 1 when done (result and index set). */
static int try_satisfy(Waiter *waiter)
{
    int abandoned = 0;
    Entry *entry;
    if (waiter->all) {
        uint32_t ready = 0;
        for (entry = entry_at(waiter->first); entry && entry->index < waiter->count; entry = entry_at(entry->next_in_wait))
            ready += signaled(object_at(entry->object), waiter->owner) ? 1 : 0;
        if (waiter->count && ready == waiter->count) {
            for (entry = entry_at(waiter->first); entry && entry->index < waiter->count;
                 entry = entry_at(entry->next_in_wait))
                abandoned |= take(object_at(entry->object), waiter->owner);
            waiter->index = 0;
            waiter->result = abandoned ? -EOWNERDEAD : 0;
            return 1;
        }
    } else {
        for (entry = entry_at(waiter->first); entry && entry->index < waiter->count; entry = entry_at(entry->next_in_wait)) {
            Object *object = object_at(entry->object);
            if (!signaled(object, waiter->owner)) continue;
            abandoned = take(object, waiter->owner);
            waiter->index = entry->index;
            waiter->result = abandoned ? -EOWNERDEAD : 0;
            return 1;
        }
    }
    for (entry = entry_at(waiter->first); entry; entry = entry_at(entry->next_in_wait)) {
        Object *alert = object_at(entry->object);
        if (entry->index != waiter->count || !alert->u.event.signaled) continue;
        take(alert, waiter->owner);
        waiter->index = waiter->count;
        waiter->result = 0;
        return 1;
    }
    return 0;
}

static void dequeue(Waiter *waiter)
{
    if (!waiter->queued) return;
    for (Entry *entry = entry_at(waiter->first); entry; entry = entry_at(entry->next_in_wait)) {
        Object *object = object_at(entry->object);
        if (entry->prev) entry_at(entry->prev)->next = entry->next;
        else object->head = entry->next;
        if (entry->next) entry_at(entry->next)->prev = entry->prev;
        else object->tail = entry->prev;
        entry->prev = entry->next = 0;
        object->queued--;
    }
    waiter->queued = 0;
}

/* Something about object changed: serve its waiters, oldest first. */
static void serve(Object *object, Wakes *wakes)
{
    for (int again = 1; again;) {
        again = 0;
        for (Entry *entry = entry_at(object->head); entry; entry = entry_at(entry->next)) {
            Waiter *waiter = waiter_at(entry->waiter);
            if (!try_satisfy(waiter)) continue;
            waiter->done = 1;
            shared->count_wakes++;
            record(OP_WAKE, number_of(object), waiter->owner, waiter->index, waiter->result != 0);
            dequeue(waiter);
            __atomic_store_n(&waiter->wake, 1, __ATOMIC_RELEASE);
            wake_later(wakes, waiter);
            again = 1;       /* the queue changed under the walk: walk it again */
            break;
        }
    }
}

static Waiter *get_waiter(void)
{
    Waiter *waiter = waiter_at(shared->free_waiters);
    if (waiter) shared->free_waiters = waiter->next_free;
    else if (shared->waiter_count < WAITERS) waiter = &shared->waiters[shared->waiter_count++];
    else {
        shared->exhausted++;
        return NULL;
    }
    waiter->used = WAITER_USED;
    waiter->first = waiter->queued = waiter->done = 0;
    return waiter;
}

static Entry *get_entry(void)
{
    Entry *entry = entry_at(shared->free_entries);
    if (entry) shared->free_entries = entry->next_in_wait;
    else if (shared->entry_count < ENTRIES) entry = &shared->entries[shared->entry_count++];
    else {
        shared->exhausted++;
        return NULL;
    }
    memset(entry, 0, sizeof(*entry));
    return entry;
}

/* Gives back a waiter and its entries; with references, also those its queued entries held. */
static void put_waiter(Waiter *waiter, int references)
{
    for (uint32_t ref = waiter->first; ref;) {
        Entry *entry = entry_at(ref);
        ref = entry->next_in_wait;
        if (references) unreference(object_at(entry->object));
        entry->next_in_wait = shared->free_entries;
        shared->free_entries = entry_ref(entry);
    }
    waiter->first = 0;
    waiter->used = WAITER_FREE;
    waiter->next_free = shared->free_waiters;
    shared->free_waiters = waiter_ref(waiter);
}

static void unblock(Waiter *waiter)
{
    if (waiter->blocked_prev) waiter_at(waiter->blocked_prev)->blocked_next = waiter->blocked_next;
    else shared->blocked_first = waiter->blocked_next;
    if (waiter->blocked_next) waiter_at(waiter->blocked_next)->blocked_prev = waiter->blocked_prev;
    else shared->blocked_last = waiter->blocked_prev;
    waiter->blocked_prev = waiter->blocked_next = 0;
    shared->blocked--;
}

/* 0 once served (or woken for nothing), else ETIMEDOUT; a signal does not end it. */
static int sleep_on(uint32_t *wake, const PwNtsyncWait *wait)
{
    while (!__atomic_load_n(wake, __ATOMIC_ACQUIRE)) {
        const int error = sleep_word(wake, 0, wait->timeout == UINT64_MAX ? NULL : &wait->timeout, wait->realtime);
        if (error == ETIMEDOUT) return ETIMEDOUT;
        if (!error) return 0;
    }
    return 0;
}

/* The object a wait took, else the first it asked for, for the record. */
static int wait_number(const PwNtsyncWait *wait, const Waiter *waiter, int status)
{
    if ((status == 0 || status == -EOWNERDEAD) && wait->index <= wait->count)
        for (Entry *entry = entry_at(waiter->first); entry; entry = entry_at(entry->next_in_wait))
            if (entry->index == wait->index) return number_of(object_at(entry->object));
    return wait->count ? wait->objects[0] : wait->alert;
}

static int add_entry(Waiter *waiter, Entry **last, Object *object, uint32_t index)
{
    Entry *entry = get_entry();
    if (!entry) return -ENOMEM;
    entry->waiter = waiter_ref(waiter);
    entry->object = object_ref(object);
    entry->index = index;
    if (*last) (*last)->next_in_wait = entry_ref(entry);
    else waiter->first = entry_ref(entry);
    *last = entry;
    return 0;
}

int pw_ntsync_wait(PwNtsyncWait *wait)
{
    Waiter *waiter;
    Entry *last = NULL;
    int status = 0, slept = 0;
    uint32_t self, shape;
    ThreadSlot *me;

    if (!wait || wait->count > PW_NTSYNC_MAX_WAIT || (wait->count && !wait->objects)) return -EINVAL;
    shape = wait->count | (wait->all ? 0x80u : 0);
    me = enter(CALL_WAIT);
    self = me ? me->thread : thread_number();
    if (me && wait->owner) __atomic_store_n(&me->owner, wait->owner, __ATOMIC_RELAXED);
    shared->count_waits++;
    if (!(waiter = get_waiter())) {
        leave(me);
        return -ENOMEM;
    }
    waiter->all = (uint32_t)wait->all;
    waiter->owner = wait->owner;
    waiter->count = wait->count;
    waiter->process = me_process;
    for (uint32_t i = 0; i < wait->count && !status; i++) {
        Object *object = lookup(wait->objects[i]);
        if (!object || (object->type == MUTEX && !wait->owner)) { status = -EINVAL; break; }
        for (Entry *entry = entry_at(waiter->first); wait->all && entry; entry = entry_at(entry->next_in_wait))
            if (object_at(entry->object) == object) status = -EINVAL;
        if (!status) status = add_entry(waiter, &last, object, i);
    }
    if (!status && wait->alert) {
        Object *alert = lookup(wait->alert);
        status = alert && alert->type == EVENT ? add_entry(waiter, &last, alert, wait->count) : -EINVAL;
    }
    if (status) goto out;

    if (try_satisfy(waiter)) {
        status = waiter->result;
        wait->index = waiter->index;
        goto out;
    }
    if (wait->timeout != UINT64_MAX &&
        now_ns(wait->realtime ? CLOCK_REALTIME : CLOCK_MONOTONIC) >= wait->timeout) {
        status = -ETIMEDOUT;
        goto out;
    }
    if (me && __atomic_load_n(&me->ending, __ATOMIC_SEQ_CST)) {
        status = -ECANCELED;    /* a thread being ended never blocks again */
        goto out;
    }

    /* Queue on every object (and the alert) and sleep until served, without
     * the lock: a signal's handler then runs at once, and a suspended waiter
     * holds nothing. The objects stay alive while the wait uses them. */
    for (Entry *entry = entry_at(waiter->first); entry; entry = entry_at(entry->next_in_wait)) {
        Object *object = object_at(entry->object);
        object->references++;
        entry->prev = object->tail;
        entry->next = 0;
        if (object->tail) entry_at(object->tail)->next = entry_ref(entry);
        else object->head = entry_ref(entry);
        object->tail = entry_ref(entry);
        object->queued++;
        record(OP_BLOCK, number_of(object), entry->index, wait->owner, shape);
    }
    waiter->queued = 1;
    __atomic_store_n(&waiter->wake, 0, __ATOMIC_RELAXED);
    slept = 1;
    shared->blocked++;
    shared->count_blocks++;
    waiter->thread = self;
    waiter->since = stamp();
    waiter->timeout = wait->timeout;
    waiter->realtime = wait->realtime;
    waiter->blocked_next = 0;
    waiter->blocked_prev = shared->blocked_last;
    if (shared->blocked_last) waiter_at(shared->blocked_last)->blocked_next = waiter_ref(waiter);
    else shared->blocked_first = waiter_ref(waiter);
    shared->blocked_last = waiter_ref(waiter);
    if (me) __atomic_store_n(&me->asleep_on, &waiter->wake, __ATOMIC_SEQ_CST);
    for (;;) {
        int error;
        leave(me);
        error = sleep_on(&waiter->wake, wait);
        me = enter(CALL_WAIT);
        if (waiter->done) break;    /* served, even as the wait timed out */
        if (error) {
            dequeue(waiter);
            status = -ETIMEDOUT;
            shared->count_timeouts++;
            break;
        }
        if (me && __atomic_load_n(&me->ending, __ATOMIC_SEQ_CST)) {
            dequeue(waiter);        /* nothing is given to a thread Wine is ending */
            status = -ECANCELED;
            break;
        }
        /* woken and not served: not this wait's wake; sleep again */
    }
    if (me) __atomic_store_n(&me->asleep_on, NULL, __ATOMIC_SEQ_CST);
    unblock(waiter);
    if (waiter->done) {
        status = waiter->result;
        wait->index = waiter->index;
    }
    record(OP_RETURN, wait_number(wait, waiter, status), (uint32_t)status, wait->index, shape);
out:
    if (!slept) record(OP_WAIT, wait_number(wait, waiter, status), (uint32_t)status, wait->index, shape);
    put_waiter(waiter, slept);
    leave(me);
    return status;
}

/* ---- processes ------------------------------------------------------------ */

/* The lock, from process pid that has ended holding it after it was told gone: then its holder was a thread on its
 * way in, which saw its process gone and was killed before it gave the lock back (the title tells a helper gone,
 * then kills it). Such a thread changed nothing, so the lock is taken over as it is. A process that held the lock
 * when it was to be told gone was never marked gone (that call returned -EBUSY), and its lock is not taken. */
static int lock_take_back(int pid)
{
    for (uint32_t i = 0; i < PROCESSES; i++) {
        const Process *process = &shared->processes[i];
        uint32_t seen = __atomic_load_n(&shared->lock, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&process->state, __ATOMIC_ACQUIRE) != PROCESS_GONE ||
            __atomic_load_n(&process->pid, __ATOMIC_RELAXED) != pid)
            continue;
        if (!(seen & LOCK_HELD) || seen >> LOCK_SLOT_SHIFT != i + 1) return 0;
        if (!__atomic_compare_exchange_n(&shared->lock, &seen, lock_mine() | (seen & LOCK_SLEEPERS), 0,
                                         __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        shared->locks_taken_back++;
        return 1;
    }
    return 0;
}

int pw_ntsync_process_gone(int pid, int ended, uint32_t wait_ms)
{
    Process *process = NULL;
    int dropped = 0;
    if (pid <= 0) return -EINVAL;
    if (!shared) return -ESRCH;
    /* Not enter(): the title's hang breaker calls this, and a helper stuck holding the lock must not stop it. */
    for (uint32_t waited = 0; !lock_try(); waited++) {
        if (ended && lock_take_back(pid)) break;
        if (waited >= wait_ms) return -EBUSY;
        usleep(1000);
    }
    __atomic_store_n(&shared->holder_pid, (int32_t)getpid(), __ATOMIC_RELAXED);
    __atomic_store_n(&shared->holder_state, holding(thread_number(), PHASE_INSIDE, CALL_PROCESS), __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < PROCESSES && !process; i++)
        if (shared->processes[i].state != PROCESS_FREE && shared->processes[i].pid == pid && i + 1 != me_process)
            process = &shared->processes[i];
    if (!process) {
        leave(NULL);
        return -ESRCH;
    }
    if (process->state == PROCESS_USING) shared->processes_using--;
    process->state = PROCESS_GONE;
    /* Its blocked waits leave the queues now, so nothing is given to a wait no one will return from; its thread, if
     * it ever wakes, finds its process gone on the way in and stays there. Outside the lock a used waiter is always
     * a blocked one (served or not), holding a reference on each object.
     * Their places are kept until it has ended. Before that its threads may still sleep on their words, and a wait
     * that took one of those places would sleep on the same word behind the dead thread: the kernel wakes the older
     * sleeper first, which stays in the device, so the live wait was served and never woke. That held a session open
     * (stress-gmdx-2026-10-18-11): the game's one blocked wait was dropped at its exit message, and the restored
     * explorer's main thread, blocked 19 ms later on that place, waited forever with its event taken. */
    for (uint32_t i = 0; i < shared->waiter_count; i++) {
        Waiter *waiter = &shared->waiters[i];
        if (!waiter->used || waiter->process != (uint32_t)(process - shared->processes) + 1) continue;
        if (waiter->used == WAITER_USED) {
            dequeue(waiter);
            unblock(waiter);
            dropped++;
        }
        if (ended) put_waiter(waiter, 1);
        else waiter->used = WAITER_KEPT;
    }
    shared->dropped += (uint32_t)dropped;
    if (ended) process->state = PROCESS_FREE;
    leave(NULL);
    return dropped;
}

/* ---- the lock, entered as a kernel driver is ------------------------------ */

/* Takes the lock, inside from before the thread asks for it: a signal that
 * lands while it waits for the lock is deferred too, which delays its handler
 * only as long as the lock's holder, who never waits for anything, keeps it.
 * A thread of a process the device was told is gone stays here: what it would
 * do no longer has a place in the region. */
static ThreadSlot *enter(uint32_t call)
{
    const uint32_t self = thread_number();
    ThreadSlot *me = thread_slot(self, 1);
    if (!shared) pthread_once(&private_once, attach_private);
    if (me) slot_hook_exit(me);
    if (me) {
        if (__atomic_load_n(&me->inside, __ATOMIC_SEQ_CST)) __atomic_fetch_add(&shared->reentries, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&me->inside, 1, __ATOMIC_SEQ_CST);
    } else {
        __atomic_fetch_add(&without_slot, 1, __ATOMIC_RELAXED);
    }
    if (pw_ntsync_test_before_lock) pw_ntsync_test_before_lock();
    lock_take();
    if (me_process && shared->processes[me_process - 1].state == PROCESS_GONE) {
        if (pw_ntsync_test_gone_holding) pw_ntsync_test_gone_holding();
        lock_give();
        for (;;) pause();
    }
    __atomic_store_n(&shared->holder_pid, me_pid, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->holder_state, holding(self, PHASE_INSIDE, call), __ATOMIC_RELAXED);
    __atomic_store_n(&shared->held_since, stamp(), __ATOMIC_RELAXED);
    return me;
}

/* Raises again the signals that landed while the thread was inside, now that
 * it holds nothing: their handlers run as they would have, a moment late. */
static void raise_deferred(ThreadSlot *me)
{
    const uint32_t deferrals = __atomic_exchange_n(&me->deferrals, 0, __ATOMIC_SEQ_CST);
    const uint32_t signals = __atomic_exchange_n(&me->signals, 0, __ATOMIC_SEQ_CST);
    if (!deferrals) return;
    for (int signal = 1; signal < 32; signal++) {
        if (!(signals & (1u << signal))) continue;
        __atomic_fetch_add(&signals_raised, 1, __ATOMIC_RELAXED);
        pthread_kill(pthread_self(), signal);
    }
}

/* Unlocks, then clears the holder unless another thread has taken the lock
 * since (from the store to the clear the holder reads "unlocking"), wakes the
 * waiters served inside, then leaves: from here a signal's handler runs at
 * once. */
static void leave_waking(ThreadSlot *me, const Wakes *wakes)
{
    uint64_t state = __atomic_load_n(&shared->holder_state, __ATOMIC_RELAXED);
    state = holding(state_thread(state), PHASE_LEAVING, state_call(state));
    __atomic_store_n(&shared->holder_state, state, __ATOMIC_RELAXED);
    lock_give();
    __atomic_compare_exchange_n(&shared->holder_state, &state, 0, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    if (wakes) wake_now(wakes);
    if (!me) return;
    __atomic_store_n(&me->inside, 0, __ATOMIC_SEQ_CST);
    if (!__atomic_load_n(&me->critical, __ATOMIC_SEQ_CST) && __atomic_load_n(&me->deferrals, __ATOMIC_SEQ_CST))
        raise_deferred(me);
}

/* Another lock of this process's Unix side that a suspend must not land inside (Wine's heap, wine/ps5/pw_wine_heap.c):
 * the same rule as the device's. A minidump suspends every thread of a process (dbghelp's SuspendThread); a thread
 * suspended holding the heap's one mutex stopped every other thread's next malloc, the dumping thread's among them
 * (suspend-server64-helper and -title, a8-rel-base-2026-11-02: the main thread stuck after create_event, the device's
 * lock and the virtual mutex free). Nested calls count; the last leave raises what landed meanwhile. A thread the
 * table has no slot for is not guarded. Only atomics and the slot table: never malloc, the heap calls this. */
void pw_ntsync_critical_enter(void)
{
    ThreadSlot *me = thread_slot(thread_number(), 1);
    if (me) __atomic_fetch_add(&me->critical, 1, __ATOMIC_SEQ_CST);
}

void pw_ntsync_critical_leave(void)
{
    ThreadSlot *me = thread_slot(thread_number(), 0);
    if (!me || !__atomic_load_n(&me->critical, __ATOMIC_SEQ_CST)) return;
    if (__atomic_sub_fetch(&me->critical, 1, __ATOMIC_SEQ_CST) == 0 && !__atomic_load_n(&me->inside, __ATOMIC_SEQ_CST) &&
        __atomic_load_n(&me->deferrals, __ATOMIC_SEQ_CST))
        raise_deferred(me);
}

static void leave(ThreadSlot *me)
{
    leave_waking(me, NULL);
}

int pw_ntsync_signal_deferred(int signal)
{
    const uint32_t self = thread_number();
    ThreadSlot *me = thread_slot(self, 0);
    uint64_t state;
    if (!me || signal <= 0 || signal >= 32) return 0;
    if (!__atomic_load_n(&me->inside, __ATOMIC_SEQ_CST) && !__atomic_load_n(&me->critical, __ATOMIC_SEQ_CST)) {
        /* Wine ends a thread with SIGQUIT. On the console its handler only marks the thread, which ends at the
         * user-mode boundary (Wine 0885), and SA_RESTART put a blocked wait straight back to sleep: the thread kept
         * its place in the queues, took the next set meant for a live waiter, and on a wait without a timeout never
         * ended. Now its word wakes it here, and its waits end (pw_ntsync_wait), as the kernel's device drops a
         * wait a signal interrupts. */
        if (signal == SIGQUIT) {
            uint32_t *word = __atomic_load_n(&me->asleep_on, __ATOMIC_SEQ_CST);
            __atomic_store_n(&me->ending, 1, __ATOMIC_SEQ_CST);
            if (word) __atomic_store_n(word, WAKE_ENDING, __ATOMIC_RELEASE);
        }
        return 0;
    }
    state = shared ? __atomic_load_n(&shared->holder_state, __ATOMIC_RELAXED) : 0;
    __atomic_store_n(&signal_last, (uint64_t)self | (uint64_t)(state_thread(state) == self ? state_call(state) : 0) << 40 |
                     (uint64_t)signal << 48, __ATOMIC_RELAXED);
    __atomic_fetch_or(&me->signals, 1u << signal, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&me->deferrals, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&signals_deferred, 1, __ATOMIC_RELAXED);
    return 1;
}

static void thread_name(char *out, size_t size, uint32_t thread)
{
    const ThreadSlot *slot = thread_slot(thread, 0);
    const uint32_t owner = slot ? __atomic_load_n(&slot->owner, __ATOMIC_RELAXED) : 0;
    if (owner) snprintf(out, size, "%04x", owner);
    else snprintf(out, size, thread ? "u%x" : "-", thread);
}

/* ---- the calls ------------------------------------------------------------ */

static int create(int type, uint32_t a, uint32_t b)
{
    Object *object;
    int number;
    ThreadSlot *me = enter(CALL_CREATE);
    if (!(object = allocate(type))) {
        leave(me);
        return -ENOMEM;
    }
    switch (type) {
    case SEMAPHORE: object->u.semaphore.count = a; object->u.semaphore.max = b; break;
    case MUTEX: object->u.mutex.owner = a; object->u.mutex.count = b; break;
    case EVENT: object->u.event.manual = !!a; object->u.event.signaled = !!b; break;
    }
    number = number_of(object);
    record(OP_CREATE, number, a, b, (uint32_t)type);
    leave(me);
    return number;
}

int pw_ntsync_create_semaphore(uint32_t count, uint32_t max)
{
    if (count > max) return -EINVAL;
    return create(SEMAPHORE, count, max);
}

int pw_ntsync_create_mutex(uint32_t owner, uint32_t count)
{
    if (!owner != !count) return -EINVAL;
    return create(MUTEX, owner, count);
}

int pw_ntsync_create_event(int manual, int signaled)
{
    return create(EVENT, (uint32_t)manual, (uint32_t)signaled);
}

/* Runs body on the object of the right type under the lock; waiters it serves wake once the lock is given back. */
#define WITH_OBJECT(number, kind, call, body) \
    do { \
        Object *object; \
        int status = 0; \
        Wakes wakes = {0}; \
        ThreadSlot *me = enter(call); \
        if (!(object = lookup(number)) || ((kind) != 0 && object->type != (uint32_t)(kind))) status = -EINVAL; \
        else { body } \
        leave_waking(me, &wakes); \
        return status; \
    } while (0)

int pw_ntsync_ref(int number)
{
    WITH_OBJECT(number, 0, CALL_REF, { object->references++; status = number; });
}

int pw_ntsync_release(int number)
{
    WITH_OBJECT(number, 0, CALL_RELEASE, { unreference(object); });
}

int pw_ntsync_semaphore_release(int number, uint32_t count, uint32_t *previous)
{
    WITH_OBJECT(number, SEMAPHORE, CALL_SEM_RELEASE, {
        const uint32_t before = object->u.semaphore.count;
        if (count > object->u.semaphore.max - before) status = -EOVERFLOW;
        else {
            object->u.semaphore.count = before + count;
            if (previous) *previous = before;
            shared->count_signals++;
            record(OP_SEM_RELEASE, number, count, before, 0);
            serve(object, &wakes);
        }
    });
}

int pw_ntsync_semaphore_read(int number, uint32_t *count, uint32_t *max)
{
    WITH_OBJECT(number, SEMAPHORE, CALL_SEM_READ, {
        if (count) *count = object->u.semaphore.count;
        if (max) *max = object->u.semaphore.max;
    });
}

int pw_ntsync_mutex_unlock(int number, uint32_t owner, uint32_t *previous)
{
    if (!owner) return -EINVAL;
    WITH_OBJECT(number, MUTEX, CALL_MUTEX_UNLOCK, {
        if (object->u.mutex.owner != owner || !object->u.mutex.count) status = -EPERM;
        else {
            if (previous) *previous = object->u.mutex.count;
            record(OP_UNLOCK, number, owner, object->u.mutex.count, 0);
            if (!--object->u.mutex.count) {
                shared->count_signals++;
                object->u.mutex.owner = 0;
                serve(object, &wakes);
            }
        }
    });
}

int pw_ntsync_mutex_kill(int number, uint32_t owner)
{
    if (!owner) return -EINVAL;
    WITH_OBJECT(number, MUTEX, CALL_MUTEX_KILL, {
        if (object->u.mutex.owner != owner) status = -EPERM;
        else {
            object->u.mutex.owner = 0;
            object->u.mutex.count = 0;
            object->u.mutex.abandoned = 1;
            shared->count_signals++;
            record(OP_KILL, number, owner, 0, 0);
            serve(object, &wakes);
        }
    });
}

int pw_ntsync_mutex_read(int number, uint32_t *owner, uint32_t *count)
{
    WITH_OBJECT(number, MUTEX, CALL_MUTEX_READ, {
        if (owner) *owner = object->u.mutex.owner;
        if (count) *count = object->u.mutex.count;
        if (object->u.mutex.abandoned) status = -EOWNERDEAD;
    });
}

int pw_ntsync_event_set(int number, uint32_t *previous)
{
    WITH_OBJECT(number, EVENT, CALL_EVENT_SET, {
        if (previous) *previous = object->u.event.signaled;
        shared->count_signals++;
        record(OP_SET, number, object->u.event.signaled, 0, 0);
        object->u.event.signaled = 1;
        serve(object, &wakes);
    });
}

int pw_ntsync_event_reset(int number, uint32_t *previous)
{
    WITH_OBJECT(number, EVENT, CALL_EVENT_RESET, {
        if (previous) *previous = object->u.event.signaled;
        record(OP_RESET, number, object->u.event.signaled, 0, 0);
        object->u.event.signaled = 0;
    });
}

int pw_ntsync_event_pulse(int number, uint32_t *previous)
{
    WITH_OBJECT(number, EVENT, CALL_EVENT_PULSE, {
        if (previous) *previous = object->u.event.signaled;
        shared->count_signals++;
        record(OP_PULSE, number, object->u.event.signaled, 0, 0);
        object->u.event.signaled = 1;
        serve(object, &wakes);
        object->u.event.signaled = 0;
    });
}

int pw_ntsync_event_read(int number, uint32_t *manual, uint32_t *signaled_out)
{
    WITH_OBJECT(number, EVENT, CALL_EVENT_READ, {
        if (manual) *manual = object->u.event.manual;
        if (signaled_out) *signaled_out = object->u.event.signaled;
    });
}

void pw_ntsync_census(uint32_t *objects, uint32_t *waiters)
{
    ThreadSlot *me = enter(CALL_CENSUS);
    if (objects) *objects = shared->live_objects;
    if (waiters) *waiters = shared->blocked;
    leave(me);
}

/* ---- diagnostics ---------------------------------------------------------- */

void pw_ntsync_counters(PwNtsyncCounters *counters)
{
    if (!counters) return;
    memset(counters, 0, sizeof(*counters));
    counters->deferred = __atomic_load_n(&signals_deferred, __ATOMIC_RELAXED);
    counters->raised = __atomic_load_n(&signals_raised, __ATOMIC_RELAXED);
    if (!shared) return;
    counters->objects = __atomic_load_n(&shared->live_objects, __ATOMIC_RELAXED);
    counters->blocked = __atomic_load_n(&shared->blocked, __ATOMIC_RELAXED);
    counters->waits = __atomic_load_n(&shared->count_waits, __ATOMIC_RELAXED);
    counters->slept = __atomic_load_n(&shared->count_blocks, __ATOMIC_RELAXED);
    counters->timeouts = __atomic_load_n(&shared->count_timeouts, __ATOMIC_RELAXED);
    counters->wakes = __atomic_load_n(&shared->count_wakes, __ATOMIC_RELAXED);
    counters->signals = __atomic_load_n(&shared->count_signals, __ATOMIC_RELAXED);
    counters->processes = __atomic_load_n(&shared->processes_using, __ATOMIC_RELAXED);
    counters->exhausted = __atomic_load_n(&shared->exhausted, __ATOMIC_RELAXED);
    counters->dropped = __atomic_load_n(&shared->dropped, __ATOMIC_RELAXED);
}

int pw_ntsync_note(int number, const char *tag)
{
    if (!tag) return -EINVAL;
    WITH_OBJECT(number, 0, CALL_NOTE, {
        if (!object->tag[0]) snprintf(object->tag, sizeof(object->tag), "%s", tag);
    });
}

int pw_ntsync_trace(uint32_t capacity)
{
    Record *fresh = NULL, *old;
    uint32_t size = 0;
    if (capacity) {
        if (capacity > 1u << 20) capacity = 1u << 20;
        for (size = 1; size < capacity; size <<= 1) {}
        if (!(fresh = calloc(size, sizeof(*fresh)))) return -ENOMEM;
    }
    ThreadSlot *me = enter(CALL_TRACE);
    old = ring;
    ring = fresh;
    ring_mask = size ? size - 1 : 0;
    ring_count = 0;
    leave(me);
    free(old);
    return 0;
}

/* The dump's lines, gathered under the lock and printed after it. */
typedef struct Text {
    char *data;
    size_t used, size;
    uint64_t now;               /* a time stamp */
    double ns_per_tick;
} Text;

__attribute__((format(printf, 2, 3))) static void add(Text *text, const char *format, ...)
{
    char line[512];
    va_list arguments;
    int length;
    va_start(arguments, format);
    length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length < 0) return;
    if ((size_t)length >= sizeof(line)) length = sizeof(line) - 1;
    if (text->used + (size_t)length + 1 > text->size) {
        size_t size = text->size ? text->size * 2 : 16384;
        char *grown;
        while (size < text->used + (size_t)length + 1) size *= 2;
        if (!(grown = realloc(text->data, size))) return;
        text->data = grown;
        text->size = size;
    }
    memcpy(text->data + text->used, line, (size_t)length + 1);
    text->used += (size_t)length + 1;
}

/* "12.345 ms" before the dump */
static void age(const Text *text, char *out, size_t size, uint64_t then)
{
    const uint64_t ns = text->now > then ? (uint64_t)((double)(text->now - then) * text->ns_per_tick) : 0;
    snprintf(out, size, "%llu.%03llu ms", (unsigned long long)(ns / 1000000u),
             (unsigned long long)(ns / 1000u % 1000u));
}

static void describe_object(char *out, size_t size, const Object *object)
{
    char owner[16];
    int used = 0;
    switch (object->type) {
    case SEMAPHORE:
        used = snprintf(out, size, "semaphore %u/%u", object->u.semaphore.count, object->u.semaphore.max);
        break;
    case MUTEX:
        snprintf(owner, sizeof(owner), "%04x", object->u.mutex.owner);
        used = snprintf(out, size, "mutex %s%s count %u%s", object->u.mutex.owner ? "owner " : "free",
                        object->u.mutex.owner ? owner : "", object->u.mutex.count,
                        object->u.mutex.abandoned ? " abandoned" : "");
        break;
    case EVENT:
        used = snprintf(out, size, "event %s %s", object->u.event.manual ? "manual" : "auto",
                        object->u.event.signaled ? "set" : "clear");
        break;
    default:
        used = snprintf(out, size, "gone (type %u)", object->type);
    }
    if (used > 0 && (size_t)used < size)
        snprintf(out + used, size - (size_t)used, "%s%s%s queue %u", object->tag[0] ? " \"" : "", object->tag,
                 object->tag[0] ? "\"" : "", object->queued);
}

static const char *outcome(char *out, size_t size, uint32_t status, uint32_t index, uint32_t shape)
{
    const int error = (int)status;
    if (!error && index == (shape & 0x7f)) snprintf(out, size, "alerted");
    else if (!error) snprintf(out, size, "took [%u]", index);
    else if (error == -EOWNERDEAD) snprintf(out, size, "abandoned [%u]", index);
    else if (error == -ETIMEDOUT) snprintf(out, size, "timed out");
    else snprintf(out, size, "error %d", error);
    return out;
}

static void describe_record(const Text *text, char *out, size_t size, const Record *entry)
{
    char when[32], who[16], owner[16], result[32];
    const char *kind = entry->c & 0x80 ? "all" : "any";
    const uint32_t count = entry->c & 0x7f;
    age(text, when, sizeof(when), entry->stamp);
    thread_name(who, sizeof(who), entry->thread);
    switch (entry->op) {
    case OP_CREATE:
        if (entry->c == SEMAPHORE)
            snprintf(out, size, "-%s %s create #%x semaphore %u/%u", when, who, entry->object, entry->a, entry->b);
        else if (entry->c == MUTEX)
            snprintf(out, size, "-%s %s create #%x mutex owner %04x count %u", when, who, entry->object, entry->a,
                     entry->b);
        else
            snprintf(out, size, "-%s %s create #%x event %s %s", when, who, entry->object,
                     entry->a ? "manual" : "auto", entry->b ? "set" : "clear");
        break;
    case OP_FREE: snprintf(out, size, "-%s %s free #%x", when, who, entry->object); break;
    case OP_WAIT:
        snprintf(out, size, "-%s %s wait-%s of %u on #%x: %s", when, who, kind, count, entry->object,
                 outcome(result, sizeof(result), entry->a, entry->b, entry->c));
        break;
    case OP_BLOCK:
        snprintf(owner, sizeof(owner), "%u", entry->a);
        snprintf(out, size, "-%s %s block on #%x [%s] of wait-%s of %u", when, who, entry->object,
                 entry->a == count ? "alert" : owner, kind, count);
        break;
    case OP_WAKE:
        snprintf(out, size, "-%s %s wake %04x [%u]%s through #%x", when, who, entry->a, entry->b,
                 entry->c ? " abandoned" : "", entry->object);
        break;
    case OP_RETURN:
        snprintf(out, size, "-%s %s return from wait-%s of %u, #%x: %s", when, who, kind, count, entry->object,
                 outcome(result, sizeof(result), entry->a, entry->b, entry->c));
        break;
    case OP_SET: case OP_RESET: case OP_PULSE:
        snprintf(out, size, "-%s %s %s #%x (was %s)", when, who,
                 entry->op == OP_SET ? "set" : entry->op == OP_RESET ? "reset" : "pulse", entry->object,
                 entry->a ? "set" : "clear");
        break;
    case OP_SEM_RELEASE:
        snprintf(out, size, "-%s %s release #%x +%u (was %u)", when, who, entry->object, entry->a, entry->b);
        break;
    case OP_UNLOCK:
        snprintf(out, size, "-%s %s unlock #%x by %04x (count was %u)", when, who, entry->object, entry->a,
                 entry->b);
        break;
    case OP_KILL: snprintf(out, size, "-%s %s kill #%x owner %04x", when, who, entry->object, entry->a); break;
    case OP_REENTER:
        thread_name(owner, sizeof(owner), entry->b);
        snprintf(out, size, "-%s %s lock entered again by %s while inside it (%s, error %u)", when, who, owner,
                 entry->c ? "the lock refused" : "seen before locking", entry->a);
        break;
    default: snprintf(out, size, "-%s %s op %u #%x", when, who, entry->op, entry->object);
    }
}

void pw_ntsync_dump(void (*line)(void *context, const char *text), void *context, uint32_t recent,
                    uint32_t wait_ms)
{
    Text text = {0};
    char buffer[384], who[16], when[32];
    uint32_t waited, locked = 0, kinds[4] = {0}, seen_count = 0, kept = 0;
    const Object *seen[64];
    uint64_t held_by, held_at, now_time;
    int32_t held_pid;
    const uint64_t last_signal = __atomic_load_n(&signal_last, __ATOMIC_RELAXED);

    if (!line) return;
    if (!shared) {
        line(context, "device: off in this process");
        return;
    }
    held_by = __atomic_load_n(&shared->holder_state, __ATOMIC_RELAXED);
    held_at = __atomic_load_n(&shared->held_since, __ATOMIC_RELAXED);
    held_pid = __atomic_load_n(&shared->holder_pid, __ATOMIC_RELAXED);
    for (waited = 0;; waited++) {
        if (lock_try()) {
            locked = 1;
            break;
        }
        if (waited >= wait_ms) break;
        usleep(1000);
    }
    text.now = stamp();
    now_time = now_ns(CLOCK_MONOTONIC);
    text.ns_per_tick = shared->base_stamp && text.now > shared->base_stamp
        ? (double)(now_time - shared->base_ns) / (double)(text.now - shared->base_stamp) : 1.0;
    if (locked) {
        add(&text, "device lock: free");
    } else {
        thread_name(who, sizeof(who), state_thread(held_by));
        age(&text, when, sizeof(when), held_at);
        add(&text, "device lock: held by %s of process %d (%s, in %s) for %s, not released in %u ms: the state below "
            "is read without it", who, held_pid, phase_names[state_phase(held_by) < 3 ? state_phase(held_by) : 0],
            call_names[state_call(held_by) < CALL_COUNT ? state_call(held_by) : 0], when, wait_ms);
    }
    thread_name(who, sizeof(who), state_thread(last_signal));
    add(&text, "device signals: %u deferred (landed on a thread inside), %u raised again after; last: signal %u "
        "to %s (in %s); %u entries without a thread slot", __atomic_load_n(&signals_deferred, __ATOMIC_RELAXED),
        __atomic_load_n(&signals_raised, __ATOMIC_RELAXED), (unsigned)(last_signal >> 48), last_signal ? who : "-",
        call_names[state_call(last_signal) < CALL_COUNT ? state_call(last_signal) : 0],
        __atomic_load_n(&without_slot, __ATOMIC_RELAXED));
    for (uint32_t i = 0; i < shared->object_count; i++) {
        const Object *object = slot_object(i);
        if (object && object->type > 0 && object->type < 4) kinds[object->type]++;
    }
    add(&text, "device: %u objects (%u events, %u semaphores, %u mutexes), %u blocked; %llu waits, %llu blocked, "
        "%llu timed out, %llu wakes, %llu signals; %u entered again, %u unlock errors; trace %llu operations, keeps %u",
        shared->live_objects, kinds[EVENT], kinds[SEMAPHORE], kinds[MUTEX], shared->blocked,
        (unsigned long long)shared->count_waits, (unsigned long long)shared->count_blocks,
        (unsigned long long)shared->count_timeouts, (unsigned long long)shared->count_wakes,
        (unsigned long long)shared->count_signals, __atomic_load_n(&shared->reentries, __ATOMIC_RELAXED),
        __atomic_load_n(&shared->unlock_errors, __ATOMIC_RELAXED), (unsigned long long)ring_count,
        ring ? ring_mask + 1 : 0);
    for (uint32_t i = 0; i < shared->waiter_count; i++) kept += shared->waiters[i].used == WAITER_KEPT;
    add(&text, "device processes: %u using it, %u waits dropped with theirs (%u of them kept until theirs end), the "
        "lock taken back from %u that ended holding it; %u objects of %u used at most, %u waits of %u, %u entries of "
        "%u; %u refused for room", shared->processes_using, shared->dropped, kept, shared->locks_taken_back,
        shared->object_count, (unsigned)OBJECTS, shared->waiter_count, (unsigned)WAITERS, shared->entry_count,
        (unsigned)ENTRIES, shared->exhausted);

    for (const Waiter *waiter = waiter_at(shared->blocked_first); waiter; waiter = waiter_at(waiter->blocked_next)) {
        char until[48];
        thread_name(who, sizeof(who), waiter->thread);
        age(&text, when, sizeof(when), waiter->since);
        if (waiter->timeout == UINT64_MAX) snprintf(until, sizeof(until), "forever");
        else {
            const uint64_t clock_now = now_ns(waiter->realtime ? CLOCK_REALTIME : CLOCK_MONOTONIC);
            const int64_t left = (int64_t)(waiter->timeout - clock_now);
            snprintf(until, sizeof(until), "%s timeout in %lld ms", waiter->realtime ? "realtime" : "monotonic",
                     (long long)(left / 1000000));
        }
        add(&text, "wait: %04x (%s) blocked %s in wait-%s of %u, %s", waiter->owner, who, when,
            waiter->all ? "all" : "any", waiter->count, until);
        for (const Entry *entry = entry_at(waiter->first); entry; entry = entry_at(entry->next_in_wait)) {
            const Object *object = object_at(entry->object);
            uint32_t known = 0;
            describe_object(buffer, sizeof(buffer), object);
            if (entry->index == waiter->count) add(&text, "wait:   [alert] #%x %s", number_of(object), buffer);
            else add(&text, "wait:   [%u] #%x %s", entry->index, number_of(object), buffer);
            for (uint32_t j = 0; j < seen_count; j++) known |= seen[j] == object;
            if (!known && seen_count < 64) seen[seen_count++] = object;
        }
    }

    if (ring && ring_count) {
        const uint64_t kept = ring_count < (uint64_t)ring_mask + 1 ? ring_count : (uint64_t)ring_mask + 1;
        for (uint32_t i = 0; i < seen_count; i++) {
            const int number = number_of(seen[i]);
            const Record *found[8];
            uint32_t found_count = 0;
            for (uint64_t back = 1; back <= kept && found_count < 8; back++) {
                const Record *entry = &ring[(ring_count - back) & ring_mask];
                if (entry->object == number) found[found_count++] = entry;
            }
            add(&text, "history #%x%s%s%s, last %u:", number, seen[i]->tag[0] ? " \"" : "", seen[i]->tag,
                seen[i]->tag[0] ? "\"" : "", found_count);
            while (found_count) {
                describe_record(&text, buffer, sizeof(buffer), found[--found_count]);
                add(&text, "history   %s", buffer);
            }
        }
        if (recent > kept) recent = (uint32_t)kept;
        add(&text, "recent: the last %u operations", recent);
        for (uint64_t back = recent; back; back--) {
            describe_record(&text, buffer, sizeof(buffer), &ring[(ring_count - back) & ring_mask]);
            add(&text, "recent   %s", buffer);
        }
    }
    if (locked) lock_give();

    for (size_t at = 0; at < text.used; at += strlen(text.data + at) + 1) line(context, text.data + at);
    free(text.data);
}
