/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Direct3D 12's heaps over a program's own memory (OpenExistingHeapFromAddress), in the title as games run. A
 * recompiled console game stopped at once without them (2026-11-01): VKD3D-Proton builds them on
 * VK_EXT_external_memory_host, which the GPU broker's client did not offer (PS5_Mesa's broker overlay,
 * client-host-memory-winsys.patch; Wine patch 2425 keeps the GPU's access through protection changes).
 *
 * 1 MiB of VirtualAlloc'd memory becomes a heap, and a placed buffer in it is copied on the GPU both ways: the GPU reads
 * what the CPU wrote through the program's pointer, and writes what the CPU then reads there. A heap that VKD3D-Proton
 * fell back to ordinary memory for (its import failed) passes neither check. Then the memory is made read-only, the GPU
 * writes it, the CPU reads it, it is made read-write again, and the GPU reads the CPU's next writes: the GPU keeps its
 * access through the program's protection changes. PASS when the feature is supported and every check holds. Exits 0. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;

#define BYTES (1u << 20)
#define HALF (BYTES / 2)

static char line[256];
static ID3D12Device *device;
static ID3D12CommandQueue *queue;
static ID3D12CommandAllocator *allocator;
static ID3D12GraphicsCommandList *list;
static ID3D12Fence *fence;
static HANDLE done;
static UINT64 fence_value;

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static void step(const char *name, HRESULT hr)
{
    wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP %s hr=%08lx\n", name, hr);
    say(line);
}

static ID3D12Resource *buffer(D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
    D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, BYTES, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *resource = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                      &IID_ID3D12Resource, (void **)&resource);
    step(type == D3D12_HEAP_TYPE_UPLOAD ? "upload-buffer" : "readback-buffer", hr);
    return SUCCEEDED(hr) ? resource : NULL;
}

/* One copy of half the buffer on the GPU, waited for. */
static BOOL copy(ID3D12Resource *to, UINT64 to_offset, ID3D12Resource *from, UINT64 from_offset)
{
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, to, to_offset, from, from_offset, HALF);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList **)&list);
    ID3D12CommandQueue_Signal(queue, fence, ++fence_value);
    ID3D12Fence_SetEventOnCompletion(fence, fence_value, done);
    return WaitForSingleObject(done, 5000) == WAIT_OBJECT_0 && ID3D12Fence_GetCompletedValue(fence) == fence_value;
}

static void fill(volatile UINT32 *words, UINT32 count, UINT32 seed)
{
    for (UINT32 i = 0; i < count; i++) words[i] = seed ^ (i * 2654435761u);
}

/* The first word of count that differs, or count. */
static UINT32 differs(const volatile UINT32 *words, UINT32 count, UINT32 seed)
{
    for (UINT32 i = 0; i < count; i++)
        if (words[i] != (seed ^ (i * 2654435761u))) return i;
    return count;
}

void mainCRTStartup(void)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    D3D12_RESOURCE_DESC placed_desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, BYTES, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                       D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER};
    D3D12_FEATURE_DATA_EXISTING_HEAPS existing = {FALSE};
    static const D3D12_RANGE nothing_read = {0, 0};
    ID3D12Device3 *device3 = NULL;
    ID3D12Heap *heap = NULL;
    ID3D12Resource *placed = NULL, *upload = NULL, *readback = NULL;
    UINT32 *memory = NULL, *mapped = NULL, at;
    DWORD old;
    HRESULT hr;
    const char *why = NULL;

    if (FAILED(hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device))) {
        step("device", hr);
        why = "no device";
        goto end;
    }
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_EXISTING_HEAPS, &existing, sizeof(existing));
    wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP feature hr=%08lx supported=%d\n", hr, existing.Supported);
    say(line);
    if (FAILED(hr) || !existing.Supported) { why = "existing heaps are not supported"; goto end; }
    if (FAILED(hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device3, (void **)&device3))) {
        step("device3", hr);
        why = "no ID3D12Device3";
        goto end;
    }
    if (!(memory = VirtualAlloc(NULL, BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) {
        why = "no memory";
        goto end;
    }
    fill(memory, HALF / 4, 0x5a5a0001);
    hr = ID3D12Device3_OpenExistingHeapFromAddress(device3, memory, &IID_ID3D12Heap, (void **)&heap);
    step("open-existing-heap", hr);
    if (FAILED(hr)) { why = "the heap was not opened"; goto end; }
    hr = ID3D12Device_CreatePlacedResource(device, heap, 0, &placed_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                           &IID_ID3D12Resource, (void **)&placed);
    step("placed-buffer", hr);
    if (FAILED(hr)) { why = "no placed buffer"; goto end; }
    if (!(upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ)) ||
            !(readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST))) {
        why = "no upload or readback buffer";
        goto end;
    }
    if (FAILED(hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue)) ||
            FAILED(hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    &IID_ID3D12CommandAllocator, (void **)&allocator)) ||
            FAILED(hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                    &IID_ID3D12GraphicsCommandList, (void **)&list)) ||
            FAILED(hr = ID3D12GraphicsCommandList_Close(list)) ||
            FAILED(hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence))) {
        step("command-objects", hr);
        why = "no command objects";
        goto end;
    }
    done = CreateEventA(NULL, FALSE, FALSE, NULL);

    /* The GPU reads what the CPU wrote through the program's pointer... */
    if (!copy(readback, 0, placed, 0)) { why = "the first GPU copy did not complete"; goto end; }
    if (FAILED(hr = ID3D12Resource_Map(readback, 0, NULL, (void **)&mapped))) { why = "no readback map"; goto end; }
    at = differs(mapped, HALF / 4, 0x5a5a0001);
    ID3D12Resource_Unmap(readback, 0, &nothing_read);
    wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP gpu-reads-cpu-writes words=%u first-wrong=%u\n", HALF / 4, at);
    say(line);
    if (at != HALF / 4) { why = "the GPU did not read the program's memory"; goto end; }

    /* ...and the CPU reads what the GPU wrote there, with the memory read-only for the CPU meanwhile. */
    if (FAILED(hr = ID3D12Resource_Map(upload, 0, &nothing_read, (void **)&mapped))) { why = "no upload map"; goto end; }
    fill(mapped, HALF / 4, 0x00c0ffee);
    ID3D12Resource_Unmap(upload, 0, NULL);
    if (!VirtualProtect(memory, BYTES, PAGE_READONLY, &old)) { why = "VirtualProtect read-only failed"; goto end; }
    if (!copy(placed, HALF, upload, 0)) { why = "the second GPU copy did not complete"; goto end; }
    at = differs(memory + HALF / 4, HALF / 4, 0x00c0ffee);
    wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP cpu-reads-gpu-writes read-only words=%u first-wrong=%u\n",
              HALF / 4, at);
    say(line);
    if (at != HALF / 4) { why = "the program did not see the GPU's writes"; goto end; }

    /* Read-write again: the GPU still reads the CPU's new writes. */
    if (!VirtualProtect(memory, BYTES, PAGE_READWRITE, &old)) { why = "VirtualProtect read-write failed"; goto end; }
    fill(memory, HALF / 4, 0x77770003);
    if (!copy(readback, 0, placed, 0)) { why = "the third GPU copy did not complete"; goto end; }
    if (FAILED(hr = ID3D12Resource_Map(readback, 0, NULL, (void **)&mapped))) { why = "no readback map"; goto end; }
    at = differs(mapped, HALF / 4, 0x77770003);
    ID3D12Resource_Unmap(readback, 0, &nothing_read);
    wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP after-protection-changes words=%u first-wrong=%u\n", HALF / 4, at);
    say(line);
    if (at != HALF / 4) why = "the GPU lost the program's memory after its protection changed";

end:
    if (why) wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP FAIL: %s\n", why);
    else wsprintfA(line, "PS5_PROTON_D3D12_EXISTING_HEAP PASS bytes=%u copies=3\n", BYTES);
    say(line);
    /* The heap and its buffer go before the memory, as Direct3D 12 asks. */
    if (done) CloseHandle(done);
    if (fence) ID3D12Fence_Release(fence);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (queue) ID3D12CommandQueue_Release(queue);
    if (readback) ID3D12Resource_Release(readback);
    if (upload) ID3D12Resource_Release(upload);
    if (placed) ID3D12Resource_Release(placed);
    if (heap) ID3D12Heap_Release(heap);
    if (device3) ID3D12Device3_Release(device3);
    if (device) ID3D12Device_Release(device);
    if (memory) VirtualFree(memory, 0, MEM_RELEASE);
    ExitProcess(0);
}
