/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <xinput.h>

void mainCRTStartup(void)
{
    HMODULE module = LoadLibraryW(L"xinput1_4.dll");
    DWORD (WINAPI *get_state)(DWORD, XINPUT_STATE *) = module ?
        (void *)GetProcAddress(module, "XInputGetState") : NULL;
    DWORD (WINAPI *set_state)(DWORD, XINPUT_VIBRATION *) = module ?
        (void *)GetProcAddress(module, "XInputSetState") : NULL;
    XINPUT_STATE state;
    XINPUT_VIBRATION vib_on, vib_off;
    vib_on.wLeftMotorSpeed = 16000;
    vib_on.wRightMotorSpeed = 8000;
    vib_off.wLeftMotorSpeed = 0;
    vib_off.wRightMotorSpeed = 0;
    BOOL ok = get_state && set_state &&
        get_state(0, &state) == ERROR_SUCCESS &&
        get_state(1, &state) == ERROR_DEVICE_NOT_CONNECTED &&
        get_state(2, &state) == ERROR_DEVICE_NOT_CONNECTED &&
        get_state(3, &state) == ERROR_DEVICE_NOT_CONNECTED &&
        get_state(4, &state) == ERROR_BAD_ARGUMENTS &&
        set_state(0, &vib_on) == ERROR_SUCCESS &&
        set_state(4, &vib_off) == ERROR_BAD_ARGUMENTS;
    if (ok) {
        Sleep(40);
        ok = set_state(0, &vib_off) == ERROR_SUCCESS;
        Sleep(20);
    }
    const char *message = ok ? "PS5_PROTON_XINPUT_PROBE PASS: controller 0 connected, controller 1 absent\r\n"
                             : "PS5_PROTON_XINPUT_PROBE FAIL: controller bridge unavailable\r\n";
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, lstrlenA(message), &written, NULL);
    OutputDebugStringA(message);
    if (module) FreeLibrary(module);
    ExitProcess(ok ? 0 : 1);
}
