/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A model of the buffer ownership in Mesa's u_vbuf_draw_vbo (platform/graphics/mesa/0001): a stream uploader that
 * retires its buffer when it is full and hands the retired buffer to the caller, and a multi-draw loop that releases
 * what it was given either once after the last draw (what Mesa did) or after each draw (the patch). It is a model, not
 * Mesa: tests/test_pw_u_vbuf.py checks that the patch has the per-draw release, and this shows what the difference
 * is, in buffers that stay referenced. */
#include <stdio.h>
#include <stdlib.h>

enum { CAPACITY = 4, MAX_ATTRIBS = 32 };
typedef struct { int refs; int used; } Buffer;
static unsigned created, destroyed;

static Buffer *buffer_new(void) { Buffer *b = calloc(1, sizeof(*b)); b->refs = 1; created++; return b; }
static void buffer_release(Buffer *b)
{
    if (b && --b->refs == 0) { destroyed++; free(b); }
}

/* u_upload_data: room in the current buffer, or retire it (the caller gets that reference) and start another. */
static Buffer *uploader_current;
static void upload(Buffer **retired)
{
    *retired = NULL;
    if (!uploader_current) uploader_current = buffer_new();
    if (uploader_current->used == CAPACITY) {
        *retired = uploader_current;   /* the uploader's reference passes to the caller */
        uploader_current = buffer_new();
    }
    uploader_current->used++;
}

/* u_vbuf_upload_buffers: writes the array from its first element and says how many it set. */
static void upload_buffers(unsigned uploads, unsigned *release_count, Buffer **releasebufs)
{
    unsigned rcount = 0;
    for (unsigned i = 0; i < uploads; i++) {
        upload(&releasebufs[rcount]);
        if (releasebufs[rcount]) rcount++;
    }
    *release_count = rcount;
}

static unsigned multidraw(unsigned draws, unsigned uploads, int per_draw)
{
    Buffer *releasebufs[MAX_ATTRIBS];
    unsigned release_count = 0;
    for (unsigned d = 0; d < draws; d++) {
        upload_buffers(uploads, &release_count, releasebufs);
        /* ... the driver binds and draws here ... */
        if (per_draw) {
            for (unsigned i = 0; i < release_count; i++) buffer_release(releasebufs[i]);
            release_count = 0;
        }
    }
    for (unsigned i = 0; i < release_count; i++) buffer_release(releasebufs[i]);   /* the out: label */
    buffer_release(uploader_current);   /* the uploader itself, at the end of the context */
    uploader_current = NULL;
    return created - destroyed;
}

int main(void)
{
    int bad = 0;
    for (unsigned draws = 1; draws <= 64; draws *= 2)
        for (unsigned uploads = 1; uploads <= 8; uploads++) {
            created = destroyed = 0;
            unsigned leaked_after = multidraw(draws, uploads, 0);
            created = destroyed = 0;
            unsigned leaked_per_draw = multidraw(draws, uploads, 1);
            if (leaked_per_draw) { printf("%u draws x %u uploads: per-draw release leaks %u\n", draws, uploads, leaked_per_draw); bad = 1; }
            /* once a draw retires a buffer, the release after the last draw is the only one: the ones before it leak */
            if (draws >= 16 && uploads >= 4 && !leaked_after) { printf("%u x %u: the model shows no leak to fix\n", draws, uploads); bad = 1; }
        }
    if (!bad) puts("pw u_vbuf model passed");
    return bad;
}
