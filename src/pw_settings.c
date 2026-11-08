/* SPDX-License-Identifier: GPL-3.0-or-later
 * The Settings app as the approved prototype draws it (prototypes/windows11-shell js/apps/settings.js, css/apps/settings.css),
 * measured at 96 DPI in its 1180 x 800 window (tools/measure-prototype-surface.mjs; the owner's reference pictures of
 * 2026-11-07 are the same window at 1.7x): a 48 px title bar with the back button, the app icon and the caption buttons,
 * the account block, "Find a setting" and the navigation in a 288 px pane, and each page's 28 px title over its cards.
 * Fixed pixels (icons, the avatar, chevrons, the toggle and slider, the search box) are the prototype's own
 * (pw_settings_sprites.h, tools/build-settings-sprites.py); text is GDI's in the shell's Proton UI; surfaces are the
 * colours the prototype shows over its wallpaper (its Mica is painted as those colours).
 *
 * What the pages do, and only that:
 *   Home                 the device (its picture is the desktop's), the network, the six themes and Color mode, the
 *                        recommended pages; Rename, Bluetooth's buttons and pages the shell cannot back open a page that
 *                        says they come later.
 *   Personalization      the preview, the six themes (picture and light or dark mode), and its pages.
 *   Background           the pictures MihawkOS ships (the runtime's share/mihawk/wallpapers) and any 1920 x 1080 BMP copied
 *                        into C:\windows\web\wallpaper, and Browse photos. The choice is the title's wallpaper.txt
 *                        ("wallpaper <id>", src/pw_wallpaper.h, which the title applies before the desktop starts) and
 *                        shows at once: the picture is copied over the managed proton.bmp and the desktop's view is sent
 *                        a registered message (wine/ps5/pw_shell_desktop_view.h).
 *   Colors               Light or dark, through uxtheme's own ApplyTheme.
 *   System > Display     what the display is (100% scale, its resolution, brightness at its full level, Night light off);
 *                        nothing there changes the display, and its rows open a page that says so.
 * A theme's accent colour is shown on its tile; MihawkOS keeps its own accent.
 *
 * Every row, tile and control is reached with the arrow keys (the DualSense's D-pad) and taken with Enter or Space; Escape or
 * Backspace goes back. tests/wallpaper-choice-probe.c drives it on the console; --render=<page> --out=<file.bmp> draws a page
 * into a picture with no window (tools/run-settings-render.py compares it with the prototype on the PC).
 */
#define UNICODE
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <ole2.h>
#include <winternl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <iphlpapi.h>
#include "appearance.h"
#include "pw_settings_sprites.h"
#include "pw_accent.h"

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2   /* DWM_WINDOW_CORNER_PREFERENCE, as the patched tree's dwmapi.h has it */
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* ---------- pages ---------- */
enum { PG_HOME, PG_SYSTEM, PG_BLUETOOTH, PG_NETWORK, PG_PERSONALIZATION, PG_APPS, PG_ACCOUNTS, PG_TIME, PG_GAMING,
       PG_ACCESSIBILITY, PG_PRIVACY, PG_UPDATE,   /* the navigation's twelve, in its order */
       PG_DISPLAY, PG_BACKGROUND, PG_COLORS, PG_LATER, PG_COUNT };
#define NAV_COUNT 12
static const WCHAR *nav_names[NAV_COUNT] = {
    L"Home", L"System", L"Bluetooth & devices", L"Network & internet", L"Personalization", L"Apps", L"Accounts",
    L"Time & language", L"Gaming", L"Accessibility", L"Privacy & security", L"MihawkOS Update",
};
/* The navigation item each page belongs to (its pill). */
static int nav_of(int p) { return p < NAV_COUNT ? p : p == PG_DISPLAY ? PG_SYSTEM : p == PG_LATER ? -1 : PG_PERSONALIZATION; }

int _fltused;   /* the theme tiles scale in doubles; no C runtime supplies this marker */
static const int window_width = 1180, window_height = 800;   /* the prototype's window, frame's visible pixel included */
static int client_width = 1178, client_height = 798;
enum { TITLE_H = 48 };   /* the page's coordinates below are the prototype's .stg-app's; it starts under the title bar */
static const int wall_cell_width = 160, wall_cell_height = 90, wall_gap = 12, wall_label = 34;

static HWND window;
static HFONT font_caption, font_body, font_strong, font_subtitle, font_title;
static HMODULE themes;
static BOOL dark;
static int page = PG_HOME, later_from = -1;
static WCHAR later_title[64];
static struct { int page, later_from; WCHAR title[64]; } history[16];
static int history_count;
static HICON caption_icon;

/* The wallpapers C:\windows\web\wallpaper offers and the one the screen shows now (proton.bmp, the managed one). */
#define WALLPAPERS_MAX 16
#define WALLPAPER_COLUMNS 4
#define WALLPAPER_DEFAULT L"bubble-city"
#define WALLPAPER_SETTING L"Z:\\data\\ps5proton\\wallpaper.txt"
#define WALLPAPER_SHIPPED L"Z:\\data\\ps5proton\\runtime\\ge11-7\\share\\mihawk\\wallpapers"
static struct { WCHAR path[MAX_PATH], id[64], name[64]; HBITMAP preview; } wallpapers[WALLPAPERS_MAX];
static int wallpaper_count;
static int wallpaper_current = -1, wallpaper_focus = 0;
static BOOL wallpaper_forced;
static WCHAR forced_id[64];
static BOOL wallpaper_saved, wallpaper_live;   /* a choice was just made: and the desktop showed it at once */
static WCHAR wallpaper_folder[MAX_PATH], shipped_folder[MAX_PATH] = WALLPAPER_SHIPPED;
static int screen_width = 1920, screen_height = 1080;

/* ---------- the colours the prototype shows (light; dark is its dark theme's) ---------- */
static COLORREF text_colour(void) { return dark ? RGB(255, 255, 255) : RGB(26, 26, 26); }
static COLORREF secondary_colour(void) { return dark ? RGB(206, 206, 206) : RGB(95, 96, 97); }
/* the applied theme's accent in this theme's fill shade (src/pw_accent.h): Settings' nav bar, toggles, slider, rings */
static COLORREF accent_colour(void) { return pw_accent_colorref(pw_accent_fill(pw_accent_current(), dark)); }
static COLORREF link_colour(void) { return dark ? RGB(153, 235, 255) : RGB(0, 62, 146); }
static COLORREF card_colour(void) { return dark ? RGB(43, 43, 43) : RGB(251, 253, 255); }
static COLORREF card_edge(void) { return dark ? RGB(29, 29, 29) : RGB(227, 234, 238); }
static COLORREF divider_colour(void) { return dark ? RGB(56, 56, 56) : RGB(236, 238, 240); }
static COLORREF subtle_colour(void) { return dark ? RGB(45, 45, 45) : RGB(234, 239, 244); }   /* SubtleFillColorSecondary */
static COLORREF combo_face(void) { return dark ? RGB(55, 55, 55) : RGB(254, 255, 255); }
static COLORREF combo_edge(void) { return dark ? RGB(48, 48, 48) : RGB(236, 239, 240); }
static COLORREF combo_bottom(void) { return dark ? RGB(70, 70, 70) : RGB(210, 212, 214); }
/* The page's Mica as the prototype shows it over its wallpaper, row by row of the client (measured where no element lies,
 * the same in all three reference pages): a pale blue that turns green under the last 90 rows (the meadow's grass). */
static const struct { short top; COLORREF colour; } mica_rows[] = {
    {0, RGB(243, 248, 253)},
    {87, RGB(243, 249, 253)},
    {151, RGB(242, 249, 253)},
    {499, RGB(241, 249, 253)},
    {571, RGB(242, 249, 253)},
    {574, RGB(241, 249, 253)},
    {655, RGB(241, 250, 253)},
    {663, RGB(241, 249, 253)},
    {711, RGB(241, 250, 253)},
    {713, RGB(241, 249, 253)},
    {715, RGB(241, 250, 253)},
    {719, RGB(240, 250, 253)},
    {727, RGB(239, 250, 253)},
    {731, RGB(238, 251, 253)},
    {733, RGB(237, 251, 253)},
    {735, RGB(236, 252, 253)},
    {739, RGB(235, 252, 253)},
    {740, RGB(234, 253, 253)},
    {743, RGB(233, 253, 253)},
    {745, RGB(234, 253, 252)},
    {748, RGB(234, 253, 251)},
    {751, RGB(234, 253, 250)},
    {756, RGB(235, 253, 250)},
    {763, RGB(235, 253, 249)},
    {768, RGB(235, 253, 248)},
    {771, RGB(236, 253, 247)},
    {775, RGB(236, 253, 246)},
    {778, RGB(236, 253, 245)},
    {781, RGB(236, 253, 244)},
    {783, RGB(237, 253, 244)},
    {786, RGB(237, 253, 243)},
    {790, RGB(237, 253, 242)},
    {794, RGB(238, 253, 241)},
};
static COLORREF page_colour(int y)
{
    int i = 0;
    if (dark) return RGB(32, 32, 32);
    while (i + 1 < (int)(sizeof(mica_rows) / sizeof(mica_rows[0])) && y >= mica_rows[i + 1].top) ++i;
    return mica_rows[i].colour;
}
/* A card is 70% white over the Mica (CardBackgroundFillColorDefault), its edge 6% black over it. */
static COLORREF card_face(int y)
{
    COLORREF m = page_colour(y);
    if (dark) return RGB(43, 43, 43);
    return RGB((GetRValue(m) * 3 + 2550 * 7 / 10 + 5) / 10, (GetGValue(m) * 3 + 2550 * 7 / 10 + 5) / 10, (GetBValue(m) * 3 + 2550 * 7 / 10 + 5) / 10);
}

/* ---------- drawing ---------- */
static void fill(HDC dc, const RECT *box, COLORREF colour)
{
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(dc, box, brush);
    DeleteObject(brush);
}

/* A rounded face with a 1 px edge inside the box (radius in pixels). */
static void rounded(HDC dc, RECT box, int radius, COLORREF face, COLORREF edge)
{
    HBRUSH brush = CreateSolidBrush(face);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ prior_brush = SelectObject(dc, brush), prior_pen = SelectObject(dc, pen);
    RoundRect(dc, box.left, box.top, box.right, box.bottom, radius * 2, radius * 2);
    SelectObject(dc, prior_brush); SelectObject(dc, prior_pen);
    DeleteObject(brush); DeleteObject(pen);
}

/* A card: its face follows the Mica row by row under it (only near the bottom does that change anything). */
static void card_fill(HDC dc, RECT box, int radius, BOOL hot)
{
    COLORREF top = hot ? subtle_colour() : card_face(box.top);
    rounded(dc, box, radius, top, card_edge());
    if (dark || hot) return;
    for (int y = box.top + 1; y < box.bottom - 1; ++y)
    {
        COLORREF c = card_face(y);
        int inset = y - box.top < radius ? radius - (y - box.top) : box.bottom - 1 - y < radius ? radius - (box.bottom - 1 - y) : 0;
        RECT row = {box.left + 1 + inset, y, box.right - 1 - inset, y + 1};
        if (c != top) fill(dc, &row, c);
    }
}

static void text_in(HDC dc, const WCHAR *text, RECT box, HFONT font, COLORREF colour, UINT flags)
{
    HGDIOBJ prior = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour);
    DrawTextW(dc, text, -1, &box, flags | DT_NOPREFIX | DT_SINGLELINE);
    SelectObject(dc, prior);
}
/* A line of text whose CSS line box is (x, y, width, height): vertically centred there, as Chromium centres it. */
static int text_lift = 1;   /* how far GDI's line sits below Chromium's for the same box (tuned on the PC render) */
static void text_at(HDC dc, const WCHAR *text, int x, int y, int width, int height, HFONT font, COLORREF colour)
{
    RECT box = {x, y - text_lift, x + width, y + height - text_lift};
    text_in(dc, text, box, font, colour, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
}
static int text_width(HDC dc, const WCHAR *text, HFONT font)
{
    SIZE size = {0, 0};
    HGDIOBJ prior = SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, prior);
    return size.cx;
}

/* The prototype's pixels: each sprite once as a premultiplied 32-bit DIB; a tinted copy keeps the alpha, takes one colour. */
static HBITMAP sprite_bitmaps[2][PW_SETTINGS_SPRITE_COUNT];
static HBITMAP make_sprite(int id, BOOL tint, COLORREF colour)
{
    const struct pw_settings_sprite *s = &pw_settings_sprites[dark][id];
    BITMAPINFO info;
    unsigned int *bits = NULL;
    HBITMAP bitmap;

    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = s->width;
    info.bmiHeader.biHeight = -s->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    if (!(bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0))) return NULL;
    for (int i = 0; i < s->width * s->height; ++i)
    {
        unsigned int word = s->bgra[i], alpha = word >> 24;
        if (tint) word = alpha << 24 | (GetRValue(colour) * alpha / 255) << 16 | (GetGValue(colour) * alpha / 255) << 8 | (GetBValue(colour) * alpha / 255);
        bits[i] = word;
    }
    return bitmap;
}
static void blend(HDC dc, HBITMAP bitmap, int x, int y, int width, int height)
{
    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ prior = SelectObject(memory, bitmap);
    BLENDFUNCTION how = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc, x, y, width, height, memory, 0, 0, width, height, how);
    SelectObject(memory, prior);
    DeleteDC(memory);
}
static void sprite(HDC dc, int id, int x, int y)
{
    const struct pw_settings_sprite *s = &pw_settings_sprites[dark][id];
    if (!sprite_bitmaps[dark][id]) sprite_bitmaps[dark][id] = make_sprite(id, FALSE, 0);
    if (sprite_bitmaps[dark][id]) blend(dc, sprite_bitmaps[dark][id], x, y, s->width, s->height);
}
static void sprite_tinted(HDC dc, int id, int x, int y, COLORREF colour)
{
    const struct pw_settings_sprite *s = &pw_settings_sprites[dark][id];
    HBITMAP bitmap = make_sprite(id, TRUE, colour);
    if (bitmap) { blend(dc, bitmap, x, y, s->width, s->height); DeleteObject(bitmap); }
}

/* A picture in a box, filled and cropped (the thumbnail look). */
static void draw_bitmap(HDC dc, const RECT *box, HBITMAP bitmap)
{
    BITMAP info;
    HDC memory;
    HGDIOBJ prior;
    int width = box->right - box->left, height = box->bottom - box->top, drawn_width, drawn_height;

    if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || !info.bmWidth || !info.bmHeight) return;
    if (!(memory = CreateCompatibleDC(dc))) return;
    prior = SelectObject(memory, bitmap);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    if ((LONGLONG)width * info.bmHeight >= (LONGLONG)height * info.bmWidth)
        drawn_width = width, drawn_height = (int)((LONGLONG)info.bmHeight * width / info.bmWidth);
    else
        drawn_height = height, drawn_width = (int)((LONGLONG)info.bmWidth * height / info.bmHeight);
    StretchBlt(dc, box->left + (width - drawn_width) / 2, box->top + (height - drawn_height) / 2, drawn_width, drawn_height,
               memory, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
    SelectObject(memory, prior);
    DeleteDC(memory);
}

/* ---------- the appearance, as mihawk-appearance reads and applies it ---------- */
typedef HRESULT (WINAPI *open_theme_fn)(const WCHAR *, const WCHAR *, const WCHAR *, HANDLE *, DWORD);
typedef HRESULT (WINAPI *apply_theme_fn)(HANDLE, char *, HWND);
typedef HRESULT (WINAPI *close_theme_fn)(HANDLE);

static BOOL appearance_is_dark(void)
{
    WCHAR file[MAX_PATH], colour[64], size[64], managed[MAX_PATH];
    COLORREF pane;
    GetWindowsDirectoryW(managed, MAX_PATH);
    lstrcatW(managed, L"\\resources\\themes\\light\\light.msstyles");
    if (SUCCEEDED(GetCurrentThemeName(file, MAX_PATH, colour, 64, size, 64)) && !lstrcmpiW(file, managed))
        return !lstrcmpW(colour, L"Dark");
    pane = GetSysColor(COLOR_WINDOW);
    return GetRValue(pane) * 54 + GetGValue(pane) * 183 + GetBValue(pane) * 19 < 128 * 256;
}

static BOOL apply_appearance(BOOL want_dark)
{
    open_theme_fn open_theme = (open_theme_fn)GetProcAddress(themes, MAKEINTRESOURCEA(2));
    close_theme_fn close_theme = (close_theme_fn)GetProcAddress(themes, MAKEINTRESOURCEA(3));
    apply_theme_fn apply_theme = (apply_theme_fn)GetProcAddress(themes, MAKEINTRESOURCEA(4));
    WCHAR path[MAX_PATH];
    HANDLE file = NULL;
    HRESULT result;

    if (!themes || !open_theme || !close_theme || !apply_theme) return FALSE;
    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\resources\\themes\\light\\light.msstyles");
    result = open_theme(path, want_dark ? L"Dark" : L"Blue", L"NormalSize", &file, 0);
    if (SUCCEEDED(result)) { result = apply_theme(file, NULL, window); close_theme(file); }
    if (FAILED(result)) return FALSE;
    dark = want_dark;
    InvalidateRect(window, NULL, TRUE);
    return TRUE;
}

/* ---------- the pictures ---------- */
/* A picture file as a bitmap: BMP, the shape the desktop's decoder reads and the theme ships (a JPEG or PNG picture
 * is not a wallpaper this app can write yet). */
static HBITMAP picture_bitmap(const WCHAR *path)
{
    return (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

/* The picture filled to width x height (Windows' Fill: cover, cropped equally), in a 24-bit top-down DIB. */
static HBITMAP picture_fit(HBITMAP source, int width, int height, void **bits_out)
{
    BITMAPINFO info;
    BITMAP source_info;
    HBITMAP target;
    HDC dc, memory;
    void *bits = NULL;
    int drawn_width, drawn_height, x, y;
    HGDIOBJ prior_target, prior_source;

    if (bits_out) *bits_out = NULL;
    if (!source || !GetObjectW(source, sizeof(source_info), &source_info) || !source_info.bmWidth || !source_info.bmHeight)
        return NULL;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    if (!(target = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0))) return NULL;
    if (!(dc = CreateCompatibleDC(NULL))) { DeleteObject(target); return NULL; }
    if (!(memory = CreateCompatibleDC(dc))) { DeleteDC(dc); DeleteObject(target); return NULL; }
    prior_target = SelectObject(dc, target);
    prior_source = SelectObject(memory, source);
    fill(dc, &(RECT){0, 0, width, height}, RGB(0, 0, 0));
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    if ((LONGLONG)width * source_info.bmHeight >= (LONGLONG)height * source_info.bmWidth)
    {
        drawn_width = width;
        drawn_height = (int)((LONGLONG)source_info.bmHeight * width / source_info.bmWidth);
    }
    else
    {
        drawn_height = height;
        drawn_width = (int)((LONGLONG)source_info.bmWidth * height / source_info.bmHeight);
    }
    x = (width - drawn_width) / 2;
    y = (height - drawn_height) / 2;
    StretchBlt(dc, x, y, drawn_width, drawn_height, memory, 0, 0, source_info.bmWidth, source_info.bmHeight, SRCCOPY);
    SelectObject(memory, prior_source); SelectObject(dc, prior_target);
    DeleteDC(memory); DeleteDC(dc);
    if (bits_out) *bits_out = bits;
    return target;
}

/* A 24-bit BMP the desktop's decoder reads (wine/ps5/pw_wine_wallpaper.c: BITMAPINFOHEADER, 24 or 32 bits), from
 * the fitted picture's own rows (top-down, 4 byte aligned). */
static BOOL write_bmp(const WCHAR *path, const void *bits, int width, int height)
{
    BITMAPFILEHEADER file;
    BITMAPINFOHEADER info;
    DWORD stride = (DWORD)((width * 3 + 3) & ~3u), size = stride * (DWORD)height, written;
    HANDLE handle;
    BOOL okay;

    memset(&file, 0, sizeof(file));
    memset(&info, 0, sizeof(info));
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + size;
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biCompression = BI_RGB;
    info.biSizeImage = size;
    handle = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return FALSE;
    okay = WriteFile(handle, &file, sizeof(file), &written, NULL) && written == sizeof(file) &&
           WriteFile(handle, &info, sizeof(info), &written, NULL) && written == sizeof(info) &&
           WriteFile(handle, bits, size, &written, NULL) && written == size;
    CloseHandle(handle);
    if (!okay) DeleteFileW(path);
    return okay;
}

/* The player's choice, as the title reads it (src/pw_wallpaper.c): one line, "wallpaper <id>", written whole. */
static BOOL save_choice(const WCHAR *id)
{
    WCHAR temporary[MAX_PATH];
    char line[160];
    DWORD written;
    HANDLE file;
    BOOL okay;
    int length;

    length = WideCharToMultiByte(CP_UTF8, 0, id, -1, line + 10, (int)sizeof(line) - 12, NULL, NULL);
    if (length <= 1) return FALSE;
    memcpy(line, "wallpaper ", 10);
    line[10 + length - 1] = '\n';
    lstrcpynW(temporary, WALLPAPER_SETTING, MAX_PATH);
    lstrcatW(temporary, L".tmp");
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    okay = WriteFile(file, line, 10 + length, &written, NULL) && written == (DWORD)(10 + length) && FlushFileBuffers(file);
    CloseHandle(file);
    if (okay) okay = MoveFileExW(temporary, WALLPAPER_SETTING, MOVEFILE_REPLACE_EXISTING);
    if (!okay) DeleteFileW(temporary);
    return okay;
}

/* The id the file names, else the default. */
static void read_choice(WCHAR *id)
{
    char text[160] = "";
    DWORD read = 0;
    HANDLE file = CreateFileW(WALLPAPER_SETTING, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    lstrcpyW(id, WALLPAPER_DEFAULT);
    if (file == INVALID_HANDLE_VALUE) return;
    ReadFile(file, text, sizeof(text) - 1, &read, NULL);
    CloseHandle(file);
    text[read] = 0;
    if (!memcmp(text, "wallpaper ", 10))
    {
        char *end = text + 10;
        while (*end && *end != '\r' && *end != '\n' && *end != ' ') ++end;
        *end = 0;
        if (end > text + 10) MultiByteToWideChar(CP_UTF8, 0, text + 10, -1, id, 64);
    }
}

/* The picture on the screen now: the file becomes the managed proton.bmp, then the desktop's view is told (a registered
 * message it answers by repainting from the new file); FALSE when no desktop view answered, and the choice then shows
 * from the next start, when the title makes it proton.bmp itself. */
static BOOL show_now(const WCHAR *picture)
{
    static const WCHAR message_name[] = L"MihawkOS.WallpaperChanged";
    WCHAR target[MAX_PATH];
    HWND progman = FindWindowW(L"Progman", NULL), view = NULL;
    DWORD_PTR answer = 0;

    lstrcpynW(target, wallpaper_folder, MAX_PATH);
    PathAppendW(target, L"proton.bmp");
    if (!CopyFileW(picture, target, FALSE)) return FALSE;
    if (progman) view = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
    if (!view) return FALSE;
    return SendMessageTimeoutW(view, RegisterWindowMessageW(message_name), 0, 0, SMTO_ABORTIFHUNG, 5000, &answer) && answer == 1;
}

static BOOL choose_wallpaper(int index)
{
    if (index < 0 || index >= wallpaper_count || !save_choice(wallpapers[index].id)) return FALSE;
    wallpaper_current = wallpaper_focus = index;
    wallpaper_live = show_now(wallpapers[index].path);
    wallpaper_saved = TRUE;
    InvalidateRect(window, NULL, TRUE);
    return TRUE;
}

/* Browse photos: the picture fitted to the screen and kept as the prefix's custom.bmp (the title copies a person's own
 * file of the chosen name from there), then chosen. */
static BOOL apply_wallpaper(const WCHAR *path)
{
    WCHAR custom[MAX_PATH];
    HBITMAP picture = NULL, fitted = NULL;
    void *bits = NULL;
    BOOL okay = FALSE;

    if ((picture = picture_bitmap(path)) && (fitted = picture_fit(picture, screen_width, screen_height, &bits)))
    {
        lstrcpynW(custom, wallpaper_folder, MAX_PATH);
        PathAppendW(custom, L"custom.bmp");
        okay = write_bmp(custom, bits, screen_width, screen_height) && save_choice(L"custom");
        if (okay) wallpaper_live = show_now(custom);
    }
    if (fitted) DeleteObject(fitted);
    if (picture) DeleteObject(picture);
    return okay;
}

/* "bubble-city" shown as "Bubble City". */
static void display_name(WCHAR *name, const WCHAR *id)
{
    BOOL start = TRUE;
    lstrcpynW(name, id, 64);
    for (WCHAR *c = name; *c; ++c)
    {
        if (*c == L'-' || *c == L'_') { *c = L' '; start = TRUE; continue; }
        if (start && *c >= L'a' && *c <= L'z') *c = (WCHAR)(*c - 32);
        start = FALSE;
    }
}

static void wallpapers_scan(const WCHAR *folder)
{
    WIN32_FIND_DATAW entry;
    WCHAR pattern[MAX_PATH];
    HANDLE found;

    lstrcpynW(pattern, folder, MAX_PATH);
    PathAppendW(pattern, L"*");
    if ((found = FindFirstFileW(pattern, &entry)) == INVALID_HANDLE_VALUE) return;
    do {
        const WCHAR *dot = wcsrchr(entry.cFileName, L'.');
        int index = wallpaper_count, k;
        WCHAR id[64];
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !dot || lstrcmpiW(dot, L".bmp")) continue;
        if (!lstrcmpiW(entry.cFileName, L"proton.bmp") || dot - entry.cFileName >= 64) continue;
        lstrcpynW(id, entry.cFileName, (int)(dot - entry.cFileName) + 1);
        for (k = 0; k < wallpaper_count; ++k)
            if (!lstrcmpiW(wallpapers[k].id, id)) { index = k; break; }   /* a person's own file wins over the shipped one */
        if (index == wallpaper_count && wallpaper_count == WALLPAPERS_MAX) continue;
        lstrcpynW(wallpapers[index].path, folder, MAX_PATH);
        PathAppendW(wallpapers[index].path, entry.cFileName);
        lstrcpyW(wallpapers[index].id, id);
        display_name(wallpapers[index].name, id);
        if (wallpapers[index].preview) { DeleteObject(wallpapers[index].preview); wallpapers[index].preview = NULL; }
        if (index == wallpaper_count) ++wallpaper_count;
    } while (FindNextFileW(found, &entry));
    FindClose(found);
}

static void wallpapers_load(void)
{
    WCHAR chosen[64];
    int i;

    for (i = 0; i < wallpaper_count; ++i) if (wallpapers[i].preview) DeleteObject(wallpapers[i].preview), wallpapers[i].preview = NULL;
    wallpaper_count = 0;
    wallpaper_current = -1;
    GetWindowsDirectoryW(wallpaper_folder, MAX_PATH);
    lstrcatW(wallpaper_folder, L"\\web\\wallpaper");
    wallpapers_scan(shipped_folder);
    wallpapers_scan(wallpaper_folder);
    read_choice(chosen);
    for (i = 0; i < wallpaper_count; ++i)
        if (!lstrcmpiW(wallpapers[i].id, chosen)) wallpaper_current = i;
    wallpaper_focus = wallpaper_current >= 0 ? wallpaper_current : 0;
}

static HBITMAP wallpaper_preview(int index)
{
    HBITMAP picture, preview;

    if (wallpapers[index].preview) return wallpapers[index].preview;
    if (!(picture = picture_bitmap(wallpapers[index].path))) return NULL;
    preview = picture_fit(picture, wall_cell_width, wall_cell_height, NULL);
    DeleteObject(picture);
    wallpapers[index].preview = preview;
    return preview;
}

/* ---------- themes: the prototype's six (js/apps/settings.js THEMES), its "Windows" pair named MihawkOS ---------- */
static const struct { const WCHAR *name, *wallpaper; BOOL dark_mode; COLORREF accent; } theme_list[6] = {
    {L"MihawkOS (light)", L"aero-meadow", FALSE, RGB(0, 120, 212)},
    {L"MihawkOS (dark)", L"aero-meadow-night", TRUE, RGB(0, 120, 212)},
    {L"Glow", L"theme-glow", TRUE, RGB(135, 100, 184)},
    {L"Captured Motion", L"theme-captured-motion", TRUE, RGB(3, 131, 135)},
    {L"Sunrise", L"theme-sunrise", FALSE, RGB(202, 80, 16)},
    {L"Flow", L"theme-flow", FALSE, RGB(107, 105, 214)},
};

static int wallpaper_index(const WCHAR *id)
{
    for (int i = 0; i < wallpaper_count; ++i) if (!lstrcmpiW(wallpapers[i].id, id)) return i;
    return -1;
}
static BOOL theme_selected(int t)
{
    return wallpaper_current >= 0 && !lstrcmpiW(wallpapers[wallpaper_current].id, theme_list[t].wallpaper) && dark == theme_list[t].dark_mode;
}
static void apply_theme(int t)
{
    int index = wallpaper_index(theme_list[t].wallpaper);
    if (index >= 0) choose_wallpaper(index);
    if (dark != theme_list[t].dark_mode) apply_appearance(theme_list[t].dark_mode);
    /* the theme's accent, stored as Windows stores it; the shell's surfaces reload it on WM_SETTINGCHANGE */
    pw_accent_store((GetRValue(theme_list[t].accent) << 16) | (GetGValue(theme_list[t].accent) << 8) |
                    GetBValue(theme_list[t].accent));
    InvalidateRect(window, NULL, FALSE);
}

/* ---------- what can be pointed at or reached with the keys: registered as it is painted ---------- */
enum { ACT_NONE, ACT_NAV, ACT_OPEN, ACT_LATER, ACT_THEME, ACT_PICTURE, ACT_BROWSE, ACT_MODE, ACT_LIGHT, ACT_DARK,
       ACT_BACK, ACT_MIN, ACT_MAX, ACT_CLOSE, ACT_SEARCH, ACT_COMBO };
struct item { RECT box; int action, arg; const WCHAR *title; };
static struct item items[96];
static int item_count, focus = -1, hover = -1, pressed = -1;
static BOOL keyboard;   /* the focus ring shows once the keys are used, as Windows shows it */
static WCHAR search_text[32];
static BOOL search_focus;

static void add_item(int left, int top, int right, int bottom, int action, int arg, const WCHAR *title)
{
    if (item_count >= (int)ARRAY_SIZE(items)) return;
    items[item_count].box.left = left; items[item_count].box.top = top;
    items[item_count].box.right = right; items[item_count].box.bottom = bottom;
    items[item_count].action = action; items[item_count].arg = arg; items[item_count].title = title;
    ++item_count;
}
static BOOL is_hot(int left, int top)
{
    return hover >= 0 && hover < item_count && items[hover].box.left == left && items[hover].box.top == top;
}

/* ---------- the window's parts ---------- */
static void paint_background(HDC dc)
{
    for (int y = 0; y < client_height; )
    {
        COLORREF colour = page_colour(y);
        int next = y + 1;
        while (next < client_height && page_colour(next) == colour) ++next;
        {
            RECT band = {0, y, client_width, next};
            fill(dc, &band, colour);
        }
        y = next;
    }
}

static void paint_titlebar(HDC dc)
{
    int caption_left = client_width - 3 * 46;
    if (history_count)   /* the back button: enabled when there is somewhere to go back to */
        sprite_tinted(dc, PW_SETTINGS_BACK, 4, 6, text_colour());
    else sprite(dc, PW_SETTINGS_BACK, 4, 6);
    add_item(4, 6, 44, 42, ACT_BACK, 0, L"Back");
    sprite(dc, PW_SETTINGS_APP_ICON, 52, 16);
    text_at(dc, L"Settings", 80, 14, 200, 20, font_caption, text_colour());
    for (int i = 0; i < 3; ++i)
    {
        int x = caption_left + i * 46, id = PW_SETTINGS_CAPTION_MIN + i;
        RECT box = {x, 0, x + 46, 48};
        if (is_hot(x, 0)) fill(dc, &box, i == 2 ? RGB(196, 43, 28) : subtle_colour());
        if (i == 2 && is_hot(x, 0)) sprite_tinted(dc, id, x, 0, RGB(255, 255, 255));
        else sprite(dc, id, x, 0);
        add_item(x, 0, x + 46, 48, ACT_MIN + i, 0, NULL);
    }
}

static void paint_pane(HDC dc)
{
    int selected = nav_of(page);
    sprite(dc, PW_SETTINGS_AVATAR, 16, TITLE_H + 8);
    text_at(dc, L"Mihawk", 96, TITLE_H + 22, 180, 20, font_strong, text_colour());
    text_at(dc, L"Local account", 96, TITLE_H + 42, 180, 16, font_caption, secondary_colour());
    sprite(dc, PW_SETTINGS_SEARCH, 16, TITLE_H + 92);
    if (search_text[0] || search_focus)
    {
        RECT inside = {17, TITLE_H + 93, 16 + 220, TITLE_H + 122};
        fill(dc, &inside, combo_face());
        text_at(dc, search_text, 28, TITLE_H + 98, 200, 20, font_body, text_colour());
        if (search_focus)
        {
            RECT line = {16, TITLE_H + 122, 16 + 256, TITLE_H + 124};
            fill(dc, &line, accent_colour());
        }
    }
    add_item(16, TITLE_H + 92, 272, TITLE_H + 124, ACT_SEARCH, 0, L"Find a setting");
    for (int i = 0, row = 0; i < NAV_COUNT; ++i)
    {
        int top = TITLE_H + 136 + row * 40;
        if (search_text[0] && !StrStrIW(nav_names[i], search_text)) continue;
        if (i == selected || is_hot(4, top))
        {
            RECT plate = {4, top, 284, top + 36};
            rounded(dc, plate, 4, subtle_colour(), subtle_colour());
        }
        if (i == selected)
        {
            RECT pill = {4, top + 10, 7, top + 26};
            rounded(dc, pill, 1, accent_colour(), accent_colour());
        }
        sprite(dc, PW_SETTINGS_NAV_1 + i, 16, top + 10);
        text_at(dc, nav_names[i], 48, top + 8, 230, 20, font_body, text_colour());
        add_item(4, top, 284, top + 36, ACT_NAV, i, nav_names[i]);
        ++row;
    }
}

/* The page's 28 px title: one name, or "Parent > Page" with the parent grey (it goes back to it). */
static void paint_title(HDC dc, const WCHAR *parent, int parent_page, const WCHAR *name)
{
    int x = 324;
    if (parent)
    {
        int width = text_width(dc, parent, font_title);
        text_at(dc, parent, x, TITLE_H + 2, width + 4, 36, font_title, secondary_colour());
        add_item(x, TITLE_H + 2, x + width, TITLE_H + 38, ACT_NAV, parent_page, parent);
        x += width + 12;
        sprite(dc, PW_SETTINGS_CRUMB_CHEVRON, x, TITLE_H + 14);
        x += 28;
    }
    text_at(dc, name, x, TITLE_H + 2, 760, 36, font_title, text_colour());
}

/* A settings card (804 x 68): its icon, title and line, an optional chevron; the whole card is one item. */
static void paint_card(HDC dc, int top, int icon, const WCHAR *title, const WCHAR *line, BOOL chevron, int action, int arg)
{
    RECT box = {324, TITLE_H + top, 324 + 804, TITLE_H + top + 68};
    card_fill(dc, box, 4, is_hot(box.left, box.top) && action);
    if (icon >= 0) sprite(dc, icon, 343, TITLE_H + top + 24);
    if (line)
    {
        text_at(dc, title, 383, TITLE_H + top + 16, 480, 20, font_body, text_colour());
        text_at(dc, line, 383, TITLE_H + top + 36, 480, 16, font_caption, secondary_colour());
    }
    else text_at(dc, title, 383, TITLE_H + top + 24, 480, 20, font_body, text_colour());
    if (chevron) sprite(dc, PW_SETTINGS_CHEVRON_CARD, 1097, TITLE_H + top + 26);
    if (action) add_item(box.left, box.top, box.right, box.bottom, action, arg, title);
}

/* A combo box as the prototype draws one (its value and chevron), right-aligned at right; registered as an item. */
static void paint_combo(HDC dc, int right, int top, const WCHAR *value, const WCHAR *widest, int arg)
{
    int width = max(text_width(dc, value, font_body), widest ? text_width(dc, widest, font_body) : 0) + 52, left = right - width;
    RECT box = {left, TITLE_H + top, right, TITLE_H + top + 32}, bottom = {left + 2, TITLE_H + top + 31, right - 2, TITLE_H + top + 32};
    rounded(dc, box, 4, combo_face(), combo_edge());
    fill(dc, &bottom, combo_bottom());
    text_at(dc, value, left + 12, TITLE_H + top + 5, width - 40, 20, font_body, text_colour());
    sprite(dc, PW_SETTINGS_COMBO_CHEVRON, right - 26, TITLE_H + top + 9);
    add_item(box.left, box.top, box.right, box.bottom, ACT_COMBO, arg, value);
}

/* The theme tiles, as the prototype draws them (captured sprites of its scenes, mock window, "Aa" and accent);
   the applied one ringed. */
static void paint_themes(HDC dc, int left, int top, int big)
{
    int w = big ? 120 : 112, h = big ? 75 : 70, step_x = big ? 128 : 120, step_y = big ? 83 : 78;
    for (int t = 0; t < 6; ++t)
    {
        int x = left + (t % 3) * step_x, y = TITLE_H + top + (t / 3) * step_y;
        {   /* the prototype's outline: 1 px edge, or a 3 px accent ring on the applied theme */
            int o = theme_selected(t) ? 3 : 1;
            RECT ring = {x - o, y - o, x + w + o, y + h + o};
            COLORREF c = theme_selected(t) ? accent_colour() : card_edge();
            rounded(dc, ring, 4 + o, c, c);
        }
        sprite(dc, (big ? PW_SETTINGS_TILE_BIG_1 : PW_SETTINGS_TILE_SMALL_1) + t, x, y);
        add_item(x, y, x + w, y + h, ACT_THEME, t, theme_list[t].name);
    }
}

/* A Home card (394 px wide, 8 px corners): its title and line; rows follow. */
static void paint_home_card(HDC dc, int left, int top, int height, const WCHAR *title, const WCHAR *line)
{
    RECT box = {left, TITLE_H + top, left + 394, TITLE_H + top + height};
    card_fill(dc, box, 8, FALSE);
    text_at(dc, title, left + 21, TITLE_H + top + 21, 352, 28, font_subtitle, text_colour());
    text_at(dc, line, left + 21, TITLE_H + top + 51, 352, 16, font_caption, secondary_colour());
}
static void paint_home_row(HDC dc, int left, int top, const WCHAR *label, BOOL chevron, BOOL divider, int action, int arg)
{
    RECT row = {left + 13, TITLE_H + top, left + 13 + 368, TITLE_H + top + 44};
    if (action && is_hot(row.left, row.top)) rounded(dc, row, 4, subtle_colour(), subtle_colour());
    text_at(dc, label, left + 21, TITLE_H + top + 12, 300, 20, font_body, text_colour());
    if (chevron) sprite(dc, PW_SETTINGS_CHEVRON_ROW, left + 357 + (left > 400 ? 0 : 0), TITLE_H + top + 14);
    if (divider)
    {
        RECT line = {left + 15, TITLE_H + top + 43, left + 378, TITLE_H + top + 44};
        fill(dc, &line, divider_colour());
    }
    if (action) add_item(row.left, row.top, row.right, row.bottom, action, arg, label);
}

/* ---------- pages ---------- */
static HBITMAP preview_for(int width, int height)
{
    static HBITMAP cached; static WCHAR cached_for[MAX_PATH]; static int cached_w;
    const WCHAR *path = wallpaper_current >= 0 ? wallpapers[wallpaper_current].path : NULL;
    HBITMAP picture;
    if (!path) return NULL;
    if (cached && cached_w == width && !lstrcmpiW(cached_for, path)) return cached;
    if (cached) DeleteObject(cached);
    cached = NULL;
    if ((picture = picture_bitmap(path))) { cached = picture_fit(picture, width, height, NULL); DeleteObject(picture); }
    lstrcpynW(cached_for, path, MAX_PATH);
    cached_w = width;
    return cached;
}

static void network_state(WCHAR *name, WCHAR *line)
{
    ULONG size = 0;
    IP_ADAPTER_ADDRESSES *list = NULL, *a;
    lstrcpyW(name, L"Network");
    lstrcpyW(line, L"Not connected");
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, NULL, &size) != ERROR_BUFFER_OVERFLOW) return;
    if (!(list = HeapAlloc(GetProcessHeap(), 0, size))) return;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, list, &size) == NO_ERROR)
        for (a = list; a; a = a->Next)
            if (a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK)
            {
                lstrcpyW(name, a->IfType == IF_TYPE_IEEE80211 ? L"Wi-Fi" : L"Ethernet");
                lstrcpyW(line, L"Connected");
                break;
            }
    HeapFree(GetProcessHeap(), 0, list);
}

static void paint_home(HDC dc)
{
    WCHAR computer[64], net_name[32], net_line[32];
    DWORD size = ARRAY_SIZE(computer);
    RECT screen = {328, TITLE_H + 64, 328 + 116, TITLE_H + 64 + 72};

    paint_title(dc, NULL, 0, L"Home");
    sprite(dc, PW_SETTINGS_DEVICE_FRAME, 324, TITLE_H + 60);   /* the frame, then the desktop's picture in its screen */
    draw_bitmap(dc, &screen, preview_for(116, 72));
    if (!GetComputerNameW(computer, &size) || !computer[0]) lstrcpyW(computer, L"MIHAWK-PS5");   /* the console may report none */
    CharUpperW(computer);
    text_at(dc, computer, 468, TITLE_H + 61, 230, 28, font_subtitle, text_colour());
    text_at(dc, L"Proton runtime", 468, TITLE_H + 89, 200, 16, font_caption, secondary_colour());
    text_at(dc, L"Rename", 468, TITLE_H + 113, 100, 20, font_body, link_colour());
    add_item(457, TITLE_H + 107, 529, TITLE_H + 139, ACT_LATER, PG_HOME, L"Rename");
    network_state(net_name, net_line);
    sprite(dc, PW_SETTINGS_HERO_WIFI, 720, TITLE_H + 82);
    text_at(dc, net_name, 768, TITLE_H + 82, 120, 20, font_strong, text_colour());
    text_at(dc, net_line, 768, TITLE_H + 102, 120, 16, font_caption, secondary_colour());
    add_item(708, TITLE_H + 72, 888, TITLE_H + 128, ACT_LATER, PG_NETWORK, L"Network & internet");
    sprite(dc, PW_SETTINGS_HERO_UPDATE, 908, TITLE_H + 82);
    text_at(dc, L"MihawkOS Update", 956, TITLE_H + 82, 170, 20, font_strong, text_colour());
    text_at(dc, L"Updates come with MihawkOS", 956, TITLE_H + 102, 170, 16, font_caption, secondary_colour());
    add_item(896, TITLE_H + 72, 1128, TITLE_H + 128, ACT_LATER, PG_UPDATE, L"MihawkOS Update");

    paint_home_card(dc, 324, 168, 276, L"Recommended settings", L"Recent and commonly used settings");
    paint_home_row(dc, 324, 251, L"Display", TRUE, TRUE, ACT_OPEN, PG_DISPLAY);
    paint_home_row(dc, 324, 295, L"Sound", TRUE, TRUE, ACT_LATER, PG_SYSTEM);
    paint_home_row(dc, 324, 339, L"Taskbar", TRUE, TRUE, ACT_LATER, PG_PERSONALIZATION);
    paint_home_row(dc, 324, 383, L"Colors", TRUE, FALSE, ACT_OPEN, PG_COLORS);

    paint_home_card(dc, 734, 168, 344, L"Personalize your device", L"Select a theme to apply");
    paint_themes(dc, 755, 247, 0);
    paint_home_row(dc, 734, 407, L"Color mode", FALSE, TRUE, ACT_NONE, 0);
    paint_combo(dc, 1107, 413, dark ? L"Dark" : L"Light", L"Light", 0);
    paint_home_row(dc, 734, 451, L"Browse more backgrounds, colors, and themes", TRUE, FALSE, ACT_OPEN, PG_PERSONALIZATION);

    paint_home_card(dc, 734, 528, 232, L"Bluetooth devices", L"Manage, add, and remove devices");
    sprite(dc, PW_SETTINGS_NAV_9, 757, TITLE_H + 625);
    text_at(dc, L"DualSense Wireless Controller", 787, TITLE_H + 615, 300, 20, font_body, text_colour());
    text_at(dc, L"Connected", 787, TITLE_H + 635, 200, 16, font_caption, secondary_colour());
    {
        RECT line = {749, TITLE_H + 654, 1112, TITLE_H + 655};
        fill(dc, &line, divider_colour());
    }
    text_at(dc, L"Bluetooth", 755, TITLE_H + 667, 200, 20, font_body, text_colour());
    text_at(dc, L"On", 1031, TITLE_H + 667, 30, 20, font_body, text_colour());
    {   /* on, as it is: the controller is connected through it; the switch is not one this shell turns */
        RECT track = {1067, TITLE_H + 667, 1107, TITLE_H + 687};
        RECT thumb = {1091, TITLE_H + 671, 1103, TITLE_H + 683};
        rounded(dc, track, 10, accent_colour(), accent_colour());
        rounded(dc, thumb, 6, RGB(255, 255, 255), RGB(255, 255, 255));
    }
    {
        RECT b1 = {755, TITLE_H + 711, 876, TITLE_H + 743}, b2 = {884, TITLE_H + 711, 977, TITLE_H + 743};
        rounded(dc, b1, 4, combo_face(), combo_edge());
        rounded(dc, b2, 4, combo_face(), combo_edge());
        text_in(dc, L"View all devices", b1, font_body, text_colour(), DT_CENTER | DT_VCENTER);
        text_in(dc, L"Add device", b2, font_body, text_colour(), DT_CENTER | DT_VCENTER);
        add_item(b1.left, b1.top, b1.right, b1.bottom, ACT_LATER, PG_BLUETOOTH, L"View all devices");
        add_item(b2.left, b2.top, b2.right, b2.bottom, ACT_LATER, PG_BLUETOOTH, L"Add device");
    }
}

static void paint_preview(HDC dc)
{
    RECT box = {324, TITLE_H + 60, 324 + 352, TITLE_H + 280}, shadow = box, inner = box;
    InflateRect(&shadow, 1, 1);
    rounded(dc, shadow, 9, dark ? RGB(24, 24, 24) : RGB(219, 226, 230), dark ? RGB(24, 24, 24) : RGB(219, 226, 230));
    {   /* the picture and its taskbar inside the preview's 8 px corners */
        HRGN corners = CreateRoundRectRgn(box.left, box.top, box.right + 1, box.bottom + 1, 16, 16);
        SelectClipRgn(dc, corners);
        draw_bitmap(dc, &inner, preview_for(352, 220));
        sprite(dc, PW_SETTINGS_PREVIEW_WIN, 408, TITLE_H + 108);
        sprite(dc, PW_SETTINGS_PREVIEW_TASKBAR, 324, TITLE_H + 260);
        SelectClipRgn(dc, NULL);
        DeleteObject(corners);
    }
}

static void paint_personalization(HDC dc)
{
    static const struct { const WCHAR *title, *line; int open; } cards[] = {
        {L"Background", L"Background image, color, slideshow", PG_BACKGROUND},
        {L"Colors", L"Accent color, transparency effects, color theme", PG_COLORS},
        {L"Themes", L"Install, create, manage", -1},
        {L"Dynamic Lighting", L"Connected devices, effects, app settings", -1},
        {L"Lock screen", L"Lock screen images, apps, animations", -1},
        {L"Text input", L"Touch keyboard, voice typing, emoji and more, input method editor", -1},
        {L"Start", L"Recent apps and items, folders", -1},
        {L"Taskbar", L"Taskbar behaviors, system pins", -1},
    };
    paint_title(dc, NULL, 0, L"Personalization");
    paint_preview(dc);
    text_at(dc, L"Select a theme to apply", 708, TITLE_H + 60, 420, 20, font_strong, text_colour());
    paint_themes(dc, 708, 92, 1);
    for (int i = 0; i < (int)ARRAY_SIZE(cards); ++i)
        paint_card(dc, 308 + i * 72, PW_SETTINGS_CARD_ICON_P1 + i, cards[i].title, cards[i].line, TRUE,
                   cards[i].open >= 0 ? ACT_OPEN : ACT_LATER, cards[i].open >= 0 ? cards[i].open : PG_PERSONALIZATION);
}

static int wallpaper_rows(void) { return wallpaper_count > 0 ? (wallpaper_count + WALLPAPER_COLUMNS - 1) / WALLPAPER_COLUMNS : 1; }
static RECT picture_cell(int index)
{
    RECT cell = {383 + (index % WALLPAPER_COLUMNS) * (wall_cell_width + wall_gap),
                 TITLE_H + 412 + (index / WALLPAPER_COLUMNS) * (wall_cell_height + wall_label), 0, 0};
    cell.right = cell.left + wall_cell_width;
    cell.bottom = cell.top + wall_cell_height;
    return cell;
}
static void paint_background_page(HDC dc)
{
    int body_bottom = 412 + wallpaper_rows() * (wall_cell_height + wall_label) + 4;
    paint_title(dc, L"Personalization", PG_PERSONALIZATION, L"Background");
    paint_preview(dc);
    paint_card(dc, 308, PW_SETTINGS_CARD_ICON_P1, L"Personalize your background",
               L"A picture background applies to your current desktop.", FALSE, ACT_NONE, 0);
    {
        RECT body = {324, TITLE_H + 375, 324 + 804, TITLE_H + body_bottom};
        rounded(dc, body, 4, card_colour(), card_edge());
        text_at(dc, L"Choose a picture", 383, TITLE_H + 384, 400, 20, font_strong, text_colour());
    }
    for (int i = 0; i < wallpaper_count; ++i)
    {
        RECT cell = picture_cell(i), inner = cell;
        BOOL current = i == wallpaper_current;
        if (current)
        {
            RECT ring = cell;
            InflateRect(&ring, 3, 3);
            rounded(dc, ring, 6, accent_colour(), accent_colour());
        }
        rounded(dc, cell, 4, card_colour(), card_edge());
        InflateRect(&inner, -1, -1);
        draw_bitmap(dc, &inner, wallpaper_preview(i));
        text_at(dc, wallpapers[i].name, cell.left, cell.bottom + 6, wall_cell_width, 16, font_caption, current ? accent_colour() : text_colour());
        add_item(cell.left, cell.top, cell.right, cell.bottom, ACT_PICTURE, i, wallpapers[i].name);
    }
    paint_card(dc, body_bottom + 4, PW_SETTINGS_CARD_ICON_P1, L"Choose a photo",
               wallpaper_saved ? (wallpaper_live ? L"Done. Your desktop shows it now." : L"Saved. It shows the next time MihawkOS starts.")
                               : L"A BMP picture from your files, fitted to the screen", FALSE, ACT_NONE, 0);
    {
        int top = TITLE_H + body_bottom + 4 + 18;
        RECT button = {324 + 804 - 16 - 130, top, 324 + 804 - 16, top + 32};
        rounded(dc, button, 4, is_hot(button.left, button.top) ? subtle_colour() : combo_face(), combo_edge());
        text_in(dc, L"Browse photos", button, font_body, text_colour(), DT_CENTER | DT_VCENTER);
        add_item(button.left, button.top, button.right, button.bottom, ACT_BROWSE, 0, L"Browse photos");
    }
}

static void paint_colors(HDC dc)
{
    paint_title(dc, L"Personalization", PG_PERSONALIZATION, L"Colors");
    paint_preview(dc);
    paint_card(dc, 308, PW_SETTINGS_CARD_ICON_P2, L"Choose your mode", L"Change the colors that appear in MihawkOS and your apps",
               FALSE, ACT_NONE, 0);
    paint_combo(dc, 1111, 326, dark ? L"Dark" : L"Light", L"Light", 0);
}

static void paint_display(HDC dc)
{
    WCHAR resolution[64];
    paint_title(dc, L"System", PG_SYSTEM, L"Display");
    text_at(dc, L"Brightness & color", 325, TITLE_H + 60, 400, 20, font_strong, text_colour());
    paint_card(dc, 86, PW_SETTINGS_ICON_BRIGHTNESS, L"Brightness", L"Adjust the brightness of the built-in display", FALSE, ACT_LATER, PG_DISPLAY);
    sprite(dc, PW_SETTINGS_SLIDER, 871, TITLE_H + 104);
    sprite(dc, PW_SETTINGS_EXPAND, 1087, TITLE_H + 104);
    paint_card(dc, 158, PW_SETTINGS_ICON_NIGHT, L"Night light", L"Use warmer colors to help block blue light", TRUE, ACT_LATER, PG_DISPLAY);
    text_at(dc, L"Off", 1005, TITLE_H + 182, 30, 20, font_body, text_colour());
    sprite(dc, PW_SETTINGS_TOGGLE_OFF, 1041, TITLE_H + 182);
    paint_card(dc, 230, PW_SETTINGS_ICON_HDR, L"HDR", L"More about HDR", TRUE, ACT_LATER, PG_DISPLAY);
    text_at(dc, L"Scale & layout", 325, TITLE_H + 324, 400, 20, font_strong, text_colour());
    paint_card(dc, 350, PW_SETTINGS_ICON_SCALE, L"Scale", L"Change the size of text, apps, and other items", TRUE, ACT_LATER, PG_DISPLAY);
    paint_combo(dc, 1081, 368, L"100% (Recommended)", NULL, 1);
    wsprintfW(resolution, L"%d \x00d7 %d (Recommended)", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    paint_card(dc, 422, PW_SETTINGS_ICON_RESOLUTION, L"Display resolution", L"Adjust the resolution to fit your connected display", FALSE, ACT_NONE, 0);
    paint_combo(dc, 1111, 440, resolution, NULL, 2);
    paint_card(dc, 494, PW_SETTINGS_ICON_ORIENTATION, L"Display orientation", NULL, FALSE, ACT_NONE, 0);
    paint_combo(dc, 1111, 512, L"Landscape", L"Landscape (flipped)", 3);
    paint_card(dc, 566, PW_SETTINGS_ICON_MULTIPLE, L"Multiple displays", L"Choose the presentation mode for your displays", FALSE, ACT_LATER, PG_DISPLAY);
    sprite(dc, PW_SETTINGS_EXPAND, 1087, TITLE_H + 584);
    text_at(dc, L"Related settings", 325, TITLE_H + 660, 400, 20, font_strong, text_colour());
    paint_card(dc, 686, PW_SETTINGS_ICON_ADVANCED, L"Advanced display", L"Display information, refresh rate", TRUE, ACT_LATER, PG_DISPLAY);
}

static void paint_system(HDC dc)
{
    RTL_OSVERSIONINFOEXW info;
    WCHAR text[96];
    paint_title(dc, NULL, 0, L"System");
    paint_card(dc, 60, PW_SETTINGS_ICON_RESOLUTION, L"Display", L"Monitors, brightness, night light, display profile", TRUE, ACT_OPEN, PG_DISPLAY);
    paint_card(dc, 132, PW_SETTINGS_ICON_BRIGHTNESS, L"Sound", L"Volume levels, output, input, sound devices", TRUE, ACT_LATER, PG_SYSTEM);
    memset(&info, 0, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    RtlGetVersion(&info);
    wsprintfW(text, L"MihawkOS, system version %lu.%lu (build %lu)", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
    paint_card(dc, 204, PW_SETTINGS_ICON_ADVANCED, L"About", text, FALSE, ACT_NONE, 0);
}

static void paint_later(HDC dc)
{
    paint_title(dc, NULL, 0, later_title[0] ? later_title : L"Settings");
    text_at(dc, L"This page comes later in MihawkOS: nothing here changes your system yet.", 324, TITLE_H + 66, 760, 20, font_body,
            secondary_colour());
}

static void paint_focus(HDC dc)
{
    if (!keyboard || focus < 0 || focus >= item_count) return;
    {
        RECT ring = items[focus].box;
        HPEN pen = CreatePen(PS_SOLID, 2, dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
        HGDIOBJ prior_pen = SelectObject(dc, pen), prior_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        InflateRect(&ring, 1, 1);
        RoundRect(dc, ring.left, ring.top, ring.right + 1, ring.bottom + 1, 8, 8);
        SelectObject(dc, prior_pen); SelectObject(dc, prior_brush);
        DeleteObject(pen);
    }
}

static void paint_all(HDC dc)
{
    item_count = 0;
    paint_background(dc);
    paint_titlebar(dc);
    paint_pane(dc);
    switch (page)
    {
    case PG_HOME: paint_home(dc); break;
    case PG_SYSTEM: paint_system(dc); break;
    case PG_DISPLAY: paint_display(dc); break;
    case PG_PERSONALIZATION: paint_personalization(dc); break;
    case PG_BACKGROUND: paint_background_page(dc); break;
    case PG_COLORS: paint_colors(dc); break;
    default: paint_later(dc); break;
    }
    if (focus >= item_count) focus = item_count - 1;
    paint_focus(dc);
}

/* Double-buffered, so a repaint never shows a half-drawn page. */
static void paint(void)
{
    PAINTSTRUCT paint_struct;
    HDC dc = BeginPaint(window, &paint_struct), memory = CreateCompatibleDC(dc);
    HBITMAP buffer = CreateCompatibleBitmap(dc, client_width, client_height);
    HGDIOBJ prior = SelectObject(memory, buffer);
    paint_all(memory);
    BitBlt(dc, 0, 0, client_width, client_height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, prior);
    DeleteObject(buffer);
    DeleteDC(memory);
    EndPaint(window, &paint_struct);
}

/* ---------- going places ---------- */
static void go(int to, const WCHAR *title, BOOL remember)
{
    if (remember && history_count < (int)ARRAY_SIZE(history))
    {
        history[history_count].page = page;
        history[history_count].later_from = later_from;
        lstrcpynW(history[history_count].title, later_title, 64);
        ++history_count;
    }
    page = to;
    lstrcpynW(later_title, title ? title : L"", 64);
    focus = -1;
    hover = -1;
    InvalidateRect(window, NULL, FALSE);
}
static void go_back(void)
{
    if (!history_count) return;
    --history_count;
    page = history[history_count].page;
    later_from = history[history_count].later_from;
    lstrcpynW(later_title, history[history_count].title, 64);
    focus = -1;
    InvalidateRect(window, NULL, FALSE);
}

static void browse_photos(void)
{
    static const WCHAR filter[] = L"Pictures (*.bmp)\0*.bmp\0All files (*.*)\0*.*\0\0";
    WCHAR path[MAX_PATH] = L"";
    OPENFILENAMEW dialog;

    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Browse photos";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog) && apply_wallpaper(path))
    {
        wallpapers_load();
        wallpaper_saved = TRUE;
        InvalidateRect(window, NULL, FALSE);
    }
}

/* A combo's list: Color mode chooses Light or Dark; the display's show the one value the display has. */
static void open_combo(const struct item *it)
{
    HMENU menu = CreatePopupMenu();
    POINT at = {it->box.left, it->box.bottom};
    UINT chosen;
    if (!menu) return;
    if (it->arg == 0)
    {
        AppendMenuW(menu, MF_STRING | (dark ? 0 : MF_CHECKED), 1, L"Light");
        AppendMenuW(menu, MF_STRING | (dark ? MF_CHECKED : 0), 2, L"Dark");
    }
    else AppendMenuW(menu, MF_STRING | MF_CHECKED, 3, it->title);
    ClientToScreen(window, &at);
    chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, at.x, at.y, 0, window, NULL);
    DestroyMenu(menu);
    if (chosen == 1 || chosen == 2) apply_appearance(chosen == 2);
}

static void activate(int index)
{
    const struct item *it;
    if (index < 0 || index >= item_count) return;
    it = &items[index];
    switch (it->action)
    {
    case ACT_NAV: if (it->arg != page) go(it->arg, NULL, TRUE); break;
    case ACT_OPEN: go(it->arg, NULL, TRUE); break;
    case ACT_LATER: go(PG_LATER, it->title, TRUE); break;
    case ACT_THEME: apply_theme(it->arg); break;
    case ACT_PICTURE: choose_wallpaper(it->arg); break;
    case ACT_BROWSE: browse_photos(); break;
    case ACT_COMBO: open_combo(it); break;
    case ACT_BACK: go_back(); break;
    case ACT_MIN: ShowWindow(window, SW_MINIMIZE); break;
    case ACT_MAX: ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE); break;
    case ACT_CLOSE: PostMessageW(window, WM_CLOSE, 0, 0); break;
    case ACT_SEARCH: search_focus = TRUE; InvalidateRect(window, NULL, FALSE); break;
    }
}

static int item_at(POINT at)
{
    for (int i = item_count - 1; i >= 0; --i) if (PtInRect(&items[i].box, at)) return i;
    return -1;
}

/* The next item the arrow keys reach: the nearest one that way, preferring one in line. */
/* Where the keys start on a page nobody has moved on yet: the applied picture, else the page's first control. */
static int page_start(void)
{
    for (int i = 0; i < item_count; ++i) if (items[i].action == ACT_PICTURE && items[i].arg == wallpaper_current) return i;
    for (int i = 0; i < item_count; ++i) if (items[i].box.left >= 308 && items[i].box.top >= TITLE_H) return i;
    return item_count ? 0 : -1;
}
/* The nearest item in a direction; the first arrow on a page starts from page_start() and moves from it. */
static int item_toward(int from, int dx, int dy)
{
    int best = -1;
    long best_score = 0x7fffffff;
    POINT here;
    if (from < 0 || from >= item_count) from = page_start();
    if (from < 0) return -1;
    here.x = (items[from].box.left + items[from].box.right) / 2;
    here.y = (items[from].box.top + items[from].box.bottom) / 2;
    for (int i = 0; i < item_count; ++i)
    {
        int x = (items[i].box.left + items[i].box.right) / 2, y = (items[i].box.top + items[i].box.bottom) / 2;
        long along = dx ? (long)(x - here.x) * dx : (long)(y - here.y) * dy;
        long across = dx ? labs(y - here.y) : labs(x - here.x);
        if (i == from || along <= 0 || (items[i].action >= ACT_BACK && items[i].action <= ACT_CLOSE)) continue;
        if (along + across * 3 < best_score) { best_score = along + across * 3; best = i; }
    }
    return best >= 0 ? best : from;
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    switch (message)
    {
    case WM_NCCALCSIZE:
        if (w)
        {
            /* Keep the frame (the invisible resize border and its visible pixel); the caption becomes client, as File
             * Explorer's (wine/ps5/pw_shell_explorer.h): the title bar is the prototype's, drawn here. */
            RECT *r = &((NCCALCSIZE_PARAMS *)l)->rgrc[0];
            int left = r->left, top = r->top;
            LRESULT result = DefWindowProcW(hwnd, message, w, l);
            r->top = top + (r->left - left);
            return result;
        }
        break;
    case WM_NCHITTEST:
    {
        LRESULT result = DefWindowProcW(hwnd, message, w, l);
        if (result == HTCLIENT)
        {
            POINT at = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(hwnd, &at);
            if (at.y < TITLE_H && item_at(at) < 0) result = HTCAPTION;
        }
        return result;
    }
    case WM_SIZE:
        client_width = LOWORD(l);
        client_height = HIWORD(l);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        dark = appearance_is_dark();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_CHAR:
        if (!search_focus) return 0;
        if (w >= 32 && lstrlenW(search_text) < 30)
        {
            int length = lstrlenW(search_text);
            search_text[length] = (WCHAR)w; search_text[length + 1] = 0;
        }
        else if (w == 8 && search_text[0]) search_text[lstrlenW(search_text) - 1] = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        keyboard = TRUE;
        if (search_focus && (w == VK_ESCAPE || w == VK_RETURN || w == VK_DOWN))
        {
            if (w == VK_ESCAPE) search_text[0] = 0;
            search_focus = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (search_focus) return 0;
        switch (w)
        {
        case VK_LEFT: focus = item_toward(focus, -1, 0); break;
        case VK_RIGHT: focus = item_toward(focus, 1, 0); break;
        case VK_UP: focus = item_toward(focus, 0, -1); break;
        case VK_DOWN: focus = item_toward(focus, 0, 1); break;
        case VK_TAB: focus = item_count ? (focus + (GetKeyState(VK_SHIFT) < 0 ? item_count - 1 : 1)) % item_count : -1; break;
        case VK_RETURN: case VK_SPACE: if (focus >= 0) activate(focus); else focus = page_start(); break;
        case VK_ESCAPE: case VK_BACK: case VK_BROWSER_BACK: go_back(); break;
        default: return 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE:
    {
        POINT at = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int now = item_at(at);
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        if (now != hover) { hover = now; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hover >= 0) { hover = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT at = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        pressed = item_at(at);
        keyboard = FALSE;
        return 0;
    }
    case WM_LBUTTONUP:
    {
        POINT at = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int now = item_at(at);
        BOOL was = search_focus;
        search_focus = FALSE;
        if (now >= 0 && now == pressed) { focus = now; activate(now); }
        else if (was) InvalidateRect(hwnd, NULL, FALSE);
        pressed = -1;
        return 0;
    }
    case WM_XBUTTONUP:
        if (GET_XBUTTON_WPARAM(w) == XBUTTON1) go_back();
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}

static int font_quality = ANTIALIASED_QUALITY;   /* grey smoothing, as Chromium draws the prototype's text */
static HFONT make_font(int height, int weight)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, font_quality, DEFAULT_PITCH, L"Proton UI");
}
static void make_fonts(void)
{
    font_caption = make_font(12, FW_NORMAL);
    font_body = make_font(14, FW_NORMAL);
    font_strong = make_font(14, FW_SEMIBOLD);
    font_subtitle = make_font(20, FW_SEMIBOLD);
    font_title = make_font(28, FW_SEMIBOLD);
}

/* The page an argument names: "home", "system", "display", "personalization", "background", "colors" (a path such as
 * "personalization/background" names its last part). */
static int page_named(const WCHAR *arguments)
{
    static const struct { const WCHAR *name; int page; } names[] = {
        {L"background", PG_BACKGROUND}, {L"colors", PG_COLORS}, {L"display", PG_DISPLAY},
        {L"personalization", PG_PERSONALIZATION}, {L"system", PG_SYSTEM}, {L"home", PG_HOME},
    };
    for (int i = 0; i < (int)ARRAY_SIZE(names); ++i) if (StrStrIW(arguments, names[i].name)) return names[i].page;
    return PG_HOME;
}

/* --render=<page>[,dark] --out=<file.bmp> [--wallpapers=<folder>]: the page drawn into a picture of the client
 * (1178 x 798, the prototype window less its frame), with no window, for tools/run-settings-render.py. */
static int render(const WCHAR *arguments)
{
    const WCHAR *out = StrStrIW(arguments, L"--out="), *folder = StrStrIW(arguments, L"--wallpapers=");
    WCHAR path[MAX_PATH], *end;
    BITMAPINFO info;
    void *bits = NULL;
    HBITMAP target;
    HDC dc;

    if (!out) return 2;
    lstrcpynW(path, out + 6, MAX_PATH);
    if ((end = wcschr(path, L' '))) *end = 0;
    {   /* --current=<id>: the picture the desktop shows, as the page to compare was captured with it */
        const WCHAR *current = StrStrIW(arguments, L"--current=");
        if (current)
        {
            WCHAR id[64];
            lstrcpynW(id, current + 10, 64);
            if ((end = wcschr(id, L' '))) *end = 0;
            wallpaper_forced = TRUE;
            lstrcpyW(forced_id, id);
        }
    }
    {   /* --lift=<n> and --quality=<n>: the text's placement and smoothing, to tune them against the prototype */
        const WCHAR *lift = StrStrIW(arguments, L"--lift="), *quality = StrStrIW(arguments, L"--quality=");
        if (lift) text_lift = lift[7] - L'0';
        if (quality) font_quality = quality[10] - L'0', make_fonts();
    }
    if (folder)
    {
        lstrcpynW(shipped_folder, folder + 13, MAX_PATH);
        if ((end = wcschr(shipped_folder, L' '))) *end = 0;
        wallpapers_load();
    }
    if (wallpaper_forced) wallpaper_current = wallpaper_index(forced_id);
    dark = StrStrIW(arguments, L",dark") != NULL;
    page = page_named(StrStrIW(arguments, L"--render="));
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = client_width;
    info.bmiHeader.biHeight = -client_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    if (!(target = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0))) return 3;
    dc = CreateCompatibleDC(NULL);
    SelectObject(dc, target);
    paint_all(dc);
    GdiFlush();
    return write_bmp(path, bits, client_width, client_height) ? 0 : 4;
}

void mainCRTStartup(void)
{
    WNDCLASSEXW type;
    MSG message;
    const WCHAR *arguments = PathGetArgsW(GetCommandLineW());

    themes = LoadLibraryW(L"uxtheme.dll");
    dark = appearance_is_dark();
    make_fonts();
    screen_width = GetSystemMetrics(SM_CXSCREEN);
    screen_height = GetSystemMetrics(SM_CYSCREEN);
    wallpapers_load();
    if (StrStrIW(arguments, L"--render=")) ExitProcess(render(arguments));
    /* Display settings opens Display, Personalize opens Personalization; the arguments only (the path holds "system32") */
    page = page_named(arguments);
    {
        HMODULE icons;
        WCHAR icons_path[MAX_PATH];
        GetWindowsDirectoryW(icons_path, MAX_PATH);
        lstrcatW(icons_path, L"\\resources\\mihawk\\icons.dll");
        if ((icons = LoadLibraryW(icons_path))) caption_icon = LoadImageW(icons, MAKEINTRESOURCEW(1011), IMAGE_ICON, 16, 16, 0);
    }

    memset(&type, 0, sizeof(type));
    type.cbSize = sizeof(type);
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleW(NULL);
    type.hCursor = LoadCursorW(NULL, IDC_ARROW);
    type.hIcon = caption_icon;
    type.hIconSm = caption_icon;
    type.lpszClassName = L"MihawkSettings";
    type.hbrBackground = NULL;
    if (!RegisterClassExW(&type)) return;
    window = CreateWindowExW(0, type.lpszClassName, L"Settings", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             (screen_width - window_width) / 2, (screen_height - window_height) / 2,
                             window_width, window_height, NULL, NULL, type.hInstance, NULL);
    if (!window) return;
    {
        DWORD corner = DWMWCP_ROUND;
        RECT outer, visible;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        /* the visible window is the prototype's 1180 x 800: its rect adds the frame's invisible part */
        GetWindowRect(window, &outer);
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible))))
            SetWindowPos(window, NULL, 0, 0, window_width + (outer.right - outer.left) - (visible.right - visible.left),
                         window_height + (outer.bottom - outer.top) - (visible.bottom - visible.top), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
