/*
 * PS5 Vulkan Template - platform.h on a Linux PC, for the host reference build.
 *
 * The host reference build (ps5/tools/host-reference.sh) runs the title's code
 * on the PC's own Vulkan driver with a headless surface: no pad, no sound,
 * messages on standard error. Its pictures are the reference the console's are compared to.
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 * SPDX-License-Identifier: MIT
 */
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *say_prefix = "";

void
platform_init(const char *title_name)
{
   static char prefix[64];
   snprintf(prefix, sizeof(prefix), "[%s] ", title_name);
   say_prefix = prefix;
   say("starts (host reference build)");
}

void
platform_kill_local_processes(void)
{
}

void
platform_say_file(int fd)
{
   (void)fd;   /* the host reference build's stderr is already a file */
}

void
say(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   fputs(say_prefix, stderr);
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

bool
pad_open(void)
{
   return false;
}

void
pad_poll(struct pad *pad)
{
   memset(pad, 0, sizeof(*pad));
}

void
pad_poll_slot(int slot, struct pad *pad)
{
   (void)slot;
   if (pad)
      memset(pad, 0, sizeof(*pad));
}

int
pad_readings(const struct pad_reading **readings)
{
   *readings = NULL;
   return 0;
}

void
pad_vibrate(float large, float small)
{
   (void)large;
   (void)small;
}

void
pad_vibrate_slot(int slot, float large, float small)
{
   (void)slot;
   (void)large;
   (void)small;
}

void
pad_light_bar(uint8_t r, uint8_t g, uint8_t b)
{
   (void)r;
   (void)g;
   (void)b;
}

void
pad_light_bar_slot(int slot, uint8_t r, uint8_t g, uint8_t b)
{
   (void)slot;
   (void)r;
   (void)g;
   (void)b;
}

bool
audio_start(audio_fill_fn fill, void *user)
{
   (void)fill;
   (void)user;
   return false;
}

void
audio_stop(void)
{
}
