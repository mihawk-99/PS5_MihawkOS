/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A stand-in for Microsoft's D3D12Core.dll: it says when it is loaded, and answers no CLSID, as the real one answers
 * none of VKD3D-Proton's. */
#include <windows.h>
int _fltused;
__declspec(dllexport) HRESULT WINAPI D3D12GetInterface(const GUID *clsid, const GUID *iid, void **out)
{
    (void)clsid; (void)iid;
    if (out) *out = NULL;
    return E_NOINTERFACE;
}
BOOL WINAPI DllMainCRTStartup(HINSTANCE instance, DWORD reason, void *reserved)
{
    char path[MAX_PATH], line[MAX_PATH + 64];
    DWORD written;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        GetModuleFileNameA(instance, path, MAX_PATH);
        wsprintfA(line, "AGILITY dummy D3D12Core.dll loaded from %s\n", path);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, lstrlenA(line), &written, NULL);
    }
    return TRUE;
}
