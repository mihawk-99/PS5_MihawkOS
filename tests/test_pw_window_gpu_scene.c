/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_window_scene.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    PwWindowScene scene={0};PwWindowSnapshot snap={.version=PW_WINDOW_SNAPSHOT_VERSION,.size=sizeof(snap),.count=1,.clip_count=1};
    snap.clips[0]=(PwWindowClip){0,0,2,2};
    snap.windows[0]=(PwWindowRow){.hwnd=2,.pid=22,.generation=2,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_CLIENT,
        .right=2,.bottom=2,.client_right=2,.client_bottom=2,.clip_count=1};
    uint32_t input[4]={0xff112233u,0xff112233u,0xff112233u,0xff112233u},out[4];
    /* Another process presents into the window (Chromium's GPU process into the browser's window): the image is
     * the owner's, and goes when its presenter does. */
    assert(pw_window_scene_gpu(&scene,11,9,2,input,2,2,8,1,&snap)<0 && errno==ESTALE && !scene.pixel_bytes);
    assert(!pw_window_scene_gpu(&scene,11,2,2,input,2,2,8,1,&snap) && scene.pixel_bytes==16);
    {
        PwWindowImage cross[PW_WINDOW_IMAGE_LIMIT];
        assert(pw_window_scene_images(&scene,cross,PW_WINDOW_IMAGE_LIMIT)==1 && cross[0].pid==22);
        assert(!pw_windows_compose(out,2,2,&snap,cross,1) && out[0]==0xff112233u);
    }
    pw_window_scene_detach(&scene,11);assert(!scene.pixel_bytes);
    assert(!pw_window_scene_gpu(&scene,22,2,2,input,2,2,8,1,&snap) && scene.pixel_bytes==16);
    input[0]=99;
    PwWindowImage images[PW_WINDOW_IMAGE_LIMIT];size_t n=pw_window_scene_images(&scene,images,PW_WINDOW_IMAGE_LIMIT);
    assert(n==1 && images[0].plane==PW_WINDOW_GPU && images[0].pixels[0]==0xff112233u);
    assert(!pw_windows_compose(out,2,2,&snap,images,n) && out[0]==0xff112233u);
    const uint32_t *retained=images[0].pixels;
    input[0]=0xff112233u;
    assert(!pw_window_scene_gpu(&scene,22,2,2,input,2,2,8,0,&snap) && scene.pixel_bytes==16);
    n=pw_window_scene_images(&scene,images,PW_WINDOW_IMAGE_LIMIT);
    assert(n==1 && images[0].pixels==retained && images[0].pixels[0]==0xff332211u);
    assert(pw_window_scene_gpu(&scene,22,2,3,input,2,2,8,1,&snap)<0 && errno==ESTALE && scene.pixel_bytes==16);
    assert(pw_window_scene_gpu(&scene,22,2,2,input,2,2,7,1,&snap)<0 && errno==EINVAL);
    assert(pw_window_scene_gpu(&scene,22,2,2,input,2,2,8,2,&snap)<0 && errno==EINVAL);
    assert(pw_window_scene_gpu(&scene,22,2,2,(const char *)input+1,2,2,8,1,&snap)<0 && errno==EINVAL);
    snap.clip_count=PW_WINDOW_CLIP_LIMIT+1;
    assert(pw_window_scene_gpu(&scene,22,2,2,input,2,2,8,1,&snap)<0 && errno==EPROTO);
    snap.clip_count=1;
    scene.pixel_bytes=128u<<20;
    assert(pw_window_scene_gpu(&scene,22,2,2,input,1,1,4,1,&snap)<0 && errno==ENOSPC);
    scene.pixel_bytes=16;
    snap.windows[0].flags=PW_WINDOW_CLIENT;assert(!pw_window_scene_update(&scene,&snap) && scene.pixel_bytes==16);
    assert(!pw_windows_compose(out,2,2,&snap,images,n) && out[0]==0);
    snap.windows[0].generation=3;assert(pw_window_scene_update(&scene,&snap)==1 && !scene.pixel_bytes);
    assert(!pw_window_scene_gpu(&scene,22,2,3,input,2,2,8,1,&snap));
    pw_window_scene_detach(&scene,22);assert(!scene.pixel_bytes && !scene.mapped_bytes);
    {
        /* Steam's restored window: a client area of 1922x1081 at (-1,-1), shown clipped to the screen. */
        enum {W=1922,H=1081};
        uint32_t *big=malloc((size_t)W*H*4),*screen=malloc((size_t)1920*1080*4);assert(big && screen);
        for(size_t i=0;i<(size_t)W*H;++i)big[i]=0xff000000u|(uint32_t)i;
        PwWindowSnapshot wide=snap;
        wide.clips[0]=(PwWindowClip){0,0,1920,1080};
        wide.windows[0]=(PwWindowRow){.hwnd=5,.pid=33,.generation=5,.flags=PW_WINDOW_VISIBLE|PW_WINDOW_CLIENT,
            .left=-1,.top=-1,.right=1921,.bottom=1080,.client_left=-1,.client_top=-1,.client_right=1921,.client_bottom=1080,
            .clip_count=1};
        assert(pw_window_scene_gpu(&scene,33,5,5,big,PW_WINDOW_WIDTH+1,H,(PW_WINDOW_WIDTH+1)*4,1,&wide)<0 && errno==EINVAL);
        assert(pw_window_scene_gpu(&scene,33,5,5,big,W,H,PW_WINDOW_WIDTH*4+4,1,&wide)<0 && errno==EINVAL);
        assert(!pw_window_scene_gpu(&scene,33,5,5,big,W,H,W*4,1,&wide));
        n=pw_window_scene_images(&scene,images,PW_WINDOW_IMAGE_LIMIT);
        assert(n==1 && images[0].width==W && images[0].height==H);
        assert(!pw_windows_compose(screen,1920,1080,&wide,images,n));
        assert(screen[0]==big[W+1] && screen[(size_t)1079*1920+1919]==big[(size_t)1080*W+1920]);
        pw_window_scene_detach(&scene,33);assert(!scene.pixel_bytes);
        free(big);free(screen);
    }
    pw_window_scene_release(&scene);
    puts("GPU scene: PASS cross-process presenter, ownership, retained copy, format, reuse, failed replacement, hidden content, lifetime, detach, a window larger than the screen");
}
