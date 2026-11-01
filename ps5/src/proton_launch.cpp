/*
 * PS5_Proton - the launch contract, the game catalog and the restart.
 *
 * Copyright (C) 2026 Mihawk
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "proton_launch.h"
#include "../../src/pw_runtime_check.h"
#include "../../src/pw_memory_helper.h"
#include "../../src/pw_steam.h"
#include "../../src/pw_parked.h"
#include <functional>
#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <exception>
#include <utility>

#if !defined(PS5_HOST_REFERENCE)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ps5platform/elevation.h>
#include <ps5platform/kernel.h>
#include "../../src/pw_elfldr.h"
#endif
#include "../../src/pw_pe_arch.h"
#include "../../src/pw_firmware.h"
#include "../../src/pw_path_nocase.h"
#include "../../src/pw_steam_appid.h"
extern "C" {
#include "../../src/pw_wine_launch.h"
}

#include "json.hpp" // external/tinygltf's vendored nlohmann::json, already in the title

#if !defined(PS5_HOST_REFERENCE)
/* The title restarts itself: sceSystemServiceLoadExec replaces this process. */
extern "C" int sceSystemServiceLoadExec(const char *path, char *const argv[]);
#endif

/* What the /data request said, for the log and the launcher's empty state. */
static bool gDataGranted = false;
static bool gDaemonStarted = false;   /* this process started the Lapy daemon */
static bool gDataAttempted = false;
static bool gDataRetry = false;      /* this process is the one the daemon's start restarted */
static bool gWebsrvDown = false;     /* the daemon could not be started: websrv did not answer */
#if !defined(PS5_HOST_REFERENCE)
static enum ps5_elevation_status gDataStatus = PS5_ELEVATION_UNAVAILABLE;
#endif
static std::string gDataNote;

const char *proton_app_root()
{
#if defined(PS5_HOST_REFERENCE)
	return PS5_APP_ROOT;
#else
	if (!gDataGranted) {
		return "/app0";
	}
	/* Elevated: /app0 is gone. The install folder is the real one (the sandbox
	 * mount is the same directory, so it is the fallback, as ProsperoEden does). */
	static const char *const install = "/data/homebrew/" PS5_TITLE_ID;
	struct stat status;
	if (stat(std::string(std::string(install) + "/eboot.bin").c_str(), &status) == 0) {
		return install;
	}
	return PROTON_SANDBOX_APP0;
#endif
}

std::string proton_title_path(const char *path)
{
	const std::string root = proton_app_root();
	if (root == "/app0") {
		return std::string(path);
	}
	const std::string prefix = "/app0/";
	const std::string text = path;
	if (text.compare(0, prefix.size(), prefix) == 0) {
		return root + text.substr(prefix.size() - 1);
	}
	return text;
}

std::string proton_controls_path()
{
#if defined(PS5_HOST_REFERENCE)
	return std::string(PS5_APP_ROOT) + "/controls.txt";
#else
	return proton_data_grant() ? PROTON_DATA_ROOT "/controls.txt" : proton_title_path("/app0/controls.txt");
#endif
}

std::string proton_game_mode_path()
{
#if defined(PS5_HOST_REFERENCE)
	return std::string(PS5_APP_ROOT) + "/game-mode.txt";
#else
	return proton_data_grant() ? PROTON_DATA_ROOT "/game-mode.txt" : proton_title_path("/app0/game-mode.txt");
#endif
}

std::string proton_high_refresh_path()
{
#if defined(PS5_HOST_REFERENCE)
	return std::string(PS5_APP_ROOT) + "/high-refresh.txt";
#else
	return proton_data_grant() ? PROTON_DATA_ROOT "/high-refresh.txt" : proton_title_path("/app0/high-refresh.txt");
#endif
}

std::string proton_controller_path()
{
#if defined(PS5_HOST_REFERENCE)
	return std::string(PS5_APP_ROOT) + "/controller.txt";
#else
	return proton_data_grant() ? PROTON_DATA_ROOT "/controller.txt" : proton_title_path("/app0/controller.txt");
#endif
}

std::string proton_logging_path()
{
#if defined(PS5_HOST_REFERENCE)
	return std::string(PS5_APP_ROOT) + "/logging.txt";
#else
	return proton_data_grant() ? PROTON_DATA_ROOT "/logging.txt" : proton_title_path("/app0/logging.txt");
#endif
}

std::vector<std::string> proton_game_roots()
{
	std::vector<std::string> roots;
#if defined(PS5_HOST_REFERENCE)
	roots.push_back(PS5_APP_ROOT "/games");
#else
	if (gDataGranted) {
		roots.push_back(PROTON_DATA_ROOT "/games");
	}
	roots.push_back(std::string(proton_app_root()) + "/games");
	if (!gDataGranted) {
		/* A game may also have been left where a granted process keeps its own
		 * folder; read it too rather than make the user move it. */
		roots.push_back(PROTON_SANDBOX_APP0 "/games");
	}
#endif
	return roots;
}

#if !defined(PS5_HOST_REFERENCE)
namespace {

bool path_exists(const char *path)
{
	struct stat status;
	return stat(path, &status) == 0;
}

/* The resident Lapy daemon (tools/build-production.sh copies the pinned build into the title folder),
 * and the request it takes: it removes the request as soon as it reads it. */
const char *const daemonImage = "/app0/lapy-root-daemon.elf";
const char *const daemonRequest = "/download0/elevate_proc";

/* All of size to a file (send_all is for sockets). */
bool write_all(int fd, const char *data, size_t size)
{
	while (size) {
		const ssize_t written = write(fd, data, size);
		if (written < 0 && errno == EINTR) {
			continue;
		}
		if (written <= 0) {
			return false;
		}
		data += written;
		size -= (size_t)written;
	}
	return true;
}

bool send_all(int fd, const char *data, size_t size)
{
	while (size) {
		const ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
		if (sent < 0 && errno == EINTR) {
			continue;
		}
		if (sent <= 0) {
			return false;
		}
		data += sent;
		size -= (size_t)sent;
	}
	return true;
}

/* The contract's request, {"PID":n}, written beside the request path and renamed over it, so the daemon never reads
 * half of it (the platform client's publish_request, ps5platform/elevation.h). Nothing stays open. */
bool publish_root_request(const struct ps5_elevation_config &contract)
{
	char temporary[256], body[64];
	const long pid = (long)getpid();
	const int body_length = snprintf(body, sizeof(body), "{\"PID\":%ld}\n", pid);
	const int path_length = snprintf(temporary, sizeof(temporary), "%s/.elevate_proc.%ld", contract.request_directory, pid);
	if (body_length <= 0 || (size_t)body_length >= sizeof(body) || path_length <= 0 ||
		(size_t)path_length >= sizeof(temporary)) {
		return false;
	}
	unlink(temporary);
	const int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0644);
	const bool written = fd >= 0 && write_all(fd, body, (size_t)body_length);
	if (fd >= 0 && close(fd) != 0) {
		unlink(temporary);
		return false;
	}
	if (!written || rename(temporary, contract.request_path) != 0) {
		unlink(temporary);
		return false;
	}
	return true;
}

/* One request to the daemon, done only once it has made this process root.
 *
 * Where a loader already mounted /data into the sandbox (ShadowMountPlus 1.7beta4), the platform client's own proof
 * cannot serve: its write and read in /data pass at once, before the daemon has read the request, and the probe file
 * it holds open while it does carries a reference to the process's new credential. The daemon elevates only a target
 * whose credential has exactly two references (the process's and its thread's), so a check that fell in that window
 * refused it with EBUSY: lapy-service.log, "target_state ... cred_refs=3" then "request_result stage=target_preflight
 * error=16" for the process that followed the daemon's start, while the launcher after it was made root.
 *
 * So, there, the request is made with nothing open: the result file is opened first (as the client does, the path
 * may vanish once the root moves), the credential made private (seteuid), the request published, and only getuid
 * watched until it is 0. /data is proved afterwards, as root, and the result written for the daemon, which waits for
 * it (LAPY_REQUIRE_CLIENT_RESULT) and stops if it is missing. Elsewhere the client works as it always did, because
 * its probe cannot open anything in /data before the daemon has acted. A process that is root waits no longer than
 * the contract waits for /data (its polls), and one that is not after that counts as a timeout. */
enum ps5_elevation_status request_root(const struct ps5_elevation_config &contract)
{
	if (!path_exists(PROTON_DATA_ROOT)) {
		enum ps5_elevation_status status = ps5_elevation_request_with(&contract, PS5_ELEVATION_FILESYSTEM);
		for (unsigned poll = 0; status == PS5_ELEVATION_OK && getuid() != 0 && poll < contract.polls; ++poll) {
			usleep(contract.poll_interval_us);
		}
		return status == PS5_ELEVATION_OK && getuid() != 0 ? PS5_ELEVATION_TIMEOUT : status;
	}
	const int result = open(contract.result_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (result < 0) {
		return PS5_ELEVATION_UNAVAILABLE;
	}
	fchmod(result, 0644);
	if (seteuid(geteuid()) != 0) {
		close(result);
		return PS5_ELEVATION_PREPARE_FAILED;
	}
	if (!publish_root_request(contract)) {
		close(result);
		return PS5_ELEVATION_TRANSPORT_ERROR;
	}
	for (unsigned poll = 0; getuid() != 0 && poll < contract.polls; ++poll) {
		usleep(contract.poll_interval_us);
	}
	bool proved = false;
	if (getuid() == 0) {
		/* The client's proof: a probe written, read back and compared under /data. */
		const std::string probe = std::string(contract.data_directory) + "/.lapy_probe_" + std::to_string((long)getpid());
		static const char token[] = "LAPYOWN\n";
		char back[sizeof(token) - 1] = {};
		unlink(probe.c_str());
		const int fd = open(probe.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
		proved = fd >= 0 && write_all(fd, token, sizeof(token) - 1) && lseek(fd, 0, SEEK_SET) == 0 &&
			read(fd, back, sizeof(back)) == (ssize_t)sizeof(back) && !memcmp(back, token, sizeof(back));
		if (fd >= 0) close(fd);
		unlink(probe.c_str());
	}
	char report[64];
	const int length = snprintf(report, sizeof(report), "DATA_OK=%d OPEN_ERRNO=0\n", proved ? 1 : 0);
	if (length > 0) write_all(result, report, (size_t)length);
	close(result);
	if (getuid() != 0) return PS5_ELEVATION_TIMEOUT;
	return proved ? PS5_ELEVATION_OK : PS5_ELEVATION_APPLY_FAILED;
}

/* Start the resident daemon as its own process, through websrv's payload launcher on the loopback
 * (POST /elfldr, as the Wine side starts its helper processes). Sockets only: no thread is made,
 * so the process stays single-threaded for the daemon. */
bool start_daemon(std::string &note)
{
	FILE *file = fopen(daemonImage, "rb");
	if (!file) {
		note = std::string("no ") + daemonImage;
		return false;
	}
	std::string elf;
	char buffer[16384];
	size_t read = 0;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0 && elf.size() <= (4u << 20)) {
		elf.append(buffer, read);
	}
	fclose(file);
	PwElfUpload request;
	const int encoded = pw_elfldr_request(&request, "lapy-root-daemon.elf", "", elf.data(), elf.size());
	if (elf.size() < 64 || elf.size() > (4u << 20) || elf.compare(0, 4, "\x7f" "ELF") != 0 || encoded) {
		note = std::string(daemonImage) + " is not a usable ELF";
		return false;
	}
	const int web = socket(AF_INET, SOCK_STREAM, 0);
	if (web < 0) {
		note = std::string("no socket: ") + strerror(errno);
		return false;
	}
	struct timeval timeout = { 10, 0 };
	setsockopt(web, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(web, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	struct sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(PW_ELFLDR_PORT);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(web, (struct sockaddr *)&address, sizeof(address)) != 0) {
		note = std::string("websrv is not answering on the loopback (") + strerror(errno) + ")";
		gWebsrvDown = true;
		close(web);
		return false;
	}
	char reply[128] = {};
	size_t used = 0;
	bool sent = send_all(web, request.head, request.head_size) && send_all(web, elf.data(), elf.size()) &&
		send_all(web, request.tail, request.tail_size);
	while (sent && used < sizeof(reply) - 1 && !strstr(reply, "\r\n")) {
		const ssize_t got = recv(web, reply + used, sizeof(reply) - 1 - used, 0);
		if (got < 0 && errno == EINTR) {
			continue;
		}
		if (got <= 0) {
			break;
		}
		used += (size_t)got;
	}
	close(web);
	if (!sent || !pw_elfldr_accepted(reply)) {
		note = std::string("websrv did not accept the daemon: ") + (sent ? std::string(reply, strcspn(reply, "\r\n")) : "send failed");
		return false;
	}
	note = "started the resident Lapy daemon (" + std::to_string(elf.size()) + " bytes)";
	// When it started, for the payload check: the daemon is listed as "payload" like others (src/pw_payload_check.h). The
	// title folder is writable before root, and the record outlives this process (the daemon stays resident).
	if (FILE *record = fopen(proton_title_path(PROTON_DAEMON_STARTED).c_str(), "w")) {
		fprintf(record, "%lld\n", (long long)time(nullptr));
		fclose(record);
	}
	return true;
}

} // namespace
#endif

const char *proton_data_status()
{
#if defined(PS5_HOST_REFERENCE)
	return "no console";
#else
	return gDataAttempted ? ps5_elevation_status_name(gDataStatus) : "not asked";
#endif
}

bool proton_data_grant(bool retry)
{
#if defined(PS5_HOST_REFERENCE)
	/* The PC has no daemon and no /data: the reference build reads its own
	 * folder (proton_game_roots). */
	gDataGranted = false;
	return false;
#else
	/* Seeing /data is not enough: a loader can mount it into the sandbox (ShadowMountPlus 1.7beta4 does), and
	 * the process still runs as the title's own user, which owns none of the files the elevated title made there
	 * (2026-10-06, record removed from the tree: Wine refused "/data/ps5proton/prefixes/1 is not owned by you"). Only a
	 * process the daemon has elevated, which makes it root, skips the request. */
	if (path_exists(PROTON_DATA_ROOT) && getuid() == 0) {
		gDataGranted = true;
		return true;
	}
	if (gDataAttempted) {
		return gDataGranted;
	}
	gDataAttempted = true;

	/* The platform layer's client for the Lapy daemon (ps5platform/elevation.h): it
	 * makes the process's credentials the shape the daemon needs, publishes the
	 * request atomically and waits for /data to work, proved by a real write and
	 * read, so only PS5_ELEVATION_OK permits using it. The contract's paths, but
	 * a 3 s wait instead of its 10 s: the daemon polls every 100 ms and finished
	 * 15 recorded grants 0.35-0.41 s after pickup (/data worked by 0.51 s), and
	 * this wait runs before anything is on screen, so with no daemon it is time
	 * the player spends looking at a black screen. */
	static const struct ps5_elevation_config contract = {
		"/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 60, 50000,
	};
	/* The process that follows a daemon's start waits longer: the daemon was started a moment ago, and a request
	 * is all this process does before it gives up. */
	static const struct ps5_elevation_config after_start = {
		"/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 300, 50000,
	};
	gDataStatus = request_root(retry ? after_start : contract);
	gDataGranted = gDataStatus == PS5_ELEVATION_OK;
	gDataRetry = retry;
	if (retry) {
		if (!gDataGranted) gDataNote = "the daemon started for this launch did not make this process root in 15 s";
		return gDataGranted;
	}

	/* The daemon removes a request as soon as it reads it, so a request still there after the
	 * wait means no daemon is running: none was started this boot (it does not survive a
	 * restart). Start it, then ask again. A request that was read and did not end in /data is
	 * never followed by a second daemon: two would take the same request. This process does not ask the daemon it
	 * started: proton_retry_elevation replaces it, and the new process asks once. Asking here, a request at a time,
	 * left requests behind for a process that was gone, and the daemon, which serves one request at a time and waits
	 * up to 10 s for its requester's result, answered the next process too late (2026-10-06, two runs, records removed
	 * from the tree: the launcher after them was made root). */
	if (gDataStatus == PS5_ELEVATION_TIMEOUT && path_exists(daemonRequest)) {
		unlink(daemonRequest);
		if (!(gDaemonStarted = start_daemon(gDataNote))) {
			gDataNote = "no daemon answered, and starting one failed: " + gDataNote;
		}
	}
	return gDataGranted;
#endif
}

void proton_retry_elevation(int argc, char **argv)
{
#if defined(PS5_HOST_REFERENCE)
	(void)argc;
	(void)argv;
#else
	/* Under ShadowMountPlus 1.7beta4 the process that had to start the daemon was not made root once
	 * (2026-10-06, two runs, records removed from the tree), while the next process, asking
	 * the daemon then running, was. Going on without root fails anyway (Wine refuses a prefix the elevated title
	 * owns), and a process that goes on into Wine starts the threads the daemon refuses: after
	 * queue-warmup-2026-10-06-01 it made no process root until a restart. */
	if (gDataGranted || !gDaemonStarted) return;
	std::vector<char *> again;
	for (int i = 1; i < argc; i++) {
		if (argv[i] && !strcmp(argv[i], "elevation-retry=1")) return;   /* once */
		again.push_back(argv[i]);
	}
	static char retry[] = "elevation-retry=1";
	again.push_back(retry);
	again.push_back(nullptr);
	static const char *const eboots[] = { "/app0/eboot.bin", PROTON_SANDBOX_APP0 "/eboot.bin" };
	/* argv may not survive LoadExec: the marker is what makes the next process the retry (main.cpp consumes it). */
	const std::string marker = proton_title_path("/app0/elevation-retry");
	if (FILE *file = fopen(marker.c_str(), "wb")) fclose(file);
	for (const char *eboot : eboots) {
		proton_say("elevation: the daemon started here did not make this process root; restarting once (%s)", eboot);
		sceSystemServiceLoadExec(eboot, again.data());
	}
	remove(marker.c_str());
#endif
}

const char *proton_data_note()
{
	return gDataNote.c_str();
}

#if !defined(PS5_HOST_REFERENCE)
/* This console's INSTALLED firmware as "13.40", or empty when it cannot be read. The daemon's crt rejects a firmware it was
 * not built for and faults before main, so no grant comes; naming it tells a too-new console apart from a dev.conf
 * fault (base PS5 on 13.40, 2026-10-20). Not sceKernelGetSystemSwVersion's text: an enabler spoofs it (the Pro said 12.09,
 * the standard PS5 13.59), and a message that names the wrong firmware sends people after the wrong fix (src/pw_firmware.h). */
static std::string system_firmware()
{
	char text[8];
	if (pw_firmware_text(pw_firmware_installed(nullptr), text, sizeof(text))) return text;
	return {};
}
#endif

std::string proton_data_problem()
{
#if defined(PS5_HOST_REFERENCE)
	return "No access to /data: this is the PC build, which runs no games.";
#else
	if (gDataGranted) return {};
	std::string why;
	const std::string firmware = system_firmware();
	const std::string onFirmware = firmware.empty() ? "" : " This console runs firmware " + firmware + ".";
	/* A loader that mounts /data into the sandbox (ShadowMountPlus) lets the title see dev.conf before it is root. */
	if (path_exists("/data/homebrew") && !path_exists("/data/homebrew/dev.conf"))
		why = "/data/homebrew/dev.conf is missing (a format removes it), and the access daemon stops without it. "
			"Recreate it with the two lines DEV_SERVER=127.0.0.1 and DEV_PORT=8080, then reopen " PS5_TITLE_NAME ".";
	else if (gWebsrvDown)
		why = "websrv is not answering, so the access daemon could not be started. Load elfldr and websrv, then reopen "
			PS5_TITLE_NAME ".";
	else if (gDataRetry)
		why = "the access daemon was started but did not grant it within 15 s." + onFirmware +
			" Check that /data/homebrew/dev.conf holds DEV_SERVER=127.0.0.1 and DEV_PORT=8080, then reopen "
			PS5_TITLE_NAME ".";
	else if (!gDataNote.empty())
		why = gDataNote + ".";
	else
		why = std::string("the access daemon did not grant it (") + proton_data_status() + ").";
	return "No access to /data: " + why + " After rest mode, payloads are gone until the PS5 is fully restarted.";
#endif
}

int proton_runtime_state()
{
	const std::string runtime = PROTON_DATA_ROOT "/runtime/ge11-7";
	auto read = [](const std::string &path) {
		std::string text;
		if (FILE *file = fopen(path.c_str(), "rb")) {
			char buffer[128];
			text.assign(buffer, fread(buffer, 1, sizeof(buffer), file));
			fclose(file);
		}
		return text;
	};
	return pw_runtime_state(access((runtime + "/lib/wine/x86_64-unix/ntdll.prx").c_str(), R_OK) == 0,
	                        read(runtime + "/runtime-stamp").c_str(), read(proton_title_path("/app0/runtime-stamp")).c_str());
}

std::string proton_build_identity()
{
	auto read = [](const std::string &path) {
		std::string text;
		if (FILE *file = fopen(path.c_str(), "rb")) {
			char buffer[4096];
			text.assign(buffer, fread(buffer, 1, sizeof(buffer), file));
			fclose(file);
		}
		return text;
	};
	auto short_stamp = [](std::string text) {
		text = text.substr(0, text.find_first_of(" \t\r\n"));
		return text.empty() ? std::string("-") : text.substr(0, 16);
	};
	const nlohmann::json param = nlohmann::json::parse(read(proton_title_path("/app0/sce_sys/param.json")), nullptr, false);
	std::string version = "-";
	if (param.is_object()) {
		const auto found = param.find("contentVersion");
		if (found != param.end() && found->is_string()) version = found->get<std::string>();
	}
	static const char *const states[] = { "this build's", "missing", "another build's" };
	const int state = proton_runtime_state();
	return "title " + version + ", runtime stamp pinned " + short_stamp(read(proton_title_path("/app0/runtime-stamp"))) +
		" installed " + short_stamp(read(PROTON_DATA_ROOT "/runtime/ge11-7/runtime-stamp")) + " (" +
		states[state >= 0 && state <= 2 ? state : 2] + ")";
}

std::string proton_runtime_problem()
{
	const std::string runtime = PROTON_DATA_ROOT "/runtime/ge11-7";
	switch (proton_runtime_state()) {
	case PW_RUNTIME_MISSING:
		return "The Wine runtime is not installed in " + runtime + ". Install it, then reopen " PS5_TITLE_NAME ".";
	case PW_RUNTIME_OTHER_BUILD:
		return "The Wine runtime on this console is from another " PS5_TITLE_NAME " build than this one. Install the "
			"runtime of this build: the setup's install opens next.";
	default:
		return {};
	}
}

namespace {

/* The title's own eboot. /app0 is not visible once /data is granted, so the
 * sandbox's view of the same file is tried next (as prospero-win does). */
const char *const selfEboots[] = { "/app0/eboot.bin", PROTON_SANDBOX_APP0 "/eboot.bin" };

ProtonLaunch gLaunch;

std::string trim(const std::string &text)
{
	const char *spaces = " \t\r\n";
	const size_t first = text.find_first_not_of(spaces);
	if (first == std::string::npos) {
		return std::string();
	}
	return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}

bool file_exists(const char *path)
{
	struct stat status;
	return stat(path, &status) == 0;
}

/* An absolute Windows path: a drive letter, a colon, a separator. */
bool absolute_windows_path(const std::string &path)
{
	return path.size() > 2 && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

/* A game directory's game.json: name, detail, executable, arguments, directory
 * (where the program starts, as a shortcut's "Start in") and env (a list of
 * "NAME=VALUE" words for the program's environment). Every field is optional:
 * a directory with no game.json is still listed, by name, so an install that
 * is only half done is visible instead of invisible. */
ProtonGame read_game(const std::string &id, const std::string &directory)
{
	ProtonGame game;
	game.id = id;
	game.name = id;
	game.folder = directory;

	const std::string path = directory + "/game.json";
	FILE *file = fopen(path.c_str(), "rb");
	if (!file) {
		return game;
	}
	std::string text;
	char buffer[4096];
	size_t read = 0;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		text.append(buffer, read);
	}
	fclose(file);

	try {
		nlohmann::json json = nlohmann::json::parse(text);
		auto word = [&json](const char *key, std::string &into) {
			auto found = json.find(key);
			if (found != json.end() && found->is_string()) {
				const std::string value = trim(found->get<std::string>());
				if (!value.empty()) {
					into = value;
				}
			}
		};
		word("name", game.name);
		word("detail", game.detail);
		word("executable", game.executable);
		word("arguments", game.arguments);
		word("directory", game.directory);
		word("prefix", game.prefix);
		auto desktop = json.find("desktop");
		if (desktop != json.end() && desktop->is_boolean()) game.desktop = desktop->get<bool>();
		auto launcher = json.find("launcher");
		if (launcher != json.end() && launcher->is_boolean()) game.launcher = launcher->get<bool>();   // listed unless false
		if (proton_game_prefix(game).empty()) {
			game.complaint = "prefix must be a folder ID of up to 64 letters, digits, underscores or hyphens";
		}
		if (!game.directory.empty() && !absolute_windows_path(game.directory)) {
			game.complaint = "directory is not an absolute Windows path (C:\\... or Z:\\...): " + game.directory;
		}
		auto variables = json.find("env");
		if (variables != json.end() && variables->is_array()) {
			for (const auto &item : *variables) {
				const std::string setting = item.is_string() ? trim(item.get<std::string>()) : std::string();
				if (setting.find('=') == std::string::npos || setting[0] == '=') {
					game.complaint = "env words are NAME=VALUE: " + setting;
				} else if (setting.rfind("WINE_PS5_PROCESS_RULES=", 0) == 0) {
					// An entry an older build wrote keeps its rules and gains this build's (src/pw_steam.h).
					char merged[2048];
					const size_t length = pw_steam_rules_merge(setting.c_str() + strlen("WINE_PS5_PROCESS_RULES="),
					                                           merged, sizeof(merged));
					game.env.push_back(length ? "WINE_PS5_PROCESS_RULES=" + std::string(merged) : setting);
				} else {
					game.env.push_back(setting);
				}
			}
		}
		proton_check_program(game);
		if (game.executable.empty()) {
			game.complaint = "no executable in game.json: the launcher lists it, the runtime cannot run it";
		}
	} catch (const std::exception &error) {
		game.complaint = std::string("game.json is not readable json: ") + error.what();
		proton_say("game %s: %s", id.c_str(), game.complaint.c_str());
	}
	if (!game.complaint.empty()) {
		proton_say("game %s (%s): %s", id.c_str(), game.name.c_str(), game.complaint.c_str());
	}
	return game;
}

} // namespace

/* A program's checks, for a library entry and for a program run by path (the launcher's Files, ProtonGame::run_path):
 * an absolute Windows path that is there (found ignoring case), and a 64-bit program, or a 32-bit one where the runtime
 * ships its i386 modules (then in the launcher layout, as WoW64 runs only in a helper). What fails is game.complaint. */
void proton_check_program(ProtonGame &game)
{
	if (!game.executable.empty()) {
		const bool absolute = absolute_windows_path(game.executable);
		if (!absolute) {
			game.complaint = "executable is not an absolute Windows path (C:\\... or Z:\\...): " +
				game.executable;
		} else if (game.executable.size() > 3) {
			/* Wine maps Z: onto the host root and C: onto the game's prefix
			 * (dosdevices: c: is ../drive_c), so both can be checked here: a
			 * typo, or a C:\\data path that names the prefix instead of /data
			 * (2026-10-04: three library entries did, and each launch ended at
			 * "failed to open"), shows in the library instead of in a run. */
			const char drive = (char)(game.executable[0] | 0x20);
			std::string rest = game.executable.substr(2);
			for (char &character : rest) {
				if (character == '\\') {
					character = '/';
				}
			}
			const std::string prefix = proton_game_prefix(game) + "/drive_c";
			std::string host = drive == 'z' ? rest : drive == 'c' ? prefix + rest : std::string();
			struct stat status;
			/* Windows finds "client.exe" where an installer wrote "Client.exe"; the file system does not (src/pw_path_nocase.h). */
			char held[4096];
			if (!host.empty() && stat(host.c_str(), &status) != 0 && pw_path_nocase(host.c_str(), held, sizeof(held))) host = held;
			if (!host.empty() && stat(host.c_str(), &status) != 0) {
				game.complaint = "the executable is not there: " + host;
				if (drive == 'c' && stat(rest.c_str(), &status) == 0) {
					game.complaint += " (C: is the game's prefix; this file is Z:" +
						game.executable.substr(2) + ")";
				}
			} else if (!host.empty()) {
				/* A 32-bit program runs under WoW64 when the runtime ships its i386
				 * modules (docs/IA32_DESIGN.md); without them it is refused here, not
				 * started: the first launch of a 32-bit-required .NET game (an FNA game,
				 * 2026-10-06), on a runtime without them, restarted the console. */
				struct PwPeInfo pe;
				const enum PwPeArch arch = pw_pe_arch_file(host.c_str(), &pe);
				struct stat i386_modules;
				const std::string i386_dir = std::string(PROTON_DATA_ROOT) + "/runtime/ge11-7/lib/wine/i386-windows";
				if (arch == PW_PE_X86) {
					game.x86_32 = true;
					if (stat(i386_dir.c_str(), &i386_modules) != 0 || !S_ISDIR(i386_modules.st_mode)) {
						game.complaint = pe.clr ?
							"a 32-bit .NET program (it requires 32 bits), and this runtime has no 32-bit modules" :
							"a 32-bit Windows program, and this runtime has no 32-bit modules";
					} else {
						/* Under WoW64 a program runs in a helper only: its signals and 32-bit TEBs
						 * rest on the payload loader and the helper's address space. The launcher
						 * layout, then, whatever game.json chose (a test's own override still wins). */
						for (auto it = game.env.begin(); it != game.env.end();)
							it = it->rfind("PROTON_LAYOUT=", 0) == 0 ? game.env.erase(it) : it + 1;
						game.env.push_back("PROTON_LAYOUT=launcher");
					}
				} else if (arch == PW_PE_OTHER) {
					char machine[16];
					snprintf(machine, sizeof(machine), "0x%04x", pe.machine);
					game.complaint = std::string("not an x86-64 program (machine ") + machine + ")";
				}
			}
		}
	}
}

/* A game's own setting: its game.json's env word NAME=value, or none of NAME's when value is empty. Atomic: the file is
 * written beside and renamed over. "" when saved, else why not. */
std::string proton_game_set_env(const ProtonGame &game, const std::string &name, const std::string &value)
{
	if (game.folder.empty()) return "this entry has no game.json";
	const std::string path = game.folder + "/game.json", temporary = path + ".tmp";
	nlohmann::json entry = nlohmann::json::object();
	if (FILE *file = fopen(path.c_str(), "rb")) {
		std::string text;
		char buffer[4096];
		for (size_t got; (got = fread(buffer, 1, sizeof(buffer), file)) > 0;) text.append(buffer, got);
		fclose(file);
		entry = nlohmann::json::parse(text, nullptr, false);
		if (!entry.is_object()) return "game.json is not readable json";
	}
	nlohmann::json env = nlohmann::json::array();
	if (entry.count("env") && entry["env"].is_array())
		for (const auto &word : entry["env"])
			if (!word.is_string() || word.get<std::string>().rfind(name + "=", 0) != 0) env.push_back(word);
	if (!value.empty()) env.push_back(name + "=" + value);
	if (env.empty()) entry.erase("env");
	else entry["env"] = env;
	const std::string text = entry.dump(2) + "\n";
	FILE *file = fopen(temporary.c_str(), "wb");
	bool saved = file && fwrite(text.data(), 1, text.size(), file) == text.size() && !fflush(file) && !fsync(fileno(file));
	if (file && fclose(file)) saved = false;
	if (!saved || rename(temporary.c_str(), path.c_str())) {
		remove(temporary.c_str());
		return std::string("game.json could not be saved: ") + strerror(errno);
	}
	return "";
}

__attribute__((format(printf, 1, 2))) void proton_say(const char *format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	say("%s", line);
}

void proton_set_args(int argc, char **argv)
{
	gLaunch = ProtonLaunch();
	for (int i = 1; i < argc; i++) {
		const std::string word = argv[i] ? argv[i] : "";
		const size_t equals = word.find('=');
		if (equals == std::string::npos) {
			continue;
		}
		const std::string key = word.substr(0, equals);
		const std::string value = trim(word.substr(equals + 1));
		if (key == "profile") {
			gLaunch.profile = value;
			gLaunch.game = !value.empty();
		} else if (key == "path") {
			gLaunch.path = value;
			gLaunch.game = gLaunch.game || !value.empty();
		} else if (key == "cycle") {
			gLaunch.cycle = (unsigned)strtoul(value.c_str(), nullptr, 10);
		} else if (key == "env" && value.find('=') != std::string::npos && value[0] != '=') {
			gLaunch.env.push_back(value);
		} else if (key == "args") {
			gLaunch.arguments = value;
			gLaunch.arguments_override = true;
		} else if (key == "aurelia") {
			gLaunch.aurelia = !value.empty() && value != "0";
		} else if (key == "ended") {
			gLaunch.ended = value;
		}
	}
	proton_say("launch: mode=%s profile=%s path=%s cycle=%u argc=%d env=%zu%s%s",
		gLaunch.game ? "game" : "launcher", gLaunch.profile.c_str(), gLaunch.path.c_str(),
		gLaunch.cycle, argc, gLaunch.env.size(), gLaunch.ended.empty() ? "" : " ended=", gLaunch.ended.c_str());
}

const ProtonLaunch &proton_launch()
{
	return gLaunch;
}

std::string proton_game_prefix(const ProtonGame &game)
{
	char id[65];
	const std::string root = PROTON_DATA_ROOT "/prefixes/";
	if (pw_wine_launch_prefix_id(game.prefix.c_str(), game.id.c_str(), id, sizeof(id))) return {};
	return root + id;
}

/* The game's program on the console's disk: Z: is the root, C: the game's prefix (read_game checks the same way). */
static std::string host_executable(const ProtonGame &game)
{
	const std::string &path = game.executable;
	if (path.size() < 4 || path[1] != ':') return {};
	const char drive = (char)(path[0] | 0x20);
	std::string rest = path.substr(2);
	for (char &character : rest) if (character == '\\') character = '/';
	const std::string prefix = proton_game_prefix(game);
	const std::string host = drive == 'z' ? rest : drive == 'c' && !prefix.empty() ? prefix + "/drive_c" + rest : std::string();
	char held[4096];
	return !host.empty() && pw_path_nocase(host.c_str(), held, sizeof(held)) ? std::string(held) : std::string();
}

void proton_add_steam_ids(ProtonGame &game)
{
	game.steam_words.clear();
	char found[PW_STEAM_ID_MAX] = "";
	const std::string host = host_executable(game);
	if (!host.empty()) pw_steam_appid_of(host.c_str(), found, sizeof(found));
	std::vector<const char *> env;
	for (const auto &word : game.env) env.push_back(word.c_str());
	char words[2][PW_STEAM_ID_WORD];
	const size_t added = pw_steam_id_words(env.data(), env.size(), found[0] ? found : nullptr, words);
	for (size_t i = 0; i < added; ++i) game.steam_words.push_back(words[i]);
}

std::vector<ProtonGame> proton_read_games(std::string *problem)
{
	std::vector<ProtonGame> games;
	std::string searched;
	if (problem) {
		problem->clear();
	}
	for (const std::string &root : proton_game_roots()) {
		std::string seen;
		DIR *directory = opendir(root.c_str());
		if (!directory) {
			seen = "not there";
		} else {
			int entries = 0;
			while (struct dirent *entry = readdir(directory)) {
				const std::string name = entry->d_name;
				if (name.empty() || name[0] == '.') {
					continue;
				}
				const std::string path = root + "/" + name;
				struct stat status;
				if (stat(path.c_str(), &status) != 0) {
					continue;
				}
				entries++;
				if (!S_ISDIR(status.st_mode)) {
					continue;   /* counted, but a game is a directory */
				}
				ProtonGame game = read_game(name, path);
				if (pw_parked_profile(game.id.c_str())) {
					continue;   /* parked: kept on the console, never listed (src/pw_parked.h) */
				}
				bool known = false;
				for (const ProtonGame &earlier : games) {
					known = known || earlier.id == game.id;
				}
				if (!known) {
					games.push_back(game);
				}
			}
			closedir(directory);
			char count[32];
			snprintf(count, sizeof(count), "%d entr%s", entries, entries == 1 ? "y" : "ies");
			seen = count;
		}
		searched += (searched.empty() ? "" : "; ") + root + " (" + seen + ")";
	}
	if (problem) {
		*problem = "searched: " + searched;
		/* An empty list without /data says nothing about what is installed. */
		if (games.empty() && gDataAttempted && !gDataGranted) {
			*problem = std::string("Library unavailable: /data not granted (") + proton_data_status() + "). " + *problem;
		} else if (games.empty()) {
			*problem = "No games installed. " + *problem;
		}
	}
	/* A real Wine shell is the persistent root process. Applications it launches
	 * are ordinary children; their exit does not end the owner's session. */
	bool desktop_known = false;
	for (const auto &game : games) desktop_known = desktop_known || game.id == "mihawk-os";
	if (!desktop_known) {
		ProtonGame desktop;
		desktop.id = "mihawk-os";
		desktop.name = "Mihawk OS";
		desktop.detail = "Shared Windows desktop";
		desktop.prefix = "mihawk";
		desktop.executable = "C:\\windows\\system32\\mihawk.exe";
		desktop.directory = "C:\\";
		desktop.env = {"PROTON_LAYOUT=launcher", "WINE_PS5_WINDOW_SURFACES=1", "MIHAWK_DESKTOP=1"};
		games.push_back(desktop);
	}
	/* Stable order: the launcher must show the same list every launch. */
	for (size_t i = 1; i < games.size(); i++) {
		for (size_t j = i; j > 0 && games[j].id < games[j - 1].id; j--) {
			std::swap(games[j], games[j - 1]);
		}
	}
	return games;
}

namespace { std::function<void()> pendingRestart; }

void proton_queue_restart(const ProtonGame *game, unsigned cycle, const char *reason)
{
	pendingRestart = [hasGame = game != nullptr, chosen = game ? *game : ProtonGame{}, cycle,
		why = std::string(reason)] { proton_restart(hasGame ? &chosen : nullptr, cycle, why.c_str()); };
}

bool proton_restart_pending()
{
	return bool(pendingRestart);
}

void proton_finish_restart()
{
	auto restart = std::move(pendingRestart);
	pendingRestart = nullptr;
	if (restart) restart();
}

void proton_restart(const ProtonGame *game, unsigned cycle, const char *reason, const char *ended)
{
	char storage[5][1024];
	char *argv[6];
	int count = 0;
	if (game) {
		snprintf(storage[count], sizeof(storage[count]), "profile=%s", game->id.c_str());
		argv[count] = storage[count];
		count++;
		/* The launcher's Aurelia page chooses the client's command (sign-in, refresh, install): the session then runs
		 * it instead of the profile's own arguments. Empty keeps the profile's. */
		if (!game->arguments.empty()) {
			snprintf(storage[count], sizeof(storage[count]), "args=%s", game->arguments.c_str());
			argv[count] = storage[count];
			count++;
		}
		/* A program the launcher's Files chose, run with this entry's prefix and settings (the title's path= launch) */
		if (!game->run_path.empty()) {
			snprintf(storage[count], sizeof(storage[count]), "path=%s", game->run_path.c_str());
			argv[count] = storage[count];
			count++;
		}
	}
	snprintf(storage[count], sizeof(storage[count]), "cycle=%u", cycle);
	argv[count] = storage[count];
	count++;
	if (ended && *ended) {
		/* One line of wine-run.txt: a reason's own line breaks become spaces. */
		snprintf(storage[count], sizeof(storage[count]), "ended=%s", ended);
		for (char *c = storage[count]; *c; ++c)
			if (*c == '\n' || *c == '\r') *c = ' ';
		argv[count] = storage[count];
		count++;
	}
	argv[count] = nullptr;

#if defined(PS5_HOST_REFERENCE)
	/* The PC has no shell to replace and no eboot: the reference build says what
	 * the console would do, so the menus and the self-test can be exercised here. */
	std::string words;
	for (int i = 0; i < count; ++i) words += (i ? " " : "") + std::string(argv[i]);
	proton_say("restart: (host reference) to=%s cycle=%u reason=%s argv=%s",
		game ? game->id.c_str() : "launcher", cycle, reason, words.c_str());
	(void)selfEboots;
#else
	// Preserve the selection independently of LoadExec argument conventions.
	// Consume the request exactly once after filesystem elevation.
	const std::string request = proton_title_path("/app0/wine-run.txt");
	const std::string temporary = request + ".tmp";
	FILE *file = fopen(temporary.c_str(), "wb");
	bool saved = file != nullptr;
	if (file) {
		for (int i = 0; i < count; ++i)
			if (fprintf(file, "%s\n", argv[i]) < 0) saved = false;
		if (fflush(file) != 0 || fsync(fileno(file)) != 0) saved = false;
		if (fclose(file) != 0) saved = false;
	}
	if (!saved || rename(temporary.c_str(), request.c_str()) != 0) {
		remove(temporary.c_str());
		proton_say("restart: cannot save launch request; staying in launcher");
		return;
	}
	for (size_t i = 0; i < sizeof(selfEboots) / sizeof(selfEboots[0]); i++) {
		proton_say("restart: to=%s cycle=%u reason=%s eboot=%s", game ? game->id.c_str() : "launcher",
			cycle, reason, selfEboots[i]);
		sceSystemServiceLoadExec(selfEboots[i], argv);
	}
	remove(request.c_str());
	proton_say("restart: refused, staying where we are");
#endif
}

bool proton_setup_unattended(bool frameBudget)
{
	return frameBudget && file_exists(PROTON_SETUP_UNATTENDED);
}

bool proton_selftest_active()
{
	return file_exists(proton_title_path("/app0/restart-check").c_str());
}

void proton_selftest_note(const char *what, unsigned cycle)
{
	const std::string log = proton_title_path("/app0/restart-check.log");
	FILE *file = fopen(log.c_str(), "a");
	if (!file) {
		proton_say("selftest: cannot write %s", log.c_str());
		return;
	}
	fprintf(file, "%s cycle=%u pid=%d\n", what, cycle, (int)getpid());
	fclose(file);
	chmod(log.c_str(), 0666);
	proton_say("selftest: %s cycle=%u", what, cycle);
}

#if !defined(PS5_HOST_REFERENCE)
namespace {

/* One request to a payload on the loopback; the reply's body, or empty. */
std::string loopback_http(int port, const std::string &request)
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return {};
	struct timeval timeout = { 1, 0 };
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	struct sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons((uint16_t)port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	std::string reply;
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0 && send_all(fd, request.data(), request.size())) {
		char buffer[4096];
		ssize_t got;
		while (reply.size() < 65536 && (got = recv(fd, buffer, sizeof(buffer), 0)) > 0) reply.append(buffer, (size_t)got);
	}
	close(fd);
	const size_t body = reply.find("\r\n\r\n");
	return body == std::string::npos ? std::string() : reply.substr(body + 4);
}

/* A string field of a JSON reply, or empty. */
std::string json_text(const std::string &body, const char *field)
{
	const auto json = nlohmann::json::parse(body, nullptr, false);
	const auto found = json.is_object() ? json.find(field) : json.end();
	return found != json.end() && found->is_string() ? found->get<std::string>() : std::string();
}

} // namespace
#endif

static PwPayloadReport gPayloads;
static PwPayloadFacts gPayloadFacts;
static bool gPayloadsTaken = false;

#if !defined(PS5_HOST_REFERENCE)
/* Every process kern.proc lists; *listed 1 when it answered */
static std::vector<PwPayloadProcess> list_processes(int *listed)
{
	std::vector<PwPayloadProcess> list;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
	size_t bytes = 0;
	if (!sysctl(mib, 4, nullptr, &bytes, nullptr, 0) && bytes) {
		bytes += bytes / 4;
		std::vector<char> raw(bytes);
		if (!sysctl(mib, 4, raw.data(), &bytes, nullptr, 0)) {
			*listed = 1;
			const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
			/* The console's records are ki_structsize apart (1096 bytes, the SDK's 1088); the fields read come first. */
			for (size_t at = 0; at + sizeof(int) <= bytes;) {
				int size;
				memcpy(&size, raw.data() + at, sizeof(size));
				if (size <= 0 || at + (size_t)size > bytes) break;
				struct kinfo_proc record = {};
				memcpy(&record, raw.data() + at, std::min((size_t)size, sizeof(record)));
				at += (size_t)size;
				PwPayloadProcess process = { record.ki_pid, record.ki_ppid, {}, (uint64_t)record.ki_rssize * page,
					(int64_t)record.ki_start.tv_sec };
				snprintf(process.name, sizeof(process.name), "%s", record.ki_comm);
				list.push_back(process);
			}
		}
	}
	return list;
}
extern "C" int sceKernelGetResidentFmemCount(int pid, uint64_t *resident, uint64_t *mapped);
#endif

PwSteamCensus proton_system_memory()
{
	PwSteamCensus out{};
#if defined(PS5_HOST_REFERENCE)
	/* The PC has no such memory; the render test's made-up figures: PROTON_MEMORY_TEST="system[,payloads MiB,payloads]" */
	if (const char *test = getenv("PROTON_MEMORY_TEST"); test && *test)
		out.known = sscanf(test, "%u,%u,%u", &out.system_mib, &out.payload_mib, &out.payload_count) >= 1;
#else
	/* The memory helper's census (src/pw_memory_helper.c census_take), taken here, when Steam is chosen: every process's
	 * resident memory but the title's (sceKernelGetResidentFmemCount, 16 KiB pages), the user's payloads apart. No /data,
	 * no list or no figure for any process: unknown, and the launcher never warns on it. */
	int listed = 0;
	const std::vector<PwPayloadProcess> list = gDataGranted ? list_processes(&listed) : std::vector<PwPayloadProcess>();
	int syscore = 0;
	for (const auto &process : list)
		if (!strncmp(process.name, "SceSysCore", 10)) syscore = process.pid;
	const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
	uint64_t system = 0, payloads = 0;
	for (const auto &process : list) {
		uint64_t pages = 0, mapped = 0;
		if (process.pid == getpid() || sceKernelGetResidentFmemCount(process.pid, &pages, &mapped)) continue;
		system += pages * page;
		out.processes++;
		if (process.pid != syscore && pw_memory_is_payload(process.ppid, syscore, process.name)) {
			payloads += pages * page;
			out.payload_count++;
		}
	}
	out.known = out.processes > 0;
	out.system_mib = (unsigned)(system >> 20);
	out.payload_mib = (unsigned)(payloads >> 20);
#endif
	return out;
}

const PwPayloadReport &proton_payloads_check()
{
	gPayloadsTaken = true;
	memset(&gPayloadFacts, 0, sizeof(gPayloadFacts));
#if defined(PS5_HOST_REFERENCE)
	memset(&gPayloads, 0, sizeof(gPayloads));
	gPayloads.listed = gPayloads.pass = 1;   /* the PC has no payloads */
	if (const char *test = getenv("PROTON_PAYLOAD_TEST"); test && !strcmp(test, "fail")) {
		/* The render test's failing check (tools/test-launcher-ui.py): no Kstuff, and one payload too many */
		static const PwPayloadProcess table[] = { { 53, 1, "SceSysCore.elf", 0, 0 }, { 69, 53, "SceRedisServer", 0, 0 },
			{ 87, 69, "elfldr.elf", 24u << 20, 0 },
			{ 101, 87, "shadowmountplus.elf", 25u << 20, 0 }, { 102, 87, "websrv.elf", 8u << 20, 0 },
			{ 130, 87, "extra.elf", 6u << 20, 0 }, { 120, 53, "eboot.bin", 300u << 20, 0 } };
		gPayloadFacts.listed = 1;
		pw_payload_check(table, sizeof(table) / sizeof(table[0]), 120, &gPayloadFacts, &gPayloads);
	}
	return gPayloads;
#else
	std::vector<PwPayloadProcess> list = list_processes(&gPayloadFacts.listed);
	/* kstuff by its process: a title may not make a raw system call (a kekcall from the title ended it with
	 * SYSTEM_ILLEGAL_FUNCTION_CALL, "directly issued a syscall 39", payload-quick-2026-10-29-01), so its version is not read */
	for (const auto &process : list)
		if (!strcmp(process.name, "kstuff.elf") || !strcmp(process.name, "kstuff")) gPayloadFacts.kstuff = PW_KSTUFF_OK;
	snprintf(gPayloadFacts.shadowmount_version, sizeof(gPayloadFacts.shadowmount_version), "%s",
		json_text(loopback_http(10101, "POST /api/v1/version HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
			"Content-Length: 2\r\nConnection: close\r\n\r\n{}"), "shadowmount_version").c_str());
	snprintf(gPayloadFacts.websrv_version, sizeof(gPayloadFacts.websrv_version), "%s",
		json_text(loopback_http(PW_ELFLDR_PORT, "GET /version HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"),
			"tag").c_str());
	gPayloadFacts.data_denied = !gDataGranted;
	if (FILE *record = fopen(proton_title_path(PROTON_DAEMON_STARTED).c_str(), "r")) {
		long long when = 0;
		if (fscanf(record, "%lld", &when) == 1) gPayloadFacts.daemon_started = when;
		fclose(record);
	}
	pw_payload_check(list.data(), (unsigned)list.size(), (int)getpid(), &gPayloadFacts, &gPayloads);
	int syscore = 0;
	for (const auto &process : list)
		if (!strncmp(process.name, "SceSysCore", 10)) syscore = process.pid;
	for (const auto &process : list)
		if (pw_memory_is_payload(process.ppid, syscore, process.name))
			proton_say("payloads: pid=%d ppid=%d name=%s resident=%.1f MiB started=%lld", process.pid, process.ppid,
				process.name, process.resident / 1048576.0, (long long)process.started);
	proton_say("payloads: pass=%d listed=%d processes=%zu kstuff=%s shadowmountplus=%s websrv=%s others=%u (%.1f MiB) "
		"payloads=%.1f MiB unnamed=%u daemon_started=%lld", gPayloads.pass, gPayloads.listed, list.size(),
		gPayloadFacts.kstuff == PW_KSTUFF_OK ? "running" : "absent",
		gPayloadFacts.shadowmount_version[0] ? gPayloadFacts.shadowmount_version : "-",
		gPayloadFacts.websrv_version[0] ? gPayloadFacts.websrv_version : "-", gPayloads.others,
		gPayloads.other_resident / 1048576.0, gPayloads.payload_resident / 1048576.0, gPayloads.unnamed,
		(long long)gPayloadFacts.daemon_started);
	return gPayloads;
#endif
}

const PwPayloadReport &proton_payloads()
{
	return gPayloadsTaken ? gPayloads : proton_payloads_check();
}

const PwPayloadFacts &proton_payload_facts()
{
	proton_payloads();
	return gPayloadFacts;
}

std::string proton_payloads_problem()
{
	// Not seeing the processes is the access problem the launcher already names (dev.conf, websrv, the daemon), not a
	// missing payload
	if (!proton_payloads().listed && !proton_data_problem().empty()) return proton_data_problem();
	char advice[1024];
	pw_payload_advice(&proton_payloads(), &gPayloadFacts, advice, sizeof(advice));
	return advice;
}
