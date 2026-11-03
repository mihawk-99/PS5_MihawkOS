/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_cwd.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_rwlock_t cwd_lock = PTHREAD_RWLOCK_INITIALIZER;
static char cwd[PW_CWD_PATH_MAX] = "/";
static char *fd_paths[PW_CWD_MAX_FDS];

/* The guard around the lock (pw_cwd_set_guard); none until set. Every path call reads under the lock with signals open
 * and close() writes under it (pw_cwd_forget): a thread suspended holding it stopped a close inside Wine's merged-write
 * lock, whose own suspend then never landed (suspend-server64-title, a8final-base-2026-11-02). */
static void (*guard_enter)(void), (*guard_leave)(void);

void pw_cwd_set_guard(void (*enter)(void), void (*leave)(void))
{
    guard_leave = leave;
    guard_enter = enter;
}

static void lock_cwd(int write)
{
    if (guard_enter) guard_enter();
    if (write) pthread_rwlock_wrlock(&cwd_lock);
    else pthread_rwlock_rdlock(&cwd_lock);
}

static void unlock_cwd(void)
{
    int saved = errno;   /* a suspend that landed meanwhile runs in the leave */

    pthread_rwlock_unlock(&cwd_lock);
    if (guard_leave) guard_leave();
    errno = saved;
}

static int copy_out(const char *text, size_t length, char *out, size_t size)
{
    if (length >= size) { errno = ENAMETOOLONG; return -1; }
    memcpy(out, text, length);
    out[length] = 0;
    return 0;
}

size_t pw_cwd_get(char *out, size_t size)
{
    size_t length;

    lock_cwd(0);
    length = strlen(cwd);
    if (copy_out(cwd, length, out, size)) length = 0;
    unlock_cwd();
    return length;
}

int pw_cwd_fold(const char *absolute, char *out, size_t size)
{
    size_t used = 0;
    const char *p = absolute;

    if (!absolute || absolute[0] != '/' || size < 2) { errno = EINVAL; return -1; }
    while (*p) {
        const char *name;
        size_t length;

        while (*p == '/') p++;
        name = p;
        while (*p && *p != '/') p++;
        length = (size_t)(p - name);
        if (!length || (length == 1 && name[0] == '.')) continue;
        if (length == 2 && name[0] == '.' && name[1] == '.') {
            while (used && out[used] != '/') used--;   /* "/.." stays "/" */
            continue;
        }
        if (used + 1 + length >= size) { errno = ENAMETOOLONG; return -1; }
        out[used++] = '/';
        memcpy(out + used, name, length);
        used += length;
        out[used] = 0;
    }
    if (!used) out[used++] = '/';
    out[used] = 0;
    return 0;
}

int pw_cwd_set(const char *absolute)
{
    char folded[PW_CWD_PATH_MAX];

    if (pw_cwd_fold(absolute, folded, sizeof(folded))) return -1;
    lock_cwd(1);
    memcpy(cwd, folded, strlen(folded) + 1);
    unlock_cwd();
    return 0;
}

static int join_locked(const char *base, const char *path, char *out, size_t size)
{
    size_t base_length = strlen(base), path_length = strlen(path);

    if (base_length && base[base_length - 1] == '/') base_length--;
    if (base_length + 1 + path_length >= size) { errno = ENAMETOOLONG; return -1; }
    memcpy(out, base, base_length);
    out[base_length] = '/';
    memcpy(out + base_length + 1, path, path_length + 1);
    return 0;
}

int pw_cwd_join(const char *base, const char *path, char *out, size_t size)
{
    int status;

    if (!path) { errno = EFAULT; return -1; }
    if (path[0] == '/') return copy_out(path, strlen(path), out, size);
    if (!path[0]) { errno = ENOENT; return -1; }
    if (base) return join_locked(base, path, out, size);
    lock_cwd(0);
    status = join_locked(cwd, path, out, size);
    unlock_cwd();
    return status;
}

void pw_cwd_track(int fd, const char *absolute)
{
    char *copy;

    if (fd < 0 || fd >= PW_CWD_MAX_FDS || !absolute || absolute[0] != '/') return;
    copy = strdup(absolute);
    lock_cwd(1);
    free(fd_paths[fd]);
    fd_paths[fd] = copy;           /* NULL when out of memory: untracked */
    unlock_cwd();
}

void pw_cwd_forget(int fd)
{
    char *old;

    if (fd < 0 || fd >= PW_CWD_MAX_FDS) return;
    lock_cwd(1);
    old = fd_paths[fd];
    fd_paths[fd] = NULL;
    unlock_cwd();
    free(old);
}

int pw_cwd_fd_path(int fd, char *out, size_t size)
{
    int status = -1;

    if (fd < 0 || fd >= PW_CWD_MAX_FDS) return -1;
    lock_cwd(0);
    if (fd_paths[fd]) status = copy_out(fd_paths[fd], strlen(fd_paths[fd]), out, size);
    unlock_cwd();
    return status;
}

int pw_cwd_resolve_at(int dirfd, int at_fdcwd, const char *path, char *out, size_t size)
{
    int status;

    if (!path) { errno = EFAULT; return -1; }
    if (path[0] == '/' || dirfd == at_fdcwd) return pw_cwd_join(NULL, path, out, size);
    if (!path[0]) { errno = ENOENT; return -1; }
    if (dirfd < 0 || dirfd >= PW_CWD_MAX_FDS) { errno = EBADF; return -1; }
    lock_cwd(0);
    status = fd_paths[dirfd] ? join_locked(fd_paths[dirfd], path, out, size) : -2;
    unlock_cwd();
    if (status == -2) { errno = EBADF; status = -1; }
    return status;
}

int pw_cwd_mode_allows(unsigned int st_mode, int want)
{
    return !((want & R_OK) && !(st_mode & (S_IRUSR | S_IRGRP | S_IROTH))) &&
           !((want & W_OK) && !(st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) &&
           !((want & X_OK) && !(st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)));
}

/* Virtual symbolic links: folded absolute link path -> target as given. */
static struct { char *path, *target; } links[PW_CWD_MAX_LINKS];
static size_t link_count;
/* Directories whose table was already looked for, by path hash. */
static char *probed[PW_CWD_MAX_PROBED];

static size_t parent_length(const char *absolute)
{
    const char *slash = strrchr(absolute, '/');
    return slash == absolute ? 1 : (size_t)(slash - absolute);
}

static int find_link_locked(const char *folded)
{
    size_t i;

    for (i = 0; i < link_count; i++) if (!strcmp(links[i].path, folded)) return (int)i;
    return -1;
}

int pw_cwd_link_add(const char *absolute, const char *target)
{
    char folded[PW_CWD_PATH_MAX], *path, *copy;
    int status = 0;

    if (!target) { errno = EFAULT; return -1; }
    if (!target[0] || strpbrk(target, "\t\n")) { errno = ENOENT; return -1; }
    if (pw_cwd_fold(absolute, folded, sizeof(folded))) return -1;
    if (!strcmp(folded, "/") || strpbrk(folded, "\t\n")) { errno = EINVAL; return -1; }
    path = strdup(folded);
    copy = strdup(target);
    lock_cwd(1);
    if (!path || !copy) { errno = ENOMEM; status = -1; }
    else if (find_link_locked(folded) >= 0) { errno = EEXIST; status = -1; }
    else if (link_count == PW_CWD_MAX_LINKS) { errno = ENOSPC; status = -1; }
    else {
        links[link_count].path = path;
        links[link_count++].target = copy;
        path = copy = NULL;
    }
    unlock_cwd();
    free(path);
    free(copy);
    return status;
}

int pw_cwd_link_remove(const char *absolute)
{
    char folded[PW_CWD_PATH_MAX];
    char *path = NULL, *target = NULL;
    int i;

    if (pw_cwd_fold(absolute, folded, sizeof(folded))) return -1;
    lock_cwd(1);
    if ((i = find_link_locked(folded)) >= 0) {
        path = links[i].path;
        target = links[i].target;
        links[i] = links[--link_count];
    }
    unlock_cwd();
    if (i < 0) { errno = ENOENT; return -1; }
    free(path);
    free(target);
    return 0;
}

ssize_t pw_cwd_link_target(const char *absolute, char *out, size_t size)
{
    char folded[PW_CWD_PATH_MAX];
    ssize_t length = -1;
    int i;

    if (pw_cwd_fold(absolute, folded, sizeof(folded))) return -1;
    lock_cwd(0);
    if ((i = find_link_locked(folded)) >= 0) {
        length = (ssize_t)strlen(links[i].target);
        if ((size_t)length > size) length = (ssize_t)size;   /* readlink() truncates */
        memcpy(out, links[i].target, (size_t)length);
    }
    unlock_cwd();
    if (i < 0) errno = EINVAL;
    return length;
}

int pw_cwd_follow(const char *absolute, int follow_last, char *out, size_t size)
{
    /* done: the resolved directories so far; todo: what is left to walk. A
     * link found on the way puts its target in front of the rest, so ".."
     * after a link leaves the target's directory, as with a real one. */
    char done[PW_CWD_PATH_MAX], todo[PW_CWD_PATH_MAX], next[PW_CWD_PATH_MAX];
    size_t used = 0;
    int hops = 0, status = 0;
    const char *p;

    if (!absolute || absolute[0] != '/') { errno = EINVAL; return -1; }
    lock_cwd(0);
    if (!link_count || copy_out(absolute, strlen(absolute), todo, sizeof(todo))) {
        unlock_cwd();
        return copy_out(absolute, strlen(absolute), out, size);
    }
    done[0] = 0;
    p = todo;
    while (*p) {
        const char *name;
        size_t length;
        int i;

        while (*p == '/') p++;
        name = p;
        while (*p && *p != '/') p++;
        length = (size_t)(p - name);
        if (!length || (length == 1 && name[0] == '.')) continue;
        if (length == 2 && name[0] == '.' && name[1] == '.') {
            while (used && done[used] != '/') used--;
            done[used] = 0;
            continue;
        }
        if (used + 1 + length >= sizeof(done)) { errno = ENAMETOOLONG; status = -1; break; }
        done[used++] = '/';
        memcpy(done + used, name, length);
        used += length;
        done[used] = 0;
        while (*p == '/') p++;
        if ((!*p && !follow_last) || (i = find_link_locked(done)) < 0) continue;
        if (++hops > PW_CWD_MAX_HOPS) { errno = ELOOP; status = -1; break; }
        {
            const char *target = links[i].target;
            size_t target_length = strlen(target), rest_length = strlen(p);

            if (target_length + 1 + rest_length >= sizeof(next)) {
                errno = ENAMETOOLONG;
                status = -1;
                break;
            }
            memcpy(next, target, target_length);
            next[target_length] = '/';
            memcpy(next + target_length + 1, p, rest_length + 1);
            memcpy(todo, next, target_length + rest_length + 2);
            p = todo;
            if (target[0] == '/') used = 0;
            else while (used && done[used] != '/') used--;   /* the link's directory */
            done[used] = 0;
        }
    }
    unlock_cwd();
    if (status) return -1;
    /* No link on the way: the path goes to the kernel as it came. */
    if (!hops) return copy_out(absolute, strlen(absolute), out, size);
    if (!used) done[used++] = '/', done[used] = 0;
    return copy_out(done, used, out, size);
}

static size_t hash_path(const char *text)
{
    size_t hash = 2166136261u;

    while (*text) hash = (hash ^ (unsigned char)*text++) * 16777619u;
    return hash;
}

int pw_cwd_probe_directory(const char *absolute)
{
    size_t slot = hash_path(absolute) % PW_CWD_MAX_PROBED, tries;
    int needed = 1;

    lock_cwd(1);
    for (tries = 0; tries < PW_CWD_MAX_PROBED; tries++, slot = (slot + 1) % PW_CWD_MAX_PROBED) {
        if (!probed[slot]) { probed[slot] = strdup(absolute); break; }  /* NULL: probe again */
        if (!strcmp(probed[slot], absolute)) { needed = 0; break; }
    }
    unlock_cwd();
    return needed;
}

ssize_t pw_cwd_links_format(const char *directory, char *out, size_t size)
{
    size_t used = 0, i, length = strlen(directory);
    int status = 0;

    lock_cwd(0);
    for (i = 0; i < link_count && !status; i++) {
        const char *name = links[i].path + parent_length(links[i].path);
        size_t name_length, target_length = strlen(links[i].target);

        if (parent_length(links[i].path) != length || strncmp(links[i].path, directory, length))
            continue;
        if (*name == '/') name++;
        name_length = strlen(name);
        if (used + name_length + target_length + 2 >= size) { errno = ENAMETOOLONG; status = -1; break; }
        memcpy(out + used, name, name_length);
        out[used + name_length] = '\t';
        memcpy(out + used + name_length + 1, links[i].target, target_length);
        out[used + name_length + 1 + target_length] = '\n';
        used += name_length + target_length + 2;
    }
    unlock_cwd();
    if (status) return -1;
    out[used] = 0;
    return (ssize_t)used;
}

int pw_cwd_links_parse(const char *directory, const char *text, size_t length)
{
    const char *end = text + length;
    int added = 0;

    while (text < end) {
        const char *line_end = memchr(text, '\n', (size_t)(end - text));
        const char *tab = memchr(text, '\t', (size_t)((line_end ? line_end : end) - text));
        char path[PW_CWD_PATH_MAX], target[PW_CWD_PATH_MAX], name[PW_CWD_PATH_MAX];

        if (!line_end) line_end = end;
        if (tab && tab > text && tab + 1 < line_end &&
            (size_t)(tab - text) < sizeof(name) && (size_t)(line_end - tab - 1) < sizeof(target)) {
            memcpy(name, text, (size_t)(tab - text));
            name[tab - text] = 0;
            memcpy(target, tab + 1, (size_t)(line_end - tab - 1));
            target[line_end - tab - 1] = 0;
            if (!strchr(name, '/') && strcmp(name, ".") && strcmp(name, "..") &&
                !join_locked(directory, name, path, sizeof(path)) &&
                !pw_cwd_link_add(path, target))
                added++;
        }
        text = line_end + 1;
    }
    return added;
}
