/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2414: win32u's register_builtin_classes, as the pin has it (-DOLD, a pthread_once) or as the patch writes it,
 * with the text of register_builtins and register_builtin_classes cut out of the file (counts.inc). The stand-ins model
 * what made the hang: registering a class loads its cursor through a callback that takes the loader lock, as does
 * user32's part. The loader lock is recursive, as Windows'.
 *
 *   harness hold    thread X holds the loader lock (a DllMain) and makes its first window while thread Y, which came first,
 *                   waits for that lock inside the registration. X must get through without Y.
 *   harness hammer  eight threads call it a thousand times each: the classes are registered once, user32's part runs once. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int BOOL;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned int UINT;
typedef intptr_t INT;
typedef uintptr_t ULONG_PTR;
typedef uint64_t UINT64;
#define TRUE 1
#define FALSE 0
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ReadAcquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define InterlockedCompareExchange(p, n, o) ({ LONG e_ = (o); __atomic_compare_exchange_n((p), &e_, (n), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); e_; })
#define InterlockedExchange(p, n) __atomic_exchange_n((p), (n), __ATOMIC_SEQ_CST)
#define CS_DBLCLKS 8
#define CS_PARENTDC 0x80
#define IDC_IBEAM 32513
#define NTUSER_WNDPROC_EDIT 3
#define NtUserInitBuiltinClasses 1

struct builtin_class_descr { const char *name; UINT style; INT extra; ULONG_PTR cursor; void *brush; int proc; };
static const struct builtin_class_descr builtin_classes[] = { { "Button" }, { "Static" }, { "ComboBox" }, { "ListBox" } };
static struct { int WowTebOffset; } the_teb;
#define NtCurrentTeb() (&the_teb)

static pthread_mutex_t loader_lock;
static pthread_mutex_t classes_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *registered[16];
static int registered_count, refused, user32_runs;
static volatile int y_waiting;
static int y_thread_is_marked;
static __thread int is_y;

static void take_loader_lock(void)
{
    if (is_y) y_waiting = 1;
    pthread_mutex_lock(&loader_lock);
    pthread_mutex_unlock(&loader_lock);
}

static void register_builtin(const struct builtin_class_descr *descr)
{
    take_loader_lock();                                   /* the cursor, loaded through user32 */
    pthread_mutex_lock(&classes_lock);
    for (int i = 0; i < registered_count; i++) if (!strcmp(registered[i], descr->name)) { refused++; pthread_mutex_unlock(&classes_lock); return; }
    registered[registered_count++] = descr->name;
    pthread_mutex_unlock(&classes_lock);
}
static int KeUserModeCallback(int id, void *in, ULONG len, void **out, ULONG *out_len)
{
    (void)in; (void)len; (void)out; (void)out_len; (void)id;
    take_loader_lock();                                   /* user32's part loads uxtheme */
    __atomic_add_fetch(&user32_runs, 1, __ATOMIC_SEQ_CST);
    return 0;
}

#include "counts.inc"

static volatile int x_done, x_holds;
static void *thread_y(void *unused)
{
    (void)unused;
    is_y = 1;
    while (!x_holds) usleep(1000);
    register_builtin_classes();
    return NULL;
}
static void *thread_x(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&loader_lock);                     /* in a DllMain */
    x_holds = 1;
    while (!y_waiting) usleep(1000);
    usleep(50 * 1000);
    register_builtin_classes();                           /* its first window */
    x_done = 1;
    pthread_mutex_unlock(&loader_lock);
    return NULL;
}

static int hold(void)
{
    pthread_mutexattr_t attr;
    pthread_t x, y;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&loader_lock, &attr);
    pthread_create(&y, NULL, thread_y, NULL);
    pthread_create(&x, NULL, thread_x, NULL);
    for (int i = 0; i < 300 && !x_done; i++) usleep(10 * 1000);
    if (!x_done) { puts("X (holding the loader lock) is still waiting in register_builtin_classes after 3 s"); fflush(stdout); _exit(1); }
    pthread_join(x, NULL);
    pthread_join(y, NULL);
    if (registered_count != (int)ARRAYSIZE(builtin_classes) + 1) { printf("%d classes registered\n", registered_count); return 1; }
    if (user32_runs != 1) { printf("user32's part ran %d times\n", user32_runs); return 1; }
    puts("pc5 builtin classes hold passed");
    return 0;
}

static void *hammer_thread(void *unused)
{
    (void)unused;
    for (int i = 0; i < 1000; i++) register_builtin_classes();
    return NULL;
}
static int hammer(void)
{
    pthread_t t[8];
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&loader_lock, &attr);
    for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, hammer_thread, NULL);
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    if (registered_count != (int)ARRAYSIZE(builtin_classes) + 1) { printf("%d classes registered\n", registered_count); return 1; }
    if (user32_runs != 1) { printf("user32's part ran %d times\n", user32_runs); return 1; }
    puts("pc5 builtin classes hammer passed");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "hold")) return hold();
    if (argc > 1 && !strcmp(argv[1], "hammer")) return hammer();
    return 2;
}
