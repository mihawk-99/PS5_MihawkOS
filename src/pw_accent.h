/* SPDX-License-Identifier: GPL-3.0-or-later
 * The accent colour the MihawkOS shell draws with: the one the applied theme chose (Settings > Personalization >
 * Themes), kept where Windows keeps it (HKCU\Software\Microsoft\Windows\DWM AccentColor, 0xAABBGGRR), and its shades as
 * the prototype derives them from one colour (prototypes/windows11-shell js/core.js applyAccent: an HSL ramp, with
 * Windows' own shades for the default #0078d4). Fills use Dark1 in the light theme and Light2 in the dark one
 * (css/tokens.css AccentFillColorDefault). The colour arithmetic is plain C, host-tested (tests/test_pw_accent.c);
 * the registry part is Windows-only. Colours are 0x00RRGGBB here, COLORREF (0x00BBGGRR) at the Windows edge. */
#ifndef PW_ACCENT_H
#define PW_ACCENT_H

#define PW_ACCENT_DEFAULT 0x0078d4u

enum pw_accent_shade { PW_ACCENT_BASE, PW_ACCENT_LIGHT1, PW_ACCENT_LIGHT2, PW_ACCENT_LIGHT3,
                       PW_ACCENT_DARK1, PW_ACCENT_DARK2, PW_ACCENT_DARK3 };

/* hsl in degrees and percent, as the prototype's hexToHsl and hsl() */
static inline void pw_accent_to_hsl(unsigned rgb, double *h, double *s, double *l)
{
    double r = ((rgb >> 16) & 255) / 255.0, g = ((rgb >> 8) & 255) / 255.0, b = (rgb & 255) / 255.0;
    double max = r > g ? (r > b ? r : b) : (g > b ? g : b), min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *h = 0; *s = 0; *l = (max + min) / 2;
    if (max != min)
    {
        double d = max - min;
        *s = *l > 0.5 ? d / (2 - max - min) : d / (max + min);
        *h = max == r ? (g - b) / d + (g < b ? 6 : 0) : max == g ? (b - r) / d + 2 : (r - g) / d + 4;
        *h *= 60;
    }
    *s *= 100; *l *= 100;
}

static inline unsigned pw_accent_from_hsl(double h, double s, double l)
{
    double c, x, m, r = 0, g = 0, b = 0, hp, hm, d;
    if (s > 100) s = 100;
    if (l < 0) l = 0;
    if (l > 100) l = 100;
    s /= 100; l /= 100;
    d = 2 * l - 1;
    c = (1 - (d < 0 ? -d : d)) * s;
    hp = h / 60;
    hm = hp - 2 * (int)(hp / 2) - 1;   /* (h / 60) mod 2, less one */
    x = c * (1 - (hm < 0 ? -hm : hm));
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    m = l - c / 2;
    return ((unsigned)((r + m) * 255 + 0.5) << 16) | ((unsigned)((g + m) * 255 + 0.5) << 8) | (unsigned)((b + m) * 255 + 0.5);
}

static inline unsigned pw_accent_shade(unsigned base, enum pw_accent_shade shade)
{
    static const unsigned windows_default[] = {0x0078d4, 0x0091f8, 0x60cdff, 0x99ebff, 0x005fb8, 0x004a99, 0x003e92};
    static const double ds[] = {0, 8, 12, 14, 0, 0, 0}, dl[] = {0, 12, 27, 38, -7, -17, -28};
    double h, s, l;
    if (base == PW_ACCENT_DEFAULT) return windows_default[shade];
    if (shade == PW_ACCENT_BASE) return base;
    pw_accent_to_hsl(base, &h, &s, &l);
    return pw_accent_from_hsl(h, s + ds[shade], l + dl[shade]);
}

/* the shade fills use in the theme: Dark1 light, Light2 dark */
static inline unsigned pw_accent_fill(unsigned base, int dark)
{
    return pw_accent_shade(base, dark ? PW_ACCENT_LIGHT2 : PW_ACCENT_DARK1);
}

#ifdef _WIN32
/* The applied accent, 0x00RRGGBB: the stored AccentColor, else Windows' default. */
static inline unsigned pw_accent_current(void)
{
    DWORD value = 0, size = sizeof(value), type = 0;
    HKEY key;
    unsigned rgb = PW_ACCENT_DEFAULT;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(key, L"AccentColor", NULL, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && size == sizeof(value))
            rgb = ((value & 0xff) << 16) | (value & 0xff00) | ((value >> 16) & 0xff);
        RegCloseKey(key);
    }
    return rgb;
}

/* Stores a theme's accent as Windows does (AccentColor and ColorizationColor) and tells the running programs, which
 * reload their colours on WM_SETTINGCHANGE "ImmersiveColorSet" as Windows' do. */
static inline BOOL pw_accent_store(unsigned rgb)
{
    DWORD abgr = 0xff000000u | ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
    DWORD argb = 0xc4000000u | rgb;
    HKEY key;
    BOOL okay;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, NULL, 0, KEY_SET_VALUE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
        return FALSE;
    okay = RegSetValueExW(key, L"AccentColor", 0, REG_DWORD, (const BYTE *)&abgr, sizeof(abgr)) == ERROR_SUCCESS &&
           RegSetValueExW(key, L"ColorizationColor", 0, REG_DWORD, (const BYTE *)&argb, sizeof(argb)) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (okay)
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 2000, NULL);
    return okay;
}

static inline COLORREF pw_accent_colorref(unsigned rgb)
{
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}
#endif
#endif
