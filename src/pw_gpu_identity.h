/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the console's GPU tells a Windows game it is, for each console model: one table, read where each interface
 * takes its figures from.
 * - Vulkan: the GPU client's RADV (graphics patch 0045, client-identity-winsys.patch) gives the PCI device ID from the
 *   model the broker's owner says at INIT. DXGI (DXVK) and Direct3D 12 (VKD3D-Proton) read Vulkan.
 * - Windows' display adapter (win32u, Wine patch 2408) adopts Vulkan's PCI ID and gives the GPU this name, which Wine
 *   then also reports as the Vulkan device's name.
 * - AMD's AGS (Wine patch 2409): its figures came from libdrm, which the console does not have, and fell back to a
 *   GCN4 GPU with no compute units and no clocks.
 *
 * The truth: an AMD RDNA2-class GPU, which RADV drives as GFX10.3 (PS5_Mesa radv_ps5_winsys.c), with the model's
 * compute units and clocks. The standard PS5's are Sony's published figures. For the Pro, Sony published 60 compute
 * units and 18 Gbps memory (576 GB/s) but no clock; 2,175 MHz is the reported one, which gives its 16.7 TFLOPS.
 *
 * The PCI device ID: the GPU has none a Windows game knows, and RADV reported 0. Games check it. a survival-horror demo refused
 * 1002:0000 with an "Unsupported graphics card" box (2026-11-01; the box sat unseen, which looked like a hang). So each
 * model takes the ID of the retail RDNA2 Radeon of its class, which any game that accepts RDNA2 accepts:
 * - 0x73DF (Navi 22: the RX 6700 and 6700 XT, 36 and 40 compute units) for the standard PS5. It is also the ID Proton
 *   gives a GPU it hides from a game (win32u's fixup_device_id). Proton does the same for Valve's handheld's own IDs,
 *   which games did not know (WINE_HIDE_VANGOGH_GPU: 0x687F).
 * - 0x73BF (Navi 21: the RX 6800, 60 compute units) for the Pro.
 * The name stays the console's own, "AMD Radeon" first, as games that look for a Radeon read it. */
#ifndef PW_GPU_IDENTITY_H
#define PW_GPU_IDENTITY_H

#include <stddef.h>
#include <string.h>

/* The model the owner says (graphics patch 0045, the INIT reply's argument[1]); unknown: the identity stays RADV's. */
enum { PW_GPU_MODEL_UNKNOWN = 0, PW_GPU_MODEL_STANDARD = 1, PW_GPU_MODEL_PRO = 2 };

typedef struct PwGpuIdentity {
    unsigned model;
    unsigned vendor_id, device_id;
    const char *name;
    unsigned compute_units, rops;
    unsigned engine_mhz;        /* the highest engine clock */
    unsigned memory_mhz;        /* the memory clock: its data rate per pin, in Mbps, / 8 */
    unsigned bandwidth_mbs;     /* MB/s: bus width / 8 x data rate */
    float teraflops;            /* FP32, single issue: compute units x 64 lanes x 2 (a multiply-add) x engine clock */
} PwGpuIdentity;

static inline const PwGpuIdentity *pw_gpu_identities(size_t *count)
{
    static const PwGpuIdentity identities[] = {
        {PW_GPU_MODEL_STANDARD, 0x1002, 0x73df, "AMD Radeon PlayStation 5 GPU", 36, 64, 2230, 1750, 448000, 10.28f},
        {PW_GPU_MODEL_PRO, 0x1002, 0x73bf, "AMD Radeon PlayStation 5 Pro GPU", 60, 64, 2175, 2250, 576000, 16.70f},
    };
    *count = sizeof(identities) / sizeof(identities[0]);
    return identities;
}

/* The identity of a model, or NULL (PW_GPU_MODEL_UNKNOWN, or a model not known). */
static inline const PwGpuIdentity *pw_gpu_identity_of_model(unsigned model)
{
    size_t count;
    const PwGpuIdentity *identities = pw_gpu_identities(&count);
    for (size_t i = 0; i < count; i++)
        if (identities[i].model == model) return &identities[i];
    return NULL;
}

/* The identity a GPU reporting this PCI ID and name has: NULL unless both are one of the table's, so a PC's own
 * RX 6700 XT, named as such, is never taken for the console's (name NULL: the PCI ID alone, for win32u, which builds
 * the name from it on the console only). */
static inline const PwGpuIdentity *pw_gpu_identity_of_device(unsigned vendor_id, unsigned device_id, const char *name)
{
    size_t count;
    const PwGpuIdentity *identities = pw_gpu_identities(&count);
    for (size_t i = 0; i < count; i++)
        if (identities[i].vendor_id == vendor_id && identities[i].device_id == device_id &&
            (!name || !strcmp(name, identities[i].name)))
            return &identities[i];
    return NULL;
}

#endif
