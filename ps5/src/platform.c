/*
 * PS5 Vulkan Template - the console: klog, the splash, the pad, time, the exit.
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 * SPDX-License-Identifier: MIT
 *
 * Every console call here is one PS5_vkQuake, PS5_RetroArch or PS5_Vulkan's
 * smoke test runs with; no SDK header declares them.
 */
#include "platform.h"
#include "../../src/pw_capped_log.h"

#include <ps5platform/klog.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int sceSystemServiceHideSplashScreen(void);
int sceNetInit(void);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);
int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int32_t *user_id);
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *params);
int scePadRead(int32_t handle, void *samples, int32_t capacity);
int scePadSetVibrationMode(int32_t handle, int32_t mode);
int scePadSetVibration(int32_t handle, const void *vibration);
int scePadSetLightBar(int32_t handle, const void *color);
int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t grain, uint32_t rate,
                    uint32_t format);
int sceAudioOutOutput(int32_t port, const void *samples);
int sceAudioOutClose(int32_t port);
int sceKernelUsleep(uint32_t microseconds);

void
platform_init(const char *title_name)
{
   static char prefix[64];
   snprintf(prefix, sizeof(prefix), "[%s] ", title_name);
   const int captured = ps5_klog_capture_stderr(prefix);
   say("starts (standard error to klog: %s)", captured == 0 ? "yes" : "no");
   /* The shell's splash covers the title's frames until it is dismissed. */
   say("splash dismissed: %d", sceSystemServiceHideSplashScreen());
   /* Winsock's Unix side resolves names through libSceNet (wine/ps5/pw_ws2_32_libc.c): loading it here,
    * with the title, is what lets ws2_32.prx bind to it. */
   const int net = sceNetInit();
   if (net < 0)
      say("network: libSceNet did not initialise (%#x); names will not resolve", (unsigned)net);
}

/* A file every say() line also goes to (platform_say_file): klog travels over the network, and when the console
 * froze under a launcher (2026-10-10; record removed from the tree) the network died first and nothing of the title's last seconds
 * survived. Bounded: past PLATFORM_SAY_FILE_BYTES the file says so and takes no more. A session's log instead keeps its
 * start and its end (platform_say_log, src/pw_capped_log.h). */
enum { PLATFORM_SAY_FILE_BYTES = 8 << 20 };
static int say_file = -1;
static size_t say_file_bytes;
static PwCappedLog *say_log;

void
platform_say_file(int fd)
{
   say_file = fd;
   say_file_bytes = 0;
}

void
platform_say_log(PwCappedLog *log)
{
   say_log = log;
}

void
say(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   if (say_log || (say_file >= 0 && say_file_bytes < PLATFORM_SAY_FILE_BYTES)) {
      char line[1024];
      va_list copy;
      va_copy(copy, args);
      int length = vsnprintf(line, sizeof(line) - 1, format, copy);
      va_end(copy);
      if (length > (int)sizeof(line) - 2) length = (int)sizeof(line) - 2;
      if (length >= 0) {
         line[length++] = '\n';
         if (say_log) {
            pw_capped_log_write(say_log, line, (size_t)length);
         } else {
            if (write(say_file, line, (size_t)length) == length) say_file_bytes += (size_t)length;
            if (say_file_bytes >= PLATFORM_SAY_FILE_BYTES)
               (void)!write(say_file, "(title log full)\n", 17);
         }
      }
   }
   vfprintf(stderr, format, args);
   va_end(args);
   fputc('\n', stderr);
   fflush(stderr);
}

double
now_seconds(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* The title ends by asking the shell to close it. exit() and returning from
 * _start both kill it, and the console then reports a crash over a run that
 * worked. The CRT calls this after main returns; the shell closes the title
 * asynchronously, so it must wait and never return. */
void
catchReturnFromMain(int status)
{
   say("exit status %d: asking the shell to close the title", status);
   fflush(NULL);
   (void)sceSystemServiceLoadExec("exit", NULL);
   for (;;)
      sceKernelUsleep(100000);
}

/* ----------------------------------------------------------------- the pad */

/* One sample as the console writes it: 120 bytes, the layout PS5_vkQuake and
 * ProsperoLight read. */
struct pad_sample {
   uint32_t buttons;
   uint8_t left_x, left_y, right_x, right_y, l2, r2;
   uint16_t reserved0;
   float quaternion[4], acceleration[3], angular_velocity[3];
   uint8_t touch_count, reserved1[3];
   uint32_t reserved2;
   struct { uint16_t x, y; uint8_t id, reserved[3]; } touches[2];
   int32_t connected;
   uint64_t timestamp_us;
   uint8_t extension[16];
   uint8_t connected_count;
   uint8_t remaining[15];
};
_Static_assert(sizeof(struct pad_sample) == 120, "the console's pad samples are 120 bytes");
_Static_assert(offsetof(struct pad_sample, touches) == 0x3c, "touch points start at 0x3c");
_Static_assert(offsetof(struct pad_sample, connected) == 0x4c, "connection state sits at 0x4c");
_Static_assert(offsetof(struct pad_sample, timestamp_us) == 0x50, "the timestamp sits at 0x50");

/* The template UI uses the compatible two-motor rumble mode. */
#define PAD_VIBRATION_COMPATIBLE 2

static int32_t pad_handle = -1;
static struct pad_sample pad_last;
static bool pad_overlay;   /* the newest reading came from a pad that is there, with the shell holding it */
static struct pad_reading pad_taken[64];
static int pad_taken_count;

bool
pad_open(void)
{
   (void)sceUserServiceInitialize(NULL);
   int32_t user = -1;
   if (sceUserServiceGetInitialUser(&user) < 0 || scePadInit() < 0) {
      say("pad: no user or no pad service");
      return false;
   }
   /* A title can start before the pad service has published the device. */
   for (int attempt = 0; attempt < 10 && pad_handle < 0; attempt++) {
      pad_handle = scePadOpen(user, 0, 0, NULL);
      if (pad_handle < 0)
         sceKernelUsleep(100000);
   }
   say(pad_handle >= 0 ? "pad: opened for user %d" : "pad: scePadOpen failed for user %d", (int)user);
   if (pad_handle >= 0)
      say("pad: rumble mode %d", scePadSetVibrationMode(pad_handle, PAD_VIBRATION_COMPATIBLE));
   memset(&pad_last, 0, sizeof(pad_last));
   pad_last.left_x = pad_last.left_y = pad_last.right_x = pad_last.right_y = 128;
   return pad_handle >= 0;
}

static float
stick(uint8_t value)
{
   const float v = ((float)value - 128.0f) / 127.0f;
   const float dead = 0.12f;
   if (v > -dead && v < dead)
      return 0.0f;
   return v > 0 ? (v - dead) / (1.0f - dead) : (v + dead) / (1.0f - dead);
}

void
pad_poll(struct pad *pad)
{
   const uint32_t before = pad->held;
   pad_taken_count = 0;
   if (pad_handle >= 0) {
      static struct pad_sample samples[64];
      const int count = scePadRead(pad_handle, samples, 64);
      if (count < 0) {
         pad_taken[pad_taken_count++] = (struct pad_reading){0};
         memset(&pad_last, 0, sizeof(pad_last));
         pad_last.left_x = pad_last.left_y = pad_last.right_x = pad_last.right_y = 128;
         pad_overlay = false;
      }
      for (int i = 0; i < count && i < 64; i++) {
         const struct pad_sample *s = &samples[i];
         pad_taken[pad_taken_count++] = (struct pad_reading){
            s->buttons, s->left_x, s->left_y, s->right_x, s->right_y, s->l2, s->r2,
            s->connected != 0, s->timestamp_us};
         pad_last = *s;
         pad_overlay = pad_last.connected && (pad_last.buttons & PAD_INTERCEPTED);
         if (!pad_last.connected || (pad_last.buttons & PAD_INTERCEPTED)) {
            memset(&pad_last, 0, sizeof(pad_last));
            pad_last.left_x = pad_last.left_y = pad_last.right_x = pad_last.right_y = 128;
         }
      }
   }
   pad->connected = pad_handle >= 0 && pad_last.connected;
   pad->intercepted = pad_handle >= 0 && pad_overlay;
   pad->held = pad->connected ? pad_last.buttons : 0;
   memcpy(pad->raw_axes, &pad_last.left_x, sizeof(pad->raw_axes));
   pad->touch_count = pad->connected ? pad_last.touch_count : 0;
   pad->touch_id = pad_last.touches[0].id;
   pad->touch_x = pad_last.touches[0].x;
   pad->touch_y = pad_last.touches[0].y;
   pad->timestamp_us = pad_last.timestamp_us;
   pad->pressed = pad->held & ~before;
   pad->left_x = stick(pad_last.left_x);
   pad->left_y = stick(pad_last.left_y);
   pad->right_x = stick(pad_last.right_x);
   pad->right_y = stick(pad_last.right_y);
   pad->l2 = pad_last.l2 / 255.0f;
   pad->r2 = pad_last.r2 / 255.0f;
}

int
pad_readings(const struct pad_reading **readings)
{
   *readings = pad_taken;
   return pad_taken_count;
}

void
pad_vibrate(float large, float small)
{
   if (pad_handle < 0)
      return;
   const float l = large < 0.0f ? 0.0f : large > 1.0f ? 1.0f : large;
   const float s = small < 0.0f ? 0.0f : small > 1.0f ? 1.0f : small;
   const uint8_t motors[2] = {(uint8_t)(l * 255.0f), (uint8_t)(s * 255.0f)};
   (void)scePadSetVibration(pad_handle, motors);
}

void
pad_light_bar(uint8_t r, uint8_t g, uint8_t b)
{
   if (pad_handle < 0)
      return;
   const uint8_t color[4] = {r, g, b, 0};
   (void)scePadSetLightBar(pad_handle, color);
}

/* --------------------------------------------------------------- the sound */

#define AUDIO_GRAIN 256
#define AUDIO_ALREADY_INITIALISED ((int)0x8026000e)

static int32_t audio_port = -1;
static pthread_t audio_thread;
static atomic_bool audio_running;
static audio_fill_fn audio_fill;
static void *audio_user;

static void *
audio_main(void *unused)
{
   (void)unused;
   static int16_t grain[AUDIO_GRAIN * 2] __attribute__((aligned(64)));
   unsigned errors = 0;
   while (atomic_load(&audio_running)) {
      audio_fill(grain, AUDIO_GRAIN, audio_user);
      /* Blocks for one grain: this is what paces the thread. An output that
       * stops taking grains is said once, not left to stall the program. */
      if (sceAudioOutOutput(audio_port, grain) < 0) {
         if (errors++ == 0)
            say("audio: sceAudioOutOutput failed");
         sceKernelUsleep(5000);
      }
   }
   (void)sceAudioOutOutput(audio_port, NULL); /* drain the grain still queued */
   return NULL;
}

bool
audio_start(audio_fill_fn fill, void *user)
{
   if (audio_port >= 0 || fill == NULL)
      return false;
   const int init = sceAudioOutInit();
   if (init < 0 && init != AUDIO_ALREADY_INITIALISED) {
      say("audio: sceAudioOutInit 0x%08x", (unsigned)init);
      return false;
   }
   /* The system user's main port: 256 frames, 48 kHz, 16-bit stereo. */
   audio_port = sceAudioOutOpen(0xff, 0, 0, AUDIO_GRAIN, 48000, 1);
   if (audio_port < 0) {
      say("audio: sceAudioOutOpen 0x%08x", (unsigned)audio_port);
      audio_port = -1;
      return false;
   }
   audio_fill = fill;
   audio_user = user;
   atomic_store(&audio_running, true);
   if (pthread_create(&audio_thread, NULL, audio_main, NULL) != 0) {
      say("audio: no thread");
      atomic_store(&audio_running, false);
      (void)sceAudioOutClose(audio_port);
      audio_port = -1;
      return false;
   }
   say("audio: 48 kHz stereo on port %d", (int)audio_port);
   return true;
}

void
audio_stop(void)
{
   if (audio_port < 0)
      return;
   atomic_store(&audio_running, false);
   pthread_join(audio_thread, NULL);
   (void)sceAudioOutClose(audio_port);
   audio_port = -1;
}
