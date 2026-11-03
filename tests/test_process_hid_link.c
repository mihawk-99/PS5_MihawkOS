/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Patch 2401's own DualSense link name, with patch 2480's Xbox gamepad (ntdll NtCreateFile): which names it takes and what it makes of them. The code
 * under test is extracted from patch 2480 (which holds the whole block) by tests/extract_process_rules.py; WCHAR is Wine's 16 bits here. */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <uchar.h>

typedef char16_t WCHAR;
#include "process_hid_link.inc"

static size_t own(const char16_t *name, unsigned int pid, char16_t *out, size_t capacity)
{
    size_t length = 0;
    while (name[length]) length++;
    return ps5_own_hid_link(name, length, pid, out, capacity);
}

static int same(const char16_t *a, size_t length, const char16_t *b)
{
    for (size_t i = 0; i < length; i++) if (a[i] != b[i]) return 0;
    return !b[length];
}

int main(void)
{
    static const char16_t link[] = u"\\??\\HID#VID_054C&PID_0CE6#256&PS5-LOCAL-1&0&0&1#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    char16_t out[256];
    size_t n = own(link, 0x4d8, out, 256);
    /* The process id in lower-case hex without leading zeros, as ntoskrnl's swprintf "%x" writes it. */
    assert(same(out, n, u"\\??\\HID#VID_054C&PID_0CE6#256&PS5-LOCAL-1&0&0&1#{4d1e55b2-f16f-11cf-88cb-001111000030}#4d8"));
    n = own(link, 0, out, 256);
    assert(out[n - 2] == u'#' && out[n - 1] == u'0');
    n = own(link, 0xffffffffu, out, 256);
    assert(same(out + n - 9, 9, u"#ffffffff"));
    /* A program may spell the name in another case; the server's lookup ignores case, and the spelling is kept. */
    n = own(u"\\??\\hid#vid_054c&pid_0ce6#x", 7, out, 256);
    assert(same(out, n, u"\\??\\hid#vid_054c&pid_0ce6#x#7"));
    /* Windows' HID gamepad for an Xbox controller (patch 2480), 360 or One, is a program's own too. */
    n = own(u"\\??\\HID#VID_045E&PID_028E&IG_00#256&PS5-LOCAL-1&0&0&0#{4d1e55b2-f16f-11cf-88cb-001111000030}", 0x2a, out, 256);
    assert(same(out, n, u"\\??\\HID#VID_045E&PID_028E&IG_00#256&PS5-LOCAL-1&0&0&0#{4d1e55b2-f16f-11cf-88cb-001111000030}#2a"));
    assert(own(u"\\??\\HID#VID_045E&PID_02EA&IG_00#x", 1, out, 256));
    /* Not the controller's link: another device, a path below the device, the prefix alone, a file. */
    assert(!own(u"\\??\\HID#VID_046D&PID_C52B#256&PS5-LOCAL-1&0&0&1#{4d1e55b2-f16f-11cf-88cb-001111000030}", 1, out, 256));
    assert(!own(u"\\??\\HID#VID_045E&PID_02", 1, out, 256));
    assert(!own(u"\\??\\HID#VID_045E&PID_028E&IG_00#x\\y", 1, out, 256));
    assert(!own(u"\\??\\HID#VID_054C&PID_0CE6#256&PS5-LOCAL-1&0&0&1#{4d1e55b2-f16f-11cf-88cb-001111000030}\\x", 1, out, 256));
    assert(!own(u"\\??\\HID#VID_054C&PID_0CE6#", 1, out, 256));
    assert(!own(u"\\??\\C:\\windows\\system32\\hid.dll", 1, out, 256));
    /* Room for the longest id or nothing. */
    size_t length = sizeof(link) / sizeof(link[0]) - 1;
    assert(!own(link, 1, out, length + 8) && own(link, 1, out, length + 9) == length + 2);
    return 0;
}
