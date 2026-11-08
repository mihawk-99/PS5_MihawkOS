/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wallpaper.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const PwWallpaper catalog[] = {
    {"bubble-city", "Bubble City", "Skeuoss"},
    {"bubble-earth", "Bubble Earth", "MihawkOS"},
    {"glass-city", "Glass City", "MihawkOS"},
    {"aqua-world", "Aqua World", "MihawkOS"},
};

const PwWallpaper *pw_wallpaper_catalog(size_t *count)
{
    if (count) *count = sizeof(catalog) / sizeof(catalog[0]);
    return catalog;
}

int pw_wallpaper_id_valid(const char *id)
{
    size_t length = 0;
    if (!id || !*id || id[0] == '.') return 0;
    for (; id[length]; ++length) {
        const char c = id[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return length <= PW_WALLPAPER_ID_MAX;
}

int pw_wallpaper_load(char *id, size_t cap, const char *path)
{
    char key[32], value[PW_WALLPAPER_ID_MAX + 8], extra[32];
    int error = 0;
    snprintf(id, cap, "%s", PW_WALLPAPER_DEFAULT);
    FILE *file = fopen(path, "r");
    if (!file) return errno;
    if (fscanf(file, "%31s %71s", key, value) != 2 || strcmp(key, "wallpaper") || !pw_wallpaper_id_valid(value) ||
        strlen(value) >= cap || fscanf(file, "%31s", extra) == 1)
        error = EINVAL;
    if (!error && ferror(file)) error = EIO;
    fclose(file);
    if (!error) snprintf(id, cap, "%s", value);
    return error;
}

int pw_wallpaper_save(const char *id, const char *path)
{
    char temporary[1024];
    if (!pw_wallpaper_id_valid(id)) return EINVAL;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return ENAMETOOLONG;
    FILE *file = fopen(temporary, "w");
    if (!file) return errno;
    int error = 0;
    if (fprintf(file, "wallpaper %s\n", id) < 0) error = EIO;
    if (fflush(file) || fsync(fileno(file))) error = errno;
    if (fclose(file) && !error) error = errno;
    if (!error && rename(temporary, path)) error = errno;
    if (error) unlink(temporary);
    return error;
}

/* 1 when both files hold the same bytes. */
static int same_bytes(FILE *a, FILE *b)
{
    unsigned char left[16384], right[16384];
    for (;;) {
        const size_t n = fread(left, 1, sizeof(left), a), m = fread(right, 1, sizeof(right), b);
        if (n != m || memcmp(left, right, n)) return 0;
        if (!n) return !ferror(a) && !ferror(b);
    }
}

int pw_wallpaper_apply(const char *folder, const char *shipped, const char *id, int *changed)
{
    char source[1024], target[1024], temporary[1032];
    int error = 0;
    *changed = 0;
    if (!pw_wallpaper_id_valid(id)) return EINVAL;
    if (snprintf(source, sizeof(source), "%s/%s.bmp", folder, id) >= (int)sizeof(source) ||
        snprintf(target, sizeof(target), "%s/" PW_WALLPAPER_TARGET, folder) >= (int)sizeof(target) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", target) >= (int)sizeof(temporary))
        return ENAMETOOLONG;
    FILE *from = fopen(source, "rb");
    if (!from && shipped) {
        if (snprintf(source, sizeof(source), "%s/%s.bmp", shipped, id) >= (int)sizeof(source)) return ENAMETOOLONG;
        from = fopen(source, "rb");
    }
    if (!from) return errno;
    FILE *current = fopen(target, "rb");
    if (current) {
        const int same = same_bytes(from, current);
        fclose(current);
        if (same) return fclose(from), 0;
        rewind(from);
    }
    FILE *to = fopen(temporary, "wb");
    if (!to) return error = errno, fclose(from), error;
    unsigned char block[16384];
    for (size_t n; (n = fread(block, 1, sizeof(block), from)) > 0;)
        if (fwrite(block, 1, n, to) != n) { error = EIO; break; }
    if (ferror(from)) error = EIO;
    fclose(from);
    if (!error && (fflush(to) || fsync(fileno(to)))) error = errno;
    if (fclose(to) && !error) error = errno;
    if (!error && rename(temporary, target)) error = errno;
    if (error) unlink(temporary);
    else *changed = 1;
    return error;
}
