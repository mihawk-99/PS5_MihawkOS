/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A texture shared between a Direct3D 9Ex device and a Direct3D 11 device of one process, where the GPU's Vulkan has no
 * Win32 external memory (every game's, on the console: the GPU broker's client). Koei Tecmo's movie player decodes into a
 * shared D3D9Ex texture that the game's D3D11 device opens (Samurai Warriors 4 DX, Warriors Orochi 3 Ultimate: the movies
 * never show). Measured on the Pro before the change: every step passed up to OpenSharedResource, which answered 80070057
 * (NOSHARE). DXVK patch 0007 (platform/dxvk/patches) shares the image inside the process when a Direct3D 9Ex device is
 * made while a Direct3D 11 device lives, so with the default creation order this should say PASS.
 *
 * The D3D9Ex device makes a shared render-target texture, clears it to a colour and waits for the GPU; the D3D11 device
 * opens the handle with OpenSharedResource, copies it to a staging texture and reads a pixel. One line per step with its
 * HRESULT, then one verdict:
 *   PASS      the colour arrived: the two devices share the image.
 *   NOSHARE   a step refused honestly (no shared handle from D3D9, or OpenSharedResource refused): what a game sees when
 *             the two devices do not share (DXVK's d3d9.shareD3D11Device = False, or the other creation order).
 *   FAIL      something else went wrong (no device, a wrong colour, a GPU timeout).
 * Exits 0 in every case; the gate reads the line. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;

#define MARKER "PS5_PROTON_D3D9_D3D11_SHARE"
#define SIZE 64
#define COLOUR 0xff336699u

static char line[256];

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

/* A case-blind search of a command line for a word; the standalone entry has no C library to ask. */
static BOOL strstr_ci(const char *text, const char *word)
{
    for (; *text; text++) {
        const char *a = text, *b = word;
        while (*a && *b && (*a | 0x20) == (*b | 0x20)) { a++; b++; }
        if (!*b) return TRUE;
    }
    return FALSE;
}

static void step(const char *name, HRESULT hr)
{
    wsprintfA(line, MARKER " %s hr=%08lx\n", name, hr);
    say(line);
}

void mainCRTStartup(void)
{
    D3DPRESENT_PARAMETERS pp = {0};
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    IDirect3D9Ex *d3d9 = NULL;
    IDirect3DDevice9Ex *device9 = NULL;
    IDirect3DTexture9 *texture9 = NULL;
    IDirect3DSurface9 *surface9 = NULL;
    IDirect3DQuery9 *query = NULL;
    ID3D11Device *device11 = NULL;
    ID3D11DeviceContext *context11 = NULL;
    ID3D11Texture2D *opened = NULL, *staging = NULL;
    HANDLE shared = NULL;
    HWND window;
    HRESULT hr;
    DWORD start;
    BOOL d3d9_first;
    const char *verdict = NULL, *why = NULL;

    window = CreateWindowExA(0, "STATIC", "share", WS_POPUP, 0, 0, SIZE, SIZE, NULL, NULL, GetModuleHandleA(NULL), NULL);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = SIZE;
    pp.BackBufferHeight = SIZE;
    pp.hDeviceWindow = window;

    /* The games made the Direct3D 11 device first (their Direct3D 9Ex one is for movies), and so does this by default;
     * "d3d9-first" on the command line makes the other order, which sharing inside the process does not cover. */
    d3d9_first = strstr_ci(GetCommandLineA(), "d3d9-first");
    wsprintfA(line, MARKER " order=%s\n", d3d9_first ? "d3d9ex-first" : "d3d11-first");
    say(line);

    if (!d3d9_first) {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device11, NULL, &context11);
        step("d3d11-device", hr);
        if (FAILED(hr)) { why = "no Direct3D 11 device"; goto end; }
    }

    if (FAILED(hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9))) { step("d3d9ex", hr); why = "no Direct3D 9Ex"; goto end; }
    hr = IDirect3D9Ex_CreateDeviceEx(d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, NULL, &device9);
    step("d3d9ex-device", hr);
    if (FAILED(hr)) { why = "no Direct3D 9Ex device"; goto end; }

    if (d3d9_first) {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device11, NULL, &context11);
        step("d3d11-device", hr);
        if (FAILED(hr)) { why = "no Direct3D 11 device"; goto end; }
    }

    /* The shared handle is asked for by passing a pointer to a NULL handle. */
    hr = IDirect3DDevice9Ex_CreateTexture(device9, SIZE, SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                          &texture9, &shared);
    step("d3d9-shared-texture", hr);
    if (FAILED(hr)) { verdict = "NOSHARE"; why = "Direct3D 9 refused a shared texture"; goto end; }
    wsprintfA(line, MARKER " d3d9-shared-handle %s\n", shared ? "yes" : "none");
    say(line);
    if (!shared) { verdict = "NOSHARE"; why = "Direct3D 9 made the texture without a shared handle"; goto end; }

    hr = IDirect3DTexture9_GetSurfaceLevel(texture9, 0, &surface9);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9Ex_SetRenderTarget(device9, 0, surface9);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9Ex_Clear(device9, 0, NULL, D3DCLEAR_TARGET, COLOUR, 1.0f, 0);
    step("d3d9-clear", hr);
    if (FAILED(hr)) { why = "the clear failed"; goto end; }
    hr = IDirect3DDevice9Ex_CreateQuery(device9, D3DQUERYTYPE_EVENT, &query);
    if (SUCCEEDED(hr)) hr = IDirect3DQuery9_Issue(query, D3DISSUE_END);
    start = GetTickCount();
    while (SUCCEEDED(hr) && (hr = IDirect3DQuery9_GetData(query, NULL, 0, D3DGETDATA_FLUSH)) == S_FALSE) {
        if (GetTickCount() - start > 5000) { hr = E_FAIL; break; }
        Sleep(1);
    }
    step("d3d9-gpu-done", hr);
    if (FAILED(hr)) { why = "the GPU did not finish the clear"; goto end; }

    hr = ID3D11Device_OpenSharedResource(device11, shared, &IID_ID3D11Texture2D, (void **)&opened);
    step("d3d11-open-shared", hr);
    if (FAILED(hr)) { verdict = "NOSHARE"; why = "Direct3D 11 could not open the shared handle"; goto end; }

    ID3D11Texture2D_GetDesc(opened, &desc);
    wsprintfA(line, MARKER " d3d11-desc %lux%lu format=%u\n", desc.Width, desc.Height, (unsigned)desc.Format);
    say(line);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device11, &desc, NULL, &staging);
    step("d3d11-staging", hr);
    if (FAILED(hr)) { why = "no staging texture"; goto end; }
    ID3D11DeviceContext_CopyResource(context11, (ID3D11Resource *)staging, (ID3D11Resource *)opened);
    hr = ID3D11DeviceContext_Map(context11, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    step("d3d11-map", hr);
    if (FAILED(hr)) { why = "the copy could not be read"; goto end; }
    {
        DWORD pixel = *(volatile DWORD *)mapped.pData;
        wsprintfA(line, MARKER " pixel=%08lx wanted=%08lx\n", pixel, (DWORD)COLOUR);
        say(line);
        ID3D11DeviceContext_Unmap(context11, (ID3D11Resource *)staging, 0);
        if (pixel != COLOUR) { why = "the shared texture holds another colour"; goto end; }
    }
    verdict = "PASS";

end:
    if (!verdict) verdict = "FAIL";
    wsprintfA(line, MARKER " %s%s%s\n", verdict, why ? ": " : "", why ? why : "");
    say(line);
    if (staging) ID3D11Texture2D_Release(staging);
    if (opened) ID3D11Texture2D_Release(opened);
    if (context11) ID3D11DeviceContext_Release(context11);
    if (device11) ID3D11Device_Release(device11);
    if (query) IDirect3DQuery9_Release(query);
    if (surface9) IDirect3DSurface9_Release(surface9);
    if (texture9) IDirect3DTexture9_Release(texture9);
    if (device9) IDirect3DDevice9Ex_Release(device9);
    if (d3d9) IDirect3D9Ex_Release(d3d9);
    if (window) DestroyWindow(window);
    ExitProcess(0);
}
