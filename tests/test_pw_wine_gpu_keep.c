/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The ranges of host memory a program gave the GPU (wine/ps5/pw_wine_gpu_keep.h): pieces wholly kept or not, repeated
 * and overlapping records, exact removal, a full table refused. */
#include "../wine/ps5/pw_wine_gpu_keep.h"
#include <assert.h>
#include <stdio.h>

static PwWineGpuKeep keep;

int main(void)
{
    uintptr_t to = 0;

    /* Nothing kept: one piece to the end. */
    assert(!pw_wine_gpu_keep_piece(&keep, 0x1000, 0x9000, &to) && to == 0x9000);
    assert(!pw_wine_gpu_keep_touches(&keep, 0, ~(uintptr_t)0));
    assert(pw_wine_gpu_keep_add(&keep, 0x4000, 0x4000) == -1);

    /* [0x4000, 0x8000): before it, in it, after it. */
    assert(!pw_wine_gpu_keep_add(&keep, 0x4000, 0x8000));
    assert(!pw_wine_gpu_keep_piece(&keep, 0x0, 0x10000, &to) && to == 0x4000);
    assert(pw_wine_gpu_keep_piece(&keep, 0x4000, 0x10000, &to) == 1 && to == 0x8000);
    assert(pw_wine_gpu_keep_piece(&keep, 0x5000, 0x6000, &to) == 1 && to == 0x6000);
    assert(!pw_wine_gpu_keep_piece(&keep, 0x8000, 0x10000, &to) && to == 0x10000);
    assert(pw_wine_gpu_keep_touches(&keep, 0x7000, 0x9000) && !pw_wine_gpu_keep_touches(&keep, 0x8000, 0x9000) &&
           !pw_wine_gpu_keep_touches(&keep, 0x0, 0x4000));

    /* An overlapping record and an adjoining one carry the kept piece on. */
    assert(!pw_wine_gpu_keep_add(&keep, 0x6000, 0xc000) && !pw_wine_gpu_keep_add(&keep, 0xc000, 0xd000));
    assert(pw_wine_gpu_keep_piece(&keep, 0x4000, 0x20000, &to) == 1 && to == 0xd000);

    /* The same range twice: kept until both records go. A removal names a record exactly. */
    assert(!pw_wine_gpu_keep_add(&keep, 0x4000, 0x8000));
    assert(pw_wine_gpu_keep_remove(&keep, 0x4000, 0x6000) == -1);
    assert(!pw_wine_gpu_keep_remove(&keep, 0x4000, 0x8000));
    assert(pw_wine_gpu_keep_piece(&keep, 0x4000, 0x6000, &to) == 1 && to == 0x6000);
    assert(!pw_wine_gpu_keep_remove(&keep, 0x4000, 0x8000));
    assert(!pw_wine_gpu_keep_piece(&keep, 0x4000, 0x20000, &to) && to == 0x6000);
    assert(!pw_wine_gpu_keep_remove(&keep, 0x6000, 0xc000) && !pw_wine_gpu_keep_remove(&keep, 0xc000, 0xd000));
    assert(keep.count == 0 && pw_wine_gpu_keep_remove(&keep, 0xc000, 0xd000) == -1);

    /* A full table refuses the next record and keeps the ones it has. */
    for (uintptr_t i = 0; i < PW_WINE_GPU_KEEP_MAX; i++) assert(!pw_wine_gpu_keep_add(&keep, i << 16, (i << 16) + 0x4000));
    assert(pw_wine_gpu_keep_add(&keep, 0x7f000000, 0x7f004000) == -1 && keep.count == PW_WINE_GPU_KEEP_MAX);
    assert(pw_wine_gpu_keep_piece(&keep, 63 << 16, 64 << 16, &to) == 1 && to == (63 << 16) + 0x4000);
    puts("pw_wine_gpu_keep: ok");
    return 0;
}
