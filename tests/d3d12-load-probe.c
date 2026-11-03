/* SPDX-License-Identifier: GPL-3.0-or-later */
/* How much a game may load through Direct3D 12 before it is told E_OUTOFMEMORY, kind by kind, as an open-world game
 * does while it loads. A broker client's host-visible memory, which UPLOAD and READBACK heaps come from, is capped
 * (PROTON_GPU_HOST_MIB, 1 GiB by default), and a game sees that cap as its shared video-memory budget:
 *   upload    64 MiB UPLOAD committed buffers, mapped and written, to 3 GiB;
 *   heaps     64 MiB DEFAULT heaps, each filled with placed 2048x2048 BC7 textures, to 3 GiB;
 *   textures  committed 1024x1024 RGBA8 textures with all mips, to 1 GiB;
 *   reserved  16 MiB reserved buffers, each mapped onto one 64 MiB DEFAULT heap with UpdateTileMappings, 2000 of them.
 * Each part stops at its first refusal and says where and with what; everything is released before the next. The
 * video memory figures (QueryVideoMemoryInfo, local and non-local) come first. Run with VKD3D_CONFIG=log_memory_budget
 * for the allocation that failed. The targets lie past what a game is given on purpose, so each part's line is the
 * measurement; PASS when upload holds the whole non-local budget the game is told (less one buffer), the heaps and the
 * textures reach their ends, and the reserved part ends in a refusal (E_OUTOFMEMORY, the page-table floor) with the
 * queue still alive after it. Exits 0. */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;
#define MIB (1ull << 20)
#define MAX_OBJECTS 4096

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static ID3D12Device *device;
static IUnknown *objects[MAX_OBJECTS];
static unsigned failed_parts;
static UINT64 non_local_budget;   /* the shared budget DXGI tells the game */

static void release_all(unsigned count)
{
    for (unsigned i = 0; i < count; i++) IUnknown_Release(objects[i]);
}

static void verdict(const char *part, unsigned made, unsigned wanted, UINT64 bytes, HRESULT hr, int good)
{
    char line[256];
    wsprintfA(line, "PS5_PROTON_D3D12_LOAD %s made=%u of %u bytes_mib=%I64u last_hr=%08lx\n", part, made, wanted,
              bytes / MIB, hr);
    say(line);
    if (!good) failed_parts++;
}

static D3D12_RESOURCE_DESC buffer_desc(UINT64 bytes)
{
    D3D12_RESOURCE_DESC desc = {0};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(UINT size, DXGI_FORMAT format, UINT16 mips)
{
    D3D12_RESOURCE_DESC desc = {0};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = mips;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    return desc;
}

static void upload_part(void)
{
    const unsigned wanted = 48;   /* 3 GiB */
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_UPLOAD};
    const D3D12_RESOURCE_DESC desc = buffer_desc(64 * MIB);
    HRESULT hr = S_OK;
    unsigned made = 0;
    while (made < wanted) {
        ID3D12Resource *resource;
        void *data;
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource,
                                                  (void **)&resource);
        if (FAILED(hr)) break;
        objects[made++] = (IUnknown *)resource;
        if (FAILED(hr = ID3D12Resource_Map(resource, 0, NULL, &data))) break;
        for (UINT64 at = 0; at < desc.Width; at += 4096) ((volatile char *)data)[at] = (char)at;
        ID3D12Resource_Unmap(resource, 0, NULL);
    }
    verdict("upload", made, wanted, made * desc.Width, hr,
            non_local_budget && made * desc.Width + desc.Width >= non_local_budget);
    release_all(made);
}

static void heaps_part(void)
{
    const unsigned wanted = 48;   /* 3 GiB of heaps */
    const D3D12_RESOURCE_DESC desc = texture_desc(2048, DXGI_FORMAT_BC7_UNORM, 12);
    D3D12_RESOURCE_ALLOCATION_INFO info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc);
    D3D12_HEAP_DESC heap_desc = {.SizeInBytes = 64 * MIB, .Properties = {.Type = D3D12_HEAP_TYPE_DEFAULT},
                                 .Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES};
    HRESULT hr = S_OK;
    unsigned heaps = 0, count = 0;
    char line[160];
    wsprintfA(line, "PS5_PROTON_D3D12_LOAD heaps texture_kib=%I64u alignment=%I64u\n", info.SizeInBytes / 1024,
              info.Alignment);
    say(line);
    while (heaps < wanted && count < MAX_OBJECTS) {
        ID3D12Heap *heap;
        if (FAILED(hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap))) break;
        objects[count++] = (IUnknown *)heap;
        heaps++;
        for (UINT64 offset = 0; offset + info.SizeInBytes <= heap_desc.SizeInBytes && count < MAX_OBJECTS;
             offset += (info.SizeInBytes + info.Alignment - 1) / info.Alignment * info.Alignment) {
            ID3D12Resource *texture;
            if (FAILED(hr = ID3D12Device_CreatePlacedResource(device, heap, offset, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                              NULL, &IID_ID3D12Resource, (void **)&texture)))
                break;
            objects[count++] = (IUnknown *)texture;
        }
        if (FAILED(hr)) break;
    }
    verdict("heaps", heaps, wanted, heaps * heap_desc.SizeInBytes, hr, heaps == wanted);
    release_all(count);
}

static void textures_part(void)
{
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = texture_desc(1024, DXGI_FORMAT_R8G8B8A8_UNORM, 11);
    const D3D12_RESOURCE_ALLOCATION_INFO info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc);
    const unsigned wanted = (unsigned)(1024 * MIB / info.SizeInBytes);
    HRESULT hr = S_OK;
    unsigned made = 0;
    while (made < wanted && made < MAX_OBJECTS) {
        ID3D12Resource *texture;
        if (FAILED(hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                             D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                             (void **)&texture)))
            break;
        objects[made++] = (IUnknown *)texture;
    }
    verdict("textures", made, wanted, made * info.SizeInBytes, hr, made == wanted);
    release_all(made);
}

static void reserved_part(ID3D12CommandQueue *queue)
{
    const unsigned wanted = 2000;
    const D3D12_RESOURCE_DESC desc = buffer_desc(16 * MIB);
    D3D12_HEAP_DESC heap_desc = {.SizeInBytes = 64 * MIB, .Properties = {.Type = D3D12_HEAP_TYPE_DEFAULT},
                                 .Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    ID3D12Heap *heap;
    HRESULT hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    unsigned made = 0;
    if (SUCCEEDED(hr)) {
        while (made < wanted && made < MAX_OBJECTS) {
            ID3D12Resource *resource;
            /* each 16 MiB buffer is 256 tiles of 64 KiB, mapped onto one quarter of the heap */
            const D3D12_TILED_RESOURCE_COORDINATE start = {0};
            const D3D12_TILE_REGION_SIZE size = {.NumTiles = 256, .UseBox = FALSE};
            const UINT offset = (made % 4) * 256, tiles = 256;
            const D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
            if (FAILED(hr = ID3D12Device_CreateReservedResource(device, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                                &IID_ID3D12Resource, (void **)&resource)))
                break;
            objects[made++] = (IUnknown *)resource;
            ID3D12CommandQueue_UpdateTileMappings(queue, resource, 1, &start, &size, heap, 1, &flags, &offset, &tiles,
                                                  D3D12_TILE_MAPPING_FLAG_NONE);
        }
    }
    /* the queue's tile mappings must have run before their resources and heap go (D3D12's rule for anything in use);
     * a lost device leaves the fence short of 1 */
    UINT64 completed = 0;
    {
        ID3D12Fence *fence;
        HANDLE done = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (SUCCEEDED(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence))) {
            if (SUCCEEDED(ID3D12CommandQueue_Signal(queue, fence, 1)) &&
                SUCCEEDED(ID3D12Fence_SetEventOnCompletion(fence, 1, done)))
                WaitForSingleObject(done, 10000);
            completed = ID3D12Fence_GetCompletedValue(fence);
            ID3D12Fence_Release(fence);
        }
        CloseHandle(done);
    }
    {
        char line[96];
        wsprintfA(line, "PS5_PROTON_D3D12_LOAD reserved queue_fence=%I64u\n", completed);
        say(line);
    }
    verdict("reserved", made, wanted, made * desc.Width, hr,
            made && (made == wanted || hr == E_OUTOFMEMORY) && completed == 1);
    release_all(made);
    if (heap) ID3D12Heap_Release(heap);
}

void mainCRTStartup(void)
{
    char line[256];
    ID3D12CommandQueue *queue = NULL;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter3 *adapter = NULL;
    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);

    if (FAILED(hr) ||
        FAILED(hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue))) {
        wsprintfA(line, "PS5_PROTON_D3D12_LOAD FAIL: no device or queue (hr=%08lx)\n", hr);
        say(line);
        ExitProcess(0);
    }
    if (SUCCEEDED(CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory)) &&
        SUCCEEDED(IDXGIFactory4_EnumAdapterByLuid(factory, ID3D12Device_GetAdapterLuid(device), &IID_IDXGIAdapter3,
                                                  (void **)&adapter))) {
        DXGI_QUERY_VIDEO_MEMORY_INFO local = {0}, shared = {0};
        IDXGIAdapter3_QueryVideoMemoryInfo(adapter, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        IDXGIAdapter3_QueryVideoMemoryInfo(adapter, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
        non_local_budget = shared.Budget;
        wsprintfA(line, "PS5_PROTON_D3D12_LOAD budget local_mib=%I64u non_local_mib=%I64u\n", local.Budget / MIB,
                  shared.Budget / MIB);
        say(line);
        IDXGIAdapter3_Release(adapter);
    }
    if (factory) IDXGIFactory4_Release(factory);
    upload_part();
    heaps_part();
    textures_part();
    reserved_part(queue);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device);
    wsprintfA(line, failed_parts ? "PS5_PROTON_D3D12_LOAD FAIL: %u parts refused before their end\n"
                                 : "PS5_PROTON_D3D12_LOAD PASS\n", failed_parts);
    say(line);
    ExitProcess(0);
}
