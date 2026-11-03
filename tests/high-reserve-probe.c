/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A fixed reservation far above the console's address space, as a game asked for one (Spelunky 2, 2026-11-01: 32 MiB
 * at 0x7XX_7F80_0000, XX odd, MEM_RESERVE | MEM_COMMIT, read-write, and nothing lower). On Windows these are free; the
 * console's app area ends near 1 TiB. For each address the probe says what VirtualAlloc gave (the address, the same
 * one or another, or the error) and whether the memory takes a write and reads it back; then frees it.
 * "PS5_PROTON_HIGH_RESERVE asked=... got=... error=... same=... works=..." lines, then PASS when every request got
 * usable memory (Windows, or Wine patch 2430 unless WINE_PS5_HIGH_RESERVE_ELSEWHERE=0), else FAIL with how many were
 * refused. Kernel32 only, no C runtime. */
#include <windows.h>

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

void mainCRTStartup(void)
{
    /* The lowest, a middle and the highest of the game's 128 slots. */
    static const ULONG_PTR asked[] = {0x7017f800000ull, 0x7817f800000ull, 0x7ff7f800000ull};
    const SIZE_T size = 32u << 20;
    unsigned refused = 0, broken = 0;
    char line[256];

    for (unsigned i = 0; i < sizeof(asked) / sizeof(asked[0]); ++i) {
        SetLastError(0);
        volatile unsigned char *got = VirtualAlloc((void *)asked[i], size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        const DWORD error = got ? 0 : GetLastError();
        int works = 0;
        if (got) {
            got[0] = 0x5a;
            got[size - 1] = 0xa5;
            works = got[0] == 0x5a && got[size - 1] == 0xa5;
            broken += !works;
        } else ++refused;
        wsprintfA(line, "PS5_PROTON_HIGH_RESERVE asked=%08lx%08lx got=%08lx%08lx error=%lu same=%d works=%d\n",
                  (DWORD)(asked[i] >> 32), (DWORD)asked[i], (DWORD)((ULONG_PTR)got >> 32), (DWORD)(ULONG_PTR)got, error,
                  (ULONG_PTR)got == asked[i], works);
        say(line);
        if (got) VirtualFree((void *)got, 0, MEM_RELEASE);
    }
    wsprintfA(line, "PS5_PROTON_HIGH_RESERVE %s: %u refused, %u not usable\n", refused || broken ? "FAIL" : "PASS",
              refused, broken);
    say(line);
    ExitProcess(refused || broken ? 1 : 0);
}
