/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_APP_ENVIRONMENT_H
#define PW_APP_ENVIRONMENT_H
#include <stddef.h>
#include <stdint.h>
/* Each catalog game's own settings (game.json "env"), for a process another one started: in the Mihawk OS session a
 * game launched from its desktop shortcut is Explorer's child, and gets what the launcher would have given it.
 *
 * A helper's Unix environment is otherwise its spawner's (pw_wine_process.c inherited()), and its Windows one its
 * parent's, so a game started from the desktop had Explorer's: no SDL_XINPUT_ENABLED=0, say. (WINE_PS5_OPENGL turns on
 * win32u's AGC OpenGL in the title only: a helper's OpenGL is Mesa/Zink, patch 1131.) The title looks the
 * new process's executable up here when it starts its helper (pw_ps5_set_application_lookup) and the block goes to
 * both sides: the loader's Unix environment and Wine's Windows one (patch 0998).
 *
 * Matching: the executable as Windows names it, case-insensitive, '/' and '\' alike, a "\\?\" or "\??\" prefix
 * ignored. Entries can share an executable (a Java game and the Java file test both run its javaw.exe): then the entry
 * whose non-empty arguments appear in the command line, else the one entry without arguments, else none (ambiguous).
 *
 * Not per application: the title's own settings, which one session has once. PROTON_* (PROTON_120HZ opens the
 * display once for the whole title; PROTON_LAYOUT, PROTON_ZINK, the shader cache), MIHAWK_* (the session's), and
 * WINE_PS5_NTSYNC (the session's one synchronization device is on or off for every process, src/pw_ntsync.h). They
 * are left out of the block. */
typedef struct PwAppEntry {
    const char *name;          /* the catalog's name, for the log */
    const char *executable;    /* game.json "executable": an absolute Windows path */
    const char *arguments;     /* game.json "arguments"; NULL or empty: none */
    const char *const *env;    /* game.json "env": NAME=VALUE words */
    size_t env_count;
} PwAppEntry;
typedef enum { PW_APP_FOUND = 0, PW_APP_NONE, PW_APP_AMBIGUOUS } PwAppMatch;
enum { PW_APP_ENVIRONMENT_MAX_WORDS = 64 };
#ifdef __cplusplus
extern "C" {
#endif
/* 1 when a NAME=VALUE setting can differ per application, 0 when it is the title's own (or not NAME=VALUE). */
int pw_app_environment_per_app(const char *word);
/* Whether two Windows paths name the same file by the rules above. */
int pw_app_environment_same_path(const char *a, const char *b);
/* The entry for a new process with this image and command line; *index set when PW_APP_FOUND. */
PwAppMatch pw_app_environment_find(const PwAppEntry *entries, size_t count, const char *image,
                                   const char *command_line, size_t *index);
/* The entry's per-application settings packed for the bootstrap (src/pw_environment.h's format). 0, or EINVAL,
 * E2BIG (more than PW_APP_ENVIRONMENT_MAX_WORDS) or what pw_environment_pack refuses; *kept: how many settings.
 * An entry that names its controller (WINE_PS5_CONTROLLER, src/pw_controller.h) gets what the session does for the
 * player's: WINE_PS5_HID=1 for "dualsense" and =0 otherwise, as each program runs its own HID stack (Wine patch 0870),
 * and no SDL_XINPUT_ENABLED=0 beside an Xbox controller, where XInput is the only controller. */
int pw_app_environment_block(const PwAppEntry *entry, char *out, size_t capacity, uint32_t *bytes, size_t *kept);
/* A game's WINEDLLOVERRIDES joins the title's instead of replacing it: Wine keeps the last entry it reads for a DLL, so
 * the game still decides every DLL it names, and keeps the title's for the rest, DXVK's and VKD3D-Proton's among them
 * (Proton appends a user's the same way). A game.json "env" line such as WINEDLLOVERRIDES=dinput8=n,b, the usual
 * advice for a mod, used to take Direct3D 9 to 12 away from that game. Into out: "WINEDLLOVERRIDES=" title ";" the
 * game's value. 1 when word is a WINEDLLOVERRIDES setting and title is not empty (out written), 0 when word stays as
 * it is, -1 when out is too small (nothing written). */
int pw_app_environment_join_overrides(const char *title, const char *word, char *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
