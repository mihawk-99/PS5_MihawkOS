/*
 * MihawkOS - the launcher's Steam option (proton_steam.h).
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "proton_steam.h"
#include "proton_fetch.h"
#include "proton_launch.h"
#include "../../src/pw_steam.h"
#include "../../src/pw_path_nocase.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

namespace {

std::mutex gLock;
ProtonSteam gState;          /* the download's part: phase, message, bytes */
bool gRunning = false;

std::string read_text(const char *path, size_t limit)
{
	std::string text;
	if (FILE *file = fopen(path, "rb")) {
		char buffer[8192];
		size_t got;
		while (text.size() < limit && (got = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
		fclose(file);
	}
	return text;
}

/* The prefix the option uses: the desktop's, or a test's (PW_STEAM_PREFIX_HOOK, src/pw_steam.h), read once. */
const std::string &steam_prefix()
{
	static const std::string prefix = [] {
		const std::string text = read_text(PW_STEAM_PREFIX_HOOK, 64);
		char word[40];
		if (!pw_steam_prefix_word(text.data(), text.size(), word, sizeof(word))) return std::string(PW_STEAM_PREFIX);
		proton_say("steam: the test prefix %s (%s)", word, PW_STEAM_PREFIX_HOOK);
		return std::string(word);
	}();
	return prefix;
}

/* A path inside the option's prefix folder. */
std::string in_prefix(const char *relative)
{
	return std::string(PROTON_DATA_ROOT "/prefixes/") + steam_prefix() + relative;
}

/* Windows matches names without regard to case, the console's file system does not: the installer writes "Steam.exe". */
bool exists(const char *path)
{
	char held[4096];
	return pw_path_nocase(path, held, sizeof(held)) != 0;
}

/* The words for a failed fetch: what the console could not do, in plain words. */
std::string fetch_words(const ProtonFetch &got)
{
	return got.error.empty() ? std::string("the connection failed") : got.error;
}

/* One try at one address: the body into `part`, its first bytes kept for the check. Returns "" on success, else a sentence. */
std::string try_download(const char *url, const std::string &part, uint8_t *head, size_t *head_length, uint64_t *total,
	int *status)
{
	*status = 0;
	FILE *file = fopen(part.c_str(), "wb");
	if (!file) return std::string("the console would not let ") + part + " be written (" + strerror(errno) + ")";
	*head_length = 0;
	bool write_failed = false;
	const ProtonFetch got = proton_fetch(url, PW_STEAM_INSTALLER_MAX, [&](const void *data, size_t size) {
		if (*head_length < 1024) {
			const size_t take = std::min(size, size_t(1024) - *head_length);
			memcpy(head + *head_length, data, take);
			*head_length += take;
		}
		if (fwrite(data, 1, size, file) != size) {
			write_failed = true;
			return false;
		}
		{
			std::lock_guard<std::mutex> hold(gLock);
			gState.bytes += size;
		}
		return true;
	});
	if (fclose(file) != 0) write_failed = true;
	*total = got.bytes;
	if (write_failed) return "the console could not write the download (is the drive full?)";
	*status = got.status;
	if (!got.ok) return fetch_words(got);
	return "";
}

void download()
{
	std::string failure;
	bool done = false;
	int status = 0;
	mkdir(PW_STEAM_DOWNLOAD_DIR, 0777);
	const std::string part = std::string(PW_STEAM_INSTALLER_PATH) + ".part";
	for (const char *url : { PW_STEAM_INSTALLER_URL, PW_STEAM_INSTALLER_URL_FALLBACK }) {
		{
			std::lock_guard<std::mutex> hold(gLock);
			gState.bytes = 0;
		}
		uint8_t head[1024];
		size_t head_length = 0;
		uint64_t total = 0;
		failure = try_download(url, part, head, &head_length, &total, &status);
		if (failure.empty()) {
			const int check = pw_steam_installer_check(head, head_length, total);
			if (check == PW_STEAM_INSTALLER_OK) {
				if (rename(part.c_str(), PW_STEAM_INSTALLER_PATH) == 0) {
					done = true;
					break;
				}
				failure = std::string("the downloaded file could not be moved into place (") + strerror(errno) + ")";
			} else {
				failure = check == PW_STEAM_INSTALLER_SIZE ? "the download is not the size of the installer"
					: check == PW_STEAM_INSTALLER_NOT_SIGNED ? "the download is not a signed installer"
					: "the download is not a PC program";
			}
		}
		proton_say("steam: %s failed: %s", url, failure.c_str());
	}
	if (!done) unlink(part.c_str());
	if (done) proton_say("steam: installer downloaded");
	else proton_say("steam: download failed: %s", failure.c_str());
	std::lock_guard<std::mutex> hold(gLock);
	gRunning = false;
	gState.phase = done ? ProtonSteam::downloaded : ProtonSteam::failed;
	gState.fetch_status = done ? 0 : status;
	gState.message = done ? "Downloaded from Valve. Installing..." : "Could not download Steam: " + failure +
		". Check the console's internet connection and try again.";
}

/* Atomic: the title reads games/ at its start, and a half-written entry is a broken library. */
bool write_entry(const char *id, const char *text, size_t length, std::string *problem)
{
	const std::string directory = std::string(PROTON_DATA_ROOT) + "/games/" + id;
	mkdir((std::string(PROTON_DATA_ROOT) + "/games").c_str(), 0777);
	mkdir(directory.c_str(), 0777);
	const std::string path = directory + "/game.json", temporary = path + ".tmp";
	FILE *file = fopen(temporary.c_str(), "wb");
	if (!file) {
		*problem = std::string("the catalog entry could not be written (") + strerror(errno) + ")";
		return false;
	}
	const bool ok = fwrite(text, 1, length, file) == length;
	const bool closed = fclose(file) == 0;
	if (!ok || !closed || rename(temporary.c_str(), path.c_str()) != 0) {
		unlink(temporary.c_str());
		*problem = "the catalog entry could not be written";
		return false;
	}
	return true;
}

} // namespace

const char *proton_steam_title()
{
	return "Steam";
}

ProtonSteam proton_steam_state()
{
	ProtonSteam state;
	{
		std::lock_guard<std::mutex> hold(gLock);
		state = gState;
	}
	state.installed = exists(in_prefix(PW_STEAM_EXE_REL).c_str());
	if (state.installed) {
		const std::string users = read_text(in_prefix(PW_STEAM_LOGINUSERS_REL).c_str(), 65536);
		char persona[96];
		state.signed_in = pw_steam_remembered_user(users.data(), users.size(), persona, sizeof(persona)) != 0;
		if (state.signed_in) state.persona = persona;
		if (state.phase == ProtonSteam::downloaded || state.phase == ProtonSteam::failed) state.phase = ProtonSteam::idle;
	}
	return state;
}

PwSteamStep proton_steam_step(const ProtonSteam &state)
{
	return pw_steam_step(state.installed, state.signed_in, state.phase == ProtonSteam::downloading,
		state.phase == ProtonSteam::downloaded, state.phase == ProtonSteam::failed);
}

const char *proton_steam_subtitle(const ProtonSteam &state)
{
	return pw_steam_step_subtitle(proton_steam_step(state));
}

void proton_steam_download()
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gRunning || exists(in_prefix(PW_STEAM_EXE_REL).c_str())) return;
	gRunning = true;
	gState = ProtonSteam{};
	gState.phase = ProtonSteam::downloading;
	gState.message = "Downloading Valve's installer...";
	std::thread(download).detach();
}

void proton_steam_fail(const std::string &message)
{
	std::lock_guard<std::mutex> hold(gLock);
	gRunning = false;
	gState.phase = ProtonSteam::failed;
	gState.message = message;
}

bool proton_steam_game(ProtonGame *out, bool installer, std::string *problem, bool big_picture)
{
	char text[2048];
	const size_t length = installer ? pw_steam_installer_entry(text, sizeof(text), steam_prefix().c_str())
		: pw_steam_client_entry(text, sizeof(text), big_picture, steam_prefix().c_str());
	std::string ignored;
	if (!problem) problem = &ignored;
	if (!length) {
		*problem = "the catalog entry does not fit";
		return false;
	}
	if (installer ? !exists(PW_STEAM_INSTALLER_PATH) : !exists(in_prefix(PW_STEAM_EXE_REL).c_str())) {
		*problem = installer ? "the installer is not downloaded" : "Steam is not installed";
		return false;
	}
	const char *id = installer ? PW_STEAM_ENTRY_INSTALL : PW_STEAM_ENTRY_CLIENT;
	if (!write_entry(id, text, length, problem)) return false;
	std::string read_problem;
	for (const ProtonGame &game : proton_read_games(&read_problem)) {
		if (game.id != id) continue;
		if (!game.complaint.empty()) {
			*problem = game.complaint;
			return false;
		}
		*out = game;
		return true;
	}
	*problem = "the catalog entry was written but not found";
	return false;
}

std::vector<std::string> proton_steam_panel(const ProtonSteam &state)
{
	std::vector<std::string> lines;
	lines.push_back("Valve's Steam client, for the games that need it. " PS5_TITLE_NAME " downloads it from Valve's own servers "
		"when you ask, installs it for you, and opens it once so you can sign in.");
	char text[160];
	if (state.phase == ProtonSteam::downloading) {
		snprintf(text, sizeof(text), "Downloading Valve's installer: %.1f MB so far.", state.bytes / 1e6);
		lines.push_back(text);
	} else if (state.phase == ProtonSteam::downloaded) {
		lines.push_back("Downloaded. " PS5_TITLE_NAME " is installing it; it comes back here when that is done.");
	} else if (!state.installed) {
		lines.push_back("Press CROSS to download and install it. The console must be online. The installer is a couple of "
			"megabytes; Steam then updates itself the first time it opens, which takes about five minutes.");
	} else if (!state.signed_in) {
		lines.push_back("Installed. Press CROSS to open it. Steam updates itself first (about five minutes the first time), then "
			"shows its sign-in page: scan the QR code with the Steam app on your phone. The window closes by itself once you "
			"are signed in. You do this once. Hold OPTIONS for two seconds to cancel.");
	} else {
		lines.push_back(state.persona.empty() ? std::string("Signed in.") : "Signed in as " + state.persona + ".");
		lines.push_back("Press CROSS to open Steam in Big Picture: your library, the store and your games. The PS5 may slow down "
			"and say its memory is getting full while Steam is open; leave Steam from its own menu to come back here.");
	}
	lines.push_back("Steam is Valve's; " PS5_TITLE_NAME " is not affiliated with Valve.");
	return lines;
}
