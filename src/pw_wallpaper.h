/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WALLPAPER_H
#define PW_WALLPAPER_H
#include <stddef.h>
/* The player's wallpaper (MihawkOS's Settings > Wallpaper, saved beside controls.txt as wallpaper.txt).
 *
 * The file holds one line, "wallpaper <id>", and can be written by hand. An id is a name of letters, digits, '.', '-'
 * and '_' (nothing that could leave the folder); it names a picture "<id>.bmp" in the prefix's
 * C:\windows\web\wallpaper. The shipped pictures (the catalog below) are in the runtime, outside every prefix
 * (share/mihawk/wallpapers, so a prefix made before them has them too); a 1920 x 1080 BMP a person copies into that
 * folder answers to its own file name, with no catalog entry, and wins over a shipped one of the same name. A missing, unreadable or
 * invalid file means the default, Bubble City by Skeuoss (NOTICE.md).
 *
 * The desktop paints proton.bmp in that folder (win32u's managed background, wine/patches 2402). Before the desktop
 * starts the title copies the chosen picture over it (pw_wallpaper_apply), so a new choice shows from the next
 * MihawkOS start and no Wine code runs to change it. Save is atomic (a temporary file, fsync, rename); the copy is
 * too, and leaves proton.bmp alone when the chosen picture is missing or already there. Functions return errno
 * values. */
#ifdef __cplusplus
extern "C" {
#endif
#define PW_WALLPAPER_DEFAULT "bubble-city"
#define PW_WALLPAPER_ID_MAX 64
#define PW_WALLPAPER_TARGET "proton.bmp"

typedef struct PwWallpaper {
    const char *id;      /* the file "<id>.bmp"; also the thumbnail assets/launcher/wallpapers/<id>.png */
    const char *name;    /* a proper name, not translated */
    const char *credit;  /* who made it, shown beside it */
} PwWallpaper;

/* The shipped pictures, the default first; *count is set. */
const PwWallpaper *pw_wallpaper_catalog(size_t *count);
/* 1 when id is a safe name (see above). */
int pw_wallpaper_id_valid(const char *id);
/* *id (capacity cap, PW_WALLPAPER_ID_MAX + 1 is enough): the file's id, else the default. 0, ENOENT or another errno
 * opening, EINVAL when the file is not "wallpaper <id>" with a valid id. */
int pw_wallpaper_load(char *id, size_t cap, const char *path);
int pw_wallpaper_save(const char *id, const char *path);
/* Makes folder/proton.bmp the picture folder/<id>.bmp (a person's own, copied in by hand), else shipped/<id>.bmp (the
 * runtime's, NULL for none): *changed is 1 when it was written. ENOENT when the chosen picture is in neither (proton.bmp
 * is left as it is), EINVAL for an invalid id. */
int pw_wallpaper_apply(const char *folder, const char *shipped, const char *id, int *changed);
#ifdef __cplusplus
}
#endif
#endif
