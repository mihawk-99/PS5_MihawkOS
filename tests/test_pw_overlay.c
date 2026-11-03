/* SPDX-License-Identifier: GPL-3.0-or-later */
/* MihawkOS's control overlay (src/pw_overlay.h): its tiles, focus, Exit's two presses, the toggles, the touchpad, and the
 * statistics HUD's lines and pool colours. */
#include "../src/pw_overlay.h"
#include "../src/pw_vkbd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    PwOverlay o = {0};
    pw_overlay_open(&o);
    assert(o.focus == PW_OVERLAY_EXIT && !o.armed_ms);
    /* the rail: its buttons right of the card, inside the panel, top to bottom with room for a label under each, centred on
     * the screen's middle row; the card clear of the panel's left margin (the bubbles') */
    assert(PW_OVERLAY_PANEL_X % 8 == 0 && PW_OVERLAY_PANEL_W % 8 == 0 && PW_OVERLAY_PANEL_X + PW_OVERLAY_PANEL_W == 1920);
    assert(PW_OVERLAY_CARD_X >= PW_OVERLAY_PANEL_X + 165 && PW_OVERLAY_CARD_Y + PW_OVERLAY_CARD_H <= 1000);
    for (int i = 0; i < PW_OVERLAY_TILES; ++i) {
        const PwOverlayTile t = pw_overlay_tile(i);
        assert(t.x >= PW_OVERLAY_CARD_X + PW_OVERLAY_CARD_W + 20 && t.x + t.w <= 1920 - 20);
        assert(t.y >= PW_OVERLAY_CARD_Y && t.y + t.h + 40 <= PW_OVERLAY_CARD_Y + PW_OVERLAY_CARD_H);
        if (i) assert(pw_overlay_tile(i - 1).y + pw_overlay_tile(i - 1).h + 40 < t.y);
        assert(t.y == (float)(int)t.y);
    }
    const PwOverlayTile first = pw_overlay_tile(0), last = pw_overlay_tile(PW_OVERLAY_TILES - 1);
    assert(first.y + last.y + last.h == 1080);
    /* up and down wrap */
    pw_overlay_move(&o, -1);
    assert(o.focus == PW_OVERLAY_TILES - 1);
    pw_overlay_move(&o, 1);
    assert(o.focus == PW_OVERLAY_EXIT);
    /* Exit: the first press arms, the second within 3 s exits; later than that it arms again */
    assert(pw_overlay_press(&o, 1000) == PW_OVERLAY_ARMED && o.armed_ms == 1000);
    assert(!pw_overlay_expire(&o, 3999));
    assert(pw_overlay_press(&o, 3999) == PW_OVERLAY_DO_EXIT);
    assert(pw_overlay_press(&o, 5000) == PW_OVERLAY_ARMED);
    assert(pw_overlay_press(&o, 8000) == PW_OVERLAY_ARMED);
    assert(pw_overlay_expire(&o, 11000) && !o.armed_ms && !pw_overlay_expire(&o, 11001));
    /* a press at time 0 still arms */
    assert(pw_overlay_press(&o, 0) == PW_OVERLAY_ARMED && o.armed_ms);
    /* another tile disarms Exit; the toggles flip and stay through a reopen */
    pw_overlay_move(&o, 1);
    assert(o.focus == PW_OVERLAY_STATS && pw_overlay_press(&o, 1) == PW_OVERLAY_TOGGLED && o.stats && !o.armed_ms);
    pw_overlay_move(&o, 2);
    assert(o.focus == PW_OVERLAY_MOUSE && pw_overlay_press(&o, 2) == PW_OVERLAY_TOGGLED && o.mouse_off);
    pw_overlay_open(&o);
    assert(o.stats && o.mouse_off && o.focus == PW_OVERLAY_EXIT);
    pw_overlay_move(&o, 2);
    assert(pw_overlay_press(&o, 3) == PW_OVERLAY_DO_KEYBOARD);
    /* Controller cycles Xbox 360, Xbox One, DualSense and round */
    pw_overlay_move(&o, 2);
    assert(o.focus == PW_OVERLAY_CONTROLLER && !o.controller);
    assert(pw_overlay_press(&o, 4) == PW_OVERLAY_DO_CONTROLLER && o.controller == 1);
    assert(pw_overlay_press(&o, 4) == PW_OVERLAY_DO_CONTROLLER && o.controller == 2);
    assert(pw_overlay_press(&o, 4) == PW_OVERLAY_DO_CONTROLLER && o.controller == 0);
    /* a touch presses the tile under it, nothing elsewhere */
    const PwOverlayTile mouse = pw_overlay_tile(PW_OVERLAY_MOUSE);
    assert(pw_overlay_touch(&o, mouse.x + 5, mouse.y + 5, 4) == PW_OVERLAY_TOGGLED && !o.mouse_off && o.focus == PW_OVERLAY_MOUSE);
    assert(pw_overlay_touch(&o, 10, 10, 5) == PW_OVERLAY_NONE && o.focus == PW_OVERLAY_MOUSE);
    assert(pw_overlay_touch(&o, mouse.x - 1, mouse.y + 5, 6) == PW_OVERLAY_NONE);

    /* the HUD */
    PwHud hud = {0};
    char line[PW_HUD_LINE];
    hud.cpu = hud.gpu = -1;
    pw_hud_frames(&hud, 60, 20);
    assert(hud.graph_count == 1 && hud.frame_ms[0] > 16.6f && hud.frame_ms[0] < 16.7f);
    assert(pw_hud_line(&hud, 0, line, sizeof(line)) && !strcmp(line, "FPS 60   1% low 50"));
    assert(pw_hud_line(&hud, 1, line, sizeof(line)) && !strcmp(line, "Frame 16.7 ms"));
    assert(pw_hud_line(&hud, 2, line, sizeof(line)) && !strcmp(line, "CPU -   GPU -"));
    assert(pw_hud_line(&hud, 3, line, sizeof(line)) && !strcmp(line, "RAM -"));
    assert(pw_hud_line(&hud, 4, line, sizeof(line)) && !strcmp(line, "PT small -"));
    hud.cpu = 42.4f, hud.gpu = 71.6f;
    hud.memory_used = 3ull << 30, hud.memory_total = 12ull << 30;
    hud.small_free = 1000, hud.small_total = 8192, hud.big_free = 16000, hud.big_total = 16384;
    assert(pw_hud_line(&hud, 2, line, sizeof(line)) && !strcmp(line, "CPU 42%   GPU 72%"));
    assert(pw_hud_line(&hud, 3, line, sizeof(line)) && !strcmp(line, "RAM 3.0 / 12.0 GB"));
    assert(pw_hud_line(&hud, 4, line, sizeof(line)) && !strcmp(line, "PT small 7192 / 8192"));
    assert(pw_hud_line(&hud, 5, line, sizeof(line)) && !strcmp(line, "PT big 384 / 16384"));
    assert(!pw_hud_line(&hud, PW_HUD_LINES, line, sizeof(line)) && !line[0]);
    /* a short buffer is cut, never overrun */
    char tiny[6];
    assert(pw_hud_line(&hud, 4, tiny, sizeof(tiny)) == 5 && !strcmp(tiny, "PT sm"));
    /* no frame known: no low, no frame time */
    pw_hud_frames(&hud, 0, 0);
    assert(pw_hud_line(&hud, 0, line, sizeof(line)) && !strcmp(line, "FPS 0"));
    /* the pools' colours: amber from where the guard watches the fall (1536 free), red from where it asks to close (768) */
    assert(pw_hud_pool_level(4000, 8192) == PW_HUD_FINE);
    assert(pw_hud_pool_level(1537, 8192) == PW_HUD_FINE && pw_hud_pool_level(1536, 8192) == PW_HUD_AMBER);
    assert(pw_hud_pool_level(769, 8192) == PW_HUD_AMBER && pw_hud_pool_level(768, 8192) == PW_HUD_RED);
    assert(pw_hud_pool_level(0, 0) == PW_HUD_FINE);
    /* the graph: a ring of the last 60 samples, its top at least 33.3 ms and a whole number of 60 Hz frames */
    PwHud ring = {0};
    for (int i = 0; i < PW_HUD_GRAPH + 5; ++i) pw_hud_frames(&ring, i == 63 ? 20 : 60, 0);
    assert(ring.graph_count == PW_HUD_GRAPH && ring.graph_next == 5);
    assert(ring.frame_ms[3] == 50.0f);
    const float top = pw_hud_graph_top(&ring);
    assert(top > 49.9f && top < 50.1f);
    PwHud calm = {0};
    pw_hud_frames(&calm, 120, 0);
    assert(pw_hud_graph_top(&calm) > 33.3f && pw_hud_graph_top(&calm) < 33.4f);
    puts("pw_overlay: ok");
    return 0;
}
