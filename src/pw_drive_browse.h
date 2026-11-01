/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_DRIVE_BROWSE_H
#define PW_DRIVE_BROWSE_H
/* A folder of a prefix's drive_c, as the launcher's Files page and the WebUI's browse show it: its folders and its
 * programs (.exe), never a link, never a hidden name, never anything above drive_c. Read-only: nothing here writes or
 * deletes (the owner, 2026-10-31: Files browses and launches). Host-tested (tests/test_pw_drive_browse.c).
 *
 * A folder is named by a relative path under drive_c, '/'-separated ("" is drive_c itself); each part is a name the
 * folder holds (pw_drive_part): no "." or "..", no separator, no ':', no control character, so no path climbs out. A
 * link on the way is refused, so none leads out either (dosdevices' z: points at the console's root). */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PwDriveEntry {
    char name[256];
    int dir;            /* 1: a folder, 0: a program */
    long long size;     /* bytes, programs only */
} PwDriveEntry;

/* 1 for a part a relative path may hold. */
int pw_drive_part(const char *part);
/* 1 for a program the launcher can start: a name ending in .exe, any case. */
int pw_drive_is_program(const char *name);
/* The folder's host path, <data_root>/prefixes/<prefix>/drive_c/<relative>, into out: 0, or EINVAL (a bad prefix or
 * part), ENOENT (no such folder: a missing one, a file, or a link on the way), ENAMETOOLONG. */
int pw_drive_folder(const char *data_root, const char *prefix, const char *relative, char *out, size_t size);
/* The folders and programs folder holds, folders first, each group by name ignoring case (then by bytes), into a
 * malloc'd array (*out, *count; free with free()); *others counts the files left out (not programs). 0 or errno. */
int pw_drive_list(const char *folder, PwDriveEntry **out, size_t *count, size_t *others);
/* relative, then name, as a Windows path on C: ("Games/Tide" + "Tide.exe" -> "C:\Games\Tide\Tide.exe"; "" + "" ->
 * "C:\"). 0, EINVAL (a bad part) or ENAMETOOLONG. */
int pw_drive_windows_path(const char *relative, const char *name, char *out, size_t size);
/* relative one level up ("Games/Tide" -> "Games", "Games" -> ""); 0, or ENOENT at drive_c itself. */
int pw_drive_parent(const char *relative, char *out, size_t size);
/* relative with one more part ("" + "Games" -> "Games"); 0, EINVAL or ENAMETOOLONG. */
int pw_drive_child(const char *relative, const char *part, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
