/* The taskbar's flyouts as the approved prototype draws them (prototypes/windows11-shell css/flyouts.css), each a
 * .surface-flyout on the prototype's acrylic (PS5_DWMSBT_FLYOUT, server patch 1160), 360 px wide, 12 px from the
 * screen's right edge and from the taskbar, with what the console really has:
 *
 *  - Quick Settings (the status button): the network tile, on while an adapter is up with an address (Wi-Fi's glyph
 *    for a wireless one, the prototype's globe for a wired one), and the footer's Settings button. The prototype's other tiles, sliders and battery have
 *    nothing behind them here (Wine's endpoint volume is a stub and the title has no master gain yet), so they are
 *    not drawn.
 *  - The notification centre and the calendar (the clock): no program posts notifications to this shell, so the
 *    centre shows the prototype's empty state; the calendar is the month of today with its paging. The prototype's
 *    Focus timer and do-not-disturb bell are not drawn.
 *
 * Fixed chrome is the prototype's own pixels (themes/windows11/flyouts through tools/build-shell-sprites.py); dates and
 * labels are text. SPDX-License-Identifier: GPL-3.0-or-later */
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include "pw_shell_style.h"
#include "pw_shell_taskbar.h"

#define PW_FLYOUT_BACKDROP 7  /* PS5_DWMSBT_FLYOUT: the prototype's acrylic, flyout stroke and shadow */
#define PW_FLYOUT_WIDTH 360
#define PW_FLYOUT_SETTINGS 100
#define PW_FLYOUT_TOGGLE 101
#define PW_FLYOUT_UP 102
#define PW_FLYOUT_DOWN 103
enum pw_flyout_kind { PW_FLYOUT_QUICK, PW_FLYOUT_CENTRE, PW_FLYOUT_CALENDAR };
struct pw_flyout
{
    HWND window, partner;   /* the notification centre and the calendar close together */
    enum pw_flyout_kind kind;
    struct pw_shell_style style;
    HFONT body, strong, caption;
    HTHEME theme;
    BOOL connected, wireless, collapsed;
    int year, month;        /* the calendar's page */
};
static HWND pw_flyout_open[3];

static HFONT pw_flyout_font(int height, int weight)
{
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return NULL;
    metrics.lfMenuFont.lfHeight = -height; metrics.lfMenuFont.lfWeight = weight;
    return CreateFontIndirectW(&metrics.lfMenuFont);
}
static void pw_flyout_text(struct pw_flyout *f, HPAINTBUFFER glass, HDC dc, HFONT font, const WCHAR *text, RECT *r,
                           COLORREF colour, DWORD flags)
{
    HGDIOBJ prior = SelectObject(dc, font ? font : f->style.font);
    pw_shell_glass_text(f->theme, glass, dc, text, r, colour, flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, prior);
}
/* An adapter that is up and has an address, as Windows' network icon reports "connected". */
static void pw_flyout_network(struct pw_flyout *f)
{
    ULONG size = 0; IP_ADAPTER_ADDRESSES *list, *a;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    f->connected = f->wireless = FALSE;
    if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &size) != ERROR_BUFFER_OVERFLOW || !size || size > (1u << 20)) return;
    if (!(list = HeapAlloc(GetProcessHeap(), 0, size))) return;
    if (!GetAdaptersAddresses(AF_UNSPEC, flags, NULL, list, &size))
        for (a = list; a; a = a->Next)
            if (a->IfType != IF_TYPE_SOFTWARE_LOOPBACK && a->OperStatus == IfOperStatusUp && a->FirstUnicastAddress)
            {
                f->connected = TRUE;
                if (a->IfType == IF_TYPE_IEEE80211) f->wireless = TRUE;
            }
    HeapFree(GetProcessHeap(), 0, list);
}
static void pw_flyout_place(struct pw_flyout *f)
{
    RECT area; int height = f->kind == PW_FLYOUT_QUICK ? 440 : f->kind == PW_FLYOUT_CENTRE ? 150 : f->collapsed ? 50 : 390;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) return;
    int bottom = area.bottom - 12;
    /* the centre stands 12 px above the calendar */
    if (f->kind == PW_FLYOUT_CENTRE && f->partner)
    {
        struct pw_flyout *calendar = (void *)GetWindowLongPtrW(f->partner, GWLP_USERDATA);
        bottom -= (calendar && calendar->collapsed ? 50 : 390) + 12;
    }
    SetWindowPos(f->window, NULL, area.right - 12 - PW_FLYOUT_WIDTH, bottom - height, PW_FLYOUT_WIDTH, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
static void pw_flyout_appearance(struct pw_flyout *f)
{
    DWORD corner = DWMWCP_ROUND, dark = f->style.dark, backdrop = PW_FLYOUT_BACKDROP, colour = DWMWA_COLOR_DEFAULT;
    MARGINS sheet = {-1, -1, -1, -1};
    if (f->body) DeleteObject(f->body);
    if (f->strong) DeleteObject(f->strong);
    if (f->caption) DeleteObject(f->caption);
    f->body = pw_flyout_font(14, FW_NORMAL); f->strong = pw_flyout_font(14, FW_SEMIBOLD); f->caption = pw_flyout_font(12, FW_NORMAL);
    DwmSetWindowAttribute(f->window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DwmSetWindowAttribute(f->window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(f->window, DWMWA_BORDER_COLOR, &colour, sizeof(colour));
    DwmExtendFrameIntoClientArea(f->window, &sheet);
    DwmSetWindowAttribute(f->window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    if (f->theme) CloseThemeData(f->theme);
    f->theme = OpenThemeData(f->window, L"WINDOW");
}
/* The prototype's accent disc behind today's number: 40 px across, sampled 4 x 4 per pixel, premultiplied. */
static void pw_flyout_disc(HDC dc, int cx, int cy, COLORREF colour)
{
    BITMAPINFO info = {{sizeof(BITMAPINFOHEADER), 40, -40, 1, 32, BI_RGB}};
    void *bits = NULL; HBITMAP disc = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0); HDC source;
    if (!disc || !bits || !(source = CreateCompatibleDC(dc))) { if (disc) DeleteObject(disc); return; }
    DWORD *p = bits;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x)
        {
            unsigned inside = 0;
            for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i)
            {
                float dx = x + (i + 0.5f) / 4 - 20, dy = y + (j + 0.5f) / 4 - 20;
                inside += dx * dx + dy * dy <= 400.0f;
            }
            unsigned a = (inside * 255 + 8) / 16;
            p[y * 40 + x] = a << 24 | (GetRValue(colour) * a / 255) << 16 | (GetGValue(colour) * a / 255) << 8 | (GetBValue(colour) * a / 255);
        }
    HGDIOBJ prior = SelectObject(source, disc); BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc, cx - 20, cy - 20, 40, 40, source, 0, 0, 40, 40, blend);
    SelectObject(source, prior); DeleteDC(source); DeleteObject(disc);
}
static int pw_flyout_days(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && ((year % 4 == 0 && year % 100) || year % 400 == 0) ? 29 : days[month - 1];
}
static void pw_flyout_paint(struct pw_flyout *f, HDC dc, HPAINTBUFFER glass)
{
    BOOL dark = f->style.dark; RECT r, client; WCHAR text[96];
    COLORREF secondary = dark ? PW_APPEARANCE_DARK_SECONDARY : PW_APPEARANCE_LIGHT_SECONDARY;
    GetClientRect(f->window, &client);
    if (f->kind == PW_FLYOUT_QUICK)
    {
        /* The prototype's eight tiles (js/quicksettings.js TILES order) in its 3 x 3 grid: 24 px in, 95 px columns
         * with 12 px gaps, 84 px rows; a 48 px button and its 12 px label 6 px below. Plates and glyphs are the
         * prototype's own pixels; a split tile carries the chevron in its 35 px second button, a page-only tile
         * inside its one button. The shell reports the adapter it has (a wired one draws the globe), and Bluetooth
         * is drawn on as the prototype shows it: the console has no radio to report. */
        static const struct pw_quick_tile
        {
            int x, y, glyph_x; unsigned plate, glyph, chevron; const WCHAR *label;
        } tiles[] = {
            {25, 25, 22, PW_SPRITE_FLYOUT_PLATE_SPLIT_ON, PW_SPRITE_FLYOUT_GLYPH_WIFI, PW_SPRITE_FLYOUT_GLYPH_CHEVRON, L"Wi-Fi"},
            {132, 25, 22, PW_SPRITE_FLYOUT_PLATE_SPLIT_ON, PW_SPRITE_FLYOUT_GLYPH_BLUETOOTH, PW_SPRITE_FLYOUT_GLYPH_CHEVRON, L"Bluetooth"},
            {240, 25, 39, PW_SPRITE_FLYOUT_PLATE_SIMPLE_OFF, PW_SPRITE_FLYOUT_GLYPH_AIRPLANE, 0, L"Airplane mode"},
            {25, 109, 40, PW_SPRITE_FLYOUT_PLATE_SIMPLE_OFF, PW_SPRITE_FLYOUT_GLYPH_BATTERY_SAVER, 0, L"Energy saver"},
            {132, 109, 40, PW_SPRITE_FLYOUT_PLATE_SIMPLE_OFF, PW_SPRITE_FLYOUT_GLYPH_MOON, 0, L"Night light"},
            {240, 109, 39, PW_SPRITE_FLYOUT_PLATE_SIMPLE_OFF, PW_SPRITE_FLYOUT_GLYPH_ACCESSIBILITY, PW_SPRITE_FLYOUT_GLYPH_CHEVRON,
             L"Accessibility"},
            {25, 193, 40, PW_SPRITE_FLYOUT_PLATE_SIMPLE_OFF, PW_SPRITE_FLYOUT_GLYPH_CAST, PW_SPRITE_FLYOUT_GLYPH_CHEVRON, L"Cast"},
            {132, 193, 40, PW_SPRITE_FLYOUT_PLATE_SIMPLE_OFF, PW_SPRITE_FLYOUT_GLYPH_PHONE, 0, L"Mobile hotspot"},
        };
        for (unsigned i = 0; i < ARRAY_SIZE(tiles); ++i)
        {
            /* the shell's own network state: a connected wireless adapter is the prototype's Wi-Fi tile, a wired one
               the globe, none the off plate; its label is the adapter it reports, not the prototype's demo network */
            unsigned plate = tiles[i].plate, glyph = tiles[i].glyph;
            const WCHAR *label = tiles[i].label;
            if (i == 0)
            {
                plate = !f->connected ? PW_SPRITE_FLYOUT_TILE_OFF : PW_SPRITE_FLYOUT_PLATE_SPLIT_ON;
                glyph = !f->connected ? PW_SPRITE_FLYOUT_GLYPH_WIFI_OFF : f->wireless ? PW_SPRITE_FLYOUT_GLYPH_WIFI : PW_SPRITE_FLYOUT_GLYPH_NETWORK;
                label = !f->connected ? L"Not connected" : f->wireless ? L"Wi-Fi" : L"Ethernet";
            }
            pw_taskbar_sprite(dc, dark, plate, tiles[i].x, tiles[i].y);
            pw_taskbar_sprite(dc, dark, glyph, tiles[i].x + tiles[i].glyph_x, tiles[i].y + 16);
            if (tiles[i].chevron) pw_taskbar_sprite(dc, dark, tiles[i].chevron, tiles[i].x + 70, tiles[i].y + 16);
            SetRect(&r, tiles[i].x, tiles[i].y + 54, tiles[i].x + 95, tiles[i].y + 70);
            pw_flyout_text(f, glass, dc, f->caption, label, &r, f->style.text, DT_CENTER | DT_TOP);
        }
        /* the bright and loud rows as the prototype drew them (its capture's 100% and 62%), then the footer */
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_SLIDER_BRIGHTNESS, 13, 285);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_SLIDER_VOLUME, 13, 329);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_FOOTER, 1, client.bottom - 49);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_BATTERY, 13, client.bottom - 42);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_EDIT, 263, client.bottom - 42);
        return;
    }
    if (f->kind == PW_FLYOUT_CENTRE)
    {
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_NC_HEADER, 1, 1);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_NC_EMPTY, 1, 53);
        return;
    }
    /* The calendar: today's long date, then the month's heading, the days of the week and six weeks of days. */
    SYSTEMTIME now; GetLocalTime(&now);
    GetDateFormatEx(L"en-US", 0, &now, L"dddd, MMMM d", text, ARRAY_SIZE(text), NULL);
    SetRect(&r, 17, 15, 300, 35);
    pw_flyout_text(f, glass, dc, f->strong, text, &r, f->style.text, DT_TOP | DT_LEFT);
    if (f->collapsed) return;
    SYSTEMTIME page = {(WORD)f->year, (WORD)f->month, 0, 1};
    GetDateFormatEx(L"en-US", 0, &page, L"MMMM yyyy", text, ARRAY_SIZE(text), NULL);
    SetRect(&r, 17, 65, 270, 85);
    pw_flyout_text(f, glass, dc, f->strong, text, &r, f->style.text, DT_TOP | DT_LEFT);
    pw_taskbar_sprite(dc, dark, PW_SPRITE_FLYOUT_CAL_DOW, 13, 97);
    /* Sunday-first weeks: 1 + (y + y/4 - y/100 + y/400 + t[m-1] + d) mod 7 (Sakamoto) */
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = f->year - (f->month < 3), first = (y + y / 4 - y / 100 + y / 400 + t[f->month - 1] + 1) % 7;
    int days = pw_flyout_days(f->year, f->month), before = pw_flyout_days(f->month == 1 ? f->year - 1 : f->year, f->month == 1 ? 12 : f->month - 1);
    for (int cell = 0; cell < 42; ++cell)
    {
        int day = cell - first + 1, column = cell % 7, row = cell / 7; BOOL other = day < 1 || day > days, today;
        int shown = day < 1 ? before + day : day > days ? day - days : day;
        today = !other && f->year == now.wYear && f->month == now.wMonth && day == now.wDay;
        /* cells are 334/7 px wide: the column's edges rounded */
        r.left = 13 + (column * 334 + 3) / 7; r.right = 13 + ((column + 1) * 334 + 3) / 7;
        r.top = 137 + row * 40; r.bottom = r.top + 40;
        if (today) pw_flyout_disc(dc, (r.left + r.right) / 2, r.top + 20, f->style.accent);
        wsprintfW(text, L"%d", shown);
        pw_flyout_text(f, glass, dc, today ? f->strong : f->body, text, &r,
                       today ? (dark ? RGB(0, 0, 0) : RGB(255, 255, 255)) : other ? secondary : f->style.text, DT_CENTER | DT_VCENTER);
    }
}
static void pw_flyout_close(struct pw_flyout *f)
{
    ShowWindow(f->window, SW_HIDE);
    if (f->partner && IsWindowVisible(f->partner)) ShowWindow(f->partner, SW_HIDE);
}
static LRESULT CALLBACK pw_flyout_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    struct pw_flyout *f = (void *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) { f = ((CREATESTRUCTW *)l)->lpCreateParams; f->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)f); }
    if (!f) return DefWindowProcW(hwnd, msg, w, l);
    switch (msg)
    {
    case WM_CLOSE: pw_flyout_close(f); return 0;
    case WM_ACTIVATE:
        /* light dismiss: a click anywhere but the flyout and its partner closes them */
        if (LOWORD(w) == WA_INACTIVE && (HWND)l != f->partner) pw_flyout_close(f);
        return 0;
    case WM_THEMECHANGED: case WM_SETTINGCHANGE:
        pw_shell_style_load(&f->style); pw_flyout_appearance(f);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN); return 0;
    case WM_ERASEBKGND: case WM_PRINTCLIENT:
    {
        RECT box; HDC dc = (HDC)w;
        if (GetClipBox(dc, &box) != ERROR) PatBlt(dc, box.left, box.top, box.right - box.left, box.bottom - box.top, BLACKNESS);
        return 1;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint; RECT client; HDC dc = BeginPaint(hwnd, &paint), buffer = NULL; HPAINTBUFFER glass;
        GetClientRect(hwnd, &client);
        if ((glass = pw_shell_glass_begin(dc, &client, &buffer))) { pw_flyout_paint(f, buffer, glass); EndBufferedPaint(glass, TRUE); }
        EndPaint(hwnd, &paint); return 0;
    }
    case WM_CTLCOLORBTN: return (LRESULT)GetStockObject(BLACK_BRUSH);
    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *draw = (const DRAWITEMSTRUCT *)l; RECT box = draw->rcItem; HDC dc = NULL; HPAINTBUFFER glass;
        unsigned sprite = draw->CtlID == PW_FLYOUT_SETTINGS ? PW_SPRITE_FLYOUT_SETTINGS : draw->CtlID == PW_FLYOUT_TOGGLE ? PW_SPRITE_FLYOUT_CAL_TOGGLE :
                          draw->CtlID == PW_FLYOUT_UP ? PW_SPRITE_FLYOUT_CAL_UP : PW_SPRITE_FLYOUT_CAL_DOWN;
        if (!(glass = pw_shell_glass_begin(draw->hDC, &box, &dc))) return TRUE;
        if (draw->CtlID == PW_FLYOUT_SETTINGS)
        {
            /* the footer bar lies under the button */
            RECT client; GetClientRect(hwnd, &client); RECT at; GetWindowRect(draw->hwndItem, &at); MapWindowPoints(NULL, hwnd, (POINT *)&at, 2);
            pw_taskbar_sprite(dc, f->style.dark, PW_SPRITE_FLYOUT_FOOTER, 1 - at.left, client.bottom - 49 - at.top);
        }
        if (GetPropW(draw->hwndItem, L"MihawkHot") || (draw->itemState & ODS_SELECTED))
        {
            /* .fly-icon-btn: SubtleFillColorSecondary on hover, Tertiary pressed */
            BYTE a = draw->itemState & ODS_SELECTED ? (f->style.dark ? 11 : 6) : (f->style.dark ? 15 : 10);
            RECT plate = box; HRGN round = CreateRoundRectRgn(plate.left, plate.top, plate.right + 1, plate.bottom + 1, 9, 9);
            COLORREF fill = f->style.dark ? RGB(a, a, a) : RGB(0, 0, 0);
            if (round) { SelectClipRgn(dc, round); pw_shell_glass_fill(glass, dc, &plate, fill, a); SelectClipRgn(dc, NULL); DeleteObject(round); }
        }
        pw_taskbar_sprite(dc, f->style.dark, sprite, 0, 0);
        if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT))
        { InflateRect(&box, -1, -1); DrawFocusRect(dc, &box); pw_shell_glass_fix(glass); }
        EndBufferedPaint(glass, TRUE);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(w))
        {
        case PW_FLYOUT_SETTINGS:
        {
            WCHAR path[MAX_PATH]; SHELLEXECUTEINFOW execute = {sizeof(execute)};
            GetSystemDirectoryW(path, MAX_PATH); PathAppendW(path, L"mihawk-settings.exe");
            execute.hwnd = hwnd; execute.lpFile = path; execute.nShow = SW_SHOWNORMAL;
            pw_flyout_close(f); ShellExecuteExW(&execute);
            return 0;
        }
        case PW_FLYOUT_TOGGLE:
            f->collapsed = !f->collapsed; pw_flyout_place(f);
            if (f->partner) pw_flyout_place((void *)GetWindowLongPtrW(f->partner, GWLP_USERDATA));
            ShowWindow(GetDlgItem(hwnd, PW_FLYOUT_UP), f->collapsed ? SW_HIDE : SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, PW_FLYOUT_DOWN), f->collapsed ? SW_HIDE : SW_SHOW);
            InvalidateRect(hwnd, NULL, TRUE); return 0;
        case PW_FLYOUT_UP: case PW_FLYOUT_DOWN:
            f->month += LOWORD(w) == PW_FLYOUT_UP ? -1 : 1;
            if (f->month < 1) { f->month = 12; --f->year; }
            if (f->month > 12) { f->month = 1; ++f->year; }
            InvalidateRect(hwnd, NULL, TRUE); return 0;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
static HWND pw_flyout_button(struct pw_flyout *f, const WCHAR *label, int id, int x, int y, int w, int h)
{
    HWND child = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, w, h, f->window,
                               (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (child) SetWindowSubclass(child, pw_shell_button_proc, 1, 0);
    return child;
}
static struct pw_flyout *pw_flyout_create(HWND owner, enum pw_flyout_kind kind, const WCHAR *class)
{
    WNDCLASSW wc = {0}; struct pw_flyout *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->kind = kind; pw_shell_style_load(&f->style);
    wc.lpfnWndProc = pw_flyout_proc; wc.hInstance = GetModuleHandleW(NULL); wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = class; RegisterClassW(&wc);
    if (!CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, class, class + 6, WS_POPUP | WS_CLIPCHILDREN,
                         0, 0, PW_FLYOUT_WIDTH, 100, owner, NULL, wc.hInstance, f))
    { pw_shell_style_free(&f->style); free(f); return NULL; }
    pw_flyout_appearance(f);
    return f;
}
static void pw_flyout_free(struct pw_flyout *f)
{
    if (!f) return;
    if (f->window) DestroyWindow(f->window);
    if (f->body) DeleteObject(f->body);
    if (f->strong) DeleteObject(f->strong);
    if (f->caption) DeleteObject(f->caption);
    if (f->theme) CloseThemeData(f->theme);
    pw_shell_style_free(&f->style); free(f);
}
/* Runs the flyout until it is dismissed, as Start does. */
static void pw_flyout_run(struct pw_flyout *f, struct pw_flyout *partner)
{
    MSG msg; int result = 1;
    ShowWindow(f->window, SW_SHOW);
    if (partner) ShowWindow(partner->window, SW_SHOWNOACTIVATE);
    SendMessageW(f->window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    SetForegroundWindow(f->window);
    while (IsWindowVisible(f->window) && (result = GetMessageW(&msg, NULL, 0, 0)) > 0)
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { pw_flyout_close(f); continue; }
        if (!IsDialogMessageW(f->window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (!result) PostQuitMessage((int)msg.wParam);
}
void pw_shell_quick_settings(HWND owner)
{
    struct pw_flyout *f;
    if (pw_flyout_open[PW_FLYOUT_QUICK]) { ShowWindow(pw_flyout_open[PW_FLYOUT_QUICK], SW_HIDE); return; }
    if (!(f = pw_flyout_create(owner, PW_FLYOUT_QUICK, L"MihawkQuickSettings"))) return;
    pw_flyout_open[PW_FLYOUT_QUICK] = f->window;
    pw_flyout_network(f); pw_flyout_place(f);
    /* the footer's Settings button: 40 x 36, 6.5 px down the 48 px footer (rounded as the prototype's box) */
    pw_flyout_button(f, L"All settings", PW_FLYOUT_SETTINGS, 307, 440 - 49 + 7, 40, 36);
    pw_flyout_run(f, NULL);
    pw_flyout_open[PW_FLYOUT_QUICK] = NULL; pw_flyout_free(f);
}
void pw_shell_notifications(HWND owner)
{
    struct pw_flyout *centre, *calendar; SYSTEMTIME now;
    if (pw_flyout_open[PW_FLYOUT_CENTRE]) { ShowWindow(pw_flyout_open[PW_FLYOUT_CENTRE], SW_HIDE); return; }
    if (!(centre = pw_flyout_create(owner, PW_FLYOUT_CENTRE, L"MihawkNotifications"))) return;
    if (!(calendar = pw_flyout_create(centre->window, PW_FLYOUT_CALENDAR, L"MihawkCalendar"))) { pw_flyout_free(centre); return; }
    centre->partner = calendar->window; calendar->partner = centre->window;
    GetLocalTime(&now); calendar->year = now.wYear; calendar->month = now.wMonth;
    pw_flyout_open[PW_FLYOUT_CENTRE] = centre->window;
    pw_flyout_place(calendar); pw_flyout_place(centre);
    /* the calendar's toggle (582.5 rounds to 583) and the month's paging, from the prototype's boxes */
    pw_flyout_button(calendar, L"Collapse the calendar", PW_FLYOUT_TOGGLE, 319, 9, 32, 32);
    pw_flyout_button(calendar, L"Previous month", PW_FLYOUT_UP, 283, 59, 32, 32);
    pw_flyout_button(calendar, L"Next month", PW_FLYOUT_DOWN, 319, 59, 32, 32);
    pw_flyout_run(centre, calendar);
    pw_flyout_open[PW_FLYOUT_CENTRE] = NULL;
    pw_flyout_free(calendar); pw_flyout_free(centre);
}
