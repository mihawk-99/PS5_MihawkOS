/* SPDX-License-Identifier: GPL-3.0-or-later */
/* src/pw_steam_memory.h: the Steam memory warning's rule on the measured figures. */
#include "../src/pw_steam_memory.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* The Alpha 6 tester's console: 1,277 MB before MihawkOS. 1,277 + 500 = 1,777 still fits under the swap point, and their
     * install's end was not memory (the memory session, 2026-10-31: the title's descriptor table, fixed in rc4) */
    PwSteamCensus tester = { 1, 1277, 95, 74, 9, 0, 0, 0 };
    assert(pw_steam_memory_need(0) == 500 && pw_steam_memory_need(1) == 450);
    assert(!pw_steam_memory_warn(&tester, 0) && !pw_steam_memory_warn(&tester, 1));   /* 1,777 and 1,727: under 1,800 */
    /* The edge: a census of 1,300 fits a first install exactly; 1,301 warns. Signed in, 1,350 fits and 1,351 warns. */
    PwSteamCensus c = { 1, 1300, 90, 60, 8, 0, 0, 0 };
    assert(!pw_steam_memory_warn(&c, 0));
    c.system_mib = 1301;
    assert(pw_steam_memory_warn(&c, 0) && !pw_steam_memory_warn(&c, 1));
    c.system_mib = 1351;
    assert(pw_steam_memory_warn(&c, 1));
    /* The tester's download run: 890 MiB at the start: nothing to warn about */
    PwSteamCensus download = { 1, 890, 89, 54, 6, 0, 0, 0 };
    assert(!pw_steam_memory_warn(&download, 0) && !pw_steam_memory_warn(&download, 1));
    /* No census (no /data, or no figure for any process): never warn, whatever the figures say */
    PwSteamCensus none = { 0, 1700, 0, 0, 0, 0, 0, 0 };
    assert(!pw_steam_memory_warn(&none, 0) && !pw_steam_memory_warn(&none, 1) && !pw_steam_memory_warn(NULL, 0));
    /* The page tables: 2,000 free warns whatever the memory says; the need fits, one less warns; the fw 11.60 tester's
     * 2,642 (short for Alpha 8's Steam) fits the 8.1 one, its browser in the title; the base's ~5,900 fits; no reading
     * never warns on them */
    PwSteamCensus tables = { 1, 900, 80, 30, 6, 1, 8192, 2000 };
    assert(pw_steam_tables_short(&tables) && pw_steam_memory_warn(&tables, 0) && pw_steam_memory_warn(&tables, 1));
    tables.tables_free = 2642;
    assert(!pw_steam_tables_short(&tables) && !pw_steam_memory_warn(&tables, 0));
    tables.tables_free = PW_STEAM_NEED_TABLES;
    assert(!pw_steam_tables_short(&tables) && !pw_steam_memory_warn(&tables, 1));
    tables.tables_free = PW_STEAM_NEED_TABLES - 1;
    assert(pw_steam_memory_warn(&tables, 1));
    tables.tables_free = 5900;
    assert(!pw_steam_memory_warn(&tables, 0));
    tables.tables_known = 0, tables.tables_free = 100;
    assert(!pw_steam_tables_short(&tables) && !pw_steam_memory_warn(&tables, 0));
    /* memory short with page tables to spare still warns on memory, and the line is the memory's */
    PwSteamCensus both = { 1, 1400, 90, 60, 8, 1, 8192, 5900 };
    assert(pw_steam_memory_warn(&both, 0) && !pw_steam_tables_short(&both));
    /* The Steam session's note (PW_STEAM_TABLES_SHORT): read back; anything else leaves the census as it was */
    PwSteamCensus noted = { 0, 0, 0, 0, 0, 0, 0, 0 };
    assert(pw_steam_tables_parse("2000 8192\n", &noted) && noted.tables_known && noted.tables_free == 2000 &&
           noted.tables_total == 8192 && pw_steam_memory_warn(&noted, 1));
    PwSteamCensus untouched = { 0, 0, 0, 0, 0, 0, 0, 0 };
    assert(!pw_steam_tables_parse("9000 8192", &untouched) && !pw_steam_tables_parse("12", &untouched) &&
           !pw_steam_tables_parse("5 0", &untouched) && !pw_steam_tables_parse(NULL, &untouched) && !untouched.tables_known);
    puts("test_pw_steam_memory: ok");
    return 0;
}
