/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_resolution.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *const words[PW_RESOLUTION_KINDS] = {"1080p", "1440p", "4k"};
static const char *const names[PW_RESOLUTION_KINDS] = {"1080p (1920x1080)", "1440p (2560x1440)", "4K (3840x2160)"};
static const char *const badges[PW_RESOLUTION_KINDS] = {"1080P", "1440P", "4K"};
static const char *const desktops[PW_RESOLUTION_KINDS] = {"1920x1080", "2560x1440", "3840x2160"};
static const unsigned dims[PW_RESOLUTION_KINDS][2] = {{1920u, 1080u}, {2560u, 1440u}, {3840u, 2160u}};

int pw_resolution_kind(const char *word)
{
    if (!word || !*word) return -1;
    if (!strcmp(word, "1080p") || !strcmp(word, "1080P") || !strcmp(word, "1920x1080")) return PW_RESOLUTION_1080P;
    if (!strcmp(word, "1440p") || !strcmp(word, "1440P") || !strcmp(word, "2560x1440")) return PW_RESOLUTION_1440P;
    if (!strcmp(word, "4k") || !strcmp(word, "4K") || !strcmp(word, "2160p") || !strcmp(word, "2160P") ||
        !strcmp(word, "3840x2160"))
        return PW_RESOLUTION_4K;
    return -1;
}

const char *pw_resolution_word(int kind)
{
    return kind >= 0 && kind < PW_RESOLUTION_KINDS ? words[kind] : words[PW_RESOLUTION_1080P];
}

const char *pw_resolution_name(int kind)
{
    return kind >= 0 && kind < PW_RESOLUTION_KINDS ? names[kind] : names[PW_RESOLUTION_1080P];
}

const char *pw_resolution_badge(int kind)
{
    return kind >= 0 && kind < PW_RESOLUTION_KINDS ? badges[kind] : badges[PW_RESOLUTION_1080P];
}

const char *pw_resolution_desktop(int kind)
{
    return kind >= 0 && kind < PW_RESOLUTION_KINDS ? desktops[kind] : desktops[PW_RESOLUTION_1080P];
}

void pw_resolution_dimensions(int kind, unsigned *width, unsigned *height)
{
    const int k = kind >= 0 && kind < PW_RESOLUTION_KINDS ? kind : PW_RESOLUTION_1080P;
    if (width) *width = dims[k][0];
    if (height) *height = dims[k][1];
}

int pw_resolution_load(int *kind, const char *path)
{
    char key[32], value[32], extra[32];
    int error = 0, parsed = -1;
    *kind = PW_RESOLUTION_1080P;
    FILE *file = fopen(path, "r");
    if (!file) return errno;
    if (fscanf(file, "%31s %31s", key, value) != 2 || strcmp(key, "resolution") ||
        (parsed = pw_resolution_kind(value)) < 0 || fscanf(file, "%31s", extra) == 1)
        error = EINVAL;
    if (!error && ferror(file)) error = EIO;
    fclose(file);
    if (!error) *kind = parsed;
    return error;
}

int pw_resolution_save(int kind, const char *path)
{
    if (kind < 0 || kind >= PW_RESOLUTION_KINDS) return EINVAL;
    char temporary[1024];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return ENAMETOOLONG;
    FILE *file = fopen(temporary, "w");
    if (!file) return errno;
    int error = 0;
    if (fprintf(file, "resolution %s\n", words[kind]) < 0) error = EIO;
    if (fflush(file) || fsync(fileno(file))) error = errno;
    if (fclose(file) && !error) error = errno;
    if (!error && rename(temporary, path)) error = errno;
    if (error) unlink(temporary);
    return error;
}
