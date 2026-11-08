/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SHELL_WALLPAPER_H
#define PW_SHELL_WALLPAPER_H
#include <windows.h>
/* The Mihawk desktop session's background, initialized once per prefix (formerly the session's Windows mihawk.exe;
 * now Explorer's, as the title's session root with MIHAWK_SESSION=1, Wine patch 0996). Existing nonempty wallpaper
 * choices survive, and later explicit empty choices too. The local image is optional and never vendored. Explorer
 * paints through Wine's real desktop APIs; the broker has no wallpaper authority or process-image fallback.
 * tests/wallpaper-probe.c runs this very function against the real registry. */
static DWORD pw_shell_prepare_wallpaper(void)
{
    HKEY settings = NULL, desktop = NULL;
    DWORD initialized = 0, size = sizeof(initialized), type = 0, status;
    WCHAR existing[MAX_PATH], path[MAX_PATH];
    existing[0] = 0;
    status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MihawkOS", 0, NULL, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &settings, NULL);
    if (status != ERROR_SUCCESS) return status;
    if (RegQueryValueExW(settings, L"WallpaperInitialized", NULL, &type,
                        (BYTE *)&initialized, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(initialized) && initialized == 1)
    { RegCloseKey(settings); return ERROR_SUCCESS; }
    status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, NULL, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &desktop, NULL);
    if (status == ERROR_SUCCESS)
    {
        size = sizeof(existing);
        DWORD queried = RegQueryValueExW(desktop, L"Wallpaper", NULL, &type, (BYTE *)existing, &size);
        if (queried == ERROR_FILE_NOT_FOUND ||
            (queried == ERROR_SUCCESS && type == REG_SZ && !existing[0]))
        {
            UINT length = GetWindowsDirectoryW(path, MAX_PATH);
            const WCHAR *suffix = L"\\web\\wallpaper\\proton.bmp";
            if (!length || length + lstrlenW(suffix) >= MAX_PATH) status = ERROR_INSUFFICIENT_BUFFER;
            else
            {
                lstrcatW(path, suffix);
                if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
                {
                    status = GetLastError();
                    /* The local wallpaper is optional and is never vendored.
                     * A checkout without it still starts the real desktop. */
                    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
                        status = ERROR_SUCCESS;
                }
                else status = RegSetValueExW(desktop, L"Wallpaper", 0, REG_SZ, (BYTE *)path,
                                             (lstrlenW(path) + 1) * sizeof(WCHAR));
            }
        }
        else if (queried != ERROR_SUCCESS && queried != ERROR_MORE_DATA) status = queried;
        RegCloseKey(desktop);
    }
    if (status == ERROR_SUCCESS)
    {
        initialized = 1;
        status = RegSetValueExW(settings, L"WallpaperInitialized", 0, REG_DWORD,
                                (BYTE *)&initialized, sizeof(initialized));
    }
    RegCloseKey(settings);
    return status;
}

/* The desktop's picture, painted by the shell's own desktop view (not Wine's PaintDesktop, whose bitmap only the process
 * that loaded it refreshes): the Wallpaper setting as it is kept (HKCU\Control Panel\Desktop\Wallpaper, the managed
 * proton.bmp), filled to the screen and kept until the file changes. Settings > Personalization > Background copies the
 * chosen picture over proton.bmp and sends the view the registered message below; the view forgets this picture and
 * repaints at once (the picture was loaded without LR_LOADFROMFILE in the first attempt, which never drew). */
#define PW_WALLPAPER_CHANGED_MESSAGE L"MihawkOS.WallpaperChanged"
static HBITMAP pw_shell_wallpaper_bitmap;
static WCHAR pw_shell_wallpaper_loaded[MAX_PATH];
static FILETIME pw_shell_wallpaper_stamp;

static inline void pw_shell_wallpaper_forget(void)
{
    if (pw_shell_wallpaper_bitmap) DeleteObject(pw_shell_wallpaper_bitmap);
    pw_shell_wallpaper_bitmap = NULL;
    pw_shell_wallpaper_loaded[0] = 0;
}

static inline BOOL pw_shell_wallpaper_load(void)
{
    WCHAR path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA status;
    BOOL same;

    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) || !path[0]) return FALSE;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &status)) return FALSE;
    same = pw_shell_wallpaper_bitmap && !lstrcmpiW(path, pw_shell_wallpaper_loaded) &&
           !CompareFileTime(&status.ftLastWriteTime, &pw_shell_wallpaper_stamp);
    if (same) return TRUE;
    pw_shell_wallpaper_forget();
    if (!(pw_shell_wallpaper_bitmap = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION)))
        return FALSE;
    lstrcpynW(pw_shell_wallpaper_loaded, path, MAX_PATH);
    pw_shell_wallpaper_stamp = status.ftLastWriteTime;
    return TRUE;
}

/* The picture filled to the screen (Windows' Fill: cover, cropped equally) and drawn where the screen lies in the window
 * the DC paints, 0 when there is no picture. The desktop view's client is the work area, 48 px shorter than the screen:
 * filling that drew the picture 24 px higher than PaintDesktop does (scorecard desktop 98.3% to 9.7%, shell81-base-03). */
static inline BOOL pw_shell_wallpaper_paint(HDC dc, const RECT *client)
{
    BITMAP info;
    HDC memory;
    HGDIOBJ prior_target, prior_source;
    RECT screen = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    HWND window = dc ? WindowFromDC(dc) : NULL;
    int width, height, drawn_width, drawn_height;

    if (window) MapWindowPoints(NULL, window, (POINT *)&screen, 2);
    else screen = *client;
    client = &screen;
    width = client->right - client->left;
    height = client->bottom - client->top;
    if (!dc || width <= 0 || height <= 0 || !pw_shell_wallpaper_load()) return FALSE;
    if (!GetObjectW(pw_shell_wallpaper_bitmap, sizeof(info), &info) || !info.bmWidth || !info.bmHeight) return FALSE;
    if (!(memory = CreateCompatibleDC(dc))) return FALSE;
    prior_target = SelectObject(dc, GetStockObject(NULL_PEN));
    prior_source = SelectObject(memory, pw_shell_wallpaper_bitmap);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    if ((LONGLONG)width * info.bmHeight >= (LONGLONG)height * info.bmWidth)
    {
        drawn_width = width;
        drawn_height = (int)((LONGLONG)info.bmHeight * width / info.bmWidth);
    }
    else
    {
        drawn_height = height;
        drawn_width = (int)((LONGLONG)info.bmWidth * height / info.bmHeight);
    }
    StretchBlt(dc, client->left + (width - drawn_width) / 2, client->top + (height - drawn_height) / 2,
               drawn_width, drawn_height, memory, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
    SelectObject(memory, prior_source);
    SelectObject(dc, prior_target);
    DeleteDC(memory);
    return TRUE;
}
#endif
