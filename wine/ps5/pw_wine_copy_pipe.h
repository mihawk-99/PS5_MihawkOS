/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_COPY_PIPE_H
#define PW_WINE_COPY_PIPE_H
/* How the in-process wineserver copies a title process's memory for another process (ReadProcessMemory and
 * WriteProcessMemory from a helper; server/ptrace.c, Wine patch 2453, copied there by tools/build-wine-ps5.sh): through a
 * pipe, so an address that is not mapped makes the kernel answer EFAULT instead of the server taking the fault.
 *
 * The server thread is the pipe's only writer and its only reader. A blocking write it cannot finish waits for a reader
 * that never comes: on FreeBSD (the console's kernel comes from it) a blocking write of 8 KiB or more waits for the
 * reader to take it (a direct write), and anywhere a pipe with less room than the chunk does the same. A tester's game
 * in the title froze when its Steam crash reporter, in a helper, began dumping it: the server stopped for good
 * (tester-freeze-loading-2026-11-01, three sessions out of three). So both ends are non-blocking, and a short write
 * copies what it took and goes on. Returns the bytes copied: fewer than size when the target ran into a page it may not read (or write). */
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* 1 when fds holds an open non-blocking pipe (opened now if fds[0] is -1), 0 if none could be made. */
static inline int pw_wine_copy_pipe_open(int fds[2])
{
    if (fds[0] != -1) return 1;
    if (pipe(fds)) { fds[0] = fds[1] = -1; return 0; }
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) || fcntl(fds[1], F_SETFL, O_NONBLOCK)) {
        close(fds[0]); close(fds[1]);
        fds[0] = fds[1] = -1;
        return 0;
    }
    return 1;
}

/* From target into local (writing 0), or from local into target (writing 1). */
static inline unsigned pw_wine_copy_memory(int fds[2], int writing, char *local, const char *target, unsigned size)
{
    unsigned done = 0;
    char scratch[4096];

    if (!pw_wine_copy_pipe_open(fds)) return 0;
    while (done < size) {
        const unsigned chunk = size - done < 16384 ? size - done : 16384;
        /* the pipe takes what it has room for: the whole chunk, or less */
        const ssize_t sent = write(fds[1], writing ? local + done : target + done, chunk);
        ssize_t count;
        if (sent <= 0) break;   /* the source's first byte cannot be read */
        count = read(fds[0], writing ? (char *)target + done : local + done, (size_t)sent);
        if (count < sent) {
            /* the target was not writable all the way: what is left in the pipe is not wanted */
            for (;;) {
                int left = 0;
                if (ioctl(fds[0], FIONREAD, &left) || left <= 0) break;
                if (read(fds[0], scratch, left < (int)sizeof(scratch) ? (size_t)left : sizeof(scratch)) <= 0) break;
            }
            if (count > 0) done += (unsigned)count;
            break;
        }
        done += (unsigned)count;
    }
    return done;
}
#endif
