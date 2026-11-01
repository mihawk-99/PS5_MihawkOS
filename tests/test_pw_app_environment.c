/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Each catalog game's settings for a process another one started (src/pw_app_environment.h), with the console's
 * catalog after the move into Mihawk OS (tools/mihawk-games.py, 2026-10-10). */
#include "../src/pw_app_environment.h"
#include "../src/pw_environment.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* A game's WINEDLLOVERRIDES joins the title's, which Wine reads last-wins (pw_app_environment_join_overrides). */
static void join_overrides(void)
{
    static const char title[] = "opengl32=n,b;d3d11,dxgi,d3d10core,d3d9,d3d12,d3d12core=n;bcrypt=b";
    char out[256], small[24];
    assert(pw_app_environment_join_overrides(title, "WINEDLLOVERRIDES=dinput8=n,b", out, sizeof(out)) == 1);
    assert(!strcmp(out, "WINEDLLOVERRIDES=opengl32=n,b;d3d11,dxgi,d3d10core,d3d9,d3d12,d3d12core=n;bcrypt=b;dinput8=n,b"));
    /* the game still decides a DLL the title names: its entry comes last */
    assert(pw_app_environment_join_overrides(title, "WINEDLLOVERRIDES=d3d12=b", out, sizeof(out)) == 1);
    assert(strstr(out, ";d3d12=b") == out + strlen(out) - strlen(";d3d12=b"));
    /* other settings, a name that only starts alike, and no title stay as they are */
    assert(!pw_app_environment_join_overrides(title, "WINE_PS5_OPENGL=1", out, sizeof(out)));
    assert(!pw_app_environment_join_overrides(title, "WINEDLLOVERRIDES_X=1", out, sizeof(out)));
    assert(!pw_app_environment_join_overrides(NULL, "WINEDLLOVERRIDES=dinput8=n", out, sizeof(out)));
    assert(!pw_app_environment_join_overrides("", "WINEDLLOVERRIDES=dinput8=n", out, sizeof(out)));
    /* too small: refused, nothing half written */
    small[0] = 'x';
    assert(pw_app_environment_join_overrides(title, "WINEDLLOVERRIDES=dinput8=n", small, sizeof(small)) == -1);
    assert(small[0] == 'x');
    /* exactly the room it needs, and one byte short */
    const size_t need = strlen("WINEDLLOVERRIDES=a;b") + 1;
    assert(pw_app_environment_join_overrides("a", "WINEDLLOVERRIDES=b", out, need) == 1 && !strcmp(out, "WINEDLLOVERRIDES=a;b"));
    assert(pw_app_environment_join_overrides("a", "WINEDLLOVERRIDES=b", out, need - 1) == -1);
}

int main(void)
{
    join_overrides();
    const char *game_env[] = {"WINE_PS5_OPENGL=1", "SDL_XINPUT_ENABLED=0"};
    const char *java_env[] = {"WINE_PS5_OPENGL=1", "PROTON_120HZ=1", "WINE_PS5_NTSYNC=1"};
    const PwAppEntry catalog[] = {
        {"Example Game", "C:\\Games\\Example Game\\Example Game.exe", "", game_env, 2},
        {"Java Game", "C:\\Games\\JavaGame\\java\\java-runtime-epsilon\\bin\\javaw.exe",
         "@C:/Games/JavaGame/launch.args", java_env, 3},
        {"Java file test", "C:\\Games\\JavaGame\\java\\java-runtime-epsilon\\bin\\javaw.exe",
         "-cp C:/Games/JavaFileTest FsTest2 C:/Games/JavaFileTest/out2 200 400", NULL, 0},
        {"Launcher", "C:\\Program Files (x86)\\Launcher\\launcher.exe", "-no-cef-sandbox", NULL, 0},
    };
    const size_t count = sizeof(catalog) / sizeof(catalog[0]);
    size_t index = 99;

    /* The settings a game may have per application, and the title's own. */
    assert(pw_app_environment_per_app("WINE_PS5_OPENGL=1") && pw_app_environment_per_app("SDL_XINPUT_ENABLED=0"));
    assert(pw_app_environment_per_app("DOTNET_gcServer=0") && pw_app_environment_per_app("EMPTY="));
    assert(!pw_app_environment_per_app("PROTON_120HZ=1") && !pw_app_environment_per_app("MIHAWK_SESSION=1"));
    assert(!pw_app_environment_per_app("WINE_PS5_NTSYNC=1") && pw_app_environment_per_app("WINE_PS5_NTSYNC_TRACE=1"));
    assert(!pw_app_environment_per_app("NOEQUALS") && !pw_app_environment_per_app("=x") && !pw_app_environment_per_app(NULL));

    /* Paths: case, slashes and the NT prefixes Windows may hand over. */
    assert(pw_app_environment_same_path("c:/games/example game/EXAMPLE GAME.EXE",
                                        "C:\\Games\\Example Game\\Example Game.exe"));
    assert(pw_app_environment_same_path("\\\\?\\C:\\Games\\x.exe", "C:\\Games\\x.exe"));
    assert(pw_app_environment_same_path("\\??\\C:\\Games\\x.exe", "c:/games/x.exe"));
    assert(!pw_app_environment_same_path("C:\\Games\\x.exe", "C:\\Games\\x.exe2"));
    assert(!pw_app_environment_same_path("C:\\Games\\x", "C:\\Games\\x.exe"));
    assert(!pw_app_environment_same_path("", "") && !pw_app_environment_same_path(NULL, "C:\\x"));

    /* A shortcut's game: found, and only its own settings. */
    assert(pw_app_environment_find(catalog, count, "C:\\Games\\Example Game\\Example Game.exe",
                                   "\"C:\\Games\\Example Game\\Example Game.exe\"", &index) == PW_APP_FOUND && index == 0);
    char block[4096];
    uint32_t bytes = 0;
    size_t kept = 0;
    assert(!pw_app_environment_block(&catalog[0], block, sizeof(block), &bytes, &kept) && kept == 2);
    assert(!pw_environment_validate(block, bytes));
    assert(!strcmp(block, "WINE_PS5_OPENGL=1") && !strcmp(block + 18, "SDL_XINPUT_ENABLED=0"));

    /* One executable, two entries: the arguments decide; the Java game keeps only what can be per application. */
    assert(pw_app_environment_find(catalog, count, "C:\\Games\\JavaGame\\java\\java-runtime-epsilon\\bin\\javaw.exe",
                                   "javaw.exe @C:/Games/JavaGame/launch.args", &index) == PW_APP_FOUND && index == 1);
    assert(!pw_app_environment_block(&catalog[1], block, sizeof(block), &bytes, &kept) && kept == 1);
    assert(!strcmp(block, "WINE_PS5_OPENGL=1") && bytes == 19);
    assert(pw_app_environment_find(catalog, count, "C:\\Games\\JavaGame\\java\\java-runtime-epsilon\\bin\\javaw.exe",
                                   "javaw -cp C:/Games/JavaFileTest FsTest2 C:/Games/JavaFileTest/out2 200 400",
                                   &index) == PW_APP_FOUND && index == 2);
    assert(!pw_app_environment_block(&catalog[2], block, sizeof(block), &bytes, &kept) && kept == 0 && bytes == 2);
    /* The same Java started some other way (a Java launcher's own launch): neither entry, so nothing. */
    assert(pw_app_environment_find(catalog, count, "C:\\Games\\JavaGame\\java\\java-runtime-epsilon\\bin\\javaw.exe",
                                   "javaw -Xmx2G net.example.game.Main", &index) == PW_APP_AMBIGUOUS);
    /* Not in the catalog. */
    assert(pw_app_environment_find(catalog, count, "C:\\windows\\system32\\notepad.exe", "notepad", &index) == PW_APP_NONE);
    assert(pw_app_environment_find(catalog, count, NULL, NULL, &index) == PW_APP_NONE);

    /* With one entry that has no arguments, a launch without the others' arguments gets it. */
    const PwAppEntry pair[] = {
        {"plain", "C:\\a.exe", NULL, game_env, 2},
        {"argued", "C:\\a.exe", "--x", java_env, 3},
    };
    assert(pw_app_environment_find(pair, 2, "C:\\a.exe", "a.exe --y", &index) == PW_APP_FOUND && index == 0);
    assert(pw_app_environment_find(pair, 2, "C:\\a.exe", "a.exe --x", &index) == PW_APP_FOUND && index == 1);

    /* Refusals. */
    assert(pw_app_environment_block(&catalog[0], block, 8, &bytes, &kept) == E2BIG);
    const char *bad_env[] = {"WINE_PS5_OPENGL=1", "=broken"};
    const PwAppEntry bad = {"bad", "C:\\b.exe", NULL, bad_env, 2};
    assert(pw_app_environment_block(&bad, block, sizeof(block), &bytes, &kept) == EINVAL);
    /* A game's own controller: the HID stack follows it, and SDL keeps XInput beside an Xbox controller */
    const char *xbox_env[] = {"WINE_PS5_CONTROLLER=xbox-one", "SDL_XINPUT_ENABLED=0", "WINE_PS5_GAME_MODE=0"};
    const PwAppEntry xbox = {"Xbox", "C:\\Games\\X\\x.exe", NULL, xbox_env, 3};
    assert(!pw_app_environment_block(&xbox, block, sizeof(block), &bytes, &kept) && kept == 3);
    assert(!strcmp(block, "WINE_PS5_CONTROLLER=xbox-one") && !strcmp(block + 29, "WINE_PS5_GAME_MODE=0") &&
           !strcmp(block + 50, "WINE_PS5_HID=0"));
    const char *pad_env[] = {"WINE_PS5_CONTROLLER=dualsense", "SDL_XINPUT_ENABLED=0"};
    const PwAppEntry pad = {"Pad", "C:\\Games\\P\\p.exe", NULL, pad_env, 2};
    assert(!pw_app_environment_block(&pad, block, sizeof(block), &bytes, &kept) && kept == 3);
    assert(!strcmp(block + 30, "SDL_XINPUT_ENABLED=0") && !strcmp(block + 51, "WINE_PS5_HID=1"));
    assert(pw_app_environment_block(NULL, block, sizeof(block), &bytes, &kept) == EINVAL);
    puts("application environment: catalog matching, shared executables, per-application settings only");
    return 0;
}
