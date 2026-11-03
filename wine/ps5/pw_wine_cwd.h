/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_CWD_H
#define PW_WINE_CWD_H
/*
 * A per-module working directory for Wine's Unix side on PS5.
 *
 * A title may not change its working directory: chdir() is EPERM for every
 * path and libc's getcwd() faults (FW 12.02). Wine relies on one anyway:
 * ntdll enters the prefix to create dosdevices and lists directories through
 * fchdir, and wineserver saves the registry from its configuration and
 * server directories. pw_wine_cwd_libc.c wraps those calls (lld --wrap) and
 * resolves relative paths against the directory kept here. Each module links
 * its own copy, so the in-process wineserver has its own directory, as the
 * separate server process has on other hosts.
 *
 * The directory is logical: chdir() folds "." and ".." lexically, like a
 * shell's cd. Paths passed to other calls are only joined to it; the kernel
 * resolves them, so symbolic links keep their meaning.
 *
 * A title cannot create symbolic links either: symlink() is libkernel_sys
 * only, and a system call from title code kills the process (FW 12.02,
 * PPRBUG-22859). Wine needs two, dosdevices/c: -> ../drive_c and z: -> /, so
 * links are virtual: kept here, followed by every wrapped call, and saved in
 * a PW_CWD_LINK_TABLE file in the link's directory, which is read the first
 * time a lookup below that directory fails, the directory as named or as
 * reached through other links (C:\users\steamuser: dosdevices/c:, then
 * drive_c/users' table; src/pw_wine_profile.h).
 */
#include <stddef.h>
#include <sys/types.h>

enum {
    /* PW_CWD_MAX_FDS: descriptor numbers whose path is kept. The title's in-process server held descriptor 8269 with
     * Steam and a game running, and every name opened relative to a directory above 4096 was refused (a .NET game's
     * "access denied" on Content\\Dialog\\Fonts, 2026-10-24). The table is pointers in zeroed memory: untouched pages cost nothing. */
    PW_CWD_PATH_MAX = 1024, PW_CWD_MAX_FDS = 65536,
    PW_CWD_MAX_LINKS = 64, PW_CWD_MAX_HOPS = 16, PW_CWD_MAX_PROBED = 1024
};
#define PW_CWD_LINK_TABLE ".pw-symlinks"

/* The directory relative paths are resolved against; "/" at start. */
size_t pw_cwd_get(char *out, size_t size);
/* Make an absolute directory path current, folded lexically. 0, or -1 with
 * errno ENAMETOOLONG or EINVAL for a relative path. */
int pw_cwd_set(const char *absolute);

/* Join path to base (or to the current directory when base is NULL);
 * absolute paths are copied unchanged. 0, or -1 with errno ENAMETOOLONG. */
int pw_cwd_join(const char *base, const char *path, char *out, size_t size);
/* Fold "//", "." and ".." out of an absolute path. 0, or -1 with errno
 * ENAMETOOLONG or EINVAL for a relative path. */
int pw_cwd_fold(const char *absolute, char *out, size_t size);

/* The path each descriptor was opened by, so fchdir() and the *at() calls can
 * resolve against it; fchdir() checks it still names the descriptor. */
void pw_cwd_track(int fd, const char *absolute);
void pw_cwd_forget(int fd);
/* ntdll's suspend guard around the lock (pw_ntsync_critical_enter/leave), set once; the server's copy has none. */
void pw_cwd_set_guard(void (*enter)(void), void (*leave)(void));
/* Copy the recorded path of fd; 0, or -1 when none is recorded. */
int pw_cwd_fd_path(int fd, char *out, size_t size);
/* Resolve path for an *at() call: AT_FDCWD (at_fdcwd) and absolute paths as
 * above, a recorded dirfd by its path. 0, or -1 with errno EBADF when dirfd
 * has no record (the kernel's *at() calls are not linked into a title),
 * ENOENT for an empty path, or ENAMETOOLONG. */
int pw_cwd_resolve_at(int dirfd, int at_fdcwd, const char *path, char *out, size_t size);

/* access() from a file's mode bits, for a title whose access() is EPERM even
 * where open() works: the title is the only user of its own storage, so any
 * of the user, group or other bits grants. want is R_OK/W_OK/X_OK flags. */
int pw_cwd_mode_allows(unsigned int st_mode, int want);

/* Record a link at an absolute path; target is kept as given, and a relative
 * one is resolved from the link's directory. 0, or -1 with errno EEXIST,
 * ENOSPC (table full), ENOENT (empty target), EINVAL or ENAMETOOLONG. */
int pw_cwd_link_add(const char *absolute, const char *target);
/* Forget the link at absolute; 0, or -1 with errno ENOENT. */
int pw_cwd_link_remove(const char *absolute);
/* readlink() of a virtual link: the target, truncated to size and not
 * terminated; its length, or -1 with errno EINVAL when absolute is no link. */
ssize_t pw_cwd_link_target(const char *absolute, char *out, size_t size);
/* Replace every link among the directories of an absolute path, and the path
 * itself when follow_last, by its target. A path through no link is copied
 * unchanged. 0, or -1 with errno ELOOP or ENAMETOOLONG. */
int pw_cwd_follow(const char *absolute, int follow_last, char *out, size_t size);
/* 1 the first time a directory is asked about (its table should be read), 0
 * after that. */
int pw_cwd_probe_directory(const char *absolute);
/* The table of the links in directory, one "name<TAB>target" line each;
 * its length, or -1 with errno ENAMETOOLONG. */
ssize_t pw_cwd_links_format(const char *directory, char *out, size_t size);
/* Record the links of a table read from directory; how many were new. */
int pw_cwd_links_parse(const char *directory, const char *text, size_t length);
#endif
