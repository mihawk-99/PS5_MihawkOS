/*
 * MihawkOS - "Check for updates" (docs/SELF_INSTALL.md, phase 8).
 *
 * When opened from the home screen, at most once an hour, by itself (Settings > Automatic update check, the owner's
 * 2026-10-31 request, hourly from 7.1) and on request: GitHub's
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
#include "../../src/pw_setup.h"
#include "../../src/pw_update.h"

struct ProtonUpdate {
	enum Phase { idle, checking, current, available, downloading, ready, failed } phase{ idle };
	std::string version;     /* the release offered, once checked (its contentVersion, "01.003.000") */
	std::string name;        /* that release as players know it ("Alpha 8.1", pw_release_name); "" when not known */
	std::string message;     /* for the screen */
	uint64_t download{ 0 };  /* the title pack's bytes to download */
	PwPackProgress progress{};
	double seconds{ 0.0 };   /* since the download started */
	int step{ 0 };           /* while downloading: pw_update_flow.h's PW_UPDATE_STEP_* (progress is that step's) */
	PwUpdateFailure failure{ PW_UPDATE_FAIL_NONE };   /* when failed: why, for the main menu's line */
};

/* This title as players know it ("Alpha 8.1": its VERSION, pw_release_name), else its contentVersion. */
std::string proton_update_installed_name();
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
/* The automatic check (the owner, 2026-10-31; hourly from 7.1): when MihawkOS is opened from the home screen, at most
 * once an hour (src/pw_update.h), unless Settings turned it off, the launcher asks GitHub as it starts
 * (proton_update_check). A download cut short is taken up again on any start. Only the launcher calls it, so never while
 * a game runs. main.cpp says whether this start is a cold one: no handoff from a restart (an elevation retry is
 * still the same open). */
void proton_update_set_cold_start(bool cold);
/* A harness's hook (/data/ps5proton/update-check-scripted, planted and removed by the run): a scripted launcher, which
 * otherwise asks nothing of the network, makes the automatic check as a home-screen open would (the on-console update
 * proof, docs/PUBLICATION_CHECKLIST.md). */
bool proton_update_scripted_check();
void proton_update_auto();
bool proton_update_auto_enabled();
void proton_update_set_auto(bool on);
/* The main menu's line about the update in the menu's language (src/pw_update_notice.h): checking, found, downloading
 * (MB and percent), checking its signature, unpacking (percent), ready, or why a check or the update failed; empty
 * when there is nothing to say. */
std::string proton_update_notice(const PwSetupLanguage *language);
/* A downloaded release waits to be switched in: the main menu offers Restart now (the owner, 2026-10-31); ignored, it
 * is switched in at the next start (proton_update_on_start). */
bool proton_update_restartable();
/* Restart into it now (the switch runs as that start begins). Returns only when the restart was refused. */
void proton_update_restart_now();

#endif
