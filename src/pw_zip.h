/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_ZIP_H
#define PW_ZIP_H
/* A release zip's reader, for the update (src/pw_update_flow.h): the end record and central directory of a plain zip
 * (no zip64, no encryption, no multi-disk; MihawkOS's release zip is about 0.6 GB), and one entry at a time extracted
 * to a file. An entry is stored (method 0, as the runtime pieces are, being zstd already) or deflated (method 8).
 * Deflate is the caller's (`inflate`): the title passes stb_image's, which it carries already, so no zlib is added.
 *
 * The rules, checked by tests/test_update_rehearsal.py:
 * - a name is a plain relative path: no leading '/', no "..", '.' or empty part, no '\\', no control character;
 *   anything else is PW_ZIP_PATH, and nothing is written;
 * - an entry is written to <path>.mihawkos-part, checked (its size and CRC-32), fsync'd, and renamed into place;
 * - a deflated entry is decoded in memory, so one larger than PW_ZIP_MAX_DEFLATED is refused (the release's largest,
 *   eboot.bin, is about 35 MB); a stored one streams.
 * Portable C over stdio; no allocation beyond the directory and one deflated entry. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PW_ZIP_OK = 0,
    PW_ZIP_FORMAT = -1,      /* not a zip this reader takes (zip64, encrypted, multi-disk, truncated) */
    PW_ZIP_PATH = -2,        /* a name that is not a plain relative path */
    PW_ZIP_METHOD = -3,      /* neither stored nor deflated */
    PW_ZIP_CRC = -4,         /* the bytes are not the entry's (damaged) */
    PW_ZIP_DECODE = -5,      /* the deflate stream did not decode to the entry's size */
    PW_ZIP_WRITE = -6,       /* the file could not be written (errno says why; ENOSPC: full) */
    PW_ZIP_MEMORY = -7,
    PW_ZIP_TOO_LARGE = -8,   /* a deflated entry over PW_ZIP_MAX_DEFLATED */
};
#define PW_ZIP_MAX_DEFLATED (256ull << 20)

/* Decode raw deflate (no zlib header) of in_size bytes into exactly out_size: 0, or nonzero when it cannot. */
typedef int (*pw_zip_inflate_fn)(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);

typedef struct PwZipEntry {
    const char *name;        /* points into the directory's names */
    uint64_t local;          /* its local header's offset */
    uint64_t packed, size;
    uint32_t crc;
    uint16_t method;
    int folder;              /* a name ending in '/': a folder entry, nothing to extract */
} PwZipEntry;
typedef struct PwZip {
    PwZipEntry *entries;
    size_t count;
    char *names;
} PwZip;

/* The zip's directory. 0 or PW_ZIP_*; pw_zip_close after 0. */
int pw_zip_open(FILE *file, PwZip *out);
void pw_zip_close(PwZip *zip);
/* 1 when `name` is a plain relative path (the rules above). */
int pw_zip_plain_path(const char *name);
/* Extract `entry` to `path` (its folders are the caller's), with the SHA-256 of what was written in sha256 when not
 * NULL. 0 or PW_ZIP_*. */
int pw_zip_extract(FILE *file, const PwZipEntry *entry, const char *path, pw_zip_inflate_fn inflate, uint8_t sha256[32]);
/* CRC-32 (zip's), continued from `crc` (0 to start). */
uint32_t pw_zip_crc32(uint32_t crc, const void *data, size_t size);
const char *pw_zip_status_text(int status);

#ifdef __cplusplus
}
#endif
#endif
