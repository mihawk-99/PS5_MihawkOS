/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_release.h"

#include "../external/monocypher/monocypher-ed25519.h"

#include <stdlib.h>
#include <string.h>

#define SIGNATURE_LINE (10 + 128 + 1)   /* "signature " + 64 bytes in hex + '\n' */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_bytes(const char *hex, size_t count, uint8_t *out)
{
    for (size_t i = 0; i < count; i++) {
        const int high = hex_value(hex[2 * i]), low = high < 0 ? -1 : hex_value(hex[2 * i + 1]);
        if (low < 0) return 0;
        out[i] = (uint8_t)(high << 4 | low);
    }
    return 1;
}

int pw_release_hex32(const char *hex, uint8_t out[32])
{
    return hex && strlen(hex) == 64 && hex_bytes(hex, 32, out);
}

/* The next line of [*cursor, end) into `line` as a string: 0 when there is none, it is too long or holds a NUL. */
static int next_line(const char **cursor, const char *end, char *line, size_t size)
{
    const char *newline = memchr(*cursor, '\n', (size_t)(end - *cursor));
    const size_t length = newline ? (size_t)(newline - *cursor) : 0;
    if (!newline || length >= size || memchr(*cursor, '\0', length)) return 0;
    memcpy(line, *cursor, length);
    line[length] = '\0';
    *cursor = newline + 1;
    return 1;
}

/* A release asset's file name: letters, digits, '.', '_', '-', not starting with '.'. */
static int plain_name(const char *name)
{
    if (!*name || *name == '.' || strlen(name) >= sizeof(((PwRelease *)0)->title)) return 0;
    for (const char *c = name; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' ||
              *c == '_' || *c == '-'))
            return 0;
    return 1;
}

static int base_url(const char *url)
{
    const size_t length = strlen(url);
    if ((strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) || length < 10 || url[length - 1] != '/' ||
        length >= sizeof(((PwRelease *)0)->base))
        return 0;
    for (const char *c = url; *c; c++)
        if (*c <= 0x20 || *c >= 0x7f || *c == '"' || *c == '\'' || *c == '\\') return 0;
    return 1;
}

/* Digits in groups of at most 9, separated by single dots. */
static int version_ok(const char *version)
{
    size_t digits = 0;
    if (!*version || strlen(version) >= sizeof(((PwRelease *)0)->version)) return 0;
    for (const char *c = version; *c; c++) {
        if (*c == '.') {
            if (!digits) return 0;
            digits = 0;
        } else if (*c >= '0' && *c <= '9') {
            if (++digits > 9) return 0;
        } else {
            return 0;
        }
    }
    return digits > 0;
}

/* "<name> <64 hex> <file>" into its digest and file name. */
static int asset(const char *line, const char *name, uint8_t digest[32], char *file)
{
    const size_t length = strlen(name);
    if (strncmp(line, name, length) || line[length] != ' ' || strlen(line) < length + 1 + 64 + 2 ||
        line[length + 1 + 64] != ' ' || !hex_bytes(line + length + 1, 32, digest) || !plain_name(line + length + 66))
        return 0;
    strcpy(file, line + length + 66);
    return 1;
}

int pw_release_parse(const char *text, size_t size, const uint8_t public_key[32], PwRelease *out)
{
    memset(out, 0, sizeof(*out));
    if (!text || size < SIGNATURE_LINE || size > 16384 || text[size - 1] != '\n') return PW_RELEASE_FORMAT;
    const char *signature_line = text + size - SIGNATURE_LINE;
    uint8_t signature[64];
    if ((signature_line != text && signature_line[-1] != '\n') || memcmp(signature_line, "signature ", 10) ||
        !hex_bytes(signature_line + 10, 64, signature))
        return PW_RELEASE_FORMAT;
    if (crypto_ed25519_check(signature, public_key, (const uint8_t *)text, (size_t)(signature_line - text)) != 0)
        return PW_RELEASE_SIGNATURE;
    /* Signed: now its fields, every one of them, in their order, and nothing else. */
    char line[640];
    const char *cursor = text;
    PwRelease release;
    memset(&release, 0, sizeof(release));
    if (!next_line(&cursor, signature_line, line, sizeof(line)) || strcmp(line, "mihawkos-release 1") ||
        !next_line(&cursor, signature_line, line, sizeof(line)) || strncmp(line, "version ", 8) ||
        !version_ok(line + 8))
        return PW_RELEASE_FORMAT;
    strcpy(release.version, line + 8);
    if (!next_line(&cursor, signature_line, line, sizeof(line)) || strncmp(line, "base ", 5) || !base_url(line + 5))
        return PW_RELEASE_FORMAT;
    strcpy(release.base, line + 5);
    if (!next_line(&cursor, signature_line, line, sizeof(line)) ||
        !asset(line, "title", release.title_sha256, release.title) ||
        !next_line(&cursor, signature_line, line, sizeof(line)) ||
        !asset(line, "runtime", release.runtime_sha256, release.runtime) || cursor != signature_line)
        return PW_RELEASE_FORMAT;
    *out = release;
    return PW_RELEASE_OK;
}

int pw_release_version_compare(const char *a, const char *b)
{
    const int good_a = a && version_ok(a), good_b = b && version_ok(b);
    if (!good_a || !good_b) return good_a - good_b;
    while (*a || *b) {   /* a missing group counts as 0: "1.2" is "1.2.0" */
        char *next;
        unsigned long left = 0, right = 0;
        if (*a) left = strtoul(a, &next, 10), a = next + (*next == '.');
        if (*b) right = strtoul(b, &next, 10), b = next + (*next == '.');
        if (left != right) return left < right ? -1 : 1;
    }
    return 0;
}

/* "v7.1.0-alpha-8" into its numbers; 0 when it does not read */
static int tag_read(const char *tag, unsigned long out[5])
{
    static const char *const stages[] = { "alpha", "beta", "rc" };
    char *end;
    if (!tag) return 0;
    if (*tag == 'v' || *tag == 'V') tag++;
    for (int k = 0; k < 3; ++k) {
        if (*tag < '0' || *tag > '9') return 0;
        out[k] = strtoul(tag, &end, 10);
        tag = end;
        if (k < 2) {
            if (*tag != '.') return 0;
            tag++;
        }
    }
    out[3] = 3;   /* a release, after every stage */
    out[4] = 0;
    if (!*tag) return 1;
    if (*tag != '-') return 0;
    tag++;
    for (unsigned long s = 0; s < 3; ++s) {
        const size_t n = strlen(stages[s]);
        if (strncmp(tag, stages[s], n)) continue;
        tag += n;
        if (*tag == '-' || *tag == '.') tag++;
        if (*tag < '0' || *tag > '9') return 0;
        out[3] = s;
        out[4] = strtoul(tag, &end, 10);
        return !*end;
    }
    return 0;
}

int pw_release_tag_compare(const char *a, const char *b)
{
    unsigned long x[5], y[5];
    const int good_a = tag_read(a, x), good_b = tag_read(b, y);
    if (!good_a || !good_b) return good_a - good_b;
    for (int k = 0; k < 5; ++k)
        if (x[k] != y[k]) return x[k] < y[k] ? -1 : 1;
    return 0;
}

int pw_release_pick(const PwReleaseEntry *list, size_t count, const char *running)
{
    int best = -1;
    for (size_t i = 0; i < count; ++i) {
        const PwReleaseEntry *e = &list[i];
        if (e->draft || !e->manifest_url || !*e->manifest_url || pw_release_tag_compare(e->tag, running) <= 0) continue;
        if (best < 0 || pw_release_tag_compare(e->tag, list[best].tag) > 0) best = (int)i;
    }
    return best;
}
