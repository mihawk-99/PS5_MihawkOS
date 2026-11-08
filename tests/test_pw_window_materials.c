/* SPDX-License-Identifier: GPL-3.0-or-later
 * Mica/acrylic materials and glass alpha against naive reference arithmetic. */
#include "../src/pw_window_effects.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned channel(uint32_t value,unsigned shift) {return (value>>shift)&255;}
static long clampl(long v,long lo,long hi) {return v<lo ? lo : v>hi ? hi : v;}
/* The documented blur definition, computed tap by tap. */
static void reference_blur(uint32_t *out,const uint32_t *src,long sw,long sh,long left,long top,
                           long aw,long ah,long scale,long radius,long passes)
{
    const long w=(aw+scale-1)/scale,h=(ah+scale-1)/scale;
    uint32_t *tmp=malloc((size_t)w*h*4);assert(tmp);
    for(long by=0;by<h;++by)for(long bx=0;bx<w;++bx) {
        uint32_t pixel=0;
        for(unsigned c=0;c<32;c+=8) {
            long sum=0;
            for(long dy=0;dy<scale;++dy)for(long dx=0;dx<scale;++dx)
                sum+=channel(src[clampl(top+by*scale+dy,0,sh-1)*sw+clampl(left+bx*scale+dx,0,sw-1)],c);
            pixel|=(uint32_t)((sum+scale*scale/2)/(scale*scale))<<c;
        }
        out[by*w+bx]=pixel;
    }
    const long span=2*radius+1;
    for(long pass=0;pass<passes;++pass) {
        for(long y=0;y<h;++y)for(long x=0;x<w;++x) {
            uint32_t pixel=0;
            for(unsigned c=0;c<32;c+=8) {long sum=0;for(long k=-radius;k<=radius;++k)sum+=channel(out[y*w+clampl(x+k,0,w-1)],c);
                pixel|=(uint32_t)((sum+span/2)/span)<<c;}
            tmp[y*w+x]=pixel;
        }
        for(long y=0;y<h;++y)for(long x=0;x<w;++x) {
            uint32_t pixel=0;
            for(unsigned c=0;c<32;c+=8) {long sum=0;for(long k=-radius;k<=radius;++k)sum+=channel(tmp[clampl(y+k,0,h-1)*w+x],c);
                pixel|=(uint32_t)((sum+span/2)/span)<<c;}
            out[y*w+x]=pixel;
        }
    }
    free(tmp);
}
static uint32_t reference_tint(uint32_t colour,uint32_t tint,uint32_t amount)
{
    uint32_t pixel=0xff000000;
    for(unsigned c=0;c<24;c+=8)pixel|=((channel(colour,c)*(255-amount)+channel(tint,c)*amount+127)/255)<<c;
    return pixel;
}
enum { W=96, H=64 };
/* W3C compositing's luminosity blend (SetLum(backdrop, Lum(grey))) then the tint, in doubles. */
static uint32_t reference_mica_alt(uint32_t colour,double grey,uint32_t tint,double amount)
{
    double c[3]={channel(colour,16),channel(colour,8),channel(colour,0)},l,n,x;
    const double d=grey-(0.3*c[0]+0.59*c[1]+0.11*c[2]);
    for(unsigned i=0;i<3;++i)c[i]+=d;
    l=0.3*c[0]+0.59*c[1]+0.11*c[2];n=fmin(c[0],fmin(c[1],c[2]));x=fmax(c[0],fmax(c[1],c[2]));
    for(unsigned i=0;i<3;++i) {
        if(n<0)c[i]=l+(c[i]-l)*l/(l-n);
        else if(x>255)c[i]=l+(c[i]-l)*(255-l)/(x-l);
    }
    uint32_t out=0xff000000u;
    for(unsigned i=0;i<3;++i)out|=(uint32_t)lround(c[i]*(255-amount)/255+channel(tint,16-8*i)*amount/255)<<(16-8*i);
    return out;
}
/* Mica Alt (src/pw_window_effects.h): the luminosity recipe within a level of doubles, including colours it clips; the
 * source of a uniform grey root is that grey wherever the canvas is fully covered and darker at its edges, where the
 * blur reaches past the drawn wallpaper (the prototype's canvas there is transparent, and black in its JPEG). */
static void mica_alt_checks(void)
{
    static const uint32_t colours[]={0xff000000,0xffffffff,0xff0060ff,0xff3c78c8,0xffff0000,0xff00ff00,0xffe6e6e6,0xff102030};
    for(unsigned i=0;i<sizeof(colours)/sizeof(*colours);++i)
        for(unsigned dark=0;dark<2;++dark) {
            const uint32_t got=pw_window_mica_alt_pixel(colours[i],dark ? 0x1e1e1e : 0xf2f2f2,dark ? 227 : 166,
                                                       dark ? PW_RECIPE_MICA_ALT_DARK : PW_RECIPE_MICA_ALT_LIGHT);
            const uint32_t want=reference_mica_alt(colours[i],dark ? 26 : 230,dark ? 0x1e1e1e : 0xf2f2f2,dark ? 227 : 166);
            for(unsigned shift=0;shift<24;shift+=8) assert(labs((long)channel(got,shift)-(long)channel(want,shift))<=1);
        }
    enum { SW=1920, SH=1080 };
    uint32_t *screen=malloc((size_t)SW*SH*4),*canvas=malloc((size_t)(SW/8)*(SH/8)*4);
    uint32_t *scratch=malloc(pw_window_mica_alt_scratch(SW,SH)*4);
    assert(screen && canvas && scratch);
    for(size_t i=0;i<(size_t)SW*SH;++i)screen[i]=0xff808080;
    assert(!pw_window_mica_alt_source(canvas,scratch,screen,SW,SH,SW*4,0,0,SW,SH));
    assert(canvas[67*240+120]==0xff808080 && canvas[9*240+9]==0xff808080);
    assert(channel(canvas[0],0)<0x80 && channel(canvas[0],0)>0x60);
    assert(pw_window_mica_alt_source(canvas,scratch,screen,SW,SH,SW*4,0,0,4,4)<0 && errno==EINVAL);
    /* A root placed past the area's edges repeats its edge pixels: the same source as that clamped image at 0,0. */
    enum { AW=200, AH=120, RW=150, RH=100 };
    static const int32_t places[][2]={{-30,17},{40,-9},{-60,-50}};
    uint32_t *clamped=malloc((size_t)AW*AH*4),*expect=malloc((size_t)(AW/8)*(AH/8)*4);
    assert(clamped && expect);
    for(size_t i=0;i<(size_t)RW*RH;++i)screen[i]=0xff000000u|(uint32_t)(i*2654435761u>>8);
    for(unsigned p=0;p<sizeof(places)/sizeof(*places);++p) {
        for(int y=0;y<AH;++y)for(int x=0;x<AW;++x) {
            const int sx=x+places[p][0],sy=y+places[p][1];
            clamped[y*AW+x]=screen[(sy<0 ? 0 : sy>=RH ? RH-1 : sy)*RW+(sx<0 ? 0 : sx>=RW ? RW-1 : sx)];
        }
        assert(!pw_window_mica_alt_source(expect,scratch,clamped,AW,AH,AW*4,0,0,AW,AH));
        assert(!pw_window_mica_alt_source(canvas,scratch,screen,RW,RH,RW*4,places[p][0],places[p][1],AW,AH));
        assert(!memcmp(canvas,expect,(size_t)(AW/8)*(AH/8)*4));
    }
    free(screen);free(canvas);free(scratch);free(clamped);free(expect);
}
static uint32_t root[W*H],glass[W*H],out[W*H],noise[W*H];
/* The backdrop cache: a frame that reuses a glass window's backdrop equals one that builds it from nothing, and a
 * change beneath it (a window moving under it, a repainted image) rebuilds it while a change out of its reach does not. */
enum { CW=320, CH=240 };
static void backdrop_cache_checks(void)
{
    static uint32_t base[CW*CH],app[CW*CH],bar[CW*CH],fresh[CW*CH],kept[CW*CH];
    for(unsigned i=0;i<CW*CH;++i){base[i]=0xff000000u|(i*2654435761u>>8);app[i]=0xff000000u|(i*40503u>>4);bar[i]=i%5 ? 0 : 0x60301008u;}
    PwWindowSnapshot s={.version=PW_WINDOW_SNAPSHOT_VERSION,.size=sizeof(s)};
    PwWindowRow root={.hwnd=1,.pid=1,.generation=1,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_DESKTOP,.right=CW,.bottom=CH};
    PwWindowRow taskbar={.hwnd=2,.pid=2,.generation=2,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_GLASS,.top=200,.right=CW,.bottom=CH,
                         .backdrop=PW_BACKDROP_TASKBAR,.solid_left=0,.solid_right=0,.solid_top=200,.solid_bottom=200};
    PwWindowRow flyout={.hwnd=3,.pid=3,.generation=3,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_GLASS,.left=200,.top=40,.right=300,.bottom=190,
                        .radius=8,.backdrop=PW_BACKDROP_FLYOUT,.solid_left=200,.solid_right=200,.solid_top=40,.solid_bottom=40};
    PwWindowRow window={.hwnd=4,.pid=4,.generation=4,.flags=PW_WINDOW_VISIBLE,.left=10,.top=10,.right=90,.bottom=70,.radius=8,.shadow=PW_SHADOW_WINDOW};
    PwWindowImage images[]={{.hwnd=1,.pid=1,.generation=1,.width=CW,.height=CH,.stride=CW*4,.pixels=base,.bytes=sizeof(base),.sequence=1},
        {.hwnd=2,.pid=2,.generation=2,.width=CW,.height=40,.stride=CW*4,.pixels=bar,.bytes=sizeof(bar),.flags=PW_WINDOW_PREMULTIPLIED,.sequence=1},
        {.hwnd=3,.pid=3,.generation=3,.width=100,.height=150,.stride=CW*4,.pixels=bar,.bytes=sizeof(bar),.flags=PW_WINDOW_PREMULTIPLIED,.sequence=1},
        {.hwnd=4,.pid=4,.generation=4,.width=80,.height=60,.stride=CW*4,.pixels=app,.bytes=sizeof(app),.sequence=1}};
    PwWindowEffects effects={0};
    #define SAME_AS_FRESH() do { PwWindowEffects once={0}; \
        assert(!pw_windows_compose_effects(fresh,CW,CH,&s,images,4,&once)); pw_window_effects_release(&once); \
        assert(!pw_windows_compose_effects(kept,CW,CH,&s,images,4,&effects)); assert(!memcmp(kept,fresh,sizeof(kept))); } while(0)
    s.count=4;s.windows[0]=flyout;s.windows[1]=taskbar;s.windows[2]=window;s.windows[3]=root;
    SAME_AS_FRESH();
    uint64_t builds=effects.backdrop_builds,hits=effects.backdrop_hits;
    SAME_AS_FRESH();
    assert(effects.backdrop_builds==builds && effects.backdrop_hits==hits+2);       /* both reused */
    for(int y=10;y<=180;y+=34) {                                                   /* a window dragged under both */
        window.top=y;window.bottom=y+60;window.left=y;window.right=y+80;s.windows[2]=window;
        SAME_AS_FRESH();
    }
    assert(effects.backdrop_builds>builds+4);
    builds=effects.backdrop_builds;
    images[3].sequence=2;app[0]^=0xffffff;SAME_AS_FRESH();                         /* the window repaints under them */
    assert(effects.backdrop_builds>builds);
    builds=effects.backdrop_builds;
    window.left=0;window.top=0;window.right=20;window.bottom=20;s.windows[2]=window;SAME_AS_FRESH();
    builds=effects.backdrop_builds;hits=effects.backdrop_hits;
    images[3].sequence=3;app[0]^=0xffffff;SAME_AS_FRESH();                         /* out of both blur areas */
    assert(effects.backdrop_hits==hits+2 && effects.backdrop_builds==builds);
    taskbar.material=PW_MATERIAL_DARK;s.windows[1]=taskbar;SAME_AS_FRESH();         /* theme */
    /* An activation changes no pixel beneath a glass window unless it turns a framed window's shadow. */
    builds=effects.backdrop_builds;hits=effects.backdrop_hits;
    flyout.flags|=PW_WINDOW_ACTIVE;s.windows[0]=flyout;root.material=PW_MATERIAL_DARK;s.windows[3]=root;SAME_AS_FRESH();
    assert(effects.backdrop_builds==builds && effects.backdrop_hits==hits+2);
    window.left=150;window.top=100;window.right=230;window.bottom=160;s.windows[2]=window;SAME_AS_FRESH();
    builds=effects.backdrop_builds;
    window.flags|=PW_WINDOW_ACTIVE;s.windows[2]=window;SAME_AS_FRESH();             /* its shadow, under the flyout */
    assert(effects.backdrop_builds>builds);
    #undef SAME_AS_FRESH
    pw_window_effects_release(&effects);
}
int main(void)
{
    /* Optimised blur equals the tap-by-tap definition, including clamped
     * edges, partial blocks and areas extending outside the source. */
    srand(7);
    for(unsigned i=0;i<W*H;++i)noise[i]=(uint32_t)rand()^((uint32_t)rand()<<16);
    static uint32_t a[W*H],b[W*H],ref[W*H];
    const long cases[][7]={{0,0,96,64,4,8,3},{-20,-9,61,50,8,6,3},{30,20,66,44,4,2,1},{90,60,30,30,3,5,2},{0,0,7,5,8,1,3},{5,5,40,30,4,0,0}};
    for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);++n) {
        const long *k=cases[n];
        assert(!pw_window_blur(a,b,noise,W,H,W*4,(int32_t)k[0],(int32_t)k[1],(uint32_t)k[2],(uint32_t)k[3],(unsigned)k[4],(unsigned)k[5],(unsigned)k[6]));
        reference_blur(ref,noise,W,H,k[0],k[1],k[2],k[3],k[4],k[5],k[6]);
        const long w=(k[2]+k[4]-1)/k[4],h=(k[3]+k[4]-1)/k[4];
        assert(!memcmp(a,ref,(size_t)w*h*4));
    }
    assert(pw_window_blur(a,b,noise,W,H,W*4,0,0,0,10,4,2,1)<0 && errno==EINVAL);
    /* Bilinear expansion against double arithmetic. */
    for(long y=-3;y<H+3;++y)for(long x=-3;x<W+3;++x) {
        const uint32_t sample=pw_window_blur_sample(noise,24,16,0,0,4,x,y);
        const double u=fmin(fmax((x+0.5)/4-0.5,0),23),v=fmin(fmax((y+0.5)/4-0.5,0),15);
        const long x0=(long)u,y0=(long)v,x1=x0<23?x0+1:x0,y1=y0<15?y0+1:y0;
        for(unsigned c=0;c<24;c+=8) {
            const double top=channel(noise[y0*24+x0],c)*(1-(u-x0))+channel(noise[y0*24+x1],c)*(u-x0);
            const double bottom=channel(noise[y1*24+x0],c)*(1-(u-x0))+channel(noise[y1*24+x1],c)*(u-x0);
            assert(fabs(channel(sample,c)-(top*(1-(v-y0))+bottom*(v-y0)))<=1.01);
        }
        assert(sample>>24==0xff);
    }
    for(uint32_t t=0;t<256;t+=17)assert(pw_window_tint(0x123456,0xfedcba,t)==reference_tint(0x123456,0xfedcba,t));
    /* The row expander the compositor uses is bit-identical to the per-pixel
     * definition, including starts left of the buffer, every scale and every recipe. */
    unsigned rows=0;
    for(unsigned recipe=PW_RECIPE_PLAIN;recipe<=PW_RECIPE_MICA_ALT_DARK;++recipe)
    for(unsigned scale=1;scale<=16;++scale)for(int left=-37;left<=37;left+=7)for(int y=-20;y<60;y+=7)for(int x0=-50;x0<60;x0+=13) {
        if(recipe!=PW_RECIPE_PLAIN && scale%3!=1)continue;
        static uint32_t line[300];
        pw_window_material_row(line,noise,24,16,left,5,scale,y,x0,300,0x123456,183,recipe);
        for(unsigned i=0;i<300;++i) {
            const uint32_t sample=pw_window_blur_sample(noise,24,16,left,5,scale,(int64_t)x0+i,y);
            uint32_t through=0;
            for(unsigned shift=0;shift<24;shift+=8)through|=pw_window_taskbar_transfer(channel(sample,shift))<<shift;
            assert(line[i]==(recipe==PW_RECIPE_TASKBAR_LIGHT ? pw_window_tint(through,0x123456,183) :
                             recipe==PW_RECIPE_TASKBAR_DARK ? pw_window_tint(pw_window_taskbar_saturate(sample),0x123456,183) :
                             recipe>=PW_RECIPE_MICA_ALT_LIGHT ? pw_window_mica_alt_pixel(sample,0x123456,183,recipe) :
                             pw_window_tint(sample,0x123456,183)));
        }
        ++rows;
    }
    assert(rows>20000);

    /* Glass over acrylic: uniform content beneath blurs to itself, so every
     * transparent glass pixel is exactly tint(content). Solid pixels stay the
     * window's own colour; premultiplied pixels blend once over the material. */
    for(unsigned i=0;i<W*H;++i) {root[i]=0xffc03020;glass[i]=0;}
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        if(x>=40 && x<50 && y>=20 && y<40)glass[y*W+x]=0x00123456;  /* solid area: any alpha */
        if(x>=60 && x<64 && y>=20 && y<40)glass[y*W+x]=0x80402010;  /* 50% premultiplied */
        if(x>=66 && x<70 && y>=20 && y<40)glass[y*W+x]=0xff00ff00;  /* opaque content */
    }
    PwWindowSnapshot s={.version=PW_WINDOW_SNAPSHOT_VERSION,.size=sizeof(s),.count=2};
    s.windows[0]=(PwWindowRow){.hwnd=2,.pid=2,.generation=2,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_GLASS,
        .left=10,.top=10,.right=80,.bottom=50,.client_left=10,.client_top=10,.client_right=80,.client_bottom=50,
        .backdrop=PW_BACKDROP_ACRYLIC,.material=PW_MATERIAL_LIGHT,.solid_left=40,.solid_top=20,.solid_right=50,.solid_bottom=40};
    s.windows[1]=(PwWindowRow){.hwnd=1,.pid=1,.generation=1,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_DESKTOP,.right=W,.bottom=H};
    PwWindowImage images[]={
        {.hwnd=1,.pid=1,.generation=1,.width=W,.height=H,.stride=W*4,.pixels=root,.bytes=sizeof(root),.sequence=1},
        {.hwnd=2,.pid=2,.generation=2,.width=W,.height=H,.stride=W*4,.offset_x=-10,.offset_y=-10,.pixels=glass,.bytes=sizeof(glass),.sequence=1}};
    /* Without a workspace the material is refused, never omitted. */
    out[0]=99;
    assert(pw_windows_compose(out,W,H,&s,images,2)<0 && errno==ENOMEM && out[0]==99);
    PwWindowEffects effects={0};
    for(unsigned variant=0;variant<2;++variant) {
        s.windows[0].material=variant;
        const uint32_t tint=variant ? pw_material_tints[PW_BACKDROP_ACRYLIC].dark : pw_material_tints[PW_BACKDROP_ACRYLIC].light;
        const uint32_t base=reference_tint(root[0],tint,pw_material_tints[PW_BACKDROP_ACRYLIC].tint);
        assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
        assert(out[15*W+15]==base && out[45*W+75]==base);
        assert(out[30*W+45]==0x00123456);                      /* solid copied verbatim */
        assert(out[30*W+68]==0xff00ff00);                      /* opaque glass content */
        uint32_t blended=0;
        for(unsigned c=0;c<32;c+=8) {
            const unsigned v=channel(0x80402010,c)+(channel(base,c)*(255-0x80)+127)/255;blended|=(v>255?255:v)<<c;
        }
        assert(out[30*W+62]==blended);
        assert(out[5*W+5]==root[0]);                            /* outside the window */
    }
    /* Acrylic samples the content beneath this frame: a lower window shows. */
    s.windows[0].material=PW_MATERIAL_LIGHT;
    for(unsigned i=0;i<W*H;++i)root[i]=(i%W)<48 ? 0xff0000ff : 0xffff0000;
    images[0].sequence=2;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    assert(channel(out[15*W+12],0)>channel(out[15*W+78],0) && channel(out[15*W+78],16)>channel(out[15*W+12],16));

    /* Mica uses the blurred desktop root, cached per root image sequence. */
    for(unsigned i=0;i<W*H;++i)root[i]=0xff3060a0;
    images[0].sequence=3;
    s.windows[0].backdrop=PW_BACKDROP_MICA;
    const uint64_t builds=effects.mica_builds;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    const uint32_t mica=reference_tint(root[0],pw_material_tints[PW_BACKDROP_MICA].light,pw_material_tints[PW_BACKDROP_MICA].tint);
    assert(out[15*W+15]==mica && effects.mica_builds==builds+1);
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && effects.mica_builds==builds+1);
    /* Mica ignores windows between the root and the glass: it is the wallpaper. */
    s.count=3;s.windows[2]=s.windows[1];
    s.windows[1]=(PwWindowRow){.hwnd=3,.pid=3,.generation=3,.flags=PW_WINDOW_VISIBLE,.right=W,.bottom=H};
    static uint32_t middle[W*H];for(unsigned i=0;i<W*H;++i)middle[i]=0xff00ff00;
    PwWindowImage three[]={images[0],images[1],{.hwnd=3,.pid=3,.generation=3,.width=W,.height=H,.stride=W*4,.pixels=middle,.bytes=sizeof(middle),.sequence=1}};
    assert(!pw_windows_compose_effects(out,W,H,&s,three,3,&effects) && out[15*W+15]==mica && out[5*W+5]==middle[0]);
    s.count=2;s.windows[1]=s.windows[2];
    for(unsigned i=0;i<W*H;++i)root[i]=0xff102030;
    images[0].sequence=4;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && effects.mica_builds==builds+2);
    assert(out[15*W+15]==reference_tint(root[0],pw_material_tints[PW_BACKDROP_MICA].light,pw_material_tints[PW_BACKDROP_MICA].tint));
    s.windows[0].backdrop=PW_BACKDROP_MICA_ALT;s.windows[0].material=PW_MATERIAL_DARK;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && effects.mica_alt_valid);
    /* Mica Alt goes through its own source and recipe, not Mica's tint. */
    assert(out[15*W+15]==pw_window_mica_alt_pixel(pw_window_blur_sample(effects.mica_alt,effects.mica_alt_width,
               effects.mica_alt_height,0,0,PW_MICA_SCALE,15,15),pw_material_tints[PW_BACKDROP_MICA_ALT].dark,
               pw_material_tints[PW_BACKDROP_MICA_ALT].tint_dark,PW_RECIPE_MICA_ALT_DARK));
    /* A framed window's Mica shows only while it is active: an inactive one is its solid caption colour, light and dark,
     * and becomes Mica again when it is activated (its backdrop is keyed on the flag); glass without a frame keeps it. */
    {
        const uint32_t active=out[15*W+15];
        s.windows[0].flags|=PW_WINDOW_ACTIVE;s.windows[0].shadow=PW_SHADOW_WINDOW;
        assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && out[15*W+15]==active);
        s.windows[0].flags&=~PW_WINDOW_ACTIVE;
        assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && out[15*W+15]==(0xff000000u|PW_MICA_INACTIVE_DARK));
        s.windows[0].material=PW_MATERIAL_LIGHT;
        assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && out[15*W+15]==(0xff000000u|PW_MICA_INACTIVE_LIGHT));
        s.windows[0].material=PW_MATERIAL_DARK;s.windows[0].flags|=PW_WINDOW_ACTIVE;
        assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && out[15*W+15]==active);
        s.windows[0].flags&=~PW_WINDOW_ACTIVE;s.windows[0].shadow=PW_SHADOW_NONE;
        assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects) && out[15*W+15]==active);
    }
    /* The taskbar's material: the prototype's light transfer, then 85% #f3f3f3; in dark saturate(1.25), then 97% #1c1c1c. */
    s.windows[0].backdrop=PW_BACKDROP_TASKBAR;s.windows[0].material=PW_MATERIAL_LIGHT;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    {
        uint32_t through=0xff000000u;
        for(unsigned shift=0;shift<24;shift+=8)through|=pw_window_taskbar_transfer(channel(root[0],shift))<<shift;
        assert(out[15*W+15]==reference_tint(through,0xf3f3f3,217));
        /* 0x102030 through contrast(0.15) brightness(1.95): 0.2925*v + 211.33, rounded */
        assert(through==(0xff000000u|(216u<<16)|(221u<<8)|225u));
    }
    s.windows[0].material=PW_MATERIAL_DARK;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    assert(out[15*W+15]==reference_tint(0xff000000u|pw_window_taskbar_saturate(root[0]),0x1c1c1c,247));
    /* saturate(1.25) of 0x102030: R 1.197*16-.179*32-.018*48, ... rounded */
    assert(pw_window_taskbar_saturate(0x102030)==((13u<<16)|(33u<<8)|53u));
    assert(pw_window_taskbar_transfer(0)==211 && pw_window_taskbar_transfer(128)==249 && pw_window_taskbar_transfer(255)==255);
    /* A flyout (Start, PW_BACKDROP_FLYOUT_BASE): the taskbar's transfer, 90% #f3f3f3 inside, none on the 1 px ring. */
    s.windows[0].backdrop=PW_BACKDROP_FLYOUT_BASE;s.windows[0].material=PW_MATERIAL_LIGHT;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    {
        uint32_t through=0xff000000u;
        for(unsigned shift=0;shift<24;shift+=8)through|=pw_window_taskbar_transfer(channel(root[0],shift))<<shift;
        assert(out[15*W+15]==reference_tint(through,0xf3f3f3,230));
        assert(out[10*W+30]==through && out[30*W+10]==through && out[49*W+30]==through && out[30*W+79]==through);
    }
    s.windows[0].backdrop=PW_BACKDROP_FLYOUT;s.windows[0].material=PW_MATERIAL_DARK;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    assert(out[15*W+15]==reference_tint(0xff000000u|pw_window_taskbar_saturate(root[0]),0x2c2c2c,245));
    assert(out[10*W+30]==(0xff000000u|pw_window_taskbar_saturate(root[0])));
    s.windows[0].backdrop=PW_BACKDROP_MICA_ALT;
    /* Without a retained root, Mica is its tint colour alone. */
    assert(!pw_windows_compose_effects(out,W,H,&s,images+1,1,&effects));
    assert(out[15*W+15]==(0xff000000u|pw_material_tints[PW_BACKDROP_MICA_ALT].dark));
    /* Rounded glass: corners reveal the background, interior the material. */
    s.windows[0].radius=8;s.windows[0].backdrop=PW_BACKDROP_MICA;s.windows[0].material=PW_MATERIAL_LIGHT;
    assert(!pw_windows_compose_effects(out,W,H,&s,images,2,&effects));
    assert(out[10*W+10]==root[0] && out[15*W+15]!=root[0]);
    /* The workspace limit is enforced, not exceeded. */
    PwWindowEffects tight={0};tight.bytes=PW_WINDOW_EFFECTS_LIMIT-16;
    images[0].sequence=5;
    assert(pw_windows_compose_effects(out,W,H,&s,images,2,&tight)<0 && errno==ENOSPC);
    pw_window_effects_release(&tight);
    pw_window_effects_release(&effects);
    mica_alt_checks();
    backdrop_cache_checks();
    puts("window materials: PASS backdrop cache equal to building afresh (reused, dragged under, repainted, out of reach, theme), blur vs tap reference (6 cases), bilinear vs double, tint, material rows bit-identical (every recipe), acrylic/Mica/Mica Alt light+dark, Mica Alt source and luminosity vs double, solid/premultiplied glass, root cache, rounded glass, limits");
}
