/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The host-visible GPU memory each program may hold (the GPU broker's host limit), which is where a Direct3D 12 or
 * Vulkan program's UPLOAD and READBACK heaps stop and what it reads as its shared video-memory budget. It comes from the
 * application's direct memory, the pool device-local memory comes from (docs/ARCHITECTURE.md), not flexible memory.
 * - helpers: 1 GiB, as since 2026-10-07; a game in a helper maps its buffers with the system's page tables too.
 * - the game in the title: 2 GiB, which the broker grants only when the direct memory left beside its device-local
 *   offer can hold the second GiB (graphics patch 0002); its mappings cost the title's own page tables. A load probe in
 *   the title made 960 MiB of upload buffers at 1 GiB and 1984 MiB at 2 GiB, with device-local room to spare
 *   (tests/d3d12-load-probe.c).
 * PROTON_GPU_HOST_MIB (a game.json may set it) sets both, up to the broker's 4096 MiB. Pure (tests/test_pw_gpu_host_limit.c).
 */
#ifndef PW_GPU_HOST_LIMIT_H
#define PW_GPU_HOST_LIMIT_H

#include <stdint.h>
#include <stdlib.h>

#define PW_GPU_HOST_HELPER_MIB 1024u
#define PW_GPU_HOST_TITLE_MIB 2048u
#define PW_GPU_HOST_MAX_MIB 4096u   /* the broker's PW_GPU_HOST_LIMIT_MAX */

/* The limits in bytes for helpers and for the game in the title, from PROTON_GPU_HOST_MIB's text (NULL or empty: the
 * defaults; a number above the maximum: the maximum; 0 or anything else not a number: the defaults). */
static inline void pw_gpu_host_limits(const char *setting, uint64_t *helpers, uint64_t *title)
{
    char *end = NULL;
    const unsigned long long mib = setting && *setting ? strtoull(setting, &end, 10) : 0;
    if (mib && end && !*end) {
        *helpers = *title = (uint64_t)(mib < PW_GPU_HOST_MAX_MIB ? mib : PW_GPU_HOST_MAX_MIB) << 20;
        return;
    }
    *helpers = (uint64_t)PW_GPU_HOST_HELPER_MIB << 20;
    *title = (uint64_t)PW_GPU_HOST_TITLE_MIB << 20;
}

#endif /* PW_GPU_HOST_LIMIT_H */
