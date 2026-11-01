/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A log file that keeps its start and its end (pw_capped_log.h). */
#include "pw_capped_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void write_all(int fd, const char *text, size_t length)
{
    while (length) {
        const ssize_t written = write(fd, text, length);
        if (written > 0) text += written, length -= (size_t)written;
        else if (written < 0 && errno == EINTR) continue;
        else return;
    }
}

/* The marker names the files, not their folder: the title moves a session's logs into sessions/1. */
static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int open_new(const char *path)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
}

int pw_capped_log_open(PwCappedLog *log, const char *head_path, const char *end_path, const char *older_path,
                       size_t head_limit, size_t end_limit)
{
    if (strlen(end_path) >= PW_CAPPED_LOG_PATH || strlen(older_path) >= PW_CAPPED_LOG_PATH) return EINVAL;
    pthread_mutex_lock(&log->lock);
    snprintf(log->end_path, sizeof(log->end_path), "%s", end_path);
    snprintf(log->older_path, sizeof(log->older_path), "%s", older_path);
    unlink(end_path);
    unlink(older_path);
    log->head_limit = head_limit;
    log->end_limit = end_limit;
    log->head_used = log->end_used = 0;
    log->rotations = 0;
    log->end = -1;
    log->head = open_new(head_path);
    const int status = log->head < 0 ? errno : 0;
    pthread_mutex_unlock(&log->lock);
    return status;
}

void pw_capped_log_write(PwCappedLog *log, const char *text, size_t length)
{
    pthread_mutex_lock(&log->lock);
    if (log->head < 0) {
    } else if (log->head_used < log->head_limit) {
        write_all(log->head, text, length);
        log->head_used += length;
        if (log->head_used >= log->head_limit) {
            char line[2 * PW_CAPPED_LOG_PATH + 96];
            snprintf(line, sizeof(line), "(this log goes on in %s, and %s holds the part before that)\n",
                     base_name(log->end_path), base_name(log->older_path));
            write_all(log->head, line, strlen(line));
        }
    } else {
        if (log->end < 0 || log->end_used >= log->end_limit) {
            if (log->end >= 0) {
                fsync(log->end);
                close(log->end);
                rename(log->end_path, log->older_path);
                log->rotations++;
            }
            log->end = open_new(log->end_path);
            log->end_used = 0;
        }
        if (log->end >= 0) {
            write_all(log->end, text, length);
            log->end_used += length;
        }
    }
    pthread_mutex_unlock(&log->lock);
}

void pw_capped_log_sync(PwCappedLog *log)
{
    pthread_mutex_lock(&log->lock);
    const int head = log->head >= 0 ? dup(log->head) : -1, end = log->end >= 0 ? dup(log->end) : -1;
    pthread_mutex_unlock(&log->lock);
    if (head >= 0) fsync(head), close(head);
    if (end >= 0) fsync(end), close(end);
}

int pw_capped_log_note(const char *head_path, const char *end_path, const char *text)
{
    int fd = open(end_path, O_WRONLY | O_APPEND);
    if (fd < 0 && errno == ENOENT) fd = open(head_path, O_WRONLY | O_APPEND);
    if (fd < 0) return errno;
    write_all(fd, text, strlen(text));
    const int status = fsync(fd) ? errno : 0;
    close(fd);
    return status;
}
