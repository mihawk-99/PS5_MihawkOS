/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wine patch 2422: ps5_no_debugger, cut from the patch (no_debugger.inc), against a fake environment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef long LONG;
typedef int BOOL;
typedef unsigned short WCHAR;
typedef size_t SIZE_T;
typedef long NTSTATUS;
#define STATUS_VARIABLE_NOT_FOUND ((NTSTATUS)0xC0000100)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TRUE 1
#define FALSE 0

static const char *environment_value;   /* WINE_PS5_NTDLL_DIR, or NULL when the variable is not set */
static unsigned queries;
static LONG InterlockedExchange(LONG *target, LONG value) { LONG old = *target; *target = value; return old; }
static NTSTATUS RtlQueryEnvironmentVariable(void *env, const wchar_t *name, size_t name_len, WCHAR *value, size_t capacity,
                                            SIZE_T *len)
{
    (void)env; (void)value; (void)capacity;
    ++queries;
    if (name_len != 18 || wcscmp(name, L"WINE_PS5_NTDLL_DIR")) return STATUS_VARIABLE_NOT_FOUND;
    if (!environment_value) return STATUS_VARIABLE_NOT_FOUND;
    *len = strlen(environment_value);
    return *len > capacity ? STATUS_BUFFER_TOO_SMALL : 0;
}

#include "no_debugger.inc"

int main(void)
{
    int bad = 0;
    /* a title: the variable is set (to a path, so longer than the buffer the function offers) */
    environment_value = "/data/ps5proton/runtime/ge11-7/lib/wine/x86_64-unix";
    if (!ps5_no_debugger() || !ps5_no_debugger()) { puts("a title was not recognised"); bad = 1; }
    if (queries != 1) { printf("asked the environment %u times, not once\n", queries); bad = 1; }
    puts(bad ? "pw no debugger FAILED" : "pw no debugger passed");
    return bad;
}
