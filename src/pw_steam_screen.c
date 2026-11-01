/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Steam guide's screens inside its sessions (pw_steam_screen.h). */
#include "pw_steam_screen.h"
#include "pw_loading_art.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ImGui's copy of stb_truetype (public domain / MIT), compiled privately here as native/proton_loading.cpp does. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../external/imgui/imstb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct PwSteamFace {
    stbtt_fontinfo info;
};

PwSteamFace *pw_steam_face_open(const unsigned char *ttf, size_t size)
{
    if (!ttf || size < 12) return NULL;
    PwSteamFace *face = calloc(1, sizeof(*face));
    if (face && !stbtt_InitFont(&face->info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        free(face);
        face = NULL;
    }
    return face;
}

void pw_steam_face_close(PwSteamFace *face)
{
    free(face);
}

/* ---- colours (0xAARRGGBB) and shapes ---- */

enum {
    W = PW_LOADING_WIDTH,
    H = PW_LOADING_HEIGHT,
};
#define INK 0xff13233fu   /* the card's words: 12:1 on the card */
#define MUTED 0xff34465fu /* the body: 8.6:1 */
#define SOFT 0xff53647cu  /* the step line and the notes: 5.6:1 */
#define BLUE 0xff2f6fe0u
#define GREEN 0xff2fae1eu
#define AMBER_INK 0xff5c3d00u

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void blend(uint32_t *canvas, int x, int y, uint32_t color, float alpha)
{
    if (x < 0 || y < 0 || x >= W || y >= H || alpha <= 0.0f) return;
    uint32_t *p = canvas + (size_t)y * W + x;
    *p = pw_loading_blend(*p, color, (unsigned)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f));
}

/* Signed distance to a rounded rectangle. */
static float rounded(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float qx = fabsf(x - cx) - ((x1 - x0) * 0.5f - r), qy = fabsf(y - cy) - ((y1 - y0) * 0.5f - r);
    const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
}

/* A rounded rectangle, its alpha from top to bottom; feather softens its edge (a shadow uses a wide one). */
static void fill_rounded(uint32_t *canvas, float x0, float y0, float x1, float y1, float r, uint32_t color, float top,
                         float bottom, float feather)
{
    for (int y = (int)floorf(y0 - feather); y <= (int)ceilf(y1 + feather); ++y)
        for (int x = (int)floorf(x0 - feather); x <= (int)ceilf(x1 + feather); ++x) {
            const float d = rounded(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r);
            const float cover = clampf(0.5f - d / feather, 0.0f, 1.0f);
            if (cover > 0) blend(canvas, x, y, color, cover * (top + (bottom - top) * clampf((y - y0) / (y1 - y0), 0, 1)));
        }
}

static void ring_rounded(uint32_t *canvas, float x0, float y0, float x1, float y1, float r, uint32_t color, float alpha,
                         float width)
{
    for (int y = (int)y0 - 2; y <= (int)y1 + 2; ++y)
        for (int x = (int)x0 - 2; x <= (int)x1 + 2; ++x) {
            const float d = fabsf(rounded(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r) + width * 0.5f);
            blend(canvas, x, y, color, alpha * clampf(width * 0.5f + 0.5f - d, 0, 1));
        }
}

static void disc(uint32_t *canvas, float cx, float cy, float radius, uint32_t color, float alpha)
{
    for (int y = (int)(cy - radius - 1); y <= (int)(cy + radius + 1); ++y)
        for (int x = (int)(cx - radius - 1); x <= (int)(cx + radius + 1); ++x) {
            const float d = hypotf(x + 0.5f - cx, y + 0.5f - cy) - radius;
            blend(canvas, x, y, color, alpha * clampf(0.5f - d, 0, 1));
        }
}

static void line(uint32_t *canvas, float ax, float ay, float bx, float by, float width, uint32_t color)
{
    const float dx = bx - ax, dy = by - ay, length2 = dx * dx + dy * dy;
    for (int y = (int)(fminf(ay, by) - width); y <= (int)(fmaxf(ay, by) + width); ++y)
        for (int x = (int)(fminf(ax, bx) - width); x <= (int)(fmaxf(ax, bx) + width); ++x) {
            const float px = x + 0.5f - ax, py = y + 0.5f - ay;
            const float t = clampf((px * dx + py * dy) / length2, 0, 1);
            const float d = hypotf(px - t * dx, py - t * dy);
            blend(canvas, x, y, color, clampf(width * 0.5f + 0.5f - d, 0, 1));
        }
}

/* ---- text ---- */

/* The next code point of UTF-8 text at *at, which moves past it; 0xFFFD for a malformed byte. */
static unsigned next_codepoint(const char **at)
{
    const unsigned char *s = (const unsigned char *)*at;
    unsigned c = s[0], n = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (c >= 0x80 && !n) {
        *at += 1;
        return 0xfffd;
    }
    c = n == 0 ? c : n == 1 ? c & 0x1f : n == 2 ? c & 0x0f : c & 0x07;
    for (unsigned i = 1; i <= n; ++i) {
        if ((s[i] & 0xc0) != 0x80) {
            *at += i;
            return 0xfffd;
        }
        c = c << 6 | (s[i] & 0x3f);
    }
    *at += n + 1;
    return c;
}

typedef struct Pen {
    const PwSteamFace *face, *fallback;
    float pixels;
} Pen;

/* The face that draws c: the pen's, else the fallback's, else NULL (it is skipped). */
static const stbtt_fontinfo *face_for(const Pen *pen, unsigned c)
{
    if (pen->face && stbtt_FindGlyphIndex(&pen->face->info, (int)c)) return &pen->face->info;
    if (pen->fallback && stbtt_FindGlyphIndex(&pen->fallback->info, (int)c)) return &pen->fallback->info;
    return NULL;
}

static float advance(const Pen *pen, unsigned c, unsigned next)
{
    const stbtt_fontinfo *info = face_for(pen, c);
    if (!info) return 0;
    const float scale = stbtt_ScaleForPixelHeight(info, pen->pixels);
    int width, bearing;
    stbtt_GetCodepointHMetrics(info, (int)c, &width, &bearing);
    float a = width * scale;
    if (next && info == face_for(pen, next)) a += stbtt_GetCodepointKernAdvance(info, (int)c, (int)next) * scale;
    return a;
}

/* The width of text[0..length). */
static float measure(const Pen *pen, const char *text, size_t length)
{
    float width = 0;
    const char *at = text, *end = text + length;
    while (at < end) {
        const unsigned c = next_codepoint(&at);
        const char *peek = at;
        width += advance(pen, c, peek < end ? next_codepoint(&peek) : 0);
    }
    return width;
}

/* Draws text[0..length) with its left edge at x on the baseline y; returns how many of its characters no face has. */
static int draw(uint32_t *canvas, const Pen *pen, const char *text, size_t length, float x, float y, uint32_t color,
                 float alpha)
{
    static unsigned char glyph[160 * 160];
    int missing = 0;
    const char *at = text, *end = text + length;
    while (at < end) {
        const unsigned c = next_codepoint(&at);
        const char *peek = at;
        const unsigned next = peek < end ? next_codepoint(&peek) : 0;
        const stbtt_fontinfo *info = face_for(pen, c);
        missing += !info && c != ' ';
        if (info && c != ' ') {
            const float scale = stbtt_ScaleForPixelHeight(info, pen->pixels), shift = x - floorf(x);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBoxSubpixel(info, (int)c, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
            const int w = x1 - x0, h = y1 - y0;
            if (w > 0 && h > 0 && w <= 160 && h <= 160) {
                stbtt_MakeCodepointBitmapSubpixel(info, glyph, w, h, w, scale, scale, shift, 0, (int)c);
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        if (glyph[j * w + i])
                            blend(canvas, (int)floorf(x) + x0 + i, (int)y + y0 + j, color, alpha * glyph[j * w + i] / 255.0f);
            }
        }
        x += advance(pen, c, next);
    }
    return missing;
}

static int cjk(unsigned c)
{
    return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef) ||
           (c >= 0x3000 && c <= 0x303f);
}

/* Closing marks a line never starts with (Chinese and Japanese). */
static int no_break_before(unsigned c)
{
    static const unsigned marks[] = {0x3001, 0x3002, 0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff01, 0xff1f, 0xff09, 0x300d,
                                     0x300f, 0x3009, 0x300b, 0x3011, 0x30fc, 0x2026, 0x30fb, 0xff5e, 0x3005};
    for (size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); ++i)
        if (c == marks[i]) return 1;
    return c == ',' || c == '.' || c == ':' || c == ';' || c == '!' || c == '?' || c == ')';
}

typedef struct Lines {
    int count;
    const char *start[12];
    size_t length[12];
} Lines;

/* text broken into lines no wider than width: at spaces, and between Chinese and Japanese characters (not before a
 * closing mark). A word longer than the line is cut where it must be. */
static void wrap(const Pen *pen, const char *text, float width, Lines *out)
{
    out->count = 0;
    const char *line = text;
    while (*line && out->count < 12) {
        while (*line == ' ') ++line;
        const char *at = line, *good = NULL, *last = line;
        unsigned previous = 0;
        while (*at) {
            const char *here = at;
            const unsigned c = next_codepoint(&at);
            /* a break may come before here */
            if (here > line && ((c == ' ') || (cjk(c) && !no_break_before(c)) || (cjk(previous) && !no_break_before(c))))
                good = here;
            if (c != ' ' && measure(pen, line, (size_t)(at - line)) > width) {
                at = here;
                break;
            }
            last = at;
            previous = c;
        }
        const char *stop = !*at ? at : good ? good : last;
        if (stop == line) next_codepoint(&stop);   /* not even one character fits: it goes alone */
        size_t length = (size_t)(stop - line);
        while (length && line[length - 1] == ' ') --length;
        out->start[out->count] = line;
        out->length[out->count] = length;
        ++out->count;
        line = stop;
    }
}

/* ---- the screens ---- */

typedef struct Ctx {
    uint32_t *canvas;
    const PwSteamFaces *faces;
    const PwSetupLanguage *language;
    PwSteamScreenReport *report;
} Ctx;

static Pen pen(const Ctx *ctx, int semibold, float pixels)
{
    Pen p = {semibold ? ctx->faces->semibold : ctx->faces->regular, ctx->faces->latin, pixels};
    return p;
}

static const char *say(const Ctx *ctx, PwSetupText key) { return pw_setup_text(ctx->language, key); }

static void reached(const Ctx *ctx, float bottom, int cut, int missing)
{
    if (!ctx->report) return;
    if ((int)bottom > ctx->report->bottom) ctx->report->bottom = (int)bottom;
    ctx->report->cut += cut;
    ctx->report->missing += missing;
}

/* One line at x on the baseline y, made smaller (to 70 %) if it is wider than width; returns its width. */
static float text_line(const Ctx *ctx, int semibold, float pixels, const char *text, float x, float y, float width,
                       uint32_t color)
{
    Pen p = pen(ctx, semibold, pixels);
    float w = measure(&p, text, strlen(text));
    if (w > width) {
        p.pixels = fmaxf(pixels * 0.7f, pixels * width / w);
        w = measure(&p, text, strlen(text));
    }
    const int missing = draw(ctx->canvas, &p, text, strlen(text), x, y, color, 1.0f);
    reached(ctx, y + p.pixels * 0.3f, w > width + 0.5f, missing);
    return w;
}

/* A paragraph from the baseline y, at most max lines; returns the baseline after it. */
static float paragraph(const Ctx *ctx, float pixels, const char *text, float x, float y, float width, int max,
                       uint32_t color)
{
    const Pen p = pen(ctx, 0, pixels);
    Lines lines;
    wrap(&p, text, width, &lines);
    const int shown = lines.count < max ? lines.count : max;
    int missing = 0;
    for (int i = 0; i < shown; ++i)
        missing += draw(ctx->canvas, &p, lines.start[i], lines.length[i], x, y + i * pixels * 1.45f, color, 1.0f);
    reached(ctx, y + (shown - 1) * pixels * 1.45f + pixels * 0.3f, lines.count - shown, missing);
    return y + shown * pixels * 1.45f;
}

/* CROSS and its word, its pill ending at right, on the baseline y. */
static void cross_hint(const Ctx *ctx, PwSetupText key, float right, float y)
{
    const char *word = say(ctx, key);
    const Pen p = pen(ctx, 1, 27);
    const float w = measure(&p, word, strlen(word));
    right -= 22;
    const float x = right - w, glyph = x - 34;
    fill_rounded(ctx->canvas, glyph - 30, y - 38, right + 22, y + 16, 27, 0xffffffffu, 0.75f, 0.75f, 1.0f);
    ring_rounded(ctx->canvas, glyph - 30, y - 38, right + 22, y + 16, 27, 0xff9fb4d2u, 0.8f, 1.5f);
    disc(ctx->canvas, glyph, y - 11, 19, BLUE, 1.0f);
    line(ctx->canvas, glyph - 7, y - 18, glyph + 7, y - 4, 3.2f, 0xffffffffu);
    line(ctx->canvas, glyph + 7, y - 18, glyph - 7, y - 4, 3.2f, 0xffffffffu);
    reached(ctx, y + 16, 0, draw(ctx->canvas, &p, word, strlen(word), x, y, INK, 1.0f));
}

static void step_line(const Ctx *ctx, int step)
{
    char a[4], b[4], out[96];
    snprintf(a, sizeof(a), "%d", step);
    snprintf(b, sizeof(b), "%d", 4);
    const char *args[] = {a, b};
    pw_setup_fill(out, sizeof(out), say(ctx, PW_TEXT_STEP_OF), args, 2);
    char line_text[128];
    snprintf(line_text, sizeof(line_text), "Steam · %s", out);
    text_line(ctx, 0, 26, line_text, PW_STEAM_TEXT_X0, PW_STEAM_CARD_Y0 + 92, PW_STEAM_TEXT_X1 - PW_STEAM_TEXT_X0, SOFT);
}

/* "212 MB downloaded · 2:14 elapsed", or the elapsed time alone. */
static void progress_line(const Ctx *ctx, const PwSteamScreenState *state, int megabytes)
{
    char clock[16], elapsed[96], mb[24], downloaded[96], out[200];
    snprintf(clock, sizeof(clock), "%u:%02u", state->seconds / 60, state->seconds % 60);
    const char *clock_args[] = {clock};
    pw_setup_fill(elapsed, sizeof(elapsed), say(ctx, PW_TEXT_STEAM_ELAPSED), clock_args, 1);
    if (megabytes && state->bytes >= 1000000ull) {
        snprintf(mb, sizeof(mb), "%llu", state->bytes / 1000000ull);
        const char *mb_args[] = {mb};
        pw_setup_fill(downloaded, sizeof(downloaded), say(ctx, PW_TEXT_STEAM_DOWNLOADED), mb_args, 1);
        snprintf(out, sizeof(out), "%s · %s", downloaded, elapsed);
    } else {
        snprintf(out, sizeof(out), "%s", elapsed);
    }
    text_line(ctx, 0, 29, out, PW_STEAM_TEXT_X0, PW_STEAM_TUBE_Y1 + 54, PW_STEAM_TEXT_X1 - PW_STEAM_TEXT_X0, INK);
}

static void tube_track(const Ctx *ctx)
{
    fill_rounded(ctx->canvas, PW_STEAM_TEXT_X0 - 2, PW_STEAM_TUBE_Y0 - 2, PW_STEAM_TEXT_X1 + 2, PW_STEAM_TUBE_Y1 + 2,
                 (PW_STEAM_TUBE_Y1 - PW_STEAM_TUBE_Y0) * 0.5f + 2, 0xff0a2a6au, 0.30f, 0.22f, 1.0f);
}

/* The phone screen's picture: a phone showing the Steam Guard shield, three dots, and a code to scan. */
static void phone_and_code(const Ctx *ctx)
{
    uint32_t *canvas = ctx->canvas;
    const float px0 = 780, py0 = 572, px1 = 880, py1 = 748;
    fill_rounded(canvas, px0, py0 + 8, px1, py1 + 8, 18, 0xff04225au, 0.18f, 0.18f, 14.0f);
    fill_rounded(canvas, px0, py0, px1, py1, 18, INK, 1.0f, 1.0f, 1.0f);
    fill_rounded(canvas, px0 + 7, py0 + 16, px1 - 7, py1 - 16, 8, 0xffffffffu, 1.0f, 1.0f, 1.0f);
    /* the shield: a rounded top, sides that meet in a point */
    const float cx = (px0 + px1) * 0.5f, top = py0 + 52, mid = py0 + 92, tip = py0 + 128;
    for (int y = (int)top; y <= (int)tip; ++y)
        for (int x = (int)cx - 34; x <= (int)cx + 34; ++x) {
            const float half = y < mid ? 30.0f : 30.0f * (tip - y) / (tip - mid);
            const float d = fabsf(x + 0.5f - cx) - half;
            const float edge = y < top + 6 ? clampf((y + 0.5f - top) / 6.0f, 0, 1) : 1.0f;
            blend(canvas, x, y, BLUE, clampf(0.5f - d, 0, 1) * edge);
        }
    line(canvas, cx - 12, mid - 6, cx - 3, mid + 4, 4.5f, 0xffffffffu);
    line(canvas, cx - 3, mid + 4, cx + 13, mid - 14, 4.5f, 0xffffffffu);
    for (int i = 0; i < 3; ++i) disc(canvas, 918 + i * 28, (py0 + py1) * 0.5f, 6, BLUE, 0.35f + 0.25f * i);
    /* the code: three finder squares and a fixed scatter of modules */
    const float qx = PW_STEAM_QR_X0, qy = PW_STEAM_QR_Y0, cell = (PW_STEAM_QR_X1 - PW_STEAM_QR_X0) / 11.0f;
    fill_rounded(canvas, qx - 8, qy - 8, PW_STEAM_QR_X1 + 8, PW_STEAM_QR_Y1 + 8, 10, 0xffffffffu, 1.0f, 1.0f, 1.0f);
    ring_rounded(canvas, qx - 8, qy - 8, PW_STEAM_QR_X1 + 8, PW_STEAM_QR_Y1 + 8, 10, 0xff9fb4d2u, 1.0f, 1.5f);
    static const char *const modules[11] = {"xxx.x.x.xxx", "x.x..x..x.x", "xxx.xx..xxx", "....x.x....", "x.xx.x.xx.x",
                                            ".x..xxx.x..", "x.x.x..x.xx", "........x.x", "xxx.x.xx..x", "x.x..x.x.x.",
                                            "xxx.xx..xx."};
    for (int j = 0; j < 11; ++j)
        for (int i = 0; i < 11; ++i)
            if (modules[j][i] == 'x')
                fill_rounded(canvas, qx + i * cell + 1, qy + j * cell + 1, qx + (i + 1) * cell - 1, qy + (j + 1) * cell - 1, 1.5f,
                             INK, 1.0f, 1.0f, 1.0f);
}

int pw_steam_screen_persona_drawable(const PwSteamFaces *faces, const char *persona)
{
    if (!persona || !*persona) return 0;
    const Pen p = {faces->semibold, faces->latin, 58};
    for (const char *at = persona; *at;) {
        const unsigned c = next_codepoint(&at);
        if (c == 0xfffd || (c != ' ' && !face_for(&p, c))) return 0;
    }
    return 1;
}

void pw_steam_screen_draw(uint32_t *canvas, const uint32_t *backdrop, const PwSteamScreenState *state,
                          const PwSteamFaces *faces, PwSteamScreenReport *report)
{
    memcpy(canvas, backdrop, (size_t)W * H * sizeof(*canvas));
    if (report) report->bottom = 0, report->cut = 0, report->missing = 0;
    if (state->screen == PW_STEAM_SCREEN_NONE) return;
    const Ctx ctx = {canvas, faces, state->language, report};
    const float x = PW_STEAM_TEXT_X0, width = PW_STEAM_TEXT_X1 - PW_STEAM_TEXT_X0, notes = PW_STEAM_CARD_Y1 - 58;

    /* The mark, white on the sky as the loading screen's words */
    {
        Pen p = pen(&ctx, 1, 36);
        draw(canvas, &p, "MihawkOS", 8, 96, 106, PW_LOADING_TEXT_SHADOW, PW_LOADING_SHADOW_ALPHA / 255.0f);
        draw(canvas, &p, "MihawkOS", 8, 96, 104, PW_LOADING_TEXT, 1.0f);
    }
    /* The card: a soft shadow, frosted glass lighter at the top, a lit rim */
    fill_rounded(canvas, PW_STEAM_CARD_X0, PW_STEAM_CARD_Y0 + 14, PW_STEAM_CARD_X1, PW_STEAM_CARD_Y1 + 14, 32, 0xff04225au,
                 0.20f, 0.20f, 36.0f);
    fill_rounded(canvas, PW_STEAM_CARD_X0, PW_STEAM_CARD_Y0, PW_STEAM_CARD_X1, PW_STEAM_CARD_Y1, 32, 0xfff7fbffu, 0.90f,
                 0.82f, 1.0f);
    ring_rounded(canvas, PW_STEAM_CARD_X0, PW_STEAM_CARD_Y0, PW_STEAM_CARD_X1, PW_STEAM_CARD_Y1, 32, 0xffffffffu, 0.95f, 2.0f);

    switch (state->screen) {
    case PW_STEAM_SCREEN_INSTALLING:
        step_line(&ctx, 2);
        text_line(&ctx, 1, 58, say(&ctx, PW_TEXT_STEAM_INSTALLING_TITLE), x, 340, width, INK);
        paragraph(&ctx, 31, say(&ctx, PW_TEXT_STEAM_INSTALLING_BODY), x, 414, width, 4, MUTED);
        tube_track(&ctx);
        progress_line(&ctx, state, 0);
        break;
    case PW_STEAM_SCREEN_UPDATING:
    case PW_STEAM_SCREEN_SLOW:
        step_line(&ctx, 3);
        text_line(&ctx, 1, 58, say(&ctx, PW_TEXT_STEAM_UPDATING_TITLE), x, 340, width, INK);
        paragraph(&ctx, 31, say(&ctx, PW_TEXT_STEAM_UPDATING_BODY), x, 414, width, 4, MUTED);
        tube_track(&ctx);
        progress_line(&ctx, state, 1);
        if (state->screen == PW_STEAM_SCREEN_SLOW) {
            /* the amber note, with CROSS to try again at its right */
            const float top = PW_STEAM_TUBE_Y1 + 82, bottom = notes - 44;
            fill_rounded(canvas, x - 4, top, PW_STEAM_TEXT_X1 + 4, bottom, 16, 0xffffc93cu, 0.30f, 0.30f, 1.0f);
            const Pen p = pen(&ctx, 0, 27);
            const char *slow = say(&ctx, PW_TEXT_STEAM_SLOW), *retry = say(&ctx, PW_TEXT_RETRY);
            const Pen bold = pen(&ctx, 1, 27);
            const float hint = measure(&bold, retry, strlen(retry)) + 110;
            Lines lines;
            wrap(&p, slow, width - hint - 24, &lines);
            const int shown = lines.count < 2 ? lines.count : 2;
            const float first = (top + bottom) * 0.5f + 10 - (shown - 1) * 27 * 0.72f;
            int missing = 0;
            for (int i = 0; i < shown; ++i)
                missing += draw(canvas, &p, lines.start[i], lines.length[i], x + 20, first + i * 27 * 1.45f, AMBER_INK, 1.0f);
            reached(&ctx, first + (shown - 1) * 27 * 1.45f + 8, lines.count - shown, missing);
            cross_hint(&ctx, PW_TEXT_RETRY, PW_STEAM_TEXT_X1 - 8, (top + bottom) * 0.5f + 12);
        }
        text_line(&ctx, 0, 24, say(&ctx, PW_TEXT_STEAM_CANCEL_NOTE), x, notes, width, SOFT);
        break;
    case PW_STEAM_SCREEN_SCAN:
        step_line(&ctx, 4);
        text_line(&ctx, 1, 58, say(&ctx, PW_TEXT_STEAM_SCAN_TITLE), x, 340, width, INK);
        paragraph(&ctx, 31, say(&ctx, PW_TEXT_STEAM_SCAN_BODY), x, 414, width, 4, MUTED);
        phone_and_code(&ctx);
        text_line(&ctx, 0, 24, say(&ctx, PW_TEXT_STEAM_CANCEL_NOTE), x, notes, width - 360, SOFT);
        cross_hint(&ctx, PW_TEXT_CONTINUE, PW_STEAM_TEXT_X1 + 22, notes);
        break;
    case PW_STEAM_SCREEN_SIGNED_IN: {
        step_line(&ctx, 4);
        /* a green check before the title */
        disc(canvas, x + 26, 322, 26, GREEN, 1.0f);
        line(canvas, x + 14, 323, x + 23, 332, 5.0f, 0xffffffffu);
        line(canvas, x + 23, 332, x + 39, 312, 5.0f, 0xffffffffu);
        char title[256];
        if (pw_steam_screen_persona_drawable(faces, state->persona)) {
            const char *args[] = {state->persona};
            pw_setup_fill(title, sizeof(title), say(&ctx, PW_TEXT_STEAM_SIGNED_IN_AS), args, 1);
        } else {
            snprintf(title, sizeof(title), "%s", say(&ctx, PW_TEXT_STEAM_SIGNED_IN));
        }
        text_line(&ctx, 1, 58, title, x + 72, 340, width - 72, INK);
        paragraph(&ctx, 31, say(&ctx, PW_TEXT_STEAM_REOPEN_BODY), x, 414, width, 4, MUTED);
        break;
    }
    case PW_STEAM_SCREEN_DIALOG: {
        /* The game's words as it wrote them: its caption, then each of its lines, as many as fit above CROSS */
        text_line(&ctx, 1, 46, state->dialog_title ? state->dialog_title : "", x, 300, width, INK);
        float y = 372;
        for (const char *at = state->dialog_text ? state->dialog_text : ""; *at && y < notes - 60;) {
            const char *end = strchr(at, '\n');
            const size_t length = end ? (size_t)(end - at) : strlen(at);
            char line[512];
            snprintf(line, sizeof(line), "%.*s", (int)(length < sizeof(line) - 1 ? length : sizeof(line) - 1), at);
            const int left = (int)((notes - 60 - y) / (27 * 1.45f));
            if (line[0]) y = paragraph(&ctx, 27, line, x, y, width, left > 0 ? left : 1, MUTED);
            else y += 27 * 0.7f;
            at += length + (end != NULL);
        }
        cross_hint(&ctx, PW_TEXT_CONTINUE, PW_STEAM_TEXT_X1 + 22, notes);
        break;
    }
    default:
        break;
    }
}

void pw_steam_screen_animate(uint32_t *canvas, const PwSteamScreenState *state, unsigned tick)
{
    switch (state->screen) {
    case PW_STEAM_SCREEN_INSTALLING:
    case PW_STEAM_SCREEN_UPDATING:
    case PW_STEAM_SCREEN_SLOW:
        pw_loading_art_tube(canvas, PW_STEAM_TEXT_X0, PW_STEAM_TUBE_Y0, PW_STEAM_TEXT_X1, PW_STEAM_TUBE_Y1, -1.0f, tick);
        break;
    case PW_STEAM_SCREEN_SCAN: {
        /* a green line sweeping down the code and back, two seconds each way at ten frames a second */
        const unsigned phase = tick % 40;
        const float t = (phase < 20 ? phase : 40 - phase) / 20.0f;
        const float y = PW_STEAM_QR_Y0 + 4 + t * (PW_STEAM_QR_Y1 - PW_STEAM_QR_Y0 - 8);
        for (int yy = (int)y - 6; yy <= (int)y + 6; ++yy)
            for (int xx = PW_STEAM_QR_X0; xx <= PW_STEAM_QR_X1; ++xx)
                blend(canvas, xx, yy, 0xff55d52au, 0.85f * clampf(1.0f - fabsf(yy + 0.5f - y) / 6.0f, 0, 1));
        break;
    }
    default:
        break;
    }
}
