/* SPDX-License-Identifier: GPL-3.0-or-later
 * The player's wallpaper reached the desktop (src/pw_wallpaper.h): the title copies <id>.bmp over C:\windows\web\wallpaper\proton.bmp
 * before the desktop starts, and the desktop paints it. Run in the Mihawk session after tools/run-wallpaper.py has set
 * wallpaper.txt:
 *
 *   wallpaper-choice-probe <id> [--settings] [--capture=PATH]
 *
 * It compares the two files byte for byte, and asks for a picture of the displayed desktop, which the runner compares with the
 * chosen picture. With --settings it then opens Settings > Personalization (mihawk-settings.exe), moves the choice with the keys a
 * person has (arrow, Enter) and checks that wallpaper.txt now names another picture, with a second picture of the screen, then
 * opens Home, Personalization and System > Display one by one and captures each (pictures 3 to 5). Prints PS5_PROTON_WALLPAPER PASS/FAIL checks=N failures=M. */
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "../src/pw_accent.h"

static unsigned checks, failures;
static char capture_base[MAX_PATH];
static HWND flicker_window;
static void wiggle(void);

static void line(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static void check(BOOL okay, const char *name)
{
    char text[200];
    ++checks;
    failures += !okay;
    wsprintfA(text, "wallpaper %s: %s error=%lu\n", okay ? "PASS" : "FAIL", name, GetLastError());
    line(text);
}

static void pump(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    MSG message;
    do {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Sleep(10);
    } while (GetTickCount() - start < milliseconds);
}

/* The owner saves a numbered picture of the next displayed frame when asked (as tests/desktop-session-probe.c does). */
static BOOL capture_display(unsigned number)
{
    char stem[MAX_PATH], path[MAX_PATH], request[MAX_PATH];
    char *dot = NULL, *c;
    HANDLE file;
    DWORD started;
    unsigned complete_ticks = 0;

    if (!capture_base[0]) return TRUE;
    if (lstrlenA(capture_base) >= MAX_PATH - 24) return FALSE;
    lstrcpyA(stem, capture_base);
    for (c = stem; *c; ++c) if (*c == '.') dot = c;
    if (dot) *dot = 0;
    wsprintfA(path, "%s-%02u.ppm", stem, number);
    wsprintfA(request, "%s.broker-request", capture_base);
    if (flicker_window) ShowWindow(flicker_window, SW_HIDE);
    DeleteFileA(path);
    /* a still desktop presents no new frame, and the owner saves the next one: ask for a repaint */
    RedrawWindow(GetShellWindow(), NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    pump(300);
    file = CreateFileA(request, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(file);
    started = GetTickCount();
    while (GetTickCount() - started < 10000) {
        WIN32_FILE_ATTRIBUTE_DATA image;
        BOOL complete = GetFileAttributesExA(path, GetFileExInfoStandard, &image) && !image.nFileSizeHigh &&
                        image.nFileSizeLow == 1920 * 1080 * 3 + 17;
        if (complete && ++complete_ticks == 3) return TRUE;
        if (!complete) complete_ticks = 0;
        if (GetTickCount() - started > 600 && (GetTickCount() - started) % 500 < 30) wiggle();
        pump(20);
    }
    {
        WIN32_FILE_ATTRIBUTE_DATA image;
        char text[300];
        BOOL there = GetFileAttributesExA(path, GetFileExInfoStandard, &image);
        wsprintfA(text, "desktop-view note: picture %u not seen: file %s size %lu, request %s\n", number, there ? "exists" : "absent",
                  there ? image.nFileSizeLow : 0, GetFileAttributesA(request) != INVALID_FILE_ATTRIBUTES ? "still there" : "taken");
        line(text);
    }
    /* how late does it come, if it comes? */
    {
        DWORD waited = GetTickCount();
        char text[160];
        while (GetTickCount() - waited < 40000) {
            WIN32_FILE_ATTRIBUTE_DATA image;
            if (GetFileAttributesExA(path, GetFileExInfoStandard, &image) && image.nFileSizeLow == 1920 * 1080 * 3 + 17) break;
            pump(100);
        }
        wsprintfA(text, "desktop-view note: picture %u %s after %lu ms more\n", number,
                  GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? "appeared" : "never appeared", GetTickCount() - waited);
        line(text);
    }
    DeleteFileA(request);
    return FALSE;
}

static void wiggle(void)
{
    if (!flicker_window)
        flicker_window = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "STATIC", NULL, WS_POPUP | SS_WHITERECT,
                                         1914, 2, 4, 4, NULL, NULL, NULL, NULL);
    if (flicker_window) ShowWindow(flicker_window, IsWindowVisible(flicker_window) ? SW_HIDE : SW_SHOWNA);
}

/* The id wallpaper.txt names ("wallpaper <id>"), or an empty string. */
static void read_choice(char *id, size_t size)
{
    char text[160] = "";
    DWORD read = 0;
    HANDLE file = CreateFileA("Z:\\data\\ps5proton\\wallpaper.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    char *end;
    id[0] = 0;
    if (file == INVALID_HANDLE_VALUE) return;
    ReadFile(file, text, sizeof(text) - 1, &read, NULL);
    CloseHandle(file);
    text[read] = 0;
    if (text[0] != 'w' || text[9] != ' ') return;
    for (end = text + 10; *end && *end != '\r' && *end != '\n' && *end != ' '; ++end) {}
    *end = 0;
    lstrcpynA(id, text + 10, (int)size);
}

static BOOL starts_with(const char *text, const char *prefix)
{
    while (*prefix) if (*text++ != *prefix++) return FALSE;
    return TRUE;
}

/* 1 when both files hold the same bytes (they are 6 MiB each, read whole). */
static BOOL same_file(const char *a, const char *b)
{
    HANDLE fa = CreateFileA(a, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE fb = CreateFileA(b, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    BOOL same = fa != INVALID_HANDLE_VALUE && fb != INVALID_HANDLE_VALUE;
    static BYTE left[65536], right[65536];
    while (same) {
        DWORD n = 0, m = 0;
        same = ReadFile(fa, left, sizeof(left), &n, NULL) && ReadFile(fb, right, sizeof(right), &m, NULL) && n == m;
        if (same && n) for (DWORD i = 0; i < n && same; ++i) same = left[i] == right[i];
        if (!n || !same) break;
    }
    if (fa != INVALID_HANDLE_VALUE) CloseHandle(fa);
    if (fb != INVALID_HANDLE_VALUE) CloseHandle(fb);
    return same;
}

/* One Settings page opened from its argument, captured, closed: prints where its client sits so the runner can compare the
   picture with the prototype's page (tools/run-wallpaper.py --reference). */
static void settings_page(const WCHAR *page, const char *name, unsigned number)
{
    WCHAR program[MAX_PATH * 2];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    HWND window = NULL;
    char text[200];
    POINT origin = {0, 0};
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    GetSystemDirectoryW(program, MAX_PATH);
    lstrcatW(program, L"\\mihawk-settings.exe ");
    lstrcatW(program, page);
    wsprintfA(text, "Settings opened on %s", name);
    check(CreateProcessW(NULL, program, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), text);
    for (DWORD start = GetTickCount(); GetTickCount() - start < 30000; pump(100))
        if ((window = FindWindowW(L"MihawkSettings", NULL)) && IsWindowVisible(window)) break;
    if (window && IsWindowVisible(window)) {
        SetForegroundWindow(window);
        pump(2000);
        ClientToScreen(window, &origin);
        wsprintfA(text, "wallpaper note: settings page %s client at %ld,%ld capture %u\n", name, origin.x, origin.y, number);
        line(text);
        wsprintfA(text, "a picture of Settings on %s was saved", name);
        check(capture_display(number), text);
        PostMessageW(window, WM_CLOSE, 0, 0);
        for (DWORD start = GetTickCount(); GetTickCount() - start < 10000 && IsWindow(window); pump(100)) {}
    } else check(FALSE, "the Settings page is showing");
    if (process.hProcess) CloseHandle(process.hProcess);
    if (process.hThread) CloseHandle(process.hThread);
}

/* A theme's accent (src/pw_accent.h; the taskbar's, patch 2492): Sunrise's #ca5010 stored as Settings' theme apply
   stores it, Personalization opened and captured (picture 6) with the taskbar under it, the values put back. The runner
   counts the accent's fill shade in Settings and in the taskbar. */
static void accent_page(void)
{
    static const WCHAR *names[2] = {L"AccentColor", L"ColorizationColor"};
    DWORD values[2] = {0, 0}, sizes[2] = {sizeof(DWORD), sizeof(DWORD)};
    LONG found[2];
    char text[160];
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                        NULL, &key, NULL) != ERROR_SUCCESS)
    { check(FALSE, "the accent's registry key opens"); return; }
    for (int i = 0; i < 2; i++) found[i] = RegQueryValueExW(key, names[i], NULL, NULL, (BYTE *)&values[i], &sizes[i]);
    check(pw_accent_store(0xca5010), "a theme's accent is stored as Settings stores it");
    pump(1500);
    wsprintfA(text, "wallpaper note: accent %06x fill %06x capture 6\n", 0xca5010, pw_accent_fill(0xca5010, 0));
    line(text);
    settings_page(L"personalization", "accent", 6);
    for (int i = 0; i < 2; i++)
        if (found[i] == ERROR_SUCCESS) RegSetValueExW(key, names[i], 0, REG_DWORD, (const BYTE *)&values[i], sizeof(DWORD));
        else RegDeleteValueW(key, names[i]);
    RegCloseKey(key);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 2000, NULL);
}

void mainCRTStartup(void)
{
    char command[MAX_PATH * 2], id[80] = "", folder[MAX_PATH], chosen[MAX_PATH], managed[MAX_PATH], text[200];
    char *c;
    unsigned argument = 0;
    BOOL settings_test = FALSE;

    lstrcpynA(command, GetCommandLineA(), sizeof(command));
    for (c = command; *c; ++c)
        if ((c == command || c[-1] == ' ') && starts_with(c, "--settings")) settings_test = TRUE;
        else if ((c == command || c[-1] == ' ') && starts_with(c, "--capture=")) {
            const char *end = c + 10;
            while (*end && *end != ' ') ++end;
            lstrcpynA(capture_base, c + 10, (int)(end - c - 10) + 1);
        } else if ((c == command || c[-1] == ' ') && c[0] != '-' && ++argument == 2) {   /* the program's own name is the first */
            const char *end = c;
            while (*end && *end != ' ') ++end;
            lstrcpynA(id, c, (int)(end - c) + 1 > (int)sizeof(id) ? (int)sizeof(id) : (int)(end - c) + 1);
        }
    check(id[0] != 0, "the probe was given the chosen picture's id");
    GetWindowsDirectoryA(folder, sizeof(folder));
    /* the runtime's copy (src/pw_wallpaper.h); a person's own file of that name in the prefix would win */
    wsprintfA(chosen, "%s\\web\\wallpaper\\%s.bmp", folder, id);
    if (GetFileAttributesA(chosen) == INVALID_FILE_ATTRIBUTES)
        wsprintfA(chosen, "Z:\\data\\ps5proton\\runtime\\ge11-7\\share\\mihawk\\wallpapers\\%s.bmp", id);
    wsprintfA(managed, "%s\\web\\wallpaper\\proton.bmp", folder);
    check(GetFileAttributesA(chosen) != INVALID_FILE_ATTRIBUTES, "the chosen picture is in the runtime or the prefix");
    check(same_file(chosen, managed), "proton.bmp, the picture the desktop paints, is the chosen picture");
    {
        DWORD start = GetTickCount();
        while (GetTickCount() - start < 30000 && !(GetShellWindow() && IsWindowVisible(GetShellWindow()))) pump(100);
        check(GetShellWindow() && IsWindowVisible(GetShellWindow()), "the desktop is showing");
    }
    pump(1500);
    check(capture_display(1), "a picture of the displayed desktop was saved");
    if (settings_test) {
        char after[80];
        WCHAR program[MAX_PATH * 2];
        STARTUPINFOW startup;
        PROCESS_INFORMATION process;
        HWND window = NULL;
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        GetSystemDirectoryW(program, MAX_PATH);
        lstrcatW(program, L"\\mihawk-settings.exe personalization/background");
        check(CreateProcessW(NULL, program, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "Settings started");
        for (DWORD start = GetTickCount(); GetTickCount() - start < 30000; pump(100))
            if ((window = FindWindowW(L"MihawkSettings", NULL)) && IsWindowVisible(window)) break;
        check(window && IsWindowVisible(window), "Settings > Personalization is showing");
        if (window) {
            SetForegroundWindow(window);
            pump(1500);
            PostMessageW(window, WM_KEYDOWN, VK_RIGHT, 0);
            PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
            pump(1000);
            read_choice(after, sizeof(after));
            if (!lstrcmpA(after, id)) {   /* the last picture: the other way */
                PostMessageW(window, WM_KEYDOWN, VK_LEFT, 0);
                PostMessageW(window, WM_KEYDOWN, VK_LEFT, 0);
                PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
                pump(1000);
                read_choice(after, sizeof(after));
            }
            wsprintfA(text, "wallpaper note: Settings chose '%s' (was '%s')\n", after, id);
            line(text);
            check(after[0] && lstrcmpA(after, id), "choosing a picture in Settings wrote another id to wallpaper.txt");
            check(capture_display(2), "a picture of Settings after the choice was saved");
            PostMessageW(window, WM_CLOSE, 0, 0);
            for (DWORD start = GetTickCount(); GetTickCount() - start < 10000 && IsWindow(window); pump(100)) {}
        }
        if (process.hProcess) CloseHandle(process.hProcess);
        if (process.hThread) CloseHandle(process.hThread);
        settings_page(L"home", "home", 3);
        settings_page(L"personalization", "personalization", 4);
        settings_page(L"display", "display", 5);
        accent_page();
    }
    wsprintfA(text, "PS5_PROTON_WALLPAPER %s: checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    line(text);
    if (IsWindow(GetShellWindow())) PostMessageA(GetShellWindow(), WM_CLOSE, 0, 0);
    ExitProcess(failures != 0);
}
