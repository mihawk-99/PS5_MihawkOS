// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROTON_LOADING_H
#define PROTON_LOADING_H
#include <string>
#include <vector>

#include "../src/pw_steam_guide.h"
// A loading screen for the seconds before a game shows its first frame. The title draws it on the
// console's display itself, straight through VideoOut (a Java game, a .NET one or a DXVK one shows
// nothing of its own for 8 to 22 s), and closes it when the game's own presenter (Vulkan or OpenGL)
// asks for the display (Wine's release_display callback or the native VideoOut wrapper),
// or when the game ends without one: VideoOut takes
// one owner, and the presenter opens it next. PROTON_LOADING_SCREEN=0 leaves it out.
//
// name: what the screen says is starting; fonts: the folder of the launcher's fonts (Inter).
void proton_loading_start(const char *name, const char *fonts);
// A verdict screen instead of the loading screen (the tester probe, native/proton_tester_probe.cpp): a heading and its
// lines, drawn large and still; each line also goes to the klog. Call after proton_loading_start; again to change it.
void proton_loading_report(const char *title, const std::vector<std::string> &lines);
// The Aurelia session screen: with it on, the loading screen becomes the client's screen — an Aero-themed layout with
// a mark and the name, the phase the session is really in, the status it last reported, the sign-in QR (src/pw_qrcodegen.h)
// on a white card, a spinner or an install's progress bar, and the footer. The setters are called from the title's session
// loop (native/proton_wine.cpp aurelia_line); safe from any thread.
void proton_aurelia_overlay(bool on);
void proton_aurelia_phase(const char *line);
void proton_aurelia_challenge(const char *url);
void proton_aurelia_status(const char *line);
// The Steam runtime's first-run page (the SteamCMD setup): the account, password and Guard fields, the focus, the
// status line and whether the runtime is signed in. Drawn instead of the sign-in card; the keyboard is the title's.
void proton_aurelia_setup(int visible, const char *account, const char *password, const char *guard, int focus,
                          const char *status, int error, int ready);
// The Steam runtime session owns the display while its setup page is up: its console window (Wine's presenter) must
// not take VideoOut and close the page. Games keep the default (false): their presenter still takes the display.
void proton_loading_keep_display(bool keep);
void proton_aurelia_error(const char *line);
void proton_aurelia_progress(int percent, const char *line);
// The session's library screen: the games a list parsed, drawn on the canvas; the pad browses them.
void proton_aurelia_library(const char *account, const char *const *names, const unsigned *app_ids, int count);
// One browse step: left/right move one cell, up/down one row of the six-column grid; 0 while browsing, the chosen
// app id, or -1 to leave.
long proton_aurelia_browse(int left, int right, int up, int down, int cross, int back);

// Idempotent and safe from any thread; returns once the display is closed and the screen's memory
// released, with the line it said (empty when nothing was shown). why: for the log.
std::string proton_loading_stop(const char *why);
// The guided setup's first session (src/pw_setup.h, "The first session's cover"): instead of the empty desktop, the
// picture the launcher kept when Start was pressed, with each stage's mark and the time since the start drawn over it
// ten times a second, handed over by show (the GPU broker's set_cover). False, and nothing shown, when the picture is
// missing or not 1920x1080.
bool proton_cover_start(const char *picture, bool (*show)(const void *));
// A Windows process started (its command line, or NULL): the stage it marks (pw_setup_build_next).
void proton_cover_process(const char *command_line);
// The prefix is made: every stage is done and the time stops.
void proton_cover_ready();
// Idempotent; the broker shows no cover once it returns, and the picture is removed. why: for the log.
std::string proton_cover_stop(const char *why);
// The Steam guide's sessions (src/pw_steam_guide.h): its screens (src/pw_steam_screen.h), drawn by the title ten times a
// second and handed to show (the GPU broker's set_cover) over the client's windows. language: the setup's tag (NULL:
// English); the faces are app_root's assets (Inter, and the Chinese or Japanese cut). False, and nothing shown, when
// Inter is missing. Nothing shows until proton_guide_show names a screen.
bool proton_guide_start(const char *language, const char *app_root, bool (*show)(const void *));
// What shows now (PW_STEAM_SCREEN_NONE: the client's own windows); the drawing thread takes it up within 0.1 s.
void proton_guide_show(PwSteamScreen screen, unsigned seconds, unsigned long long bytes, const char *persona);
// A game's dialog box over its frames (PW_STEAM_SCREEN_DIALOG): its caption and its lines; NULL takes it off.
void proton_guide_dialog(const char *title, const char *text);
// Idempotent; the broker shows no cover once it returns. why: for the log.
std::string proton_guide_stop(const char *why);
#endif
