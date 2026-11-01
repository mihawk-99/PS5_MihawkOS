/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_RELEASE_H
#define PW_RELEASE_H

/* The signed release manifest MihawkOS updates itself from (docs/SELF_INSTALL.md, phase 8). A short text, one field a
 * line, in this order, each line ending in '\n':
 *
 *   mihawkos-release 1
 *   version 01.001.000          the new title's contentVersion; an update must be newer than the running title
 *   base https://.../v1.1/      where the release's assets are: its own tag's folder, never a "latest" URL
 *   title <sha256> <name>       the title pack's manifest (tools/pack-runtime.py --title-pack), base + name
 *   runtime <sha256> <name>     the runtime pack's manifest the new title pins (runtime-pack.txt), base + name
 *   signature <128 hex digits>  Ed25519 (RFC 8032) over every byte before this line
 *
 * tools/sign-release.py writes it with the owner's key, which never leaves the owner's PC; the title carries the
 * public key. The signature is checked before any field is read, so nothing an unsigned manifest says is acted on:
 * the files it names are then checked by their SHA-256 (src/pw_pack.h). Replaying an older signed manifest is refused
 * by the version. The check is Monocypher's crypto_ed25519_check (external/monocypher). tests/test_pw_release.c and
 * tests/test_sign_release.py (OpenSSL's signatures) check it. Portable, no allocation. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PW_RELEASE_OK = 0,
    PW_RELEASE_FORMAT = -1,      /* not a release manifest, or a field out of shape */
    PW_RELEASE_SIGNATURE = -2,   /* not signed by this build's key */
};

typedef struct PwRelease {
    char version[24];
    char base[512];
    char title[128], runtime[128];
    uint8_t title_sha256[32], runtime_sha256[32];
} PwRelease;

/* Check `text` against `public_key`, then read it. */
int pw_release_parse(const char *text, size_t size, const uint8_t public_key[32], PwRelease *out);

/* Dotted versions ("01.001.000") compared number by number: <0, 0 or >0. A malformed one compares below every
 * well-formed one. */
int pw_release_version_compare(const char *a, const char *b);

/* A release tag's version, as GitHub's releases name them ("v7.1.0-alpha-8", "6.0.0", "v8.0.0-rc-2"): X.Y.Z, then
 * the stage (alpha < beta < rc < none), then its number. <0, 0 or >0; a tag that does not read compares below every
 * one that does, so it is never taken as newer. */
int pw_release_tag_compare(const char *a, const char *b);

/* One release of a listing (GitHub's /releases, newest first or not). */
typedef struct PwReleaseEntry {
    const char *tag;
    int draft;
    const char *manifest_url;   /* its mihawkos-release.txt asset's download address; NULL or "" when it has none */
} PwReleaseEntry;
/* The release to offer: the newest by tag, not a draft, with a signed manifest, and newer than `running` (the
 * build's VERSION). Its index, or -1 when none is. Pre-releases count: MihawkOS's releases are all pre-releases. */
int pw_release_pick(const PwReleaseEntry *list, size_t count, const char *running);

/* 64 hex digits into 32 bytes (the build's public key, a manifest's digests): 1 when it is exactly that. */
int pw_release_hex32(const char *hex, uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif
