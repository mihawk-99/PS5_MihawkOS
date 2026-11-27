/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sony local-process Wine helper (/app0/wine-local-child.bin): spawned by the
 * title through sceSystemServiceAddLocalProcess so it inherits the title's
 * application container (65,536 CPU page tables, 1 GiB flexible memory, and
 * direct memory) while staying an isolated OS process whose resources are
 * completely reclaimed on exit or crash.
 *
 * fd 3 is the SOCK_SEQPACKET control endpoint passed in SpawnOptions.fd.
 * Before reading the Wine bootstrap, this process elevates through the
 * resident Lapy daemon (granting /data access and JIT shared memory), sends a
 * pw_local_hello packet to the title, then runs the shared proton_wine_child.c
 * loop. A Sony local process never calls exit(), _exit(), or returns from
 * _start; after flushing its log it closes fd 3 and waits for the title to
 * close it via sceSystemServiceKillLocalProcess. */
#define PROTON_LOCAL_CHILD 1
#define PROTON_CHILD_GPU_BROKER 1

#include "pw_local_child.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ps5platform/elevation.h>

extern void _init_env(void *);
extern int sceKernelUsleep(unsigned);
extern int64_t sceKernelGetDirectMemorySize(void);
extern int sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
extern int sceKernelGetPageTableStats(int *, int *, int *, int *);

static int tty_fstat_status = -1, tty_fstat_error, tty_stdin_null, tty_stdout_null, tty_stderr_null, tty_error;
static unsigned long long tty_dev, tty_rdev;
static unsigned tty_mode;

static int carrier_refused = -1, carrier_errno;
static uintptr_t carrier_end;
static void carrier_release(void) { }

/* A Sony local process lives in the application container, so ordinary
 * sceKernelAllocateDirectMemory takes pages straight from the title's direct
 * memory pool without importing libkernel_sys. */
static int payload_allocate_direct(size_t bytes, size_t alignment, int type, int64_t *offset)
{
    return sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes, alignment, type, offset);
}

#include "proton_wine_child.c"

static int payload_prepare_module(int32_t handle)
{
    (void)handle;
    return 0;
}

static int local_elevate(void)
{
    static const struct ps5_elevation_config grant = {
        "/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 40, 50000
    };
    int status = EPERM;
    for (int attempt = 0; attempt < 3 && geteuid() != 0; ++attempt) {
        status = ps5_elevation_request_with(&grant, PS5_ELEVATION_FILESYSTEM);
        if (geteuid() == 0) return 0;
        /* Keep credentials private (cred_refs == 2) while Lapy polls /download0/elevate_proc.
         * If Lapy hit a transient cred_refs=3 race during verify_data(), retrying once clears it. */
        (void)seteuid(geteuid());
        for (int wait = 0; wait < 75 && geteuid() != 0; ++wait) sceKernelUsleep(20000);
    }
    return geteuid() == 0 ? 0 : (status ? status : EPERM);
}

__attribute__((noreturn)) void _start(void *parameters, void *teardown)
{
    (void)teardown;
    _init_env(parameters);
    signal(SIGPIPE, SIG_IGN);
    const int elev_status = local_elevate();
    struct pw_local_hello hello = {
        PW_LOCAL_HELLO_MAGIC,
        (int32_t)getpid(),
        elev_status,
        (int32_t)geteuid(),
    };
    (void)send(3, &hello, sizeof(hello), MSG_NOSIGNAL);
    if (elev_status == 0) {
        start_main_ms = payload_monotonic_ms();
        start_control_ms = start_main_ms;
        wine_child_run();
        char line[96];
        snprintf(line, sizeof(line), "wine-child: local-process ending monotonic_ms=%lld\n", payload_monotonic_ms());
        stage(line);
        if (output_fd >= 0) fsync(output_fd);
    }
    /* Close fd 3 so the title's monitor observes completion and closes this
     * local process via sceSystemServiceKillLocalProcess. Never exit or return. */
    close(3);
    for (;;) sceKernelUsleep(100000);
}
