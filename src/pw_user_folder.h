/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_USER_FOLDER_H
#define PW_USER_FOLDER_H
#include <stdint.h>

/* The folders above a folder a program makes in a user's own folders (Wine patch 2417).
 *
 * A game copied from a PC was never installed here, so the folders its installer makes under C:\users\<name>\ are
 * missing: Call of Juarez Gunslinger's engine makes AppData\Local\Techland\COJGunslinger, relies on the installer for
 * Techland, and stopped on an uncaught C++ exception when that failed (STATUS_OBJECT_PATH_NOT_FOUND). ntdll's
 * NtCreateFile makes the missing parents of such a folder first, and this says where a name's parent ends.
 *
 * `name` is an NT name in UTF-16 (`length` characters, no terminator): "\??\C:\users\<name>\<folder>[\...]\<last>".
 * Returns the parent's length (the name up to, not including, the last backslash) when the name lies inside a user's own
 * folder, deeper than the user's folder itself, so the parent is a folder below it; 0 for every other name: another drive
 * or folder, the users folder, a user's own folder, a root directory handle's relative name (the caller checks), an empty
 * or "." or ".." component, a doubled backslash. Case is ignored in the prefix, as Windows does. A trailing backslash is
 * dropped first. Host test: tests/test_pw_user_folder.c. */
static inline unsigned pw_user_folder_parent_length(const uint16_t *name, unsigned length)
{
    static const char prefix[] = "\\??\\c:\\users\\";
    const unsigned prefix_length = sizeof(prefix) - 1;
    unsigned i, last = 0, component = 0, below_user = 0;

    while (length && name[length - 1] == '\\') length--;
    if (length <= prefix_length) return 0;
    for (i = 0; i < prefix_length; i++) {
        uint16_t c = name[i];
        if (c >= 'A' && c <= 'Z') c = (uint16_t)(c - 'A' + 'a');
        if (c != (unsigned char)prefix[i]) return 0;
    }
    /* Walk the components after the prefix: each is non-empty and not "." or "..". */
    for (i = prefix_length; i <= length; i++) {
        if (i == length || name[i] == '\\') {
            const unsigned size = i - component;
            if (!size || (size == 1 && name[component] == '.') || (size == 2 && name[component] == '.' && name[component + 1] == '.'))
                return 0;
            if (i < length) { last = i; below_user++; }
            component = i + 1;
        }
    }
    /* below_user counts the backslashes after the prefix: 0 for "users\<name>", 1 for "users\<name>\x" whose parent is the
     * user's own folder (never made here), 2 and more have a parent inside it. */
    return below_user >= 2 ? last : 0;
}
#endif
