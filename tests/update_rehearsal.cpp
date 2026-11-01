/* SPDX-License-Identifier: GPL-3.0-or-later */
/* tests/test_update_rehearsal.py's client: the automatic update's steps (src/pw_update_flow.h) as the title runs them,
 * over pw_https against a LAN fixture, then the switch and its confirm (src/pw_update.h) on a title folder. One line
 * of key=value results; every name resolves to 127.0.0.1.
 *
 *   update_rehearsal <roots.pem> <releases URL> <public key hex> <build tag> <running contentVersion> <update> <title> */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#pragma GCC diagnostic pop

#include "../src/pw_update_flow.h"
extern "C" {
#include "../src/pw_https.h"
#include "../src/pw_release.h"
#include "../src/pw_update.h"
}

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

static int loopback(const char *, uint8_t address[4])
{
	address[0] = 127, address[1] = 0, address[2] = 0, address[3] = 1;
	return 0;
}

/* The title's inflate (stb_image's, which the title carries for its PNGs) */
static int inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
	return stbi_zlib_decode_noheader_buffer(reinterpret_cast<char *>(out), (int)out_size, reinterpret_cast<const char *>(in),
	                                        (int)in_size) == (int)out_size ? 0 : 1;
}

static int rehearse(PwHttps *https, char **argv)
{
	const PwUpdateGet get = [&](const std::string &url, uint64_t limit, const std::function<bool(const void *, size_t)> &sink) {
		PwHttpsResult result;
		pw_https_get(https, url.c_str(), limit, loopback, [](void *context, const void *data, size_t size) {
			return (*static_cast<const std::function<bool(const void *, size_t)> *>(context))(data, size) ? 0 : 1;
		}, const_cast<std::function<bool(const void *, size_t)> *>(&sink), &result);
		return result.status ? std::string("fetch ") + url + ": " + pw_https_status_text(result.status) : std::string();
	};
	uint8_t key[32];
	if (!pw_release_hex32(argv[3], key)) return 4;
	PwUpdateOffer offer;
	std::string error = pw_update_find(get, argv[2], "", argv[4], argv[5], key, offer);
	printf("find=%s found=%d tag=%s version=%s zip_size=%llu\n", error.empty() ? "ok" : error.c_str(), offer.found,
	       offer.tag.c_str(), offer.version.c_str(), (unsigned long long)offer.zip_size);
	if (!error.empty() || !offer.found) return 0;
	uint64_t last = 0;
	error = pw_update_take(get, offer, argv[6], inflate, [&](uint64_t done) { last = done; });
	printf("take=%s downloaded=%llu\n", error.empty() ? "ok" : error.c_str(), (unsigned long long)last);
	if (!error.empty()) return 0;
	char failed[512] = "", version[64] = "";
	printf("pending=%d version=%s\n", pw_update_pending(argv[6], version, sizeof(version)), version);
	const int switched = pw_update_switch(argv[6], argv[7], failed, sizeof(failed));
	printf("switch=%d failed=%s\n", switched, failed);
	struct stat held;
	printf("previous=%d\n", stat((std::string(argv[6]) + "/previous").c_str(), &held) == 0);   /* kept until the launcher draws */
	printf("confirm=%d\n", pw_update_confirm(argv[6]));
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 8) return 2;
	PwHttps *https;
	if (pw_https_create(&https, argv[1], 0)) return 3;
	const int status = rehearse(https, argv);
	pw_https_destroy(https);
	return status;
}
