/* SPDX-License-Identifier: GPL-3.0-or-later
 * The controller a game sees, before and after the control overlay switches it (PS5_Proton src/pw_overlay.h, Wine patches
 * 2471 and 2472; tools/run-battery.py controller-swap64-title, whose pad script switches Xbox 360, Xbox One, DualSense).
 * Every 250 ms it looks as a game would: XInput's controller 0 (connected, and the product XInputGetCapabilitiesEx gives),
 * and whether the DualSense (054c:0ce6) is in the raw input device list; a top-level window counts WM_DEVICECHANGE.
 * Each new identity is printed as it is first seen. PS5_PROTON_PAD_IDENTITY PASS once it has seen, in order, an Xbox 360
 * controller (045e:028e), an Xbox One controller (045e:02ea), then the DualSense with no XInput controller; FAIL with what
 * it saw after 90 s. */
#include <windows.h>
#include <xinput.h>

typedef DWORD (WINAPI *caps_ex_fn)(DWORD, DWORD, DWORD, XINPUT_CAPABILITIES_EX *);

static caps_ex_fn caps_ex;
static unsigned device_changes;
static char seen[512];
static unsigned seen_length;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static void append(char *out, unsigned *n, unsigned capacity, const char *text)
{
    while (*text && *n + 1 < capacity) out[(*n)++] = *text++;
    out[*n] = 0;
}

static void append_hex4(char *out, unsigned *n, unsigned capacity, unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    char text[5] = {digits[(value >> 12) & 15], digits[(value >> 8) & 15], digits[(value >> 4) & 15], digits[value & 15], 0};
    append(out, n, capacity, text);
}

static void append_number(char *out, unsigned *n, unsigned capacity, unsigned value)
{
    char text[12];
    unsigned i = sizeof(text) - 1;
    text[i] = 0;
    do text[--i] = (char)('0' + value % 10); while ((value /= 10) && i);
    append(out, n, capacity, text + i);
}

/* "xinput=028e hid=0": controller 0's product or "none", and the DualSense's HID device in the raw input list */
static void identity(char *out, unsigned capacity, unsigned *product, int *hid)
{
    XINPUT_CAPABILITIES_EX caps;
    RAWINPUTDEVICELIST list[32];
    UINT count = sizeof(list) / sizeof(list[0]), i, n = 0;

    *product = 0;
    for (i = 0; i < sizeof(caps); i++) ((volatile BYTE *)&caps)[i] = 0;
    if (caps_ex && caps_ex(1, 0, 0, &caps) == ERROR_SUCCESS) *product = caps.ProductId;
    *hid = 0;
    if (GetRawInputDeviceList(list, &count, sizeof(list[0])) != (UINT)-1)
        for (i = 0; i < count; i++) {
            RID_DEVICE_INFO info;
            UINT size = sizeof(info);
            info.cbSize = sizeof(info);
            if (list[i].dwType == RIM_TYPEHID &&
                GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) != (UINT)-1 &&
                info.hid.dwVendorId == 0x054c && info.hid.dwProductId == 0x0ce6)
                *hid = 1;
        }
    out[0] = 0;
    append(out, &n, capacity, "xinput=");
    if (*product) append_hex4(out, &n, capacity, *product);
    else append(out, &n, capacity, "none");
    append(out, &n, capacity, *hid ? " hid=1" : " hid=0");
}

static void finish(const char *verdict)
{
    char line[700];
    unsigned n = 0;
    append(line, &n, sizeof(line), "PS5_PROTON_PAD_IDENTITY ");
    append(line, &n, sizeof(line), verdict);
    append(line, &n, sizeof(line), " device_changes=");
    append_number(line, &n, sizeof(line), device_changes);
    append(line, &n, sizeof(line), " seen=");
    append(line, &n, sizeof(line), seen);
    append(line, &n, sizeof(line), "\r\n");
    say(line);
    ExitProcess(0);
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DEVICECHANGE) {
        ++device_changes;
        say("PS5_PROTON_PAD_IDENTITY device change\r\n");
        return TRUE;
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
    HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
    const DWORD start = GetTickCount();
    char last[64] = "";
    int stage = 0;   /* 0 looking for the Xbox 360 controller, 1 the Xbox One, 2 the DualSense, 3 all seen */

    caps_ex = xinput ? (caps_ex_fn)(void *)GetProcAddress(xinput, (LPCSTR)108) : NULL;
    class.lpfnWndProc = proc;
    class.hInstance = GetModuleHandleW(NULL);
    class.lpszClassName = L"ps5_pad_identity_probe";
    RegisterClassW(&class);
    window = CreateWindowW(L"ps5_pad_identity_probe", L"pad identity probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           0, 0, 640, 360, NULL, NULL, class.hInstance, NULL);
    if (!window || !caps_ex) finish(window ? "FAIL no XInputGetCapabilitiesEx" : "FAIL no window");
    say("PS5_PROTON_PAD_IDENTITY watching\r\n");
    for (;;) {
        char now[64], line[128];
        unsigned product, n = 0;
        int hid;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        identity(now, sizeof(now), &product, &hid);
        if (lstrcmpA(now, last)) {
            lstrcpyA(last, now);
            append(line, &n, sizeof(line), "PS5_PROTON_PAD_IDENTITY seen ");
            append(line, &n, sizeof(line), now);
            append(line, &n, sizeof(line), " at ");
            append_number(line, &n, sizeof(line), (GetTickCount() - start) / 1000);
            append(line, &n, sizeof(line), " s\r\n");
            say(line);
            if (seen_length) append(seen, &seen_length, sizeof(seen), ",");
            append(seen, &seen_length, sizeof(seen), now);
            if (stage == 0 && product == 0x028e) stage = 1;
            else if (stage == 1 && product == 0x02ea) stage = 2;
            else if (stage == 2 && !product && hid) finish("PASS");
        }
        if (GetTickCount() - start > 90000) finish("FAIL");
        Sleep(250);
    }
}
