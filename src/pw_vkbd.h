/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_VKBD_H
#define PW_VKBD_H
/* MihawkOS's own on-screen keyboard, for consoles whose system refuses the title its IME dialog (a standard PS5 on 13.60:
 * sceSysmoduleLoadModule(0x0096) and every other way gave 0x80020063, hotfix-osk-base-2026-11-01). The title draws it as a
 * screen of its guide (src/pw_steam_screen.c, PW_STEAM_SCREEN_KEYBOARD) and types the line it gives through the same
 * typer as the system dialog's (src/pw_osk.h). Here: the pages, one geometry for drawing and for the touchpad, moving the
 * focus, the line being typed. No console call; host test: tests/test_pw_vkbd.c.
 *
 * Pages: abc (QWERTY, Shift for capitals), 123 (digits and symbols), the accented letters of the menu's Latin languages
 * (fr, es, de, it, pt, pl), and Russian. Every page ends with Shift, the page key, Space, Backspace and Done. No input
 * method for Chinese or Japanese: a line may hold any character (the test line does), but only these keys type. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PW_VKBD_ABC, PW_VKBD_SYMBOLS, PW_VKBD_ACCENTS, PW_VKBD_CYRILLIC, PW_VKBD_PAGES };
enum { PW_VKBD_ROWS = 5, PW_VKBD_COLUMNS = 10, PW_VKBD_TEXT = 128 };
typedef enum { PW_VKBD_CHAR, PW_VKBD_SHIFT, PW_VKBD_PAGE, PW_VKBD_SPACE, PW_VKBD_BACK, PW_VKBD_DONE } PwVkbdKind;
/* What a key press did */
enum { PW_VKBD_NONE, PW_VKBD_TYPED, PW_VKBD_FINISHED, PW_VKBD_CANCELLED };
/* The keyboard on the 1920x1080 screen: a panel across the bottom 480 rows (the owner: "the bottom part of the screen with the
 * game visible above it"), drawn by the broker over every frame inside it (ps5_gpu_broker_set_overlay); in it the text
 * field, the keys and the hints. Its corners are on multiples of 4, so the broker writes it a 4x4 block at a time. */
#define PW_VKBD_PANEL_Y 600
#define PW_VKBD_PANEL_H 480
#define PW_VKBD_FIELD_X 165.0f
#define PW_VKBD_FIELD_Y 618.0f
#define PW_VKBD_FIELD_W 1590.0f
#define PW_VKBD_FIELD_H 60.0f
#define PW_VKBD_KEYS_X 165.0f
#define PW_VKBD_KEYS_Y 692.0f
#define PW_VKBD_KEY_W 150.0f
#define PW_VKBD_KEY_H 60.0f
#define PW_VKBD_GAP 10.0f
#define PW_VKBD_ROW_GAP 8.0f

typedef struct PwVkbdKey {
    PwVkbdKind kind;
    char text[8];        /* a character key's UTF-8; the page key: the next page's name */
    float x, y, w, h;
} PwVkbdKey;

typedef struct PwVkbd {
    int page, shift, row, col;
    uint16_t text[PW_VKBD_TEXT + 1];   /* UTF-16, as the system dialog's line */
    size_t length;
} PwVkbd;

/* Repeat for the left stick: a move as it leaves the centre, then every 140 ms after 400 ms held */
typedef struct PwVkbdStick { int dx, dy; uint64_t since_ms, last_ms; } PwVkbdStick;

void pw_vkbd_open(PwVkbd *kbd, int page);
int pw_vkbd_row_keys(int row);
/* The key at (row, col) on the shown page: 1, or 0 for no such key */
int pw_vkbd_key(const PwVkbd *kbd, int row, int col, PwVkbdKey *out);
/* Moves the focus: left and right wrap in a row; up and down go to the key under the focused one's centre */
void pw_vkbd_move(PwVkbd *kbd, int dx, int dy);
/* The focused key, the key under a touch (1920x1080; NONE where there is none), and the buttons' own actions */
int pw_vkbd_press(PwVkbd *kbd);
int pw_vkbd_touch(PwVkbd *kbd, float x, float y);
void pw_vkbd_page(PwVkbd *kbd, int step);
int pw_vkbd_backspace(PwVkbd *kbd);
int pw_vkbd_type(PwVkbd *kbd, const char *utf8);   /* appends; PW_VKBD_TYPED, or NONE when it would not fit */
size_t pw_vkbd_utf8(const PwVkbd *kbd, char *out, size_t capacity);
const char *pw_vkbd_page_name(int page);
/* The stick's moves: 1 when (dx, dy) is a move to make now */
int pw_vkbd_stick(PwVkbdStick *stick, float x, float y, uint64_t now_ms, int *dx, int *dy);

#ifdef __cplusplus
}
#endif
#endif
