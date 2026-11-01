/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_steam_appid.h"
#include "pw_path_nocase.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int pw_steam_id_valid(const char *text)
{
    size_t digits = 0;
    int nonzero = 0;
    if (!text) return 0;
    for (; text[digits]; ++digits) {
        if (text[digits] < '0' || text[digits] > '9') return 0;
        if (text[digits] != '0') nonzero = 1;
    }
    return digits && digits < PW_STEAM_ID_MAX && nonzero;
}

/* The next KeyValues token from *at: 'q' with a quoted string in out (cut: set when it did not fit), '{' or '}', 0 at the end. */
static int token(const char **at, const char *end, char *out, size_t cap, int *cut)
{
    const char *p = *at;
    while (p < end && *p != '"' && *p != '{' && *p != '}') ++p;   /* blanks, and any stray character */
    if (p >= end) { *at = p; return 0; }
    if (*p != '"') { *at = p + 1; return *p; }
    size_t used = 0;
    *cut = 0;
    for (++p; p < end && *p != '"'; ++p) {
        char c = *p;
        if (c == '\\' && p + 1 < end) c = *++p;
        if (used + 1 < cap) out[used++] = c; else *cut = 1;
    }
    out[used] = 0;
    *at = p < end ? p + 1 : p;
    return 'q';
}

int pw_steam_manifest_read(const char *text, size_t length, char *appid, size_t appid_cap, char *installdir,
                           size_t installdir_cap)
{
    char key[64] = "", value[272];
    int depth = 0, have_key = 0, have_id = 0, have_dir = 0, cut = 0;
    const char *at = text, *end = text ? text + length : NULL;
    if (!text || !appid || !installdir || appid_cap < PW_STEAM_ID_MAX || !installdir_cap) return EINVAL;
    for (int kind; (kind = token(&at, end, value, sizeof(value), &cut)) && !(have_id && have_dir);) {
        if (kind == '{') { ++depth; have_key = 0; continue; }
        if (kind == '}') { --depth; have_key = 0; continue; }
        if (!have_key) { snprintf(key, sizeof(key), "%.63s", value); have_key = 1; continue; }
        have_key = 0;
        if (depth != 1 || cut) continue;
        if (!strcasecmp(key, "appid") && pw_steam_id_valid(value)) { strcpy(appid, value); have_id = 1; }
        else if (!strcasecmp(key, "installdir") && *value && strlen(value) < installdir_cap) {
            strcpy(installdir, value);
            have_dir = 1;
        }
    }
    return have_id && have_dir ? 0 : EINVAL;
}

/* host_exe lies in <steamapps>/common/<folder>/...: the length of the steamapps path and the folder's name. */
static int common_folder(const char *host_exe, size_t *steamapps, char *folder, size_t cap)
{
    static const char marker[] = "/steamapps/common/";
    for (const char *at = host_exe; *at; ++at) {
        if (strncasecmp(at, marker, sizeof(marker) - 1)) continue;
        const char *name = at + sizeof(marker) - 1, *slash = strchr(name, '/');
        if (!slash || slash == name || (size_t)(slash - name) >= cap) return 0;   /* the program must be inside it */
        memcpy(folder, name, (size_t)(slash - name));
        folder[slash - name] = 0;
        *steamapps = (size_t)(at - host_exe) + sizeof("/steamapps") - 1;
        return 1;
    }
    return 0;
}

static int manifest_name(const char *name)
{
    const size_t length = strlen(name);
    if (length < 17 || strncasecmp(name, "appmanifest_", 12) || strcasecmp(name + length - 4, ".acf")) return 0;
    for (size_t i = 12; i + 4 < length; ++i) if (name[i] < '0' || name[i] > '9') return 0;
    return 1;
}

static int from_manifests(const char *host_exe, char *id)
{
    char given[1024], steamapps[1024], folder[260], best[PW_STEAM_ID_MAX] = "";
    size_t prefix;
    if (!common_folder(host_exe, &prefix, folder, sizeof(folder)) || prefix >= sizeof(given)) return ENOENT;
    memcpy(given, host_exe, prefix);
    given[prefix] = 0;
    if (!pw_path_nocase(given, steamapps, sizeof(steamapps))) return ENOENT;   /* the folder as the file system spells it */
    DIR *directory = opendir(steamapps);
    if (!directory) return ENOENT;
    for (struct dirent *entry; (entry = readdir(directory));) {
        char file[1100 + 64], text[8192], found[PW_STEAM_ID_MAX], dir[260];
        if (!manifest_name(entry->d_name) || snprintf(file, sizeof(file), "%s/%s", steamapps, entry->d_name) >= (int)sizeof(file))
            continue;
        FILE *manifest = fopen(file, "rb");
        if (!manifest) continue;
        const size_t length = fread(text, 1, sizeof(text), manifest);   /* the two keys are at the top */
        fclose(manifest);
        if (pw_steam_manifest_read(text, length, found, sizeof(found), dir, sizeof(dir)) || strcasecmp(dir, folder)) continue;
        if (!*best || strtoull(found, NULL, 10) < strtoull(best, NULL, 10)) strcpy(best, found);
    }
    closedir(directory);
    if (!*best) return ENOENT;
    strcpy(id, best);
    return 0;
}

static int from_appid_file(const char *host_exe, char *id)
{
    char file[1100], held[1100], text[32];
    const char *slash = strrchr(host_exe, '/');
    const size_t folder = slash ? (size_t)(slash - host_exe) : 0;
    if (!slash || snprintf(file, sizeof(file), "%.*s/steam_appid.txt", (int)folder, host_exe) >= (int)sizeof(file)) return ENOENT;
    if (!pw_path_nocase(file, held, sizeof(held))) return ENOENT;
    FILE *appid = fopen(held, "rb");
    if (!appid) return ENOENT;
    const size_t length = fread(text, 1, sizeof(text) - 1, appid);
    fclose(appid);
    text[length] = 0;
    size_t from = 0, to;
    while (text[from] == ' ' || text[from] == '\t' || text[from] == '\r' || text[from] == '\n') ++from;
    for (to = from; text[to] >= '0' && text[to] <= '9'; ++to) {}
    text[to] = 0;
    if (!pw_steam_id_valid(text + from)) return ENOENT;
    strcpy(id, text + from);
    return 0;
}

int pw_steam_appid_of(const char *host_exe, char *id, size_t cap)
{
    char found[PW_STEAM_ID_MAX];
    if (!host_exe || !id || cap < PW_STEAM_ID_MAX) return EINVAL;
    if (from_manifests(host_exe, found) && from_appid_file(host_exe, found)) return ENOENT;
    strcpy(id, found);
    return 0;
}

/* The last word of NAME in env, or NULL. */
static const char *value_of(const char *const *env, size_t count, const char *name)
{
    const size_t length = strlen(name);
    const char *found = NULL;
    for (size_t i = 0; i < count; ++i)
        if (env[i] && !strncmp(env[i], name, length) && env[i][length] == '=') found = env[i] + length + 1;
    return found;
}

size_t pw_steam_id_words(const char *const *env, size_t count, const char *found, char out[2][PW_STEAM_ID_WORD])
{
    const char *game = value_of(env, count, "SteamGameId"), *app = value_of(env, count, "SteamAppId");
    const char *id = game ? game : app ? app : found;
    size_t added = 0;
    if (!pw_steam_id_valid(id)) return 0;
    if (!game) snprintf(out[added++], PW_STEAM_ID_WORD, "SteamGameId=%s", id);
    if (!app) snprintf(out[added++], PW_STEAM_ID_WORD, "SteamAppId=%s", id);
    return added;
}
