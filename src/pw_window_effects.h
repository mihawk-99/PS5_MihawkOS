/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINDOW_EFFECTS_H
#define PW_WINDOW_EFFECTS_H
#include "pw_window_snapshot.h"
/* DWM surface effects for the retained compositor: rounded coverage, border,
 * shadow, Mica/acrylic materials and live thumbnails. Everything here is a
 * pure function of a validated snapshot and retained images, so the title's
 * display thread and the host tests run the same code. No Wine state is read.
 *
 * Recipe values mirror themes/windows11/appearance.json "materials"; the
 * host test tests/test_window_material_tokens.py fails if they drift. They are
 * this project's design tokens, not measured Windows 11 constants.
 *
 * Blur is a box filter applied blur_passes times horizontally then vertically
 * at 1/scale resolution (each output pixel is the rounded mean of an exact
 * scale x scale block, clamped to the sampled area), then bilinearly expanded.
 * Mica blurs the retained desktop root once per root image; acrylic blurs the
 * pixels already composed beneath the window, every frame. Tint is
 * (blur*(255-tint)+colour*tint+127)/255 per channel.
 *
 * The taskbar's material is the approved prototype's taskbar acrylic (prototypes/windows11-shell css/tokens.css
 * --acrylic-taskbar over --acrylic-blur): in light, blur(30px) contrast(0.15) brightness(1.95) then 85% #f3f3f3; in
 * dark, saturate(125%) then 97% #1c1c1c. It blurs what lies beneath like acrylic (over the prototype's own Bloom its
 * blur gives 94% of the light bar within a level, Mica's 91%). The prototype's 2% noise overlay is random per page
 * load and moves the bar by under a level; it is left out.
 *
 * The shell's flyouts are the prototype's .surface-flyout: the same filter chain under its acrylic-base (Start and
 * Search, PW_BACKDROP_FLYOUT_BASE: 90% #f3f3f3, 92% #202020 in dark) or its acrylic (Quick Settings, notifications,
 * hidden icons, PW_BACKDROP_FLYOUT: 85% #fcfcfc, 96% #2c2c2c); the tint stops inside the 1 px stroke
 * (background-clip: padding-box), so the stroke (black at 5.9%, 20% in dark) lies over the untinted backdrop; and
 * --shadow-28 (0 0 8px and 0 14px 28px, black at 12% and 14%, 24% and 28% in dark) is drawn as Chromium draws a
 * framed window's shadow (below): two box-shadow layers (pw_flyout_shadow_layers). Around the desktop menu at 600,400
 * over the reference desktop it is the prototype's capture within a level (tests/test_pw_window_effects.c has the
 * recipe); the Gaussians of the CSS definition came within a level at 94-95% of those pixels. Chromium blurs only what
 * lies under the flyout, its edges repeated outward.
 *
 * Mica Alt (DWMSBT_TABBEDWINDOW, File Explorer's title bar) is the prototype's .mica-alt (css/base.css, js/core.js
 * micaOf): the wallpaper drawn into a canvas of 1/8 the screen's width, offset by -MICA_ALT_MARGIN and that much
 * larger on every side, through blur(12px) (three box passes of radius MICA_ALT_RADIUS, transparent beyond the drawn
 * wallpaper) and saturate(140%), shown over the screen at 8x; then the luminosity of #e6e6e6 (#1a1a1a dark) over it
 * and 65% #f2f2f2 (89% #1e1e1e). Over the prototype's Bloom its source is 0.96 levels from the prototype's own canvas
 * on average and the title bar within one level of the prototype's capture everywhere.
 *
 * A framed window's shadow (PW_SHADOW_WINDOW) is the prototype's --shadow-window, or --shadow-window-inactive while
 * another window is active (css/tokens.css, light and dark): three box-shadow layers, drawn bottom first, each as
 * Chromium (Skia, CPU raster) draws one. Its 8-bit mask is the window's rounded rectangle, moved down by the offset and
 * grown by the spread, blurred across then down by the W3C three-box approximation of a Gaussian of deviation blur/2
 * (pw_window_shadow_kernel), each pass rounded to 8 bits; the layer's colour, alpha*255 rounded half up, then darkens
 * by a=(alpha*(mask+1))>>8, each channel becoming (v*(256-a))>>8; under the window's anti-aliased corner the mask is
 * scaled by what the window leaves uncovered (Chromium clips the shadow out of the border box). Composed over the
 * prototype's Bloom, File Explorer's shadow is its reference capture's within a level at 99.997% of the pixels around
 * it (99.992% dark), exact at 99.95%; left over are the ring's anti-aliased corner tips. A true Gaussian came to 97-98%,
 * its tail reaching past where (alpha*(mask+1))>>8 is already 0. Where an active window's shadow falls on an inactive
 * one's, Chromium composited the upper window as a layer of its own and comes out up to 2 levels lighter. */
enum { PW_MICA_ALT_MARGIN=24, PW_MICA_ALT_RADIUS=11, PW_MICA_ALT_LUMINOSITY=230, PW_MICA_ALT_LUMINOSITY_DARK=26 };
enum { PW_MICA_SCALE=8, PW_MICA_RADIUS=6, PW_ACRYLIC_SCALE=4, PW_ACRYLIC_RADIUS=8, PW_BLUR_PASSES=3,
       PW_SHADOW_BORDER_LIMIT=66, PW_SHADOW_WINDOW_REACH=82, /* blur 48's three boxes of 45 reach 66 px, 16 below */
       PW_SHADOW_SMALL_SIZE=8, PW_SHADOW_SMALL_ALPHA=41, PW_SHADOW_SMALL_OFFSET=2,
       PW_SHADOW_FLYOUT_OFFSET=14, PW_SHADOW_FLYOUT_REACH=48, /* with the offset: blur 28's boxes reach 38 px, 52 below */
       PW_FLYOUT_STROKE=15, PW_FLYOUT_STROKE_DARK=51 };
/* One box-shadow layer: y offset, blur radius, spread (px), and its colour's alpha*255 rounded half up. */
typedef struct PwShadowLayer { uint8_t offset, blur, spread, alpha; } PwShadowLayer;
/* [dark], bottom layer first: --shadow-28's 0 14px 28px and 0 0 8px, black at 14% and 12% (28% and 24% in dark). */
static const PwShadowLayer pw_flyout_shadow_layers[2][2]={{{14,28,0,36},{0,8,0,31}},{{14,28,0,71},{0,8,0,61}}};
/* [active][dark], bottom layer first: 0 8px 24px, 0 2px 6px, 0 0 0 1px black at 12%, 8%, 5% (30%, 25%, 30% in dark)
 * while inactive; 0 16px 48px, 0 2px 8px, 0 0 0 1px at 22%, 10%, 6% (45%, 30%, 35%) while active. */
static const PwShadowLayer pw_window_shadow_layers[2][2][3]={
    {{{8,24,0,31},{2,6,0,20},{0,0,1,13}},{{8,24,0,77},{2,6,0,64},{0,0,1,77}}},
    {{{16,48,0,56},{2,8,0,26},{0,0,1,15}},{{16,48,0,115},{2,8,0,77},{0,0,1,89}}}};
/* The 1 px ring (blur 0, spread 1: radius 9 around an 8 px window) as Chromium's Skia fills a rounded rectangle, with
 * analytic AA (tools/skia-rrect-coverage.py 9): [corner: top left, top right, bottom left, bottom right][j: rows in
 * from its outer edge][i: columns in]. Each arc is a few snapped line segments, so the tips are lighter than an exact
 * circle and not symmetric: around File Explorer over the reference desktop 272 of its 272 tip pixels come within a
 * level (light), 271 (dark, one at 2), where the supersampled circle missed 24 by up to 4 levels and 49 by up to 17.
 * Other radii keep pw_window_corner_coverage. */
enum { PW_SHADOW_RING_RADIUS=9 };
static const uint8_t pw_window_ring_corners[4][PW_SHADOW_RING_RADIUS][PW_SHADOW_RING_RADIUS]=
{{{0,0,0,0,0,44,117,172,228},{0,0,0,8,136,252,255,255,255},{0,0,47,216,255,255,255,255,255},{0,11,220,255,255,255,255,255,255},{0,144,255,255,255,255,255,255,255},{58,253,255,255,255,255,255,255,255},{168,255,255,255,255,255,255,255,255},{208,255,255,255,255,255,255,255,255},{240,255,255,255,255,255,255,255,255}},
 {{0,0,0,0,0,44,116,172,228},{0,0,0,5,134,252,255,255,255},{0,0,49,206,255,255,255,255,255},{0,6,220,255,255,255,255,255,255},{0,136,255,255,255,255,255,255,255},{59,253,255,255,255,255,255,255,255},{160,255,255,255,255,255,255,255,255},{200,255,255,255,255,255,255,255,255},{240,255,255,255,255,255,255,255,255}},
 {{0,0,0,0,0,44,116,172,228},{0,0,0,5,134,252,255,255,255},{0,0,51,252,255,255,255,255,255},{0,7,204,255,255,255,255,255,255},{0,128,255,255,255,255,255,255,255},{39,248,255,255,255,255,255,255,255},{136,255,255,255,255,255,255,255,255},{184,255,255,255,255,255,255,255,255},{232,255,255,255,255,255,255,255,255}},
 {{0,0,0,0,0,44,117,172,228},{0,0,0,5,136,252,255,255,255},{0,0,49,252,255,255,255,255,255},{0,3,206,255,255,255,255,255,255},{0,120,255,255,255,255,255,255,255},{37,249,255,255,255,255,255,255,255},{136,255,255,255,255,255,255,255,255},{184,255,255,255,255,255,255,255,255},{232,255,255,255,255,255,255,255,255}}};
/* A tooltip's (PW_SHADOW_TOOLTIP: the flyout's acrylic with 4 px corners) --shadow-tooltip, the prototype's --shadow-8, drawn
 * as a framed window's layers are: [dark], bottom layer first: 0 4px 8px, 0 0 2px, black at 14% and 12% (28%, 24% dark). */
static const PwShadowLayer pw_window_tooltip_shadow_layers[2][2]={{{4,8,0,36},{0,2,0,31}},{{4,8,0,71},{0,2,0,61}}};
/* A blur's kernel: integer weights at offsets [-border,border] summing to divisor, below[u+border+1] the sum of those
 * at offsets <= u, and Skia's rounding weight round(2^32/divisor). */
typedef struct PwShadowKernel {
    int32_t border;
    uint32_t divisor;
    uint64_t weight;
    uint32_t weights[2*PW_SHADOW_BORDER_LIMIT+1], below[2*PW_SHADOW_BORDER_LIMIT+2];
} PwShadowKernel;
typedef struct PwMaterialTint { uint32_t light, dark; uint32_t tint, tint_dark; } PwMaterialTint;
/* Indexed by PW_BACKDROP_*; NONE is unused. Colours are 0x00RRGGBB. */
static const PwMaterialTint pw_material_tints[7]={
    {0,0,0,0},
    {0xeef2f8,0x22262d,204,204},
    {0xf7f9fc,0x292c33,184,184},
    {0xf2f2f2,0x1e1e1e,166,227},
    {0xf3f3f3,0x1c1c1c,217,247},
    {0xf3f3f3,0x202020,230,235},
    {0xfcfcfc,0x2c2c2c,217,245}};
/* The prototype's own materials: the taskbar's filter chain, and for the flyouts a tint inside the stroke. */
static inline int pw_window_prototype_backdrop(unsigned backdrop) {return backdrop>=PW_BACKDROP_TASKBAR && backdrop<=PW_BACKDROP_FLYOUT;}
static inline int pw_window_flyout_backdrop(unsigned backdrop) {return backdrop==PW_BACKDROP_FLYOUT_BASE || backdrop==PW_BACKDROP_FLYOUT;}
static inline int pw_window_mica_backdrop(unsigned backdrop) {return backdrop==PW_BACKDROP_MICA || backdrop==PW_BACKDROP_MICA_ALT;}
/* An inactive window's Mica: the caption's solid inactive colour (themes/windows11/appearance.json caption_inactive). */
enum { PW_MICA_INACTIVE_LIGHT=0xf3f3f3, PW_MICA_INACTIVE_DARK=0x202020 };
/* What the blurred backdrop goes through before the tint. */
enum { PW_RECIPE_PLAIN=0, PW_RECIPE_TASKBAR_LIGHT=1, PW_RECIPE_TASKBAR_DARK=2, PW_RECIPE_MICA_ALT_LIGHT=3,
       PW_RECIPE_MICA_ALT_DARK=4 };
/* contrast(0.15) then brightness(1.95), rounded and clamped: round(0.2925*v + 211.33). */
static inline uint32_t pw_window_taskbar_transfer(uint32_t v) {uint32_t t=(2925*v+2113313+5000)/10000;return t>255 ? 255 : t;}
/* saturate(1.25), the Filter Effects matrix in thousandths, rounded and clamped; 0x00RRGGBB in and out. */
static inline uint32_t pw_window_taskbar_saturate(uint32_t pixel)
{
    static const int32_t m[3][3]={{1197,-179,-18},{-53,1071,-18},{-53,-179,1232}};
    const int32_t c[3]={(int32_t)(pixel>>16)&255,(int32_t)(pixel>>8)&255,(int32_t)pixel&255};
    uint32_t out=0;
    for(unsigned i=0;i<3;++i) {
        int32_t v=(m[i][0]*c[0]+m[i][1]*c[1]+m[i][2]*c[2]+500)/1000;
        out|=(uint32_t)(v<0 ? 0 : v>255 ? 255 : v)<<(16-8*i);
    }
    return out;
}

/* Bounded caches owned by the display thread. Zero-initialise; release with
 * pw_window_effects_release. Allocation is lazy and capped at
 * PW_WINDOW_EFFECTS_LIMIT bytes; exceeding it fails the frame with ENOSPC. */
enum { PW_WINDOW_EFFECTS_LIMIT=32u<<20, PW_BACKDROP_CACHE_LIMIT=8 };
typedef struct PwThumbnailCache {
    uint32_t source, gdi_sequence, gpu_sequence, width, height;
    int32_t source_left, source_top, source_right, source_bottom;
    uint64_t source_generation;
    uint32_t *pixels;
    size_t capacity;
    int valid;
} PwThumbnailCache;
typedef struct PwWindowEffects {
    uint32_t *mica;              /* blurred root, ceil(1920/scale) x ceil(1080/scale) */
    uint32_t mica_width, mica_height, mica_hwnd, mica_sequence;
    size_t mica_capacity;
    int32_t mica_pid;
    uint64_t mica_generation;
    int mica_valid;
    uint32_t *mica_alt;          /* the prototype's Mica image: width/scale x height/scale, built per root image */
    uint32_t mica_alt_width, mica_alt_height, mica_alt_hwnd, mica_alt_sequence;
    size_t mica_alt_capacity, mica_alt_scratch_capacity;
    uint32_t *mica_alt_scratch;  /* its canvas while it is built */
    int32_t mica_alt_pid;
    uint64_t mica_alt_generation;
    int mica_alt_valid;
    uint32_t *blur[2];           /* reduced-resolution scratch */
    size_t blur_capacity;
    uint32_t *source;            /* one thumbnail source, composed at full size */
    size_t source_capacity;
    PwThumbnailCache thumbnails[PW_WINDOW_THUMBNAIL_LIMIT];
    /* A glass window's finished backdrop (blurred, tinted, ringed), kept while nothing it is made from changes:
     * the rows beneath its blur area and their images' sequences, its own place and recipe, the Mica images. The
     * taskbar and flyouts sit over a still wallpaper, so they are rebuilt only when that changes or a window moves
     * under them; the rest of the time a frame reads these rows. */
    struct PwBackdropCache {
        uint32_t hwnd; int32_t pid;
        uint64_t key, used;
        int32_t left, top, right, bottom;  /* the window's rectangle on the output */
        uint32_t *pixels; size_t capacity;
        int valid;
    } backdrops[PW_BACKDROP_CACHE_LIMIT];
    uint64_t frame;
    size_t bytes;
    uint64_t mica_builds, thumbnail_builds, backdrop_builds, backdrop_hits; /* counters for tests and logs */
} PwWindowEffects;

#ifdef __cplusplus
extern "C" {
#endif
void pw_window_effects_release(PwWindowEffects *effects);
/* Coverage 0..255 of a pixel at (i,j) from a rounded corner of radius r
 * (pixel centre (i+0.5,j+0.5), circle centre (r,r)). i,j < r <= RADIUS_LIMIT+1 (a shadow's 1 px spread). */
uint8_t pw_window_corner_coverage(unsigned radius,unsigned i,unsigned j);
/* Strength 0..255 of a 1-pixel border stroke at the same corner position. */
uint8_t pw_window_corner_border(unsigned radius,unsigned i,unsigned j);
/* The radius actually drawn: limited to half the row's width and height. */
unsigned pw_window_row_radius(const PwWindowRow *row);
/* Coverage of (x,y) for a row's rounded visible rectangle: 0 outside it, 255
 * inside a square row. */
uint8_t pw_window_row_coverage(const PwWindowRow *row,int64_t x,int64_t y);
/* PW_SHADOW_SMALL's darkening 0..255 at signed distance sd (pixels, positive outside). */
uint8_t pw_window_shadow_alpha(unsigned shadow,float distance);
/* The kernel of a CSS blur radius (deviation blur/2): Skia's SkMaskBlurFilter window floor(s*3*sqrt(2pi)/4+0.5), at
 * least 1; three centred boxes of it when odd, else two of it, half a pixel either side, and one a pixel wider. Blur
 * 0 is the identity. -1 (EINVAL) when the kernel would reach past PW_SHADOW_BORDER_LIMIT. */
int pw_window_shadow_kernel(PwShadowKernel *kernel,unsigned blur);
/* The weights a pixel at p takes from a run of pixels [a,b). */
uint32_t pw_window_shadow_span(const PwShadowKernel *kernel,int64_t p,int64_t a,int64_t b);
/* One pass's 8-bit output for a sum of weights times 8-bit values, rounded as Skia rounds it. */
static inline uint32_t pw_window_shadow_pass(const PwShadowKernel *kernel,uint64_t sum)
{
    return (uint32_t)((sum*kernel->weight+(1ull<<31))>>32);
}
/* Signed distance from (px,py) to a rounded rectangle [l,r)x[t,b), radius rad. */
float pw_window_rounded_distance(float px,float py,float l,float t,float r,float b,float radius);
/* Reduce an area of source to 1/scale and blur it; output is width x height
 * with width=ceil(area_w/scale). Source pixels outside [0,sw)x[0,sh) clamp. */
int pw_window_blur(uint32_t *output,uint32_t *scratch,const uint32_t *source,uint32_t sw,uint32_t sh,
                   uint32_t stride,int32_t left,int32_t top,uint32_t area_w,uint32_t area_h,
                   unsigned scale,unsigned radius,unsigned passes);
/* Bilinear sample at screen pixel (x,y) of a buffer covering screen origin
 * (left,top) at 1/scale resolution. */
uint32_t pw_window_blur_sample(const uint32_t *blurred,uint32_t width,uint32_t height,
                               int32_t left,int32_t top,unsigned scale,int64_t x,int64_t y);
uint32_t pw_window_tint(uint32_t colour,uint32_t tint,uint32_t amount);
/* Mica Alt's source from the screen-sized area at (left,top) of a root image: output is (width/scale) x
 * (height/scale); scratch holds pw_window_mica_alt_scratch(width,height) words. */
size_t pw_window_mica_alt_scratch(uint32_t width,uint32_t height);
int pw_window_mica_alt_source(uint32_t *output,uint32_t *scratch,const uint32_t *source,uint32_t sw,uint32_t sh,
                              uint32_t stride,int32_t left,int32_t top,uint32_t width,uint32_t height);
/* One Mica Alt pixel from its sampled source: the luminosity of the recipe's grey, then its tint. */
uint32_t pw_window_mica_alt_pixel(uint32_t sampled,uint32_t tint,uint32_t amount,unsigned recipe);
/* One row of tinted material: out[i] equals
 * pw_window_tint(pw_window_blur_sample(...,x0+i,y),tint,amount) bit for bit (PW_RECIPE_PLAIN; with
 * PW_RECIPE_TASKBAR_LIGHT each blurred channel goes through pw_window_taskbar_transfer first, with
 * PW_RECIPE_TASKBAR_DARK the blurred pixel through pw_window_taskbar_saturate),
 * with the vertical blend and the tint table computed once per row. */
void pw_window_material_row(uint32_t *out,const uint32_t *blurred,uint32_t width,uint32_t height,
                            int32_t left,int32_t top,unsigned scale,int64_t y,int64_t x0,uint32_t count,
                            uint32_t tint,uint32_t amount,unsigned recipe);
/* Area-average scale: destination pixel (dx,dy) averages source columns
 * [floor(dx*sw/dw), max(that+1, floor((dx+1)*sw/dw))) and likewise rows. */
int pw_window_scale(uint32_t *output,uint32_t dw,uint32_t dh,const uint32_t *source,
                    uint32_t sw,uint32_t sh,uint32_t stride);
#ifdef __cplusplus
}
#endif
#endif
