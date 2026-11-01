/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_SINK_H
#define PW_WINE_SINK_H
#include <stdint.h>
#include <stddef.h>

/* The meeting point between the title and Wine's PS5 user driver. ntdll
 * links it: the title reaches the set/post calls through ntdll.prx's
 * descriptor before __wine_main, and win32u's driver reaches the present
 * and next calls with dlsym. Frames go out through one sink, the title's
 * pw_present_frame path; input comes in through a bounded queue the title
 * fills from the DualSense and the driver drains in ProcessEvents. A
 * game's XInput controller is one more slot: the title keeps the newest
 * gamepad state in it, Wine's xinput reads it (its Unix library, patch
 * 0470, finds these calls with dlsym) and leaves the rumble it asks for.
 * A game that presents with Vulkan scans out itself (patch 0460): the
 * driver asks the title once to release the video output. Frames still
 * reach the title after that, which shows them through the Vulkan driver
 * while no swapchain presents (a DirectShow movie drawn with GDI).
 * Sound goes out like frames: Wine's audio driver (wine/wineps5) hands its
 * mix to the title's one audio port, one grain at a time. */

/* BGRA, top-down; stride in bytes. Returns the sink's result. */
typedef int (*PwWinePresentSink)(void *context,const void *bgra,uint32_t width,uint32_t height,
                                 uint32_t stride);

/* CLOSE asks the game to close as a user would (WM_CLOSE to its window);
 * TERMINATE ends its process (NtTerminateProcess). Both are carried out on
 * the thread that pumps the game's messages, a Windows thread, so the
 * process ends through Wine's own exit path. */
enum { PW_WINE_INPUT_KEY=1,PW_WINE_INPUT_MOUSE_MOVE,PW_WINE_INPUT_MOUSE_BUTTON,
       PW_WINE_INPUT_CLOSE,PW_WINE_INPUT_TERMINATE,PW_WINE_INPUT_MOUSE_WHEEL,PW_WINE_INPUT_END_PROCESS,
       PW_WINE_INPUT_LOG_OFF,PW_WINE_INPUT_DIALOG_KEY,PW_WINE_INPUT_CHAR,PW_WINE_INPUT_QUEUE=256 };
/* CHAR (the in-process server and the driver, patch 2424): one UTF-16 unit in code, typed as a KEYEVENTF_UNICODE key; down 1
 * presses it, 0 releases it. The on-screen keyboard's line arrives this way (src/pw_osk.h). */
/* END_PROCESS (the in-process server only, patch 1002): one process, by its native helper's pid in code; down 1 asks
 * its visible top-level windows to close and ends it when it has none, down 0 ends it. */
/* DIALOG_KEY (the in-process server only, patch 2418): a KEY code as above that the server delivers only while a dialog box is
 * the active window, with the release of one it delivered (src/pw_dialog_keys.h): the controller's Cross and D-pad as Enter and
 * arrows. */
/* LOG_OFF (the in-process server only, patch 1214): the desktop session's end, WM_CLOSE to the shell window as Start's
 * Power does; with Explorer unloaded, CLOSE. */
/* A KEY code: the Windows virtual key in bits 0-7, its set-1 scan code in bits 8-15 (0: none, Wine's driver maps
 * one), bit 16 the extended flag (E0). A USB keyboard sends all three (src/pw_usb_input.h). */
enum { PW_WINE_KEY_SCAN_SHIFT=8,PW_WINE_KEY_EXTENDED=1u<<16 };
typedef struct PwWineInput {
    uint32_t type;
    uint32_t code;      /* KEY: as above; MOUSE_BUTTON: 0 left, 1 right, 2 middle, 3 back (X1), 4 forward (X2) */
    int32_t x,y;        /* MOUSE_MOVE: relative motion in desktop pixels; MOUSE_WHEEL: horizontal and vertical
                           turns in WHEEL_DELTA units (120 a notch; y > 0 away from the user, x > 0 right) */
    uint32_t down;      /* KEY, MOUSE_BUTTON: 1 pressed, 0 released */
} PwWineInput;

/* The console's output format: interleaved left,right 16-bit frames at
 * 48 kHz, played PW_WINE_AUDIO_GRAIN frames at a time. */
enum { PW_WINE_AUDIO_RATE=48000,PW_WINE_AUDIO_GRAIN=256 };
/* Plays one grain and returns once the console has taken it, so the
 * console's output clocks the caller. Returns the sink's result. */
typedef int (*PwWineAudioSink)(void *context,const int16_t *frames);

/* Hands the video output over to a Vulkan display-plane surface; 0 once
 * the title no longer uses it. */
typedef int (*PwWineDisplayRelease)(void *context);

typedef struct PwWineSinkStats {
    uint64_t frames,frames_dropped;       /* dropped: no sink set or it failed */
    uint64_t inputs_posted,inputs_dropped,inputs_delivered;
    uint64_t grains,grains_dropped;       /* audio; dropped as for frames */
    uint32_t display_released;            /* 1 once Vulkan took the video output */
} PwWineSinkStats;

/* An XInput gamepad (XINPUT_GAMEPAD with the connection and a packet
 * number in front), laid out as xinput's Unix library reads it. */
typedef struct PwWinePad {
    uint32_t connected;     /* 0: no controller */
    uint32_t packet;        /* the sink's: changes whenever the state does */
    uint16_t buttons;       /* XINPUT_GAMEPAD_* */
    uint8_t left_trigger,right_trigger;
    int16_t thumb_lx,thumb_ly,thumb_rx,thumb_ry;
} PwWinePad;

/* Title side. */
void pw_wine_set_present_sink(PwWinePresentSink sink,void *context);
/* Native payload installs this before Wine starts. Frame transport needs a
 * zeroed shared fd, with no byte-I/O/private-map contract. NULL keeps the
 * ordinary diagnostic path. Only payload code imports private physhm calls. */
void pw_wine_set_frame_allocator(int (*alloc)(size_t));
void pw_wine_set_audio_sink(PwWineAudioSink sink,void *context);
void pw_wine_set_display_release(PwWineDisplayRelease release,void *context);
/* 0, or -1 when the queue is full or the event is invalid. */
int pw_wine_post_input(const PwWineInput *event);
void pw_wine_sink_stats(PwWineSinkStats *stats);
/* The gamepad's newest state, or NULL when it is disconnected. The sink
 * numbers the packets; the pad's own packet field is ignored. */
void pw_wine_set_pad(const PwWinePad *pad);
/* How many times the controller connected or disconnected, as this process sees it: the title counts them as it sets the
 * pad, and wakes the input fd; a helper counts the differences between its looks at the input page. The driver tells the
 * game's windows (WM_DEVICECHANGE, patch 2423) when the count moved. */
uint32_t pw_wine_pad_changes(void);
/* 1 and the newest motor speeds (0..65535) when they changed since the
 * last call, else 0. */
int pw_wine_rumble(uint32_t *left,uint32_t *right);

/* DualSense USB reports, shared with winebus. NULL disconnects. In a helper, whose sink the title never sets, this is
 * the title's report, read from the input page. */
void pw_wine_set_dualsense(const uint8_t *report);
int pw_wine_dualsense(uint8_t *report);
/* Where a press goes in a helper, counted for its log: the PS5 bus (winebus) notes what it did with each report it
 * read, and pw_wine_input_stats gives every count since the start. */
enum { PW_WINE_INPUT_QUEUED, PW_WINE_INPUT_UNSTARTED, PW_WINE_INPUT_CREATED, PW_WINE_INPUT_REMOVED };
void pw_wine_input_note(int what);
typedef struct PwWineInputStats {
    uint64_t reads, connected, pressed;              /* pw_wine_dualsense calls, those connected, those with a press */
    uint64_t queued, unstarted, created, removed;    /* the bus: reports passed on, held back, devices made, removed */
} PwWineInputStats;
void pw_wine_input_stats(PwWineInputStats *out);

/* Driver side. -1 when no sink is set or the arguments are invalid. In a Windows program in a helper process, whose
 * sink the title never sets, frames go to the title through a page they share (src/pw_frame_page.h). */
int pw_wine_present(const void *bgra,uint32_t width,uint32_t height,uint32_t stride);
/* Where the next frames' top-left corner is on the desktop, for the cursor the title draws over a helper's frames. */
void pw_wine_present_origin(int32_t x,int32_t y);
/* Asks the title to release the video output, once: 0 when it did (or has
 * nothing to release), else the title's refusal, and it may be asked again. */
int pw_wine_release_display(void);
/* 1 and the oldest event, or 0 when the queue is empty. */
int pw_wine_next_input(PwWineInput *event);
/* A non-blocking pipe that becomes readable when input is posted, so the
 * driver can have the server wake a thread idle in GetMessage. Readers
 * drain it before emptying the queue, which loses no wakeup. -1 if no
 * pipe could be created. */
int pw_wine_input_fd(void);
/* 1 and the gamepad's state while one is connected, else 0. In a Windows child process, whose sink the title never
 * sets, this is the title's pad, read from the input page the title shares with its children. */
int pw_wine_pad(PwWinePad *pad);
/* The input page and the audio ring a helper shares with the title are the helper's (src/pw_shared_page.h): its
 * loader makes them and gives them to the title, which maps them. */
#define PW_WINE_INPUT_PAGE_BYTES 16384
#define PW_WINE_AUDIO_RING_BYTES 16384
/* The title: map a helper's input page and keep the pad and the DualSense report written into it, with every other
 * helper's, until pw_wine_input_page_release. NULL when it cannot be mapped or no place is free. */
void *pw_wine_input_page_adopt(int fd);
void pw_wine_input_page_release(void *page);
/* The title, every tick: its suspend word in every adopted page (src/pw_suspend.h). */
void pw_wine_set_suspend(uint32_t away,uint32_t beat);
/* The rumble a game asked for (XInputSetState); speeds are clamped. */
void pw_wine_set_rumble(uint32_t left,uint32_t right);
/* 1 when the title set an audio sink, or, in a helper, when the title gave it an audio ring (WINE_PS5_AUDIO_FD,
 * src/pw_audio_ring.h): the audio driver offers its device only then. */
int pw_wine_audio_available(void);
/* Blocks while the sink plays one grain; in a helper, while the ring is full, at most 100 ms. -1 when no sink or
 * ring is set, frames is NULL, or the ring stayed full. */
int pw_wine_audio_output(const int16_t *frames);
/* The title: map a helper's audio ring (docs/GRAPHICS_PROCESS_BROKER.md, "Audio for helpers") and start it empty;
 * the title's mixer reads it a grain at a time, sums it with the other helpers' and plays it through the audio sink,
 * until pw_wine_audio_ring_release. NULL when it cannot be mapped or no place is free. */
void *pw_wine_audio_ring_adopt(int fd);
void pw_wine_audio_ring_release(void *ring);

/* Before the title replaces the process. Stop the audio sink: 0 once no
 * grain is being played and none will be (the title may close its port),
 * -1 if a grain still held the sink after timeout_ms. */
int pw_wine_stop_audio(unsigned timeout_ms);
/* Work for the end of the process, after Wine's own threads stop drawing:
 * the graphics drivers close their presenters and let the GPU go idle.
 * Up to PW_WINE_EXIT_HOOKS hooks; each runs once, newest first. */
enum { PW_WINE_EXIT_HOOKS=8 };
int pw_wine_add_exit_hook(void (*hook)(void));
void pw_wine_run_exit_hooks(void);

/* Hooks every thread runs as it ends, on the thread itself (ntdll's
 * pthread_exit_wrapper): what only the thread can undo, such as releasing the
 * OpenGL context it has current, without which the presenter cannot be closed
 * at exit. Up to PW_WINE_THREAD_EXIT_HOOKS hooks, newest first; a hook must
 * cope with a thread that has nothing to undo. */
enum { PW_WINE_THREAD_EXIT_HOOKS=4 };
int pw_wine_add_thread_exit_hook(void (*hook)(void));
void pw_wine_run_thread_exit_hooks(void);

#endif
