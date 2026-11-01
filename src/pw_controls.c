/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_controls.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const struct { const char *id, *label; unsigned mask; } buttons[] = {
    {"none", "Unassigned", 0}, {"touchpad", "Touchpad", PAD_TOUCH_PAD},
    {"cross", "Cross", PAD_CROSS}, {"circle", "Circle", PAD_CIRCLE},
    {"square", "Square", PAD_SQUARE}, {"triangle", "Triangle", PAD_TRIANGLE},
    {"l1", "L1", PAD_L1}, {"r1", "R1", PAD_R1},
    {"l2", "L2", 0}, {"r2", "R2", 0},
    {"l3", "L3", PAD_L3}, {"r3", "R3", PAD_R3},
    {"up", "D-pad Up", PAD_UP}, {"down", "D-pad Down", PAD_DOWN},
    {"left", "D-pad Left", PAD_LEFT}, {"right", "D-pad Right", PAD_RIGHT},
    {"options", "Options", PAD_OPTIONS}
};
/* Keep the UI and parser on exactly the same table. */
_Static_assert(sizeof(buttons) / sizeof(buttons[0]) == PW_CONTROLS_BUTTONS, "button count");
static const char *actions[] = {"left", "right", "middle"};

const char *pw_controls_button_id(unsigned index)
{ return index < PW_CONTROLS_BUTTONS ? buttons[index].id : NULL; }
const char *pw_controls_button_label(unsigned index)
{ return index < PW_CONTROLS_BUTTONS ? buttons[index].label : NULL; }
void pw_controls_defaults(pw_controls *controls)
{ *controls = (pw_controls){{1, 0, 0}, PW_OSK_OFF}; }

int pw_controls_assign(pw_controls *controls, unsigned mouse, unsigned button)
{
    if (mouse >= 3 || button >= PW_CONTROLS_BUTTONS) return EINVAL;
    for (unsigned i = 0; i < 3; ++i)
        if (button && controls->button[i] == button) controls->button[i] = 0;
    controls->button[mouse] = button;
    return 0;
}

int pw_controls_set_keyboard(pw_controls *controls, unsigned trigger)
{
    if (trigger >= PW_OSK_TRIGGERS) return EINVAL;
    controls->keyboard = trigger;
    return 0;
}

int pw_controls_load(pw_controls *controls, const char *path)
{
    pw_controls parsed = {{0}, PW_OSK_OFF};
    char action[32], id[32];
    pw_controls_defaults(controls);
    FILE *file = fopen(path, "r");
    if (!file) return errno;
    int error = 0;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned index;
        if (fscanf(file, "%31s %31s", action, id) != 2 || strcmp(action, actions[i])) { error = EINVAL; break; }
        for (index = 0; index < PW_CONTROLS_BUTTONS; ++index)
            if (!strcmp(id, buttons[index].id)) break;
        if (index == PW_CONTROLS_BUTTONS) { error = EINVAL; break; }
        for (unsigned j = 0; j < i; ++j)
            if (index && parsed.button[j] == index) error = EINVAL;
        parsed.button[i] = index;
    }
    /* an optional fourth line, "keyboard <trigger>": files from before the on-screen keyboard end after three */
    if (!error) {
        const int words = fscanf(file, "%31s %31s", action, id);
        const int trigger = words == 2 && !strcmp(action, "keyboard") ? pw_osk_trigger_parse(id) : -1;
        if (words == 1 || (words == 2 && trigger < 0)) error = EINVAL;
        else if (words == 2) parsed.keyboard = (unsigned)trigger;
    }
    if (!error && fscanf(file, "%31s", id) == 1) error = EINVAL;
    if (ferror(file)) error = EIO;
    fclose(file);
    if (!error) *controls = parsed;
    return error;
}

int pw_controls_save(const pw_controls *controls, const char *path)
{
    char temporary[1024];
    if (controls->keyboard >= PW_OSK_TRIGGERS) return EINVAL;
    for (unsigned i = 0; i < 3; ++i) {
        if (controls->button[i] >= PW_CONTROLS_BUTTONS) return EINVAL;
        for (unsigned j = 0; j < i; ++j)
            if (controls->button[i] && controls->button[i] == controls->button[j]) return EINVAL;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return ENAMETOOLONG;
    FILE *file = fopen(temporary, "w");
    if (!file) return errno;
    int error = 0;
    for (unsigned i = 0; i < 3; ++i)
        if (fprintf(file, "%s %s\n", actions[i], buttons[controls->button[i]].id) < 0) error = EIO;
    /* only a chosen gesture is written: a file with the keyboard Off stays what an earlier version wrote and reads */
    if (controls->keyboard && fprintf(file, "keyboard %s\n", pw_osk_trigger_id(controls->keyboard)) < 0) error = EIO;
    if (fflush(file) || fsync(fileno(file))) error = errno;
    if (fclose(file)) error = errno;
    if (!error && rename(temporary, path)) error = errno;
    if (error) unlink(temporary);
    return error;
}

static int down(unsigned index, const struct pad *pad)
{
    if (!pad->connected || (pad->held & PAD_INTERCEPTED) || !index || index >= PW_CONTROLS_BUTTONS) return 0;
    if (index == 8) return pad->l2 >= 0.5f;
    if (index == 9) return pad->r2 >= 0.5f;
    return !!(pad->held & buttons[index].mask);
}

unsigned pw_controls_update(const pw_controls *controls, pw_controls_mouse *state,
                            const struct pad *pad, PwWineInput events[4])
{
    int connected = pad->connected && !(pad->held & PAD_INTERCEPTED);
    /* Two fingers and a click: the right button, when left is the touchpad click (src/proton_touchpad.h). */
    unsigned count = proton_touchpad_update(&state->pointer, connected, pad->touch_count != 0,
        pad->touch_id, pad->touch_x, pad->touch_y, down(controls->button[0], pad),
        controls->button[0] == 1 && pad->touch_count >= 2, events);
    int *previous[] = {&state->right, &state->middle};
    for (unsigned i = 1; i < 3; ++i) {
        int clicked = down(controls->button[i], pad);
        if (clicked != *previous[i-1])
            events[count++] = (PwWineInput){PW_WINE_INPUT_MOUSE_BUTTON, i, 0, 0, (unsigned)clicked};
        *previous[i-1] = clicked;
    }
    return count;
}

int pw_controls_game_connected(const struct pad *pad)
{
    return pad->connected || pad->intercepted;
}

void pw_controls_gamepad(const pw_controls *controls, const struct pad *input, struct pad *output)
{
    *output = *input;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned index = controls->button[i];
        if (index >= PW_CONTROLS_BUTTONS) continue;
        output->held &= ~buttons[index].mask;
        output->pressed &= ~buttons[index].mask;
        if (index == 8) { output->l2 = 0; output->raw_axes[4] = 0; }
        if (index == 9) { output->r2 = 0; output->raw_axes[5] = 0; }
    }
}
