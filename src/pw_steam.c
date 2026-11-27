/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_steam.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* ---- the installer file ---- */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int pw_steam_installer_check(const uint8_t *first, size_t n, uint64_t total)
{
    if (total < PW_STEAM_INSTALLER_MIN || total > PW_STEAM_INSTALLER_MAX) return PW_STEAM_INSTALLER_SIZE;
    if (!first || n < 1024 || first[0] != 'M' || first[1] != 'Z') return PW_STEAM_INSTALLER_NOT_PE;
    const uint32_t pe = le32(first + 0x3c);
    /* The PE header, its COFF header (20 bytes), and an optional header with its data directories, all inside the first
     * KiB we are given. */
    if (pe < 0x40 || pe > n - 24 - 120 || memcmp(first + pe, "PE\0\0", 4) != 0) return PW_STEAM_INSTALLER_NOT_PE;
    const uint8_t *optional = first + pe + 4 + 20;
    const uint16_t magic = le16(optional);
    size_t directories;   /* offset of the data directory table in the optional header */
    if (magic == 0x10b) directories = 96;
    else if (magic == 0x20b) directories = 112;
    else return PW_STEAM_INSTALLER_NOT_PE;
    if (pe + 4 + 20 + directories + 5 * 8 > n) return PW_STEAM_INSTALLER_NOT_PE;
    /* Directory 4 is the certificate table: a file offset and a size. Valve signs the installer. */
    const uint8_t *certificate = optional + directories + 4 * 8;
    if (le32(certificate) == 0 || le32(certificate + 4) == 0) return PW_STEAM_INSTALLER_NOT_SIGNED;
    return PW_STEAM_INSTALLER_OK;
}

/* ---- the remembered sign-in ---- */

typedef struct Lexer { const char *at, *end; } Lexer;
enum { TOKEN_END, TOKEN_STRING, TOKEN_OPEN, TOKEN_CLOSE };

/* The next token: a quoted string (unescaped into buffer, cut at cap - 1), a brace, or the end. A comment ("//") runs
 * to the end of its line. */
static int next_token(Lexer *lexer, char *buffer, size_t cap)
{
    for (;;) {
        while (lexer->at < lexer->end && (*lexer->at == ' ' || *lexer->at == '\t' || *lexer->at == '\r' || *lexer->at == '\n'))
            lexer->at++;
        if (lexer->at + 1 < lexer->end && lexer->at[0] == '/' && lexer->at[1] == '/') {
            while (lexer->at < lexer->end && *lexer->at != '\n') lexer->at++;
            continue;
        }
        break;
    }
    if (lexer->at >= lexer->end) return TOKEN_END;
    if (*lexer->at == '{') { lexer->at++; return TOKEN_OPEN; }
    if (*lexer->at == '}') { lexer->at++; return TOKEN_CLOSE; }
    if (*lexer->at != '"') { lexer->at++; return next_token(lexer, buffer, cap); }   /* stray byte: skipped */
    lexer->at++;
    size_t used = 0;
    while (lexer->at < lexer->end && *lexer->at != '"') {
        char c = *lexer->at++;
        if (c == '\\' && lexer->at < lexer->end) {
            const char escaped = *lexer->at++;
            c = escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
        }
        if (cap && used + 1 < cap) buffer[used++] = c;
    }
    if (lexer->at >= lexer->end) return TOKEN_END;   /* cut inside a string */
    lexer->at++;
    if (cap) buffer[used] = 0;
    return TOKEN_STRING;
}

int pw_steam_remembered_user(const char *vdf, size_t length, char *persona, size_t cap)
{
    Lexer lexer = { vdf, vdf ? vdf + length : vdf };
    char key[96], value[160], name[96] = "";
    int found = 0, found_recent = 0;
    int depth = 0;
    int remember = 0, automatic = -1, recent = 0;
    char current[96] = "";
    if (persona && cap) persona[0] = 0;
    if (!vdf) return 0;
    for (;;) {
        const int token = next_token(&lexer, key, sizeof(key));
        if (token == TOKEN_END) break;
        if (token == TOKEN_CLOSE) {
            if (depth == 2) {
                /* A user block closed. */
                if (remember && automatic != 0 && (!found || (recent && !found_recent))) {
                    found = 1;
                    found_recent = recent;
                    snprintf(name, sizeof(name), "%s", current);
                }
                remember = 0; automatic = -1; recent = 0; current[0] = 0;
            }
            if (depth > 0) depth--;
            continue;
        }
        if (token == TOKEN_OPEN) { depth++; continue; }
        /* A string: a key. Its value is a string (a field) or an open brace (a block). */
        Lexer ahead = lexer;
        const int after = next_token(&ahead, value, sizeof(value));
        if (after == TOKEN_STRING) {
            lexer = ahead;
            if (depth == 2) {
                if (!strcmp(key, "RememberPassword")) remember = !strcmp(value, "1");
                else if (!strcmp(key, "AllowAutoLogin")) automatic = !strcmp(value, "1");
                else if (!strcmp(key, "MostRecent")) recent = !strcmp(value, "1");
                else if (!strcmp(key, "PersonaName")) snprintf(current, sizeof(current), "%.95s", value);
            }
        } else if (after == TOKEN_OPEN) {
            lexer = ahead;
            depth++;
        }
    }
    if (found && persona && cap) snprintf(persona, cap, "%s", name);
    return found;
}

/* ---- the client's set-up steps ---- */

PwSteamStep pw_steam_step(int installed, int signed_in, int downloading, int downloaded, int failed)
{
    if (installed) return signed_in ? PW_STEAM_STEP_SIGNED_IN : PW_STEAM_STEP_SIGN_IN_NEEDED;
    if (downloading) return PW_STEAM_STEP_DOWNLOADING;
    if (downloaded) return PW_STEAM_STEP_INSTALLING;
    return failed ? PW_STEAM_STEP_FAILED : PW_STEAM_STEP_NOT_INSTALLED;
}

const char *pw_steam_step_subtitle(PwSteamStep step)
{
    switch (step) {
    case PW_STEAM_STEP_DOWNLOADING: return "Downloading from Valve";
    case PW_STEAM_STEP_INSTALLING: return "Installing";
    case PW_STEAM_STEP_FAILED: return "Could not download. Press CROSS to try again";
    case PW_STEAM_STEP_SIGN_IN_NEEDED: return "Installed. Press CROSS to sign in";
    case PW_STEAM_STEP_SIGNED_IN: return "Signed in. Games that need it start it for you";
    case PW_STEAM_STEP_NOT_INSTALLED: break;
    }
    return "Press CROSS to download and install Valve's client";
}

const char *pw_steam_step_badge(PwSteamStep step)
{
    switch (step) {
    case PW_STEAM_STEP_DOWNLOADING: return "DOWNLOADING";
    case PW_STEAM_STEP_INSTALLING: return "INSTALLING";
    case PW_STEAM_STEP_FAILED: return "FAILED";
    case PW_STEAM_STEP_SIGN_IN_NEEDED: return "SIGN IN";
    case PW_STEAM_STEP_SIGNED_IN: return "SIGNED IN";
    case PW_STEAM_STEP_NOT_INSTALLED: break;
    }
    return "";
}

const char *pw_steam_step_action(PwSteamStep step)
{
    switch (step) {
    case PW_STEAM_STEP_NOT_INSTALLED: return "Download and install";
    case PW_STEAM_STEP_FAILED: return "Try again";
    case PW_STEAM_STEP_SIGN_IN_NEEDED: return "Sign in";
    case PW_STEAM_STEP_DOWNLOADING:
    case PW_STEAM_STEP_INSTALLING:
    case PW_STEAM_STEP_SIGNED_IN: break;
    }
    return "";
}

/* ---- the process rules ---- */

/* Each is measured (docs/PLAN.md, "Steam's client and Isaac on the Pro, inside the memory limit"). `fail` refuses to
 * start the program (the client's own start-up probes, its updater, the overlay's window); `run` starts the named
 * image in its place; `add` appends to its command line. The browser helper cannot be refused (the client's core
 * does not run without it) but runs as one process on every console: 13 helpers and 555 MiB became 2 and about 210.
 * Its own processes drew Big Picture more smoothly (base PS5, 13.60, 2026-10-27: scrolling 7 frames over 20 ms against
 * 66 in one process with the code budget's raise), but each helper is a system process and takes the console's system
 * page tables: on the base the browser's 10 to 12 processes took about 2,300 of the pool's 8,192 pages, Steam alone left
 * 486, and a game's start exhausted it ("system page table exhausted", 2026-10-28, owner-test-base). One process stays,
 * at the owner's word that day; its stutter is what the listed clients' code-budget raise is for. */
static const struct { const char *name, *action, *value; } kRules[] = {
    { "steamsysinfo.exe", "fail", "" },
    { "gldriverquery.exe", "fail", "" },
    { "gldriverquery64.exe", "fail", "" },
    { "vulkandriverquery.exe", "fail", "" },
    { "vulkandriverquery64.exe", "fail", "" },
    { "hardwareupdater.exe", "fail", "" },
    { "bin\\SteamService.exe", "run", "C:\\windows\\system32\\hostname.exe" },   /* the install-script runner */
    { "steamwebhelper.exe", "add", " --single-process --js-flags=--max-old-space-size=128 --disable-extensions --disable-background-networking --disable-dev-shm-usage --disk-cache-size=33554432" },
    { "gameoverlayui.exe", "fail", "" },
    { "gameoverlayui64.exe", "fail", "" },
    /* While a library game runs, the browser waits suspended (patch 2104): about 950 of the client's ~1,676 system page
     * tables on a fresh Pro (2026-10-29), and the client relaunches it 10-40 s after it ends, so it is held, not ended. */
    { "steamwebhelper.exe", "hold", "\\steamapps\\common\\" },
};

unsigned pw_steam_rule_count(void) { return (unsigned)(sizeof(kRules) / sizeof(kRules[0])); }

int pw_steam_rule(unsigned i, const char **name, const char **action, const char **value)
{
    if (i >= pw_steam_rule_count()) return 0;
    if (name) *name = kRules[i].name;
    if (action) *action = kRules[i].action;
    if (value) *value = kRules[i].value;
    return 1;
}

size_t pw_steam_rules(char *out, size_t cap)
{
    size_t used = 0;
    for (unsigned i = 0; i < pw_steam_rule_count(); i++) {
        const int wrote = snprintf(out + used, cap > used ? cap - used : 0, "%s%s|%s|%s", used ? ";" : "", kRules[i].name,
                                   kRules[i].action, kRules[i].value);
        if (wrote < 0 || (size_t)wrote >= (cap > used ? cap - used : 0)) { if (cap) out[0] = 0; return 0; }
        used += (size_t)wrote;
    }
    return used;
}

/* Whether the stored list has a rule for this program and action (the text up to the second '|', any case). */
static int has_rule(const char *stored, const char *name, const char *action)
{
    const size_t name_len = strlen(name), action_len = strlen(action);
    for (const char *rule = stored; rule && *rule; rule = strchr(rule, ';') ? strchr(rule, ';') + 1 : NULL)
        if (!strncasecmp(rule, name, name_len) && rule[name_len] == '|' &&
            !strncasecmp(rule + name_len + 1, action, action_len) && rule[name_len + 1 + action_len] == '|')
            return 1;
    return 0;
}

size_t pw_steam_rules_merge(const char *stored, char *out, size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "%s", stored ? stored : "");
    if (used >= cap) { if (cap) out[0] = 0; return 0; }
    for (unsigned i = 0; i < pw_steam_rule_count(); i++) {
        if (has_rule(stored, kRules[i].name, kRules[i].action)) continue;
        const int wrote = snprintf(out + used, cap - used, "%s%s|%s|%s", used ? ";" : "", kRules[i].name, kRules[i].action,
                                   kRules[i].value);
        if (wrote < 0 || (size_t)wrote >= cap - used) { out[0] = 0; return 0; }
        used += (size_t)wrote;
    }
    return used;
}

/* ---- the catalog entries ---- */

static int append(char *out, size_t cap, size_t *used, const char *text)
{
    const size_t length = strlen(text);
    if (*used + length + 1 > cap) return 0;
    memcpy(out + *used, text, length + 1);
    *used += length;
    return 1;
}

static int append_json_string(char *out, size_t cap, size_t *used, const char *text)
{
    if (!append(out, cap, used, "\"")) return 0;
    for (; *text; text++) {
        char piece[8];
        const unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') snprintf(piece, sizeof(piece), "\\%c", c);
        else if (c == '\n') snprintf(piece, sizeof(piece), "\\n");
        else if (c == '\t') snprintf(piece, sizeof(piece), "\\t");
        else if (c < 0x20) snprintf(piece, sizeof(piece), "\\u%04x", c);
        else { piece[0] = (char)c; piece[1] = 0; }
        if (!append(out, cap, used, piece)) return 0;
    }
    return append(out, cap, used, "\"");
}

int pw_steam_prefix_word(const char *text, size_t length, char *out, size_t cap)
{
    if (!text || !out) return 0;
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r')) --length;
    if (!length || length > 32 || length >= cap) return 0;
    for (size_t i = 0; i < length; i++)
        if (!((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= '0' && text[i] <= '9') || text[i] == '-')) return 0;
    memcpy(out, text, length);
    out[length] = 0;
    return 1;
}

size_t pw_steam_entry_json(char *out, size_t cap, const char *name, const char *detail, const char *executable,
                           const char *directory, const char *arguments, int desktop, const char *const *environment,
                           const char *prefix)
{
    size_t used = 0;
    if (!out || !cap || !name || !detail || !executable || !directory || !arguments) return 0;
    out[0] = 0;
    const struct { const char *label, *text; } fields[] = {
        { "name", name }, { "detail", detail }, { "executable", executable }, { "directory", directory },
        { "prefix", prefix ? prefix : PW_STEAM_PREFIX }, { "arguments", arguments } };
    if (!append(out, cap, &used, "{\n")) return 0;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (!append(out, cap, &used, "  \"") || !append(out, cap, &used, fields[i].label) ||
            !append(out, cap, &used, "\": ") || !append_json_string(out, cap, &used, fields[i].text) ||
            !append(out, cap, &used, ",\n"))
            return out[0] = 0, 0;
    }
    if (!append(out, cap, &used, desktop ? "  \"desktop\": true,\n  \"env\": [" : "  \"desktop\": false,\n  \"env\": ["))
        return out[0] = 0, 0;
    for (size_t i = 0; environment && environment[i]; i++) {
        if (!append(out, cap, &used, i ? ",\n    " : "\n    ") || !append_json_string(out, cap, &used, environment[i]))
            return out[0] = 0, 0;
    }
    if (!append(out, cap, &used, environment && environment[0] ? "\n  ]\n}\n" : "]\n}\n")) return out[0] = 0, 0;
    return used;
}

size_t pw_steam_installer_entry(char *out, size_t cap, const char *prefix)
{
    /* PROTON_STEAM_GUIDE: the Steam guide's screens over the session (src/pw_steam_guide.h) */
    const char *const environment[] = { "PROTON_STEAM_GUIDE=install", NULL };
    return pw_steam_entry_json(out, cap, "Steam setup", "Installs Valve's Steam client, then ends",
                               PW_STEAM_INSTALLER_WINDOWS_PATH, "Z:\\data\\ps5proton\\downloads", "/S", 0, environment, prefix);
}

size_t pw_steam_client_entry(char *out, size_t cap, int big_picture, const char *prefix)
{
    char rules[1024], rules_word[1100];
    if (!pw_steam_rules(rules, sizeof(rules))) return 0;
    snprintf(rules_word, sizeof(rules_word), "WINE_PS5_PROCESS_RULES=%s", rules);
    /* WINE_PS5_WINDOW_SURFACES=1: the client's windows go through the composed window route, as the shell's and the browser's do. On the direct
     * route its several swapchains each flipped the whole screen and the window flickered (2026-10-24, surfaces-window-2026-10-24-01). */
    const char *const signing_in[] = { rules_word, "PROTON_STEAM_SIGNIN_WATCH=1", "PROTON_SESSION_FOLLOWS=processes",
                                       "WINE_PS5_WINDOW_SURFACES=1", "PROTON_STEAM_GUIDE=signin", NULL };
    const char *const running[] = { rules_word, "PROTON_SESSION_FOLLOWS=processes", "WINE_PS5_WINDOW_SURFACES=1", NULL };
    return pw_steam_entry_json(out, cap, "Steam", "Valve's Steam, in Big Picture: your library, store and games",
                               PW_STEAM_CLIENT_WINDOWS_PATH, PW_STEAM_CLIENT_WINDOWS_DIR, big_picture ? "-bigpicture -noverifyfiles" : "", 0,
                               big_picture ? running : signing_in, prefix);
}
