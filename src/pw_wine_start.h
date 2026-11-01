/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_START_H
#define PW_WINE_START_H
/*
 * In-process start of Wine's Unix side from a PS5 title.
 *
 * The title loads ntdll as a PRX (the firmware does not run its module_start
 * and cannot name it for dladdr), resolves its entry points through the
 * module's PRXDESC1 descriptor, prepares Wine's environment and enters
 * __wine_main on a dedicated thread, since __wine_main never returns. No
 * process is created and nothing is executed from a file system path.
 *
 * Kernel and libc calls are injected so the sequence is host-testable.
 */
#include <stddef.h>
#include <stdint.h>
#include "../wine/ps5/pw_wine_prx.h"
#include "pw_wine_launch.h"

enum {
    PW_WINE_START_MAX_ENV = 32,
    /* wine, the executable and every word a profile's arguments may split into */
    PW_WINE_START_MAX_ARGS = 2 + PW_WINE_LAUNCH_WORDS,
    /* Stages, reported so a failed start names where it stopped. */
    PW_WINE_START_LOAD = 1, PW_WINE_START_MODULE_INFO, PW_WINE_START_DESCRIPTOR,
    PW_WINE_START_ENTRY, PW_WINE_START_MODULE_START, PW_WINE_START_ADOPT,
    PW_WINE_START_ENVIRONMENT, PW_WINE_START_THREAD, PW_WINE_START_RUNNING
};

typedef struct PwWineStartOps {
    int32_t (*load_start)(const char *path, size_t argc, const void *argv, uint32_t flags,
                          const void *option, int *result);
    int (*module_info)(int32_t handle, void *info);
    int (*set_env)(const char *name, const char *value);
    /* Runs entry(arg) on a new thread with at least stack_bytes of stack. */
    int (*start_thread)(void (*entry)(void *), void *arg, size_t stack_bytes);
} PwWineStartOps;

typedef struct PwWineStartEnv { const char *name, *value; } PwWineStartEnv;

typedef struct PwWineStartConfig {
    const char *ntdll_path;          /* the PRX the title loads */
    const char *ntdll_dir;           /* its directory, for WINE_PS5_NTDLL_DIR */
    const char *prefix;              /* WINEPREFIX */
    const PwWineStartEnv *extra_env; /* optional NAME=VALUE pairs, e.g. WINEDEBUG */
    uint32_t extra_env_count;
    int argc;                        /* Wine's argv: argv[0] is the loader name */
    const char *const *argv;
    size_t stack_bytes;
} PwWineStartConfig;

typedef struct PwWineStart {
    int32_t module;
    int stage, status;               /* last stage reached; status of the failing call */
    uint32_t segment_count;
    PwPrxSegment segments[PW_PRX_MAX_SEGMENTS];
    const PwPrxDescriptor *descriptor;
    void (*wine_main)(int, char **);
    unsigned int (*virtual_stats)(uint64_t *out, unsigned int count);
    /* Direct memory and heap counters (wine/ps5/pw_wine_dmem_ps5.c); NULL
     * when ntdll does not export them. */
    unsigned int (*memory_stats)(uint64_t *out, unsigned int count);
    /* The pages that fault most often (Wine patch 0545), four values each:
     * count, page, last PC, info; NULL when ntdll does not export it. */
    unsigned int (*fault_top)(uint64_t *out, unsigned int count);
    int module_start_result;
    void *adopted;                   /* pw_wine_dl_adopt handle, NULL if not exported */
    int argc;
    char *argv[PW_WINE_START_MAX_ARGS + 1];
} PwWineStart;

/* Load ntdll, resolve its entries, run its module_start once and register it
 * with the Unix-module loader. Returns PW_OK or a negative status; start->stage
 * names the step that failed. */
int pw_wine_start_load(PwWineStart *start, const PwWineStartConfig *config,
                       const PwWineStartOps *ops);
/* Set Wine's environment: WINEPREFIX, WINE_PS5_NTDLL_DIR, WINELOADERNOEXEC=1
 * and the extra pairs. */
int pw_wine_start_environment(PwWineStart *start, const PwWineStartConfig *config,
                              const PwWineStartOps *ops);
/* Enter __wine_main(argc, argv) on a dedicated thread. */
int pw_wine_start_run(PwWineStart *start, const PwWineStartConfig *config,
                      const PwWineStartOps *ops);
#endif
