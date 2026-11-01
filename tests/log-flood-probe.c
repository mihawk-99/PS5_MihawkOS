/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Whether a game's Wine output keeps the end of the session's logs: LINES debug lines of 100 bytes each (20 MB), through
 * OutputDebugStringA, which Wine prints on its debugstr channel when WINEDEBUG has warn+debugstr. A tester's in-title
 * game filled the title's 8 MiB log with its Wine output, and the log lost its last 400 s before the system ended the
 * title (2026-10-31; src/pw_capped_log.h). The probe only writes them; tools/run-console.py --expect-log-ends reads the
 * session's logs back: the start kept, the last line kept, the size bounded. PASS once every line is written. Exits 0. */
#include <windows.h>

#define LINES 200000

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

void mainCRTStartup(void)
{
    char line[160];
    const DWORD started = GetTickCount();

    for (unsigned i = 0; i < LINES; i++) {
        /* 100 bytes with the newline: "log-flood line 0000000 " and 77 bytes of padding. */
        wsprintfA(line, "log-flood line %07u %s\n", i,
                  "................................................................ ...........");
        OutputDebugStringA(line);
    }
    wsprintfA(line, "PS5_PROTON_LOG_FLOOD wrote lines=%u bytes=%u ms=%lu\n", LINES, LINES * 100u,
              GetTickCount() - started);
    say(line);
    say("PS5_PROTON_LOG_FLOOD PASS\n");
    ExitProcess(0);
}
