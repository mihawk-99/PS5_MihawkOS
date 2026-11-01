/*
 * PS5_Proton - what the title runs, from its arguments.
 *
 * The title runs one game per process: the launcher restarts the title with
 * sceSystemServiceLoadExec and the chosen game's arguments, and closing the
 * game restarts it into the launcher. The desktop entry instead keeps its
 * owner session alive across application exits; prefixes and caches persist.
 *
 * Arguments are "key=value" words, as prospero-win's launcher uses them
 * (docs/ARCHITECTURE.md):
 *   profile=<id>   a game from the catalog under /data/ps5proton/games;
 *   path=<exe>     an absolute Windows path, run directly (overrides the
 *                  profile's executable);
 *   cycle=<n>      launcher/game round trips completed, for unattended runs;
 *   env=<N>=<V>    one environment override for this launch (test runs);
 *   args=<text>    the program's command line arguments for this launch (test runs).
 * With neither profile nor path the title shows the launcher, as it does when
 * the system starts it (argc 1).
 *
 * Copyright (C) 2026 Mihawk
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PS5_PROTON_LAUNCH_H
#define PS5_PROTON_LAUNCH_H

#include <string>
#include <vector>
#include "../../src/pw_payload_check.h"
#include "../../src/pw_steam_memory.h"

/* One game the launcher can start. */
struct ProtonGame {
	std::string id;         /* the directory name under games/, and profile=<id> */
	std::string name;       /* shown in the launcher */
	std::string detail;     /* second line: what the game is */
	std::string executable; /* absolute Windows path, from game.json */
	std::string arguments;  /* the profile's own arguments, from game.json */
	std::string directory;  /* the working directory ("start in"), an absolute Windows path; empty: the executable's */
	std::string prefix;     /* optional prefix folder ID; empty: shared default or retained legacy folder */
	std::vector<std::string> env; /* NAME=VALUE words for this game's environment, from game.json */
	/* SteamGameId / SteamAppId words found from the installation (proton_add_steam_ids), given beside env at a launch;
	 * kept apart so the WebUI never writes them back into game.json. Empty until proton_add_steam_ids runs. */
	std::vector<std::string> steam_words;
	bool desktop{ true };   /* game.json "desktop": false keeps a diagnostic entry off the Mihawk OS desktop */
	bool launcher{ true };  /* game.json "launcher": false keeps it out of the launcher's list (the WebUI's "In MihawkOS's
	                           game list" unticked); it still starts from the desktop, a tile or a profile */
	std::string complaint;  /* what is wrong with this entry, shown in the launcher */
	/* The executable is a 32-bit-only Windows program (src/pw_pe_arch.h): it runs under WoW64 when
	 * the runtime ships its i386 modules, and has a complaint (is not launched) when it does not. */
	bool x86_32{ false };
	std::string folder;     /* where its game.json is; empty for the built-in Mihawk OS */
	/* A program to run instead, with this entry's prefix and settings (the launcher's Files: the title's path= launch);
	 * empty: the entry's own executable */
	std::string run_path;
};

struct ProtonLaunch {
	bool game{ false };     /* false: the launcher, as when the system starts it */
	std::string profile;    /* game mode: the requested game */
	std::string path;       /* game mode: a Windows path given directly */
	unsigned cycle{ 0 };
	/* env=NAME=VALUE: test-only overrides of the game's environment. They come
	 * only from the one-shot wine-run.txt (the restart never writes them), so
	 * they apply to one launch and never to a later manual one. */
	std::vector<std::string> env;
	/* args=<command line arguments>: test-only, like env=; they replace the
	 * profile's own arguments for one launch (a conformance test takes the name
	 * of the test file to run). An explicitly empty value clears them. */
	std::string arguments;
	bool arguments_override{ false };
	/* aurelia=1: this session runs the Aurelia client (src/pw_aurelia.h), whose --json lines the title captures
	 * from the session output (native/proton_wine.cpp aurelia_line). The launcher sets it for its Aurelia entry. */
	bool aurelia{ false };
	/* ended=<reason>: why the last session ended, when it failed (native/proton_wine.cpp fail), for the launcher. */
	std::string ended;
};

/* Present while a session runs (native/proton_wine.cpp): written at its start, removed when it ends through the title.
 * One the launcher finds is a session that crashed, froze or lost power. Holds the session's game name. */
#define PROTON_SESSION_MARKER "/app0/session-running"
/* What a session said on screen (native/proton_wine.cpp's notices), one line each, for the launcher after it. */
#define PROTON_SESSION_NOTICES "/app0/session-notices"
/* When this title last started the resident Lapy daemon (seconds since the epoch, as text): the payload check tells the
 * daemon from other processes listed as "payload" by it (src/pw_payload_check.h). Written before root, in the title folder. */
#define PROTON_DAEMON_STARTED "/app0/lapy-daemon-started"

/* Where games and their prefixes live.
 *
 * A title starts able to see only its own sandbox: /data is absent until the
 * Lapy daemon grants it (the platform layer's elevation client). So there are two places a game may be, and the
 * catalog reads both:
 *   /data/ps5proton/games            once /data is granted (the library)
 *   <title folder>/games             always, over FTP, with no daemon
 * After a grant the title's own folder is no longer at /app0 but in the
 * sandbox, so proton_app_root() and proton_title_path() resolve it. */
#define PROTON_DATA_ROOT "/data/ps5proton"
/* What the user's payloads held at the last session's start, MiB, as text (native/proton_wine.cpp writes it, the launcher
 * shows the startup tip from it: the launcher itself cannot take the census, which only a helper may). */
#define PROTON_PAYLOADS_FILE PROTON_DATA_ROOT "/payloads-mib"
#define PROTON_SANDBOX_APP0 "/mnt/sandbox/" PS5_TITLE_ID "_000/app0"

/* Ask for /data, before any other thread exists, through the platform layer's
 * client for the Lapy daemon (ps5platform/elevation.h). True when /data was
 * proved reachable; false keeps the title on its own folder. */
/* retry: this process is the one proton_retry_elevation started, the daemon having just been started. */
bool proton_data_grant(bool retry = false);
/* What the request came to, for the log ("ok", "timeout" without a daemon, ...). */
const char *proton_data_status();
/* What the title did about a missing daemon ("started the resident Lapy daemon ...", or why it
 * could not); empty when a daemon answered the first request. */
const char *proton_data_note();
/* Why /data is not reachable, in plain words for the launcher's screen; empty when it is. */
std::string proton_data_problem();
/* Whether the installed Wine runtime is this build's (src/pw_runtime_check.h): the title folder's runtime-stamp
 * against the runtime's. Needs /data. */
int proton_runtime_state();
/* This title's build, for the top of every session log: its contentVersion, the runtime stamp it pins and the installed
 * one (16 hex digits each, "-" for none), and what that makes the runtime. Needs /data for the installed stamp. */
std::string proton_build_identity();
/* Why the Wine runtime cannot run a game, in plain words: missing, or not the one this title folder pins
 * (runtime-stamp, tools/build-production.sh); empty when it can. Needs /data. */
std::string proton_runtime_problem();
/* When this process started the daemon and still did not get /data, replace it once, with the same arguments and
 * "elevation-retry=1": a fresh process asks the daemon that now runs. Returns only when it does not restart. */
void proton_retry_elevation(int argc, char **argv);
/* The title's own folder, whichever view is current. */
const char *proton_app_root();
/* "/app0/x" resolved for the current view (unchanged before a grant). */
std::string proton_title_path(const char *path);
/* The payload checklist (src/pw_payload_check.h): taken now, from the processes, kstuff's kekcalls and the versions
 * ShadowMountPlus and websrv give on the loopback; every payload seen goes to the log. The launcher refuses entry and a
 * game start ends while it fails. */
const PwPayloadReport &proton_payloads_check();
/* The console's memory census now, as the memory helper takes it (src/pw_steam_memory.h, for the Steam warning); unknown
 * (known 0) without /data or any figure. */
PwSteamCensus proton_system_memory();
/* The last one taken (one is taken if none was). */
const PwPayloadReport &proton_payloads();
const PwPayloadFacts &proton_payload_facts();
/* What the user must do, in plain words; empty when it passed. */
std::string proton_payloads_problem();
/* Global mouse assignments, shared by the launcher and game owner. */
std::string proton_controls_path();
/* The Game Mode setting (src/pw_game_mode.h), beside controls.txt. */
std::string proton_game_mode_path();
/* The controller setting (src/pw_controller.h), beside game-mode.txt. */
std::string proton_controller_path();
/* The 120 Hz setting (src/pw_high_refresh.h), beside game-mode.txt. */
std::string proton_high_refresh_path();
/* The Logging setting (src/pw_detailed_logs.h), beside game-mode.txt. */
std::string proton_logging_path();
/* Every place the catalog is read from, in order, existing or not. */
std::vector<std::string> proton_game_roots();

/* The parsed arguments, kept for the program (the sample's createExample()
 * has no arguments of its own). main() sets them once. */
void proton_set_args(int argc, char **argv);
const ProtonLaunch &proton_launch();

/* The games the launcher shows: every directory under protonGamesRoot with a
 * game.json (or, failing that, the directory's own name). Reads the directory
 * through the platform layer's enumeration (ps5platform/libc.h: opendir is
 * refused to a title, the link recipe binds it to getdents). */
std::vector<ProtonGame> proton_read_games(std::string *problem);
/* Same selection for executable validation and the owner bootstrap. Empty on
 * invalid IDs; existing per-game folders remain untouched. */
std::string proton_game_prefix(const ProtonGame &game);

/* Fills game.steam_words: the game's Steam app id, from Steam's appmanifest for the folder holding its program or from
 * the steam_appid.txt beside it (src/pw_steam_appid.h), as SteamGameId and SteamAppId words, for the name its env does
 * not set. Proton's Wine changes behaviour for a game by those names; nothing else set them. Reads the disk (a few
 * small files), so a launch calls it, not the catalog's listing. Safe to call again after the executable changes. */
void proton_add_steam_ids(ProtonGame &game);

/* Replace this process with the title again: the game, or the launcher when
 * game is NULL. Never returns when it works: the caller is gone. ended, when not empty, is the failed session's reason
 * for the launcher (ProtonLaunch::ended), on one line. */
void proton_restart(const ProtonGame *game, unsigned cycle, const char *reason, const char *ended = nullptr);

/* Queue a launcher choice; dispatch only after its UI, audio and Vulkan device are destroyed. */
void proton_queue_restart(const ProtonGame *game, unsigned cycle, const char *reason);
/* A program's checks (an absolute Windows path that is there; 32-bit only where the runtime has its modules): the catalog
 * applies them to every entry, the session to a program run by path. What fails is game.complaint. */
void proton_check_program(ProtonGame &game);
/* A game's own setting, its game.json env word name=value (none of name's when value is empty); "" when saved. */
std::string proton_game_set_env(const ProtonGame &game, const std::string &name, const std::string &value);
void proton_finish_restart();
/* Whether proton_queue_restart has a restart waiting: a budgeted test run that queued one (the Steam option's) carries it out. */
bool proton_restart_pending();

/* A line to klog, with the title's prefix. */
__attribute__((format(printf, 1, 2))) void proton_say(const char *format, ...);

/* The self-test hook: with /app0/restart-check present, the launcher runs one
 * launcher -> game -> launcher cycle by itself and records it in
 * /app0/restart-check.log, so the restart contract can be proved without a pad
 * (ps5/tools/run.sh --menu, or tools/restart-selftest.sh). */
bool proton_selftest_active();
void proton_selftest_note(const char *what, unsigned cycle);

/* The unattended first-install rehearsal (tools/run-setup-unattended.py, docs/SELF_INSTALL.md): a scripted run
 * (frame budget) walks the guided setup with the real install engine, and restarts into its sessions, while this marker
 * exists. Only a scripted run reads it, so a normal launch behaves the same with or without it. */
#define PROTON_SETUP_UNATTENDED PROTON_DATA_ROOT "/setup-unattended"
bool proton_setup_unattended(bool frameBudget);

#endif /* PS5_PROTON_LAUNCH_H */
