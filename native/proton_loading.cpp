// SPDX-License-Identifier: GPL-3.0-or-later
#include "proton_loading.h"
#include "../src/pw_qrcodegen.h"
#include "../ps5/src/platform.h"
#include "../ps5/src/proton_launch.h"
#include "../src/pw_loading_art.h"
#include "../src/pw_setup.h"
#include "../src/pw_steam_screen.h"
// The declarations only: Dear ImGui's imgui.cpp defines STB_IMAGE_IMPLEMENTATION in this title already, so the session
// screen owns no second decoder (the poster art is decoded here, on the drawing thread).
#include "../external/tinygltf/stb_image.h"
#include <ps5platform/kernel.h>
#include <ps5platform/videoout.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <vector>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

extern "C" int scePthreadSetprio(pthread_t thread, int priority);
extern "C" int __real_sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *parameter);

// The Steam runtime's setup page owns the display: the SteamCMD console window is not a game presenter.
static std::atomic<bool> display_keep{false};

// A native presenter takes the display only when it opens VideoOut, not when
// the broker initializes its GPU. Wine's PRX presenters use their existing
// release_display callback; this covers the broker's statically linked presenter.
// While the Steam runtime owns the display, a second presenter is refused instead: the page stays, and the
// SteamCMD keeps its output on the pipe.
extern "C" int __wrap_sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *parameter)
{
    if (display_keep.load(std::memory_order_acquire)) return -1;
    proton_loading_stop("the game's presenter takes the display");
    return __real_sceVideoOutOpen(user, bus, index, parameter);
}

// ImGui's copy of stb_truetype (public domain / MIT), compiled privately here as imgui_draw.cpp does.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../external/imgui/imstb_truetype.h"
#pragma clang diagnostic pop

namespace {
constexpr unsigned kWidth = 1920, kHeight = 1080;
constexpr unsigned kBlocksX = (kWidth + 127) / 128, kBlocksY = (kHeight + 127) / 128;
constexpr size_t kFrameBytes = size_t(kBlocksX) * kBlocksY << 16;
constexpr size_t kAlign = size_t(2) << 20;
constexpr size_t kBufferBytes = (kFrameBytes + kAlign - 1) / kAlign * kAlign;
constexpr size_t kMemoryBytes = kBufferBytes * 2;
// Write-combined direct memory the display scans out and the CPU writes (PS5_Vulkan's demo renderer).
constexpr int kMemoryType = 3;
constexpr int kProtection = PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE | PS5_KERNEL_PROT_GPU_READ |
                            PS5_KERNEL_PROT_GPU_WRITE;
// The part of a game's start that moves (src/pw_loading_art.h): the globe and its bubbles, the line under the name, the
// bar.
constexpr unsigned kDynX0 = PW_LOADING_DYN_X0, kDynX1 = PW_LOADING_DYN_X1, kDynY0 = PW_LOADING_DYN_Y0,
                   kDynY1 = PW_LOADING_DYN_Y1;
static_assert(kWidth == PW_LOADING_WIDTH && kHeight == PW_LOADING_HEIGHT, "the art is drawn for this screen");

// Where pixel (x, y) lies in a buffer of VideoOut tiling 0, 64 KiB tiles of 128x128 four-byte
// pixels: the swizzle PS5_Vulkan measured, which its demo renderer writes from the CPU.
size_t tiled(unsigned x, unsigned y)
{
    const uint32_t offset = ((y << 4) & 0x70u) ^ ((y << 5) & 0xf00u) ^ ((y << 9) & 0x1000u) ^ ((y << 8) & 0x4000u) ^
                            ((x << 2) & 0xcu) ^ ((x << 5) & 0x380u) ^ ((x << 4) & 0x400u) ^ ((x << 6) & 0x800u) ^
                            ((x << 9) & 0xa000u);
    return (size_t((y >> 7) * kBlocksX + (x >> 7)) << 16) + offset;
}

// Pixels are 0xAARRGGBB: bytes B, G, R, A, as the display reads PS5_VIDEO_OUT_PIXEL_FORMAT_B8G8R8A8_SDR.
constexpr uint32_t rgb(unsigned r, unsigned g, unsigned b) { return 0xff000000u | (r << 16) | (g << 8) | b; }
uint32_t blend(uint32_t under, uint32_t over, unsigned alpha) { return pw_loading_blend(under, over, alpha); }

// The session screen's theme: MihawkOS's own Frutiger Aero tokens, over the sky-and-meadow backdrop the loading screen
// draws (src/pw_loading_art.h). Panels are opaque deep-sky blues so white text keeps its contrast over any part of the
// backdrop; focus and progress are the glass tube's green. The white QR card stays white: contrast is a hard
// requirement. The fonts are the Inter-SemiBold title font and Inter-Regular text font the title already ships.
struct Palette {
    uint32_t background = rgb(0x08, 0x2e, 0x8c);   // deep navy: the QR's modules, cut-outs, fades at the screen's foot
    uint32_t surface = rgb(0x14, 0x54, 0xbe);      // a panel
    uint32_t surface_high = rgb(0x28, 0x6e, 0xd7); // a poster's placeholder, a raised panel
    uint32_t secondary = rgb(0x1e, 0x5f, 0xc8);
    uint32_t accent = rgb(0x55, 0xd5, 0x2a);       // the tube's liquid green: focus rings, the mark, a rail bar
    uint32_t on_accent = rgb(0x0a, 0x28, 0x14);
    uint32_t text = rgb(0xff, 0xff, 0xff);
    uint32_t text_muted = rgb(0xcd, 0xe8, 0xff);
    uint32_t muted = rgb(0x96, 0xbe, 0xeb);
    uint32_t success = rgb(0x8c, 0xf0, 0x5a);
    uint32_t warning = rgb(0xff, 0xd6, 0x5a);
    uint32_t danger = rgb(0xff, 0x6e, 0x64);
    float radius = 10.0f;
    float radius_card = 18.0f;                    // the tip card's radius
};
constexpr Palette kTheme;                         // 1 px = one token: the canvas is 1920x1080

uint64_t now_ns()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec) * 1000000000u + uint64_t(now.tv_nsec);
}

struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

bool load_font(Font &font, const std::string &path)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size > 0) {
        font.data.resize(size_t(size));
        if (fread(font.data.data(), 1, font.data.size(), file) != font.data.size()) font.data.clear();
    }
    fclose(file);
    font.ok = !font.data.empty() &&
              stbtt_InitFont(&font.info, font.data.data(), stbtt_GetFontOffsetForIndex(font.data.data(), 0)) != 0;
    return font.ok;
}

std::vector<uint32_t> codepoints(const std::string &text)
{
    std::vector<uint32_t> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        uint32_t cp = length == 1 ? c : c & (0x3fu >> (length - 1));
        for (int k = 1; k < length && i + size_t(k) < text.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + size_t(k)]) & 0x3fu);
        out.push_back(cp);
        i += size_t(length);
    }
    return out;
}

float text_width(const Font &font, float pixels, const std::vector<uint32_t> &text)
{
    const float scale = stbtt_ScaleForPixelHeight(&font.info, pixels);
    float width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&font.info, int(text[i]), &advance, &bearing);
        width += float(advance) * scale;
        if (i + 1 < text.size()) width += float(stbtt_GetCodepointKernAdvance(&font.info, int(text[i]), int(text[i + 1]))) * scale;
    }
    return width;
}

struct Screen {
    std::mutex lock;
    bool running = false;
    std::atomic<bool> stop{false};
    pthread_t thread{};
    int video = -1;
    int64_t physical = 0;
    void *mapped = nullptr;
    uint8_t *buffers[2] = {};
    std::vector<uint32_t> canvas, base;
    Font title_font, text_font;
    std::string name;
    uint64_t started = 0;
};
Screen screen;

// Draws text with its baseline at y, centred on x when centre is set, cut with an ellipsis to
// max_width; coverage from stb_truetype, blended over what is there.
void draw_text(std::vector<uint32_t> &canvas, const Font &font, float pixels, std::string text, float x, int y,
               uint32_t color, bool centre, float max_width = 1e9f, unsigned opacity = 255)
{
    if (!font.ok) return;
    std::vector<uint32_t> cps = codepoints(text);
    if (text_width(font, pixels, cps) > max_width) {
        while (!cps.empty()) {
            cps.pop_back();
            std::vector<uint32_t> cut = cps;
            cut.push_back(0x2026);
            if (text_width(font, pixels, cut) <= max_width) { cps = cut; break; }
        }
    }
    const float scale = stbtt_ScaleForPixelHeight(&font.info, pixels);
    float pen = centre ? x - text_width(font, pixels, cps) / 2 : x;
    std::vector<unsigned char> glyph;
    for (size_t i = 0; i < cps.size(); ++i) {
        const int cp = int(cps[i]);
        const float shift = pen - std::floor(pen);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&font.info, cp, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            glyph.assign(size_t(w) * size_t(h), 0);
            stbtt_MakeCodepointBitmapSubpixel(&font.info, glyph.data(), w, h, w, scale, scale, shift, 0, cp);
            for (int gy = 0; gy < h; ++gy) {
                const int py = y + y0 + gy;
                if (py < 0 || py >= int(kHeight)) continue;
                for (int gx = 0; gx < w; ++gx) {
                    const int px = int(std::floor(pen)) + x0 + gx;
                    const unsigned alpha = glyph[size_t(gy) * size_t(w) + size_t(gx)];
                    if (px < 0 || px >= int(kWidth) || !alpha) continue;
                    uint32_t &pixel = canvas[size_t(py) * kWidth + size_t(px)];
                    pixel = blend(pixel, color, alpha * opacity / 255);
                }
            }
        }
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&font.info, cp, &advance, &bearing);
        pen += float(advance) * scale;
        if (i + 1 < cps.size()) pen += float(stbtt_GetCodepointKernAdvance(&font.info, cp, int(cps[i + 1]))) * scale;
    }
}

// Right-aligned text: draw_text's x is a left edge or a centre, so the footer credit's width is measured here.
void draw_text_right(const Font &font, float pixels, const std::string &text, float right, int y, uint32_t color)
{
    const std::vector<uint32_t> cps = codepoints(text);
    draw_text(screen.base, font, pixels, text, right - text_width(font, pixels, cps), y, color, false);
}

void draw_dot(std::vector<uint32_t> &canvas, float cx, float cy, float radius, uint32_t color, unsigned strength)
{
    for (int y = int(cy - radius - 1); y <= int(cy + radius + 1); ++y)
        for (int x = int(cx - radius - 1); x <= int(cx + radius + 1); ++x) {
            if (x < 0 || y < 0 || x >= int(kWidth) || y >= int(kHeight)) continue;
            const float d = std::sqrt((float(x) + 0.5f - cx) * (float(x) + 0.5f - cx) +
                                      (float(y) + 0.5f - cy) * (float(y) + 0.5f - cy)) - radius;
            const float coverage = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (coverage <= 0) continue;
            uint32_t &pixel = canvas[size_t(y) * kWidth + size_t(x)];
            pixel = blend(pixel, color, unsigned(coverage * float(strength)));
        }
}

// A stroked circle, antialiased: the loading screen's mark is a ring, not an image.
void draw_ring(std::vector<uint32_t> &canvas, float cx, float cy, float radius, float thickness, uint32_t color)
{
    const float outer = radius + thickness / 2, inner = radius - thickness / 2;
    for (int y = int(cy - outer - 1); y <= int(cy + outer + 1); ++y)
        for (int x = int(cx - outer - 1); x <= int(cx + outer + 1); ++x) {
            if (x < 0 || y < 0 || x >= int(kWidth) || y >= int(kHeight)) continue;
            const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float coverage = std::clamp(std::min(distance - inner + 0.5f, outer - distance + 0.5f), 0.0f, 1.0f);
            if (coverage <= 0) continue;
            uint32_t &pixel = canvas[size_t(y) * kWidth + size_t(x)];
            pixel = blend(pixel, color, unsigned(coverage * 255));
        }
}

// The spinner: the loading screen's glass globe with its orbiting bubbles (src/pw_loading_art.h), radius the globe's.
// Drawn over the canvas copy of the static base, so nothing else moves.
void draw_spinner(unsigned frame, float cx, float cy, float radius, std::vector<uint32_t> &canvas)
{
    pw_loading_art_spinner(canvas.data(), cx, cy, radius, frame);
}

// The Aurelia session screen (proton_loading.h): the client's sign-in / loading / library screen drawn on the canvas
// instead of the spinner. One visual language for every phase: the Aero tokens (above), a centred mark and name, the
// phase the session actually knows, the last status it reported, and the footer.
// The library page has three focus regions: the tab bar, the left rail and the poster grid. Up from the grid's
// first row reaches the tabs; left from its first column reaches the rail; their Cross selects, and the grid's Cross
// opens the game. Home leaves the session back to the launcher.
enum class NavRegion { tabs, rail, grid };
struct Overlay {
    std::mutex lock;
    bool on = false;
    bool dirty = false;
    bool error = false;     // the status is an error, drawn in the theme's danger colour
    int percent = -1;
    bool library = false;   // the browse screen instead of the sign-in card
    bool setup = false;     // the Steam runtime's first-run page (proton_aurelia_setup)
    std::string setup_account, setup_password, setup_guard, setup_status;
    int setup_focus = 0;
    bool setup_error = false, setup_ready = false;
    int focus = 0;
    long chosen = 0;
    NavRegion region = NavRegion::grid;
    int tab = 1, rail = 1, filter = 0;   // Library; All; no filter
    std::string notice;                   // a tab/rail action the session does not have, shown in the top bar
    std::vector<unsigned char> installed; // one per games entry, fixed when the library arrives
    std::string account;
    std::vector<std::pair<unsigned, std::string>> games;
    std::string url, status, phase;
    bool report = false;                  // the tester probe's verdict (proton_loading_report), drawn over everything
    std::string report_title;
    std::vector<std::string> report_lines;
};
Overlay overlay;

void fill_rect(std::vector<uint32_t> &canvas, int x0, int y0, int x1, int y1, uint32_t color)
{
    for (int y = std::max(0, y0); y < std::min(int(kHeight), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(int(kWidth), x1); ++x) canvas[size_t(y) * kWidth + size_t(x)] = color;
}

// A filled rectangle with the theme's corner radius. Rows outside the corner bands are full width; each corner row is
// inset by the circle's chord, which is cheap and exact enough at 8 or 10 px.
void fill_round_rect(std::vector<uint32_t> &canvas, int x0, int y0, int x1, int y1, float radius, uint32_t color)
{
    if (x1 <= x0 || y1 <= y0) return;
    const int r = std::max(0, std::min(int(radius), std::min(x1 - x0, y1 - y0) / 2));
    for (int y = std::max(0, y0); y < std::min(int(kHeight), y1); ++y) {
        int inset = 0;
        const int dy = std::min(y - y0, y1 - 1 - y);
        if (dy < r) {
            const float d = float(r) - 0.5f - float(dy);
            inset = r - int(std::sqrt(float(r) * r - d * d) + 0.5f);
        }
        fill_rect(canvas, x0 + inset, y, x1 - inset, y + 1, color);
    }
}

// The mark: an accent ring with a geometric A in it, drawn from primitives (no binary asset).
void draw_mark(int cx, int cy)
{
    draw_ring(screen.base, float(cx), float(cy), 30.0f, 4.0f, kTheme.accent);
    draw_text(screen.base, screen.title_font, 38, "A", float(cx), cy + 13, kTheme.accent, true, 60);
}

void draw_header()
{
    draw_mark(960, 88);
    draw_text(screen.base, screen.title_font, 54, "Aurelia", 960, 178, kTheme.text, true, 1400);
}

void draw_footer()
{
    draw_text_right(screen.text_font, 22, "MihawkOS", PW_LOADING_FOOTER_X, PW_LOADING_FOOTER_Y, PW_LOADING_TEXT_SOFT);
}

// The phase the session is in (its own words) and the last thing it reported; the phase is green once it is a result,
// not a wait. The status is the theme's danger colour when the session reported an error.
void draw_phase(const std::string &phase, const std::string &status, int baseline, uint32_t phase_color, uint32_t status_color)
{
    draw_text(screen.base, screen.text_font, 30, phase, 960, baseline, phase_color, true, 1400);
    if (!status.empty()) draw_text(screen.base, screen.text_font, 22, status, 960, baseline + 38, status_color, true, 1400);
}

// The install's determinate bar: the glass tube (src/pw_loading_art.h) filled to the percent, centred on y.
void draw_progress_bar(int percent, int x, int y, int width)
{
    const int clamped = std::max(0, std::min(100, percent));
    pw_loading_art_tube(screen.base.data(), float(x), float(y - 10), float(x + width), float(y + 18),
                        float(clamped) / 100.0f, 0);
}

// The QR on a white card, 600x600 with a 40 px quiet zone: contrast is a hard requirement, not a style choice, so the
// card is white whatever the theme and the modules are the theme's near-black.
void draw_qr(const std::string &url)
{
    static uint8_t code[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
    const int card = 600, card_x = int(kWidth) / 2 - card / 2, card_y = 306;
    fill_rect(screen.base, card_x, card_y, card_x + card, card_y + card, rgb(255, 255, 255));
    if (!qrcodegen_encodeText(url.c_str(), temp, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        draw_text(screen.base, screen.text_font, 24, "The QR could not be drawn", kWidth / 2.0f, card_y + card / 2,
                  kTheme.danger, true, card - 80);
        return;
    }
    const int size = qrcodegen_getSize(code), quiet = 40, module = (card - 2 * quiet) / size;
    const int left = card_x + (card - module * size) / 2, top = card_y + (card - module * size) / 2;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (qrcodegen_getModule(code, x, y))
                fill_rect(screen.base, left + x * module, top + y * module, left + (x + 1) * module,
                          top + (y + 1) * module, kTheme.background);
}

// The library page: the covers the PC fetched (tools/fetch-aurelia-covers.py) at
// /data/ps5proton/covers/<app id>.jpg, decoded once and resampled once to the 220x330 poster; the cache holds the
// five columns x four rows the grid can touch. A missing picture leaves the theme's placeholder, never a failed screen.
// The library's own layout: five columns of 220x330 posters (2:3) on 36 px gaps, with 14 px glass corners.
constexpr int kColumns = 5;
constexpr int kPosterW = 220, kPosterH = 330, kPosterRadius = 14;
constexpr int kGridX = 498, kGridY = 160, kGridGapX = 36, kGridGapY = 44;
struct PosterCover {
    unsigned id = 0;
    uint64_t used = 0;
    std::vector<uint32_t> pixels;   // kPosterW x kPosterH, cover-fit, opaque
};
std::vector<PosterCover> poster_covers;
uint64_t cover_uses;

uint32_t mix_color(uint32_t a, uint32_t b, float t)
{
    const float keep = 1.0f - t;
    return rgb(unsigned(((a >> 16) & 0xff) * keep + ((b >> 16) & 0xff) * t),
               unsigned(((a >> 8) & 0xff) * keep + ((b >> 8) & 0xff) * t),
               unsigned((a & 0xff) * keep + (b & 0xff) * t));
}

// The source image cover-fit into the poster once; the drawing loop then copies pixels. Every poster is full-bleed
// (the owner's choice): a wide store header or hero is cropped to the 2:3 tile, never letterboxed. A 3x3 box around
// each output pixel replaces one bilinear tap, so the art's own text stays smooth at 220x330.
void fit_cover(const stbi_uc *rgba, int width, int height, std::vector<uint32_t> &out)
{
    out.assign(size_t(kPosterW) * kPosterH, kTheme.surface_high);
    const float scale = std::max(float(kPosterW) / width, float(kPosterH) / height);
    const float draw_w = float(width) * scale, draw_h = float(height) * scale;
    const float off_x = (float(kPosterW) - draw_w) * 0.5f, off_y = (float(kPosterH) - draw_h) * 0.5f;
    const auto sample = [&](float fx, float fy) {
        const int x0 = std::clamp(int(std::floor(fx)), 0, width - 1);
        const int y0 = std::clamp(int(std::floor(fy)), 0, height - 1);
        const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
        const float tx = std::clamp(fx - float(x0), 0.0f, 1.0f);
        const float ty = std::clamp(fy - float(y0), 0.0f, 1.0f);
        const auto at = [&](int x, int y) {
            const stbi_uc *pixel = rgba + (size_t(y) * size_t(width) + size_t(x)) * 4;
            return rgb(pixel[0], pixel[1], pixel[2]);
        };
        return mix_color(mix_color(at(x0, y0), at(x1, y0), tx), mix_color(at(x0, y1), at(x1, y1), tx), ty);
    };
    const float radius = scale < 1.0f ? 0.5f / scale : 0.0f;   // half the source footprint this output pixel covers
    for (int py = 0; py < kPosterH; ++py)
        for (int px = 0; px < kPosterW; ++px) {
            const float cx = (float(px) + 0.5f - off_x) / scale;
            const float cy = (float(py) + 0.5f - off_y) / scale;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const uint32_t pixel = sample(cx + float(ox) * radius - 0.5f, cy + float(oy) * radius - 0.5f);
                    r += (pixel >> 16) & 0xff;
                    g += (pixel >> 8) & 0xff;
                    b += pixel & 0xff;
                    ++n;
                }
            out[size_t(py) * kPosterW + size_t(px)] = rgb(r / n, g / n, b / n);
        }
}

const PosterCover *cover_for(unsigned id)
{
    for (auto &cover : poster_covers)
        if (cover.id == id) {
            cover.used = ++cover_uses;
            return &cover;
        }
    char path[128];
    snprintf(path, sizeof(path), PROTON_DATA_ROOT "/covers/%u.jpg", id);
    int width = 0, height = 0, channels = 0;
    stbi_uc *rgba = stbi_load(path, &width, &height, &channels, 4);
    PosterCover made;
    made.id = id;
    made.used = ++cover_uses;
    if (rgba) {
        fit_cover(rgba, width, height, made.pixels);
        stbi_image_free(rgba);
    }
    if (poster_covers.size() >= 24)
        poster_covers.erase(std::min_element(poster_covers.begin(), poster_covers.end(),
                                             [](const PosterCover &a, const PosterCover &b) { return a.used < b.used; }));
    poster_covers.push_back(std::move(made));
    return &poster_covers.back();
}

// A rounded rectangle's signed distance (negative inside), for glows and rims.
float rounded_distance(float px, float py, float x0, float y0, float x1, float y1, float r)
{
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2 - r, hy = (y1 - y0) / 2 - r;
    const float qx = std::fabs(px - cx) - hx, qy = std::fabs(py - cy) - hy;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Aero focus: a soft aqua glow spreading 16 px outside the rectangle and a thin white rim 3 px off it. rim_only draws
// the 1 px glass edge a panel or a capsule carries instead.
void draw_glow(int x0, int y0, int x1, int y1, float radius, bool rim_only = false)
{
    const int spread = rim_only ? 2 : 18;
    for (int y = std::max(0, y0 - spread); y < std::min(int(kHeight), y1 + spread); ++y)
        for (int x = std::max(0, x0 - spread); x < std::min(int(kWidth), x1 + spread); ++x) {
            const float d = rounded_distance(float(x) + 0.5f, float(y) + 0.5f, float(x0), float(y0), float(x1), float(y1), radius);
            uint32_t &pixel = screen.base[size_t(y) * kWidth + size_t(x)];
            if (rim_only) {
                const float edge = std::clamp(1.0f - std::fabs(d + 0.5f), 0.0f, 1.0f);
                if (edge > 0) pixel = blend(pixel, rgb(255, 255, 255), unsigned(edge * 120));
                continue;
            }
            if (d > 0) pixel = blend(pixel, rgb(0x6a, 0xe8, 0xff), unsigned(200.0f * std::exp(-(d / 7.0f) * (d / 7.0f))));
            const float rim = std::clamp(1.2f - std::fabs(d - 3.0f), 0.0f, 1.0f);
            if (rim > 0) pixel = blend(pixel, rgb(255, 255, 255), unsigned(rim * 235));
        }
}

// A poster: 14 px glass corners, the capsule art, an aqua glow when focused with a frosted navy plate at its foot
// carrying the title and status, and the round status badge at the top-right.
void draw_poster(int x, int y, int w, int h, unsigned id, const std::string &name, bool focused, bool installed)
{
    if (focused) draw_glow(x, y, x + w, y + h, float(kPosterRadius));
    fill_round_rect(screen.base, x, y, x + w, y + h, float(kPosterRadius), kTheme.surface_high);
    const PosterCover *cover = cover_for(id);
    if (w == kPosterW && h == kPosterH && !cover->pixels.empty()) {
        const int radius = kPosterRadius;
        for (int py = 0; py < h && y + py < int(kHeight); ++py) {
            const int dy = std::min(py, h - 1 - py);
            int inset = 0;
            if (dy < radius) {
                const float d = float(radius) - 0.5f - float(dy);
                inset = radius - int(std::sqrt(float(radius) * float(radius) - d * d) + 0.5f);
            }
            for (int px = inset; px < w - inset; ++px)
                screen.base[size_t(y + py) * kWidth + size_t(x + px)] = cover->pixels[size_t(py) * kPosterW + size_t(px)];
        }
    } else {
        draw_text(screen.base, screen.text_font, 22, name, x + w / 2, y + h / 2, kTheme.text_muted, true, w - 30);
    }
    if (focused) {
        // A frosted navy plate inset 10 px at the poster's foot, its glass edge lit, carrying the title and status.
        const int px0 = x + 10, px1 = x + w - 10, py0 = y + h - 82, py1 = y + h - 10;
        for (int py = std::max(0, py0); py < std::min(int(kHeight), py1); ++py)
            for (int px = px0; px < px1; ++px) {
                const float d = rounded_distance(float(px) + 0.5f, float(py) + 0.5f, float(px0), float(py0), float(px1),
                                                 float(py1), 12.0f);
                const float cover = std::clamp(0.5f - d, 0.0f, 1.0f);
                if (cover <= 0) continue;
                uint32_t &pixel = screen.base[size_t(py) * kWidth + size_t(px)];
                pixel = blend(pixel, kTheme.background, unsigned(cover * 205));
            }
        draw_glow(px0, py0, px1, py1, 12.0f, true);
        draw_text(screen.base, screen.text_font, 22, name, x + 24, y + h - 50, kTheme.text, false, w - 48);
        draw_text(screen.base, screen.text_font, 15, installed ? "Installed" : "Not installed", x + 24, y + h - 26,
                  kTheme.text_muted, false, w - 48);
    }
    const int bx = x + w - 24, by = y + 24;
    if (by < int(kHeight)) {
        draw_dot(screen.base, float(bx), float(by), 12, kTheme.background, 220);
        draw_ring(screen.base, float(bx), float(by), 11, 2, kTheme.text_muted);
        draw_text(screen.base, screen.text_font, 15, installed ? "\u2713" : "\u2193", float(bx), by + 5, kTheme.text,
                  true, 18);
    }
}

// The tab bar's icons, drawn from primitives in the theme's ink at 20 px: a house, a grid, a download tray, a
// bookmark and a gear. The tabs carry icons; text alone looked plain beside the posters.
void icon_home(int cx, int cy, uint32_t color)
{
    for (int row = 0; row < 7; ++row)
        fill_rect(screen.base, cx - row, cy - 9 + row, cx + row + 1, cy - 8 + row, color);
    fill_rect(screen.base, cx - 6, cy - 2, cx + 7, cy + 9, color);
    fill_rect(screen.base, cx - 2, cy + 3, cx + 3, cy + 10, kTheme.background);
}

void icon_grid(int cx, int cy, uint32_t color)
{
    fill_round_rect(screen.base, cx - 9, cy - 9, cx - 2, cy - 2, 2.0f, color);
    fill_round_rect(screen.base, cx + 1, cy - 9, cx + 8, cy - 2, 2.0f, color);
    fill_round_rect(screen.base, cx - 9, cy + 1, cx - 2, cy + 8, 2.0f, color);
    fill_round_rect(screen.base, cx + 1, cy + 1, cx + 8, cy + 8, 2.0f, color);
}

void icon_download(int cx, int cy, uint32_t color)
{
    fill_rect(screen.base, cx - 1, cy - 10, cx + 2, cy + 3, color);
    for (int row = 0; row < 5; ++row)
        fill_rect(screen.base, cx - row - 2, cy + 1 + row, cx + row + 3, cy + 2 + row, color);
    fill_rect(screen.base, cx - 9, cy + 7, cx - 5, cy + 10, color);
    fill_rect(screen.base, cx + 6, cy + 7, cx + 10, cy + 10, color);
    fill_rect(screen.base, cx - 9, cy + 9, cx + 10, cy + 12, color);
}

void icon_save(int cx, int cy, uint32_t color)
{
    fill_round_rect(screen.base, cx - 8, cy - 10, cx + 8, cy + 10, 2.0f, color);
    for (int row = 0; row < 6; ++row) {
        const int half = 6 - row;
        fill_rect(screen.base, cx - half, cy + 4 + row, cx + half, cy + 5 + row, kTheme.background);
    }
}

void icon_settings(int cx, int cy, uint32_t color)
{
    draw_ring(screen.base, float(cx), float(cy), 7.0f, 3.0f, color);
    for (int i = 0; i < 8; ++i) {
        const float angle = float(i) * 6.2831853f / 8.0f;
        draw_dot(screen.base, cx + std::cos(angle) * 10.0f, cy + std::sin(angle) * 10.0f, 2.2f, color, 255);
    }
}

void draw_tab(int x, int width, int icon, const char *label, bool active)
{
    // A glass capsule, 48 px tall and fully rounded, for the active tab.
    if (active) {
        fill_round_rect(screen.base, x, 58, x + width, 106, 24.0f, kTheme.surface_high);
        draw_glow(x, 58, x + width, 106, 24.0f, true);
    }
    const uint32_t ink = active ? kTheme.text : kTheme.text_muted;
    const int cx = x + 28;
    switch (icon) {
    case 0: icon_home(cx, 82, ink); break;
    case 1: icon_grid(cx, 82, ink); break;
    case 2: icon_download(cx, 82, ink); break;
    case 3: icon_save(cx, 82, ink); break;
    default: icon_settings(cx, 82, ink); break;
    }
    draw_text(screen.base, screen.title_font, active ? 26 : 24, label, x + 50, 91, ink, false, width - 58);
}

// The legend's button glyphs: a filled white circle with the dark glyph inside, and a dark pill
// for the D-pad's arrows. Text alone looked like a caption, not a controller legend.
void legend_button(int x, bool cross, const char *label)
{
    draw_dot(screen.base, float(x), 1030, 13, kTheme.text, 255);
    if (cross)
        draw_text(screen.base, screen.text_font, 18, "\u00d7", float(x), 1036, kTheme.background, true, 24);
    else
        draw_ring(screen.base, float(x), 1030, 5.5f, 2.0f, kTheme.background);
    draw_text(screen.base, screen.text_font, 22, label, x + 22, 1040, kTheme.text, false, 120);
}

// The state one library frame draws: the full library plus the installed flags, and the focus region, tab, rail row
// and filter the pad has chosen. Copied under the overlay lock in draw_base.
struct SetupView {
    std::string account, password, guard, status;
    int focus = 0;
    bool error = false, ready = false;
};

struct ShelfView {
    std::string account;
    std::vector<std::pair<unsigned, std::string>> games;
    std::vector<unsigned char> installed;
    int focus = 0;
    NavRegion region = NavRegion::grid;
    int tab = 1, rail = 1, filter = 0;
    std::string notice;
};

constexpr int kTabX[5] = {256, 404, 580, 788, 928};
constexpr int kTabW[5] = {136, 164, 196, 128, 164};
const char *const kTabLabel[5] = {"Home", "Library", "Downloads", "Saves", "Settings"};

// One row of the left rail: the selected category gets a glass capsule; the focused row a green dot beside it.
void draw_rail_item(int y, const char *label, int count, bool selected, bool focused)
{
    if (selected) {
        fill_round_rect(screen.base, 80, y, 350, y + 48, 24.0f, kTheme.surface);
        draw_glow(80, y, 350, y + 48, 24.0f, true);
    }
    if (focused) draw_dot(screen.base, 62, float(y + 24), 6, kTheme.accent, 255);
    draw_text(screen.base, screen.text_font, 26, label, 96, y + 34, selected ? kTheme.text : kTheme.text_muted,
              false, 250);
    if (count > 0)
        draw_text_right(screen.text_font, 22, std::to_string(count), 334, y + 34,
                        selected ? kTheme.text : kTheme.text_muted);
}

void draw_setup(const SetupView &view)
{
    fill_round_rect(screen.base, 460, 200, 1460, 900, kTheme.radius_card, kTheme.surface);
    draw_text(screen.base, screen.title_font, 44, "Steam runtime", 960, 280, kTheme.text, true, 1200);
    draw_text(screen.base, screen.text_font, 18, "Sign in once; the game uses this runtime after that.",
              960, 320, kTheme.muted, true, 1200);
    const char *labels[] = {"Account", "Password", "Steam Guard code"};
    const std::string *values[] = {&view.account, &view.password, &view.guard};
    for (int i = 0; i < 3; ++i) {
        const int y = 350 + i * 130;
        draw_text(screen.base, screen.text_font, 20, labels[i], 540, y, i == view.focus ? kTheme.text : kTheme.muted,
                  false, 400);
        fill_round_rect(screen.base, 540, y + 18, 1380, y + 86, 10, kTheme.background);
        if (view.focus == i) {
            fill_round_rect(screen.base, 538, y + 16, 1382, y + 88, 12, kTheme.accent);
            fill_round_rect(screen.base, 540, y + 18, 1380, y + 86, 10, kTheme.background);
        }
        // The password is visible while typing: a masked field hid keyboard-layout mistakes during sign-in.
        const std::string &shown = *values[i];
        draw_text(screen.base, screen.text_font, 26, shown, 558, y + 52, kTheme.text, false, 800);
    }
    draw_text(screen.base, screen.text_font, 16,
              "Steam Guard is the code Steam emails you or shows in the Steam mobile app when this device signs in.",
              960, 742, kTheme.muted, true, 1100);
    draw_text(screen.base, screen.text_font, 20, view.status, 960, 812, view.error ? kTheme.danger : kTheme.text_muted,
              true, 1200);
    draw_text(screen.base, screen.text_font, 18,
              view.ready ? "The runtime is signed in. Back returns to the launcher."
                         : "Type with the keyboard. Enter continues; Tab moves between the fields; Back leaves.",
              960, 860, kTheme.muted, true, 1200);
}

void draw_library(const ShelfView &view)
{
    // The library page: the tab bar (Library selected), the left rail and the five-column poster grid. Up from
    // the grid's first row reaches the tab bar, left from its first column the rail; their Cross acts, the grid's
    // Cross opens the game. Home leaves the session back to the launcher.
    draw_ring(screen.base, 100, 82, 14, 3, kTheme.accent);
    draw_text(screen.base, screen.title_font, 20, "A", 100, 89, kTheme.accent, true, 30);
    draw_text(screen.base, screen.title_font, 28, "Aurelia", 124, 91, kTheme.text, false, 240);
    for (int i = 0; i < 5; ++i) draw_tab(kTabX[i], kTabW[i], i, kTabLabel[i], i == 1);
    if (view.region == NavRegion::tabs) {
        const int i = view.tab;
        draw_glow(kTabX[i], 58, kTabX[i] + kTabW[i], 106, 24.0f);
    }
    draw_dot(screen.base, 1440, 84, 5, kTheme.success, 255);
    draw_text(screen.base, screen.text_font, 20, "Library ready", 1452, 91, kTheme.success, false, 200);
    if (!view.notice.empty())
        draw_text(screen.base, screen.text_font, 20, view.notice, 1110, 91, kTheme.warning, false, 320);
    char clock[8] = {};
    const time_t now = time(nullptr);
    struct tm local {};
    if (localtime_r(&now, &local)) strftime(clock, sizeof(clock), "%H:%M", &local);
    draw_text_right(screen.text_font, 22, clock, int(kWidth) - 64, 91, kTheme.text_muted);

    const int count = int(view.games.size());
    int installed_total = 0;
    for (int i = 0; i < count; ++i)
        if (i < int(view.installed.size()) && view.installed[size_t(i)]) ++installed_total;
    draw_text(screen.base, screen.text_font, 18,
              view.account.empty() ? std::string("Aurelia") : "Signed in as " + view.account, 96, 160,
              kTheme.text_muted, false, 260);
    draw_rail_item(200, "Installed", installed_total, view.filter == 1, view.region == NavRegion::rail && view.rail == 0);
    draw_rail_item(252, "All", count, view.filter == 0, view.region == NavRegion::rail && view.rail == 1);
    draw_rail_item(310, "Not installed", count - installed_total, view.filter == 2,
                   view.region == NavRegion::rail && view.rail == 2);
    draw_rail_item(370, "New collection +", 0, false, view.region == NavRegion::rail && view.rail == 3);

    std::vector<int> shown;
    for (int i = 0; i < count; ++i) {
        const bool installed = i < int(view.installed.size()) && view.installed[size_t(i)];
        if (view.filter == 1 && !installed) continue;
        if (view.filter == 2 && installed) continue;
        shown.push_back(i);
    }
    if (!shown.empty()) {
        const int total = int(shown.size());
        const int clamped = std::max(0, std::min(view.focus, total - 1));
        const int rows = (total + kColumns - 1) / kColumns;
        const int first_row = std::max(0, std::min(clamped / kColumns - 1, rows - 3));
        for (int row = first_row; row < first_row + 3 && row < rows; ++row)
            for (int col = 0; col < kColumns; ++col) {
                const int index = row * kColumns + col;
                if (index >= total) break;
                const int game = shown[size_t(index)];
                draw_poster(kGridX + col * (kPosterW + kGridGapX), kGridY + (row - first_row) * (kPosterH + kGridGapY),
                            kPosterW, kPosterH,
                            view.games[size_t(game)].first, view.games[size_t(game)].second,
                            index == clamped && view.region == NavRegion::grid,
                            game < int(view.installed.size()) && view.installed[size_t(game)]);
            }
    } else {
        draw_text(screen.base, screen.title_font, 56,
                  view.filter == 1 ? "Nothing installed yet" : view.filter == 2 ? "Everything is installed" : "No games here",
                  1134, 434, kTheme.text, true, 900);
        draw_text(screen.base, screen.text_font, 26,
                  view.filter == 1 ? "Choose a game from All and press Cross to install it"
                                   : view.filter == 2 ? "There is nothing left to install" : "The library is empty",
                  1134, 486, kTheme.text_muted, true, 900);
    }

    // The legend sits over the grid at the bottom; a short fade keeps the third row of covers readable and the
    // hints clear (the reference's legend is at y=986 over a poster row that continues beneath it).
    for (int row = 0; row < 90; ++row) {
        const int y = int(kHeight) - 90 + row;
        const unsigned alpha = unsigned(210.0f * float(row) / 89.0f);
        for (int x = 0; x < int(kWidth); ++x) {
            uint32_t &pixel = screen.base[size_t(y) * kWidth + size_t(x)];
            pixel = blend(pixel, kTheme.background, alpha);
        }
    }
    legend_button(116, true, "Select");
    legend_button(256, false, "Back");
    fill_round_rect(screen.base, 372, 1016, 466, 1046, 8.0f, kTheme.surface);
    draw_text(screen.base, screen.text_font, 18, "\u2190 \u2192", 419, 1039, kTheme.text, true, 80);
    draw_text(screen.base, screen.text_font, 22, "Choose", 480, 1040, kTheme.text, false, 120);
}

// Text with a soft shadow 2 px below it, as the Aero screens set their type over the sky.
void draw_lit_text(std::vector<uint32_t> &canvas, const Font &font, float pixels, const std::string &text, float x, int y,
                   uint32_t color, bool centre, float max_width = 1e9f)
{
    draw_text(canvas, font, pixels, text, x, y + 2, PW_LOADING_TEXT_SHADOW, centre, max_width, PW_LOADING_SHADOW_ALPHA);
    draw_text(canvas, font, pixels, text, x, y, color, centre, max_width);
}

// Lines of text no wider than width: words kept whole, a word too long for a line cut by draw_text's ellipsis.
std::vector<std::string> wrap(const Font &font, float pixels, const std::string &text, float width)
{
    std::vector<std::string> lines;
    std::string line, word;
    auto fits = [&](const std::string &s) { return text_width(font, pixels, codepoints(s)) <= width; };
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != ' ') { word += text[i]; continue; }
        const std::string tried = line.empty() ? word : line + " " + word;
        if (line.empty() || fits(tried)) line = tried;
        else { lines.push_back(line); line = word; }
        word.clear();
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// Tips about MihawkOS itself, one per load, as the README's Controls section states them.
const char *const kTips[] = {
    "Click the touchpad to left-click, and click with two fingers to right-click, anywhere on the Mihawk OS desktop.",
    "Use Start, then End session, to return to the launcher: the desktop saves its settings every 30 seconds.",
    "If a game\xe2\x80\x99s screen stays black, turn Game Mode off in the launcher\xe2\x80\x99s Settings and start it again.",
    "Copy a game\xe2\x80\x99s folder into C:\\Games, and the next Mihawk OS session puts its shortcut on the desktop.",
    "A USB keyboard and mouse work too, through a USB-C hub; one plugged in later is found within 3 seconds.",
};

void draw_base()
{
    // Every screen of the family stands on the Aero backdrop (src/pw_loading_art.h).
    pw_loading_art_backdrop(screen.base.data());
    std::string url, status, phase;
    int percent;
    bool overlay_on, library, error, overlay_setup;
    ShelfView shelf;
    SetupView setup_view;
    bool report;
    std::string report_title;
    std::vector<std::string> report_lines;
    {
        std::lock_guard<std::mutex> guard(overlay.lock);
        report = overlay.report;
        report_title = overlay.report_title;
        report_lines = overlay.report_lines;
        overlay_on = overlay.on;
        url = overlay.url;
        status = overlay.status;
        phase = overlay.phase;
        error = overlay.error;
        percent = overlay.percent;
        library = overlay.library;
        overlay_setup = overlay.setup;
        setup_view.account = overlay.setup_account;
        setup_view.password = overlay.setup_password;
        setup_view.guard = overlay.setup_guard;
        setup_view.status = overlay.setup_status;
        setup_view.focus = overlay.setup_focus;
        setup_view.error = overlay.setup_error;
        setup_view.ready = overlay.setup_ready;
        shelf.account = overlay.account;
        shelf.games = overlay.games;
        shelf.installed = overlay.installed;
        shelf.focus = overlay.focus;
        shelf.region = overlay.region;
        shelf.tab = overlay.tab;
        shelf.rail = overlay.rail;
        shelf.filter = overlay.filter;
        shelf.notice = overlay.notice;
    }
    if (report) {
        // A heading and one line each, large enough to read in a phone's photograph of the television.
        draw_mark(960, 150);
        draw_text(screen.base, screen.title_font, 48, report_title, 960, 250, kTheme.text, true, 1700);
        int y = 350;
        for (const std::string &line : report_lines) {
            draw_text(screen.base, screen.text_font, 32, line, 960, y, kTheme.text, true, 1760);
            y += 62;
        }
    } else if (overlay_on && overlay_setup) {
        draw_setup(setup_view);
    } else if (overlay_on && library) {
        draw_library(shelf);
    } else if (overlay_on) {
        if (phase.empty()) phase = "Starting Wine";
        const bool result = phase.compare(0, 9, "Signed in") == 0 || phase.compare(0, 13, "Library ready") == 0;
        const uint32_t phase_color = result ? kTheme.success : kTheme.text;
        const uint32_t status_color = error ? kTheme.danger : kTheme.text_muted;
        draw_header();
        if (!url.empty()) {
            draw_phase(phase, status.empty() ? "Scan the QR with the Steam app" : status, 238, phase_color, status_color);
            draw_qr(url);
            draw_lit_text(screen.base, screen.text_font, 20, url, 960, 946, kTheme.text, true, 1600);
        } else {
            fill_round_rect(screen.base, 560, 300, 1360, 620, kTheme.radius_card, kTheme.surface);
            draw_phase(phase, status, 395, phase_color, status_color);
            if (percent >= 0) draw_progress_bar(percent, 660, 520, 600);
            // else the animated spinner is drawn over this panel by draw_dynamic
        }
    } else {
        // A game's start: the globe, its name, the elapsed line and the flowing tube (draw_dynamic), a tip and the
        // footer, as the owner approved (2026-10-26).
        pw_loading_art_base(screen.base.data());
        draw_lit_text(screen.base, screen.title_font, 64, screen.name, kWidth / 2.0f, PW_LOADING_TITLE_Y, PW_LOADING_TEXT,
                      true, 1600);
        const char *tip = kTips[(screen.started / 1000000000u) % (sizeof(kTips) / sizeof(kTips[0]))];
        draw_lit_text(screen.base, screen.title_font, 18, "Tip", PW_LOADING_TIP_X, PW_LOADING_TIP_LABEL_Y,
                      PW_LOADING_TEXT_LABEL, false);
        const std::vector<std::string> lines = wrap(screen.text_font, 22, tip, PW_LOADING_TIP_WIDTH);
        for (size_t i = 0; i < lines.size() && i < 2; ++i)
            draw_lit_text(screen.base, screen.text_font, 22, lines[i], PW_LOADING_TIP_X, PW_LOADING_TIP_Y + int(i) * 30,
                          PW_LOADING_TEXT, false, PW_LOADING_TIP_WIDTH);
    }
    // The library page has its own legend at the bottom; the loading screen keeps the MihawkOS footer.
    if (!(overlay_on && library)) draw_footer();
    screen.canvas = screen.base;
}

struct Region {
    unsigned x0, y0, x1, y1;
};

// Copies a static part of base into the canvas and returns the region it covers, so run() knows what to flush.
Region copy_base_region(unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    for (unsigned y = y0; y < y1; ++y)
        std::copy(screen.base.begin() + ptrdiff_t(y) * kWidth + x0, screen.base.begin() + ptrdiff_t(y) * kWidth + x1,
                  screen.canvas.begin() + ptrdiff_t(y) * kWidth + x0);
    return Region{x0, y0, x1, y1};
}

// The moving part: the globe spinner for a phase with no known percent, or the elapsed line of a game's start. Over an
// overlay base the spinner sits left of the phase (QR layout) or under the status (the loading panel); the shelf has no
// motion. Returns an empty region when this frame has nothing to draw.
Region draw_dynamic(unsigned frame)
{
    std::string url, phase;
    int percent;
    bool on, library, setup;
    {
        std::lock_guard<std::mutex> guard(overlay.lock);
        if (overlay.report) return Region{0, 0, 0, 0};   // the verdict holds still
        on = overlay.on;
        library = overlay.library;
        setup = overlay.setup;
        url = overlay.url;
        phase = overlay.phase;
        percent = overlay.percent;
    }
    if (on) {
        if (library || setup || percent >= 0) return Region{0, 0, 0, 0};
        if (!url.empty()) {
            const Region region = copy_base_region(300, 200, 1620, 260);
            if (phase.empty()) phase = "Starting Wine";
            const float width = text_width(screen.text_font, 30, codepoints(phase));
            draw_spinner(frame, 960 - width / 2 - 36, 228, 10, screen.canvas);
            return region;
        }
        const Region region = copy_base_region(880, 470, 1040, 570);
        draw_spinner(frame, 960, 520, 18, screen.canvas);
        return region;
    }
    pw_loading_art_frame(screen.canvas.data(), screen.base.data(), frame);
    char line[96];
    snprintf(line, sizeof(line), "Starting in Mihawk OS\xc2\xa0\xc2\xb7\xc2\xa0%u s",
             unsigned((now_ns() - screen.started) / 1000000000u));
    draw_lit_text(screen.canvas, screen.text_font, 26, line, kWidth / 2.0f, PW_LOADING_LINE_Y, PW_LOADING_TEXT_SOFT, true);
    return Region{kDynX0, kDynY0, kDynX1, kDynY1};
}

void copy_region(uint8_t *buffer, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x)
            *reinterpret_cast<uint32_t *>(buffer + tiled(x, y)) = screen.canvas[size_t(y) * kWidth + x];
}

// The display reads the buffer as the GPU does: CPU writes go out of the cache first.
void flush_blocks(uint8_t *buffer, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    for (unsigned by = y0 >> 7; by <= (y1 - 1) >> 7; ++by)
        for (unsigned bx = x0 >> 7; bx <= (x1 - 1) >> 7; ++bx) {
            uint8_t *block = buffer + ((size_t(by) * kBlocksX + bx) << 16);
            for (size_t at = 0; at < 65536; at += 64) __asm__ volatile("clflush (%0)" : : "r"(block + at) : "memory");
        }
    __asm__ volatile("mfence" ::: "memory");
}

// A look at what this screen draws, for a test: with PROTON_DATA_ROOT/loading-capture present when the screen
// starts, each kind of screen is written once, from the first frame drawn of it (a probe's screen shows for 0.1 s), as
// PROTON_DATA_ROOT/captures/loading-<kind>.ppm (P6, 1920x1080, the canvas the buffers are copied from). Without the file
// nothing is written. The Aero theme's first console run needed a frame of each (2026-10-26).
const char *const kCaptureKinds[] = {"game-start", "phase", "qr-signin", "progress", "setup", "library"};
struct {
    bool on = false;
    unsigned taken = 0;
} capture;

int screen_kind()
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    if (!overlay.on) return 0;
    if (overlay.library) return 5;
    if (overlay.setup) return 4;
    if (!overlay.url.empty()) return 2;
    return overlay.percent >= 0 ? 3 : 1;
}

void capture_frame()
{
    const int kind = screen_kind();
    if (capture.taken & (1u << kind)) return;
    capture.taken |= 1u << kind;
    mkdir(PROTON_DATA_ROOT "/captures", 0755);
    char path[160];
    snprintf(path, sizeof(path), PROTON_DATA_ROOT "/captures/loading-%s.ppm", kCaptureKinds[kind]);
    FILE *file = fopen(path, "wb");
    if (!file) {
        say("loading: capture %s not written", kCaptureKinds[kind]);
        return;
    }
    fprintf(file, "P6 %u %u 255\n", unsigned(kWidth), unsigned(kHeight));
    std::vector<unsigned char> row(size_t(kWidth) * 3);
    for (unsigned y = 0; y < unsigned(kHeight); ++y) {
        for (unsigned x = 0; x < unsigned(kWidth); ++x) {
            const uint32_t pixel = screen.canvas[size_t(y) * kWidth + x];
            row[x * 3] = uint8_t(pixel >> 16), row[x * 3 + 1] = uint8_t(pixel >> 8), row[x * 3 + 2] = uint8_t(pixel);
        }
        fwrite(row.data(), 1, row.size(), file);
    }
    fclose(file);
    say("loading: captured %s", path);
}

void *run(void *)
{
    // Above the game's threads (695-700) like the title's own loop, and asleep nearly all the time.
    scePthreadSetprio(pthread_self(), 690);
    int back = 1;
    for (unsigned frame = 1; !screen.stop.load(); ++frame) {
        // The flip to the other buffer has shown, so this one is off the screen.
        for (int i = 0; i < 100 && sceVideoOutIsFlipPending(screen.video) > 0; ++i) usleep(2000);
        bool overlay_on, dirty;
        {
            std::lock_guard<std::mutex> guard(overlay.lock);
            overlay_on = overlay.on || overlay.report;   // the verdict screen repaints the same way
            dirty = overlay.dirty;
            overlay.dirty = false;
        }
        if (overlay_on) {
            // Both buffers are refreshed together: writing only the back one left the other stale, so the screen
            // alternated between the overlay and the old content (flicker, 2026-10-22).
            if (dirty) {
                draw_base();
                for (int buffer = 0; buffer < 2; ++buffer) {
                    copy_region(screen.buffers[buffer], 0, 0, kWidth, kHeight);
                    flush_blocks(screen.buffers[buffer], 0, 0, kWidth, kHeight);
                }
            } else {
                const Region region = draw_dynamic(frame);
                if (region.x1 > region.x0) {
                    copy_region(screen.buffers[back], region.x0, region.y0, region.x1, region.y1);
                    flush_blocks(screen.buffers[back], region.x0, region.y0, region.x1, region.y1);
                }
            }
        } else {
            const Region region = draw_dynamic(frame);
            copy_region(screen.buffers[back], region.x0, region.y0, region.x1, region.y1);
            flush_blocks(screen.buffers[back], region.x0, region.y0, region.x1, region.y1);
        }
        sceVideoOutSubmitFlip(screen.video, back, PS5_VIDEO_OUT_FLIP_VSYNC, int64_t(frame));
        if (capture.on) capture_frame();
        back ^= 1;
        for (int i = 0; i < 6 && !screen.stop.load(); ++i) usleep(10000);
    }
    return nullptr;
}

void release_memory()
{
    if (screen.mapped) sceKernelMunmap(screen.mapped, kMemoryBytes);
    if (screen.physical) sceKernelReleaseDirectMemory(screen.physical, kMemoryBytes);
    screen.mapped = nullptr;
    screen.physical = 0;
}
} // namespace

void proton_loading_report(const char *title, const std::vector<std::string> &lines)
{
    bool showing = false;
    {
        std::lock_guard<std::mutex> guard(overlay.lock);
        overlay.report = true;
        overlay.report_title = title ? title : "";
        overlay.report_lines = lines;
        overlay.dirty = true;
        showing = true;
    }
    for (const std::string &line : lines) say("report: %s", line.c_str());
    (void)showing;
}

void proton_aurelia_overlay(bool on)
{
    bool started = false;
    {
        std::lock_guard<std::mutex> guard(overlay.lock);
        overlay.on = on;
        if (on && overlay.phase.empty()) {
            overlay.phase = "Starting Wine";
            started = true;
        }
        if (on && overlay.status.empty()) overlay.status = "Preparing the session";
        overlay.dirty = true;
    }
    if (started) say("Aurelia: phase Starting Wine");
}

// The phase the session is really in (native/proton_wine.cpp aurelia_phase_from_arguments and the event handlers):
// "Starting Wine", "Signing in", "Code scanned", "Listing your library", "Installing <name> · N%",
// "Signed in as <account>", "Library ready".
void proton_aurelia_phase(const char *line)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> guard(overlay.lock);
        if (!line) return;
        if (overlay.phase != line) {
            overlay.phase = line;
            overlay.dirty = true;
            changed = true;
        }
    }
    // The phase is the screen's honesty; the klog carries the same words so a run can prove which one was shown.
    if (changed) say("Aurelia: phase %s", line);
}

void proton_aurelia_challenge(const char *url)
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    if (!url) return;
    overlay.url = url;
    overlay.phase = "Signing in";
    overlay.status = "Scan the QR with the Steam app";
    overlay.error = false;
    overlay.dirty = true;
}

void proton_aurelia_progress(int percent, const char *line)
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    overlay.percent = percent;
    overlay.error = false;
    if (line) overlay.status = line;
    overlay.dirty = true;
}

void proton_aurelia_library(const char *account, const char *const *names, const unsigned *app_ids, int count)
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    overlay.library = true;
    overlay.on = true;
    overlay.url.clear();
    overlay.percent = -1;
    overlay.error = false;
    overlay.phase = "Library ready";
    overlay.account = account ? account : "";
    overlay.games.clear();
    overlay.installed.clear();
    for (int i = 0; i < count; ++i) {
        overlay.games.emplace_back(app_ids[i], names[i] ? names[i] : "");
        char path[128];
        snprintf(path, sizeof(path), PROTON_DATA_ROOT "/games/%u/game.json", app_ids[i]);
        overlay.installed.push_back(access(path, F_OK) == 0 ? 1 : 0);
    }
    overlay.region = NavRegion::grid;
    overlay.tab = 1;
    overlay.rail = 1;
    overlay.filter = 0;
    overlay.notice.clear();
    overlay.focus = 0;
    overlay.dirty = true;
}

long proton_aurelia_browse(int left, int right, int up, int down, int cross, int back)
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    if (!overlay.library) return 0;
    const int count = int(overlay.games.size());
    const auto shown = [&](const int *index_out) {
        int at = 0;
        for (int i = 0; i < count; ++i) {
            const bool installed = overlay.installed[size_t(i)] != 0;
            if (overlay.filter == 1 && !installed) continue;
            if (overlay.filter == 2 && installed) continue;
            if (index_out && at == *index_out) return i;
            ++at;
        }
        return -1;
    };
    const auto shown_count = [&]() {
        int at = 0;
        for (int i = 0; i < count; ++i) {
            const bool installed = overlay.installed[size_t(i)] != 0;
            if (overlay.filter == 1 && !installed) continue;
            if (overlay.filter == 2 && installed) continue;
            ++at;
        }
        return at;
    };
    if (back) {
        overlay.chosen = -1;
        return -1;
    }
    bool moved = left || right || up || down;
    if (overlay.region == NavRegion::tabs) {
        if (left) overlay.tab = std::max(0, overlay.tab - 1);
        if (right) overlay.tab = std::min(4, overlay.tab + 1);
        if (down) overlay.region = NavRegion::grid;
        if (cross) {
            if (overlay.tab == 0) {           // Home: back to the launcher
                overlay.chosen = -1;
                return -1;
            }
            if (overlay.tab != 1) overlay.notice = "This session has only the library";
        }
    } else if (overlay.region == NavRegion::rail) {
        if (up) {
            if (overlay.rail == 0) overlay.region = NavRegion::tabs;
            else --overlay.rail;
        }
        if (down) overlay.rail = std::min(3, overlay.rail + 1);
        if (right) overlay.region = NavRegion::grid;
        if (cross) {
            if (overlay.rail == 0 || overlay.rail == 1 || overlay.rail == 2) {
                overlay.filter = overlay.rail == 0 ? 1 : overlay.rail == 2 ? 2 : 0;
                overlay.focus = 0;
            } else {
                overlay.notice = "Collections are not in this session";
            }
        }
    } else {
        const int shown_total = shown_count();
        if (shown_total > 0) {
            const int column = overlay.focus % kColumns;
            if (left) {
                if (column == 0) {
                    overlay.region = NavRegion::rail;
                    overlay.rail = overlay.filter == 1 ? 0 : overlay.filter == 2 ? 2 : 1;
                } else --overlay.focus;
            } else if (right) {
                if (overlay.focus + 1 < shown_total) ++overlay.focus;
            } else if (up) {
                if (overlay.focus / kColumns == 0) {
                    overlay.region = NavRegion::tabs;
                    overlay.tab = 1;
                } else overlay.focus = std::max(0, overlay.focus - kColumns);
            } else if (down) {
                overlay.focus = std::min(shown_total - 1, overlay.focus + kColumns);
            } else if (cross) {
                const int index = overlay.focus;
                const int game = shown(&index);
                if (game >= 0) {
                    overlay.chosen = long(overlay.games[size_t(game)].first);
                    return overlay.chosen;
                }
            }
        }
    }
    if (moved) overlay.notice.clear();
    overlay.dirty = true;
    return 0;
}

void proton_loading_keep_display(bool keep)
{
    display_keep.store(keep, std::memory_order_release);
}

void proton_aurelia_setup(int visible, const char *account, const char *password, const char *guard, int focus,
                          const char *status, int error, int ready)
{
    std::lock_guard<std::mutex> held(overlay.lock);
    overlay.setup = visible != 0;
    overlay.on = visible != 0;
    overlay.library = false;
    overlay.url.clear();
    overlay.percent = -1;
    overlay.setup_account = account ? account : "";
    overlay.setup_password = password ? password : "";
    overlay.setup_guard = guard ? guard : "";
    overlay.setup_focus = focus;
    overlay.setup_status = status ? status : "";
    overlay.setup_error = error != 0;
    overlay.setup_ready = ready != 0;
    overlay.phase = "Steam runtime";
    overlay.dirty = true;
}

void proton_aurelia_status(const char *line)
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    if (!line) return;
    overlay.status = line;
    overlay.error = false;
    overlay.dirty = true;
}

// An error the client reported: the status is drawn in the theme's danger colour until the next normal event.
void proton_aurelia_error(const char *line)
{
    std::lock_guard<std::mutex> guard(overlay.lock);
    if (!line) return;
    overlay.status = line;
    overlay.error = true;
    overlay.dirty = true;
}

void proton_loading_start(const char *name, const char *fonts)
{
    std::lock_guard<std::mutex> guard(screen.lock);
    if (screen.running) return;
    const char *setting = getenv("PROTON_LOADING_SCREEN");
    if (setting && !strcmp(setting, "0")) {
        say("loading screen: off (PROTON_LOADING_SCREEN=0)");
        return;
    }
    capture.taken = 0;
    capture.on = access(PROTON_DATA_ROOT "/loading-capture", F_OK) == 0;
    if (capture.on) say("loading: capturing each kind of screen once (%s)", PROTON_DATA_ROOT "/loading-capture");
    // The loading screen owns this opening; only subsequent presenters hand it off.
    screen.video = __real_sceVideoOutOpen(PS5_VIDEO_OUT_USER_SYSTEM, 0, 0, nullptr);
    if (screen.video < 0) {
        say("loading screen: none, sceVideoOutOpen failed %#x", unsigned(screen.video));
        return;
    }
    int status = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), kMemoryBytes, kAlign, kMemoryType,
                                               &screen.physical);
    if (status >= 0)
        status = sceKernelMapDirectMemory(&screen.mapped, kMemoryBytes, kProtection, 0, screen.physical, kAlign);
    if (status < 0) {
        say("loading screen: none, direct memory %#x", unsigned(status));
        release_memory();
        sceVideoOutClose(screen.video);
        return;
    }
    screen.buffers[0] = static_cast<uint8_t *>(screen.mapped);
    screen.buffers[1] = screen.buffers[0] + kBufferBytes;
    screen.name = name && *name ? name : "the game";
    const std::string folder = fonts ? fonts : "";
    const bool fonts_ok = load_font(screen.title_font, folder + "/Inter-SemiBold.ttf") &
                          load_font(screen.text_font, folder + "/Inter-Regular.ttf");
    screen.base.assign(size_t(kWidth) * kHeight, 0);
    screen.started = now_ns();
    draw_base();
    draw_dynamic(0);
    for (uint8_t *buffer : screen.buffers) {
        copy_region(buffer, 0, 0, kWidth, kHeight);
        flush_blocks(buffer, 0, 0, kWidth, kHeight);
    }
    uint8_t attribute[PS5_VIDEO_OUT_ATTRIBUTE_BYTES] = {};
    sceVideoOutSetFlipRate(screen.video, 0);
    sceVideoOutSetBufferAttribute2(attribute, PS5_VIDEO_OUT_PIXEL_FORMAT_B8G8R8A8_SDR, PS5_VIDEO_OUT_TILING_64KB_R_X,
                                   kWidth, kHeight, 0, 0, 0);
    ps5_video_out_buffer list[2] = {{screen.buffers[0], nullptr, {}}, {screen.buffers[1], nullptr, {}}};
    status = sceVideoOutRegisterBuffers2(screen.video, 0, 0, list, 2, attribute, 0, nullptr);
    if (status >= 0) status = sceVideoOutSubmitFlip(screen.video, 0, PS5_VIDEO_OUT_FLIP_VSYNC, 0);
    if (status < 0) {
        say("loading screen: none, VideoOut %#x", unsigned(status));
        sceVideoOutClose(screen.video);
        release_memory();
        return;
    }
    screen.stop = false;
    if (pthread_create(&screen.thread, nullptr, run, nullptr)) {
        say("loading screen: shown without its animation (no thread)");
        screen.thread = pthread_t{};
    }
    screen.running = true;
    say("loading screen: shown for \"%s\"%s", screen.name.c_str(), fonts_ok ? "" : " (fonts missing: no text)");
}

std::string proton_loading_stop(const char *why)
{
    std::lock_guard<std::mutex> guard(screen.lock);
    if (!screen.running) return std::string();
    screen.stop = true;
    if (screen.thread != pthread_t{}) pthread_join(screen.thread, nullptr);
    // A blank flip takes the screen's buffers off the display; VideoOut refuses to unregister a
    // set while one of its buffers is shown (PS5_Mesa's videoout WSI).
    const int blank = sceVideoOutSubmitFlip(screen.video, -1, PS5_VIDEO_OUT_FLIP_VSYNC, 0);
    for (int i = 0; i < 120 && sceVideoOutIsFlipPending(screen.video) > 0; ++i) sceVideoOutWaitVblank(screen.video);
    int unregistered = sceVideoOutUnregisterBuffers(screen.video, 0);
    for (int i = 0; i < 60 && unregistered == PS5_VIDEO_OUT_ERROR_BUSY; ++i) {
        sceVideoOutWaitVblank(screen.video);
        unregistered = sceVideoOutUnregisterBuffers(screen.video, 0);
    }
    const int closed = sceVideoOutClose(screen.video);
    // The memory goes only once the display no longer reads it.
    const bool free_memory = unregistered == 0 || closed == 0;
    if (free_memory) release_memory();
    screen.video = -1;
    std::vector<uint32_t>().swap(screen.canvas);
    std::vector<uint32_t>().swap(screen.base);
    screen.running = false;
    char line[256];
    snprintf(line, sizeof(line), "loading screen: closed after %.1f s (%s): blank flip %d, unregister %#x, close %#x, memory %s",
             double(now_ns() - screen.started) / 1e9, why ? why : "", blank, unsigned(unregistered), unsigned(closed),
             free_memory ? "released" : "kept (the display may still read it)");
    say("%s", line);
    return line;
}

// The guided setup's first session: its cover (src/pw_setup.h, "The first session's cover"). Two frames, so the one
// drawn is never the one the broker may be copying: once show returns, the broker reads only the frame it was given.
namespace {
struct Cover {
    std::mutex lock;
    std::atomic<bool> running{false}, stop{false};
    std::atomic<int> stage{PW_SETUP_BUILD_SETTINGS};
    pthread_t thread{};
    bool (*show)(const void *) = nullptr;
    std::vector<uint32_t> picture, frames[2];
    std::string path;
    uint64_t started = 0;
    unsigned shown = 0, refused = 0;
};
Cover cover;

// The launcher's capture (ps5/src/example_ps5.cpp): P6, 1920x1080, RGB.
bool load_picture(const char *path, std::vector<uint32_t> &out)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    unsigned width = 0, height = 0, depth = 0;
    bool ok = fscanf(file, "P6 %u %u %u", &width, &height, &depth) == 3 && fgetc(file) != EOF &&
              width == PW_SETUP_COVER_WIDTH && height == PW_SETUP_COVER_HEIGHT && depth == 255;
    std::vector<unsigned char> bytes(ok ? size_t(width) * height * 3 : 0);
    ok = ok && fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!ok) return false;
    out.resize(bytes.size() / 3);
    for (size_t i = 0; i < out.size(); ++i) out[i] = rgb(bytes[i * 3], bytes[i * 3 + 1], bytes[i * 3 + 2]);
    return true;
}

void *cover_run(void *)
{
    // As the loading screen: above the game's threads, and asleep nearly all the time.
    scePthreadSetprio(pthread_self(), 690);
    int back = 0;
    for (unsigned tick = 0; !cover.stop.load(); ++tick) {
        pw_setup_cover_draw(cover.frames[back].data(), cover.picture.data(), PwSetupBuild(cover.stage.load()), tick);
        if (cover.show(cover.frames[back].data())) back ^= 1, ++cover.shown;
        else ++cover.refused;
        for (int i = 0; i < 10 && !cover.stop.load(); ++i) usleep(10000);
    }
    return nullptr;
}
} // namespace

bool proton_cover_start(const char *picture, bool (*show)(const void *))
{
    std::lock_guard<std::mutex> guard(cover.lock);
    if (cover.running) return true;
    if (!show || !load_picture(picture, cover.picture)) {
        say("setup: no cover (%s is missing or not a 1920x1080 picture); the desktop shows", picture);
        std::vector<uint32_t>().swap(cover.picture);
        return false;
    }
    cover.frames[0] = cover.frames[1] = cover.picture;
    cover.show = show;
    cover.path = picture;
    cover.started = now_ns();
    cover.stop = false;
    if (pthread_create(&cover.thread, nullptr, cover_run, nullptr)) {
        say("setup: no cover (no thread); the desktop shows");
        for (auto *frame : {&cover.picture, &cover.frames[0], &cover.frames[1]}) std::vector<uint32_t>().swap(*frame);
        return false;
    }
    cover.running = true;
    say("setup: the cover is shown instead of the desktop until the session ends");
    return true;
}

void proton_cover_process(const char *command_line)
{
    if (!cover.running.load()) return;
    int was = cover.stage.load();
    for (;;) {
        const int next = pw_setup_build_next(PwSetupBuild(was), command_line);
        if (next == was) return;
        if (cover.stage.compare_exchange_weak(was, next)) {
            say("setup: the cover's stage %d, %.1f s in", next, double(now_ns() - cover.started) / 1e9);
            return;
        }
    }
}

void proton_cover_ready()
{
    if (cover.running.load()) cover.stage = PW_SETUP_BUILD_READY;
}

std::string proton_cover_stop(const char *why)
{
    std::lock_guard<std::mutex> guard(cover.lock);
    if (!cover.running) return std::string();
    cover.stop = true;
    pthread_join(cover.thread, nullptr);
    bool cleared = false;
    for (int i = 0; i < 10 && !cleared; ++i) cleared = cover.show(nullptr);
    // A broker that never let the cover go may still read the frames: they are kept.
    if (cleared)
        for (auto *frame : {&cover.picture, &cover.frames[0], &cover.frames[1]}) std::vector<uint32_t>().swap(*frame);
    unlink(cover.path.c_str());
    cover.running = false;
    char line[256];
    snprintf(line, sizeof(line), "setup: the cover closed after %.1f s (%s): %u frames shown, %u refused, stage %d, %s",
             double(now_ns() - cover.started) / 1e9, why ? why : "", cover.shown, cover.refused, cover.stage.load(),
             cleared ? "taken off" : "still held by the broker");
    say("%s", line);
    return line;
}

// The Steam guide's session screens (src/pw_steam_screen.h), shown as the setup's cover is: two frames, so the one drawn
// is never the one the broker may be copying. The still picture is drawn again only when its words change (the seconds,
// the megabytes: once a second); each frame copies it and moves the tube or the scanned code over it.
namespace {
struct Guide {
    std::mutex lock, state_lock;
    std::atomic<bool> running{false}, stop{false};
    pthread_t thread{};
    bool (*show)(const void *) = nullptr;
    std::vector<unsigned char> bytes[3];   // Inter Regular, Inter SemiBold, the language's own cut
    PwSteamFace *faces[3] = {};
    PwSteamFaces set{};
    const PwSetupLanguage *language = nullptr;
    PwSteamScreenState wanted{};           // under state_lock, with persona and the dialog's words
    std::string persona, dialog_title, dialog_text;
    std::vector<uint32_t> backdrop, still, frames[2];
    uint64_t started = 0;
    unsigned shown = 0, refused = 0, drawn = 0;
    bool covering = false;
};
Guide guide;

bool read_all(const std::string &path, std::vector<unsigned char> &out)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    out.clear();
    unsigned char chunk[65536];
    for (size_t got; (got = fread(chunk, 1, sizeof(chunk), file)) > 0;) out.insert(out.end(), chunk, chunk + got);
    fclose(file);
    return !out.empty();
}

void guide_release()
{
    for (auto *&face : guide.faces) pw_steam_face_close(face), face = nullptr;
    for (auto &bytes : guide.bytes) std::vector<unsigned char>().swap(bytes);
}

void *guide_run(void *)
{
    // As the loading screen: above the game's threads, and asleep nearly all the time.
    scePthreadSetprio(pthread_self(), 690);
    int back = 0;
    PwSteamScreenState drawn{};
    std::string drawn_persona, drawn_dialog;
    bool any = false;
    for (unsigned tick = 0; !guide.stop.load(); ++tick) {
        PwSteamScreenState now;
        std::string persona, dialog_title, dialog_text;
        {
            std::lock_guard<std::mutex> hold(guide.state_lock);
            now = guide.wanted;
            persona = guide.persona;
            dialog_title = guide.dialog_title;
            dialog_text = guide.dialog_text;
        }
        now.language = guide.language;
        now.persona = persona.empty() ? nullptr : persona.c_str();
        now.dialog_title = dialog_title.c_str();
        now.dialog_text = dialog_text.c_str();
        if (now.screen == PW_STEAM_SCREEN_NONE) {
            if (guide.covering && guide.show(nullptr)) {
                guide.covering = false;
                say("steam guide: the client's own window shows, %.1f s in", double(now_ns() - guide.started) / 1e9);
            }
        } else {
            if (!any || now.screen != drawn.screen || now.seconds != drawn.seconds ||
                now.bytes / 1000000 != drawn.bytes / 1000000 || persona != drawn_persona ||
                dialog_title + '\n' + dialog_text != drawn_dialog) {
                if (!any || now.screen != drawn.screen) say("steam guide: screen %d, %u s in", int(now.screen), now.seconds);
                pw_steam_screen_draw(guide.still.data(), guide.backdrop.data(), &now, &guide.set, nullptr);
                ++guide.drawn;
                drawn = now;
                drawn_persona = persona;
                drawn_dialog = dialog_title + '\n' + dialog_text;
                any = true;
            }
            memcpy(guide.frames[back].data(), guide.still.data(), guide.still.size() * sizeof(uint32_t));
            pw_steam_screen_animate(guide.frames[back].data(), &now, tick);
            if (guide.show(guide.frames[back].data())) back ^= 1, ++guide.shown, guide.covering = true;
            else ++guide.refused;
        }
        for (int i = 0; i < 10 && !guide.stop.load(); ++i) usleep(10000);
    }
    return nullptr;
}
} // namespace

bool proton_guide_start(const char *language, const char *app_root, bool (*show)(const void *))
{
    std::lock_guard<std::mutex> hold(guide.lock);
    if (guide.running) return true;
    guide.language = language && pw_setup_language(language) ? pw_setup_language(language) : pw_setup_default_language();
    const std::string fonts = std::string(app_root) + "/assets/hui/fonts/", own = std::string(app_root) + "/assets/setup/fonts/";
    bool ok = show && read_all(fonts + "Inter-Regular.ttf", guide.bytes[0]) && read_all(fonts + "Inter-SemiBold.ttf", guide.bytes[1]);
    // Without its own cut, a Chinese or Japanese screen draws what Inter has (the log says so)
    if (ok && guide.language->font && !read_all(own + guide.language->font + ".ttf", guide.bytes[2]))
        say("steam guide: no %s%s.ttf; its words draw with Inter", own.c_str(), guide.language->font);
    for (int i = 0; ok && i < 3; ++i)
        if (!guide.bytes[i].empty()) guide.faces[i] = pw_steam_face_open(guide.bytes[i].data(), guide.bytes[i].size());
    if (!ok || !guide.faces[0] || !guide.faces[1]) {
        say("steam guide: no screens (the faces in %s are missing); the client's windows show", fonts.c_str());
        guide_release();
        return false;
    }
    guide.set.regular = guide.faces[2] ? guide.faces[2] : guide.faces[0];
    guide.set.semibold = guide.faces[2] ? guide.faces[2] : guide.faces[1];
    guide.set.latin = guide.faces[0];
    const size_t pixels = size_t(PW_LOADING_WIDTH) * PW_LOADING_HEIGHT;
    for (auto *canvas : {&guide.backdrop, &guide.still, &guide.frames[0], &guide.frames[1]}) canvas->assign(pixels, 0);
    pw_loading_art_backdrop(guide.backdrop.data());
    guide.show = show;
    guide.started = now_ns();
    guide.stop = false;
    guide.covering = false;
    guide.shown = guide.refused = guide.drawn = 0;
    if (pthread_create(&guide.thread, nullptr, guide_run, nullptr)) {
        say("steam guide: no screens (no thread); the client's windows show");
        for (auto *canvas : {&guide.backdrop, &guide.still, &guide.frames[0], &guide.frames[1]}) std::vector<uint32_t>().swap(*canvas);
        guide_release();
        return false;
    }
    guide.running = true;
    say("steam guide: its screens are drawn over the client's windows, in %s%s", guide.language->tag,
        guide.faces[2] ? " with its own face" : "");
    return true;
}

void proton_guide_show(PwSteamScreen screen, unsigned seconds, unsigned long long bytes, const char *persona)
{
    std::lock_guard<std::mutex> hold(guide.state_lock);
    guide.wanted.screen = screen;
    guide.wanted.seconds = seconds;
    guide.wanted.bytes = bytes;
    guide.persona = persona ? persona : "";
}

void proton_guide_dialog(const char *title, const char *text)
{
    std::lock_guard<std::mutex> hold(guide.state_lock);
    guide.wanted.screen = title ? PW_STEAM_SCREEN_DIALOG : PW_STEAM_SCREEN_NONE;
    guide.dialog_title = title ? title : "";
    guide.dialog_text = text ? text : "";
}

std::string proton_guide_stop(const char *why)
{
    std::lock_guard<std::mutex> hold(guide.lock);
    if (!guide.running) return std::string();
    guide.stop = true;
    pthread_join(guide.thread, nullptr);
    bool cleared = !guide.covering;
    for (int i = 0; i < 10 && !cleared; ++i) cleared = guide.show(nullptr);
    // A broker that never let the cover go may still read the frames: they are kept.
    if (cleared)
        for (auto *canvas : {&guide.backdrop, &guide.still, &guide.frames[0], &guide.frames[1]}) std::vector<uint32_t>().swap(*canvas);
    guide_release();
    guide.running = false;
    char line[256];
    snprintf(line, sizeof(line), "steam guide: its screens closed after %.1f s (%s): %u frames shown, %u refused, %u drawn, %s",
             double(now_ns() - guide.started) / 1e9, why ? why : "", guide.shown, guide.refused, guide.drawn,
             cleared ? "taken off" : "still held by the broker");
    say("%s", line);
    return line;
}
