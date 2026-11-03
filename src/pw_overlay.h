/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_OVERLAY_H
#define PW_OVERLAY_H
/* MihawkOS's control overlay over a game (Alpha 8; the owner, 2026-11-02): the gesture of Settings > Controls opens it as
 * a Frosted Meadow panel down the right of the screen, laid out as kabuki-ps5's (stupiditytries/kabuki-ps5 187180b, ideas
 * only): a rail of round buttons at the edge, the focused one's card beside it, bubbles rising in the panel's margins and
 * the game live to its left. Its buttons: Exit game (two presses), Statistics (the HUD below, on until turned off), the on-screen keyboard,
 * and the Mouse (off: as if unplugged, no mouse input reaches the game and its pointer is not drawn). Here: the tiles'
 * geometry for drawing and for the touchpad, the focus, what a press does, and the HUD's figures and lines. No console
 * call; host test: tests/test_pw_overlay.c. Drawn by src/pw_steam_screen.c (PW_STEAM_SCREEN_OVERLAY, pw_steam_screen_hud). */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PW_OVERLAY_EXIT, PW_OVERLAY_STATS, PW_OVERLAY_KEYBOARD, PW_OVERLAY_MOUSE, PW_OVERLAY_CONTROLLER, PW_OVERLAY_TILES };
/* What a press did */
enum { PW_OVERLAY_NONE, PW_OVERLAY_ARMED, PW_OVERLAY_DO_EXIT, PW_OVERLAY_TOGGLED, PW_OVERLAY_DO_KEYBOARD, PW_OVERLAY_DO_CONTROLLER };
/* Exit's second press counts within this long of the first */
#define PW_OVERLAY_EXIT_MS 3000u
/* The panel: the broker's rectangle (corners on multiples of 8), its left 165 px and the rail's strip the bubbles' margins */
#define PW_OVERLAY_PANEL_X 1032
#define PW_OVERLAY_PANEL_W 888
/* The rail's round buttons, top to bottom, and the card beside them (kabuki's proportions) */
#define PW_OVERLAY_RAIL_X 1818.0f
#define PW_OVERLAY_RAIL_STEP 130.0f
#define PW_OVERLAY_DISC 40.0f
#define PW_OVERLAY_CARD_X 1200.0f
#define PW_OVERLAY_CARD_Y 135.0f
#define PW_OVERLAY_CARD_W 516.0f
#define PW_OVERLAY_CARD_H 810.0f

typedef struct PwOverlayTile { float x, y, w, h; } PwOverlayTile;


/* ---- The statistics HUD, MangoHUD-like, top left: a see-through glass card (the broker's second rectangle, no bubbles).
 * Figures come from counters MihawkOS already keeps (native/proton_wine.cpp samples them twice a second). ---- */
#define PW_HUD_X 16
#define PW_HUD_Y 16
#define PW_HUD_W 384
#define PW_HUD_H 240
enum { PW_HUD_GRAPH = 60, PW_HUD_LINE = 64 };
/* A page-table pool's state: fine, near where the guard asks the game to close (amber), near where it ends it (red) */
enum { PW_HUD_FINE, PW_HUD_AMBER, PW_HUD_RED };

typedef struct PwHud {
    float fps, low_fps;               /* low_fps: 1000 / the 99th percentile frame time; 0: not known */
    float frame_ms[PW_HUD_GRAPH];     /* the frame time at each sample, oldest first once full */
    unsigned graph_count, graph_next;
    float cpu, gpu;                   /* percent; below 0: not known */
    unsigned long long memory_used, memory_total;   /* bytes; total 0: not known */
    int small_free, small_total;      /* the system pool's page tables (helpers and Steam); total 0: not known */
    int big_free, big_total;          /* the title's own (a game in the title) */
} PwHud;

typedef struct PwOverlay {
    int focus;
    int stats;             /* the HUD shows */
    int mouse_off;         /* the mouse is "unplugged" */
    uint64_t armed_ms;     /* Exit's first press, 0: not armed */
    int controller;        /* the controller kind the game sees (src/pw_controller.h), cycled by the Controller button */
    int pad_next_launch;   /* a DualSense chosen here reaches the game only from its next launch (a helper's game) */
    PwHud figures;         /* the Statistics card's, sampled while it is focused */
} PwOverlay;

/* Opens on the first button, the toggles kept as they are */
void pw_overlay_open(PwOverlay *overlay);
/* A rail button's square around its disc */
PwOverlayTile pw_overlay_tile(int index);
/* Up and down wrap */
void pw_overlay_move(PwOverlay *overlay, int dy);
/* CROSS on the focused button, or a touch at (x, y) on 1920x1080 (NONE where no button is); a press elsewhere than Exit,
 * or Exit's second press after PW_OVERLAY_EXIT_MS, disarms it. Controller: the next kind, Xbox 360, Xbox One, DualSense
 * and round again (DO_CONTROLLER: the title switches the game's pad). */
int pw_overlay_press(PwOverlay *overlay, uint64_t now_ms);
int pw_overlay_touch(PwOverlay *overlay, float x, float y, uint64_t now_ms);
/* 1 when Exit's arming ran out just now (the panel is drawn again) */
int pw_overlay_expire(PwOverlay *overlay, uint64_t now_ms);

/* A sample: fps and the 99th percentile frame time in ms (0: not known); frame time goes into the graph */
void pw_hud_frames(PwHud *hud, float fps, float p99_ms);
/* A pool's state from its free count, with the guard's marks (src/pw_memory_guard.h) */
int pw_hud_pool_level(int free_count, int total);
/* The HUD's lines, top to bottom: 0 FPS, 1 frame time, 2 CPU and GPU, 3 memory, 4 the small pool, 5 the big pool; the
 * length written, 0 past the last */
enum { PW_HUD_LINES = 6 };
size_t pw_hud_line(const PwHud *hud, int line, char *out, size_t capacity);
/* The graph's tallest bar, ms: 33.3 or more, rounded up to a multiple of 16.7 */
float pw_hud_graph_top(const PwHud *hud);

#ifdef __cplusplus
}
#endif
#endif
