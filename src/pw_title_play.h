/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_TITLE_PLAY_H
#define PW_TITLE_PLAY_H
#include <stddef.h>

/* "The game comes first": a Steam game started from Big Picture gets the title, not a helper.
 *
 * In Steam's browse session its browser runs in the title (wine/ps5/pw_wine_title_game.h, pw_title_game_steam_browser),
 * and the title takes one program a session, so a library game Steam starts there found the title's place taken and ran
 * in a helper, on the system pool (a81-bb1a-base-2026-11-03: ~5,300 system page tables free and ~6,000 MiB direct memory,
 * against ~6,150 and ~7,850 MiB in the title). Instead the title takes the game's claim as a play request: it asks Steam to
 * shut down (steam.exe -shutdown, Steam ending its own processes and finishing what it writes), and restarts into a play
 * session: the same entry with "-silent ... -applaunch <id>", Steam's browser kept in a helper (WINE_PS5_TITLE_OPTOUT),
 * where Steam starts the game itself and the game takes the title. When the game ends Steam is shut down the same way and
 * the title restarts into the browse session; the player's own exit returns to the launcher.
 *
 * No setting: the trigger is the session's program (steam.exe), the title's holder (Steam's browser) and the claimant (a
 * program in Steam's library folder with an app id from Steam's own files, src/pw_steam_appid.h).
 *
 * Portable, nothing allocated. Host test: tests/test_pw_title_play.c. */

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when a claim the title refused (EBUSY) is a play request: the session's program is steam.exe, the title's holder is
 * Steam's browser (its main process, holder_command_line without --type=), and the claimant is another program under a
 * "\steamapps\common\" folder whose app id (appid, may be NULL) is valid. Images are Windows paths. */
int pw_title_play_wanted(const char *session_exe, const char *holder_image, const char *holder_command_line,
                         const char *claimant_image, const char *appid);

/* What follows a session's end. */
enum PwTitleNext {
    PW_TITLE_NEXT_LAUNCHER,   /* the launcher, as every session's end before */
    PW_TITLE_NEXT_PLAY,       /* the play session for the request's app id */
    PW_TITLE_NEXT_BROWSE      /* the entry again with its own arguments (Big Picture) */
};
/* The player's exit (player_left: the stop the player asked for) always goes to the launcher; then a play request to
 * its play session; then the end of a play session back to browsing. */
enum PwTitleNext pw_title_play_next(int play_session, int requested, int player_left);
const char *pw_title_play_next_word(enum PwTitleNext next);

/* Steam's shutdown is waited for with no time limit: it ends when Steam's processes have, whatever Steam does first (its
 * cloud sync, a controller's firmware). Only when it shows no activity at all for PW_STEAM_QUIET_MS (the same processes,
 * its logs not one byte longer) does the title ask the player: keep waiting, or close Steam. */
#define PW_STEAM_QUIET_MS 180000u
typedef struct PwSteamWatch {
    unsigned long long since_ms;     /* the last activity */
    unsigned processes;              /* Steam's processes then */
    unsigned long long log_bytes;    /* its logs' bytes then */
} PwSteamWatch;
/* From now (the ask, or the player's "keep waiting"). */
void pw_steam_watch_begin(PwSteamWatch *watch, unsigned long long now_ms, unsigned processes, unsigned long long log_bytes);
/* One look: any change is activity and starts the quiet over. 1 when Steam has been quiet PW_STEAM_QUIET_MS. */
int pw_steam_watch_quiet(PwSteamWatch *watch, unsigned long long now_ms, unsigned processes, unsigned long long log_bytes);

/* Who waits for whom across a handover. The rule (8.1's B4 rerun on the base, 2026-11-03, both handovers deadlocked): the title
 * never waits for Steam inside something Steam itself waits for. As a graph, waiter -> what it waits for, it has no
 * cycle; pw_title_play_waits gives each handover's as native/proton_wine.cpp and wine/ps5/pw_wine_process.c do it. */
typedef enum PwWait {
    PW_WAIT_STEAM_LAUNCH,   /* Steam's CreateProcess for the game: until the title answers */
    PW_WAIT_STEAM_SHUTDOWN, /* Steam acting on -shutdown: not while its CreateProcess is out, not while its game runs */
    PW_WAIT_STEAM_EXIT,     /* Steam's processes ended: once its shutdown (and the sync after a game) has run */
    PW_WAIT_TITLE_ANSWER,   /* the title's answer to the claim (title_busy) */
    PW_WAIT_TITLE_COVER,    /* the handover screen's first frame: the guide's own thread, nothing of Steam's */
    PW_WAIT_TITLE_ASK,      /* the title asking Steam to shut down (steam.exe -shutdown) */
    PW_WAIT_TITLE_RESTART,  /* the title's restart into the next session */
    PW_WAIT_GAME_EXIT_HOOK, /* the game's exit hooks (play_game_exit), inside its exit */
    PW_WAIT_GAME_END,       /* the server seeing the game end: after its exit hooks, when its socket closes */
    PW_WAIT_TITLE_EXIT,     /* the title's wait at the game's end for Steam (play_exit_wait) */
    PW_WAIT_HELPERS_CLOSE,  /* Wine's 15 s close of the helpers left (pw_ps5_wait_children) */
    PW_WAITS
} PwWait;
typedef struct PwWaitEdge { PwWait waiter, waited; } PwWaitEdge;
enum PwTitleHandover { PW_TITLE_HANDOVER_PLAY_REQUEST, PW_TITLE_HANDOVER_GAME_END };
/* The handover's waits (static, count in *count). */
const PwWaitEdge *pw_title_play_waits(enum PwTitleHandover handover, unsigned *count);
/* 1 when some waiter waits, through others, for itself (a deadlock), else 0. */
int pw_wait_cycle(const PwWaitEdge *edges, unsigned count);

/* The play session's arguments line from the entry's own (entry, may be NULL): its words without -bigpicture, -silent and
 * an -applaunch with its id, then "-silent" first and "-applaunch <appid>" last; a word with a blank is quoted again.
 * 0 with the line in out (cap bytes), EINVAL for an id that is not valid or a line that does not split
 * (src/pw_wine_launch.h), E2BIG when it does not fit cap or the launcher's PW_WINE_LAUNCH_WORDS. */
int pw_title_play_arguments(const char *entry, const char *appid, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
