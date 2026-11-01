/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A game that ships the Agility SDK: it exports D3D12SDKVersion and D3D12SDKPath as Microsoft's d3d12.dll reads them,
 * and ships a D3D12Core.dll (a dummy here) in D3D12\x64 and, in the second case, beside the exe. Which d3d12 and
 * d3d12core does it get? It must be VKD3D-Proton's from system32 in both cases, and the dummy must be refused. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
int _fltused;
__declspec(dllexport) const UINT D3D12SDKVersion = 616;
__declspec(dllexport) const char *D3D12SDKPath = ".\\D3D12\\";

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
}

void mainCRTStartup(void)
{
    char line[512], path[MAX_PATH];
    HMODULE d3d12 = LoadLibraryA("d3d12.dll"), core;
    PFN_D3D12_GET_INTERFACE get_interface;
    HRESULT (WINAPI *experimental)(UINT, const IID *, void *, UINT *);
    ID3D12SDKConfiguration *config = NULL;
    HRESULT hr;
    if (!d3d12) { say("AGILITY FAIL: no d3d12.dll\n"); ExitProcess(0); }
    GetModuleFileNameA(d3d12, path, MAX_PATH);
    wsprintfA(line, "AGILITY d3d12=%s\n", path); say(line);
    get_interface = (PFN_D3D12_GET_INTERFACE)GetProcAddress(d3d12, "D3D12GetInterface");
    hr = get_interface ? get_interface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration, (void **)&config) : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12SDKConfiguration_SetSDKVersion(config, D3D12SDKVersion, D3D12SDKPath);
    wsprintfA(line, "AGILITY SetSDKVersion hr=%08lx\n", hr); say(line);
    /* The core loads on the first call that needs it; this one makes no device and touches no GPU. */
    experimental = (void *)GetProcAddress(d3d12, "D3D12EnableExperimentalFeatures");
    hr = experimental ? experimental(0, NULL, NULL, NULL) : E_FAIL;
    wsprintfA(line, "AGILITY EnableExperimentalFeatures hr=%08lx\n", hr); say(line);
    core = GetModuleHandleA("d3d12core.dll");
    if (core) GetModuleFileNameA(core, path, MAX_PATH); else lstrcpyA(path, "(none)");
    wsprintfA(line, "AGILITY d3d12core=%s\n", path); say(line);
    ExitProcess(0);
}
