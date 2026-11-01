/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The speed switches on by default (src/pw_speed_defaults.h): an unset one becomes "1"; one the game or the run set,
 * "0" included, stays as it was; the five are the ones the title, the loader, the WSI and the broker client read. */
#define _GNU_SOURCE
#include "../src/pw_speed_defaults.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *get(const char *name) { return getenv(name); }

int main(void)
{
    for (size_t i = 0; i < PW_SPEED_DEFAULTS; ++i) unsetenv(pw_speed_defaults[i]);
    /* No settings at all: every one on. */
    assert(pw_speed_defaults_apply(get, setenv) == PW_SPEED_DEFAULTS);
    for (size_t i = 0; i < PW_SPEED_DEFAULTS; ++i) assert(!strcmp(getenv(pw_speed_defaults[i]), "1"));
    /* Turned off by a game or a run: stays off, the others on. */
    for (size_t i = 0; i < PW_SPEED_DEFAULTS; ++i) unsetenv(pw_speed_defaults[i]);
    setenv("PROTON_120HZ", "0", 1);
    setenv("WINE_PS5_PRESENT_LINK", "0", 1);
    assert(pw_speed_defaults_apply(get, setenv) == PW_SPEED_DEFAULTS - 2);
    assert(!strcmp(getenv("PROTON_120HZ"), "0") && !strcmp(getenv("WINE_PS5_PRESENT_LINK"), "0"));
    assert(!strcmp(getenv("WINE_PS5_NTSYNC"), "1") && !strcmp(getenv("WINE_PS5_HELPER_CPUS_WIDEN"), "1") &&
           !strcmp(getenv("WINE_PS5_ASYNC_PRESENT"), "1"));
    /* Applied again: nothing changes. */
    assert(pw_speed_defaults_apply(get, setenv) == 0);
    /* The names the readers look for, exactly. */
    const char *const expected[] = {"WINE_PS5_NTSYNC", "WINE_PS5_HELPER_CPUS_WIDEN", "WINE_PS5_ASYNC_PRESENT",
                                    "WINE_PS5_PRESENT_LINK", "PROTON_120HZ", "PROTON_GPU_FAST_COPY"};
    assert(PW_SPEED_DEFAULTS == 6);
    for (size_t i = 0; i < PW_SPEED_DEFAULTS; ++i) assert(!strcmp(pw_speed_defaults[i], expected[i]));
    puts("test_pw_speed_defaults: ok");
    return 0;
}
