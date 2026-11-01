/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_FDV_H
#define PW_WINE_FDV_H
/*
 * Open files past the console's limit, for Wine's Unix side.
 *
 * A title can hold about 256 files open at once (ps5platform/fdv.h has the
 * measurements), and Wine holds one descriptor for every Windows file handle
 * in the server, another in the client's cache of descriptors, and a Java
 * program's jars and region files are Windows handles. The platform layer's
 * descriptor budget parks idle files (the descriptor numbers stay, held by a
 * socket, and the file is reopened on the next call that uses one) and this
 * is how Wine reaches it:
 *
 *   pw_wine_fdv_libc.c  --wrap's every libc call that takes a descriptor, in
 *                       ntdll and in the server, so each call pins its file;
 *   pw_wine_fdv.c       the one instance, in ntdll: the descriptors of both
 *                       modules are one table, and the budget must be too.
 *
 * The server is a separate module with its own copy of the wraps; the
 * loader hands it ntdll's table through pw_module_share as it loads, and until
 * then (or if ntdll has none) its wraps call libc.
 */
#include <stdint.h>
#include <sys/types.h>

struct pw_wine_fdv {
    int (*open)(const char *path, int flags, mode_t mode);
    int (*close)(int fd);
    int (*enter)(int fd, uint32_t *pin);
    void (*leave)(uint32_t pin);
    int (*sending)(int fd, uint32_t *pin);
    void (*sent)(uint32_t pin, int delivered);
    int (*received)(int fd);
    void (*dup_note)(int from, int to);
    void (*forget)(int fd);
    void (*keep)(uint32_t pin);
    void (*renamed)(const char *from, const char *to);
    unsigned (*make_room)(void);
};

/* This module's table; NULL means plain libc. */
extern const struct pw_wine_fdv *pw_wine_fdv;

/* What a module that links pw_wine_fdv_libc.c receives from the loader. */
void pw_module_share(const void *table);

/* ntdll only. */
const struct pw_wine_fdv *pw_wine_fdv_table(void);

typedef struct PwWineFdvStats {
    unsigned live, parked, high_water;
    unsigned long parks, unparks, limit_hits, failures;
    int disabled;
    unsigned code_copies;        /* shared code copies the title holds (pw_wine_codeshare.h): each a descriptor */
    unsigned long code_released; /* of those, taken back for a full descriptor table */
} PwWineFdvStats;
void pw_wine_fdv_stats(PwWineFdvStats *out);

#endif /* PW_WINE_FDV_H */
