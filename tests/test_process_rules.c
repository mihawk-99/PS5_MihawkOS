/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Patch 2070's WINE_PS5_PROCESS_RULES (kernelbase CreateProcessInternalW): the parsing, and which program a rule
 * matches. The code under test is extracted from the patch by tests/extract_process_rules.py; Windows' wide strings are
 * the host's wchar_t here, and the few Windows calls it makes are stand-ins. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

typedef wchar_t WCHAR;
typedef unsigned long DWORD;
typedef long LONG;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define InterlockedCompareExchange(p, n, c) __sync_val_compare_and_swap((p), (c), (n))
#define InterlockedExchange(p, v) __sync_lock_test_and_set((p), (v))
#define GetProcessHeap() NULL
static void *blocks[16];   /* what the rules point into lives for the process; a test that starts over frees it */
static void *test_alloc(size_t n)
{
    for (size_t i = 0; i < 16; i++)
        if (!blocks[i]) return blocks[i] = malloc(n);
    abort();
}
static void test_free(void *p)
{
    for (size_t i = 0; i < 16; i++)
        if (blocks[i] == p) blocks[i] = NULL;
    free(p);
}
#define HeapAlloc(h, f, n) test_alloc(n)
#define HeapFree(h, f, p) test_free(p)
static void Sleep(DWORD ms) { (void)ms; }
#define wcsicmp wcscasecmp
#define wcsnicmp wcsncasecmp

static const WCHAR *environment;   /* the rules as the test sets them; NULL: the variable is not there */
static const WCHAR *hold_switch;   /* WINE_PS5_PROCESS_HOLD as the test sets it; NULL: not there */
static DWORD GetEnvironmentVariableW(const WCHAR *name, WCHAR *out, DWORD size)
{
    const int hold = !wcscmp(name, L"WINE_PS5_PROCESS_HOLD");
    assert(hold || !wcscmp(name, L"WINE_PS5_PROCESS_RULES"));
    const WCHAR *value = hold ? hold_switch : environment;
    if (!value) return 0;
    const DWORD needed = (DWORD)wcslen(value) + 1;
    if (size < needed) return needed;
    wcscpy(out, value);
    return needed - 1;
}

#include "process_rules.inc"
#include "../src/pw_steam.h"

/* The state is read once per process; a test of another environment starts it over. */
static void reset(const WCHAR *rules)
{
    for (size_t i = 0; i < 16; i++) { free(blocks[i]); blocks[i] = NULL; }
    environment = rules;
    ps5_rule_count = 0;
    ps5_rules_state = 0;
    ps5_rules_read();
}

int main(void)
{
    /* Without the variable: nothing matches, nothing is read twice. */
    reset(NULL);
    assert(!ps5_process_rule(L"C:\\Program Files\\Tool\\tool.exe", L"run") && !ps5_rule_count && ps5_rules_state == 2);

    /* Two rules: a program run in place of another, and flags added to a third. */
    reset(L"probe.exe|run|C:\\windows\\system32\\hostname.exe;helper.exe|add| --in-process-gpu --limit=1;noisy.exe|fail|");
    assert(ps5_rule_count == 3);
    assert(ps5_process_rule(L"C:\\Tool\\noisy.exe", L"fail") && !*ps5_process_rule(L"C:\\Tool\\noisy.exe", L"fail"));   /* no value needed */
    assert(!ps5_process_rule(L"C:\\Tool\\noisy.exe", L"run") && !ps5_process_rule(L"C:\\Tool\\quiet.exe", L"fail"));
    assert(!wcscmp(ps5_process_rule(L"C:\\Program Files\\Tool\\probe.exe", L"run"), L"C:\\windows\\system32\\hostname.exe"));
    assert(!wcscmp(ps5_process_rule(L"c:\\program files\\tool\\PROBE.EXE", L"run"), L"C:\\windows\\system32\\hostname.exe"));
    assert(!ps5_process_rule(L"C:\\Program Files\\Tool\\probe.exe", L"add"));                  /* the action must match */
    assert(!wcscmp(ps5_process_rule(L"\"C:\\Program Files\\Tool\\helper.exe\" --type=renderer --x", L"add"),
                   L" --in-process-gpu --limit=1"));                                          /* a quoted command line */
    assert(!wcscmp(ps5_process_rule(L"C:\\Tool\\helper.exe --type=gpu", L"add"), L" --in-process-gpu --limit=1"));
    assert(!wcscmp(ps5_process_rule(L"helper.exe", L"add"), L" --in-process-gpu --limit=1"));  /* a bare name */

    /* A whole path component only, ending at the end, a space or a quote. */
    assert(!ps5_process_rule(L"C:\\Tool\\myprobe.exe", L"run"));
    assert(!ps5_process_rule(L"C:\\Tool\\probe.exe.bak", L"run"));
    assert(!ps5_process_rule(L"C:\\Tool\\probe.exe2", L"run"));
    assert(ps5_process_rule(L"C:\\Tool\\probe.exe /q", L"run"));
    assert(ps5_process_rule(L"C:/Tool/probe.exe", L"run"));
    assert(!ps5_process_rule(NULL, L"run"));

    /* Malformed and empty rules are skipped, the rest still work; more than the table holds are dropped. */
    reset(L";nobars;a.exe|run;|run|x;ok.exe|run|C:\\y.exe;;");
    assert(ps5_rule_count == 1 && !wcscmp(ps5_process_rule(L"ok.exe", L"run"), L"C:\\y.exe"));
    {
        WCHAR many[16 * 24 + 64] = L"";
        for (int i = 0; i < 20; i++) {
            WCHAR rule[24];
            swprintf(rule, ARRAY_SIZE(rule), L"p%d.exe|run|v%d;", i, i);
            wcscat(many, rule);
        }
        reset(many);
        assert(ps5_rule_count == ARRAY_SIZE(ps5_rules) && !wcscmp(ps5_process_rule(L"p15.exe", L"run"), L"v15"));
        assert(!ps5_process_rule(L"p16.exe", L"run"));
    }
    /* The rules the Steam option writes (src/pw_steam.c), read by this parser: every rule arrives, and each fires on its
     * program and on nothing the client cannot run without. */
    {
        char text[1024];
        WCHAR wide[1024];
        assert(pw_steam_rules(text, sizeof(text)));
        for (size_t i = 0; text[i]; i++) wide[i] = (unsigned char)text[i];
        wide[strlen(text)] = 0;
        reset(wide);
        assert(ps5_rule_count == pw_steam_rule_count());
        const WCHAR *web = ps5_process_rule(L"\"C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe\" -lang=en_US", L"add");
        assert(web && wcsstr(web, L"--single-process") && wcsstr(web, L"--disable-background-networking"));
        assert(ps5_process_rule(L"C:\\Program Files (x86)\\Steam\\bin\\gldriverquery64.exe", L"fail"));
        assert(ps5_process_rule(L".\\bin\\vulkandriverquery.exe", L"fail"));
        assert(ps5_process_rule(L"C:\\Program Files (x86)\\Steam\\bin\\hardwareupdater\\hardwareupdater.exe --check-for-updates", L"fail"));
        assert(ps5_process_rule(L"\"C:\\Program Files (x86)\\Steam\\bin\\gameoverlayui64.exe\" -steampid=1", L"fail"));
        const WCHAR *install = ps5_process_rule(L"\"C:\\Program Files (x86)\\Steam\\bin\\SteamService.exe\" /installscript x", L"run");
        assert(install && !wcscmp(install, L"C:\\windows\\system32\\hostname.exe"));
        /* Patch 2104: the browser is held while a library game runs; the game is any program under steamapps\\common. */
        const WCHAR *hold = ps5_process_rule(L"C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe", L"hold");
        assert(hold && !wcscmp(hold, L"\\steamapps\\common\\"));
        assert(ps5_hold_is_game(L"C:\\users\\Mihawk\\Games\\Aurelia\\steamapps\\common\\The Binding of Isaac Rebirth\\isaac-ng.exe"));
        assert(ps5_hold_is_game(L"D:\\SteamLibrary\\SteamApps\\Common\\Game\\game.exe"));   /* any case, any library */
        assert(!ps5_hold_is_game(L"C:\\Program Files (x86)\\Steam\\steam.exe"));
        assert(!ps5_hold_is_game(L"C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe"));
        assert(!ps5_hold_is_game(L"C:\\Program Files (x86)\\Steam\\steamapps\\downloading\\1\\setup.exe"));
        assert(!ps5_process_rule(L"C:\\users\\Mihawk\\Games\\Aurelia\\steamapps\\common\\Game\\game.exe", L"hold"));
        assert(!ps5_hold_off());
        hold_switch = L"0";
        assert(ps5_hold_off());
        hold_switch = L"1";
        assert(!ps5_hold_off());
        hold_switch = NULL;
        /* The client, its service and its game are left alone. */
        static const WCHAR *const actions[] = { L"fail", L"run", L"add" };
        for (unsigned i = 0; i < 3; i++) {
            assert(!ps5_process_rule(L"C:\\Program Files (x86)\\Steam\\steam.exe -silent", actions[i]));
            assert(!ps5_process_rule(L"C:\\Program Files (x86)\\Common Files\\Steam\\steamservice.exe /RunAsService", actions[i]));
            assert(!ps5_process_rule(L"C:\\users\\Mihawk\\Games\\App\\game.exe", actions[i]));
        }
    }
    /* No hold rule: nothing is a game, and an empty part matches nothing. */
    reset(L"a.exe|fail|");
    assert(!ps5_hold_is_game(L"C:\\x\\steamapps\\common\\y.exe"));
    assert(!ps5_path_has(L"C:\\x", L"") && !ps5_path_has(NULL, L"x") && ps5_path_has(L"abc", L"B"));
    reset(NULL);
    return puts("process rules: parsing, matching by whole file name, actions, malformed rules, the table limit"), 0;
}
