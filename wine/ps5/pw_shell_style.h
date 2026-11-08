/* Mihawk shell appearance. GUI-thread resources, refreshed on theme changes.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SHELL_STYLE_H
#define PW_SHELL_STYLE_H
#include <windows.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <dwmapi.h>
#include "appearance.h"
#include "icons.h"

#include "pw_accent.h"   /* the applied theme's accent (Settings > Themes) */
struct pw_shell_style { COLORREF surface, pane, text, line, accent, hover, selected; HFONT font, heading; HBRUSH background, paper; BOOL dark; };
static inline BOOL pw_shell_enabled(void)
{
    WCHAR mode[2];
    return GetEnvironmentVariableW(L"MIHAWK_DESKTOP", mode, 2) == 1 && mode[0] == '1';
}
static inline void pw_shell_style_free(struct pw_shell_style *s)
{
    if (s->font) DeleteObject(s->font);
    if (s->heading) DeleteObject(s->heading);
    if (s->background) DeleteObject(s->background);
    if (s->paper) DeleteObject(s->paper);
    s->font = s->heading = NULL; s->background = s->paper = NULL;
}
static inline void pw_shell_style_load(struct pw_shell_style *s)
{
    WCHAR path[MAX_PATH], expected[MAX_PATH], colour[64], size[64];
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    BOOL managed;
    pw_shell_style_free(s);
    GetWindowsDirectoryW(expected, MAX_PATH);
    lstrcatW(expected, L"\\resources\\themes\\light\\light.msstyles");
    managed = SUCCEEDED(GetCurrentThemeName(path, MAX_PATH, colour, 64, size, 64)) && !lstrcmpiW(path, expected);
    s->dark = managed && !lstrcmpW(colour, L"Dark");
    s->surface = managed ? (s->dark ? PW_APPEARANCE_DARK_MICA_FALLBACK : PW_APPEARANCE_LIGHT_MICA_FALLBACK) : GetSysColor(COLOR_BTNFACE);
    s->pane = managed ? (s->dark ? PW_APPEARANCE_DARK_PANE : PW_APPEARANCE_LIGHT_PANE) : GetSysColor(COLOR_WINDOW);
    s->text = managed ? (s->dark ? PW_APPEARANCE_DARK_TEXT : PW_APPEARANCE_LIGHT_TEXT) : GetSysColor(COLOR_WINDOWTEXT);
    s->line = managed ? (s->dark ? PW_APPEARANCE_DARK_LINE : PW_APPEARANCE_LIGHT_LINE) : GetSysColor(COLOR_3DSHADOW);
    s->accent = managed ? pw_accent_colorref(pw_accent_fill(pw_accent_current(), s->dark)) : GetSysColor(COLOR_HIGHLIGHT);
    s->hover = managed ? (s->dark ? PW_APPEARANCE_DARK_HOVER : PW_APPEARANCE_LIGHT_HOVER) : GetSysColor(COLOR_3DLIGHT);
    s->selected = managed ? (s->dark ? PW_APPEARANCE_DARK_SELECTED : PW_APPEARANCE_LIGHT_SELECTED) : GetSysColor(COLOR_HIGHLIGHT);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        s->font = CreateFontIndirectW(&metrics.lfMenuFont);
        metrics.lfMenuFont.lfWeight = FW_SEMIBOLD;
        s->heading = CreateFontIndirectW(&metrics.lfMenuFont);
    }
    s->background = CreateSolidBrush(s->surface); s->paper = CreateSolidBrush(s->pane);
}
static inline void pw_shell_fill(HDC dc, const RECT *r, COLORREF colour)
{
    SetDCBrushColor(dc, colour); FillRect(dc, r, GetStockObject(DC_BRUSH));
}
static inline BOOL CALLBACK pw_shell_set_font(HWND hwnd, LPARAM font)
{
    SendMessageW(hwnd, WM_SETFONT, font, TRUE); return TRUE;
}
static inline LRESULT CALLBACK pw_shell_button_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    if (msg == WM_GETDLGCODE && l && ((MSG *)l)->wParam == VK_RETURN)
        return DefSubclassProc(hwnd, msg, w, l) | DLGC_WANTMESSAGE;
    if (msg == WM_KEYDOWN && w == VK_RETURN) { SendMessageW(hwnd, BM_CLICK, 0, 0); return 0; }
    if (msg == WM_MOUSEMOVE && !GetPropW(hwnd, L"MihawkHot"))
    {
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, hwnd, 0};
        if (TrackMouseEvent(&tracking)) SetPropW(hwnd, L"MihawkHot", (HANDLE)1);
        InvalidateRect(hwnd, NULL, FALSE);
    }
    else if (msg == WM_MOUSELEAVE) { RemovePropW(hwnd, L"MihawkHot"); InvalidateRect(hwnd, NULL, FALSE); }
    else if (msg == WM_NCDESTROY) { RemovePropW(hwnd, L"MihawkHot"); RemoveWindowSubclass(hwnd, pw_shell_button_proc, id); }
    return DefSubclassProc(hwnd, msg, w, l);
}
/* DWM surfaces for shell windows (docs/MIHAWK_OS.md, DWM surfaces). The
 * compositor rounds, shadows, borders and draws the material; the shell only
 * declares them through the public API and paints alpha-aware content. */
static inline void pw_shell_surface(HWND hwnd, DWORD corner, DWORD backdrop, const struct pw_shell_style *s, BOOL border)
{
    DWORD dark = s->dark, colour = border ? s->line : DWMWA_COLOR_NONE;
    MARGINS sheet = {-1, -1, -1, -1};
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &colour, sizeof(colour));
    if (backdrop != DWMSBT_NONE) DwmExtendFrameIntoClientArea(hwnd, &sheet);
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}
/* GDI writes colour with alpha 0. Over glass, transparent black stays
 * transparent and every other GDI pixel becomes opaque; premultiplied pixels
 * already carry their alpha. Our text colours are never pure black. */
static inline void pw_shell_glass_fix(HPAINTBUFFER paint)
{
    RGBQUAD *bits; int width; RECT r;
    if (FAILED(GetBufferedPaintBits(paint, &bits, &width)) || FAILED(GetBufferedPaintTargetRect(paint, &r))) return;
    for (int y = 0; y < r.bottom - r.top; ++y)
        for (int x = 0; x < r.right - r.left; ++x)
        {
            DWORD *pixel = (DWORD *)bits + (size_t)y * width + x;
            if (!(*pixel >> 24) && (*pixel & 0xffffff)) *pixel |= 0xff000000;
        }
}
static inline HPAINTBUFFER pw_shell_glass_begin(HDC dc, const RECT *rect, HDC *buffer)
{
    BP_PAINTPARAMS params = {sizeof(params), BPPF_ERASE};
    return IsRectEmpty(rect) ? NULL : BeginBufferedPaint(dc, rect, BPBF_TOPDOWNDIB, &params, buffer);
}
/* A child control we do not draw: make its GDI output opaque after it paints. */
static inline void pw_shell_glass_window(HWND hwnd)
{
    RECT window, r; HDC dc = GetWindowDC(hwnd), buffer; HPAINTBUFFER paint;
    if (!dc) return;
    GetWindowRect(hwnd, &window); SetRect(&r, 0, 0, window.right - window.left, window.bottom - window.top);
    if ((paint = BeginBufferedPaint(dc, &r, BPBF_TOPDOWNDIB, NULL, &buffer)))
    {
        BitBlt(buffer, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        pw_shell_glass_fix(paint); EndBufferedPaint(paint, TRUE);
    }
    ReleaseDC(hwnd, dc);
}
static inline LRESULT CALLBACK pw_shell_glass_child(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    LRESULT result = DefSubclassProc(hwnd, msg, w, l);
    (void)data;
    switch (msg)
    {
    case WM_PAINT: case WM_NCPAINT: case WM_PRINTCLIENT: case WM_ENABLE: case WM_SETTEXT:
    case WM_CHAR: case WM_KEYDOWN: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEWHEEL:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_VSCROLL: case WM_HSCROLL: case 0x0118 /* WM_SYSTIMER: caret */:
        pw_shell_glass_window(hwnd); break;
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, pw_shell_glass_child, id); break;
    }
    return result;
}
static inline BOOL CALLBACK pw_shell_glass_children(HWND hwnd, LPARAM unused)
{
    (void)unused; SetWindowSubclass(hwnd, pw_shell_glass_child, 2, 0); return TRUE;
}
/* Anti-aliased text with real alpha (DTT_COMPOSITED) in the DC's font: an
 * absent font property keeps the selected font. Without a theme, GDI text is
 * made opaque instead. */
static inline void pw_shell_glass_text(HTHEME theme, HPAINTBUFFER paint, HDC dc, const WCHAR *text, RECT *rect,
                                       COLORREF colour, DWORD flags)
{
    DTTOPTS options = {sizeof(options), DTT_COMPOSITED | DTT_TEXTCOLOR | DTT_FONTPROP};
    options.crText = colour; options.iFontPropId = 0;
    if (theme && SUCCEEDED(DrawThemeTextEx(theme, dc, 0, 0, text, -1, flags, rect, &options))) return;
    SetTextColor(dc, colour); SetBkMode(dc, TRANSPARENT); DrawTextW(dc, text, -1, rect, flags);
    if (paint) pw_shell_glass_fix(paint);
}
/* Premultiplied fill: opacity 255 is an opaque fill. */
static inline void pw_shell_glass_fill(HPAINTBUFFER paint, HDC dc, const RECT *rect, COLORREF colour, BYTE opacity)
{
    COLORREF premultiplied = RGB(GetRValue(colour) * opacity / 255, GetGValue(colour) * opacity / 255, GetBValue(colour) * opacity / 255);
    pw_shell_fill(dc, rect, premultiplied);
    BufferedPaintSetAlpha(paint, rect, opacity);
}
void pw_shell_start(HWND owner);
/* Start opened on its Search view (the taskbar's search pill). */
void pw_shell_search(HWND owner);
/* The taskbar's flyouts (pw_shell_flyouts.c): Quick Settings (the status button), the notification centre and calendar
 * (the clock). */
void pw_shell_quick_settings(HWND owner);
void pw_shell_notifications(HWND owner);
#endif
