/* SPDX-License-Identifier: GPL-3.0-or-later */
/* wine/ps5/pw_wine_xusb.h: the title's pad slot as Windows' HID gamepad for an Xbox controller (Wine patch 2480). */
#include <assert.h>
#include <stdio.h>
#include "../wine/ps5/pw_wine_xusb.h"

static unsigned word(const uint8_t *r, unsigned i) { return r[2 * i] | r[2 * i + 1] << 8; }
static unsigned hat(const uint8_t *r) { return (word(r, 5) >> 10) & 15; }

/* The input bits the descriptor declares (main items 0x81 with the global size and count), which must fill the report. */
static unsigned input_bits(void)
{
    unsigned size = 0, count = 0, bits = 0;
    for (size_t i = 0; i < sizeof(pw_wine_xusb_descriptor);) {
        const uint8_t prefix = pw_wine_xusb_descriptor[i];
        const unsigned n = (prefix & 3) == 3 ? 4 : prefix & 3;
        unsigned value = 0;
        for (unsigned b = 0; b < n; b++) value |= pw_wine_xusb_descriptor[i + 1 + b] << (8 * b);
        if ((prefix & 0xfc) == 0x74) size = value;
        if ((prefix & 0xfc) == 0x94) count = value;
        if ((prefix & 0xfc) == 0x80) bits += size * count;
        i += 1 + n;
    }
    return bits;
}

int main(void)
{
    uint8_t r[PW_WINE_XUSB_REPORT];
    PwWinePad pad = {0};

    assert(input_bits() == PW_WINE_XUSB_REPORT * 8);
    /* Disconnected (all zero) and at rest: centred sticks, triggers at rest on Z, no button, the hat centred. */
    pw_wine_xusb_report(&pad, r);
    assert(word(r, 0) == 32768 && word(r, 1) == 32767 && word(r, 2) == 32768 && word(r, 3) == 32767);
    assert(word(r, 4) == 0x8000 && word(r, 5) == 0 && word(r, 6) == 0);
    /* Up and left are the small values, as DirectInput reports them; XInput's Y grows upwards. */
    pad.connected = 1;
    pad.thumb_lx = -32768, pad.thumb_ly = 32767, pad.thumb_rx = 32767, pad.thumb_ry = -32768;
    pw_wine_xusb_report(&pad, r);
    assert(word(r, 0) == 0 && word(r, 1) == 0 && word(r, 2) == 65535 && word(r, 3) == 65535);
    /* The left trigger raises Z, the right one lowers it, both at once cancel. */
    pad.left_trigger = 255;
    pw_wine_xusb_report(&pad, r);
    assert(word(r, 4) == 0x8000 + 255 * 128);
    pad.left_trigger = 0, pad.right_trigger = 255;
    pw_wine_xusb_report(&pad, r);
    assert(word(r, 4) == 0x8000 - 255 * 128);
    pad.left_trigger = 255;
    pw_wine_xusb_report(&pad, r);
    assert(word(r, 4) == 0x8000);
    /* Buttons 1-10: A B X Y LB RB Back Start LS RS; Guide is not one. */
    static const uint16_t xi[10] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};
    for (unsigned i = 0; i < 10; i++) {
        pad.buttons = xi[i];
        pw_wine_xusb_report(&pad, r);
        assert((word(r, 5) & 0x3ff) == 1u << i && hat(r) == 0);
    }
    pad.buttons = 0x0400;
    pw_wine_xusb_report(&pad, r);
    assert(word(r, 5) == 0);
    /* The hat: up 1, clockwise to up-left 8; opposite directions cancel. */
    static const struct { uint16_t dpad; unsigned hat; } dirs[] = {
        {1, 1}, {1 | 8, 2}, {8, 3}, {2 | 8, 4}, {2, 5}, {2 | 4, 6}, {4, 7}, {1 | 4, 8}, {1 | 2, 0}, {4 | 8, 0}, {1 | 2 | 8, 3}};
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        pad.buttons = dirs[i].dpad | 0x1000;
        pw_wine_xusb_report(&pad, r);
        assert(hat(r) == dirs[i].hat && (word(r, 5) & 0x3ff) == 1);
    }
    puts("test_pw_wine_xusb: ok");
    return 0;
}
