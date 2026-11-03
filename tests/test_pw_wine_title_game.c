/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which program runs in the title by default (wine/ps5/pw_wine_title_game.h), and why: a 64-bit library game that
 * imports a graphics API while the setting is on; not a 32-bit one, not the store client or its browser, not an
 * installer, crash reporter or a game's launcher, not a program the player opted out, and not one whose imports say it
 * is no game; and the one program a game's entry picks, whatever its name. */
#include "../wine/ps5/pw_wine_title_game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static enum PwTitleRoute route(const char *image, int x64, const char *setting, const char *optout, const char *picked)
{
    return pw_title_game_route(image, x64, setting, optout, picked, NULL);
}

int main(void)
{
    const char *lib = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Some 3D Game  Deluxe\\game3d.exe";
    const char *gog = "C:\\users\\Mihawk\\Games\\Aurelia\\steamapps\\common\\Some Game\\game.exe";
    const char *store = "C:\\users\\Mihawk\\Games\\Some Studio\\Game64.exe";

    /* A 64-bit library program, the setting on: a candidate; its imports decide. */
    assert(route(lib, 1, "1", NULL, NULL) == PW_TITLE_ROUTE_CANDIDATE);
    assert(route(gog, 1, "1", "", "") == PW_TITLE_ROUTE_CANDIDATE);
    assert(route(store, 1, "1", NULL, NULL) == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_decide(PW_TITLE_ROUTE_CANDIDATE, 1) == PW_TITLE_ROUTE_GAME);
    assert(pw_title_game_decide(PW_TITLE_ROUTE_CANDIDATE, 0) == PW_TITLE_ROUTE_NOT_GAME);
    assert(pw_title_game_decide(PW_TITLE_ROUTE_TOOL, 1) == PW_TITLE_ROUTE_TOOL);   /* a decided route stays */
    assert(pw_title_route_in_title(PW_TITLE_ROUTE_GAME) && pw_title_route_in_title(PW_TITLE_ROUTE_PICKED));
    assert(!pw_title_route_in_title(PW_TITLE_ROUTE_NOT_GAME) && !pw_title_route_in_title(PW_TITLE_ROUTE_CANDIDATE));
    /* Off: no setting, another value; a 32-bit program. */
    assert(route(lib, 1, NULL, NULL, NULL) == PW_TITLE_ROUTE_OFF);
    assert(route(lib, 1, "0", NULL, NULL) == PW_TITLE_ROUTE_OFF);
    assert(route(lib, 0, "1", NULL, NULL) == PW_TITLE_ROUTE_32BIT);
    assert(route("", 1, "1", NULL, NULL) == PW_TITLE_ROUTE_OFF && route(NULL, 1, "1", NULL, NULL) == PW_TITLE_ROUTE_OFF);
    /* Not a library program: the store client, its browser, a program in C:\. */
    assert(route("C:\\Program Files (x86)\\Steam\\steam.exe", 1, "1", NULL, NULL) == PW_TITLE_ROUTE_NOT_LIBRARY);
    assert(route("C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe", 1, "1", NULL, NULL) ==
           PW_TITLE_ROUTE_NOT_LIBRARY);
    assert(route("C:\\d3d11-probe.exe", 1, "1", NULL, NULL) == PW_TITLE_ROUTE_NOT_LIBRARY);
    /* What runs from a game's folder or the store's first launch: installers, crash reporters, a game's launcher. */
    assert(route("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Steamworks Shared\\_CommonRedist\\vcredist\\2022\\VC_redist.x64.exe",
                 1, "1", NULL, NULL) == PW_TITLE_ROUTE_TOOL);
    assert(route("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Game\\_CommonRedist\\DirectX\\DXSETUP.exe", 1, "1", NULL, NULL) ==
           PW_TITLE_ROUTE_TOOL);
    assert(route("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Game\\UnityCrashHandler64.exe", 1, "1", NULL, NULL) ==
           PW_TITLE_ROUTE_TOOL);
    assert(route("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Game\\Engine\\Binaries\\Win64\\CrashReportClient.exe", 1, "1",
                 NULL, NULL) == PW_TITLE_ROUTE_TOOL);
    assert(route("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Game\\Launcher.exe", 1, "1", NULL, NULL) == PW_TITLE_ROUTE_TOOL);
    assert(route("C:\\users\\Mihawk\\Games\\Game\\GameLauncher64.exe", 1, "1", NULL, NULL) == PW_TITLE_ROUTE_TOOL);
    /* The player's opt-out, by executable name, any case, any position in the list. */
    assert(route(lib, 1, "1", "game3d.exe", NULL) == PW_TITLE_ROUTE_OPTOUT);
    assert(route(lib, 1, "1", "other.exe;GAME3D.EXE", NULL) == PW_TITLE_ROUTE_OPTOUT);
    assert(route(lib, 1, "1", "game3d.exe.bak;game3d;other.exe", NULL) == PW_TITLE_ROUTE_CANDIDATE);
    /* A game the player added (a library entry) counts wherever it lives: a USB drive, a folder of their own. Its tool
     * names and its opt-out still apply, and an unlisted program there stays out. */
    const char *usb = "Z:\\mnt\\usb0\\Some Racing Game\\RacingGame.exe";
    assert(pw_title_game_route(usb, 1, "1", NULL, NULL, "racinggame.exe") == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_route(usb, 1, "1", NULL, NULL, "other.exe;RacingGame.exe") == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_route(usb, 1, "1", NULL, NULL, NULL) == PW_TITLE_ROUTE_NOT_LIBRARY);
    assert(pw_title_game_route(usb, 1, "1", "RacingGame.exe", NULL, "RacingGame.exe") == PW_TITLE_ROUTE_OPTOUT);
    assert(pw_title_game_route("Z:\\mnt\\usb0\\Game\\GameLauncher.exe", 1, "1", NULL, NULL, "GameLauncher.exe") ==
           PW_TITLE_ROUTE_TOOL);
    assert(pw_title_game_route(usb, 0, "1", NULL, NULL, "RacingGame.exe") == PW_TITLE_ROUTE_32BIT);
    /* The entry's pick: the title, whatever the folder or the name, but never with the setting off or 32-bit. */
    assert(route(lib, 1, "1", NULL, "GAME3D.exe") == PW_TITLE_ROUTE_PICKED);
    assert(route("C:\\users\\Mihawk\\Games\\Game\\GameLauncher64.exe", 1, "1", NULL, "gamelauncher64.exe") == PW_TITLE_ROUTE_PICKED);
    assert(route("D:\\elsewhere\\real.exe", 1, "1", NULL, "real.exe") == PW_TITLE_ROUTE_PICKED);
    assert(route(lib, 1, "1", "game3d.exe", "game3d.exe") == PW_TITLE_ROUTE_PICKED);   /* the pick wins over the opt-out */
    assert(route(lib, 0, "1", NULL, "game3d.exe") == PW_TITLE_ROUTE_32BIT);
    assert(route(lib, 1, "0", NULL, "game3d.exe") == PW_TITLE_ROUTE_OFF);
    /* A Chromium-based game (its folder says so): its main process goes to the title whatever its imports say, its own
     * processes (--type=) to helpers; a CEF program (libcef.dll beside it) stays what its imports say; a decided route
     * stays. */
    assert(pw_title_game_chromium(PW_TITLE_ROUTE_CANDIDATE, 1, 0, 0) == PW_TITLE_ROUTE_CHROMIUM);
    assert(pw_title_game_chromium(PW_TITLE_ROUTE_CANDIDATE, 1, 0, 1) == PW_TITLE_ROUTE_CHROMIUM_CHILD);
    assert(pw_title_game_chromium(PW_TITLE_ROUTE_CANDIDATE, 1, 1, 0) == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_chromium(PW_TITLE_ROUTE_CANDIDATE, 0, 0, 0) == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_chromium(PW_TITLE_ROUTE_TOOL, 1, 0, 0) == PW_TITLE_ROUTE_TOOL);
    assert(pw_title_game_chromium(PW_TITLE_ROUTE_OPTOUT, 1, 0, 0) == PW_TITLE_ROUTE_OPTOUT);
    /* Not in the title for now (it did not draw there, 2026-10-31), and never through the import rule either. */
    assert(!pw_title_route_in_title(PW_TITLE_ROUTE_CHROMIUM) && !pw_title_route_in_title(PW_TITLE_ROUTE_CHROMIUM_CHILD));
    assert(pw_title_game_decide(pw_title_game_chromium(PW_TITLE_ROUTE_CANDIDATE, 1, 0, 0), 1) == PW_TITLE_ROUTE_CHROMIUM);
    /* Library status down a chain (pw_title_game_inherit): a Steam shortcut to a game on a USB drive, as Grounded 2 ran
     * (steam.exe, then its stub, then the stub's real executable), 2026-11-01. */
    char steam_root[300], stub_root[300], ship_root[300], other[300];
    const char *steam = "C:\\Program Files (x86)\\Steam\\steam.exe";
    const char *stub = "Z:\\mnt\\usb0\\Grounded 2\\Grounded2.exe";
    const char *ship = "Z:\\mnt\\usb0\\Grounded 2\\Augusta\\Binaries\\Win64\\Grounded2Steam-Win64-Shipping.exe";
    assert(!pw_title_game_inherit(steam, NULL, NULL, steam_root, sizeof(steam_root)));
    assert(!strcmp(steam_root, "steam|C:\\Program Files (x86)\\Steam\\"));
    assert(pw_title_game_inherit(stub, steam_root, NULL, stub_root, sizeof(stub_root)));
    assert(!strcmp(stub_root, "Z:\\mnt\\usb0\\Grounded 2\\"));
    assert(pw_title_game_inherit(ship, stub_root, NULL, ship_root, sizeof(ship_root)));
    assert(!strcmp(ship_root, stub_root));
    /* Both are library programs now; their imports decide, so a stub that draws nothing stays a helper's. */
    assert(pw_title_game_route_inherited(stub, 1, "1", NULL, NULL, NULL, 1) == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_route_inherited(ship, 1, "1", NULL, NULL, NULL, 1) == PW_TITLE_ROUTE_CANDIDATE);
    assert(pw_title_game_route_inherited(ship, 1, "1", NULL, NULL, NULL, 0) == PW_TITLE_ROUTE_NOT_LIBRARY);
    /* Inheriting changes nothing else: a crash reporter in the tree, the opt-out, 32-bit, the setting off. */
    const char *reporter = "Z:\\mnt\\usb0\\Grounded 2\\Engine\\Binaries\\Win64\\CrashReportClient.exe";
    assert(pw_title_game_inherit(reporter, stub_root, NULL, other, sizeof(other)));
    assert(pw_title_game_route_inherited(reporter, 1, "1", NULL, NULL, NULL, 1) == PW_TITLE_ROUTE_TOOL);
    assert(pw_title_game_route_inherited(ship, 1, "1", "grounded2steam-win64-shipping.exe", NULL, NULL, 1) ==
           PW_TITLE_ROUTE_OPTOUT);
    assert(pw_title_game_route_inherited(ship, 0, "1", NULL, NULL, NULL, 1) == PW_TITLE_ROUTE_32BIT);
    assert(pw_title_game_route_inherited(ship, 1, "0", NULL, NULL, NULL, 1) == PW_TITLE_ROUTE_OFF);
    /* Steam's own programs inherit nothing, nor does a program outside the tree or one with no library spawner. */
    assert(!pw_title_game_inherit("C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe", steam_root,
                                  NULL, other, sizeof(other)) && !other[0]);
    assert(!pw_title_game_inherit("C:\\PROGRAM FILES (X86)\\STEAM\\steamerrorreporter64.exe", steam_root, NULL, other,
                                  sizeof(other)) && !other[0]);
    assert(!pw_title_game_inherit("Z:\\mnt\\usb0\\Other Game\\other.exe", stub_root, NULL, other, sizeof(other)) &&
           !other[0]);
    assert(!pw_title_game_inherit("Z:\\mnt\\usb0\\Grounded 2 Mods\\tool.exe", stub_root, NULL, other, sizeof(other)));
    assert(!pw_title_game_inherit(ship, NULL, NULL, other, sizeof(other)) && !other[0]);
    assert(!pw_title_game_inherit(ship, "", NULL, other, sizeof(other)) && !other[0]);
    /* The tree's case does not matter, as on Windows. */
    assert(pw_title_game_inherit("z:\\MNT\\USB0\\grounded 2\\augusta\\x.exe", stub_root, NULL, other, sizeof(other)));
    /* A library program by its own right passes its own folder, and what it starts inside inherits from it. */
    assert(!pw_title_game_inherit(lib, NULL, NULL, other, sizeof(other)));
    assert(!strcmp(other, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Some 3D Game  Deluxe\\"));
    /* (one there is a library program by its own path already, not by inheritance) */
    assert(!pw_title_game_inherit("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Some 3D Game  Deluxe\\bin\\real.exe",
                                  other, NULL, ship_root, sizeof(ship_root)));
    assert(!strcmp(ship_root, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Some 3D Game  Deluxe\\bin\\"));
    assert(!pw_title_game_inherit(usb, NULL, "RacingGame.exe", other, sizeof(other)) &&
           !strcmp(other, "Z:\\mnt\\usb0\\Some Racing Game\\"));
    assert(pw_title_game_inherit("Z:\\mnt\\usb0\\Some Racing Game\\bin\\x64\\RacingGame-Win64.exe", other, "RacingGame.exe",
                                 ship_root, sizeof(ship_root)) && !strcmp(ship_root, other));
    /* A root that does not fit is left empty, never cut short. */
    char tiny[8] = "x";
    assert(pw_title_game_inherit(ship, stub_root, NULL, tiny, sizeof(tiny)) && !tiny[0]);
    assert(!pw_title_game_inherit(steam, NULL, NULL, tiny, sizeof(tiny)) && !tiny[0]);
    assert(!pw_title_game_inherit(NULL, stub_root, NULL, other, sizeof(other)) && !other[0]);
    /* Each reason has its word for the log. */
    for (int r = PW_TITLE_ROUTE_GAME; r <= PW_TITLE_ROUTE_CHROMIUM_CHILD; ++r) assert(strcmp(pw_title_route_word(r), "unknown"));
    assert(pw_title_game_listed("a.exe;;b.exe;", "b.exe") && !pw_title_game_listed("a.exe", "") && !pw_title_game_listed(NULL, "a.exe"));
    printf("test_pw_wine_title_game: ok\n");
    return 0;
}
