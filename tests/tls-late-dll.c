/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A DLL with static TLS (a TLS directory, as __declspec(thread) data makes), for tests/tls-late-probe.c: loaded after
 * a program's threads started, Wine must give each of them a block for it (ProcessTlsInformation). */
#include <windows.h>
#include <winternl.h>

ULONG _tls_index;
__attribute__((section(".tls$AAA"))) char _tls_start = 0;
__attribute__((section(".tls$BBB"))) LONG tls_value = 0x7151;   /* the template every thread's block starts from */
__attribute__((section(".tls$ZZZ"))) char _tls_end = 0;
const IMAGE_TLS_DIRECTORY64 _tls_used = {(ULONGLONG)&_tls_start, (ULONGLONG)&_tls_end, (ULONGLONG)&_tls_index, 0, 0, 0};

/* The calling thread's copy of tls_value, read as compiled code reads TLS: the TEB's vector at _tls_index. -1 when the
 * thread has no vector, -2 when the vector has no block for this DLL. */
__declspec(dllexport) LONG tls_late_value(void)
{
    void **vector = NtCurrentTeb()->ThreadLocalStoragePointer;
    char *block;
    if (!vector) return -1;
    if (!(block = vector[_tls_index])) return -2;
    return *(LONG *)(block + ((char *)&tls_value - &_tls_start));
}

BOOL WINAPI DllMainCRTStartup(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
