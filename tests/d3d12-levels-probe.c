/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which Direct3D 12 feature levels a game gets on the console, and why: D3D12CreateDevice at 11_0 to 12_2, with no
 * adapter and with DXGI's first, then the 11_0 device's MaxSupportedFeatureLevel and the OPTIONS the levels rest on
 * (platform/vkd3d-proton/patches/0001). A tester's DX12 game asked for 12_0 and got E_INVALIDARG (2026-10-31).
 * PASS when every answer agrees with MaxSupportedFeatureLevel, both ways, and that level is at least 12_0; the lines
 * before the verdict are the measurement either way. Exits 0. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;
#define LEVEL_COUNT (sizeof(levels) / sizeof(levels[0]))

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                                           D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2};

static const char *level_name(D3D_FEATURE_LEVEL level)
{
    switch (level) {
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    default: return "?";
    }
}

void mainCRTStartup(void)
{
    char line[256];
    HRESULT created[2][LEVEL_COUNT];
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    ID3D12Device *device = NULL;
    D3D12_FEATURE_DATA_FEATURE_LEVELS query = {LEVEL_COUNT, levels, 0};
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    unsigned mismatches = 0;
    const char *why = NULL;

    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory)) ||
            FAILED(IDXGIFactory1_EnumAdapters(factory, 0, &adapter)))
        adapter = NULL;
    for (unsigned way = 0; way < 2; way++) {
        for (unsigned i = 0; i < LEVEL_COUNT; i++) {
            ID3D12Device *made = NULL;
            created[way][i] = way && !adapter ? E_FAIL :
                D3D12CreateDevice(way ? (IUnknown *)adapter : NULL, levels[i], &IID_ID3D12Device, (void **)&made);
            wsprintfA(line, "PS5_PROTON_D3D12_LEVELS create adapter=%s level=%s hr=%08lx\n", way ? "dxgi0" : "none",
                      level_name(levels[i]), created[way][i]);
            say(line);
            if (made) ID3D12Device_Release(made);
        }
    }
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device))) {
        why = "no 11_0 device";
    } else if (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS, &query, sizeof(query))) ||
               FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
        why = "CheckFeatureSupport failed";
    } else {
        wsprintfA(line, "PS5_PROTON_D3D12_LEVELS caps max=%s tiled_tier=%d binding_tier=%d typed_uav_loads=%d rovs=%d "
                  "conservative_tier=%d logic_op=%d\n", level_name(query.MaxSupportedFeatureLevel),
                  options.TiledResourcesTier, options.ResourceBindingTier, options.TypedUAVLoadAdditionalFormats,
                  options.ROVsSupported, options.ConservativeRasterizationTier, options.OutputMergerLogicOp);
        say(line);
        /* A level at or below the maximum makes a device, one above it is refused with E_INVALIDARG. */
        for (unsigned way = 0; way < 2; way++)
            for (unsigned i = 0; i < LEVEL_COUNT; i++) {
                if (way && !adapter) continue;
                const HRESULT wanted = levels[i] <= query.MaxSupportedFeatureLevel ? S_OK : E_INVALIDARG;
                if (created[way][i] != wanted) mismatches++;
            }
        if (mismatches) why = "a device answer differs from MaxSupportedFeatureLevel";
        else if (!adapter) why = "DXGI listed no adapter";
        else if (query.MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_12_0) why = "below 12_0";
        ID3D12Device_Release(device);
    }
    if (why) wsprintfA(line, "PS5_PROTON_D3D12_LEVELS FAIL: %s\n", why);
    else lstrcpyA(line, "PS5_PROTON_D3D12_LEVELS PASS\n");
    say(line);
    if (adapter) IDXGIAdapter_Release(adapter);
    if (factory) IDXGIFactory1_Release(factory);
    ExitProcess(0);
}
