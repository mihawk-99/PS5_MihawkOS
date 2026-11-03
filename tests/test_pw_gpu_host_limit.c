/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GPU broker's host-visible limits the title sets (src/pw_gpu_host_limit.h): the defaults, the override, the maximum. */
#include "../src/pw_gpu_host_limit.h"
#include <assert.h>
#include <stdio.h>

static void limits(const char *setting, uint64_t helpers_mib, uint64_t title_mib)
{
    uint64_t helpers = 0, title = 0;
    pw_gpu_host_limits(setting, &helpers, &title);
    assert(helpers == helpers_mib << 20 && title == title_mib << 20);
}

int main(void)
{
    limits(NULL, 1024, 2048);
    limits("", 1024, 2048);
    limits("1536", 1536, 1536);   /* the override sets both */
    limits("4096", 4096, 4096);
    limits("8192", 4096, 4096);   /* the broker's maximum */
    limits("0", 1024, 2048);      /* not a limit: the defaults */
    limits("2g", 1024, 2048);
    printf("gpu host limit passed: 1 GiB for helpers and 2 GiB for the game in the title by default, PROTON_GPU_HOST_MIB "
           "for both up to 4 GiB\n");
    return 0;
}
