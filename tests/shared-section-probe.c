/* SPDX-License-Identifier: GPL-3.0-or-later
 * An image with a small shared writable section off the host page starts, and the section is its own bytes and
 * writable. GameMaker's runner has a 4 KiB .mydata, shared and writable, at a 4 KiB offset; on the PS5 (16 KiB host
 * pages) Wine refused to map it shared and failed the image (c000007b, a tester's two GameMaker games, 2026-10-31).
 * Wine patch 2406 maps such a section private. This probe carries the same shape (the build links .mydata RWS and
 * checks it is not 16 KiB aligned); it checks its own section header, the initial bytes and a write. */
#include <windows.h>

__attribute__((section(".mydata"))) volatile unsigned int shared_flags[4] = {0x5ec7ed01u, 2, 3, 4};

static char line[400];
static int failures;

static void fail(const char *what)
{
    ++failures;
    lstrcatA(line, " ");
    lstrcatA(line, what);
    lstrcatA(line, ";");
}

void mainCRTStartup(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    DWORD rva = 0, flags = 0, size = 0;
    char out[600];
    WORD i;

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (!lstrcmpA((const char *)sec[i].Name, ".mydata")) {
            rva = sec[i].VirtualAddress;
            flags = sec[i].Characteristics;
            size = sec[i].Misc.VirtualSize;
        }
    if (!rva) fail("no .mydata section");
    if (!(flags & IMAGE_SCN_MEM_SHARED) || !(flags & IMAGE_SCN_MEM_WRITE)) fail(".mydata is not shared and writable");
    /* Its start or its 4 KiB-rounded size off a 16 KiB page is what Wine refused (the PE64 build has it at 0x5000,
     * the PE32 one at 0x4000 but 4 KiB long, with .reloc after it). */
    if (!((rva | ((size + 0xfff) & ~0xfffu)) & 0x3fff)) fail(".mydata is 16 KiB aligned: the probe does not test the unaligned case");
    if ((unsigned char *)shared_flags < base + rva || (unsigned char *)shared_flags >= base + rva + 0x1000)
        fail("the variable is not in .mydata");
    if (shared_flags[0] != 0x5ec7ed01u || shared_flags[3] != 4) fail("initial bytes wrong");
    shared_flags[1] = 0xabcd;
    if (shared_flags[1] != 0xabcd || shared_flags[0] != 0x5ec7ed01u) fail("write lost");

    wsprintfA(out, "PS5_PROTON_SHARED_SECTION %s: failures=%d rva=%#lx size=%#lx flags=%#lx%s\n",
              failures ? "FAIL" : "PASS", failures, rva, size, flags, line);
    OutputDebugStringA(out);
    ExitProcess(failures ? 1 : 0);
}
