/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for the payload SDK's <ps5platform/kernel.h> (the declarations the GPU client uses), so a host test can
 * compile .deps/graphics-foundation/tooling/gpu-broker/client.c against tests/fake_ps5_kernel.c. */
#ifndef PS5PLATFORM_KERNEL_H
#define PS5PLATFORM_KERNEL_H
#include <stddef.h>
#include <stdint.h>
#define PS5_KERNEL_MAP_FIXED 0x10
#define PS5_KERNEL_PROT_CPU_READ 0x01
#define PS5_KERNEL_PROT_CPU_WRITE 0x02
#define PS5_KERNEL_PROT_GPU_READ 0x10
#define PS5_KERNEL_PROT_GPU_WRITE 0x20
int32_t sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags, int64_t direct_start,
                                 size_t alignment);
int32_t sceKernelReserveVirtualRange(void **address, size_t length, int flags, size_t alignment);
int32_t sceKernelMunmap(void *address, size_t length);
int32_t sceKernelMprotect(const void *address, size_t length, int protection);
#endif
