/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SHORTCUT_H
#define PW_SHORTCUT_H
/* The pure part of "make a shortcut" in MihawkOS's File Explorer and desktop menus (wine/ps5/pw_shell_shortcut.h writes
 * the file): the shortcut's name and the Start menu's pin list. Text is UTF-16 in 16-bit units (Wine's WCHAR), so the
 * shell code and tests/test_pw_shortcut.c share it. Header-only: Wine's Explorer includes a copy.
 *
 * Name: "<item> - Shortcut.lnk", then "<item> - Shortcut (2).lnk" and on, as Windows names a shortcut made beside its
 * target. The item is the last part of the path with a file's extension taken off and the characters a file name
 * cannot hold dropped; a drive's root ("C:\") is the drive's letter. The " - Shortcut" suffix also keeps the file
 * apart from the shortcuts the title owns on the Public Desktop (src/pw_shell_link.h: it claims "<game>.lnk" and
 * would refuse to overwrite a person's file of the same name).
 *
 * Pins: the Start menu keeps its pinned programs as a registry REG_MULTI_SZ ("StartPins", wine/ps5/pw_shell_start.c):
 * paths, each ended by a NUL, the list ended by a second one. pw_shortcut_pins_add appends one the list lacks (paths
 * compare without regard to ASCII case, as Windows' do). The list is user-editable: input that is not whole (no end,
 * a string longer than max_path) is refused, not guessed at. */
#include <stddef.h>

typedef unsigned short pw_u16;

enum { PW_SHORTCUT_NAME_MAX = 260, PW_SHORTCUT_PINS_MAX = 18, PW_SHORTCUT_PATH_MAX = 260 };

static inline int pw_shortcut_illegal(pw_u16 c)
{
    return c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
}

/* Writes the file name for target's attempt-th shortcut (1, 2, ...) into out (cap units). Returns its length, or 0
 * when target gives no name or cap is too small. is_folder: no extension is taken off a folder's name. */
static inline size_t pw_shortcut_name(pw_u16 *out, size_t cap, const pw_u16 *target, int is_folder, unsigned attempt)
{
    static const char suffix[] = " - Shortcut", extension[] = ".lnk";
    pw_u16 item[PW_SHORTCUT_NAME_MAX];
    char number[16] = "";
    size_t length = 0, end = 0, begin, n = 0, digits = 0;
    if (!out || !cap || !target || !attempt) return 0;
    while (target[end]) ++end;
    while (end && (target[end - 1] == '\\' || target[end - 1] == '/')) --end;
    for (begin = end; begin && target[begin - 1] != '\\' && target[begin - 1] != '/'; --begin) {}
    if (end - begin == 2 && target[begin + 1] == ':') end = begin + 1;   /* "C:" is the drive C */
    if (!is_folder)
        for (size_t dot = end; dot > begin + 1; --dot)
            if (target[dot - 1] == '.') { end = dot - 1; break; }
    for (size_t i = begin; i < end && length < PW_SHORTCUT_NAME_MAX - 1; ++i)
        if (!pw_shortcut_illegal(target[i])) item[length++] = target[i];
    while (length && (item[length - 1] == ' ' || item[length - 1] == '.')) --length;
    if (!length) return 0;
    if (attempt > 1) {
        char reversed[12];
        unsigned value = attempt;
        while (value) reversed[digits++] = (char)('0' + value % 10), value /= 10;
        size_t at = 0;
        number[at++] = ' ', number[at++] = '(';
        while (digits) number[at++] = reversed[--digits];
        number[at++] = ')', number[at] = 0;
    }
    size_t number_length = 0;
    while (number[number_length]) ++number_length;
    if (length + (sizeof(suffix) - 1) + number_length + (sizeof(extension) - 1) + 1 > cap) return 0;
    for (size_t i = 0; i < length; ++i) out[n++] = item[i];
    for (size_t i = 0; suffix[i]; ++i) out[n++] = (pw_u16)suffix[i];
    for (size_t i = 0; number[i]; ++i) out[n++] = (pw_u16)number[i];
    for (size_t i = 0; extension[i]; ++i) out[n++] = (pw_u16)extension[i];
    out[n] = 0;
    return n;
}

static inline int pw_shortcut_same_path(const pw_u16 *a, const pw_u16 *b)
{
    for (;; ++a, ++b) {
        pw_u16 x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (pw_u16)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (pw_u16)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* units: the list's length in 16-bit units, including its two NULs (0 or 1 for a missing or empty value). On return
 * the list is in multisz (capacity cap units) with path appended when it lacked it. Returns 1 when path was added, 0
 * when it was there already, -1 when the list is not whole or is full (PW_SHORTCUT_PINS_MAX), -2 when path is empty,
 * too long or there is no room. *units is the new length. */
static inline int pw_shortcut_pins_add(pw_u16 *multisz, size_t cap, size_t *units, const pw_u16 *path)
{
    size_t used = 0, count = 0, length = 0;
    while (path && path[length]) ++length;
    if (!length || length >= PW_SHORTCUT_PATH_MAX) return -2;
    if (*units >= 2) {
        if (*units > cap || multisz[*units - 1] || multisz[*units - 2]) return -1;   /* not ended by two NULs */
        while (used < *units - 1 && multisz[used]) {
            size_t each = 0;
            while (used + each < *units && multisz[used + each]) ++each;
            if (used + each >= *units || each >= PW_SHORTCUT_PATH_MAX) return -1;
            if (pw_shortcut_same_path(multisz + used, path)) return 0;
            used += each + 1;
            ++count;
        }
    }
    if (count >= PW_SHORTCUT_PINS_MAX) return -1;
    if (used + length + 2 > cap) return -2;
    for (size_t i = 0; i < length; ++i) multisz[used + i] = path[i];
    multisz[used + length] = 0;
    multisz[used + length + 1] = 0;
    *units = used + length + 2;
    return 1;
}
#endif
