/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_release.h"
#include "../external/monocypher/monocypher-ed25519.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *hex(const uint8_t *bytes, size_t count, char *out)
{
    for (size_t i = 0; i < count; i++) sprintf(out + 2 * i, "%02x", bytes[i]);
    return out;
}

/* body, then its signature line by `secret`. */
static size_t sign(char *text, const char *body, const uint8_t secret[64])
{
    uint8_t signature[64];
    char digits[129];
    crypto_ed25519_sign(signature, secret, (const uint8_t *)body, strlen(body));
    return (size_t)sprintf(text, "%ssignature %s\n", body, hex(signature, 64, digits));
}

static void key_pair(uint8_t fill, uint8_t secret[64], uint8_t public_key[32])
{
    uint8_t seed[32];
    memset(seed, fill, sizeof(seed));
    crypto_ed25519_key_pair(secret, public_key, seed);
}

int main(void)
{
    /* RFC 8032 section 7.1, test 1: Monocypher's Ed25519 is the RFC's, so OpenSSL's signatures check. */
    uint8_t seed[32], secret[64], public_key[32], expected[32], signature[64];
    assert(pw_release_hex32("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed));
    assert(pw_release_hex32("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", expected));
    crypto_ed25519_key_pair(secret, public_key, seed);
    assert(!memcmp(public_key, expected, 32));
    assert(pw_release_hex32("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155", signature) &&
           pw_release_hex32("5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", signature + 32));
    assert(crypto_ed25519_check(signature, public_key, (const uint8_t *)"", 0) == 0);
    assert(!pw_release_hex32("d75a98", expected) && !pw_release_hex32(NULL, expected));

    uint8_t owner[64], owner_public[32], other[64], other_public[32];
    key_pair(7, owner, owner_public);
    key_pair(8, other, other_public);
    const char *body =
        "mihawkos-release 1\n"
        "version 01.001.000\n"
        "base https://example.invalid/releases/download/v1.1/\n"
        "title 0101010101010101010101010101010101010101010101010101010101010101 title-pack.txt\n"
        "runtime abababababababababababababababababababababababababababababababab runtime-pack.txt\n";
    char text[2048];
    PwRelease release;
    size_t size = sign(text, body, owner);
    assert(pw_release_parse(text, size, owner_public, &release) == PW_RELEASE_OK);
    assert(!strcmp(release.version, "01.001.000") && !strcmp(release.base, "https://example.invalid/releases/download/v1.1/"));
    assert(!strcmp(release.title, "title-pack.txt") && !strcmp(release.runtime, "runtime-pack.txt"));
    assert(release.title_sha256[0] == 0x01 && release.title_sha256[31] == 0x01 && release.runtime_sha256[5] == 0xab);

    /* Another key, a changed byte, a cut or a longer text: not signed by this build's key, nothing read. */
    assert(pw_release_parse(text, size, other_public, &release) == PW_RELEASE_SIGNATURE && !release.version[0]);
    text[30] ^= 1;
    assert(pw_release_parse(text, size, owner_public, &release) == PW_RELEASE_SIGNATURE);
    text[30] ^= 1;
    char longer[2100];
    snprintf(longer, sizeof(longer), "x%s", text);
    assert(pw_release_parse(longer, size + 1, owner_public, &release) == PW_RELEASE_SIGNATURE);
    assert(pw_release_parse(text, size - 1, owner_public, &release) == PW_RELEASE_FORMAT);
    assert(pw_release_parse(text + 20, size - 20, owner_public, &release) == PW_RELEASE_SIGNATURE);
    assert(pw_release_parse(NULL, 0, owner_public, &release) == PW_RELEASE_FORMAT);

    /* Signed, but out of shape: refused all the same. */
    static const char *const bad[] = {
        "mihawkos-release 2\nversion 1\nbase https://a.invalid/x/\ntitle 0101010101010101010101010101010101010101010101010101010101010101 t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\n",
        "mihawkos-release 1\nversion 1..2\nbase https://a.invalid/x/\ntitle 0101010101010101010101010101010101010101010101010101010101010101 t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\n",
        "mihawkos-release 1\nversion 1\nbase https://a.invalid/x\ntitle 0101010101010101010101010101010101010101010101010101010101010101 t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\n",
        "mihawkos-release 1\nversion 1\nbase ftp://a.invalid/x/\ntitle 0101010101010101010101010101010101010101010101010101010101010101 t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\n",
        "mihawkos-release 1\nversion 1\nbase https://a.invalid/x/\ntitle 0101010101010101010101010101010101010101010101010101010101010101 ../t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\n",
        "mihawkos-release 1\nversion 1\nbase https://a.invalid/x/\ntitle 01010101 t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\n",
        "mihawkos-release 1\nversion 1\nbase https://a.invalid/x/\ntitle 0101010101010101010101010101010101010101010101010101010101010101 t\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\nextra 1\n",
        "mihawkos-release 1\nversion 1\nbase https://a.invalid/x/\nruntime 0101010101010101010101010101010101010101010101010101010101010101 r\ntitle 0101010101010101010101010101010101010101010101010101010101010101 t\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        size = sign(text, bad[i], owner);
        assert(pw_release_parse(text, size, owner_public, &release) == PW_RELEASE_FORMAT);
    }

    /* Versions: number by number, a missing group is 0, a malformed one is below all. */
    assert(pw_release_version_compare("01.001.000", "01.000.000") > 0);
    assert(pw_release_version_compare("01.000.000", "01.000.000") == 0);
    assert(pw_release_version_compare("01.010.000", "01.009.999") > 0);
    assert(pw_release_version_compare("1.2", "01.002.000") == 0);
    assert(pw_release_version_compare("2", "01.999.999") > 0);
    assert(pw_release_version_compare("01.000.000", "01.000.001") < 0);
    assert(pw_release_version_compare("1..2", "0") < 0 && pw_release_version_compare("0", "x") > 0);
    assert(pw_release_version_compare("", "") == 0 && pw_release_version_compare(NULL, "1") < 0);

    /* Release tags (GitHub's names for MihawkOS releases) and the pick of the newest (the automatic check, 2026-10-31) */
    assert(pw_release_tag_compare("v7.0.0-alpha-7", "v6.0.0-alpha-6") > 0);
    assert(pw_release_tag_compare("v5.0.1-alpha-5", "5.0.0-alpha-5") > 0);
    assert(pw_release_tag_compare("v7.0.0-alpha-7", "7.0.0-alpha-7") == 0);
    assert(pw_release_tag_compare("v7.0.0-beta-1", "v7.0.0-alpha-9") > 0 && pw_release_tag_compare("v7.0.0-rc-1", "v7.0.0-beta-3") > 0);
    assert(pw_release_tag_compare("v7.0.0", "v7.0.0-rc-4") > 0 && pw_release_tag_compare("v7.0.10-alpha-1", "v7.0.9") > 0);
    assert(pw_release_tag_compare("v7.0.0-alpha-10", "v7.0.0-alpha-9") > 0);
    assert(pw_release_tag_compare("nightly", "v1.0.0-alpha-1") < 0 && pw_release_tag_compare("v7", "v1.0.0") < 0);
    assert(pw_release_tag_compare("v7.0.0-gamma-1", "v1.0.0") < 0 && pw_release_tag_compare("v7.0.0-alpha", "v1.0.0") < 0);
    assert(pw_release_tag_compare(NULL, NULL) == 0);
    const PwReleaseEntry listing[] = {
        { "v8.0.0-alpha-8", 1, "https://example.invalid/d/mihawkos-release.txt" },   /* a draft: never */
        { "v7.1.0-alpha-8", 0, "" },                                                 /* not signed: never */
        { "v7.0.0-alpha-7", 0, "https://example.invalid/7/mihawkos-release.txt" },
        { "v6.0.0-alpha-6", 0, "https://example.invalid/6/mihawkos-release.txt" },
        { "garbage", 0, "https://example.invalid/g/mihawkos-release.txt" },
    };
    assert(pw_release_pick(listing, 5, "6.0.0-alpha-6") == 2);
    assert(pw_release_pick(listing, 5, "7.0.0-alpha-7") == -1);   /* the running one is the newest offered */
    assert(pw_release_pick(listing, 5, "8.0.0") == -1);
    assert(pw_release_pick(listing, 0, "1.0.0") == -1);

    puts("test_pw_release: ok");
    return 0;
}
