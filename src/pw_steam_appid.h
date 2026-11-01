/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_STEAM_APPID_H
#define PW_STEAM_APPID_H
#include <stddef.h>

/* A game's Steam app id, for the environment names Proton's Wine reads: SteamGameId (about 75 places in its dlls/ change
 * behaviour for one game by it) and SteamAppId (Steamworks, and a few more places). Steam sets both for the games it
 * starts; nothing here did for the others (docs/PROTON_GAME_KNOWLEDGE.md), so the fixes Proton carries could not fire.
 *
 * The id is read from what the installation already says, so a game needs no entry written by hand:
 *   1. Steam's own record: steamapps/appmanifest_<id>.acf whose "installdir" names the folder under steamapps/common/
 *      that holds the program (any depth below it);
 *   2. steam_appid.txt beside the program (the file Steamworks itself reads).
 * An id a catalog entry already sets in its env wins and fills the other name.
 *
 * Portable: opendir, readdir, fopen; nothing is allocated; errors are errno codes. Host test: tests/test_pw_steam_appid.c. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PW_STEAM_ID_MAX = 11,     /* ten digits (a 32-bit app id) and the end */
    PW_STEAM_ID_WORD = 24     /* "SteamGameId=" and an id */
};

/* 1 when text is a plausible app id: one to ten digits, not zero. Steam's 64-bit game ids (mods, shortcuts) are not. */
int pw_steam_id_valid(const char *text);

/* The "appid" and "installdir" of an appmanifest's text (KeyValues, top level of "AppState"). 0 when both are there
 * and fit cap bytes (appid: PW_STEAM_ID_MAX at least), else EINVAL. A manifest cut short after them is still read. */
int pw_steam_manifest_read(const char *text, size_t length, char *appid, size_t appid_cap, char *installdir,
                           size_t installdir_cap);

/* The app id of the program at host_exe (a host path, '/' separators) from the installation: 0 with the id in id (cap
 * bytes, PW_STEAM_ID_MAX at least), ENOENT when nothing says. Several manifests naming one folder: the lowest id. */
int pw_steam_appid_of(const char *host_exe, char *id, size_t cap);

/* The words to add to an entry's env (NAME=VALUE words) so both names carry its id: out[0..n) get "SteamGameId=<id>"
 * and/or "SteamAppId=<id>". An id the env sets itself comes first (SteamGameId, then SteamAppId; the last word of a
 * name wins as Wine applies them), else found (may be NULL). Nothing is added for a name the env sets, or when the id
 * is not valid. Returns n, 0 to 2. */
size_t pw_steam_id_words(const char *const *env, size_t count, const char *found, char out[2][PW_STEAM_ID_WORD]);

#ifdef __cplusplus
}
#endif

#endif
