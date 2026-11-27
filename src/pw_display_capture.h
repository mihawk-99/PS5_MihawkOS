/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_DISPLAY_CAPTURE_H
#define PW_DISPLAY_CAPTURE_H
#include "pw_display_copy.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* A picture of a frame the broker shows (a probe's or a pad script's capture), taken without holding up the present
 * (graphics patch 0025, display.cpp).
 *
 * Before, a present carrying a capture did all of this before it returned: wait for its flip to be shown, read the
 * 1920x1080 frame back from the display's buffer pixel by pixel through the tiling, then write and fsync a 6 MiB PPM.
 * The client's frame stopped 0.63-0.75 s. That happened in all 60 ten-second windows with a capture in a Direct3D 12
 * game's runs, and was taken for the game's own hitch (PS5_Proton docs/PLAN.md; four runs of 2026-10-17, records
 * removed from the tree, in git history).
 *
 * Now the present fills the same frame, pointer included, a second time into tiled memory of its own (8.4 MiB, the
 * display copy's own code). It never reads it back from the display's buffer, which reads at about 105 MB/s: a
 * whole-buffer copy from it still took 80 ms in the present (2026-10-17). It hands that copy to one
 * capture thread (pw_display_capture_take), which writes the PPM as PATH.part, fsyncs it and renames it to PATH. So a
 * reader that finds PATH finds all of it; the title's pad script polls for it.
 *
 * What is written is what the flip shows; the present no longer waits for the flip to be shown. At most PW_DISPLAY_CAPTURE_QUEUE pictures wait, 8.4 MiB each.
 * A further capture waits in the present for the oldest to be written. The caller writes a capture in the present
 * itself, as before, when the memory or the thread cannot be had. pw_display_capture_drain waits for every picture
 * before the display closes.
 *
 * Each written picture is one line in the broker's log:
 * "gpu-display capture flip= copy_us= write_us= status= path=". Host test: tests/test_pw_display_capture.c. */
enum { PW_DISPLAY_CAPTURE_QUEUE = 2, PW_DISPLAY_TILED_BYTES = PW_DISPLAY_BLOCKS * 9 << 16 };

typedef struct PwDisplayCapture {
    uint8_t *frame;   /* tiled frame, malloc'd; the capture thread frees it */
    unsigned width, height, blocks;
    uint64_t serial, copy_ns;
    FILE *log;        /* where its line goes, or NULL */
    char path[240];
} PwDisplayCapture;

typedef struct PwDisplayCaptures {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    PwDisplayCapture waiting[PW_DISPLAY_CAPTURE_QUEUE];   /* oldest first */
    unsigned count;
    int started, stopping;
    pthread_t thread;
    uint64_t written, failed;
} PwDisplayCaptures;
#define PW_DISPLAY_CAPTURES_INIT \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, { { NULL, 0, 0, 0, 0, 0, NULL, { 0 } } }, 0, 0, 0, 0, 0, 0 }

static inline uint64_t pw_display_capture_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* The picture of a tiled B8G8R8A8 frame as a binary PPM: PATH.part written and fsynced, then renamed to PATH.
 * 0, or an errno; nothing is left at PATH.part on failure. */
static inline int pw_display_capture_write_wh(const uint8_t *frame, unsigned width, unsigned height, unsigned blocks,
                                              const char *path)
{
    if (!frame || !path || !width || width > PW_DISPLAY_MAX_WIDTH || !height || height > PW_DISPLAY_MAX_HEIGHT || !blocks)
        return EINVAL;
    char part[256];
    if (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part)) return ENAMETOOLONG;
    FILE *file = fopen(part, "wb");
    if (!file) return errno ? errno : EIO;
    int okay = fprintf(file, "P6\n%u %u\n255\n", width, height) > 0;
    uint8_t row[PW_DISPLAY_MAX_WIDTH * 3];
    const size_t row_bytes = (size_t)width * 3;
    for (unsigned y = 0; y < height && okay; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t pixel;
            memcpy(&pixel, frame + pw_display_tiled_stride(x, y, blocks), 4);
            row[x * 3] = (uint8_t)(pixel >> 16);
            row[x * 3 + 1] = (uint8_t)(pixel >> 8);
            row[x * 3 + 2] = (uint8_t)pixel;
        }
        okay = fwrite(row, 1, row_bytes, file) == row_bytes;
    }
    okay &= !fflush(file);
    okay &= !fsync(fileno(file));
    okay &= !fclose(file);
    if (!okay) {
        remove(part);
        return EIO;
    }
    return rename(part, path) ? (errno ? errno : EIO) : 0;
}

static inline int pw_display_capture_write(const uint8_t *frame, const char *path)
{
    return pw_display_capture_write_wh(frame, PW_DISPLAY_WIDTH, PW_DISPLAY_HEIGHT, PW_DISPLAY_BLOCKS, path);
}

static inline void *pw_display_capture_main(void *argument)
{
    PwDisplayCaptures *captures = (PwDisplayCaptures *)argument;
    pthread_mutex_lock(&captures->lock);
    for (;;) {
        while (!captures->count && !captures->stopping) pthread_cond_wait(&captures->changed, &captures->lock);
        if (!captures->count) break;   /* stopping, and every picture written */
        const PwDisplayCapture job = captures->waiting[0];
        memmove(captures->waiting, captures->waiting + 1, (captures->count - 1) * sizeof(job));
        --captures->count;
        pthread_cond_broadcast(&captures->changed);   /* a present waiting for room */
        pthread_mutex_unlock(&captures->lock);
        const uint64_t begin = pw_display_capture_now_ns();
        const unsigned w = job.width ? job.width : (unsigned)PW_DISPLAY_WIDTH;
        const unsigned h = job.height ? job.height : (unsigned)PW_DISPLAY_HEIGHT;
        const unsigned blocks = job.blocks ? job.blocks : (unsigned)PW_DISPLAY_BLOCKS;
        const int status = pw_display_capture_write_wh(job.frame, w, h, blocks, job.path);
        const uint64_t spent = pw_display_capture_now_ns() - begin;
        free(job.frame);
        if (job.log)
            fprintf(job.log, "gpu-display capture flip=%llu copy_us=%llu write_us=%llu status=%d path=%s\n",
                    (unsigned long long)job.serial, (unsigned long long)(job.copy_ns / 1000),
                    (unsigned long long)(spent / 1000), status, job.path);
        pthread_mutex_lock(&captures->lock);
        if (status) ++captures->failed;
        else ++captures->written;
        pthread_cond_broadcast(&captures->changed);   /* a drain waiting */
    }
    pthread_mutex_unlock(&captures->lock);
    return NULL;
}

/* Hands frame (a malloc'd tiled frame of width x height with `blocks` tile columns) to the capture thread, which starts
 * on first use and then owns and frees the frame. Waits while PW_DISPLAY_CAPTURE_QUEUE pictures already wait. Returns 0,
 * or an errno when the path is too long or the thread cannot start; the frame then stays the caller's. */
static inline int pw_display_capture_take_wh(PwDisplayCaptures *captures, uint8_t *frame, unsigned width,
                                             unsigned height, unsigned blocks, const char *path,
                                             uint64_t serial, uint64_t copy_ns, FILE *log)
{
    if (!frame || !path || !width || width > PW_DISPLAY_MAX_WIDTH || !height || height > PW_DISPLAY_MAX_HEIGHT ||
        !blocks || strlen(path) >= sizeof(captures->waiting[0].path))
        return EINVAL;
    pthread_mutex_lock(&captures->lock);
    if (!captures->started) {
        captures->stopping = 0;
        const int status = pthread_create(&captures->thread, NULL, pw_display_capture_main, captures);
        if (status) {
            pthread_mutex_unlock(&captures->lock);
            return status;
        }
        captures->started = 1;
    }
    while (captures->count == PW_DISPLAY_CAPTURE_QUEUE) pthread_cond_wait(&captures->changed, &captures->lock);
    PwDisplayCapture *job = &captures->waiting[captures->count++];
    job->frame = frame;
    job->width = width;
    job->height = height;
    job->blocks = blocks;
    job->serial = serial;
    job->copy_ns = copy_ns;
    job->log = log;
    strcpy(job->path, path);
    pthread_cond_broadcast(&captures->changed);
    pthread_mutex_unlock(&captures->lock);
    return 0;
}

static inline int pw_display_capture_take(PwDisplayCaptures *captures, uint8_t *frame, const char *path,
                                          uint64_t serial, uint64_t copy_ns, FILE *log)
{
    return pw_display_capture_take_wh(captures, frame, PW_DISPLAY_WIDTH, PW_DISPLAY_HEIGHT, PW_DISPLAY_BLOCKS, path,
                                      serial, copy_ns, log);
}

/* Waits until every picture handed over is written, then ends the thread; a later capture starts it again. */
static inline void pw_display_capture_drain(PwDisplayCaptures *captures)
{
    pthread_mutex_lock(&captures->lock);
    const int started = captures->started;
    captures->stopping = 1;
    pthread_cond_broadcast(&captures->changed);
    pthread_mutex_unlock(&captures->lock);
    if (started) pthread_join(captures->thread, NULL);
    pthread_mutex_lock(&captures->lock);
    captures->started = captures->stopping = 0;
    pthread_mutex_unlock(&captures->lock);
}
#endif
