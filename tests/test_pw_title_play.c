/* SPDX-License-Identifier: GPL-3.0-or-later */
/* "The game comes first" (src/pw_title_play.h): which refused claim is a play request, what follows a session's end, and
 * the play session's arguments line. */
#include "../src/pw_title_play.h"
#include "../src/pw_wine_launch.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define STEAM "C:\\Program Files (x86)\\Steam\\steam.exe"
#define BROWSER "C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe"
#define GAME "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Spelunky 2\\Spel2.exe"

static void wanted(void)
{
    const char *line = "\"" BROWSER "\" -nocrashdialog --single-process";
    assert(pw_title_play_wanted(STEAM, BROWSER, line, GAME, "418530"));
    assert(pw_title_play_wanted("c:\\steam\\STEAM.EXE", BROWSER, NULL, GAME, "418530"));
    /* Another session's program, or no session program */
    assert(!pw_title_play_wanted("C:\\windows\\system32\\mihawk.exe", BROWSER, line, GAME, "418530"));
    assert(!pw_title_play_wanted(NULL, BROWSER, line, GAME, "418530"));
    /* The holder is not Steam's browser: a game, a browser child, a web helper elsewhere, none */
    assert(!pw_title_play_wanted(STEAM, GAME, NULL, GAME, "418530"));
    assert(!pw_title_play_wanted(STEAM, BROWSER, "steamwebhelper.exe --type=renderer", GAME, "418530"));
    assert(!pw_title_play_wanted(STEAM, "C:\\Other\\steamwebhelper.exe", NULL, GAME, "418530"));
    assert(!pw_title_play_wanted(STEAM, NULL, NULL, GAME, "418530"));
    /* The claimant: Steam's browser again, a program outside the library, no or a bad id */
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, BROWSER, "418530"));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, "C:\\Games\\Spel2.exe", "418530"));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, GAME, NULL));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, GAME, ""));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, GAME, "0"));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, GAME, "41853x"));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, GAME, "12345678901"));
    assert(!pw_title_play_wanted(STEAM, BROWSER, line, NULL, "418530"));
}

static void next(void)
{
    /* The player's exit wins over everything */
    assert(pw_title_play_next(0, 0, 1) == PW_TITLE_NEXT_LAUNCHER);
    assert(pw_title_play_next(0, 1, 1) == PW_TITLE_NEXT_LAUNCHER);
    assert(pw_title_play_next(1, 0, 1) == PW_TITLE_NEXT_LAUNCHER);
    assert(pw_title_play_next(1, 1, 1) == PW_TITLE_NEXT_LAUNCHER);
    /* Browse -> play on a request; play -> browse at its end; anything else as before */
    assert(pw_title_play_next(0, 1, 0) == PW_TITLE_NEXT_PLAY);
    assert(pw_title_play_next(1, 0, 0) == PW_TITLE_NEXT_BROWSE);
    assert(pw_title_play_next(0, 0, 0) == PW_TITLE_NEXT_LAUNCHER);
    assert(strstr(pw_title_play_next_word(PW_TITLE_NEXT_PLAY), "play"));
    assert(strstr(pw_title_play_next_word(PW_TITLE_NEXT_BROWSE), "browsing"));
    assert(strstr(pw_title_play_next_word(PW_TITLE_NEXT_LAUNCHER), "launcher"));
}

static void arguments(void)
{
    char out[512];
    /* The Steam entry's own line */
    assert(pw_title_play_arguments("-bigpicture -noverifyfiles", "418530", out, sizeof(out)) == 0);
    assert(!strcmp(out, "-silent -noverifyfiles -applaunch 418530"));
    assert(pw_title_play_arguments(NULL, "418530", out, sizeof(out)) == 0 && !strcmp(out, "-silent -applaunch 418530"));
    assert(pw_title_play_arguments("", "7", out, sizeof(out)) == 0 && !strcmp(out, "-silent -applaunch 7"));
    /* An earlier -silent or -applaunch with its id goes; a quoted word stays one word */
    assert(pw_title_play_arguments("-SILENT -applaunch 10 \"-cef dir\" -BigPicture", "20", out, sizeof(out)) == 0);
    assert(!strcmp(out, "-silent \"-cef dir\" -applaunch 20"));
    char storage[512];
    const char *words[PW_WINE_LAUNCH_WORDS + 1];
    assert(pw_wine_launch_split(out, storage, sizeof(storage), words, PW_WINE_LAUNCH_WORDS) == 4);
    assert(!strcmp(words[1], "-cef dir"));
    /* Bad id, bad line, no room */
    assert(pw_title_play_arguments("-bigpicture", "0", out, sizeof(out)) == EINVAL);
    assert(pw_title_play_arguments("-bigpicture", NULL, out, sizeof(out)) == EINVAL);
    assert(pw_title_play_arguments("\"open", "20", out, sizeof(out)) == EINVAL);
    assert(pw_title_play_arguments("-bigpicture", "20", out, 10) == E2BIG);
    assert(pw_title_play_arguments("-bigpicture", "20", NULL, 0) == EINVAL);
    /* The launcher's words: the entry's 29 and the three added fit; 30 do not */
    char line[400] = "";
    for (int i = 0; i < 29; ++i) strcat(line, " -w");
    assert(pw_title_play_arguments(line, "20", out, sizeof(out)) == 0);
    assert(pw_wine_launch_split(out, storage, sizeof(storage), words, PW_WINE_LAUNCH_WORDS) == PW_WINE_LAUNCH_WORDS);
    strcat(line, " -w");
    assert(pw_title_play_arguments(line, "20", out, sizeof(out)) == E2BIG);
}

static void watch(void)
{
    PwSteamWatch w;
    pw_steam_watch_begin(&w, 1000, 5, 4096);
    /* quiet only after PW_STEAM_QUIET_MS with nothing changing */
    assert(!pw_steam_watch_quiet(&w, 1000 + PW_STEAM_QUIET_MS - 1, 5, 4096));
    assert(pw_steam_watch_quiet(&w, 1000 + PW_STEAM_QUIET_MS, 5, 4096));
    /* a process ending, or one log byte, is activity: the quiet starts over from then */
    assert(!pw_steam_watch_quiet(&w, 1000 + PW_STEAM_QUIET_MS + 10, 4, 4096));
    assert(!pw_steam_watch_quiet(&w, 1000 + 2 * PW_STEAM_QUIET_MS, 4, 4096));
    assert(!pw_steam_watch_quiet(&w, 1000 + 2 * PW_STEAM_QUIET_MS + 5, 4, 4097));
    assert(pw_steam_watch_quiet(&w, 1000 + 3 * PW_STEAM_QUIET_MS + 5, 4, 4097));
    /* a long, busy shutdown (a controller's firmware, a big upload) is never quiet: no timer */
    pw_steam_watch_begin(&w, 0, 3, 0);
    for (unsigned long long t = 0, bytes = 0; t < 30ull * PW_STEAM_QUIET_MS; t += 60000, bytes += 80)
        assert(!pw_steam_watch_quiet(&w, t, 3, bytes));
    /* a clock that goes back is no quiet */
    pw_steam_watch_begin(&w, 5000, 1, 1);
    assert(!pw_steam_watch_quiet(&w, 10, 1, 1));
}

/* The handovers' waits have no cycle; the B4 designs, which deadlocked on the console, have one each. */
static void waits(void)
{
    unsigned count = 0;
    const PwWaitEdge *play = pw_title_play_waits(PW_TITLE_HANDOVER_PLAY_REQUEST, &count);
    assert(count && !pw_wait_cycle(play, count));
    const PwWaitEdge *end = pw_title_play_waits(PW_TITLE_HANDOVER_GAME_END, &count);
    assert(count && !pw_wait_cycle(end, count));
    /* B4's play request: the claim held until Steam's processes ended */
    const PwWaitEdge held[] = {
        {PW_WAIT_STEAM_LAUNCH, PW_WAIT_TITLE_ANSWER}, {PW_WAIT_TITLE_ANSWER, PW_WAIT_STEAM_EXIT},
        {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_TITLE_ASK},  {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_STEAM_LAUNCH},
        {PW_WAIT_STEAM_EXIT, PW_WAIT_STEAM_SHUTDOWN}, {PW_WAIT_TITLE_RESTART, PW_WAIT_STEAM_EXIT},
    };
    assert(pw_wait_cycle(held, sizeof(held) / sizeof(held[0])));
    /* B4's game end: the exit hook waited for Steam, before the server saw the game end */
    const PwWaitEdge hooked[] = {
        {PW_WAIT_GAME_END, PW_WAIT_GAME_EXIT_HOOK},  {PW_WAIT_GAME_EXIT_HOOK, PW_WAIT_STEAM_EXIT},
        {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_TITLE_ASK},
        {PW_WAIT_STEAM_SHUTDOWN, PW_WAIT_GAME_END},  {PW_WAIT_STEAM_EXIT, PW_WAIT_STEAM_SHUTDOWN},
    };
    assert(pw_wait_cycle(hooked, sizeof(hooked) / sizeof(hooked[0])));
    /* the check itself: a chain, a two-party wait, a party waiting for itself */
    const PwWaitEdge chain[] = {{PW_WAIT_TITLE_ASK, PW_WAIT_STEAM_EXIT}, {PW_WAIT_STEAM_EXIT, PW_WAIT_GAME_END}};
    assert(!pw_wait_cycle(chain, 2) && !pw_wait_cycle(chain, 0));
    const PwWaitEdge pair[] = {{PW_WAIT_TITLE_ASK, PW_WAIT_STEAM_EXIT}, {PW_WAIT_STEAM_EXIT, PW_WAIT_TITLE_ASK}};
    assert(pw_wait_cycle(pair, 2));
    const PwWaitEdge self[] = {{PW_WAIT_TITLE_EXIT, PW_WAIT_TITLE_EXIT}};
    assert(pw_wait_cycle(self, 1));
}

int main(void)
{
    waits();
    watch();
    wanted();
    next();
    arguments();
    puts("test_pw_title_play: ok");
    return 0;
}
