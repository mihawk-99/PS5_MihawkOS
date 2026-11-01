/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_PE_IMPORTS_H
#define PW_WINE_PE_IMPORTS_H
/* Is a Windows program a game? By what it is, not its name (Alpha 7, "Run inside MihawkOS"; docs/TITLE_CLIENT.md):
 * the DLLs its PE image imports, directly or delay-loaded, and one level deeper the DLLs it imports from its own folder
 * (UnityPlayer.dll, an engine's modules). A program that imports a graphics API itself is a game, whatever else it
 * imports (a game may embed CEF for its menus); otherwise one built on a browser shell (CEF, Qt WebEngine) or on .NET
 * is a launcher or a tool, and one whose folder DLL reaches a graphics API is a game; anything else draws nothing
 * the title is needed for. Only a game gets the title's place (wine/ps5/pw_wine_title_game.h), so a launcher that
 * holds it cannot end the session under the game it starts.
 *
 * Header-only, as ntdll's PS5 code includes it; the file reads are injected so tests/test_pw_wine_pe_imports.c runs it
 * on fixture images. Nothing here allocates. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum PwPeKind {
    PW_PE_KIND_UNREAD = 0,  /* not read: no file, not a PE image */
    PW_PE_KIND_GAME,        /* reaches d3d8/9/10/11/12, dxgi, ddraw, vulkan-1 or opengl32 */
    PW_PE_KIND_BROWSER,     /* a browser shell: libcef, chrome_elf, Qt WebEngine */
    PW_PE_KIND_DOTNET,      /* a .NET program (a CLR header): WPF, WinForms */
    PW_PE_KIND_PLAIN,       /* none of those */
};

/* Read bytes [offset, offset + size) of the image named by `name` (NULL: the program itself; otherwise a DLL in its
 * folder) into `into`; 0 when all of them were read. */
typedef int (*PwPeImportRead)(void *context, const char *name, uint64_t offset, void *into, size_t size);

#define PW_PE_IMPORTS_MAX 96          /* names kept from one image */
#define PW_PE_IMPORT_NAME 48
#define PW_PE_SECTIONS_MAX 96

static inline uint32_t pw_pe_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint16_t pw_pe_u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline char pw_pe_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* 1 when name equals want, ignoring case. */
static inline int pw_pe_name_is(const char *name, const char *want)
{
    size_t i = 0;
    while (name[i] && want[i] && pw_pe_lower(name[i]) == want[i]) ++i;
    return !name[i] && !want[i];
}

struct PwPeImports {
    unsigned count;
    int clr;  /* the image has a CLR (.NET) header */
    char names[PW_PE_IMPORTS_MAX][PW_PE_IMPORT_NAME];
};

/* The file offset of an RVA, from the section table; 0 when no section holds it. */
static inline uint64_t pw_pe_rva(PwPeImportRead read, void *context, const char *image, uint64_t table, unsigned sections,
                                 uint32_t rva)
{
    unsigned char section[40];
    for (unsigned i = 0; i < sections && i < PW_PE_SECTIONS_MAX; i++) {
        if (read(context, image, table + 40ull * i, section, sizeof(section))) return 0;
        const uint32_t virtual_size = pw_pe_u32(section + 8), address = pw_pe_u32(section + 12);
        const uint32_t raw_size = pw_pe_u32(section + 16), raw = pw_pe_u32(section + 20);
        const uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        if (rva >= address && rva - address < span && rva - address < raw_size) return (uint64_t)raw + (rva - address);
    }
    return 0;
}

/* The DLL names an image imports (directory 1, 20-byte descriptors, name at +12) and delay-loads (directory 13,
 * 32-byte descriptors, name at +4), and whether it has a CLR header (directory 14). 0 when it is a PE image. */
static inline int pw_pe_imports_read(PwPeImportRead read, void *context, const char *image, struct PwPeImports *out)
{
    unsigned char dos[64], header[24], magic[2], count[4], directory[8];
    memset(out, 0, sizeof(*out));
    if (read(context, image, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') return -1;
    const uint64_t pe = pw_pe_u32(dos + 0x3c);
    if (read(context, image, pe, header, sizeof(header)) || memcmp(header, "PE\0\0", 4)) return -1;
    const unsigned sections = pw_pe_u16(header + 6), optional_size = pw_pe_u16(header + 20);
    const uint64_t start = pe + sizeof(header), table = start + optional_size;
    if (read(context, image, start, magic, sizeof(magic))) return -1;
    const uint16_t kind = pw_pe_u16(magic);
    if (kind != 0x010b && kind != 0x020b) return -1;
    const uint64_t count_at = kind == 0x010b ? 92 : 108, directories = count_at + 4;
    if (read(context, image, start + count_at, count, sizeof(count))) return -1;
    const uint32_t present = pw_pe_u32(count);
    static const unsigned which[2] = { 1, 13 }, stride[2] = { 20, 32 }, name_at[2] = { 12, 4 };
    for (unsigned d = 0; d < 2; ++d) {
        if (present <= which[d] || optional_size < directories + 8ull * (which[d] + 1) ||
            read(context, image, start + directories + 8ull * which[d], directory, sizeof(directory)) || !pw_pe_u32(directory))
            continue;
        const uint64_t at = pw_pe_rva(read, context, image, table, sections, pw_pe_u32(directory));
        for (unsigned i = 0; at && i < 256 && out->count < PW_PE_IMPORTS_MAX; ++i) {
            unsigned char entry[32];
            if (read(context, image, at + (uint64_t)stride[d] * i, entry, stride[d])) break;
            const uint32_t name_rva = pw_pe_u32(entry + name_at[d]);
            if (!name_rva) break;   /* the all-zero descriptor ends the table */
            const uint64_t name = pw_pe_rva(read, context, image, table, sections, name_rva);
            char *slot = out->names[out->count];
            if (!name || read(context, image, name, slot, PW_PE_IMPORT_NAME - 1)) continue;
            slot[PW_PE_IMPORT_NAME - 1] = 0;
            if (slot[0]) out->count++;
        }
    }
    out->clr = present > 14 && optional_size >= directories + 8ull * 15 &&
               !read(context, image, start + directories + 8ull * 14, directory, sizeof(directory)) && pw_pe_u32(directory);
    return 0;
}

static inline int pw_pe_graphics(const char *name)
{
    static const char *const apis[] = { "d3d8.dll", "d3d9.dll", "d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "d3d11.dll",
                                        "d3d12.dll", "dxgi.dll", "ddraw.dll", "vulkan-1.dll", "opengl32.dll" };
    for (size_t i = 0; i < sizeof(apis) / sizeof(*apis); ++i) if (pw_pe_name_is(name, apis[i])) return 1;
    return 0;
}
static inline int pw_pe_browser(const char *name)
{
    static const char *const shells[] = { "libcef.dll", "chrome_elf.dll", "qt5webenginecore.dll", "qt6webenginecore.dll" };
    for (size_t i = 0; i < sizeof(shells) / sizeof(*shells); ++i) if (pw_pe_name_is(name, shells[i])) return 1;
    return 0;
}
/* A DLL every Windows program may import, never one of the game's own: not looked for in its folder. */
static inline int pw_pe_system(const char *name)
{
    static const char *const prefixes[] = { "api-ms-", "ext-ms-", "msvcp", "vcruntime", "ucrtbase" };
    static const char *const names[] = { "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll",
                                         "ole32.dll", "oleaut32.dll", "ntdll.dll", "ws2_32.dll", "winmm.dll",
                                         "version.dll", "shlwapi.dll", "comctl32.dll", "comdlg32.dll", "imm32.dll",
                                         "setupapi.dll", "crypt32.dll", "bcrypt.dll", "dbghelp.dll", "psapi.dll",
                                         "dinput8.dll", "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll",
                                         "mscoree.dll", "msvcrt.dll", "winhttp.dll", "wininet.dll", "iphlpapi.dll",
                                         "dwmapi.dll", "uxtheme.dll", "hid.dll", "dsound.dll", "xaudio2_7.dll",
                                         "xaudio2_9.dll", "mfplat.dll", "mf.dll", "mfreadwrite.dll", "secur32.dll" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(*prefixes); ++i) {
        size_t n = 0;
        while (prefixes[i][n] && pw_pe_lower(name[n]) == prefixes[i][n]) ++n;
        if (!prefixes[i][n]) return 1;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) if (pw_pe_name_is(name, names[i])) return 1;
    return 0;
}

/* What the program is. `deepest` (may be NULL; PW_PE_IMPORT_NAME bytes) gets the folder DLL that decided, or "" when
 * the program's own imports did. */
static inline enum PwPeKind pw_pe_kind(PwPeImportRead read, void *context, char *deepest)
{
    struct PwPeImports top, dll;   /* about 9 KiB of stack: a Windows thread's is far larger */
    if (deepest) deepest[0] = 0;
    if (pw_pe_imports_read(read, context, NULL, &top)) return PW_PE_KIND_UNREAD;
    /* The program's own graphics import decides first: a game that embeds a browser for its menus imports both (a big
     * racing game imports d3d11 and CEF, its web helper beside it; it went to a helper as "a browser shell",
     * 2026-11-01). A browser shell or a launcher imports the browser and no graphics API itself. */
    for (unsigned i = 0; i < top.count; ++i) if (pw_pe_graphics(top.names[i])) return PW_PE_KIND_GAME;
    for (unsigned i = 0; i < top.count; ++i) if (pw_pe_browser(top.names[i])) return PW_PE_KIND_BROWSER;
    if (top.clr) return PW_PE_KIND_DOTNET;
    for (unsigned i = 0; i < top.count; ++i) {
        if (pw_pe_system(top.names[i]) || pw_pe_imports_read(read, context, top.names[i], &dll)) continue;
        for (unsigned j = 0; j < dll.count; ++j)
            if (pw_pe_browser(dll.names[j])) { if (deepest) strcpy(deepest, top.names[i]); return PW_PE_KIND_BROWSER; }
        for (unsigned j = 0; j < dll.count; ++j)
            if (pw_pe_graphics(dll.names[j])) { if (deepest) strcpy(deepest, top.names[i]); return PW_PE_KIND_GAME; }
    }
    return PW_PE_KIND_PLAIN;
}

static inline const char *pw_pe_kind_word(enum PwPeKind kind)
{
    switch (kind) {
    case PW_PE_KIND_GAME: return "imports a graphics API";
    case PW_PE_KIND_BROWSER: return "a browser shell";
    case PW_PE_KIND_DOTNET: return ".NET";
    case PW_PE_KIND_PLAIN: return "no graphics imports";
    default: return "unreadable";
    }
}
#endif
