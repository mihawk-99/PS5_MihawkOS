/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A game whose DLL is missing (a tester's store demo with its upscaler's loader DLL renamed, 2026-11-01): it imports
 * Direct3D 11, so the title takes it, and pw-missing-import.dll, which no folder has, so Wine's loader fails before any
 * of its code runs (loader_init, STATUS_DLL_NOT_FOUND, c0000135). That ended MihawkOS instead of the session. This code
 * never runs; tests/process-probe.c --missing-import starts it and reads its exit code. Import library:
 * tools/build-wine-probe.sh, from a one-line .def. */
#include <windows.h>
#include <d3d11.h>

__declspec(dllimport) int pw_missing_import(void);

void mainCRTStartup(void)
{
    if (pw_missing_import())
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_NULL, NULL, 0, NULL, 0, D3D11_SDK_VERSION, NULL, NULL, NULL);
    ExitProcess(0);
}
