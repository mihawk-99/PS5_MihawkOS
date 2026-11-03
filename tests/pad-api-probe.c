/* SPDX-License-Identifier: GPL-3.0-or-later
 * What a game finds of the controller through each API, and what each reads with no one touching it (Wine patch 2480).
 * Players' reports said games saw no controller, only the keyboard and mouse (DirectInput, raw input and SDL games while
 * an Xbox controller was chosen), and that cameras turned by themselves (a resting stick or trigger read off centre).
 * Every 250 ms for up to 15 s it looks as games do:
 * - XInput (xinput1_3, as most games load it): controller 0 connected, its product (XInputGetCapabilitiesEx);
 * - DirectInput 8: the attached game controllers, the first one's name and vendor:product, read with c_dfDIJoystick2;
 * - raw input: HID game pads and joysticks in GetRawInputDeviceList, and whether one is an XInput one ("IG_" in its name);
 * - winmm: the first joystick joyGetPosEx reads.
 * Each new sight is printed. It ends once every API it expects has the controller (XInput only for an Xbox controller,
 * WINE_PS5_CONTROLLER), then prints what each reads at rest: PS5_PROTON_PAD_API PASS when all are centred and released,
 * FAIL with what is missing or off centre (more than XInput's own dead zones, 7849 and 30 of 255; 10% for the others).
 * Then, polling every millisecond for up to 30 s from its start, it times a press (tests/pad-scripts/pad-api.txt presses
 * CROSS) through each API that has the controller, from the first that shows it: an API that has not shown it 1 s later
 * fails. A probe in the title gets no press (the script counts from a frame it never sees) and says so. */
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <xinput.h>
#include <dinput.h>
#include <mmsystem.h>

typedef DWORD (WINAPI *get_state_fn)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *caps_ex_fn)(DWORD, DWORD, DWORD, XINPUT_CAPABILITIES_EX *);

static get_state_fn get_state;
static caps_ex_fn caps_ex;
static IDirectInput8W *dinput;
static IDirectInputDevice8W *joystick;
static WCHAR joystick_name[MAX_PATH];
static DWORD joystick_vidpid;
static unsigned joysticks, device_changes;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static BOOL CALLBACK found_joystick(const DIDEVICEINSTANCEW *instance, void *context)
{
    (void)context;
    if (joysticks++ || joystick) return DIENUM_CONTINUE;
    if (FAILED(IDirectInput8_CreateDevice(dinput, &instance->guidInstance, &joystick, NULL))) return DIENUM_CONTINUE;
    lstrcpynW(joystick_name, instance->tszProductName, MAX_PATH);
    joystick_vidpid = instance->guidProduct.Data1;   /* MAKELONG(vendor, product) */
    if (FAILED(IDirectInputDevice8_SetDataFormat(joystick, &c_dfDIJoystick2)) ||
        FAILED(IDirectInputDevice8_SetCooperativeLevel(joystick, NULL, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        IDirectInputDevice8_Release(joystick);
        joystick = NULL;
        return DIENUM_CONTINUE;
    }
    {
        DIPROPRANGE range = {{sizeof(range), sizeof(range.diph), 0, DIPH_DEVICE}, -1000, 1000};
        IDirectInputDevice8_SetProperty(joystick, DIPROP_RANGE, &range.diph);
    }
    IDirectInputDevice8_Acquire(joystick);
    return DIENUM_CONTINUE;
}

/* HID game pads and joysticks in the raw input list; *xinput: one of them is an XInput controller's ("IG_") */
static unsigned raw_pads(int *xinput)
{
    RAWINPUTDEVICELIST list[32];
    UINT count = sizeof(list) / sizeof(list[0]), i, pads = 0;

    *xinput = 0;
    if (GetRawInputDeviceList(list, &count, sizeof(list[0])) == (UINT)-1) return 0;
    for (i = 0; i < count; i++) {
        RID_DEVICE_INFO info;
        WCHAR name[256];
        UINT size = sizeof(info), length = sizeof(name) / sizeof(name[0]), c;
        info.cbSize = sizeof(info);
        if (list[i].dwType != RIM_TYPEHID ||
            GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1 ||
            info.hid.usUsagePage != 1 || (info.hid.usUsage != 4 && info.hid.usUsage != 5)) continue;
        pads++;
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, name, &length) == (UINT)-1) continue;
        for (c = 0; name[c] && name[c + 1] && name[c + 2]; c++)
            if ((name[c] == 'I' || name[c] == 'i') && (name[c + 1] == 'G' || name[c + 1] == 'g') && name[c + 2] == '_') *xinput = 1;
    }
    return pads;
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DEVICECHANGE) device_changes++;
    return DefWindowProcW(window, message, wparam, lparam);
}

static int away(LONG value, LONG limit) { return value > limit || value < -limit; }

void mainCRTStartup(void)
{
    WNDCLASSW class = {0};
    HMODULE xinput = LoadLibraryW(L"xinput1_3.dll");
    char kind[32] = "", line[600], last[300] = "", missing[160] = "", off[200] = "";
    const DWORD start = GetTickCount();
    int expect_xinput, done = 0;
    XINPUT_STATE xstate = {0};
    DIJOYSTATE2 dstate = {0};
    JOYINFOEX jstate = {sizeof(jstate), JOY_RETURNALL};
    UINT joy = (UINT)-1, u;
    unsigned product = 0, raw = 0;
    int xconnected = 0, raw_xinput = 0;
    MSG message;

    GetEnvironmentVariableA("WINE_PS5_CONTROLLER", kind, sizeof(kind));
    expect_xinput = lstrcmpA(kind, "dualsense") != 0;
    get_state = xinput ? (get_state_fn)(void *)GetProcAddress(xinput, "XInputGetState") : NULL;
    caps_ex = xinput ? (caps_ex_fn)(void *)GetProcAddress(xinput, (LPCSTR)108) : NULL;
    class.lpfnWndProc = proc;
    class.hInstance = GetModuleHandleW(NULL);
    class.lpszClassName = L"ps5_pad_api_probe";
    RegisterClassW(&class);
    CreateWindowW(class.lpszClassName, L"pad API probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 360,
                  NULL, NULL, class.hInstance, NULL);
    if (FAILED(DirectInput8Create(class.hInstance, DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&dinput, NULL)))
        dinput = NULL;
    wsprintfA(line, "PS5_PROTON_PAD_API watching (%s, %d-bit)\r\n", kind[0] ? kind : "no WINE_PS5_CONTROLLER",
              (int)sizeof(void *) * 8);
    say(line);
    while (!done && GetTickCount() - start < 15000) {
        char now[300];
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        xconnected = get_state && get_state(0, &xstate) == ERROR_SUCCESS;
        product = 0;
        if (xconnected && caps_ex) {
            XINPUT_CAPABILITIES_EX caps;
            for (u = 0; u < sizeof(caps); u++) ((volatile BYTE *)&caps)[u] = 0;
            if (caps_ex(1, 0, 0, &caps) == ERROR_SUCCESS) product = caps.ProductId;
        }
        if (dinput && !joystick) {
            joysticks = 0;
            IDirectInput8_EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, found_joystick, NULL, DIEDFL_ATTACHEDONLY);
        }
        if (joystick) IDirectInputDevice8_Poll(joystick);
        if (joystick && FAILED(IDirectInputDevice8_GetDeviceState(joystick, sizeof(dstate), &dstate))) {
            IDirectInputDevice8_Release(joystick);
            joystick = NULL;
        }
        raw = raw_pads(&raw_xinput);
        if (joy == (UINT)-1)
            for (u = 0; u < 16 && joy == (UINT)-1; u++)
                if (joyGetPosEx(u, &jstate) == JOYERR_NOERROR) joy = u;
        if (joy != (UINT)-1 && joyGetPosEx(joy, &jstate) != JOYERR_NOERROR) joy = (UINT)-1;
        {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, joystick_name, -1, name, sizeof(name), NULL, NULL);
            wsprintfA(now, "xinput=%d(%04x) dinput=%u '%s' %04x:%04x raw=%u ig=%d winmm=%d", xconnected, product,
                      joystick ? joysticks : 0, joystick ? name : "", LOWORD(joystick_vidpid), HIWORD(joystick_vidpid),
                      raw, raw_xinput, joy != (UINT)-1);
        }
        if (lstrcmpA(now, last)) {
            lstrcpyA(last, now);
            wsprintfA(line, "PS5_PROTON_PAD_API seen %s at %u ms\r\n", now, (unsigned)(GetTickCount() - start));
            say(line);
        }
        done = (xconnected || !expect_xinput) && joystick && raw && joy != (UINT)-1;
        if (!done) Sleep(250);
    }
    if (expect_xinput && !xconnected) lstrcatA(missing, " xinput");
    if (!expect_xinput && xconnected) lstrcatA(missing, " xinput-not-hidden");
    if (!joystick) lstrcatA(missing, " dinput");
    if (!raw) lstrcatA(missing, " rawinput");
    if (joy == (UINT)-1) lstrcatA(missing, " winmm");
    if (xconnected && (away(xstate.Gamepad.sThumbLX, 7849) || away(xstate.Gamepad.sThumbLY, 7849) ||
                       away(xstate.Gamepad.sThumbRX, 8689) || away(xstate.Gamepad.sThumbRY, 8689) ||
                       xstate.Gamepad.bLeftTrigger > 30 || xstate.Gamepad.bRightTrigger > 30 || xstate.Gamepad.wButtons))
        lstrcatA(off, " xinput");
    if (joystick) {
        int pressed = 0;
        for (u = 0; u < 128; u++) pressed |= dstate.rgbButtons[u] & 0x80;
        /* the Xbox gamepad's Z is both triggers, centred at rest; the DualSense's Rx and Ry are its triggers, 0 at rest */
        if (away(dstate.lX, 100) || away(dstate.lY, 100) || away(dstate.lZ, 100) || away(dstate.lRz, 100) ||
            (LOWORD(joystick_vidpid) == 0x045e && (away(dstate.lRx, 100) || away(dstate.lRy, 100))) ||
            LOWORD(dstate.rgdwPOV[0]) != 0xffff || pressed)
            lstrcatA(off, " dinput");
    }
    if (joy != (UINT)-1 && (away((LONG)jstate.dwXpos - 32767, 6554) || away((LONG)jstate.dwYpos - 32767, 6554) ||
                            jstate.dwButtons || jstate.dwPOV != JOY_POVCENTERED))
        lstrcatA(off, " winmm");
    wsprintfA(line, "PS5_PROTON_PAD_API rest xinput lx=%d ly=%d rx=%d ry=%d lt=%u rt=%u buttons=%04x | dinput x=%d y=%d z=%d "
              "rx=%d ry=%d rz=%d pov=%d | winmm x=%u y=%u z=%u r=%u u=%u v=%u buttons=%x pov=%u\r\n",
              xstate.Gamepad.sThumbLX, xstate.Gamepad.sThumbLY, xstate.Gamepad.sThumbRX, xstate.Gamepad.sThumbRY,
              xstate.Gamepad.bLeftTrigger, xstate.Gamepad.bRightTrigger, xstate.Gamepad.wButtons,
              (int)dstate.lX, (int)dstate.lY, (int)dstate.lZ, (int)dstate.lRx, (int)dstate.lRy, (int)dstate.lRz,
              (int)dstate.rgdwPOV[0], (unsigned)jstate.dwXpos, (unsigned)jstate.dwYpos, (unsigned)jstate.dwZpos,
              (unsigned)jstate.dwRpos, (unsigned)jstate.dwUpos, (unsigned)jstate.dwVpos, (unsigned)jstate.dwButtons,
              (unsigned)jstate.dwPOV);
    say(line);
    {
        /* the press: milliseconds from the start until each API first shows a button down (0: not yet) */
        DWORD at[3] = {0}, first = 0;
        const char *names[3] = {"xinput", "dinput", "winmm"};
        while (GetTickCount() - start < (first ? first + 1000 : 30000) && (!at[0] || !at[1] || !at[2])) {
            int down[3] = {0}, i;
            DIJOYSTATE2 now = {0};
            JOYINFOEX joynow = {sizeof(joynow), JOY_RETURNALL};
            if (xconnected && get_state(0, &xstate) == ERROR_SUCCESS) down[0] = xstate.Gamepad.wButtons != 0;
            if (joystick) IDirectInputDevice8_Poll(joystick);
            if (joystick && SUCCEEDED(IDirectInputDevice8_GetDeviceState(joystick, sizeof(now), &now)))
                for (u = 0; u < 128; u++) down[1] |= (now.rgbButtons[u] & 0x80) != 0;
            if (joy != (UINT)-1 && joyGetPosEx(joy, &joynow) == JOYERR_NOERROR) down[2] = joynow.dwButtons != 0;
            for (i = 0; i < 3; i++)
                if (down[i] && !at[i]) at[i] = GetTickCount() - start, first = first ? first : at[i];
            if (!xconnected) at[0] = at[0] ? at[0] : 1;   /* not expected: the DualSense has no XInput controller */
            Sleep(1);
        }
        if (first) {
            wsprintfA(line, "PS5_PROTON_PAD_API press xinput=%d dinput=%d winmm=%d ms after the first sight at %u ms\r\n",
                      xconnected && at[0] ? (int)(at[0] - first) : -1, at[1] ? (int)(at[1] - first) : -1,
                      at[2] ? (int)(at[2] - first) : -1, (unsigned)first);
            say(line);
            for (u = 0; u < 3; u++)
                if (!at[u] && (u != 1 || joystick) && (u != 2 || joy != (UINT)-1)) {
                    lstrcatA(missing, " press-");
                    lstrcatA(missing, names[u]);
                }
        } else say("PS5_PROTON_PAD_API press: none seen (no pad script)\r\n");
    }
    wsprintfA(line, "PS5_PROTON_PAD_API %s missing=%s off_centre=%s device_changes=%u seen=%s\r\n",
              missing[0] || off[0] ? "FAIL" : "PASS", missing[0] ? missing + 1 : "none", off[0] ? off + 1 : "none",
              device_changes, last);
    say(line);
    ExitProcess(0);
}
