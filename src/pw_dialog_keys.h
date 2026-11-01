/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_DIALOG_KEYS_H
#define PW_DIALOG_KEYS_H
#include <stdint.h>

/* The controller's Cross and D-pad as Enter and the arrows, only while a dialog box is in front (Wine patch 2418).
 *
 * A game's pop-up (a minimum-specifications warning, a Yes/No question, "run the launcher?") is a Windows dialog box, and
 * with the controller as the game's XInput pad or DualSense there is no keyboard to answer it with. The title sends
 * Cross and the D-pad's presses and releases as PW_WINE_INPUT_DIALOG_KEY events (wine/ps5/pw_wine_sink.h) whatever the game
 * is doing, and the server (server/queue.c, ps5_inject_input) delivers a press as a key only while the active window is a
 * dialog box (class #32770), so a game that uses these buttons itself never gets a stray Enter or arrow. A release is
 * delivered when its press was, even if the dialog is gone by then, so no key stays down.
 *
 * Keys are the codes of a KEY event: the virtual key in bits 0-7, its set-1 scan code in bits 8-15, bit 16 the extended
 * flag (wine/ps5/pw_wine_sink.h), those of a PC keyboard. Host test: tests/test_pw_dialog_keys.c. */
enum { PW_DIALOG_KEY_CROSS, PW_DIALOG_KEY_UP, PW_DIALOG_KEY_DOWN, PW_DIALOG_KEY_LEFT, PW_DIALOG_KEY_RIGHT, PW_DIALOG_KEYS };

/* The KEY code Cross, Up, Down, Left, Right send; 0 for another index. */
static inline uint32_t pw_dialog_key_code(unsigned index)
{
    static const uint32_t codes[PW_DIALOG_KEYS] = {
        0x0d | (0x1c << 8),                 /* Enter */
        0x26 | (0x48 << 8) | (1u << 16),    /* Up */
        0x28 | (0x50 << 8) | (1u << 16),    /* Down */
        0x25 | (0x4b << 8) | (1u << 16),    /* Left */
        0x27 | (0x4d << 8) | (1u << 16),    /* Right */
    };
    return index < PW_DIALOG_KEYS ? codes[index] : 0;
}

/* Which of the five are down: a bit each, in the order above. The server keeps one. */
typedef struct PwDialogKeys { uint8_t down; } PwDialogKeys;

/* Whether the server delivers this event as a key. `code` is one of the five codes; a press is delivered when a dialog is
 * in front, a release when its press was. Anything
 * else is dropped and changes nothing. */
static inline int pw_dialog_key_deliver(PwDialogKeys *keys, uint32_t code, unsigned down, int dialog_in_front)
{
    for (unsigned i = 0; i < PW_DIALOG_KEYS; i++) {
        if (pw_dialog_key_code(i) != code) continue;
        if (down) {
            if (!dialog_in_front) return 0;
            keys->down = (uint8_t)(keys->down | (1u << i));
            return 1;
        }
        if (!(keys->down & (1u << i))) return 0;
        keys->down = (uint8_t)(keys->down & ~(1u << i));
        return 1;
    }
    return 0;
}
#endif
