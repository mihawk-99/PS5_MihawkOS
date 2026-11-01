/* SPDX-License-Identifier: GPL-3.0-or-later */
/* src/pw_user_folder.h: where the parent of a folder made in a user's own folders ends. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/pw_user_folder.h"

static unsigned parent(const char *ascii)
{
    uint16_t wide[256];
    size_t n = strlen(ascii);
    for (size_t i = 0; i < n; i++) wide[i] = (unsigned char)ascii[i];
    return pw_user_folder_parent_length(wide, (unsigned)n);
}

static void inside(const char *name, const char *want_parent)
{
    unsigned got = parent(name);
    if (got != strlen(want_parent) || strncmp(name, want_parent, got)) {
        printf("%s: parent length %u, wanted \"%s\"\n", name, got, want_parent);
        assert(0);
    }
}

int main(void)
{
    /* The game that needed it: Techland's folder, then the game's inside it. */
    inside("\\??\\C:\\users\\Mihawk\\AppData\\Local\\Techland\\COJGunslinger", "\\??\\C:\\users\\Mihawk\\AppData\\Local\\Techland");
    inside("\\??\\C:\\users\\Mihawk\\AppData\\Local\\Techland", "\\??\\C:\\users\\Mihawk\\AppData\\Local");
    inside("\\??\\C:\\users\\Mihawk\\AppData\\Local", "\\??\\C:\\users\\Mihawk\\AppData");
    inside("\\??\\C:\\users\\Mihawk\\Documents\\My Games", "\\??\\C:\\users\\Mihawk\\Documents");
    assert(parent("\\??\\C:\\users\\Mihawk\\AppData") == 0);          /* its parent is the user's own folder: not made here */
    /* Case of the prefix is not significant; the rest is kept as given. */
    inside("\\??\\c:\\USERS\\Mihawk\\Saved Games\\Deep\\er", "\\??\\c:\\USERS\\Mihawk\\Saved Games\\Deep");
    inside("\\??\\C:\\Users\\Public\\Documents\\Game", "\\??\\C:\\Users\\Public\\Documents");
    /* A trailing backslash is dropped. */
    inside("\\??\\C:\\users\\Mihawk\\AppData\\Local\\X\\", "\\??\\C:\\users\\Mihawk\\AppData\\Local");
    /* Names that are not inside a user's folders. */
    assert(parent("\\??\\C:\\users") == 0 && parent("\\??\\C:\\users\\") == 0 && parent("\\??\\C:\\users\\Mihawk") == 0);
    assert(parent("\\??\\C:\\windows\\system32\\drivers") == 0);
    assert(parent("\\??\\D:\\users\\Mihawk\\AppData\\Local") == 0);
    assert(parent("\\??\\C:\\usersX\\Mihawk\\AppData\\Local") == 0);
    assert(parent("\\??\\C:\\Program Files\\Techland\\Game") == 0);
    assert(parent("\\??\\Z:\\home\\mihawk\\a\\b") == 0);
    assert(parent("") == 0 && parent("AppData\\Local\\X") == 0);
    /* Components that would climb out or do nothing. */
    assert(parent("\\??\\C:\\users\\Mihawk\\..\\..\\windows\\x") == 0);
    assert(parent("\\??\\C:\\users\\Mihawk\\AppData\\.\\Local\\x") == 0);
    assert(parent("\\??\\C:\\users\\Mihawk\\AppData\\\\Local\\x") == 0);
    assert(parent("\\??\\C:\\users\\Mihawk\\AppData\\Local\\..") == 0);
    /* Dots inside a name are fine. */
    inside("\\??\\C:\\users\\Mihawk\\.config\\game.d\\save", "\\??\\C:\\users\\Mihawk\\.config\\game.d");
    /* Wide characters are compared as they are: a Cyrillic folder is a folder. */
    {
        uint16_t wide[64] = { '\\', '?', '?', '\\', 'C', ':', '\\', 'u', 's', 'e', 'r', 's', '\\', 'M', '\\', 0x041f, '\\', 0x0442 };
        assert(pw_user_folder_parent_length(wide, 18) == 16);
    }
    puts("pw_user_folder: all checks passed");
    return 0;
}
