/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Whether threads that started before a DLL with static TLS loaded get its TLS block. 24 threads start and wait; the
 * program then loads tls-late-dll.dll (tests/tls-late-dll.c) and every thread, old and new, reads its copy of the DLL's
 * thread variable. Wine gives the old threads theirs through ProcessTlsInformation, one per thread it counted; a thread
 * it missed reads through NULL + the variable's offset (a game's workers did, 2026-11-03).
 *   value  the threads that read the template's value / those with no block (-2) / with no vector (-1) / other
 * PASS when all 25 read the value (the 24 old threads and one started after the load). Exits 0. */
#include <windows.h>

#define OLD 24
static HANDLE go;
static LONG (*read_value)(void);
static volatile LONG good, no_block, no_vector, other;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static DWORD WINAPI reader(void *arg)
{
    LONG value;
    if (arg) WaitForSingleObject(go, INFINITE);
    value = read_value();
    if (value == 0x7151) InterlockedIncrement(&good);
    else if (value == -2) InterlockedIncrement(&no_block);
    else if (value == -1) InterlockedIncrement(&no_vector);
    else InterlockedIncrement(&other);
    return 0;
}

void mainCRTStartup(void)
{
    HANDLE threads[OLD + 1];
    HMODULE dll;
    char line[256];

    go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (unsigned i = 0; i < OLD; i++) threads[i] = CreateThread(NULL, 0, reader, (void *)1, 0, NULL);
    Sleep(200);
    if (!(dll = LoadLibraryA("tls-late-dll.dll")) || !(read_value = (void *)GetProcAddress(dll, "tls_late_value"))) {
        wsprintfA(line, "PS5_PROTON_TLS_LATE FAIL: tls-late-dll.dll did not load (error %lu)\n", GetLastError());
        say(line);
        ExitProcess(0);
    }
    SetEvent(go);
    threads[OLD] = CreateThread(NULL, 0, reader, NULL, 0, NULL);
    WaitForMultipleObjects(OLD + 1, threads, TRUE, 10000);
    wsprintfA(line, "PS5_PROTON_TLS_LATE value=%ld no_block=%ld no_vector=%ld other=%ld of %u\n", good, no_block, no_vector,
              other, OLD + 1);
    say(line);
    wsprintfA(line, "PS5_PROTON_TLS_LATE %s: %ld of %u threads have the late DLL's TLS block\n",
              good == OLD + 1 ? "PASS" : "FAIL", good, OLD + 1);
    say(line);
    ExitProcess(0);
}
