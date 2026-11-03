/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A USB keyboard and mouse as Windows input (src/pw_usb_input.h): the record layouts, keys with their scan codes,
 * press and release by difference, and the mouse's motion, wheels and five buttons. */
#include "../src/pw_usb_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void keyboard_state(uint8_t state[PW_USB_KEYBOARD_STATE], int connected, uint32_t modifiers,
                           const uint16_t *usages, int32_t count)
{
    memset(state, 0, PW_USB_KEYBOARD_STATE);
    state[16] = (uint8_t)connected;
    memcpy(state + 20, &count, 4);
    memcpy(state + 28, &modifiers, 4);
    memcpy(state + 32, usages, 2 * (size_t)(count > 0 ? count : 0));
}

static void mouse_record(uint8_t *r, uint64_t time, uint32_t buttons, int32_t x, int32_t y, int32_t wheel,
                         int32_t tilt)
{
    memset(r, 0, PW_USB_MOUSE_RECORD);
    memcpy(r, &time, 8);
    r[8] = 1;
    memcpy(r + 12, &buttons, 4);
    memcpy(r + 16, &x, 4);
    memcpy(r + 20, &y, 4);
    memcpy(r + 24, &wheel, 4);
    memcpy(r + 28, &tilt, 4);
}

int main(void)
{
    /* Codes: virtual key | scan << 8 | extended << 16. */
    assert(pw_usb_key_code(0x04) == ('A' | 0x1e << 8));
    assert(pw_usb_key_code(0x1a) == ('W' | 0x11 << 8));
    assert(pw_usb_key_code(0x29) == (0x1b | 0x01 << 8));                       /* Escape */
    /* Backspace: VK_BACK with its scan code, pressed and released like any key (a tester, 2026-11-02, whose backspace did
     * nothing: it was the on-screen keyboard's, src/pw_vkbd.h; a USB keyboard's is this, and Wine makes it '\b') */
    assert(pw_usb_key_code(0x2a) == (0x08 | 0x0e << 8));
    {
        PwUsbKeyboard up = {0}, back = {.keys = {0x2a}};
        PwWineInput out[PW_USB_KEYBOARD_EVENTS];
        assert(pw_usb_keyboard_events(&up, &back, out, PW_USB_KEYBOARD_EVENTS) == 1 && out[0].type == PW_WINE_INPUT_KEY &&
               out[0].code == (0x08 | 0x0e << 8) && out[0].down == 1);
        assert(pw_usb_keyboard_events(&back, &up, out, PW_USB_KEYBOARD_EVENTS) == 1 && out[0].code == (0x08 | 0x0e << 8) &&
               out[0].down == 0);
    }
    assert(pw_usb_key_code(0x4f) == (0x27 | 0x4d << 8 | PW_WINE_KEY_EXTENDED)); /* Right arrow */
    assert(pw_usb_key_code(0x58) == (0x0d | 0x1c << 8 | PW_WINE_KEY_EXTENDED)); /* keypad Enter */
    assert(pw_usb_key_code(0x28) == (0x0d | 0x1c << 8));                       /* Enter */
    assert(pw_usb_key_code(0x45) == (0x7b | 0x58 << 8));                       /* F12 */
    assert(!pw_usb_key_code(0) && !pw_usb_key_code(1) && !pw_usb_key_code(3) && !pw_usb_key_code(0x66) &&
           !pw_usb_key_code(0xe0));
    assert(pw_usb_modifier_code(1) == (0x10 | 0x2a << 8));                     /* left Shift */
    assert(pw_usb_modifier_code(5) == (0x10 | 0x36 << 8));                     /* right Shift */
    assert(pw_usb_modifier_code(6) == (0x12 | 0x38 << 8 | PW_WINE_KEY_EXTENDED)); /* right Alt */
    assert(!pw_usb_modifier_code(8));
    /* Every key code is one the sink accepts. */
    for (uint16_t usage = 0; usage < 0x100; usage++) {
        PwWineInput key = {PW_WINE_INPUT_KEY, pw_usb_key_code(usage), 0, 0, 1};
        assert(!key.code || ((key.code & 0xff) && key.code <= (PW_WINE_KEY_EXTENDED | 0xffff)));
    }

    /* The keyboard state's layout; a disconnected keyboard holds nothing; the count is bounded. */
    uint8_t state[PW_USB_KEYBOARD_STATE];
    PwUsbKeyboard none = {0}, shift_w, w_d, after;
    const uint16_t w[] = {0x1a}, wd[] = {0x07, 0x1a}, many[20] = {0x04};
    keyboard_state(state, 1, 0x02, w, 1);
    assert(pw_usb_keyboard_decode(state, &shift_w) && shift_w.modifiers == 0x02 && shift_w.keys[0] == 0x1a &&
           !shift_w.keys[1]);
    keyboard_state(state, 0, 0x02, w, 1);
    assert(!pw_usb_keyboard_decode(state, &after) && !after.modifiers && !after.keys[0]);
    keyboard_state(state, 1, 0, many, 16);
    memcpy(state + 20, &(int32_t){40}, 4);
    assert(pw_usb_keyboard_decode(state, &after) && after.keys[0] == 0x04);
    memcpy(state + 20, &(int32_t){-1}, 4);
    assert(pw_usb_keyboard_decode(state, &after) && !after.keys[0]);

    /* Shift then W: the modifier first, then the key. */
    PwWineInput events[PW_USB_KEYBOARD_EVENTS];
    size_t count = pw_usb_keyboard_events(&none, &shift_w, events, PW_USB_KEYBOARD_EVENTS);
    assert(count == 2);
    assert(events[0].type == PW_WINE_INPUT_KEY && events[0].code == pw_usb_modifier_code(1) && events[0].down);
    assert(events[1].code == pw_usb_key_code(0x1a) && events[1].down);
    /* W moves to another slot as D is pressed, Shift is let go: releases first, W never released. */
    keyboard_state(state, 1, 0, wd, 2);
    assert(pw_usb_keyboard_decode(state, &w_d));
    count = pw_usb_keyboard_events(&shift_w, &w_d, events, PW_USB_KEYBOARD_EVENTS);
    assert(count == 2);
    assert(events[0].code == pw_usb_modifier_code(1) && !events[0].down);
    assert(events[1].code == pw_usb_key_code(0x07) && events[1].down);
    /* Both Shifts: two keys, each side its own release. */
    PwUsbKeyboard both = {.modifiers = 0x22}, left = {.modifiers = 0x02};
    count = pw_usb_keyboard_events(&both, &left, events, PW_USB_KEYBOARD_EVENTS);
    assert(count == 1 && events[0].code == pw_usb_modifier_code(5) && !events[0].down);
    /* Unplugged with keys held: every key released. */
    count = pw_usb_keyboard_events(&w_d, &none, events, PW_USB_KEYBOARD_EVENTS);
    assert(count == 2 && !events[0].down && !events[1].down);
    /* A usage reported twice is pressed once; a full buffer stops at its capacity. */
    PwUsbKeyboard twice = {0, {0x04, 0x04}};
    assert(pw_usb_keyboard_events(&none, &twice, events, PW_USB_KEYBOARD_EVENTS) == 1);
    assert(pw_usb_keyboard_events(&none, &shift_w, events, 1) == 1);
    assert(!pw_usb_keyboard_events(NULL, &shift_w, events, 4));
    /* Two keyboards merged: both modifiers, the second's keys in the free slots; a full state keeps its own. */
    PwUsbKeyboard merged = {0, {0x04}}, other = {0x02, {0, 0x05, 0x06}}, full = {0};
    pw_usb_keyboard_merge(&merged, &other);
    assert(merged.modifiers == 0x02 && merged.keys[0] == 0x04 && merged.keys[1] == 0x05 && merged.keys[2] == 0x06 &&
           !merged.keys[3]);
    for (int i = 0; i < PW_USB_KEYS; i++) full.keys[i] = (uint16_t)(0x04 + i);
    pw_usb_keyboard_merge(&full, &other);
    assert(full.modifiers == 0x02 && full.keys[0] == 0x04 && full.keys[PW_USB_KEYS - 1] == 0x04 + PW_USB_KEYS - 1);

    /* Mouse records: motion and wheels summed, the newest record's buttons, disconnected records skipped. */
    uint8_t records[3 * PW_USB_MOUSE_RECORD];
    PwUsbMouse mouse = {0};
    mouse_record(records, 200, 0x01, 5, -3, 1, 0);
    mouse_record(records + PW_USB_MOUSE_RECORD, 100, 0x00, 2, 1, 0, -1);
    mouse_record(records + 2 * PW_USB_MOUSE_RECORD, 300, 0x18, 9, 9, 9, 9);
    records[2 * PW_USB_MOUSE_RECORD + 8] = 0;
    assert(pw_usb_mouse_decode(records, 3, &mouse) == 2);
    assert(mouse.dx == 7 && mouse.dy == -2 && mouse.wheel == 1 && mouse.tilt == -1);
    assert(mouse.buttons == 0x01 && mouse.newest == 200);
    PwWineInput mouse_events[PW_USB_MOUSE_EVENTS];
    count = pw_usb_mouse_events(0x02, &mouse, mouse_events, PW_USB_MOUSE_EVENTS);
    assert(count == 4);
    assert(mouse_events[0].type == PW_WINE_INPUT_MOUSE_MOVE && mouse_events[0].x == 7 && mouse_events[0].y == -2);
    assert(mouse_events[1].type == PW_WINE_INPUT_MOUSE_WHEEL && mouse_events[1].y == 120 && mouse_events[1].x == -120);
    assert(mouse_events[2].type == PW_WINE_INPUT_MOUSE_BUTTON && mouse_events[2].code == 0 && mouse_events[2].down);
    assert(mouse_events[3].code == 1 && !mouse_events[3].down);
    /* The side buttons, and nothing when nothing changed. */
    PwUsbMouse side = {0, 0, 0, 0, 0x18, 0};
    count = pw_usb_mouse_events(0, &side, mouse_events, PW_USB_MOUSE_EVENTS);
    assert(count == 2 && mouse_events[0].code == 3 && mouse_events[1].code == 4);
    assert(!pw_usb_mouse_events(0x18, &side, mouse_events, PW_USB_MOUSE_EVENTS));
    puts("usb input: key and scan codes, keyboard state, press/release order, both Shifts, unplug, "
         "keyboards merged, mouse records, motion, wheels, five buttons");
    return 0;
}
