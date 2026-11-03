/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pw_wine_dl asks the loader only for a module file that is there: a builtin DLL without a Unix side costs a stat,
 * not two loader tries (installer-dxsetup-2026-10-29-163636: 18 misses per process, 7.5 ms each). */
#include "../wine/ps5/pw_wine_dl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int loads;
static char last_load[PW_WINE_DL_MAX_PATH], last_report[PW_WINE_DL_REPORT];

static int32_t fake_load_start(const char *path, size_t argc, const void *argv, uint32_t flags, const void *option,
                               int *result)
{
    (void)argc; (void)argv; (void)flags; (void)option; (void)result;
    loads++;
    snprintf(last_load, sizeof(last_load), "%s", path);
    return (int32_t)0x80020002u;   /* refused, as the console answers a module it will not load */
}
static void fake_report(const char *line) { snprintf(last_report, sizeof(last_report), "%s", line); }

static void *fake_map(size_t bytes) { (void)bytes; return NULL; }
static void fake_unmap(void *address, size_t bytes) { (void)address; (void)bytes; }
static void *(*handed_map)(size_t);
static void (*handed_unmap)(void *, size_t);
static int area_ready = 1;
static void fake_set_backing(void *(*map)(size_t), void (*unmap)(void *, size_t)) { handed_map = map; handed_unmap = unmap; }
static int fake_backing(void *(**map)(size_t), void (**unmap)(void *, size_t))
{
    if (!area_ready) return -1;
    *map = fake_map;
    *unmap = fake_unmap;
    return 0;
}

int main(void)
{
    char dir[] = "/tmp/pw-wine-dl-XXXXXX", present[64];
    assert(mkdtemp(dir));
    snprintf(present, sizeof(present), "%s/present.prx", dir);
    FILE *file = fopen(present, "w");
    assert(file && fclose(file) == 0);
    const PwWineDlOps ops = {.load_start = fake_load_start, .report = fake_report};
    pw_wine_dl_configure(&ops, dir);

    /* Absent: no loader call, and the failure line still names what was tried. */
    assert(!pw_wine_dl_open("absent.so"));
    assert(loads == 0);
    assert(strstr(last_report, "absent.prx") && strstr(last_report, "not found"));
    /* Present: the loader is asked, once, for that file. */
    assert(!pw_wine_dl_open("present.so"));
    assert(loads == 1 && !strcmp(last_load, present));
    /* A full path to a missing module: none either. */
    assert(!pw_wine_dl_open("/nowhere/x86_64-unix/kernel32.so") && loads == 1);

    /* A module with a heap of its own (the in-process server) gets the process's backing before it runs, once the
     * backing exists; one without a heap, or before the area exists, gets nothing. */
    assert(pw_wine_dl_hand_heap(fake_set_backing, fake_backing) == 1 && handed_map == fake_map && handed_unmap == fake_unmap);
    handed_map = NULL;
    assert(pw_wine_dl_hand_heap(NULL, fake_backing) == 0);
    assert(pw_wine_dl_hand_heap(fake_set_backing, NULL) == 0 && !handed_map);
    area_ready = 0;
    assert(pw_wine_dl_hand_heap(fake_set_backing, fake_backing) == 0 && !handed_map);

    unlink(present);
    rmdir(dir);
    printf("pw_wine_dl: absent modules cost no loader call; present ones are asked once; a module's own heap gets the backing\n");
    return 0;
}
