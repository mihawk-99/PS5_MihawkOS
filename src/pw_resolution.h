/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_RESOLUTION_H
#define PW_RESOLUTION_H
/* The player's game resolution setting (MihawkOS's Settings, per-game Options and WebUI, saved beside
 * game-mode.txt as resolution.txt): 1080p (default, 1920x1080), 1440p (2560x1440) or 4K (3840x2160).
 *
 * Applies to game sessions (including store clients and direct launches); the MihawkOS desktop stays at
 * 1920x1080 unless WINE_PS5_RESOLUTION or WINE_PS5_DESKTOP is explicitly overridden. A game's own
 * WINE_PS5_RESOLUTION or WINE_PS5_DESKTOP in game.json "env" wins over the console setting. If PS5 VideoOut
 * refuses 1440p or 4K on a display that cannot take it, the GPU broker falls back cleanly to 1920x1080.
 *
 * File format: one line, "resolution 1080p", "resolution 1440p" or "resolution 4k". Missing or unreadable
 * file means 1080p. Save is atomic (temporary file, fsync, rename). Functions return errno values. */
enum {
    PW_RESOLUTION_1080P = 0,
    PW_RESOLUTION_1440P = 1,
    PW_RESOLUTION_4K = 2,
    PW_RESOLUTION_KINDS = 3
};

#ifdef __cplusplus
extern "C" {
#endif
/* Parses "1080p"/"1920x1080", "1440p"/"2560x1440", "4k"/"4K"/"2160p"/"3840x2160"; -1 when unknown. */
int pw_resolution_kind(const char *word);
/* Canonical id: "1080p", "1440p", "4k" (defaults to "1080p" out of range). */
const char *pw_resolution_word(int kind);
/* Display label: "1080p", "1440p", "4K" (out of range: "1080p"). */
const char *pw_resolution_name(int kind);
/* Uppercase badge: "1080P", "1440P", "4K". */
const char *pw_resolution_badge(int kind);
/* WINE_PS5_DESKTOP value: "1920x1080", "2560x1440", "3840x2160". */
const char *pw_resolution_desktop(int kind);
/* Pixel dimensions for kind (defaults to 1920x1080 out of range). */
void pw_resolution_dimensions(int kind, unsigned *width, unsigned *height);
/* *kind: PW_RESOLUTION_*; PW_RESOLUTION_1080P when missing (ENOENT) or invalid (EINVAL). */
int pw_resolution_load(int *kind, const char *path);
int pw_resolution_save(int kind, const char *path);
#ifdef __cplusplus
}
#endif
#endif
