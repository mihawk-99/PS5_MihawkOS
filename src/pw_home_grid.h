/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_HOME_GRID_INCLUDED
#define PW_HOME_GRID_INCLUDED
/* The launcher's main menu (Alpha 7, 2026-10-31), a Frutiger Aero tile grid: tiles in a grid of
 * PW_HOME_COLUMNS x PW_HOME_ROWS on pages, a dock of system tiles under it, and a pointer the left stick moves. This lays
 * them out and moves the focus; examples/ps5proton/home_grid.hpp draws them. Geometry is in the launcher's 1920x1080
 * space. Host-tested (tests/test_pw_home_grid.c).
 *
 * Page 0: Mihawk OS in columns 0-1, both rows (always the first and largest tile); Steam (when shown) at column 2, row 0;
 * games in the rest of columns 2-3; the Web UI's QR code in column 4, both rows (the owner: "QR Code should be big, users
 * must be able to find it easily"). Later pages: ten games each, row by row. The dock (Settings, Files, Payloads) is under
 * every page. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_HOME_COLUMNS 5
#define PW_HOME_ROWS 2
#define PW_HOME_GRID_X 150.0f
#define PW_HOME_GRID_Y 178.0f
#define PW_HOME_GRID_W 1620.0f
#define PW_HOME_GRID_H 580.0f
#define PW_HOME_GAP 26.0f
#define PW_HOME_DOCK_ITEMS 3          /* Settings, Files, Payloads */
#define PW_HOME_MAX_TILES 1024        /* the games past this are not shown (a library is far smaller) */
#define PW_HOME_POINTER_SPEED 1500.0f /* px/s with the stick pushed all the way */

typedef enum { PW_HOME_MIHAWK, PW_HOME_STEAM, PW_HOME_GAME, PW_HOME_WEBUI, PW_HOME_DOCK } PwHomeKind;
enum { PW_HOME_SETTINGS, PW_HOME_FILES, PW_HOME_PAYLOADS };   /* the dock's items, left to right */

typedef struct PwHomeRect { float x, y, w, h; } PwHomeRect;

typedef struct PwHomeTile {
    PwHomeKind kind;
    int item;               /* a game: its index in the order given; a dock tile: PW_HOME_SETTINGS... ; else 0 */
    int page;               /* -1: the dock, on every page */
    int col, row, cols, rows;
} PwHomeTile;

typedef struct PwHomeLayout {
    PwHomeTile tiles[PW_HOME_MAX_TILES + PW_HOME_DOCK_ITEMS + 3];
    size_t count;           /* the dock's tiles are the last PW_HOME_DOCK_ITEMS */
    int pages;
} PwHomeLayout;

/* games: how many, in the order they are shown; steam: 1 to show Steam's tile. */
void pw_home_layout(PwHomeLayout *out, size_t games, int steam);
/* Where a tile is, at rest (the focused one grows when drawn). */
PwHomeRect pw_home_rect(const PwHomeTile *tile);
/* The focus after a D-pad press (dx, dy: -1, 0 or 1, one of them 0), *page following it:
 * - in the grid, the nearest tile that way on the page, preferring one in line; past the grid's right or left edge, the
 *   next or previous page's tile nearest the same height; down from the bottom row, the dock's nearest item;
 * - in the dock, left and right move along it, up goes to the page's nearest tile, down does nothing.
 * Returns focus unchanged when nothing lies that way. */
int pw_home_move(const PwHomeLayout *layout, int focus, int dx, int dy, int *page);
/* L1 (step -1) or R1 (+1): the page before or after, if there is one, its first tile focused; returns the page. */
int pw_home_page(const PwHomeLayout *layout, int page, int step, int *focus);
/* The tile under the pointer on page, the dock's included; -1 for none. */
int pw_home_hit(const PwHomeLayout *layout, int page, float x, float y);
/* The pointer after a frame of dt seconds: the left stick (-1..1 each way, its deadzone already taken out) moves it, up to
 * PW_HOME_POINTER_SPEED px/s and slower for a small push, so it can stop on a tile; kept on the screen. */
void pw_home_pointer(float *x, float *y, float stick_x, float stick_y, float dt);

#ifdef __cplusplus
}
#endif
#endif
