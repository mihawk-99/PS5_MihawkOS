/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Steam guide's session screens (src/pw_steam_screen.h), every one in every language: the words stay inside the
 * card, no line is cut and every character has a face; the animation writes only its own rectangle; a display name the faces cannot draw is left
 * out. With an output folder, each screen is also written as a PPM (the PC's pictures of the console's screens).
 *
 *   test_pw_steam_screen <folder with Inter-Regular.ttf, Inter-SemiBold.ttf> [<folder with noto-sans-sc.ttf,
 *                        noto-sans-jp.ttf> | -] [<pictures folder>]
 *
 * Without the second folder Chinese and Japanese are skipped (they need the build's subsets), and the run says so. */
#include "../src/pw_loading_art.h"
#include "../src/pw_steam_screen.h"
#include "../src/pw_vkbd.h"
#include "../src/pw_overlay.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp(const char *folder, const char *name, size_t *size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    *size = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *bytes = malloc(*size);
    assert(bytes && fread(bytes, 1, *size, file) == *size);
    fclose(file);
    return bytes;
}

static void write_ppm(const char *folder, const char *tag, const char *name, const uint32_t *canvas)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/steam-%s-%s.ppm", folder, name, tag);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", PW_LOADING_WIDTH, PW_LOADING_HEIGHT);
    for (size_t i = 0; i < (size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT; ++i) {
        const unsigned char rgb[3] = {(unsigned char)(canvas[i] >> 16), (unsigned char)(canvas[i] >> 8), (unsigned char)canvas[i]};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <inter folder> [<noto folder> | -] [<pictures folder>]\n", argv[0]);
        return 2;
    }
    size_t sizes[4] = {0};
    unsigned char *regular_ttf = slurp(argv[1], "Inter-Regular.ttf", &sizes[0]);
    unsigned char *semibold_ttf = slurp(argv[1], "Inter-SemiBold.ttf", &sizes[1]);
    assert(regular_ttf && semibold_ttf);
    const int cjk = argc > 2 && strcmp(argv[2], "-");
    unsigned char *sc_ttf = cjk ? slurp(argv[2], "noto-sans-sc.ttf", &sizes[2]) : NULL;
    unsigned char *jp_ttf = cjk ? slurp(argv[2], "noto-sans-jp.ttf", &sizes[3]) : NULL;
    assert(!cjk || (sc_ttf && jp_ttf));
    PwSteamFace *regular = pw_steam_face_open(regular_ttf, sizes[0]), *semibold = pw_steam_face_open(semibold_ttf, sizes[1]);
    PwSteamFace *sc = pw_steam_face_open(sc_ttf, sizes[2]), *jp = pw_steam_face_open(jp_ttf, sizes[3]);
    assert(regular && semibold && (!cjk || (sc && jp)));
    assert(!pw_steam_face_open((const unsigned char *)"not a font", 10));
    const char *pictures = argc > 3 ? argv[3] : NULL;

    uint32_t *backdrop = malloc((size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4);
    uint32_t *still = malloc((size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4);
    uint32_t *frame = malloc((size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4);
    assert(backdrop && still && frame);
    pw_loading_art_backdrop(backdrop);

    /* Display names: Inter draws Latin and Cyrillic ones; Han only with the language's own face */
    const PwSteamFaces latin = {regular, semibold, regular};
    assert(pw_steam_screen_persona_drawable(&latin, "Mihawk_99"));
    assert(pw_steam_screen_persona_drawable(&latin, "Ястреб Łódź"));
    assert(!pw_steam_screen_persona_drawable(&latin, "鹰"));
    assert(!pw_steam_screen_persona_drawable(&latin, ""));
    assert(!pw_steam_screen_persona_drawable(&latin, NULL));
    assert(!pw_steam_screen_persona_drawable(&latin, "bad \xff byte"));

    static const struct {
        PwSteamScreen screen;
        const char *name;
        unsigned seconds;
        unsigned long long bytes;
        const char *persona;
        const char *dialog_title, *dialog_text;
    } screens[] = {
        {PW_STEAM_SCREEN_INSTALLING, "installing", 12, 0, NULL, NULL, NULL},
        {PW_STEAM_SCREEN_UPDATING, "updating", 134, 212000000ull, NULL, NULL, NULL},
        {PW_STEAM_SCREEN_SLOW, "slow", 431, 241000000ull, NULL, NULL, NULL},
        {PW_STEAM_SCREEN_SCAN, "scan", 318, 480000000ull, NULL, NULL, NULL},
        {PW_STEAM_SCREEN_SIGNED_IN, "signed-in", 612, 480000000ull, "Mihawk", NULL, NULL},
        {PW_STEAM_SCREEN_SIGNED_IN, "signed-in-long", 612, 480000000ull, "a display name of thirty-two chars", NULL, NULL},
        {PW_STEAM_SCREEN_DIALOG, "dialog", 0, 0, NULL, "A Game - Aborted",
         "Unsupported graphics card.\nPlease read the system requirements for details.\nOK"},
    };
    size_t count = 0, drawn = 0, skipped = 0;
    const PwSetupLanguage *languages = pw_setup_languages(&count);
    for (size_t l = 0; l < count; ++l) {
        const PwSetupLanguage *language = &languages[l];
        const PwSteamFace *own = !language->font ? NULL : !strcmp(language->font, "noto-sans-sc") ? sc : jp;
        if (language->font && !own) {
            ++skipped;
            continue;
        }
        const PwSteamFaces faces = {own ? own : regular, own ? own : semibold, regular};
        for (size_t s = 0; s < sizeof(screens) / sizeof(screens[0]); ++s) {
            const PwSteamScreenState state = {screens[s].screen, language, screens[s].seconds, screens[s].bytes,
                                              screens[s].persona, screens[s].dialog_title, screens[s].dialog_text, NULL, NULL};
            PwSteamScreenReport report;
            pw_steam_screen_draw(still, backdrop, &state, &faces, &report);
            if (report.cut || report.missing || report.bottom > PW_STEAM_CARD_Y1 - 12 || report.bottom < PW_STEAM_CARD_Y0) {
                fprintf(stderr, "%s %s: %d lines cut, %d characters without a face, words down to row %d (the card ends at "
                        "%d)\n", language->tag, screens[s].name, report.cut, report.missing, report.bottom, PW_STEAM_CARD_Y1);
                abort();
            }
            memcpy(frame, still, (size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4);
            pw_steam_screen_animate(frame, &state, 7);
            /* the animation stays in the tube's or the globe's rectangle */
            const int code = state.screen == PW_STEAM_SCREEN_SCAN;
            const int x0 = code ? PW_STEAM_QR_X0 : PW_STEAM_TEXT_X0 - 2, x1 = code ? PW_STEAM_QR_X1 : PW_STEAM_TEXT_X1 + 2;
            const int y0 = code ? PW_STEAM_QR_Y0 : PW_STEAM_TUBE_Y0 - 2, y1 = code ? PW_STEAM_QR_Y1 : PW_STEAM_TUBE_Y1 + 2;
            size_t moved = 0;
            for (int y = 0; y < PW_LOADING_HEIGHT; ++y)
                for (int x = 0; x < PW_LOADING_WIDTH; ++x) {
                    const size_t i = (size_t)y * PW_LOADING_WIDTH + x;
                    if (frame[i] == still[i]) continue;
                    assert(x >= x0 && x <= x1 && y >= y0 && y <= y1);
                    ++moved;
                }
            assert(state.screen == PW_STEAM_SCREEN_SIGNED_IN || state.screen == PW_STEAM_SCREEN_DIALOG ? moved == 0 : moved > 1000);
            if (pictures) write_ppm(pictures, language->tag, screens[s].name, frame);
            ++drawn;
        }
    }
    /* A dialog box with more text than the card holds (a game's call stack after its message): cut, never past the card */
    {
        char text[6000] = "Unsupported graphics card.\nPlease read the system requirements for details.\n";
        for (int i = 0; i < 60; ++i) {
            char line[96];
            snprintf(line, sizeof(line), "\ngame.exe!0x140000000 + 0x%07x a long frame name that wraps on its own line", i * 4099);
            strcat(text, line);
        }
        const PwSteamScreenState state = {PW_STEAM_SCREEN_DIALOG, NULL, 0, 0, NULL, "A Game - Aborted", text, NULL, NULL};
        PwSteamScreenReport report;
        pw_steam_screen_draw(still, backdrop, &state, &latin, &report);
        assert(!report.missing && report.bottom <= PW_STEAM_CARD_Y1 - 12 && report.bottom > PW_STEAM_CARD_Y0);
    }
    /* MihawkOS's own keyboard (src/pw_vkbd.h): every page, with and without Shift, in every language, its focus anywhere, a
     * line typed: no character without a face (key labels fall back to Inter), nothing below the screen */
    {
        size_t kcount = 0;
        const PwSetupLanguage *langs = pw_setup_languages(&kcount);
        for (size_t l = 0; l < kcount; ++l) {
            const PwSteamFace *own = !langs[l].font ? NULL : !strcmp(langs[l].font, "noto-sans-sc") ? sc : jp;
            if (langs[l].font && !own) continue;
            const PwSteamFaces faces = {own ? own : regular, own ? own : semibold, regular};
            for (int page = 0; page < PW_VKBD_PAGES; ++page)
                for (int shift = 0; shift < 2; ++shift) {
                    PwVkbd kbd;
                    pw_vkbd_open(&kbd, page);
                    kbd.shift = shift;
                    kbd.row = (page + shift) % PW_VKBD_ROWS;
                    kbd.col = 1;
                    pw_vkbd_type(&kbd, "héllo Жюль ł");
                    PwSteamScreenState state = {PW_STEAM_SCREEN_KEYBOARD, &langs[l], 0, 0, NULL, NULL, NULL, &kbd, NULL};
                    PwSteamScreenReport report;
                    pw_steam_screen_draw(still, backdrop, &state, &faces, &report);
                    if (report.missing || report.cut || report.bottom >= PW_LOADING_HEIGHT) {
                        fprintf(stderr, "%s keyboard page %d shift %d: %d without a face, %d cut, down to row %d\n",
                                langs[l].tag, page, shift, report.missing, report.cut, report.bottom);
                        assert(0);
                    }
                    if (pictures && shift == 0) {
                        char name[32];
                        snprintf(name, sizeof(name), "keyboard-%s", pw_vkbd_page_name(page)[0] == 'a' ? "abc" :
                                 page == PW_VKBD_SYMBOLS ? "123" : page == PW_VKBD_ACCENTS ? "accents" : "cyrillic");
                        write_ppm(pictures, langs[l].tag, name, still);
                    }
                }
        }
    }
    /* A change drawn over the keyboard as it was is the whole draw's, pixel for pixel: moves in every direction across rows
     * and the specials row, keys typed and taken back, Shift and a page (those draw it whole) */
    {
        const PwSteamFaces faces = {regular, semibold, regular};
        uint32_t *whole = malloc((size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4);
        assert(whole);
        PwVkbd kbd, before;
        pw_vkbd_open(&kbd, PW_VKBD_ABC);
        PwSteamScreenState state = {PW_STEAM_SCREEN_KEYBOARD, pw_setup_default_language(), 0, 0, NULL, NULL, NULL, &kbd, NULL};
        pw_steam_screen_draw(still, backdrop, &state, &faces, NULL);
        const char *steps = "rrrdllluuuxxrdddxbbsrxpdlx";   /* moves, x types, b backspace, s Shift, p the page key */
        size_t panel = (size_t)PW_VKBD_PANEL_Y * PW_LOADING_WIDTH, rows = (size_t)PW_VKBD_PANEL_H * PW_LOADING_WIDTH * 4;
        for (const char *s = steps; *s; ++s) {
            before = kbd;
            switch (*s) {
            case 'r': pw_vkbd_move(&kbd, 1, 0); break;
            case 'l': pw_vkbd_move(&kbd, -1, 0); break;
            case 'u': pw_vkbd_move(&kbd, 0, -1); break;
            case 'd': pw_vkbd_move(&kbd, 0, 1); break;
            case 'x': pw_vkbd_press(&kbd); break;
            case 'b': pw_vkbd_backspace(&kbd); break;
            case 's': kbd.shift = !kbd.shift; break;
            case 'p': pw_vkbd_page(&kbd, 1); break;
            }
            pw_steam_screen_keyboard_changed(still, backdrop, &state, &faces, &before);
            pw_steam_screen_draw(whole, backdrop, &state, &faces, NULL);
            if (memcmp(still + panel, whole + panel, rows)) {
                fprintf(stderr, "keyboard: step %ld ('%c') drawn as a change differs from the whole draw\n", (long)(s - steps), *s);
                assert(0);
            }
        }
        free(whole);
    }
    /* The control overlay (src/pw_overlay.h): every language, its focus on each tile, Exit armed and the toggles both ways:
     * no character without a face, no word cut, nothing drawn left of the panel */
    {
        size_t ocount = 0;
        const PwSetupLanguage *langs = pw_setup_languages(&ocount);
        for (size_t l = 0; l < ocount; ++l) {
            const PwSteamFace *own = !langs[l].font ? NULL : !strcmp(langs[l].font, "noto-sans-sc") ? sc : jp;
            if (langs[l].font && !own) continue;
            const PwSteamFaces faces = {own ? own : regular, own ? own : semibold, regular};
            for (int focus = 0; focus < PW_OVERLAY_TILES; ++focus) {
                PwOverlay overlay = {.focus = focus, .stats = focus & 1, .mouse_off = focus >> 1,
                                     .armed_ms = focus == PW_OVERLAY_EXIT ? 1000 : 0};
                overlay.figures.cpu = 12, overlay.figures.gpu = -1, overlay.figures.small_total = 8192;
                overlay.figures.small_free = 900, overlay.figures.big_total = 16384, overlay.figures.big_free = 15000;
                pw_hud_frames(&overlay.figures, 58, 21);
                PwSteamScreenState state = {PW_STEAM_SCREEN_OVERLAY, &langs[l], 0, 0, NULL, NULL, NULL, NULL, &overlay};
                PwSteamScreenReport report;
                pw_steam_screen_draw(still, backdrop, &state, &faces, &report);
                if (report.missing || report.cut || report.bottom >= PW_LOADING_HEIGHT) {
                    fprintf(stderr, "%s overlay focus %d: %d without a face, %d cut, down to row %d\n", langs[l].tag, focus,
                            report.missing, report.cut, report.bottom);
                    assert(0);
                }
                /* nothing left of the panel */
                for (int y = 0; y < PW_LOADING_HEIGHT; ++y)
                    assert(!memcmp(still + (size_t)y * PW_LOADING_WIDTH, backdrop + (size_t)y * PW_LOADING_WIDTH,
                                   PW_OVERLAY_PANEL_X * 4));
                if (pictures && focus == PW_OVERLAY_EXIT) write_ppm(pictures, langs[l].tag, "overlay", still);
            }
        }
    }
    /* The statistics HUD: inside its rectangle only, see-through glass (premultiplied: no channel above alpha) */
    {
        const PwSteamFaces faces = {regular, semibold, regular};
        PwHud hud = {0};
        hud.cpu = 37, hud.gpu = -1, hud.memory_used = 5ull << 30, hud.memory_total = 12ull << 30;
        hud.small_free = 700, hud.small_total = 8192, hud.big_free = 15000, hud.big_total = 16384;
        for (int i = 0; i < 70; ++i) pw_hud_frames(&hud, i % 9 ? 60 : 24, 25);
        memcpy(still, backdrop, (size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4);
        pw_steam_screen_hud(still, &faces, &hud);
        for (int y = 0; y < PW_LOADING_HEIGHT; ++y)
            for (int x = 0; x < PW_LOADING_WIDTH; ++x) {
                const size_t at = (size_t)y * PW_LOADING_WIDTH + x;
                const int inside = x >= PW_HUD_X && x < PW_HUD_X + PW_HUD_W && y >= PW_HUD_Y && y < PW_HUD_Y + PW_HUD_H;
                if (!inside) { assert(still[at] == backdrop[at]); continue; }
                const uint32_t p = still[at], a = p >> 24;
                assert(((p >> 16) & 0xff) <= a && ((p >> 8) & 0xff) <= a && (p & 0xff) <= a);
            }
        /* the middle of the card is glass, its corner clear */
        assert((still[(size_t)(PW_HUD_Y + 120) * PW_LOADING_WIDTH + PW_HUD_X + 300] >> 24) > 100);
        assert(still[(size_t)PW_HUD_Y * PW_LOADING_WIDTH + PW_HUD_X] == 0);
        if (pictures) write_ppm(pictures, "en", "hud", still);
    }
    /* Nothing over the client: the backdrop alone (the cover is taken off then, but a draw must not fail) */
    const PwSteamScreenState none = {PW_STEAM_SCREEN_NONE, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL};
    pw_steam_screen_draw(still, backdrop, &none, &latin, NULL);
    assert(!memcmp(still, backdrop, (size_t)PW_LOADING_WIDTH * PW_LOADING_HEIGHT * 4));

    printf("steam screens: %zu drawn in %zu languages%s, words inside the card, the animation in its place\n", drawn,
           count - skipped, skipped ? " (Chinese and Japanese skipped: no subset faces given)" : "");
    pw_steam_face_close(regular);
    pw_steam_face_close(semibold);
    pw_steam_face_close(sc);
    pw_steam_face_close(jp);
    free(regular_ttf), free(semibold_ttf), free(sc_ttf), free(jp_ttf), free(backdrop), free(still), free(frame);
    return 0;
}
