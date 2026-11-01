/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What a game in the title learns of its parent, a live process in another helper (Wine patch 2426): a store game ended
 * itself 0.67 s after its Direct3D 12 device whenever a parent was alive (cmd.exe or the desktop's explorer.exe), and
 * played when it had none (witcher3-dx12-parent-pro-2026-11-01 against witcher3-dx12-repro-pro-2026-11-01). It asks
 * what that game asks, of its parent and of itself: NtQueryInformationProcess classes 0 (basic), 0x1a (WoW64) and 3
 * (memory counters), the PEB read through ReadProcessMemory, EnumProcessModulesEx, GetModuleBaseNameA and
 * GetModuleFileNameExA. Every answer must be as good for the parent as for itself. It imports Direct3D 11, so a copy in a
 * library folder runs in the title (tests/process-probe.c --parent-query starts it). Exits 0 on PASS. */
#include <windows.h>
#include <winternl.h>
#include <d3d11.h>

typedef LONG (WINAPI *Query)(HANDLE, ULONG, void *, ULONG, ULONG *);
typedef BOOL (WINAPI *EnumModules)(HANDLE, HMODULE *, DWORD, DWORD *, DWORD);
typedef DWORD (WINAPI *ModuleName)(HANDLE, HMODULE, char *, DWORD);

static void say(const char *text) { OutputDebugStringA(text); }

/* Each check of one process: 1 when it got a usable answer, with what it got in the log. */
static unsigned ask(const char *who, HANDLE process, Query query, EnumModules enum_modules, ModuleName base_name,
                    ModuleName file_name, char *image, size_t image_size)
{
    char line[512];
    unsigned good = 0;
    PROCESS_BASIC_INFORMATION basic = {0};
    ULONG got = 0;
    LONG status = query(process, 0, &basic, sizeof(basic), &got);
    const int basic_ok = !status && basic.PebBaseAddress;
    wsprintfA(line, "parent-query: %s basic status=%08lx peb=%p\n", who, status, basic.PebBaseAddress); say(line);
    good += basic_ok;
    ULONG_PTR wow64 = 1;
    status = query(process, 0x1a, &wow64, sizeof(wow64), &got);
    wsprintfA(line, "parent-query: %s wow64 status=%08lx value=%Iu\n", who, status, wow64); say(line);
    good += !status && !wow64;
    VM_COUNTERS counters = {0};
    status = query(process, 3, &counters, sizeof(counters), &got);
    const SIZE_T working = counters.WorkingSetSize, pagefile = counters.PagefileUsage;
    wsprintfA(line, "parent-query: %s counters status=%08lx working=%Iu pagefile=%Iu\n", who, status, working, pagefile); say(line);
    good += !status && working && pagefile;
    void *ldr = NULL;
    SIZE_T read = 0;
    const int peb_ok = basic_ok && ReadProcessMemory(process, (char *)basic.PebBaseAddress + 0x18, &ldr, sizeof(ldr), &read) &&
                       read == sizeof(ldr) && ldr;
    wsprintfA(line, "parent-query: %s peb read=%d ldr=%p error=%lu\n", who, peb_ok, ldr, peb_ok ? 0 : GetLastError()); say(line);
    good += peb_ok;
    HMODULE modules[64];
    DWORD needed = 0;
    const int listed = enum_modules(process, modules, sizeof(modules), &needed, 3 /* LIST_MODULES_ALL */) && needed;
    wsprintfA(line, "parent-query: %s modules listed=%d count=%lu error=%lu\n", who, listed, needed / (DWORD)sizeof(HMODULE),
              listed ? 0 : GetLastError()); say(line);
    good += listed;
    image[0] = 0;
    char path[MAX_PATH] = "";
    const DWORD base = listed ? base_name(process, modules[0], image, (DWORD)image_size) : 0;
    const DWORD full = listed ? file_name(process, modules[0], path, sizeof(path)) : 0;
    wsprintfA(line, "parent-query: %s base=%lu \"%s\" file=%lu \"%s\"\n", who, base, image, full, path); say(line);
    good += base && full;
    return good;
}

void mainCRTStartup(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), kernel32 = GetModuleHandleA("kernel32.dll");
    Query query = (Query)GetProcAddress(ntdll, "NtQueryInformationProcess");
    EnumModules enum_modules = (EnumModules)GetProcAddress(kernel32, "K32EnumProcessModulesEx");
    ModuleName base_name = (ModuleName)GetProcAddress(kernel32, "K32GetModuleBaseNameA");
    ModuleName file_name = (ModuleName)GetProcAddress(kernel32, "K32GetModuleFileNameExA");
    char line[256], self_image[MAX_PATH], parent_image[MAX_PATH];
    if (!query || !enum_modules || !base_name || !file_name) { say("PS5_PROTON_PARENT_QUERY FAIL: no entry points\n"); ExitProcess(1); }
    /* Direct3D 11 is imported so the title takes this program; a null device keeps the GPU out of it. */
    if (GetEnvironmentVariableA("PARENT_QUERY_DEVICE", line, sizeof(line)))
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_NULL, NULL, 0, NULL, 0, D3D11_SDK_VERSION, NULL, NULL, NULL);
    PROCESS_BASIC_INFORMATION mine = {0};
    ULONG got = 0;
    query(GetCurrentProcess(), 0, &mine, sizeof(mine), &got);
    const DWORD parent_pid = (DWORD)(ULONG_PTR)mine.Reserved3;  /* InheritedFromUniqueProcessId */
    HANDLE parent = parent_pid ? OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, parent_pid) : NULL;
    wsprintfA(line, "parent-query: parent pid=%lu opened=%d\n", parent_pid, parent != NULL); say(line);
    const unsigned self = ask("self", GetCurrentProcess(), query, enum_modules, base_name, file_name, self_image, sizeof(self_image));
    const unsigned other = parent ? ask("parent", parent, query, enum_modules, base_name, file_name, parent_image, sizeof(parent_image)) : 0;
    const int pass = self == 6 && other == 6;
    wsprintfA(line, "PS5_PROTON_PARENT_QUERY %s self=%u/6 parent=%u/6 parent_image=%s\n", pass ? "PASS" : "FAIL", self, other,
              parent ? parent_image : "(none)");
    say(line);
    ExitProcess(pass ? 0 : 1);
}
