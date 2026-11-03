/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-process server's copy of a title process's memory (wine/ps5/pw_wine_copy_pipe.h, Wine patch 2453): whole
 * copies of every size, a copy that stops at a page it may not read or write, and a pipe with less room than a chunk,
 * which a blocking pipe turned into a write that waits forever for its own thread (alarm ends the test instead). */
#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../wine/ps5/pw_wine_copy_pipe.h"

static void timed_out(int signal)
{
    (void)signal;
    static const char text[] = "copy pipe: FAIL a copy blocked on its own pipe\n";
    if (write(2, text, sizeof(text) - 1)) {}
    _exit(1);
}

static void round_trip(int fds[2], unsigned size)
{
    char *source = calloc(1, size), *copy = calloc(1, size), *back = calloc(1, size);
    for (unsigned i = 0; i < size; i++) source[i] = (char)(i * 7 + size);
    assert(pw_wine_copy_memory(fds, 0, copy, source, size) == size && !memcmp(copy, source, size));
    assert(pw_wine_copy_memory(fds, 1, copy, back, size) == size && !memcmp(back, source, size));
    free(source); free(copy); free(back);
}

int main(void)
{
    const long page = sysconf(_SC_PAGESIZE);
    int fds[2] = {-1, -1};
    signal(SIGALRM, timed_out);
    alarm(10);
    assert(pw_wine_copy_pipe_open(fds) && fds[0] != -1 && (fcntl(fds[1], F_GETFL) & O_NONBLOCK));
    const unsigned sizes[] = {1, 4096, 8192, 16384, 16385, 100000, 262144};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) round_trip(fds, sizes[i]);
    /* Two pages, the second no-access: a read stops at it, a write into it too, and leaves the pipe empty. */
    char *pages = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pages != MAP_FAILED && !mprotect(pages + page, page, PROT_NONE));
    char *local = calloc(1, 2 * page);
    assert(pw_wine_copy_memory(fds, 0, local, pages, 2 * page) == (unsigned)page);
    assert(pw_wine_copy_memory(fds, 1, local, pages, 2 * page) == (unsigned)page);
    int left = -1;
    assert(!ioctl(fds[0], FIONREAD, &left) && left == 0);
    round_trip(fds, 65536);   /* the pipe still copies whole */
    free(local);
    munmap(pages, 2 * page);
#ifdef F_SETPIPE_SZ
    /* A pipe with one page of room: the old blocking write of a 16 KiB chunk never returned. */
    int small[2] = {-1, -1};
    assert(pw_wine_copy_pipe_open(small) && fcntl(small[1], F_SETPIPE_SZ, 4096) >= 0);
    round_trip(small, 65536);
    round_trip(small, 262144);
#endif
    puts("copy pipe: PASS whole copies, stops at a page it may not touch, a pipe with less room than a chunk");
    return 0;
}
