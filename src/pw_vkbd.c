/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_vkbd.h"
#include <string.h>

/* Each page's four rows of ten characters, without and with Shift (UTF-8, one character a key) */
static const char *const rows[PW_VKBD_PAGES][2][4] = {
    [PW_VKBD_ABC] = {{"1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm,.-"},
                     {"1234567890", "QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM,.-"}},
    [PW_VKBD_SYMBOLS] = {{"1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\"/\\?<>|~`"},
                         {"1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\"/\\?<>|~`"}},
    [PW_VKBD_ACCENTS] = {{"àáâäãåæçèé", "êëìíîïñòóô", "õöøœßùúûüý", "ÿąćęłńśźż€"},
                         {"ÀÁÂÄÃÅÆÇÈÉ", "ÊËÌÍÎÏÑÒÓÔ", "ÕÖØŒßÙÚÛÜÝ", "ŸĄĆĘŁŃŚŹŻ€"}},
    [PW_VKBD_CYRILLIC] = {{"йцукенгшщз", "хфывапролд", "жэячсмитьб", "юъё.,-!?:;"},
                          {"ЙЦУКЕНГШЩЗ", "ХФЫВАПРОЛД", "ЖЭЯЧСМИТЬБ", "ЮЪЁ.,-!?:;"}},
};
static const char *const names[PW_VKBD_PAGES] = {"abc", "123", "àé", "АБВ"};
/* The last row, in key widths: Shift, the page key, Space, Backspace, Done */
static const PwVkbdKind specials[] = {PW_VKBD_SHIFT, PW_VKBD_PAGE, PW_VKBD_SPACE, PW_VKBD_BACK, PW_VKBD_DONE};
static const float special_units[] = {1.5f, 1.5f, 4.0f, 1.5f, 1.5f};

static size_t utf8_length(unsigned char lead)
{
    return lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
}

/* The n-th character of a row, into out (a string); 0 past its end */
static int nth(const char *row, int n, char *out, size_t capacity)
{
    for (int i = 0; *row; ++i) {
        const size_t length = utf8_length((unsigned char)*row);
        if (i == n) {
            if (length + 1 > capacity) return 0;
            memcpy(out, row, length);
            out[length] = '\0';
            return 1;
        }
        for (size_t k = 0; k < length && *row; ++k) ++row;
    }
    return 0;
}

const char *pw_vkbd_page_name(int page) { return page >= 0 && page < PW_VKBD_PAGES ? names[page] : ""; }

void pw_vkbd_open(PwVkbd *kbd, int page)
{
    memset(kbd, 0, sizeof(*kbd));
    kbd->page = page >= 0 && page < PW_VKBD_PAGES ? page : PW_VKBD_ABC;
    kbd->row = 1;   /* on q, the first letter */
}

int pw_vkbd_row_keys(int row) { return row >= 0 && row < 4 ? PW_VKBD_COLUMNS : row == 4 ? 5 : 0; }

int pw_vkbd_key(const PwVkbd *kbd, int row, int col, PwVkbdKey *out)
{
    if (col < 0 || col >= pw_vkbd_row_keys(row)) return 0;
    memset(out, 0, sizeof(*out));
    out->y = PW_VKBD_KEYS_Y + row * (PW_VKBD_KEY_H + PW_VKBD_ROW_GAP);
    out->h = PW_VKBD_KEY_H;
    if (row < 4) {
        out->kind = PW_VKBD_CHAR;
        out->x = PW_VKBD_KEYS_X + col * (PW_VKBD_KEY_W + PW_VKBD_GAP);
        out->w = PW_VKBD_KEY_W;
        return nth(rows[kbd->page][kbd->shift ? 1 : 0][row], col, out->text, sizeof(out->text));
    }
    float x = PW_VKBD_KEYS_X;
    for (int i = 0; i < col; ++i) x += special_units[i] * PW_VKBD_KEY_W + (special_units[i] - 1) * PW_VKBD_GAP + PW_VKBD_GAP;
    out->kind = specials[col];
    out->x = x;
    out->w = special_units[col] * PW_VKBD_KEY_W + (special_units[col] - 1) * PW_VKBD_GAP;
    if (out->kind == PW_VKBD_PAGE) strcpy(out->text, names[(kbd->page + 1) % PW_VKBD_PAGES]);
    return 1;
}

void pw_vkbd_move(PwVkbd *kbd, int dx, int dy)
{
    const int keys = pw_vkbd_row_keys(kbd->row);
    if (dx) kbd->col = ((kbd->col + dx) % keys + keys) % keys;
    if (dy) {
        PwVkbdKey here;
        pw_vkbd_key(kbd, kbd->row, kbd->col, &here);
        const float centre = here.x + here.w / 2;
        const int row = ((kbd->row + dy) % PW_VKBD_ROWS + PW_VKBD_ROWS) % PW_VKBD_ROWS;
        int best = 0;
        float distance = 1e9f;
        for (int col = 0; col < pw_vkbd_row_keys(row); ++col) {
            PwVkbdKey there;
            pw_vkbd_key(kbd, row, col, &there);
            const float d = centre < there.x ? there.x - centre : centre > there.x + there.w ? centre - there.x - there.w : 0;
            if (d < distance) distance = d, best = col;
        }
        kbd->row = row;
        kbd->col = best;
    }
}

/* One UTF-8 character to UTF-16 (the keys are all in the first plane; others take a surrogate pair) */
static size_t to_utf16(const unsigned char *in, uint16_t out[2], size_t *used)
{
    const size_t length = utf8_length(*in);
    uint32_t c = length == 1 ? in[0] : in[0] & (0x7f >> length);
    for (size_t i = 1; i < length; ++i) {
        if ((in[i] & 0xc0) != 0x80) { *used = i; out[0] = 0xfffd; return 1; }
        c = c << 6 | (in[i] & 0x3f);
    }
    *used = length;
    if (c < 0x10000) { out[0] = (uint16_t)c; return 1; }
    c -= 0x10000;
    out[0] = (uint16_t)(0xd800 | c >> 10);
    out[1] = (uint16_t)(0xdc00 | (c & 0x3ff));
    return 2;
}

int pw_vkbd_type(PwVkbd *kbd, const char *utf8)
{
    uint16_t units[PW_VKBD_TEXT];
    size_t count = 0;
    for (const unsigned char *at = (const unsigned char *)utf8; *at;) {
        uint16_t pair[2];
        size_t used = 0;
        const size_t n = to_utf16(at, pair, &used);
        if (count + n > PW_VKBD_TEXT) break;
        for (size_t i = 0; i < n; ++i) units[count++] = pair[i];
        at += used;
    }
    if (kbd->length + count > PW_VKBD_TEXT) {   /* the field scrolls: the oldest units go, a pair as one */
        size_t drop = kbd->length + count - PW_VKBD_TEXT;
        if (drop < kbd->length && (kbd->text[drop] & 0xfc00) == 0xdc00) ++drop;
        if (drop > kbd->length) drop = kbd->length;
        memmove(kbd->text, kbd->text + drop, (kbd->length - drop) * sizeof(kbd->text[0]));
        kbd->length -= drop;
    }
    memcpy(kbd->text + kbd->length, units, count * sizeof(units[0]));
    kbd->length += count;
    kbd->text[kbd->length] = 0;
    return count ? PW_VKBD_TYPED : PW_VKBD_NONE;
}

int pw_vkbd_backspace(PwVkbd *kbd)
{
    if (kbd->length) {
        --kbd->length;
        if (kbd->length && (kbd->text[kbd->length] & 0xfc00) == 0xdc00 && (kbd->text[kbd->length - 1] & 0xfc00) == 0xd800)
            --kbd->length;   /* a surrogate pair goes as one */
        kbd->text[kbd->length] = 0;
    }
    return PW_VKBD_ERASED;
}

void pw_vkbd_page(PwVkbd *kbd, int step)
{
    kbd->page = ((kbd->page + step) % PW_VKBD_PAGES + PW_VKBD_PAGES) % PW_VKBD_PAGES;
    kbd->shift = 0;
}

static int press_key(PwVkbd *kbd, const PwVkbdKey *key)
{
    switch (key->kind) {
    case PW_VKBD_CHAR: {
        const int typed = pw_vkbd_type(kbd, key->text);
        memcpy(kbd->typed, key->text, sizeof(kbd->typed));
        if (typed == PW_VKBD_TYPED && kbd->shift == 1) kbd->shift = 0;   /* one capital, as a phone's Shift */
        return typed;
    }
    case PW_VKBD_SHIFT: kbd->shift = !kbd->shift; return PW_VKBD_NONE;
    case PW_VKBD_PAGE: pw_vkbd_page(kbd, 1); return PW_VKBD_NONE;
    case PW_VKBD_SPACE: memcpy(kbd->typed, " ", 2); return pw_vkbd_type(kbd, " ");
    case PW_VKBD_BACK: return pw_vkbd_backspace(kbd);
    case PW_VKBD_DONE: return PW_VKBD_FINISHED;
    }
    return PW_VKBD_NONE;
}

int pw_vkbd_press(PwVkbd *kbd)
{
    PwVkbdKey key;
    return pw_vkbd_key(kbd, kbd->row, kbd->col, &key) ? press_key(kbd, &key) : PW_VKBD_NONE;
}

int pw_vkbd_touch(PwVkbd *kbd, float x, float y)
{
    for (int row = 0; row < PW_VKBD_ROWS; ++row)
        for (int col = 0; col < pw_vkbd_row_keys(row); ++col) {
            PwVkbdKey key;
            if (pw_vkbd_key(kbd, row, col, &key) && x >= key.x && x < key.x + key.w && y >= key.y && y < key.y + key.h) {
                kbd->row = row;
                kbd->col = col;
                return press_key(kbd, &key);
            }
        }
    return PW_VKBD_NONE;
}

size_t pw_vkbd_utf8(const PwVkbd *kbd, char *out, size_t capacity)
{
    size_t used = 0;
    if (!capacity) return 0;
    for (size_t i = 0; i < kbd->length; ++i) {
        uint32_t c = kbd->text[i];
        if ((c & 0xfc00) == 0xd800 && i + 1 < kbd->length && (kbd->text[i + 1] & 0xfc00) == 0xdc00)
            c = 0x10000 + ((c - 0xd800) << 10) + (kbd->text[++i] - 0xdc00);
        char bytes[4];
        size_t n;
        if (c < 0x80) bytes[0] = (char)c, n = 1;
        else if (c < 0x800) bytes[0] = (char)(0xc0 | c >> 6), bytes[1] = (char)(0x80 | (c & 0x3f)), n = 2;
        else if (c < 0x10000) bytes[0] = (char)(0xe0 | c >> 12), bytes[1] = (char)(0x80 | (c >> 6 & 0x3f)),
                              bytes[2] = (char)(0x80 | (c & 0x3f)), n = 3;
        else bytes[0] = (char)(0xf0 | c >> 18), bytes[1] = (char)(0x80 | (c >> 12 & 0x3f)),
             bytes[2] = (char)(0x80 | (c >> 6 & 0x3f)), bytes[3] = (char)(0x80 | (c & 0x3f)), n = 4;
        if (used + n + 1 > capacity) break;
        memcpy(out + used, bytes, n);
        used += n;
    }
    out[used] = '\0';
    return used;
}

int pw_vkbd_stick(PwVkbdStick *stick, float x, float y, uint64_t now_ms, int *dx, int *dy)
{
    const int sx = x > 0.6f ? 1 : x < -0.6f ? -1 : 0, sy = y > 0.6f ? 1 : y < -0.6f ? -1 : 0;
    /* the stronger axis only: a diagonal moves one way at a time */
    const int ax = (x < 0 ? -x : x) >= (y < 0 ? -y : y) ? sx : 0, ay = ax ? 0 : sy;
    if (!ax && !ay) { stick->dx = stick->dy = 0; return 0; }
    if (ax != stick->dx || ay != stick->dy) {
        stick->dx = ax, stick->dy = ay, stick->since_ms = stick->last_ms = now_ms;
        *dx = ax, *dy = ay;
        return 1;
    }
    if (now_ms - stick->since_ms >= 400 && now_ms - stick->last_ms >= 140) {
        stick->last_ms = now_ms;
        *dx = ax, *dy = ay;
        return 1;
    }
    return 0;
}
