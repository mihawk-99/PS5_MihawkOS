/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_code_budget.h"
#include <stdlib.h>
#include <string.h>

/* The listed clients: large browser-based programs whose code is mostly cold, so 64 MiB costs them nothing visible
 * (Steam: fmem-steam-b64-2026-10-25-01, fmem-isaac-b64-2026-10-25-01). A name too generic to be only a launcher's
 * (Rockstar's Launcher.exe, another launcher's Agent.exe) is left out: a game of that name would be held to 64 MiB. */
static const char *const listed[] = {
    "steam.exe", "steamwebhelper.exe", "steamservice.exe", "steamerrorreporter.exe",  /* Steam and its CEF browser */
    "epicgameslauncher.exe", "epicwebhelper.exe",                                     /* Epic Games Launcher, its CEF */
    "battle.net.exe", "battle.net helper.exe",                                        /* Battle.net, its CEF */
    "galaxyclient.exe", "galaxyclient helper.exe", "galaxycommunication.exe",         /* GOG Galaxy, its CEF, service */
    "eadesktop.exe", "eabackgroundservice.exe",                                       /* the EA app and its service */
    "upc.exe", "ubisoftconnect.exe", "uplaywebcore.exe",                              /* Ubisoft Connect, its CEF */
    "rockstarservice.exe", "socialclubhelper.exe",                                    /* Rockstar's service, its CEF */
    "qtwebengineprocess.exe",                                                         /* Chromium in Qt: the EA app's */
    "cefsharp.browsersubprocess.exe", "msedgewebview2.exe",                           /* launchers' embedded browsers */
};

static const char *const browsers[] = {
    "steamwebhelper.exe", "epicwebhelper.exe", "battle.net helper.exe", "galaxyclient helper.exe",
    "uplaywebcore.exe", "socialclubhelper.exe", "qtwebengineprocess.exe",
    "cefsharp.browsersubprocess.exe", "msedgewebview2.exe",
};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

static int matches_any(const char *image, const char *const *table, size_t count)
{
    const char *name = image;
    if (!image) return 0;
    for (const char *at = image; *at; ++at)
        if (*at == '\\' || *at == '/') name = at + 1;
    for (size_t i = 0; i < count; ++i) {
        const char *a = name, *b = table[i];
        while (*a && lower((unsigned char)*a) == *b) ++a, ++b;
        if (!*a && !*b) return 1;
    }
    return 0;
}

int pw_code_budget_listed(const char *image)
{
    return matches_any(image, listed, sizeof(listed) / sizeof(listed[0]));
}

static long number(const char *text, long low, long high)
{
    char *end = NULL;
    const long value = text && *text ? strtol(text, &end, 10) : -1;
    return text && *text && !*end && value >= low && value <= high ? value : -1;
}

void pw_code_budget_setup(PwCodeBudget *budget, const char *setting, const char *start, const char *image)
{
    const long mib = number(setting, 0, 4096), first = number(start, 1, PW_CODE_BUDGET_MAX_MIB);

    memset(budget, 0, sizeof(*budget));
    if (mib >= 0) {
        budget->mode = mib ? PW_CODE_BUDGET_SET : PW_CODE_BUDGET_OFF;
        budget->bytes = (uint64_t)mib << 20;
        return;
    }
    budget->mode = pw_code_budget_listed(image) ? PW_CODE_BUDGET_LISTED : PW_CODE_BUDGET_ADAPTIVE;
    if (budget->mode == PW_CODE_BUDGET_LISTED && matches_any(image, browsers, sizeof(browsers) / sizeof(browsers[0]))) {
        budget->bytes = (uint64_t)PW_CODE_BUDGET_BROWSER_START_MIB << 20;
        return;
    }
    budget->bytes = (uint64_t)(budget->mode == PW_CODE_BUDGET_ADAPTIVE && first > 0 ? first : PW_CODE_BUDGET_START_MIB) << 20;
}

static unsigned slot(uintptr_t at) { return (unsigned)((at >> 14) * 2654435761u) % PW_CODE_BUDGET_EVICTED; }

/* A slot holds the cluster's address (16 KiB aligned) and two flags in its low bits. */
enum { GIVEN_BACK = 1, RETURNED = 2, FLAGS = 3 };

void pw_code_budget_evicted(PwCodeBudget *budget, uintptr_t at)
{
    const uintptr_t was = budget->evicted[slot(at)];
    /* A cluster that returned before keeps saying so: its next return is a cycle. */
    budget->evicted[slot(at)] = at | ((was & ~(uintptr_t)FLAGS) == at ? (was & RETURNED) : 0) | GIVEN_BACK;
}

int pw_code_budget_converted(PwCodeBudget *budget, uintptr_t at, size_t bytes, uint64_t now_ms, uint64_t *window_kib)
{
    const unsigned s = slot(at);
    const uint32_t kib = (uint32_t)(bytes >> 10);

    const uintptr_t was = budget->evicted[s];

    if (!at || (was & ~(uintptr_t)FLAGS) != at || !(was & GIVEN_BACK)) return 0;
    budget->evicted[s] = at | RETURNED;
    budget->reconverted_kib += kib;
    /* A first return is a phase change (a level's code after its loading code), not thrash: only a cluster coming back
     * again counts (fmem-re3-budget-64-2026-10-26-01). */
    if ((budget->mode != PW_CODE_BUDGET_ADAPTIVE && budget->mode != PW_CODE_BUDGET_LISTED) || !(was & RETURNED)) return 0;
    /* Forget what fell out of the window, then count this one. */
    while (budget->sample_count) {
        const unsigned oldest = (budget->sample_head + PW_CODE_BUDGET_SAMPLES - budget->sample_count) % PW_CODE_BUDGET_SAMPLES;
        if (budget->sample_ms[oldest] + PW_CODE_BUDGET_WINDOW_MS > now_ms && budget->sample_count < PW_CODE_BUDGET_SAMPLES) break;
        budget->window_kib -= budget->sample_kib[oldest];
        budget->sample_count--;
    }
    budget->sample_ms[budget->sample_head] = now_ms;
    budget->sample_kib[budget->sample_head] = kib;
    budget->sample_head = (budget->sample_head + 1) % PW_CODE_BUDGET_SAMPLES;
    budget->sample_count++;
    budget->window_kib += kib;
    if (budget->window_kib < PW_CODE_BUDGET_THRASH_KIB || budget->bytes >= ((uint64_t)PW_CODE_BUDGET_MAX_MIB << 20))
        return 0;
    if (window_kib) *window_kib = budget->window_kib;
    budget->bytes += (uint64_t)PW_CODE_BUDGET_STEP_MIB << 20;
    if (budget->bytes > ((uint64_t)PW_CODE_BUDGET_MAX_MIB << 20)) budget->bytes = (uint64_t)PW_CODE_BUDGET_MAX_MIB << 20;
    budget->raises++;
    budget->sample_count = 0;   /* the next raise needs a full window of its own */
    budget->window_kib = 0;
    return 1;
}
