/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The code cache's budget (wine/ps5/pw_wine_code_budget.h): who gets a fixed 64 MiB, who is raised and when. */
#include "../wine/ps5/pw_wine_code_budget.h"
#include <assert.h>
#include <stdio.h>

static PwCodeBudget budget;

/* count clusters of 16 KiB evicted and converted again, spread over span_ms starting at t0; how many raises it made. */
static unsigned thrash(uintptr_t base, unsigned count, uint64_t t0, uint64_t span_ms)
{
    unsigned raised = 0;
    uint64_t kib = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uintptr_t at = base + (uintptr_t)i * 16384;
        pw_code_budget_evicted(&budget, at);
        raised += pw_code_budget_converted(&budget, at, 16384, t0 + span_ms * i / (count ? count : 1), &kib);
    }
    return raised;
}
/* The same clusters evicted and converted again twice over: a cycle, which is what thrash is. */
static unsigned cycle(uintptr_t base, unsigned count, uint64_t t0, uint64_t span_ms)
{
    const unsigned first = thrash(base, count, t0, span_ms);
    return first + thrash(base, count, t0, span_ms);
}

int main(void)
{
    /* The list: by name, ignoring case, either separator; a bare name too; not a game's own launcher name. */
    assert(pw_code_budget_listed("C:\\Program Files (x86)\\Steam\\steam.exe"));
    assert(pw_code_budget_listed("C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win7x64\\SteamWebHelper.exe"));
    assert(pw_code_budget_listed("Z:/x/EpicGamesLauncher.exe") && pw_code_budget_listed("Battle.net Helper.exe"));
    assert(pw_code_budget_listed("GalaxyClient Helper.exe") && pw_code_budget_listed("upc.exe"));
    assert(!pw_code_budget_listed("C:\\Games\\game.exe") && !pw_code_budget_listed("Launcher.exe"));
    assert(!pw_code_budget_listed("steam.exe.bak") && !pw_code_budget_listed("xsteam.exe") && !pw_code_budget_listed(NULL));

    /* A listed browser helper: 96 MiB to start (avoids libcef boot thrash). Its cold churn raises nothing... */
    pw_code_budget_setup(&budget, NULL, NULL, "C:\\Program Files (x86)\\Steam\\steamwebhelper.exe");
    assert(budget.mode == PW_CODE_BUDGET_LISTED && budget.bytes == 96u << 20);
    assert(!thrash(0x140000000, 2000, 0, 5000) && budget.bytes == 96u << 20 && budget.reconverted_kib == 2000 * 16);
    /* ...but code cycling through it (Big Picture drawing in one process) raises it as a game's is. */
    assert(cycle(0x1a0000000, 64, 20000, 5000) == 1 && budget.bytes == 128u << 20 && budget.raises == 1);
    /* Non-browser listed clients (steam.exe) still start at 64 MiB. */
    pw_code_budget_setup(&budget, NULL, NULL, "C:\\Program Files (x86)\\Steam\\steam.exe");
    assert(budget.mode == PW_CODE_BUDGET_LISTED && budget.bytes == 64u << 20);

    /* A game: 64 MiB to start; light eviction (under 1 MiB re-converted in 10 s) raises nothing. */
    pw_code_budget_setup(&budget, NULL, NULL, "C:\\Games\\game.exe");
    assert(budget.mode == PW_CODE_BUDGET_ADAPTIVE && budget.bytes == 64u << 20);
    assert(!thrash(0x140000000, 60, 0, 9000) && budget.bytes == 64u << 20);
    /* ...and the window forgets it: another 60 a window later raise nothing either, even coming back again. */
    assert(!cycle(0x150000000, 60, 20000, 9000) && budget.bytes == 64u << 20);
    /* A cluster's second return counts only after it was given back again: converting it twice in a row is not one. */
    pw_code_budget_evicted(&budget, 0x1f0000000);
    assert(!pw_code_budget_converted(&budget, 0x1f0000000, 16384, 25000, NULL));
    const uint64_t once = budget.reconverted_kib;
    assert(!pw_code_budget_converted(&budget, 0x1f0000000, 16384, 25001, NULL) && budget.reconverted_kib == once);
    /* A phase change: 2 MiB of evicted code back once each within 10 s (a level's code after its loading code, Resident
     * Evil 3's 3.4 MiB as its loading ended) raises nothing, though every return is counted for the log. */
    const uint64_t before = budget.reconverted_kib;
    assert(!thrash(0x158000000, 128, 30000, 5000) && budget.bytes == 64u << 20 && budget.reconverted_kib == before + 2048);
    /* 1 MiB of clusters back a second time in 10 s: raised 32 MiB, once, then it needs a window of its own again. */
    assert(cycle(0x160000000, 64, 40000, 5000) == 1 && budget.bytes == 96u << 20 && budget.raises == 1);
    /* Sustained thrash climbs in steps to the cap and stops there; it is never lowered. */
    for (unsigned w = 0; w < 20; ++w) cycle(0x170000000 + (uintptr_t)w * 0x10000000, 80, 60000 + w * 10000ull, 4000);
    assert(budget.bytes == 256u << 20 && budget.raises == (256 - 64) / 32);

    /* A conversion of something never evicted is not a re-conversion. */
    pw_code_budget_setup(&budget, NULL, NULL, "game.exe");
    for (unsigned i = 0; i < 200; ++i) assert(!pw_code_budget_converted(&budget, 0x140000000 + i * 16384ull, 16384, i, NULL));
    assert(!budget.reconverted_kib);

    /* The user's setting wins for any program: N fixed (a game not raised), 0 off; nonsense is ignored. */
    pw_code_budget_setup(&budget, "16", NULL, "C:\\Games\\game.exe");
    assert(budget.mode == PW_CODE_BUDGET_SET && budget.bytes == 16u << 20);
    assert(!cycle(0x140000000, 500, 0, 5000) && budget.bytes == 16u << 20);
    pw_code_budget_setup(&budget, "0", NULL, "steam.exe");
    assert(budget.mode == PW_CODE_BUDGET_OFF && !budget.bytes);
    pw_code_budget_setup(&budget, "64x", NULL, "steam.exe");
    assert(budget.mode == PW_CODE_BUDGET_LISTED && budget.bytes == 64u << 20);
    pw_code_budget_setup(&budget, "9999", NULL, "game.exe");
    assert(budget.mode == PW_CODE_BUDGET_ADAPTIVE);
    /* A test's start (the forced case): an adaptive program starts there and is raised from it; a listed one ignores it. */
    pw_code_budget_setup(&budget, NULL, "16", "C:\\Games\\game.exe");
    assert(budget.mode == PW_CODE_BUDGET_ADAPTIVE && budget.bytes == 16u << 20);
    assert(!thrash(0x140000000, 64, 0, 5000) && cycle(0x148000000, 64, 0, 5000) == 1 && budget.bytes == 48u << 20);
    pw_code_budget_setup(&budget, NULL, "16", "steam.exe");
    assert(budget.mode == PW_CODE_BUDGET_LISTED && budget.bytes == 64u << 20);
    pw_code_budget_setup(&budget, NULL, "999", "game.exe");
    assert(budget.bytes == 64u << 20);
    /* No image known yet (before Wine has its command line): adaptive, at the start. */
    pw_code_budget_setup(&budget, NULL, NULL, NULL);
    assert(budget.mode == PW_CODE_BUDGET_ADAPTIVE && budget.bytes == 64u << 20);

    /* Large ~250 MB binary (~10,000 evicted 16 KiB clusters): real thrash still raises without hash collision loss. */
    pw_code_budget_setup(&budget, NULL, NULL, "C:\\Games\\large_game.exe");
    for (unsigned i = 0; i < 10000; ++i)
        pw_code_budget_evicted(&budget, 0x140000000ull + (uintptr_t)i * 16384);
    assert(cycle(0x140000000ull, 64, 10000, 5000) == 1 && budget.bytes == 96u << 20 && budget.raises == 1);

    puts("code budget: games and the listed clients raised in 32 MiB steps on 1 MiB coming back a second time in "
         "10 s up to 256 MiB, a phase change not raised, the user's setting first: ok");
    return 0;
}
