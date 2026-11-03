/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_osk.h"
#include "pw_vkbd.h"
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

/* Room for n more items: the ones already typed are dropped first */
static int room(PwOskTyper *typer, unsigned n)
{
    const unsigned typed = typer->next / 2;
    if (typed) {
        memmove(typer->items, typer->items + typed, (typer->count - typed) * sizeof(typer->items[0]));
        typer->count -= typed;
        typer->next -= typed * 2;
    }
    return typer->count + n <= PW_OSK_MAX_UNITS;
}

int pw_osk_typer_text(PwOskTyper *typer, const char *utf8)
{
    const unsigned char *text = (const unsigned char *)utf8;
    while (text && *text) {
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
        if (!room(typer, 2)) return 0;                 /* a pair's room, and one left for an Enter after the line */
        if (code >= 0x10000u) {
            code -= 0x10000u;
            typer->items[typer->count++] = 0xd800u | (code >> 10);
            typer->items[typer->count++] = 0xdc00u | (code & 0x3ffu);
        } else {
            typer->items[typer->count++] = code;
        }
    }
    return 1;
}

int pw_osk_typer_key(PwOskTyper *typer, uint32_t code)
{
    if (!room(typer, 1)) return 0;
    typer->items[typer->count++] = code | PW_OSK_TYPER_KEY;
    return 1;
}

void pw_osk_typer_start(PwOskTyper *typer, const char *utf8, int enter)
{
    memset(typer, 0, sizeof(*typer));
    pw_osk_typer_text(typer, utf8);
    if (enter) pw_osk_typer_key(typer, PW_OSK_KEY_RETURN);
}

int pw_osk_typer_next(PwOskTyper *typer, PwWineInput *event)
{
    if (typer->next >= typer->count * 2) return 0;
    const uint32_t item = typer->items[typer->next / 2];
    memset(event, 0, sizeof(*event));
    event->type = (item & PW_OSK_TYPER_KEY) ? PW_WINE_INPUT_KEY : PW_WINE_INPUT_CHAR;
    event->code = item & ~PW_OSK_TYPER_KEY;
    event->down = (typer->next & 1) ? 0 : 1;
    typer->next++;
    return 1;
}

int pw_osk_typer_done(const PwOskTyper *typer)
{
    return typer->next >= typer->count * 2;
}

int pw_osk_own_typed(PwOskTyper *typer, int vkbd_result, const char *typed)
{
    if (vkbd_result == PW_VKBD_TYPED) pw_osk_typer_text(typer, typed);
    else if (vkbd_result == PW_VKBD_ERASED) pw_osk_typer_key(typer, PW_OSK_KEY_BACK);
    else if (vkbd_result == PW_VKBD_FINISHED) pw_osk_typer_key(typer, PW_OSK_KEY_RETURN);
    return vkbd_result;
}
