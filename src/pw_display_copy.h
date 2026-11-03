/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_DISPLAY_COPY_H
#define PW_DISPLAY_COPY_H
#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* The broker's copy of a whole 1920x1080 frame into VideoOut's 64KB_R_X-tiled buffer, a 64-byte line at a time
 * (graphics patch 0022, display.cpp, with PROTON_GPU_FAST_COPY=1; a frame of another size is scaled into rows first,
 * pw_display_scale_rows).
 *
 * In the tiling, offset bits 2-3 are x bits 0-1 and bits 4-5 are y bits 0-1, and nothing else reaches bits 0-5: a
 * 4x4 block of pixels whose corner is a multiple of 4 is one aligned 64-byte line, its rows 16 bytes each in order. So
 * four source rows are read side by side and each line is written whole with streaming stores, which bypass the cache:
 * the display reads memory, and the sfence at the end orders the stores before the flip, as the per-line clflushopt
 * of the pixel loop did. The pixel loop read one pixel at a time through two tables in the tiles' order and took
 * 4.3-7.7 ms on the console in a Direct3D 12 game's presents (2026-10-17, present_avg 5.8 ms; record removed from the tree); on a
 * host this is 2-3.4 times faster than that loop. SSE2 only. */
enum { PW_DISPLAY_WIDTH = 1920, PW_DISPLAY_HEIGHT = 1080, PW_DISPLAY_BLOCKS = 15 };

/* The byte offset of pixel (x, y) in the tiled buffer (display.cpp's tiled()). */
static inline size_t pw_display_tiled(unsigned x, unsigned y)
{
    const unsigned offset = ((y << 4) & 0x70) ^ ((y << 5) & 0xf00) ^ ((y << 9) & 0x1000) ^ ((y << 8) & 0x4000) ^
                            ((x << 2) & 0xc) ^ ((x << 5) & 0x380) ^ ((x << 4) & 0x400) ^ ((x << 6) & 0x800) ^
                            ((x << 9) & 0xa000);
    return ((size_t)((y >> 7) * PW_DISPLAY_BLOCKS + (x >> 7)) << 16) + offset;
}

/* pixels: 1920x1080, pitch bytes a row, any alignment; buffer: the tiled buffer, 64-byte aligned. bgra 0 swaps the red
 * and blue channels (RGBA in, the display's BGRA out), as the pixel loop does. */
static inline void pw_display_copy_lines(uint8_t *buffer, const uint8_t *pixels, size_t pitch, int bgra)
{
    const __m128i keep = _mm_set1_epi32((int)0xff00ff00u), low = _mm_set1_epi32(0xff);
    for (unsigned y = 0; y < PW_DISPLAY_HEIGHT; y += 4)
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; x += 4) {
            const uint8_t *row = pixels + (size_t)y * pitch + (size_t)x * 4;
            __m128i *line = (__m128i *)(void *)(buffer + pw_display_tiled(x, y));
            for (unsigned r = 0; r < 4; ++r) {
                __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(row + r * pitch));
                if (!bgra)
                    v = _mm_or_si128(_mm_and_si128(v, keep),
                                     _mm_or_si128(_mm_slli_epi32(_mm_and_si128(v, low), 16),
                                                  _mm_and_si128(_mm_srli_epi32(v, 16), low)));
                _mm_stream_si128(line + r, v);
            }
        }
    _mm_sfence();
}

/* A frame of any other size scaled to the screen's 1920x1080, nearest neighbour exactly as display.cpp's pixel loop
 * scales it (the source pixel x*width/1920, y*height/1080), into rows: 1920x1080, PW_DISPLAY_WIDTH*4 bytes a row,
 * cached memory of the caller's. pw_display_copy_lines then takes them to the display. The pixel loop read each pixel
 * through two tables in the tiles' order and took 5.34-5.41 ms a 1272x680 frame on the standard PS5, every present
 * (v71-fix-gate-base-2026-11-02, gpu-display copy_us), against 0.61-0.65 ms for an unscaled frame by the line copy. A
 * row the scaling repeats is copied whole from the one before. width at most 2176, so x*width fits. */
static inline void pw_display_scale_rows(uint8_t *rows, const uint8_t *pixels, unsigned width, unsigned height, size_t pitch)
{
    uint16_t source_x[PW_DISPLAY_WIDTH];
    for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) source_x[x] = (uint16_t)(x * width / PW_DISPLAY_WIDTH);
    uint32_t *out = (uint32_t *)(void *)rows;
    for (unsigned y = 0, previous = ~0u; y < PW_DISPLAY_HEIGHT; ++y, out += PW_DISPLAY_WIDTH) {
        const unsigned sy = y * height / PW_DISPLAY_HEIGHT;
        if (sy == previous) { memcpy(out, out - PW_DISPLAY_WIDTH, PW_DISPLAY_WIDTH * 4); continue; }
        previous = sy;
        const uint8_t *row = pixels + (size_t)sy * pitch;
        for (unsigned x = 0; x < PW_DISPLAY_WIDTH; ++x) memcpy(&out[x], row + (size_t)source_x[x] * 4, 4);
    }
}
#endif
