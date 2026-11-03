/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_steam_memory.h"
#include <stdio.h>

unsigned pw_steam_memory_need(int signed_in)
{
    return signed_in ? PW_STEAM_NEED_SIGNED_IN_MIB : PW_STEAM_NEED_INSTALL_MIB;
}

int pw_steam_tables_short(const PwSteamCensus *census)
{
    return census && census->tables_known && census->tables_free < PW_STEAM_NEED_TABLES;
}

int pw_steam_memory_warn(const PwSteamCensus *census, int signed_in)
{
    return pw_steam_tables_short(census) ||
           (census && census->known && census->system_mib + pw_steam_memory_need(signed_in) > PW_STEAM_SWAP_POINT_MIB);
}

int pw_steam_tables_parse(const char *text, PwSteamCensus *census)
{
    unsigned free_tables = 0, total = 0;
    if (!text || !census || sscanf(text, "%u %u", &free_tables, &total) != 2 || !total || free_tables > total) return 0;
    census->tables_known = 1, census->tables_free = free_tables, census->tables_total = total;
    return 1;
}
