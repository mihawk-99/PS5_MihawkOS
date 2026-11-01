/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_home_grid.h"

#include <math.h>

static void add(PwHomeLayout *out, PwHomeKind kind, int item, int page, int col, int row, int cols, int rows)
{
    out->tiles[out->count++] = (PwHomeTile){ kind, item, page, col, row, cols, rows };
}

void pw_home_layout(PwHomeLayout *out, size_t games, int steam)
{
    out->count = 0;
    if (games > PW_HOME_MAX_TILES) games = PW_HOME_MAX_TILES;
    add(out, PW_HOME_MIHAWK, 0, 0, 0, 0, 2, 2);
    if (steam) add(out, PW_HOME_STEAM, 0, 0, 2, 0, 1, 1);
    size_t game = 0;
    for (int slot = steam ? 1 : 0; slot < 4 && game < games; ++slot, ++game)   /* columns 2-3, row by row */
        add(out, PW_HOME_GAME, (int)game, 0, 2 + slot % 2, slot / 2, 1, 1);
    add(out, PW_HOME_WEBUI, 0, 0, 4, 0, 1, 2);
    int page = 1;
    for (int slot = 0; game < games; ++game, ++slot) {
        if (slot == PW_HOME_COLUMNS * PW_HOME_ROWS) slot = 0, ++page;
        add(out, PW_HOME_GAME, (int)game, page, slot % PW_HOME_COLUMNS, slot / PW_HOME_COLUMNS, 1, 1);
    }
    out->pages = game > (size_t)(steam ? 3 : 4) ? page + 1 : 1;
    for (int i = 0; i < PW_HOME_DOCK_ITEMS; ++i) add(out, PW_HOME_DOCK, i, -1, i, 0, 1, 1);
}

PwHomeRect pw_home_rect(const PwHomeTile *tile)
{
    if (tile->kind == PW_HOME_DOCK) {
        const float w = 660.0f / PW_HOME_DOCK_ITEMS;
        return (PwHomeRect){ 960.0f - 330.0f + tile->col * w, 852.0f, w, 132.0f };
    }
    const float cw = (PW_HOME_GRID_W - (PW_HOME_COLUMNS - 1) * PW_HOME_GAP) / PW_HOME_COLUMNS;
    const float ch = (PW_HOME_GRID_H - (PW_HOME_ROWS - 1) * PW_HOME_GAP) / PW_HOME_ROWS;
    return (PwHomeRect){ PW_HOME_GRID_X + tile->col * (cw + PW_HOME_GAP), PW_HOME_GRID_Y + tile->row * (ch + PW_HOME_GAP),
                         tile->cols * cw + (tile->cols - 1) * PW_HOME_GAP, tile->rows * ch + (tile->rows - 1) * PW_HOME_GAP };
}

static float cx(PwHomeRect r) { return r.x + r.w * 0.5f; }
static float cy(PwHomeRect r) { return r.y + r.h * 0.5f; }

/* The nearest tile that lies wholly dx, dy from `from` (on page, or in the dock), preferring one in line (as the setup
 * tour moves its focus); -1 for none. */
static int nearest(const PwHomeLayout *layout, int from, int dx, int dy, int page, int dock)
{
    const PwHomeRect a = pw_home_rect(&layout->tiles[from]);
    int best = -1;
    float best_score = 1e9f;
    for (size_t i = 0; i < layout->count; ++i) {
        const PwHomeTile *t = &layout->tiles[i];
        if ((int)i == from || (dock ? t->kind != PW_HOME_DOCK : t->page != page)) continue;
        const PwHomeRect b = pw_home_rect(t);
        /* wholly past the edge it moves from: a tall tile's neighbours beside it are not above or below it */
        if ((dx > 0 && b.x < a.x + a.w) || (dx < 0 && b.x + b.w > a.x) || (dy > 0 && b.y < a.y + a.h) ||
            (dy < 0 && b.y + b.h > a.y))
            continue;
        const float vx = cx(b) - cx(a), vy = cy(b) - cy(a), along = vx * dx + vy * dy, across = fabsf(vx * dy - vy * dx);
        const float score = along + across * 2.5f;
        if (score < best_score) best_score = score, best = (int)i;
    }
    return best;
}

/* The tile of page nearest height y at its left (enter 1) or right (enter -1) edge. */
static int edge(const PwHomeLayout *layout, int page, float y, int enter)
{
    int best = -1;
    float best_score = 1e9f;
    for (size_t i = 0; i < layout->count; ++i) {
        const PwHomeTile *t = &layout->tiles[i];
        if (t->page != page) continue;
        const PwHomeRect r = pw_home_rect(t);
        const float side = enter > 0 ? r.x : -(r.x + r.w), off = fabsf(cy(r) - y);
        const float inside = y >= r.y && y <= r.y + r.h ? 0.0f : off;
        const float score = side * 4.0f + inside;
        if (score < best_score) best_score = score, best = (int)i;
    }
    return best;
}

int pw_home_move(const PwHomeLayout *layout, int focus, int dx, int dy, int *page)
{
    if (focus < 0 || (size_t)focus >= layout->count || (dx && dy) || (!dx && !dy)) return focus;
    const PwHomeTile *at = &layout->tiles[focus];
    if (at->kind == PW_HOME_DOCK) {
        if (dy > 0) return focus;
        const int to = nearest(layout, focus, dx, dy, *page, dy == 0);
        return to < 0 ? focus : to;
    }
    int to = nearest(layout, focus, dx, dy, *page, 0);
    if (to >= 0) return to;
    if (dy > 0) {
        to = nearest(layout, focus, dx, dy, *page, 1);
        return to < 0 ? focus : to;
    }
    if (dx && *page + dx >= 0 && *page + dx < layout->pages) {
        to = edge(layout, *page + dx, cy(pw_home_rect(at)), dx);
        if (to >= 0) {
            *page += dx;
            return to;
        }
    }
    return focus;
}

int pw_home_page(const PwHomeLayout *layout, int page, int step, int *focus)
{
    const int to = page + step;
    if (to < 0 || to >= layout->pages || !step) return page;
    for (size_t i = 0; i < layout->count; ++i)
        if (layout->tiles[i].page == to) {
            *focus = (int)i;
            break;
        }
    return to;
}

int pw_home_hit(const PwHomeLayout *layout, int page, float x, float y)
{
    for (size_t i = 0; i < layout->count; ++i) {
        const PwHomeTile *t = &layout->tiles[i];
        if (t->page != page && t->kind != PW_HOME_DOCK) continue;
        const PwHomeRect r = pw_home_rect(t);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return (int)i;
    }
    return -1;
}

void pw_home_pointer(float *x, float *y, float stick_x, float stick_y, float dt)
{
    const float push = sqrtf(stick_x * stick_x + stick_y * stick_y);
    if (push > 0.0f) {
        /* a curve: a small push for a fine move, a full one to cross the screen in about a second */
        const float speed = PW_HOME_POINTER_SPEED * powf(fminf(push, 1.0f), 1.7f) / push;
        *x += stick_x * speed * dt;
        *y += stick_y * speed * dt;
    }
    *x = fminf(fmaxf(*x, 0.0f), 1919.0f);
    *y = fminf(fmaxf(*y, 0.0f), 1079.0f);
}
