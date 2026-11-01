/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2426: ps5_floor_vm_counters and ps5_describe_foreign_query, cut from the patch (foreign.inc), with the few
 * Windows types they use declared as winternl.h does. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint16_t WCHAR;
typedef uintptr_t ULONG_PTR;
typedef unsigned long ULONG_LONG_HOST;
typedef uint32_t ULONG;
typedef size_t SIZE_T;
typedef int PROCESSINFOCLASS;
enum { ProcessBasicInformation = 0, ProcessVmCounters = 3, ProcessWow64Information = 26, ProcessImageFileName = 27,
       ProcessImageFileNameWin32 = 43 };
typedef struct { uint32_t ExitStatus; void *PebBaseAddress; ULONG_PTR AffinityMask; int32_t BasePriority;
                 ULONG_PTR UniqueProcessId, InheritedFromUniqueProcessId; } PROCESS_BASIC_INFORMATION;
typedef struct { SIZE_T PeakVirtualSize, VirtualSize; ULONG PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize,
                 QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage,
                 PagefileUsage, PeakPagefileUsage; } VM_COUNTERS;
typedef struct { SIZE_T PeakVirtualSize, VirtualSize; ULONG PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize,
                 QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage,
                 PagefileUsage, PeakPagefileUsage, PrivateUsage; } VM_COUNTERS_EX;
typedef struct { uint16_t Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING;

#include "foreign.inc"

static int bad;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); bad = 1; } } while (0)

int main(void)
{
    char line[160];
    /* the floor: only an empty process gets one; a measured one is left as it is */
    VM_COUNTERS_EX vmc;
    memset(&vmc, 0, sizeof(vmc));
    ps5_floor_vm_counters(&vmc);
    CHECK(vmc.WorkingSetSize == (16u << 20) && vmc.PeakWorkingSetSize == vmc.WorkingSetSize &&
          vmc.PagefileUsage == (16u << 20) && vmc.VirtualSize == (256u << 20) && vmc.PeakVirtualSize == vmc.VirtualSize,
          "the floor of an empty process: ws %zu virtual %zu", vmc.WorkingSetSize, vmc.VirtualSize);
    memset(&vmc, 0, sizeof(vmc));
    vmc.WorkingSetSize = 5 << 20;
    vmc.VirtualSize = 1u << 30;
    ps5_floor_vm_counters(&vmc);
    CHECK(vmc.WorkingSetSize == (5u << 20) && vmc.VirtualSize == (1u << 30) && !vmc.PagefileUsage,
          "a measured process was changed");
    /* what the log says */
    PROCESS_BASIC_INFORMATION pbi;
    memset(&pbi, 0, sizeof(pbi));
    pbi.UniqueProcessId = 0x30;
    pbi.InheritedFromUniqueProcessId = 0x2c;
    pbi.ExitStatus = 0x103;
    CHECK(ps5_describe_foreign_query(ProcessBasicInformation, &pbi, sizeof(pbi), 0, line, sizeof(line)) > 0 &&
          !strcmp(line, "basic: pid 0030 parent 002c exit 00000103"), "basic: %s", line);
    VM_COUNTERS vm;
    memset(&vm, 0, sizeof(vm));
    vm.WorkingSetSize = 16 << 20;
    vm.VirtualSize = 256 << 20;
    CHECK(ps5_describe_foreign_query(ProcessVmCounters, &vm, sizeof(vm), 0, line, sizeof(line)) > 0 &&
          !strcmp(line, "vm: working set 16384 KiB, virtual 262144 KiB"), "vm: %s", line);
    ULONG_PTR wow = 0;
    CHECK(ps5_describe_foreign_query(ProcessWow64Information, &wow, sizeof(wow), 0, line, sizeof(line)) > 0 &&
          !strcmp(line, "wow64: 0"), "wow64: %s", line);
    struct { UNICODE_STRING name; WCHAR text[40]; } image;
    const char *path = "C:\\windows\\system32\\explorer.exe";
    size_t n = strlen(path);
    for (size_t i = 0; i <= n; i++) image.text[i] = (WCHAR)path[i];
    image.name.Length = (uint16_t)(n * 2);
    image.name.MaximumLength = image.name.Length + 2;
    image.name.Buffer = image.text;
    CHECK(ps5_describe_foreign_query(ProcessImageFileNameWin32, &image, sizeof(image), 0, line, sizeof(line)) > 0 &&
          !strcmp(line, "image: C:\\windows\\system32\\explorer.exe"), "image: %s", line);
    image.text[1] = 0x416;   /* a name with a non-ASCII character is shown with a ? */
    ps5_describe_foreign_query(ProcessImageFileName, &image, sizeof(image), 0, line, sizeof(line));
    CHECK(!strcmp(line, "image: C?\\windows\\system32\\explorer.exe") || strstr(line, "?"), "non-ASCII: %s", line);
    /* a failure shows its status, whatever the class; an unknown class says nothing; a short answer says nothing */
    CHECK(ps5_describe_foreign_query(ProcessVmCounters, NULL, 0, 0xc0000008u, line, sizeof(line)) > 0 &&
          !strcmp(line, "class 3: status c0000008"), "status: %s", line);
    CHECK(!ps5_describe_foreign_query(1234, &pbi, sizeof(pbi), 0, line, sizeof(line)), "an unknown class was described");
    CHECK(!ps5_describe_foreign_query(ProcessBasicInformation, &pbi, 4, 0, line, sizeof(line)), "a short answer was described");
    puts(bad ? "pw foreign process FAILED" : "pw foreign process passed");
    return bad;
}
