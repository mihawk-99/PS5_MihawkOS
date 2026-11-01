/* SPDX-License-Identifier: GPL-3.0-or-later */
/* src/pw_home_grid.h: the main menu's layout (Mihawk OS first and largest, Steam, games, the QR code's tall tile, ten
 * games a page after that, the dock), the D-pad's moves (in the page, across pages at the edges, into and along the dock),
 * L1/R1, the pointer's hit test and its speed. */
#include "../src/pw_home_grid.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int find(const PwHomeLayout *l, PwHomeKind kind, int item)
{
    for (size_t i = 0; i < l->count; ++i)
        if (l->tiles[i].kind == kind && l->tiles[i].item == item) return (int)i;
    return -1;
}

static int at(const PwHomeLayout *l, int page, int col, int row)
{
    for (size_t i = 0; i < l->count; ++i) {
        const PwHomeTile *t = &l->tiles[i];
        if (t->page == page && col >= t->col && col < t->col + t->cols && row >= t->row && row < t->row + t->rows) return (int)i;
    }
    return -1;
}

int main(void)
{
    static PwHomeLayout l;
    /* No games: Mihawk OS, Steam, the QR code, the dock; one page */
    pw_home_layout(&l, 0, 1);
    assert(l.count == 3 + PW_HOME_DOCK_ITEMS && l.pages == 1);
    assert(l.tiles[0].kind == PW_HOME_MIHAWK && l.tiles[0].col == 0 && l.tiles[0].cols == 2 && l.tiles[0].rows == 2);
    /* Three games fill page 0 beside Steam; the fourth starts page 1 */
    pw_home_layout(&l, 3, 1);
    assert(l.pages == 1 && at(&l, 0, 2, 0) == find(&l, PW_HOME_STEAM, 0) && at(&l, 0, 3, 0) == find(&l, PW_HOME_GAME, 0) &&
           at(&l, 0, 2, 1) == find(&l, PW_HOME_GAME, 1) && at(&l, 0, 3, 1) == find(&l, PW_HOME_GAME, 2) &&
           at(&l, 0, 4, 0) == find(&l, PW_HOME_WEBUI, 0) && at(&l, 0, 4, 1) == find(&l, PW_HOME_WEBUI, 0));
    pw_home_layout(&l, 4, 1);
    assert(l.pages == 2 && at(&l, 1, 0, 0) == find(&l, PW_HOME_GAME, 3));
    /* Without Steam, four games beside Mihawk OS; 14 games make three pages, ten on page 1 */
    pw_home_layout(&l, 4, 0);
    assert(l.pages == 1 && find(&l, PW_HOME_STEAM, 0) < 0 && at(&l, 0, 2, 0) == find(&l, PW_HOME_GAME, 0));
    pw_home_layout(&l, 14, 1);
    assert(l.pages == 3 && at(&l, 1, 4, 1) == find(&l, PW_HOME_GAME, 12) && at(&l, 2, 0, 0) == find(&l, PW_HOME_GAME, 13));
    /* Every tile of a page lies in the grid, none overlapping; Mihawk OS is the leftmost and the largest */
    for (size_t i = 0; i < l.count; ++i) {
        const PwHomeRect a = pw_home_rect(&l.tiles[i]);
        assert(a.x >= 0 && a.y >= 0 && a.x + a.w <= 1920 && a.y + a.h <= 1080);
        for (size_t j = i + 1; j < l.count; ++j) {
            if (l.tiles[i].page != l.tiles[j].page) continue;
            const PwHomeRect b = pw_home_rect(&l.tiles[j]);
            assert(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
        }
    }
    const PwHomeRect mihawk = pw_home_rect(&l.tiles[0]), steam = pw_home_rect(&l.tiles[find(&l, PW_HOME_STEAM, 0)]);
    assert(mihawk.x == PW_HOME_GRID_X && mihawk.w * mihawk.h > 3 * steam.w * steam.h);

    /* The D-pad on page 0: right from Mihawk OS is Steam, then the game, then the QR code, then page 1 */
    int page = 0, focus = 0;
    focus = pw_home_move(&l, focus, 1, 0, &page);
    assert(focus == find(&l, PW_HOME_STEAM, 0) && page == 0);
    focus = pw_home_move(&l, focus, 1, 0, &page);
    assert(focus == find(&l, PW_HOME_GAME, 0));
    focus = pw_home_move(&l, focus, 1, 0, &page);
    assert(focus == find(&l, PW_HOME_WEBUI, 0));
    focus = pw_home_move(&l, focus, 1, 0, &page);
    assert(page == 1 && l.tiles[focus].page == 1 && l.tiles[focus].col == 0);
    /* left from page 1's first column goes back to page 0's right edge, the QR code */
    focus = pw_home_move(&l, focus, -1, 0, &page);
    assert(page == 0 && focus == find(&l, PW_HOME_WEBUI, 0));
    /* left at Mihawk OS goes nowhere, nor up from the top row */
    page = 0, focus = 0;
    assert(pw_home_move(&l, 0, -1, 0, &page) == 0 && pw_home_move(&l, 0, 0, -1, &page) == 0 && page == 0);
    /* down: Steam to the game under it, then into the dock; along the dock; up back to the page; down in the dock: nothing */
    focus = find(&l, PW_HOME_STEAM, 0);
    focus = pw_home_move(&l, focus, 0, 1, &page);
    assert(focus == find(&l, PW_HOME_GAME, 1));
    focus = pw_home_move(&l, focus, 0, 1, &page);
    assert(l.tiles[focus].kind == PW_HOME_DOCK && l.tiles[focus].item == PW_HOME_FILES);
    assert(pw_home_move(&l, focus, 0, 1, &page) == focus);
    focus = pw_home_move(&l, focus, -1, 0, &page);
    assert(l.tiles[focus].item == PW_HOME_SETTINGS);
    assert(pw_home_move(&l, focus, -1, 0, &page) == focus);   /* the dock's left end */
    focus = pw_home_move(&l, focus, 0, -1, &page);
    assert(l.tiles[focus].kind != PW_HOME_DOCK && l.tiles[focus].page == 0);
    /* Mihawk OS straight down is the dock too: no tile of the page is under it */
    focus = pw_home_move(&l, 0, 0, 1, &page);
    assert(l.tiles[focus].kind == PW_HOME_DOCK);
    /* Every tile of every page is reached from Mihawk OS by D-pad presses (nothing skipped) */
    int seen[PW_HOME_MAX_TILES + 8] = { 0 }, queue[PW_HOME_MAX_TILES + 8], pages[PW_HOME_MAX_TILES + 8], head = 0, tail = 0;
    queue[tail] = 0, pages[tail++] = 0, seen[0] = 1;
    while (head < tail) {
        const int from = queue[head], from_page = pages[head++];
        const int moves[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int m = 0; m < 4; ++m) {
            int p = from_page;
            const int to = pw_home_move(&l, from, moves[m][0], moves[m][1], &p);
            if (!seen[to]) seen[to] = 1, queue[tail] = to, pages[tail++] = p;
        }
    }
    for (size_t i = 0; i < l.count; ++i) assert(seen[i]);

    /* L1/R1: the pages, the first tile of each focused; no page before the first or after the last */
    page = 0, focus = 0;
    page = pw_home_page(&l, page, 1, &focus);
    assert(page == 1 && l.tiles[focus].page == 1 && l.tiles[focus].col == 0 && l.tiles[focus].row == 0);
    page = pw_home_page(&l, page, 1, &focus);
    page = pw_home_page(&l, page, 1, &focus);
    assert(page == 2);
    page = pw_home_page(&l, 0, -1, &focus);
    assert(page == 0);

    /* The pointer: what it is over on the page, the dock's on every page, nothing in a gap */
    const PwHomeRect q = pw_home_rect(&l.tiles[find(&l, PW_HOME_WEBUI, 0)]);
    assert(pw_home_hit(&l, 0, mihawk.x + 10, mihawk.y + 10) == 0);
    assert(pw_home_hit(&l, 0, q.x + q.w / 2, q.y + q.h - 5) == find(&l, PW_HOME_WEBUI, 0));
    assert(pw_home_hit(&l, 0, mihawk.x + mihawk.w + PW_HOME_GAP / 2, mihawk.y + 10) == -1);
    assert(pw_home_hit(&l, 1, mihawk.x + 10, mihawk.y + 10) == at(&l, 1, 0, 0));
    const PwHomeRect files = pw_home_rect(&l.tiles[find(&l, PW_HOME_DOCK, PW_HOME_FILES)]);
    assert(pw_home_hit(&l, 2, files.x + 5, files.y + 5) == find(&l, PW_HOME_DOCK, PW_HOME_FILES));
    /* its speed: a full push crosses the screen in about a second, a small one creeps; it stays on the screen */
    float x = 960, y = 540;
    pw_home_pointer(&x, &y, 1.0f, 0.0f, 0.5f);
    assert(fabsf(x - (960 + PW_HOME_POINTER_SPEED * 0.5f)) < 1.0f && y == 540);
    x = 960;
    pw_home_pointer(&x, &y, 0.2f, 0.0f, 1.0f);
    assert(x > 960 && x - 960 < 0.1f * PW_HOME_POINTER_SPEED);
    pw_home_pointer(&x, &y, -1.0f, -1.0f, 10.0f);
    assert(x == 0 && y == 0);
    pw_home_pointer(&x, &y, 0.0f, 0.0f, 1.0f);
    assert(x == 0 && y == 0);
    printf("home grid: layouts, D-pad moves and pages, the dock, L1/R1, the pointer\n");
    return 0;
}
