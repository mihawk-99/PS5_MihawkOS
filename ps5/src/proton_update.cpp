/*
 * MihawkOS - "Check for updates" (proton_update.h).
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "proton_update.h"
#include "proton_fetch.h"
#include "proton_install.h"
#include "proton_launch.h"
#include "../../src/pw_release.h"
#include "../../src/pw_sha256.h"
#include "../../src/pw_update.h"
#include "../../src/pw_update_flow.h"
#include "stb_image.h"   // its inflate: the title carries stb_image for its PNGs (base/VulkanglTFModel.cpp)

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdlib>
#include <ctime>

#include "json.hpp" // external/tinygltf's vendored nlohmann::json, already in the title

#if !defined(PS5_HOST_REFERENCE)
extern "C" int sceSystemServiceLoadExec(const char *path, char *const argv[]);
#endif

/* Where the owner publishes the signed manifest, and the public half of their key (64 hex digits,
 * platform/update/release-key.txt, which tools/sign-release.py keygen writes). Both empty until the owner publishes. */
#ifndef PROTON_UPDATE_URL
#define PROTON_UPDATE_URL ""
#endif
#ifndef PROTON_RELEASE_KEY
#define PROTON_RELEASE_KEY ""
#endif
#ifndef PROTON_VERSION
#define PROTON_VERSION ""
#endif
/* Where the releases are, when no address is set: the public repository's listing, newest found by tag, never
 * /releases/latest (every MihawkOS release is a pre-release, which "latest" skips). Only GitHub is asked; the request
 * carries no account and nothing about the console (pw_https sends "User-Agent: MihawkOS/1"). */
#define PROTON_RELEASES_URL "https://api.github.com/repos/mihawk-99/PS5_MihawkOS/releases?per_page=20"
/* The release asset the title reads first: the signed manifest (src/pw_release.h, tools/sign-release.py). */
#define PROTON_RELEASE_ASSET "mihawkos-release.txt"

namespace {

constexpr const char *kUpdate = PROTON_DATA_ROOT "/update";
/* Settings > Automatic update check: "0" turns it off; on otherwise. When it last asked (seconds since the epoch), and
 * the release it last found (kept so the main menu still says so on later starts that day). */
constexpr const char *kAutoFile = PROTON_DATA_ROOT "/update/auto.txt";
constexpr const char *kLastCheckFile = PROTON_DATA_ROOT "/update/last-check";
constexpr const char *kFoundFile = PROTON_DATA_ROOT "/update/available.txt";
constexpr uint64_t kMarginBytes = 64ull << 20;

std::mutex gLock;
std::atomic<bool> gCancel{ false };   /* shortcut: no cancel on screen; the title pack is small (about 25 MB) */
ProtonUpdate gState;
PwUpdateOffer gOffer;                 /* the release found, downloaded at once (the owner, 2026-10-31) */
std::atomic<uint64_t> gDone{ 0 };     /* its bytes downloaded so far */
bool gSwitchedHere = false;   /* this process switched and its restart was refused: it is not the new title */
std::chrono::steady_clock::time_point gStarted;

std::string read_file(const std::string &path)
{
	std::string text;
	if (FILE *file = fopen(path.c_str(), "rb")) {
		char buffer[16384];
		size_t got;
		while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
		fclose(file);
	}
	return text;
}

std::string running_version()
{
	const nlohmann::json param = nlohmann::json::parse(read_file(proton_title_path("/app0/sce_sys/param.json")), nullptr, false);
	if (!param.is_object()) return {};
	const auto version = param.find("contentVersion");
	return version != param.end() && version->is_string() ? version->get<std::string>() : std::string();
}

/* A PC fixture's address in the title folder (a rehearsal), else the build's. */
std::string source()
{
	std::string url = read_file(proton_title_path("/app0/update-source.txt"));
	url = url.substr(0, url.find_first_of("\r\n"));
	if (url.empty()) url = PROTON_UPDATE_URL;
	const bool web = !url.compare(0, 7, "http://") || !url.compare(0, 8, "https://");
	return web && url.find_first_of(" \t\"'") == std::string::npos ? url : std::string();
}

void write_file(const char *path, const std::string &text)
{
	mkdir(kUpdate, 0777);
	if (FILE *file = fopen(path, "wb")) {
		fwrite(text.data(), 1, text.size(), file);
		fclose(file);
	}
}

/* pw_update_flow's way to fetch: the title's own TLS (proton_fetch.h) */
std::string fetch(const std::string &url, uint64_t limit, const std::function<bool(const void *, size_t)> &sink)
{
	const ProtonFetch got = proton_fetch(url, limit, sink);
	return got.ok ? std::string() : got.error.empty() ? std::string("the download stopped.") : got.error;
}

void finish(ProtonUpdate found)
{
	proton_say("update: %s %s: %s", found.phase == ProtonUpdate::failed ? "failed" : "state", found.version.c_str(),
		found.message.c_str());
	// The main menu's notice on later starts: kept while a release is offered, dropped once none is
	if (found.phase == ProtonUpdate::available) write_file(kFoundFile, found.version + "\n");
	else if (found.phase == ProtonUpdate::current) remove(kFoundFile);
	std::lock_guard<std::mutex> hold(gLock);
	gState = found;
}

void check()
{
	ProtonUpdate found;
	found.phase = ProtonUpdate::failed;
	const std::string running = running_version();
	char staged[64];
	if (pw_update_pending(kUpdate, staged, sizeof(staged))) {
		found.phase = ProtonUpdate::ready;
		found.version = staged;
		found.message = PS5_TITLE_NAME " " + found.version + " is downloaded. It is switched in when " PS5_TITLE_NAME
			" starts: press CROSS to restart now.";
		return finish(found);
	}
	uint8_t key[32];
	if (!pw_release_hex32(PROTON_RELEASE_KEY, key)) {
		found.message = "This build carries no release key, so it cannot check for updates.";
		return finish(found);
	}
	PwUpdateOffer offer;
	const std::string error = pw_update_find(fetch, PROTON_RELEASES_URL, source(), PROTON_VERSION, running.c_str(), key, offer);
	if (!error.empty()) {
		found.message = PS5_TITLE_NAME " could not check for updates: " + error;
		return finish(found);
	}
	if (!offer.found) {
		found.phase = ProtonUpdate::current;
		found.message = PS5_TITLE_NAME " " + running + " is the newest release.";
		return finish(found);
	}
	found.phase = ProtonUpdate::available;
	found.version = offer.version;
	found.download = offer.zip_size;
	found.message = PS5_TITLE_NAME " " + found.version + " is available.";
	{
		std::lock_guard<std::mutex> hold(gLock);
		gOffer = offer;
	}
	finish(found);
	proton_update_download();   /* at once, in the background, while the launcher is up (the owner, 2026-10-31) */
}

/* stb_image's inflate, which the title carries for its PNGs: a release zip's deflated files (src/pw_zip.h) */
int inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
	return stbi_zlib_decode_noheader_buffer(reinterpret_cast<char *>(out), (int)out_size, reinterpret_cast<const char *>(in),
	                                        (int)in_size) == (int)out_size ? 0 : 1;
}

void download(PwUpdateOffer offer)
{
	ProtonUpdate found;
	found.phase = ProtonUpdate::failed;
	found.version = offer.version;
	// No free-space check: a title process has no statfs (measured in phase 8); a full disk is said as the write fails.
	// Leaving the launcher (a game) ends this process and so the download; the next launcher start begins it again.
	const std::string error = pw_update_take(fetch, offer, kUpdate, inflate, [](uint64_t done) { gDone = done; });
	found.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStarted).count();
	if (!error.empty()) {
		found.message = error + " " PS5_TITLE_NAME " stays as it is.";
		return finish(found);
	}
	found.phase = ProtonUpdate::ready;
	found.message = PS5_TITLE_NAME " " + offer.version + " is ready. Restart now, or it is switched in the next time "
		PS5_TITLE_NAME " starts.";
	finish(found);
}

/* This title again, from the start: the switch runs as it begins. */
void restart(char *const argv[])
{
#if defined(PS5_HOST_REFERENCE)
	(void)argv;
#else
	static const char *const eboots[] = { "/app0/eboot.bin", PROTON_SANDBOX_APP0 "/eboot.bin" };
	for (const char *eboot : eboots) {
		proton_say("update: restarting (%s)", eboot);
		sceSystemServiceLoadExec(eboot, argv);
	}
	proton_say("update: the restart was refused");
#endif
}

} // namespace

void proton_update_check()
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gState.phase == ProtonUpdate::checking || gState.phase == ProtonUpdate::downloading) return;
	gState = {};
	gState.phase = ProtonUpdate::checking;
	gState.message = "Asking for the newest release";
	std::thread(check).detach();
}

void proton_update_download()
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gState.phase != ProtonUpdate::available || !gOffer.found) return;
	gState.phase = ProtonUpdate::downloading;
	gState.message = "Downloading " PS5_TITLE_NAME " " + gState.version;
	gStarted = std::chrono::steady_clock::now();
	gDone = 0;
	std::thread(download, gOffer).detach();
}

void proton_update_restart()
{
	if (proton_update_state().phase != ProtonUpdate::ready) return;
	char *none[] = { nullptr };
	restart(none);
	std::lock_guard<std::mutex> hold(gLock);
	gState.message = "The restart was refused. Close " PS5_TITLE_NAME " and open it again: the update is switched in then.";
}

ProtonUpdate proton_update_state()
{
	std::lock_guard<std::mutex> hold(gLock);
	ProtonUpdate state = gState;
	if (state.phase == ProtonUpdate::downloading) {
		state.progress.written = gDone;
		state.progress.to_write = gOffer.zip_size;
		state.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStarted).count();
	}
	return state;
}

void proton_update_on_start(int argc, char **argv)
{
	char version[64];
	if (!pw_update_pending(kUpdate, version, sizeof(version))) return;
	/* A switch that is due, but this title is as new already (a newer folder copied over by hand): dropped. A switch
	 * cut short (`switching`) always goes on. */
	const std::string ready = std::string(kUpdate) + "/ready";
	if (access(ready.c_str(), F_OK) == 0 && pw_release_version_compare(version, running_version().c_str()) <= 0) {
		proton_say("update: %s is staged, but this title is %s already: dropped", version, running_version().c_str());
		pw_update_discard(kUpdate);
		return;
	}
	char failed[512];
	const int status = pw_update_switch(kUpdate, proton_app_root(), failed, sizeof(failed));
	proton_say("update: switch to %s into %s: status %d%s%s", version, proton_app_root(), status, *failed ? " at " : "",
		failed);
	if (status != 1) {
		ProtonUpdate found;
		found.phase = ProtonUpdate::failed;
		found.version = version;
		found.message = std::string("The switch to " PS5_TITLE_NAME " ") + version + " stopped at " + failed + ": " +
			strerror(-status) + ". It goes on the next time " PS5_TITLE_NAME " starts.";
		finish(found);
		return;
	}
	gSwitchedHere = true;
	/* None of this start's arguments: the new title asks for root afresh (an elevation-retry=1 is this process's). */
	(void)argc, (void)argv;
	char *none[] = { nullptr };
	restart(none);
}

void proton_update_started()
{
	if (gSwitchedHere) return;
	const int status = pw_update_confirm(kUpdate);
	if (status) proton_say("update: the new title started; the previous title's files are removed (%d)", status);
}

bool proton_update_auto_enabled()
{
	return read_file(kAutoFile).compare(0, 1, "0") != 0;
}

void proton_update_set_auto(bool on)
{
	write_file(kAutoFile, on ? "1\n" : "0\n");
	proton_say("update: automatic check %s", on ? "on" : "off");
}

void proton_update_auto()
{
	const int64_t now = (int64_t)time(nullptr), last = strtoll(read_file(kLastCheckFile).c_str(), nullptr, 10);
	char staged[64];
	// a release found earlier whose download a game cut short is taken up again at once, whatever the day's check
	const bool unfinished = !read_file(kFoundFile).empty() && !pw_update_pending(kUpdate, staged, sizeof(staged));
	if (!proton_update_auto_enabled() || (!pw_update_check_due(1, now, last) && !unfinished)) return;
	write_file(kLastCheckFile, std::to_string(now) + "\n");
	proton_say("update: the automatic check asks (last %lld%s)", (long long)last, unfinished ? ", a download to finish" : "");
	proton_update_check();
}

std::string proton_update_notice()
{
	const ProtonUpdate state = proton_update_state();
	char staged[64];
	if (state.phase == ProtonUpdate::ready || (state.phase != ProtonUpdate::downloading && pw_update_pending(kUpdate, staged, sizeof(staged))))
		return PS5_TITLE_NAME " " + (state.phase == ProtonUpdate::ready ? state.version : std::string(staged)) + " is ready.";
	if (state.phase == ProtonUpdate::downloading) {
		char line[160];
		const double share = state.progress.to_write ? 100.0 * (double)state.progress.written / (double)state.progress.to_write : 0.0;
		snprintf(line, sizeof(line), PS5_TITLE_NAME " %s is downloading in the background: %.0f%%.", state.version.c_str(), share);
		return line;
	}
	std::string version = state.phase == ProtonUpdate::available ? state.version : std::string();
	if (version.empty() && state.phase != ProtonUpdate::current) {
		version = read_file(kFoundFile);
		version = version.substr(0, version.find_first_of("\r\n"));
		// a kept release no newer than this title (it was installed since) is not offered
		if (pw_release_version_compare(version.c_str(), running_version().c_str()) <= 0) version.clear();
	}
	return version.empty() ? std::string() : PS5_TITLE_NAME " " + version + " is available: it downloads in the background.";
}

bool proton_update_restartable()
{
	char staged[64];
	return proton_update_state().phase != ProtonUpdate::downloading && pw_update_pending(kUpdate, staged, sizeof(staged));
}

void proton_update_restart_now()
{
	if (!proton_update_restartable()) return;
	proton_say("update: Restart now, from the main menu");
	char *none[] = { nullptr };
	restart(none);
}
