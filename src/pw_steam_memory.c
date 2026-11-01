/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_steam_memory.h"

unsigned pw_steam_memory_need(int signed_in)
{
    return signed_in ? PW_STEAM_NEED_SIGNED_IN_MIB : PW_STEAM_NEED_INSTALL_MIB;
}

int pw_steam_memory_warn(const PwSteamCensus *census, int signed_in)
{
    return census && census->known && census->system_mib + pw_steam_memory_need(signed_in) > PW_STEAM_SWAP_POINT_MIB;
}
