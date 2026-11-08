/* Native Start and Search, as the approved prototype draws them (prototypes/windows11-shell css/start.css), over its
 * acrylic-base (PS5_DWMSBT_FLYOUT_BASE, server patch 1160): one 600 x 566 panel with three views. Start shows the
 * search pill, Pinned (6 x 3), Recommended (the shell's recent files, two rows that fit) and the account and power
 * row; All apps lists every app under its letter; Search holds the input, the filter tabs, Top apps, Recent and Quick
 * searches, and while a query is typed the matching apps with a preview of the selected one.
 *
 * At 96 DPI the fixed chrome (search pill, headings and their buttons, the built-in apps' and file kinds' icons, the
 * footer, the avatar, the power glyph, the tabs, the quick searches) is the prototype's own pixels
 * (themes/windows11/start, tools/build-shell-sprites.py), placed at the prototype's boxes; names, dates and the user
 * name are text. The panel owns its bounded snapshot and icons; Wine owns launching, shortcut resolution and
 * recent-file state. No application/process registry is invented. Limits: 1024 apps, 256 directories, depth 8, 18
 * pins, 6 recent files. Reparse directories are not followed. The UI reports incomplete enumeration.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include "pw_shell_style.h"
#include "pw_shell_taskbar.h"

#define PW_START_APPS 1024
#define PW_START_PINS 18
#define PW_START_RECENT 6
#define PW_START_SEARCH 100
#define PW_START_LIST 101
#define PW_START_ALL 102
#define PW_START_PIN 103
#define PW_START_EXIT 104
#define PW_START_PILL 105
#define PW_START_MORE 106
#define PW_START_PIN_FIRST 200
#define PW_START_RECENT_FIRST 300
#define PW_START_TILE_FIRST 500
#define PW_START_QUICK_FIRST 600
#define PW_START_TILES 6
#define PW_START_QUICK 4
/* A list row that is a letter heading, not an app: its item data. */
#define PW_START_LETTER 0x10000
/* The prototype's PS5_DWMSBT_FLYOUT_BASE (server patch 1160): its acrylic-base, flyout stroke and shadow. */
#define PW_START_BACKDROP 6
enum pw_start_view { PW_VIEW_MAIN, PW_VIEW_ALL, PW_VIEW_FIND };
static const WCHAR pw_start_key[] = L"Software\\Mihawk\\Shell";
/* The prototype's quick searches: each runs its word as the query. */
static const WCHAR *pw_start_quick[PW_START_QUICK] = {L"Screenshots", L"Display", L"Downloads", L"Notepad"};
struct pw_start_item { WCHAR path[MAX_PATH], name[MAX_PATH]; HICON icon; int resource, sprite; FILETIME time; };
struct pw_start_state
{
    HWND window, search, list, all, pin, more, pill, exit, empty;
    HWND pins[PW_START_PINS], recent_buttons[PW_START_RECENT], tiles[PW_START_TILES], quick[PW_START_QUICK];
    struct pw_start_item apps[PW_START_APPS], recent[PW_START_RECENT];
    WCHAR pinned[PW_START_PINS][MAX_PATH], user[256];
    unsigned count, pin_count, recent_count, directories, tile_count;
    int tile_apps[PW_START_TILES];
    enum pw_start_view view;
    BOOL partial, choosing;
    HMODULE resources;
    struct pw_shell_style style;
    HFONT body, strong, caption;
    HTHEME theme;
};
static HWND pw_start_window;

/* Where the prototype puts things, in the panel's pixels (its boxes at 1920 x 1080 less the panel's 660,454). */
static void pw_start_pin_rect(unsigned i, RECT *r)
{
    int column = i % 6, row = i / 6;
    r->left = (31 * 3 + column * 272 + 1) / 3; r->right = (31 * 3 + column * 272 + 254 + 1) / 3;
    r->top = 118 + row * 76; r->bottom = r->top + 70;
}
static int pw_start_pin_icon_x(unsigned i) { return (172 + (i % 6) * 272 + 1) / 3; }

static int pw_start_find(struct pw_start_state *s, const WCHAR *path)
{
    unsigned i;
    for (i = 0; i < s->count; ++i) if (!lstrcmpiW(s->apps[i].path, path)) return i;
    return -1;
}
static int pw_start_pin_index(struct pw_start_state *s, const WCHAR *path)
{
    unsigned i;
    for (i = 0; i < s->pin_count; ++i) if (!lstrcmpiW(s->pinned[i], path)) return i;
    return -1;
}
static void pw_start_name(struct pw_start_item *item, const WCHAR *path)
{
    lstrcpynW(item->path, path, MAX_PATH);
    lstrcpynW(item->name, PathFindFileNameW(path), MAX_PATH);
    PathRemoveExtensionW(item->name);
}
static void pw_start_add(struct pw_start_state *s, const WCHAR *path)
{
    if (pw_start_find(s, path) >= 0) return;
    if (s->count == PW_START_APPS) { s->partial = TRUE; return; }
    pw_start_name(&s->apps[s->count], path);
    s->apps[s->count++].sprite = -1;
}
static void pw_start_scan(struct pw_start_state *s, const WCHAR *folder, unsigned depth, BOOL recursive)
{
    WCHAR mask[MAX_PATH], path[MAX_PATH]; WIN32_FIND_DATAW data; HANDLE find;
    if (depth > 8 || ++s->directories > 256) { s->partial = TRUE; return; }
    if (!PathCombineW(mask, folder, L"*")) { s->partial = TRUE; return; }
    find = FindFirstFileW(mask, &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) s->partial = TRUE;
        return;
    }
    do
    {
        if (!lstrcmpW(data.cFileName, L".") || !lstrcmpW(data.cFileName, L"..")) continue;
        if (!PathCombineW(path, folder, data.cFileName)) { s->partial = TRUE; continue; }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (recursive && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pw_start_scan(s, path, depth + 1, TRUE);
        }
        else if (!lstrcmpiW(PathFindExtensionW(path), L".lnk") || !lstrcmpiW(PathFindExtensionW(path), L".exe")) pw_start_add(s, path);
        if (s->count == PW_START_APPS || s->directories > 256) { s->partial = TRUE; break; }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}
static int __cdecl pw_start_compare(const void *a, const void *b)
{
    const struct pw_start_item *x = a, *y = b;
    return lstrcmpiW(x->name, y->name);
}
static BOOL pw_start_save_pins(struct pw_start_state *s)
{
    WCHAR data[PW_START_PINS * MAX_PATH + 2], *p = data; HKEY key; unsigned i; LONG result;
    for (i = 0; i < s->pin_count; ++i) { lstrcpyW(p, s->pinned[i]); p += lstrlenW(p) + 1; }
    *p++ = 0; if (!s->pin_count) *p++ = 0;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, pw_start_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS) return FALSE;
    result = RegSetValueExW(key, L"StartPins", 0, REG_MULTI_SZ, (BYTE *)data, (p - data) * sizeof(WCHAR));
    RegCloseKey(key); return result == ERROR_SUCCESS;
}
/* The built-in apps, in the prototype's pin order (its Calculator has no counterpart here), with its names and the
 * prototype's own icons for them, each a file in a folder (a CSIDL): MihawkBrowser is the Public Desktop shortcut the
 * title keeps pointing at the runtime's copy (native/proton_wine.cpp). */
enum { PW_START_TERMINAL = 3, PW_START_BROWSER = 4 };
static const struct { const WCHAR *image, *name; int folder, resource, sprite; } pw_start_builtins[] = {
    {L"explorer.exe", L"File Explorer", CSIDL_WINDOWS, PW_ICON_ORIGINAL_FOLDER_OPEN, PW_SPRITE_START_ICON_EXPLORER},
    {L"mihawk-settings.exe", L"Settings", CSIDL_SYSTEM, PW_ICON_LIGHT_SETTINGS, PW_SPRITE_START_ICON_SETTINGS},
    {L"notepad.exe", L"Notepad", CSIDL_SYSTEM, PW_ICON_LIGHT_NOTEBOOK_PEN, PW_SPRITE_START_ICON_NOTEPAD},
    {L"cmd.exe", L"Terminal", CSIDL_SYSTEM, PW_ICON_LIGHT_TERMINAL, PW_SPRITE_START_ICON_TERMINAL},
    {L"MihawkBrowser.lnk", L"MihawkBrowser", CSIDL_COMMON_DESKTOPDIRECTORY, PW_ICON_ORIGINAL_APP_BROWSER, PW_SPRITE_START_ICON_BROWSER},
};
static BOOL pw_start_builtin_path(unsigned i, WCHAR *path)
{
    return SUCCEEDED(SHGetFolderPathW(NULL, pw_start_builtins[i].folder, NULL, SHGFP_TYPE_CURRENT, path)) &&
           PathAppendW(path, pw_start_builtins[i].image) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
/* MihawkBrowser joined the built-ins after Terminal on 2026-10-17: pins saved before then get it there once, and a
 * later unpin stays (BrowserPinned). Before the title has made its shortcut, nothing is marked. */
static void pw_start_pin_browser_once(struct pw_start_state *s)
{
    WCHAR path[MAX_PATH], terminal[MAX_PATH]; DWORD done = 0, size = sizeof(done), one = 1; HKEY key; int at;
    if (RegGetValueW(HKEY_CURRENT_USER, pw_start_key, L"BrowserPinned", RRF_RT_REG_DWORD, NULL, &done, &size) == ERROR_SUCCESS && done)
        return;
    if (!pw_start_builtin_path(PW_START_BROWSER, path)) return;
    if (pw_start_pin_index(s, path) < 0 && s->pin_count < PW_START_PINS)
    {
        at = pw_start_builtin_path(PW_START_TERMINAL, terminal) ? pw_start_pin_index(s, terminal) + 1 : 0;
        if (at <= 0) at = s->pin_count;
        memmove(s->pinned[at + 1], s->pinned[at], (s->pin_count - at) * sizeof(s->pinned[0]));
        lstrcpyW(s->pinned[at], path); ++s->pin_count;
        if (!pw_start_save_pins(s)) { s->partial = TRUE; return; }
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, pw_start_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(key, L"BrowserPinned", 0, REG_DWORD, (BYTE *)&one, sizeof(one));
    RegCloseKey(key);
}
static void pw_start_catalog(struct pw_start_state *s)
{
    static const int roots[] = {CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS, CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY};
    WCHAR path[MAX_PATH], folder[MAX_PATH], data[PW_START_PINS * MAX_PATH + 2];
    DWORD size = sizeof(data), type; LONG result; unsigned i;
    for (i = 0; i < ARRAY_SIZE(roots); ++i)
        if (SUCCEEDED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, folder))) pw_start_scan(s, folder, 0, i < 2);
    for (i = 0; i < ARRAY_SIZE(pw_start_builtins); ++i)
    {
        int at;
        if (!pw_start_builtin_path(i, path)) continue;
        pw_start_add(s, path);
        if ((at = pw_start_find(s, path)) < 0) continue;
        lstrcpyW(s->apps[at].name, pw_start_builtins[i].name);
        s->apps[at].resource = pw_start_builtins[i].resource; s->apps[at].sprite = pw_start_builtins[i].sprite;
    }
    result = RegGetValueW(HKEY_CURRENT_USER, pw_start_key, L"StartPins", RRF_RT_REG_MULTI_SZ, &type, data, &size);
    if (result == ERROR_SUCCESS && size >= 2 * sizeof(WCHAR) && size % sizeof(WCHAR) == 0)
    {
        WCHAR *p = data, *end = data + size / sizeof(WCHAR);
        /* Registry data is user-editable. Validate each terminator and bound. */
        while (p < end && *p && s->pin_count < PW_START_PINS)
        {
            WCHAR *next = p;
            while (next < end && *next) ++next;
            if (next == end || next - p >= MAX_PATH) { s->partial = TRUE; break; }
            if (pw_start_pin_index(s, p) < 0)
            {
                lstrcpyW(s->pinned[s->pin_count++], p);
                if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) pw_start_add(s, p);
            }
            p = next + 1;
        }
        if (!s->partial) pw_start_pin_browser_once(s);
    }
    else if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
    {
        /* The prototype's defaults: the built-in apps, then the desktop's shortcuts (its games). */
        for (i = 0; i < ARRAY_SIZE(pw_start_builtins); ++i)
            if (pw_start_builtin_path(i, path) && pw_start_find(s, path) >= 0) lstrcpyW(s->pinned[s->pin_count++], path);
        for (i = 2; i < ARRAY_SIZE(roots); ++i)
        {
            WCHAR mask[MAX_PATH]; WIN32_FIND_DATAW found; HANDLE find;
            if (FAILED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, folder)) || !PathCombineW(mask, folder, L"*.lnk")) continue;
            if ((find = FindFirstFileW(mask, &found)) == INVALID_HANDLE_VALUE) continue;
            do
                if (s->pin_count < PW_START_PINS && PathCombineW(path, folder, found.cFileName) && pw_start_find(s, path) >= 0 &&
                    pw_start_pin_index(s, path) < 0) lstrcpyW(s->pinned[s->pin_count++], path);
            while (FindNextFileW(find, &found));
            FindClose(find);
        }
        if (!pw_start_save_pins(s)) s->partial = TRUE;
        else pw_start_pin_browser_once(s);  /* already pinned among the built-ins: this only marks it */
    }
    else s->partial = TRUE; /* Do not overwrite unreadable preferences. */
    qsort(s->apps, s->count, sizeof(s->apps[0]), pw_start_compare);
    /* Search's Top apps: the pins that are apps, in order. */
    for (i = 0; i < s->pin_count && s->tile_count < PW_START_TILES; ++i)
    {
        int at = pw_start_find(s, s->pinned[i]);
        if (at >= 0) s->tile_apps[s->tile_count++] = at;
    }
}
static void pw_start_recent(struct pw_start_state *s)
{
    WCHAR folder[MAX_PATH], mask[MAX_PATH]; WIN32_FIND_DATAW data; HANDLE find;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_RECENT, NULL, SHGFP_TYPE_CURRENT, folder)) || !PathCombineW(mask, folder, L"*.lnk")) return;
    if ((find = FindFirstFileW(mask, &data)) == INVALID_HANDLE_VALUE) return;
    do
    {
        unsigned at = 0, i;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        while (at < s->recent_count && CompareFileTime(&s->recent[at].time, &data.ftLastWriteTime) >= 0) ++at;
        if (at == PW_START_RECENT) continue;
        if (s->recent_count < PW_START_RECENT) ++s->recent_count;
        for (i = s->recent_count - 1; i > at; --i) s->recent[i] = s->recent[i - 1];
        if (PathCombineW(mask, folder, data.cFileName))
        {
            pw_start_name(&s->recent[at], mask); s->recent[at].time = data.ftLastWriteTime;
            /* "photo.png.lnk" shows as "photo.png"; the prototype's image and document icons by the target's kind */
            s->recent[at].sprite = -1;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}
static BOOL pw_start_image(const WCHAR *name)
{
    static const WCHAR *kinds[] = {L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".webp", L".tif", L".tiff", L".ico"};
    const WCHAR *extension = PathFindExtensionW(name);
    for (unsigned i = 0; i < ARRAY_SIZE(kinds); ++i) if (!lstrcmpiW(extension, kinds[i])) return TRUE;
    return FALSE;
}
/* When a recent file was used, as the prototype words it: Just now, 5m ago, 3h ago, Yesterday at 9:41 PM, Oct 14. */
static void pw_start_when(const FILETIME *time, WCHAR *out, int size)
{
    FILETIME now_utc; SYSTEMTIME now, then, local; ULARGE_INTEGER a, b; WCHAR clock[32];
    GetSystemTimeAsFileTime(&now_utc);
    a.LowPart = now_utc.dwLowDateTime; a.HighPart = now_utc.dwHighDateTime; b.LowPart = time->dwLowDateTime; b.HighPart = time->dwHighDateTime;
    ULONGLONG seconds = a.QuadPart > b.QuadPart ? (a.QuadPart - b.QuadPart) / 10000000 : 0;
    FileTimeToSystemTime(&now_utc, &local); SystemTimeToTzSpecificLocalTime(NULL, &local, &now);
    FileTimeToSystemTime(time, &local); SystemTimeToTzSpecificLocalTime(NULL, &local, &then);
    ULONGLONG since_midnight = now.wHour * 3600 + now.wMinute * 60 + now.wSecond;
    if (seconds < 60) lstrcpynW(out, L"Just now", size);
    else if (seconds < 3600) wsprintfW(out, L"%um ago", (unsigned)(seconds / 60));
    else if (seconds <= since_midnight) wsprintfW(out, L"%uh ago", (unsigned)(seconds / 3600));
    else if (seconds <= since_midnight + 86400)
    {
        GetTimeFormatEx(L"en-US", TIME_NOSECONDS, &then, L"h:mm tt", clock, ARRAY_SIZE(clock));
        wsprintfW(out, L"Yesterday at %s", clock);
    }
    else GetDateFormatEx(L"en-US", 0, &then, then.wYear == now.wYear ? L"MMM d" : L"MMM d, yyyy", out, size, NULL);
}
static int pw_start_selection(struct pw_start_state *s)
{
    LRESULT row = SendMessageW(s->list, LB_GETCURSEL, 0, 0), index;
    if (row < 0) return -1;
    index = SendMessageW(s->list, LB_GETITEMDATA, row, 0);
    return index >= 0 && index < s->count ? index : -1;
}
static void pw_start_place(HWND child, BOOL shown, int x, int y, int w, int h)
{
    SetWindowPos(child, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | (shown ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}
static void pw_start_query(struct pw_start_state *s, WCHAR *query, int size)
{
    if (!GetWindowTextW(s->search, query, size)) query[0] = 0;
}
/* Every child where this view and its contents put it; the list holds All apps or the query's matches. */
static void pw_start_layout(struct pw_start_state *s)
{
    WCHAR query[128]; unsigned i; RECT r; WCHAR letter = 0;
    BOOL main = s->view == PW_VIEW_MAIN, all = s->view == PW_VIEW_ALL, find = s->view == PW_VIEW_FIND;
    pw_start_query(s, query, ARRAY_SIZE(query));
    BOOL results = find && query[0];
    SendMessageW(s->list, WM_SETREDRAW, FALSE, 0); SendMessageW(s->list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < s->count && (all || results); ++i)
    {
        if (results && !StrStrIW(s->apps[i].name, query)) continue;
        if (all && CharUpperW((WCHAR *)(UINT_PTR)s->apps[i].name[0]) != (WCHAR *)(UINT_PTR)letter)
        {
            letter = (WCHAR)(UINT_PTR)CharUpperW((WCHAR *)(UINT_PTR)s->apps[i].name[0]);
            /* rows carry their data from the start, so WM_MEASUREITEM knows a heading from an app */
            SendMessageW(s->list, LB_ADDSTRING, 0, PW_START_LETTER | letter);
        }
        SendMessageW(s->list, LB_ADDSTRING, 0, i);
    }
    /* the first app, not a heading, is selected */
    for (i = 0; i < (unsigned)SendMessageW(s->list, LB_GETCOUNT, 0, 0); ++i)
        if (SendMessageW(s->list, LB_GETITEMDATA, i, 0) < PW_START_LETTER) { SendMessageW(s->list, LB_SETCURSEL, i, 0); break; }
    SendMessageW(s->list, WM_SETREDRAW, TRUE, 0);
    pw_start_place(s->pill, !find, 33, 29, 534, 36);
    pw_start_place(s->search, find, 78, 37, 472, 20);
    pw_start_place(s->all, !find, main ? 473 : 490, 85, main ? 78 : 61, 24);
    if (all) pw_start_place(s->list, TRUE, 1, 119, 598, 382);
    else pw_start_place(s->list, results, 25, 109, 267, 382);
    pw_start_place(s->pin, results, 314, 449, 253, 36);
    EnableWindow(s->pin, results && SendMessageW(s->list, LB_GETCOUNT, 0, 0) > 0);
    for (i = 0; i < PW_START_PINS; ++i)
    {
        BOOL exists = i < s->pin_count; int index = exists ? pw_start_find(s, s->pinned[i]) : -1;
        SetWindowTextW(s->pins[i], index >= 0 ? s->apps[index].name : exists ? PathFindFileNameW(s->pinned[i]) : L"");
        pw_start_pin_rect(i, &r);
        pw_start_place(s->pins[i], main && exists, r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
    /* Recommended shows the rows that fit, two of two; Search's Recent lists five. */
    for (i = 0; i < PW_START_RECENT; ++i)
    {
        if (main) pw_start_place(s->recent_buttons[i], i < 4 && i < s->recent_count, i % 2 ? 306 : 33, 391 + (i / 2) * 52, 261, 48);
        else pw_start_place(s->recent_buttons[i], find && !results && i < 5 && i < s->recent_count, 25, 286 + i * 42, 267, 40);
    }
    pw_start_place(s->more, main && s->recent_count, 487, 359, 64, 24);
    for (i = 0; i < PW_START_TILES; ++i) pw_start_place(s->tiles[i], find && !results && i < s->tile_count, 25 + i * 93, 157, 85, 84);
    for (i = 0; i < PW_START_QUICK; ++i) pw_start_place(s->quick[i], find && !results, 308, 286 + i * 42, 267, 40);
    pw_start_place(s->exit, !find, 523, 514, 40, 40);
    SetWindowTextW(s->all, all ? L"Back" : L"All apps");
    SetWindowTextW(s->empty, s->partial ? L"Some items could not be read. Open their folder to browse them." :
                   results && !SendMessageW(s->list, LB_GETCOUNT, 0, 0) ? L"No results for this search." :
                   main && !s->pin_count ? L"Open All apps to pin an application." : L"");
    pw_start_place(s->empty, GetWindowTextLengthW(s->empty) > 0, 33, results ? 125 : 466, 534, 20);
    InvalidateRect(s->window, NULL, TRUE);
}
static void pw_start_show_view(struct pw_start_state *s, enum pw_start_view view)
{
    s->view = view;
    if (view != PW_VIEW_FIND) SetWindowTextW(s->search, L"");
    pw_start_layout(s);
    SetFocus(view == PW_VIEW_FIND ? s->search : view == PW_VIEW_ALL ? s->list : s->window);
}
static void pw_start_launch(struct pw_start_state *s, const WCHAR *path)
{
    SHELLEXECUTEINFOW execute = {sizeof(execute)};
    execute.hwnd = s->window; execute.lpFile = path; execute.nShow = SW_SHOWNORMAL;
    s->choosing = TRUE;
    if (ShellExecuteExW(&execute)) ShowWindow(s->window, SW_HIDE);
    else MessageBoxW(s->window, L"This item could not be opened. Check that it is still installed in this prefix.", L"Start", MB_OK | MB_ICONERROR);
    s->choosing = FALSE;
}
static void pw_start_toggle_pin(struct pw_start_state *s, const WCHAR *path)
{
    WCHAR previous[PW_START_PINS][MAX_PATH]; unsigned count = s->pin_count; int index = pw_start_pin_index(s, path);
    memcpy(previous, s->pinned, sizeof(previous));
    if (index >= 0)
    { memmove(s->pinned[index], s->pinned[index + 1], (--s->pin_count - index) * sizeof(s->pinned[0])); }
    else if (s->pin_count < PW_START_PINS) lstrcpyW(s->pinned[s->pin_count++], path);
    else { MessageBoxW(s->window, L"Unpin an application before adding another. Start holds 18 pins.", L"Start", MB_OK); return; }
    if (!pw_start_save_pins(s))
    {
        memcpy(s->pinned, previous, sizeof(previous)); s->pin_count = count;
        MessageBoxW(s->window, L"The pins could not be saved. Check that this prefix is writable.", L"Start", MB_OK | MB_ICONERROR);
    }
    pw_start_layout(s);
}
static HICON pw_start_icon(struct pw_start_state *s, struct pw_start_item *item)
{
    if (!item->icon)
    {
        SHFILEINFOW info;
        if (item->resource && s->resources)
            item->icon = LoadImageW(s->resources, MAKEINTRESOURCEW(item->resource + (item->resource >= 2000 && s->style.dark ? 1000 : 0)), IMAGE_ICON, 32, 32, 0);
        if (!item->icon && SHGetFileInfoW(item->path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON)) item->icon = info.hIcon;
    }
    return item->icon ? item->icon : LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
}
/* The view's fixed chrome under a child at (x, y) of the panel (the panel itself at 0, 0). */
static void pw_start_chrome(struct pw_start_state *s, HDC dc, int x, int y)
{
    BOOL dark = s->style.dark;
    if (s->view == PW_VIEW_FIND)
    {
        WCHAR query[2];
        pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_BOX, 33 - x, 29 - y);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_TABS, 25 - x, 77 - y);
        pw_start_query(s, query, ARRAY_SIZE(query));
        if (!query[0])
        {
            pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_GROUP_TOP, 25 - x, 121 - y);
            if (s->recent_count) pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_GROUP_RECENT, 25 - x, 253 - y);
            pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_GROUP_QUICK, 308 - x, 253 - y);
        }
    }
    else
    {
        pw_taskbar_sprite(dc, dark, PW_SPRITE_START_SEARCH, 33 - x, 29 - y);
        pw_taskbar_sprite(dc, dark, s->view == PW_VIEW_ALL ? PW_SPRITE_START_HEAD_ALL : PW_SPRITE_START_HEAD_PINNED, 49 - x, 83 - y);
        if (s->view == PW_VIEW_MAIN && s->recent_count) pw_taskbar_sprite(dc, dark, PW_SPRITE_START_HEAD_RECOMMENDED, 49 - x, 357 - y);
    }
    if (s->view == PW_VIEW_FIND) return;  /* Search has no account row */
    pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FOOT, 1 - x, 501 - y);
    pw_taskbar_sprite(dc, dark, PW_SPRITE_START_USER, 37 - x, 514 - y);
}
/* .st-plate:hover and :active: ControlFillColorSecondary / Tertiary inside a ControlStrokeColorDefault hairline. */
static void pw_start_plate(struct pw_start_state *s, HPAINTBUFFER glass, HDC dc, const RECT *rect, BOOL pressed)
{
    RECT r = *rect; BOOL dark = s->style.dark;
    HRGN outer = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, 9, 9);
    HRGN inner = CreateRoundRectRgn(r.left + 1, r.top + 1, r.right, r.bottom, 7, 7);
    (void)glass;
    if (outer && inner)
    {
        /* premultiplied fills: white at 50% (30% pressed), black at 5.8%; dark: white at 6% (4%), white at 7% */
        BYTE fill = dark ? (pressed ? 10 : 15) : (pressed ? 77 : 128), edge = dark ? 18 : 15;
        COLORREF fill_colour = RGB(fill, fill, fill), edge_colour = dark ? RGB(edge, edge, edge) : RGB(0, 0, 0);
        BITMAPINFO info = {{sizeof(BITMAPINFOHEADER), r.right - r.left, -(r.bottom - r.top), 1, 32, BI_RGB}};
        void *bits = NULL; HBITMAP layer = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0); HDC ldc;
        if (layer && bits && (ldc = CreateCompatibleDC(dc)))
        {
            HGDIOBJ prior = SelectObject(ldc, layer);
            DWORD *p = bits; int w = r.right - r.left, h = r.bottom - r.top;
            OffsetRgn(outer, -r.left, -r.top); OffsetRgn(inner, -r.left, -r.top);
            for (int yy = 0; yy < h; ++yy)
                for (int xx = 0; xx < w; ++xx)
                {
                    DWORD value = 0;
                    if (PtInRegion(inner, xx, yy)) value = (DWORD)fill << 24 | GetRValue(fill_colour) << 16 | GetGValue(fill_colour) << 8 | GetBValue(fill_colour);
                    else if (PtInRegion(outer, xx, yy)) value = (DWORD)edge << 24 | GetRValue(edge_colour) << 16 | GetGValue(edge_colour) << 8 | GetBValue(edge_colour);
                    p[yy * w + xx] = value;
                }
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            GdiAlphaBlend(dc, r.left, r.top, w, h, ldc, 0, 0, w, h, blend);
            SelectObject(ldc, prior); DeleteDC(ldc);
        }
        if (layer) DeleteObject(layer);
    }
    if (outer) DeleteObject(outer);
    if (inner) DeleteObject(inner);
}
static void pw_start_text(struct pw_start_state *s, HPAINTBUFFER glass, HDC dc, HFONT font, const WCHAR *text, RECT *r,
                          COLORREF colour, DWORD flags)
{
    HGDIOBJ prior = SelectObject(dc, font ? font : s->style.font);
    pw_shell_glass_text(s->theme, glass, dc, text, r, colour, flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, prior);
}
static COLORREF pw_start_secondary(struct pw_start_state *s)
{
    return s->style.dark ? PW_APPEARANCE_DARK_SECONDARY : PW_APPEARANCE_LIGHT_SECONDARY;
}
static void pw_start_draw(struct pw_start_state *s, const DRAWITEMSTRUCT *draw)
{
    HWND item = draw->hwndItem; RECT box = draw->rcItem, r, at; HDC dc = NULL; UINT id = draw->CtlID;
    BOOL hot = !!GetPropW(item, L"MihawkHot"), pressed = !!(draw->itemState & ODS_SELECTED), dark = s->style.dark;
    HPAINTBUFFER glass;
    if (id == PW_START_LIST)
    {
        /* list rows draw in the list's own coordinates; the panel's chrome lies under none of them */
        if (draw->itemID == (UINT)-1 || !(glass = pw_shell_glass_begin(draw->hDC, &box, &dc))) return;
        LRESULT data = SendMessageW(item, LB_GETITEMDATA, draw->itemID, 0);
        if (data >= PW_START_LETTER)
        {
            WCHAR heading[2] = {(WCHAR)(data & 0xffff), 0};
            /* .st-letter: at least 32 px wide, 11 px padding, the letter centred */
            r = box; r.left += 40; r.top = r.bottom - 34; r.bottom -= 2; r.right = r.left + 33;
            pw_start_text(s, glass, dc, s->body, heading, &r, s->style.text, DT_VCENTER | DT_CENTER);
        }
        else if (data >= 0 && data < s->count)
        {
            struct pw_start_item *app = &s->apps[data]; int left = s->view == PW_VIEW_ALL ? 40 : 0;
            SetRect(&r, box.left + left, box.top + 1, box.right - (s->view == PW_VIEW_ALL ? 36 : 4), box.bottom - 1);
            if ((draw->itemState & ODS_SELECTED) && s->view == PW_VIEW_FIND) pw_start_plate(s, glass, dc, &r, FALSE);
            DrawIconEx(dc, r.left + (left ? 13 : 9), r.top + 8, pw_start_icon(s, app), 24, 24, 0, NULL, DI_NORMAL);
            pw_shell_glass_fix(glass);
            at = r; at.left += left ? 53 : 45;
            pw_start_text(s, glass, dc, s->body, app->name, &at, s->style.text, DT_VCENTER | DT_LEFT);
        }
        if ((draw->itemState & ODS_FOCUS) && s->view == PW_VIEW_ALL)
        { r = box; r.left += 40; r.right -= 36; InflateRect(&r, 0, -1); DrawFocusRect(dc, &r); pw_shell_glass_fix(glass); }
        EndBufferedPaint(glass, TRUE);
        return;
    }
    if (!(glass = pw_shell_glass_begin(draw->hDC, &box, &dc))) return;
    GetWindowRect(item, &r); MapWindowPoints(NULL, s->window, (POINT *)&r, 2);
    pw_start_chrome(s, dc, r.left, r.top);
    if (id >= PW_START_PIN_FIRST && id < PW_START_PIN_FIRST + PW_START_PINS)
    {
        unsigned pin = id - PW_START_PIN_FIRST; int index = pin < s->pin_count ? pw_start_find(s, s->pinned[pin]) : -1;
        struct pw_start_item *app = index >= 0 ? &s->apps[index] : NULL; WCHAR name[MAX_PATH];
        if (hot || pressed) pw_start_plate(s, glass, dc, &box, pressed);
        int icon_x = pw_start_pin_icon_x(pin) - r.left;
        if (app && app->sprite >= 0) pw_taskbar_sprite(dc, dark, app->sprite, icon_x, 7);
        else
        {
            DrawIconEx(dc, icon_x, 7, app ? pw_start_icon(s, app) : LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION), 32, 32, 0, NULL, DI_NORMAL);
            pw_shell_glass_fix(glass);
        }
        GetWindowTextW(item, name, ARRAY_SIZE(name));
        SetRect(&at, 5, 47, box.right - 5, 63);
        pw_start_text(s, glass, dc, s->caption, name, &at, s->style.text, DT_CENTER | DT_TOP);
    }
    else if (id >= PW_START_RECENT_FIRST && id < PW_START_RECENT_FIRST + s->recent_count)
    {
        struct pw_start_item *file = &s->recent[id - PW_START_RECENT_FIRST]; WCHAR when[64];
        BOOL image = pw_start_image(file->name);
        if (hot || pressed) pw_start_plate(s, glass, dc, &box, pressed);
        if (s->view == PW_VIEW_MAIN)
        {
            pw_taskbar_sprite(dc, dark, image ? PW_SPRITE_START_ICON_IMAGE : PW_SPRITE_START_ICON_DOCUMENT, 13, 8);
            SetRect(&at, 57, 8, box.right - 12, 24);
            pw_start_text(s, glass, dc, s->caption, file->name, &at, s->style.text, DT_TOP | DT_LEFT);
            pw_start_when(&file->time, when, ARRAY_SIZE(when));
            SetRect(&at, 57, 24, box.right - 12, 40);
            pw_start_text(s, glass, dc, s->caption, when, &at, pw_start_secondary(s), DT_TOP | DT_LEFT);
        }
        else
        {
            pw_taskbar_sprite(dc, dark, image ? PW_SPRITE_START_FIND_ROW_IMAGE : PW_SPRITE_START_FIND_ROW_DOCUMENT, 9, 8);
            SetRect(&at, 45, 10, box.right - 12, 30);
            pw_start_text(s, glass, dc, s->body, file->name, &at, s->style.text, DT_TOP | DT_LEFT);
        }
    }
    else if (id >= PW_START_TILE_FIRST && id < PW_START_TILE_FIRST + s->tile_count)
    {
        struct pw_start_item *app = &s->apps[s->tile_apps[id - PW_START_TILE_FIRST]];
        pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_TILE, 0, 0);
        if (hot || pressed) pw_start_plate(s, glass, dc, &box, pressed);
        if (app->sprite >= 0) pw_taskbar_sprite(dc, dark, app->sprite, 27, 14);
        else { DrawIconEx(dc, 27, 14, pw_start_icon(s, app), 32, 32, 0, NULL, DI_NORMAL); pw_shell_glass_fix(glass); }
        SetRect(&at, 4, 54, box.right - 4, 70);
        pw_start_text(s, glass, dc, s->caption, app->name, &at, s->style.text, DT_CENTER | DT_TOP);
    }
    else if (id >= PW_START_QUICK_FIRST && id < PW_START_QUICK_FIRST + PW_START_QUICK)
    {
        if (hot || pressed) pw_start_plate(s, glass, dc, &box, pressed);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_START_FIND_QUICK_1 + (id - PW_START_QUICK_FIRST), 0, 0);
    }
    else if (id == PW_START_EXIT)
    {
        if (hot || pressed) pw_start_plate(s, glass, dc, &box, pressed);
        pw_taskbar_sprite(dc, dark, PW_SPRITE_START_POWER, 0, 0);
    }
    else if (id == PW_START_PIN)
    {
        int index = pw_start_selection(s); WCHAR label[64];
        pw_start_plate(s, glass, dc, &box, pressed);
        lstrcpyW(label, index >= 0 && pw_start_pin_index(s, s->apps[index].path) >= 0 ? L"Unpin from Start" : L"Pin to Start");
        at = box; at.left += 12;
        pw_start_text(s, glass, dc, s->body, label, &at, IsWindowEnabled(item) ? s->style.text : pw_start_secondary(s), DT_VCENTER | DT_LEFT);
    }
    /* the search pill, All apps / Back and More are the chrome itself */
    if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT))
    { r = box; InflateRect(&r, -1, -1); DrawFocusRect(dc, &r); pw_shell_glass_fix(glass); }
    EndBufferedPaint(glass, TRUE);
}
static HFONT pw_start_font(int height, int weight)
{
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return NULL;
    metrics.lfMenuFont.lfHeight = -height; metrics.lfMenuFont.lfWeight = weight;
    return CreateFontIndirectW(&metrics.lfMenuFont);
}
static void pw_start_appearance(struct pw_start_state *s)
{
    DWORD corner = DWMWCP_ROUND, dark = s->style.dark, backdrop = PW_START_BACKDROP, colour = DWMWA_COLOR_DEFAULT;
    MARGINS sheet = {-1, -1, -1, -1};
    if (s->body) DeleteObject(s->body);
    if (s->strong) DeleteObject(s->strong);
    if (s->caption) DeleteObject(s->caption);
    s->body = pw_start_font(14, FW_NORMAL); s->strong = pw_start_font(14, FW_SEMIBOLD); s->caption = pw_start_font(12, FW_NORMAL);
    EnumChildWindows(s->window, pw_shell_set_font, (LPARAM)s->body);
    /* .surface-flyout over the acrylic-base: the compositor rounds, strokes, shadows and draws the material. */
    DwmSetWindowAttribute(s->window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DwmSetWindowAttribute(s->window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(s->window, DWMWA_BORDER_COLOR, &colour, sizeof(colour));
    DwmExtendFrameIntoClientArea(s->window, &sheet);
    DwmSetWindowAttribute(s->window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    if (s->theme) CloseThemeData(s->theme);
    s->theme = OpenThemeData(s->window, L"WINDOW");
}
static LRESULT CALLBACK pw_start_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    struct pw_start_state *s = (void *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) { s = ((CREATESTRUCTW *)l)->lpCreateParams; s->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s); }
    if (!s) return DefWindowProcW(hwnd, msg, w, l);
    switch (msg)
    {
    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && !s->choosing) ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_THEMECHANGED: case WM_SETTINGCHANGE:
        pw_shell_style_load(&s->style);
        for (unsigned i = 0; i < s->count; ++i) if (s->apps[i].resource && s->apps[i].icon) { DestroyIcon(s->apps[i].icon); s->apps[i].icon = NULL; }
        pw_start_appearance(s); RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN); return 0;
    case WM_ERASEBKGND: case WM_PRINTCLIENT:
    {
        /* Children's DrawThemeParentBackground receives transparent glass. */
        RECT box; HDC dc = (HDC)w;
        if (GetClipBox(dc, &box) != ERROR) PatBlt(dc, box.left, box.top, box.right - box.left, box.bottom - box.top, BLACKNESS);
        return 1;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint; RECT client, r; HDC dc = BeginPaint(hwnd, &paint), buffer = NULL;
        HPAINTBUFFER glass;
        GetClientRect(hwnd, &client);
        if ((glass = pw_shell_glass_begin(dc, &client, &buffer)))
        {
            pw_start_chrome(s, buffer, 0, 0);
            /* .st-user-name: the session's user, 12 px at 94,525.5 */
            SetRect(&r, 94, 526, 511, 542);
            if (s->view != PW_VIEW_FIND) pw_start_text(s, glass, buffer, s->caption, s->user, &r, s->style.text, DT_TOP | DT_LEFT);
            EndBufferedPaint(glass, TRUE);
        }
        EndPaint(hwnd, &paint); return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
    {
        /* Black brushes are transparent glass; the search input sits on the box's fill, ControlFillColorDefault (white
         * at 70%, 6% in dark) over the acrylic as the prototype composes it there. */
        COLORREF input = s->style.dark ? RGB(43, 47, 58) : RGB(250, 251, 251);
        static HBRUSH inputs[2];
        if (!inputs[!!s->style.dark]) inputs[!!s->style.dark] = CreateSolidBrush(input);
        SetTextColor((HDC)w, msg == WM_CTLCOLORSTATIC ? pw_start_secondary(s) : s->style.text);
        SetBkColor((HDC)w, msg == WM_CTLCOLOREDIT ? input : RGB(0, 0, 0));
        SetBkMode((HDC)w, msg == WM_CTLCOLOREDIT ? OPAQUE : TRANSPARENT);
        return (LRESULT)(msg == WM_CTLCOLOREDIT ? inputs[!!s->style.dark] : GetStockObject(BLACK_BRUSH));
    }
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *measure = (MEASUREITEMSTRUCT *)l;
        if (measure->CtlID == PW_START_LIST)
        {
            /* .st-letter (32 px, 6 above and 2 below, none above the first) and .st-app (40 px, 1 above and below) */
            BOOL letter = measure->itemData >= PW_START_LETTER;
            measure->itemHeight = letter ? (measure->itemID ? 40 : 34) : 42;
        }
        else measure->itemHeight = 44;
        return TRUE;
    }
    case WM_DRAWITEM: pw_start_draw(s, (DRAWITEMSTRUCT *)l); return TRUE;
    case WM_CONTEXTMENU:
    {
        HWND child = (HWND)w; int id = GetDlgCtrlID(child), index = -1; const WCHAR *path = NULL; RECT r; POINT point; HMENU menu;
        if (child == s->list)
        {
            point.x = (short)LOWORD(l); point.y = (short)HIWORD(l);
            if (point.x != -1 || point.y != -1)
            {
                ScreenToClient(child, &point);
                LRESULT hit = SendMessageW(child, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
                if (HIWORD(hit)) return 0;
                SendMessageW(child, LB_SETCURSEL, LOWORD(hit), 0);
            }
            index = pw_start_selection(s);
        }
        if (index >= 0) path = s->apps[index].path;
        else if (id >= PW_START_PIN_FIRST && id < PW_START_PIN_FIRST + s->pin_count) path = s->pinned[id - PW_START_PIN_FIRST];
        if (!path || !(menu = CreatePopupMenu())) return 0;
        AppendMenuW(menu, MF_STRING, 1, pw_start_pin_index(s, path) < 0 ? L"Pin to Start" : L"Unpin from Start");
        point.x = (short)LOWORD(l); point.y = (short)HIWORD(l);
        if (point.x == -1 && point.y == -1) { GetWindowRect(child, &r); point.x = r.left; point.y = r.bottom; }
        s->choosing = TRUE;
        if (TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, NULL)) pw_start_toggle_pin(s, path);
        s->choosing = FALSE; DestroyMenu(menu); return 0;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(w), index;
        if (id == PW_START_SEARCH && HIWORD(w) == EN_CHANGE)
        {
            /* typing anywhere is a search (and an automation's WM_SETTEXT on the input too) */
            if (s->view != PW_VIEW_FIND && GetWindowTextLengthW(s->search)) { s->view = PW_VIEW_FIND; SetFocus(s->search); }
            pw_start_layout(s); return 0;
        }
        if (id == PW_START_LIST && HIWORD(w) == LBN_SELCHANGE) { InvalidateRect(s->pin, NULL, FALSE); return 0; }
        if (id == PW_START_PILL) pw_start_show_view(s, PW_VIEW_FIND);
        else if (id == PW_START_ALL) pw_start_show_view(s, s->view == PW_VIEW_ALL ? PW_VIEW_MAIN : PW_VIEW_ALL);
        else if (id == PW_START_PIN && (index = pw_start_selection(s)) >= 0)
        { s->choosing = TRUE; pw_start_toggle_pin(s, s->apps[index].path); s->choosing = FALSE; }
        else if (id == PW_START_LIST && HIWORD(w) == LBN_DBLCLK && (index = pw_start_selection(s)) >= 0) pw_start_launch(s, s->apps[index].path);
        else if (id >= PW_START_PIN_FIRST && id < PW_START_PIN_FIRST + s->pin_count) pw_start_launch(s, s->pinned[id - PW_START_PIN_FIRST]);
        else if (id >= PW_START_RECENT_FIRST && id < PW_START_RECENT_FIRST + s->recent_count) pw_start_launch(s, s->recent[id - PW_START_RECENT_FIRST].path);
        else if (id >= PW_START_TILE_FIRST && id < PW_START_TILE_FIRST + s->tile_count) pw_start_launch(s, s->apps[s->tile_apps[id - PW_START_TILE_FIRST]].path);
        else if (id >= PW_START_QUICK_FIRST && id < PW_START_QUICK_FIRST + PW_START_QUICK)
        { SetWindowTextW(s->search, pw_start_quick[id - PW_START_QUICK_FIRST]); SetFocus(s->search); SendMessageW(s->search, EM_SETSEL, 0, -1); }
        else if (id == PW_START_MORE)
        {
            WCHAR folder[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_RECENT, NULL, SHGFP_TYPE_CURRENT, folder))) pw_start_launch(s, folder);
        }
        else if (id == PW_START_EXIT)
        {
            s->choosing = TRUE;
            if (MessageBoxW(hwnd, L"Close this desktop session and return to the launcher? Save your work first.", L"End desktop session", MB_YESNO | MB_ICONQUESTION) == IDYES)
            { ShowWindow(hwnd, SW_HIDE); PostMessageW(GetShellWindow(), WM_CLOSE, 0, 0); }
            s->choosing = FALSE;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
static HWND pw_start_control(struct pw_start_state *s, const WCHAR *class, const WCHAR *label, DWORD style, int id)
{
    HWND child = CreateWindowW(class, label, WS_CHILD | style, 0, 0, 0, 0, s->window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (!lstrcmpW(class, L"BUTTON")) SetWindowSubclass(child, pw_shell_button_proc, 1, 0);
    return child;
}
static void pw_shell_start_view(HWND owner, enum pw_start_view view)
{
    struct pw_start_state *s; WNDCLASSW wc = {0}; RECT area; MSG msg; unsigned i; DWORD size; int result = 1;
    if (pw_start_window) { ShowWindow(pw_start_window, SW_HIDE); return; }
    s = calloc(1, sizeof(*s)); if (!s) return;
    pw_shell_style_load(&s->style); pw_start_catalog(s); pw_start_recent(s);
    size = ARRAY_SIZE(s->user);
    if (!GetUserNameW(s->user, &size)) lstrcpyW(s->user, L"Current session");
    WCHAR resources[MAX_PATH]; GetWindowsDirectoryW(resources, MAX_PATH); PathAppendW(resources, L"resources\\mihawk\\icons.dll");
    s->resources = LoadLibraryExW(resources, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    wc.lpfnWndProc = pw_start_proc; wc.hInstance = GetModuleHandleW(NULL); wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.lpszClassName = L"MihawkStart";
    RegisterClassW(&wc);
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) goto done;
    pw_start_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, wc.lpszClassName, L"Start",
                                     WS_POPUP | WS_CLIPCHILDREN, (area.left + area.right - PW_APPEARANCE_START_WIDTH) / 2, area.bottom - PW_APPEARANCE_START_HEIGHT - PW_APPEARANCE_START_BOTTOM_GAP, PW_APPEARANCE_START_WIDTH, PW_APPEARANCE_START_HEIGHT, owner, NULL, wc.hInstance, s);
    if (!pw_start_window) goto done;
    s->search = pw_start_control(s, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, PW_START_SEARCH);
    SendMessageW(s->search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Type here to search"); SendMessageW(s->search, EM_SETLIMITTEXT, 127, 0);
    s->pill = pw_start_control(s, L"BUTTON", L"Search for apps, settings, and documents", WS_TABSTOP | BS_OWNERDRAW, PW_START_PILL);
    s->all = pw_start_control(s, L"BUTTON", L"All apps", WS_TABSTOP | BS_OWNERDRAW, PW_START_ALL);
    s->list = pw_start_control(s, L"LISTBOX", L"Installed applications", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT, PW_START_LIST);
    s->pin = pw_start_control(s, L"BUTTON", L"Pin / unpin selected app", WS_TABSTOP | BS_OWNERDRAW, PW_START_PIN);
    for (i = 0; i < PW_START_PINS; ++i) s->pins[i] = pw_start_control(s, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW, PW_START_PIN_FIRST + i);
    s->more = pw_start_control(s, L"BUTTON", L"More", WS_TABSTOP | BS_OWNERDRAW, PW_START_MORE);
    for (i = 0; i < PW_START_RECENT; ++i) s->recent_buttons[i] = pw_start_control(s, L"BUTTON", s->recent[i].name, WS_TABSTOP | BS_OWNERDRAW, PW_START_RECENT_FIRST + i);
    for (i = 0; i < PW_START_TILES; ++i)
        s->tiles[i] = pw_start_control(s, L"BUTTON", i < s->tile_count ? s->apps[s->tile_apps[i]].name : L"", WS_TABSTOP | BS_OWNERDRAW, PW_START_TILE_FIRST + i);
    for (i = 0; i < PW_START_QUICK; ++i) s->quick[i] = pw_start_control(s, L"BUTTON", pw_start_quick[i], WS_TABSTOP | BS_OWNERDRAW, PW_START_QUICK_FIRST + i);
    s->empty = pw_start_control(s, L"STATIC", L"", 0, -1);
    s->exit = pw_start_control(s, L"BUTTON", L"Power: end session", WS_TABSTOP | BS_OWNERDRAW, PW_START_EXIT);
    EnumChildWindows(s->window, pw_shell_glass_children, 0);
    pw_start_appearance(s); pw_start_show_view(s, view);
    SendMessageW(s->window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    ShowWindow(s->window, SW_SHOW); SetForegroundWindow(s->window); SetFocus(view == PW_VIEW_FIND ? s->search : s->window);
    while (IsWindowVisible(s->window) && (result = GetMessageW(&msg, NULL, 0, 0)) > 0)
    {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == s->window || IsChild(s->window, msg.hwnd)))
        {
            if (msg.wParam == VK_ESCAPE) { ShowWindow(s->window, SW_HIDE); continue; }
            if (msg.wParam == VK_DOWN && msg.hwnd == s->search && IsWindowVisible(s->list)) { SetFocus(s->list); continue; }
            if (msg.wParam == VK_RETURN && (msg.hwnd == s->search || msg.hwnd == s->list))
            { int at = pw_start_selection(s); if (at >= 0) pw_start_launch(s, s->apps[at].path); continue; }
        }
        /* typing in Start begins a search, as Windows does */
        if (msg.message == WM_CHAR && msg.hwnd != s->search && msg.wParam > ' ' && (msg.hwnd == s->window || IsChild(s->window, msg.hwnd)))
        { pw_start_show_view(s, PW_VIEW_FIND); msg.hwnd = s->search; }
        if (!IsDialogMessageW(s->window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (!result) PostQuitMessage((int)msg.wParam);
    DestroyWindow(s->window); pw_start_window = NULL;
done:
    for (i = 0; i < s->count; ++i) if (s->apps[i].icon) DestroyIcon(s->apps[i].icon);
    for (i = 0; i < s->recent_count; ++i) if (s->recent[i].icon) DestroyIcon(s->recent[i].icon);
    if (s->resources) FreeLibrary(s->resources);
    if (s->body) DeleteObject(s->body);
    if (s->strong) DeleteObject(s->strong);
    if (s->caption) DeleteObject(s->caption);
    if (s->theme) CloseThemeData(s->theme);
    pw_shell_style_free(&s->style); free(s);
}
void pw_shell_start(HWND owner) { pw_shell_start_view(owner, PW_VIEW_MAIN); }
void pw_shell_search(HWND owner) { pw_shell_start_view(owner, PW_VIEW_FIND); }
