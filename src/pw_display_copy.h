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
enum {
    PW_DISPLAY_WIDTH = 1920,
    PW_DISPLAY_HEIGHT = 1080,
    PW_DISPLAY_BLOCKS = 15,
    PW_DISPLAY_MAX_WIDTH = 3840,
    PW_DISPLAY_MAX_HEIGHT = 2160,
    PW_DISPLAY_MAX_BLOCKS = 30,
    PW_DISPLAY_MAX_BLOCK_ROWS = 17
};

/* The byte offset of pixel (x, y) in a 64KB_R_X-tiled buffer of `blocks` 128-pixel tile columns. */
static inline size_t pw_display_tiled_stride(unsigned x, unsigned y, unsigned blocks)
{
    const unsigned offset = ((y << 4) & 0x70) ^ ((y << 5) & 0xf00) ^ ((y << 9) & 0x1000) ^ ((y << 8) & 0x4000) ^
                            ((x << 2) & 0xc) ^ ((x << 5) & 0x380) ^ ((x << 4) & 0x400) ^ ((x << 6) & 0x800) ^
                            ((x << 9) & 0xa000);
    return ((size_t)((y >> 7) * blocks + (x >> 7)) << 16) + offset;
}

/* The byte offset of pixel (x, y) in the 1080p tiled buffer (display.cpp's tiled()). */
static inline size_t pw_display_tiled(unsigned x, unsigned y)
{
    return pw_display_tiled_stride(x, y, PW_DISPLAY_BLOCKS);
}

/* pixels: dst_w x dst_h (multiples of 4), pitch bytes a row, any alignment; buffer: tiled buffer of `blocks` columns,
 * 64-byte aligned. bgra 0 swaps red and blue channels (RGBA in, display's BGRA out). */
static inline void pw_display_copy_lines_wh(uint8_t *buffer, unsigned dst_w, unsigned dst_h, unsigned blocks,
                                            const uint8_t *pixels, size_t pitch, int bgra)
{
    const __m128i keep = _mm_set1_epi32((int)0xff00ff00u), low = _mm_set1_epi32(0xff);
    for (unsigned y = 0; y < dst_h; y += 4)
        for (unsigned x = 0; x < dst_w; x += 4) {
            const uint8_t *row = pixels + (size_t)y * pitch + (size_t)x * 4;
            __m128i *line = (__m128i *)(void *)(buffer + pw_display_tiled_stride(x, y, blocks));
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

static inline void pw_display_copy_lines(uint8_t *buffer, const uint8_t *pixels, size_t pitch, int bgra)
{
    pw_display_copy_lines_wh(buffer, PW_DISPLAY_WIDTH, PW_DISPLAY_HEIGHT, PW_DISPLAY_BLOCKS, pixels, pitch, bgra);
}

/* A frame of src_w x src_h scaled nearest-neighbour to dst_w x dst_h (up to 3840x2160) into rows (dst_w*4 bytes a row,
 * cached caller memory). pw_display_copy_lines_wh then streams them to VideoOut. */
static inline void pw_display_scale_rows_wh(uint8_t *rows, unsigned dst_w, unsigned dst_h, const uint8_t *pixels,
                                            unsigned width, unsigned height, size_t pitch)
{
    uint16_t source_x[PW_DISPLAY_MAX_WIDTH];
    if (!dst_w || dst_w > PW_DISPLAY_MAX_WIDTH || !dst_h || dst_h > PW_DISPLAY_MAX_HEIGHT) return;
    for (unsigned x = 0; x < dst_w; ++x) source_x[x] = (uint16_t)((uint64_t)x * width / dst_w);
    uint32_t *out = (uint32_t *)(void *)rows;
    for (unsigned y = 0, previous = ~0u; y < dst_h; ++y, out += dst_w) {
        const unsigned sy = (unsigned)((uint64_t)y * height / dst_h);
        if (sy == previous) { memcpy(out, out - dst_w, (size_t)dst_w * 4); continue; }
        previous = sy;
        const uint8_t *row = pixels + (size_t)sy * pitch;
        for (unsigned x = 0; x < dst_w; ++x) memcpy(&out[x], row + (size_t)source_x[x] * 4, 4);
    }
}

static inline void pw_display_scale_rows(uint8_t *rows, const uint8_t *pixels, unsigned width, unsigned height, size_t pitch)
{
    pw_display_scale_rows_wh(rows, PW_DISPLAY_WIDTH, PW_DISPLAY_HEIGHT, pixels, width, height, pitch);
}
#endif
