/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "../ps5/src/platform.h"
#include "../wine/ps5/pw_wine_sink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The system's on-screen keyboard over a running game (Settings > Controls > On-screen keyboard): which pad gesture
 * opens it, how the pad is handed to it, and how the line it returns is typed into the game. Off by default.
 * Portable: no console calls (native/pw_osk_ps5.h shows the dialog). */
typedef enum {
    PW_OSK_OFF = 0,
    PW_OSK_TOUCHPAD_TWO,        /* a touchpad click with two fingers on it */
    PW_OSK_L3_R3,               /* both sticks pressed in */
    PW_OSK_OPTIONS_TOUCHPAD,    /* Options and a touchpad click together */
    PW_OSK_TRIGGERS
} PwOskTrigger;

/* The saved name of a trigger: "off", "touchpad", "l3-r3", "options-touchpad" (in the header, so the settings file's
 * reader does not link the rest). */
static inline const char *pw_osk_trigger_id(unsigned trigger)
{
    static const char *const ids[PW_OSK_TRIGGERS] = {"off", "touchpad", "l3-r3", "options-touchpad"};
    return trigger < PW_OSK_TRIGGERS ? ids[trigger] : 0;
}
/* The trigger a saved name stands for, or -1. */
static inline int pw_osk_trigger_parse(const char *id)
{
    for (unsigned i = 0; id && i < PW_OSK_TRIGGERS; ++i) {
        const char *name = pw_osk_trigger_id(i), *a = name, *b = id;
        while (*a && *a == *b) ++a, ++b;
        if (!*a && !*b) return (int)i;
    }
    return -1;
}

/* One per pad. pw_osk_detect is called at every poll and answers 1 on the poll that completes the chosen gesture: once
 * per gesture, however long it is held; never with the keyboard Off, never for a pad that is gone or held by the
 * system, never for one stick pressed alone or one finger on the touchpad. */
typedef struct PwOskDetector { uint8_t was_complete; } PwOskDetector;
int pw_osk_detect(PwOskDetector *detector, unsigned trigger, const struct pad *pad);

/* The pad while the keyboard is open: the game sees it idle, and a button held when the keyboard opened or closed stays
 * the keyboard's until it is let go (a press that opened or answered it must not reach the game). */
typedef struct PwOskPad { uint8_t open; uint32_t hold; } PwOskPad;
void pw_osk_pad_open(PwOskPad *state, const struct pad *now);
void pw_osk_pad_close(PwOskPad *state, const struct pad *now);
void pw_osk_pad_filter(PwOskPad *state, struct pad *shown);
/* The presses the keyboard (or the control overlay) itself may act on: none of the buttons held when it opened, until each
 * is let go. The Cross that chose the overlay's Keyboard tile opens the keyboard on the same poll; read as the keyboard's
 * own press it typed the focused key, q (a tester, 2026-11-02: "the letter q is always pressed"). */
static inline uint32_t pw_osk_pad_pressed(const PwOskPad *state, const struct pad *now)
{
    return now->pressed & ~state->hold;
}

/* Typing into the game: one event per call, in order, until 0. Each UTF-16 unit is a PW_WINE_INPUT_CHAR press and release
 * (a unit with no key of its own, an accent or a CJK character, still types: Wine makes it a VK_PACKET key and WM_CHAR, as
 * Windows' own touch keyboard does); controls are skipped. A key (Backspace, Return) is a PW_WINE_INPUT_KEY press and
 * release with a PC keyboard's virtual key and scan code. MihawkOS's own keyboard adds each press as it is made
 * (pw_osk_typer_text, pw_osk_typer_key); the system dialog's line comes whole, Enter after it (pw_osk_typer_start). */
enum { PW_OSK_MAX_UNITS = 255 };
enum { PW_OSK_KEY_BACK = 0x08 | (0x0e << PW_WINE_KEY_SCAN_SHIFT), PW_OSK_KEY_RETURN = 0x0d | (0x1c << PW_WINE_KEY_SCAN_SHIFT) };
/* The fixed line the console check types (WINE_PS5_OSK_TEST=type, tests/osk-typed-probe.c): "h", e acute, "llo ", Zhe, ri. */
#define PW_OSK_TEST_TEXT "h\xc3\xa9llo \xd0\x96\xe6\x97\xa5"
typedef struct PwOskTyper {
    uint32_t items[PW_OSK_MAX_UNITS];   /* a UTF-16 unit, or a KEY code with PW_OSK_TYPER_KEY */
    unsigned count, next;               /* items; the next step: 2 per item (press, release) */
} PwOskTyper;
#define PW_OSK_TYPER_KEY 0x80000000u
void pw_osk_typer_start(PwOskTyper *typer, const char *utf8, int enter);
/* Adds to what is still to type: 1, or 0 when the queue is full (the rest is dropped) */
int pw_osk_typer_text(PwOskTyper *typer, const char *utf8);
int pw_osk_typer_key(PwOskTyper *typer, uint32_t code);
int pw_osk_typer_next(PwOskTyper *typer, PwWineInput *event);
int pw_osk_typer_done(const PwOskTyper *typer);
/* What a press of MihawkOS's own keyboard (src/pw_vkbd.h) sends the game, now: TYPED its characters, ERASED Backspace,
 * FINISHED (Done) Return; CANCELLED nothing (what was typed stays the game's). The result back. */
int pw_osk_own_typed(PwOskTyper *typer, int vkbd_result, const char *typed);

#ifdef __cplusplus
}
#endif
