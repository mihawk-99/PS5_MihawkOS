/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The shortcut's name and the Start pin list (src/pw_shortcut.h), from hand-written inputs. */
#include "../src/pw_shortcut.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t widen(pw_u16 *out, const char *text)
{
    size_t n = 0;
    for (; text[n]; ++n) out[n] = (unsigned char)text[n];
    out[n] = 0;
    return n;
}
static int named(const char *target, int folder, unsigned attempt, const char *expected)
{
    pw_u16 in[300], out[300];
    char got[300];
    widen(in, target);
    const size_t n = pw_shortcut_name(out, 300, in, folder, attempt);
    if (!expected) return n == 0;
    for (size_t i = 0; i <= n; ++i) got[i] = (char)out[i];
    return n == strlen(expected) && !strcmp(got, expected);
}
static size_t pins(pw_u16 *list, const char *const *paths)
{
    size_t n = 0;
    for (; *paths; ++paths) n += widen(list + n, *paths) + 1;
    list[n++] = 0;
    return n;
}

int main(void)
{
    assert(named("C:\\Games\\Foo\\Foo.exe", 0, 1, "Foo - Shortcut.lnk"));
    assert(named("C:\\Games\\Foo\\Foo.exe", 0, 2, "Foo - Shortcut (2).lnk"));
    assert(named("C:\\Games\\Foo\\Foo.exe", 0, 12, "Foo - Shortcut (12).lnk"));
    assert(named("C:\\Games\\My.Game 2\\run.v2.exe", 0, 1, "run.v2 - Shortcut.lnk"));
    assert(named("C:\\Games\\My.Game 2", 1, 1, "My.Game 2 - Shortcut.lnk"));    /* a folder keeps its dots */
    assert(named("C:\\Games\\My.Game 2\\", 1, 1, "My.Game 2 - Shortcut.lnk"));   /* a trailing separator */
    assert(named("C:\\", 1, 1, "C - Shortcut.lnk"));
    assert(named("Z:/data/notes.txt", 0, 1, "notes - Shortcut.lnk"));
    assert(named("C:\\Games\\.hidden", 0, 1, ".hidden - Shortcut.lnk"));
    assert(named("C:\\Games\\a*b?.exe", 0, 1, "ab - Shortcut.lnk"));            /* characters a name cannot hold */
    assert(named("", 0, 1, NULL) && named("C:\\x\\*.exe", 0, 1, NULL) && named("C:\\x\\a.exe", 0, 0, NULL));
    {
        pw_u16 in[16], out[8];
        widen(in, "C:\\a.exe");
        assert(!pw_shortcut_name(out, 8, in, 0, 1));                              /* too small for the name */
    }

    pw_u16 list[PW_SHORTCUT_PINS_MAX * PW_SHORTCUT_PATH_MAX + 4], path[300];
    size_t units = 0;
    widen(path, "C:\\users\\mihawk\\Start Menu\\Programs\\Foo - Shortcut.lnk");
    /* nothing yet (value absent) */
    assert(pw_shortcut_pins_add(list, 32, &units, path) == -2);                     /* no room */
    units = 0;
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == 1 && units == strlen("C:\\users\\mihawk\\Start Menu\\Programs\\Foo - Shortcut.lnk") + 2);
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == 0);        /* already there */
    widen(path, "c:\\USERS\\mihawk\\start menu\\programs\\foo - shortcut.lnk");
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == 0);        /* ASCII case folds */
    widen(path, "C:\\Games\\Bar.exe");
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == 1);
    /* a hand-written list */
    const char *written[] = {"C:\\Windows\\notepad.exe", "C:\\Games\\Baz.lnk", NULL};
    units = pins(list, written);
    widen(path, "C:\\Games\\Baz.lnk");
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == 0);
    widen(path, "C:\\Games\\New.lnk");
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == 1);
    assert(!list[units - 1] && !list[units - 2] && list[units - 3] == 'k');
    /* a list that is not whole is refused, never guessed at */
    list[0] = 'C', list[1] = ':', list[2] = 'x', units = 3;
    assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == -1);
    /* a full list */
    {
        char many[PW_SHORTCUT_PINS_MAX][16];
        const char *paths[PW_SHORTCUT_PINS_MAX + 1];
        for (int i = 0; i < PW_SHORTCUT_PINS_MAX; ++i) snprintf(many[i], sizeof(many[i]), "C:\\a\\%d.lnk", i), paths[i] = many[i];
        paths[PW_SHORTCUT_PINS_MAX] = NULL;
        units = pins(list, paths);
        assert(pw_shortcut_pins_add(list, sizeof(list) / 2, &units, path) == -1);
    }
    printf("shortcut: names, Start pins\n");
    return 0;
}
