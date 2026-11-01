/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_CAPPED_LOG_H
#define PW_CAPPED_LOG_H
/* A log file with a size limit that keeps both its start and its end.
 *
 * Why: the title's log stopped at 8 MiB with "(title log full)". When a game's Wine output filled it, the last minutes
 * before a crash were lost, and those are the part that says what happened (a tester's in-title game ended by the
 * system, 2026-10-31: the log's last line was 400 s before the end).
 *
 * The first head_limit bytes go to the head file. After that, each write goes to the end file. When the end file has
 * end_limit bytes, it is renamed to the older file (replacing it) and a new end file is begun. The file set is then
 * at most head_limit + 2 * end_limit bytes, plus one write's overshoot per file. It holds the log's start, and at least
 * its last end_limit bytes (the older file plus the end file). The head says where the log goes on.
 *
 * pw_capped_log_open removes stale end and older files, then truncates or creates the head. It returns 0 or the errno
 * of the head's open; the log then takes no writes. A path longer than PW_CAPPED_LOG_PATH - 1 is EINVAL. Writes are
 * serialised by the log's lock: each write lands whole in one file. A failed end-file open drops writes until the next
 * rotation is tried (at the next write). pw_capped_log_sync fsyncs both files on duplicates of their descriptors, so
 * writers never wait on the disk.
 *
 * pw_capped_log_note appends text to the newest file of a set that another process left (the end file if it exists,
 * otherwise the head): a mark written after a session that never wrote its own last line. It returns 0 or an errno. */
#include <pthread.h>
#include <stddef.h>
#define PW_CAPPED_LOG_PATH 256u
typedef struct PwCappedLog {
    pthread_mutex_t lock;
    int head, end;
    size_t head_limit, end_limit, head_used, end_used;
    unsigned rotations;
    char end_path[PW_CAPPED_LOG_PATH], older_path[PW_CAPPED_LOG_PATH];
} PwCappedLog;
#define PW_CAPPED_LOG_INIT { PTHREAD_MUTEX_INITIALIZER, -1, -1, 0, 0, 0, 0, 0, "", "" }
#ifdef __cplusplus
extern "C" {
#endif
int pw_capped_log_open(PwCappedLog *log, const char *head_path, const char *end_path, const char *older_path,
                       size_t head_limit, size_t end_limit);
void pw_capped_log_write(PwCappedLog *log, const char *text, size_t length);
void pw_capped_log_sync(PwCappedLog *log);
int pw_capped_log_note(const char *head_path, const char *end_path, const char *text);
#ifdef __cplusplus
}
#endif
#endif
