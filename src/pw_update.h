/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_UPDATE_H
#define PW_UPDATE_H

/* The switch to a new MihawkOS title (docs/SELF_INSTALL.md, phase 8). The running eboot.bin is never written: the new
 * title is installed whole into a staging folder first, every file checked by its SHA-256 (src/pw_pack.h, a title
 * pack), and moved into the title folder at the next start, before anything else runs, followed by a restart into it
 * through sceSystemServiceLoadExec (rule 2). Under the update folder (/data/ps5proton/update):
 *
 *   stage/       the new title, with its record title-pack.txt (the manifest it was installed from)
 *   ready        written once the stage is whole: the new version; the next start switches
 *   switching    `ready`, renamed as the switch begins: a switch cut short resumes from where it stopped
 *   previous/    every file the switch replaced or removed, kept until the new title has started once
 *   switched     `switching`, renamed once every file is in place: the new version, until pw_update_confirm
 *
 * The switch moves (rename(2), so each file is either the old one or the new one) the new title's files in path
 * order, then moves aside the files the old record lists and the new one does not, then the record, then eboot.bin
 * and sce_sys/param.json last, as the install writes them: ShadowMountPlus never sees half a title. Every file it
 * replaces goes to previous/ first. Only files a record lists are touched: the title folder's own state (markers,
 * settings written beside the eboot) stays. The stage and the title folder are on one file system (/data).
 * shortcut: no automatic rollback; a new title that never draws its launcher is undone by hand (previous/ copied back
 * over the title folder, or an older release's folder copied in): add a start counter if one ever ships broken.
 * tests/test_pw_update.c checks the switch, a switch cut at each step and resumed, and the confirm. */

#include <stddef.h>
#include <stdint.h>
#include "pw_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The automatic update check (Settings > Automatic update check, on unless turned off) is due: on, and none in the
 * last day. last 0: never checked; a clock behind `last` (a console clock set back) counts as due. */
#define PW_UPDATE_CHECK_EVERY_S (24 * 60 * 60)
int pw_update_check_due(int enabled, int64_t now, int64_t last);

/* A release zip into the stage (the automatic update, 2026-10-31; src/pw_update_flow.h). The zip's own SHA-256 is
 * checked against the signed manifest before this runs, so its files are trusted as they come; this adds:
 * - only the files under `prefix` ("PPSA99170/") are taken, at their path below it; the rest (SHA256SUMS,
 *   README.txt) is left, and a title-pack.txt in the zip is skipped (the record is written here);
 * - the title must have eboot.bin and sce_sys/param.json (PW_UPDATE_NOT_A_TITLE), and its runtime-pack.txt the
 *   SHA-256 the signed manifest pins (PW_UPDATE_RUNTIME_PIN), so the runtime the title installs is the signed one;
 * - the stage is emptied first, its record written (pw_update_record_write), then `ready` with `version`.
 * 0, a PW_ZIP_* status, or a PW_UPDATE_* one below; the path it stopped at in `failed`. */
enum { PW_UPDATE_NOT_A_TITLE = -30, PW_UPDATE_RUNTIME_PIN = -31, PW_UPDATE_STAGE = -32 };
int pw_update_install_zip(const char *zip_path, const char *update, const char *prefix, const uint8_t runtime_sha256[32],
                          const char *version, pw_zip_inflate_fn inflate, char *failed, size_t size);
/* One sentence for a status pw_update_install_zip returns. */
const char *pw_update_status_text(int status);

/* One file of a new title, as unpacked. */
typedef struct PwUpdateFile {
    const char *path;
    uint64_t size;
    uint8_t sha256[32];
} PwUpdateFile;
/* `stage`/title-pack.txt for these files: pw_pack's title manifest, its file lines in path order with runtime-stamp
 * (when there is one), eboot.bin then sce_sys/param.json last, under one synthetic chunk line (the switch reads only the paths; no chunk is fetched).
 * The array is sorted in place. 0 or -errno (-EINVAL without both stamps or with a path twice). */
int pw_update_record_write(const char *stage, PwUpdateFile *files, size_t count);

/* The record a title pack's install leaves in its root: the manifest, byte for byte (src/pw_pack.h). */
#define PW_UPDATE_RECORD "title-pack.txt"

/* Write `ready` (through a part file, fsync'd, renamed): 0 or -errno. */
int pw_update_mark_ready(const char *update, const char *version);
/* 1 when a switch is due (`ready`) or was cut short (`switching`), with its version; 0 when none is. */
int pw_update_pending(const char *update, char *version, size_t size);
/* The switch, when one is due: 1 when it ended (the caller restarts into the new title), 0 when none was due, or
 * -errno with the path that failed in `failed`. Run again after a failure or a cut, it goes on from where it stopped. */
int pw_update_switch(const char *update, const char *title, char *failed, size_t size);
/* The new title has started once: previous/ and `switched` are removed. 1 when there was a switch to confirm, 0 when
 * not, -errno when previous/ could not be removed. */
int pw_update_confirm(const char *update);
/* A due switch dropped (the title is as new already): `ready` and the stage (a whole release, ~0.6 GB) removed. A switch
 * cut short is never dropped. 0 or -errno. */
int pw_update_discard(const char *update);

#ifdef __cplusplus
}
#endif

#endif
