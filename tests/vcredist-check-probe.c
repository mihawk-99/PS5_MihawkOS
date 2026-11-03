/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What a launcher or an installer finds when it asks whether a Visual C++ redistributable is installed (Wine patch 2455):
 *   registry  VisualStudio\14.0, 12.0, 11.0\VC\Runtimes\{x64,x86} and 10.0\VC\VCRedist\{x64,x86}, "Installed" = 1, in the
 *             64-bit view (x64) and the 32-bit view (x64 and x86), as on 64-bit Windows;
 *   modules   the runtimes those records stand for load: msvcr80/90 (WinSxS), msvcr100, msvcr110, msvcr120, msvcp140,
 *             vcruntime140, ucrtbase;
 *   msi       MsiQueryProductState for the 2010 SP1 redistributables' published product codes (reported, not claimed:
 *             the builtins are not those MSI products);
 *   steam     the values under Steam's CommonRedist\vcredist markers (reported: Steam writes them after a run).
 * --run=PATH (the last argument; spaces allowed) starts PATH /install /quiet /norestart (a real vc_redist) and reports its
 * exit code and time.
 * "PS5_PROTON_VCREDIST" lines, then PASS (every registry record and module) or FAIL. Kernel32 and user32 only. */
#include <windows.h>

typedef LONG (WINAPI *RegOpen)(HKEY, const char *, DWORD, REGSAM, HKEY *);
typedef LONG (WINAPI *RegQuery)(HKEY, const char *, DWORD *, DWORD *, BYTE *, DWORD *);
typedef LONG (WINAPI *RegEnum)(HKEY, DWORD, char *, DWORD *, DWORD *, DWORD *, BYTE *, DWORD *);
typedef LONG (WINAPI *RegClose)(HKEY);
typedef int (WINAPI *MsiState)(const char *);

static RegOpen reg_open;
static RegQuery reg_query;
static RegEnum reg_enum;
static RegClose reg_close;
static unsigned failures;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

/* "Installed" of key in view (KEY_WOW64_64KEY or KEY_WOW64_32KEY): 1, 0, or -1 for no key. */
static int installed(const char *key, REGSAM view)
{
    HKEY handle;
    DWORD type = 0, value = 0, size = sizeof(value);
    if (reg_open(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | view, &handle)) return -1;
    const LONG status = reg_query(handle, "Installed", NULL, &type, (BYTE *)&value, &size);
    reg_close(handle);
    return !status && type == REG_DWORD ? (int)value : 0;
}

static void record(const char *year, const char *key, const char *arch, REGSAM view, const char *view_name)
{
    char path[160], line[256];
    wsprintfA(path, key, arch);
    const int got = installed(path, view);
    wsprintfA(line, "PS5_PROTON_VCREDIST registry %s %s %s-view installed=%d\n", year, arch, view_name, got);
    say(line);
    if (got != 1) ++failures;
}

static const char *find(const char *name)
{
    const char *line = GetCommandLineA();
    const int length = lstrlenA(name);
    for (; *line; ++line) {
        int i = 0;
        while (i < length && line[i] == name[i]) ++i;
        if (i == length) return line + length;
    }
    return NULL;
}

void mainCRTStartup(void)
{
    char line[512];
    HMODULE advapi = LoadLibraryA("advapi32.dll");
    reg_open = (RegOpen)GetProcAddress(advapi, "RegOpenKeyExA");
    reg_query = (RegQuery)GetProcAddress(advapi, "RegQueryValueExA");
    reg_enum = (RegEnum)GetProcAddress(advapi, "RegEnumValueA");
    reg_close = (RegClose)GetProcAddress(advapi, "RegCloseKey");
    if (!reg_open || !reg_query || !reg_enum || !reg_close) { say("PS5_PROTON_VCREDIST FAIL: no registry entry points\n"); ExitProcess(1); }
    static const struct { const char *year, *key; } keys[] = {
        {"2015-2022", "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\%s"},
        {"2013", "SOFTWARE\\Microsoft\\VisualStudio\\12.0\\VC\\Runtimes\\%s"},
        {"2012", "SOFTWARE\\Microsoft\\VisualStudio\\11.0\\VC\\Runtimes\\%s"},
        {"2010", "SOFTWARE\\Microsoft\\VisualStudio\\10.0\\VC\\VCRedist\\%s"},
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        record(keys[i].year, keys[i].key, "x64", KEY_WOW64_64KEY, "64");
        record(keys[i].year, keys[i].key, "x64", KEY_WOW64_32KEY, "32");
        record(keys[i].year, keys[i].key, "x86", KEY_WOW64_32KEY, "32");
    }
    static const char *const modules[] = {"msvcr80.dll", "msvcr90.dll", "msvcr100.dll", "msvcr110.dll", "msvcr120.dll",
                                          "msvcp140.dll", "vcruntime140.dll", "ucrtbase.dll"};
    for (unsigned i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        const HMODULE module = LoadLibraryA(modules[i]);
        wsprintfA(line, "PS5_PROTON_VCREDIST module %s %s\n", modules[i], module ? "loads" : "MISSING");
        say(line);
        if (!module) ++failures;
    }
    /* Reported only: the builtins are not these MSI products, and nothing claims they are. */
    MsiState msi_state = (MsiState)GetProcAddress(LoadLibraryA("msi.dll"), "MsiQueryProductStateA");
    static const char *const products[][2] = {{"2010 SP1 x64", "{1D8E6291-B0D5-35EC-8441-6616F567A0F7}"},
                                              {"2010 SP1 x86", "{F0C3E5D1-1ADE-321E-8167-68EF0DE699A5}"}};
    for (unsigned i = 0; i < 2; ++i) {
        wsprintfA(line, "PS5_PROTON_VCREDIST msi %s state=%d (5 installed, -1 unknown)\n", products[i][0],
                  msi_state ? msi_state(products[i][1]) : -99);
        say(line);
    }
    static const char *const years[] = {"2005", "2008", "2010", "2012", "2013", "2015", "2017", "2019", "2022"};
    for (unsigned i = 0; i < sizeof(years) / sizeof(years[0]); ++i) {
        char key[128], name[128];
        HKEY handle;
        unsigned values = 0;
        wsprintfA(key, "Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\%s", years[i]);
        if (!reg_open(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_32KEY, &handle)) {
            DWORD length = sizeof(name);
            while (!reg_enum(handle, values, name, &length, NULL, NULL, NULL, NULL)) ++values, length = sizeof(name);
            reg_close(handle);
        }
        wsprintfA(line, "PS5_PROTON_VCREDIST steam marker %s values=%u\n", years[i], values);
        say(line);
    }
    const char *run = find("--run=");
    if (run) {
        char command[MAX_PATH + 64];
        STARTUPINFOA startup = {sizeof(startup)};
        PROCESS_INFORMATION child;
        DWORD code = 0, began = GetTickCount();
        int n = 0;
        command[n++] = '"';
        for (; *run && *run != '"' && n < MAX_PATH; ++run) command[n++] = *run;   /* the rest of the line: a path may hold spaces */
        while (n > 1 && command[n - 1] == ' ') --n;
        command[n] = 0;
        lstrcatA(command, "\" /install /quiet /norestart");
        if (CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child)) {
            const DWORD waited = WaitForSingleObject(child.hProcess, 300000);
            GetExitCodeProcess(child.hProcess, &code);
            wsprintfA(line, "PS5_PROTON_VCREDIST run exit=%lu %s after %lu ms\n", code, waited ? "(still running)" : "",
                      GetTickCount() - began);
        } else {
            wsprintfA(line, "PS5_PROTON_VCREDIST run could not start: error %lu\n", GetLastError());
        }
        say(line);
    }
    wsprintfA(line, "PS5_PROTON_VCREDIST %s: %u missing\n", failures ? "FAIL" : "PASS", failures);
    say(line);
    ExitProcess(failures ? 1 : 0);
}
