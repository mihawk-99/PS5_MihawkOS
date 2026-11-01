/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_osk.h"
#include <string.h>

static int complete(unsigned trigger, const struct pad *pad)
{
    if (!pad->connected || pad->intercepted) return 0;
    switch (trigger) {
    case PW_OSK_TOUCHPAD_TWO: return (pad->held & PAD_TOUCH_PAD) && pad->touch_count >= 2;
    case PW_OSK_L3_R3: return (pad->held & (PAD_L3 | PAD_R3)) == (PAD_L3 | PAD_R3);
    case PW_OSK_OPTIONS_TOUCHPAD: return (pad->held & (PAD_OPTIONS | PAD_TOUCH_PAD)) == (PAD_OPTIONS | PAD_TOUCH_PAD);
    default: return 0;
    }
}

int pw_osk_detect(PwOskDetector *detector, unsigned trigger, const struct pad *pad)
{
    const int now = complete(trigger, pad);
    const int fire = now && !detector->was_complete;
    detector->was_complete = (uint8_t)now;
    return fire;
}

void pw_osk_pad_open(PwOskPad *state, const struct pad *now)
{
    state->open = 1;
    state->hold = now->held;
}

void pw_osk_pad_close(PwOskPad *state, const struct pad *now)
{
    state->open = 0;
    state->hold = now->held;
}

void pw_osk_pad_filter(PwOskPad *state, struct pad *shown)
{
    state->hold &= shown->held;   /* a button let go is the game's again */
    if (state->open) {
        shown->held = shown->pressed = 0;
        shown->left_x = shown->left_y = shown->right_x = shown->right_y = 0.0f;
        shown->l2 = shown->r2 = 0.0f;
        shown->touch_count = 0;
        return;
    }
    shown->held &= ~state->hold;
    shown->pressed &= ~state->hold;
}

void pw_osk_typer_start(PwOskTyper *typer, const char *utf8, int enter)
{
    const unsigned char *text = (const unsigned char *)utf8;
    memset(typer, 0, sizeof(*typer));
    typer->enter = enter != 0;
    while (text && *text && typer->count + 2 <= PW_OSK_MAX_UNITS) {
        uint32_t code = *text;
        unsigned length = 1;
        if ((code & 0xe0u) == 0xc0u) { code &= 0x1fu; length = 2; }
        else if ((code & 0xf0u) == 0xe0u) { code &= 0x0fu; length = 3; }
        else if ((code & 0xf8u) == 0xf0u) { code &= 0x07u; length = 4; }
        else if (code & 0x80u) { code = 0xfffdu; length = 1; }
        for (unsigned i = 1; i < length; ++i) {
            if ((text[i] & 0xc0u) != 0x80u) { code = 0xfffdu; length = 1; break; }
            code = (code << 6) | (text[i] & 0x3fu);
        }
        text += length;
        if (code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu)) code = 0xfffdu;
        if (code < 0x20u || code == 0x7fu) continue;   /* a control is not typed */
        if (code >= 0x10000u) {
            code -= 0x10000u;
            typer->units[typer->count++] = (uint16_t)(0xd800u | (code >> 10));
            typer->units[typer->count++] = (uint16_t)(0xdc00u | (code & 0x3ffu));
        } else {
            typer->units[typer->count++] = (uint16_t)code;
        }
    }
}

int pw_osk_typer_next(PwOskTyper *typer, PwWineInput *event)
{
    const unsigned steps = typer->count * 2 + (typer->enter ? 2 : 0);
    if (typer->next >= steps) return 0;
    memset(event, 0, sizeof(*event));
    if (typer->next < typer->count * 2) {
        event->type = PW_WINE_INPUT_CHAR;
        event->code = typer->units[typer->next / 2];
    } else {
        event->type = PW_WINE_INPUT_KEY;
        event->code = 0x0d | (0x1cu << PW_WINE_KEY_SCAN_SHIFT);   /* Return, scan code 0x1c */
    }
    event->down = (typer->next & 1) ? 0 : 1;
    typer->next++;
    return 1;
}

int pw_osk_typer_done(const PwOskTyper *typer)
{
    return typer->next >= typer->count * 2 + (typer->enter ? 2 : 0);
}
