/*
 * MihawkOS - "Check for updates" (docs/SELF_INSTALL.md, phase 8).
 *
 * Once a day by itself (Settings > Automatic update check, the owner's 2026-10-31 request) and on request: GitHub's
 * release listing, the newest signed release newer than this build (src/pw_update_flow.h). Its manifest
 * (src/pw_release.h, signed with the owner's Ed25519 key; this build carries the public half) names the release zip
 * and the runtime-pack.txt its title folder must carry, by SHA-256. The zip downloads at once, in the background,
 * while the launcher is up (a game ends the process and the download; the next launcher start takes it up again), over
 * the title's own TLS (proton_fetch.h), is checked, and is unpacked into /data/ps5proton/update/stage. The main menu
 * then offers Restart now (R2); otherwise it is switched in at the next start, before anything else runs, followed by
 * a restart into the new title (src/pw_update.h; rule 2). The previous title's files stay until the new one has drawn
 * its launcher once. The new title installs the runtime it carries itself, by its stamp.
 *
 * The address it asks: the title folder's update-source.txt (a manifest's, for a rehearsal), else the one the build
 * names (PROTON_UPDATE_URL), else GitHub's listing. No release key: it says so, and the automatic check stays quiet.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PROTON_UPDATE_H
#define PROTON_UPDATE_H

#include <stdint.h>

#include <string>

#include "../../src/pw_pack.h"

struct ProtonUpdate {
	enum Phase { idle, checking, current, available, downloading, ready, failed } phase{ idle };
	std::string version;     /* the release offered, once checked */
	std::string message;     /* for the screen */
	uint64_t download{ 0 };  /* the title pack's bytes to download */
	PwPackProgress progress{};
	double seconds{ 0.0 };   /* since the download started */
};

/* Ask the release address what is newest; returns at once. */
void proton_update_check();
/* Download the release proton_update_check found into the stage; returns at once. */
void proton_update_download();
/* Restart into the staged title now (it switches as that start begins). Returns only when the restart was refused. */
void proton_update_restart();
ProtonUpdate proton_update_state();

/* main.cpp, once /data is granted and before anything reads the title folder: a staged title is switched in, then the
 * title restarts into it. Returns when there was nothing to switch, or the switch failed (the old title goes on, and
 * says why). */
void proton_update_on_start(int argc, char **argv);
/* The launcher drew: the title that runs has started once, so the previous title's files go. */
void proton_update_started();
/* The automatic check (the owner, 2026-10-31): at most once a day (src/pw_update.h), unless Settings turned it off, the
 * launcher asks GitHub as it starts (proton_update_check). Only the launcher calls it, so never while a game runs. */
void proton_update_auto();
bool proton_update_auto_enabled();
void proton_update_set_auto(bool on);
/* The main menu's line about a newer release: found (it downloads in the background), downloading, or ready; empty
 * when there is none. */
std::string proton_update_notice();
/* A downloaded release waits to be switched in: the main menu offers Restart now (the owner, 2026-10-31); ignored, it
 * is switched in at the next start (proton_update_on_start). */
bool proton_update_restartable();
/* Restart into it now (the switch runs as that start begins). Returns only when the restart was refused. */
void proton_update_restart_now();

#endif
