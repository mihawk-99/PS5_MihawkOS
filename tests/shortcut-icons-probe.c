/* SPDX-License-Identifier: GPL-3.0-or-later
 * Every desktop shortcut shows the right icon (patch 2499): the icon it names when that file is there, else its target's
 * own icon (a folder's, a program's, a file type's), with paths made by another PC or prefix found inside this one. Each
 * case's icon, as SHGetFileInfo gives it to the desktop, File Explorer and Start (the system image list), must be the
 * expected one and must not be Wine's default icon, pixel for pixel. A shortcut whose target is gone keeps the default:
 * no icon is invented. Writes C:\shortcut-icons<bits>.bmp, every case's icon at 48 px, for the record. PE32 and PE64. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

static unsigned checks, failures;
static WCHAR fixture[MAX_PATH];
static void say(const char *text)
{
    DWORD written;
    OutputDebugStringA(text);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
}
static void check(BOOL okay, const char *name)
{
    char line[300];
    ++checks; if (!okay) ++failures;
    wsprintfA(line, "shortcut-icons: %s %s\n", name, okay ? "ok" : "FAIL");
    say(line);
}

/* A 256 x 256 PNG, the only image of its icon (blue, a white diagonal band), as store clients and new programs ship. */
static const BYTE png256[] = {
#include "shortcut-icons-png256.inc"
};

static WCHAR *in_fixture(WCHAR *out, const WCHAR *name)
{
    PathCombineW(out, fixture, name);
    return out;
}
static BOOL write_bytes(const WCHAR *path, const void *data, DWORD size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return written == size;
}

/* An .ico with one 48 x 48 32-bit image of one colour. */
static BOOL write_ico(const WCHAR *path, DWORD colour)
{
    enum { N = 48, MASK = 8 * N, PIXELS = N * N * 4, SIZE = 6 + 16 + 40 + PIXELS + MASK };
    static BYTE data[SIZE];
    BITMAPINFOHEADER *header = (BITMAPINFOHEADER *)(data + 22);
    DWORD *pixels = (DWORD *)(data + 62), i;
    ZeroMemory(data, sizeof(data));
    data[2] = 1; data[4] = 1;
    data[6] = N; data[7] = N; data[10] = 1; data[12] = 32;
    *(DWORD *)(data + 14) = SIZE - 22; *(DWORD *)(data + 18) = 22;
    header->biSize = 40; header->biWidth = N; header->biHeight = 2 * N; header->biPlanes = 1; header->biBitCount = 32;
    for (i = 0; i < N * N; ++i) pixels[i] = 0xff000000 | colour;
    return write_bytes(path, data, SIZE);
}
static BOOL write_png_ico(const WCHAR *path)
{
    static BYTE data[22 + sizeof(png256)];
    ZeroMemory(data, 22);
    data[2] = 1; data[4] = 1; data[10] = 1; data[12] = 32;
    *(DWORD *)(data + 14) = sizeof(png256); *(DWORD *)(data + 18) = 22;
    CopyMemory(data + 22, png256, sizeof(png256));
    return write_bytes(path, data, sizeof(data));
}

static BOOL write_link(const WCHAR *path, const WCHAR *target, const WCHAR *icon, int index)
{
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    BOOL okay = FALSE;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link))) return FALSE;
    IShellLinkW_SetPath(link, target);
    if (icon) IShellLinkW_SetIconLocation(link, icon, index);
    if (SUCCEEDED(IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file)))
    {
        okay = SUCCEEDED(IPersistFile_Save(file, path, TRUE));
        IPersistFile_Release(file);
    }
    IShellLinkW_Release(link);
    return okay;
}
static BOOL write_url(const WCHAR *path, const char *icon_file)
{
    char text[600];
    wsprintfA(text, "[InternetShortcut]\r\nURL=store://run/1\r\nIconFile=%s\r\nIconIndex=0\r\n", icon_file);
    return write_bytes(path, text, lstrlenA(text));
}

/* The system image list's entry for an icon file and index, as a shortcut naming it gets (Shell_GetCachedImageIndexW,
 * shell32 ordinal 72): Wine registers no type icon for .ico files, so SHGetFileInfo on one cannot say. */
static int cached_index(const WCHAR *file, int index)
{
    typedef int (WINAPI *cached_fn)(const WCHAR *, int, UINT);
    cached_fn cached = (cached_fn)(void *)GetProcAddress(GetModuleHandleW(L"shell32.dll"), (const char *)72);
    return cached ? cached(file, index, 0) : -1;
}

static int icon_index(const WCHAR *path)
{
    SHFILEINFOW info = {0};
    return SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_SYSICONINDEX) ? info.iIcon : -1;
}

/* The icon drawn at 48 px on white, as the desktop's list view draws it; the contact sheet gets a copy at slot. */
static HDC sheet_dc;
static DWORD *sheet_bits;
enum { CELL = 56, SLOTS = 16 };
static BOOL draw_icon(const WCHAR *path, DWORD *out, int slot)
{
    SHFILEINFOW info = {0};
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO bmi = {0};
    void *bits = NULL;
    HBITMAP bitmap;
    HGDIOBJ old;
    HIMAGELIST list = NULL;
    BOOL okay;
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader); bmi.bmiHeader.biWidth = 48; bmi.bmiHeader.biHeight = -48;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(dc, bitmap);
    /* the 48 px system image list's entry (the desktop's icon size) by index, at its own size and without the
       shortcut arrow: what the desktop's list view draws under its overlay */
    okay = SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_SYSICONINDEX) &&
           SUCCEEDED(SHGetImageList(SHIL_EXTRALARGE, &IID_IImageList, (void **)&list));
    PatBlt(dc, 0, 0, 48, 48, WHITENESS);
    if (okay) okay = ImageList_DrawEx(list, info.iIcon, dc, 0, 0, 0, 0, CLR_NONE, CLR_NONE, ILD_NORMAL);
    GdiFlush();
    CopyMemory(out, bits, 48 * 48 * 4);
    if (slot >= 0 && slot < SLOTS) BitBlt(sheet_dc, 4 + slot * CELL, 4, 48, 48, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    return okay;
}
static unsigned differing(const DWORD *a, const DWORD *b)
{
    unsigned i, count = 0;
    for (i = 0; i < 48 * 48; ++i) if ((a[i] & 0xffffff) != (b[i] & 0xffffff)) ++count;
    return count;
}

static DWORD default_pixels[48 * 48];
static int default_index, slot;
/* link shows expected's icon (by the system image list's index, the same entry the desktop draws) and not the default. */
static void expect_index(const WCHAR *link, int want, BOOL own, const char *name)
{
    static DWORD pixels[48 * 48];
    char line[300];
    int got = icon_index(link);
    BOOL drawn = draw_icon(link, pixels, slot++);
    if (own)
    {
        wsprintfA(line, "%s: its own icon (index %d, the expected %d, the default %d)", name, got, want, default_index);
        check(drawn && got >= 0 && got == want, line);
        wsprintfA(line, "%s: not Wine's default icon (%u of 2304 pixels differ)", name, differing(pixels, default_pixels));
        check(differing(pixels, default_pixels) > 200, line);
    }
    else
    {
        wsprintfA(line, "%s: no invented icon, the default (index %d)", name, got);
        check(got == default_index, line);
    }
}
static void expect(const WCHAR *link, const WCHAR *expected, const char *name)
{
    expect_index(link, expected ? icon_index(expected) : default_index, expected != NULL, name);
}
/* link shows the icon file's own image */
static void expect_ico(const WCHAR *link, const WCHAR *ico, const char *name)
{
    expect_index(link, cached_index(ico, 0), TRUE, name);
}

static void save_sheet(void)
{
    WCHAR path[MAX_PATH];
    BITMAPFILEHEADER file = {0};
    BITMAPINFOHEADER info = {0};
    DWORD size;
    HANDLE out;
    DWORD written;
    info.biSize = sizeof(info); info.biWidth = 8 + SLOTS * CELL; info.biHeight = -(4 + CELL); info.biPlanes = 1;
    info.biBitCount = 32;
    size = info.biWidth * (4 + CELL) * 4;
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + size;
    wsprintfW(path, L"C:\\shortcut-icons%d.bmp", (int)sizeof(void *) * 8);
    out = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (out == INVALID_HANDLE_VALUE) return;
    WriteFile(out, &file, sizeof(file), &written, NULL);
    WriteFile(out, &info, sizeof(info), &written, NULL);
    WriteFile(out, sheet_bits, size, &written, NULL);
    CloseHandle(out);
}

static void remove_fixture(const WCHAR *folder)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE find;
    PathCombineW(pattern, folder, L"*");
    if ((find = FindFirstFileW(pattern, &found)) != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!lstrcmpW(found.cFileName, L".") || !lstrcmpW(found.cFileName, L"..")) continue;
            PathCombineW(path, folder, found.cFileName);
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_fixture(path);
            else DeleteFileW(path);
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(folder);
}

void mainCRTStartup(void)
{
    /* static: a 32-bit stack frame over a page would need __chkstk, which nothing here links */
    static WCHAR windows[MAX_PATH], app64[MAX_PATH], app32[MAX_PATH], plain[MAX_PATH], blue[MAX_PATH], green[MAX_PATH];
    static WCHAR png[MAX_PATH], folder[MAX_PATH], text[MAX_PATH], unknown[MAX_PATH], link[MAX_PATH], copies[MAX_PATH];
    static WCHAR source[MAX_PATH], moved[MAX_PATH], self[MAX_PATH];
    static char line[300], ansi[MAX_PATH];
    BITMAPINFO bmi = {0};
    HBITMAP sheet;
    BOOL is64 = sizeof(void *) == 8;

    CoInitialize(NULL);
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader); bmi.bmiHeader.biWidth = 8 + SLOTS * CELL;
    bmi.bmiHeader.biHeight = -(4 + CELL); bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    sheet_dc = CreateCompatibleDC(NULL);
    sheet = CreateDIBSection(sheet_dc, &bmi, DIB_RGB_COLORS, (void **)&sheet_bits, NULL, 0);
    SelectObject(sheet_dc, sheet);
    PatBlt(sheet_dc, 0, 0, bmi.bmiHeader.biWidth, 4 + CELL, WHITENESS);

    GetWindowsDirectoryW(windows, MAX_PATH);
    wsprintfW(fixture, L"C:\\shortcut-icons%d", is64 ? 64 : 32);
    remove_fixture(fixture);
    CreateDirectoryW(fixture, NULL);
    /* The programs: Wine's notepad, a 64-bit and a 32-bit build, each with its icon; this probe has none. */
    wsprintfW(source, is64 ? L"%s\\system32\\notepad.exe" : L"%s\\sysnative\\notepad.exe", windows);
    check(CopyFileW(source, in_fixture(app64, L"app64.exe"), FALSE), "a 64-bit program with an icon is there");
    wsprintfW(source, L"%s\\syswow64\\notepad.exe", windows);
    check(CopyFileW(source, in_fixture(app32, L"app32.exe"), FALSE), "a 32-bit program with an icon is there");
    GetModuleFileNameW(NULL, self, MAX_PATH);
    check(CopyFileW(self, in_fixture(plain, L"plain.exe"), FALSE), "a program without an icon is there");
    check(write_ico(in_fixture(blue, L"blue.ico"), 0x2060e0) && write_ico(in_fixture(green, L"green.ico"), 0x20c040) &&
          write_png_ico(in_fixture(png, L"png256.ico")), "the icon files are written");
    CreateDirectoryW(in_fixture(folder, L"folder"), NULL);
    write_bytes(in_fixture(text, L"help.chm"), "ITSF", 4);   /* a type Wine gives an icon of its own (hh.exe's) */
    write_bytes(in_fixture(unknown, L"nothing.zzzicon"), "x", 1);
    default_index = icon_index(unknown);
    draw_icon(unknown, default_pixels, -1);
    wsprintfA(line, "Wine's default icon is index %d", default_index);
    check(default_index >= 0, line);
    check(icon_index(app64) != default_index && cached_index(png, 0) != default_index && cached_index(blue, 0) >= 0 &&
          icon_index(text) != default_index,
          "the fixtures' own icons are not the default (the expected icons can be told apart)");

    /* .lnk: the icon it names */
    write_link(in_fixture(link, L"names-ico.lnk"), app64, blue, 0);
    expect_ico(link, blue, "a .lnk naming an .ico");
    write_link(in_fixture(link, L"names-png.lnk"), app64, png, 0);
    expect_ico(link, png, "a .lnk naming a 256 px PNG icon");
    write_link(in_fixture(link, L"names-exe.lnk"), plain, app32, 0);
    expect(link, app32, "a .lnk naming a program's icon (an installer's link)");
    write_link(in_fixture(link, L"names-relative.lnk"), app64, L"green.ico", 0);
    expect_ico(link, green, "a .lnk naming its icon relative to itself");
    /* .lnk: no icon named (Create shortcut before 2494, other programs' links), its target's own */
    write_link(in_fixture(link, L"to-app64.lnk"), app64, NULL, 0);
    expect(link, app64, "a .lnk to a 64-bit program");
    write_link(in_fixture(link, L"to-app32.lnk"), app32, NULL, 0);
    expect(link, app32, "a .lnk to a 32-bit program");
    write_link(in_fixture(link, L"to-folder.lnk"), folder, NULL, 0);
    expect(link, folder, "a .lnk to a folder");
    write_link(in_fixture(link, L"to-text.lnk"), text, NULL, 0);
    expect(link, text, "a .lnk to a document, its type's icon");
    /* .lnk copied from another PC: its paths name a folder that is not here, the files were copied beside it */
    in_fixture(copies, L"copied");
    CreateDirectoryW(copies, NULL);
    PathCombineW(moved, copies, L"game.exe");
    CopyFileW(app32, moved, FALSE);
    PathCombineW(link, copies, L"game.lnk");
    write_link(link, L"D:\\Games\\Somewhere\\game.exe", NULL, 0);
    expect(link, moved, "a copied .lnk whose program was copied beside it");
    PathCombineW(moved, copies, L"game.ico");
    CopyFileW(blue, moved, FALSE);
    PathCombineW(link, copies, L"game-icon.lnk");
    write_link(link, L"D:\\Games\\Somewhere\\game.exe", L"D:\\Games\\Somewhere\\game.ico", 0);
    expect_ico(link, moved, "a copied .lnk whose icon file was copied beside it");
    /* .url: a store client's shortcut names its icon file */
    WideCharToMultiByte(CP_ACP, 0, png, -1, ansi, MAX_PATH, NULL, NULL);
    write_url(in_fixture(link, L"store.url"), ansi);
    expect_ico(link, png, "a .url naming its icon (a store client's desktop shortcut)");
    PathCombineW(link, copies, L"store-copied.url");
    write_url(link, "C:\\Program Files (x86)\\Elsewhere\\game.ico");
    expect_ico(link, moved, "a copied .url whose icon file was copied beside it");
    /* Genuinely missing: nothing to show but the default, no icon invented */
    write_link(in_fixture(link, L"gone.lnk"), L"D:\\Nowhere\\gone.exe", NULL, 0);
    expect(link, NULL, "a .lnk whose program is nowhere");

    save_sheet();
    remove_fixture(fixture);
    CoUninitialize();
    wsprintfA(line, "PS5_PROTON_SHORTCUT_ICONS %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    say(line);
    ExitProcess(failures ? 1 : 0);
}
