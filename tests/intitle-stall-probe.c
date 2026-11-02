/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A game's waits while its own threads spin, with no input: what a 64-bit game in the title (Alpha 7's "Run inside
 * MihawkOS") froze on for up to 11 s until the mouse moved (a tester's Elden Ring, 2026-11-01: zero-timeout waits at
 * 1.4 million a second, server calls at 0, no new frame). Each phase runs --seconds=N (8) while --spinners=N (3)
 * threads poll an event that is never set, with no yield, and a churn thread makes and closes events (the game's
 * create_event and close_handle):
 *   pace   MsgWaitForMultipleObjects for nothing but messages, 16 ms timeout (a frame loop's pacing)
 *   timer  a periodic 16 ms waitable timer
 *   ping   an event set by a worker thread and back (a job handed off and finished)
 *   msg    a message posted to its own window and taken back, and GetCursorPos (server calls)
 *   sleep  Sleep(1)
 * A step that takes 500 ms or more is a stall; one stall fails the probe. "PS5_PROTON_INTITLE_STALL phase=..." lines,
 * then PASS or FAIL. Kernel32 and user32 only, no C runtime. */
#include <windows.h>

static LARGE_INTEGER frequency;
static HANDLE never;
static volatile LONG stop;
static unsigned failures;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

/* Integer milliseconds: no floating point, which needs the C runtime's _fltused. */
static ULONGLONG now_ms(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (ULONGLONG)counter.QuadPart * 1000 / (ULONGLONG)frequency.QuadPart;
}

/* The number after name on the command line (name ends in '='), or fallback. */
static DWORD option(const char *name, DWORD fallback)
{
    const int length = lstrlenA(name);
    for (const char *at = GetCommandLineA(); *at; ++at) {
        int i = 0;
        DWORD value = 0;
        while (i < length && at[i] == name[i]) ++i;
        if (i < length || at[length] < '0' || at[length] > '9') continue;
        for (at += length; *at >= '0' && *at <= '9'; ++at) value = value * 10 + (DWORD)(*at - '0');
        return value;
    }
    return fallback;
}

static DWORD WINAPI spinner(void *unused)
{
    (void)unused;
    while (!stop) WaitForSingleObject(never, 0);
    return 0;
}

static DWORD WINAPI churn(void *unused)
{
    (void)unused;
    while (!stop) {
        for (int i = 0; i < 4; ++i) CloseHandle(CreateEventW(NULL, FALSE, FALSE, NULL));
        Sleep(1);
    }
    return 0;
}

static HANDLE go, done;
static DWORD WINAPI worker(void *unused)
{
    (void)unused;
    while (WaitForSingleObject(go, INFINITE) == WAIT_OBJECT_0 && !stop) SetEvent(done);
    return 0;
}

typedef struct Phase { const char *name; unsigned steps, over100, stalls; ULONGLONG worst; } Phase;

static void step(Phase *phase, ULONGLONG began)
{
    const ULONGLONG took = now_ms() - began;
    ++phase->steps;
    phase->over100 += took >= 100;
    phase->stalls += took >= 500;
    if (took > phase->worst) phase->worst = took;
}

static void report(const Phase *phase)
{
    char line[200];
    wsprintfA(line, "PS5_PROTON_INTITLE_STALL phase=%s steps=%u worst_ms=%u over100=%u stalls=%u\n", phase->name,
              phase->steps, (unsigned)phase->worst, phase->over100, phase->stalls);
    say(line);
    if (phase->stalls || !phase->steps) ++failures;
}

static void drain(void)
{
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
}

void mainCRTStartup(void)
{
    const DWORD seconds = option("--seconds=", 8), spinners = option("--spinners=", 3);
    static WNDCLASSW type;   /* static: zeroed without a memset, which this probe has no C runtime for */
    HWND window;
    HANDLE timer;
    LARGE_INTEGER due;
    char line[200];
    Phase pace = {"pace"}, ticks = {"timer"}, ping = {"ping"}, msg = {"msg"}, nap = {"sleep"};
    ULONGLONG end, began;

    QueryPerformanceFrequency(&frequency);
    never = CreateEventW(NULL, TRUE, FALSE, NULL);
    go = CreateEventW(NULL, FALSE, FALSE, NULL);
    done = CreateEventW(NULL, FALSE, FALSE, NULL);
    timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    type.lpfnWndProc = DefWindowProcW;
    type.lpszClassName = L"intitle-stall";
    RegisterClassW(&type);
    window = CreateWindowW(L"intitle-stall", L"intitle-stall", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    wsprintfA(line, "PS5_PROTON_INTITLE_STALL start seconds=%lu spinners=%lu window=%d\n", seconds, spinners, window != NULL);
    say(line);
    for (DWORD i = 0; i < spinners; ++i) CloseHandle(CreateThread(NULL, 0, spinner, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, churn, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));

    for (end = now_ms() + seconds * 1000ull; now_ms() < end;) {
        began = now_ms();
        if (MsgWaitForMultipleObjects(1, &never, FALSE, 16, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) drain();
        step(&pace, began);
    }
    report(&pace);

    due.QuadPart = -160000;
    SetWaitableTimer(timer, &due, 16, NULL, NULL, FALSE);
    for (end = now_ms() + seconds * 1000ull, began = now_ms(); now_ms() < end;) {
        if (WaitForSingleObject(timer, 1000) != WAIT_OBJECT_0) ++ticks.stalls;
        step(&ticks, began);
        began = now_ms();
    }
    CancelWaitableTimer(timer);
    report(&ticks);

    for (end = now_ms() + seconds * 1000ull; now_ms() < end;) {
        began = now_ms();
        SetEvent(go);
        if (WaitForSingleObject(done, 2000) != WAIT_OBJECT_0) ++ping.stalls;
        step(&ping, began);
    }
    report(&ping);

    for (end = now_ms() + seconds * 1000ull; now_ms() < end;) {
        MSG message;
        POINT cursor;
        began = now_ms();
        PostMessageW(window, WM_USER, 0, 0);
        if (!GetMessageW(&message, window, WM_USER, WM_USER)) ++msg.stalls;
        GetCursorPos(&cursor);
        step(&msg, began);
    }
    report(&msg);

    for (end = now_ms() + seconds * 1000ull; now_ms() < end;) {
        began = now_ms();
        Sleep(1);
        step(&nap, began);
    }
    report(&nap);

    stop = 1;
    SetEvent(go);
    wsprintfA(line, "PS5_PROTON_INTITLE_STALL %s: %u of 5 phases stalled\n", failures ? "FAIL" : "PASS", failures);
    say(line);
    ExitProcess(failures ? 1 : 0);
}
