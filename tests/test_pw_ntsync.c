/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_ntsync.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static uint64_t monotonic(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static int wait_for(const int *objects, uint32_t count, int all, uint32_t owner, int alert,
                    int64_t timeout_ns, uint32_t *index)
{
    PwNtsyncWait wait = {.timeout = timeout_ns < 0 ? UINT64_MAX : monotonic() + (uint64_t)timeout_ns,
                         .all = all, .objects = objects, .count = count, .owner = owner, .alert = alert,
                         .index = ~0u};
    const int status = pw_ntsync_wait(&wait);
    if (index) *index = wait.index;
    return status;
}

static void semantics(void)
{
    uint32_t a, b, index;
    /* semaphores */
    assert(pw_ntsync_create_semaphore(3, 2) == -EINVAL);
    int sem = pw_ntsync_create_semaphore(2, 3);
    assert(sem > 0);
    assert(!wait_for(&sem, 1, 0, 1, 0, 0, &index) && index == 0);
    assert(!wait_for(&sem, 1, 0, 1, 0, 0, NULL));
    assert(wait_for(&sem, 1, 0, 1, 0, 0, NULL) == -ETIMEDOUT);
    assert(!pw_ntsync_semaphore_release(sem, 2, &a) && a == 0);
    assert(pw_ntsync_semaphore_release(sem, 2, &a) == -EOVERFLOW);
    assert(!pw_ntsync_semaphore_read(sem, &a, &b) && a == 2 && b == 3);
    assert(pw_ntsync_semaphore_release(sem, UINT32_MAX, NULL) == -EOVERFLOW);
    assert(pw_ntsync_event_set(sem, NULL) == -EINVAL);          /* wrong type */

    /* mutexes: recursion, ownership, abandonment */
    assert(pw_ntsync_create_mutex(5, 0) == -EINVAL && pw_ntsync_create_mutex(0, 1) == -EINVAL);
    int mutex = pw_ntsync_create_mutex(0, 0);
    assert(wait_for(&mutex, 1, 0, 0, 0, 0, NULL) == -EINVAL);  /* a mutex needs an owner */
    assert(!wait_for(&mutex, 1, 0, 7, 0, 0, NULL) && !wait_for(&mutex, 1, 0, 7, 0, 0, NULL));
    assert(wait_for(&mutex, 1, 0, 8, 0, 0, NULL) == -ETIMEDOUT);
    assert(!pw_ntsync_mutex_read(mutex, &a, &b) && a == 7 && b == 2);
    assert(pw_ntsync_mutex_unlock(mutex, 8, NULL) == -EPERM);
    assert(!pw_ntsync_mutex_unlock(mutex, 7, &a) && a == 2);
    assert(pw_ntsync_mutex_kill(mutex, 8) == -EPERM);
    assert(!pw_ntsync_mutex_kill(mutex, 7));
    assert(pw_ntsync_mutex_read(mutex, &a, &b) == -EOWNERDEAD && a == 0 && b == 0);
    assert(wait_for(&mutex, 1, 0, 8, 0, 0, &index) == -EOWNERDEAD && index == 0);
    assert(!pw_ntsync_mutex_read(mutex, &a, &b) && a == 8 && b == 1);   /* reported once */
    assert(!pw_ntsync_mutex_unlock(mutex, 8, &a) && a == 1);
    assert(pw_ntsync_mutex_unlock(mutex, 8, NULL) == -EPERM);           /* not owned */
    int owned = pw_ntsync_create_mutex(9, 1);
    assert(wait_for(&owned, 1, 0, 3, 0, 0, NULL) == -ETIMEDOUT && !wait_for(&owned, 1, 0, 9, 0, 0, NULL));

    /* events */
    int manual = pw_ntsync_create_event(1, 0), automatic = pw_ntsync_create_event(0, 1);
    assert(wait_for(&manual, 1, 0, 1, 0, 0, NULL) == -ETIMEDOUT);
    assert(!pw_ntsync_event_set(manual, &a) && a == 0);
    assert(!wait_for(&manual, 1, 0, 1, 0, 0, NULL) && !wait_for(&manual, 1, 0, 1, 0, 0, NULL));
    assert(!pw_ntsync_event_reset(manual, &a) && a == 1);
    assert(!wait_for(&automatic, 1, 0, 1, 0, 0, NULL) && wait_for(&automatic, 1, 0, 1, 0, 0, NULL) == -ETIMEDOUT);
    assert(!pw_ntsync_event_pulse(manual, &a) && a == 0);              /* no waiter: stays clear */
    assert(!pw_ntsync_event_read(manual, &a, &b) && a == 1 && b == 0);

    /* wait-any takes the lowest signaled index; wait-all takes all or none */
    int any[3] = {pw_ntsync_create_event(1, 0), pw_ntsync_create_event(1, 1), pw_ntsync_create_event(1, 1)};
    assert(!wait_for(any, 3, 0, 1, 0, 0, &index) && index == 1);
    int pair[2] = {pw_ntsync_create_event(0, 1), pw_ntsync_create_event(0, 0)};
    assert(wait_for(pair, 2, 1, 1, 0, 0, NULL) == -ETIMEDOUT);
    assert(!pw_ntsync_event_read(pair[0], NULL, &a) && a == 1);        /* consumed nothing */
    pw_ntsync_event_set(pair[1], NULL);
    assert(!wait_for(pair, 2, 1, 1, 0, 0, &index) && index == 0);
    assert(!pw_ntsync_event_read(pair[0], NULL, &a) && a == 0 && !pw_ntsync_event_read(pair[1], NULL, &b) && b == 0);
    int twice[2] = {manual, manual};
    assert(wait_for(twice, 2, 1, 1, 0, 0, NULL) == -EINVAL);           /* the same object twice in wait-all */
    pw_ntsync_event_set(manual, NULL);
    assert(!wait_for(twice, 2, 0, 1, 0, 0, &index) && index == 0);     /* is fine in wait-any */
    int mixed[2] = {pw_ntsync_create_mutex(0, 0), pw_ntsync_create_semaphore(1, 1)};
    assert(!wait_for(mixed, 2, 1, 4, 0, 0, NULL));
    assert(!pw_ntsync_mutex_read(mixed[0], &a, &b) && a == 4 && b == 1);
    assert(!pw_ntsync_semaphore_read(mixed[1], &a, NULL) && a == 0);

    /* alerts: index == count, and only when no object is signaled */
    int alert = pw_ntsync_create_event(1, 1), clear = pw_ntsync_create_event(1, 0);
    assert(!wait_for(&clear, 1, 0, 1, alert, 0, &index) && index == 1);
    assert(!wait_for(any, 3, 0, 1, alert, 0, &index) && index == 1);
    assert(!wait_for(NULL, 0, 0, 1, alert, 0, &index) && index == 0);
    assert(wait_for(&clear, 1, 0, 1, sem, 0, NULL) == -EINVAL);       /* an alert is an event */

    /* numbers: references and stale numbers */
    int temporary = pw_ntsync_create_event(1, 1);
    assert(pw_ntsync_ref(temporary) == temporary);
    assert(!pw_ntsync_release(temporary) && !pw_ntsync_event_read(temporary, NULL, &a));
    assert(!pw_ntsync_release(temporary));
    assert(pw_ntsync_event_read(temporary, NULL, &a) == -EINVAL && pw_ntsync_release(temporary) == -EINVAL);
    int reused = pw_ntsync_create_event(1, 0);
    assert(reused != temporary && pw_ntsync_event_read(temporary, NULL, &a) == -EINVAL);
    assert(pw_ntsync_release(PW_NTSYNC_DEVICE) == -EINVAL && pw_ntsync_release(0) == -EINVAL);

    int objects[] = {sem, mutex, owned, manual, automatic, any[0], any[1], any[2], pair[0], pair[1],
                     mixed[0], mixed[1], alert, clear, reused};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) assert(!pw_ntsync_release(objects[i]));
    pw_ntsync_census(&a, &b);
    assert(a == 0 && b == 0);
}

/* ---- threads ------------------------------------------------------------- */

static int shared_a, shared_b, shared_event, shared_alert;
static int counter;
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

/* Wait-all over two semaphores, then put both units back: if wait-all were
 * not atomic, units would be taken from one semaphore and not the other,
 * and the totals would drift or the threads would deadlock. */
static void *wait_all_worker(void *argument)
{
    const uint32_t owner = (uint32_t)(uintptr_t)argument;
    const int pair[2] = {shared_a, shared_b};
    for (int i = 0; i < 20000; i++) {
        uint32_t index;
        int status = wait_for(pair, 2, 1, owner, 0, 2000000000, &index);
        assert(status == 0 && index == 0);
        pthread_mutex_lock(&counter_lock);
        counter++;
        pthread_mutex_unlock(&counter_lock);
        assert(!pw_ntsync_semaphore_release(shared_b, 1, NULL));
        assert(!pw_ntsync_semaphore_release(shared_a, 1, NULL));
    }
    return NULL;
}

/* A thread that takes single units of a and puts them back. */
static void *single_worker(void *argument)
{
    (void)argument;
    for (int i = 0; i < 20000; i++) {
        assert(!wait_for(&shared_a, 1, 0, 99, 0, 2000000000, NULL));
        assert(!pw_ntsync_semaphore_release(shared_a, 1, NULL));
    }
    return NULL;
}

static void wait_all_atomicity(void)
{
    pthread_t threads[6];
    uint32_t a, b;
    shared_a = pw_ntsync_create_semaphore(2, 2);
    shared_b = pw_ntsync_create_semaphore(2, 2);
    for (uintptr_t i = 0; i < 4; i++) pthread_create(&threads[i], NULL, wait_all_worker, (void *)(i + 1));
    for (int i = 4; i < 6; i++) pthread_create(&threads[i], NULL, single_worker, NULL);
    for (int i = 0; i < 6; i++) pthread_join(threads[i], NULL);
    assert(counter == 80000);
    assert(!pw_ntsync_semaphore_read(shared_a, &a, NULL) && a == 2);
    assert(!pw_ntsync_semaphore_read(shared_b, &b, NULL) && b == 2);
    pw_ntsync_release(shared_a);
    pw_ntsync_release(shared_b);
}

/* An auto-reset event wakes exactly one of many waiters per set, in FIFO
 * order; a manual one wakes them all. */
static int woken;
static int order[8], order_used;
static void *event_waiter(void *argument)
{
    const int me = (int)(uintptr_t)argument;
    assert(!wait_for(&shared_event, 1, 0, (uint32_t)me + 1, 0, 5000000000, NULL));
    pthread_mutex_lock(&counter_lock);
    woken++;
    order[order_used++] = me;
    pthread_mutex_unlock(&counter_lock);
    return NULL;
}

static void wait_for_blocked(uint32_t count)
{
    uint32_t blocked = 0;
    for (int spin = 0; spin < 5000 && blocked != count; spin++) {
        pw_ntsync_census(NULL, &blocked);
        usleep(200);
    }
    assert(blocked == count);
}

static void waking(void)
{
    pthread_t threads[8];
    for (int manual = 0; manual < 2; manual++) {
        shared_event = pw_ntsync_create_event(manual, 0);
        woken = order_used = 0;
        for (uintptr_t i = 0; i < 8; i++) {
            pthread_create(&threads[i], NULL, event_waiter, (void *)i);
            wait_for_blocked((uint32_t)i + 1);   /* a known queue order */
        }
        if (manual) {
            assert(!pw_ntsync_event_set(shared_event, NULL));
        } else {
            for (int i = 0; i < 8; i++) {
                int seen = 0;
                assert(!pw_ntsync_event_set(shared_event, NULL));
                /* one waiter wakes, and only one: the rest stay blocked */
                for (int spin = 0; spin < 5000 && seen != i + 1; spin++) {
                    pthread_mutex_lock(&counter_lock);
                    seen = woken;
                    pthread_mutex_unlock(&counter_lock);
                    if (seen != i + 1) usleep(200);
                }
                assert(seen == i + 1);
                usleep(2000);
                wait_for_blocked(7 - (uint32_t)i);
                pthread_mutex_lock(&counter_lock);
                assert(woken == i + 1);
                pthread_mutex_unlock(&counter_lock);
            }
        }
        for (int i = 0; i < 8; i++) pthread_join(threads[i], NULL);
        assert(woken == 8);
        if (!manual) for (int i = 0; i < 8; i++) assert(order[i] == i);
        pw_ntsync_release(shared_event);
    }
    /* pulse wakes every current waiter of a manual event and leaves it clear */
    shared_event = pw_ntsync_create_event(1, 0);
    woken = order_used = 0;
    for (uintptr_t i = 0; i < 4; i++) pthread_create(&threads[i], NULL, event_waiter, (void *)i);
    wait_for_blocked(4);
    uint32_t previous = 9, state = 9;
    assert(!pw_ntsync_event_pulse(shared_event, &previous) && previous == 0);
    for (int i = 0; i < 4; i++) pthread_join(threads[i], NULL);
    assert(woken == 4 && !pw_ntsync_event_read(shared_event, NULL, &state) && state == 0);
    pw_ntsync_release(shared_event);
}

static int abandoned_mutex;
static void *take_and_die(void *argument)
{
    (void)argument;
    assert(!wait_for(&abandoned_mutex, 1, 0, 42, 0, -1, NULL));
    return NULL;
}
static void *wait_abandoned(void *argument)
{
    uint32_t index = 9;
    (void)argument;
    assert(wait_for(&abandoned_mutex, 1, 0, 43, 0, 5000000000, &index) == -EOWNERDEAD && index == 0);
    assert(!pw_ntsync_mutex_unlock(abandoned_mutex, 43, NULL));
    return NULL;
}

static void abandonment(void)
{
    pthread_t owner, waiter;
    abandoned_mutex = pw_ntsync_create_mutex(0, 0);
    pthread_create(&owner, NULL, take_and_die, NULL);
    pthread_join(owner, NULL);
    pthread_create(&waiter, NULL, wait_abandoned, NULL);
    wait_for_blocked(1);
    assert(!pw_ntsync_mutex_kill(abandoned_mutex, 42));   /* what the server does when 42 ends */
    pthread_join(waiter, NULL);
    pw_ntsync_release(abandoned_mutex);
}

static void *alertable(void *argument)
{
    uint32_t index = 9;
    (void)argument;
    assert(!wait_for(&shared_event, 1, 0, 1, shared_alert, 5000000000, &index) && index == 1);
    return NULL;
}

static void alerts_and_timeouts(void)
{
    pthread_t thread;
    shared_event = pw_ntsync_create_event(1, 0);
    shared_alert = pw_ntsync_create_event(1, 0);
    pthread_create(&thread, NULL, alertable, NULL);
    wait_for_blocked(1);
    pw_ntsync_event_set(shared_alert, NULL);
    pthread_join(thread, NULL);

    /* a relative 30 ms wait takes 30 ms, give or take the scheduler */
    const uint64_t start = monotonic();
    assert(wait_for(&shared_event, 1, 0, 1, 0, 30000000, NULL) == -ETIMEDOUT);
    const uint64_t elapsed = monotonic() - start;
    assert(elapsed >= 29000000 && elapsed < 500000000);
    /* a realtime deadline in the past is a poll */
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    PwNtsyncWait wait = {.timeout = (uint64_t)now.tv_sec * 1000000000u, .realtime = 1, .objects = &shared_event,
                         .count = 1, .owner = 1};
    assert(pw_ntsync_wait(&wait) == -ETIMEDOUT);
    pw_ntsync_release(shared_event);
    pw_ntsync_release(shared_alert);
}

/* An object closed by its holders while a wait still uses it stays valid
 * until the wait ends. */
static void *wait_then_signal(void *argument)
{
    (void)argument;
    assert(!wait_for(&shared_event, 1, 0, 1, 0, 5000000000, NULL));
    return NULL;
}

static void lifetime(void)
{
    pthread_t thread;
    uint32_t objects;
    shared_event = pw_ntsync_create_event(1, 0);
    const int second = pw_ntsync_ref(shared_event);
    pthread_create(&thread, NULL, wait_then_signal, NULL);
    wait_for_blocked(1);
    assert(!pw_ntsync_release(shared_event));            /* the first holder goes */
    assert(!pw_ntsync_event_set(second, NULL));          /* the second signals and goes */
    assert(!pw_ntsync_release(second));
    pthread_join(thread, NULL);
    pw_ntsync_census(&objects, NULL);
    assert(objects == 0);
}

/* The dump, for a hang: blocked waits with their objects, the trace's
 * history of those objects, and the last operations. */
static char dumped[65536];
static size_t dumped_used;
static void collect(void *context, const char *text)
{
    (void)context;
    const size_t length = strlen(text);
    assert(dumped_used + length + 2 < sizeof(dumped));
    memcpy(dumped + dumped_used, text, length);
    dumped[dumped_used + length] = '\n';
    dumped_used += length + 1;
    dumped[dumped_used] = 0;
}

static void dump(uint32_t recent)
{
    dumped_used = 0;
    dumped[0] = 0;
    pw_ntsync_dump(collect, NULL, recent, 100);
}

static void *blocked_waiter(void *argument)
{
    (void)argument;
    assert(!wait_for(&shared_event, 1, 0, 0x30, shared_alert, -1, NULL));
    return NULL;
}

/* A handler that uses the device, as Wine's suspend does through the server:
 * run on a thread that held the device's lock, it would never return. */
static volatile int handled;
static void report_signal(int signal)
{
    if (pw_ntsync_signal_deferred(signal)) return;
    pw_ntsync_event_read(shared_alert, NULL, NULL);
    handled++;
}

/* The device's test hook delivers one real signal after enter() marked this
 * thread inside, before it asks for the lock. The stress loop remains, but
 * positive deferral evidence no longer depends on a signal hitting a tiny
 * critical section by chance (400 timed signals once produced zero deferrals). */
static _Thread_local int inject_device_signal;
static void before_lock(void)
{
    if(inject_device_signal) {
        inject_device_signal=0;
        assert(!pthread_kill(pthread_self(),SIGUSR1));
    }
}
static atomic_int spinning, spinner_ready;
static void *spin_in_device(void *argument)
{
    (void)argument;
    pw_ntsync_test_before_lock=before_lock;
    inject_device_signal=1;
    pw_ntsync_event_read(shared_alert,NULL,NULL);
    atomic_store(&spinner_ready,1);
    while (spinning) pw_ntsync_event_read(shared_alert, NULL, NULL);
    return NULL;
}

static void diagnostics(void)
{
    pthread_t thread;
    char expected[256];
    assert(!pw_ntsync_trace(64));
    shared_event = pw_ntsync_create_event(1, 0);
    shared_alert = pw_ntsync_create_event(0, 0);
    assert(pw_ntsync_note(shared_event, "Thread 0034") == 0);
    assert(pw_ntsync_note(shared_event, "something else") == 0);    /* the first note stays */
    assert(pw_ntsync_note(PW_NTSYNC_DEVICE, "x") == -EINVAL && pw_ntsync_note(shared_event, NULL) == -EINVAL);
    pthread_create(&thread, NULL, blocked_waiter, NULL);
    wait_for_blocked(1);
    dump(16);
    assert(strstr(dumped, "device lock: free\n"));
    assert(strstr(dumped, "2 objects (2 events, 0 semaphores, 0 mutexes), 1 blocked"));
    assert(strstr(dumped, "wait: 0030 ("));
    assert(strstr(dumped, "in wait-any of 1, forever\n"));
    snprintf(expected, sizeof(expected), "wait:   [0] #%x event manual clear \"Thread 0034\" queue 1\n", shared_event);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "wait:   [alert] #%x event auto clear queue 1\n", shared_alert);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "history #%x \"Thread 0034\", last 2:", shared_event);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "create #%x event manual clear", shared_event);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "0030 block on #%x [0] of wait-any of 1", shared_event);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "0030 block on #%x [alert] of wait-any of 1", shared_alert);
    assert(strstr(dumped, expected));
    assert(strstr(dumped, "0 entered again, 0 unlock errors"));
    assert(strstr(dumped, "device signals: 0 deferred (landed on a thread inside), 0 raised again after"));

    /* A signal to a thread outside the device, or asleep in a wait, runs its
     * handler at once; one that lands inside is deferred and raised again
     * when the thread leaves, and its handler, which uses the device, runs
     * then: a thread that spins in the device and is signalled 400 times
     * finishes, with every deferred signal raised again. */
    struct sigaction action = {.sa_handler = report_signal};
    sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGUSR1, &action, NULL));
    assert(!pw_ntsync_signal_deferred(SIGUSR1));
    handled = 0;
    pthread_kill(thread, SIGUSR1);
    for (int spin = 0; spin < 500 && !handled; spin++) usleep(1000);
    assert(handled == 1);
    dump(16);
    assert(strstr(dumped, "device signals: 0 deferred (landed on a thread inside), 0 raised again after"));
    assert(strstr(dumped, "wait: 0030 ("));                     /* still blocked after its handler ran */
    pthread_t spinner;
    spinning = 1;
    atomic_store(&spinner_ready,0);
    assert(!pthread_create(&spinner, NULL, spin_in_device, NULL));
    for(int spin=0;spin<500 && !atomic_load(&spinner_ready);++spin)usleep(1000);
    assert(atomic_load(&spinner_ready));
    for (int i = 0; i < 400; i++) {
        pthread_kill(spinner, SIGUSR1);
        usleep(200);
    }
    spinning = 0;
    pthread_join(spinner, NULL);
    dump(16);
    unsigned deferred = 0, raised = 0;
    const char *signals = strstr(dumped, "device signals: ");
    assert(signals && sscanf(signals, "device signals: %u deferred (landed on a thread inside), %u raised again",
                             &deferred, &raised) == 2);
    assert(deferred > 0 && raised > 0 && raised <= deferred);
    PwNtsyncCounters counters;
    pw_ntsync_counters(&counters);
    assert(counters.deferred == deferred && counters.raised == raised && counters.objects == 2 &&
           counters.blocked == 1 && counters.waits >= counters.slept && counters.slept >= counters.timeouts);
    snprintf(expected, sizeof(expected), "last: signal %d to ", SIGUSR1);
    assert(strstr(dumped, expected));
    printf("signals to a thread spinning in the device: %u deferred, %u raised again, %d handled\n", deferred,
           raised, handled);

    assert(!pw_ntsync_event_set(shared_event, NULL));
    pthread_join(thread, NULL);
    dump(4);
    assert(!strstr(dumped, "wait: ") && !strstr(dumped, "history #"));
    assert(strstr(dumped, "recent: the last 4 operations\n"));
    snprintf(expected, sizeof(expected), "set #%x (was clear)\n", shared_event);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "wake 0030 [0] through #%x\n", shared_event);
    assert(strstr(dumped, expected));
    snprintf(expected, sizeof(expected), "0030 return from wait-any of 1, #%x: took [0]\n", shared_event);
    assert(strstr(dumped, expected));

    /* the ring keeps the last operations only */
    for (int i = 0; i < 100; i++) pw_ntsync_event_reset(shared_alert, NULL);
    dump(1000);
    assert(strstr(dumped, "recent: the last 64 operations\n") && !strstr(dumped, " set #"));
    assert(!pw_ntsync_trace(0));
    dump(1000);
    assert(!strstr(dumped, "recent:") && strstr(dumped, "trace 0 operations, keeps 0"));
    pw_ntsync_release(shared_event);
    pw_ntsync_release(shared_alert);
}

/* Waits that time out while they are being served: a semaphore released one
 * unit at a time and taken with waits of 0 to 100 us. Every unit is taken
 * once, and a waiter served as its wait timed out takes its post before it
 * is used again, or a later wait would wake early. */
static int racing_semaphore;
static volatile int racing_taken;
static void *racing_taker(void *argument)
{
    (void)argument;
    for (unsigned i = 0; racing_taken < 20000; i++) {
        const int status = wait_for(&racing_semaphore, 1, 0, 0x40, 0, (int64_t)(i % 100) * 1000, NULL);
        assert(status == 0 || status == -ETIMEDOUT);
        if (!status) racing_taken++;
    }
    return NULL;
}

/* The hang's object: a wait-any of 1 on an auto-reset event, set while the wait times out. Each set is answered
 * before the next, so a set spent on a wait that then reported a timeout (or a served wait that never woke) leaves
 * the setter without its answer. */
static int racing_event, racing_answer;
static void *racing_event_taker(void *argument)
{
    (void)argument;
    for (unsigned i = 0, taken = 0; taken < 20000; i++) {
        const int status = wait_for(&racing_event, 1, 0, 0x41, 0, (int64_t)(i % 100) * 1000, NULL);
        assert(status == 0 || status == -ETIMEDOUT);
        if (!status) {
            taken++;
            assert(!pw_ntsync_event_set(racing_answer, NULL));
        }
    }
    return NULL;
}

static void timeouts_racing_wakes(void)
{
    pthread_t thread;
    uint32_t count, manual, set;
    racing_semaphore = pw_ntsync_create_semaphore(0, 1000000);
    pthread_create(&thread, NULL, racing_taker, NULL);
    for (int i = 0; i < 20000; i++) {
        assert(!pw_ntsync_semaphore_release(racing_semaphore, 1, NULL));
        if (i % 16 == 0) usleep(20);
    }
    pthread_join(thread, NULL);
    assert(racing_taken == 20000);
    assert(!pw_ntsync_semaphore_read(racing_semaphore, &count, NULL) && count == 0);
    pw_ntsync_release(racing_semaphore);

    racing_event = pw_ntsync_create_event(0, 0);
    racing_answer = pw_ntsync_create_event(0, 0);
    pthread_create(&thread, NULL, racing_event_taker, NULL);
    for (int i = 0; i < 20000; i++) {
        assert(!pw_ntsync_event_set(racing_event, NULL));
        assert(!wait_for(&racing_answer, 1, 0, 0x42, 0, 5000000000ll, NULL));
    }
    pthread_join(thread, NULL);
    assert(!pw_ntsync_event_read(racing_event, &manual, &set) && !manual && !set);
    pw_ntsync_release(racing_event);
    pw_ntsync_release(racing_answer);
}

/* A thread Wine ends while it waits (TerminateThread: the server's SIGQUIT). On the console Wine's handler only marks
 * the thread and returns, and SA_RESTART puts its wait back to sleep. Its wait must leave the queue, so the next set
 * wakes the live waiter behind it; before, the ending thread took it and the live one slept on. */
static int ending_event, ending_status, ending_again, live_status;
static uint64_t live_woke_ns;
static void quit_like_wine(int signal)
{
    if (pw_ntsync_signal_deferred(signal)) return;
    /* Wine 0885: the thread is marked and ends at the user-mode boundary, not here */
}
static void *ending_waiter(void *unused)
{
    (void)unused;
    /* timed, so a device that keeps the wait fails here instead of hanging the test */
    ending_status = wait_for(&ending_event, 1, 0, 0x61, 0, 3000000000ll, NULL);
    ending_again = wait_for(&ending_event, 1, 0, 0x61, 0, 1000000000ll, NULL);
    return NULL;
}
static void *live_waiter(void *unused)
{
    (void)unused;
    live_status = wait_for(&ending_event, 1, 0, 0x62, 0, 3000000000ll, NULL);
    live_woke_ns = monotonic();
    return NULL;
}

/* Two threads the console gives the same number, one after the other ended: the first is ended while it waits, and
 * the second's wait must not inherit its mark (a Direct3D 12 game's investigation, 2026-10-18: e9b9eac's mark stayed in the slot). */
static __thread int reused_number;
static uint32_t test_number(void) { return reused_number ? 0x7777u : 0; }
static int reuse_event, first_status, second_status;
static void *first_holder(void *unused)
{
    (void)unused;
    reused_number = 1;
    first_status = wait_for(&reuse_event, 1, 0, 0x63, 0, 3000000000ll, NULL);
    return NULL;            /* its exit resets the slot */
}
static void *second_holder(void *unused)
{
    (void)unused;
    reused_number = 1;
    second_status = wait_for(&reuse_event, 1, 0, 0x64, 0, 3000000000ll, NULL);
    return NULL;
}

static void ending_threads(void)
{
    struct sigaction quit = {0}, before;
    pthread_t ending, live;
    uint32_t objects, waiters;
    quit.sa_handler = quit_like_wine;
    quit.sa_flags = SA_RESTART;
    assert(!sigaction(SIGQUIT, &quit, &before));
    ending_event = pw_ntsync_create_event(0, 0);
    pthread_create(&ending, NULL, ending_waiter, NULL);
    for (int spin = 0; spin < 5000 && (pw_ntsync_census(&objects, &waiters), waiters != 1); spin++) usleep(1000);
    pthread_create(&live, NULL, live_waiter, NULL);
    for (int spin = 0; spin < 5000 && (pw_ntsync_census(&objects, &waiters), waiters != 2); spin++) usleep(1000);
    assert(waiters == 2);
    assert(!pthread_kill(ending, SIGQUIT));
    pthread_join(ending, NULL);
    assert(ending_status == -ECANCELED && ending_again == -ECANCELED);    /* and it never blocks again */
    const uint64_t set_at = monotonic();
    assert(!pw_ntsync_event_set(ending_event, NULL));
    pthread_join(live, NULL);
    printf("a wait behind a thread being ended: woke %.2f ms after the set (status %d)\n",
           (live_woke_ns - set_at) / 1e6, live_status);
    assert(live_status == 0 && live_woke_ns - set_at < 1000000000ull);
    pw_ntsync_census(&objects, &waiters);
    assert(waiters == 0);
    pw_ntsync_release(ending_event);

    pthread_t first, second;
    pw_ntsync_test_thread_number = test_number;
    reuse_event = pw_ntsync_create_event(0, 0);
    pthread_create(&first, NULL, first_holder, NULL);
    for (int spin = 0; spin < 5000 && (pw_ntsync_census(&objects, &waiters), waiters != 1); spin++) usleep(1000);
    assert(waiters == 1 && !pthread_kill(first, SIGQUIT));
    pthread_join(first, NULL);
    assert(first_status == -ECANCELED);
    pthread_create(&second, NULL, second_holder, NULL);
    for (int spin = 0; spin < 5000 && (pw_ntsync_census(&objects, &waiters), waiters != 1); spin++) usleep(1000);
    assert(waiters == 1);        /* it blocks: no mark inherited */
    assert(!pw_ntsync_event_set(reuse_event, NULL));
    pthread_join(second, NULL);
    assert(second_status == 0);
    pw_ntsync_test_thread_number = NULL;
    pw_ntsync_release(reuse_event);
    assert(!sigaction(SIGQUIT, &before, NULL));
}

/* Ping-pong latency on the host, for comparison with the console. */
static int ping, pong;
static void *ponger(void *argument)
{
    (void)argument;
    for (int i = 0; i < 20000; i++) {
        assert(!wait_for(&ping, 1, 0, 2, 0, -1, NULL));
        pw_ntsync_event_set(pong, NULL);
    }
    return NULL;
}

static void round_trips(void)
{
    pthread_t thread;
    ping = pw_ntsync_create_event(0, 0);
    pong = pw_ntsync_create_event(0, 0);
    pthread_create(&thread, NULL, ponger, NULL);
    const uint64_t start = monotonic();
    for (int i = 0; i < 20000; i++) {
        pw_ntsync_event_set(ping, NULL);
        assert(!wait_for(&pong, 1, 0, 1, 0, -1, NULL));
    }
    const uint64_t elapsed = monotonic() - start;
    pthread_join(thread, NULL);
    printf("event round trip: %.2f us on the host\n", elapsed / 20000 / 1000.0);
    pw_ntsync_release(ping);
    pw_ntsync_release(pong);
}

/* ---- several processes -----------------------------------------------------
 * The title and its helpers share one region; here the parent and children it
 * starts afresh (fork, then exec of this program, so nothing of the parent's
 * device is in them but the region's descriptor). */
static const char *self_path;
static int region_fd;

/* The second pass's children map the region as a helper does (src/pw_ntsync.h, pw_ntsync_set_chunk_mapper): the
 * fixed tables, then each 64 KiB chunk of objects the first time they reach it. */
static int chunk_fd = -1;
static void *map_test_chunk(size_t offset, size_t bytes)
{
    void *map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, chunk_fd, (off_t)offset);
    return map == MAP_FAILED ? NULL : map;
}

static void *map_region(int fd)
{
    void *map = mmap(NULL, pw_ntsync_region_bytes(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(map != MAP_FAILED);
    return map;
}

static pid_t spawn(const char *role, int a, int b)
{
    const pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        char fd_text[16], a_text[16], b_text[16];
        snprintf(fd_text, sizeof(fd_text), "%d", region_fd);
        snprintf(a_text, sizeof(a_text), "%d", a);
        snprintf(b_text, sizeof(b_text), "%d", b);
        execl(self_path, self_path, role, fd_text, a_text, b_text, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void reap(pid_t pid, int expected)
{
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    if (expected < 0) assert(WIFSIGNALED(status) && WTERMSIG(status) == -expected);
    else assert(WIFEXITED(status) && WEXITSTATUS(status) == expected);
}

/* Two threads of one process taking a mutex in turns with the other process's: the owner it reads inside is always
 * its own. */
static int hammer_mutex;
static void *hammer(void *argument)
{
    const uint32_t owner = (uint32_t)(uintptr_t)argument;
    for (int i = 0; i < 20000; i++) {
        uint32_t seen, count;
        assert(!wait_for(&hammer_mutex, 1, 0, owner, 0, -1, NULL));
        assert(!pw_ntsync_mutex_read(hammer_mutex, &seen, &count) && seen == owner && count == 1);
        assert(!pw_ntsync_mutex_unlock(hammer_mutex, owner, NULL));
    }
    return NULL;
}

static void *spin_signals(void *argument)
{
    const int event = (int)(intptr_t)argument;
    for (;;) {
        pw_ntsync_event_set(event, NULL);
        pw_ntsync_event_reset(event, NULL);
        wait_for(&event, 1, 0, 0x99, 0, 0, NULL);
    }
    return NULL;
}

/* A live thread's wait on the handle of a process that is ending: how long after the set it woke, and how. */
static int gone_handle, gone_handle_status;
static uint64_t gone_handle_woke_ns;
static void *wait_on_gone_handle(void *unused)
{
    (void)unused;
    gone_handle_status = wait_for(&gone_handle, 1, 0, 0x55, 0, 3000000000ll, NULL);
    gone_handle_woke_ns = monotonic();
    return NULL;
}

/* A gone process's thread on its way in, holding the lock: it tells the parent, then waits for its kill there. */
static int gone_holding_fd = -1;
static void gone_holding(void)
{
    if (write(gone_holding_fd, "h", 1) != 1) _exit(4);
    for (;;) pause();
}

/* A child's part: attach to the parent's region, then play its role. */
static int child(const char *role, int fd, int a, int b)
{
    if (getenv("PW_NTSYNC_TEST_CHUNKS")) {
        void *fixed = mmap(NULL, pw_ntsync_fixed_bytes(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        assert(fixed != MAP_FAILED);
        chunk_fd = fd;
        pw_ntsync_set_chunk_mapper(map_test_chunk);
        assert(pw_ntsync_attach(fixed, pw_ntsync_fixed_bytes() - 1, 0) == -EINVAL);
        assert(!pw_ntsync_attach(fixed, pw_ntsync_fixed_bytes(), 0));
    } else {
        assert(!pw_ntsync_attach(map_region(fd), pw_ntsync_region_bytes(), 0));
    }
    if (!strcmp(role, "ponger")) {
        for (int i = 0; i < 20000; i++) {
            assert(!wait_for(&a, 1, 0, 2, 0, -1, NULL));
            assert(!pw_ntsync_event_set(b, NULL));
        }
    } else if (!strcmp(role, "hammer")) {
        pthread_t threads[2];
        hammer_mutex = a;
        for (uintptr_t i = 0; i < 2; i++) pthread_create(&threads[i], NULL, hammer, (void *)(0x200 + (uintptr_t)b * 2 + i));
        for (int i = 0; i < 2; i++) pthread_join(threads[i], NULL);
    } else if (!strcmp(role, "sleeper")) {
        alarm(30);      /* the parent kills it; a parent that failed an assert must not leave it holding the pipe */
        assert(!pw_ntsync_event_set(a, NULL));
        wait_for(&b, 1, 0, 0x77, 0, -1, NULL);          /* never returns: its process is gone first */
        return 3;
    } else if (!strcmp(role, "gone-holder")) {
        alarm(30);
        gone_holding_fd = b;
        pw_ntsync_test_gone_holding = gone_holding;
        assert(!pw_ntsync_event_set(a, NULL));
        for (;;) {
            uint32_t manual, set;
            pw_ntsync_event_read(a, &manual, &set);
            usleep(1000);
        }
    } else if (!strcmp(role, "far")) {
        /* an object in a later chunk than any this process reached: set it, read it back, and wait on it */
        uint32_t manual = 2, set = 2;
        assert(!pw_ntsync_event_set(a, NULL));
        assert(!pw_ntsync_event_read(a, &manual, &set) && manual == 1 && set == 1);
        assert(!wait_for(&b, 1, 0, 0x99, 0, -1, NULL));
    } else if (!strcmp(role, "spinner")) {
        pthread_t threads[3];
        assert(!pw_ntsync_event_set(a, NULL));
        for (int i = 0; i < 3; i++) pthread_create(&threads[i], NULL, spin_signals, (void *)(intptr_t)b);
        for (;;) pause();
    }
    return 0;
}

static void processes(void)
{
    PwNtsyncCounters counters;
    uint32_t objects, waiters;

    /* events across processes, as a helper's threads signal each other's: every wake reaches the other process */
    ping = pw_ntsync_create_event(0, 0);
    pong = pw_ntsync_create_event(0, 0);
    pid_t ponger = spawn("ponger", ping, pong);
    const uint64_t start = monotonic();
    for (int i = 0; i < 20000; i++) {
        assert(!pw_ntsync_event_set(ping, NULL));
        assert(!wait_for(&pong, 1, 0, 1, 0, 5000000000ll, NULL));
    }
    const uint64_t elapsed = monotonic() - start;
    reap(ponger, 0);
    printf("event round trip between processes: %.2f us on the host\n", elapsed / 20000 / 1000.0);
    assert(pw_ntsync_process_gone(ponger, 1, 1000) == 0);
    pw_ntsync_release(ping);
    pw_ntsync_release(pong);

    /* one lock, three processes: a mutex taken 120000 times by six threads, never by two at once */
    hammer_mutex = pw_ntsync_create_mutex(0, 0);
    pid_t hammers[2] = {spawn("hammer", hammer_mutex, 1), spawn("hammer", hammer_mutex, 2)};
    pthread_t threads[2];
    for (uintptr_t i = 0; i < 2; i++) pthread_create(&threads[i], NULL, hammer, (void *)(0x100 + i));
    for (int i = 0; i < 2; i++) pthread_join(threads[i], NULL);
    for (int i = 0; i < 2; i++) {
        reap(hammers[i], 0);
        assert(pw_ntsync_process_gone(hammers[i], 1, 1000) == 0);
    }
    uint32_t owner, count;
    assert(!pw_ntsync_mutex_read(hammer_mutex, &owner, &count) && owner == 0 && count == 0);
    pw_ntsync_release(hammer_mutex);

    /* a process gone while its thread waits: the wait leaves the queue, and the next set is not spent on it */
    int ready = pw_ntsync_create_event(1, 0), prize = pw_ntsync_create_event(0, 0);
    pid_t sleeper = spawn("sleeper", ready, prize);
    assert(!wait_for(&ready, 1, 0, 1, 0, 5000000000ll, NULL));
    for (int spin = 0; spin < 5000; spin++) {
        pw_ntsync_census(&objects, &waiters);
        if (waiters == 1) break;
        usleep(1000);
    }
    assert(waiters == 1);
    pw_ntsync_counters(&counters);
    assert(counters.processes == 2);
    assert(pw_ntsync_process_gone(sleeper, 0, 1000) == 1);
    assert(pw_ntsync_process_gone(getpid(), 0, 1000) == -ESRCH);       /* never itself */
    pw_ntsync_census(&objects, &waiters);
    assert(waiters == 0);
    assert(!kill(sleeper, SIGKILL));
    reap(sleeper, -SIGKILL);
    assert(pw_ntsync_process_gone(sleeper, 1, 1000) == 0);
    assert(pw_ntsync_process_gone(sleeper, 1, 1000) == -ESRCH);        /* its place is free */
    assert(!pw_ntsync_event_set(prize, NULL));
    assert(!wait_for(&prize, 1, 0, 1, 0, 0, NULL));                /* still set: nobody dead took it */
    pw_ntsync_release(ready);
    pw_ntsync_release(prize);

    /* A process told gone at its exit message, its thread still asleep in the device, and a live wait on its
     * process handle, which the server sets as it ends: the wait wakes at the set. Its place is not the dead
     * thread's, whose word the kernel would wake first (stress-gmdx-2026-10-18-11: the restored explorer's main
     * thread, served and asleep forever). Before the fix this wait woke only at its 3 s timeout. */
    ready = pw_ntsync_create_event(1, 0);
    prize = pw_ntsync_create_event(0, 0);
    gone_handle = pw_ntsync_create_event(1, 0);
    sleeper = spawn("sleeper", ready, prize);
    assert(!wait_for(&ready, 1, 0, 1, 0, 5000000000ll, NULL));
    for (int spin = 0; spin < 5000 && (pw_ntsync_census(&objects, &waiters), waiters != 1); spin++) usleep(1000);
    assert(waiters == 1);
    assert(pw_ntsync_process_gone(sleeper, 0, 1000) == 1);         /* its exit message: it has not ended */
    pthread_t live;
    assert(!pthread_create(&live, NULL, wait_on_gone_handle, NULL));
    for (int spin = 0; spin < 5000 && (pw_ntsync_census(&objects, &waiters), waiters != 1); spin++) usleep(1000);
    assert(waiters == 1);
    dump(0);
    assert(strstr(dumped, "(1 of them kept until theirs end)"));
    const uint64_t set_at = monotonic();
    assert(!pw_ntsync_event_set(gone_handle, NULL));
    pthread_join(live, NULL);
    printf("a wait on an ending process's handle: woke %.2f ms after the set (status %d)\n",
           (gone_handle_woke_ns - set_at) / 1e6, gone_handle_status);
    assert(gone_handle_status == 0 && gone_handle_woke_ns - set_at < 1000000000ull);
    assert(!kill(sleeper, SIGKILL));
    reap(sleeper, -SIGKILL);
    assert(pw_ntsync_process_gone(sleeper, 1, 1000) == 0);         /* dropped once, its place freed now */
    dump(0);
    assert(strstr(dumped, "(0 of them kept until theirs end)"));
    pw_ntsync_release(ready);
    pw_ntsync_release(prize);
    pw_ntsync_release(gone_handle);

    /* A process told gone, then killed while its thread on its way in holds the lock (the title's end of a helper,
     * whose threads still run): the call once it has ended takes the lock back, and the device goes on. Before, that
     * call returned -EBUSY and every later call of every process waited forever (one host run in about 36). */
    {
        int pipe_fds[2];
        char byte;
        ready = pw_ntsync_create_event(1, 0);
        assert(!pipe(pipe_fds));
        pid_t holder = spawn("gone-holder", ready, pipe_fds[1]);
        close(pipe_fds[1]);
        assert(!wait_for(&ready, 1, 0, 1, 0, 5000000000ll, NULL));
        assert(pw_ntsync_process_gone(holder, 0, 1000) == 0);
        assert(read(pipe_fds[0], &byte, 1) == 1);                      /* it holds the lock now */
        close(pipe_fds[0]);
        assert(!kill(holder, SIGKILL));
        reap(holder, -SIGKILL);
        assert(pw_ntsync_process_gone(holder, 1, 1000) == 0);
        dump(0);
        assert(strstr(dumped, "device lock: free\n") && strstr(dumped, "the lock taken back from 1 that ended"));
        assert(!pw_ntsync_event_reset(ready, NULL) && !pw_ntsync_event_set(ready, NULL));
        assert(!wait_for(&ready, 1, 0, 1, 0, 1000000000ll, NULL));
        pw_ntsync_release(ready);
    }

    /* processes gone while their threads spin through the lock, then killed: none is cut off inside it, and the
     * device goes on for the others */
    for (int round = 0; round < 20; round++) {
        int started = pw_ntsync_create_event(1, 0), busy = pw_ntsync_create_event(0, 0);
        pid_t spinner = spawn("spinner", started, busy);
        assert(!wait_for(&started, 1, 0, 1, 0, 5000000000ll, NULL));
        usleep(2000 + (useconds_t)round * 500);
        assert(pw_ntsync_process_gone(spinner, 0, 1000) >= 0);
        assert(!kill(spinner, SIGKILL));
        reap(spinner, -SIGKILL);
        assert(pw_ntsync_process_gone(spinner, 1, 1000) >= 0);
        assert(!pw_ntsync_event_set(busy, NULL));
        assert(!wait_for(&busy, 1, 0, 1, 0, 1000000000ll, NULL));
        pw_ntsync_release(started);
        pw_ntsync_release(busy);
    }
    pw_ntsync_census(&objects, &waiters);
    assert(waiters == 0);
    pw_ntsync_counters(&counters);
    assert(counters.processes == 1 && counters.exhausted == 0 && counters.dropped >= 1);
    dump(0);
    assert(strstr(dumped, "device lock: free\n"));
    printf("processes: %u waits dropped with their process\n", counters.dropped);
}

/* Objects three chunks in (past 2048 more), which a chunked child reaches for the first time by number. */
static void far_chunks(void)
{
    int far = 0, back, made[2200];
    for (int i = 0; i < 2200; i++) assert((made[i] = pw_ntsync_create_event(1, 0)) > 0);
    far = made[2199];
    assert((back = pw_ntsync_create_event(1, 0)) > 0);
    {
        const pid_t pid = spawn("far", far, back);
        uint32_t manual, set;
        assert(!wait_for(&far, 1, 0, 0x98, 0, -1, NULL));
        assert(!pw_ntsync_event_read(far, &manual, &set) && set == 1);
        assert(!pw_ntsync_event_set(back, NULL));
        reap(pid, 0);
    }
    for (int i = 0; i < 2200; i++) assert(!pw_ntsync_release(made[i]));
    assert(!pw_ntsync_release(back));
}

/* Another lock's critical section (Wine's heap, pw_ntsync_critical_enter): a signal that lands inside waits for the
 * last leave, nested sections count, and outside one runs at once. A thread suspended holding the heap's mutex
 * stopped every other thread's next malloc (a8-rel-base-2026-11-02 suspend-server64). */
static volatile int critical_handled;
static void critical_signal(int signal)
{
    if (pw_ntsync_signal_deferred(signal)) return;
    critical_handled++;
}

static void critical_sections(void)
{
    struct sigaction action = {.sa_handler = critical_signal}, old;
    sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGUSR1, &action, &old));
    critical_handled = 0;
    pw_ntsync_critical_enter();
    pw_ntsync_critical_enter();
    assert(!pthread_kill(pthread_self(), SIGUSR1) && critical_handled == 0);   /* to itself: delivered before it returns */
    pw_ntsync_critical_leave();
    assert(critical_handled == 0);
    pw_ntsync_critical_leave();
    assert(critical_handled == 1);
    assert(!pthread_kill(pthread_self(), SIGUSR1) && critical_handled == 2);
    pw_ntsync_critical_leave();   /* one leave too many changes nothing */
    assert(!pthread_kill(pthread_self(), SIGUSR1) && critical_handled == 3);
    assert(!sigaction(SIGUSR1, &old, NULL));
    puts("critical sections: a signal inside waits for the last leave");
}

int main(int argc, char **argv)
{
    char name[64];
    if (argc == 5) return child(argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    self_path = argv[0];
    snprintf(name, sizeof(name), "/pw-ntsync-test-%d", (int)getpid());
    region_fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    assert(region_fd >= 0);
    shm_unlink(name);
    assert(!ftruncate(region_fd, (off_t)pw_ntsync_region_bytes()));
    assert(fcntl(region_fd, F_SETFD, 0) == 0);                     /* the children get it */
    assert(pw_ntsync_attach(map_region(region_fd), 100, 1) == -EINVAL);
    assert(pw_ntsync_attach(map_region(region_fd), pw_ntsync_region_bytes(), 0) == -EINVAL);  /* not formatted */
    assert(!pw_ntsync_attach(map_region(region_fd), pw_ntsync_region_bytes(), 1));
    assert(pw_ntsync_attach(map_region(region_fd), pw_ntsync_region_bytes(), 0) == -EBUSY);
    semantics();
    wait_all_atomicity();
    waking();
    abandonment();
    alerts_and_timeouts();
    lifetime();
    timeouts_racing_wakes();
    ending_threads();
    diagnostics();
    round_trips();
    processes();
    far_chunks();
    critical_sections();
    if (!getenv("PW_NTSYNC_TEST_CHUNKS")) {
        /* The whole suite again, with every child mapping the objects in chunks as a helper does. */
        const pid_t pid = fork();
        int status;
        assert(pid >= 0);
        if (!pid) {
            setenv("PW_NTSYNC_TEST_CHUNKS", "1", 1);
            execl(self_path, self_path, (char *)NULL);
            _exit(127);
        }
        assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    puts(getenv("PW_NTSYNC_TEST_CHUNKS") ? "pw_ntsync (children in chunks): ok" : "pw_ntsync: ok");
    return 0;
}
