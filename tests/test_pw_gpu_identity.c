/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The console GPU's identity for each model (src/pw_gpu_identity.h): what Vulkan, DXGI, Windows' adapter and AGS tell a
 * game, the same from every one of them. */
#include "../src/pw_gpu_identity.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const PwGpuIdentity *standard = pw_gpu_identity_of_model(PW_GPU_MODEL_STANDARD);
    const PwGpuIdentity *pro = pw_gpu_identity_of_model(PW_GPU_MODEL_PRO);
    size_t count;
    assert(standard && pro && standard != pro);
    assert(!pw_gpu_identity_of_model(PW_GPU_MODEL_UNKNOWN) && !pw_gpu_identity_of_model(3));

    /* AMD, never the 0 RADV reported (a survival-horror demo refused 1002:0000), and a retail RDNA2 Radeon's ID per model */
    assert(standard->vendor_id == 0x1002 && standard->device_id == 0x73df);
    assert(pro->vendor_id == 0x1002 && pro->device_id == 0x73bf);
    assert(!strncmp(standard->name, "AMD Radeon ", 11) && !strncmp(pro->name, "AMD Radeon ", 11));
    assert(strcmp(standard->name, pro->name));

    /* each entry's figures agree with themselves: FP32 TFLOPS from its units and clock, bandwidth from 256 bits */
    const PwGpuIdentity *identities = pw_gpu_identities(&count);
    assert(count == 2);
    for (size_t i = 0; i < count; i++) {
        const PwGpuIdentity *identity = &identities[i];
        const double tflops = identity->compute_units * 64.0 * 2.0 * identity->engine_mhz / 1e6;
        assert(tflops - identity->teraflops < 0.05 && identity->teraflops - tflops < 0.05);
        assert(identity->bandwidth_mbs == 256 / 8 * identity->memory_mhz * 8);
        assert(identity->rops && identity->compute_units % 2 == 0);    /* RDNA's work-group processors hold 2 */
    }
    assert(pro->compute_units == 60 && standard->compute_units == 36);

    /* by PCI ID and name: only the table's pairs, so a PC's own RX 6700 XT is not the console's */
    assert(pw_gpu_identity_of_device(0x1002, 0x73df, standard->name) == standard);
    assert(pw_gpu_identity_of_device(0x1002, 0x73bf, pro->name) == pro);
    assert(!pw_gpu_identity_of_device(0x1002, 0x73df, "AMD Radeon RX 6700 XT"));
    assert(!pw_gpu_identity_of_device(0x1002, 0x73df, pro->name));
    assert(!pw_gpu_identity_of_device(0x1002, 0x0000, standard->name));
    assert(!pw_gpu_identity_of_device(0x10de, 0x73df, NULL));
    /* the PCI ID alone (win32u, on the console only) */
    assert(pw_gpu_identity_of_device(0x1002, 0x73df, NULL) == standard);
    assert(pw_gpu_identity_of_device(0x1002, 0x73bf, NULL) == pro);
    puts("pw_gpu_identity: ok");
    return 0;
}
