/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_drive_browse.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

int pw_drive_part(const char *part)
{
    const size_t length = part ? strlen(part) : 0;
    if (!length || length > 255 || part[0] == '.') return 0;
    for (const unsigned char *c = (const unsigned char *)part; *c; ++c)
        if (*c < 0x20 || *c == 0x7f || *c == '/' || *c == '\\' || *c == ':') return 0;
    return 1;
}

int pw_drive_is_program(const char *name)
{
    const size_t length = name ? strlen(name) : 0;
    return length > 4 && name[length - 4] == '.' && lower(name[length - 3]) == 'e' && lower(name[length - 2]) == 'x' &&
           lower(name[length - 1]) == 'e';
}

static int prefix_id(const char *id)
{
    const size_t length = id ? strlen(id) : 0;
    if (!length || length > 64) return 0;
    for (const char *c = id; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return 0;
    return 1;
}

/* A real folder, not a link to one. */
static int folder(const char *path)
{
    struct stat status;
    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

int pw_drive_folder(const char *data_root, const char *prefix, const char *relative, char *out, size_t size)
{
    if (!data_root || !prefix_id(prefix) || !relative) return EINVAL;
    int written = snprintf(out, size, "%s/prefixes/%s/drive_c", data_root, prefix);
    if (written < 0 || (size_t)written >= size) return ENAMETOOLONG;
    if (!folder(out)) return ENOENT;
    size_t used = (size_t)written;
    for (const char *start = relative; *start;) {
        const char *slash = strchr(start, '/');
        const size_t length = slash ? (size_t)(slash - start) : strlen(start);
        char part[256];
        if (length >= sizeof(part)) return EINVAL;
        memcpy(part, start, length);
        part[length] = 0;
        if (!pw_drive_part(part)) return EINVAL;
        written = snprintf(out + used, size - used, "/%s", part);
        if (written < 0 || (size_t)written >= size - used) return ENAMETOOLONG;
        used += (size_t)written;
        if (!folder(out)) return ENOENT;
        if (!slash) break;
        start = slash + 1;
    }
    return 0;
}

static int compare(const void *a, const void *b)
{
    const PwDriveEntry *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;   /* folders first */
    for (const unsigned char *p = (const unsigned char *)x->name, *q = (const unsigned char *)y->name;; ++p, ++q) {
        if (lower(*p) != lower(*q)) return lower(*p) - lower(*q);
        if (!*p) break;
    }
    return strcmp(x->name, y->name);
}

int pw_drive_list(const char *path, PwDriveEntry **out, size_t *count, size_t *others)
{
    *out = NULL, *count = 0, *others = 0;
    DIR *directory = opendir(path);
    if (!directory) return errno ? errno : EIO;
    size_t capacity = 0;
    PwDriveEntry *list = NULL;
    char full[4096];
    for (struct dirent *item; (item = readdir(directory));) {
        if (!pw_drive_part(item->d_name)) continue;   /* ".", "..", hidden names */
        struct stat status;
        if (snprintf(full, sizeof(full), "%s/%s", path, item->d_name) >= (int)sizeof(full) || lstat(full, &status) != 0 ||
            S_ISLNK(status.st_mode))
            continue;
        const int dir = S_ISDIR(status.st_mode);
        if (!dir && !(S_ISREG(status.st_mode) && pw_drive_is_program(item->d_name))) {
            *others += S_ISREG(status.st_mode);
            continue;
        }
        if (*count == capacity) {
            PwDriveEntry *grown = realloc(list, (capacity ? capacity * 2 : 32) * sizeof(*list));
            if (!grown) {
                free(list);
                closedir(directory);
                return ENOMEM;
            }
            list = grown, capacity = capacity ? capacity * 2 : 32;
        }
        PwDriveEntry *entry = &list[(*count)++];
        snprintf(entry->name, sizeof(entry->name), "%s", item->d_name);
        entry->dir = dir;
        entry->size = dir ? 0 : (long long)status.st_size;
    }
    closedir(directory);
    if (*count) qsort(list, *count, sizeof(*list), compare);
    *out = list;
    return 0;
}

int pw_drive_child(const char *relative, const char *part, char *out, size_t size)
{
    if (!relative || !pw_drive_part(part)) return EINVAL;
    const int written = snprintf(out, size, *relative ? "%s/%s" : "%s%s", relative, part);
    return written < 0 || (size_t)written >= size ? ENAMETOOLONG : 0;
}

int pw_drive_parent(const char *relative, char *out, size_t size)
{
    if (!relative || !*relative) return ENOENT;
    const char *slash = strrchr(relative, '/');
    const size_t length = slash ? (size_t)(slash - relative) : 0;
    if (length >= size) return ENAMETOOLONG;
    memcpy(out, relative, length);
    out[length] = 0;
    return 0;
}

int pw_drive_windows_path(const char *relative, const char *name, char *out, size_t size)
{
    char joined[4096];
    if (!relative || !name) return EINVAL;
    if (*name) {
        const int error = pw_drive_child(relative, name, joined, sizeof(joined));
        if (error) return error;
    } else {
        if (snprintf(joined, sizeof(joined), "%s", relative) >= (int)sizeof(joined)) return ENAMETOOLONG;
    }
    /* Every part checked: a relative path a caller built by hand gets the same rules as one built here */
    char part[256];
    size_t length = 0;
    for (const char *c = joined;; ++c) {
        if (*c == '/' || !*c) {
            part[length] = 0;
            if (length && !pw_drive_part(part)) return EINVAL;
            if (!length && *c == '/') return EINVAL;
            length = 0;
            if (!*c) break;
        } else if (length + 1 < sizeof(part)) {
            part[length++] = *c;
        } else {
            return EINVAL;
        }
    }
    const int written = snprintf(out, size, "C:\\%s", joined);
    if (written < 0 || (size_t)written >= size) return ENAMETOOLONG;
    for (char *c = out; *c; ++c)
        if (*c == '/') *c = '\\';
    return 0;
}
