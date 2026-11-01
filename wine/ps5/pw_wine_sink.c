/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_sink.h"
#include "pw_wine_window.h"
#include "pw_wine_pages.h"
#include "pw_wine_process.h"
#include "../../src/pw_frame_page.h"
#include "../../src/pw_audio_ring.h"
#include "../../src/pw_suspend.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static PwWinePresentSink sink;
static void *sink_context;
static PwWineDisplayRelease display_release;
static void *display_context;
static PwWineInput queue[PW_WINE_INPUT_QUEUE];
static uint32_t head,count;
static PwWineSinkStats stats;
static pthread_once_t wake_once=PTHREAD_ONCE_INIT;
static int wake[2]={-1,-1};
static PwWinePad pad_state;
static uint32_t pad_changes;      /* times the pad connected or disconnected (lock) */
static int pad_seen=-1;           /* a helper: whether it saw the pad connected at its last look; -1 before the first */
/* The title's pad, for its Windows child processes (docs/GRAPHICS_PROCESS_BROKER.md). A child has a sink of its own,
 * which no title sets: each child makes an input page (src/pw_shared_page.h) and gives it to the title, whose sink
 * keeps a copy of its pad in every page it adopted (pw_wine_input_page_adopt); a child's sink reads the pad from its
 * own page (WINE_PS5_INPUT_FD). The sequence is odd while the page is written, so a read is whole. */
_Static_assert(PW_WINE_INPUT_PAGE_BYTES == 16384, "an input page is one console page");
/* The page also carries the DualSense's USB report, which winebus's PS5 bus reads (pw_wine_dualsense): a game in a
 * helper that finds the pad through raw HID, not XInput (a .NET (MonoGame) game's SDL with SDL_XINPUT_ENABLED=0), sees it. */
typedef struct PwWineInputPage { uint32_t sequence, reserved; PwWinePad pad; uint32_t dualsense_connected;
                                 uint8_t dualsense[64]; } PwWineInputPage;
_Static_assert(sizeof(PwWineInputPage) <= PW_SUSPEND_PAGE_OFFSET, "the title's suspend word is clear of the pad");
static PwWineInputPage *input_page;   /* a child's own page, read here */
static int input_page_tried,pad_set;
/* The title: the pages its helpers gave it, written whenever the pad or the DualSense report changes. */
#define PW_WINE_CHILD_PAGES 32
static PwWineInputPage *child_pages[PW_WINE_CHILD_PAGES];
static uint8_t dualsense_report[64];
static int dualsense_connected,dualsense_set;
static uint32_t rumble_left,rumble_right,rumble_changed;
/* Audio has its own lock: its sink blocks for a grain, which must not hold
 * up frames or input. */
static pthread_mutex_t audio_lock=PTHREAD_MUTEX_INITIALIZER;
static PwWineAudioSink audio_sink;
static void *audio_context;
static uint64_t grains,grains_dropped;
/* A helper's audio ring, from the loader (WINE_PS5_AUDIO_FD), mapped once; and the title's, one per helper, which its
 * mixer reads under ring_lock, so a helper's ring is never unmapped under it. */
_Static_assert((int)PW_AUDIO_RING_GRAIN==(int)PW_WINE_AUDIO_GRAIN,"a ring's grain is the sink's");
#define PW_WINE_AUDIO_RINGS 32
/* A ring is one 16 KiB console page, as the input page is (PW_WINE_AUDIO_RING_BYTES); the ring's 8,224 bytes fit. */
static PwAudioRing *child_ring;
static int child_ring_tried;
static pthread_mutex_t ring_lock=PTHREAD_MUTEX_INITIALIZER;
static PwAudioRing *rings[PW_WINE_AUDIO_RINGS];
static pthread_once_t mixer_once=PTHREAD_ONCE_INIT;
/* The title's own process, while helpers play: its grains go into this ring, read by the mixer like theirs. */
static PwAudioRing local_ring;
static int local_ring_ready,mixer_in_flight;   /* in_flight: ring_lock */

static void create_wake_pipe(void)
{
    int pair[2];
    if(pipe(pair))return;
    for(int i=0;i<2;i++) {
        fcntl(pair[i],F_SETFL,fcntl(pair[i],F_GETFL)|O_NONBLOCK);
        fcntl(pair[i],F_SETFD,FD_CLOEXEC);
    }
    wake[0]=pair[0];wake[1]=pair[1];
}
int pw_wine_input_fd(void)
{
    pthread_once(&wake_once,create_wake_pipe);
    return wake[0];
}

void pw_wine_set_present_sink(PwWinePresentSink new_sink,void *context)
{
    pthread_mutex_lock(&lock);
    sink=new_sink;sink_context=context;
    pthread_mutex_unlock(&lock);
}
void pw_wine_set_display_release(PwWineDisplayRelease release,void *context)
{
    pthread_mutex_lock(&lock);
    display_release=release;display_context=context;
    pthread_mutex_unlock(&lock);
}
int pw_wine_release_display(void)
{
    int status=0;
    /* Under the frame lock: no frame is being shown while the title lets go. */
    pthread_mutex_lock(&lock);
    if(!stats.display_released && display_release)status=display_release(display_context);
    if(!status)stats.display_released=1;
    pthread_mutex_unlock(&lock);
    return status;
}
/* A helper's frames go to the title through a shared page (src/pw_frame_page.h), made on the first frame. The
 * process module (pw_wine_process.c) is linked beside this one in ntdll; a host test has none. */
int pw_ps5_child_frame(int fd) __attribute__((weak));
static PwFramePage *frame_page;
static int frame_page_tried;
static int (*frame_allocator)(size_t);
/* The console's shared-page range (pw_wine_pages_ps5.c, linked in ntdll); a host build has none. */
int pw_wine_pages_reserve(void) __attribute__((weak));
void pw_wine_set_frame_allocator(int (*alloc)(size_t))
{
    /* Called by a helper's loader before Wine starts: the range is taken before any program can fill the space. */
    if(alloc && pw_wine_pages_reserve)(void)pw_wine_pages_reserve();
    pthread_mutex_lock(&lock);
    frame_allocator=alloc;
    pthread_mutex_unlock(&lock);
    pw_wine_window_set_allocator(alloc);
    static int window_release_registered;
    if(alloc && !window_release_registered) {
        if(!pw_wine_add_exit_hook(pw_wine_window_release))window_release_registered=1;
        else fprintf(stderr,"wine-ps5: no window release hook (errno %d)\n",errno);
    }
}
static int32_t frame_x,frame_y;
void pw_wine_present_origin(int32_t x,int32_t y)
{
    pthread_mutex_lock(&lock);
    frame_x=x;frame_y=y;
    pthread_mutex_unlock(&lock);
}
static int child_present_locked(const void *bgra,uint32_t width,uint32_t height,uint32_t stride)
{
    if(!frame_page && !frame_page_tried) {
        frame_page_tried=1;
        int fd=-1;
        if(frame_allocator)fd=frame_allocator(PW_FRAME_BYTES);
        else {
#ifdef SHM_ANON
        fd=shm_open(SHM_ANON,O_RDWR,0600);
#else
        /* A host build (the tests): a named object, unlinked at once. */
        char name[48];snprintf(name,sizeof(name),"/pw-wine-frame-%d",(int)getpid());
        fd=shm_open(name,O_RDWR|O_CREAT|O_EXCL,0600);
        if(fd>=0)shm_unlink(name);
#endif
        if(fd>=0 && ftruncate(fd,PW_FRAME_BYTES)) { close(fd);fd=-1; }
        }
        void *page=MAP_FAILED;
        if(fd>=0)
            page=pw_wine_pages_map(PW_FRAME_BYTES,PROT_READ|PROT_WRITE,fd);
        if(page!=MAP_FAILED) {
            ((PwFramePage *)page)->magic=PW_FRAME_MAGIC;
            if(pw_ps5_child_frame && !pw_ps5_child_frame(fd))frame_page=page;
            else pw_wine_pages_unmap(page,PW_FRAME_BYTES);
        }
        if(fd>=0)close(fd);
        fprintf(stderr,"wine-ps5: frames to the title: %s\n",frame_page ? "shared" : "unavailable");
    }
    if(!frame_page)return -1;
    const uint32_t shown_width=width<PW_FRAME_WIDTH ? width : PW_FRAME_WIDTH;
    const uint32_t shown_height=height<PW_FRAME_HEIGHT ? height : PW_FRAME_HEIGHT;
    const uint32_t sequence=__atomic_load_n(&frame_page->sequence,__ATOMIC_RELAXED);
    __atomic_store_n(&frame_page->sequence,sequence|1,__ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    uint8_t *pixels=pw_frame_pixels(frame_page);
    for(uint32_t y=0;y<shown_height;++y)
        memcpy(pixels+(size_t)y*shown_width*4,(const uint8_t *)bgra+(size_t)y*stride,(size_t)shown_width*4);
    frame_page->width=shown_width;frame_page->height=shown_height;frame_page->stride=shown_width*4;
    frame_page->x=frame_x;frame_page->y=frame_y;frame_page->frames++;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    frame_page->time_ns=(uint64_t)now.tv_sec*1000000000u+(uint64_t)now.tv_nsec;
    __atomic_store_n(&frame_page->sequence,(sequence|1)+1,__ATOMIC_RELEASE);
    return 0;
}
int pw_wine_present(const void *bgra,uint32_t width,uint32_t height,uint32_t stride)
{
    int status=-1;
    /* The lock also serialises frames: the sink sees one at a time. */
    pthread_mutex_lock(&lock);
    if(sink && bgra && width && height && stride>=width*4u)
        status=sink(sink_context,bgra,width,height,stride);
    else if(!sink && bgra && width && height && stride>=width*4u && getenv("WINE_PS5_CHILD"))
        status=child_present_locked(bgra,width,height,stride);
    if(status)stats.frames_dropped++;
    else stats.frames++;
    pthread_mutex_unlock(&lock);
    return status;
}

static int valid(const PwWineInput *e)
{
    switch(e->type) {
    case PW_WINE_INPUT_KEY:return (e->code&0xff) && e->code<=(PW_WINE_KEY_EXTENDED|0xffff) && e->down<=1;
    case PW_WINE_INPUT_MOUSE_MOVE:case PW_WINE_INPUT_MOUSE_WHEEL:return 1;
    case PW_WINE_INPUT_MOUSE_BUTTON:return e->code<=4 && e->down<=1;
    case PW_WINE_INPUT_CLOSE:case PW_WINE_INPUT_TERMINATE:return 1;
    case PW_WINE_INPUT_END_PROCESS:return e->code>0 && e->down<=1;
    case PW_WINE_INPUT_CHAR:return e->code>0 && e->code<=0xffff && e->down<=1;
    default:return 0;
    }
}
/* A full pipe is already readable: nothing is lost. */
static void wake_driver(void)
{
    char byte=1;
    pthread_once(&wake_once,create_wake_pipe);
    if(wake[1]!=-1 && write(wake[1],&byte,1)<0){}
}
int pw_wine_post_input(const PwWineInput *event)
{
    int status=-1;
    pthread_mutex_lock(&lock);
    if(event && valid(event) && count<PW_WINE_INPUT_QUEUE) {
        queue[(head+count)%PW_WINE_INPUT_QUEUE]=*event;count++;
        stats.inputs_posted++;status=0;
    }
    else stats.inputs_dropped++;
    pthread_mutex_unlock(&lock);
    if(!status)wake_driver();
    return status;
}
int pw_wine_next_input(PwWineInput *event)
{
    int found=0;
    if(!event)return 0;
    pthread_mutex_lock(&lock);
    if(count) {
        *event=queue[head];head=(head+1)%PW_WINE_INPUT_QUEUE;count--;
        stats.inputs_delivered++;found=1;
    }
    pthread_mutex_unlock(&lock);
    return found;
}
void pw_wine_set_audio_sink(PwWineAudioSink new_sink,void *context)
{
    pthread_mutex_lock(&audio_lock);
    audio_sink=new_sink;audio_context=context;
    pthread_mutex_unlock(&audio_lock);
}
/* In a helper: the ring the loader was given, mapped once it is asked for. */
static void child_ring_attach_locked(void)
{
    const char *value=getenv("WINE_PS5_AUDIO_FD");char *end;long fd;void *ring;
    if(child_ring || child_ring_tried || !value || !*value)return;
    child_ring_tried=1;
    fd=strtol(value,&end,10);
    if(*end || fd<0 || fd>INT_MAX)return;
    ring=pw_wine_pages_map(PW_WINE_AUDIO_RING_BYTES,PROT_READ|PROT_WRITE,(int)fd);
    if(ring==MAP_FAILED)return;
    if(pw_audio_ring_check(ring,PW_WINE_AUDIO_RING_BYTES)==PW_OK)child_ring=ring;
    else pw_wine_pages_unmap(ring,PW_WINE_AUDIO_RING_BYTES);
}
static uint64_t ring_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000;
}
static void ring_wait(void)
{
    const struct timespec millisecond={0,1000000};
    nanosleep(&millisecond,NULL);
}
int pw_wine_audio_available(void)
{
    pthread_mutex_lock(&audio_lock);
    if(!audio_sink)child_ring_attach_locked();
    int available=audio_sink!=NULL || child_ring!=NULL;
    pthread_mutex_unlock(&audio_lock);
    return available;
}
/* One grain to the sink, or to the helper's ring; audio_lock held. */
static int audio_write_locked(const int16_t *frames)
{
    int status=-1;
    if(!audio_sink)child_ring_attach_locked();
    if(audio_sink && frames)status=audio_sink(audio_context,frames);
    else if(child_ring && frames)status=pw_audio_ring_write(child_ring,frames,100,ring_now_ms,ring_wait)==PW_OK ? 0 : -1;
    if(status)grains_dropped++;
    else grains++;
    return status;
}
/* The title's own process plays through the mixer's ring while any helper has one, or grains of it still wait or are
 * on their way to the port, so the mixer is the only writer to the port: two writers, each blocked a grain, would
 * alternate and halve each other's rate (a launcher that starts the game as a helper and stays alive for seconds).
 * This takes ring_lock, not audio_lock: the mixer holds that for a grain and takes it again at once, and a thread
 * waiting for it would starve. */
static int audio_via_mixer(void)
{
    int via=0;
    pthread_mutex_lock(&ring_lock);
    if(local_ring_ready) {
        via=mixer_in_flight || pw_audio_ring_fill(&local_ring)>0;
        for(unsigned i=0;i<PW_WINE_AUDIO_RINGS && !via;i++)via=rings[i]!=NULL;
    }
    pthread_mutex_unlock(&ring_lock);
    return via;
}
int pw_wine_audio_output(const int16_t *frames)
{
    int status;
    if(frames && audio_sink && audio_via_mixer()) {
        /* One writer: the process's own mixer thread. The wait is the port's clock, through the mixer. */
        status=pw_audio_ring_write(&local_ring,frames,100,ring_now_ms,ring_wait)==PW_OK ? 0 : -1;
        if(status)__atomic_fetch_add(&grains_dropped,1,__ATOMIC_RELAXED);
        return status;
    }
    /* The lock also serialises grains: the sink, or the ring, sees one at a time. */
    pthread_mutex_lock(&audio_lock);
    status=audio_write_locked(frames);
    pthread_mutex_unlock(&audio_lock);
    return status;
}
/* The title's mixer: a grain from each helper's ring that has one, summed and clamped, played through the sink, which
 * blocks for the grain, so the port is the clock. With no grain waiting it sleeps 2 ms: it holds the port only while
 * a helper plays. */
static void *audio_mixer(void *unused)
{
    static int16_t grain[PW_AUDIO_RING_GRAIN*2],mixed[PW_AUDIO_RING_GRAIN*2];
    static int32_t sum[PW_AUDIO_RING_GRAIN*2];
    int failed;
    (void)unused;
    for(;;) {
        int any=0;
        memset(sum,0,sizeof(sum));
        pthread_mutex_lock(&ring_lock);
        for(unsigned i=0;i<=PW_WINE_AUDIO_RINGS;i++) {
            PwAudioRing *ring=i<PW_WINE_AUDIO_RINGS ? rings[i] : local_ring_ready ? &local_ring : NULL;
            if(!ring || pw_audio_ring_read(ring,grain)!=1)continue;
            any=1;
            for(unsigned s=0;s<PW_AUDIO_RING_GRAIN*2;s++)sum[s]+=grain[s];
        }
        mixer_in_flight=any;
        pthread_mutex_unlock(&ring_lock);
        if(!any) { const struct timespec pause={0,2000000};nanosleep(&pause,NULL);continue; }
        for(unsigned s=0;s<PW_AUDIO_RING_GRAIN*2;s++)
            mixed[s]=(int16_t)(sum[s]>32767 ? 32767 : sum[s]<-32768 ? -32768 : sum[s]);
        pthread_mutex_lock(&audio_lock);
        failed=audio_write_locked(mixed);
        pthread_mutex_unlock(&audio_lock);
        pthread_mutex_lock(&ring_lock);
        mixer_in_flight=0;
        pthread_mutex_unlock(&ring_lock);
        if(failed) { const struct timespec pause={0,5000000};nanosleep(&pause,NULL); }
    }
    return NULL;
}
static void start_mixer(void)
{
    pthread_t thread;
    if(!pthread_create(&thread,NULL,audio_mixer,NULL))pthread_detach(thread);
}
void *pw_wine_audio_ring_adopt(int fd)
{
    void *ring;
    unsigned slot=0;
    if(fd<0 || pw_audio_ring_bytes()>PW_WINE_AUDIO_RING_BYTES)return NULL;
    ring=mmap(NULL,PW_WINE_AUDIO_RING_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(ring==MAP_FAILED)return NULL;
    if(pw_audio_ring_init(ring,PW_WINE_AUDIO_RING_BYTES)!=PW_OK) { munmap(ring,PW_WINE_AUDIO_RING_BYTES);return NULL; }
    pthread_mutex_lock(&ring_lock);
    if(!local_ring_ready && pw_audio_ring_init(&local_ring,sizeof(local_ring))==PW_OK)local_ring_ready=1;
    while(slot<PW_WINE_AUDIO_RINGS && rings[slot])slot++;
    if(slot<PW_WINE_AUDIO_RINGS)rings[slot]=ring;
    pthread_mutex_unlock(&ring_lock);
    if(slot==PW_WINE_AUDIO_RINGS) { munmap(ring,PW_WINE_AUDIO_RING_BYTES);return NULL; }
    pthread_once(&mixer_once,start_mixer);
    return ring;
}
void pw_wine_audio_ring_release(void *ring)
{
    if(!ring)return;
    pthread_mutex_lock(&ring_lock);
    for(unsigned i=0;i<PW_WINE_AUDIO_RINGS;i++)
        if(rings[i]==ring) { rings[i]=NULL;munmap(ring,PW_WINE_AUDIO_RING_BYTES); }
    pthread_mutex_unlock(&ring_lock);
}

void pw_wine_sink_stats(PwWineSinkStats *out)
{
    if(!out)return;
    pthread_mutex_lock(&lock);
    *out=stats;
    pthread_mutex_unlock(&lock);
    pthread_mutex_lock(&audio_lock);
    out->grains=grains;out->grains_dropped=__atomic_load_n(&grains_dropped,__ATOMIC_RELAXED);
    pthread_mutex_unlock(&audio_lock);
}

/* The pad has its own fields compared, not memcmp'd: padding stays out. */
static int same_pad(const PwWinePad *a,const PwWinePad *b)
{
    return a->buttons==b->buttons && a->left_trigger==b->left_trigger &&
           a->right_trigger==b->right_trigger && a->thumb_lx==b->thumb_lx &&
           a->thumb_ly==b->thumb_ly && a->thumb_rx==b->thumb_rx && a->thumb_ry==b->thumb_ry;
}
static void input_page_write_locked(PwWineInputPage *page)
{
    const uint32_t sequence=__atomic_load_n(&page->sequence,__ATOMIC_RELAXED);
    __atomic_store_n(&page->sequence,sequence+1,__ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    page->pad=pad_state;
    page->dualsense_connected=(uint32_t)dualsense_connected;
    memcpy(page->dualsense,dualsense_report,sizeof(page->dualsense));
    __atomic_store_n(&page->sequence,sequence+2,__ATOMIC_RELEASE);
}
static void input_page_publish_locked(void)
{
    for(unsigned i=0;i<PW_WINE_CHILD_PAGES;i++)
        if(child_pages[i])input_page_write_locked(child_pages[i]);
}
void *pw_wine_input_page_adopt(int fd)
{
    PwWineInputPage *page;
    unsigned slot=0;
    if(fd<0)return NULL;
    page=mmap(NULL,PW_WINE_INPUT_PAGE_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(page==MAP_FAILED)return NULL;
    pthread_mutex_lock(&lock);
    while(slot<PW_WINE_CHILD_PAGES && child_pages[slot])slot++;
    if(slot<PW_WINE_CHILD_PAGES) { child_pages[slot]=page;input_page_write_locked(page); }
    pthread_mutex_unlock(&lock);
    if(slot==PW_WINE_CHILD_PAGES) { munmap(page,PW_WINE_INPUT_PAGE_BYTES);return NULL; }
    return page;
}
/* The title's word to its helpers, every tick (src/pw_suspend.h): away while the system says the app is not in the
 * foreground, and the tick as a beat, so a helper that reads the same beat a second knows the title is frozen. */
void pw_wine_set_suspend(uint32_t away,uint32_t beat)
{
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<PW_WINE_CHILD_PAGES;i++) {
        if(!child_pages[i])continue;
        PwSuspendPage *word=(PwSuspendPage *)((char *)child_pages[i]+PW_SUSPEND_PAGE_OFFSET);
        __atomic_store_n(&word->away,away,__ATOMIC_RELAXED);
        __atomic_store_n(&word->beat,beat,__ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&lock);
}
void pw_wine_input_page_release(void *page)
{
    if(!page)return;
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<PW_WINE_CHILD_PAGES;i++)
        if(child_pages[i]==page) { child_pages[i]=NULL;munmap(page,PW_WINE_INPUT_PAGE_BYTES); }
    pthread_mutex_unlock(&lock);
}
/* In a child: the page the loader was given, mapped once. */
static void input_page_attach_locked(void)
{
    const char *value;char *end;long fd;void *page;
    if(input_page_tried || input_page)return;
    input_page_tried=1;
    value=getenv("WINE_PS5_INPUT_FD");
    if(!value || !*value)return;
    fd=strtol(value,&end,10);
    if(*end || fd<0 || fd>INT_MAX)return;
    page=pw_wine_pages_map(PW_WINE_INPUT_PAGE_BYTES,PROT_READ,(int)fd);
    if(page!=MAP_FAILED)input_page=page;
}
void pw_wine_set_pad(const PwWinePad *pad)
{
    int changed;
    pthread_mutex_lock(&lock);
    pad_set=1;
    changed=(pad && pad->connected)!=(pad_state.connected!=0);
    if(changed)pad_changes++;
    if(!pad || !pad->connected)pad_state.connected=0;
    else if(!pad_state.connected || !same_pad(pad,&pad_state)) {
        uint32_t packet=pad_state.packet+1;
        pad_state=*pad;pad_state.connected=1;
        pad_state.packet=packet?packet:1;   /* 0 is never a packet */
    }
    input_page_publish_locked();
    pthread_mutex_unlock(&lock);
    if(changed)wake_driver();   /* the driver tells the game's windows */
}
uint32_t pw_wine_pad_changes(void)
{
    PwWinePad copy;
    uint32_t changes;
    int connected,helper;
    pthread_mutex_lock(&lock);
    helper=!pad_set;
    pthread_mutex_unlock(&lock);
    /* A helper does not set the pad: it sees the title's through the input page, so a change is the page showing a
     * different state than at the last look (the first look is the baseline). */
    connected=helper ? pw_wine_pad(&copy)!=0 : 0;
    pthread_mutex_lock(&lock);
    if(helper) {
        if(pad_seen>=0 && pad_seen!=connected)pad_changes++;
        pad_seen=connected;
    }
    changes=pad_changes;
    pthread_mutex_unlock(&lock);
    return changes;
}
int pw_wine_pad(PwWinePad *pad)
{
    int connected;
    if(!pad)return 0;
    pthread_mutex_lock(&lock);
    if(!pad_set) {
        input_page_attach_locked();
        if(input_page) {
            PwWinePad copy;memset(&copy,0,sizeof(copy));
            for(unsigned tries=0;tries<1000;++tries) {
                const uint32_t before=__atomic_load_n(&input_page->sequence,__ATOMIC_ACQUIRE);
                if(before&1)continue;
                copy=input_page->pad;
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                if(__atomic_load_n(&input_page->sequence,__ATOMIC_RELAXED)==before)break;
            }
            pthread_mutex_unlock(&lock);
            *pad=copy;
            if(!copy.connected) { memset(pad,0,sizeof(*pad));return 0; }
            return 1;
        }
    }
    connected=pad_state.connected!=0;
    if(connected)*pad=pad_state;
    else memset(pad,0,sizeof(*pad));
    pthread_mutex_unlock(&lock);
    return connected;
}
void pw_wine_set_rumble(uint32_t left,uint32_t right)
{
    if(left>0xffffu)left=0xffffu;
    if(right>0xffffu)right=0xffffu;
    pthread_mutex_lock(&lock);
    if(left!=rumble_left || right!=rumble_right) {
        rumble_left=left;rumble_right=right;rumble_changed=1;
    }
    pthread_mutex_unlock(&lock);
}
int pw_wine_rumble(uint32_t *left,uint32_t *right)
{
    int changed;
    pthread_mutex_lock(&lock);
    changed=(int)rumble_changed;rumble_changed=0;
    if(left)*left=rumble_left;
    if(right)*right=rumble_right;
    pthread_mutex_unlock(&lock);
    return changed;
}

void pw_wine_set_dualsense(const uint8_t *report)
{
    pthread_mutex_lock(&lock);
    dualsense_set = 1;
    dualsense_connected = report != NULL;
    if (report) memcpy(dualsense_report, report, sizeof(dualsense_report));
    input_page_publish_locked();
    pthread_mutex_unlock(&lock);
}
static uint64_t input_counts[7];
static void input_count(int index) { __atomic_add_fetch(&input_counts[index], 1, __ATOMIC_RELAXED); }
void pw_wine_input_note(int what)
{
    if (what >= PW_WINE_INPUT_QUEUED && what <= PW_WINE_INPUT_REMOVED) input_count(3 + what);
}
void pw_wine_input_stats(PwWineInputStats *out)
{
    if (!out) return;
    uint64_t *const fields[7] = {&out->reads, &out->connected, &out->pressed, &out->queued, &out->unstarted,
                                 &out->created, &out->removed};
    for (unsigned i = 0; i < 7; ++i) *fields[i] = __atomic_load_n(&input_counts[i], __ATOMIC_RELAXED);
}
/* A USB report 0x01 with a button down or the hat off centre (src/proton_dualsense.h). */
static int report_pressed(const uint8_t *report)
{
    return (report[8] & 0x0f) != 8 || (report[8] & 0xf0) || report[9] || (report[10] & 3);
}
static int dualsense_read(uint8_t *report);
int pw_wine_dualsense(uint8_t *report)
{
    uint8_t copy[64];
    const int connected = dualsense_read(copy);
    input_count(0);
    if (connected) {
        input_count(1);
        if (report_pressed(copy)) input_count(2);
        if (report) memcpy(report, copy, sizeof(copy));
    }
    return connected;
}
static int dualsense_read(uint8_t *report)
{
    int connected;
    pthread_mutex_lock(&lock);
    if (!dualsense_set) {
        /* In a helper: the title's report, from the page it shares, read whole as the pad is. */
        input_page_attach_locked();
        if (input_page) {
            uint8_t copy[64] = {0};
            uint32_t page_connected = 0;
            for (unsigned tries = 0; tries < 1000; ++tries) {
                const uint32_t before = __atomic_load_n(&input_page->sequence, __ATOMIC_ACQUIRE);
                if (before & 1) continue;
                page_connected = input_page->dualsense_connected;
                memcpy(copy, input_page->dualsense, sizeof(copy));
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                if (__atomic_load_n(&input_page->sequence, __ATOMIC_RELAXED) == before) break;
            }
            pthread_mutex_unlock(&lock);
            if (report && page_connected) memcpy(report, copy, sizeof(copy));
            return page_connected != 0;
        }
    }
    connected = dualsense_connected;
    if (report && connected) memcpy(report, dualsense_report, sizeof(dualsense_report));
    pthread_mutex_unlock(&lock);
    return connected;
}

int pw_wine_stop_audio(unsigned timeout_ms)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME,&deadline);
    deadline.tv_sec+=timeout_ms/1000;
    deadline.tv_nsec+=(long)(timeout_ms%1000)*1000000;
    if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}
    /* A grain being played holds the lock: having it means none is. */
    if(pthread_mutex_timedlock(&audio_lock,&deadline))return -1;
    audio_sink=NULL;audio_context=NULL;
    pthread_mutex_unlock(&audio_lock);
    return 0;
}

static pthread_mutex_t hook_lock=PTHREAD_MUTEX_INITIALIZER;
static void (*exit_hooks[PW_WINE_EXIT_HOOKS])(void);
static unsigned exit_hook_count;
static int exit_hooks_ran;

int pw_wine_add_exit_hook(void (*hook)(void))
{
    int status=-1;
    pthread_mutex_lock(&hook_lock);
    if(hook && !exit_hooks_ran && exit_hook_count<PW_WINE_EXIT_HOOKS) {
        exit_hooks[exit_hook_count++]=hook;status=0;
    }
    pthread_mutex_unlock(&hook_lock);
    return status;
}

void pw_wine_run_exit_hooks(void)
{
    void (*hooks[PW_WINE_EXIT_HOOKS])(void);
    unsigned count;
    pthread_mutex_lock(&hook_lock);
    count=exit_hooks_ran?0:exit_hook_count;
    exit_hooks_ran=1;
    for(unsigned i=0;i<count;i++)hooks[i]=exit_hooks[i];
    pthread_mutex_unlock(&hook_lock);
    while(count)hooks[--count]();
}

static void (*thread_exit_hooks[PW_WINE_THREAD_EXIT_HOOKS])(void);
static unsigned thread_exit_hook_count;

int pw_wine_add_thread_exit_hook(void (*hook)(void))
{
    int status=-1;
    pthread_mutex_lock(&hook_lock);
    if(hook && thread_exit_hook_count<PW_WINE_THREAD_EXIT_HOOKS) {
        thread_exit_hooks[thread_exit_hook_count++]=hook;status=0;
    }
    pthread_mutex_unlock(&hook_lock);
    return status;
}

void pw_wine_run_thread_exit_hooks(void)
{
    void (*hooks[PW_WINE_THREAD_EXIT_HOOKS])(void);
    unsigned count;
    /* every thread ends here, and most have no hook to run */
    if(!__atomic_load_n(&thread_exit_hook_count,__ATOMIC_ACQUIRE))return;
    pthread_mutex_lock(&hook_lock);
    count=thread_exit_hook_count;
    for(unsigned i=0;i<count;i++)hooks[i]=thread_exit_hooks[i];
    pthread_mutex_unlock(&hook_lock);
    while(count)hooks[--count]();
}
