/* SPDX-License-Identifier: GPL-3.0-or-later */
/* src/pw_dialog_keys.h: the controller's Cross and D-pad as Enter and arrows, only while a dialog is in front. */
#include <assert.h>
#include <stdio.h>
#include "../src/pw_dialog_keys.h"

int main(void)
{
    PwDialogKeys keys = { 0 };
    /* The codes are a PC keyboard's: virtual key, set-1 scan code, extended flag for the arrows. */
    assert(pw_dialog_key_code(PW_DIALOG_KEY_CROSS) == 0x1c0d);
    assert(pw_dialog_key_code(PW_DIALOG_KEY_UP) == (0x10000 | 0x4826));
    assert(pw_dialog_key_code(PW_DIALOG_KEY_DOWN) == (0x10000 | 0x5028));
    assert(pw_dialog_key_code(PW_DIALOG_KEY_LEFT) == (0x10000 | 0x4b25));
    assert(pw_dialog_key_code(PW_DIALOG_KEY_RIGHT) == (0x10000 | 0x4d27));
    assert(pw_dialog_key_code(PW_DIALOG_KEYS) == 0 && pw_dialog_key_code(99) == 0);
    for (unsigned i = 0; i < PW_DIALOG_KEYS; i++)
        for (unsigned j = i + 1; j < PW_DIALOG_KEYS; j++) assert(pw_dialog_key_code(i) != pw_dialog_key_code(j));

    const uint32_t enter = pw_dialog_key_code(PW_DIALOG_KEY_CROSS), down = pw_dialog_key_code(PW_DIALOG_KEY_DOWN);

    /* No dialog in front: nothing is delivered and nothing is remembered, release included. */
    assert(!pw_dialog_key_deliver(&keys, enter, 1, 0) && keys.down == 0);
    assert(!pw_dialog_key_deliver(&keys, enter, 0, 0) && !pw_dialog_key_deliver(&keys, enter, 0, 1));
    /* A dialog in front: the press goes, and so does its release even when the dialog has gone. */
    assert(pw_dialog_key_deliver(&keys, enter, 1, 1) && keys.down == 1);
    assert(pw_dialog_key_deliver(&keys, enter, 0, 0) && keys.down == 0);
    assert(!pw_dialog_key_deliver(&keys, enter, 0, 0));                           /* a second release is dropped */
    /* Two keys at once are kept apart, and a held key repeats while the dialog is there. */
    assert(pw_dialog_key_deliver(&keys, down, 1, 1) && pw_dialog_key_deliver(&keys, enter, 1, 1));
    assert(keys.down == ((1u << PW_DIALOG_KEY_DOWN) | (1u << PW_DIALOG_KEY_CROSS)));
    assert(pw_dialog_key_deliver(&keys, down, 1, 1));
    assert(!pw_dialog_key_deliver(&keys, down, 1, 0));                            /* the dialog closed: no more repeats */
    assert(pw_dialog_key_deliver(&keys, down, 0, 0) && keys.down == (1u << PW_DIALOG_KEY_CROSS));
    assert(pw_dialog_key_deliver(&keys, enter, 0, 0) && keys.down == 0);
    /* Codes that are not the five are never delivered, whatever the dialog. */
    assert(!pw_dialog_key_deliver(&keys, 0x41 | (0x1e << 8), 1, 1) && !pw_dialog_key_deliver(&keys, 0, 1, 1) && keys.down == 0);
    assert(!pw_dialog_key_deliver(&keys, 0x26 | (0x48 << 8), 1, 1));              /* the arrow without the extended flag */
    puts("pw_dialog_keys: all checks passed");
    return 0;
}
