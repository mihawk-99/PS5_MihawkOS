/* SPDX-License-Identifier: GPL-3.0-or-later
 * A deliberate native fault, to test whether a crash in the title process recovers cleanly (the title returns to the
 * launcher via sceSystemServiceLoadExec) or hangs the console. This is the decisive in-title crash-safety probe
 * (docs/TITLE_CLIENT.md, "Stop and end"). It prints a marker, waits a few seconds so the title's
 * steady state is logged, then writes through a null pointer (a native SIGSEGV). It never prints PASS: the verdict is
 * whether the console still answers afterward (klog "restart: to=launcher reason=game-fault", ps5vkctl replies) — read by
 * the runner, not by this program. 64-bit only, matching the in-title constraint. */
#include <windows.h>

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

void mainCRTStartup(void)
{
    say("PS5_PROTON_CRASH_PROBE start: a null write follows in 5 s, to test in-title crash recovery\n");
    Sleep(5000);
    say("PS5_PROTON_CRASH_PROBE fault: writing through a null pointer now\n");
    Sleep(200);
    *(volatile int *)0 = 0x1234;   /* native SIGSEGV: the fault under test */
    /* Only reached if the write did not fault, which would itself be a finding. */
    say("PS5_PROTON_CRASH_PROBE FAIL: the null write did not fault\n");
    ExitProcess(2);
}
