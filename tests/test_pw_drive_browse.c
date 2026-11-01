/* SPDX-License-Identifier: GPL-3.0-or-later */
/* src/pw_drive_browse.h: listing a made-up drive_c (folders first, by name ignoring case), the .exe filter, the guard
 * that keeps every path under drive_c (no "..", no separator in a part, no link on the way), and the Windows path the
 * launch request carries. */
#include "../src/pw_drive_browse.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void touch(const char *path, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    for (size_t i = 0; i < size; ++i) fputc('x', file);
    fclose(file);
}

int main(void)
{
    char root[] = "/tmp/pw-drive-XXXXXX", path[512], out[512];
    assert(mkdtemp(root));
    const char *dirs[] = { "prefixes", "prefixes/mihawk", "prefixes/mihawk/drive_c", "prefixes/mihawk/drive_c/Games",
                           "prefixes/mihawk/drive_c/Games/Tide", "prefixes/mihawk/drive_c/Games/apple",
                           "prefixes/mihawk/drive_c/Games/Zeta", "prefixes/mihawk/drive_c/Games/.hidden", "outside" };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        assert(!mkdir(path, 0755));
    }
    const char *files[] = { "Games/Tide/Tide.exe", "Games/Tide/readme.txt", "Games/Tide/engine.DLL", "Games/Tide/setup.EXE",
                            "Games/Tide/Bonus.Exe", "Games/b.exe", "Games/A.exe" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "%s/prefixes/mihawk/drive_c/%s", root, files[i]);
        touch(path, 10 + i);
    }
    /* a link out of drive_c (as dosdevices' z: is), a link to a program, and a folder link */
    snprintf(path, sizeof(path), "%s/prefixes/mihawk/drive_c/Games/out", root);
    snprintf(out, sizeof(out), "%s/outside", root);
    assert(!symlink(out, path));
    snprintf(path, sizeof(path), "%s/prefixes/mihawk/drive_c/Games/Tide/link.exe", root);
    assert(!symlink("Tide.exe", path));

    /* The .exe filter: programs in any case, nothing else */
    assert(pw_drive_is_program("Tide.exe") && pw_drive_is_program("SETUP.EXE") && pw_drive_is_program("a.Exe"));
    assert(!pw_drive_is_program(".exe") && !pw_drive_is_program("engine.dll") && !pw_drive_is_program("exe") &&
           !pw_drive_is_program("Tide.exe.txt") && !pw_drive_is_program(NULL));

    /* Listing: folders first, then programs, each by name ignoring case; others counted, links and hidden names left out */
    assert(!pw_drive_folder(root, "mihawk", "Games", out, sizeof(out)));
    PwDriveEntry *list;
    size_t count, others;
    assert(!pw_drive_list(out, &list, &count, &others));
    const char *games[] = { "apple", "Tide", "Zeta", "A.exe", "b.exe" };
    assert(count == 5 && others == 0);
    for (size_t i = 0; i < count; ++i) assert(!strcmp(list[i].name, games[i]) && list[i].dir == (i < 3));
    free(list);
    assert(!pw_drive_folder(root, "mihawk", "Games/Tide", out, sizeof(out)));
    assert(!pw_drive_list(out, &list, &count, &others));
    const char *tide[] = { "Bonus.Exe", "setup.EXE", "Tide.exe" };
    assert(count == 3 && others == 2);   /* readme.txt and engine.DLL are not programs; link.exe is a link */
    for (size_t i = 0; i < count; ++i) assert(!strcmp(list[i].name, tide[i]) && !list[i].dir && list[i].size > 0);
    free(list);
    assert(!pw_drive_folder(root, "mihawk", "Games/Zeta", out, sizeof(out)));
    assert(!pw_drive_list(out, &list, &count, &others) && count == 0 && others == 0);   /* an empty folder */
    free(list);

    /* The guard: nothing above drive_c, no link on the way, no bad prefix */
    assert(!pw_drive_folder(root, "mihawk", "", out, sizeof(out)) && strstr(out, "/drive_c") && !strstr(out, "drive_c/"));
    assert(pw_drive_folder(root, "mihawk", "..", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "Games/..", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "Games/../..", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "/etc", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "Games//Tide", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "Games\\..", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "C:", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "Games/out", out, sizeof(out)) == ENOENT);       /* the link out */
    assert(pw_drive_folder(root, "mihawk", "Games/.hidden", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "mihawk", "Games/Tide/Tide.exe", out, sizeof(out)) == ENOENT);   /* a file */
    assert(pw_drive_folder(root, "mihawk", "Missing", out, sizeof(out)) == ENOENT);
    assert(pw_drive_folder(root, "../mihawk", "", out, sizeof(out)) == EINVAL);
    assert(pw_drive_folder(root, "nope", "", out, sizeof(out)) == ENOENT);
    assert(pw_drive_folder("/no/such/data", "mihawk", "", out, sizeof(out)) == ENOENT);   /* no /data: no drive_c */
    assert(pw_drive_folder(root, "mihawk", "Games", out, 10) == ENAMETOOLONG);

    /* Moving: up stops at drive_c, down takes one checked part */
    assert(!pw_drive_parent("Games/Tide", out, sizeof(out)) && !strcmp(out, "Games"));
    assert(!pw_drive_parent("Games", out, sizeof(out)) && !strcmp(out, ""));
    assert(pw_drive_parent("", out, sizeof(out)) == ENOENT);
    assert(!pw_drive_child("", "Games", out, sizeof(out)) && !strcmp(out, "Games"));
    assert(!pw_drive_child("Games", "Tide", out, sizeof(out)) && !strcmp(out, "Games/Tide"));
    assert(pw_drive_child("Games", "..", out, sizeof(out)) == EINVAL && pw_drive_child("Games", "a/b", out, sizeof(out)) == EINVAL);

    /* The launch request's path: the program as Windows names it on C:, every part checked again */
    assert(!pw_drive_windows_path("Games/Tide", "Tide.exe", out, sizeof(out)) && !strcmp(out, "C:\\Games\\Tide\\Tide.exe"));
    assert(!pw_drive_windows_path("", "A.exe", out, sizeof(out)) && !strcmp(out, "C:\\A.exe"));
    assert(!pw_drive_windows_path("Games", "", out, sizeof(out)) && !strcmp(out, "C:\\Games"));
    assert(!pw_drive_windows_path("", "", out, sizeof(out)) && !strcmp(out, "C:\\"));
    assert(pw_drive_windows_path("Games/..", "x.exe", out, sizeof(out)) == EINVAL);
    assert(pw_drive_windows_path("Games", "..", out, sizeof(out)) == EINVAL);
    assert(pw_drive_windows_path("/Games", "x.exe", out, sizeof(out)) == EINVAL);
    assert(pw_drive_windows_path("Games", "x.exe", out, 8) == ENAMETOOLONG);

    char command[600];
    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    assert(!system(command));
    printf("drive browse: listing, sorting, the .exe filter, the guard and the launch path\n");
    return 0;
}
