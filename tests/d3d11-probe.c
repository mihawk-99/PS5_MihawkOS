/* SPDX-License-Identifier: GPL-3.0-or-later */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;
#ifdef PS5_GRAPHICS_CHILD_PROBE
#include <winternl.h>
#include <xinput.h>
#include "shaders/d3d11-triangle.h"
#endif

static void check(HRESULT result, const char *step)
{
    char message[160];
    if (SUCCEEDED(result)) return;
    wsprintfA(message, "PS5_PROTON_D3D11_PROBE FAIL: %s %08lx\n", step, result);
    OutputDebugStringA(message);
    ExitProcess(1);
}

/* A decimal environment variable, or fallback. */
static unsigned env_number(const char *name, unsigned fallback)
{
    char text[16];
    unsigned value = 0;
    DWORD length = GetEnvironmentVariableA(name, text, sizeof(text));
    if (!length || length >= sizeof(text)) return fallback;
    for (DWORD i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') return fallback;
        value = value * 10 + (unsigned)(text[i] - '0');
    }
    return value ? value : fallback;
}

void mainCRTStartup(void)
{
#ifdef PS5_GRAPHICS_CHILD_PROBE
    typedef NTSTATUS (WINAPI *Query)(HANDLE,PROCESSINFOCLASS,void *,ULONG,ULONG *);
    Query query=(Query)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQueryInformationProcess");
    PROCESS_BASIC_INFORMATION basic={0};
    DWORD parent=env_number("PS5_GRAPHICS_PARENT",0),pid=GetCurrentProcessId(),written=0;
    HANDLE pipe=(HANDLE)(ULONG_PTR)env_number("PS5_GRAPHICS_PIPE",0);
    char cookie[32];
    if(!parent || parent==pid || !query || query(GetCurrentProcess(),ProcessBasicInformation,&basic,sizeof(basic),NULL) ||
       (ULONG_PTR)basic.Reserved3!=parent || GetEnvironmentVariableA("PS5_PROCESS_COOKIE",cookie,sizeof(cookie))!=12 ||
       lstrcmpA(cookie,"native-child") || !WriteFile(pipe,&pid,sizeof(pid),&written,NULL) || written!=sizeof(pid)) {
        OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD FAIL: parent, environment or inherited pipe\n");ExitProcess(1);
    }
    CloseHandle(pipe);
    char broker_value[8];const int broker=GetEnvironmentVariableA("PS5_GRAPHICS_BROKER",broker_value,sizeof(broker_value)) && broker_value[0]=='1';
    const unsigned depth=env_number("PS5_GRAPHICS_DEPTH",0);
    if(broker && !GetEnvironmentVariableW(L"PS5_PROTON_PROBE_CAPTURE", NULL, 0)) {
        WCHAR capture[160];wsprintfW(capture,L"Z:\\data\\ps5proton\\logs\\graphics-broker-%u.ppm",pid);
        SetEnvironmentVariableW(L"PS5_PROTON_PROBE_CAPTURE",capture);
    }
    OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD PASS: Windows parent and inherited pipe\n");
#endif
#ifndef PS5_GRAPHICS_CHILD_PROBE
    static const char shader[] =
        "struct V{float4 p:SV_Position;float3 c:COLOR;};"
        "V vs(uint id:SV_VertexID){V v;"
        "float2 p[3]={float2(0,.8),float2(.8,-.8),float2(-.8,-.8)};"
        "float3 c[3]={float3(1,0,0),float3(0,1,0),float3(0,0,1)};"
        "v.p=float4(p[id],0,1);v.c=c[id];return v;}"
        "float4 ps(V v):SV_Target{return float4(v.c,1);}";
#endif
    const float clear[4] = {.025f,.05f,.1f,1};
    const unsigned width=env_number("PS5_PROTON_PROBE_WIDTH",1280),height=env_number("PS5_PROTON_PROBE_HEIGHT",720);
    if(width>1920 || height>1080)ExitProcess(1);
    DWORD style=WS_OVERLAPPEDWINDOW | WS_VISIBLE;
#ifdef PS5_GRAPHICS_CHILD_PROBE
    if(broker)style=WS_POPUP | WS_VISIBLE;
#endif
    HWND window = CreateWindowW(L"STATIC", L"PS5 Proton D3D11 probe", style,
                                0, 0, width, height, NULL, NULL, NULL, NULL);
    DXGI_SWAP_CHAIN_DESC swap_desc = {0};
    IDXGISwapChain *swap;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11Texture2D *back, *staging;
    ID3D11RenderTargetView *target;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
#ifndef PS5_GRAPHICS_CHILD_PROBE
    ID3DBlob *code;
#endif
    D3D11_VIEWPORT viewport = {0,0,width,height,0,1};
    D3D11_TEXTURE2D_DESC texture;
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D_FEATURE_LEVEL level;
    MSG message;
    if (!window) { OutputDebugStringA("PS5_PROTON_D3D11_PROBE FAIL: window\n"); ExitProcess(1); }
    swap_desc.BufferDesc.Width = width;
    swap_desc.BufferDesc.Height = height;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;
    swap_desc.OutputWindow = window;
    swap_desc.Windowed = TRUE;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    check(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &swap_desc, &swap, &device, &level, &context), "device");
    check(IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&back), "backbuffer");
    check(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)back, NULL, &target), "target");
#ifdef PS5_GRAPHICS_CHILD_PROBE
    /* The process probe tests DXVK rendering, without loading Wine's HLSL
     * compiler, wined3d and OpenGL into every graphics descendant. */
    check(ID3D11Device_CreateVertexShader(device,d3d_probe_vs,sizeof(d3d_probe_vs),NULL,&vs),"vertex shader");
    check(ID3D11Device_CreatePixelShader(device,d3d_probe_ps,sizeof(d3d_probe_ps),NULL,&ps),"pixel shader");
#else
    check(D3DCompile(shader, sizeof(shader), NULL, NULL, NULL, "vs", "vs_5_0", 0, 0, &code, NULL), "compile VS");
    check(ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(code),
        ID3D10Blob_GetBufferSize(code), NULL, &vs), "vertex shader");
    ID3D10Blob_Release(code);
    check(D3DCompile(shader, sizeof(shader), NULL, NULL, NULL, "ps", "ps_5_0", 0, 0, &code, NULL), "compile PS");
    check(ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(code),
        ID3D10Blob_GetBufferSize(code), NULL, &ps), "pixel shader");
    ID3D10Blob_Release(code);
#endif
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &target, NULL);
    ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
    /* Knobs for presentation and submission measurements (one-shot env= in
     * wine-run.txt): frames, draws per frame, and the Present sync interval
     * (0 renders as fast as the GPU and the queue allow). The picture is the
     * same whatever they are: extra draws overdraw the same triangle. */
    const unsigned frames = env_number("PS5_PROTON_PROBE_FRAMES", 180);
    const unsigned draws = env_number("PS5_PROTON_PROBE_DRAWS", 1);
    const unsigned interval = env_number("PS5_PROTON_PROBE_VSYNC", 1);
    const unsigned fault_frame = env_number("PS5_PROTON_PROBE_FAULT_FRAME", 0);
    const unsigned hang_frame = env_number("PS5_PROTON_PROBE_HANG_FRAME", 0), hang_wait = env_number("PS5_PROTON_PROBE_HANG_WAIT", 0);
    LARGE_INTEGER frequency, started, ended;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
#ifdef PS5_GRAPHICS_CHILD_PROBE
    /* The title's pad, which reaches a child through the title's input page: read every frame. */
    DWORD pad_result = ERROR_DEVICE_NOT_CONNECTED, pad_packet = 0;
    WORD pad_buttons = 0;
    SHORT pad_lx = 0;
#endif
    for (unsigned frame = 0; frame < frames; ++frame) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
#ifdef PS5_GRAPHICS_CHILD_PROBE
        {
            XINPUT_STATE pad;
            const DWORD result = XInputGetState(0, &pad);
            if (result == ERROR_SUCCESS) {
                pad_result = result;pad_buttons |= pad.Gamepad.wButtons;pad_packet = pad.dwPacketNumber;
                if (pad.Gamepad.sThumbLX > pad_lx) pad_lx = pad.Gamepad.sThumbLX;
            } else if (pad_result != ERROR_SUCCESS) pad_result = result;
        }
#endif
        ID3D11DeviceContext_ClearRenderTargetView(context, target, clear);
        for (unsigned draw = 0; draw < draws; ++draw) ID3D11DeviceContext_Draw(context, 3, 0);
        if (frame == frames - 1) {
            ID3D11Texture2D_GetDesc(back, &texture);
            texture.Usage = D3D11_USAGE_STAGING;
            texture.BindFlags = texture.MiscFlags = 0;
            texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(ID3D11Device_CreateTexture2D(device, &texture, NULL, &staging), "staging texture");
            ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)back);
            check(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped), "readback");
            BYTE *pixels = HeapAlloc(GetProcessHeap(), 0, width*height*3);
            if (!pixels) ExitProcess(3);
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
                for (unsigned c = 0; c < 3; ++c) pixels[(y*width+x)*3+c] = ((BYTE *)mapped.pData)[y*mapped.RowPitch+x*4+c];
#ifdef PS5_GRAPHICS_CHILD_PROBE
            WCHAR capture[1024]=L"Z:\\data\\ps5proton\\logs\\graphics-child-probe.ppm";
#else
            WCHAR capture[1024]=L"Z:\\data\\ps5proton\\logs\\d3d-probe.ppm";
#endif
            GetEnvironmentVariableW(L"PS5_PROTON_PROBE_CAPTURE",capture,sizeof(capture)/sizeof(capture[0]));
            // Copies of this probe that run together (process-probe --title-games) write the same capture: one that
            // finds it open waits its turn (rc2b-pro-2026-10-31-title-games: the opted-out copy exited 2 16 ms after
            // another copy's write).
            HANDLE file = INVALID_HANDLE_VALUE;
            for (int tries = 0; tries < 40; ++tries) {
                file = CreateFileW(capture, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
                Sleep(50);
            }
            DWORD written;
            char header[64];const unsigned header_bytes=wsprintfA(header,"P6\n%u %u\n255\n",width,height);
            if (file == INVALID_HANDLE_VALUE || !WriteFile(file, header, header_bytes, &written, NULL) ||
                !WriteFile(file, pixels, width*height*3, &written, NULL) || written != width*height*3)
                ExitProcess(2);
            CloseHandle(file);
            HeapFree(GetProcessHeap(), 0, pixels);
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
            ID3D11Texture2D_Release(staging);
        }
        check(IDXGISwapChain_Present(swap, interval, 0), "present");
        /* PS5_PROTON_PROBE_FAULT_FRAME=N (a crash-safety probe, off by default): fault right after frame N's Present,
         * with submitted GPU work still in flight, to see whether a crash with live graphics returns to the launcher
         * or hangs the console (docs/TITLE_CLIENT.md, "Stop and end"). */
        if (frame + 1 == fault_frame) {
            OutputDebugStringA("PS5_PROTON_D3D11_PROBE fault: writing through a null pointer with frames in flight\n");
            *(volatile int *)0 = 0x1234;
        }
        /* PS5_PROTON_PROBE_HANG_FRAME=N (off by default): stop making progress right after frame N's Present, the title
         * watchdog's case (src/pw_hang_breaker.h): a busy loop, or with PS5_PROTON_PROBE_HANG_WAIT=1 a wait that never ends. */
        if (frame + 1 == hang_frame) {
            OutputDebugStringA(hang_wait ? "PS5_PROTON_D3D11_PROBE hang: waiting forever after a Present\n" :
                                           "PS5_PROTON_D3D11_PROBE hang: spinning forever after a Present\n");
            if (hang_wait) WaitForSingleObject(CreateEventA(NULL, TRUE, FALSE, NULL), INFINITE);
            for (volatile unsigned spin = 0;; ++spin) {}
        }
    }
    QueryPerformanceCounter(&ended);
    {
        char line[160];
        const unsigned long long us = (ended.QuadPart - started.QuadPart) * 1000000ULL / frequency.QuadPart;
        wsprintfA(line, "PS5_PROTON_D3D11_PROBE bench frames=%u draws=%u vsync=%u total_ms=%u frame_us=%u\n",
                  frames, draws, interval, (unsigned)(us / 1000), (unsigned)(us / frames));
        OutputDebugStringA(line);
    }
    OutputDebugStringA("PS5_PROTON_D3D11_PROBE PASS: frames and readback\n");
#ifdef PS5_GRAPHICS_CHILD_PROBE
    /* A small live device, after verified frames, for the parent's bounded
     * graceful-exit / TerminateProcess check. No descendant in this mode. */
    HANDLE teardown_ready = (HANDLE)(ULONG_PTR)env_number("PS5_TEARDOWN_READY", 0);
    if (teardown_ready) {
        HANDLE finish = (HANDLE)(ULONG_PTR)env_number("PS5_TEARDOWN_FINISH", 0);
        if (!finish || !SetEvent(teardown_ready)) ExitProcess(5);
        OutputDebugStringA("PS5_PROTON_GRAPHICS_TEARDOWN READY: rendered with live device\n");
        if (WaitForSingleObject(finish, 30000) != WAIT_OBJECT_0) ExitProcess(5);
        CloseHandle(teardown_ready); CloseHandle(finish);
    }
    if(broker && depth==0 && !teardown_ready) {
        SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};HANDLE read,write;
        if(!CreatePipe(&read,&write,&security,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0))ExitProcess(4);
        char value[32];wsprintfA(value,"%u",pid);SetEnvironmentVariableA("PS5_GRAPHICS_PARENT",value);
        wsprintfA(value,"%u",(unsigned)(ULONG_PTR)write);SetEnvironmentVariableA("PS5_GRAPHICS_PIPE",value);
        SetEnvironmentVariableA("PS5_GRAPHICS_DEPTH","1");
        STARTUPINFOW startup={0};PROCESS_INFORMATION child={0};startup.cb=sizeof(startup);
        WCHAR command[]=L"C:\\graphics-render-probe.exe";
        DWORD reported=0,bytes=0,exit_code=0;
        if(!CreateProcessW(NULL,command,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,&child))ExitProcess(4);
        CloseHandle(write);
        if(!ReadFile(read,&reported,sizeof(reported),&bytes,NULL) || bytes!=sizeof(reported) || reported!=child.dwProcessId || reported==pid ||
           WaitForSingleObject(child.hProcess,60000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(child.hProcess,&exit_code) || exit_code)ExitProcess(4);
        CloseHandle(read);CloseHandle(child.hThread);CloseHandle(child.hProcess);
        OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD PASS: rendering descendant during GPU lifetime\n");
    }
    if(!broker && !teardown_ready) {
    /* Keep the graphics device alive while this Windows child creates a
     * separate headless grandchild. The routing request must be consumed. */
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};
    HANDLE read,write,done=CreateEventA(&security,TRUE,FALSE,NULL);
    if(!done || !CreatePipe(&read,&write,&security,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)) ExitProcess(4);
    char command[512];WCHAR wide[512];
    wsprintfA(command,"C:\\process-probe.exe --descendant-node 0 %u %u %u %u 0 0 \"two words\"",
              (unsigned)(ULONG_PTR)write,(unsigned)(ULONG_PTR)done,pid,parent);
    STARTUPINFOW startup={0};PROCESS_INFORMATION child={0};startup.cb=sizeof(startup);
    struct {DWORD pid,parent,level,flags;} reply={0};DWORD bytes=0,exit_code=0;
    if(!MultiByteToWideChar(CP_UTF8,0,command,-1,wide,512) ||
       !CreateProcessW(NULL,wide,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,&child)) {
        OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD FAIL: headless descendant creation\n");ExitProcess(4);
    }
    CloseHandle(write);
    if(!ReadFile(read,&reply,sizeof(reply),&bytes,NULL) || bytes!=sizeof(reply) || reply.pid!=child.dwProcessId ||
       reply.parent!=pid || reply.level || reply.flags!=15 || WaitForSingleObject(done,10000)!=WAIT_OBJECT_0 ||
       WaitForSingleObject(child.hProcess,15000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(child.hProcess,&exit_code) || exit_code!=0x5100) {
        OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD FAIL: headless descendant lifecycle\n");ExitProcess(4);
    }
    CloseHandle(read);CloseHandle(done);CloseHandle(child.hThread);CloseHandle(child.hProcess);
    OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD PASS: headless descendant during GPU lifetime\n");
    }
#endif
    /* As most games do: exit with the device and swapchain still alive, so
     * the runtime's own end-of-process teardown is what idles the GPU. */
    if (env_number("PS5_PROTON_PROBE_EXIT_LIVE", 0)) ExitProcess(0);
    ID3D11DeviceContext_ClearState(context);
    ID3D11PixelShader_Release(ps);
    ID3D11VertexShader_Release(vs);
    ID3D11RenderTargetView_Release(target);
    ID3D11Texture2D_Release(back);
    IDXGISwapChain_Release(swap);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    DestroyWindow(window);
#ifdef PS5_GRAPHICS_CHILD_PROBE
    {
        char line[160];
        wsprintfA(line, "PS5_PROTON_GRAPHICS_CHILD input result=%lu buttons=%#x packet=%lu lx=%d\n", pad_result, pad_buttons,
                  pad_packet, (int)pad_lx);
        OutputDebugStringA(line);
    }
    OutputDebugStringA("PS5_PROTON_GRAPHICS_CHILD PASS: GPU initialized, frames, readback and explicit teardown\n");
#endif
    ExitProcess(0);
}
