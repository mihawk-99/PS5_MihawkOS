/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_wine_start.h"
#include "../include/prospero_win.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A fake ntdll module: segment 0 holds the PRXDESC1 descriptor and its
 * names, segment 1 covers the fake entry points. */
enum { EXPORTS = 6 };
static struct {
    struct { uint64_t magic; uint32_t version, count; PwPrxExport exports[EXPORTS]; } descriptor;
    char names[EXPORTS][32];
} __attribute__((aligned(16))) module_data;

static int calls_module_start, calls_wine_main, calls_adopt, wine_argc;
static char *wine_argv0, *wine_argv1;
static const char *adopt_path;
static int32_t adopt_handle;

static void fake_wine_main(int argc, char **argv)
{
    calls_wine_main++;
    wine_argc = argc;
    wine_argv0 = argv[0];
    wine_argv1 = argv[1];
    assert(argv[argc] == NULL);
}
static int fake_module_start(size_t argc, const void *argv)
{
    (void)argc; (void)argv;
    return ++calls_module_start == 1 ? 0 : -1;
}
static void *fake_adopt(const char *path, int32_t handle)
{
    calls_adopt++;
    adopt_path = path;
    adopt_handle = handle;
    return &module_data;
}
static unsigned int fake_stats(uint64_t *out, unsigned int count)
{
    if (out && count) out[0] = 42;
    return 9;
}
static unsigned int fake_memory_stats(uint64_t *out, unsigned int count)
{
    if (out && count) out[0] = 7;
    return 6;
}
static unsigned int fake_fault_top(uint64_t *out, unsigned int count)
{
    if (out && count >= 4) { out[0] = 33000; out[1] = 0x21100000; out[2] = 0x1234; out[3] = 0x01000302; return 1; }
    return 0;
}

static int32_t load_result = 0x44;
static int info_result = 0;
static int32_t fake_load(const char *path, size_t argc, const void *argv, uint32_t flags,
                         const void *option, int *result)
{
    (void)argc; (void)argv; (void)flags; (void)option; (void)result;
    assert(!strcmp(path, "/app0/win/wine/lib/wine/x86_64-unix/ntdll.prx"));
    return load_result;
}
static int fake_info(int32_t handle, void *info)
{
    uint8_t *raw = info;
    uint64_t zero = 0, address;
    uint32_t count = 2, size, prot = 1;
    uintptr_t low = (uintptr_t)fake_wine_main, high = low;
    const uintptr_t fns[] = { (uintptr_t)fake_module_start, (uintptr_t)fake_adopt,
                              (uintptr_t)fake_stats, (uintptr_t)fake_memory_stats,
                              (uintptr_t)fake_fault_top };

    assert(handle == load_result);
    if (info_result) return info_result;
    for (unsigned i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) { if (fns[i] < low) low = fns[i]; if (fns[i] > high) high = fns[i]; }
    memcpy(raw, &zero, 8);                          /* FW 12.02 clears the size word */
    address = (uint64_t)(uintptr_t)&module_data; size = sizeof(module_data);
    memcpy(raw + 0x108, &address, 8); memcpy(raw + 0x110, &size, 4); memcpy(raw + 0x114, &prot, 4);
    address = low; size = (uint32_t)(high - low + 64);
    memcpy(raw + 0x118, &address, 8); memcpy(raw + 0x120, &size, 4); memcpy(raw + 0x124, &prot, 4);
    memcpy(raw + 0x148, &count, 4);
    return 0;
}

static char env_log[512];
static int env_fail_on;
static int fake_env(const char *name, const char *value)
{
    if (env_fail_on && !strcmp(name, "WINEDEBUG")) return -7;
    strcat(env_log, name); strcat(env_log, "="); strcat(env_log, value); strcat(env_log, ";");
    return 0;
}

static void (*thread_entry)(void *);
static void *thread_arg;
static size_t thread_stack;
static int fake_thread(void (*entry)(void *), void *arg, size_t stack)
{
    thread_entry = entry; thread_arg = arg; thread_stack = stack;
    return 0;
}

static void build_module(int with_optional)
{
    static const char *const names[EXPORTS] = { "__wine_main", "module_start",
                                                "pw_wine_dl_adopt", "__wine_virtual_stats",
                                                "__wine_ps5_memory_stats", "__wine_virtual_fault_top" };
    const void *addresses[EXPORTS] = { (const void *)fake_wine_main, (const void *)fake_module_start,
                                       (const void *)fake_adopt, (const void *)fake_stats,
                                       (const void *)fake_memory_stats, (const void *)fake_fault_top };

    memset(&module_data, 0, sizeof(module_data));
    module_data.descriptor.magic = PW_PRX_MAGIC;
    module_data.descriptor.version = PW_PRX_VERSION;
    module_data.descriptor.count = with_optional ? EXPORTS : 1;
    for (unsigned i = 0; i < EXPORTS; i++) {
        strcpy(module_data.names[i], names[i]);
        module_data.descriptor.exports[i].name = module_data.names[i];
        module_data.descriptor.exports[i].address = addresses[i];
    }
}

int main(void)
{
    static const PwWineStartEnv extra[] = { { "WINEDEBUG", "err+all" } };
    static const char *const argv[] = { "wine", "C:\\Games\\Pinball\\PINBALL.EXE" };
    const PwWineStartOps ops = { fake_load, fake_info, fake_env, fake_thread };
    PwWineStartConfig config = {
        .ntdll_path = "/app0/win/wine/lib/wine/x86_64-unix/ntdll.prx",
        .ntdll_dir = "/app0/win/wine/lib/wine/x86_64-unix",
        .prefix = "/data/prospero-win/prefixes/pinball",
        .extra_env = extra, .extra_env_count = 1,
        .argc = 2, .argv = argv, .stack_bytes = 16u << 20,
    };
    PwWineStart start;
    uint64_t stats[1] = { 0 };

    /* Full sequence: load, descriptor (with the firmware's zeroed size word),
     * module_start once, adopt, environment, thread, __wine_main. */
    build_module(1);
    assert(pw_wine_start_load(&start, &config, &ops) == PW_OK);
    assert(start.module == 0x44 && start.segment_count == 2 && start.descriptor);
    assert(calls_module_start == 1 && calls_adopt == 1 && adopt_handle == 0x44 &&
           !strcmp(adopt_path, config.ntdll_path) && start.adopted);
    assert(start.virtual_stats && start.virtual_stats(stats, 1) == 9 && stats[0] == 42);
    assert(start.memory_stats && start.memory_stats(stats, 1) == 6 && stats[0] == 7);
    {
        uint64_t top[4];
        assert(start.fault_top && start.fault_top(top, 4) == 1 && top[0] == 33000 && top[3] == 0x01000302);
    }
    assert(pw_wine_start_environment(&start, &config, &ops) == PW_OK);
    assert(!strcmp(env_log, "WINEPREFIX=/data/prospero-win/prefixes/pinball;"
                            "WINE_PS5_NTDLL_DIR=/app0/win/wine/lib/wine/x86_64-unix;"
                            "WINELOADERNOEXEC=1;WINEDEBUG=err+all;"));
    assert(pw_wine_start_run(&start, &config, &ops) == PW_OK);
    assert(thread_entry && thread_arg == &start && thread_stack == (16u << 20));
    assert(calls_wine_main == 0 && start.stage == PW_WINE_START_THREAD);
    thread_entry(thread_arg);
    assert(calls_wine_main == 1 && wine_argc == 2 && !strcmp(wine_argv0, "wine") &&
           !strcmp(wine_argv1, argv[1]) && start.stage == PW_WINE_START_RUNNING);

    /* The title gives a game 17 settings of its own (OPENSSL_ia32cap was the 17th): up to PW_WINE_START_MAX_ENV go through. */
    {
        static PwWineStartEnv many[PW_WINE_START_MAX_ENV + 1];
        PwWineStartConfig big = config;
        for (unsigned i = 0; i < PW_WINE_START_MAX_ENV + 1; i++) many[i] = (PwWineStartEnv){ "K", "v" };
        big.extra_env = many;
        big.extra_env_count = 17;
        env_log[0] = 0;
        assert(PW_WINE_START_MAX_ENV >= 17 && pw_wine_start_environment(&start, &big, &ops) == PW_OK);
        big.extra_env_count = PW_WINE_START_MAX_ENV;
        env_log[0] = 0;
        assert(pw_wine_start_environment(&start, &big, &ops) == PW_OK);
        big.extra_env_count = PW_WINE_START_MAX_ENV + 1;
        env_log[0] = 0;
        assert(pw_wine_start_environment(&start, &big, &ops) != PW_OK);
    }

    /* Only __wine_main exported: optional entries are skipped. */
    calls_module_start = calls_adopt = 0;
    build_module(0);
    assert(pw_wine_start_load(&start, &config, &ops) == PW_OK);
    assert(calls_module_start == 0 && calls_adopt == 0 && !start.adopted && !start.virtual_stats &&
           !start.memory_stats);

    /* Failures report the stage they stopped at. */
    load_result = -5;
    assert(pw_wine_start_load(&start, &config, &ops) < 0 && start.stage == PW_WINE_START_LOAD);
    load_result = 0x44;
    info_result = -3;
    assert(pw_wine_start_load(&start, &config, &ops) < 0 && start.stage == PW_WINE_START_MODULE_INFO);
    info_result = 0;
    build_module(1);
    module_data.descriptor.magic = 0;
    assert(pw_wine_start_load(&start, &config, &ops) < 0 && start.stage == PW_WINE_START_DESCRIPTOR);
    build_module(1);
    strcpy(module_data.names[0], "not_wine_main");
    assert(pw_wine_start_load(&start, &config, &ops) < 0 && start.stage == PW_WINE_START_ENTRY);
    build_module(1);
    calls_module_start = 1;              /* the fake entry now fails */
    assert(pw_wine_start_load(&start, &config, &ops) < 0 && start.stage == PW_WINE_START_MODULE_START);

    env_fail_on = 1;
    env_log[0] = 0;
    assert(pw_wine_start_environment(&start, &config, &ops) == -7 &&
           start.stage == PW_WINE_START_ENVIRONMENT);
    config.argc = 1;
    assert(pw_wine_start_run(&start, &config, &ops) == PW_ERR_PRECONDITION);
    /* A profile's longest argument list fits; one word more is refused. */
    _Static_assert(PW_WINE_START_MAX_ARGS == 2 + PW_WINE_LAUNCH_WORDS, "argument limits agree");
    config.argc = PW_WINE_START_MAX_ARGS + 1;
    assert(pw_wine_start_run(&start, &config, &ops) == PW_ERR_PRECONDITION);

    printf("wine start passed: load, zeroed size word, descriptor, module_start once, adopt, "
           "environment, thread entry and staged failures\n");
    return 0;
}
