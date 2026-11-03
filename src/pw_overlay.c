/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_overlay.h"
#include "pw_memory_guard.h"
#include <math.h>
#include <stdio.h>

void pw_overlay_open(PwOverlay *overlay)
{
    overlay->focus = PW_OVERLAY_EXIT;
    overlay->armed_ms = 0;
}

PwOverlayTile pw_overlay_tile(int index)
{
    /* centred on the screen's middle row, as kabuki's rail */
    const float cy = 540 + (index - (PW_OVERLAY_TILES - 1) / 2.0f) * PW_OVERLAY_RAIL_STEP;
    const PwOverlayTile tile = {PW_OVERLAY_RAIL_X - PW_OVERLAY_DISC, cy - PW_OVERLAY_DISC, 2 * PW_OVERLAY_DISC, 2 * PW_OVERLAY_DISC};
    return tile;
}

void pw_overlay_move(PwOverlay *overlay, int dy)
{
    overlay->focus = ((overlay->focus + dy) % PW_OVERLAY_TILES + PW_OVERLAY_TILES) % PW_OVERLAY_TILES;
}

int pw_overlay_press(PwOverlay *overlay, uint64_t now_ms)
{
    const int armed = overlay->armed_ms && now_ms - overlay->armed_ms < PW_OVERLAY_EXIT_MS;
    overlay->armed_ms = 0;
    switch (overlay->focus) {
    case PW_OVERLAY_EXIT:
        if (armed) return PW_OVERLAY_DO_EXIT;
        overlay->armed_ms = now_ms ? now_ms : 1;
        return PW_OVERLAY_ARMED;
    case PW_OVERLAY_STATS: overlay->stats = !overlay->stats; return PW_OVERLAY_TOGGLED;
    case PW_OVERLAY_MOUSE: overlay->mouse_off = !overlay->mouse_off; return PW_OVERLAY_TOGGLED;
    case PW_OVERLAY_KEYBOARD: return PW_OVERLAY_DO_KEYBOARD;
    case PW_OVERLAY_CONTROLLER: overlay->controller = (overlay->controller + 1) % 3; return PW_OVERLAY_DO_CONTROLLER;
    }
    return PW_OVERLAY_NONE;
}

int pw_overlay_touch(PwOverlay *overlay, float x, float y, uint64_t now_ms)
{
    for (int i = 0; i < PW_OVERLAY_TILES; ++i) {
        const PwOverlayTile t = pw_overlay_tile(i);
        if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) {
            overlay->focus = i;
            return pw_overlay_press(overlay, now_ms);
        }
    }
    return PW_OVERLAY_NONE;
}

int pw_overlay_expire(PwOverlay *overlay, uint64_t now_ms)
{
    if (!overlay->armed_ms || now_ms - overlay->armed_ms < PW_OVERLAY_EXIT_MS) return 0;
    overlay->armed_ms = 0;
    return 1;
}

void pw_hud_frames(PwHud *hud, float fps, float p99_ms)
{
    hud->fps = fps;
    hud->low_fps = p99_ms > 0 ? 1000.0f / p99_ms : 0;
    hud->frame_ms[hud->graph_next] = fps > 0 ? 1000.0f / fps : 0;
    hud->graph_next = (hud->graph_next + 1) % PW_HUD_GRAPH;
    if (hud->graph_count < PW_HUD_GRAPH) ++hud->graph_count;
}

int pw_hud_pool_level(int free_count, int total)
{
    if (total <= 0) return PW_HUD_FINE;
    if (free_count <= (int)PW_PAGE_TABLE_CLOSE_FREE) return PW_HUD_RED;     /* the guard asks to close here, ends at 512 */
    if (free_count <= (int)PW_PAGE_TABLE_STEEP_FREE) return PW_HUD_AMBER;   /* the guard watches the fall from here */
    return PW_HUD_FINE;
}

size_t pw_hud_line(const PwHud *hud, int line, char *out, size_t capacity)
{
    int n = 0;
    switch (line) {
    case 0:
        n = hud->low_fps > 0 ? snprintf(out, capacity, "FPS %.0f   1%% low %.0f", hud->fps, hud->low_fps)
                             : snprintf(out, capacity, "FPS %.0f", hud->fps);
        break;
    case 1: n = snprintf(out, capacity, "Frame %.1f ms", hud->fps > 0 ? 1000.0f / hud->fps : 0.0f); break;
    case 2: {
        char cpu[16] = "-", gpu[16] = "-";
        if (hud->cpu >= 0) snprintf(cpu, sizeof(cpu), "%.0f%%", hud->cpu);
        if (hud->gpu >= 0) snprintf(gpu, sizeof(gpu), "%.0f%%", hud->gpu);
        n = snprintf(out, capacity, "CPU %s   GPU %s", cpu, gpu);
        break;
    }
    case 3:
        n = hud->memory_total ? snprintf(out, capacity, "RAM %.1f / %.1f GB", hud->memory_used / 1073741824.0,
                                         hud->memory_total / 1073741824.0)
                              : snprintf(out, capacity, "RAM -");
        break;
    case 4:
    case 5: {
        const int total = line == 4 ? hud->small_total : hud->big_total, free_count = line == 4 ? hud->small_free : hud->big_free;
        const char *name = line == 4 ? "PT small" : "PT big";
        n = total > 0 ? snprintf(out, capacity, "%s %d / %d", name, total - free_count, total)
                      : snprintf(out, capacity, "%s -", name);
        break;
    }
    default: if (capacity) out[0] = 0; return 0;
    }
    return n < 0 ? 0 : (size_t)n < capacity ? (size_t)n : capacity ? capacity - 1 : 0;
}

float pw_hud_graph_top(const PwHud *hud)
{
    float top = 0;
    for (unsigned i = 0; i < hud->graph_count; ++i) top = fmaxf(top, hud->frame_ms[i]);
    const float step = 1000.0f / 60;
    top = ceilf(top / step - 0.001f) * step;
    return top < 2 * step ? 2 * step : top;
}
