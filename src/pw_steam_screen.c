/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Steam guide's screens inside its sessions (pw_steam_screen.h). */
#include "pw_steam_screen.h"
#include "pw_loading_art.h"
#include "pw_vkbd.h"
#include "pw_overlay.h"
#include "pw_controller.h"

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

/* Where drawing lands: the screen, or the keyboard's region being drawn again (keyboard_region) */
static struct { int x0, y0, x1, y1; } clip = {0, 0, W, H};

/* Over an opaque pixel as the loading screen blends; over a see-through one (the keyboard's panel, premultiplied) the
 * "over" of premultiplied alpha. No canvas: only measuring. */
static void blend(uint32_t *canvas, int x, int y, uint32_t color, float alpha)
{
    if (!canvas || x < clip.x0 || y < clip.y0 || x >= clip.x1 || y >= clip.y1 || alpha <= 0.0f) return;
    uint32_t *p = canvas + (size_t)y * W + x;
    const unsigned a = (unsigned)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    if ((*p >> 24) == 0xff) {
        *p = pw_loading_blend(*p, color, a);
        return;
    }
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const unsigned under = (*p >> shift) & 0xff, over = shift == 24 ? 255 : (color >> shift) & 0xff;
        out |= ((over * a + under * (255 - a) + 127) / 255) << shift;
    }
    *p = out;
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

/* A pad button and its word, its pill ending at right, on the baseline y: CROSS (blue) or TRIANGLE (green). The pill's
 * left edge. */
static float pad_hint(const Ctx *ctx, int triangle, PwSetupText key, float right, float y)
{
    const char *word = say(ctx, key);
    const Pen p = pen(ctx, 1, 27);
    const float w = measure(&p, word, strlen(word));
    right -= 22;
    const float x = right - w, glyph = x - 34;
    fill_rounded(ctx->canvas, glyph - 30, y - 38, right + 22, y + 16, 27, 0xffffffffu, 0.75f, 0.75f, 1.0f);
    ring_rounded(ctx->canvas, glyph - 30, y - 38, right + 22, y + 16, 27, 0xff9fb4d2u, 0.8f, 1.5f);
    disc(ctx->canvas, glyph, y - 11, 19, triangle ? GREEN : BLUE, 1.0f);
    if (triangle) {
        line(ctx->canvas, glyph, y - 21, glyph - 9, y - 4, 3.0f, 0xffffffffu);
        line(ctx->canvas, glyph - 9, y - 4, glyph + 9, y - 4, 3.0f, 0xffffffffu);
        line(ctx->canvas, glyph + 9, y - 4, glyph, y - 21, 3.0f, 0xffffffffu);
    } else {
        line(ctx->canvas, glyph - 7, y - 18, glyph + 7, y - 4, 3.2f, 0xffffffffu);
        line(ctx->canvas, glyph + 7, y - 18, glyph - 7, y - 4, 3.2f, 0xffffffffu);
    }
    reached(ctx, y + 16, 0, draw(ctx->canvas, &p, word, strlen(word), x, y, INK, 1.0f));
    return glyph - 30;
}

/* CROSS and its word, its pill ending at right, on the baseline y. */
static void cross_hint(const Ctx *ctx, PwSetupText key, float right, float y) { (void)pad_hint(ctx, 0, key, right, y); }

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

/* ---- MihawkOS's own on-screen keyboard (src/pw_vkbd.h): its keys' words are the keys themselves, and the special keys
 * and the hints are drawn, so no word but the panel's title needs a language ---- */
static void glyph_centred(const Ctx *ctx, int semibold, const char *text, float cx, float baseline, float pixels, uint32_t color)
{
    const Pen p = pen(ctx, semibold, pixels);
    const float w = measure(&p, text, strlen(text));
    reached(ctx, baseline + pixels * 0.3f, 0, draw(ctx->canvas, &p, text, strlen(text), cx - w / 2, baseline, color, 1.0f));
}

static void icon_shift(uint32_t *canvas, float cx, float cy, uint32_t color, int on)
{
    const float pts[][2] = {{0, -20}, {-18, 0}, {-8, 0}, {-8, 16}, {8, 16}, {8, 0}, {18, 0}};
    for (int i = 0; i < 7; ++i)   /* Shift on: drawn bold */
        line(canvas, cx + pts[i][0], cy + pts[i][1], cx + pts[(i + 1) % 7][0], cy + pts[(i + 1) % 7][1], on ? 5.5f : 3.0f, color);
}

static void icon_back(uint32_t *canvas, float cx, float cy, uint32_t color)
{
    const float pts[][2] = {{-26, 0}, {-12, -16}, {24, -16}, {24, 16}, {-12, 16}};
    for (int i = 0; i < 5; ++i)
        line(canvas, cx + pts[i][0], cy + pts[i][1], cx + pts[(i + 1) % 5][0], cy + pts[(i + 1) % 5][1], 3.0f, color);
    line(canvas, cx - 2, cy - 7, cx + 12, cy + 7, 3.0f, color);
    line(canvas, cx + 12, cy - 7, cx - 2, cy + 7, 3.0f, color);
}

static void icon_space(uint32_t *canvas, float cx, float cy, float half, uint32_t color)
{
    line(canvas, cx - half, cy - 6, cx - half, cy + 8, 3.0f, color);
    line(canvas, cx - half, cy + 8, cx + half, cy + 8, 3.0f, color);
    line(canvas, cx + half, cy + 8, cx + half, cy - 6, 3.0f, color);
}

static void icon_check(uint32_t *canvas, float cx, float cy, uint32_t color)
{
    line(canvas, cx - 16, cy, cx - 4, cy + 12, 4.0f, color);
    line(canvas, cx - 4, cy + 12, cx + 18, cy - 14, 4.0f, color);
}

/* A pad button for the hints: CROSS, CIRCLE, SQUARE or TRIANGLE drawn, or a word (L1, R1, OPTIONS) in a pill */
static float hint_button(const Ctx *ctx, int which, const char *word, float x, float cy)
{
    uint32_t *canvas = ctx->canvas;
    if (word) {
        const Pen p = pen(ctx, 1, 22);
        const float w = measure(&p, word, strlen(word));
        fill_rounded(canvas, x, cy - 18, x + w + 24, cy + 18, 18, 0xff3c5878u, 0.9f, 0.9f, 1.0f);
        reached(ctx, cy + 18, 0, draw(canvas, &p, word, strlen(word), x + 12, cy + 8, 0xffffffffu, 1.0f));
        return x + w + 24;
    }
    disc(canvas, x + 18, cy, 18, 0xff3c5878u, 0.9f);
    if (which == 0) { line(canvas, x + 11, cy - 7, x + 25, cy + 7, 3.0f, 0xff8fc1ffu); line(canvas, x + 25, cy - 7, x + 11, cy + 7, 3.0f, 0xff8fc1ffu); }
    if (which == 1) ring_rounded(canvas, x + 9, cy - 9, x + 27, cy + 9, 9, 0xffff8f8fu, 1.0f, 3.0f);
    if (which == 2) ring_rounded(canvas, x + 10, cy - 8, x + 26, cy + 8, 1, 0xffffa8e0u, 1.0f, 3.0f);
    if (which == 3) {
        line(canvas, x + 18, cy - 9, x + 9, cy + 7, 3.0f, 0xff8fffd0u);
        line(canvas, x + 9, cy + 7, x + 27, cy + 7, 3.0f, 0xff8fffd0u);
        line(canvas, x + 27, cy + 7, x + 18, cy - 9, 3.0f, 0xff8fffd0u);
    }
    return x + 36;
}

/* ---- MihawkOS's own keyboard: Frosted Meadow (the owner's pick, 2026-11-01; the side-session note
 * osk-frosted-meadow-spec.md, mockup E). The panel is see-through: drawn premultiplied over nothing, the broker blends it
 * over the game's blurred frame and adds the rising bubbles in the margins (PS5_Vulkan pw_display_overlay.h). ---- */

#define KEY_INK 0xff0e2b46u
#define NAME_INK 0xff14385au
#define FIELD_RIM 0xff2a7bd6u
enum { KEY_MARGIN = 4, GLOW = 30 };   /* past a key's edge: its rims; the focus's glow */

static uint32_t rgb(unsigned r, unsigned g, unsigned b) { return 0xff000000u | r << 16 | g << 8 | b; }

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    const float s = clampf(t, 0, 1);
    unsigned out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8)
        out |= (unsigned)(((a >> shift) & 0xff) + (((int)((b >> shift) & 0xff) - (int)((a >> shift) & 0xff)) * s) + 0.5f) << shift;
    return out;
}

/* The meadow's top edge for each column: two cubic curves, (0,1010) to (900,1062) to (1920,1000) */
static const float *meadow_top(void)
{
    static float top[W];
    static int made;
    if (made) return top;
    for (int x = 0; x < W; ++x) top[x] = H;
    static const float curves[2][8] = {{0, 1010, 300, 990, 560, 1040, 900, 1062}, {900, 1062, 1200, 1080, 1500, 1020, 1920, 1000}};
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i <= 4000; ++i) {
            const float t = i / 4000.0f, u = 1 - t, *p = curves[c];
            const float x = u * u * u * p[0] + 3 * u * u * t * p[2] + 3 * u * t * t * p[4] + t * t * t * p[6];
            const float y = u * u * u * p[1] + 3 * u * u * t * p[3] + 3 * u * t * t * p[5] + t * t * t * p[7];
            const int col = (int)x;
            if (col >= 0 && col < W && y < top[col]) top[col] = y;
        }
    made = 1;
    return top;
}

/* The slab, its sheen and rim, and the meadow, from nothing, inside the clip */
static void keyboard_backdrop(uint32_t *canvas, int x0, int y0, int x1, int y1)
{
    const float *top = meadow_top();
    for (int y = y0; y < y1; ++y) {
        const float t = (float)(y - PW_VKBD_PANEL_Y) / PW_VKBD_PANEL_H;
        const uint32_t slab = mix(rgb(228, 243, 255), rgb(206, 232, 252), t);
        const float slab_alpha = 0.42f + (0.55f - 0.42f) * t;
        const float sheen = y < PW_VKBD_PANEL_Y + 150 ? 0.45f * (1 - (y - PW_VKBD_PANEL_Y) / 150.0f) : 0;
        const uint32_t green = mix(rgb(184, 242, 90), rgb(70, 170, 40), (y - 990) / 90.0f);
        const float green_alpha = 0.42f + (0.55f - 0.42f) * clampf((y - 990) / 90.0f, 0, 1);
        /* the row's colour once (the slab, its sheen and rim), then the meadow where it reaches */
        uint32_t *row = canvas + (size_t)y * W;
        row[x0] = 0;
        blend(canvas, x0, y, slab, slab_alpha);
        if (sheen > 0) blend(canvas, x0, y, 0xffffffffu, sheen);
        if (y < PW_VKBD_PANEL_Y + 2) blend(canvas, x0, y, 0xffffffffu, 0.95f);
        const uint32_t plain = row[x0];
        for (int x = x0; x < x1; ++x) {
            row[x] = plain;
            const float cover = clampf(y + 1.0f - top[x], 0, 1);
            if (cover > 0) blend(canvas, x, y, green, green_alpha * cover);
        }
    }
}

/* A colour band inside a rounded rectangle: rows from f0 to f1 of its height, colour and alpha from (c0, a0) to (c1, a1) */
static void band(uint32_t *canvas, float x0, float y0, float x1, float y1, float r, float f0, float f1, uint32_t c0, float a0,
                 uint32_t c1, float a1)
{
    const float b0 = y0 + (y1 - y0) * f0, b1 = y0 + (y1 - y0) * f1;
    for (int y = (int)floorf(b0); y < (int)ceilf(b1); ++y)
        for (int x = (int)floorf(x0); x <= (int)ceilf(x1); ++x) {
            const float cover = clampf(0.5f - rounded(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r), 0, 1) *
                                clampf(fminf(y + 1 - b0, b1 - y), 0, 1);
            const float t = (y + 0.5f - b0) / (b1 - b0);
            if (cover > 0) blend(canvas, x, y, mix(c0, c1, t), cover * (a0 + (a1 - a0) * clampf(t, 0, 1)));
        }
}

/* The glass over a key: gloss on its top 48 %, a light lip over its bottom 45 %, a white rim and a blue outer one */
static void glass(uint32_t *canvas, const PwVkbdKey *key, float rim)
{
    const float x1 = key->x + key->w, y1 = key->y + key->h;
    band(canvas, key->x, key->y, x1, y1, 14, 0, 0.48f, 0xffffffffu, 0.9f, 0xffffffffu, 0.1f);
    band(canvas, key->x, key->y, x1, y1, 14, 0.55f, 1, rgb(150, 210, 255), 0.4f, rgb(150, 210, 255), 0.4f);
    ring_rounded(canvas, key->x - 1, key->y - 1, x1 + 1, y1 + 1, 15, rgb(90, 145, 205), 0.55f, 1.0f);
    ring_rounded(canvas, key->x, key->y, x1, y1, 14, 0xffffffffu, rim > 2 ? 1.0f : 0.95f, rim);
}

static void keyboard_field_box(uint32_t *canvas)
{
    const float x0 = PW_VKBD_FIELD_X, y0 = PW_VKBD_FIELD_Y, x1 = x0 + PW_VKBD_FIELD_W, y1 = y0 + PW_VKBD_FIELD_H;
    band(canvas, x0, y0, x1, y1, 14, 0, 1, 0xffffffffu, 0.86f, 0xffffffffu, 0.82f);
    ring_rounded(canvas, x0, y0, x1, y1, 14, FIELD_RIM, 1.0f, 2.5f);
}

/* The line being typed and the caret after it, over the field's box */
static void keyboard_field(const Ctx *ctx, const PwVkbd *kbd)
{
    uint32_t *canvas = ctx->canvas;
    const float x0 = PW_VKBD_FIELD_X, y0 = PW_VKBD_FIELD_Y, y1 = y0 + PW_VKBD_FIELD_H;
    char typed[PW_VKBD_TEXT * 4 + 1] = "";
    if (kbd) pw_vkbd_utf8(kbd, typed, sizeof(typed));
    const float w = typed[0] ? text_line(ctx, 0, 36, typed, x0 + 22, y0 + 42, PW_VKBD_FIELD_W - 60, KEY_INK) : 0;
    const float caret = x0 + 24 + fminf(w, PW_VKBD_FIELD_W - 60);
    line(canvas, caret, y0 + 12, caret, y1 - 12, 3.0f, FIELD_RIM);
}

static void keyboard_key(const Ctx *ctx, const PwVkbd *kbd, const PwVkbdKey *key, int focus)
{
    uint32_t *canvas = ctx->canvas;
    const float x1 = key->x + key->w, y1 = key->y + key->h;
    const int done = key->kind == PW_VKBD_DONE;
    if (focus) {
        fill_rounded(canvas, key->x - 6, key->y - 6, x1 + 6, y1 + 6, 20, rgb(80, 205, 255), 0.9f, 0.9f, 36.0f);   /* the glow */
        band(canvas, key->x, key->y, x1, y1, 14, 0, 0.5f, rgb(0x9b, 0xe8, 0xff), 1, rgb(0x2a, 0xa3, 0xee), 1);
        band(canvas, key->x, key->y, x1, y1, 14, 0.5f, 1, rgb(0x2a, 0xa3, 0xee), 1, rgb(0x0b, 0x5f, 0xc4), 1);
    } else if (done) {
        band(canvas, key->x, key->y, x1, y1, 14, 0, 0.5f, rgb(0xc6, 0xf5, 0x6e), 1, rgb(0x5c, 0xc2, 0x2a), 1);
        band(canvas, key->x, key->y, x1, y1, 14, 0.5f, 1, rgb(0x5c, 0xc2, 0x2a), 1, rgb(0x23, 0x86, 0x1a), 1);
    } else if (key->kind == PW_VKBD_CHAR) {
        band(canvas, key->x, key->y, x1, y1, 14, 0, 1, 0xffffffffu, 0.9f, rgb(228, 241, 252), 0.84f);
    } else {
        band(canvas, key->x, key->y, x1, y1, 14, 0, 1, rgb(226, 240, 252), 0.9f, rgb(196, 222, 246), 0.86f);
    }
    glass(canvas, key, focus ? 2.5f : 1.5f);
    const uint32_t ink = focus || done ? 0xffffffffu : KEY_INK;
    const float cx = key->x + key->w / 2, cy = key->y + key->h / 2;
    switch (key->kind) {
    case PW_VKBD_CHAR:
        if (focus) glyph_centred(ctx, 0, key->text, cx + 1, cy + 13, 34, rgb(0x0b, 0x3a, 0x6e));   /* its shadow */
        glyph_centred(ctx, 0, key->text, cx, cy + 12, 34, ink);
        break;
    case PW_VKBD_PAGE: glyph_centred(ctx, 1, key->text, cx, cy + 9, 26, ink); break;
    case PW_VKBD_SHIFT: icon_shift(canvas, cx, cy, kbd->shift && !focus ? FIELD_RIM : ink, kbd->shift); break;
    case PW_VKBD_SPACE: icon_space(canvas, cx, cy - 2, key->w / 4, ink); break;
    case PW_VKBD_BACK: icon_back(canvas, cx, cy, ink); break;
    case PW_VKBD_DONE: icon_check(canvas, cx, cy, 0xffffffffu); break;
    }
}

/* The buttons, CROSS a key, CIRCLE closes, SQUARE one back, TRIANGLE a space, L1/R1 the page, OPTIONS done; from x, or
 * only measured (a context without a canvas). Returns where they end. */
static float keyboard_hints(const Ctx *ctx, const PwVkbd *kbd, float hx, float hy)
{
    uint32_t *canvas = ctx->canvas;
    hx = hint_button(ctx, 0, NULL, hx, hy) + 10;
    fill_rounded(canvas, hx, hy - 15, hx + 30, hy + 15, 6, 0xffffffffu, 0.9f, 0.9f, 1.0f);
    ring_rounded(canvas, hx, hy - 15, hx + 30, hy + 15, 6, NAME_INK, 1.0f, 2.0f);
    glyph_centred(ctx, 0, "a", hx + 15, hy + 7, 22, KEY_INK);
    hx += 60;
    hx = hint_button(ctx, 1, NULL, hx, hy) + 10;
    line(canvas, hx, hy - 8, hx + 16, hy + 8, 3.0f, NAME_INK);
    line(canvas, hx + 16, hy - 8, hx, hy + 8, 3.0f, NAME_INK);
    hx += 60;
    hx = hint_button(ctx, 2, NULL, hx, hy) + 40;
    icon_back(canvas, hx, hy, NAME_INK);
    hx += 66;
    hx = hint_button(ctx, 3, NULL, hx, hy) + 40;
    icon_space(canvas, hx, hy - 4, 22, NAME_INK);
    hx += 66;
    hx = hint_button(ctx, 0, "L1", hx, hy) + 8;
    hx = hint_button(ctx, 0, "R1", hx, hy) + 16;
    glyph_centred(ctx, 1, pw_vkbd_page_name((kbd->page + 1) % PW_VKBD_PAGES), hx + 30, hy + 9, 24, NAME_INK);
    hx += 96;
    hx = hint_button(ctx, 0, "OPTIONS", hx, hy) + 34;
    icon_check(canvas, hx, hy, NAME_INK);
    return hx + 16;
}

static int meets(float ax0, float ay0, float ax1, float ay1, int x0, int y0, int x1, int y1)
{
    return ax1 > x0 && ax0 < x1 && ay1 > y0 && ay0 < y1;
}

/* The panel without the line typed and without the focus: the slab, the field's box, every key, the hints and the
 * panel's name. Kept for the last two pages or Shift states drawn (a page's first draw takes about 0.1 s on a PC; each
 * change after it only copies from here: the owner, 2026-11-01, "insane input lag"). */
static const uint32_t *keyboard_base(const Ctx *ctx, const PwVkbd *kbd)
{
    static struct { uint32_t *pixels; int page, shift; const PwSetupLanguage *language; const PwSteamFaces *faces; } bases[2];
    static int next;
    for (int i = 0; i < 2; ++i)
        if (bases[i].pixels && bases[i].page == kbd->page && bases[i].shift == kbd->shift &&
            bases[i].language == ctx->language && bases[i].faces == ctx->faces)
            return bases[i].pixels;
    const int i = next;
    next ^= 1;
    if (!bases[i].pixels) bases[i].pixels = malloc((size_t)W * H * sizeof(uint32_t));
    if (!bases[i].pixels) return NULL;
    bases[i].page = kbd->page, bases[i].shift = kbd->shift, bases[i].language = ctx->language, bases[i].faces = ctx->faces;
    const Ctx base = {bases[i].pixels, ctx->faces, ctx->language, ctx->report};
    const int saved[4] = {clip.x0, clip.y0, clip.x1, clip.y1};
    clip.x0 = 0, clip.y0 = PW_VKBD_PANEL_Y, clip.x1 = W, clip.y1 = PW_VKBD_PANEL_Y + PW_VKBD_PANEL_H;
    keyboard_backdrop(base.canvas, 0, PW_VKBD_PANEL_Y, W, PW_VKBD_PANEL_Y + PW_VKBD_PANEL_H);
    keyboard_field_box(base.canvas);
    PwVkbdKey key;
    for (int row = 0; row < PW_VKBD_ROWS; ++row)
        for (int col = 0; col < pw_vkbd_row_keys(row); ++col)
            if (pw_vkbd_key(kbd, row, col, &key)) keyboard_key(&base, kbd, &key, 0);
    const float hy = 1052;
    const Ctx measuring = {NULL, ctx->faces, ctx->language, NULL};
    const float width = keyboard_hints(&measuring, kbd, 0, hy);
    keyboard_hints(&base, kbd, (W - width) / 2, hy);
    /* the panel's name, its end at the field's right edge */
    const char *name = say(ctx, PW_TEXT_SET_KEYBOARD);
    const Pen p = pen(ctx, 1, 26);
    const float nw = measure(&p, name, strlen(name));
    reached(ctx, hy + 8, 0, draw(base.canvas, &p, name, strlen(name), PW_VKBD_FIELD_X + PW_VKBD_FIELD_W - nw, hy + 9, NAME_INK, 1.0f));
    clip.x0 = saved[0], clip.y0 = saved[1], clip.x1 = saved[2], clip.y1 = saved[3];
    return bases[i].pixels;
}

/* Everything of the panel inside (x0, y0)-(x1, y1): the base, the line typed, the focus with its glow, clipped to it; the
 * whole panel or the places a change touched, either way the same pixels (pw_steam_screen_keyboard_changed) */
static void keyboard_region(const Ctx *ctx, const PwVkbd *kbd, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < PW_VKBD_PANEL_Y) y0 = PW_VKBD_PANEL_Y;
    if (x1 > W) x1 = W;
    if (y1 > PW_VKBD_PANEL_Y + PW_VKBD_PANEL_H) y1 = PW_VKBD_PANEL_Y + PW_VKBD_PANEL_H;
    if (x0 >= x1 || y0 >= y1 || !kbd) return;
    const uint32_t *base = keyboard_base(ctx, kbd);
    if (!base) return;
    for (int y = y0; y < y1; ++y)
        memcpy(ctx->canvas + (size_t)y * W + x0, base + (size_t)y * W + x0, (size_t)(x1 - x0) * sizeof(uint32_t));
    clip.x0 = x0, clip.y0 = y0, clip.x1 = x1, clip.y1 = y1;
    if (meets(PW_VKBD_FIELD_X - KEY_MARGIN, PW_VKBD_FIELD_Y - KEY_MARGIN, PW_VKBD_FIELD_X + PW_VKBD_FIELD_W + KEY_MARGIN,
              PW_VKBD_FIELD_Y + PW_VKBD_FIELD_H + KEY_MARGIN, x0, y0, x1, y1))
        keyboard_field(ctx, kbd);
    PwVkbdKey key;
    if (pw_vkbd_key(kbd, kbd->row, kbd->col, &key) &&
        meets(key.x - GLOW, key.y - GLOW, key.x + key.w + GLOW, key.y + key.h + GLOW, x0, y0, x1, y1))
        keyboard_key(ctx, kbd, &key, 1);
    clip.x0 = 0, clip.y0 = 0, clip.x1 = W, clip.y1 = H;
}

static void draw_keyboard(const Ctx *ctx, const PwVkbd *kbd)
{
    keyboard_region(ctx, kbd, 0, PW_VKBD_PANEL_Y, W, PW_VKBD_PANEL_Y + PW_VKBD_PANEL_H);
}

/* ---- The control overlay (src/pw_overlay.h): kabuki-ps5's layout (ideas only) in the keyboard's Frosted Meadow glass. One
 * see-through panel down the right of the screen, x 1032..1920, which the broker blurs the game under and lets bubbles rise
 * in (its left 165 px and the rail's strip); on it the rail of round glass buttons at the edge, and the focused button's
 * card beside it. Drawn whole on each change (a few presses a second, and the Statistics card twice a second; shortcut:
 * no cached base as the keyboard's, add one if a change is seen to lag on the console). ---- */

/* One line centred on cx, made smaller (to 70 %) if it is wider than width */
static void centred_line(const Ctx *ctx, int semibold, float pixels, const char *text, float cx, float y, float width,
                         uint32_t color)
{
    Pen p = pen(ctx, semibold, pixels);
    float w = measure(&p, text, strlen(text));
    if (w > width) {
        p.pixels = fmaxf(pixels * 0.7f, pixels * width / w);
        w = measure(&p, text, strlen(text));
    }
    reached(ctx, y + p.pixels * 0.3f, w > width + 0.5f, draw(ctx->canvas, &p, text, strlen(text), cx - w / 2, y, color, 1.0f));
}

/* The panel's glass from nothing: the keyboard slab's colours down the screen, a sheen and a white rim on its left edge,
 * and the meadow along the bottom */
static void overlay_backdrop(uint32_t *canvas)
{
    const float *top = meadow_top();
    for (int y = 0; y < H; ++y) {
        const float t = (float)y / H;
        const uint32_t slab = mix(rgb(228, 243, 255), rgb(206, 232, 252), t);
        const uint32_t green = mix(rgb(184, 242, 90), rgb(70, 170, 40), (y - 990) / 90.0f);
        const float green_alpha = 0.42f + (0.55f - 0.42f) * clampf((y - 990) / 90.0f, 0, 1);
        for (int x = PW_OVERLAY_PANEL_X; x < W; ++x) {
            const int dx = x - PW_OVERLAY_PANEL_X;
            canvas[(size_t)y * W + x] = 0;
            blend(canvas, x, y, slab, 0.42f + (0.55f - 0.42f) * t);
            if (dx < 150) blend(canvas, x, y, 0xffffffffu, 0.45f * (1 - dx / 150.0f));
            if (dx < 2) blend(canvas, x, y, 0xffffffffu, 0.95f);
            const float cover = clampf(y + 1.0f - top[x], 0, 1);
            if (cover > 0) blend(canvas, x, y, green, green_alpha * cover);
        }
    }
}

/* A button's picture, line drawn, size about 2 * r: Exit a door with an arrow out, Statistics three bars, the keyboard
 * its keys, the mouse its shape (struck through when it is off) */
static void overlay_icon(uint32_t *canvas, int which, float cx, float cy, float r, uint32_t ink, int off)
{
    const float w = r > 20 ? 3.0f : 2.5f;
    switch (which) {
    case PW_OVERLAY_EXIT:
        line(canvas, cx - r * 0.2f, cy - r * 0.8f, cx - r * 0.8f, cy - r * 0.8f, w, ink);
        line(canvas, cx - r * 0.8f, cy - r * 0.8f, cx - r * 0.8f, cy + r * 0.8f, w, ink);
        line(canvas, cx - r * 0.8f, cy + r * 0.8f, cx - r * 0.2f, cy + r * 0.8f, w, ink);
        line(canvas, cx - r * 0.3f, cy, cx + r * 0.8f, cy, w, ink);
        line(canvas, cx + r * 0.8f, cy, cx + r * 0.4f, cy - r * 0.4f, w, ink);
        line(canvas, cx + r * 0.8f, cy, cx + r * 0.4f, cy + r * 0.4f, w, ink);
        break;
    case PW_OVERLAY_STATS:
        for (int i = 0; i < 3; ++i) {
            const float x = cx + (i - 1) * r * 0.6f, h = r * (0.6f + 0.5f * (i == 1) + 0.25f * (i == 2));
            fill_rounded(canvas, x - r * 0.18f, cy + r * 0.8f - h * 1.2f, x + r * 0.18f, cy + r * 0.8f, 2, ink, 1, 1, 1.0f);
        }
        break;
    case PW_OVERLAY_KEYBOARD:
        ring_rounded(canvas, cx - r * 0.95f, cy - r * 0.6f, cx + r * 0.95f, cy + r * 0.6f, r * 0.15f, ink, 1, w);
        for (int row = 0; row < 2; ++row)
            for (int k = 0; k < 4; ++k)
                disc(canvas, cx + (k - 1.5f) * r * 0.42f, cy - r * 0.22f + row * r * 0.3f, r * 0.08f, ink, 1);
        line(canvas, cx - r * 0.45f, cy + r * 0.35f, cx + r * 0.45f, cy + r * 0.35f, w, ink);
        break;
    case PW_OVERLAY_MOUSE:
        ring_rounded(canvas, cx - r * 0.5f, cy - r * 0.85f, cx + r * 0.5f, cy + r * 0.85f, r * 0.5f, ink, 1, w);
        line(canvas, cx, cy - r * 0.85f, cx, cy - r * 0.25f, w, ink);
        line(canvas, cx - r * 0.5f, cy - r * 0.25f, cx + r * 0.5f, cy - r * 0.25f, w, ink);
        if (off) line(canvas, cx - r * 0.9f, cy + r * 0.9f, cx + r * 0.9f, cy - r * 0.9f, w + 1, rgb(0xe8, 0x41, 0x2c));
        break;
    case PW_OVERLAY_CONTROLLER:   /* a pad: its body, the D-pad's cross, two face buttons */
        ring_rounded(canvas, cx - r, cy - r * 0.5f, cx + r, cy + r * 0.55f, r * 0.45f, ink, 1, w);
        line(canvas, cx - r * 0.62f, cy, cx - r * 0.22f, cy, w, ink);
        line(canvas, cx - r * 0.42f, cy - r * 0.2f, cx - r * 0.42f, cy + r * 0.2f, w, ink);
        disc(canvas, cx + r * 0.32f, cy + r * 0.08f, r * 0.11f, ink, 1);
        disc(canvas, cx + r * 0.56f, cy - r * 0.12f, r * 0.11f, ink, 1);
        break;
    }
}

/* The focus's blue glass, or the keys' white glass, over a rounded rectangle */
static void glass_face(uint32_t *canvas, float x0, float y0, float x1, float y1, float r, int focus)
{
    const PwVkbdKey key = {PW_VKBD_CHAR, "", x0, y0, x1 - x0, y1 - y0};
    if (focus) {
        fill_rounded(canvas, x0 - 6, y0 - 6, x1 + 6, y1 + 6, r + 6, rgb(80, 205, 255), 0.9f, 0.9f, 30.0f);   /* the glow */
        band(canvas, x0, y0, x1, y1, r, 0, 0.5f, rgb(0x9b, 0xe8, 0xff), 1, rgb(0x2a, 0xa3, 0xee), 1);
        band(canvas, x0, y0, x1, y1, r, 0.5f, 1, rgb(0x2a, 0xa3, 0xee), 1, rgb(0x0b, 0x5f, 0xc4), 1);
    } else {
        band(canvas, x0, y0, x1, y1, r, 0, 1, 0xffffffffu, 0.9f, rgb(228, 241, 252), 0.84f);
    }
    if (r <= 16) { glass(canvas, &key, focus ? 2.5f : 1.5f); return; }
    band(canvas, x0, y0, x1, y1, r, 0, 0.48f, 0xffffffffu, focus ? 0.55f : 0.9f, 0xffffffffu, 0.1f);   /* the gloss */
    ring_rounded(canvas, x0 - 1, y0 - 1, x1 + 1, y1 + 1, r + 1, rgb(90, 145, 205), 0.55f, 1.0f);
    ring_rounded(canvas, x0, y0, x1, y1, r, 0xffffffffu, focus ? 1.0f : 0.95f, focus ? 2.5f : 1.5f);
}

/* On (Aero green) or Off (grey), its right end at right, centred on cy */
static void on_off(const Ctx *ctx, int on, float right, float cy)
{
    uint32_t *canvas = ctx->canvas;
    const char *word = say(ctx, on ? PW_TEXT_OVERLAY_ON : PW_TEXT_SET_KEYBOARD_OFF);
    const Pen p = pen(ctx, 1, 24);
    const float w = fminf(measure(&p, word, strlen(word)), 140), x0 = right - w - 44, y0 = cy - 20, y1 = cy + 20;
    if (on) {
        band(canvas, x0, y0, right, y1, 20, 0, 0.5f, rgb(0xc6, 0xf5, 0x6e), 1, rgb(0x5c, 0xc2, 0x2a), 1);
        band(canvas, x0, y0, right, y1, 20, 0.5f, 1, rgb(0x5c, 0xc2, 0x2a), 1, rgb(0x23, 0x86, 0x1a), 1);
    } else {
        fill_rounded(canvas, x0, y0, right, y1, 20, rgb(150, 165, 185), 0.9f, 0.9f, 1.0f);
    }
    ring_rounded(canvas, x0, y0, right, y1, 20, 0xffffffffu, 0.9f, 1.5f);
    centred_line(ctx, 1, 24, word, (x0 + right) / 2, cy + 9, 140, 0xffffffffu);
}

/* The HUD's lines from line `first`, each `step` apart from the baseline y, the pools with their bars at bar_right */
static void hud_rows(const Ctx *ctx, const PwHud *hud, int first, float x, float y, float step, float pixels, float width,
                     float bar_right)
{
    char text[PW_HUD_LINE];
    for (int i = first; i < PW_HUD_LINES; ++i, y += step) {
        pw_hud_line(hud, i, text, sizeof(text));
        text_line(ctx, 0, pixels, text, x, y, i >= 4 ? width - 150 : width, KEY_INK);
        const int total = i == 4 ? hud->small_total : hud->big_total, free_count = i == 4 ? hud->small_free : hud->big_free;
        if (i < 4 || total <= 0) continue;
        /* how full, green, then amber near the guard's close, red near its end */
        static const uint32_t colours[] = {0xff5cc22au, 0xffffb020u, 0xffe8412cu};
        const float bx0 = bar_right - 132, by = y - pixels * 0.35f;
        fill_rounded(ctx->canvas, bx0, by - 5, bar_right, by + 5, 5, 0xffffffffu, 0.7f, 0.7f, 1.0f);
        const float used = clampf((float)(total - free_count) / total, 0, 1);
        if (used > 0) fill_rounded(ctx->canvas, bx0, by - 5, bx0 + fmaxf(10, (bar_right - bx0) * used), by + 5, 5,
                                   colours[pw_hud_pool_level(free_count, total)], 1, 1, 1.0f);
    }
}

/* The frame-time graph inside (x0, y0)-(x1, y1): one bar a sample, a faint line at 16.7 ms */
static void hud_graph(uint32_t *canvas, const PwHud *hud, float x0, float y0, float x1, float y1)
{
    const float bar = (x1 - x0) / PW_HUD_GRAPH, top = pw_hud_graph_top(hud);
    fill_rounded(canvas, x0 - 4, y0 - 2, x1 + 4, y1 + 2, 6, 0xffffffffu, 0.45f, 0.45f, 1.0f);
    for (unsigned i = 0; i < hud->graph_count; ++i) {
        const unsigned at = hud->graph_count < PW_HUD_GRAPH ? i : (hud->graph_next + i) % PW_HUD_GRAPH;
        const float h = fminf(hud->frame_ms[at] / top, 1) * (y1 - y0), bx = x0 + i * bar;
        for (int y = (int)(y1 - h); y < (int)y1; ++y)
            for (int x = (int)bx; x < (int)(bx + bar - 1); ++x) blend(canvas, x, y, rgb(0x2a, 0xa3, 0xee), 0.95f);
    }
    const float line60 = y1 - (1000.0f / 60 / top) * (y1 - y0);
    line(canvas, x0, line60, x1, line60, 1.0f, rgb(0x23, 0x86, 0x1a));
}

static const PwSetupText overlay_names[PW_OVERLAY_TILES] = {PW_TEXT_OVERLAY_EXIT, PW_TEXT_OVERLAY_STATS, PW_TEXT_SET_KEYBOARD,
                                                            PW_TEXT_OVERLAY_MOUSE, PW_TEXT_OVERLAY_CONTROLLER};

/* The focused button's card: its picture and name, its switch, and what it does */
static void overlay_card(const Ctx *ctx, const PwOverlay *overlay)
{
    uint32_t *canvas = ctx->canvas;
    const float x0 = PW_OVERLAY_CARD_X, y0 = PW_OVERLAY_CARD_Y, x1 = x0 + PW_OVERLAY_CARD_W, y1 = y0 + PW_OVERLAY_CARD_H;
    const float pad = 28, left = x0 + pad, right = x1 - pad, width = right - left;
    const int which = overlay->focus;
    fill_rounded(canvas, x0, y0 + 10, x1, y1 + 10, 28, 0xff04225au, 0.18f, 0.18f, 30.0f);   /* a soft shadow */
    band(canvas, x0, y0, x1, y1, 28, 0, 1, 0xffffffffu, 0.80f, rgb(232, 244, 253), 0.74f);
    band(canvas, x0, y0, x1, y1, 28, 0, 0.22f, 0xffffffffu, 0.6f, 0xffffffffu, 0.0f);
    ring_rounded(canvas, x0, y0, x1, y1, 28, 0xffffffffu, 0.95f, 2.0f);
    /* the header: the picture in a blue glass tile, the name, the switch */
    glass_face(canvas, left, y0 + 26, left + 56, y0 + 82, 14, 1);
    overlay_icon(canvas, which, left + 28, y0 + 54, 17, 0xffffffffu, which == PW_OVERLAY_MOUSE && overlay->mouse_off);
    const float header_w = which == PW_OVERLAY_STATS || which == PW_OVERLAY_MOUSE ? width - 76 - 140 : width - 76;
    text_line(ctx, 1, 32, say(ctx, overlay_names[which]), left + 76, y0 + 66, header_w, KEY_INK);
    if (which == PW_OVERLAY_STATS) on_off(ctx, overlay->stats, right, y0 + 54);
    if (which == PW_OVERLAY_MOUSE) on_off(ctx, !overlay->mouse_off, right, y0 + 54);
    line(canvas, left, y0 + 110, right, y0 + 110, 1.5f, rgb(160, 200, 235));
    switch (which) {
    case PW_OVERLAY_STATS: {
        const PwHud *hud = &overlay->figures;
        char text[PW_HUD_LINE];
        pw_hud_line(hud, 0, text, sizeof(text));
        text_line(ctx, 1, 34, text, left, y0 + 166, width, KEY_INK);
        hud_graph(canvas, hud, left + 4, y0 + 188, right - 4, y0 + 268);
        hud_rows(ctx, hud, 1, left, y0 + 320, 52, 27, width, right);
        break;
    }
    case PW_OVERLAY_CONTROLLER: {
        /* the three kinds, the chosen one blue glass with a check; Cross moves to the next */
        for (int kind = 0; kind < PW_CONTROLLER_KINDS; ++kind) {
            const float ry = y0 + 136 + kind * 76, chosen = kind == overlay->controller;
            glass_face(canvas, left, ry, right, ry + 60, 16, chosen);
            text_line(ctx, 1, 27, pw_controller_name(kind), left + 24, ry + 40, width - 100, chosen ? 0xffffffffu : KEY_INK);
            if (chosen) icon_check(canvas, right - 34, ry + 30, 0xffffffffu);
        }
        /* what to expect, in short words: a line each, its dot */
        const PwSetupText limits[] = {PW_TEXT_OVERLAY_PAD_LIMIT_START,
                                      overlay->pad_next_launch ? PW_TEXT_OVERLAY_PAD_NEXT_LAUNCH : PW_TEXT_OVERLAY_PAD_LIMIT_DUALSENSE,
                                      PW_TEXT_OVERLAY_PAD_LIMIT_STEAM};
        float y = y0 + 392;
        for (int i = 0; i < 3; ++i) {
            disc(canvas, left + 6, y - 8, 4, rgb(0x2a, 0xa3, 0xee), 1);
            y = paragraph(ctx, 22, say(ctx, limits[i]), left + 22, y, width - 22, 4, KEY_INK) + 12;
        }
        break;
    }
    case PW_OVERLAY_MOUSE:
        overlay_icon(canvas, PW_OVERLAY_MOUSE, (x0 + x1) / 2, y0 + 330, 110, overlay->mouse_off ? rgb(150, 165, 185) : KEY_INK,
                     overlay->mouse_off);
        break;
    default: {   /* Exit and the keyboard: one big glass button, the press it waits for */
        const int armed = which == PW_OVERLAY_EXIT && overlay->armed_ms;
        glass_face(canvas, left + 30, y0 + 250, right - 30, y0 + 350, 20, 1);
        overlay_icon(canvas, which, left + 82, y0 + 300, 22, 0xffffffffu, 0);
        centred_line(ctx, 1, 32, say(ctx, overlay_names[which]), (left + right) / 2 + 26, y0 + 312, width - 180, 0xffffffffu);
        if (armed) {   /* Exit's second press: the words under it, an amber line */
            centred_line(ctx, 1, 26, say(ctx, PW_TEXT_OVERLAY_EXIT_AGAIN), (left + right) / 2, y0 + 410, width, AMBER_INK);
            fill_rounded(canvas, left + 60, y0 + 430, right - 60, y0 + 438, 4, rgb(0xff, 0xb0, 0x20), 1, 1, 1.0f);
        }
        break;
    }
    }
    /* the hints at the card's foot, centred: CROSS chooses, CIRCLE closes */
    const float hy = y1 - 44;
    float hwidth = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const Ctx *c = pass ? ctx : &(const Ctx){NULL, ctx->faces, ctx->language, NULL};
        float hx = pass ? (x0 + x1 - hwidth) / 2 : 0;
        hx = hint_button(c, 0, NULL, hx, hy) + 34;
        icon_check(c->canvas, hx, hy, NAME_INK);
        hx += 60;
        hx = hint_button(c, 1, NULL, hx, hy) + 10;
        line(c->canvas, hx, hy - 8, hx + 16, hy + 8, 3.0f, NAME_INK);
        line(c->canvas, hx + 16, hy - 8, hx, hy + 8, 3.0f, NAME_INK);
        hwidth = hx + 16;
    }
}

static void draw_overlay(const Ctx *ctx, const PwOverlay *overlay)
{
    if (!overlay) return;
    overlay_backdrop(ctx->canvas);
    text_line(ctx, 1, 26, say(ctx, PW_TEXT_SET_OVERLAY), PW_OVERLAY_CARD_X + 6, PW_OVERLAY_CARD_Y - 32, PW_OVERLAY_CARD_W,
              NAME_INK);
    overlay_card(ctx, overlay);
    /* the rail: a round glass button each, its picture, and the focused one's name under it */
    for (int i = 0; i < PW_OVERLAY_TILES; ++i) {
        const PwOverlayTile t = pw_overlay_tile(i);
        const float cx = t.x + t.w / 2, cy = t.y + t.h / 2, r = t.w / 2;
        const int focus = overlay->focus == i;
        glass_face(ctx->canvas, t.x, t.y, t.x + t.w, t.y + t.h, r, focus);
        overlay_icon(ctx->canvas, i, cx, cy, r * 0.5f, focus ? 0xffffffffu : KEY_INK, i == PW_OVERLAY_MOUSE && overlay->mouse_off);
        const int on = i == PW_OVERLAY_STATS ? overlay->stats : i == PW_OVERLAY_MOUSE ? !overlay->mouse_off : -1;
        if (on >= 0) {   /* a toggle's state: a green or grey dot at its corner */
            disc(ctx->canvas, cx + r * 0.72f, cy - r * 0.72f, 9, 0xffffffffu, 1);
            disc(ctx->canvas, cx + r * 0.72f, cy - r * 0.72f, 7, on ? rgb(0x5c, 0xc2, 0x2a) : rgb(150, 165, 185), 1);
        }
        if (focus) centred_line(ctx, 1, 22, say(ctx, overlay_names[i]), cx, t.y + t.h + 30, 1920 - (PW_OVERLAY_CARD_X +
                                PW_OVERLAY_CARD_W) - 24, NAME_INK);
    }
}

static uint32_t premultiplied(uint32_t color, float alpha)
{
    const unsigned a = (unsigned)(clampf(alpha, 0, 1) * 255 + 0.5f);
    uint32_t out = a << 24;
    for (int shift = 0; shift < 24; shift += 8) out |= (((color >> shift) & 0xff) * a + 127) / 255 << shift;
    return out;
}

void pw_steam_screen_hud(uint32_t *canvas, const PwSteamFaces *faces, const PwHud *hud)
{
    const int x0 = PW_HUD_X, y0 = PW_HUD_Y, x1 = PW_HUD_X + PW_HUD_W, y1 = PW_HUD_Y + PW_HUD_H;
    const Ctx ctx = {canvas, faces, NULL, NULL};
    const int saved[4] = {clip.x0, clip.y0, clip.x1, clip.y1};
    clip.x0 = x0, clip.y0 = y0, clip.x1 = x1, clip.y1 = y1;
    for (int y = y0; y < y1; ++y) memset(canvas + (size_t)y * W + x0, 0, PW_HUD_W * sizeof(uint32_t));
    /* the glass: the panel's slab, a sheen on its top, a white rim; the corners stay clear */
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float cover = clampf(0.5f - rounded(x + 0.5f, y + 0.5f, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 18), 0, 1);
            if (cover <= 0) continue;
            const float t = (float)(y - y0) / PW_HUD_H;
            canvas[(size_t)y * W + x] = premultiplied(mix(rgb(228, 243, 255), rgb(206, 232, 252), t), (0.62f + 0.12f * t) * cover);
            if (t < 0.3f) blend(canvas, x, y, 0xffffffffu, 0.35f * (1 - t / 0.3f) * cover);
        }
    ring_rounded(canvas, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 18, 0xffffffffu, 0.95f, 2.0f);
    char text[PW_HUD_LINE];
    const float left = x0 + 18;
    pw_hud_line(hud, 0, text, sizeof(text));
    text_line(&ctx, 1, 30, text, left, y0 + 40, PW_HUD_W - 36, KEY_INK);
    hud_graph(canvas, hud, left, y0 + 50, x1 - 18, y0 + 92);
    hud_rows(&ctx, hud, 1, left, y0 + 118, 26, 22, PW_HUD_W - 36, x1 - 18);
    clip.x0 = saved[0], clip.y0 = saved[1], clip.x1 = saved[2], clip.y1 = saved[3];
}

void pw_steam_screen_keyboard_changed(uint32_t *canvas, const uint32_t *backdrop, const PwSteamScreenState *state,
                                      const PwSteamFaces *faces, const struct PwVkbd *before)
{
    const PwVkbd *kbd = state->keyboard;
    if (!before || !kbd || before->page != kbd->page || before->shift != kbd->shift) {
        pw_steam_screen_draw(canvas, backdrop, state, faces, NULL);
        return;
    }
    const Ctx ctx = {canvas, faces, state->language, NULL};
    if (before->length != kbd->length || memcmp(before->text, kbd->text, kbd->length * sizeof(kbd->text[0])))
        keyboard_region(&ctx, kbd, (int)PW_VKBD_FIELD_X - KEY_MARGIN, (int)PW_VKBD_FIELD_Y - KEY_MARGIN,
                        (int)(PW_VKBD_FIELD_X + PW_VKBD_FIELD_W) + KEY_MARGIN + 1, (int)(PW_VKBD_FIELD_Y + PW_VKBD_FIELD_H) + KEY_MARGIN + 1);
    if (before->row != kbd->row || before->col != kbd->col) {
        const PwVkbd *both[2] = {before, kbd};
        for (int i = 0; i < 2; ++i) {
            PwVkbdKey key;
            if (pw_vkbd_key(kbd, both[i]->row, both[i]->col, &key))
                keyboard_region(&ctx, kbd, (int)key.x - GLOW, (int)key.y - GLOW, (int)(key.x + key.w) + GLOW + 1,
                                (int)(key.y + key.h) + GLOW + 1);
        }
    }
}

void pw_steam_handover_checklist(PwSteamHandover step, PwSteamChecklist *out)
{
    static const PwSteamChecklist ready = {PW_TEXT_HANDOVER_READY_GAME, PW_TEXT_HANDOVER_READY, PW_TEXT_HANDOVER_READY_BODY, 3, 0,
                                           {PW_TEXT_HANDOVER_SAVE, PW_TEXT_HANDOVER_ROOM, PW_TEXT_HANDOVER_START_GAME},
                                           {PW_TEXT_HANDOVER_SAVE, PW_TEXT_HANDOVER_ROOM, PW_TEXT_HANDOVER_START}};
    static const PwSteamChecklist back = {PW_TEXT_HANDOVER_BACK, PW_TEXT_HANDOVER_BACK, PW_TEXT_HANDOVER_BACK_BODY, 2, 0,
                                          {PW_TEXT_HANDOVER_CLOUD, PW_TEXT_HANDOVER_REOPEN},
                                          {PW_TEXT_HANDOVER_CLOUD, PW_TEXT_HANDOVER_REOPEN}};
    static const PwSteamChecklist leaving = {PW_TEXT_HANDOVER_SAVE, PW_TEXT_HANDOVER_SAVE, PW_TEXT_HANDOVER_BACK_BODY, 0, 0,
                                             {0}, {0}};
    static const PwSteamChecklist stuck = {PW_TEXT_HANDOVER_STUCK, PW_TEXT_HANDOVER_STUCK, PW_TEXT_HANDOVER_STUCK_BODY, 0, 0,
                                           {0}, {0}};
    switch (step) {
    case PW_STEAM_HANDOVER_SAVE: *out = ready; break;
    case PW_STEAM_HANDOVER_ROOM: *out = ready; out->done = 1; break;
    case PW_STEAM_HANDOVER_START: *out = ready; out->done = 2; break;
    case PW_STEAM_HANDOVER_CLOUD: *out = back; break;
    case PW_STEAM_HANDOVER_REOPEN: *out = back; out->done = 1; break;
    case PW_STEAM_HANDOVER_LEAVING: *out = leaving; break;
    default: *out = stuck; break;
    }
}

void pw_steam_screen_draw(uint32_t *canvas, const uint32_t *backdrop, const PwSteamScreenState *state,
                          const PwSteamFaces *faces, PwSteamScreenReport *report)
{
    memcpy(canvas, backdrop, (size_t)W * H * sizeof(*canvas));
    if (report) report->bottom = 0, report->cut = 0, report->missing = 0;
    if (state->screen == PW_STEAM_SCREEN_NONE) return;
    const Ctx ctx = {canvas, faces, state->language, report};
    if (state->screen == PW_STEAM_SCREEN_KEYBOARD) {
        draw_keyboard(&ctx, state->keyboard);
        return;
    }
    if (state->screen == PW_STEAM_SCREEN_OVERLAY) {
        draw_overlay(&ctx, state->overlay);
        return;
    }
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
    case PW_STEAM_SCREEN_HANDOVER: {
        /* "Steam", the headline and its line, the tube moving while the title waits, and the checklist (a tick for each
         * step done, a ring for each to come); the choice instead when Steam has gone quiet */
        text_line(&ctx, 0, 26, "Steam", x, PW_STEAM_CARD_Y0 + 92, width, SOFT);
        PwSteamChecklist list;
        pw_steam_handover_checklist(state->handover, &list);
        const int named = state->game && state->game[0] && pw_steam_screen_persona_drawable(faces, state->game);
        const char *args[] = {state->game};
        char title[320], body[640];
        if (named) pw_setup_fill(title, sizeof(title), say(&ctx, list.title_named), args, 1);
        else snprintf(title, sizeof(title), "%s", say(&ctx, list.title));
        if (state->handover == PW_STEAM_HANDOVER_STUCK) {
            char minutes[16];
            snprintf(minutes, sizeof(minutes), "%u", state->seconds / 60 ? state->seconds / 60 : 1);
            const char *minute_args[] = {minutes};
            pw_setup_fill(body, sizeof(body), say(&ctx, list.body), minute_args, 1);
        } else {
            snprintf(body, sizeof(body), "%s", say(&ctx, list.body));
        }
        /* A long game name: the headline in two lines, smaller, and the line below them */
        float body_y = 414;
        const Pen big = pen(&ctx, 1, 58);
        if (measure(&big, title, strlen(title)) <= width) {
            text_line(&ctx, 1, 58, title, x, 340, width, INK);
        } else {
            const Pen p = pen(&ctx, 1, 44);
            Lines lines;
            wrap(&p, title, width, &lines);
            const int shown = lines.count < 2 ? lines.count : 2;
            int missing = 0;
            for (int i = 0; i < shown; ++i)
                missing += draw(canvas, &p, lines.start[i], lines.length[i], x, 318 + i * 44 * 1.3f, INK, 1.0f);
            reached(&ctx, 318 + (shown - 1) * 44 * 1.3f + 44 * 0.3f, lines.count - shown, missing);
            body_y = 318 + shown * 44 * 1.3f + 40;
        }
        if (state->handover == PW_STEAM_HANDOVER_STUCK) {
            paragraph(&ctx, 31, body, x, body_y, width, 4, MUTED);
            const float left = pad_hint(&ctx, 0, PW_TEXT_HANDOVER_WAIT, PW_STEAM_TEXT_X1 + 22, notes);
            (void)pad_hint(&ctx, 1, PW_TEXT_HANDOVER_CLOSE, left - 24, notes);
            break;
        }
        paragraph(&ctx, 31, body, x, body_y, width, 3, MUTED);   /* three lines end above the tube, the headline in two too */
        tube_track(&ctx);
        for (unsigned i = 0; i < list.count; ++i) {
            const float cy = PW_STEAM_TUBE_Y1 + 72 + i * 56.0f;
            const int done = i < list.done;
            if (done) {
                disc(canvas, x + 18, cy, 18, GREEN, 1.0f);
                line(canvas, x + 9, cy + 1, x + 15, cy + 7, 3.5f, 0xffffffffu);   /* a tick inside the disc */
                line(canvas, x + 15, cy + 7, x + 27, cy - 6, 3.5f, 0xffffffffu);
            } else {
                ring_rounded(canvas, x, cy - 18, x + 36, cy + 18, 18, i == list.done ? BLUE : SOFT, 1.0f, 3.0f);
            }
            char item[320];
            if (named) pw_setup_fill(item, sizeof(item), say(&ctx, list.item_named[i]), args, 1);
            else snprintf(item, sizeof(item), "%s", say(&ctx, list.item[i]));
            text_line(&ctx, done || i == list.done, 31, item, x + 60, cy + 11, width - 60, done || i == list.done ? INK : SOFT);
        }
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
    case PW_STEAM_SCREEN_HANDOVER:
        if (state->handover != PW_STEAM_HANDOVER_STUCK)
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
