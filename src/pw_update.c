/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_update.h"
#include "pw_pack.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH 1200

static int join(char *out, const char *base, const char *relative)
{
    return snprintf(out, PATH, "%s/%s", base, relative) < PATH;
}

static int fail(char *failed, size_t size, int error, const char *path)
{
    if (failed && size) snprintf(failed, size, "%s", path);
    return -(error ? error : EIO);
}

static char *read_all(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = NULL;
    size_t used = 0, room = 0, got;
    char buffer[16384];
    while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (used + got > room) {
            char *bigger = realloc(text, room = (used + got) * 2);
            if (!bigger) {
                free(text);
                fclose(file);
                return NULL;
            }
            text = bigger;
        }
        memcpy(text + used, buffer, got);
        used += got;
    }
    fclose(file);
    *size = used;
    return text ? text : calloc(1, 1);
}

/* The folders on the way to `path`. */
static int make_parents(char *path)
{
    for (char *slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        const int made = mkdir(path, 0777) == 0 || errno == EEXIST;
        *slash = '/';
        if (!made) return 0;
    }
    return 1;
}

/* title/<relative> into previous/, when it is there. */
static int move_aside(const char *title, const char *previous, const char *relative, char *failed, size_t size)
{
    char from[PATH], to[PATH];
    struct stat st;
    if (!join(from, title, relative) || !join(to, previous, relative)) return fail(failed, size, ENAMETOOLONG, relative);
    if (lstat(from, &st) != 0) return errno == ENOENT ? 0 : fail(failed, size, errno, from);
    if (S_ISDIR(st.st_mode)) return fail(failed, size, EISDIR, from);
    if (!make_parents(to) || rename(from, to) != 0) return fail(failed, size, errno, from);
    return 0;
}

/* stage/<relative> into the title folder, the file it replaces into previous/. Done already when it is not in the stage. */
static int move_in(const char *stage, const char *title, const char *previous, const char *relative, char *failed,
                   size_t size)
{
    char from[PATH], to[PATH];
    struct stat st;
    if (!join(from, stage, relative) || !join(to, title, relative)) return fail(failed, size, ENAMETOOLONG, relative);
    if (lstat(from, &st) != 0) return errno == ENOENT ? 0 : fail(failed, size, errno, from);
    const int aside = move_aside(title, previous, relative, failed, size);
    if (aside) return aside;
    if (!make_parents(to) || rename(from, to) != 0) return fail(failed, size, errno, to);
    return 0;
}

static int listed(const PwPackManifest *manifest, const char *path)
{
    for (size_t f = 0; f < manifest->file_count; f++)
        if (!strcmp(manifest->files[f].path, path)) return 1;
    return !strcmp(path, PW_UPDATE_RECORD);
}

/* A folder and everything in it, never through a link. */
static int remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -errno;
    if (!S_ISDIR(st.st_mode)) return unlink(path) == 0 || errno == ENOENT ? 0 : -errno;
    DIR *dir = opendir(path);
    if (!dir) return -errno;
    int status = 0;
    char child[PATH];
    for (struct dirent *entry; !status && (entry = readdir(dir));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        status = join(child, path, entry->d_name) ? remove_tree(child) : -ENAMETOOLONG;
    }
    closedir(dir);
    return status ? status : rmdir(path) == 0 ? 0 : -errno;
}

static int write_new(const char *path, const char *text)
{
    char part[PATH];
    if (snprintf(part, sizeof(part), "%s.mihawkos-part", path) >= (int)sizeof(part)) return -ENAMETOOLONG;
    const int fd = open(part, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) return -errno;
    const size_t length = strlen(text);
    int error = write(fd, text, length) == (ssize_t)length ? 0 : errno ? errno : EIO;
    if (!error && fsync(fd) != 0) error = errno;
    if (close(fd) != 0 && !error) error = errno;
    if (!error && rename(part, path) != 0) error = errno;
    if (error) unlink(part);
    return -error;
}

int pw_update_mark_ready(const char *update, const char *version)
{
    char path[PATH], text[256];
    if (!join(path, update, "ready") || snprintf(text, sizeof(text), "%s\n", version) >= (int)sizeof(text))
        return -ENAMETOOLONG;
    return write_new(path, text);
}

int pw_update_pending(const char *update, char *version, size_t size)
{
    static const char *const names[] = { "ready", "switching" };
    for (size_t i = 0; i < 2; i++) {
        char path[PATH];
        size_t length = 0;
        char *text = join(path, update, names[i]) ? read_all(path, &length) : NULL;
        if (!text) continue;
        snprintf(version, size, "%.*s", (int)strcspn(text, "\r\n"), text);
        free(text);
        return 1;
    }
    return 0;
}

int pw_update_switch(const char *update, const char *title, char *failed, size_t size)
{
    char ready[PATH], switching[PATH], switched[PATH], stage[PATH], previous[PATH], stage_record[PATH], title_record[PATH];
    if (failed && size) *failed = '\0';
    if (!join(ready, update, "ready") || !join(switching, update, "switching") || !join(switched, update, "switched") ||
        !join(stage, update, "stage") || !join(previous, update, "previous") ||
        !join(stage_record, stage, PW_UPDATE_RECORD) || !join(title_record, title, PW_UPDATE_RECORD))
        return fail(failed, size, ENAMETOOLONG, update);
    struct stat st;
    if (rename(ready, switching) != 0 && errno != ENOENT) return fail(failed, size, errno, ready);
    if (lstat(switching, &st) != 0) return 0;
    /* The new manifest: in the stage until the switch moves it, in the title folder after. */
    const int record_moved = lstat(stage_record, &st) != 0;
    size_t length = 0;
    char *text = read_all(record_moved ? title_record : stage_record, &length);
    PwPackManifest manifest;
    if (!text || pw_pack_parse(text, length, &manifest) != PW_PACK_OK || !manifest.title) {
        free(text);
        return fail(failed, size, EINVAL, record_moved ? title_record : stage_record);
    }
    free(text);
    int status = mkdir(previous, 0777) == 0 || errno == EEXIST ? 0 : fail(failed, size, errno, previous);
    if (!record_moved) {
        for (size_t f = 0; !status && f < manifest.stamp; f++)
            status = move_in(stage, title, previous, manifest.files[f].path, failed, size);
        /* What the old title had and the new one has not: aside, by the old record. A folder copied by hand has none:
         * its files are kept, but for the runtime's pieces the new manifest does not name (about 0.5 GB a release). */
        PwPackManifest old;
        char *old_text = status ? NULL : read_all(title_record, &length);
        if (old_text && pw_pack_parse(old_text, length, &old) == PW_PACK_OK) {
            for (size_t f = 0; !status && f < old.file_count; f++)
                if (!listed(&manifest, old.files[f].path))
                    status = move_aside(title, previous, old.files[f].path, failed, size);
            pw_pack_manifest_free(&old);
        } else if (!status && !old_text) {
            char pieces[PATH], piece[PATH];
            DIR *dir = join(pieces, title, "runtime-pieces") ? opendir(pieces) : NULL;
            for (struct dirent *entry; dir && !status && (entry = readdir(dir));)
                if (entry->d_name[0] != '.' &&
                    snprintf(piece, sizeof(piece), "runtime-pieces/%s", entry->d_name) < (int)sizeof(piece) &&
                    !listed(&manifest, piece))
                    status = move_aside(title, previous, piece, failed, size);
            if (dir) closedir(dir);
        }
        free(old_text);
        if (!status) status = move_in(stage, title, previous, PW_UPDATE_RECORD, failed, size);
    }
    for (size_t f = manifest.stamp; !status && f < manifest.file_count; f++)
        status = move_in(stage, title, previous, manifest.files[f].path, failed, size);
    pw_pack_manifest_free(&manifest);
    if (status) return status;
    if (rename(switching, switched) != 0) return fail(failed, size, errno, switching);
    remove_tree(stage);   /* only empty folders are left in it, or files of an older stage no manifest names */
    return 1;
}

int pw_update_confirm(const char *update)
{
    char switched[PATH], previous[PATH];
    struct stat st;
    if (!join(switched, update, "switched") || !join(previous, update, "previous")) return -ENAMETOOLONG;
    if (lstat(switched, &st) != 0) return 0;
    const int status = remove_tree(previous);
    if (status) return status;
    return unlink(switched) == 0 ? 1 : -errno;
}

int pw_update_discard(const char *update)
{
    char ready[PATH], stage[PATH];
    if (!join(ready, update, "ready") || !join(stage, update, "stage")) return -ENAMETOOLONG;
    if (unlink(ready) != 0 && errno != ENOENT) return -errno;
    return remove_tree(stage);
}

int pw_update_check_due(int enabled, int64_t now, int64_t last)
{
    return enabled && (last <= 0 || now < last || now - last >= PW_UPDATE_CHECK_EVERY_S);
}

/* Moved in last, in this order: the runtime's pin (the title's runtime-stamp: the new title re-copies its runtime when
 * it differs from the installed one, so it changes only once the pieces and runtime-pack.txt it pins are in place),
 * then eboot.bin and sce_sys/param.json (ShadowMountPlus registers no half title). runtime-stamp is optional. */
static const char *const title_last[] = { "runtime-stamp", "eboot.bin", "sce_sys/param.json" };

static int last_rank(const char *path)
{
    for (int i = 0; i < 3; ++i)
        if (!strcmp(path, title_last[i])) return i + 1;
    return 0;
}

static int record_order(const void *a, const void *b)
{
    const PwUpdateFile *x = a, *y = b;
    const int rx = last_rank(x->path), ry = last_rank(y->path);
    if (rx != ry) return rx - ry;   /* the stamps after every other file, eboot.bin first */
    return strcmp(x->path, y->path);
}

int pw_update_record_write(const char *stage, PwUpdateFile *files, size_t count)
{
    char path[PATH], part[PATH];
    if (!join(path, stage, PW_UPDATE_RECORD) || snprintf(part, PATH, "%s.part", path) >= PATH) return -ENAMETOOLONG;
    qsort(files, count, sizeof(*files), record_order);
    if (count < 2 || strcmp(files[count - 2].path, title_last[1]) || strcmp(files[count - 1].path, title_last[2]))
        return -EINVAL;
    const int pinned = count > 2 && !strcmp(files[count - 3].path, title_last[0]);
    uint64_t plain = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i && !strcmp(files[i - 1].path, files[i].path)) return -EINVAL;
        plain += files[i].size;
    }
    FILE *out = fopen(part, "wb");
    if (!out) return -errno;
    fprintf(out, "mihawkos-title-pack 1\nzstd 0 -19\n%s%s%sstamp %s\nstamp %s\n", pinned ? "stamp " : "",
            pinned ? title_last[0] : "", pinned ? "\n" : "", title_last[1], title_last[2]);
    /* one chunk line over every file: pw_pack_parse wants one, and the switch never fetches it */
    fprintf(out, "chunk %064d 1 %llu %zu\n", 0, (unsigned long long)plain, count);
    for (size_t i = 0; i < count; ++i) {
        fputs("file ", out);
        for (int b = 0; b < 32; ++b) fprintf(out, "%02x", files[i].sha256[b]);
        fprintf(out, " %llu 0 %s\n", (unsigned long long)files[i].size, files[i].path);
    }
    const int bad = ferror(out) || fflush(out) || fsync(fileno(out));
    const int saved = errno;
    if (fclose(out) || bad) {
        remove(part);
        return -(saved ? saved : EIO);
    }
    return rename(part, path) ? -errno : 0;
}

const char *pw_update_status_text(int status)
{
    switch (status) {
    case PW_UPDATE_NOT_A_TITLE: return "the update's archive holds no MihawkOS title";
    case PW_UPDATE_RUNTIME_PIN: return "the update's runtime is not the one its signed description names";
    case PW_UPDATE_STAGE: return "the update could not be prepared on the console's storage";
    default: return pw_zip_status_text(status);
    }
}

int pw_update_install_zip(const char *zip_path, const char *update, const char *prefix, const uint8_t runtime_sha256[32],
                          const char *version, pw_zip_inflate_fn inflate, char *failed, size_t size)
{
    char stage[PATH], target[PATH];
    if (failed && size) *failed = '\0';
    if (!join(stage, update, "stage")) return PW_UPDATE_STAGE;
    mkdir(update, 0777);
    if (remove_tree(stage) || (mkdir(stage, 0777) && errno != EEXIST)) {
        if (failed && size) snprintf(failed, size, "%s", stage);
        return PW_UPDATE_STAGE;
    }
    FILE *file = fopen(zip_path, "rb");
    if (!file) {
        if (failed && size) snprintf(failed, size, "%s", zip_path);
        return PW_ZIP_FORMAT;
    }
    PwZip zip;
    int status = pw_zip_open(file, &zip);
    PwUpdateFile *files = status ? NULL : calloc(zip.count ? zip.count : 1, sizeof(*files));
    if (!status && !files) status = PW_ZIP_MEMORY;
    size_t count = 0;
    const size_t skip = strlen(prefix);
    int pinned = 0;
    for (size_t e = 0; !status && e < zip.count; ++e) {
        const PwZipEntry *entry = &zip.entries[e];
        const char *relative = entry->name + skip;
        if (entry->folder || strncmp(entry->name, prefix, skip) || !*relative || !strcmp(relative, PW_UPDATE_RECORD))
            continue;
        if (!join(target, stage, relative)) status = PW_UPDATE_STAGE;
        else if (!make_parents(target)) status = PW_ZIP_WRITE;
        else status = pw_zip_extract(file, entry, target, inflate, files[count].sha256);
        if (status) {
            if (failed && size) snprintf(failed, size, "%s", entry->name);
            break;
        }
        files[count].path = relative;
        files[count].size = entry->size;
        if (!strcmp(relative, "runtime-pack.txt"))
            pinned = !runtime_sha256 || !memcmp(files[count].sha256, runtime_sha256, 32);
        count++;
    }
    int stamps = 0;
    for (size_t i = 0; i < count; ++i) stamps += last_rank(files[i].path) > 1;   /* eboot.bin and param.json */
    if (!status && stamps != 2) status = PW_UPDATE_NOT_A_TITLE;
    if (!status && !pinned) status = PW_UPDATE_RUNTIME_PIN;
    if (!status && (pw_update_record_write(stage, files, count) || pw_update_mark_ready(update, version))) status = PW_UPDATE_STAGE;
    free(files);
    if (zip.entries) pw_zip_close(&zip);
    fclose(file);
    if (status) remove_tree(stage);
    return status;
}
