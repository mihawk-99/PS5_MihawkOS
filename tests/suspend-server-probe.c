/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A game in the title whose threads are suspended from outside while they work, as a crash reporter's minidump does
 * (dbghelp's MiniDumpWriteDump: SuspendThread and GetThreadContext on every thread). A tester's Risk of Rain 2 froze at
 * loading three times out of three, each time just after its Steam crash handler started steamerrorreporter64.exe
 * -pid=<game>. Within seconds the in-process wineserver stopped for good: Wine's tick count, which only its main loop
 * advances, stayed at one value for ten minutes, and every 30 s registry save timed out (tester-freeze-loading-2026-11-01,
 * sessions 0-2). In a title the game's threads share one process with the server, so a thread suspended inside Wine's
 * Unix side while it holds a lock the server needs can stop the server; in a helper it cannot.
 *
 * --workers=N (4) threads loop on calls that run Wine's Unix side and the server: create, write and close a file, read
 * its attributes, make and close an event. --rounds=N (300) times the main thread suspends every worker, reads its
 * context, makes a server call (an event made and closed) while they are all suspended, and resumes them. A round of
 * 2 s or more is a stall: a watchdog thread, never suspended, says so and the probe fails.
 *
 * --mode=read: what the reporter's dump does next, from another process. The probe starts a copy of itself (in a
 * helper, the title holding this one) that reads and writes a 256 KiB block of this process 4 KiB to 256 KiB at a time,
 * 10 rounds each, checking every byte. In the title the server copies that memory itself, through a pipe (Wine patch
 * 2453); a copy that does not end in 20 s fails the probe.
 * "PS5_PROTON_SUSPEND_SERVER" lines, then PASS or FAIL. Kernel32 only, no C runtime. */
#include <windows.h>

static volatile LONG round_started_ms, round_done = 1, stop;
static DWORD limit_ms = 2000;
static LONG work_done;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

/* Where the command line's name ends, or NULL. */
static const char *find(const char *name)
{
    const int length = lstrlenA(name);
    for (const char *at = GetCommandLineA(); *at; ++at) {
        int i = 0;
        while (i < length && at[i] == name[i]) ++i;
        if (i == length) return at + length;
    }
    return NULL;
}

static unsigned option(const char *name, unsigned fallback)
{
    unsigned value = 0;
    const char *at = find(name);
    for (; at && *at >= '0' && *at <= '9'; ++at) value = value * 10 + (unsigned)(*at - '0');
    return value ? value : fallback;
}

static DWORD WINAPI worker(void *arg)
{
    WCHAR path[64];
    wsprintfW(path, L"C:\\suspend-server-%u.tmp", (unsigned)(ULONG_PTR)arg);
    static char block[4096];
    while (!stop) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written;
        if (file != INVALID_HANDLE_VALUE) { WriteFile(file, block, sizeof(block), &written, NULL); CloseHandle(file); }
        GetFileAttributesW(path);
        CloseHandle(CreateEventW(NULL, TRUE, FALSE, NULL));
        InterlockedIncrement(&work_done);
    }
    DeleteFileW(path);
    return 0;
}

/* Never suspended: says when a round has run 2 s, which a stopped server makes forever. */
static DWORD WINAPI watchdog(void *unused)
{
    (void)unused;
    while (!stop) {
        Sleep(250);
        if (!round_done && GetTickCount() - (DWORD)round_started_ms >= limit_ms) {
            say(limit_ms > 2000 ? "PS5_PROTON_SUSPEND_SERVER FAIL: the reader has not finished in 20 s (the server stopped?)\n" :
                "PS5_PROTON_SUSPEND_SERVER FAIL: a round with the workers suspended has run 2 s (the server stopped?)\n");
            ExitProcess(1);
        }
    }
    return 0;
}

static char block[262144];

static unsigned long long option_hex(const char *name)
{
    unsigned long long value = 0;
    for (const char *at = find(name); at; ++at) {
        const int digit = *at >= '0' && *at <= '9' ? *at - '0' : *at >= 'a' && *at <= 'f' ? *at - 'a' + 10 : -1;
        if (digit < 0) break;
        value = value * 16 + (unsigned)digit;
    }
    return value;
}

/* The copy in a helper: --reader=<pid> --at=<hex address of block>. */
static void reader(DWORD pid, unsigned long long at)
{
    static char local[262144];
    static const unsigned sizes[] = {4096, 8192, 16384, 65536, 262144};
    char line[160];
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!process) { say("PS5_PROTON_SUSPEND_SERVER reader FAIL: OpenProcess\n"); ExitProcess(2); }
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for (unsigned round = 0; round < 10; ++round) {
            SIZE_T moved = 0;
            if (!ReadProcessMemory(process, (void *)(ULONG_PTR)at, local, sizes[s], &moved) || moved != sizes[s])
                { wsprintfA(line, "PS5_PROTON_SUSPEND_SERVER reader FAIL: read %u bytes, %lu copied\n", sizes[s], (DWORD)moved);
                  say(line); ExitProcess(3); }
            for (unsigned i = 0; i < sizes[s]; ++i)
                if (local[i] != (char)(i * 13 + 5)) { say("PS5_PROTON_SUSPEND_SERVER reader FAIL: wrong bytes\n"); ExitProcess(4); }
            if (!WriteProcessMemory(process, (void *)(ULONG_PTR)at, local, sizes[s], &moved) || moved != sizes[s])
                { wsprintfA(line, "PS5_PROTON_SUSPEND_SERVER reader FAIL: write %u bytes, %lu copied\n", sizes[s], (DWORD)moved);
                  say(line); ExitProcess(5); }
        }
    say("PS5_PROTON_SUSPEND_SERVER reader ok: 4 KiB to 256 KiB read and written, 10 rounds each\n");
    ExitProcess(0);
}

/* --mode=read: a copy of this program in another process reads and writes block. */
static void read_mode(void)
{
    WCHAR path[MAX_PATH], command[MAX_PATH + 96];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION child;
    DWORD code = 99;
    char line[160];
    for (unsigned i = 0; i < sizeof(block); ++i) block[i] = (char)(i * 13 + 5);
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wsprintfW(command, L"\"%s\" --reader=%lu --at=%I64x", path, GetCurrentProcessId(), (unsigned long long)(ULONG_PTR)block);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child))
        { say("PS5_PROTON_SUSPEND_SERVER FAIL: the reader could not start\n"); ExitProcess(1); }
    limit_ms = 20000;
    InterlockedExchange(&round_started_ms, (LONG)GetTickCount());
    InterlockedExchange(&round_done, 0);
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    WaitForSingleObject(child.hProcess, INFINITE);
    InterlockedExchange(&round_done, 1);
    GetExitCodeProcess(child.hProcess, &code);
    wsprintfA(line, "PS5_PROTON_SUSPEND_SERVER %s: mode=read reader_exit=%lu\n", code ? "FAIL" : "PASS", code);
    say(line);
    ExitProcess(code ? 1 : 0);
}

void mainCRTStartup(void)
{
    if (find("--reader=")) reader((DWORD)option("--reader=", 0), option_hex("--at="));
    if (find("--mode=read")) read_mode();
    const unsigned workers = option("--workers=", 4), rounds = option("--rounds=", 300);
    HANDLE threads[16];
    char line[200];
    const unsigned count = workers < 16 ? workers : 16;
    for (unsigned i = 0; i < count; ++i) threads[i] = CreateThread(NULL, 0, worker, (void *)(ULONG_PTR)i, 0, NULL);
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    Sleep(200);
    DWORD slowest = 0;
    for (unsigned round = 0; round < rounds; ++round) {
        const DWORD began = GetTickCount();
        InterlockedExchange(&round_started_ms, (LONG)began);
        InterlockedExchange(&round_done, 0);
        for (unsigned i = 0; i < count; ++i) {
            CONTEXT context;
            context.ContextFlags = CONTEXT_FULL;
            SuspendThread(threads[i]);
            GetThreadContext(threads[i], &context);
        }
        CloseHandle(CreateEventW(NULL, TRUE, FALSE, NULL));   /* a server call while every worker is held */
        for (unsigned i = 0; i < count; ++i) ResumeThread(threads[i]);
        InterlockedExchange(&round_done, 1);
        const DWORD took = GetTickCount() - began;
        if (took > slowest) slowest = took;
        Sleep(round % 7);   /* the workers run on between rounds, landing each suspend somewhere else */
    }
    InterlockedExchange(&stop, 1);
    WaitForMultipleObjects(count, threads, TRUE, 10000);
    wsprintfA(line, "PS5_PROTON_SUSPEND_SERVER PASS: rounds=%u workers=%u slowest_ms=%lu work=%ld\n", rounds, count, slowest,
              work_done);
    say(line);
    ExitProcess(0);
}
