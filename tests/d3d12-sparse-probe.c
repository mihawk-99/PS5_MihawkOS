/* SPDX-License-Identifier: GPL-3.0-or-later */
/* How many Direct3D 12 reserved resources a game may hold: CreateReservedResource of 1 MiB buffers, each one sparse
 * range from the GPU broker's owner, until COUNT or the first refusal, then all released. A big racing game streaming
 * its open world was refused its 4097th range and wrote through the null it got back (a tester's standard PS5,
 * 2026-11-01); graphics patch 0044 bounds the ranges by the owner's page tables instead of a count of 4096.
 * PASS when all COUNT are made (more than 4096); the lines before the verdict are the measurement either way. Exits 0. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;
#define COUNT 6000

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static ID3D12Resource *reserved[COUNT];

void mainCRTStartup(void)
{
    char line[256];
    ID3D12Device *device = NULL;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_RESOURCE_DESC desc = {0};
    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    unsigned made = 0;
    DWORD started;

    wsprintfA(line, "PS5_PROTON_D3D12_SPARSE device hr=%08lx\n", hr);
    say(line);
    if (FAILED(hr)) {
        say("PS5_PROTON_D3D12_SPARSE FAIL: no Direct3D 12 device\n");
        ExitProcess(0);
    }
    if (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        options.TiledResourcesTier = D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
    wsprintfA(line, "PS5_PROTON_D3D12_SPARSE tiled_tier=%d\n", (int)options.TiledResourcesTier);
    say(line);
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 1 << 20;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    started = GetTickCount();
    hr = S_OK;
    while (made < COUNT) {
        hr = ID3D12Device_CreateReservedResource(device, &desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                 (void **)&reserved[made]);
        if (FAILED(hr)) break;
        if (++made % 1000 == 0) {
            wsprintfA(line, "PS5_PROTON_D3D12_SPARSE reserved=%u ms=%lu\n", made, GetTickCount() - started);
            say(line);
        }
    }
    wsprintfA(line, "PS5_PROTON_D3D12_SPARSE reserved=%u of %u last_hr=%08lx ms=%lu\n", made, COUNT, hr,
              GetTickCount() - started);
    say(line);
    for (unsigned i = 0; i < made; i++) ID3D12Resource_Release(reserved[i]);
    ID3D12Device_Release(device);
    if (made == COUNT) say("PS5_PROTON_D3D12_SPARSE PASS\n");
    else {
        wsprintfA(line, "PS5_PROTON_D3D12_SPARSE FAIL: refused after %u reserved resources\n", made);
        say(line);
    }
    ExitProcess(0);
}
