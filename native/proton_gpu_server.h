/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PROTON_GPU_SERVER_H
#define PROTON_GPU_SERVER_H
#include <stddef.h>
/* Experimental owning-title broker. Close only after Sony has ended the service. */
/* local: offer clients device-local memory from the owner's direct pool, beside their shared host-visible memory. */
bool proton_gpu_broker_start(bool capture,bool local=false);
/* Called when a program's device-local (graphics) memory is refused for want of direct memory (graphics patch 0042);
 * set before the broker starts. On the broker's thread, under its lock: it only records. */
void proton_gpu_broker_set_local_refused(void (*hook)(int pid,unsigned long long bytes));
int proton_gpu_broker_open(int *cookie,int pid);
/* The slot of the one Windows program the title runs itself (docs/TITLE_CLIENT.md, graphics patch 0031). */
int proton_gpu_broker_open_in_owner(int *cookie);
void proton_gpu_broker_close(int cookie,int service);
bool proton_gpu_broker_stop();
/* The desktop (docs/WINDOWS_PROCESSES.md, "The desktop"): the server's foreground process and cursor, and helpers'
 * frame pages (src/pw_frame_page.h), which attach owns the descriptor of. */
void proton_gpu_broker_set_desktop(int (*foreground)(),void (*cursor)(int *,int *));
void proton_gpu_broker_set_windows(int (*snapshot)(void *,unsigned));
int proton_gpu_broker_window_attach(int pid,int fd,size_t bytes);
void proton_gpu_broker_frame_attach(int pid,int fd);
void proton_gpu_broker_frame_detach(int pid);
/* Frames the broker's display has shown, and a picture of the next one (written to path): a pad script's clock and
 * camera when the program presents from a helper (the launcher layout). */
unsigned long long proton_gpu_broker_flips();
/* The rate the broker's display kept, 120 or 60, opening it now if no present has (0: not open); asked right after the
 * broker starts, before any helper, so WINE_PS5_REFRESH says the truth (graphics patch 0029, src/pw_display_rate.h). */
unsigned proton_gpu_broker_display_hz();
/* The native pid whose full-screen frames the broker flips directly (game mode), or 0. */
int proton_gpu_broker_game_mode();
bool proton_gpu_broker_capture_next(const char *path);
/* The guided setup's cover, shown instead of the desktop (graphics patch 0030; native/proton_loading.h). */
bool proton_gpu_broker_set_cover(const void *pixels);
/* The broker's locks and GPU fences as of now, without waiting on either lock (graphics patch 0012), and the call the
 * client lock's holder waits in, for how long, on which thread (patch 0017; phase null: none named): the flight
 * recorder's (native/proton_flight_recorder.h). */
struct ProtonGpuHealth { unsigned lock_busy,display_busy,fences_issued,fences_completed;const char *phase;unsigned long long phase_ms;int holder_tid; };
void proton_gpu_broker_health(ProtonGpuHealth *health);
/* The app is away from the foreground (src/pw_suspend.h): the broker drains and holds new GPU work (graphics patch 0038). */
void proton_gpu_broker_set_away(bool away);
/* Where the broker gets the memory it shares with a helper (a buffer's object, and its pages written through before the
 * broker maps it for the GPU): the title's memory helper. Unset, the broker makes it itself. */
void proton_gpu_broker_set_shared_memory(int (*alloc)(size_t),int (*touch)(int,size_t,size_t));
// Host-visible client buffers from direct memory the memory helper owns and grants (patch 0009); unset: shared-memory objects.
void proton_gpu_broker_set_dmem_memory(int (*alloc)(size_t,int,int64_t *),int (*release)(int64_t,size_t));
#endif
