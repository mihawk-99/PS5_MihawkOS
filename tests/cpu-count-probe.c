/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Every count of processors a game can ask for, side by side, and the processor numbers its threads actually see. A
 * game that sizes a per-processor table by one and indexes it by another reads past its end when they disagree (a
 * Direct3D 12 game's worker read 0x10 + 8 x 12 and 0x10 + 8 x 13 through such a table, 2026-11-03).
 *   system_info     GetSystemInfo's dwNumberOfProcessors
 *   active          GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), and the maximum
 *   logical         GetLogicalProcessorInformationEx(RelationProcessorCore): cores, and the logical processors in them
 *   cpu_sets        GetSystemCpuSetInformation's entries
 *   affinity        GetProcessAffinityMask's process and system masks
 *   number          GetCurrentProcessorNumber over 32 busy threads, as a mask
 *   tsc_aux         RDTSCP's processor id over the same threads, as a mask (what an inlined processor number reads)
 * PASS when every count agrees, no thread sees a processor number at or above it, and GetCurrentProcessorNumber
 * spreads the threads over more than one processor (it gave 0 to every thread before Wine 2457). Exits 0. */
#include <windows.h>

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static volatile LONG64 seen_number, seen_aux;

static DWORD WINAPI spin(void *arg)
{
    const DWORD until = GetTickCount() + 400;
    (void)arg;
    while ((LONG)(GetTickCount() - until) < 0) {
        unsigned aux;
        const DWORD number = GetCurrentProcessorNumber();
        __builtin_ia32_rdtscp(&aux);
        if (number < 64) InterlockedOr64(&seen_number, (LONG64)1 << number);
        if ((aux & 0xfff) < 64) InterlockedOr64(&seen_aux, (LONG64)1 << (aux & 0xfff));
        else InterlockedOr64(&seen_aux, (LONG64)1 << 63);
    }
    return 0;
}

static unsigned highest(LONG64 mask)
{
    unsigned top = 0;
    for (unsigned i = 0; i < 64; i++) if (mask & ((LONG64)1 << i)) top = i + 1;
    return top;   /* one past the highest number seen */
}

void mainCRTStartup(void)
{
    char line[320];
    SYSTEM_INFO info;
    static char buffer[1 << 16];
    DWORD bytes = sizeof(buffer), cores = 0, logical = 0;
    ULONG set_bytes = 0, sets = 0;
    DWORD_PTR process_mask = 0, system_mask = 0;
    HANDLE threads[32];
    BOOL (WINAPI *cpu_sets)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG) =
        (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemCpuSetInformation");

    GetSystemInfo(&info);
    const DWORD active = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), maximum = GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, (void *)buffer, &bytes))
        for (DWORD at = 0; at < bytes;) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entry = (void *)(buffer + at);
            cores++;
            for (WORD g = 0; g < entry->Processor.GroupCount; g++) {
                KAFFINITY mask = entry->Processor.GroupMask[g].Mask;
                while (mask) { logical++; mask &= mask - 1; }
            }
            at += entry->Size;
        }
    if (cpu_sets) {
        cpu_sets(NULL, 0, &set_bytes, GetCurrentProcess(), 0);
        if (set_bytes && set_bytes <= sizeof(buffer) && cpu_sets((void *)buffer, sizeof(buffer), &set_bytes, GetCurrentProcess(), 0))
            for (ULONG at = 0; at < set_bytes; at += ((SYSTEM_CPU_SET_INFORMATION *)(buffer + at))->Size) sets++;
    }
    GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
    for (unsigned i = 0; i < 32; i++) threads[i] = CreateThread(NULL, 0, spin, NULL, 0, NULL);
    WaitForMultipleObjects(32, threads, TRUE, 5000);
    for (unsigned i = 0; i < 32; i++) CloseHandle(threads[i]);

    wsprintfA(line, "PS5_PROTON_CPU_COUNT system_info=%lu active=%lu maximum=%lu cores=%lu logical=%lu cpu_sets=%lu\n",
              info.dwNumberOfProcessors, active, maximum, cores, logical, sets);
    say(line);
    wsprintfA(line, "PS5_PROTON_CPU_COUNT affinity process=%I64x system=%I64x number_seen=%I64x tsc_aux_seen=%I64x\n",
              (ULONG64)process_mask, (ULONG64)system_mask, (ULONG64)seen_number, (ULONG64)seen_aux);
    say(line);
    const DWORD count = info.dwNumberOfProcessors;
    const int agree = active == count && logical == count && (!cpu_sets || sets == count);
    const int numbers_in = highest(seen_number) <= count, aux_in = highest(seen_aux) <= count;
    const int spread = count < 2 || (seen_number & (seen_number - 1)) != 0;
    wsprintfA(line, "PS5_PROTON_CPU_COUNT %s: counts %s, GetCurrentProcessorNumber %s%s, RDTSCP's processor id %s (highest %u of %lu)\n",
              agree && numbers_in && spread ? "PASS" : "FAIL", agree ? "agree" : "disagree",
              numbers_in ? "in range" : "out of range", spread ? "" : " and the same for every thread",
              aux_in ? "in range" : "out of range", highest(seen_aux), count);
    say(line);
    ExitProcess(0);
}
