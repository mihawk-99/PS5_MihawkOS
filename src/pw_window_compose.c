/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_window_effects.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static const PwWindowImage *image_for(const PwWindowRow *row,const PwWindowImage *images,size_t count,unsigned plane)
{
    for(size_t i=0;i<count;++i)
        if(images[i].hwnd==row->hwnd && images[i].pid==row->pid &&
           images[i].generation==row->generation && images[i].plane==plane)return &images[i];
    return NULL;
}
static const PwWindowRow *row_for(const PwWindowSnapshot *snapshot,uint32_t hwnd,uint64_t generation)
{
    for(uint32_t i=0;i<snapshot->count;++i)
        if(snapshot->windows[i].hwnd==hwnd && snapshot->windows[i].generation==generation)return &snapshot->windows[i];
    return NULL;
}
/* Premultiplied source-over, every channel, saturating. */
static uint32_t over(uint32_t source,uint32_t dest)
{
    const uint32_t inverse=255-(source>>24);
    uint32_t pixel=0;
    for(unsigned shift=0;shift<32;shift+=8) {
        uint32_t channel=((source>>shift)&255)+((((dest>>shift)&255)*inverse+127)/255);
        pixel|=(channel>255 ? 255 : channel)<<shift;
    }
    return pixel;
}
static uint32_t mix(uint32_t dest,uint32_t source,uint32_t amount)
{
    if(amount>=255)return source;
    if(!amount)return dest;
    uint32_t pixel=0;
    for(unsigned shift=0;shift<32;shift+=8)
        pixel|=((((dest>>shift)&255)*(255-amount)+((source>>shift)&255)*amount+127)/255)<<shift;
    return pixel;
}
static uint32_t scaled(uint32_t value,uint32_t amount)
{
    if(amount>=255)return value;
    uint32_t pixel=0;
    for(unsigned shift=0;shift<32;shift+=8)pixel|=((((value>>shift)&255)*amount+127)/255)<<shift;
    return pixel;
}
typedef struct Material {
    const uint32_t *pixels;
    uint32_t width,height,tint,amount;
    int32_t left,top;
    unsigned scale,recipe;
    /* A finished backdrop from PwWindowEffects.backdrops: rows read in place, already tinted and ringed. */
    const uint32_t *cached;
    int64_t cached_left,cached_top;
    uint32_t cached_stride;
    int ring;  /* a flyout: the tint stops inside the 1 px stroke, which shows the untinted backdrop */
} Material;
/* A run of material colours for one scanline (count <= 1920). */
static void material_row(const Material *material,uint32_t *out,int64_t y,int64_t x0,uint32_t count)
{
    if(!material->pixels) {for(uint32_t i=0;i<count;++i)out[i]=0xff000000u|material->tint;return;}
    pw_window_material_row(out,material->pixels,material->width,material->height,material->left,material->top,
                           material->scale,y,x0,count,material->tint,material->amount,material->recipe);
}
static int reserve(PwWindowEffects *effects,uint32_t **buffer,size_t *capacity,size_t pixels)
{
    if(*capacity>=pixels)return 0;
    const size_t old=*capacity*4,next=pixels*4;
    if(next-old>PW_WINDOW_EFFECTS_LIMIT-effects->bytes) {errno=ENOSPC;return -1;}
    uint32_t *grown=realloc(*buffer,next);
    if(!grown) {errno=ENOMEM;return -1;}
    *buffer=grown;*capacity=pixels;effects->bytes+=next-old;
    return 0;
}
static int reserve_blur(PwWindowEffects *effects,size_t pixels)
{
    size_t second=effects->blur_capacity;
    if(reserve(effects,&effects->blur[0],&effects->blur_capacity,pixels))return -1;
    return reserve(effects,&effects->blur[1],&second,pixels);
}
static void tint_for(const PwWindowRow *row,Material *material)
{
    const PwMaterialTint *tint=&pw_material_tints[row->backdrop];
    material->tint=row->material==PW_MATERIAL_DARK ? tint->dark : tint->light;
    material->amount=row->material==PW_MATERIAL_DARK ? tint->tint_dark : tint->tint;
    material->recipe=row->backdrop==PW_BACKDROP_MICA_ALT ?
                     (row->material==PW_MATERIAL_DARK ? PW_RECIPE_MICA_ALT_DARK : PW_RECIPE_MICA_ALT_LIGHT) :
                     !pw_window_prototype_backdrop(row->backdrop) ? PW_RECIPE_PLAIN :
                     row->material==PW_MATERIAL_DARK ? PW_RECIPE_TASKBAR_DARK : PW_RECIPE_TASKBAR_LIGHT;
    material->ring=pw_window_flyout_backdrop(row->backdrop);
    /* A framed window's Mica shows only while it is the active window: another one's is its solid inactive caption
     * colour, as Windows 11 and the prototype draw it (#f3f3f3, #202020 dark: themes/windows11/appearance.json
     * caption_inactive; the reference's windows-active-inactive capture). Glass without a frame (a popup) keeps it. */
    if(pw_window_mica_backdrop(row->backdrop) && row->shadow==PW_SHADOW_WINDOW && !(row->flags&PW_WINDOW_ACTIVE)) {
        material->tint=row->material==PW_MATERIAL_DARK ? PW_MICA_INACTIVE_DARK : PW_MICA_INACTIVE_LIGHT;
        material->amount=255;material->recipe=PW_RECIPE_PLAIN;
    }
}
/* Mica samples the retained desktop root, blurred once per root image. */
static int prepare_mica(PwWindowEffects *effects,const PwWindowSnapshot *snapshot,const PwWindowImage *images,
                        size_t count,uint32_t width,uint32_t height)
{
    const PwWindowRow *root=NULL;
    for(uint32_t i=0;i<snapshot->count;++i)
        if(snapshot->windows[i].flags&PW_WINDOW_DESKTOP) {root=&snapshot->windows[i];break;}
    const PwWindowImage *image=root ? image_for(root,images,count,PW_WINDOW_GDI) : NULL;
    if(!image) {effects->mica_valid=0;return 0;}
    const uint32_t mw=(width+PW_MICA_SCALE-1)/PW_MICA_SCALE,mh=(height+PW_MICA_SCALE-1)/PW_MICA_SCALE;
    if(effects->mica_valid && effects->mica_hwnd==image->hwnd && effects->mica_pid==image->pid &&
       effects->mica_generation==image->generation && effects->mica_sequence==image->sequence &&
       effects->mica_width==mw && effects->mica_height==mh)return 0;
    effects->mica_valid=0;
    if(reserve(effects,&effects->mica,&effects->mica_capacity,(size_t)mw*mh) || reserve_blur(effects,(size_t)mw*mh))return -1;
    const int64_t origin_x=(int64_t)root->left+image->offset_x,origin_y=(int64_t)root->top+image->offset_y;
    if(origin_x<-4096 || origin_x>4096 || origin_y<-4096 || origin_y>4096) {effects->mica_valid=0;return 0;}
    if(pw_window_blur(effects->mica,effects->blur[1],image->pixels,image->width,image->height,image->stride,
                      (int32_t)-origin_x,(int32_t)-origin_y,width,height,PW_MICA_SCALE,PW_MICA_RADIUS,PW_BLUR_PASSES))return -1;
    effects->mica_width=mw;effects->mica_height=mh;
    effects->mica_hwnd=image->hwnd;effects->mica_pid=image->pid;
    effects->mica_generation=image->generation;effects->mica_sequence=image->sequence;
    effects->mica_valid=1;++effects->mica_builds;
    return 0;
}
/* Mica Alt is the prototype's own Mica image of the retained root, built once per root image (src/pw_window_effects.h). */
static int prepare_mica_alt(PwWindowEffects *effects,const PwWindowSnapshot *snapshot,const PwWindowImage *images,
                            size_t count,uint32_t width,uint32_t height)
{
    const PwWindowRow *root=NULL;
    for(uint32_t i=0;i<snapshot->count;++i)
        if(snapshot->windows[i].flags&PW_WINDOW_DESKTOP) {root=&snapshot->windows[i];break;}
    const PwWindowImage *image=root ? image_for(root,images,count,PW_WINDOW_GDI) : NULL;
    if(!image) {effects->mica_alt_valid=0;return 0;}
    const uint32_t mw=width/PW_MICA_SCALE,mh=height/PW_MICA_SCALE;
    if(effects->mica_alt_valid && effects->mica_alt_hwnd==image->hwnd && effects->mica_alt_pid==image->pid &&
       effects->mica_alt_generation==image->generation && effects->mica_alt_sequence==image->sequence &&
       effects->mica_alt_width==mw && effects->mica_alt_height==mh)return 0;
    effects->mica_alt_valid=0;
    const int64_t origin_x=(int64_t)root->left+image->offset_x,origin_y=(int64_t)root->top+image->offset_y;
    if(!mw || !mh || origin_x<-4096 || origin_x>4096 || origin_y<-4096 || origin_y>4096)return 0;
    if(reserve(effects,&effects->mica_alt,&effects->mica_alt_capacity,(size_t)mw*mh) ||
       reserve(effects,&effects->mica_alt_scratch,&effects->mica_alt_scratch_capacity,pw_window_mica_alt_scratch(width,height)))
        return -1;
    if(pw_window_mica_alt_source(effects->mica_alt,effects->mica_alt_scratch,image->pixels,image->width,image->height,image->stride,
                                 (int32_t)-origin_x,(int32_t)-origin_y,width,height))return -1;
    effects->mica_alt_width=mw;effects->mica_alt_height=mh;
    effects->mica_alt_hwnd=image->hwnd;effects->mica_alt_pid=image->pid;
    effects->mica_alt_generation=image->generation;effects->mica_alt_sequence=image->sequence;
    effects->mica_alt_valid=1;++effects->mica_builds;
    return 0;
}
/* Acrylic samples what is already composed beneath the window this frame. */
static int prepare_acrylic(PwWindowEffects *effects,const uint32_t *output,uint32_t width,uint32_t height,
                           const PwWindowRow *row,Material *material)
{
    /* A flyout blurs only what lies under it, its edges repeated outward, as Chromium's backdrop-filter does. */
    const int flyout=pw_window_flyout_backdrop(row->backdrop);
    const int64_t margin=flyout ? 0 : PW_ACRYLIC_SCALE*PW_ACRYLIC_RADIUS*PW_BLUR_PASSES;
    int64_t left=(int64_t)row->left-margin,top=(int64_t)row->top-margin;
    int64_t right=(int64_t)row->right+margin,bottom=(int64_t)row->bottom+margin;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>width)right=width;
    if(bottom>height)bottom=height;
    if(right<=left || bottom<=top)return 0;
    const uint32_t aw=(uint32_t)(right-left),ah=(uint32_t)(bottom-top);
    const uint32_t bw=(aw+PW_ACRYLIC_SCALE-1)/PW_ACRYLIC_SCALE,bh=(ah+PW_ACRYLIC_SCALE-1)/PW_ACRYLIC_SCALE;
    const uint32_t *source=flyout ? output+(size_t)top*width+(size_t)left : output;
    if(reserve_blur(effects,(size_t)bw*bh) ||
       pw_window_blur(effects->blur[0],effects->blur[1],source,flyout ? aw : width,flyout ? ah : height,width*4,
                      flyout ? 0 : (int32_t)left,flyout ? 0 : (int32_t)top,aw,ah,PW_ACRYLIC_SCALE,PW_ACRYLIC_RADIUS,
                      PW_BLUR_PASSES))return -1;
    material->pixels=effects->blur[0];material->width=bw;material->height=bh;
    material->left=(int32_t)left;material->top=(int32_t)top;material->scale=PW_ACRYLIC_SCALE;
    return 0;
}
static void ring_row(const Material *material,const PwWindowRow *row,uint32_t *out,int64_t y,int64_t x0,uint32_t count);
/* FNV-1a over bytes: the backdrop cache's key. */
static uint64_t key_bytes(uint64_t key,const void *data,size_t size)
{
    const unsigned char *p=data;
    for(size_t i=0;i<size;++i)key=(key^p[i])*0x100000001b3ull;
    return key;
}
/* A row as its pixels show it: whether it is active changes only a framed window's shadow, its theme only that shadow
 * and glass; under a glass window's own backdrop neither of its own shows (its shadow is drawn after). An activation
 * flips the flag on two rows, and keying it everywhere rebuilt the taskbar's and a flyout's backdrops for no pixel. */
static uint64_t key_row(uint64_t key,const PwWindowRow *row,int own)
{
    PwWindowRow seen=*row;
    /* a framed Mica window's own backdrop changes with activation (tint_for) */
    if((own && !(pw_window_mica_backdrop(seen.backdrop) && seen.shadow==PW_SHADOW_WINDOW)) ||
       (!own && seen.shadow!=PW_SHADOW_WINDOW)) {
        seen.flags&=~PW_WINDOW_ACTIVE;
        if(!(seen.flags&PW_WINDOW_GLASS) && seen.shadow!=PW_SHADOW_WINDOW)seen.material=0;
    }
    return key_bytes(key,&seen,sizeof(seen));
}
/* Everything a glass window's backdrop is made from: its own place and recipe, the screen, the Mica images, and for
 * the blurred backdrops every visible row beneath it that reaches its blur area (with its shadow), its images' versions
 * and the thumbnails drawn there. Equal keys mean equal backdrop pixels. */
static uint64_t backdrop_key(const PwWindowEffects *effects,const PwWindowSnapshot *snapshot,uint32_t index,
                             const PwWindowImage *images,size_t count,uint32_t width,uint32_t height)
{
    const PwWindowRow *glass=&snapshot->windows[index];
    uint64_t key=key_row(0xcbf29ce484222325ull,glass,1);
    const uint32_t screen[2]={width,height};
    key=key_bytes(key,screen,sizeof(screen));
    if(glass->backdrop==PW_BACKDROP_MICA || glass->backdrop==PW_BACKDROP_MICA_ALT) {
        const uint64_t mica[6]={effects->mica_valid,effects->mica_generation,effects->mica_sequence,
                                effects->mica_alt_valid,effects->mica_alt_generation,effects->mica_alt_sequence};
        return key_bytes(key,mica,sizeof(mica));
    }
    /* the blur area (pw_window_effects.h): a flyout blurs only what lies under it; acrylic reaches its margin */
    const int64_t margin=pw_window_flyout_backdrop(glass->backdrop) ? 0 : PW_ACRYLIC_SCALE*PW_ACRYLIC_RADIUS*PW_BLUR_PASSES;
    const int64_t reach=PW_SHADOW_WINDOW_REACH; /* every shadow's extent */
    for(uint32_t i=index+1;i<snapshot->count;++i) {
        const PwWindowRow *row=&snapshot->windows[i];
        if(!(row->flags&PW_WINDOW_VISIBLE))continue;
        if((int64_t)row->right+reach<=(int64_t)glass->left-margin || (int64_t)row->left-reach>=(int64_t)glass->right+margin ||
           (int64_t)row->bottom+reach<=(int64_t)glass->top-margin || (int64_t)row->top-reach>=(int64_t)glass->bottom+margin)continue;
        key=key_bytes(key,&i,sizeof(i));
        key=key_row(key,row,0);
        if(row->flags&PW_WINDOW_SHAPED)
            key=key_bytes(key,&snapshot->clips[row->shape_start],(size_t)row->shape_count*sizeof(PwWindowClip));
        if(row->flags&PW_WINDOW_CLIENT)
            key=key_bytes(key,&snapshot->clips[row->clip_start],(size_t)row->clip_count*sizeof(PwWindowClip));
        for(size_t k=0;k<count;++k) {
            const PwWindowImage *image=&images[k];
            if(image->hwnd!=row->hwnd || image->pid!=row->pid || image->generation!=row->generation)continue;
            const uint64_t version[7]={image->plane,image->width,image->height,(uint32_t)image->offset_x,
                                       (uint32_t)image->offset_y,image->flags,image->sequence};
            key=key_bytes(key,version,sizeof(version));
        }
        for(uint32_t t=0;t<snapshot->thumbnail_count;++t) {
            const PwWindowThumbnail *thumbnail=&snapshot->thumbnails[t];
            if(thumbnail->destination!=row->hwnd)continue;
            key=key_bytes(key,thumbnail,sizeof(*thumbnail));
            for(size_t k=0;k<count;++k)
                if(images[k].hwnd==thumbnail->source)key=key_bytes(key,&images[k].sequence,sizeof(images[k].sequence));
        }
    }
    return key;
}
/* The cache entry for a window: its own, else the least recently used. */
static struct PwBackdropCache *backdrop_entry(PwWindowEffects *effects,const PwWindowRow *row)
{
    struct PwBackdropCache *oldest=&effects->backdrops[0];
    for(unsigned i=0;i<PW_BACKDROP_CACHE_LIMIT;++i) {
        struct PwBackdropCache *entry=&effects->backdrops[i];
        if(entry->valid && entry->hwnd==row->hwnd && entry->pid==row->pid)return entry;
        if(!entry->valid || entry->used<oldest->used)oldest=entry;
    }
    oldest->valid=0;
    return oldest;
}
/* The window's backdrop rows into its cache entry, the solid span skipped as paint skips it. */
static int build_backdrop(PwWindowEffects *effects,struct PwBackdropCache *entry,const PwWindowRow *row,const Material *material,
                          uint32_t width,uint32_t height,uint64_t key)
{
    const int64_t left=row->left<0 ? 0 : row->left,top=row->top<0 ? 0 : row->top;
    const int64_t right=row->right>(int64_t)width ? (int64_t)width : row->right,bottom=row->bottom>(int64_t)height ? (int64_t)height : row->bottom;
    entry->valid=0;
    if(right<=left || bottom<=top)return 0;
    const uint32_t stride=(uint32_t)(right-left);
    if(reserve(effects,&entry->pixels,&entry->capacity,(size_t)stride*(size_t)(bottom-top)))return -1;
    const unsigned radius=pw_window_row_radius(row);
    for(int64_t y=top;y<bottom;++y) {
        uint32_t *line=entry->pixels+(size_t)(y-top)*stride;
        const int corner=radius && (y<row->top+(int64_t)radius || y>=row->bottom-(int64_t)radius);
        int64_t from=right,to=right;
        if(!corner && y>=row->solid_top && y<row->solid_bottom) {
            from=row->solid_left>left ? row->solid_left : left;to=row->solid_right<right ? row->solid_right : right;
            if(to<=from)from=to=right;
        }
        if(from<right) {
            if(from>left)material_row(material,line,y,left,(uint32_t)(from-left));
            if(right>to)material_row(material,line+(to-left),y,to,(uint32_t)(right-to));
        } else material_row(material,line,y,left,(uint32_t)(right-left));
        if(material->ring)ring_row(material,row,line,y,left,stride);
    }
    entry->hwnd=row->hwnd;entry->pid=row->pid;entry->key=key;
    entry->left=(int32_t)left;entry->top=(int32_t)top;entry->right=(int32_t)right;entry->bottom=(int32_t)bottom;
    entry->valid=1;++effects->backdrop_builds;
    return 0;
}
/* Skia's SkAlphaMulQ: each colour channel (v*(256-a))>>8; the pixel's own alpha is kept. */
static uint32_t darken(uint32_t pixel,uint32_t a)
{
    const uint32_t scale=256-a;
    return ((pixel&0xff00ffu)*scale>>8&0xff00ffu)|((pixel>>8&0xffu)*scale&0xff00u)|(pixel&0xff000000u);
}
static uint32_t tap(const PwShadowKernel *kernel,int64_t offset)
{
    return offset<-kernel->border || offset>kernel->border ? 0 : kernel->weights[offset+kernel->border];
}
/* One layer of a framed window's or a flyout's shadow (pw_window_effects.h), over the pixels the window leaves
 * uncovered. Its mask is Skia's two passes over the layer's rounded rectangle: a full row's value across each column
 * times its weight down each row, and near a rounded corner what the corner's rows lack. Beside a straight side the
 * alpha is the column's, above and below one the row's, so only the squares around the corners are computed per pixel
 * (host clang, an active 1100 x 700 window: 0.38 ms, where the 18 px shadow before it took 0.63); the unblurred 1 px
 * ring's corners are Skia's analytic AA (pw_window_ring_corners), not the supersampled circle. A layer is set up
 * once (shadow_layer_begin) and gives one row's alphas at a time (shadow_layer_row), so layers can be combined per row
 * without a buffer. */
enum { SHADOW_ZONE=PW_WINDOW_RADIUS_LIMIT+1+2*PW_SHADOW_BORDER_LIMIT };
typedef struct ShadowLayer {
    PwShadowKernel kernel;
    const PwShadowLayer *layer;
    unsigned radius,round,ring; /* ring: the 1 px ring, its corners pw_window_ring_corners */
    int64_t border,l,r,t,b,x0,x1,y0,y1,zone[2],zone_width;
    uint8_t across[1920],column_alpha[1920];
    int16_t lack[2][PW_WINDOW_RADIUS_LIMIT+1][SHADOW_ZONE];
} ShadowLayer;
/* 0 when the layer reaches no pixel of the output. */
static int shadow_layer_begin(ShadowLayer *s,uint32_t width,uint32_t height,const PwWindowRow *row,const PwShadowLayer *layer)
{
    if(pw_window_shadow_kernel(&s->kernel,layer->blur))return 0;
    s->layer=layer;
    s->radius=pw_window_row_radius(row);s->round=s->radius ? s->radius+layer->spread : 0; /* square stays square */
    s->border=s->kernel.border;
    s->ring=!s->border && s->round==PW_SHADOW_RING_RADIUS;
    s->l=(int64_t)row->left-layer->spread;s->r=(int64_t)row->right+layer->spread;
    s->t=(int64_t)row->top+layer->offset-layer->spread;s->b=(int64_t)row->bottom+layer->offset+layer->spread;
    s->x0=s->l-s->border<0 ? 0 : s->l-s->border;s->x1=s->r+s->border>width ? width : s->r+s->border;
    s->y0=s->t-s->border<0 ? 0 : s->t-s->border;s->y1=s->b+s->border>height ? height : s->b+s->border;
    if(s->x1<=s->x0 || s->y1<=s->y0)return 0;
    for(int64_t x=s->x0;x<s->x1;++x) {
        s->across[x-s->x0]=(uint8_t)pw_window_shadow_pass(&s->kernel,255ull*pw_window_shadow_span(&s->kernel,x,s->l,s->r));
        s->column_alpha[x-s->x0]=(uint8_t)((layer->alpha*(pw_window_shadow_pass(&s->kernel,(uint64_t)s->across[x-s->x0]*s->kernel.divisor)+1))>>8);
    }
    /* A corner row lacks 255-coverage at its first and last `round` pixels; across, that changes only the columns
     * within the kernel's reach of them (a zone at each side), kept as differences from a full row. */
    uint8_t missing[PW_WINDOW_RADIUS_LIMIT+1][PW_WINDOW_RADIUS_LIMIT+1];
    s->zone[0]=s->l-s->border;s->zone[1]=s->r-s->round-s->border;s->zone_width=s->round+2*s->border;
    for(unsigned j=0;j<s->round;++j)for(unsigned c=0;c<s->round;++c)missing[j][c]=(uint8_t)(255-pw_window_corner_coverage(s->round,c,j));
    for(unsigned side=0;side<2;++side)for(int64_t i=0;i<s->zone_width;++i) {
        const int64_t x=s->zone[side]+i;
        const uint64_t full=255ull*pw_window_shadow_span(&s->kernel,x,s->l,s->r);
        for(unsigned j=0;j<s->round;++j) {
            uint64_t sum=full;
            for(unsigned c=0;c<s->round;++c)sum-=(uint64_t)missing[j][c]*(tap(&s->kernel,x-(s->l+c))+tap(&s->kernel,x-(s->r-1-c)));
            s->lack[side][j][i]=(int16_t)((int32_t)pw_window_shadow_pass(&s->kernel,sum)-(int32_t)pw_window_shadow_pass(&s->kernel,full));
        }
    }
    return 1;
}
/* The layer's darkening a=(alpha*(mask+1))>>8 of row y's pixels the window leaves uncovered (all, beside it, or
 * beside its fully covered middle: spans, within [x0,x1)) into alpha[x-x0], 0 where the window covers one; 0 when the
 * row lies outside the layer. Only the spans are written. */
static int shadow_layer_row(const ShadowLayer *s,const PwWindowRow *row,int64_t y,uint8_t *alpha,int64_t spans[2][2])
{
    const PwShadowKernel *kernel=&s->kernel;
    const int64_t x0=s->x0,x1=s->x1,border=s->border;
    if(y<s->y0 || y>=s->y1)return 0;
    const uint64_t down=pw_window_shadow_span(kernel,y,s->t,s->b);
    uint32_t weights[PW_WINDOW_RADIUS_LIMIT+1],corners=0;  /* the corner rows': top t+j and bottom b-1-j */
    for(unsigned j=0;j<s->round;++j)corners|=weights[j]=tap(kernel,y-(s->t+j))+tap(kernel,y-(s->b-1-j));
    const int inside_y=y>=row->top && y<row->bottom;
    const int window_corner=inside_y && (y<row->top+(int64_t)s->radius || y>=row->bottom-(int64_t)s->radius);
    /* Where every row the kernel reaches is a full one, the column decides; where every column is, the row. */
    const int by_column=!corners && down==kernel->divisor && !window_corner;
    const uint32_t row_alpha=(s->layer->alpha*(pw_window_shadow_pass(kernel,255ull*down)+1))>>8;
    const int64_t fast_l=s->l+border+(corners ? s->round : 0),fast_r=s->r-border-(corners ? s->round : 0);
    spans[0][0]=x0;spans[0][1]=x1;spans[1][0]=spans[1][1]=x1;
    if(inside_y) {
        const int64_t inset=window_corner ? s->radius : 0;
        spans[0][1]=row->left+inset<x1 ? (row->left+inset>x0 ? row->left+inset : x0) : x1;
        spans[1][0]=row->right-inset>x0 ? (row->right-inset<x1 ? row->right-inset : x1) : x0;
        if(spans[1][0]<spans[0][1])spans[1][0]=spans[0][1];
    }
    for(unsigned n=0;n<2;++n) {
        const int64_t from=spans[n][0],to=spans[n][1];
        if(by_column) {
            memcpy(alpha+(from-x0),s->column_alpha+(from-x0),(size_t)(to>from ? to-from : 0));
            continue;
        }
        const int64_t run_l=fast_l>from ? (fast_l<to ? fast_l : to) : from,run_r=fast_r<to ? (fast_r>run_l ? fast_r : run_l) : to;
        if(run_r>run_l)memset(alpha+(run_l-x0),(int)row_alpha,(size_t)(run_r-run_l));
        for(int64_t x=from;x<to;++x) {
            if(x==run_l)x=run_r;
            if(x>=to)break;
            const uint32_t cover=inside_y && x>=row->left && x<row->right ? pw_window_row_coverage(row,x,y) : 0;
            if(cover==255) {alpha[x-x0]=0;continue;}
            int64_t sum=(int64_t)s->across[x-x0]*(int64_t)down;
            int ring=-1;
            for(unsigned side=0;corners && side<2;++side) {
                const int64_t i=x-s->zone[side];
                if(i<0 || i>=s->zone_width)continue;
                if(s->ring) {  /* unblurred: the corner's own coverage, top or bottom */
                    const int top=y-s->t<(int64_t)s->round;
                    ring=pw_window_ring_corners[(top ? 0 : 2)+side][top ? y-s->t : s->b-1-y][side ? s->round-1-i : i];
                } else for(unsigned j=0;j<s->round;++j)sum+=(int64_t)weights[j]*s->lack[side][j][i];
                break;
            }
            uint32_t mask=ring>=0 ? (uint32_t)ring : pw_window_shadow_pass(kernel,(uint64_t)sum);
            if(cover) {const uint32_t m=mask*(255-cover)+128;mask=(m+(m>>8))>>8;} /* the clip out of the border box */
            alpha[x-x0]=(uint8_t)((s->layer->alpha*(mask+1))>>8);
        }
    }
    return 1;
}
static void shadow(uint32_t *output,uint32_t width,uint32_t height,const PwWindowRow *row)
{
    if(row->shadow==PW_SHADOW_WINDOW) {
        /* each layer darkens the pixels in turn, bottom first, as Chromium draws a window's box-shadows */
        const PwShadowLayer *layers=pw_window_shadow_layers[!!(row->flags&PW_WINDOW_ACTIVE)][row->material==PW_MATERIAL_DARK];
        ShadowLayer s;
        uint8_t alpha[1920];
        int64_t spans[2][2];
        for(unsigned i=0;i<3;++i) {
            if(!shadow_layer_begin(&s,width,height,row,&layers[i]))continue;
            for(int64_t y=s.y0;y<s.y1;++y) {
                uint32_t *line=output+(size_t)y*width;
                shadow_layer_row(&s,row,y,alpha,spans);
                for(unsigned n=0;n<2;++n)for(int64_t x=spans[n][0];x<spans[n][1];++x)
                    if(alpha[x-s.x0])line[x]=darken(line[x],alpha[x-s.x0]);
            }
        }
        return;
    }
    if(row->shadow==PW_SHADOW_FLYOUT || row->shadow==PW_SHADOW_TOOLTIP) {
        /* --shadow-28 (a tooltip's --shadow-8): Chromium draws a flyout (backdrop-filter, its own layer) and its two
         * box-shadows into a layer, the top one over the bottom (A = a + A(256-a)>>8), then darkens the page by that once */
        const PwShadowLayer *layers=(row->shadow==PW_SHADOW_TOOLTIP ? pw_window_tooltip_shadow_layers
                                                                    : pw_flyout_shadow_layers)[row->material==PW_MATERIAL_DARK];
        ShadowLayer s[2];
        uint8_t alpha[1920];
        int64_t spans[2][2],x0=width,x1=0,y0=height,y1=0;
        unsigned used=0;
        for(unsigned i=0;i<2;++i)if(shadow_layer_begin(&s[used],width,height,row,&layers[i])) {
            if(s[used].x0<x0)x0=s[used].x0;
            if(s[used].x1>x1)x1=s[used].x1;
            if(s[used].y0<y0)y0=s[used].y0;
            if(s[used].y1>y1)y1=s[used].y1;
            ++used;
        }
        for(int64_t y=y0;y<y1;++y) {
            uint32_t *line=output+(size_t)y*width;
            uint8_t combined[1920];
            memset(combined+x0,0,(size_t)(x1-x0));
            for(unsigned i=0;i<used;++i) {
                if(!shadow_layer_row(&s[i],row,y,alpha,spans))continue;
                for(unsigned n=0;n<2;++n)for(int64_t x=spans[n][0];x<spans[n][1];++x) {
                    const uint32_t a=alpha[x-s[i].x0];
                    combined[x]=(uint8_t)(a+((combined[x]*(256-a))>>8));
                }
            }
            for(int64_t x=x0;x<x1;++x)if(combined[x])line[x]=darken(line[x],combined[x]);
        }
        return;
    }
    const int64_t size=PW_SHADOW_SMALL_SIZE,offset=PW_SHADOW_SMALL_OFFSET;
    const unsigned radius=pw_window_row_radius(row);
    int64_t top=(int64_t)row->top+offset-size,bottom=(int64_t)row->bottom+offset+size;
    int64_t left=(int64_t)row->left-size,right=(int64_t)row->right+size;
    if(top<0)top=0;
    if(left<0)left=0;
    if(bottom>height)bottom=height;
    if(right>width)right=width;
    /* Beside the shadow box's straight sides its distance is the column's alone, above and below them the row's
     * alone (pw_window_rounded_distance: oy, or ox, is exactly 0 there), so those alphas are taken once and only the
     * corners are computed per pixel; the pixels are the same. */
    const int64_t box_top=(int64_t)row->top+offset,box_bottom=(int64_t)row->bottom+offset;
    int16_t columns[2][64];
    for(unsigned i=0;i<64;++i)columns[0][i]=columns[1][i]=-1;
    for(int64_t y=top;y<bottom;++y) {
        uint32_t *line=output+(size_t)y*width;
        const int straight_y=y>=box_top+(int64_t)radius && y<box_bottom-(int64_t)radius;
        int16_t row_alpha=-1;
        for(int64_t x=left;x<right;++x) {
            /* Pixels the opaque window fully covers are painted next anyway. */
            if(x>=row->left+(int64_t)radius && x<row->right-(int64_t)radius &&
               y>=row->top && y<row->bottom) {x=row->right-(int64_t)radius-1;continue;}
            if(pw_window_row_coverage(row,x,y)==255)continue;
            int16_t *once=NULL;
            if(straight_y && x<row->left && row->left-x<=64)once=&columns[0][row->left-x-1];
            else if(straight_y && x>=row->right && x-row->right<64)once=&columns[1][x-row->right];
            else if(!straight_y && x>=row->left+(int64_t)radius && x<row->right-(int64_t)radius)once=&row_alpha;
            uint32_t alpha;
            if(once && *once>=0)alpha=(uint32_t)*once;
            else {
                const float distance=pw_window_rounded_distance((float)x+0.5f,(float)y+0.5f,(float)row->left,
                    (float)box_top,(float)row->right,(float)box_bottom,(float)radius);
                alpha=pw_window_shadow_alpha(row->shadow,distance);
                if(once)*once=(int16_t)alpha;
            }
            if(alpha)line[x]=mix(line[x],line[x]&0xff000000u,alpha);
        }
    }
}
static void border(uint32_t *output,uint32_t width,uint32_t height,const PwWindowRow *row)
{
    const unsigned radius=pw_window_row_radius(row);
    const uint32_t colour=0xff000000u|row->border;
    /* An alpha (Windows 11's default border, #66757575) blends over the window's own edge; none is opaque. */
    const uint32_t alpha=row->border>>24 ? row->border>>24 : 255;
    const int64_t w=width,h=height;
    const int64_t top=row->top<0 ? 0 : row->top,bottom=row->bottom>h ? h : row->bottom;
    const int64_t left=row->left<0 ? 0 : row->left,right=row->right>w ? w : row->right;
    for(int64_t y=top;y<bottom;++y) {
        uint32_t *line=output+(size_t)y*width;
        const int edge_row=y==row->top || y==row->bottom-1;
        const int64_t ty=y-row->top,by=(int64_t)row->bottom-1-y,j=ty<radius ? ty : by<radius ? by : -1;
        if(!edge_row && j<0) {
            /* A straight side: only its outermost columns. */
            if(row->left>=0 && row->left<w)line[row->left]=mix(line[row->left],colour,alpha);
            if((int64_t)row->right-1>=0 && (int64_t)row->right-1<w)line[row->right-1]=mix(line[row->right-1],colour,alpha);
            continue;
        }
        for(int64_t x=left;x<right;++x) {
            const int64_t lx=x-row->left,rx=(int64_t)row->right-1-x,i=lx<radius ? lx : rx<radius ? rx : -1;
            if(i>=0 && j>=0)line[x]=mix(line[x],colour,(pw_window_corner_border(radius,(unsigned)i,(unsigned)j)*alpha+127)/255);
            else if(edge_row || x==row->left || x==row->right-1)line[x]=mix(line[x],colour,alpha);
        }
    }
}
/* A flyout's 1 px stroke ring shows the untinted backdrop; the tint covers the padding box, its corners rounded one
 * pixel less (the stroke is drawn over the ring afterwards). */
static void ring_row(const Material *material,const PwWindowRow *row,uint32_t *out,int64_t y,int64_t x0,uint32_t count)
{
    const unsigned radius=pw_window_row_radius(row),inner=radius ? radius-1 : 0;
    const int edge=y==row->top || y==row->bottom-1;
    const int64_t ty=y-row->top-1,by=(int64_t)row->bottom-2-y,j=ty<(int64_t)inner ? ty : by<(int64_t)inner ? by : -1;
    if(!edge && j<0) {
        /* A straight side: only the outermost columns lie in the ring. */
        Material untinted=*material;untinted.amount=0;
        if(row->left>=x0 && row->left<x0+(int64_t)count)material_row(&untinted,out+(row->left-x0),y,row->left,1);
        if(row->right-1>=x0 && row->right-1<x0+(int64_t)count)material_row(&untinted,out+(row->right-1-x0),y,row->right-1,1);
        return;
    }
    uint32_t bare[1920];
    Material untinted=*material;untinted.amount=0;material_row(&untinted,bare,y,x0,count);
    for(uint32_t i=0;i<count;++i) {
        const int64_t x=x0+i,lx=x-row->left-1,rx=(int64_t)row->right-2-x;
        if(edge || lx<0 || rx<0) {out[i]=bare[i];continue;}
        const int64_t k=lx<(int64_t)inner ? lx : rx<(int64_t)inner ? rx : -1;
        if(k<0 || j<0)continue;
        const uint32_t cover=pw_window_corner_coverage(inner,(unsigned)k,(unsigned)j);
        if(cover<255)out[i]=mix(bare[i],out[i],cover);
    }
}
static void paint(uint32_t *output,uint32_t width,uint32_t height,const PwWindowRow *row,
                  const PwWindowImage *image,const PwWindowClip *clip,const Material *material)
{
    if(!image)return;
    const int gpu=image->plane==PW_WINDOW_GPU;
    const int64_t origin_x=gpu ? row->client_left : (int64_t)row->left+image->offset_x;
    const int64_t origin_y=gpu ? row->client_top : (int64_t)row->top+image->offset_y;
    int64_t left=row->left,top=row->top,right=row->right,bottom=row->bottom;
    if(gpu) {
        if(left<row->client_left)left=row->client_left;
        if(top<row->client_top)top=row->client_top;
        if(right>row->client_right)right=row->client_right;
        if(bottom>row->client_bottom)bottom=row->client_bottom;
    }
    if(clip) {
        if(left<clip->left)left=clip->left;
        if(top<clip->top)top=clip->top;
        if(right>clip->right)right=clip->right;
        if(bottom>clip->bottom)bottom=clip->bottom;
    }
    if(left<origin_x)left=origin_x;
    if(top<origin_y)top=origin_y;
    if(right>origin_x+image->width)right=origin_x+image->width;
    if(bottom>origin_y+image->height)bottom=origin_y+image->height;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>width)right=width;
    if(bottom>height)bottom=height;
    if(right<=left || bottom<=top)return;
    const unsigned radius=pw_window_row_radius(row);
    const int glass=material && !gpu;
    const int premultiplied=image->flags&PW_WINDOW_PREMULTIPLIED;
    for(int64_t y=top;y<bottom;++y) {
        uint32_t *dest=output+(size_t)y*width+(size_t)left;
        const uint32_t *source=(const uint32_t *)((const char *)image->pixels+
            (size_t)(y-origin_y)*image->stride+(size_t)(left-origin_x)*4);
        const int corner=radius && (y<row->top+(int64_t)radius || y>=row->bottom-(int64_t)radius);
        if(!corner && !glass) {
            if(!premultiplied)memcpy(dest,source,(size_t)(right-left)*4);
            else for(int64_t x=0;x<right-left;++x)dest[x]=over(source[x],dest[x]);
            continue;
        }
        const int solid_line=glass && y>=row->solid_top && y<row->solid_bottom;
        /* An opaque row of a glass window outside its corners: the window's own span is copied whole, only the glass
         * edges beside it take the material. */
        int64_t solid_from=right,solid_to=right;
        if(solid_line && !corner) {
            solid_from=row->solid_left>left ? row->solid_left : left;
            solid_to=row->solid_right<right ? row->solid_right : right;
            if(solid_to<=solid_from)solid_from=solid_to=right;
            else if(!premultiplied)memcpy(dest+(solid_from-left),source+(solid_from-left),(size_t)(solid_to-solid_from)*4);
            else for(int64_t x=solid_from-left;x<solid_to-left;++x)dest[x]=over(source[x],dest[x]);
        }
        uint32_t line[1920]; /* spans never exceed the 1920-pixel output */
        const uint32_t *backdrop=line;
        if(glass && material->cached)
            backdrop=material->cached+(size_t)(y-material->cached_top)*material->cached_stride+(size_t)(left-material->cached_left);
        else if(glass) {
            if(solid_from<right) {
                if(solid_from>left)material_row(material,line,y,left,(uint32_t)(solid_from-left));
                if(right>solid_to)material_row(material,line+(solid_to-left),y,solid_to,(uint32_t)(right-solid_to));
            } else material_row(material,line,y,left,(uint32_t)(right-left));
            if(material->ring)ring_row(material,row,line,y,left,(uint32_t)(right-left));
        }
        for(int64_t x=0;x<right-left;++x) {
            if(x==solid_from-left) {x=solid_to-left-1;continue;}
            const int64_t sx=left+x;
            const uint32_t cover=corner ? pw_window_row_coverage(row,sx,y) : 255;
            if(!cover)continue;
            uint32_t value=source[x];
            if(glass && !(solid_line && sx>=row->solid_left && sx<row->solid_right)) {
                /* Transparent glass is the material itself; opaque glass is the window. */
                const uint32_t alpha=value>>24;
                value=!value ? backdrop[x] : alpha==255 ? value : over(value,backdrop[x]);
            }
            else if(premultiplied) {dest[x]=over(scaled(value,cover),dest[x]);continue;}
            dest[x]=cover==255 ? value : mix(dest[x],value,cover);
        }
    }
}
/* Compose the source window's own planes into a source-rectangle scratch. */
static void compose_source(uint32_t *scratch,uint32_t sw,uint32_t sh,const PwWindowRow *row,
                           const PwWindowImage *gdi,const PwWindowImage *gpu,const PwWindowThumbnail *thumbnail)
{
    for(size_t i=0;i<(size_t)sw*sh;++i)scratch[i]=0xff000000u;
    for(uint32_t y=0;y<sh;++y)for(uint32_t x=0;x<sw;++x) {
        const int64_t wx=(int64_t)thumbnail->source_left+x,wy=(int64_t)thumbnail->source_top+y;
        uint32_t *dest=&scratch[(size_t)y*sw+x];
        if(gdi) {
            const int64_t ix=wx-gdi->offset_x,iy=wy-gdi->offset_y;
            if(ix>=0 && iy>=0 && ix<gdi->width && iy<gdi->height) {
                const uint32_t value=*(const uint32_t *)((const char *)gdi->pixels+(size_t)iy*gdi->stride+(size_t)ix*4);
                *dest=gdi->flags&PW_WINDOW_PREMULTIPLIED ? over(value,*dest) : value;
            }
        }
        if(gpu) {
            const int64_t cx=(int64_t)row->left+wx,cy=(int64_t)row->top+wy;
            const int64_t ix=cx-row->client_left,iy=cy-row->client_top;
            if(cx<row->client_right && cy<row->client_bottom && ix>=0 && iy>=0 && ix<gpu->width && iy<gpu->height)
                *dest=*(const uint32_t *)((const char *)gpu->pixels+(size_t)iy*gpu->stride+(size_t)ix*4);
        }
        *dest|=0xff000000u;
    }
}
static int thumbnails(PwWindowEffects *effects,uint32_t *output,uint32_t width,uint32_t height,
                      const PwWindowSnapshot *snapshot,const PwWindowImage *images,size_t count,const PwWindowRow *row)
{
    for(uint32_t n=0;n<snapshot->thumbnail_count;++n) {
        const PwWindowThumbnail *thumbnail=&snapshot->thumbnails[n];
        if(thumbnail->destination!=row->hwnd || thumbnail->destination_generation!=row->generation)continue;
        const PwWindowRow *source=row_for(snapshot,thumbnail->source,thumbnail->source_generation);
        const PwWindowImage *gdi=image_for(source,images,count,PW_WINDOW_GDI);
        const PwWindowImage *gpu=image_for(source,images,count,PW_WINDOW_GPU);
        if(!gdi && !gpu)continue;
        const uint32_t dw=(uint32_t)(thumbnail->right-thumbnail->left),dh=(uint32_t)(thumbnail->bottom-thumbnail->top);
        const uint32_t sw=(uint32_t)(thumbnail->source_right-thumbnail->source_left);
        const uint32_t sh=(uint32_t)(thumbnail->source_bottom-thumbnail->source_top);
        PwThumbnailCache *cache=&effects->thumbnails[n];
        const uint32_t gdi_sequence=gdi ? gdi->sequence : 0,gpu_sequence=gpu ? gpu->sequence : 0;
        if(!cache->valid || cache->source!=thumbnail->source || cache->source_generation!=thumbnail->source_generation ||
           cache->gdi_sequence!=gdi_sequence || cache->gpu_sequence!=gpu_sequence || cache->width!=dw || cache->height!=dh ||
           cache->source_left!=thumbnail->source_left || cache->source_top!=thumbnail->source_top ||
           cache->source_right!=thumbnail->source_right || cache->source_bottom!=thumbnail->source_bottom) {
            cache->valid=0;
            if(reserve(effects,&effects->source,&effects->source_capacity,(size_t)sw*sh) ||
               reserve(effects,&cache->pixels,&cache->capacity,(size_t)dw*dh))return -1;
            compose_source(effects->source,sw,sh,source,gdi,gpu,thumbnail);
            if(pw_window_scale(cache->pixels,dw,dh,effects->source,sw,sh,sw*4))return -1;
            cache->source=thumbnail->source;cache->source_generation=thumbnail->source_generation;
            cache->gdi_sequence=gdi_sequence;cache->gpu_sequence=gpu_sequence;cache->width=dw;cache->height=dh;
            cache->source_left=thumbnail->source_left;cache->source_top=thumbnail->source_top;
            cache->source_right=thumbnail->source_right;cache->source_bottom=thumbnail->source_bottom;
            cache->valid=1;++effects->thumbnail_builds;
        }
        int64_t top=thumbnail->top,bottom=thumbnail->bottom,left=thumbnail->left,right=thumbnail->right;
        if(top<row->top)top=row->top;
        if(left<row->left)left=row->left;
        if(bottom>row->bottom)bottom=row->bottom;
        if(right>row->right)right=row->right;
        if(top<0)top=0;
        if(left<0)left=0;
        if(bottom>height)bottom=height;
        if(right>width)right=width;
        for(int64_t y=top;y<bottom;++y)for(int64_t x=left;x<right;++x) {
            const uint32_t value=cache->pixels[(size_t)(y-thumbnail->top)*dw+(size_t)(x-thumbnail->left)];
            const uint32_t amount=(thumbnail->opacity*pw_window_row_coverage(row,x,y)+127)/255;
            output[(size_t)y*width+x]=mix(output[(size_t)y*width+x],value,amount);
        }
    }
    return 0;
}
static int valid_row(const PwWindowSnapshot *snapshot,const PwWindowRow *row)
{
    const uint32_t known=PW_WINDOW_VISIBLE|PW_WINDOW_DESKTOP|PW_WINDOW_UNSUPPORTED|PW_WINDOW_CLIENT|
                         PW_WINDOW_CHILD|PW_WINDOW_SHAPED|PW_WINDOW_BORDER|PW_WINDOW_GLASS|PW_WINDOW_ACTIVE;
    if(!row->hwnd || !row->generation || row->right<row->left || row->bottom<row->top ||
       row->client_right<row->client_left || row->client_bottom<row->client_top ||
       row->clip_start>snapshot->clip_count || row->clip_count>snapshot->clip_count-row->clip_start ||
       row->shape_start>snapshot->clip_count || row->shape_count>snapshot->clip_count-row->shape_start ||
       (!(row->flags&PW_WINDOW_SHAPED) && row->shape_count) || (row->flags&~known))return 0;
    if(row->radius>PW_WINDOW_RADIUS_LIMIT || row->backdrop>PW_BACKDROP_FLYOUT || row->material>PW_MATERIAL_DARK ||
       row->shadow>PW_SHADOW_TOOLTIP ||
       (!(row->flags&PW_WINDOW_BORDER) && row->border) ||
       (row->backdrop!=PW_BACKDROP_NONE)!=!!(row->flags&PW_WINDOW_GLASS))return 0;
    if(row->flags&PW_WINDOW_GLASS) {
        if(row->solid_right<row->solid_left || row->solid_bottom<row->solid_top)return 0;
    } else if(row->solid_left || row->solid_top || row->solid_right || row->solid_bottom)return 0;
    /* Effects belong to top-level windows; children draw inside their parent. */
    if((row->flags&(PW_WINDOW_CHILD|PW_WINDOW_DESKTOP)) &&
       (row->radius || row->shadow || (row->flags&(PW_WINDOW_BORDER|PW_WINDOW_GLASS))))return 0;
    return 1;
}
static int valid_thumbnail(const PwWindowSnapshot *snapshot,const PwWindowThumbnail *thumbnail)
{
    if(!thumbnail->destination || !thumbnail->source || thumbnail->destination==thumbnail->source ||
       !thumbnail->destination_generation || !thumbnail->source_generation || thumbnail->reserved ||
       thumbnail->opacity>255 || thumbnail->right<=thumbnail->left || thumbnail->bottom<=thumbnail->top ||
       (int64_t)thumbnail->right-thumbnail->left>1920 || (int64_t)thumbnail->bottom-thumbnail->top>1080 ||
       thumbnail->source_left<0 || thumbnail->source_top<0 || thumbnail->source_right<=thumbnail->source_left ||
       thumbnail->source_bottom<=thumbnail->source_top || thumbnail->source_right>2176 || thumbnail->source_bottom>1408)return 0;
    const PwWindowRow *destination=row_for(snapshot,thumbnail->destination,thumbnail->destination_generation);
    const PwWindowRow *source=row_for(snapshot,thumbnail->source,thumbnail->source_generation);
    return destination && source && !(destination->flags&(PW_WINDOW_CHILD|PW_WINDOW_DESKTOP)) &&
           !(source->flags&PW_WINDOW_CHILD);
}
int pw_windows_compose_effects(uint32_t *output,uint32_t width,uint32_t height,
                               const PwWindowSnapshot *snapshot,const PwWindowImage *images,size_t count,
                               PwWindowEffects *effects)
{
    if(!output || !width || width>1920 || !height || height>1080 || !snapshot ||
       snapshot->version!=PW_WINDOW_SNAPSHOT_VERSION || snapshot->size!=sizeof(*snapshot) ||
       snapshot->count>PW_WINDOW_LIMIT || snapshot->clip_count>PW_WINDOW_CLIP_LIMIT ||
       snapshot->thumbnail_count>PW_WINDOW_THUMBNAIL_LIMIT || count>PW_WINDOW_IMAGE_LIMIT || (count && !images)) {errno=EINVAL;return -1;}
    if(snapshot->overflow) {errno=EOVERFLOW;return -1;}
    for(size_t i=0;i<count;++i) {
        const PwWindowImage *image=&images[i];
        uint64_t needed=(uint64_t)(image->height?image->height-1:0)*image->stride+(uint64_t)image->width*4;
        if(!image->hwnd || image->pid<=0 || !image->generation || !image->pixels || !image->width ||
           image->width>2176 || !image->height || image->height>1408 || image->stride%4 ||
           image->stride<image->width*4 || needed>image->bytes || image->plane>PW_WINDOW_GPU ||
           (image->flags&~PW_WINDOW_PREMULTIPLIED) ||
           ((uintptr_t)image->pixels&3) ||
           (image->plane==PW_WINDOW_GPU && (image->offset_x || image->offset_y))) {errno=EINVAL;return -1;}
        for(size_t j=0;j<i;++j)
            if(image->hwnd==images[j].hwnd && image->pid==images[j].pid &&
               image->generation==images[j].generation && image->plane==images[j].plane) {errno=EINVAL;return -1;}
    }
    int needs_workspace=snapshot->thumbnail_count!=0;
    for(uint32_t i=0;i<snapshot->count;++i) {
        const PwWindowRow *row=&snapshot->windows[i];
        if(!valid_row(snapshot,row)) {errno=EINVAL;return -1;}
        if((row->flags&PW_WINDOW_VISIBLE) && (row->flags&PW_WINDOW_UNSUPPORTED)) {errno=ENOTSUP;return -1;}
        if((row->flags&PW_WINDOW_VISIBLE) && (row->flags&PW_WINDOW_GLASS))needs_workspace=1;
        for(uint32_t j=0;j<i;++j)if(row->hwnd==snapshot->windows[j].hwnd) {errno=EINVAL;return -1;}
    }
    for(uint32_t i=0;i<snapshot->clip_count;++i) {
        const PwWindowClip *clip=&snapshot->clips[i];
        if(clip->right<clip->left || clip->bottom<clip->top) {errno=EINVAL;return -1;}
    }
    for(uint32_t i=0;i<snapshot->thumbnail_count;++i)
        if(!valid_thumbnail(snapshot,&snapshot->thumbnails[i])) {errno=EINVAL;return -1;}
    /* Never omit requested content silently. */
    if(needs_workspace && !effects) {errno=ENOMEM;return -1;}
    memset(output,0,(size_t)width*height*4);
    int mica_ready=0;
    for(uint32_t n=snapshot->count;n;--n) {
        const PwWindowRow *row=&snapshot->windows[n-1];
        if(!(row->flags&PW_WINDOW_VISIBLE))continue;
        if(!(row->flags&PW_WINDOW_CHILD)) {
            Material material={0};
            struct PwBackdropCache *cached=NULL;uint64_t key=0;
            if(row->flags&PW_WINDOW_GLASS) {
                tint_for(row,&material);
                /* Mica's images first: a backdrop over them keys on their versions. */
                if(row->backdrop==PW_BACKDROP_MICA_ALT) {
                    if(prepare_mica_alt(effects,snapshot,images,count,width,height))return -1;
                } else if(row->backdrop==PW_BACKDROP_MICA) {
                    if(!mica_ready && prepare_mica(effects,snapshot,images,count,width,height))return -1;
                    mica_ready=1;
                }
                key=backdrop_key(effects,snapshot,n-1,images,count,width,height);
                cached=backdrop_entry(effects,row);
                cached->used=++effects->frame;
                if(cached->valid && cached->key==key) {
                    material.cached=cached->pixels;material.cached_left=cached->left;material.cached_top=cached->top;
                    material.cached_stride=(uint32_t)(cached->right-cached->left);++effects->backdrop_hits;
                } else if(row->backdrop==PW_BACKDROP_ACRYLIC || pw_window_prototype_backdrop(row->backdrop)) {
                    if(prepare_acrylic(effects,output,width,height,row,&material))return -1;
                } else if(row->backdrop==PW_BACKDROP_MICA_ALT) {
                    if(effects->mica_alt_valid) {
                        material.pixels=effects->mica_alt;material.width=effects->mica_alt_width;
                        material.height=effects->mica_alt_height;material.scale=PW_MICA_SCALE;
                    }
                } else {
                    if(effects->mica_valid) {
                        material.pixels=effects->mica;material.width=effects->mica_width;material.height=effects->mica_height;
                        material.scale=PW_MICA_SCALE;
                    }
                }
                if(!material.cached) {
                    if(build_backdrop(effects,cached,row,&material,width,height,key))return -1;
                    if(cached->valid) {
                        material.cached=cached->pixels;material.cached_left=cached->left;material.cached_top=cached->top;
                        material.cached_stride=(uint32_t)(cached->right-cached->left);
                    }
                }
            }
            if(row->shadow)shadow(output,width,height,row);
            const PwWindowImage *image=image_for(row,images,count,PW_WINDOW_GDI);
            const Material *glass=row->flags&PW_WINDOW_GLASS ? &material : NULL;
            if(row->flags&PW_WINDOW_SHAPED) {
                for(uint32_t i=0;i<row->shape_count;++i)
                    paint(output,width,height,row,image,&snapshot->clips[row->shape_start+i],glass);
            } else paint(output,width,height,row,image,NULL,glass);
        }
        if(row->flags&PW_WINDOW_CLIENT)for(uint32_t i=0;i<row->clip_count;++i)
            paint(output,width,height,row,image_for(row,images,count,PW_WINDOW_GPU),&snapshot->clips[row->clip_start+i],NULL);
        if(!(row->flags&PW_WINDOW_CHILD)) {
            if(row->flags&PW_WINDOW_BORDER)border(output,width,height,row);
            if(snapshot->thumbnail_count && thumbnails(effects,output,width,height,snapshot,images,count,row))return -1;
        }
    }
    return 0;
}
int pw_windows_compose(uint32_t *output,uint32_t width,uint32_t height,
                       const PwWindowSnapshot *snapshot,const PwWindowImage *images,size_t count)
{
    return pw_windows_compose_effects(output,width,height,snapshot,images,count,NULL);
}
