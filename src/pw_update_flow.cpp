/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The automatic update's steps (pw_update_flow.h). */
#include "pw_update_flow.h"

extern "C" {
#include "pw_release.h"
#include "pw_sha256.h"
#include "pw_update.h"
}

#include <cstdio>
#include <sys/stat.h>
#include <cstring>
#include <vector>

#include "json.hpp" // external/tinygltf's vendored nlohmann::json

namespace {

constexpr const char *kManifestAsset = "mihawkos-release.txt";

std::string fetch_text(const PwUpdateGet &get, const std::string &url, uint64_t limit, std::string &text)
{
	text.clear();
	return get(url, limit, [&](const void *data, size_t size) {
		text.append(static_cast<const char *>(data), size);
		return true;
	});
}

} // namespace

std::string pw_update_find(const PwUpdateGet &get, const std::string &releases_url, const std::string &manifest_url,
                           const char *build_tag, const char *running, const uint8_t key[32], PwUpdateOffer &offer)
{
	offer = {};
	std::string url = manifest_url, text, error;
	if (url.empty()) {
		if (!(error = fetch_text(get, releases_url, 2u << 20, text)).empty()) return error;
		const nlohmann::json list = nlohmann::json::parse(text, nullptr, false);
		if (!list.is_array()) return "GitHub's list of releases could not be read.";
		std::vector<std::string> tags, urls;
		std::vector<int> drafts;
		std::vector<nlohmann::json> assets;
		for (const auto &release : list) {
			if (!release.is_object()) continue;
			const auto tag = release.find("tag_name"), draft = release.find("draft"), files = release.find("assets");
			std::string manifest;
			if (files != release.end() && files->is_array())
				for (const auto &asset : *files) {
					const auto name = asset.find("name"), link = asset.find("browser_download_url");
					if (name != asset.end() && link != asset.end() && name->is_string() && link->is_string() &&
					    name->get<std::string>() == kManifestAsset)
						manifest = link->get<std::string>();
				}
			tags.push_back(tag != release.end() && tag->is_string() ? tag->get<std::string>() : std::string());
			drafts.push_back(draft != release.end() && draft->is_boolean() && draft->get<bool>());
			urls.push_back(manifest);
			assets.push_back(files != release.end() ? *files : nlohmann::json::array());
		}
		std::vector<PwReleaseEntry> entries;
		for (size_t i = 0; i < tags.size(); ++i) entries.push_back({ tags[i].c_str(), drafts[i], urls[i].c_str() });
		const int pick = pw_release_pick(entries.data(), entries.size(), build_tag);
		if (pick < 0) return {};
		offer.tag = tags[(size_t)pick];
		url = urls[(size_t)pick];
		// the zip's size, for the progress, from the same release's assets
		for (const auto &asset : assets[(size_t)pick]) {
			const auto size = asset.find("size"), name = asset.find("name");
			if (size != asset.end() && size->is_number_unsigned() && name != asset.end() && name->is_string() &&
			    name->get<std::string>().size() > 4 && name->get<std::string>().compare(name->get<std::string>().size() - 4, 4, ".zip") == 0)
				offer.zip_size = size->get<uint64_t>();
		}
	}
	if (!(error = fetch_text(get, url, 16384, text)).empty()) return error;
	PwRelease release;
	const int parsed = pw_release_parse(text.data(), text.size(), key, &release);
	if (parsed == PW_RELEASE_SIGNATURE) return "The update offered is not signed with MihawkOS's key, so none of it is installed.";
	if (parsed != PW_RELEASE_OK) return "The update's description is damaged, so none of it is installed.";
	const size_t name = strlen(release.title);
	if (name < 5 || strcmp(release.title + name - 4, ".zip")) return "The update's description names no release archive.";
	if (pw_release_version_compare(release.version, running) <= 0) return {};   /* not newer than what runs: no replay */
	offer.found = true;
	offer.version = release.version;
	offer.zip_url = std::string(release.base) + release.title;
	memcpy(offer.zip_sha256, release.title_sha256, 32);
	memcpy(offer.runtime_sha256, release.runtime_sha256, 32);
	return {};
}

std::string pw_update_take(const PwUpdateGet &get, const PwUpdateOffer &offer, const std::string &update,
                           pw_zip_inflate_fn inflate, const std::function<void(uint64_t)> &progress)
{
	const std::string zip = update + "/download.zip", part = zip + ".part";
	mkdir(update.c_str(), 0777);
	remove(part.c_str());
	FILE *out = fopen(part.c_str(), "wb");
	if (!out) return "The update could not be saved on the console's storage.";
	PwSha256 hash;
	pw_sha256_init(&hash);
	uint64_t done = 0;
	bool wrote = true;
	std::string error = get(offer.zip_url, 8ull << 30, [&](const void *data, size_t size) {
		if (fwrite(data, 1, size, out) != size) return wrote = false;
		pw_sha256_update(&hash, data, size);
		progress(done += size);
		return true;
	});
	if (fflush(out) || fclose(out)) wrote = false;
	uint8_t digest[32];
	pw_sha256_final(&hash, digest);
	if (error.empty() && !wrote) error = "The update could not be saved on the console's storage (is it full?).";
	if (error.empty() && memcmp(digest, offer.zip_sha256, 32)) error = "The update's download is not the one its signed description names.";
	if (error.empty() && rename(part.c_str(), zip.c_str())) error = "The update could not be saved on the console's storage.";
	if (!error.empty()) {
		remove(part.c_str());
		return error;
	}
	char failed[512];
	const int status = pw_update_install_zip(zip.c_str(), update.c_str(), PW_UPDATE_ZIP_PREFIX, offer.runtime_sha256,
	                                         offer.version.c_str(), inflate, failed, sizeof(failed));
	remove(zip.c_str());
	if (status) return std::string("The update could not be prepared: ") + pw_update_status_text(status) +
		(failed[0] ? std::string(" (") + failed + ")." : std::string("."));
	return {};
}
