/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_WINE_XUSB_H
#define PW_WINE_XUSB_H
#include <stdint.h>
#include <string.h>
#include "pw_wine_sink.h"

/* The HID gamepad Windows makes for an Xbox controller (its XUSB driver), which the PS5 bus makes from the title's pad
 * slot while an Xbox controller is chosen (Wine patch 2480). XInput reads the slot itself (patch 0470); this device is
 * for every program that finds controllers another way: DirectInput, raw input, SDL, Rewired. Before it, those found no
 * controller at all while an Xbox one was chosen, the default (Ys Origin, DirectInput 8, 2026-11-02).
 *
 * The layout is Windows' (and Wine's winexinput.sys, xinput_report_desc), no report id, 14 bytes:
 * X, Y, Rx, Ry 0..65535 (Y grows downwards, as DirectInput reports it), Z the triggers on one axis (0x8000 at rest, the
 * left trigger towards 0xff80, the right one towards 0), buttons 1-10 A B X Y LB RB Back Start LS RS, the hat
 * 1 (up) .. 8 (up-left) clockwise, 0 centred, then 18 constant bits. The Guide button is not in it, as on Windows. */
enum { PW_WINE_XUSB_REPORT = 14 };

static const uint8_t pw_wine_xusb_descriptor[] = {
    0x05,0x01, 0x09,0x05, 0xa1,0x01,                    /* Generic desktop, Game Pad, Application */
    0x09,0x00, 0xa1,0x00,                               /* Physical: X, Y */
    0x09,0x30, 0x09,0x31, 0x27,0xff,0xff,0x00,0x00, 0x47,0xff,0xff,0x00,0x00,
    0x75,0x10, 0x95,0x02, 0x81,0x02, 0xc0,
    0xa1,0x00, 0x09,0x33, 0x09,0x34, 0x95,0x02, 0x81,0x02, 0xc0,   /* Physical: Rx, Ry */
    0xa1,0x00, 0x09,0x32, 0x95,0x01, 0x81,0x02, 0xc0,               /* Physical: Z */
    0x05,0x09, 0x19,0x01, 0x29,0x0a, 0x25,0x01, 0x45,0x01, 0x95,0x0a, 0x75,0x01, 0x81,0x02,   /* 10 buttons */
    0x05,0x01, 0x09,0x39, 0x15,0x01, 0x25,0x08, 0x46,0x3b,0x01, 0x75,0x04, 0x97,0x01,0x00,0x00,0x00,
    0x65,0x0e, 0x81,0x42,                               /* hat 0..315 degrees, null state */
    0x95,0x12, 0x75,0x01, 0x81,0x03,                    /* 18 constant bits */
    0xc0
};

/* The report for the slot's state; a disconnected slot (all zero) reads as released and centred. */
static inline void pw_wine_xusb_report(const PwWinePad *pad, uint8_t report[PW_WINE_XUSB_REPORT])
{
    /* XINPUT_GAMEPAD_* bits for buttons 1-10, in order */
    static const uint16_t order[10] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};
    /* the hat from the D-pad bits (up 1, down 2, left 4, right 8); opposite directions cancel */
    static const uint8_t hat[16] = {0, 1, 5, 0, 7, 8, 6, 7, 3, 2, 4, 3, 0, 1, 5, 0};
    const uint16_t values[5] = {
        (uint16_t)(pad->thumb_lx + 32768), (uint16_t)(32767 - pad->thumb_ly),
        (uint16_t)(pad->thumb_rx + 32768), (uint16_t)(32767 - pad->thumb_ry),
        (uint16_t)(0x8000 + (pad->left_trigger - pad->right_trigger) * 128)};
    uint16_t buttons = 0;

    for (unsigned i = 0; i < 10; i++)
        if (pad->buttons & order[i]) buttons |= 1u << i;
    buttons |= (uint16_t)(hat[pad->buttons & 15] << 10);
    memset(report, 0, PW_WINE_XUSB_REPORT);
    for (unsigned i = 0; i < 5; i++) report[2 * i] = values[i] & 0xff, report[2 * i + 1] = values[i] >> 8;
    report[10] = buttons & 0xff;
    report[11] = buttons >> 8;
}
#endif
