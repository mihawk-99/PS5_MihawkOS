/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_STEAM_GUIDE_H
#define PW_STEAM_GUIDE_H

/* The Steam guide (docs/STEAM.md, "The guide"): the first time the user asks for Steam, guided screens in the guided
 * setup's style say what happens and keep saying it until Steam is signed in. Its words are the setup's
 * (src/pw_setup.h, PW_TEXT_STEAM_*), in the setup's language.
 *
 * It spans three processes, so this file holds what they share, host-tested by tests/test_pw_steam_guide.c:
 *   - the launcher's pages (the setup tour's steps): what will happen, the download, a failure, signed in;
 *   - the record the launcher keeps across its restarts into the install and sign-in sessions, and what the launcher does
 *     with it when it starts again (pw_steam_guide_resume);
 *   - the sign-in session's own screens, which the title draws over the client's windows (native/proton_wine.cpp, the
 *     broker's cover): the state machine below, ticked once a second, decides which one shows and when the session ends.
 *
 * The times are the ones measured on the Pro (docs/STEAM.md, "The first run on a fresh install"): the install session
 * 22-27 s; the client's first start updates itself twice (241 and 239 MB packages) and showed its sign-in page about
 * 5.5 minutes in, after a black window from about 320 s to 340 s. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The launcher's pages: steps of the setup tour (examples/ps5proton/setup_tour.hpp). */
typedef enum {
    PW_STEAM_PAGE_INTRO = 0, /* the four stages, Start or Not now */
    PW_STEAM_PAGE_DOWNLOAD,  /* the installer coming from Valve, then "Installed. Starting Steam" before the sign-in session */
    PW_STEAM_PAGE_FAILED,    /* why the guide stopped: Try again or Back */
    PW_STEAM_PAGE_DONE,      /* signed in: Big Picture opens after a countdown, or at Continue */
} PwSteamPage;

/* Why the guide stopped, each with its words (pw_steam_guide_failure_text). */
typedef enum {
    PW_STEAM_FAIL_NONE = 0,
    PW_STEAM_FAIL_OFFLINE,   /* the name did not resolve or no connection was made */
    PW_STEAM_FAIL_CLOCK,     /* the certificate was refused for its dates: a console clock that is wrong */
    PW_STEAM_FAIL_DOWNLOAD,  /* any other failed download, or a file that is not the installer */
    PW_STEAM_FAIL_INSTALL,   /* the install session ended without the client in the prefix */
    PW_STEAM_FAIL_STALLED,   /* the client's update did not move for PW_STEAM_STALL_SECONDS */
    PW_STEAM_FAIL_CANCELLED, /* OPTIONS held in the sign-in session */
    PW_STEAM_FAIL_TIMEOUT,   /* no sign-in in 30 minutes */
    PW_STEAM_FAIL_GUARD,     /* no sign-in, after the client asked for a Steam Guard code */
    PW_STEAM_FAIL_ENDED,     /* the sign-in session ended without a sign-in and without a reason */
} PwSteamFailure;

/* The failure a download's PW_HTTPS_* status (src/pw_https.h) is, 0 (none) for PW_HTTPS_OK. */
PwSteamFailure pw_steam_guide_fetch_failure(int https_status);
/* Its words: a PwSetupText (src/pw_setup.h). */
int pw_steam_guide_failure_text(PwSteamFailure failure);

/* How a guided session ended: the title writes it into the record when it ends the session itself. */
typedef enum {
    PW_STEAM_RESULT_NONE = 0,  /* the session ended for another reason (the program ended, a restart) */
    PW_STEAM_RESULT_SIGNED_IN,
    PW_STEAM_RESULT_RETRY,     /* CROSS on "taking longer than usual": start the sign-in session again */
    PW_STEAM_RESULT_CANCELLED,
    PW_STEAM_RESULT_TIMEOUT,
    PW_STEAM_RESULT_STALLED,
    PW_STEAM_RESULT_GUARD,
    PW_STEAM_RESULTS
} PwSteamResult;
/* "signed-in", "retry", ...; "none" for PW_STEAM_RESULT_NONE. For the record and the logs. */
const char *pw_steam_guide_result_word(PwSteamResult result);

/* The record, /data/ps5proton/steam-guide.txt: "stage=install" or "stage=signin" (the session the launcher started last)
 * and "result=<word>" (how it ended, written by the title). No file: no guide under way. */
#define PW_STEAM_GUIDE_RECORD "/data/ps5proton/steam-guide.txt"
typedef enum { PW_STEAM_STAGE_NONE = 0, PW_STEAM_STAGE_INSTALL, PW_STEAM_STAGE_SIGNIN } PwSteamStage;
typedef struct PwSteamGuideRecord {
    PwSteamStage stage;
    PwSteamResult result;
} PwSteamGuideRecord;
/* 0, or the errno (ENOENT: no guide under way); *out is zeroed first. Unknown lines and words read as none. */
int pw_steam_guide_record_load(PwSteamGuideRecord *out, const char *path);
/* Written beside it and renamed into place; 0 or the errno. */
int pw_steam_guide_record_save(const PwSteamGuideRecord *record, const char *path);

/* What the launcher does when it starts with a record: */
typedef enum {
    PW_STEAM_RESUME_NOTHING = 0, /* no guide under way */
    PW_STEAM_RESUME_SIGNIN,      /* start the sign-in session (the install worked, or the user asked to try again) */
    PW_STEAM_RESUME_DONE,        /* the done page, then Big Picture */
    PW_STEAM_RESUME_FAILED,      /* the failed page, for *failure */
} PwSteamResume;
/* From the record and what the prefix holds now (the client installed, a remembered sign-in): a sign-in on disk wins
 * over any result, a missing client is an install that failed. */
PwSteamResume pw_steam_guide_resume(const PwSteamGuideRecord *record, int installed, int signed_in,
                                    PwSteamFailure *failure);

/* The installer is 2,380,800 bytes (steam-option-2026-10-24-02); a download carries no length, so the page's bar
 * fills against this and stops short of full until the file is checked. */
#define PW_STEAM_INSTALLER_BYTES 2380800ull
/* The bar's fraction, 0..0.97, for the bytes so far. */
float pw_steam_guide_download_fraction(unsigned long long bytes);

/* ---- the sign-in session's screens (the title draws them; src/pw_steam_screen.h) ---- */
typedef enum {
    PW_STEAM_SCREEN_NONE = 0,   /* nothing over the client: its sign-in window shows */
    PW_STEAM_SCREEN_INSTALLING, /* the install session, from start to end */
    PW_STEAM_SCREEN_UPDATING,   /* the client updates itself: elapsed time, megabytes */
    PW_STEAM_SCREEN_SLOW,       /* the same, with "taking longer than usual" and CROSS to try again */
    PW_STEAM_SCREEN_SCAN,       /* the client's browser has started: get the phone ready */
    PW_STEAM_SCREEN_SIGNED_IN,  /* the sign-in is saved: the session ends, Steam closes and reopens in Big Picture */
    PW_STEAM_SCREEN_DIALOG,     /* a game in the title opened a dialog box: its caption and text, CROSS to answer it */
} PwSteamScreen;

enum {
    /* No package byte and no new process for this long before the browser starts: "taking longer than usual". Each update's
     * download moves the package folder all the time; the quiet stretches are its unpacking, well under a minute. */
    PW_STEAM_SLOW_SECONDS = 180,
    /* ...and for this long: the session ends (PW_STEAM_RESULT_STALLED), the launcher offers Try again. */
    PW_STEAM_STALL_SECONDS = 600,
    /* The browser's start to its sign-in page was 20 s on the Pro (the black window): the phone screen shows this long,
     * or until CROSS. */
    PW_STEAM_SCAN_SECONDS = 25,
};

/* What the title knows each second. */
typedef struct PwSteamSessionInput {
    unsigned seconds;                 /* since the session began */
    unsigned long long package_bytes; /* the client's package folder (its downloads) */
    unsigned processes;               /* Windows processes started so far */
    int browser;                      /* the client's browser process has started */
    int signed_in;                    /* a remembered user is saved (and was not at the start) */
    int guard;                        /* the client asked for a Steam Guard code */
    int cross;                        /* CROSS was pressed since the last tick */
} PwSteamSessionInput;

typedef struct PwSteamSession {
    int signin;                       /* the sign-in session; else the install session */
    PwSteamScreen screen;
    unsigned active_at;               /* the last second something moved */
    unsigned long long package_bytes;
    unsigned processes;
    unsigned browser_at;              /* when the browser started; 0 before */
    unsigned signed_at;               /* when the sign-in was saved; 0 before */
    int scan_done;                    /* the phone screen was shown and left */
    int guard;
} PwSteamSession;

void pw_steam_session_begin(PwSteamSession *session, int signin);
/* One second's step: sets session->screen; returns a PwSteamResult to end the session with, or -1 to
 * go on. The install session only ever shows PW_STEAM_SCREEN_INSTALLING and ends with its program. */
int pw_steam_session_tick(PwSteamSession *session, const PwSteamSessionInput *input);
/* The result a session that ends for its own reasons reports: the sign-in watcher's cancel and timeout. A timeout or a
 * cancel after a Steam Guard request is reported as PW_STEAM_RESULT_GUARD. */
PwSteamResult pw_steam_session_ended(const PwSteamSession *session, PwSteamResult why);

#ifdef __cplusplus
}
#endif

#endif
