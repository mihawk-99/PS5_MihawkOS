/*
 * PS5 Vulkan Template - the console: klog, the splash, the pad, time, the exit.
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard error into klog (each line prefixed with the title's name), and
 * the shell's splash dismissed. Call first. */
void platform_init(const char *title_name);

/* One line to klog, flushed. */
void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* Every later say() line is also written to fd (bounded; the caller flushes it to disk), or none with -1. */
void platform_say_file(int fd);
/* Every later say() line goes to log instead of fd: its start and its end are kept (src/pw_capped_log.h). */
struct PwCappedLog;
void platform_say_log(struct PwCappedLog *log);

double now_seconds(void);

/* The pad, in the console's button numbering. */
enum {
   PAD_L3 = 0x000002,
   PAD_R3 = 0x000004,
   PAD_OPTIONS = 0x000008,
   PAD_UP = 0x000010,
   PAD_RIGHT = 0x000020,
   PAD_DOWN = 0x000040,
   PAD_LEFT = 0x000080,
   PAD_L1 = 0x000400,
   PAD_R1 = 0x000800,
   PAD_TRIANGLE = 0x001000,
   PAD_CIRCLE = 0x002000,
   PAD_CROSS = 0x004000,
   PAD_SQUARE = 0x008000,
   PAD_TOUCH_PAD = 0x100000,
};

struct pad {
   uint32_t held;    /* buttons down now */
   uint32_t pressed; /* buttons that went down since the last poll */
   float left_x, left_y, right_x, right_y; /* -1..1, a dead zone removed; y is down-positive */
   float l2, r2;                           /* 0..1 */
   bool connected;                         /* false while disconnected or intercepted */
   bool intercepted;                       /* the shell has the pad: connected, but the reading is not input */
   uint8_t raw_axes[6];                     /* LX, LY, RX, RY, L2, R2 */
   uint8_t touch_count, touch_id;
   uint16_t touch_x, touch_y;
   uint64_t timestamp_us;
};

/* Open the first user's pad; false (and no input) when there is none. */
bool pad_open(void);
void pad_poll(struct pad *pad);

/* Set in a reading's buttons while the shell has the pad (the home screen, a
 * system dialog): that reading is not input for the title. */
#define PAD_INTERCEPTED 0x80000000u

/* One reading as the console took it, in its numbering. */
struct pad_reading {
   uint32_t buttons;
   uint8_t left_x, left_y, right_x, right_y; /* 0..255, 128 at rest; y is down-positive */
   uint8_t l2, r2;                           /* 0..255 */
   bool connected;
   uint64_t timestamp_us;
};

/* Every reading the last pad_poll took, oldest first (the console keeps up to
 * 64 between polls), for input models that must see a tap shorter than a
 * frame. Returns their number; *readings stays valid until the next poll. */
int pad_readings(const struct pad_reading **readings);

/* Rumble: the large and the small motor, 0..1 each; 0, 0 stops it. */
void pad_vibrate(float large, float small);
/* The light bar's colour. */
void pad_light_bar(uint8_t r, uint8_t g, uint8_t b);

/* Sound: one 48 kHz output of interleaved stereo 16-bit samples. A thread of
 * its own calls fill for 256 frames at a time and blocks while the console
 * plays them, so fill paces itself and must never wait on the render loop.
 * false when there is no output (the host reference build has none). */
typedef void (*audio_fill_fn)(int16_t *frames, int count, void *user);
bool audio_start(audio_fill_fn fill, void *user);
/* Stops the thread and closes the output; call it before what fill uses goes. */
void audio_stop(void);

#ifdef __cplusplus
}
#endif
