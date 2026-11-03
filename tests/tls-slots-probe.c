/* SPDX-License-Identifier: GPL-3.0-or-later */
/* How many of the 64 TLS slots in the TEB a program still gets after the system's DLLs took theirs. A TlsAlloc index of
 * 64 or more lives in TEB->TlsExpansionSlots, a table each thread gets only on its first TlsSetValue there; a thread
 * that reads it before then reads through NULL + 8 x (index - 64). On Windows a program that loads Direct3D 12 still
 * has most of the 64; a game that counts on that met NULL + 8 x n reads (2026-11-03).
 *   start   free primary slots before loading anything
 *   loaded  free primary slots after dxgi, d3d12, d3d11, xinput1_4, dinput8, ws2_32, winhttp, bcrypt, dbghelp, ole32
 *   fls     the first FlsAlloc index after the same loads
 * Every index taken is released before the next count. PASS when at least 48 primary slots are free after the loads.
 * Exits 0. */
#include <windows.h>

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

/* Takes every slot below 64 that TlsAlloc hands out, counts them, then gives them back. */
static unsigned free_primary(DWORD *first)
{
    DWORD taken[TLS_MINIMUM_AVAILABLE];
    unsigned count = 0;
    *first = TLS_OUT_OF_INDEXES;
    for (;;) {
        const DWORD index = TlsAlloc();
        if (index == TLS_OUT_OF_INDEXES) break;
        if (index >= TLS_MINIMUM_AVAILABLE) { TlsFree(index); break; }
        if (!count) *first = index;
        taken[count++] = index;
    }
    for (unsigned i = 0; i < count; i++) TlsFree(taken[i]);
    return count;
}

void mainCRTStartup(void)
{
    static const char *const dlls[] = {"dxgi.dll", "d3d12.dll", "d3d11.dll", "xinput1_4.dll", "dinput8.dll", "ws2_32.dll",
                                       "winhttp.dll", "bcrypt.dll", "dbghelp.dll", "ole32.dll"};
    char line[256];
    DWORD first_start, first_loaded, fls;
    unsigned loaded_dlls = 0;
    const unsigned start = free_primary(&first_start);

    for (unsigned i = 0; i < sizeof(dlls) / sizeof(dlls[0]); i++) loaded_dlls += LoadLibraryA(dlls[i]) != NULL;
    const unsigned loaded = free_primary(&first_loaded);
    fls = FlsAlloc(NULL);
    if (fls != FLS_OUT_OF_INDEXES) FlsFree(fls);

    wsprintfA(line, "PS5_PROTON_TLS_SLOTS start free=%u first=%lu\n", start, first_start);
    say(line);
    wsprintfA(line, "PS5_PROTON_TLS_SLOTS loaded %u dlls free=%u first=%lu fls=%lu\n", loaded_dlls, loaded, first_loaded, fls);
    say(line);
    wsprintfA(line, "PS5_PROTON_TLS_SLOTS %s: %u of 64 primary TLS slots free after the loads\n", loaded >= 48 ? "PASS" : "FAIL",
              loaded);
    say(line);
    ExitProcess(0);
}
