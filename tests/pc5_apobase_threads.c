/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2410: FAPOBase's reference count under threads. Several threads take and drop references on one effect, as a
 * game's voices on several threads do; the references the test holds keep the count above zero, so the destructor must
 * not run, and the count must come back to its start. The pinned FAudio changes the count with plain += and -=. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FAPOBase.h"

enum { THREADS = 8, ROUNDS = 400000 };
static int destroyed;

static void destructor(void *fapo) { (void)fapo; __atomic_add_fetch(&destroyed, 1, __ATOMIC_SEQ_CST); }

static void *worker(void *arg)
{
    FAPOBase *fapo = arg;
    for (int i = 0; i < ROUNDS; i++) { FAPOBase_AddRef(fapo); FAPOBase_Release(fapo); }
    return NULL;
}

int main(void)
{
    FAPOBase fapo;
    pthread_t threads[THREADS];
    memset(&fapo, 0, sizeof(fapo));
    CreateFAPOBase(&fapo, NULL, NULL, 0, 0);   /* registration properties are not read here */
    fapo.Destructor = destructor;
    for (int i = 0; i < THREADS; i++) pthread_create(&threads[i], NULL, worker, &fapo);
    for (int i = 0; i < THREADS; i++) pthread_join(threads[i], NULL);
    if (fapo.m_lReferenceCount != 1 || destroyed) {
        printf("count %d after %d threads, destructor ran %d times\n", (int)fapo.m_lReferenceCount, THREADS, destroyed);
        return 1;
    }
    if (FAPOBase_AddRef(&fapo) != 2 || FAPOBase_Release(&fapo) != 1 || destroyed) { puts("single-thread count wrong"); return 1; }
    if (FAPOBase_Release(&fapo) != 0 || destroyed != 1) { puts("the last release must run the destructor once"); return 1; }
    puts("pc5 apobase passed");
    return 0;
}
