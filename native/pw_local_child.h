/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_LOCAL_CHILD_H
#define PW_LOCAL_CHILD_H
#include <stdint.h>

/* Handshake from /app0/wine-local-child.bin over fd 3 once Lapy elevation has
 * completed (euid == 0, full capabilities, host root). The title waits for this
 * packet while holding its local-spawn mutex so concurrent local children never
 * race on /download0/elevate_proc. */
#define PW_LOCAL_HELLO_MAGIC 0x50574c43u /* "PWLC" */

struct pw_local_hello {
    uint32_t magic;
    int32_t pid;
    int32_t status;
    int32_t euid;
};

/* Layout for sceSystemServiceAddLocalProcess options (72 bytes, matching
 * native/proton_service_preflight.cpp). */
struct pw_local_spawn_options {
    uint32_t size;
    int32_t fd;
    int32_t crash_report;
    uint32_t other[15];
};
#endif
