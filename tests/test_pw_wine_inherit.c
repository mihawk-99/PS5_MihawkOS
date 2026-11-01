/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What a helper inherits of the title's environment (wine/ps5/pw_wine_inherit.h): the GPU broker client's switches
 * (WINE_PS5_ASYNC_PRESENT, graphics 0024; WINE_PS5_PRESENT_LINK, 0026) reach it, beside the settings that always did;
 * the title's own switches and the rest stay behind; an entry that does not fit the block is left out whole. */
#include "../wine/ps5/pw_wine_inherit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 1 when NAME=value is one of the block's entries. */
static int carries(const char *block, const char *entry)
{
    for (const char *at = block; *at; at += strlen(at) + 1)
        if (!strcmp(at, entry)) return 1;
    return 0;
}

int main(void)
{
    char *const environment[] = {"HOME=/app0", "WINE_PS5_ASYNC_PRESENT=1", "PROTON_120HZ=1", "WINE_PS5_PRESENT_LINK=1",
                                 "WINE_PS5_REFRESH=120", "PROTON_GPU_TIMESTAMPS=1", "DXVK_HUD=fps", "WINE_PS5_NTSYNC=1",
                                 "WINE_PS5_PRESENT_LINKED", NULL};
    char block[4096];
    const size_t used = pw_wine_inherit_environment(environment, block, sizeof(block));
    assert(carries(block, "WINE_PS5_ASYNC_PRESENT=1") && carries(block, "WINE_PS5_PRESENT_LINK=1"));
    assert(carries(block, "WINE_PS5_REFRESH=120") && carries(block, "DXVK_HUD=fps"));
    /* The title's alone: its display rate request, the broker's timestamps, the sync device the title owns. */
    assert(!carries(block, "HOME=/app0") && !carries(block, "PROTON_120HZ=1") && !carries(block, "PROTON_GPU_TIMESTAMPS=1"));
    assert(!carries(block, "WINE_PS5_NTSYNC=1") && !carries(block, "WINE_PS5_PRESENT_LINKED"));
    assert(used == strlen("WINE_PS5_ASYNC_PRESENT=1") + strlen("WINE_PS5_PRESENT_LINK=1") + strlen("WINE_PS5_REFRESH=120") +
                   strlen("DXVK_HUD=fps") + 4 && block[used] == 0);
    /* The loader's CPU placement switch reaches every helper and its descendants (src/pw_cpu_placement.c). */
    assert(pw_wine_inherited("WINE_PS5_HELPER_CPUS_WIDEN=1"));
    /* The helper's census budgets and its once-a-second early memory line (native/proton_wine_child.c). */
    assert(pw_wine_inherited("WINE_PS5_CENSUS_MS=400:100:300") && pw_wine_inherited("WINE_PS5_EARLY=1") && pw_wine_inherited("WINE_PS5_VMMAP=1") && pw_wine_inherited("WINE_PS5_MAPLOG_MIB=8") && pw_wine_inherited("WINE_PS5_SAMPLE=1"));
    /* The code cache reaches the helpers a program starts, not only the first (fmem-steam-b32-2026-10-25-01: the web helper ran without it). */
    assert(pw_wine_inherited("WINE_PS5_CODE_BUDGET_MIB=32") && pw_wine_inherited("WINE_PS5_CODE_BUDGET_START_MIB=16"));
    assert(pw_wine_inherited("WINE_PS5_DSECTION_MIN_MIB=16") && pw_wine_inherited("WINE_PS5_TITLE_PROCESS=game.exe"));
    assert(pw_wine_inherited("WINE_PS5_TITLE_GAMES=1") && pw_wine_inherited("WINE_PS5_TITLE_OPTOUT=a.exe"));
    assert(pw_wine_inherited("WINE_PS5_TITLE_PICK=game.exe") && pw_wine_inherited("WINE_PS5_CHROMIUM_ARGS=--single-process") &&
           pw_wine_inherited("WINE_PS5_TITLE_LISTED=game.exe"));
    /* The game's Steam ids reach the programs it starts (Proton's Wine reads them on the Unix side too); a lookalike does not. */
    assert(pw_wine_inherited("SteamGameId=250900") && pw_wine_inherited("SteamAppId=250900"));
    assert(!pw_wine_inherited("SteamGameIdX=1") && !pw_wine_inherited("STEAMGAMEID=1") && !pw_wine_inherited("SteamPath=/x"));
    /* The setup's language, for every Windows program (patch 1230); never the C library's own LC_ variables. */
    assert(pw_wine_inherited("WINE_PS5_LOCALE=fr_FR.UTF-8") && !pw_wine_inherited("LC_MESSAGES=fr_FR.UTF-8"));
    /* A game's own controller and Game Mode reach the game its launcher starts (src/pw_window_direct.h). */
    assert(pw_wine_inherited("WINE_PS5_GAME_MODE=0") && pw_wine_inherited("WINE_PS5_CONTROLLER=dualsense") &&
           pw_wine_inherited("WINE_PS5_HID=1"));
    static const char settings[] = "SDL_X=1\0WINE_PS5_GAME_MODE=1\0WINE_PS5_GAME_MODES=7\0WINE_PS5_GAME_MODE=0\0\0";
    assert(!strcmp(pw_wine_block_value(settings, sizeof(settings), "WINE_PS5_GAME_MODE"), "0"));   /* the last wins */
    assert(!pw_wine_block_value(settings, sizeof(settings), "WINE_PS5_GAME") && !pw_wine_block_value(settings, 0, "SDL_X"));
    assert(!strcmp(pw_wine_block_value(settings, 8, "SDL_X"), "1"));   /* a block cut short ends at its size */
    assert(pw_wine_inherited("WINE_PS5_CONTROLLER=dualsense"));

    /* A block too small for the last switch: the entries that fit stay whole, the one that does not is left out. */
    char small[sizeof("WINE_PS5_ASYNC_PRESENT=1") + 8];
    char *const two[] = {"WINE_PS5_ASYNC_PRESENT=1", "WINE_PS5_PRESENT_LINK=1", NULL};
    const size_t kept = pw_wine_inherit_environment(two, small, sizeof(small));
    assert(kept == sizeof("WINE_PS5_ASYNC_PRESENT=1") && carries(small, "WINE_PS5_ASYNC_PRESENT=1") &&
           !carries(small, "WINE_PS5_PRESENT_LINK=1") && small[kept] == 0);
    puts("test_pw_wine_inherit: ok");
    return 0;
}
