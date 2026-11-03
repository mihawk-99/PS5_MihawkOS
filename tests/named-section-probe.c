/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A named shared section between two processes, the shape of Steam's browser IPC: steam.exe makes
 * SteamChrome_ClientStream_*_client_mem and _server_mem, 25 MiB each, pagefile-backed, and steamwebhelper.exe opens
 * them. With the browser in the title and steam.exe in a helper (Alpha 8, e1-webhelper-title-base-2026-11-02-01) Steam
 * logged "Size on connection to existing CSharedMemStream doesn't match actual: ..., 26214400, 0" and never reached the
 * browser; as two helpers it works. This probe makes the section (25 MiB unless --mib=N), writes a pattern at the start,
 * the middle and the end, starts a second copy of itself with "open" (--child=NAME runs another file name, so the title's
 * pick can place either side), and the child opens the section by name, asks its size (VirtualQuery of a whole view
 * and NtQuerySection), checks the pattern and writes its reply at the end; the parent reads the reply.
 * "PS5_PROTON_NAMED_SECTION ..." lines, then PASS or FAIL. Kernel32 and ntdll only, no C runtime. */
#include <windows.h>
#include <winternl.h>

typedef struct { PVOID base; ULONG attributes; LARGE_INTEGER size; } SectionBasic;
typedef NTSTATUS (WINAPI *QuerySection)(HANDLE, int, PVOID, SIZE_T, SIZE_T *);

static const char kName[] = "Local\\PS5ProtonNamedSectionProbe";
static unsigned failures;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static DWORD option(const char *name, DWORD fallback)
{
    const char *line = GetCommandLineA(), *at;
    const int length = lstrlenA(name);
    for (at = line; *at; ++at) {
        int i = 0;
        while (i < length && at[i] == name[i]) ++i;
        if (i == length) {
            DWORD value = 0;
            for (at += length; *at >= '0' && *at <= '9'; ++at) value = value * 10 + (DWORD)(*at - '0');
            return value;
        }
    }
    return fallback;
}

static int has(const char *word)
{
    const char *line = GetCommandLineA(), *at;
    const int length = lstrlenA(word);
    for (at = line; *at; ++at) {
        int i = 0;
        while (i < length && at[i] == word[i]) ++i;
        if (i == length) return 1;
    }
    return 0;
}

/* The section's size as the caller sees it: a whole view's region (VirtualQuery) and NtQuerySection's. */
static void sizes(HANDLE section, const unsigned char *view, const char *who, SIZE_T want)
{
    char out[300];
    MEMORY_BASIC_INFORMATION info;
    SectionBasic basic;
    ZeroMemory(&basic, sizeof(basic));
    QuerySection query = (QuerySection)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySection");
    const SIZE_T region = VirtualQuery(view, &info, sizeof(info)) ? info.RegionSize : 0;
    const NTSTATUS status = query ? query(section, 0, &basic, sizeof(basic), NULL) : (NTSTATUS)-1;
    wsprintfA(out, "PS5_PROTON_NAMED_SECTION %s region=%lu query=%lu status=%lx want=%lu\n", who, (unsigned long)region,
              (unsigned long)basic.size.QuadPart, (unsigned long)status, (unsigned long)want);
    say(out);
    if ((SIZE_T)basic.size.QuadPart != want) ++failures;
}

static void check(const unsigned char *view, SIZE_T bytes, const char *who)
{
    char out[200];
    const SIZE_T at[3] = { 0, bytes / 2, bytes - 64 };
    for (int i = 0; i < 3; ++i)
        if (view[at[i]] != (unsigned char)(0xa0 + i)) {
            wsprintfA(out, "PS5_PROTON_NAMED_SECTION %s pattern %d wrong at %lu: %02x\n", who, i, (unsigned long)at[i], view[at[i]]);
            say(out);
            ++failures;
        }
}

void mainCRTStartup(void)
{
    const SIZE_T bytes = (SIZE_T)option("--mib=", 25) << 20;
    char out[400];
    if (has(" open")) {
        HANDLE section = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kName);
        unsigned char *view = section ? MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
        if (!view) {
            wsprintfA(out, "PS5_PROTON_NAMED_SECTION child open failed: section=%p error=%lu\n", section, GetLastError());
            say(out);
            ExitProcess(2);
        }
        sizes(section, view, "child", bytes);
        check(view, bytes, "child");
        view[bytes - 1] = 0x5a;
        ExitProcess(failures ? 1 : 0);
    }
    HANDLE section = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)bytes, kName);
    unsigned char *view = section ? MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
    if (!view) {
        wsprintfA(out, "PS5_PROTON_NAMED_SECTION FAIL: create error=%lu\n", GetLastError());
        say(out);
        ExitProcess(1);
    }
    view[0] = 0xa0, view[bytes / 2] = 0xa1, view[bytes - 64] = 0xa2;
    sizes(section, view, "parent", bytes);
    /* The child: this file again, or --child=NAME beside it (a copy under another name the title's pick can tell apart) */
    char path[MAX_PATH], command[MAX_PATH + 64];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    const char *line = GetCommandLineA(), *child = NULL;
    for (const char *at = line; *at; ++at)
        if (at[0] == '-' && at[1] == '-' && at[2] == 'c' && at[3] == 'h' && at[4] == 'i' && at[5] == 'l' && at[6] == 'd' &&
            at[7] == '=') { child = at + 8; break; }
    if (child) {
        char *slash = path + lstrlenA(path);
        while (slash > path && slash[-1] != '\\') --slash;
        int i = 0;
        while (child[i] && child[i] != ' ' && slash - path + i < MAX_PATH - 1) { slash[i] = child[i]; ++i; }
        slash[i] = 0;
    }
    wsprintfA(command, "\"%s\" open --mib=%lu", path, (unsigned long)(bytes >> 20));
    STARTUPINFOA start;
    ZeroMemory(&start, sizeof(start));
    start.cb = sizeof(start);
    PROCESS_INFORMATION process;
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &start, &process)) {
        wsprintfA(out, "PS5_PROTON_NAMED_SECTION FAIL: child not started, error=%lu\n", GetLastError());
        say(out);
        ExitProcess(1);
    }
    DWORD code = 99;
    if (WaitForSingleObject(process.hProcess, 60000) != WAIT_OBJECT_0) say("PS5_PROTON_NAMED_SECTION child did not end in 60 s\n");
    GetExitCodeProcess(process.hProcess, &code);
    const int reply = view[bytes - 1] == 0x5a;
    wsprintfA(out, "PS5_PROTON_NAMED_SECTION child exit=%lu reply=%d\n", code, reply);
    say(out);
    if (code || !reply) ++failures;
    wsprintfA(out, failures ? "PS5_PROTON_NAMED_SECTION FAIL: %u checks\n" : "PS5_PROTON_NAMED_SECTION PASS\n", failures);
    say(out);
    ExitProcess(failures ? 1 : 0);
}
