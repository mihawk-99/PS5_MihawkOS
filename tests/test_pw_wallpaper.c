/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The wallpaper setting (src/pw_wallpaper.h): read from a hand-written file, written and read back, and applied to
 * proton.bmp without touching it when the picture is missing or already there. */
#include "../src/pw_wallpaper.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
}
static int same(const char *path, const char *text)
{
    char got[256] = {0};
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    const size_t n = fread(got, 1, sizeof(got) - 1, file);
    fclose(file);
    return n == strlen(text) && !memcmp(got, text, n);
}

int main(void)
{
    char folder[] = "/tmp/proton-wallpaper-XXXXXX", path[256], bmp[300], id[PW_WALLPAPER_ID_MAX + 1];
    size_t count = 0;
    int changed = 9;
    assert(mkdtemp(folder));
    snprintf(path, sizeof(path), "%s/wallpaper.txt", folder);

    const PwWallpaper *catalog = pw_wallpaper_catalog(&count);
    assert(count >= 2 && !strcmp(catalog[0].id, PW_WALLPAPER_DEFAULT) && !strcmp(catalog[0].credit, "Skeuoss"));
    for (size_t i = 0; i < count; ++i) assert(pw_wallpaper_id_valid(catalog[i].id) && catalog[i].name[0]);

    /* a missing file is the default; so is a damaged one, with the error said */
    assert(pw_wallpaper_load(id, sizeof(id), path) == ENOENT && !strcmp(id, PW_WALLPAPER_DEFAULT));
    /* hand-written: CRLF, no final newline, extra spaces */
    put(path, "wallpaper   glass-city\r\n");
    assert(!pw_wallpaper_load(id, sizeof(id), path) && !strcmp(id, "glass-city"));
    put(path, "wallpaper my-picture.v2");
    assert(!pw_wallpaper_load(id, sizeof(id), path) && !strcmp(id, "my-picture.v2"));
    const char *bad[] = {"wallpaper\n", "wall aqua-world\n", "wallpaper ../etc/passwd\n", "wallpaper a/b\n", "wallpaper .hidden\n",
                         "wallpaper aqua-world extra\n", "wallpaper \n", ""};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        put(path, bad[i]);
        assert(pw_wallpaper_load(id, sizeof(id), path) == EINVAL && !strcmp(id, PW_WALLPAPER_DEFAULT));
    }
    char longest[PW_WALLPAPER_ID_MAX + 2];
    memset(longest, 'a', sizeof(longest) - 1), longest[sizeof(longest) - 1] = 0;
    assert(!pw_wallpaper_id_valid(longest) && pw_wallpaper_save(longest, path) == EINVAL);

    /* save and read back; an invalid id is refused and leaves the file */
    assert(!pw_wallpaper_save("aqua-world", path) && !pw_wallpaper_load(id, sizeof(id), path) && !strcmp(id, "aqua-world"));
    assert(pw_wallpaper_save("../x", path) == EINVAL && !pw_wallpaper_load(id, sizeof(id), path) && !strcmp(id, "aqua-world"));
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    assert(access(tmp, F_OK) && !pw_wallpaper_id_valid("") && !pw_wallpaper_id_valid(NULL));

    /* apply: a missing picture leaves proton.bmp; a present one replaces it once, then reads "unchanged" */
    snprintf(bmp, sizeof(bmp), "%s/proton.bmp", folder);
    put(bmp, "default picture");
    assert(pw_wallpaper_apply(folder, NULL, "glass-city", &changed) == ENOENT && !changed && same(bmp, "default picture"));
    assert(pw_wallpaper_apply(folder, NULL, "../glass-city", &changed) == EINVAL && !changed);
    snprintf(tmp, sizeof(tmp), "%s/glass-city.bmp", folder);
    put(tmp, "glass city picture");
    assert(!pw_wallpaper_apply(folder, NULL, "glass-city", &changed) && changed == 1 && same(bmp, "glass city picture"));
    assert(!pw_wallpaper_apply(folder, NULL, "glass-city", &changed) && changed == 0 && same(bmp, "glass city picture"));
    snprintf(tmp, sizeof(tmp), "%s/proton.bmp.tmp", folder);
    assert(access(tmp, F_OK));
    /* the runtime's copy is used when the prefix has none, and the person's own file wins over it */
    {
        char shipped[256], own[256];
        snprintf(shipped, sizeof(shipped), "%s/shipped", folder);
        assert(!mkdir(shipped, 0700));
        snprintf(tmp, sizeof(tmp), "%s/aqua-world.bmp", shipped);
        put(tmp, "shipped aqua");
        assert(!pw_wallpaper_apply(folder, shipped, "aqua-world", &changed) && changed == 1 && same(bmp, "shipped aqua"));
        snprintf(own, sizeof(own), "%s/aqua-world.bmp", folder);
        put(own, "my own aqua");
        assert(!pw_wallpaper_apply(folder, shipped, "aqua-world", &changed) && changed == 1 && same(bmp, "my own aqua"));
        assert(pw_wallpaper_apply(folder, shipped, "no-such-picture", &changed) == ENOENT && changed == 0 && same(bmp, "my own aqua"));
    }
    /* a picture of any size, and a person's own file name */
    snprintf(tmp, sizeof(tmp), "%s/my-picture.v2.bmp", folder);
    char *large = malloc(100000);
    memset(large, 'x', 99999), large[99999] = 0;
    put(tmp, large);
    assert(!pw_wallpaper_apply(folder, NULL, "my-picture.v2", &changed) && changed == 1);
    free(large);
    FILE *written = fopen(bmp, "rb");
    fseek(written, 0, SEEK_END);
    assert(ftell(written) == 99999);
    fclose(written);
    printf("wallpaper: %zu pictures, hand-written file, round trip, apply\n", count);
    return 0;
}
