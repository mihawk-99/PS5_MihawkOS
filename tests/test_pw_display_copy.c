/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The broker's line copy into the display's tiled buffer (src/pw_display_copy.h) against display.cpp's pixel loop,
 * pixel for pixel, in both channel orders, from a source with a wider pitch and no 16-byte alignment; and a frame of
 * another size scaled into rows (pw_display_scale_rows) then line-copied, against display.cpp's scaling loop, with the
 * time of each on this host. */
#define _POSIX_C_SOURCE 200809L
#include "../src/pw_display_copy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { TILED_BYTES = PW_DISPLAY_BLOCKS * 9 << 16 };

/* display.cpp's loop for an unscaled frame: each pixel to its tiled offset, the channels swapped unless bgra. */
static void pixel_loop(uint8_t *buffer, const uint8_t *pixels, size_t pitch, int bgra)
{
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) {
            uint32_t rgba;
            memcpy(&rgba, pixels + (size_t)y * pitch + (size_t)x * 4, 4);
            const uint32_t out = bgra ? rgba : (rgba & 0xff00ff00u) | ((rgba & 0xff) << 16) | ((rgba >> 16) & 0xff);
            memcpy(buffer + pw_display_tiled(x, y), &out, 4);
        }
}

/* display.cpp's loop for a scaled frame: nearest neighbour through two tables, each tile in memory order. */
static void scale_loop(uint8_t *buffer, const uint8_t *pixels, unsigned w, unsigned h, size_t pitch, int bgra)
{
    static uint16_t source_x[PW_DISPLAY_WIDTH], source_y[PW_DISPLAY_HEIGHT];
    for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) source_x[x] = (uint16_t)(x * w / PW_DISPLAY_WIDTH);
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y) source_y[y] = (uint16_t)(y * h / PW_DISPLAY_HEIGHT);
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; ++y)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) {
            uint32_t rgba;
            memcpy(&rgba, pixels + (size_t)source_y[y] * pitch + (size_t)source_x[x] * 4, 4);
            const uint32_t out = bgra ? rgba : (rgba & 0xff00ff00u) | ((rgba & 0xff) << 16) | ((rgba >> 16) & 0xff);
            memcpy(buffer + pw_display_tiled(x, y), &out, 4);
        }
}

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

int main(void)
{
    /* The layout this relies on: each 4x4 block at a multiple of 4 is one aligned line, its rows in order. */
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; y += 4)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; x += 4) {
            assert(pw_display_tiled(x, y) % 64 == 0);
            for (unsigned j = 0; j < 16; ++j) assert(pw_display_tiled(x + (j & 3), y + (j >> 2)) == pw_display_tiled(x, y) + 4 * j);
        }
    const size_t pitch = PW_DISPLAY_WIDTH * 4 + 196;
    uint8_t *source = malloc(pitch * PW_DISPLAY_HEIGHT + 4), *pixels = source + 4;
    uint8_t *expected = aligned_alloc(65536, TILED_BYTES), *actual = aligned_alloc(65536, TILED_BYTES);
    assert(source && expected && actual);
    for (size_t i = 0; i < pitch * PW_DISPLAY_HEIGHT; ++i) pixels[i] = (uint8_t)(i * 2654435761u >> 13);
    for (int bgra = 0; bgra <= 1; ++bgra) {
        memset(expected, 0x5a, TILED_BYTES);
        memset(actual, 0x5a, TILED_BYTES);
        pixel_loop(expected, pixels, pitch, bgra);
        pw_display_copy_lines(actual, pixels, pitch, bgra);
        /* Every byte, the rows below the frame in the last tile row untouched by both. */
        assert(!memcmp(expected, actual, TILED_BYTES));
    }
    /* Scaled: smaller (the gate's 1272x680, 720p, 4:3, one pixel), larger in a row (2176 wide), from rows padded. */
    static const unsigned sizes[][2] = {{1272, 680}, {1280, 720}, {1024, 768}, {640, 360}, {1, 1}, {2176, 900}, {1920, 1}};
    uint8_t *rows = aligned_alloc(64, PW_DISPLAY_WIDTH * PW_DISPLAY_HEIGHT * 4);
    assert(rows);
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for (int bgra = 0; bgra <= 1; ++bgra) {
            const unsigned w = sizes[s][0], h = sizes[s][1];
            const size_t row_pitch = (size_t)w * 4 + 64;
            assert(row_pitch * h <= pitch * PW_DISPLAY_HEIGHT);
            memset(expected, 0x5a, TILED_BYTES);
            memset(actual, 0x5a, TILED_BYTES);
            const double a = now_ms();
            scale_loop(expected, pixels, w, h, row_pitch, bgra);
            const double b = now_ms();
            pw_display_scale_rows(rows, pixels, w, h, row_pitch);
            pw_display_copy_lines(actual, rows, PW_DISPLAY_WIDTH * 4, bgra);
            const double c = now_ms();
            assert(!memcmp(expected, actual, TILED_BYTES));
            if (s == 0 && bgra) printf("scaled %ux%u: loop %.2f ms, rows and line copy %.2f ms (this host)\n", w, h, b - a, c - b);
        }
    free(rows);
    free(source);
    free(expected);
    free(actual);
    /* Verify 1440p (2560x1440, 20x12 tiles) and 4K (3840x2160, 30x17 tiles) tiled line copy & scaling. */
    static const unsigned modes[][4] = {{2560, 1440, 20, 12}, {3840, 2160, 30, 17}};
    for (unsigned m = 0; m < 2; ++m) {
        const unsigned dw = modes[m][0], dh = modes[m][1], db = modes[m][2], dr = modes[m][3];
        const size_t tb = (size_t)db * dr << 16;
        uint8_t *src = malloc((size_t)dw * dh * 4);
        uint8_t *buf = aligned_alloc(65536, tb);
        uint8_t *sc = aligned_alloc(64, (size_t)dw * dh * 4);
        assert(src && buf && sc);
        for (size_t i = 0; i < (size_t)dw * dh; ++i) {
            uint32_t p = 0xff000000u | (uint32_t)((i * 2654435761u) & 0x00ffffffu);
            memcpy(src + i * 4, &p, 4);
        }
        memset(buf, 0, tb);
        pw_display_copy_lines_wh(buf, dw, dh, db, src, (size_t)dw * 4, 1);
        for (unsigned y = 0; y < dh; y += 97)
            for (unsigned x = 0; x < dw; x += 113) {
                uint32_t got, want;
                memcpy(&got, buf + pw_display_tiled_stride(x, y, db), 4);
                memcpy(&want, src + ((size_t)y * dw + x) * 4, 4);
                assert(got == want);
            }
        pw_display_scale_rows_wh(sc, dw, dh, src, 1920, 1080, 1920 * 4);
        pw_display_copy_lines_wh(buf, dw, dh, db, sc, (size_t)dw * 4, 0);
        {
            uint32_t got, raw;
            memcpy(&got, buf + pw_display_tiled_stride(dw - 1, dh - 1, db), 4);
            const unsigned sx = (unsigned)((uint64_t)(dw - 1) * 1920 / dw);
            const unsigned sy = (unsigned)((uint64_t)(dh - 1) * 1080 / dh);
            memcpy(&raw, src + ((size_t)sy * 1920 + sx) * 4, 4);
            const uint32_t want = (raw & 0xff00ff00u) | ((raw & 0xffu) << 16) | ((raw >> 16) & 0xffu);
            assert(got == want);
        }
        free(sc);
        free(buf);
        free(src);
    }
    puts("test_pw_display_copy: ok");
    return 0;
}
