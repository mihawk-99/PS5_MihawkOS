/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_STEAM_MEMORY_H
#define PW_STEAM_MEMORY_H
/* Whether Steam likely fits in the console's memory (the owner, 2026-10-31: "I want for situations like this a warning
 * screen that says something like 'Steam will likely not work under your configuration, please follow these steps'").
 *
 * The figure is the memory census's (src/pw_memory_helper.h: every process's resident memory but the title's), taken by
 * the launcher when Steam is chosen. A standard PS5 swaps past about PW_STEAM_SWAP_POINT_MIB of it
 * (src/pw_memory_guard.h, the ceiling test). Steam's need above the census, measured, then a margin (the memory session,
 * 2026-10-31, from records only):
 * - First install, then its first start (it updates itself and brings up its web helpers): steam-signin-real-2026-10-24-01
 *   (Pro) rose 163 -> 559 MiB, +396; the installer alone (steam-install-real-2026-10-24-01) peaked at 171 MiB. The peak
 *   was the run's last sample, so +100 margin: PW_STEAM_NEED_INSTALL_MIB.
 * - Big Picture with a download running: tester-steam-download-2026-10-31 (standard PS5) 890 -> 1218 MiB, +328, for
 *   28 minutes; more web helpers (store pages, overlays) add 50-100 MiB each, so +120 margin: PW_STEAM_NEED_SIGNED_IN_MIB.
 * An Alpha 6 tester's console used 1,277 MB before MihawkOS: 1,132 system + 74 in 9 payloads (2026-10-31). */

#ifdef __cplusplus
extern "C" {
#endif

#define PW_STEAM_SWAP_POINT_MIB 1800u
#define PW_STEAM_NEED_INSTALL_MIB 500u     /* not installed, or installed and never signed in (its first start) */
#define PW_STEAM_NEED_SIGNED_IN_MIB 450u   /* signed in: Big Picture, a download running */
#define PW_STEAM_WARNING_HOLD_SECONDS 5.0f /* TRIANGLE held this long goes on anyway (no dead end) */

typedef struct PwSteamCensus {
    int known;                  /* 0: no census (no /data, or no figure for any process): never warn */
    unsigned system_mib;        /* every process's resident memory but the title's */
    unsigned processes;
    unsigned payload_mib;       /* the user's payloads' (pw_memory_is_payload) */
    unsigned payload_count;
} PwSteamCensus;

/* Steam's need above the census: PW_STEAM_NEED_SIGNED_IN_MIB once it is signed in, else PW_STEAM_NEED_INSTALL_MIB. */
unsigned pw_steam_memory_need(int signed_in);
/* 1 when a known census plus Steam's need passes the swap point, so the launcher warns before Steam installs or starts. */
int pw_steam_memory_warn(const PwSteamCensus *census, int signed_in);

#ifdef __cplusplus
}
#endif
#endif
