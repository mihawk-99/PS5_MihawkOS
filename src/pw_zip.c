/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A release zip's reader (pw_zip.h). */
#include "pw_zip.h"
#include "pw_sha256.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t pw_zip_crc32(uint32_t crc, const void *data, size_t size)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    const uint8_t *p = data;
    crc = ~crc;
    while (size--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

const char *pw_zip_status_text(int status)
{
    switch (status) {
    case PW_ZIP_OK: return "ok";
    case PW_ZIP_FORMAT: return "the update's archive is not one this version can read";
    case PW_ZIP_PATH: return "the update's archive names a file outside its folder";
    case PW_ZIP_METHOD: return "the update's archive uses a compression this version cannot read";
    case PW_ZIP_CRC: return "a file in the update's archive is damaged";
    case PW_ZIP_DECODE: return "a file in the update's archive could not be unpacked";
    case PW_ZIP_WRITE: return "the update could not be written to the console's storage";
    case PW_ZIP_MEMORY: return "there was not enough memory to unpack the update";
    case PW_ZIP_TOO_LARGE: return "a file in the update's archive is too large to unpack";
    default: return "the update's archive could not be read";
    }
}

int pw_zip_plain_path(const char *name)
{
    if (!name || !*name || *name == '/') return 0;
    for (const char *part = name; *part;) {
        const char *end = strchr(part, '/');
        const size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length && end && !end[1]) break;   /* a folder entry's trailing '/' */
        if (!length || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.')) return 0;
        for (size_t i = 0; i < length; ++i)
            if ((unsigned char)part[i] < 0x20 || part[i] == '\\' || part[i] == 0x7f) return 0;
        if (!end) break;
        part = end + 1;
    }
    return 1;
}

int pw_zip_open(FILE *file, PwZip *out)
{
    memset(out, 0, sizeof(*out));
    /* The end record: in the last 22 + 65535 bytes */
    if (fseeko(file, 0, SEEK_END)) return PW_ZIP_FORMAT;
    const off_t size = ftello(file);
    if (size < 22) return PW_ZIP_FORMAT;
    const size_t tail = (size_t)(size < 22 + 65535 ? size : 22 + 65535);
    uint8_t *buffer = malloc(tail);
    if (!buffer) return PW_ZIP_MEMORY;
    if (fseeko(file, size - (off_t)tail, SEEK_SET) || fread(buffer, 1, tail, file) != tail) {
        free(buffer);
        return PW_ZIP_FORMAT;
    }
    const uint8_t *end = NULL;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (u32(buffer + i) == 0x06054b50u && i + 22 + u16(buffer + i + 20) == tail) {
            end = buffer + i;
            break;
        }
    if (!end || u16(end + 4) || u16(end + 6) || u16(end + 8) != u16(end + 10)) {   /* one disk only */
        free(buffer);
        return PW_ZIP_FORMAT;
    }
    const size_t count = u16(end + 10);
    const uint32_t directory_size = u32(end + 12), directory = u32(end + 16);
    free(buffer);
    if (count == 0xffff || directory == 0xffffffffu || (off_t)directory + directory_size > size) return PW_ZIP_FORMAT;
    uint8_t *dir = malloc(directory_size ? directory_size : 1);
    out->entries = calloc(count ? count : 1, sizeof(*out->entries));
    out->names = malloc(directory_size + 1);
    if (!dir || !out->entries || !out->names) {
        free(dir);
        pw_zip_close(out);
        return PW_ZIP_MEMORY;
    }
    if (fseeko(file, directory, SEEK_SET) || fread(dir, 1, directory_size, file) != directory_size) {
        free(dir);
        pw_zip_close(out);
        return PW_ZIP_FORMAT;
    }
    size_t at = 0, names = 0;
    int status = PW_ZIP_OK;
    for (size_t e = 0; e < count && !status; ++e) {
        if (at + 46 > directory_size || u32(dir + at) != 0x02014b50u) { status = PW_ZIP_FORMAT; break; }
        const uint8_t *h = dir + at;
        const uint16_t flags = u16(h + 8), length = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
        PwZipEntry *entry = &out->entries[e];
        entry->method = u16(h + 10);
        entry->crc = u32(h + 16);
        entry->packed = u32(h + 20);
        entry->size = u32(h + 24);
        entry->local = u32(h + 42);
        if ((flags & 1) || entry->packed == 0xffffffffu || entry->size == 0xffffffffu || entry->local == 0xffffffffu ||
            at + 46 + length + extra + comment > directory_size) { status = PW_ZIP_FORMAT; break; }
        memcpy(out->names + names, h + 46, length);
        out->names[names + length] = 0;
        entry->name = out->names + names;   /* the names buffer is allocated once: the pointers stay valid */
        names += length + 1;
        if (memchr(h + 46, 0, length) || !pw_zip_plain_path(entry->name)) status = PW_ZIP_PATH;
        entry->folder = length && entry->name[length - 1] == '/';
        at += 46 + length + extra + comment;
        out->count++;
    }
    free(dir);
    if (status) pw_zip_close(out);
    return status;
}

void pw_zip_close(PwZip *zip)
{
    free(zip->entries);
    free(zip->names);
    memset(zip, 0, sizeof(*zip));
}

/* The entry's data: past its local header, whose own name and extra lengths may differ from the directory's. */
static int seek_data(FILE *file, const PwZipEntry *entry)
{
    uint8_t h[30];
    if (fseeko(file, (off_t)entry->local, SEEK_SET) || fread(h, 1, 30, file) != 30 || u32(h) != 0x04034b50u)
        return PW_ZIP_FORMAT;
    return fseeko(file, (off_t)(entry->local + 30 + u16(h + 26) + u16(h + 28)), SEEK_SET) ? PW_ZIP_FORMAT : PW_ZIP_OK;
}

int pw_zip_extract(FILE *file, const PwZipEntry *entry, const char *path, pw_zip_inflate_fn inflate, uint8_t sha256[32])
{
    if (entry->folder) return PW_ZIP_OK;
    if (entry->method != 0 && entry->method != 8) return PW_ZIP_METHOD;
    if (entry->method == 0 && entry->packed != entry->size) return PW_ZIP_FORMAT;
    if (entry->method == 8 && (entry->size > PW_ZIP_MAX_DEFLATED || entry->packed > PW_ZIP_MAX_DEFLATED)) return PW_ZIP_TOO_LARGE;
    int status = seek_data(file, entry);
    if (status) return status;
    const size_t part_size = strlen(path) + sizeof(".mihawkos-part");
    char *part = malloc(part_size);
    if (!part) return PW_ZIP_MEMORY;
    snprintf(part, part_size, "%s.mihawkos-part", path);
    FILE *to = fopen(part, "wb");
    if (!to) {
        free(part);
        return PW_ZIP_WRITE;
    }
    PwSha256 hash;
    pw_sha256_init(&hash);
    uint32_t crc = 0;
    if (entry->method == 0) {
        uint8_t *buffer = malloc(1u << 20);
        uint64_t left = entry->size;
        if (!buffer) status = PW_ZIP_MEMORY;
        while (!status && left) {
            const size_t want = left < (1u << 20) ? (size_t)left : (1u << 20);
            if (fread(buffer, 1, want, file) != want) status = PW_ZIP_FORMAT;
            else if (fwrite(buffer, 1, want, to) != want) status = PW_ZIP_WRITE;
            else {
                crc = pw_zip_crc32(crc, buffer, want);
                pw_sha256_update(&hash, buffer, want);
                left -= want;
            }
        }
        free(buffer);
    } else {
        uint8_t *in = malloc(entry->packed ? (size_t)entry->packed : 1), *plain = malloc(entry->size ? (size_t)entry->size : 1);
        if (!in || !plain) status = PW_ZIP_MEMORY;
        else if (fread(in, 1, (size_t)entry->packed, file) != entry->packed) status = PW_ZIP_FORMAT;
        else if (!inflate || inflate(in, (size_t)entry->packed, plain, (size_t)entry->size)) status = PW_ZIP_DECODE;
        else if (entry->size && fwrite(plain, 1, (size_t)entry->size, to) != entry->size) status = PW_ZIP_WRITE;
        else {
            crc = pw_zip_crc32(0, plain, (size_t)entry->size);
            pw_sha256_update(&hash, plain, (size_t)entry->size);
        }
        free(in);
        free(plain);
    }
    if (!status && crc != entry->crc) status = PW_ZIP_CRC;
    if (!status && (fflush(to) || fsync(fileno(to)))) status = PW_ZIP_WRITE;
    const int saved = errno;
    if (fclose(to) && !status) status = PW_ZIP_WRITE;
    if (!status && rename(part, path)) status = PW_ZIP_WRITE;
    if (status) {
        remove(part);
        errno = saved;
    } else if (sha256) {
        pw_sha256_final(&hash, sha256);
    }
    free(part);
    return status;
}
