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
int sceSystemServiceGetAppStatus(void *status);
int sceSystemServiceGetLocalProcessStatusList(void *entries, uint32_t capacity, uint32_t *count);
int sceSystemServiceKillLocalProcess(int app_id, int service_id);
int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int32_t *user_id);
int sceUserServiceGetLoginUserIdList(int32_t user_ids[4]);
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *params);
int scePadClose(int32_t handle);
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
platform_kill_local_processes(void)
{
   uint32_t app_status[4] = {0};
   struct { int32_t id; char name[32]; } entries[16];
   uint32_t count = 0;
   if (sceSystemServiceGetAppStatus(app_status) != 0 || (int32_t)app_status[0] <= 0) return;
   memset(entries, 0, sizeof(entries));
   if (sceSystemServiceGetLocalProcessStatusList(entries, 16, &count) != 0 || count == 0 || count > 16) return;
   for (uint32_t i = 0; i < count; ++i) {
      entries[i].name[sizeof(entries[i].name) - 1] = 0;
      if (entries[i].id > 0 && strstr(entries[i].name, "wine-local-child") != NULL) {
         const int killed = sceSystemServiceKillLocalProcess((int)app_status[0], entries[i].id);
         say("cleanup: killed lingering local process %s service=%#x status=%#x",
             entries[i].name, (unsigned)entries[i].id, (unsigned)killed);
      }
   }
   for (int wait = 0; wait < 100; ++wait) {
      count = 0;
      if (sceSystemServiceGetLocalProcessStatusList(entries, 16, &count) != 0 || count == 0 || count > 16) break;
      int left = 0;
      for (uint32_t i = 0; i < count; ++i) {
         entries[i].name[sizeof(entries[i].name) - 1] = 0;
         if (entries[i].id > 0 && strstr(entries[i].name, "wine-local-child") != NULL) left++;
      }
      if (!left) break;
      sceKernelUsleep(10000);
   }
}

void
catchReturnFromMain(int status)
{
   platform_kill_local_processes();
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

static int32_t pad_handles[PAD_MAX_SLOTS] = {-1, -1, -1, -1};
static int32_t pad_users[PAD_MAX_SLOTS] = {-1, -1, -1, -1};
static struct pad_sample pad_last[PAD_MAX_SLOTS];
static bool pad_overlay[PAD_MAX_SLOTS];   /* the newest reading came from a pad that is there, with the shell holding it */
static bool pad_service_ready;
static double pad_last_refresh;
static struct pad_reading pad_taken[64];
static int pad_taken_count;

static void
pad_reset_sample(int slot)
{
   memset(&pad_last[slot], 0, sizeof(pad_last[slot]));
   pad_last[slot].left_x = pad_last[slot].left_y = pad_last[slot].right_x = pad_last[slot].right_y = 128;
}

static void
pad_refresh_slots(bool force)
{
   if (!pad_service_ready)
      return;
   const double now = now_seconds();
   if (!force && now - pad_last_refresh < 2.0)
      return;
   pad_last_refresh = now;

   int32_t list[4] = {-1, -1, -1, -1};
   if (sceUserServiceGetLoginUserIdList(list) < 0)
      return;

   /* Close slots 1..3 whose user signed out. Slot 0 stays sticky to the initial user. */
   for (int s = 1; s < PAD_MAX_SLOTS; ++s) {
      if (pad_handles[s] < 0)
         continue;
      bool present = false;
      for (int i = 0; i < 4; ++i) {
         if (list[i] == pad_users[s]) {
            present = true;
            break;
         }
      }
      if (!present) {
         (void)scePadClose(pad_handles[s]);
         say("pad: closed slot %d for user %d (logged out)", s, (int)pad_users[s]);
         pad_handles[s] = -1;
         pad_users[s] = -1;
         pad_reset_sample(s);
         pad_overlay[s] = false;
      }
   }

   /* Open any newly logged-in users in the next free slot. */
   for (int i = 0; i < 4; ++i) {
      const int32_t uid = list[i];
      if (uid == -1 || uid == 0)
         continue;
      bool assigned = false;
      for (int s = 0; s < PAD_MAX_SLOTS; ++s) {
         if (pad_handles[s] >= 0 && pad_users[s] == uid) {
            assigned = true;
            break;
         }
      }
      if (assigned)
         continue;
      int free_slot = -1;
      for (int s = 1; s < PAD_MAX_SLOTS; ++s) {
         if (pad_handles[s] < 0) {
            free_slot = s;
            break;
         }
      }
      if (free_slot < 0)
         break;
      const int32_t h = scePadOpen(uid, 0, 0, NULL);
      if (h >= 0) {
         pad_handles[free_slot] = h;
         pad_users[free_slot] = uid;
         pad_reset_sample(free_slot);
         pad_overlay[free_slot] = false;
         const int vmode = scePadSetVibrationMode(h, PAD_VIBRATION_COMPATIBLE);
         say("pad: opened slot %d for user %d (rumble mode %d)", free_slot, (int)uid, vmode);
      }
   }
}

bool
pad_open(void)
{
   for (int s = 0; s < PAD_MAX_SLOTS; ++s) {
      pad_handles[s] = -1;
      pad_users[s] = -1;
      pad_reset_sample(s);
      pad_overlay[s] = false;
   }
   (void)sceUserServiceInitialize(NULL);
   int32_t user = -1;
   if (sceUserServiceGetInitialUser(&user) < 0 || scePadInit() < 0) {
      say("pad: no user or no pad service");
      return false;
   }
   /* A title can start before the pad service has published the device. */
   for (int attempt = 0; attempt < 10 && pad_handles[0] < 0; attempt++) {
      pad_handles[0] = scePadOpen(user, 0, 0, NULL);
      if (pad_handles[0] < 0)
         sceKernelUsleep(100000);
   }
   say(pad_handles[0] >= 0 ? "pad: opened for user %d" : "pad: scePadOpen failed for user %d", (int)user);
   if (pad_handles[0] >= 0) {
      pad_users[0] = user;
      say("pad: rumble mode %d", scePadSetVibrationMode(pad_handles[0], PAD_VIBRATION_COMPATIBLE));
   }
   pad_service_ready = true;
   pad_refresh_slots(true);
   return pad_handles[0] >= 0;
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
pad_poll_slot(int slot, struct pad *pad)
{
   if (!pad)
      return;
   if (slot < 0 || slot >= PAD_MAX_SLOTS) {
      memset(pad, 0, sizeof(*pad));
      return;
   }
   if (slot == 0) {
      pad_refresh_slots(false);
      pad_taken_count = 0;
   }
   const uint32_t before = pad->held;
   const int32_t handle = pad_handles[slot];
   if (handle >= 0) {
      static struct pad_sample samples[64];
      const int count = scePadRead(handle, samples, 64);
      if (count < 0) {
         if (slot == 0)
            pad_taken[pad_taken_count++] = (struct pad_reading){0};
         pad_reset_sample(slot);
         pad_overlay[slot] = false;
      }
      for (int i = 0; i < count && i < 64; i++) {
         const struct pad_sample *s = &samples[i];
         if (slot == 0)
            pad_taken[pad_taken_count++] = (struct pad_reading){
               s->buttons, s->left_x, s->left_y, s->right_x, s->right_y, s->l2, s->r2,
               s->connected != 0, s->timestamp_us};
         pad_last[slot] = *s;
         pad_overlay[slot] = pad_last[slot].connected && (pad_last[slot].buttons & PAD_INTERCEPTED);
         if (!pad_last[slot].connected || (pad_last[slot].buttons & PAD_INTERCEPTED))
            pad_reset_sample(slot);
      }
   }
   const struct pad_sample *last = &pad_last[slot];
   pad->connected = handle >= 0 && last->connected;
   pad->intercepted = handle >= 0 && pad_overlay[slot];
   pad->held = pad->connected ? last->buttons : 0;
   memcpy(pad->raw_axes, &last->left_x, sizeof(pad->raw_axes));
   pad->touch_count = pad->connected ? last->touch_count : 0;
   pad->touch_id = last->touches[0].id;
   pad->touch_x = last->touches[0].x;
   pad->touch_y = last->touches[0].y;
   pad->timestamp_us = last->timestamp_us;
   pad->pressed = pad->held & ~before;
   pad->left_x = stick(last->left_x);
   pad->left_y = stick(last->left_y);
   pad->right_x = stick(last->right_x);
   pad->right_y = stick(last->right_y);
   pad->l2 = last->l2 / 255.0f;
   pad->r2 = last->r2 / 255.0f;
}

void
pad_poll(struct pad *pad)
{
   pad_poll_slot(0, pad);
}

int
pad_readings(const struct pad_reading **readings)
{
   *readings = pad_taken;
   return pad_taken_count;
}

void
pad_vibrate_slot(int slot, float large, float small)
{
   if (slot < 0 || slot >= PAD_MAX_SLOTS || pad_handles[slot] < 0)
      return;
   const float l = large < 0.0f ? 0.0f : large > 1.0f ? 1.0f : large;
   const float s = small < 0.0f ? 0.0f : small > 1.0f ? 1.0f : small;
   const uint8_t motors[2] = {(uint8_t)(l * 255.0f), (uint8_t)(s * 255.0f)};
   (void)scePadSetVibration(pad_handles[slot], motors);
}

void
pad_vibrate(float large, float small)
{
   pad_vibrate_slot(0, large, small);
}

void
pad_light_bar_slot(int slot, uint8_t r, uint8_t g, uint8_t b)
{
   if (slot < 0 || slot >= PAD_MAX_SLOTS || pad_handles[slot] < 0)
      return;
   const uint8_t color[4] = {r, g, b, 0};
   (void)scePadSetLightBar(pad_handles[slot], color);
}

void
pad_light_bar(uint8_t r, uint8_t g, uint8_t b)
{
   pad_light_bar_slot(0, r, g, b);
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
