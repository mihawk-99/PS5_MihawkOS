/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Whether a game reads the GPU's clock: ID3D12CommandQueue::GetClockCalibration three times 200 ms apart, which
 * vkd3d-proton answers with vkGetCalibratedTimestampsKHR and RADV with a timestamp packet it submits alone. The GPU
 * broker refused that submission (it carries no marker), so every read gave 0 in a broker client, in the title and
 * in helpers alike. The broker takes a submission without a marker now (graphics patch 0001).
 * PASS when every GPU reading is non-zero, each later than the last, and the GPU's elapsed time is within half of
 * the CPU's. The lines before the verdict are the measurement either way. Exits 0. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
/* This standalone PE entry does not link the MSVC startup object. */
int _fltused;
#define READS 3

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

void mainCRTStartup(void)
{
    char line[256];
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    UINT64 frequency = 0, gpu[READS] = {0}, cpu[READS] = {0};
    LARGE_INTEGER cpu_frequency;
    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    int good = 1;

    if (FAILED(hr) || FAILED(hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue))) {
        wsprintfA(line, "PS5_PROTON_D3D12_CLOCK FAIL: no device or queue (hr=%08lx)\n", hr);
        say(line);
        ExitProcess(0);
    }
    QueryPerformanceFrequency(&cpu_frequency);
    hr = ID3D12CommandQueue_GetTimestampFrequency(queue, &frequency);
    wsprintfA(line, "PS5_PROTON_D3D12_CLOCK frequency hr=%08lx hz=%I64u\n", hr, frequency);
    say(line);
    if (FAILED(hr) || !frequency) good = 0;
    for (unsigned i = 0; i < READS; i++) {
        if (i) Sleep(200);
        hr = ID3D12CommandQueue_GetClockCalibration(queue, &gpu[i], &cpu[i]);
        wsprintfA(line, "PS5_PROTON_D3D12_CLOCK read %u hr=%08lx gpu=%I64u cpu=%I64u\n", i, hr, gpu[i], cpu[i]);
        say(line);
        if (FAILED(hr) || !gpu[i] || (i && gpu[i] <= gpu[i - 1])) good = 0;
    }
    if (good) {
        /* microseconds each clock counted between the first and the last read */
        const UINT64 gpu_us = (gpu[READS - 1] - gpu[0]) * 1000000 / frequency;
        const UINT64 cpu_us = (cpu[READS - 1] - cpu[0]) * 1000000 / (UINT64)cpu_frequency.QuadPart;
        wsprintfA(line, "PS5_PROTON_D3D12_CLOCK elapsed gpu_us=%I64u cpu_us=%I64u\n", gpu_us, cpu_us);
        say(line);
        if (gpu_us * 2 < cpu_us || gpu_us > cpu_us * 2) good = 0;
    }
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device);
    say(good ? "PS5_PROTON_D3D12_CLOCK PASS\n"
             : "PS5_PROTON_D3D12_CLOCK FAIL: the GPU clock read 0, did not rise, or ran at the wrong rate\n");
    ExitProcess(0);
}
