/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Direct3D 12's shared resources where the GPU's Vulkan has no Win32 external memory (every game's, on the console:
 * the GPU broker's client). Unreal Engine 5 games make a shared texture and a shared fence at start; the export
 * failed the allocation in Wine or called a NULL function in VKD3D-Proton, and the game closed (Wine patch 2407,
 * platform/vkd3d-proton/patches/0003). A shared render target and a shared fence are made, cleared and signalled on
 * the GPU, then asked for shared handles: S_OK where the device can share, E_NOTIMPL where it cannot. PASS when both
 * are made, the GPU work completes, and each handle request answers one of those two. Exits 0. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;

static char line[256];

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static void step(const char *name, HRESULT hr)
{
    wsprintfA(line, "PS5_PROTON_D3D12_SHARED %s hr=%08lx\n", name, hr);
    say(line);
}

static BOOL handle_answer_ok(HRESULT hr) { return hr == S_OK || hr == E_NOTIMPL; }

void mainCRTStartup(void)
{
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
    D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 256, 256, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS};
    D3D12_COMMAND_QUEUE_DESC queue_desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    static const float colour[4] = {0.0f, 0.5f, 1.0f, 1.0f};
    ID3D12Device *device = NULL;
    ID3D12Resource *texture = NULL;
    ID3D12Fence *fence = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    HANDLE texture_handle = NULL, fence_handle = NULL, done = NULL;
    HRESULT texture_share = E_FAIL, fence_share = E_FAIL, hr;
    const char *why = NULL;

    if (FAILED(hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device))) {
        step("device", hr);
        why = "no device";
        goto end;
    }
    hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&texture);
    step("shared-texture", hr);
    if (FAILED(hr)) { why = "no shared texture"; goto end; }
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&fence);
    step("shared-fence", hr);
    if (FAILED(hr)) { why = "no shared fence"; goto end; }

    /* The shared texture is a real render target, and the shared fence a real fence, whatever the sharing. */
    if (FAILED(hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue)) ||
            FAILED(hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    &IID_ID3D12CommandAllocator, (void **)&allocator)) ||
            FAILED(hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                    &IID_ID3D12GraphicsCommandList, (void **)&list)) ||
            FAILED(hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap,
                    (void **)&rtv_heap))) {
        step("command-objects", hr);
        why = "no command objects";
        goto end;
    }
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);   /* a struct return: the vtable form */
        ID3D12Device_CreateRenderTargetView(device, texture, NULL, rtv);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, colour, 0, NULL);
    }
    ID3D12GraphicsCommandList_Close(list);
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList **)&list);
    done = CreateEventA(NULL, FALSE, FALSE, NULL);
    ID3D12CommandQueue_Signal(queue, fence, 1);
    ID3D12Fence_SetEventOnCompletion(fence, 1, done);
    hr = WaitForSingleObject(done, 5000) == WAIT_OBJECT_0 ? S_OK : E_FAIL;
    step("gpu-clear-and-signal", hr);
    if (FAILED(hr) || ID3D12Fence_GetCompletedValue(fence) != 1) { why = "the GPU work did not complete"; goto end; }

    texture_share = ID3D12Device_CreateSharedHandle(device, (ID3D12DeviceChild *)texture, NULL, GENERIC_ALL, NULL,
                                                    &texture_handle);
    step("texture-handle", texture_share);
    fence_share = ID3D12Device_CreateSharedHandle(device, (ID3D12DeviceChild *)fence, NULL, GENERIC_ALL, NULL,
                                                  &fence_handle);
    step("fence-handle", fence_share);
    if (!handle_answer_ok(texture_share) || !handle_answer_ok(fence_share))
        why = "a handle request answered neither S_OK nor E_NOTIMPL";

end:
    if (why) wsprintfA(line, "PS5_PROTON_D3D12_SHARED FAIL: %s\n", why);
    else wsprintfA(line, "PS5_PROTON_D3D12_SHARED PASS sharing=%s\n",
                   texture_share == S_OK && fence_share == S_OK ? "yes" : "no");
    say(line);
    if (texture_handle) CloseHandle(texture_handle);
    if (fence_handle) CloseHandle(fence_handle);
    if (done) CloseHandle(done);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (queue) ID3D12CommandQueue_Release(queue);
    if (fence) ID3D12Fence_Release(fence);
    if (texture) ID3D12Resource_Release(texture);
    if (device) ID3D12Device_Release(device);
    ExitProcess(0);
}
