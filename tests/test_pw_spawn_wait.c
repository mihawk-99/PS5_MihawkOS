/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A helper start in flight keeps the title's end waiting for 30 s, not for ever (wine/ps5/pw_spawn_wait.h). */
#include "../wine/ps5/pw_spawn_wait.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(!pw_spawn_wait_holds(0, 0, 5000));               /* nothing in flight */
    assert(pw_spawn_wait_holds(1, 1000, 1000));             /* just asked */
    assert(pw_spawn_wait_holds(1, 1000, 30999));            /* 29.999 s */
    assert(!pw_spawn_wait_holds(1, 1000, 31000));           /* 30 s: let go (the overlay of 2026-11-01) */
    assert(!pw_spawn_wait_holds(3, 1000, 600000));          /* still let go ten minutes on */
    assert(pw_spawn_wait_holds(1, 0, 600000));              /* no start time recorded: waited for, as before */
    assert(pw_spawn_wait_holds(1, 9000, 8000));             /* a clock that went back: waited for */
    puts("pw_spawn_wait: ok");
    return 0;
}
