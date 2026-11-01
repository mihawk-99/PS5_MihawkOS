/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_INHERIT_H
#define PW_WINE_INHERIT_H
#include <stddef.h>
#include <string.h>
/* What a helper inherits of its spawner's environment (pw_wine_process.c's spawn paths; host test:
 * tests/test_pw_wine_inherit.c).
 *
 * The desktop and display (WINE_PS5_DESKTOP, without which a helper's Wine reported an 800x600 monitor), Direct3D's and
 * DXVK's and VKD3D-Proton's settings, the CPU set, .NET's and SDL's, and the debug switches of the helper's RADV and
 * its compiler (RADV_DEBUG, RADV_PERFTEST, ACO_DEBUG; unset unless a test sets them; RADV_DEBUG=shaders still printed
 * nothing in a helper, zink-sprite-2026-10-06-08, which is not understood). Mesa's Gallium and Zink switches and its
 * log, for a helper's OpenGL (Mesa's WGL over Zink): GALLIUM_DRIVER the title sets for it, ZINK_DESCRIPTORS,
 * GALLIUM_THREAD or ZINK_DEBUG a test sets (before them, a test's ZINK_DESCRIPTORS=lazy never reached Mesa,
 * zink-sprite-2026-10-06-03). Not the rest of MESA_: the title's shader cache folder is a native path for its own RADV.
 * Not what the loader sets for itself (the server socket, the child markers, the GPU broker's and input page's
 * descriptors, the open-file threshold) nor what is the title's alone (its standard output file). The helper
 * scheduling settings reach every helper and the memory helper, whose loaders apply them. The GPU broker client's
 * switches live in the helper that presents: WINE_PS5_ASYNC_PRESENT (graphics patch 0024) and WINE_PS5_PRESENT_LINK
 * (0026); set only in the title, they never reached the client before they were named here. The language the
 * guided setup chose for the prefix (WINE_PS5_LOCALE): every Windows program but the shell's Explorer takes it (patch
 * 1230). The controller the player chose (WINE_PS5_CONTROLLER, src/pw_controller.h), which xinput reads in each
 * program (patch 2400). A game's own Game Mode (WINE_PS5_GAME_MODE, src/pw_game_mode.h), so the game a launcher starts
 * keeps the choice its library entry made. The game's Steam ids (SteamGameId, SteamAppId): Proton's Wine changes
 * behaviour for a game by them on its Unix side too, so a program the game starts has them as the game has. */
static inline int pw_wine_inherited(const char *entry)
{
    static const char *const names[]={"WINE_PS5_DESKTOP=","WINE_PS5_VIEW=","WINE_PS5_FIRMWARE=","WINE_PS5_WINDOW_SURFACES=","WINE_PS5_REFRESH=","WINE_PS5_HID=","WINE_PS5_CONTROLLER=","WINE_PS5_GAME_MODE=","WINE_PS5_LOCALE=",
        "WINE_PS5_HELPER_SCHED=","WINE_PS5_HELPER_PRIORITY=","WINE_PS5_HELPER_CPUS=",
        "WINE_PS5_HELPER_CPUS_WIDEN=",   /* the loader places the helper on the title's CPUs (src/pw_cpu_placement.c) */
        "WINE_PS5_ASYNC_PRESENT=",   /* the broker WSI presents from its own thread (graphics patch 0024) */
        "WINE_PS5_PRESENT_LINK=",    /* presents over a connection of their own (graphics patch 0026) */
        "WINE_PS5_SCHED_CENSUS=","WINE_PS5_CENSUS_MS=","WINE_PS5_EARLY=","WINE_PS5_VMMAP=","WINE_PS5_MAPLOG_MIB=","WINE_PS5_SAMPLE=","WINE_PS5_MALLOC_TRACE=","WINE_PS5_FILE_TRACE=",   /* the helper's census budgets and its once-a-second early memory line (native/proton_wine_child.c) */
        "WINE_PS5_DSECTION_MIN_MIB=","WINE_PS5_TITLE_PROCESS=","WINE_PS5_TITLE_PID=","WINE_PS5_TITLE_GAMES=","WINE_PS5_TITLE_OPTOUT=","WINE_PS5_TITLE_PICK=","WINE_PS5_TITLE_LISTED=","WINE_PS5_CHROMIUM_ARGS=",   /* the title and its helpers agree on which sections are direct memory */
        "WINE_PS5_CODE_BUDGET_MIB=","WINE_PS5_CODE_BUDGET_START_MIB=",   /* the code cache's budget and a test's start (docs/FLEXIBLE_MEMORY.md) */
        "WINE_PS5_PAYLOAD_DMEM=","WINE_PS5_PRX_LOADER=","WINE_PS5_NATIVE_SIGNALS=","WINEARCH=",   /* wow64: a 32-bit program runs in its helper (docs/IA32_DESIGN.md, M3) */
        "WINETEST_",  /* watchdog, slice and threading controls must reach payload conformance tests */
        "WINE_PS5_OPENGL=","WINE_D3D_CONFIG=","WINE_CPU_TOPOLOGY=","WINEESYNC=","WINEFSYNC=","DXVK_","VKD3D_",
        "SteamGameId=","SteamAppId=",   /* the game's Steam ids (src/pw_steam_appid.h): what its children inherit, as under Proton */
        "DOTNET_","SDL_","WINE_ADDITIONAL_CERTS_DIR=","WINE_PS5_DNS=","WINE_PS5_SERVICES=","RADV_DEBUG=","RADV_PERFTEST=",
        "ACO_DEBUG=","GALLIUM_","ZINK_","MESA_LOG="};
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);++i)
        if(!strncmp(entry,names[i],strlen(names[i])))return 1;
    return 0;
}
/* The value of name= in a block of NAME=VALUE words each ended by a NUL, the list by an empty one (a bootstrap's
 * environment or application settings); the last wins, as Wine applies them. NULL when none. */
static inline const char *pw_wine_block_value(const char *block,size_t size,const char *name)
{
    const char *found=NULL;
    const size_t length=strlen(name);
    for(size_t at=0;at<size && block[at];) {
        const size_t word=strnlen(block+at,size-at);
        if(word>length && !strncmp(block+at,name,length) && block[at+length]=='=')found=block+at+length+1;
        at+=word+1;
    }
    return found;
}
/* The block a helper's bootstrap carries: each inherited NAME=value of environment, NUL-terminated, then an empty one;
 * an entry that does not fit in size is left out. Returns the bytes used before the final NUL. */
static inline size_t pw_wine_inherit_environment(char *const *environment,char *out,size_t size)
{
    size_t used=0;
    for(char *const *entry=environment;entry && *entry;++entry) {
        const size_t length=strlen(*entry)+1;
        if(!pw_wine_inherited(*entry) || !strchr(*entry,'=') || used+length+1>size)continue;
        memcpy(out+used,*entry,length);used+=length;
    }
    out[used]=0;
    return used;
}
#endif
