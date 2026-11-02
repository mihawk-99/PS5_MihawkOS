/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_UPDATE_NOTICE_H
#define PW_UPDATE_NOTICE_H
/* The main menu's update line in the player's language (the owner, 2026-11-01, for 7.1: "Make the progress line be more
 * detailed and user friendly and reflective of its status"): each real step in plain words, the download in MB of the
 * zip's and its percent (only MB when the size is unknown, as from a rehearsal's manifest), the unpack's percent, ready,
 * and why a check or an update failed. ps5/src/proton_update.cpp fills it from the update's state; no console call.
 * Host test: tests/test_pw_update_notice.c. */
#include <stddef.h>
#include <stdint.h>
#include "pw_setup.h"
#include "pw_update.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PW_UPDATE_NOTICE_NONE, PW_UPDATE_NOTICE_CHECKING, PW_UPDATE_NOTICE_AVAILABLE, PW_UPDATE_NOTICE_DOWNLOADING,
    PW_UPDATE_NOTICE_VERIFYING, PW_UPDATE_NOTICE_UNPACKING, PW_UPDATE_NOTICE_READY, PW_UPDATE_NOTICE_FAILED
} PwUpdateNoticePhase;

typedef struct PwUpdateNotice {
    PwUpdateNoticePhase phase;
    const char *version;       /* the release's ("01.001.000"); "" when not known yet */
    uint64_t done, total;      /* bytes downloaded or unpacked, of total (0: unknown) */
    PwUpdateFailure failure;   /* with PW_UPDATE_NOTICE_FAILED */
} PwUpdateNotice;

/* The line, or "" (NONE, or a failure with no kind); its length. */
size_t pw_update_notice_text(const PwSetupLanguage *language, const PwUpdateNotice *notice, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
