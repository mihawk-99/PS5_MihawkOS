/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2413: gdi32's SetPixelFormat, the pinned Wine's own opengl.c with the patch, in front of an opengl32 that
 * behaves as Mesa's WGL does: a window takes its pixel format once, and a second call, even for the same format, fails
 * (Mesa's DrvSetPixelFormat returns false once the window has a framebuffer). The stand-ins for LoadLibraryW and
 * GetProcAddress are the whole of the "driver". */
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"

static int format_of[4];            /* per fake window (HDC value 1..3) */
static int driver_set_calls;

static INT WINAPI mesa_get(HDC hdc) { return format_of[(UINT_PTR)hdc]; }
static BOOL WINAPI mesa_set(HDC hdc, INT fmt, const PIXELFORMATDESCRIPTOR *pfd)
{
    (void)pfd;
    driver_set_calls++;
    if (fmt < 1 || fmt > 30) return FALSE;
    if (format_of[(UINT_PTR)hdc]) return FALSE;           /* "SetPixelFormat must be called only once" */
    format_of[(UINT_PTR)hdc] = fmt;
    return TRUE;
}

/* Wine's msvcrt headers declare the C library's functions for the Windows runtime (another calling convention): none of
 * them is called here. Wine's msvcrt headers send printf to the Windows C runtime, which is not here: write(2) and no formatting. */
extern long write(int, const void *, unsigned long);
static unsigned long slen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void say(const char *text) { write(1, text, slen(text)); }
HMODULE WINAPI LoadLibraryW(LPCWSTR name) { (void)name; return (HMODULE)1; }
FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name)
{
    (void)module;
    if (same(name, "wglGetPixelFormat")) return (FARPROC)mesa_get;
    if (same(name, "wglSetPixelFormat")) return (FARPROC)mesa_set;
    return NULL;
}

static int bad;
static void expect(const char *what, int got, int want)
{
    if (got == want) return;
    say(what); say(": got "); say(got ? "nonzero" : "zero"); say(", wanted "); say(want ? "nonzero" : "zero"); say("\n");
    bad = 1;
}

int main(void)
{
    HDC window = (HDC)1, other = (HDC)2;
    expect("the first call sets the format", SetPixelFormat(window, 5, NULL), TRUE);
    expect("the window has it", GetPixelFormat(window), 5);
    int calls = driver_set_calls;
    expect("the same format again succeeds", SetPixelFormat(window, 5, NULL), TRUE);
    expect("...and does not reach the driver", driver_set_calls, calls);
    expect("another format on that window fails, as on Windows", SetPixelFormat(window, 6, NULL), FALSE);
    expect("...and the window keeps its own", GetPixelFormat(window), 5);
    expect("an invalid format is the driver's to refuse", SetPixelFormat(other, 0, NULL), FALSE);
    expect("a window without a format takes one", SetPixelFormat(other, 7, NULL), TRUE);
    expect("a format outside the driver's list fails", SetPixelFormat((HDC)3, 99, NULL), FALSE);
    if (!bad) say("pc5 setpixelformat passed\n");
    return bad;
}
