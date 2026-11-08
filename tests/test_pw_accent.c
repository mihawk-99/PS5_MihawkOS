/* SPDX-License-Identifier: GPL-3.0-or-later
 * src/pw_accent.h against the prototype's accent ramp (js/core.js applyAccent, CSS hsl()): each theme's accent and
 * shade, computed independently with Python's colorsys when this test was written, within one level a channel;
 * Windows' own shades for the default #0078d4; the fill shade per theme. */
#include <stdio.h>
#include "../src/pw_accent.h"

static const struct { unsigned base; enum pw_accent_shade shade; unsigned want; } cases[] = {
    {0x8764b8, PW_ACCENT_LIGHT1, 0xa687d2},
    {0x8764b8, PW_ACCENT_LIGHT2, 0xcfbde9},
    {0x8764b8, PW_ACCENT_LIGHT3, 0xeee7f7},
    {0x8764b8, PW_ACCENT_DARK1, 0x744eaa},
    {0x8764b8, PW_ACCENT_DARK2, 0x5d3e87},
    {0x8764b8, PW_ACCENT_DARK3, 0x422c61},
    {0x038387, PW_ACCENT_LIGHT1, 0x00c1c7},
    {0x038387, PW_ACCENT_LIGHT2, 0x15f8ff},
    {0x038387, PW_ACCENT_LIGHT3, 0x4dfaff},
    {0x038387, PW_ACCENT_DARK1, 0x026164},
    {0x038387, PW_ACCENT_DARK2, 0x013132},
    {0x038387, PW_ACCENT_DARK3, 0x000000},
    {0xca5010, PW_ACCENT_LIGHT1, 0xf76a20},
    {0xca5010, PW_ACCENT_LIGHT2, 0xfd9a67},
    {0xca5010, PW_ACCENT_LIGHT3, 0xffbf9d},
    {0xca5010, PW_ACCENT_DARK1, 0xa9430d},
    {0xca5010, PW_ACCENT_DARK2, 0x7a300a},
    {0xca5010, PW_ACCENT_DARK3, 0x461c06},
    {0x6b69d6, PW_ACCENT_LIGHT1, 0x9594e8},
    {0x6b69d6, PW_ACCENT_LIGHT2, 0xd3d2f7},
    {0x6b69d6, PW_ACCENT_LIGHT3, 0xffffff},
    {0x6b69d6, PW_ACCENT_DARK1, 0x4f4dce},
    {0x6b69d6, PW_ACCENT_DARK2, 0x3432b6},
    {0x6b69d6, PW_ACCENT_DARK3, 0x28268a}
};

static int near(unsigned a, unsigned b)
{
    for (int k = 0; k < 24; k += 8) { int d = (int)((a >> k) & 255) - (int)((b >> k) & 255); if (d < -1 || d > 1) return 0; }
    return 1;
}

int main(void)
{
    int failures = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        unsigned got = pw_accent_shade(cases[i].base, cases[i].shade);
        if (!near(got, cases[i].want)) { printf("FAIL %06x shade %d: %06x, want %06x\n", cases[i].base, cases[i].shade, got, cases[i].want); failures++; }
    }
    if (pw_accent_shade(PW_ACCENT_DEFAULT, PW_ACCENT_DARK1) != 0x005fb8 || pw_accent_shade(PW_ACCENT_DEFAULT, PW_ACCENT_LIGHT2) != 0x60cdff)
        { printf("FAIL the default's Windows shades\n"); failures++; }
    if (pw_accent_fill(0xca5010, 0) != pw_accent_shade(0xca5010, PW_ACCENT_DARK1) || pw_accent_fill(0xca5010, 1) != pw_accent_shade(0xca5010, PW_ACCENT_LIGHT2))
        { printf("FAIL the fill shade per theme\n"); failures++; }
    if (pw_accent_shade(0x8764b8, PW_ACCENT_BASE) != 0x8764b8) { printf("FAIL the base is itself\n"); failures++; }
    printf("pw_accent: %s (%u cases)\n", failures ? "FAIL" : "ok", (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return failures != 0;
}
