/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_PARKED_H
#define PW_PARKED_H
/* The catalog's parked profiles: their code stays in the repository, but nothing a person sees lists or opens them.
 *
 * Aurelia was parked on 2026-10-24 (examples/ps5proton/ps5proton.cpp). A console that had used it still holds its
 * games/aurelia entry, and the launcher showed it as a tile (the owner, 2026-11-01: "it should be parked in repo and never
 * shown"). proton_read_games (ps5/src/proton_launch.cpp) skips these ids. The library, the tile grid, Files and the WebUI
 * all list games through it. The console's folder is left as it is. Host test: tests/test_pw_parked.c. */
#include <string.h>

static inline int pw_parked_profile(const char *id)
{
    static const char *const parked[] = {"aurelia"};
    for (size_t i = 0; id && i < sizeof(parked) / sizeof(parked[0]); i++)
        if (!strcmp(id, parked[i])) return 1;
    return 0;
}
#endif
