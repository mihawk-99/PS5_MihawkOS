/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_controls.h"
#include "../src/proton_dualsense.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    pw_controls controls, loaded;
    pw_controls_mouse mouse = {0};
    PwWineInput events[4];
    struct pad pad = {.connected = 1, .touch_count = 1, .touch_id = 7, .touch_x = 100, .touch_y = 100};
    pw_controls_defaults(&controls);
    assert(controls.button[0] == 1 && !controls.button[1] && !controls.button[2]);
    assert(!pw_controls_assign(&controls, 0, 7)); /* R1 */
    assert(!pw_controls_assign(&controls, 1, 6)); /* L1 */
    assert(!pw_controls_assign(&controls, 2, 9)); /* R2 */
    assert(pw_controls_assign(&controls, 3, 1) == EINVAL);
    assert(pw_controls_assign(&controls, 0, PW_CONTROLS_BUTTONS) == EINVAL);
    assert(!pw_controls_update(&controls, &mouse, &pad, events));
    pad.held = PAD_R1 | PAD_L1 | PAD_CROSS;
    pad.touch_x += 10;
    pad.r2 = 0.6f;
    pad.raw_axes[5] = 200;
    assert(pw_controls_update(&controls, &mouse, &pad, events) == 4);
    assert(events[0].type == PW_WINE_INPUT_MOUSE_MOVE && events[0].x == 10);
    for (unsigned i = 0; i < 3; ++i) assert(events[i+1].code == i && events[i+1].down);
    assert(!pw_controls_update(&controls, &mouse, &pad, events));
    struct pad gamepad;
    uint8_t report[64];
    pw_controls_gamepad(&controls, &pad, &gamepad);
    assert(gamepad.held == PAD_CROSS && !gamepad.r2 && !gamepad.raw_axes[5]);
    assert(gamepad.touch_x == pad.touch_x);
    proton_dualsense_report(&gamepad, 0, report);
    assert(!(report[9] & 0x0a)); /* R1 and R2 removed from HID too. */
    pad.held |= PAD_INTERCEPTED;
    assert(pw_controls_update(&controls, &mouse, &pad, events) == 3);
    for (unsigned i = 0; i < 3; ++i) assert(events[i].code == i && !events[i].down);
    pad.held = PAD_R1;
    pad.connected = 0;
    assert(!pw_controls_update(&controls, &mouse, &pad, events));
    /* The shell holding the pad (pad_poll: connected false, intercepted true) is not an unplugged controller for the
     * game; a pad that is gone, or never read, is. */
    struct pad shell = {0}, gone = {0}, present = {.connected = 1};
    shell.intercepted = 1;
    assert(pw_controls_game_connected(&shell) && pw_controls_game_connected(&present) && !pw_controls_game_connected(&gone));
    struct pad idle;
    pw_controls_gamepad(&controls, &shell, &idle);   /* what the game is given from it: no buttons, a connected pad */
    assert(pw_controls_game_connected(&idle) && !idle.held && !idle.pressed);
    assert(pw_controls_update(&controls, &mouse, &shell, events) == 0);   /* the mouse emulation still lets go */
    assert(!pw_controls_assign(&controls, 1, 7));
    assert(!controls.button[0] && controls.button[1] == 7); /* conflict is unassigned */

    /* Defaults: a touchpad click with two fingers on the pad is the right button; with one, the left. */
    pw_controls defaults;
    pw_controls_mouse trackpad = {0};
    pw_controls_defaults(&defaults);
    struct pad touch = {.connected = 1, .touch_count = 2, .touch_id = 4, .touch_x = 300, .touch_y = 300};
    assert(!pw_controls_update(&defaults, &trackpad, &touch, events));
    touch.held = PAD_TOUCH_PAD;
    assert(pw_controls_update(&defaults, &trackpad, &touch, events) == 1 && events[0].code == 1 && events[0].down);
    touch.held = 0;
    assert(pw_controls_update(&defaults, &trackpad, &touch, events) == 1 && events[0].code == 1 && !events[0].down);
    touch.touch_count = 1;
    touch.held = PAD_TOUCH_PAD;
    assert(pw_controls_update(&defaults, &trackpad, &touch, events) == 1 && events[0].code == 0 && events[0].down);

    char folder[] = "/tmp/proton-controls-XXXXXX";
    assert(mkdtemp(folder));
    char path[256];
    snprintf(path, sizeof(path), "%s/controls.txt", folder);
    assert(pw_controls_load(&loaded, path) == ENOENT && loaded.button[0] == 1);
    assert(!pw_controls_save(&controls, path));
    assert(!pw_controls_load(&loaded, path) && !memcmp(&loaded, &controls, sizeof(controls)));
    controls.button[0] = 7; /* invalid duplicate must not damage the last saved file */
    assert(pw_controls_save(&controls, path) == EINVAL);
    assert(!pw_controls_load(&controls, path) && !memcmp(&loaded, &controls, sizeof(controls)));
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("left cross\nright cross\nmiddle none\n", file);
    fclose(file);
    assert(pw_controls_load(&loaded, path) == EINVAL && loaded.button[0] == 1);
    file = fopen(path, "w");
    assert(file);
    fputs("left no-such-button\n", file);
    fclose(file);
    assert(pw_controls_load(&loaded, path) == EINVAL && loaded.button[0] == 1);
    /* The on-screen keyboard's gesture: Off by default, saved only when chosen, and a file from before it still reads. */
    pw_controls_defaults(&controls);
    assert(controls.keyboard == PW_OSK_OFF);
    assert(!pw_controls_save(&controls, path));
    char text[256] = {0};
    file = fopen(path, "r");
    assert(file && fread(text, 1, sizeof(text) - 1, file) > 0);
    fclose(file);
    assert(!strcmp(text, "left touchpad\nright none\nmiddle none\n"));   /* byte for byte what an earlier version wrote */
    assert(!pw_controls_set_keyboard(&controls, PW_OSK_L3_R3) && pw_controls_set_keyboard(&controls, PW_OSK_TRIGGERS) == EINVAL);
    assert(controls.keyboard == PW_OSK_L3_R3);
    assert(!pw_controls_save(&controls, path));
    memset(&loaded, 0, sizeof(loaded));
    assert(!pw_controls_load(&loaded, path) && loaded.keyboard == PW_OSK_L3_R3 && !memcmp(&loaded, &controls, sizeof(controls)));
    for (unsigned t = 0; t < PW_OSK_TRIGGERS; t++) {
        pw_controls_set_keyboard(&controls, t);
        assert(!pw_controls_save(&controls, path) && !pw_controls_load(&loaded, path) && loaded.keyboard == t);
    }
    controls.keyboard = PW_OSK_TRIGGERS;
    assert(pw_controls_save(&controls, path) == EINVAL);
    file = fopen(path, "w");
    assert(file);
    fputs("left touchpad\nright none\nmiddle none\n", file);   /* a file from before the keyboard */
    fclose(file);
    loaded.keyboard = PW_OSK_L3_R3;
    assert(!pw_controls_load(&loaded, path) && loaded.keyboard == PW_OSK_OFF);
    const char *bad[] = {"left touchpad\nright none\nmiddle none\nkeyboard nonsense\n",
                         "left touchpad\nright none\nmiddle none\nkeyboard\n",
                         "left touchpad\nright none\nmiddle none\nmouse l3-r3\n",
                         "left touchpad\nright none\nmiddle none\nkeyboard l3-r3\nextra\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        file = fopen(path, "w");
        assert(file);
        fputs(bad[i], file);
        fclose(file);
        loaded.keyboard = PW_OSK_OPTIONS_TOUCHPAD;
        assert(pw_controls_load(&loaded, path) == EINVAL && loaded.keyboard == PW_OSK_OFF && loaded.button[0] == 1);
    }
    unlink(path);
    rmdir(folder);
    puts("controls: persistence, conflicts, motion, three clicks, HID consumption and disconnect PASS");
}
