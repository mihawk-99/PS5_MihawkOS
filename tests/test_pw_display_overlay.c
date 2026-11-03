/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The broker's overlay (the graphics foundation's pw_display_overlay.h, the own keyboard's bottom panel over the game):
 * every pixel inside the rectangle is the panel's, and every byte of the tiled buffer outside it is as the frame left it,
 * for the keyboard's aligned panel, an unaligned rectangle and one clipped at the screen's corner; the shadow streamed out
 * byte for byte; and the see-through panel's blend over the blurred frame, with its bubbles only in the margins. */
#include "../.deps/graphics-foundation/tooling/gpu-broker/pw_display_overlay.h"
#include "../src/pw_vkbd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TILED_BYTES = PW_DISPLAY_BLOCKS * 9 << 16 };

static void check(uint8_t *frame, uint8_t *expected, const uint32_t *panel, unsigned x, unsigned y, unsigned w, unsigned h)
{
    for (size_t i = 0; i < TILED_BYTES; ++i) frame[i] = (uint8_t)(i * 2654435761u >> 11);
    memcpy(expected, frame, TILED_BYTES);
    for (unsigned j = 0; j < h && y + j < PW_DISPLAY_HEIGHT; ++j)
        for (unsigned i = 0; i < w && x + i < PW_DISPLAY_WIDTH; ++i)
            memcpy(expected + pw_display_tiled(x + i, y + j), &panel[(size_t)j * w + i], 4);
    pw_display_overlay_blit(frame, panel, x, y, w, h);
    assert(!memcmp(frame, expected, TILED_BYTES));
}

int main(void)
{
    uint8_t *frame = aligned_alloc(65536, TILED_BYTES), *expected = aligned_alloc(65536, TILED_BYTES);
    uint32_t *panel = malloc((size_t)PW_DISPLAY_WIDTH * PW_VKBD_PANEL_H * 4);
    assert(frame && expected && panel);
    for (size_t i = 0; i < (size_t)PW_DISPLAY_WIDTH * PW_VKBD_PANEL_H; ++i) panel[i] = 0xff000000u | (uint32_t)(i * 40503u);
    check(frame, expected, panel, 0, PW_VKBD_PANEL_Y, PW_DISPLAY_WIDTH, PW_VKBD_PANEL_H);   /* the keyboard's, 4x4 blocks */
    check(frame, expected, panel, 3, 5, 101, 37);                                         /* a pixel at a time */
    check(frame, expected, panel, 1900, 1068, 64, 64);                                    /* clipped */
    /* the shadow streamed out (a re-show): byte for byte */
    for (size_t i = 0; i < TILED_BYTES; ++i) expected[i] = (uint8_t)(i * 2654435761u >> 9);
    pw_display_stream_copy(frame, expected, TILED_BYTES);
    _mm_sfence();
    assert(!memcmp(frame, expected, TILED_BYTES));
    /* off the screen or without pixels: nothing written */
    for (size_t i = 0; i < TILED_BYTES; ++i) frame[i] = expected[i] = (uint8_t)i;
    pw_display_overlay_blit(frame, panel, PW_DISPLAY_WIDTH, 0, 4, 4);
    pw_display_overlay_blit(frame, NULL, 0, 0, 4, 4);
    assert(!memcmp(frame, expected, TILED_BYTES));
    /* The see-through panel (pw_display_overlay_compose) over a frame of one colour: where the panel is clear, the frame
     * (its blur is the same colour) exactly; an opaque pixel, itself; a half one, itself plus the frame's colour times
     * (255 - its alpha) / 255; the margins only brighter (bubbles, some showing); nothing outside the rectangle. */
    const uint32_t colour = 0xff336699u;
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) memcpy(expected + pw_display_tiled(x, y), &colour, 4);
    for (size_t i = 0; i < TILED_BYTES; ++i) frame[i] = (uint8_t)(i * 2654435761u >> 11);
    uint8_t *before = aligned_alloc(65536, TILED_BYTES);
    assert(before);
    memcpy(before, frame, TILED_BYTES);
    memset(panel, 0, (size_t)PW_DISPLAY_WIDTH * PW_VKBD_PANEL_H * 4);
    panel[100 * PW_DISPLAY_WIDTH + 500] = 0xff112233u;
    panel[200 * PW_DISPLAY_WIDTH + 600] = 0x80404040u;
    uint32_t *scratch = malloc(((size_t)(PW_DISPLAY_WIDTH / 8 + 2) * (PW_DISPLAY_HEIGHT / 8 + 2) * 2 +
                                (size_t)(PW_DISPLAY_WIDTH / 4) * (PW_DISPLAY_HEIGHT / 4)) * 4);
    assert(scratch);
    pw_display_overlay_compose(frame, expected, panel, 0, PW_VKBD_PANEL_Y, PW_DISPLAY_WIDTH, PW_VKBD_PANEL_H, 5.0, scratch, 1, -1);
    unsigned bubbled = 0;
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) {
            uint32_t got, was;
            memcpy(&got, frame + pw_display_tiled(x, y), 4);
            memcpy(&was, before + pw_display_tiled(x, y), 4);
            if (y < PW_VKBD_PANEL_Y) { assert(got == was); continue; }
            const int margin = x < PW_OVERLAY_MARGIN || x >= PW_DISPLAY_WIDTH - PW_OVERLAY_MARGIN;
            if (x == 500 && y == PW_VKBD_PANEL_Y + 100) assert(got == 0xff112233u);
            else if (x == 600 && y == PW_VKBD_PANEL_Y + 200) {
                for (int shift = 0; shift < 24; shift += 8) {
                    const unsigned want = 0x40 + ((colour >> shift & 0xff) * 127 + 127) / 255;
                    assert(abs((int)(got >> shift & 0xff) - (int)want) <= 1);
                }
            } else if (!margin) assert(got == colour);
            else {
                for (int shift = 0; shift < 24; shift += 8) assert((got >> shift & 0xff) >= (colour >> shift & 0xff));
                bubbled += got != colour;
            }
        }
    assert(bubbled > 100);
    /* The control overlay's panel down the right (x 1032, 888 x 1080, src/pw_overlay.h): clear, it shows the frame's colour
     * but in its own margins (its left 165 px and its right 165 px), where the bubbles rise; nothing left of it changes */
    {
        enum { RX = 1032, RW = 888 };
        uint32_t *right = calloc((size_t)RW * PW_DISPLAY_HEIGHT, 4), *own = malloc(((size_t)(RW / 8 + 2) *
            (PW_DISPLAY_HEIGHT / 8 + 2) * 2 + (size_t)(RW / 4) * (PW_DISPLAY_HEIGHT / 4)) * 4);   /* the keyboard's blur stays */
        uint8_t *work = aligned_alloc(65536, TILED_BYTES);
        assert(right && own && work);
        memcpy(work, before, TILED_BYTES);
        unsigned rising = 0;
        for (double t = 0; t < 12; t += 1.5) {
            pw_display_overlay_compose(work, expected, right, RX, 0, RW, PW_DISPLAY_HEIGHT, t, own, 1, -1);
            for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
                for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) {
                    uint32_t got, was;
                    memcpy(&got, work + pw_display_tiled(x, y), 4);
                    memcpy(&was, before + pw_display_tiled(x, y), 4);
                    if (x < RX) { assert(got == was); continue; }
                    const int margin = x - RX < PW_OVERLAY_MARGIN || x >= PW_DISPLAY_WIDTH - PW_OVERLAY_MARGIN;
                    if (!margin) assert(got == colour);
                    else rising += got != colour;
                }
        }
        assert(rising > 500);
        free(right), free(own), free(work);
    }
    /* the blur kept for a second frame (fresh_blur 0) gives the same frame the same pixels */
    uint8_t *again = aligned_alloc(65536, TILED_BYTES);
    assert(again);
    pw_display_overlay_compose(again, expected, panel, 0, PW_VKBD_PANEL_Y, PW_DISPLAY_WIDTH, PW_VKBD_PANEL_H, 5.0, scratch, 0, -1);
    for (unsigned y = PW_VKBD_PANEL_Y; y < PW_DISPLAY_HEIGHT; ++y)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x)
            assert(!memcmp(again + pw_display_tiled(x, y), frame + pw_display_tiled(x, y), 4));
    /* the glass sweep half way (0.9 s of 1.8): only brighter, only near its band's middle, more on opaque glass; none outside
     * the margins is darker; an opaque white-ish key and the clear slab both lit at the band's middle */
    for (unsigned i = 0; i < 64; ++i) panel[300 * PW_DISPLAY_WIDTH + 800 + i] = 0xf0606060u;   /* a strip of key glass */
    pw_display_overlay_compose(again, expected, panel, 0, PW_VKBD_PANEL_Y, PW_DISPLAY_WIDTH, PW_VKBD_PANEL_H, 5.0, scratch, 1, 0.9);
    pw_display_overlay_compose(frame, expected, panel, 0, PW_VKBD_PANEL_Y, PW_DISPLAY_WIDTH, PW_VKBD_PANEL_H, 5.0, scratch, 1, -1);
    unsigned lit = 0;
    for (unsigned y = PW_VKBD_PANEL_Y; y < PW_DISPLAY_HEIGHT; ++y)
        for (unsigned x = PW_OVERLAY_MARGIN; x < PW_DISPLAY_WIDTH - PW_OVERLAY_MARGIN; ++x) {
            uint32_t with, without;
            memcpy(&with, again + pw_display_tiled(x, y), 4);
            memcpy(&without, frame + pw_display_tiled(x, y), 4);
            const float diag = (float)x + (float)(y - PW_VKBD_PANEL_Y) * 0.45f, middle = (PW_DISPLAY_WIDTH - 0) / 2.0f;
            for (int shift = 0; shift < 24; shift += 8) assert((with >> shift & 0xff) >= (without >> shift & 0xff));
            if (with != without) {
                ++lit;
                assert(diag > middle - 420 && diag < middle + 420);   /* 3.5 band widths of 110 px around 960 */
            }
        }
    assert(lit > 10000);
    {
        uint32_t key, slab;   /* on the band's middle (x + 0.45 row near 960): the key strip at row 300, the slab at row 280 */
        memcpy(&key, again + pw_display_tiled(830, PW_VKBD_PANEL_Y + 300), 4);
        memcpy(&slab, again + pw_display_tiled(830, PW_VKBD_PANEL_Y + 280), 4);
        uint32_t key0, slab0;
        memcpy(&key0, frame + pw_display_tiled(830, PW_VKBD_PANEL_Y + 300), 4);
        memcpy(&slab0, frame + pw_display_tiled(830, PW_VKBD_PANEL_Y + 280), 4);
        const int key_gain = (int)(key & 0xff) - (int)(key0 & 0xff), slab_gain = (int)(slab & 0xff) - (int)(slab0 & 0xff);
        /* both lit; the glass relatively more (0.26 of the way to white against the slab's 0.10) */
        assert(key_gain > 0 && slab_gain > 0);
        assert(key_gain * 255 / (255 - (int)(key0 & 0xff)) > slab_gain * 255 / (255 - (int)(slab0 & 0xff)));
    }
    /* The panel's work spread over frames (the 120 Hz budget): over a still frame, four bands one after the other give the
     * whole blend byte for byte, each band writes only its rows, and show puts the cache out as compose does */
    {
        enum { RX = 1032, RW = 888, RH = 1080 };
        uint32_t *right = malloc((size_t)RW * RH * 4), *own = malloc(((size_t)(RW / 8 + 2) * (RH / 8 + 2) * 2 + (size_t)(RW / 4) * (RH / 4)) * 4);
        uint8_t *whole = aligned_alloc(65536, TILED_BYTES), *banded = aligned_alloc(65536, TILED_BYTES);
        uint8_t *shown = aligned_alloc(65536, TILED_BYTES), *composed = aligned_alloc(65536, TILED_BYTES);
        assert(right && own && whole && banded && shown && composed);
        for (size_t i = 0; i < (size_t)RW * RH; ++i) {
            const unsigned a = (unsigned)(i * 13 % 256);
            right[i] = a << 24 | (a / 2) << 16 | (a / 3) << 8 | (a / 4);
        }
        memset(whole, 0, TILED_BYTES), memset(banded, 0, TILED_BYTES);
        pw_display_overlay_blend(whole, before, right, RX, 0, RW, RH, own, 1, 0, 1);
        for (unsigned band = 0; band < 4; ++band) {
            uint8_t *was = malloc(TILED_BYTES);
            assert(was);
            memcpy(was, banded, TILED_BYTES);
            pw_display_overlay_blend(banded, before, right, RX, 0, RW, RH, own, 1, band, 4);
            for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
                for (unsigned x = 0; x < PW_DISPLAY_WIDTH; x += 4) {
                    const size_t at = pw_display_tiled(x, y);
                    const int in_band = x >= RX && y >= RH / 4 * band / 4 * 4 && y < RH / 4 * (band + 1) / 4 * 4;
                    if (!in_band) assert(!memcmp(banded + at, was + at, 16));
                }
            free(was);
        }
        assert(!memcmp(whole, banded, TILED_BYTES));
        memcpy(shown, before, TILED_BYTES), memcpy(composed, before, TILED_BYTES);
        pw_display_overlay_show(shown, whole, right, RX, 0, RW, RH, 7.0, 0.9);
        pw_display_overlay_compose(composed, before, right, RX, 0, RW, RH, 7.0, own, 0, 0.9);
        for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
            for (unsigned x = RX; x < PW_DISPLAY_WIDTH; x += 4)
                assert(!memcmp(shown + pw_display_tiled(x, y), composed + pw_display_tiled(x, y), 16));
        free(right), free(own), free(whole), free(banded), free(shown), free(composed);
    }
    /* The HUD's small panel (pw_display_hud_compose): inside its rectangle each pixel is panel + frame * (255 - alpha), the
     * frame read from a tiled shadow or from the presented RGBA picture (scaled from 960x540), and nothing else changes */
    {
        enum { X = 16, Y = 16, HW = 384, HH = 240 };
        uint32_t *hud = malloc((size_t)HW * HH * 4), *picture = malloc((size_t)960 * 540 * 4);
        uint8_t *shadow = aligned_alloc(65536, TILED_BYTES);
        assert(hud && picture && shadow);
        for (size_t i = 0; i < (size_t)HW * HH; ++i) {
            const unsigned a = (unsigned)(i * 7 % 256), c = a * (unsigned)(i % 3) / 2;   /* premultiplied: no channel above alpha */
            hud[i] = a << 24 | c << 16 | (a / 2) << 8 | (a - c / 2);
        }
        for (size_t i = 0; i < (size_t)960 * 540; ++i) picture[i] = 0xff000000u | (uint32_t)(i * 2246822519u >> 8);
        for (size_t i = 0; i < TILED_BYTES; ++i) shadow[i] = (uint8_t)(i * 40503u >> 7);
        for (int from_shadow = 0; from_shadow < 2; ++from_shadow) {
            for (size_t i = 0; i < TILED_BYTES; ++i) frame[i] = (uint8_t)(i * 2654435761u >> 11);
            memcpy(expected, frame, TILED_BYTES);
            pw_display_hud_compose(frame, hud, X, Y, HW, HH, from_shadow ? shadow : NULL, picture, 960, 540, 960 * 4, 0);
            for (unsigned j = 0; j < HH; ++j)
                for (unsigned i = 0; i < HW; ++i) {
                    uint32_t under, got, p = hud[(size_t)j * HW + i];
                    if (from_shadow) memcpy(&under, shadow + pw_display_tiled(X + i, Y + j), 4);
                    else {
                        const uint32_t c = picture[(size_t)((Y + j) / 2) * 960 + (X + i) / 2];
                        under = (c & 0xff00ff00u) | (c & 0xff) << 16 | (c >> 16 & 0xff);
                    }
                    uint32_t want = 0xff000000u;
                    for (int shift = 0; shift < 24; shift += 8) {
                        const unsigned x = (under >> shift & 0xff) * (255 - (p >> 24)) + 128;
                        want |= ((p >> shift & 0xff) + ((x + (x >> 8)) >> 8)) << shift;
                    }
                    memcpy(&got, frame + pw_display_tiled(X + i, Y + j), 4);
                    assert(got == want);
                    memcpy(expected + pw_display_tiled(X + i, Y + j), &got, 4);
                }
            assert(!memcmp(frame, expected, TILED_BYTES));
        }
        /* unaligned, off the screen or without a frame under it: nothing written */
        memcpy(expected, frame, TILED_BYTES);
        pw_display_hud_compose(frame, hud, 2, Y, HW, HH, shadow, NULL, 0, 0, 0, 1);
        pw_display_hud_compose(frame, hud, 1600, Y, HW, HH, shadow, NULL, 0, 0, 0, 1);
        pw_display_hud_compose(frame, hud, X, Y, HW, HH, NULL, NULL, 0, 0, 0, 1);
        assert(!memcmp(frame, expected, TILED_BYTES));
        free(hud), free(picture), free(shadow);
    }
    free(again);
    free(before);
    free(scratch);
    free(frame);
    free(expected);
    free(panel);
    puts("pw_display_overlay: ok");
    return 0;
}
