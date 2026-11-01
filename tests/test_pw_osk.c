/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../native/pw_osk_ps5.h"
#include "../src/pw_osk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct pad pad_with(uint32_t held, unsigned touches)
{
    struct pad pad = {.connected = 1, .held = held, .touch_count = (uint8_t)touches};
    return pad;
}

static void detection(void)
{
    PwOskDetector detector = {0};
    /* Off never fires, whatever is held */
    struct pad all = pad_with(PAD_L3 | PAD_R3 | PAD_OPTIONS | PAD_TOUCH_PAD, 2);
    assert(!pw_osk_detect(&detector, PW_OSK_OFF, &all));
    struct pad none = pad_with(0, 0);
    /* L3 + R3: a single stick press is nothing; both fire once; holding fires no more; a new press fires again */
    struct pad l3 = pad_with(PAD_L3, 0), r3 = pad_with(PAD_R3, 0), both = pad_with(PAD_L3 | PAD_R3, 0);
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &l3));
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &r3));
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &none));
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &l3));
    assert(pw_osk_detect(&detector, PW_OSK_L3_R3, &both));
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &both));
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &l3));       /* one let go, the other still held */
    assert(pw_osk_detect(&detector, PW_OSK_L3_R3, &both));      /* made again */
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &none));
    /* touchpad: a click with one finger is the mouse button's, two fingers open the keyboard; no click is nothing */
    struct pad one = pad_with(PAD_TOUCH_PAD, 1), two = pad_with(PAD_TOUCH_PAD, 2), touch2 = pad_with(0, 2);
    assert(!pw_osk_detect(&detector, PW_OSK_TOUCHPAD_TWO, &one));
    assert(!pw_osk_detect(&detector, PW_OSK_TOUCHPAD_TWO, &touch2));
    assert(pw_osk_detect(&detector, PW_OSK_TOUCHPAD_TWO, &two));
    assert(!pw_osk_detect(&detector, PW_OSK_TOUCHPAD_TWO, &two));
    assert(!pw_osk_detect(&detector, PW_OSK_TOUCHPAD_TWO, &none));
    /* Options + touchpad: either alone is nothing */
    struct pad options = pad_with(PAD_OPTIONS, 0), pad_click = pad_with(PAD_TOUCH_PAD, 1), op = pad_with(PAD_OPTIONS | PAD_TOUCH_PAD, 1);
    assert(!pw_osk_detect(&detector, PW_OSK_OPTIONS_TOUCHPAD, &options));
    assert(!pw_osk_detect(&detector, PW_OSK_OPTIONS_TOUCHPAD, &pad_click));
    assert(pw_osk_detect(&detector, PW_OSK_OPTIONS_TOUCHPAD, &op));
    assert(!pw_osk_detect(&detector, PW_OSK_OPTIONS_TOUCHPAD, &op));
    /* a gesture is not another trigger's: L3+R3 does nothing when Options + touchpad is chosen */
    assert(!pw_osk_detect(&detector, PW_OSK_OPTIONS_TOUCHPAD, &none));
    assert(!pw_osk_detect(&detector, PW_OSK_OPTIONS_TOUCHPAD, &both));
    /* a pad that is gone, or that the system holds, fires nothing and does not arm the gesture */
    struct pad gone = both, held_by_system = both;
    gone.connected = 0;
    held_by_system.connected = 0;
    held_by_system.intercepted = 1;
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &gone));
    assert(!pw_osk_detect(&detector, PW_OSK_L3_R3, &held_by_system));
    /* the names */
    for (unsigned i = 0; i < PW_OSK_TRIGGERS; i++) assert(pw_osk_trigger_parse(pw_osk_trigger_id(i)) == (int)i);
    assert(pw_osk_trigger_parse("banana") == -1 && pw_osk_trigger_parse(NULL) == -1 && !pw_osk_trigger_id(PW_OSK_TRIGGERS));
}

static void pad_hand_over(void)
{
    PwOskPad state = {0};
    struct pad opening = pad_with(PAD_L3 | PAD_R3 | PAD_CROSS, 0);
    struct pad shown = opening;
    pw_osk_pad_open(&state, &opening);
    pw_osk_pad_filter(&state, &shown);
    assert(!shown.held && !shown.pressed && !shown.touch_count);     /* the game sees an idle pad while it is open */
    shown = pad_with(PAD_SQUARE, 1);
    shown.left_x = 0.5f;
    shown.pressed = PAD_SQUARE;
    pw_osk_pad_filter(&state, &shown);
    assert(!shown.held && shown.left_x == 0.0f);
    struct pad closing = pad_with(PAD_CIRCLE, 0);                    /* the button that answered the keyboard */
    pw_osk_pad_close(&state, &closing);
    shown = pad_with(PAD_CIRCLE | PAD_SQUARE, 0);
    shown.pressed = PAD_CIRCLE | PAD_SQUARE;
    pw_osk_pad_filter(&state, &shown);
    assert(shown.held == PAD_SQUARE && shown.pressed == PAD_SQUARE); /* Circle stays the keyboard's until released */
    shown = pad_with(PAD_SQUARE, 0);
    pw_osk_pad_filter(&state, &shown);
    shown = pad_with(PAD_CIRCLE, 0);
    shown.pressed = PAD_CIRCLE;
    pw_osk_pad_filter(&state, &shown);
    assert(shown.held == PAD_CIRCLE);                                /* let go once, it is the game's again */
}

static unsigned typed(const char *text, int enter, PwWineInput *events, unsigned capacity)
{
    PwOskTyper typer;
    unsigned count = 0;
    pw_osk_typer_start(&typer, text, enter);
    assert(!pw_osk_typer_done(&typer) || (!*text && !enter));
    while (count < capacity && pw_osk_typer_next(&typer, &events[count])) count++;
    assert(pw_osk_typer_done(&typer));
    return count;
}

static void typing(void)
{
    PwWineInput e[600];
    /* "hi" + Enter: each unit pressed and released, then Return */
    assert(typed("hi", 1, e, 600) == 6);
    assert(e[0].type == PW_WINE_INPUT_CHAR && e[0].code == 'h' && e[0].down == 1);
    assert(e[1].type == PW_WINE_INPUT_CHAR && e[1].code == 'h' && e[1].down == 0);
    assert(e[2].code == 'i' && e[2].down == 1 && e[3].code == 'i' && e[3].down == 0);
    assert(e[4].type == PW_WINE_INPUT_KEY && (e[4].code & 0xff) == 0x0d && e[4].down == 1 && e[5].down == 0);
    assert(typed("hi", 0, e, 600) == 4);
    assert(typed("", 1, e, 600) == 2 && typed("", 0, e, 600) == 0);
    /* a character with no key of its own types (accent, Cyrillic, CJK), a pair of units for one beyond the plane */
    assert(typed("\xc3\xa9", 0, e, 600) == 2 && e[0].code == 0xe9);
    assert(typed("\xd0\x96", 0, e, 600) == 2 && e[0].code == 0x416);
    assert(typed("\xe6\x97\xa5", 0, e, 600) == 2 && e[0].code == 0x65e5);
    assert(typed("\xf0\x9f\x98\x80", 0, e, 600) == 4 && e[0].code == 0xd83d && e[2].code == 0xde00);
    /* the console check's fixed line is what tests/osk-typed-probe.c waits for, then Return */
    static const uint16_t expected[] = {0x68, 0xe9, 0x6c, 0x6c, 0x6f, 0x20, 0x416, 0x65e5};
    assert(typed(PW_OSK_TEST_TEXT, 1, e, 600) == 2 * 8 + 2);
    for (unsigned i = 0; i < 8; i++) assert(e[2 * i].type == PW_WINE_INPUT_CHAR && e[2 * i].code == expected[i]);
    /* controls are skipped, invalid bytes become U+FFFD, and the line is cut at its unit limit */
    assert(typed("a\tb\n", 0, e, 600) == 4 && e[0].code == 'a' && e[2].code == 'b');
    assert(typed("\xff", 0, e, 600) == 2 && e[0].code == 0xfffd);
    char long_line[400];
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = 0;
    assert(typed(long_line, 1, e, 600) == (PW_OSK_MAX_UNITS - 1) * 2 + 2);   /* 254 units, then Enter */
    /* every event the typer makes is one the title's input sink accepts */
    for (unsigned i = 0; i < 6; i++) assert(e[i].down <= 1);
}

/* the console's dialog, faked */
static int fake_common_rc, fake_module_rc, fake_init_rc, fake_status_script[8], fake_status_at, fake_outcome;
static unsigned fake_terms, fake_aborts, fake_inits;
static uint16_t *fake_buffer;
static const char *fake_line;
static int common_init(void) { return fake_common_rc; }
static int load_module(void) { return fake_module_rc; }
static int foreground_user(int32_t *user) { *user = 7; return 0; }
static int dialog_init(const void *param)
{
    struct { int32_t user_id, type; uint64_t languages; int32_t label, method; void *filter; uint32_t option, max_length;
             uint16_t *buffer; } const *p = param;
    assert(p->user_id == 7 && p->max_length == PW_OSK_PS5_TEXT - 1);
    fake_buffer = p->buffer;
    fake_inits++;
    return fake_init_rc;
}
static int dialog_status(void)
{
    const int status = fake_status_script[fake_status_at];
    if (fake_status_at < 7) fake_status_at++;
    return status;
}
static int dialog_result(int32_t *outcome)
{
    *outcome = fake_outcome;
    if (fake_outcome == PW_OSK_PS5_OK) pw_osk_ps5_utf16(fake_line, fake_buffer, PW_OSK_PS5_TEXT);
    return 0;
}
static int dialog_term(void) { fake_terms++; return 0; }
static int dialog_abort(void) { fake_aborts++; return 0; }
static const PwOskPs5Ops fake_ops = {common_init, load_module, foreground_user, dialog_init, dialog_status, dialog_result,
                                     dialog_term, dialog_abort, 0};

static void system_dialog(void)
{
    PwOskPs5 osk;
    char text[64];
    /* the dialog family already up counts; a module that will not load is an error and nothing shows */
    fake_common_rc = (int)0x80b80002; fake_module_rc = 0;
    assert(!pw_osk_ps5_open(&osk, &fake_ops));
    fake_common_rc = -2;   /* no way to start the dialog family: logged, not fatal while the module's calls are there */
    assert(!pw_osk_ps5_open(&osk, &fake_ops));
    fake_common_rc = (int)0x80b80002;
    fake_module_rc = -1;
    assert(pw_osk_ps5_open(&osk, &fake_ops) < 0 && pw_osk_ps5_show(&osk, "x") < 0);
    fake_common_rc = 0; fake_module_rc = 0;
    assert(!pw_osk_ps5_open(&osk, &fake_ops));
    assert(pw_osk_ps5_poll(&osk, text, sizeof(text)) == -1);         /* not shown */
    /* shown, running twice, finished with a line (UTF-8 out of the dialog's UTF-16) */
    fake_init_rc = 0; fake_outcome = PW_OSK_PS5_OK; fake_line = "h\xc3\xa9llo \xf0\x9f\x98\x80";
    memset(fake_status_script, 0, sizeof(fake_status_script));
    fake_status_script[0] = fake_status_script[1] = PW_OSK_PS5_RUNNING;
    fake_status_script[2] = PW_OSK_PS5_FINISHED;
    fake_status_at = 0; fake_terms = fake_inits = 0;
    assert(!pw_osk_ps5_show(&osk, "Type"));
    assert(pw_osk_ps5_show(&osk, "again") < 0);                       /* one at a time */
    assert(!pw_osk_ps5_poll(&osk, text, sizeof(text)) && !pw_osk_ps5_poll(&osk, text, sizeof(text)));
    assert(pw_osk_ps5_poll(&osk, text, sizeof(text)) == 1 && !strcmp(text, "h\xc3\xa9llo \xf0\x9f\x98\x80"));
    assert(fake_terms == 1 && !osk.showing && osk.accepted == 1);
    /* cancelled: ended with nothing */
    fake_outcome = 1; fake_status_at = 0;
    assert(!pw_osk_ps5_show(&osk, "Type"));
    assert(!pw_osk_ps5_poll(&osk, text, sizeof(text)) && !pw_osk_ps5_poll(&osk, text, sizeof(text)));
    assert(pw_osk_ps5_poll(&osk, text, sizeof(text)) == 2 && !text[0] && osk.cancelled == 1 && fake_terms == 2);
    /* accepted with an empty line is nothing to type */
    fake_outcome = PW_OSK_PS5_OK; fake_line = ""; fake_status_at = 0;
    assert(!pw_osk_ps5_show(&osk, "Type"));
    pw_osk_ps5_poll(&osk, text, sizeof(text)); pw_osk_ps5_poll(&osk, text, sizeof(text));
    assert(pw_osk_ps5_poll(&osk, text, sizeof(text)) == 2);
    /* a dialog that never comes up is given up on after the grace, not held forever */
    fake_status_at = 0;
    memset(fake_status_script, 0, sizeof(fake_status_script));
    assert(!pw_osk_ps5_show(&osk, "Type"));
    int answer = 0, polls = 0;
    while (!(answer = pw_osk_ps5_poll(&osk, text, sizeof(text))) && polls < 1000) polls++;
    assert(answer == 2 && polls == PW_OSK_PS5_GRACE_POLLS - 1);
    /* a dialog the system refuses to start leaves nothing shown, and closing a shown one aborts it */
    fake_init_rc = -1;
    assert(pw_osk_ps5_show(&osk, "Type") < 0 && !osk.showing);
    fake_init_rc = 0;
    assert(!pw_osk_ps5_show(&osk, "Type"));
    pw_osk_ps5_close(&osk);
    assert(!osk.showing && fake_aborts == 1);
    /* UTF round trips */
    uint16_t units[16];
    char back[32];
    assert(pw_osk_ps5_utf16("a\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80", units, 16) == 5);
    assert(pw_osk_ps5_utf8(units, back, sizeof(back)) == 10 && !strcmp(back, "a\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80"));
}

int main(void)
{
    detection();
    pad_hand_over();
    typing();
    system_dialog();
    puts("pw_osk: ok");
    return 0;
}
