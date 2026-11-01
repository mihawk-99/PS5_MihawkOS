/*
 * MihawkOS - "Install runtime from storage" (proton_install.h).
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "proton_install.h"
#include "proton_fetch.h"
#include "../../src/pw_https.h"
#include "proton_launch.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "json.hpp" // external/tinygltf's vendored nlohmann::json, already in the title

namespace {

/* What a first session adds beside the runtime: the mihawk prefix wineboot builds. The owner's used one holds
 * 1,352,411,293 B in those parts (evidence/probe-prefix-size-2026-10-18-01): a fresh one is smaller. */
constexpr uint64_t kFirstPrefixBytes = 1352411293ull;
constexpr uint64_t kMarginBytes = 256ull << 20;
constexpr const char *kRecord = "runtime/ge11-7/runtime-pack.txt";
/* Where this build's own release keeps its pieces, by its tag: never a "latest" URL, so an old title repairs with its
 * own runtime. Empty until the owner publishes a release (docs/SELF_INSTALL.md, phase 9). */
#ifndef PROTON_RELEASE_BASE
#define PROTON_RELEASE_BASE ""
#endif

std::mutex gLock;
std::atomic<bool> gCancel{ false };
ProtonInstall gState;
PwPackManifest gManifest{};
bool gParsed = false;
PwPack *gPack = nullptr;   /* while the install thread runs: its progress is read from here */
std::chrono::steady_clock::time_point gStarted;

std::string read_file(const std::string &path)
{
	std::string text;
	if (FILE *file = fopen(path.c_str(), "rb")) {
		char buffer[65536];
		size_t got;
		while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
		fclose(file);
	}
	return text;
}

std::string gigabytes(uint64_t bytes)
{
	char text[32];
	snprintf(text, sizeof(text), "%.1f GB", bytes / 1e9);
	return text;
}

/* The manifest the title folder pins: parsed once. */
bool manifest(std::string &problem)
{
	if (gParsed) return true;
	const std::string text = read_file(proton_title_path("/app0/runtime-pack.txt"));
	if (text.empty()) {
		problem = "This " PS5_TITLE_NAME " build carries no runtime manifest (runtime-pack.txt), so it cannot install a runtime.";
		return false;
	}
	if (pw_pack_parse(text.data(), text.size(), &gManifest) != PW_PACK_OK) {
		problem = "The runtime manifest in the title folder is damaged. Reinstall " PS5_TITLE_NAME ".";
		return false;
	}
	gParsed = true;
	return true;
}

bool web(const std::string &source) { return !source.compare(0, 7, "http://") || !source.compare(0, 8, "https://"); }

/* The release, or a PC's fixture named in the title folder's install-source.txt (tools put it there for a test; a
 * user's title has none). Plain http serves as well as https: every piece is pinned by its SHA-256. */
std::string release_base()
{
	std::string base = read_file(proton_title_path("/app0/install-source.txt"));
	base = base.substr(0, base.find_first_of("\r\n"));
	if (base.empty()) base = PROTON_RELEASE_BASE;
	if (!web(base) || base.back() != '/' || base.find_first_of(" \t\"'") != std::string::npos) return {};
	return base;
}

/* A failed download's kind, for a screen that words it itself. */
ProtonInstall::Problem download_problem(int status)
{
	switch (status) {
	case PW_HTTPS_UNTRUSTED: case PW_HTTPS_HOSTNAME: case PW_HTTPS_ROOTS: return ProtonInstall::certificate;
	case PW_HTTPS_CLOCK: return ProtonInstall::clock;
	case PW_HTTPS_STATUS: case PW_HTTPS_PROTOCOL: case PW_HTTPS_LIMIT: case PW_HTTPS_REDIRECTS: case PW_HTTPS_URL:
		return ProtonInstall::server;
	default: return ProtonInstall::network;
	}
}

/* One piece from the release into the install: up to three tries, each from its first byte. `failed` gets the last
 * download's PW_HTTPS_* status when the network is what failed. */
std::string download(PwPack *pack, const PwPackManifest &manifest, size_t chunk, const std::string &base,
	const std::atomic<bool> &cancel, int &status, int &failed)
{
	const PwPackChunk &piece = manifest.chunks[chunk];
	std::string problem;
	for (int attempt = 1; attempt <= 3; attempt++) {
		status = pw_pack_chunk_begin(pack, chunk);
		if (status != PW_PACK_OK) return {};
		const ProtonFetch got = proton_fetch(base + piece.name, piece.packed, [&](const void *data, size_t size) {
			status = pw_pack_chunk_feed(pack, data, size);
			return status == PW_PACK_OK && !cancel;
		});
		if (cancel) return {};
		if (status == PW_PACK_OK && got.ok) status = pw_pack_chunk_end(pack);
		if (status == PW_PACK_OK && got.ok) return {};
		proton_say("install: piece %zu try %d: status=%d http=%d fetch=%d bytes=%llu %s", chunk, attempt, status,
			got.http, got.status, (unsigned long long)got.bytes, got.error.c_str());
		failed = status == PW_PACK_OK ? got.status : PW_HTTPS_OK;   /* the piece's fault is the caller's to say */
		/* The server refusing, or a certificate it cannot change, does not get better by asking again. */
		if (got.status == PW_HTTPS_STATUS || got.status == PW_HTTPS_UNTRUSTED || got.status == PW_HTTPS_HOSTNAME ||
			got.status == PW_HTTPS_CLOCK || got.status == PW_HTTPS_ROOTS) {
			status = PW_PACK_OK;
			return got.error;
		}
		/* The storage refusing, or a link in the way, does not get better by downloading again. */
		if (status == PW_PACK_WRITE || status == PW_PACK_PATH || status == PW_PACK_MEMORY) return {};
		problem = got.error.empty() ? std::string() : got.error;
		if (status != PW_PACK_OK) problem.clear();
	}
	if (problem.empty()) return {};   /* the piece itself was wrong three times: the caller says so from status */
	status = PW_PACK_OK;
	return problem + " The install goes on from this piece when you press CROSS again.";
}

/* How many of the manifest's pieces `folder` holds, at their sizes. */
size_t pieces(const std::string &folder)
{
	size_t found = 0;
	for (size_t c = 0; c < gManifest.chunk_count; c++) {
		struct stat st;
		const std::string path = folder + "/" + gManifest.chunks[c].name;
		found += stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == gManifest.chunks[c].packed;
	}
	return found;
}

/* What the runtime held before, for pw_pack_finish to delete what this one no longer has: the last install's
 * manifest, or the PC deploy's list (tools/deploy-wine.py). */
std::vector<std::string> old_paths()
{
	std::vector<std::string> paths;
	PwPackManifest old{};
	const std::string text = read_file(PROTON_DATA_ROOT "/" + std::string(kRecord));
	if (!text.empty() && pw_pack_parse(text.data(), text.size(), &old) == PW_PACK_OK) {
		for (size_t f = 0; f < old.file_count; f++) paths.push_back(old.files[f].path);
		pw_pack_manifest_free(&old);
		return paths;
	}
	const std::string deployed = read_file(PROTON_DATA_ROOT "/runtime/ge11-7/deploy-manifest.json");
	if (!deployed.empty()) {
		const nlohmann::json json = nlohmann::json::parse(deployed, nullptr, false);
		if (json.is_object())
			for (auto item = json.begin(); item != json.end(); ++item) paths.push_back(item.key());
	}
	return paths;
}

std::string failure(int status, PwPack *pack)
{
	const int error = pw_pack_errno(pack);
	std::string text = std::string("The install stopped: ") + pw_pack_status_text(status);
	if (status == PW_PACK_WRITE && error == ENOSPC) text = "The install stopped: the console's storage is full";
	if (*pw_pack_failed_path(pack)) text += std::string(" (") + pw_pack_failed_path(pack) + ")";
	if (error && error != ENOSPC) text += std::string(": ") + strerror(error);
	return text + ". Press CROSS to try again; what is installed already is kept.";
}

void run(std::string source)
{
	ProtonPackJob job;
	job.manifest = &gManifest;
	job.root = PROTON_DATA_ROOT;
	job.source = source;
	job.old = old_paths();
	job.record = kRecord;
	job.cancel = &gCancel;
	job.publish = [](PwPack *pack) {
		std::lock_guard<std::mutex> hold(gLock);
		gPack = pack;
		if (pack) gState.phase = ProtonInstall::running;
	};
	ProtonInstall::Problem problem = ProtonInstall::none;
	PwPackProgress progress{};
	const std::string message = proton_pack_install(job, problem, progress);
	std::lock_guard<std::mutex> hold(gLock);
	gState.progress = progress;
	gState.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStarted).count();
	gState.phase = message.empty() ? ProtonInstall::done : ProtonInstall::failed;
	gState.problem = problem;
	gState.message = message.empty() ? "The runtime is installed. Games can start." : message;
}

} // namespace

std::string proton_pack_install(const ProtonPackJob &job, ProtonInstall::Problem &problem, PwPackProgress &progress)
{
	const PwPackManifest &manifest = *job.manifest;
	const std::atomic<bool> &cancel = *job.cancel;
	const auto started = std::chrono::steady_clock::now();
	PwPack *pack = nullptr;
	int status = pw_pack_open(&pack, &manifest, job.root.c_str());
	if (job.publish) job.publish(pack);
	std::string message;
	int fetch_failed = PW_HTTPS_OK;
	std::vector<uint8_t> buffer(1u << 20);
	for (size_t c = 0; status == PW_PACK_OK && !cancel && c < manifest.chunk_count; c++) {
		if (!pw_pack_chunk_needed(pack, c)) continue;
		if (web(job.source)) {
			message = download(pack, manifest, c, job.source, cancel, status, fetch_failed);
		} else {
			const std::string path = job.source + "/" + manifest.chunks[c].name;
			const int fd = open(path.c_str(), O_RDONLY);
			status = fd < 0 ? PW_PACK_OK : pw_pack_chunk_begin(pack, c);
			ssize_t got = fd < 0 ? -1 : 0;
			while (fd >= 0 && status == PW_PACK_OK && !cancel && (got = read(fd, buffer.data(), buffer.size())) > 0)
				status = pw_pack_chunk_feed(pack, buffer.data(), (size_t)got);
			const int error = errno;
			if (fd >= 0) close(fd);
			if (status == PW_PACK_OK && got < 0)
				message = "Could not read " + path + ": " + strerror(error) + ". Check the storage, then press CROSS to try again.";
			else if (status == PW_PACK_OK && !cancel)
				status = pw_pack_chunk_end(pack);
		}
		if (!message.empty()) break;
		PwPackProgress now;
		pw_pack_progress(pack, &now);   // one line a piece: the write curve, for the rehearsal
		proton_say("install: piece %zu status=%d written=%llu of %llu at %.2f s", c, status, (unsigned long long)now.written,
			(unsigned long long)now.to_write, std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
	}
	problem = message.empty() ? ProtonInstall::none :
		fetch_failed != PW_HTTPS_OK ? download_problem(fetch_failed) : ProtonInstall::read_failed;
	if (cancel) {
		message = "The install was stopped. What is installed already is kept: press CROSS to go on.";
		problem = ProtonInstall::cancelled;
	}
	if (message.empty() && status == PW_PACK_OK) {
		std::vector<const char *> list;
		for (const auto &path : job.old) list.push_back(path.c_str());
		status = pw_pack_finish(pack, list.data(), list.size(), job.record);
	}
	if (message.empty() && status != PW_PACK_OK) {
		message = failure(status, pack);
		problem = status == PW_PACK_WRITE && pw_pack_errno(pack) == ENOSPC ? ProtonInstall::storage_full :
			status == PW_PACK_CHUNK_SIZE || status == PW_PACK_CHUNK_HASH || status == PW_PACK_DECODE ||
			status == PW_PACK_FILE_HASH ? ProtonInstall::damaged : ProtonInstall::write_failed;
	}
	pw_pack_progress(pack, &progress);
	proton_say("install: %s into %s status=%d errno=%d path=%s written=%llu of %llu files=%zu chunks=%zu of %zu in %.1f s%s%s",
		message.empty() ? "done" : "failed", job.root.c_str(), status, pw_pack_errno(pack), pw_pack_failed_path(pack),
		(unsigned long long)progress.written, (unsigned long long)progress.to_write, progress.files_written,
		progress.chunks_done, progress.chunks_needed,
		std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
		message.empty() ? "" : ": ", message.c_str());
	if (job.publish) job.publish(nullptr);
	pw_pack_close(pack);
	return message;
}

ProtonInstall proton_install_find(const char *only)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gState.phase == ProtonInstall::checking || gState.phase == ProtonInstall::running) return gState;
	ProtonInstall found;
	if (!manifest(found.message)) {
		found.problem = ProtonInstall::no_manifest;
		return gState = found;
	}
	found.bytes = gManifest.plain;
	const bool title_only = only && !strcmp(only, "title");
	const std::string folder = proton_title_path("/app0/runtime-pieces");
	const size_t have = pieces(folder);
	if (have == gManifest.chunk_count) found.source = folder;
	else if (!have && !title_only) found.source = release_base();
	const bool online = web(found.source);
	found.total = gManifest.chunk_count;
	found.have = have;
	if (found.source.empty()) {
		found.problem = have ? ProtonInstall::partial : ProtonInstall::no_pieces;
		found.message = have ? "The " PS5_TITLE_NAME " folder holds " + std::to_string(have) + " of the " +
			std::to_string(gManifest.chunk_count) + " runtime pieces. Copy the whole folder again, then try again." :
			"The " PS5_TITLE_NAME " folder holds no runtime pieces (runtime-pieces/). Copy the whole folder of the release "
			"again, then try again.";
		return gState = found;
	}
	const uint64_t needed = pw_pack_space_needed(&gManifest, kFirstPrefixBytes + kMarginBytes);
	/* The free space is not known: a title process has neither statvfs nor statfs. Both are null imports, and calling
	 * statfs here ended the title (2026-10-19; tools/check-title-imports.py now refuses such a build). A full disk is
	 * said by the install's ENOSPC (storage_full). `available` stays 0, "not known". */
	found.needed = needed;
	found.phase = ProtonInstall::ready;
	found.message = online ?
		"Ready to download " + gigabytes(gManifest.packed) + " and install " + gigabytes(gManifest.plain) + " from " +
			found.source.substr(0, found.source.find('/', 8)) + ". It needs " + gigabytes(needed) + " free. Press CROSS." :
		"Ready to install " + gigabytes(gManifest.plain) + " from " + found.source + ". Press CROSS.";
	return gState = found;
}

void proton_install_start()
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gState.phase != ProtonInstall::ready && gState.phase != ProtonInstall::failed) return;
	if (gState.source.empty()) return;
	gState.phase = ProtonInstall::checking;
	gState.problem = ProtonInstall::none;
	gCancel = false;
	gState.message = "Checking what is installed already";
	gStarted = std::chrono::steady_clock::now();
	proton_say("install: from %s, %zu pieces, %llu bytes", gState.source.c_str(), gManifest.chunk_count,
		(unsigned long long)gManifest.plain);
	std::thread(run, gState.source).detach();
}

void proton_install_cancel()
{
	gCancel = true;
}

ProtonInstall proton_install_state()
{
	std::lock_guard<std::mutex> hold(gLock);
	ProtonInstall state = gState;
	if (gPack) {
		pw_pack_progress(gPack, &state.progress);
		state.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStarted).count();
	}
	return state;
}
