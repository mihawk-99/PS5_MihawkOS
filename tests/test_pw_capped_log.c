/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A log that keeps its start and its end (src/pw_capped_log.h): the head, the rolling end, the bound, four writers,
 * the note after a session that never ended. */
#include "../src/pw_capped_log.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char head[192], end[192], older[192];

static long size_of(const char *path)
{
    struct stat status;
    return stat(path, &status) ? -1 : (long)status.st_size;
}
static int holds(const char *path, const char *text)
{
    static char data[1 << 20];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    const size_t got = fread(data, 1, sizeof(data) - 1, file);
    fclose(file);
    data[got] = 0;
    return strstr(data, text) != NULL;
}

static PwCappedLog shared = PW_CAPPED_LOG_INIT;
static void *writer(void *arg)
{
    char line[64];
    for (int i = 0; i < 5000; i++) {
        const int length = snprintf(line, sizeof(line), "writer %ld line %05d\n", (long)arg, i);
        pw_capped_log_write(&shared, line, (size_t)length);
    }
    return NULL;
}

int main(void)
{
    char base[] = "/tmp/proton-capped-log-XXXXXX";
    assert(mkdtemp(base));
    snprintf(head, sizeof(head), "%s/title-last.log", base);
    snprintf(end, sizeof(end), "%s/title-end.log", base);
    snprintf(older, sizeof(older), "%s/title-end.1.log", base);

    /* A stale end from an earlier session goes at open; a short log is the head alone. */
    FILE *stale = fopen(end, "w");
    fputs("stale\n", stale);
    fclose(stale);
    PwCappedLog log = PW_CAPPED_LOG_INIT;
    assert(!pw_capped_log_open(&log, head, end, older, 1000, 400));
    assert(size_of(end) == -1);
    pw_capped_log_write(&log, "first\n", 6);
    pw_capped_log_sync(&log);
    assert(size_of(head) == 6 && size_of(end) == -1);

    /* 20,000 lines of 20 bytes, 400 KB: the head keeps the start and says where it goes on; the end files keep the
     * last line and at least 400 bytes; the set never grows past head + 2 ends + a line each. */
    char line[64];
    for (int i = 0; i < 20000; i++) {
        snprintf(line, sizeof(line), "flood line %08d\n", i);
        pw_capped_log_write(&log, line, strlen(line));
    }
    pw_capped_log_sync(&log);
    assert(holds(head, "first\n") && holds(head, "flood line 00000000\n"));
    assert(holds(head, "(this log goes on in title-end.log, and title-end.1.log holds the part before that)"));
    assert(holds(end, "flood line 00019999\n"));
    assert(size_of(head) <= 1000 + 20 + 128 && size_of(end) <= 400 + 20 && size_of(older) >= 400);
    assert(size_of(older) + size_of(end) >= 400 && log.rotations > 900);
    assert(!holds(older, "flood line 00000000\n"));

    /* The note goes to the newest file of the set; without an end file, to the head. */
    assert(!pw_capped_log_note(head, end, "(ended here)\n") && holds(end, "(ended here)\n"));
    unlink(end);
    assert(!pw_capped_log_note(head, end, "(ended at the head)\n") && holds(head, "(ended at the head)\n"));
    unlink(head);
    assert(pw_capped_log_note(head, end, "x\n") != 0);

    /* Four writers: every line lands whole in one file (the end files stay line-aligned and complete). */
    assert(!pw_capped_log_open(&shared, head, end, older, 4096, 4096));
    pthread_t threads[4];
    for (long i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, writer, (void *)i));
    for (int i = 0; i < 4; i++) pthread_join(threads[i], NULL);
    pw_capped_log_sync(&shared);
    FILE *file = fopen(end, "rb");
    assert(file);
    unsigned lines = 0;
    while (fgets(line, sizeof(line), file)) assert(!strncmp(line, "writer ", 7) && strlen(line) == 20), lines++;
    fclose(file);
    assert(lines > 0 && holds(end, "line 04999\n"));

    /* A path too long for the record. */
    char long_path[PW_CAPPED_LOG_PATH + 8];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = 0;
    PwCappedLog other = PW_CAPPED_LOG_INIT;
    assert(pw_capped_log_open(&other, head, long_path, older, 10, 10) != 0);

    char command[256];
    snprintf(command, sizeof(command), "rm -rf %s", base);
    assert(!system(command));
    puts("pw_capped_log: ok");
    return 0;
}
