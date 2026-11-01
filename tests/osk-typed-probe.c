/* SPDX-License-Identifier: GPL-3.0-or-later
 * The on-screen keyboard's text reaching a game window, without a person (PW_OSK_TEST_TEXT in src/pw_osk.h).
 * A visible window waits for characters; the title, started with WINE_PS5_OSK_TEST=type, types the fixed line
 * ("h", e acute, "llo ", Zhe, ri, then Return) into the desktop as the keyboard does after the system dialog: UTF-16
 * units as KEYEVENTF_UNICODE keys (Wine patch 2424). Every unit must arrive as WM_CHAR, in order.
 * Prints PS5_PROTON_OSK_TYPED PASS or FAIL with what arrived; gives up after 70 s. */
#include <windows.h>

static const WCHAR expected[] = {0x68, 0xe9, 0x6c, 0x6c, 0x6f, 0x20, 0x416, 0x65e5, 0x0d};
#define EXPECTED_COUNT (sizeof(expected) / sizeof(expected[0]))

static WCHAR got[64];
static unsigned count;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static void finish(const char *verdict)
{
    static const char digits[] = "0123456789abcdef";
    char line[256];
    unsigned i, n = 0;
    const char *head = "PS5_PROTON_OSK_TYPED ";
    while (*head) line[n++] = *head++;
    while (*verdict) line[n++] = *verdict++;
    for (head = " got="; *head; head++) line[n++] = *head;
    for (i = 0; i < count && n < sizeof(line) - 8; i++) {
        if (i) line[n++] = ',';
        line[n++] = digits[(got[i] >> 12) & 15];
        line[n++] = digits[(got[i] >> 8) & 15];
        line[n++] = digits[(got[i] >> 4) & 15];
        line[n++] = digits[got[i] & 15];
    }
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    say(line);
    ExitProcess(0);
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CHAR) {
        if (count < sizeof(got) / sizeof(got[0])) got[count++] = (WCHAR)wparam;
        if (count >= EXPECTED_COUNT) {
            unsigned i;
            for (i = 0; i < EXPECTED_COUNT; i++)
                if (got[i] != expected[i]) finish("FAIL");
            finish("PASS");
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    WNDCLASSW class = {0};
    MSG message;
    HWND window;
    DWORD start = GetTickCount();

    class.lpfnWndProc = proc;
    class.hInstance = GetModuleHandleW(NULL);
    class.lpszClassName = L"ps5_osk_typed_probe";
    RegisterClassW(&class);
    window = CreateWindowW(L"ps5_osk_typed_probe", L"osk typed probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           0, 0, 640, 360, NULL, NULL, class.hInstance, NULL);
    if (!window) finish("FAIL");
    SetForegroundWindow(window);
    SetFocus(window);
    say("PS5_PROTON_OSK_TYPED waiting for typed text\r\n");
    for (;;) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount() - start > 70000) finish("FAIL");
        Sleep(16);
    }
}
