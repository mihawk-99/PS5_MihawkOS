/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_UPDATE_FLOW_H
#define PW_UPDATE_FLOW_H
/* The automatic update's steps (the owner, 2026-10-31: "fetch the latest release from Github and update should be
 * seamless"), shared by the title (ps5/src/proton_update.cpp) and the PC rehearsal (tests/test_update_rehearsal.py),
 * so what is proven on the PC is what runs on the console. Each takes the way to fetch a URL.
 *
 * 1. pw_update_find: GitHub's release listing (or a set address), the newest signed release newer than this build
 *    (pw_release_pick), its signed manifest checked with the build's key (pw_release_parse) and its version newer than
 *    the running title's contentVersion (no replay). The manifest's title line names the release zip and its SHA-256;
 *    its runtime line the SHA-256 the title folder's runtime-pack.txt must have.
 * 2. pw_update_take: the zip downloaded to <update>/download.zip (a part file first, its SHA-256 as it streams, against
 *    the signed one), unpacked into the stage (pw_update_install_zip), `ready` written, the zip removed. The switch is
 *    pw_update.h's, at the next start. Any failure leaves the running title untouched and the stage removed; a partial
 *    download is not kept.
 * Each returns "" or one sentence for the screen. */
#include <cstdint>
#include <functional>
#include <string>
#include "pw_zip.h"

/* GET url, its body to sink (false from sink stops); "" or the failure in plain words. More than limit bytes fails. */
using PwUpdateGet = std::function<std::string(const std::string &url, uint64_t limit,
                                              const std::function<bool(const void *, size_t)> &sink)>;

struct PwUpdateOffer {
    bool found{ false };
    std::string tag;          /* the GitHub release's */
    std::string version;      /* the signed manifest's (the new title's contentVersion) */
    std::string zip_url;
    uint64_t zip_size{ 0 };   /* from the listing; 0 when unknown */
    uint8_t zip_sha256[32]{};
    uint8_t runtime_sha256[32]{};
};

/* releases_url: GitHub's listing; manifest_url: a set address instead (a rehearsal), or "". build_tag: VERSION.
 * running: the title's contentVersion. key: the build's public key. "" with offer.found false: nothing newer. */
std::string pw_update_find(const PwUpdateGet &get, const std::string &releases_url, const std::string &manifest_url,
                           const char *build_tag, const char *running, const uint8_t key[32], PwUpdateOffer &offer);
/* Download, check and stage `offer` under `update`; progress(bytes so far) as it downloads. */
std::string pw_update_take(const PwUpdateGet &get, const PwUpdateOffer &offer, const std::string &update,
                           pw_zip_inflate_fn inflate, const std::function<void(uint64_t)> &progress);
/* The release zip's top folder: the title folder, as copied to /data/homebrew. */
#define PW_UPDATE_ZIP_PREFIX "PPSA99170/"
#endif
