/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Virtual links through the libc wrappers (wine/ps5/pw_wine_cwd_libc.c), as Wine's Unix side calls them: a table in a
 * folder that is reached through another link. C:\users\steamuser\... is dosdevices/c: -> ../drive_c, then
 * drive_c/users/.pw-symlinks, steamuser -> Mihawk (src/pw_wine_profile.h); on the console (profile-move-base-01) the
 * second table was never read, since it was looked for at dosdevices/c:/users, which the kernel does not have.
 * Built with --wrap for every call the file wraps (tools/host-tests.sh), so this file's calls reach the wrappers. */
#include "../wine/ps5/pw_wine_fdv.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "pw_wine_cwd.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const struct pw_wine_fdv *pw_wine_fdv;   /* no open-file budget: the wrappers call the kernel's open */

static char base[] = "/tmp/pw-cwd-links-XXXXXX";

static const char *at(const char *relative)
{
    static char paths[4][512];
    static int next;
    char *path = paths[next++ % 4];
    snprintf(path, sizeof(paths[0]), "%s/%s", base, relative);
    return path;
}

static void put(const char *relative, const char *text)
{
    FILE *file = fopen(at(relative), "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static const char *read_all(const char *path)
{
    static char text[256];
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    const ssize_t got = read(fd, text, sizeof(text) - 1);
    close(fd);
    text[got > 0 ? got : 0] = 0;
    return text;
}

static int guard_entered, guard_left, guard_depth;
static void guard_in(void) { guard_entered++; guard_depth++; }
static void guard_out(void) { guard_left++; guard_depth--; errno = EINTR; }   /* a suspend's handler may change errno */

int main(void)
{
    struct stat state;
    assert(mkdtemp(base));
    static const char *const folders[] = {"dosdevices", "drive_c", "drive_c/users", "drive_c/users/Mihawk",
                                          "drive_c/users/Mihawk/Documents"};
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) assert(mkdir(at(folders[i]), 0777) == 0);
    put("dosdevices/.pw-symlinks", "c:\t../drive_c\nz:\t/\n");
    put("drive_c/users/.pw-symlinks", "steamuser\tMihawk\n");
    put("drive_c/users/Mihawk/Documents/save.txt", "day 12");

    /* First through the drive letter, the way Wine names C:\users\steamuser\Documents\save.txt. */
    const char *text = read_all(at("dosdevices/c:/users/steamuser/Documents/save.txt"));
    assert(text && !strcmp(text, "day 12"));
    assert(stat(at("dosdevices/c:/users/steamuser/Documents"), &state) == 0 && S_ISDIR(state.st_mode));
    /* A file made through the old name lands in the new folder. */
    put("dosdevices/c:/users/steamuser/Documents/new.txt", "made later");
    assert(!strcmp(read_all(at("drive_c/users/Mihawk/Documents/new.txt")), "made later"));
    /* The folder's own path and a missing name still answer as before. */
    assert(!strcmp(read_all(at("drive_c/users/steamuser/Documents/save.txt")), "day 12"));
    assert(stat(at("dosdevices/c:/users/nobody/Documents"), &state) == -1 && errno == ENOENT);

    /* A directory descriptor numbered past 4096 keeps its path: the title's server reached 8269 (a .NET game, 2026-10-24). */
    {
        char joined[PW_CWD_PATH_MAX];
        pw_cwd_track(8269, at("drive_c"));
        assert(pw_cwd_resolve_at(8269, -100, "users", joined, sizeof(joined)) == 0 && !strcmp(joined, at("drive_c/users")));
        pw_cwd_forget(8269);
        assert(pw_cwd_resolve_at(8269, -100, "users", joined, sizeof(joined)) == -1);
    }

    /* The suspend guard brackets every hold of the path lock, and errno comes through it. */
    {
        char joined[PW_CWD_PATH_MAX];
        pw_cwd_set_guard(guard_in, guard_out);
        pw_cwd_track(5, at("drive_c"));
        assert(pw_cwd_resolve_at(5, -100, "users", joined, sizeof(joined)) == 0);
        pw_cwd_forget(5);
        assert(pw_cwd_resolve_at(5, -100, "users", joined, sizeof(joined)) == -1);
        assert(stat(at("dosdevices/c:/users/nobody/Documents"), &state) == -1 && errno == ENOENT);
        assert(guard_entered >= 4 && guard_entered == guard_left && !guard_depth);
        pw_cwd_set_guard(NULL, NULL);
    }

    char cleanup[600];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", base);
    assert(system(cleanup) == 0);
    printf("wine cwd links passed: a link table in a folder reached through another link (C: users steamuser), "
           "read, stat and create through it, missing names still ENOENT\n");
    return 0;
}
