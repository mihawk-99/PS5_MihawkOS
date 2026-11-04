/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_title_play.h"
#include "pw_steam_appid.h"
#include "pw_wine_launch.h"
#include "../wine/ps5/pw_wine_title_game.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

int pw_title_play_wanted(const char *session_exe, const char *holder_image, const char *holder_command_line,
                         const char *claimant_image, const char *appid)
{
    if (!session_exe || !holder_image || !claimant_image) return 0;
    if (strcasecmp(pw_title_game_name(session_exe), "steam.exe")) return 0;
    if (pw_title_game_steam_browser(PW_TITLE_ROUTE_NOT_LIBRARY, holder_image, holder_command_line, NULL) !=
        PW_TITLE_ROUTE_STEAM_BROWSER) return 0;
    /* A browser started again claims too (Steam relaunches it): never a game. */
    if (pw_title_game_steam_browser(PW_TITLE_ROUTE_NOT_LIBRARY, claimant_image, NULL, NULL) ==
        PW_TITLE_ROUTE_STEAM_BROWSER) return 0;
    return pw_title_game_has(claimant_image, "\\steamapps\\common\\") && pw_steam_id_valid(appid);
}

enum PwTitleNext pw_title_play_next(int play_session, int requested, int player_left)
{
    if (player_left) return PW_TITLE_NEXT_LAUNCHER;
    if (requested) return PW_TITLE_NEXT_PLAY;
    return play_session ? PW_TITLE_NEXT_BROWSE : PW_TITLE_NEXT_LAUNCHER;
}

const char *pw_title_play_next_word(enum PwTitleNext next)
{
    switch (next) {
    case PW_TITLE_NEXT_PLAY: return "the play session";
    case PW_TITLE_NEXT_BROWSE: return "browsing (the entry's own arguments)";
    default: return "the launcher";
    }
}

void pw_steam_watch_begin(PwSteamWatch *watch, unsigned long long now_ms, unsigned processes, unsigned long long log_bytes)
{
    watch->since_ms = now_ms;
    watch->processes = processes;
    watch->log_bytes = log_bytes;
}

int pw_steam_watch_quiet(PwSteamWatch *watch, unsigned long long now_ms, unsigned processes, unsigned long long log_bytes)
{
    if (processes != watch->processes || log_bytes != watch->log_bytes || now_ms < watch->since_ms) {
        pw_steam_watch_begin(watch, now_ms, processes, log_bytes);
        return 0;
    }
    return now_ms - watch->since_ms >= PW_STEAM_QUIET_MS;
}

int pw_title_play_arguments(const char *entry, const char *appid, char *out, size_t cap)
{
    char storage[2048];
    const char *words[PW_WINE_LAUNCH_WORDS + 1];
    if (!pw_steam_id_valid(appid) || !out || !cap) return EINVAL;
    const int count = pw_wine_launch_split(entry ? entry : "", storage, sizeof(storage), words, PW_WINE_LAUNCH_WORDS);
    if (count < 0) return EINVAL;
    size_t used = 0;
    int kept = 0;   /* the entry's words kept, beside -silent and -applaunch <id> */
#define PUT(...) do { const int n = snprintf(out + used, cap - used, __VA_ARGS__); \
                      if (n < 0 || (size_t)n >= cap - used) return E2BIG; \
                      used += (size_t)n; } while (0)
    PUT("-silent");
    for (int i = 0; i < count; ++i) {
        if (!strcasecmp(words[i], "-bigpicture") || !strcasecmp(words[i], "-silent")) continue;
        if (!strcasecmp(words[i], "-applaunch")) { ++i; continue; }
        if (1 + ++kept + 2 > PW_WINE_LAUNCH_WORDS) return E2BIG;
        if (strpbrk(words[i], " \t")) PUT(" \"%s\"", words[i]);
        else PUT(" %s", words[i]);
    }
    PUT(" -applaunch %s", appid);
#undef PUT
    return 0;
}

const PwWaitEdge *pw_title_play_waits(enum PwTitleHandover handover, unsigned *count)
{
    /* The play request: the claim is answered once the screen is up, then Steam is asked; Steam's CreateProcess returns
     * before its shutdown runs, and the restart waits for its processes. */
    static const PwWaitEdge play_request[] = {
        {PW_WAIT_STEAM_LAUNCH, PW_WAIT_TITLE_ANSWER},  {PW_WAIT_TITLE_ANSWER, PW_WAIT_TITLE_COVER},
        {PW_WAIT_TITLE_ASK, PW_WAIT_TITLE_ANSWER},     {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_TITLE_ASK},
        {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_STEAM_LAUNCH}, {PW_WAIT_STEAM_EXIT, PW_WAIT_STEAM_SHUTDOWN},
        {PW_WAIT_TITLE_RESTART, PW_WAIT_STEAM_EXIT},
    };
    /* The game's end: its exit hook only shows the screen; the server sees the game end; then the title asks Steam (an
     * ask waits for what comes before it) and waits for it, before Wine's 15 s close. */
    static const PwWaitEdge game_end[] = {
        {PW_WAIT_GAME_END, PW_WAIT_GAME_EXIT_HOOK},    {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_GAME_END},
        {PW_WAIT_TITLE_EXIT, PW_WAIT_GAME_END},        {PW_WAIT_TITLE_ASK, PW_WAIT_GAME_END},
        {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_TITLE_ASK},   {PW_WAIT_STEAM_EXIT, PW_WAIT_STEAM_SHUTDOWN},
        {PW_WAIT_TITLE_EXIT, PW_WAIT_STEAM_EXIT},      {PW_WAIT_HELPERS_CLOSE, PW_WAIT_TITLE_EXIT},
        {PW_WAIT_TITLE_RESTART, PW_WAIT_HELPERS_CLOSE},
    };
    const int play = handover == PW_TITLE_HANDOVER_PLAY_REQUEST;
    *count = play ? sizeof(play_request) / sizeof(play_request[0]) : sizeof(game_end) / sizeof(game_end[0]);
    return play ? play_request : game_end;
}

/* Depth-first, each party white (0), on the path (1) or done (2): an edge back to the path is a cycle. */
static int visit(const PwWaitEdge *edges, unsigned count, PwWait at, unsigned char *mark)
{
    mark[at] = 1;
    for (unsigned i = 0; i < count; ++i) {
        if (edges[i].waiter != at) continue;
        if (mark[edges[i].waited] == 1) return 1;
        if (!mark[edges[i].waited] && visit(edges, count, edges[i].waited, mark)) return 1;
    }
    mark[at] = 2;
    return 0;
}

int pw_wait_cycle(const PwWaitEdge *edges, unsigned count)
{
    unsigned char mark[PW_WAITS] = {0};
    for (unsigned i = 0; i < count; ++i)
        if (!mark[edges[i].waiter] && visit(edges, count, edges[i].waiter, mark)) return 1;
    return 0;
}
