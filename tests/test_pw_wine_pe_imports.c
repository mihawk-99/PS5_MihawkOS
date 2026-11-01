/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Is a program a game, by its imports (wine/ps5/pw_wine_pe_imports.h)? Fixture images shaped as a linker writes them:
 * a PE32+ header, one section holding the import and delay-import tables and their names, a CLR directory for .NET,
 * and the program's folder holding the DLLs it brings (an engine's player DLL, a launcher's UI DLL, a game's CEF). */
#include "../wine/ps5/pw_wine_pe_imports.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { IMAGE = 0x2000, RAW = 0x400, RVA = 0x1000 };
struct fixture { unsigned char bytes[IMAGE]; };

static void put32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> 8 * i); }
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

/* A 64-bit image importing `imports` and delay-loading `delayed` (NULL-ended lists); clr: a CLR directory. */
static void make(struct fixture *f, const char *const *imports, const char *const *delayed, int clr)
{
    unsigned char *b = f->bytes;
    memset(b, 0, sizeof(f->bytes));
    b[0] = 'M'; b[1] = 'Z'; put32(b + 0x3c, 0x40);
    memcpy(b + 0x40, "PE\0\0", 4);
    put16(b + 0x44, 0x8664); put16(b + 0x46, 1); put16(b + 0x54, 240);   /* machine, one section, PE32+ optional size */
    unsigned char *opt = b + 0x58;
    put16(opt, 0x020b); put32(opt + 108, 16);
    unsigned char *section = opt + 240;
    memcpy(section, ".rdata", 6);
    put32(section + 8, 0x1000); put32(section + 12, RVA); put32(section + 16, 0x1000); put32(section + 20, RAW);
    uint32_t names = RVA + 0x600;   /* the DLL names after both tables */
    unsigned char *table = b + RAW;   /* the import descriptors at RVA, the delay-load ones at RVA + 0x300 */
    unsigned n = 0;
    for (; imports && imports[n]; ++n) {
        put32(table + 20 * n + 12, names);
        strcpy((char *)b + RAW + (names - RVA), imports[n]);
        names += (uint32_t)strlen(imports[n]) + 1;
    }
    if (n) { put32(opt + 112 + 8 * 1, RVA); put32(opt + 112 + 8 * 1 + 4, 20 * (n + 1)); }
    unsigned d = 0;
    for (; delayed && delayed[d]; ++d) {
        put32(table + 0x300 + 32 * d + 4, names);
        strcpy((char *)b + RAW + (names - RVA), delayed[d]);
        names += (uint32_t)strlen(delayed[d]) + 1;
    }
    if (d) { put32(opt + 112 + 8 * 13, RVA + 0x300); put32(opt + 112 + 8 * 13 + 4, 32 * (d + 1)); }
    if (clr) { put32(opt + 112 + 8 * 14, RVA + 0x800); put32(opt + 112 + 8 * 14 + 4, 72); }
}

/* The program, and up to two DLLs in its folder. */
struct folder { const struct fixture *exe; const char *dll_name[2]; const struct fixture *dll[2]; int reads; };

static int folder_read(void *context, const char *name, uint64_t offset, void *into, size_t size)
{
    struct folder *f = context;
    const struct fixture *image = NULL;
    if (!name) image = f->exe;
    for (int i = 0; name && i < 2; ++i) if (f->dll_name[i] && !strcasecmp(name, f->dll_name[i])) image = f->dll[i];
    f->reads++;
    if (!image || offset + size > sizeof(image->bytes)) return -1;
    memcpy(into, image->bytes + offset, size);
    return 0;
}

static enum PwPeKind kind_of(struct folder *f, char *deepest)
{
    return pw_pe_kind(folder_read, f, deepest);
}

int main(void)
{
    static struct fixture exe, dll, other;
    char deepest[PW_PE_IMPORT_NAME];
    struct folder f;

    /* A D3D11 game: its own imports reach the API. */
    make(&exe, (const char *const[]){ "KERNEL32.dll", "USER32.dll", "d3d11.dll", "dxgi.dll", NULL }, NULL, 0);
    f = (struct folder){ &exe, { NULL }, { NULL }, 0 };
    assert(kind_of(&f, deepest) == PW_PE_KIND_GAME && !deepest[0]);

    /* Direct3D 12 delay-loaded, as an engine does: still a game. */
    make(&exe, (const char *const[]){ "KERNEL32.dll", NULL }, (const char *const[]){ "d3d12.dll", NULL }, 0);
    f = (struct folder){ &exe, { NULL }, { NULL }, 0 };
    assert(kind_of(&f, deepest) == PW_PE_KIND_GAME);

    /* A Unity game: the exe imports only its player DLL from its folder, and that one imports D3D11 and DXGI. */
    make(&exe, (const char *const[]){ "UnityPlayer.dll", "KERNEL32.dll", NULL }, NULL, 0);
    make(&dll, (const char *const[]){ "KERNEL32.dll", "d3d11.dll", "DXGI.dll", NULL }, NULL, 0);
    f = (struct folder){ &exe, { "UnityPlayer.dll" }, { &dll }, 0 };
    assert(kind_of(&f, deepest) == PW_PE_KIND_GAME && pw_pe_name_is(deepest, "unityplayer.dll"));

    /* A game that embeds a browser for its menus (a big racing game's layout, 2026-11-01): the exe imports D3D11, DXGI
     * and CEF, with libcef.dll and its web helper beside it. The exe's own graphics import makes it a game; the helper,
     * importing CEF and no graphics API, is a browser shell. */
    make(&exe, (const char *const[]){ "KERNEL32.dll", "libcef.dll", "d3d11.dll", "dxgi.dll", NULL }, NULL, 0);
    make(&dll, (const char *const[]){ "KERNEL32.dll", "USER32.dll", NULL }, NULL, 0);
    f = (struct folder){ &exe, { "libcef.dll" }, { &dll }, 0 };
    assert(kind_of(&f, deepest) == PW_PE_KIND_GAME && !deepest[0]);
    make(&exe, (const char *const[]){ "libcef.dll", "KERNEL32.dll", NULL }, NULL, 0);
    f = (struct folder){ &exe, { "libcef.dll" }, { &dll }, 0 };
    assert(kind_of(&f, NULL) == PW_PE_KIND_BROWSER);
    /* A browser-shell launcher with no graphics import of its own: its UI DLL brings the browser. */
    make(&exe, (const char *const[]){ "KERNEL32.dll", "LauncherUI.dll", NULL }, NULL, 0);
    make(&dll, (const char *const[]){ "Qt5WebEngineCore.dll", "opengl32.dll", NULL }, NULL, 0);
    f = (struct folder){ &exe, { "LauncherUI.dll" }, { &dll }, 0 };
    assert(kind_of(&f, deepest) == PW_PE_KIND_BROWSER && pw_pe_name_is(deepest, "launcherui.dll"));

    /* A .NET launcher (WPF, WinForms): its CLR header decides, whatever it imports. */
    make(&exe, (const char *const[]){ "mscoree.dll", NULL }, NULL, 1);
    f = (struct folder){ &exe, { NULL }, { NULL }, 0 };
    assert(kind_of(&f, NULL) == PW_PE_KIND_DOTNET);

    /* A bootstrap stub that only starts the real game: no graphics anywhere, so plain. A folder DLL not there, or a
     * system DLL, is not looked for (the reads stay on the exe). */
    make(&exe, (const char *const[]){ "KERNEL32.dll", "SHELL32.dll", "api-ms-win-crt-runtime-l1-1-0.dll", "missing.dll", NULL },
         NULL, 0);
    f = (struct folder){ &exe, { NULL }, { NULL }, 0 };
    assert(kind_of(&f, NULL) == PW_PE_KIND_PLAIN);
    make(&other, (const char *const[]){ "KERNEL32.dll", NULL }, NULL, 0);
    make(&exe, (const char *const[]){ "KERNEL32.dll", "helper.dll", NULL }, NULL, 0);
    f = (struct folder){ &exe, { "helper.dll" }, { &other }, 0 };
    assert(kind_of(&f, NULL) == PW_PE_KIND_PLAIN);

    /* Not a PE image, or no file: unread. */
    memset(&exe, 0, sizeof(exe));
    f = (struct folder){ &exe, { NULL }, { NULL }, 0 };
    assert(kind_of(&f, NULL) == PW_PE_KIND_UNREAD);
    f = (struct folder){ NULL, { NULL }, { NULL }, 0 };
    assert(kind_of(&f, NULL) == PW_PE_KIND_UNREAD);

    assert(!strcmp(pw_pe_kind_word(PW_PE_KIND_GAME), "imports a graphics API"));
    printf("test_pw_wine_pe_imports: ok\n");
    return 0;
}
