/* SPDX-License-Identifier: GPL-3.0-or-later */
/* MihawkOS's own on-screen keyboard (src/pw_vkbd.h): its pages, geometry, focus, Shift, the line and the stick. */
#include "../src/pw_vkbd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *line(const PwVkbd *kbd)
{
    static char out[PW_VKBD_TEXT * 4 + 1];
    pw_vkbd_utf8(kbd, out, sizeof(out));
    return out;
}

int main(void)
{
    PwVkbd kbd;
    PwVkbdKey key;
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    /* every key of every page is on the screen and inside the keyboard's width, and none overlaps the next */
    for (int page = 0; page < PW_VKBD_PAGES; ++page) {
        kbd.page = page;
        for (int shift = 0; shift < 2; ++shift) {
            kbd.shift = shift;
            for (int row = 0; row < PW_VKBD_ROWS; ++row) {
                float right = 0;
                for (int col = 0; col < pw_vkbd_row_keys(row); ++col) {
                    assert(pw_vkbd_key(&kbd, row, col, &key));
                    assert(key.x >= PW_VKBD_KEYS_X - 0.01f && key.x + key.w <= PW_VKBD_KEYS_X + PW_VKBD_FIELD_W + 0.01f);
                    assert(key.y + key.h <= 1080 && key.x >= right);
                    if (key.kind == PW_VKBD_CHAR) assert(key.text[0]);
                    right = key.x + key.w;
                }
                assert(right > PW_VKBD_KEYS_X + PW_VKBD_FIELD_W - 1);   /* each row spans the keyboard */
            }
        }
    }
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    /* typing h, i with the focus: starts on q (row 1) */
    assert(pw_vkbd_key(&kbd, kbd.row, kbd.col, &key) && !strcmp(key.text, "q"));
    pw_vkbd_move(&kbd, 0, 1);                   /* a */
    for (int i = 0; i < 5; ++i) pw_vkbd_move(&kbd, 1, 0);   /* h */
    assert(pw_vkbd_press(&kbd) == PW_VKBD_TYPED && !strcmp(kbd.typed, "h"));   /* what the game is sent */
    pw_vkbd_move(&kbd, 0, -1);                  /* y, above h (the rows are not staggered) */
    pw_vkbd_move(&kbd, 2, 0);                   /* i */
    assert(pw_vkbd_press(&kbd) == PW_VKBD_TYPED && !strcmp(line(&kbd), "hi"));
    /* left wraps; Shift types one capital */
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    pw_vkbd_move(&kbd, -1, 0);
    assert(pw_vkbd_key(&kbd, kbd.row, kbd.col, &key) && !strcmp(key.text, "p"));
    kbd.shift = 1;
    assert(pw_vkbd_press(&kbd) == PW_VKBD_TYPED && pw_vkbd_press(&kbd) == PW_VKBD_TYPED && !strcmp(line(&kbd), "Pp"));
    /* down from the letters to the last row lands on the key under the focus; up again comes back near it */
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    kbd.row = 3, kbd.col = 5;                   /* n */
    pw_vkbd_move(&kbd, 0, 1);
    assert(kbd.row == 4 && pw_vkbd_key(&kbd, 4, kbd.col, &key) && key.kind == PW_VKBD_SPACE);
    pw_vkbd_move(&kbd, 0, 1);                   /* wraps to the top row */
    assert(kbd.row == 0);
    /* the last row: Space, Backspace, Done, the page key */
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    kbd.row = 4;
    kbd.col = 2; assert(pw_vkbd_press(&kbd) == PW_VKBD_TYPED && !strcmp(line(&kbd), " ") && !strcmp(kbd.typed, " "));
    kbd.col = 3; assert(pw_vkbd_press(&kbd) == PW_VKBD_ERASED && !strcmp(line(&kbd), ""));
    /* the field empty, Backspace is still Backspace: the game's text typed before (a tester, 2026-11-02: "the backspace
     * does nothing" after the line was sent) */
    assert(pw_vkbd_press(&kbd) == PW_VKBD_ERASED && pw_vkbd_backspace(&kbd) == PW_VKBD_ERASED);
    kbd.col = 1; assert(pw_vkbd_key(&kbd, 4, 1, &key) && !strcmp(key.text, "123"));
    pw_vkbd_press(&kbd); assert(kbd.page == PW_VKBD_SYMBOLS);
    kbd.col = 4; assert(pw_vkbd_press(&kbd) == PW_VKBD_FINISHED);
    /* the accented and the Russian pages type their letters; paging back wraps */
    pw_vkbd_open(&kbd, PW_VKBD_ACCENTS);
    kbd.row = 0, kbd.col = 9;
    assert(pw_vkbd_press(&kbd) == PW_VKBD_TYPED && !strcmp(line(&kbd), "é"));
    pw_vkbd_page(&kbd, 1);
    assert(kbd.page == PW_VKBD_CYRILLIC);
    kbd.row = 2, kbd.col = 0; kbd.shift = 1;
    assert(pw_vkbd_press(&kbd) == PW_VKBD_TYPED && !strcmp(line(&kbd), "éЖ"));
    pw_vkbd_page(&kbd, 1); assert(kbd.page == PW_VKBD_ABC);
    pw_vkbd_page(&kbd, -1); assert(kbd.page == PW_VKBD_CYRILLIC);
    /* a touch types the key under it; outside the keys, nothing */
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    pw_vkbd_key(&kbd, 2, 0, &key);                 /* a */
    assert(pw_vkbd_touch(&kbd, key.x + 5, key.y + 5) == PW_VKBD_TYPED && !strcmp(line(&kbd), "a") && kbd.row == 2);
    assert(pw_vkbd_touch(&kbd, 10, 10) == PW_VKBD_NONE);
    /* the test line (any character, a plane-2 one too) goes in and out whole; the limit holds */
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    assert(pw_vkbd_type(&kbd, "héllo Ж日\xf0\xa0\x80\x80") == PW_VKBD_TYPED && !strcmp(line(&kbd), "héllo Ж日\xf0\xa0\x80\x80"));
    assert(pw_vkbd_backspace(&kbd) == PW_VKBD_ERASED && !strcmp(line(&kbd), "héllo Ж日"));   /* the pair as one */
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    for (int i = 0; i < PW_VKBD_TEXT; ++i) assert(pw_vkbd_type(&kbd, "x") == PW_VKBD_TYPED);
    /* full, typing goes on: the field scrolls, the oldest unit gone, a pair as one */
    assert(pw_vkbd_type(&kbd, "y") == PW_VKBD_TYPED && kbd.length == PW_VKBD_TEXT && kbd.text[PW_VKBD_TEXT - 1] == 'y');
    char small[4];
    assert(pw_vkbd_utf8(&kbd, small, sizeof(small)) == 3 && !strcmp(small, "xxx"));
    pw_vkbd_open(&kbd, PW_VKBD_ABC);
    assert(pw_vkbd_type(&kbd, "\xf0\xa0\x80\x80") == PW_VKBD_TYPED && kbd.length == 2);
    for (int i = 0; i < PW_VKBD_TEXT - 2; ++i) pw_vkbd_type(&kbd, "x");
    assert(pw_vkbd_type(&kbd, "z") == PW_VKBD_TYPED && kbd.length == PW_VKBD_TEXT - 1 && kbd.text[0] == 'x');
    /* the stick: a move at once, then none until 400 ms, then one every 140 ms; back to the centre resets it */
    PwVkbdStick stick = {0};
    int dx = 0, dy = 0;
    assert(pw_vkbd_stick(&stick, 0.9f, 0.1f, 1000, &dx, &dy) && dx == 1 && dy == 0);
    assert(!pw_vkbd_stick(&stick, 0.9f, 0.1f, 1300, &dx, &dy));
    assert(pw_vkbd_stick(&stick, 0.9f, 0.1f, 1400, &dx, &dy));
    assert(!pw_vkbd_stick(&stick, 0.9f, 0.1f, 1500, &dx, &dy));
    assert(pw_vkbd_stick(&stick, 0.9f, 0.1f, 1540, &dx, &dy));
    assert(!pw_vkbd_stick(&stick, 0.1f, 0.1f, 1600, &dx, &dy));
    assert(pw_vkbd_stick(&stick, 0.2f, -0.8f, 1610, &dx, &dy) && dx == 0 && dy == -1);
    puts("pw_vkbd: ok");
    return 0;
}
