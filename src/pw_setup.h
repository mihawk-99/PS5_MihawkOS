/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SETUP_H
#define PW_SETUP_H

/* The first-launch guided setup's portable part (docs/GUIDED_SETUP.md): its steps, when it shows, its languages and
 * their text, and the record it keeps (/data/ps5proton/setup.txt). The launcher draws it
 * (examples/ps5proton/kit/setup_tour.cpp); tests/test_pw_setup.c checks every rule here.
 *
 * - It shows when the record has no done=1 (the first launch), when the runtime or the mihawk prefix is missing, or
 *   when it is asked for (Settings, or the first-run marker a test sets). Never without /data: then nothing can be
 *   written, and the launcher's own complaint says why.
 * - A language is listed only once its text is complete: every key, in characters its face carries (the launcher's,
 *   baked with the kit's "european" set by ps5/ui/setup-kit.sh, or its own CJK face).
 * - The chosen language is the setup's text, and the Windows locale (and so the keyboard layout) of a mihawk prefix
 *   made after it: the title pins it in the prefix at creation (native/proton_wine.cpp). */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PW_SETUP_NONE = -1,
    PW_SETUP_WELCOME = 0,
    PW_SETUP_LANGUAGE = 1,
    /* 2, 3 and 7 were How to install, the USB drive and FTP, gone since the runtime ships in the title folder (the
     * owner, 2026-10-31); the other steps keep their numbers in logs and test expectations */
    PW_SETUP_INSTALLING = 4,  /* the runtime from the title's own runtime-pieces/, started by itself */
    PW_SETUP_PREPARING = 5,   /* the first Mihawk OS session, which builds the prefix and ends by itself */
    PW_SETUP_DONE = 6,
    PW_SETUP_LIMITS = 8,      /* before Done: what MihawkOS is and the games it cannot run; cross held
                               * PW_SETUP_HOLD_SECONDS goes on */
    PW_SETUP_STEPS
} PwSetupStep;

typedef struct PwSetupFacts {
    int data;      /* /data is granted */
    int done;      /* the record says the setup was completed or skipped */
    int runtime;   /* the Wine runtime is installed */
    int prefix;    /* the mihawk prefix exists */
    int asked;     /* Settings or the first-run marker asked for the whole tour */
    int returning; /* back from the first Mihawk OS session the tour started (its marker, setup-preparing) */
} PwSetupFacts;

/* Where the tour starts, or PW_SETUP_NONE. Back from its first session, it goes on at Done, or at Preparing again
 * when the session ended before the prefix was made. */
PwSetupStep pw_setup_start(const PwSetupFacts *facts);
/* The step after `step` once it is finished: the install is skipped when the runtime is there, the first session
 * when the prefix is. */
PwSetupStep pw_setup_next(PwSetupStep step, const PwSetupFacts *facts);
/* Where Back goes from `step`; PW_SETUP_NONE where there is no going back (installing, done). */
PwSetupStep pw_setup_back(PwSetupStep step);
/* The limits page goes on only once cross has been held this long without letting go (the owner, 2026-10-31). */
#define PW_SETUP_HOLD_SECONDS 10.0f
/* The hold after a frame of dt seconds: dt more while cross is held, 0 once it is let go (a new hold starts over). */
float pw_setup_hold(float seconds, int held, float dt);

/* The text keys. Each language's table has every one, in this order (pw_setup.c). */
typedef enum {
    PW_TEXT_WELCOME_TITLE,
    PW_TEXT_WELCOME_BODY,
    PW_TEXT_GET_STARTED,
    PW_TEXT_LANGUAGE_TITLE,
    PW_TEXT_LANGUAGE_BODY,
    PW_TEXT_CONTINUE,
    PW_TEXT_RETRY,
    PW_TEXT_ERR_NO_PIECES,     /* the title folder holds no runtime pieces (runtime-pieces/) */
    PW_TEXT_ERR_PARTIAL,       /* {1} pieces of {2} */
    PW_TEXT_ERR_NO_SPACE,      /* {1} needed, {2} free */
    PW_TEXT_ERR_READ,
    PW_TEXT_ERR_DAMAGED,
    PW_TEXT_ERR_FULL,
    PW_TEXT_ERR_WRITE,
    PW_TEXT_ERR_CANCELLED,
    PW_TEXT_ERR_BUILD,
    PW_TEXT_INSTALLING_TITLE,
    PW_TEXT_INSTALLING_BODY,
    PW_TEXT_INSTALLING_CHECKING,
    PW_TEXT_INSTALLING_STATS,  /* {1} of {2} MB, {3} MB/s */
    PW_TEXT_INSTALLED,
    PW_TEXT_STOP,
    PW_TEXT_PREPARE_TITLE,
    PW_TEXT_PREPARE_BODY,
    PW_TEXT_PREPARE_NOTE,
    PW_TEXT_PREPARE_START,
    PW_TEXT_PREPARE_FAILED,
    PW_TEXT_BUILD_TITLE,       /* the first session's cover (PwSetupBuild) */
    PW_TEXT_BUILD_BODY,
    PW_TEXT_BUILD_SETTINGS,    /* its stages, in order */
    PW_TEXT_BUILD_DOTNET,
    PW_TEXT_BUILD_FINISHING,
    PW_TEXT_LIMITS_TITLE,      /* the limits page (PW_SETUP_LIMITS) */
    PW_TEXT_LIMITS_BODY,
    PW_TEXT_LIMITS_NOTE_TITLE,
    PW_TEXT_LIMITS_DRM,
    PW_TEXT_LIMITS_ANTICHEAT,
    PW_TEXT_LIMITS_RESULT,
    PW_TEXT_LIMITS_HOLD,       /* {1}: where the cross button's glyph is drawn */
    PW_TEXT_DONE_TITLE,
    PW_TEXT_DONE_BODY,
    PW_TEXT_DONE_STEAM,        /* Done's two tiles: a name, then a line */
    PW_TEXT_DONE_STEAM_BODY,
    PW_TEXT_DONE_LIBRARY,
    PW_TEXT_DONE_LIBRARY_BODY,
    PW_TEXT_HINT_SELECT,
    PW_TEXT_HINT_BACK,
    PW_TEXT_HINT_MOVE,
    PW_TEXT_STEP_OF,           /* {1} of {2} */
    PW_TEXT_GIGABYTES,         /* {1}: the number, written with PW_TEXT_DECIMAL */
    PW_TEXT_DECIMAL,
    /* The Steam guide (src/pw_steam_guide.h): the launcher's pages, then the screens its sessions draw */
    PW_TEXT_STEAM_TITLE,
    PW_TEXT_STEAM_BODY,
    PW_TEXT_STEAM_STEP_DOWNLOAD, /* the intro's four stages: name, then a line */
    PW_TEXT_STEAM_STEP_DOWNLOAD_BODY,
    PW_TEXT_STEAM_STEP_INSTALL,
    PW_TEXT_STEAM_STEP_INSTALL_BODY,
    PW_TEXT_STEAM_STEP_UPDATE,
    PW_TEXT_STEAM_STEP_UPDATE_BODY,
    PW_TEXT_STEAM_STEP_SIGNIN,
    PW_TEXT_STEAM_STEP_SIGNIN_BODY,
    PW_TEXT_STEAM_NOTE,
    PW_TEXT_STEAM_START,
    PW_TEXT_STEAM_NOT_NOW,
    PW_TEXT_STEAM_DOWNLOADING,
    PW_TEXT_STEAM_DOWNLOADING_BODY,
    PW_TEXT_STEAM_MEGABYTES, /* {1} of about {2} MB */
    PW_TEXT_STEAM_STARTING,
    PW_TEXT_STEAM_FAIL_TITLE,
    PW_TEXT_STEAM_ERR_OFFLINE, /* why the guide stopped (PwSteamGuideFailure) */
    PW_TEXT_STEAM_ERR_CLOCK,
    PW_TEXT_STEAM_ERR_DOWNLOAD,
    PW_TEXT_STEAM_ERR_INSTALL,
    PW_TEXT_STEAM_ERR_STALLED,
    PW_TEXT_STEAM_ERR_CANCELLED,
    PW_TEXT_STEAM_ERR_TIMEOUT,
    PW_TEXT_STEAM_ERR_GUARD,
    PW_TEXT_STEAM_ERR_ENDED,
    PW_TEXT_STEAM_INSTALLING_TITLE, /* the session's screens (title-drawn) */
    PW_TEXT_STEAM_INSTALLING_BODY,
    PW_TEXT_STEAM_UPDATING_TITLE,
    PW_TEXT_STEAM_UPDATING_BODY,
    PW_TEXT_STEAM_DOWNLOADED, /* {1} MB */
    PW_TEXT_STEAM_ELAPSED, /* {1}: minutes:seconds */
    PW_TEXT_STEAM_SLOW,
    PW_TEXT_STEAM_SCAN_TITLE,
    PW_TEXT_STEAM_SCAN_BODY,
    PW_TEXT_STEAM_CANCEL_NOTE,
    PW_TEXT_STEAM_SIGNED_IN,
    PW_TEXT_STEAM_SIGNED_IN_AS, /* {1}: the Steam display name */
    PW_TEXT_STEAM_REOPEN_BODY,
    PW_TEXT_STEAM_DONE_BODY,
    PW_TEXT_STEAM_OPENING_IN, /* {1} seconds */
    /* The memory guard's notices (native/proton_wine.cpp): what ran out, memory or the page tables (a pool apart,
     * src/pw_memory_guard.h), and what was done; pw_setup_guard_notice picks one. */
    PW_TEXT_NOTICE_MEMORY_ASKED,
    PW_TEXT_NOTICE_MEMORY_ENDED,
    PW_TEXT_NOTICE_MEMORY_STUCK,
    PW_TEXT_NOTICE_TABLES_ASKED,
    PW_TEXT_NOTICE_TABLES_ENDED,
    PW_TEXT_NOTICE_TABLES_STUCK,
    /* The main menu (examples/ps5proton/home_grid.hpp), Files and a game's Options; names (games, files, Steam,
     * Mihawk OS) are never translated */
    PW_TEXT_HOME_GAMES, /* {1}: how many */
    PW_TEXT_HOME_PAGE, /* {1} of {2} */
    PW_TEXT_HOME_DATE, /* {1}: the day, {2}: the month, as numbers */
    PW_TEXT_HOME_MIHAWK_BODY,
    PW_TEXT_HOME_START,
    PW_TEXT_HOME_NEEDS_DATA,
    PW_TEXT_HOME_UNAVAILABLE,
    PW_TEXT_HOME_32BIT,
    PW_TEXT_HOME_COMPANION, /* the Web UI's tile */
    PW_TEXT_HOME_COMPANION_BODY,
    PW_TEXT_HOME_SCAN, /* {1}: the address */
    PW_TEXT_HOME_CONNECT,
    PW_TEXT_HOME_SETTINGS, /* the dock */
    PW_TEXT_HOME_FILES,
    PW_TEXT_HOME_PAYLOADS,
    PW_TEXT_HINT_POINTER, /* the menu's hints */
    PW_TEXT_HINT_OPEN,
    PW_TEXT_HINT_PAGE,
    PW_TEXT_HINT_OPTIONS,
    PW_TEXT_HINT_THEMES,
    PW_TEXT_HINT_RESTART,
    PW_TEXT_HINT_CLOSE,
    PW_TEXT_HINT_CHOOSE,
    PW_TEXT_HINT_CHANGE,
    PW_TEXT_HINT_UP,
    PW_TEXT_STEAM_ROW_GET, /* Steam's tile: its step (src/pw_steam.h), a line */
    PW_TEXT_STEAM_ROW_DOWNLOADING,
    PW_TEXT_STEAM_ROW_INSTALLING,
    PW_TEXT_STEAM_ROW_FAILED,
    PW_TEXT_STEAM_ROW_SIGN_IN,
    PW_TEXT_STEAM_ROW_SIGNED_IN,
    PW_TEXT_STEAM_BADGE_DOWNLOADING, /* ... and a badge */
    PW_TEXT_STEAM_BADGE_INSTALLING,
    PW_TEXT_STEAM_BADGE_FAILED,
    PW_TEXT_STEAM_BADGE_SIGN_IN,
    PW_TEXT_STEAM_BADGE_SIGNED_IN,
    PW_TEXT_FILES_FOLDER, /* Files (src/pw_drive_browse.h) */
    PW_TEXT_FILES_PROGRAM,
    PW_TEXT_FILES_MEGABYTES, /* {1}: the number */
    PW_TEXT_FILES_NO_DATA,
    PW_TEXT_FILES_NO_DRIVE,
    PW_TEXT_FILES_GONE,
    PW_TEXT_FILES_UNREADABLE,
    PW_TEXT_FILES_EMPTY,
    PW_TEXT_FILES_OTHERS, /* {1}: how many */
    PW_TEXT_FILES_FOLDER_BODY,
    PW_TEXT_FILES_PROGRAM_BODY,
    PW_TEXT_FILES_READ_ONLY,
    PW_TEXT_FILES_CANNOT_START, /* {1}: the program, {2}: why (English, from the program check) */
    PW_TEXT_FILES_NOT_HERE,
    PW_TEXT_OPTIONS_TITLE, /* a game's Options */
    PW_TEXT_OPTIONS_GAME_MODE,
    PW_TEXT_OPTIONS_CONTROLLER,
    PW_TEXT_OPTIONS_INSIDE,
    PW_TEXT_OPTIONS_SETTINGS_ON,
    PW_TEXT_OPTIONS_SETTINGS_OFF,
    PW_TEXT_OPTIONS_SETTINGS_PAD, /* {1}: the controller */
    PW_TEXT_OPTIONS_ON_HERE,
    PW_TEXT_OPTIONS_OFF_HERE,
    PW_TEXT_OPTIONS_PAD_360,
    PW_TEXT_OPTIONS_PAD_ONE,
    PW_TEXT_OPTIONS_INSIDE_ON,
    PW_TEXT_OPTIONS_INSIDE_OFF,
    PW_TEXT_OPTIONS_BADGE_ON,
    PW_TEXT_OPTIONS_BADGE_OFF,
    PW_TEXT_OPTIONS_BADGE_SETTINGS,
    PW_TEXT_OPTIONS_GAME_MODE_BODY,
    PW_TEXT_OPTIONS_GAME_MODE_CYCLE,
    PW_TEXT_OPTIONS_CONTROLLER_BODY,
    PW_TEXT_OPTIONS_CONTROLLER_CYCLE,
    PW_TEXT_OPTIONS_INSIDE_BODY,
    PW_TEXT_OPTIONS_INSIDE_OFF_BODY,
    PW_TEXT_OPTIONS_APPLIES, /* {1}: the game */
    PW_TEXT_OPTIONS_NOT_SAVED, /* {1}: why (English) */
    /* Settings, Controls and the theme picker (examples/ps5proton/ps5proton.cpp); the themes' own names and
     * descriptions are the UI kit's and stay as they are */
    PW_TEXT_SET_SUBTITLE, /* Settings: the page */
    PW_TEXT_SET_CONTROLS_SUBTITLE,
    PW_TEXT_SET_CRUMB,
    PW_TEXT_SET_THEMES_SUBTITLE, /* the theme picker */
    PW_TEXT_SET_PALETTE,
    PW_TEXT_SET_SWATCH_BACKGROUND,
    PW_TEXT_SET_SWATCH_PANELS,
    PW_TEXT_SET_SWATCH_TEXT,
    PW_TEXT_SET_SWATCH_ACCENT,
    PW_TEXT_SET_THEME_CURRENT,
    PW_TEXT_SET_THEME_PREVIEW,
    PW_TEXT_SET_THEME_APPLIED,
    PW_TEXT_HINT_BROWSE, /* more hints */
    PW_TEXT_HINT_USE_THEME,
    PW_TEXT_HINT_TURN_ON,
    PW_TEXT_HINT_TURN_OFF,
    PW_TEXT_HINT_INSTALL,
    PW_TEXT_HINT_NEXT,
    PW_TEXT_HINT_DOWNLOAD,
    PW_TEXT_HINT_CHECK,
    PW_TEXT_HINT_RESTART_UPDATE,
    PW_TEXT_SET_CLOSE, /* the hold-OPTIONS ring */
    PW_TEXT_SET_CONTROLS, /* Settings' rows */
    PW_TEXT_SET_CONTROLS_SUB,
    PW_TEXT_SET_KEYBOARD, /* the on-screen keyboard's row */
    PW_TEXT_SET_KEYBOARD_OFF,
    PW_TEXT_SET_KEYBOARD_TOUCHPAD,
    PW_TEXT_SET_KEYBOARD_L3R3,
    PW_TEXT_SET_KEYBOARD_OPTIONS,
    PW_TEXT_SET_KEYBOARD_BODY,
    PW_TEXT_SET_ABOUT,
    PW_TEXT_SET_ABOUT_SUB,
    PW_TEXT_SET_RUNTIME,
    PW_TEXT_SET_SETUP,
    PW_TEXT_SET_SETUP_SUB,
    PW_TEXT_SET_MUSIC,
    PW_TEXT_SET_UPDATES,
    PW_TEXT_SET_LOGGING,
    PW_TEXT_SET_WEBUI,
    PW_TEXT_SET_WEBUI_SUB,
    PW_TEXT_SET_WEBUI_WAITING,
    PW_TEXT_SET_HZ,
    PW_TEXT_SET_AUTO,
    PW_TEXT_SET_OFF, /* the rows' lines */
    PW_TEXT_SET_GAME_MODE_ON,
    PW_TEXT_SET_GAME_MODE_OFF,
    PW_TEXT_SET_HZ_ON,
    PW_TEXT_SET_HZ_OFF,
    PW_TEXT_SET_LOG_ON,
    PW_TEXT_SET_LOG_OFF,
    PW_TEXT_SET_PAD_SEEN, /* {1}: the controller */
    PW_TEXT_SET_UPDATES_SUB,
    PW_TEXT_SET_AUTO_ON,
    PW_TEXT_SET_AUTO_OFF,
    PW_TEXT_SET_BADGE_CHECKING, /* the rows' badges */
    PW_TEXT_SET_BADGE_NEWEST,
    PW_TEXT_SET_BADGE_AVAILABLE,
    PW_TEXT_SET_BADGE_READY,
    PW_TEXT_SET_BADGE_DONE,
    PW_TEXT_SET_INSTALLED,
    PW_TEXT_SET_FROM_FOLDER,
    PW_TEXT_SET_NOT_SAVED, /* what a change did */
    PW_TEXT_SET_ON_NEXT_LAUNCH,
    PW_TEXT_SET_OFF_NEXT_LAUNCH,
    PW_TEXT_SET_ON_NEXT_SESSION,
    PW_TEXT_SET_OFF_NEXT_SESSION,
    PW_TEXT_SET_PAD_CHOSEN, /* {1}: the controller */
    PW_TEXT_SET_GAME_MODE_BODY_ON, /* the panels */
    PW_TEXT_SET_GAME_MODE_BODY_OFF,
    PW_TEXT_SET_AUTO_BODY,
    PW_TEXT_SET_AUTO_PRIVACY,
    PW_TEXT_SET_HZ_BODY_ON,
    PW_TEXT_SET_HZ_BODY_OFF,
    PW_TEXT_SET_ABOUT_COPYRIGHT, /* About: GPLv3's Appropriate Legal Notices */
    PW_TEXT_SET_ABOUT_LICENCE,
    PW_TEXT_SET_ABOUT_TEXTS, /* {1}: this title's licences folder, {2}: the runtime's */
    PW_TEXT_SET_ABOUT_SOURCE, /* {1}: where */
    PW_TEXT_SET_ABOUT_AFFILIATION,
    PW_TEXT_SET_RUNTIME_TITLE,
    PW_TEXT_SET_RUNTIME_BODY, /* {1}: the install folder */
    PW_TEXT_SET_RUNTIME_PROGRESS, /* {1} of {2} MB, {3} MB/s, piece {4} of {5} */
    PW_TEXT_SET_RUNTIME_CHECKING,
    PW_TEXT_SET_RUNTIME_KEEP,
    PW_TEXT_SET_RUNTIME_PRESS,
    PW_TEXT_SET_MUSIC_BODY,
    PW_TEXT_SET_MUSIC_PLAYING, /* {1}: the piece */
    PW_TEXT_SET_MUSIC_NOW, /* {1}: the piece */
    PW_TEXT_SET_MUSIC_OFF,
    PW_TEXT_SET_SETUP_BODY,
    PW_TEXT_SET_UPDATES_TITLE,
    PW_TEXT_SET_UPDATES_BODY,
    PW_TEXT_SET_UPDATES_PROGRESS, /* {1} of {2} MB */
    PW_TEXT_SET_UPDATES_PRESS,
    PW_TEXT_SET_LOG_BODY, /* {1}: the logs folder */
    PW_TEXT_SET_LOG_MORE,
    PW_TEXT_SET_WEBUI_CONNECT,
    PW_TEXT_SET_WEBUI_BODY,
    PW_TEXT_SET_PAD_BODY,
    PW_TEXT_SET_PAD_XBOX,
    PW_TEXT_SET_PAD_DUALSENSE,
    PW_TEXT_SET_MOUSE_TITLE,
    PW_TEXT_SET_MOUSE_BODY,
    PW_TEXT_SET_MOUSE_ASSIGNED,
    PW_TEXT_SET_MOUSE_SAVED,
    PW_TEXT_SET_CLICK_LEFT,
    PW_TEXT_SET_CLICK_RIGHT,
    PW_TEXT_SET_CLICK_MIDDLE,
    PW_TEXT_SET_RESTORE,
    PW_TEXT_BTN_UNASSIGNED, /* the controller's buttons (src/pw_controls.h); L1...R3 keep their names */
    PW_TEXT_BTN_TOUCHPAD,
    PW_TEXT_BTN_CROSS,
    PW_TEXT_BTN_CIRCLE,
    PW_TEXT_BTN_SQUARE,
    PW_TEXT_BTN_TRIANGLE,
    PW_TEXT_BTN_UP,
    PW_TEXT_BTN_DOWN,
    PW_TEXT_BTN_LEFT,
    PW_TEXT_BTN_RIGHT,
    PW_TEXT_BTN_OPTIONS,
    /* The UI kit's 30 themes (.deps/hui/src/ui/theme.cpp): each one's name, family and summary, in that order; the
     * frameworks a theme follows keep their names */
    PW_TEXT_THEME_ACRYLIC, PW_TEXT_THEME_ACRYLIC_FAMILY, PW_TEXT_THEME_ACRYLIC_SUMMARY,
    PW_TEXT_THEME_BRUTAL, PW_TEXT_THEME_BRUTAL_FAMILY, PW_TEXT_THEME_BRUTAL_SUMMARY,
    PW_TEXT_THEME_CLAY, PW_TEXT_THEME_CLAY_FAMILY, PW_TEXT_THEME_CLAY_SUMMARY,
    PW_TEXT_THEME_TILES, PW_TEXT_THEME_TILES_FAMILY, PW_TEXT_THEME_TILES_SUMMARY,
    PW_TEXT_THEME_GLOSS, PW_TEXT_THEME_GLOSS_FAMILY, PW_TEXT_THEME_GLOSS_SUMMARY,
    PW_TEXT_THEME_CLASSIC, PW_TEXT_THEME_CLASSIC_FAMILY, PW_TEXT_THEME_CLASSIC_SUMMARY,
    PW_TEXT_THEME_BLUEPRINT, PW_TEXT_THEME_BLUEPRINT_FAMILY, PW_TEXT_THEME_BLUEPRINT_SUMMARY,
    PW_TEXT_THEME_HAZARD, PW_TEXT_THEME_HAZARD_FAMILY, PW_TEXT_THEME_HAZARD_SUMMARY,
    PW_TEXT_THEME_CANDY, PW_TEXT_THEME_CANDY_FAMILY, PW_TEXT_THEME_CANDY_SUMMARY,
    PW_TEXT_THEME_CONTRAST, PW_TEXT_THEME_CONTRAST_FAMILY, PW_TEXT_THEME_CONTRAST_SUMMARY,
    PW_TEXT_THEME_PIXEL, PW_TEXT_THEME_PIXEL_FAMILY, PW_TEXT_THEME_PIXEL_SUMMARY,
    PW_TEXT_THEME_SOFT, PW_TEXT_THEME_SOFT_FAMILY, PW_TEXT_THEME_SOFT_SUMMARY,
    PW_TEXT_THEME_DAISY, PW_TEXT_THEME_DAISY_FAMILY, PW_TEXT_THEME_DAISY_SUMMARY,
    PW_TEXT_THEME_PICO, PW_TEXT_THEME_PICO_FAMILY, PW_TEXT_THEME_PICO_SUMMARY,
    PW_TEXT_THEME_ANT, PW_TEXT_THEME_ANT_FAMILY, PW_TEXT_THEME_ANT_SUMMARY,
    PW_TEXT_THEME_CHAKRA, PW_TEXT_THEME_CHAKRA_FAMILY, PW_TEXT_THEME_CHAKRA_SUMMARY,
    PW_TEXT_THEME_FRESH, PW_TEXT_THEME_FRESH_FAMILY, PW_TEXT_THEME_FRESH_SUMMARY,
    PW_TEXT_THEME_NEUTRAL, PW_TEXT_THEME_NEUTRAL_FAMILY, PW_TEXT_THEME_NEUTRAL_SUMMARY,
    PW_TEXT_THEME_PAPER, PW_TEXT_THEME_PAPER_FAMILY, PW_TEXT_THEME_PAPER_SUMMARY,
    PW_TEXT_THEME_HUMANE, PW_TEXT_THEME_HUMANE_FAMILY, PW_TEXT_THEME_HUMANE_SUMMARY,
    PW_TEXT_THEME_STANDARD, PW_TEXT_THEME_STANDARD_FAMILY, PW_TEXT_THEME_STANDARD_SUMMARY,
    PW_TEXT_THEME_PILL, PW_TEXT_THEME_PILL_FAMILY, PW_TEXT_THEME_PILL_SUMMARY,
    PW_TEXT_THEME_ADMIN, PW_TEXT_THEME_ADMIN_FAMILY, PW_TEXT_THEME_ADMIN_SUMMARY,
    PW_TEXT_THEME_FRIENDLY, PW_TEXT_THEME_FRIENDLY_FAMILY, PW_TEXT_THEME_FRIENDLY_SUMMARY,
    PW_TEXT_THEME_CRISP, PW_TEXT_THEME_CRISP_FAMILY, PW_TEXT_THEME_CRISP_SUMMARY,
    PW_TEXT_THEME_LAYERS, PW_TEXT_THEME_LAYERS_FAMILY, PW_TEXT_THEME_LAYERS_SUMMARY,
    PW_TEXT_THEME_UTILITY, PW_TEXT_THEME_UTILITY_FAMILY, PW_TEXT_THEME_UTILITY_SUMMARY,
    PW_TEXT_THEME_SKETCH, PW_TEXT_THEME_SKETCH_FAMILY, PW_TEXT_THEME_SKETCH_SUMMARY,
    PW_TEXT_THEME_LIGHT, PW_TEXT_THEME_LIGHT_FAMILY, PW_TEXT_THEME_LIGHT_SUMMARY,
    PW_TEXT_THEME_CODE, PW_TEXT_THEME_CODE_FAMILY, PW_TEXT_THEME_CODE_SUMMARY,
    PW_TEXT_THEME_IN_USE,      /* the picker's badge on the theme in use */
    /* The payload checklist's way through a fail (src/pw_payload_check.h): the hold, and the menu's line after it */
    PW_TEXT_PAYLOAD_SKIP_HOLD, /* {1}: where the triangle button's glyph is drawn */
    PW_TEXT_PAYLOAD_SKIPPED,
    /* The Steam memory warning (src/pw_steam_memory.h): its title, the numbers ({1} the census, {2} Steam's need, {3} the
     * swap point, in MB), the steps' heading, the steps (install or open), and one for extra payloads */
    PW_TEXT_STEAM_MEM_TITLE,
    PW_TEXT_STEAM_MEM_LINE,
    PW_TEXT_STEAM_MEM_STEPS,
    PW_TEXT_STEAM_MEM_STEP_CLOSE,
    PW_TEXT_STEAM_MEM_STEP_RESTART,
    PW_TEXT_STEAM_MEM_STEP_INSTALL,
    PW_TEXT_STEAM_MEM_STEP_OPEN,
    PW_TEXT_STEAM_MEM_STEP_PAYLOADS,
    PW_TEXT_COUNT
} PwSetupText;

typedef struct PwSetupLanguage {
    const char *tag;            /* BCP 47, the record's value: "fr-FR" */
    const char *name;           /* in its own language: "Français" */
    const char *flag;           /* its flag in ps5/assets/flags (lipis/flag-icons' code): "fr" */
    const char *locale;         /* the Unix locale Wine reads for it: "fr_FR.UTF-8" */
    const char *font;           /* its face in the title's assets/setup/fonts, or NULL for the launcher's own */
    const char *const *text;    /* PW_TEXT_COUNT strings, or NULL until its translation lands */
} PwSetupLanguage;

/* Every planned language, with or without text; *count is set. */
const PwSetupLanguage *pw_setup_languages(size_t *count);
/* The language a record's tag names, if it has text; NULL otherwise. */
const PwSetupLanguage *pw_setup_language(const char *tag);
/* The default: English (US). */
const PwSetupLanguage *pw_setup_default_language(void);
const char *pw_setup_text(const PwSetupLanguage *language, PwSetupText key);
/* The memory guard's notice: by_tables, the page tables ran out (the console's memory need not be full; a game in many
 * helpers, a Chromium game's processes, 2026-10-31); ended, the game was ended (else asked to close); can_end, the title
 * could end it (else the player closes MihawkOS from the home screen). */
PwSetupText pw_setup_guard_notice(int by_tables, int ended, int can_end);
/* text with {1}..{9} replaced by args[0..]; a missing argument leaves the braces. Returns the length written,
 * truncated to size - 1. */
size_t pw_setup_fill(char *out, size_t size, const char *text, const char *const *args, size_t count);

/* bytes as the language writes gigabytes, to two decimals: "1.19 GB", "1,19 Go". */
size_t pw_setup_gigabytes(const PwSetupLanguage *language, unsigned long long bytes, char *out, size_t size);

/* The record, /data/ps5proton/setup.txt: "language=<tag>" and "done=1" lines; any other line is ignored. */
typedef struct PwSetupRecord {
    char language[16];
    int done;
} PwSetupRecord;
/* 0, or the errno (ENOENT: none yet); *out is zeroed first, so a missing or unreadable record reads as not done. */
int pw_setup_record_load(PwSetupRecord *out, const char *path);
/* Written beside it and renamed into place after an fsync; 0 or the errno. */
int pw_setup_record_save(const PwSetupRecord *record, const char *path);
/* The first session ends once the prefix is made: wineboot (patch 0952) writes .update-timestamp after the update,
 * ending in " wow64". It writes it through msvcrt in text mode, "1734871457 wow64\r\n": an LF-only strip never
 * matched, and the first session of first-install-unattended-2026-10-19-03 never ended. Trims trailing CR, LF and
 * spaces from stamp in place; 1 when it is made. */
int pw_setup_prefix_made(char *stamp);

/* The first session's cover (docs/GUIDED_SETUP.md, "Preparing"): what the user sees instead of a black desktop while
 * wineboot makes the prefix. The launcher draws its words when Start is pressed and keeps that frame
 * (PW_SETUP_COVER_NAME, in /data/ps5proton); the title shows it through the GPU broker until the session ends, with a mark for each
 * stage and the time since the start at the places below (1920x1080, the frame's own pixels). The stages follow the
 * processes wineboot starts; first-install-unattended-2026-10-19-04 took 75 s: wine.inf's settings from the start, Wine
 * Mono from about 15 s (control.exe appwiz.cpl install_mono, then msiexec and its helpers, each command line naming
 * mono), the rest from about 58 s (iexplore /RegServer, the 32-bit settings), and the prefix made. */
typedef enum {
    PW_SETUP_BUILD_SETTINGS,
    PW_SETUP_BUILD_DOTNET,
    PW_SETUP_BUILD_FINISHING,
    PW_SETUP_BUILD_READY /* set by the title once pw_setup_prefix_made */
} PwSetupBuild;
/* The stage once a process with this command line (NULL: none known) has started; never earlier than stage. */
PwSetupBuild pw_setup_build_next(PwSetupBuild stage, const char *command_line);
#define PW_SETUP_COVER_NAME "setup-cover.ppm"
enum {
    PW_SETUP_COVER_WIDTH = 1920,
    PW_SETUP_COVER_HEIGHT = 1080,
    PW_SETUP_COVER_MARK_X = 394,   /* each stage's mark: its centre, 22 px in radius; its words start at x 446 */
    PW_SETUP_COVER_ROW_Y = 520,    /* the first stage's centre */
    PW_SETUP_COVER_ROW_STEP = 84
};
/* Draws the stage marks over frame (PW_SETUP_COVER_WIDTH x HEIGHT, 0xAARRGGBB: the display's B, G, R, A bytes), after
 * copying their square back from picture, the frame without them: a green disc with a check for each stage done, a
 * turning ring of dots for the current one (tick: the frame's number; the title draws ten a second), a grey ring for
 * each to come. Nothing outside that square is written. */
void pw_setup_cover_draw(unsigned *frame, const unsigned *picture, PwSetupBuild stage, unsigned tick);

#ifdef __cplusplus
}
#endif

#endif
