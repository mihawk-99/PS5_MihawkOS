/* SPDX-License-Identifier: GPL-3.0-or-later */
/* How many files a program may hold open for writing at once: COUNT files created in C:\many-files-probe, each written
 * with its own number, all held open, then each read back through its own handle, then closed and deleted. A store
 * client's download held thousands open and one CreateFile failed with STATUS_TOO_MANY_OPENED_FILES: the title's
 * descriptor table (12,555 slots) was full of shared code copies and parked files (a tester's store-client download, 2026-10-31;
 * wine/ps5/pw_wine_codeshare.h). PASS when all COUNT open, write, read back and close; the lines before the verdict
 * are the measurement either way. Exits 0. */
#include <windows.h>

#define COUNT 10000

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static HANDLE files[COUNT];

static void name_of(WCHAR *path, unsigned i)
{
    wsprintfW(path, L"C:\\many-files-probe\\f%05u.bin", i);
}

void mainCRTStartup(void)
{
    char line[256];
    WCHAR path[MAX_PATH];
    unsigned opened = 0, written = 0, read_back = 0, closed = 0;
    DWORD error = 0, started = GetTickCount();

    CreateDirectoryW(L"C:\\many-files-probe", NULL);
    for (; opened < COUNT; opened++) {
        name_of(path, opened);
        files[opened] = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (files[opened] == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            break;
        }
        DWORD value = opened, done = 0;
        if (WriteFile(files[opened], &value, sizeof(value), &done, NULL) && done == sizeof(value)) written++;
        if ((opened + 1) % 2000 == 0) {
            wsprintfA(line, "PS5_PROTON_MANY_FILES opened=%u ms=%lu\n", opened + 1, GetTickCount() - started);
            say(line);
        }
    }
    wsprintfA(line, "PS5_PROTON_MANY_FILES opened=%u of %u written=%u error=%lu ms=%lu\n", opened, COUNT, written, error,
              GetTickCount() - started);
    say(line);
    /* Every file is still open: each read goes through its own handle, so a parked file is reopened at its place. */
    for (unsigned i = 0; i < opened; i++) {
        DWORD value = 0xffffffffu, done = 0;
        if (SetFilePointer(files[i], 0, NULL, FILE_BEGIN) == 0 && ReadFile(files[i], &value, sizeof(value), &done, NULL) &&
            done == sizeof(value) && value == i)
            read_back++;
    }
    for (unsigned i = 0; i < opened; i++) {
        closed += CloseHandle(files[i]) != 0;
        name_of(path, i);
        DeleteFileW(path);
    }
    RemoveDirectoryW(L"C:\\many-files-probe");
    wsprintfA(line, "PS5_PROTON_MANY_FILES read_back=%u closed=%u ms=%lu\n", read_back, closed, GetTickCount() - started);
    say(line);
    if (opened == COUNT && written == COUNT && read_back == COUNT && closed == COUNT) say("PS5_PROTON_MANY_FILES PASS\n");
    else {
        wsprintfA(line, "PS5_PROTON_MANY_FILES FAIL: opened %u, written %u, read back %u, closed %u of %u\n", opened,
                  written, read_back, closed, COUNT);
        say(line);
    }
    ExitProcess(0);
}
