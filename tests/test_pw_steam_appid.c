/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Steam app id of a program, from the installation (src/pw_steam_appid.h): Steam's appmanifest naming the folder
 * under steamapps/common that holds it, or steam_appid.txt beside it; and the env words that carry it as SteamGameId and
 * SteamAppId, an id the entry sets itself winning. */
#include "../src/pw_steam_appid.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];

static const char *at(const char *relative)
{
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    return path;
}

/* pw_steam_app_name on a 64 KiB stack, as Wine's spawn thread had when the title called it (title_busy): its 64 KiB
 * buffer overflowed that stack (8.1's B4 run on the base, 2026-11-03). */
static void *small_stack_name(void *name)
{
    return (void *)(long)pw_steam_app_name(at("Steam/steamapps"), "504230", name, 64);
}

static int name_on_small_stack(char *name)
{
    pthread_attr_t attributes;
    pthread_t thread;
    void *result = NULL;
    assert(!pthread_attr_init(&attributes) && !pthread_attr_setstacksize(&attributes, 64u << 10));
    assert(!pthread_create(&thread, &attributes, small_stack_name, name) && !pthread_join(thread, &result));
    pthread_attr_destroy(&attributes);
    return (int)(long)result;
}

static void folder(const char *relative)
{
    char command[600];
    snprintf(command, sizeof(command), "mkdir -p '%s'", at(relative));
    assert(system(command) == 0);
}

static void write_file(const char *relative, const char *text)
{
    FILE *file = fopen(at(relative), "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
}

static void manifest(const char *name, const char *appid, const char *installdir)
{
    char text[1024];
    snprintf(text, sizeof(text),
             "\"AppState\"\n{\n\t\"appid\"\t\t\"%s\"\n\t\"Universe\"\t\t\"1\"\n\t\"name\"\t\t\"A \\\"quoted\\\" game\"\n"
             "\t\"StateFlags\"\t\t\"4\"\n\t\"installdir\"\t\t\"%s\"\n\t\"InstalledDepots\"\n\t{\n\t\t\"228990\"\n\t\t{\n"
             "\t\t\t\"manifest\"\t\t\"1\"\n\t\t}\n\t}\n\t\"UserConfig\"\n\t{\n\t\t\"installdir\"\t\t\"Wrong\"\n\t}\n}\n",
             appid, installdir);
    write_file(name, text);
}

static void words(const char *const *env, size_t count, const char *found, const char *first, const char *second)
{
    char out[2][PW_STEAM_ID_WORD];
    const size_t added = pw_steam_id_words(env, count, found, out);
    assert(added == (first ? 1u : 0u) + (second ? 1u : 0u));
    if (first) assert(!strcmp(out[0], first));
    if (second) assert(!strcmp(out[1], second));
}

int main(void)
{
    char id[PW_STEAM_ID_MAX], dir[64], temporary[] = "/tmp/pw-steam-appid-XXXXXX";
    assert(mkdtemp(temporary));
    snprintf(root, sizeof(root), "%s", temporary);

    /* The id's own rule. */
    assert(pw_steam_id_valid("250900") && pw_steam_id_valid("4294967295") && pw_steam_id_valid("1"));
    assert(!pw_steam_id_valid("") && !pw_steam_id_valid("0") && !pw_steam_id_valid("000") && !pw_steam_id_valid(NULL));
    assert(!pw_steam_id_valid("12a") && !pw_steam_id_valid("-5") && !pw_steam_id_valid("12345678901"));
    assert(!pw_steam_id_valid("90071992547409921"));   /* a 64-bit game id (a mod, a shortcut) is not an app id */

    /* A manifest: the top-level keys only (a nested "installdir" under UserConfig is not the game's), escapes read. */
    manifest("probe.acf", "250900", "Some Roguelike");
    {
        char text[2048];
        FILE *file = fopen(at("probe.acf"), "r");
        assert(file);
        const size_t length = fread(text, 1, sizeof(text), file);
        fclose(file);
        assert(pw_steam_manifest_read(text, length, id, sizeof(id), dir, sizeof(dir)) == 0);
        assert(!strcmp(id, "250900") && !strcmp(dir, "Some Roguelike"));
        /* Cut after the two keys it still reads; cut before them, or damaged, it refuses. */
        const char *cut = strstr(text, "\"Universe\"");
        assert(pw_steam_manifest_read(text, (size_t)(cut - text), id, sizeof(id), dir, sizeof(dir)) == EINVAL);
        assert(pw_steam_manifest_read(text, strstr(text, "\"InstalledDepots\"") - text, id, sizeof(id), dir, sizeof(dir)) == 0);
        const char *bad = "\"AppState\" { \"appid\" \"abc\" \"installdir\" \"x\" }", *wide = "\"AppState\" { \"appid\" \"7\" \"installdir\" \"long\" }";
        assert(pw_steam_manifest_read(bad, strlen(bad), id, sizeof(id), dir, sizeof(dir)) == EINVAL);
        assert(pw_steam_manifest_read("", 0, id, sizeof(id), dir, sizeof(dir)) == EINVAL);
        assert(pw_steam_manifest_read(wide, strlen(wide), id, sizeof(id), dir, 3) == EINVAL);
        assert(pw_steam_manifest_read(wide, strlen(wide), id, sizeof(id), dir, sizeof(dir)) == 0 && !strcmp(dir, "long"));
    }

    /* A Steam library: the program anywhere under common/<installdir>, the folder matched without regard to case. */
    folder("Steam/steamapps/common/Platformer2D/Mods");
    folder("Steam/steamapps/common/Some Action Game/x64");
    folder("Steam/steamapps/common/Orphan");
    manifest("Steam/steamapps/appmanifest_504230.acf", "504230", "Platformer2D");
    manifest("Steam/steamapps/appmanifest_1145350.acf", "1145350", "Some Action Game");
    manifest("Steam/steamapps/appmanifest_228980.acf", "228980", "Steamworks Common Redistributables");
    write_file("Steam/steamapps/appmanifest_notes.acf", "not a manifest");
    write_file("Steam/steamapps/common/Platformer2D/Platformer2D.exe", "MZ");
    write_file("Steam/steamapps/common/Some Action Game/x64/ActionGame.exe", "MZ");
    write_file("Steam/steamapps/common/Orphan/Orphan.exe", "MZ");
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Platformer2D/Platformer2D.exe"), id, sizeof(id)) == 0 && !strcmp(id, "504230"));
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Platformer2D/Mods/deep/Mod.exe"), id, sizeof(id)) == 0 && !strcmp(id, "504230"));
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Some Action Game/x64/ActionGame.exe"), id, sizeof(id)) == 0 && !strcmp(id, "1145350"));
    assert(pw_steam_appid_of(at("Steam/STEAMAPPS/Common/PLATFORMER2D/Platformer2D.exe"), id, sizeof(id)) == 0 && !strcmp(id, "504230"));
    /* The game's name (the handover's screen, src/pw_title_play.h): its manifest's top-level "name", escapes read, the
     * folder in any case; none for an id with no manifest, a bad id, or a name that does not fit */
    {
        char name[64];
        assert(pw_steam_app_name(at("Steam/steamapps"), "504230", name, sizeof(name)) == 0 && !strcmp(name, "A \"quoted\" game"));
        assert(pw_steam_app_name(at("steam/SteamApps"), "1145350", name, sizeof(name)) == 0);
        assert(pw_steam_app_name(at("Steam/steamapps"), "999", name, sizeof(name)) == ENOENT);
        assert(pw_steam_app_name(at("Steam/steamapps"), "0", name, sizeof(name)) == EINVAL);
        assert(pw_steam_app_name(at("Steam/steamapps"), "504230", name, 4) == EINVAL);
        name[0] = 0;
        assert(name_on_small_stack(name) == 0 && !strcmp(name, "A \"quoted\" game"));
        const char *spaced = "\"AppState\" { \"appid\" \"7\" \"name\" \"  A Game Demo \" }";
        assert(pw_steam_manifest_name(spaced, strlen(spaced), name, sizeof(name)) == 0 && !strcmp(name, "A Game Demo"));
        const char *blank = "\"AppState\" { \"name\" \"   \" }";
        assert(pw_steam_manifest_name(blank, strlen(blank), name, sizeof(name)) == EINVAL);
        const char *nested = "\"AppState\" { \"UserConfig\" { \"name\" \"inner\" } \"appid\" \"7\" }";
        assert(pw_steam_manifest_name(nested, strlen(nested), name, sizeof(name)) == EINVAL);
    }
    /* The game's artwork for the handover's screen: none at first (the screen's own backdrop), then each cache layout,
     * the newest first; a bad id or no room is refused */
    {
        char hero[600];
        assert(pw_steam_hero_path(at("Steam"), "504230", hero, sizeof(hero)) == ENOENT);
        folder("Steam/appcache/librarycache");
        assert(pw_steam_hero_path(at("Steam"), "504230", hero, sizeof(hero)) == ENOENT);
        write_file("Steam/appcache/librarycache/504230_library_hero.jpg", "old");
        assert(pw_steam_hero_path(at("Steam"), "504230", hero, sizeof(hero)) == 0 && strstr(hero, "504230_library_hero.jpg"));
        folder("Steam/appcache/librarycache/504230/3f2a9c");
        write_file("Steam/appcache/librarycache/504230/3f2a9c/library_hero.jpg", "deeper");
        assert(pw_steam_hero_path(at("steam"), "504230", hero, sizeof(hero)) == 0 && strstr(hero, "3f2a9c/library_hero.jpg"));
        write_file("Steam/appcache/librarycache/504230/LIBRARY_HERO.JPG", "new");
        assert(pw_steam_hero_path(at("Steam"), "504230", hero, sizeof(hero)) == 0 && strstr(hero, "504230/LIBRARY_HERO.JPG"));
        assert(pw_steam_hero_path(at("Steam"), "1145350", hero, sizeof(hero)) == ENOENT);
        assert(pw_steam_hero_path(at("Steam"), "0", hero, sizeof(hero)) == EINVAL);
        assert(pw_steam_hero_path(at("Steam"), "504230", hero, 8) == ENOENT);
    }
    /* A folder with no manifest, a path outside any library, and the folder itself (no program inside) say nothing. */
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Orphan/Orphan.exe"), id, sizeof(id)) == ENOENT);
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Platformer2D"), id, sizeof(id)) == ENOENT);
    assert(pw_steam_appid_of(at("Games/Platformer2D/Platformer2D.exe"), id, sizeof(id)) == ENOENT);
    assert(pw_steam_appid_of(NULL, id, sizeof(id)) == EINVAL && pw_steam_appid_of("/x", id, 4) == EINVAL);

    /* Two manifests naming one folder: the lowest id, whatever order the folder lists them in. */
    folder("Twin/steamapps/common/Shared");
    manifest("Twin/steamapps/appmanifest_900.acf", "900", "Shared");
    manifest("Twin/steamapps/appmanifest_100.acf", "100", "Shared");
    assert(pw_steam_appid_of(at("Twin/steamapps/common/Shared/a.exe"), id, sizeof(id)) == 0 && !strcmp(id, "100"));

    /* steam_appid.txt beside the program (Steamworks' own file), any case; the manifest wins when both say. */
    folder("Games/Gog Copy");
    write_file("Games/Gog Copy/game.exe", "MZ");
    assert(pw_steam_appid_of(at("Games/Gog Copy/game.exe"), id, sizeof(id)) == ENOENT);
    write_file("Games/Gog Copy/Steam_AppId.TXT", "  480\r\n");
    assert(pw_steam_appid_of(at("Games/Gog Copy/game.exe"), id, sizeof(id)) == 0 && !strcmp(id, "480"));
    write_file("Games/Gog Copy/Steam_AppId.TXT", "not an id\n");
    assert(pw_steam_appid_of(at("Games/Gog Copy/game.exe"), id, sizeof(id)) == ENOENT);
    write_file("Steam/steamapps/common/Platformer2D/steam_appid.txt", "480\n");
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Platformer2D/Platformer2D.exe"), id, sizeof(id)) == 0 && !strcmp(id, "504230"));
    write_file("Steam/steamapps/common/Orphan/steam_appid.txt", "1234567");
    assert(pw_steam_appid_of(at("Steam/steamapps/common/Orphan/Orphan.exe"), id, sizeof(id)) == 0 && !strcmp(id, "1234567"));

    /* The words: both names from what was found; an explicit id wins and fills the other; nothing for a name set. */
    {
        const char *none[] = {"WINE_PS5_OPENGL=1"};
        const char *game_only[] = {"SteamGameId=111", "FOO=1"};
        const char *app_only[] = {"SteamAppId=222"};
        const char *both[] = {"SteamGameId=111", "SteamAppId=222"};
        const char *later[] = {"SteamGameId=1", "SteamGameId=333"};
        const char *off[] = {"SteamGameId="};
        const char *mod[] = {"SteamGameId=90071992547409921"};
        words(none, 1, "250900", "SteamGameId=250900", "SteamAppId=250900");
        words(NULL, 0, "250900", "SteamGameId=250900", "SteamAppId=250900");
        words(game_only, 2, "250900", "SteamAppId=111", NULL);
        words(app_only, 1, "250900", "SteamGameId=222", NULL);
        words(both, 2, "250900", NULL, NULL);
        words(later, 2, NULL, "SteamAppId=333", NULL);
        words(none, 1, NULL, NULL, NULL);
        words(none, 1, "12x", NULL, NULL);
        words(off, 1, "250900", NULL, NULL);     /* an entry that blanks SteamGameId keeps it blank */
        words(mod, 1, "250900", NULL, NULL);     /* a 64-bit game id is the entry's own; no app id is guessed from it */
    }

    char command[600];
    snprintf(command, sizeof(command), "rm -rf '%s'", temporary);
    assert(system(command) == 0);
    puts("test_pw_steam_appid: ok");
    return 0;
}
