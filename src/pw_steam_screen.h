/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_STEAM_SCREEN_H
#define PW_STEAM_SCREEN_H

/* The Steam guide's screens inside its sessions (src/pw_steam_guide.h): the title draws them on the CPU and the GPU
 * broker shows them over the client's windows (native/proton_wine.cpp; the broker's cover). They are the guided setup's
 * pages in the loading screen's Frutiger Aero: its sky and meadow (src/pw_loading_art.h), a frosted card with dark ink
 * as the setup's, the glass tube, the globe. Every word is the setup's (src/pw_setup.h) in the setup's language; the
 * faces are the title's Inter and, for Chinese and Japanese, the Noto Sans subsets the build makes from those texts.
 * Host test, and the PC's pictures of every screen in every language: tests/test_pw_steam_screen.c.
 *
 * Canvas: PW_LOADING_WIDTH x PW_LOADING_HEIGHT, 0xAARRGGBB, as the loading screen's. pw_steam_screen_draw draws the
 * still picture (at most once a second: the elapsed time and the megabytes change); pw_steam_screen_animate draws what
 * moves over a copy of it, about ten times a second. */

#include <stddef.h>
#include <stdint.h>

#include "pw_setup.h"
#include "pw_steam_guide.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One TrueType face over bytes the caller keeps for as long as the face; NULL when they are not a face. */
typedef struct PwSteamFace PwSteamFace;
PwSteamFace *pw_steam_face_open(const unsigned char *ttf, size_t size);
void pw_steam_face_close(PwSteamFace *face);

typedef struct PwSteamFaces {
    const PwSteamFace *regular, *semibold; /* the language's: Inter, or its Noto Sans subset for both */
    const PwSteamFace *latin;              /* Inter Regular, for a display name's letters the language's face lacks */
} PwSteamFaces;

typedef struct PwSteamScreenState {
    PwSteamScreen screen;
    const PwSetupLanguage *language; /* NULL: English */
    unsigned seconds;                /* since the session began: the elapsed line */
    unsigned long long bytes;        /* the client's downloads so far: the megabytes line */
    const char *persona;             /* the signed-in display name (UTF-8), or NULL */
    const char *dialog_title;        /* PW_STEAM_SCREEN_DIALOG: the box's caption (UTF-8), or NULL */
    const char *dialog_text;         /* its text, lines split by '\n' (UTF-8), or NULL; cut to what fits */
} PwSteamScreenState;

/* What a drawing did with its words (for the test): the lowest pixel row one reached, how many lines were cut because
 * they did not fit, and how many characters no face had (none should be either). */
typedef struct PwSteamScreenReport {
    int bottom;
    int cut;
    int missing;
} PwSteamScreenReport;
/* The still picture: backdrop (pw_loading_art_backdrop's, copied in), the card and every word. NONE draws the backdrop
 * alone. report may be NULL. */
void pw_steam_screen_draw(uint32_t *canvas, const uint32_t *backdrop, const PwSteamScreenState *state,
                          const PwSteamFaces *faces, PwSteamScreenReport *report);
/* What moves, over a copy of the still picture: the tube's liquid (installing, updating, slow) or the line scanning the
 * code (the phone screen), for tick (the loading screen's frame count). Nothing outside their rectangles is written. */
void pw_steam_screen_animate(uint32_t *canvas, const PwSteamScreenState *state, unsigned tick);

/* Whether the display name can be drawn whole with these faces (else the screen says "You're signed in" alone). */
int pw_steam_screen_persona_drawable(const PwSteamFaces *faces, const char *persona);

/* The card and the places the screens put things (1920x1080 pixels): tests check the words stay inside. */
enum {
    PW_STEAM_CARD_X0 = 300, PW_STEAM_CARD_Y0 = 170, PW_STEAM_CARD_X1 = 1620, PW_STEAM_CARD_Y1 = 930,
    PW_STEAM_TEXT_X0 = 372, PW_STEAM_TEXT_X1 = 1548,
    PW_STEAM_TUBE_Y0 = 604, PW_STEAM_TUBE_Y1 = 638,
    /* the phone screen's picture: a phone with the Steam Guard shield, and the code it scans, a line moving over it */
    PW_STEAM_QR_X0 = 1010, PW_STEAM_QR_Y0 = 590, PW_STEAM_QR_X1 = 1150, PW_STEAM_QR_Y1 = 730,
};

#ifdef __cplusplus
}
#endif

#endif
