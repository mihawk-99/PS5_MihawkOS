/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINDOW_PAGE_H
#define PW_WINDOW_PAGE_H
#include <stdint.h>
#include <stddef.h>
/* Payload owns this mapping. The title adopts a descriptor only after matching
 * native PID/HWND/lifetime against a copied server snapshot. Adoption is ACKed
 * before a producer retires its old mapping. Fields except sequence and pixels
 * are immutable. The reader uses its validated copy of metadata, never live
 * producer sizes, and retains the previous image while a write is in progress.
 * 16 KiB native alignment; up to 3840x2160 output plus Wine's 128-pixel surface rounding, no padding in pixel rows. */
enum { PW_WINDOW_PAGE_MAGIC=0x50575731, PW_WINDOW_PAGE_VERSION=2,
       PW_WINDOW_PAGE_PREMULTIPLIED=1,
       PW_WINDOW_PAGE_ALIGN=16384, PW_WINDOW_WIDTH=4096, PW_WINDOW_HEIGHT=2432 };
typedef struct PwWindowPage {
    uint32_t magic, version, bytes, hwnd;
    uint64_t generation;
    uint32_t width, height, stride, sequence;
    int32_t offset_x, offset_y;
    uint32_t flags, reserved[3];
} PwWindowPage;
typedef char PwWindowPageSizeCheck[sizeof(PwWindowPage)==64 ? 1 : -1];
#ifdef __cplusplus
extern "C" {
#endif
size_t pw_window_page_bytes(uint32_t width,uint32_t height);
int pw_window_page_init(PwWindowPage *page,size_t bytes,uint32_t hwnd,uint64_t generation,
                        uint32_t width,uint32_t height,int32_t offset_x,int32_t offset_y);
/* Copy immutable metadata, checking actual mapping extent. */
int pw_window_page_header(const PwWindowPage *page,size_t bytes,PwWindowPage *header);
int pw_window_page_write(PwWindowPage *page,const void *pixels,uint32_t stride);
/* Convert top-down BGRA to premultiplied BGRA once. alpha_mask is zero for
 * ordinary RGB or 0xff000000 for already-premultiplied RGB. opacity is the
 * combined constant factor, 0..255. Optional shape is MSB-first 1bpp coverage.
 * Set immutable PREMULTIPLIED format before adoption. Invalid metadata leaves
 * sequence and previous pixels untouched. */
int pw_window_page_write_alpha(PwWindowPage *page,const void *pixels,uint32_t stride,
                               uint32_t alpha_mask,uint32_t opacity,
                               const void *shape,uint32_t shape_stride,size_t shape_bytes);
/* 1: stable new image, 0: unchanged or a concurrent write, -1: bad arguments.
 * Destination is scratch until 1. The caller swaps it with retained pixels then.
 * Single reader; single serialized writer. No retry/wait in the display thread. */
int pw_window_page_read(const PwWindowPage *page,const PwWindowPage *header,
                        void *scratch,size_t bytes,uint32_t previous,uint32_t *sequence);
#ifdef __cplusplus
}
#endif
#endif
