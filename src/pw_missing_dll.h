/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_MISSING_DLL_H
#define PW_MISSING_DLL_H
/* Why a program in the title could not start, from Wine's own two lines (its err channel is on with Logging off):
 *   ...:err:module:import_dll Library X.dll (which is needed by L"C:\\...\\game.exe") not found
 *   ...:err:module:loader_init Importing dlls for L"C:\\...\\game.exe" failed, status c0000135
 * Wine shows no message box for it; on Windows the player sees "X.dll was not found". The title gives the launcher
 * that sentence as the session's end reason (native/proton_wine.cpp, output and return_to_launcher). A DLL missing for
 * a LoadLibrary the program survives names nothing: only the loader's failure after it does. Host test:
 * tests/test_pw_missing_dll.c. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct PwMissingDll {
    char dll[96];      /* the last library the loader could not find */
    char reason[256];  /* set once a program's imports failed: "game.exe could not start: X.dll was not found" */
} PwMissingDll;

/* The last path element of a Wine-quoted path (L"C:\\a\\b.exe"), up to the closing quote, into out. */
static inline void pw_missing_dll_name(const char *from, const char *end, char *out, size_t size)
{
    const char *quote = (const char *)memchr(from, '"', (size_t)(end - from));
    const char *stop = quote ? quote : end, *start = from;
    for (const char *p = from; p < stop; ++p) if (*p == '\\' || *p == '/') start = p + 1;
    size_t n = (size_t)(stop - start);
    if (n >= size) n = size - 1;
    memcpy(out, start, n);
    out[n] = 0;
}

static inline const char *pw_missing_dll_find(const char *text, size_t length, const char *what)
{
    const size_t w = strlen(what);
    for (size_t i = 0; i + w <= length; ++i) if (!memcmp(text + i, what, w)) return text + i + w;
    return NULL;
}

/* One chunk of Wine's output (one or more whole lines). Returns 1 when it set the reason. */
static inline int pw_missing_dll_line(PwMissingDll *state, const char *text, size_t length)
{
    const char *end = text + length, *at;
    if ((at = pw_missing_dll_find(text, length, ":import_dll Library "))) {
        const char *space = (const char *)memchr(at, ' ', (size_t)(end - at));
        size_t n = space ? (size_t)(space - at) : 0;
        if (n && n < sizeof(state->dll)) { memcpy(state->dll, at, n); state->dll[n] = 0; }
    }
    if ((at = pw_missing_dll_find(text, length, ":loader_init Importing dlls for L\"")) &&
        pw_missing_dll_find(at, (size_t)(end - at), "failed, status c0000135") && state->dll[0]) {
        char program[96];
        pw_missing_dll_name(at, end, program, sizeof(program));
        snprintf(state->reason, sizeof(state->reason), "%s could not start: %s was not found", program, state->dll);
        return 1;
    }
    return 0;
}
#endif
