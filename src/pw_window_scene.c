/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_window_scene.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
enum { PW_WINDOW_SCENE_MEMORY_LIMIT=128u<<20 };
static int valid_snapshot(const PwWindowSnapshot *snapshot)
{
    if(!snapshot || snapshot->version!=PW_WINDOW_SNAPSHOT_VERSION || snapshot->size!=sizeof(*snapshot) ||
       snapshot->count>PW_WINDOW_LIMIT || snapshot->clip_count>PW_WINDOW_CLIP_LIMIT) {errno=EPROTO;return 0;}
    if(snapshot->overflow) {errno=EOVERFLOW;return 0;}
    return 1;
}
static const PwWindowRow *lifetime(const PwWindowSnapshot *snapshot,int pid,uint32_t hwnd,uint64_t generation)
{
    for(uint32_t i=0;i<snapshot->count;++i) {
        const PwWindowRow *r=&snapshot->windows[i];
        if(r->pid==pid && r->hwnd==hwnd && r->generation==generation)return r;
    }
    return NULL;
}
/* A window by its lifetime alone: generations are global and never reused (server window.c). */
static const PwWindowRow *window_of(const PwWindowSnapshot *snapshot,uint32_t hwnd,uint64_t generation)
{
    for(uint32_t i=0;i<snapshot->count;++i) {
        const PwWindowRow *r=&snapshot->windows[i];
        if(r->hwnd==hwnd && r->generation==generation)return r;
    }
    return NULL;
}
static void release_surface(PwWindowScene *scene,PwWindowSurface *surface)
{
    if(surface->page) {munmap(surface->page,surface->header.bytes);scene->mapped_bytes-=surface->header.bytes;}
    if(surface->pixels) {free(surface->pixels);scene->pixel_bytes-=(size_t)surface->header.stride*surface->header.height;}
    *surface=(PwWindowSurface){0};
}
int pw_window_scene_attach(PwWindowScene *scene,int32_t pid,int fd,size_t bytes,const PwWindowSnapshot *snapshot)
{
    int status=-1;PwWindowPage *page=MAP_FAILED;uint32_t *pixels=NULL;
    PwWindowPage header;struct stat st;
    const size_t max=pw_window_page_bytes(PW_WINDOW_WIDTH,PW_WINDOW_HEIGHT);
    if(!scene || pid<=0 || fd<0 || !bytes || bytes>max || (bytes&(PW_WINDOW_PAGE_ALIGN-1))) {errno=EINVAL;goto done;}
    if(!valid_snapshot(snapshot))goto done;
    /* Firmware 10.01 physical-contract stat-size passes (the same run explicitly
     * rejects byte-I/O/private-map operations). Verify extent before any read. */
    if(fstat(fd,&st))goto done;
    if(st.st_size<0 || (uint64_t)st.st_size!=bytes) {errno=EPROTO;goto done;}
    if(bytes>PW_WINDOW_SCENE_MEMORY_LIMIT-scene->mapped_bytes) {errno=ENOSPC;goto done;}
    page=mmap(NULL,bytes,PROT_READ,MAP_SHARED,fd,0);if(page==MAP_FAILED)goto done;
    if(pw_window_page_header(page,bytes,&header))goto done;
    if(!lifetime(snapshot,pid,header.hwnd,header.generation)) {errno=ESTALE;goto done;}
    PwWindowSurface *slot=NULL,*empty=NULL;
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        PwWindowSurface *s=&scene->surfaces[n];
        if(s->page && s->pid==pid && s->header.hwnd==header.hwnd) {slot=s;break;}
        if(!s->page && !empty)empty=s;
    }
    if(!slot)slot=empty;
    const size_t pixel_bytes=(size_t)header.stride*header.height;
    if(!slot || pixel_bytes>PW_WINDOW_SCENE_MEMORY_LIMIT-scene->pixel_bytes) {errno=ENOSPC;goto done;}
    pixels=malloc(pixel_bytes);if(!pixels)goto done;
    /* Nothing can fail after retiring the old title-side surface. */
    release_surface(scene,slot);
    *slot=(PwWindowSurface){.pid=pid,.page=page,.header=header,.pixels=pixels};
    scene->mapped_bytes+=bytes;scene->pixel_bytes+=pixel_bytes;
    page=MAP_FAILED;pixels=NULL;status=0;
done:
    {
        int saved=errno;if(page!=MAP_FAILED)munmap(page,bytes);free(pixels);if(fd>=0)close(fd);errno=saved;
    }
    return status;
}
int pw_window_scene_update(PwWindowScene *scene,const PwWindowSnapshot *snapshot)
{
    if(!scene || !valid_snapshot(snapshot))return -1;
    int changes=0;
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        PwWindowSurface *s=&scene->gpu[n];
        if(s->pixels && !lifetime(snapshot,s->pid,s->header.hwnd,s->header.generation))
            {release_surface(scene,s);++changes;}
    }
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        PwWindowSurface *s=&scene->surfaces[n];if(!s->page)continue;
        if(!lifetime(snapshot,s->pid,s->header.hwnd,s->header.generation)) {release_surface(scene,s);++changes;continue;}
        const size_t bytes=(size_t)s->header.stride*s->header.height;
        if(bytes>scene->scratch_bytes) {
            void *scratch=realloc(scene->scratch,bytes);if(!scratch)return -1;
            scene->scratch=scratch;scene->scratch_bytes=bytes;
        }
        uint32_t sequence=0;
        int result=pw_window_page_read(s->page,&s->header,scene->scratch,scene->scratch_bytes,s->shown,&sequence);
        if(result<0)return -1;
        if(result) {memcpy(s->pixels,scene->scratch,bytes);s->shown=sequence;++changes;}
    }
    return changes;
}
size_t pw_window_scene_images(const PwWindowScene *scene,PwWindowImage *images,size_t capacity)
{
    if(!scene || !images || capacity<PW_WINDOW_IMAGE_LIMIT) {errno=EINVAL;return 0;}
    size_t count=0;
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        const PwWindowSurface *s=&scene->surfaces[n];if(!s->page || !s->shown)continue;
        if(count==capacity) {errno=ENOSPC;return 0;}
        images[count++]=(PwWindowImage){.hwnd=s->header.hwnd,.pid=s->pid,.generation=s->header.generation,
            .width=s->header.width,.height=s->header.height,.stride=s->header.stride,
            .offset_x=s->header.offset_x,.offset_y=s->header.offset_y,.pixels=s->pixels,
            .bytes=(size_t)s->header.stride*s->header.height,
            .flags=s->header.flags&PW_WINDOW_PAGE_PREMULTIPLIED ? PW_WINDOW_PREMULTIPLIED : 0,.sequence=s->shown};
    }
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        const PwWindowSurface *s=&scene->gpu[n];if(!s->pixels)continue;
        images[count++]=(PwWindowImage){.hwnd=s->header.hwnd,.pid=s->pid,.generation=s->header.generation,
            .width=s->header.width,.height=s->header.height,.stride=s->header.stride,
            .plane=PW_WINDOW_GPU,.pixels=s->pixels,.bytes=(size_t)s->header.stride*s->header.height,.sequence=s->shown};
    }
    return count;
}
void pw_window_scene_detach(PwWindowScene *scene,int32_t pid)
{
    if(!scene || pid<=0) {errno=EINVAL;return;}
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        if(scene->surfaces[n].page && scene->surfaces[n].pid==pid)release_surface(scene,&scene->surfaces[n]);
        if(scene->gpu[n].pixels && (scene->gpu[n].pid==pid || scene->gpu[n].presenter==pid))
            release_surface(scene,&scene->gpu[n]);
    }
}
void pw_window_scene_release(PwWindowScene *scene)
{
    if(!scene)return;
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {release_surface(scene,&scene->surfaces[n]);release_surface(scene,&scene->gpu[n]);}
    free(scene->scratch);*scene=(PwWindowScene){0};
}

int pw_window_scene_gpu(PwWindowScene *scene,int32_t presenter,uint32_t hwnd,uint64_t generation,
                        const void *pixels,uint32_t width,uint32_t height,uint32_t stride,
                        unsigned bgra,const PwWindowSnapshot *snapshot)
{
    /* A window may be larger than the screen (Steam's restored window is 1922x1081): the compositor clips it. */
    if(!scene || presenter<=0 || !hwnd || !generation || !pixels || ((uintptr_t)pixels&3) || !width ||
       width>PW_WINDOW_WIDTH || !height || height>PW_WINDOW_HEIGHT || stride<width*4 || stride>PW_WINDOW_WIDTH*4 ||
       stride%4 || bgra>1)
        {errno=EINVAL;return -1;}
    if(!valid_snapshot(snapshot))return -1;
    const PwWindowRow *row=window_of(snapshot,hwnd,generation);
    if(!row || row->pid<=0 || !(row->flags&PW_WINDOW_CLIENT)) {errno=ESTALE;return -1;}
    const int32_t pid=row->pid;
    PwWindowSurface *slot=NULL,*empty=NULL;
    for(unsigned n=0;n<PW_WINDOW_LIMIT;++n) {
        PwWindowSurface *s=&scene->gpu[n];
        if(s->pixels && s->pid==pid && s->header.hwnd==hwnd) {slot=s;break;}
        if(!s->pixels && !empty)empty=s;
    }
    if(!slot)slot=empty;
    const size_t bytes=(size_t)width*height*4;
    if(!slot) {errno=ENOSPC;return -1;}
    const int reuse=slot->pixels && slot->header.width==width && slot->header.height==height;
    if(!reuse && bytes>PW_WINDOW_SCENE_MEMORY_LIMIT-scene->pixel_bytes) {errno=ENOSPC;return -1;}
    uint32_t *copy=reuse ? slot->pixels : malloc(bytes);if(!copy)return -1;
    for(uint32_t y=0;y<height;++y) {
        const uint32_t *source=(const uint32_t *)((const char *)pixels+(size_t)y*stride);
        uint32_t *dest=copy+(size_t)y*width;
        if(bgra)memcpy(dest,source,(size_t)width*4);
        else for(uint32_t x=0;x<width;++x) {
            const uint32_t value=source[x];
            dest[x]=(value&0xff00ff00u)|((value&0xffu)<<16)|((value>>16)&0xffu);
        }
    }
    /* Each copy is a new image even when its buffer is reused: effect caches key on it. */
    uint32_t shown=reuse ? slot->shown+1 : 1;
    if(!shown)shown=1;
    if(!reuse)release_surface(scene,slot);
    *slot=(PwWindowSurface){.pid=pid,.presenter=presenter,.pixels=copy,.shown=shown,
        .header={.hwnd=hwnd,.generation=generation,.width=width,.height=height,.stride=width*4}};
    if(!reuse)scene->pixel_bytes+=bytes;
    return 0;
}
