/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_TITLE_GAME_H
#define PW_WINE_TITLE_GAME_H
/* Which program runs in the title by default (Alpha 7, "Run inside MihawkOS"; docs/TITLE_CLIENT.md): a 64-bit game from
 * a library, so its memory and page tables are the title's (its own 16,384-entry container, double the helpers'
 * 8,192 on the standard PS5). The title takes one such program a session; another goes to a helper (EBUSY), as do
 * 32-bit programs (the title has no WoW64 signal entry). Host-tested (tests/test_pw_wine_title_game.c).
 *
 *   setting  WINE_PS5_TITLE_GAMES: "1" on (the title sets it beside the GPU broker unless the launch set it), anything
 *            else off
 *   optout   WINE_PS5_TITLE_OPTOUT: ';'-separated executable names that stay in helpers (a library entry's
 *            WINE_PS5_TITLE_GAMES=0)
 *   picked   WINE_PS5_TITLE_PICK: ';'-separated executable names that run in the title (a library entry's
 *            WINE_PS5_TITLE_GAME=<exe>, for the program the rules below would miss)
 *   listed   WINE_PS5_TITLE_LISTED: ';'-separated executable names of the library's entries: a game the player added
 *            counts as a library program wherever it lives (a USB drive, a folder of their own)
 *   image    the Windows path of the program being started
 *   inherited a program a library program started (pw_title_game_inherit below): its library status comes from its
 *            spawner, as a Steam shortcut's game does from steam.exe (Grounded 2 on a USB drive, 2026-11-01) */
#include <stddef.h>
#include <string.h>

static inline char pw_title_game_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* 1 when needle (lower case) occurs in text, ignoring case. */
static inline int pw_title_game_has(const char *text, const char *needle)
{
    for (; *text; ++text) {
        size_t i = 0;
        while (needle[i] && text[i] && pw_title_game_lower(text[i]) == needle[i]) ++i;
        if (!needle[i]) return 1;
    }
    return 0;
}

/* The file name of a Windows or Unix path. */
static inline const char *pw_title_game_name(const char *image)
{
    const char *name = image;
    for (const char *at = image; *at; ++at) if (*at == '\\' || *at == '/') name = at + 1;
    return name;
}

/* 1 when name equals one of list's ';'-separated entries, ignoring case. */
static inline int pw_title_game_listed(const char *list, const char *name)
{
    while (list && *list) {
        const char *end = list;
        while (*end && *end != ';') ++end;
        size_t i = 0, n = (size_t)(end - list);
        while (i < n && name[i] && pw_title_game_lower(list[i]) == pw_title_game_lower(name[i])) ++i;
        if (n && i == n && !name[i]) return 1;
        list = *end ? end + 1 : end;
    }
    return 0;
}

/* A library program by its own right: Steam's library, MihawkOS's Games folder (a store's install, e.g.
 * C:\users\Mihawk\Games), or a program the library's entries name, wherever it lives (a game on a USB drive,
 * 2026-10-31). */
static inline int pw_title_game_library_path(const char *image, const char *listed)
{
    return pw_title_game_has(image, "\\steamapps\\common\\") || pw_title_game_has(image, "\\games\\") ||
           pw_title_game_listed(listed, pw_title_game_name(image));
}

/* Where a program goes, and why (each decision is logged with its word, pw_title_route_word). */
enum PwTitleRoute {
    PW_TITLE_ROUTE_GAME,        /* the title: a 64-bit library game that imports a graphics API */
    PW_TITLE_ROUTE_PICKED,      /* the title: the program its game's entry names (WINE_PS5_TITLE_PICK) */
    PW_TITLE_ROUTE_CANDIDATE,   /* not yet decided: what it imports says (pw_title_game_decide) */
    PW_TITLE_ROUTE_OFF,         /* a helper: the setting is off */
    PW_TITLE_ROUTE_32BIT,       /* a helper: 32-bit (the title has no WoW64 signal entry) */
    PW_TITLE_ROUTE_NOT_LIBRARY, /* a helper: not under a library folder */
    PW_TITLE_ROUTE_TOOL,        /* a helper: an installer, a crash reporter or a game's launcher, by folder or name */
    PW_TITLE_ROUTE_OPTOUT,      /* a helper: its game's entry keeps it there (WINE_PS5_TITLE_OPTOUT) */
    PW_TITLE_ROUTE_NOT_GAME,    /* a helper: no graphics import, a browser shell or .NET (wine/ps5/pw_wine_pe_imports.h) */
    PW_TITLE_ROUTE_CHROMIUM,    /* a helper: a Chromium-based game's main process (Electron, NW.js), by its folder;
                                 * in the title it does not draw yet (moonstone-base-2026-10-31-01), so it stays one */
    PW_TITLE_ROUTE_CHROMIUM_CHILD, /* a helper: one of a Chromium game's own processes (--type=) */
    PW_TITLE_ROUTE_STEAM_BROWSER,  /* the title: Steam's browser (pw_title_game_steam_browser) */
};

/* The first, cheap half: the setting, the CPU, the per-game lists and the path and name rules. picked wins over every
 * rule but the setting and the CPU: it names the one program of an entry that runs in the title. */
static inline enum PwTitleRoute pw_title_game_route_inherited(const char *image, int x64, const char *setting,
                                                              const char *optout, const char *picked, const char *listed,
                                                              int inherited)
{
    if (!image || !*image || !setting || strcmp(setting, "1")) return PW_TITLE_ROUTE_OFF;
    if (!x64) return PW_TITLE_ROUTE_32BIT;
    const char *name = pw_title_game_name(image);
    if (pw_title_game_listed(picked, name)) return PW_TITLE_ROUTE_PICKED;
    /* A library game, or a program a library program started (pw_title_game_inherit). */
    if (!pw_title_game_library_path(image, listed) && !inherited) return PW_TITLE_ROUTE_NOT_LIBRARY;
    /* What a store runs from a game's folder before or beside it would take the title's one place: redistributable
     * installers (Steam's first-launch "Steamworks Shared", a game's _CommonRedist), crash reporters, and a game's own
     * launcher, whose end would end the session under the game it started (that game, started from a helper, comes here). */
    static const char *const folders[] = { "\\steamworks shared\\", "\\_commonredist\\", "\\redist\\", "\\redistributables\\" };
    for (size_t i = 0; i < sizeof(folders) / sizeof(*folders); ++i) if (pw_title_game_has(image, folders[i])) return PW_TITLE_ROUTE_TOOL;
    static const char *const tools[] = { "vcredist", "vc_redist", "dxsetup", "dotnet", "unitycrashhandler", "crashreport",
                                         "crashhandler", "uninstall", "unins0", "steamerrorreporter", "launcher" };
    for (size_t i = 0; i < sizeof(tools) / sizeof(*tools); ++i) if (pw_title_game_has(name, tools[i])) return PW_TITLE_ROUTE_TOOL;
    if (pw_title_game_listed(optout, name)) return PW_TITLE_ROUTE_OPTOUT;
    return PW_TITLE_ROUTE_CANDIDATE;
}

static inline enum PwTitleRoute pw_title_game_route(const char *image, int x64, const char *setting, const char *optout,
                                                    const char *picked, const char *listed)
{
    return pw_title_game_route_inherited(image, x64, setting, optout, picked, listed, 0);
}

/* The length of image's folder, through its last separator. */
static inline size_t pw_title_game_folder_length(const char *image)
{
    return (size_t)(pw_title_game_name(image) - image);
}

/* 1 when image lies inside folder (a path ending in its separator), ignoring case. */
static inline int pw_title_game_inside(const char *image, const char *folder, size_t length)
{
    if (!length || strlen(image) <= length) return 0;
    for (size_t i = 0; i < length; ++i) if (pw_title_game_lower(image[i]) != pw_title_game_lower(folder[i])) return 0;
    return 1;
}

/* Library status down a chain of programs (WINE_PS5_LIBRARY_ROOT, each spawner's word for the program it starts). A
 * library program (by its path or the library's list) passes on its own folder: a program it starts inside that tree
 * counts as a library program too (a game's stub starting its real executable from Augusta\Binaries\Win64 under the
 * stub's folder) and passes the same folder on. steam.exe passes "steam|" and its own folder: what it starts outside
 * its folder is a game it was asked to start (a Steam shortcut), inherited and passing its own folder on; what it
 * starts inside is Steam's own (steamwebhelper, its error reporter), which inherits nothing. Anything else passes
 * nothing. Only the status is inherited: the tool names, the opt-out and the import test still decide (the route),
 * so a stub that draws nothing stays in a helper and never takes the title's one place.
 *
 * Writes what image passes on into root (empty for nothing) and returns 1 when image is a library program by
 * inheritance alone. parent_root: the spawner's WINE_PS5_LIBRARY_ROOT (NULL or empty: none). */
static inline int pw_title_game_inherit(const char *image, const char *parent_root, const char *listed, char *root,
                                        size_t size)
{
    static const char steam_mark[] = "steam|";
    const char *name = image ? pw_title_game_name(image) : "";
    const char *from = NULL;
    size_t length = 0;
    int inherited = 0;
    if (root && size) root[0] = 0;
    if (!image || !*image || !root || !size) return 0;
    if (!parent_root) parent_root = "";
    if (pw_title_game_listed("steam.exe", name)) {
        if (sizeof(steam_mark) - 1 + pw_title_game_folder_length(image) < size) {
            memcpy(root, steam_mark, sizeof(steam_mark) - 1);
            memcpy(root + sizeof(steam_mark) - 1, image, pw_title_game_folder_length(image));
            root[sizeof(steam_mark) - 1 + pw_title_game_folder_length(image)] = 0;
        }
        return 0;
    }
    if (pw_title_game_library_path(image, listed)) {
        from = image;   /* a library program by its own right */
        length = pw_title_game_folder_length(image);
    } else if (!strncmp(parent_root, steam_mark, sizeof(steam_mark) - 1)) {
        const char *steam = parent_root + sizeof(steam_mark) - 1;
        if (!pw_title_game_inside(image, steam, strlen(steam))) {
            inherited = 1;   /* a game Steam was asked to start */
            from = image;
            length = pw_title_game_folder_length(image);
        }
    } else if (pw_title_game_inside(image, parent_root, strlen(parent_root))) {
        inherited = 1;   /* inside its library program's tree: the same tree goes on */
        from = parent_root;
        length = strlen(parent_root);
    }
    if (from && length < size) {
        memcpy(root, from, length);
        root[length] = 0;
    }
    return inherited;
}

/* The second half, for a candidate only: a program that reaches a graphics API is a game (is_game: its imports said
 * so, wine/ps5/pw_wine_pe_imports.h); anything else stays in a helper, so a launcher never holds the title's place. */
static inline enum PwTitleRoute pw_title_game_decide(enum PwTitleRoute route, int is_game)
{
    return route != PW_TITLE_ROUTE_CANDIDATE ? route : is_game ? PW_TITLE_ROUTE_GAME : PW_TITLE_ROUTE_NOT_GAME;
}

/* A Chromium-based game draws through Direct3D it loads at run time, so its imports may say anything: its folder says
 * what it is (chromium: Chromium's resources.pak, icudtl.dat and a V8 snapshot beside the exe; cef: a libcef.dll there
 * too, a launcher's browser, which stays what its imports say). Its main process stays in a helper for now: run in
 * the title, a 2D NW.js game never drew, as one process or many (moonstone-base-2026-10-31-01, -nonone, -fallback),
 * while in a helper it does. Its process count is cut instead (kernelbase patch 2404, WINE_PS5_CHROMIUM_ARGS). Its
 * own processes (child: --type=) are helpers too. Its entry's pick (WINE_PS5_TITLE_PICK) still runs it in the title.
 * A decided route stays as it is. */
static inline enum PwTitleRoute pw_title_game_chromium(enum PwTitleRoute route, int chromium, int cef, int child)
{
    if (route != PW_TITLE_ROUTE_CANDIDATE || !chromium || cef) return route;
    return child ? PW_TITLE_ROUTE_CHROMIUM_CHILD : PW_TITLE_ROUTE_CHROMIUM;
}

/* Steam's browser runs in the title, so its memory and page tables are the title's: steamwebhelper.exe in Steam's
 * bin\cef folder, its main process (no --type=; Steam starts it --single-process, src/pw_steam.c). In a helper it took
 * 1,874 of the 8,192 system page tables over Big Picture's first 4 minutes, in the title 607 (a81-b1a/b-base-2026-11-03),
 * Big Picture drawn and signed in either way. The opt-out (WINE_PS5_TITLE_OPTOUT) keeps it in a helper. Only a route the
 * path rules left at NOT_LIBRARY changes. */
static inline enum PwTitleRoute pw_title_game_steam_browser(enum PwTitleRoute route, const char *image,
                                                            const char *command_line, const char *optout)
{
    if (route != PW_TITLE_ROUTE_NOT_LIBRARY || !image) return route;
    const char *name = pw_title_game_name(image);
    if (!pw_title_game_listed("steamwebhelper.exe", name) || !pw_title_game_has(image, "\\steam\\bin\\cef\\")) return route;
    if (command_line && pw_title_game_has(command_line, "--type=")) return route;
    return pw_title_game_listed(optout, name) ? PW_TITLE_ROUTE_OPTOUT : PW_TITLE_ROUTE_STEAM_BROWSER;
}

static inline int pw_title_route_in_title(enum PwTitleRoute route)
{
    return route == PW_TITLE_ROUTE_GAME || route == PW_TITLE_ROUTE_PICKED || route == PW_TITLE_ROUTE_STEAM_BROWSER;
}

static inline const char *pw_title_route_word(enum PwTitleRoute route)
{
    switch (route) {
    case PW_TITLE_ROUTE_GAME: return "a 64-bit library game";
    case PW_TITLE_ROUTE_PICKED: return "its game's entry names it";
    case PW_TITLE_ROUTE_CANDIDATE: return "undecided";
    case PW_TITLE_ROUTE_OFF: return "the setting is off";
    case PW_TITLE_ROUTE_32BIT: return "32-bit";
    case PW_TITLE_ROUTE_NOT_LIBRARY: return "not in a library";
    case PW_TITLE_ROUTE_TOOL: return "an installer, crash reporter or launcher";
    case PW_TITLE_ROUTE_OPTOUT: return "its game's entry keeps it in a helper";
    case PW_TITLE_ROUTE_NOT_GAME: return "not a game";
    case PW_TITLE_ROUTE_CHROMIUM: return "a Chromium-based game: a helper until one draws in the title";
    case PW_TITLE_ROUTE_CHROMIUM_CHILD: return "a Chromium game's own process";
    case PW_TITLE_ROUTE_STEAM_BROWSER: return "Steam's browser";
    }
    return "unknown";
}
#endif
