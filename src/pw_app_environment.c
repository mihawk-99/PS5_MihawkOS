/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_app_environment.h"
#include "pw_environment.h"
#include <errno.h>
#include <string.h>

int pw_app_environment_per_app(const char *word)
{
    const char *equals = word ? strchr(word, '=') : NULL;
    if (!equals || equals == word) return 0;
    const size_t length = (size_t)(equals - word);
    if (length >= 7 && !strncmp(word, "PROTON_", 7)) return 0;
    if (length >= 7 && !strncmp(word, "MIHAWK_", 7)) return 0;
    /* The in-process sync device is the title's (wine/patches/0890); a helper refuses it on WINE_PS5_CHILDREN
     * (patch 0925), and a catalog setting must not even reach its block. */
    if (length == 15 && !strncmp(word, "WINE_PS5_NTSYNC", 15)) return 0;
    return 1;
}

static const char *skip_prefix(const char *path)
{
    if (!strncmp(path, "\\\\?\\", 4) || !strncmp(path, "\\??\\", 4)) return path + 4;
    return path;
}

static char fold(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c == '/' ? '\\' : c;
}

int pw_app_environment_same_path(const char *a, const char *b)
{
    if (!a || !b) return 0;
    a = skip_prefix(a);
    b = skip_prefix(b);
    if (!*a || !*b) return 0;
    for (; *a && *b; ++a, ++b)
        if (fold(*a) != fold(*b)) return 0;
    return !*a && !*b;
}

PwAppMatch pw_app_environment_find(const PwAppEntry *entries, size_t count, const char *image,
                                   const char *command_line, size_t *index)
{
    size_t matches = 0, by_arguments = 0, without_arguments = 0;
    size_t first = 0, argument_match = 0, plain = 0;
    if (!entries || !image) return PW_APP_NONE;
    for (size_t i = 0; i < count; ++i) {
        if (!pw_app_environment_same_path(entries[i].executable, image)) continue;
        if (!matches++) first = i;
        const char *arguments = entries[i].arguments;
        if (!arguments || !*arguments) {
            if (!without_arguments++) plain = i;
        } else if (command_line && strstr(command_line, arguments)) {
            if (!by_arguments++) argument_match = i;
        }
    }
    if (!matches) return PW_APP_NONE;
    if (matches == 1) { if (index) *index = first; return PW_APP_FOUND; }
    if (by_arguments == 1) { if (index) *index = argument_match; return PW_APP_FOUND; }
    if (!by_arguments && without_arguments == 1) { if (index) *index = plain; return PW_APP_FOUND; }
    return PW_APP_AMBIGUOUS;
}

int pw_app_environment_join_overrides(const char *title, const char *word, char *out, size_t size)
{
    static const char name[] = "WINEDLLOVERRIDES=";
    if (!title || !*title || !word || strncmp(word, name, sizeof(name) - 1)) return 0;
    const char *value = word + sizeof(name) - 1;
    const size_t length = sizeof(name) - 1 + strlen(title) + 1 + strlen(value);
    if (!out || length >= size) return -1;
    memcpy(out, name, sizeof(name) - 1);
    memcpy(out + sizeof(name) - 1, title, strlen(title));
    out[sizeof(name) - 1 + strlen(title)] = ';';
    memcpy(out + sizeof(name) + strlen(title), value, strlen(value) + 1);
    return 1;
}

int pw_app_environment_block(const PwAppEntry *entry, char *out, size_t capacity, uint32_t *bytes, size_t *kept)
{
    const char *words[PW_APP_ENVIRONMENT_MAX_WORDS];
    size_t count = 0, packed = 0;
    if (!entry || !out || !bytes || (entry->env_count && !entry->env)) return EINVAL;
    const char *controller = NULL;   /* the entry's own, the last one named */
    int hid = 0;
    for (size_t i = 0; i < entry->env_count; ++i) {
        const char *equals = entry->env[i] ? strchr(entry->env[i], '=') : NULL;
        if (!equals || equals == entry->env[i]) return EINVAL;      /* a broken catalog entry shows, never vanishes */
        if (!strncmp(entry->env[i], "WINE_PS5_CONTROLLER=", 20)) controller = equals + 1;
        if (!strncmp(entry->env[i], "WINE_PS5_HID=", 13)) hid = 1;
    }
    const int dualsense = controller && !strcmp(controller, "dualsense");
    for (size_t i = 0; i < entry->env_count; ++i) {
        if (!pw_app_environment_per_app(entry->env[i])) continue;
        if (controller && !dualsense && !strcmp(entry->env[i], "SDL_XINPUT_ENABLED=0")) continue;
        if (count == PW_APP_ENVIRONMENT_MAX_WORDS) return E2BIG;
        words[count++] = entry->env[i];
    }
    if (controller && !hid) {
        if (count == PW_APP_ENVIRONMENT_MAX_WORDS) return E2BIG;
        words[count++] = dualsense ? "WINE_PS5_HID=1" : "WINE_PS5_HID=0";
    }
    if (pw_environment_pack(out, capacity, words, count, &packed)) return errno ? errno : EINVAL;
    *bytes = (uint32_t)packed;
    if (kept) *kept = count;
    return 0;
}
