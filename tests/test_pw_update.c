/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_update.h"
#include "../src/pw_pack.h"
#include <limits.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char work[256], title[300], update[300], stage[320];

static const char *at(const char *base, const char *relative)
{
    static char paths[4][700];
    static int next;
    char *path = paths[next++ % 4];
    snprintf(path, sizeof(paths[0]), "%s/%s", base, relative);
    return path;
}

static void put(const char *base, const char *relative, const char *text)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", base, relative);
    for (char *slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        mkdir(path, 0777);
        *slash = '/';
    }
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static int holds(const char *base, const char *relative, const char *text)
{
    char got[4096] = { 0 };
    FILE *file = fopen(at(base, relative), "rb");
    if (!file) return 0;
    const size_t size = fread(got, 1, sizeof(got) - 1, file);
    fclose(file);
    return size == strlen(text) && !memcmp(got, text, size);
}

static int exists(const char *base, const char *relative)
{
    struct stat st;
    return lstat(at(base, relative), &st) == 0;
}

/* A title pack's record naming `paths` (the others in path order, then eboot.bin and param.json), one chunk. */
static void record(const char *base, const char *const *paths, size_t count)
{
    char text[4096], *t = text;
    t += sprintf(t, "mihawkos-title-pack 1\nzstd test -19\nstamp eboot.bin\nstamp sce_sys/param.json\n");
    t += sprintf(t, "chunk %064d 10 %zu %zu\n", 0, count * 4, count);
    for (size_t i = 0; i < count; i++) t += sprintf(t, "file %064d 4 0 %s\n", 0, paths[i]);
    put(base, "title-pack.txt", text);
}

static const char *const old_files[] = { "assets/a", "assets/gone", "eboot.bin", "sce_sys/param.json" };
static const char *const new_files[] = { "assets/a", "assets/new", "eboot.bin", "sce_sys/param.json" };
static char old_record[4096], new_record[4096];

static void read_text(const char *path, char *out)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    out[fread(out, 1, 4095, file)] = '\0';
    fclose(file);
}

/* The old title installed, the new one staged and ready. */
static void prepare(void)
{
    char command[700];
    snprintf(command, sizeof(command), "rm -rf '%s/title' '%s/update'", work, work);
    assert(system(command) == 0);
    put(title, "eboot.bin", "old eboot");
    put(title, "sce_sys/param.json", "old param");
    put(title, "assets/a", "a1");
    put(title, "assets/gone", "g");
    put(title, "controls.txt", "the title's own state");
    record(title, old_files, 4);
    put(stage, "eboot.bin", "new eboot");
    put(stage, "sce_sys/param.json", "new param");
    put(stage, "assets/a", "a2");
    put(stage, "assets/new", "n");
    record(stage, new_files, 4);
    read_text(at(title, "title-pack.txt"), old_record);
    read_text(at(stage, "title-pack.txt"), new_record);
    assert(pw_update_mark_ready(update, "01.001.000") == 0);
}

static void check_switched(void)
{
    char previous[320];
    snprintf(previous, sizeof(previous), "%s/previous", update);
    assert(holds(title, "eboot.bin", "new eboot") && holds(title, "sce_sys/param.json", "new param"));
    assert(holds(title, "assets/a", "a2") && holds(title, "assets/new", "n") && !exists(title, "assets/gone"));
    assert(holds(title, "controls.txt", "the title's own state") && holds(title, "title-pack.txt", new_record));
    assert(holds(previous, "eboot.bin", "old eboot") && holds(previous, "sce_sys/param.json", "old param"));
    assert(holds(previous, "assets/a", "a1") && holds(previous, "assets/gone", "g"));
    assert(holds(previous, "title-pack.txt", old_record) && !exists(previous, "assets/new"));
    assert(holds(update, "switched", "01.001.000\n") && !exists(update, "ready") && !exists(update, "switching"));
    assert(!exists(update, "stage"));
    char version[32];
    assert(pw_update_pending(update, version, sizeof(version)) == 0);
}

int main(void)
{
    snprintf(work, sizeof(work), "%s/pw-update-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    assert(mkdtemp(work));
    snprintf(title, sizeof(title), "%s/title", work);
    snprintf(update, sizeof(update), "%s/update", work);
    snprintf(stage, sizeof(stage), "%s/stage", update);
    char failed[512], version[32];

    /* Nothing due: nothing moves. */
    prepare();
    assert(unlink(at(update, "ready")) == 0);
    assert(pw_update_pending(update, version, sizeof(version)) == 0);
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == 0 && holds(title, "eboot.bin", "old eboot"));
    assert(pw_update_confirm(update) == 0);

    /* A due switch dropped: `ready` and the stage go, the title stays. */
    prepare();
    assert(pw_update_discard(update) == 0 && !exists(update, "ready") && !exists(update, "stage"));
    assert(pw_update_pending(update, version, sizeof(version)) == 0 && holds(title, "eboot.bin", "old eboot"));

    /* A whole switch, then the confirm. */
    prepare();
    assert(pw_update_pending(update, version, sizeof(version)) == 1 && !strcmp(version, "01.001.000"));
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == 1);
    check_switched();
    assert(pw_update_confirm(update) == 1 && !exists(update, "previous") && !exists(update, "switched"));
    assert(pw_update_confirm(update) == 0 && holds(title, "eboot.bin", "new eboot"));

    /* Cut short after the first file: resumed from `switching`. */
    prepare();
    char previous[320];
    snprintf(previous, sizeof(previous), "%s/previous", update);
    assert(rename(at(update, "ready"), at(update, "switching")) == 0 && mkdir(previous, 0777) == 0);
    assert(mkdir(at(previous, "assets"), 0777) == 0 && rename(at(title, "assets/a"), at(previous, "assets/a")) == 0);
    assert(rename(at(stage, "assets/a"), at(title, "assets/a")) == 0);
    assert(pw_update_pending(update, version, sizeof(version)) == 1);
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == 1);
    check_switched();

    /* Cut between moving a file aside and moving its new one in. */
    prepare();
    assert(rename(at(update, "ready"), at(update, "switching")) == 0 && mkdir(previous, 0777) == 0);
    assert(mkdir(at(previous, "assets"), 0777) == 0 && rename(at(title, "assets/a"), at(previous, "assets/a")) == 0);
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == 1);
    check_switched();

    /* Cut after the record moved: only eboot.bin and param.json were left, read from the record in the title folder. */
    prepare();
    assert(rename(at(update, "ready"), at(update, "switching")) == 0 && mkdir(previous, 0777) == 0);
    assert(mkdir(at(previous, "assets"), 0777) == 0);
    assert(rename(at(title, "assets/a"), at(previous, "assets/a")) == 0 && rename(at(stage, "assets/a"), at(title, "assets/a")) == 0);
    assert(rename(at(stage, "assets/new"), at(title, "assets/new")) == 0);
    assert(rename(at(title, "assets/gone"), at(previous, "assets/gone")) == 0);
    assert(rename(at(title, "title-pack.txt"), at(previous, "title-pack.txt")) == 0);
    assert(rename(at(stage, "title-pack.txt"), at(title, "title-pack.txt")) == 0);
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == 1);
    check_switched();

    /* A stage whose record is not a title pack's: refused, the title untouched, and still due. */
    prepare();
    put(stage, "title-pack.txt", "mihawkos-runtime-pack 1\n");
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == -EINVAL && strstr(failed, "title-pack.txt"));
    assert(holds(title, "eboot.bin", "old eboot") && holds(title, "assets/a", "a1"));
    assert(pw_update_pending(update, version, sizeof(version)) == 1);

    /* A folder copied by hand has no record: nothing it holds is moved aside but what the new title replaces. */
    prepare();
    assert(unlink(at(title, "title-pack.txt")) == 0);
    assert(pw_update_switch(update, title, failed, sizeof(failed)) == 1);
    assert(holds(title, "assets/gone", "g") && holds(title, "eboot.bin", "new eboot"));

    char command[700];
    snprintf(command, sizeof(command), "rm -rf '%s'", work);
    assert(system(command) == 0);
    /* A release zip's record (2026-10-31): sorted, the stamps last, and a manifest the switch reads */
    {
        char records[PATH_MAX - 64];
        snprintf(records, sizeof(records), "%s/pw-update-record-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        assert(mkdtemp(records));
        PwUpdateFile files[] = { { "sce_sys/param.json", 900, { 1 } }, { "eboot.bin", 35000000, { 2 } },
                                 { "runtime-pack.txt", 300000, { 3 } }, { "assets/a.png", 10, { 4 } } };
        assert(pw_update_record_write(records, files, 4) == 0);
        assert(!strcmp(files[0].path, "assets/a.png") && !strcmp(files[1].path, "runtime-pack.txt") &&
               !strcmp(files[2].path, "eboot.bin") && !strcmp(files[3].path, "sce_sys/param.json"));
        char record[PATH_MAX + 32];
        snprintf(record, sizeof(record), "%s/" PW_UPDATE_RECORD, records);
        FILE *in = fopen(record, "rb");
        char text[4096];
        const size_t got = fread(text, 1, sizeof(text), in);
        fclose(in);
        PwPackManifest manifest;
        assert(pw_pack_parse(text, got, &manifest) == PW_PACK_OK && manifest.title && manifest.file_count == 4);
        assert(manifest.stamp == 2 && !strcmp(manifest.files[3].path, "sce_sys/param.json"));
        pw_pack_manifest_free(&manifest);
        /* with the runtime's pin: it goes in after every other file, before eboot.bin */
        PwUpdateFile pinned[] = { { "sce_sys/param.json", 9, { 1 } }, { "runtime-stamp", 65, { 5 } }, { "eboot.bin", 7, { 2 } },
                                  { "runtime-pieces/ab.zst", 3, { 6 } }, { "runtime-pack.txt", 4, { 3 } } };
        assert(pw_update_record_write(records, pinned, 5) == 0);
        in = fopen(record, "rb");
        const size_t again = fread(text, 1, sizeof(text), in);
        fclose(in);
        assert(pw_pack_parse(text, again, &manifest) == PW_PACK_OK && manifest.stamp == 2);
        assert(!strcmp(manifest.files[2].path, "runtime-stamp") && !strcmp(manifest.files[3].path, "eboot.bin"));
        pw_pack_manifest_free(&manifest);
        PwUpdateFile no_eboot[] = { { "sce_sys/param.json", 1, { 0 } }, { "a", 1, { 0 } } };
        assert(pw_update_record_write(records, no_eboot, 2) == -EINVAL);
        remove(record);
        rmdir(records);
    }

    /* The automatic check: on and a day since the last; never when off; a clock set back checks again */
    const int64_t day = PW_UPDATE_CHECK_EVERY_S, now = INT64_C(1793500000);
    assert(pw_update_check_due(1, now, 0) && pw_update_check_due(1, now, now - day) && !pw_update_check_due(1, now, now - day + 1));
    assert(!pw_update_check_due(0, now, 0) && pw_update_check_due(1, now, now + 3600));

    puts("test_pw_update: ok");
    return 0;
}
