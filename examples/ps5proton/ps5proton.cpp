/*
 * PS5_Proton - game library and the template UI kit's thirty themes.
 * Derived from PS5_VulkanTemplate's Samples menu and Themes picker.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kit.hpp"
#include "launcher_music.hpp"
#include "setup_tour.hpp"
#include "home_grid.hpp"
#include "../../src/pw_drive_browse.h"
#include "../../src/pw_capped_log.h"
#include "proton_webui.h"
#include "../../src/pw_qrcodegen.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

namespace {
class VulkanExample : public ps5ui::KitExample
{
	std::vector<ProtonGame> games;
	std::string problem;
	std::string sessionNote;   // why the last session failed, or that it never came back
	std::string noticesNote;   // what the last session said on screen
	std::string payloadsNote;  // the startup tip: what the user's payloads held at the last session's start
	bool payloadShown{ false };     // the payload checklist's page: failing, all there is until it passes; at a fresh start always
	bool payloadsSkipped{ false };  // the player held TRIANGLE past a failing checklist: the menu says so, this start only
	int payloadPicture{ -1 };       // frames until the checklist's picture is taken (the payload-check-show flag); -1: none
	hui::ui::ListView library, themes, settingsList, controlsList, buttonList;
	pw_controls controls{};
	std::string controlsNotice;
	int gameMode{1};
	int highRefresh{1};   // Settings > 120 Hz output (src/pw_high_refresh.h)
	int detailedLogs{0};   // Settings > Logging (src/pw_detailed_logs.h)
	int controller{PW_CONTROLLER_XBOX_360};   // Settings > Controller (src/pw_controller.h)
	unsigned webuiSeen{0};   // the WebUI's change count the launcher last reloaded at (ps5/src/proton_webui.h)
	std::string qrText;      // the QR code drawn, and its modules (pw_qrcodegen)
	std::vector<uint8_t> qrModules;
	int qrSize{0};
	std::map<std::string, std::pair<long long, uint32_t>> covers;   // entry id: its header art's time and texture
	enum class Page { library, aurelia, themes, settings, controls, buttons, files, options, steamWarning };
	// The Steam memory warning (src/pw_steam_memory.h): what asked for Steam, the census it judged, and its hold
	enum class SteamFrom { tile, setup, files };
	SteamFrom steamWarnFrom{ SteamFrom::tile };
	PwSteamCensus steamWarnCensus{};
	unsigned steamWarnNeed{ 0 };
	bool steamWarnSignedIn{ false }, steamWarnArmed{ false }, steamWarnPassed{ false };
	float steamWarnSeconds{ 0.0f };
	std::string steamWarnProgram;   // Files' program, started again once the player goes on
	// The main menu (home_grid.hpp) on the setup's sky; Files, a read-only look at Mihawk OS's C: drive whose programs
	// start as a library game does; Options, the focused game's own settings (its game.json env words, as the WebUI writes)
	HomeGrid home;
	SetupSky homeSky;
	std::vector<int> homeRows;   // the grid's games: their library rows (Steam and Mihawk OS have tiles of their own)
	int homeMihawk{ -1 };         // Mihawk OS's library row
	hui::ui::ListView filesList, optionsList;
	std::vector<PwDriveEntry> filesEntries;
	std::string filesAt, filesNote;   // the folder under drive_c ('/' between parts), and what the page says about it
	bool filesRefused{ false };       // filesNote says why a program cannot start (in red)
	int optionsGame{ -1 };             // the game Options shows: its index in games
	std::string optionsNote;
	bool optionsSaveFailed{ false };   // optionsNote says why a change was not saved (in red)
	// The menu's words: the language chosen in the setup (src/pw_setup.h's table) and, for Chinese or Japanese, its own
	// face, loaded only then and only once the menu shows (the setup's tour loads its own while it runs)
	const PwSetupLanguage *homeLanguage{ pw_setup_default_language() };
	bool homeLanguageReady{ false };
	hui::gfx::Font homeFace;
	std::string homeFaceName;
	uint32_t homeFaceTexture{ 0 };
	hui::ui::Fonts homeFonts{};

	// The Aurelia page's library: the snapshot a session wrote (native/proton_wine.cpp aurelia_save_library), so the
	// page renders without starting the client. account is the name its sign-in reported, if any.
	struct AureliaEntry { unsigned app_id; std::string name; unsigned minutes; bool update; };
	std::vector<AureliaEntry> aureliaEntries;
	std::string aureliaAccount;
	std::string aureliaNote;
	int aureliaFocus{ 0 };
	float aureliaScroll{ 0.0f };   // the shelf's leftmost tile, animated
	hui::ui::ListView aureliaList;
	Page page{Page::library};
	unsigned mouseAction{0};
	hui::gfx::DrawList scene;
	hui::ui::Feedback feedback;
	ps5ui::HoldToLeave leaving;
	bool dataAvailable{ false };
	float clock{ 0.0f }, applied{ 0.0f };
	int selectedTheme{ 0 };
	ProtonInstall install;   // Settings > Install runtime (proton_install.h)
	// The first-launch guided setup (setup_tour.hpp), while it shows
	std::unique_ptr<SetupTour> tour;
	PwSetupFacts setupFacts{};
	PwSetupStep setupStep{ PW_SETUP_NONE };
	bool setupSimulated{ false };
	bool setupUnattended{ false };   // proton_setup_unattended: a scripted run with the real engine and its sessions
	// MihawkOS's own music (launcher_music.hpp): R1 or Settings > Music switches it; the name shows for a moment
	Jukebox music;
	float musicShown{ 0.0f };
	ProtonUpdate updates;    // Settings > Check for updates (proton_update.h)
	std::string updateNotice;   // the main menu's line when a newer release is found (proton_update_notice)
	bool updateRestart{ false };   // a downloaded update waits: R2 restarts into it
	float updateNoticePoll{ 0.0f };
	bool drawnOnce{ false };
	// The store client's set-up (ps5/src/proton_steam.h), shown as a row of the Aurelia page's Accounts block
	ProtonSteam store;
	std::string clientNote;
	float clientPoll{ 0.0f };
	float steamPoll{ 0.0f };             // the Steam guide's next read of the client's state
	bool clientTestTried{ false }; bool clientTest{ false };
	bool clientInstallQueued{ false };
	bool aureliaAccounts{ false };       // the Aurelia page shows its Accounts rows instead of the shelf
	int clientRow{ -1 };                 // the library's Steam row (a default entry, not a catalog entry: its tag is -1)
	bool fromAccounts{ false };          // the press came from the Aurelia page's rows: the session returns there
	std::vector<ProtonGame> allGames;    // the catalog before the library's filter: the Aurelia page's entries are in it
	std::string runtimeBlocked;          // why no session can start now (the runtime or /data), or empty

	const hui::ui::Theme &theme() const
	{
		return hui::ui::themes()[page == Page::themes ? themes.focus() : selectedTheme];
	}

	void syncThemes()
	{
		for (int i = 0; i < (int)themes.items().size(); ++i)
			themes.item(i).badge = i == selectedTheme ? tr(PW_TEXT_THEME_IN_USE) : "";
	}

	void openThemes()
	{
		page = Page::themes;
		themes.set_focus(selectedTheme);
		themes.enter();
		syncThemes();
	}

	// What the page is showing, written where a run leaves it: a page opened, its game count, the account and the
	// focused row (the console's klog is not readable from a recorder on every boot, so the page proves itself here).
	void aureliaMark(const char *state, const char *note)
	{
		const std::string path = std::string(PROTON_DATA_ROOT) + "/aurelia-page.txt", temporary = path + ".tmp";
		FILE *file = fopen(temporary.c_str(), "w");
		if (!file) return;
		fprintf(file, "state=%s games=%zu account=%s focus=%d of=%zu note=%s\n", state, aureliaEntries.size(),
			aureliaAccount.empty() ? "(none)" : aureliaAccount.c_str(), aureliaFocus, aureliaEntries.size(),
			note ? note : "");
		fclose(file);
		rename(temporary.c_str(), path.c_str());
	}

	// The Aurelia page: the session's snapshot of the account's library, drawn in the launcher's theme, and the two
	// commands the page can run (sign-in, refresh) queued as ordinary game restarts with their own arguments.
	void openAurelia(bool accounts = false)
	{
		aureliaEntries.clear();
		aureliaAccount.clear();
		aureliaNote.clear();
		std::ifstream account(std::string(PROTON_DATA_ROOT) + "/aurelia-account.txt");
		if (account) std::getline(account, aureliaAccount);
		std::ifstream snapshot(std::string(PROTON_DATA_ROOT) + "/aurelia-library.txt");
		for (std::string line; std::getline(snapshot, line); ) {
			unsigned app_id = 0, minutes = 0;
			int update = 0;
			char name[160] = {};
			if (sscanf(line.c_str(), "%u\t%159[^\t]\t%u\t%d", &app_id, name, &minutes, &update) >= 2)
				aureliaEntries.push_back({app_id, name, minutes, update != 0});
		}
		// The Accounts rows: the library account (what lists and installs) and the store client (the official one, which a
		// game that calls the store's API runs on; set up once).
		std::vector<hui::ui::ListItem> accounts_rows(2);
		accounts_rows[0].title = "Library account";
		accounts_rows[0].tag = 0;
		accounts_rows[1].title = "Steam client";
		accounts_rows[1].tag = 1;
		aureliaList.set_items(accounts_rows);
		aureliaList.set_focus(0);
		aureliaAccounts = accounts || aureliaEntries.empty();
		aureliaFocus = 0;
		aureliaScroll = 0.0f;
		syncAccounts();
		if (aureliaEntries.empty())
			aureliaNote = aureliaAccount.empty() ? "Sign in to Aurelia to see your library."
				: "The library is empty; press TRIANGLE to refresh it.";
		else if (aureliaAccount.empty())
			aureliaNote = "Signed in; the account name shows after the next sign-in.";
		proton_say("launcher: Aurelia page, %zu game(s) from the session snapshot, account %s", aureliaEntries.size(),
			aureliaAccount.empty() ? "(none)" : aureliaAccount.c_str());
		page = Page::aurelia;
		aureliaMark("open", aureliaNote.c_str());
	}

	ProtonGame *aureliaProfile()
	{
		for (auto &game : allGames)
			if (game.id == "aurelia") return &game;
		return nullptr;
	}

	// Queues a session that runs the client with these arguments, through the ordinary game restart.
	void aureliaCommand(const char *arguments, const char *reason)
	{
		if (ps5.frameBudget) return;
		ProtonGame *game = aureliaProfile();
		if (!game) {
			aureliaNote = "The Aurelia entry is missing from the game catalog.";
			return;
		}
		if (sessionBlocked()) return;
		// The launcher comes back to this page instead of its library: the startup hook below reopens it.
		if (FILE *flag = fopen((std::string(PROTON_DATA_ROOT) + "/aurelia-open").c_str(), "w")) fclose(flag);
		ProtonGame queued = *game;
		queued.arguments = arguments;
		aureliaNote = std::string(arguments).find("login") != std::string::npos ? "Opening the sign-in screen..."
			: "Refreshing the library...";
		proton_queue_restart(&queued, proton_launch().cycle, reason);
		quit = true;
	}

	// A game the client installed and the catalog lists under its app id, launched the launcher's own way.
	ProtonGame *aureliaInstalled(const AureliaEntry &entry)
	{
		const std::string id = std::to_string(entry.app_id);
		for (auto &game : allGames)
			if (game.id == id) return &game;
		return nullptr;
	}

	void syncMusic()
	{
		settingsList.item(5).subtitle = music.title() ? music.title() : tr(PW_TEXT_SET_OFF);
		settingsList.item(5).badge = tr(music.title() ? PW_TEXT_OPTIONS_BADGE_ON : PW_TEXT_OPTIONS_BADGE_OFF);
	}

	void nextMusic()
	{
		music.next();
		musicShown = 2.5f;
		feedback.play(hui::audio::Cue::toggle);
		syncMusic();
	}

	void syncGameMode()
	{
		settingsList.item(1).subtitle = tr(gameMode ? PW_TEXT_SET_GAME_MODE_ON : PW_TEXT_SET_GAME_MODE_OFF);
		settingsList.item(1).badge = tr(gameMode ? PW_TEXT_OPTIONS_BADGE_ON : PW_TEXT_OPTIONS_BADGE_OFF);
	}

	void toggleGameMode()
	{
		const int changed = !gameMode;
		// Scripted UI runs preview the choice without overwriting the player's file.
		const int error = ps5.frameBudget ? 0 : pw_game_mode_save(changed, proton_game_mode_path().c_str());
		if (error) {
			controlsNotice = tr(PW_TEXT_SET_NOT_SAVED);
			proton_say("Game Mode: save failed (%d)", error);
		} else {
			gameMode = changed;
			controlsNotice = tr(gameMode ? PW_TEXT_SET_ON_NEXT_LAUNCH : PW_TEXT_SET_OFF_NEXT_LAUNCH);
			feedback.play(hui::audio::Cue::saved);
			proton_say("Game Mode: %s %s", ps5.frameBudget ? "preview" : "saved", gameMode ? "on" : "off");
		}
		syncGameMode();
	}

	void syncHighRefresh()
	{
		settingsList.item(10).subtitle = tr(highRefresh ? PW_TEXT_SET_HZ_ON : PW_TEXT_SET_HZ_OFF);
		settingsList.item(10).badge = tr(highRefresh ? PW_TEXT_OPTIONS_BADGE_ON : PW_TEXT_OPTIONS_BADGE_OFF);
	}

	void toggleHighRefresh()
	{
		const int changed = !highRefresh;
		// Scripted UI runs preview the choice without overwriting the player's file.
		const int error = ps5.frameBudget ? 0 : pw_high_refresh_save(changed, proton_high_refresh_path().c_str());
		if (error) {
			controlsNotice = tr(PW_TEXT_SET_NOT_SAVED);
			proton_say("120 Hz: save failed (%d)", error);
		} else {
			highRefresh = changed;
			controlsNotice = tr(highRefresh ? PW_TEXT_SET_ON_NEXT_LAUNCH : PW_TEXT_SET_OFF_NEXT_LAUNCH);
			feedback.play(hui::audio::Cue::saved);
			proton_say("120 Hz: %s %s", ps5.frameBudget ? "preview" : "saved", highRefresh ? "on" : "off");
		}
		syncHighRefresh();
	}

	void syncLogging()
	{
		settingsList.item(7).subtitle = tr(detailedLogs ? PW_TEXT_SET_LOG_ON : PW_TEXT_SET_LOG_OFF);
		settingsList.item(7).badge = tr(detailedLogs ? PW_TEXT_OPTIONS_BADGE_ON : PW_TEXT_OPTIONS_BADGE_OFF);
	}

	void toggleLogging()
	{
		const int changed = !detailedLogs;
		// Scripted UI runs preview the choice without overwriting the player's file.
		const int error = ps5.frameBudget ? 0 : pw_detailed_logs_save(changed, proton_logging_path().c_str());
		if (error) {
			controlsNotice = tr(PW_TEXT_SET_NOT_SAVED);
			proton_say("Logging: save failed (%d)", error);
		} else {
			detailedLogs = changed;
			controlsNotice = tr(detailedLogs ? PW_TEXT_SET_ON_NEXT_SESSION : PW_TEXT_SET_OFF_NEXT_SESSION);
			feedback.play(hui::audio::Cue::saved);
			proton_say("Logging: %s %s", ps5.frameBudget ? "preview" : "saved", detailedLogs ? "on" : "off");
		}
		syncLogging();
	}

	void syncController()
	{
		settingsList.item(8).subtitle = home.fill(PW_TEXT_SET_PAD_SEEN, { padName(controller) });
		settingsList.item(8).badge = controller == PW_CONTROLLER_DUALSENSE ? "DUALSENSE" : controller == PW_CONTROLLER_XBOX_ONE ?
			"XBOX ONE" : "XBOX 360";
	}

	// CROSS takes the next choice: Xbox 360, Xbox One, DualSense.
	void nextController()
	{
		const int changed = (controller + 1) % PW_CONTROLLER_KINDS;
		// Scripted UI runs preview the choice without overwriting the player's file.
		const int error = ps5.frameBudget ? 0 : pw_controller_save(changed, proton_controller_path().c_str());
		if (error) {
			controlsNotice = tr(PW_TEXT_SET_NOT_SAVED);
			proton_say("Controller: save failed (%d)", error);
		} else {
			controller = changed;
			controlsNotice = home.fill(PW_TEXT_SET_PAD_CHOSEN, { padName(controller) });
			feedback.play(hui::audio::Cue::saved);
			proton_say("Controller: %s %s", ps5.frameBudget ? "preview" : "saved", pw_controller_word(controller));
		}
		syncController();
	}

	void syncUpdates()
	{
		updates = proton_update_state();
		const PwSetupText badges[] = { PW_TEXT_COUNT, PW_TEXT_SET_BADGE_CHECKING, PW_TEXT_SET_BADGE_NEWEST, PW_TEXT_SET_BADGE_AVAILABLE,
			PW_TEXT_STEAM_BADGE_DOWNLOADING, PW_TEXT_SET_BADGE_READY, PW_TEXT_STEAM_BADGE_FAILED };
		settingsList.item(6).badge = badges[updates.phase] == PW_TEXT_COUNT ? "" : tr(badges[updates.phase]);
		settingsList.item(6).subtitle = updates.version.empty() ? tr(PW_TEXT_SET_UPDATES_SUB) : PS5_TITLE_NAME " " + updates.version;
		const bool automatic = proton_update_auto_enabled();
		settingsList.item(11).badge = tr(automatic ? PW_TEXT_OPTIONS_BADGE_ON : PW_TEXT_OPTIONS_BADGE_OFF);
		settingsList.item(11).subtitle = tr(automatic ? PW_TEXT_SET_AUTO_ON : PW_TEXT_SET_AUTO_OFF);
	}

	void syncInstall()
	{
		install = proton_install_state();
		const PwSetupText badges[] = { PW_TEXT_COUNT, PW_TEXT_SET_BADGE_READY, PW_TEXT_SET_BADGE_CHECKING, PW_TEXT_STEAM_BADGE_INSTALLING,
			PW_TEXT_SET_BADGE_DONE, PW_TEXT_STEAM_BADGE_FAILED };
		settingsList.item(3).badge = badges[install.phase] == PW_TEXT_COUNT ? "" : tr(badges[install.phase]);
		settingsList.item(3).subtitle = tr(install.phase == ProtonInstall::running ? PW_TEXT_STEAM_ROW_INSTALLING :
			install.phase == ProtonInstall::done ? PW_TEXT_SET_INSTALLED : PW_TEXT_SET_FROM_FOLDER);
	}

	// The on-screen keyboard's gestures in the menu's words, in PwOskTrigger order (src/pw_osk.h)
	static constexpr PwSetupText keyboardWords[PW_OSK_TRIGGERS] = { PW_TEXT_SET_KEYBOARD_OFF, PW_TEXT_SET_KEYBOARD_TOUCHPAD,
		PW_TEXT_SET_KEYBOARD_L3R3, PW_TEXT_SET_KEYBOARD_OPTIONS };

	void syncControls()
	{
		for (unsigned i = 0; i < 3; ++i)
			controlsList.item(i).subtitle = buttonName(controls.button[i]);
		controlsList.item(3).subtitle = tr(keyboardWords[controls.keyboard < PW_OSK_TRIGGERS ? controls.keyboard : 0]);
	}

	// A controller's name in the menu's words (the DualSense keeps its own)
	std::string padName(int kind) const
	{
		return kind == PW_CONTROLLER_XBOX_360 ? tr(PW_TEXT_OPTIONS_PAD_360) : kind == PW_CONTROLLER_XBOX_ONE ? tr(PW_TEXT_OPTIONS_PAD_ONE)
			: pw_controller_name(kind);
	}

	// A DualSense button's name in the menu's words (src/pw_controls.h's table; L1...R3 keep theirs)
	const char *buttonName(unsigned index) const
	{
		static const struct { const char *id; PwSetupText key; } named[] = { { "none", PW_TEXT_BTN_UNASSIGNED },
			{ "touchpad", PW_TEXT_BTN_TOUCHPAD }, { "cross", PW_TEXT_BTN_CROSS }, { "circle", PW_TEXT_BTN_CIRCLE },
			{ "square", PW_TEXT_BTN_SQUARE }, { "triangle", PW_TEXT_BTN_TRIANGLE }, { "up", PW_TEXT_BTN_UP },
			{ "down", PW_TEXT_BTN_DOWN }, { "left", PW_TEXT_BTN_LEFT }, { "right", PW_TEXT_BTN_RIGHT },
			{ "options", PW_TEXT_BTN_OPTIONS } };
		const char *id = pw_controls_button_id(index);
		for (const auto &one : named)
			if (id && !strcmp(id, one.id)) return tr(one.key);
		return pw_controls_button_label(index);
	}

	// A theme's name, family or summary (part 0, 1 or 2) in the menu's words (src/pw_setup.h's theme keys, in the kit's
	// themes() order); a theme the table does not know keeps the kit's own words
	const char *themeWords(const hui::ui::Theme &look, int part) const
	{
		const int index = themeIndex(look);
		if (index >= 0) return tr((PwSetupText)(PW_TEXT_THEME_ACRYLIC + 3 * index + part));
		return part == 0 ? look.name : part == 1 ? look.family : look.summary;
	}
	static int themeIndex(const hui::ui::Theme &look)
	{
		static const char *const ids[] = { "acrylic", "brutal", "clay", "tiles", "gloss", "classic", "blueprint", "hazard", "candy",
			"contrast", "pixel", "soft", "daisy", "pico", "ant", "chakra", "fresh", "neutral", "paper", "humane", "standard", "pill",
			"admin", "friendly", "crisp", "layers", "utility", "sketch", "light", "code" };
		for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
			if (!strcmp(look.id, ids[i])) return (int)i;
		return -1;
	}

	// Settings' rows and the Controls pages' words in the menu's language (again whenever it changes)
	void syncSettingsWords()
	{
		const PwSetupText rows[][2] = { { PW_TEXT_SET_CONTROLS, PW_TEXT_SET_CONTROLS_SUB }, { PW_TEXT_OPTIONS_GAME_MODE, PW_TEXT_COUNT },
			{ PW_TEXT_SET_ABOUT, PW_TEXT_SET_ABOUT_SUB }, { PW_TEXT_SET_RUNTIME, PW_TEXT_COUNT }, { PW_TEXT_SET_SETUP, PW_TEXT_SET_SETUP_SUB },
			{ PW_TEXT_SET_MUSIC, PW_TEXT_COUNT }, { PW_TEXT_SET_UPDATES, PW_TEXT_COUNT }, { PW_TEXT_SET_LOGGING, PW_TEXT_COUNT },
			{ PW_TEXT_OPTIONS_CONTROLLER, PW_TEXT_COUNT }, { PW_TEXT_SET_WEBUI, PW_TEXT_SET_WEBUI_SUB }, { PW_TEXT_SET_HZ, PW_TEXT_COUNT },
			{ PW_TEXT_SET_AUTO, PW_TEXT_COUNT } };
		for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]) && i < settingsList.items().size(); ++i) {
			settingsList.item(i).title = tr(rows[i][0]);
			if (rows[i][1] != PW_TEXT_COUNT) settingsList.item(i).subtitle = tr(rows[i][1]);
		}
		const PwSetupText clicks[] = { PW_TEXT_SET_CLICK_LEFT, PW_TEXT_SET_CLICK_RIGHT, PW_TEXT_SET_CLICK_MIDDLE, PW_TEXT_SET_KEYBOARD,
			PW_TEXT_SET_RESTORE };
		for (size_t i = 0; i < 5 && i < controlsList.items().size(); ++i) controlsList.item(i).title = tr(clicks[i]);
		for (unsigned i = 0; i < PW_CONTROLS_BUTTONS && i < buttonList.items().size(); ++i) buttonList.item(i).title = buttonName(i);
		const auto looks = hui::ui::themes();
		for (size_t i = 0; i < looks.size() && i < themes.items().size(); ++i)
			themes.item(i).title = themeWords(looks[i], 0), themes.item(i).subtitle = themeWords(looks[i], 1);
		syncThemes();
		syncControls();
		syncGameMode();
		syncHighRefresh();
		syncLogging();
		syncController();
		syncInstall();
		syncMusic();
		syncUpdates();
	}

	// The library's rows: the Steam row first, then each game; first: the focus starts on the first game below Steam.
	void syncLibraryRows(bool first)
	{
		std::vector<hui::ui::ListItem> rows;
		for (size_t i = 0; i < games.size(); ++i) {
			hui::ui::ListItem item;
			item.title = games[i].name;
			item.subtitle = games[i].detail;
			item.badge = games[i].complaint.empty() ? "" : games[i].x86_32 ? "32-BIT" : "UNAVAILABLE";
			item.disabled = !games[i].complaint.empty();
			item.tag = (int)i;
			rows.push_back(item);
		}
		if (dataAvailable) {
			// The default entry: Steam is always in the library. CROSS downloads and installs it from Valve when it is missing, opens
			// its sign-in once, and from then on runs it in Big Picture (src/pw_steam.h's steps).
			hui::ui::ListItem clientItem;
			clientItem.title = "Steam";
			clientItem.tag = -1;
			clientRow = 0;
			rows.insert(rows.begin(), clientItem);   // first: a default entry. The focus starts on the first game below it.
		}
		const int focus = library.focus();
		library.set_items(rows);
		if (first && clientRow == 0 && rows.size() > 1) library.set_focus(1);
		else if (!first && !rows.empty()) library.set_focus(std::min(focus, (int)rows.size() - 1));
		syncAccounts();
		syncHome();
	}

	// ---- The main menu (home_grid.hpp): the library's rows as tiles ----
	void syncHome()
	{
		homeRows.clear();
		homeMihawk = -1;
		for (int row = 0; row < (int)library.items().size(); ++row) {
			const int tag = library.items()[row].tag;
			if (tag >= 0 && games[tag].id == "mihawk-os") homeMihawk = row;
			else if (tag >= 0) homeRows.push_back(row);
		}
		home.sync(homeRows.size(), true);   // Steam's tile always: without /data it says so (its row needs /data)
	}

	HomeGrid::Game homeGame(int row)
	{
		HomeGrid::Game tile;
		if (row < 0 || row >= (int)library.items().size()) return tile;
		const auto &item = library.items()[row];
		tile.name = item.title, tile.sub = item.subtitle, tile.disabled = item.disabled;
		if (item.tag >= 0) {
			const ProtonGame &game = games[item.tag];
			tile.badge = game.complaint.empty() ? "" : tr(game.x86_32 ? PW_TEXT_HOME_32BIT : PW_TEXT_HOME_UNAVAILABLE);
			tile.cover = coverTexture(game.id);
		}
		return tile;
	}

	const char *tr(PwSetupText key) const { return pw_setup_text(homeLanguage, key); }

	// Steam's tile: its step's line and badge in the menu's words (src/pw_steam.h's steps); without /data, why not
	HomeGrid::Game homeSteam()
	{
		if (clientRow < 0) return { "Steam", tr(PW_TEXT_HOME_NEEDS_DATA), "", true, 0 };
		PwSetupText line = PW_TEXT_STEAM_ROW_GET, badge = PW_TEXT_COUNT;
		switch (proton_steam_step(store)) {
		case PW_STEAM_STEP_NOT_INSTALLED: break;
		case PW_STEAM_STEP_DOWNLOADING: line = PW_TEXT_STEAM_ROW_DOWNLOADING, badge = PW_TEXT_STEAM_BADGE_DOWNLOADING; break;
		case PW_STEAM_STEP_INSTALLING: line = PW_TEXT_STEAM_ROW_INSTALLING, badge = PW_TEXT_STEAM_BADGE_INSTALLING; break;
		case PW_STEAM_STEP_FAILED: line = PW_TEXT_STEAM_ROW_FAILED, badge = PW_TEXT_STEAM_BADGE_FAILED; break;
		case PW_STEAM_STEP_SIGN_IN_NEEDED: line = PW_TEXT_STEAM_ROW_SIGN_IN, badge = PW_TEXT_STEAM_BADGE_SIGN_IN; break;
		case PW_STEAM_STEP_SIGNED_IN: line = PW_TEXT_STEAM_ROW_SIGNED_IN, badge = PW_TEXT_STEAM_BADGE_SIGNED_IN; break;
		}
		HomeGrid::Game tile = homeGame(clientRow);
		tile.sub = tr(line);
		tile.badge = badge == PW_TEXT_COUNT ? "" : tr(badge);
		return tile;
	}

	// The menu's language, from the setup's record (a scripted run previews PROTON_SETUP_LANGUAGE, as the tour does);
	// every word the menu, Files and Options draw is checked against the face it is drawn in (the render test reads the
	// line: no missing glyph, in any language)
	void useHomeLanguage()
	{
		homeLanguageReady = true;
		PwSetupRecord record;
		pw_setup_record_load(&record, SetupTour::recordPath);
		const char *preview = ps5.frameBudget ? getenv("PROTON_SETUP_LANGUAGE") : nullptr;
		const PwSetupLanguage *chosen = pw_setup_language(preview ? preview : record.language);
		homeLanguage = chosen ? chosen : pw_setup_default_language();
		const std::string wanted = homeLanguage->font ? homeLanguage->font : "";
		if (wanted != homeFaceName) {
			if (homeFaceTexture) kit.renderer.destroy_texture(homeFaceTexture);
			homeFaceTexture = 0, homeFaceName.clear(), homeFace = hui::gfx::Font();
			std::string data;
			if (!wanted.empty() && hui::save::read_file(getAssetPath() + "setup/fonts/" + wanted + ".huifont", &data) &&
				homeFace.load(data) && (homeFaceTexture = kit.renderer.create_font_texture(homeFace))) {
				homeFaceName = wanted;
				// Its cost while loaded: the atlas, once in memory and once as an R8 texture
				proton_say("home: face %s loaded: %zu KiB file, atlas %ux%u, %zu KiB in memory and %zu KiB of texture",
					wanted.c_str(), data.size() >> 10, homeFace.atlas_width(), homeFace.atlas_height(), homeFace.atlas().size() >> 10,
					((size_t)homeFace.atlas_width() * homeFace.atlas_height()) >> 10);
			} else if (!wanted.empty()) {
				proton_say("home: no face %s; %s keeps the kit's", wanted.c_str(), homeLanguage->tag);
			}
		}
		homeFonts = kit.fonts;
		if (homeFaceTexture)   // every role a theme may draw words with (the Pixel and Sketch looks too)
			homeFonts.regular = homeFonts.semibold = homeFonts.display = homeFonts.mono = homeFonts.pixel = homeFonts.hand =
				{ &homeFace, homeFaceTexture };
		home.setLanguage(homeLanguage, homeFonts, kit.fonts);
		syncSettingsWords();
		int strings = 0, missing = 0;
		std::string first;
		auto check = [&](PwSetupText key) {
			const std::string words = tr(key);
			++strings;
			for (size_t i = 0; i < words.size();) {
				const size_t at = i;
				const uint32_t c = hui::gfx::next_codepoint(words, &i);
				if (homeFonts.regular.font->has_glyph(c) && homeFonts.semibold.font->has_glyph(c)) continue;
				if (!missing++) first = words.substr(at, i - at) + " in \"" + words + "\"";
			}
		};
		for (int key = PW_TEXT_HOME_GAMES; key < PW_TEXT_COUNT; ++key) check((PwSetupText)key);   // the menu's, Settings' and the themes'
		for (PwSetupText key : { PW_TEXT_HINT_MOVE, PW_TEXT_HINT_BACK, PW_TEXT_HINT_SELECT, PW_TEXT_DECIMAL }) check(key);
		// The table's English stays the kit's own words: a theme the kit adds or rewords shows here
		int known = 0, differ = 0;
		const PwSetupLanguage *kept = homeLanguage;
		homeLanguage = pw_setup_default_language();
		for (const auto &look : hui::ui::themes()) {
			if (themeIndex(look) < 0) continue;   // not in the table: the kit's own words
			++known;
			if (strcmp(themeWords(look, 0), look.name) || strcmp(themeWords(look, 1), look.family) ||
				strcmp(themeWords(look, 2), look.summary)) ++differ;
		}
		homeLanguage = kept;
		proton_say("home: themes: %d of %zu in the table, %d whose English differs from the kit's", known, hui::ui::themes().size(), differ);
		proton_say("home: language %s, face %s, %d strings, %d characters missing%s%s", homeLanguage->tag,
			homeFaceName.empty() ? "the kit's" : homeFaceName.c_str(), strings, missing, missing ? ", first: " : "", first.c_str());
	}

	// CROSS on a game's tile (Mihawk OS's too): the library row's start, refused with its reason when it cannot
	void launchRow(int row)
	{
		const auto &item = library.items()[row];
		ProtonGame &game = games[item.tag];
		if (item.disabled) {
			feedback.play(hui::audio::Cue::error);
			proton_say("home: %s cannot start: %s", game.name.c_str(), game.complaint.c_str());
			return;
		}
		feedback.play(hui::audio::Cue::select);
		proton_say("home: %s chosen%s", game.name.c_str(), ps5.frameBudget && !setupUnattended ? " (a scripted run starts nothing)" : "");
		if (!ps5.frameBudget || setupUnattended) {   // an unattended setup goes on from Done into Mihawk OS
			proton_queue_restart(&game, proton_launch().cycle, "chosen");
			quit = true;
		}
	}

	void homeAction(HomeGrid::Action action, int item)
	{
		switch (action) {
		case HomeGrid::Action::none: break;
		case HomeGrid::Action::mihawk: if (homeMihawk >= 0) launchRow(homeMihawk); break;
		case HomeGrid::Action::game: launchRow(homeRows[item]); break;
		case HomeGrid::Action::steam:
			fromAccounts = false;
			if (!steamMemoryWarns(SteamFrom::tile, "Steam's tile")) pressSteam();
			break;
		case HomeGrid::Action::webui:   // its Settings row: the address, and what the WebUI does
			page = Page::settings;
			settingsList.set_focus(9);
			settingsList.enter();
			break;
		case HomeGrid::Action::settings:
			page = Page::settings;
			settingsList.enter();
			break;
		case HomeGrid::Action::files: openFiles(""); break;
		case HomeGrid::Action::payloads: startPayloadCheck(); break;
		case HomeGrid::Action::options: openOptions(library.items()[homeRows[item]].tag); break;
		}
	}

	// ---- Files: Mihawk OS's C: drive, read only (src/pw_drive_browse.h); a program starts as a library game does ----
	void openFiles(const std::string &at, const std::string &focusName = "")
	{
		page = Page::files;
		filesAt = at;
		filesNote.clear();
		filesRefused = false;
		filesEntries.clear();
		std::vector<hui::ui::ListItem> rows;
		char folder[4096];
		PwDriveEntry *list = nullptr;
		size_t count = 0, others = 0;
#if defined(PS5_HOST_REFERENCE)
		const bool reachable = true;   // the PC's reference build reads its own folder (proton_webui_data_root)
#else
		const bool reachable = dataAvailable;
#endif
		const int error = !reachable ? EACCES
			: pw_drive_folder(proton_webui_data_root().c_str(), "mihawk", at.c_str(), folder, sizeof(folder));
		const int listed = error ? error : pw_drive_list(folder, &list, &count, &others);
		if (!reachable) filesNote = tr(PW_TEXT_FILES_NO_DATA);
		else if (error == ENOENT && at.empty()) filesNote = tr(PW_TEXT_FILES_NO_DRIVE);
		else if (error == ENOENT) filesNote = tr(PW_TEXT_FILES_GONE);
		else if (listed) filesNote = tr(PW_TEXT_FILES_UNREADABLE);
		else if (others) filesNote = home.fill(PW_TEXT_FILES_OTHERS, { std::to_string(others) });
		else if (!count) filesNote = tr(PW_TEXT_FILES_EMPTY);
		int focus = 0;
		for (size_t i = 0; i < count; ++i) {
			filesEntries.push_back(list[i]);
			hui::ui::ListItem row;
			row.title = list[i].name;
			row.subtitle = tr(list[i].dir ? PW_TEXT_FILES_FOLDER : PW_TEXT_FILES_PROGRAM);
			row.chevron = list[i].dir != 0;
			if (!list[i].dir) {   // megabytes to one decimal, as the language writes them
				char number[32];
				snprintf(number, sizeof(number), "%.1f", list[i].size / 1048576.0);
				std::string size = number;
				size.replace(size.find('.'), 1, tr(PW_TEXT_DECIMAL));
				row.value = home.fill(PW_TEXT_FILES_MEGABYTES, { size });
			}
			row.tag = (int)i;
			if (focusName == list[i].name) focus = (int)i;
			rows.push_back(row);
		}
		free(list);
		filesList.set_items(rows);
		filesList.set_focus(focus);
		filesList.enter();
		char where[4096] = "C:\\";
		pw_drive_windows_path(at.c_str(), "", where, sizeof(where));
		proton_say("files: %s: %zu shown, %zu others%s%s", where, count, others, filesNote.empty() ? "" : "; ", filesNote.c_str());
	}

	void filesInput(const hui::InputFrame &input)
	{
		const auto event = filesEntries.empty() ? hui::ui::Event::none : filesList.handle(input, feedback);
		if (event == hui::ui::Event::cancelled || input.is_pressed(hui::Action::back)) {
			char up[4096];
			if (pw_drive_parent(filesAt.c_str(), up, sizeof(up))) {
				page = Page::library;
			} else {
				const size_t slash = filesAt.find_last_of('/');
				openFiles(up, slash == std::string::npos ? filesAt : filesAt.substr(slash + 1));
			}
			feedback.play(hui::audio::Cue::back);
			return;
		}
		if (event != hui::ui::Event::activated) return;
		const PwDriveEntry &entry = filesEntries[filesList.focus()];
		char next[4096];
		if (entry.dir && !pw_drive_child(filesAt.c_str(), entry.name, next, sizeof(next))) openFiles(next);
		else if (!entry.dir) filesLaunch(entry.name);
	}

	// CROSS on a program: Mihawk OS's entry with this program (wine-run.txt's path=), so the session checks it and picks
	// in-title or a helper from the image as for any game (native/proton_wine.cpp); none of the desktop comes with it
	void filesLaunch(const char *name)
	{
		if (!steamWarnPassed && !strcasecmp(name, "steam.exe") && steamMemoryWarns(SteamFrom::files, "Files' steam.exe")) {
			steamWarnProgram = name;
			return;
		}
		char path[4096];
		const auto mihawk = std::find_if(games.begin(), games.end(), [](const ProtonGame &game) { return game.id == "mihawk-os"; });
		if (pw_drive_windows_path(filesAt.c_str(), name, path, sizeof(path)) || mihawk == games.end()) {
			filesNote = tr(PW_TEXT_FILES_NOT_HERE), filesRefused = true;
			feedback.play(hui::audio::Cue::error);
			return;
		}
		ProtonGame program = *mihawk;
		program.run_path = program.executable = path;
		program.name = name;
		program.env.clear();
		program.directory.clear();
		program.arguments.clear();
		if (program.complaint.empty()) {   // the runtime's or /data's reason stays; else the session's own check, ahead
			program.x86_32 = false;
			if (!ps5.frameBudget) proton_check_program(program);
		}
		proton_say("files: run %s (profile=mihawk-os path=%s)%s%s%s", name, path, program.complaint.empty() ? "" : " refused: ",
			program.complaint.c_str(), ps5.frameBudget ? " (a scripted run starts nothing)" : "");
		if (!program.complaint.empty()) {
			filesNote = home.fill(PW_TEXT_FILES_CANNOT_START, { name, program.complaint }), filesRefused = true;
			feedback.play(hui::audio::Cue::error);
			return;
		}
		feedback.play(hui::audio::Cue::select);
		if (!ps5.frameBudget) {
			proton_queue_restart(&program, proton_launch().cycle, "files");
			quit = true;
		}
	}

	// ---- The Steam memory warning (src/pw_steam_memory.h): before Steam installs or starts, a census that leaves less
	// than Steam's measured need under the swap point shows what to do first; CIRCLE goes back, TRIANGLE held 5 s goes on ----
	bool steamMemoryWarns(SteamFrom from, const char *what)
	{
		const bool signed_in = proton_steam_step(store) == PW_STEAM_STEP_SIGNED_IN;
		const PwSteamCensus census = proton_system_memory();
		if (!pw_steam_memory_warn(&census, signed_in)) {
			if (census.known)
				proton_say("steam memory: %u MiB in %u processes before %s; Steam needs about %u MiB more: it fits under %u",
					census.system_mib, census.processes, what, pw_steam_memory_need(signed_in), PW_STEAM_SWAP_POINT_MIB);
			else
				proton_say("steam memory: no census before %s: no warning", what);
			return false;
		}
		steamWarnFrom = from, steamWarnCensus = census, steamWarnSignedIn = signed_in;
		steamWarnNeed = pw_steam_memory_need(signed_in);
		steamWarnSeconds = 0.0f, steamWarnArmed = false;
		page = Page::steamWarning;
		feedback.play(hui::audio::Cue::error);
		proton_say("steam memory: warned before %s: %u MiB in %u processes (payloads %u MiB in %u); Steam needs about %u MiB "
			"more; the swap point is %u MiB", what, census.system_mib, census.processes, census.payload_mib, census.payload_count,
			steamWarnNeed, PW_STEAM_SWAP_POINT_MIB);
		return true;
	}

	void steamWarningInput(const hui::InputFrame &input, float dt)
	{
		if (input.is_pressed(hui::Action::back)) {
			feedback.play(hui::audio::Cue::back);
			proton_say("steam memory: the player went back");
			page = steamWarnFrom == SteamFrom::files ? Page::files : Page::library;
			return;
		}
		const bool held = input.is_held(hui::Action::north);
		if (!held) steamWarnArmed = true;   // a hold counts from a press made here
		const float was = steamWarnSeconds;
		steamWarnSeconds = pw_setup_hold(steamWarnSeconds, held && steamWarnArmed, dt);
		if (was > 0.0f && steamWarnSeconds == 0.0f) proton_say("steam memory: hold let go at %.1f s", was);
		if (steamWarnSeconds < PW_STEAM_WARNING_HOLD_SECONDS) return;
		feedback.play(hui::audio::Cue::saved);
		proton_say("steam memory: the player held TRIANGLE %.0f s and went on", PW_STEAM_WARNING_HOLD_SECONDS);
		page = steamWarnFrom == SteamFrom::files ? Page::files : Page::library;
		switch (steamWarnFrom) {
		case SteamFrom::tile: pressSteam(); break;
		case SteamFrom::setup: startSteamGuide(PW_STEAM_PAGE_INTRO, PW_STEAM_FAIL_NONE); break;
		case SteamFrom::files:
			steamWarnPassed = true;
			filesLaunch(steamWarnProgram.c_str());
			steamWarnPassed = false;
			break;
		}
	}

	// ---- Options: the focused game's own settings, its game.json env words (the WebUI's game editor writes the same) ----
	static std::string envWord(const ProtonGame &game, const std::string &name)
	{
		std::string value;
		for (const auto &word : game.env)
			if (word.rfind(name + "=", 0) == 0) value = word.substr(name.size() + 1);
		return value;
	}

	void openOptions(int index)
	{
		page = Page::options;
		optionsGame = index;
		optionsNote.clear();
		optionsSaveFailed = false;
		std::vector<hui::ui::ListItem> rows(3);
		rows[0].title = tr(PW_TEXT_OPTIONS_GAME_MODE);
		rows[1].title = tr(PW_TEXT_OPTIONS_CONTROLLER);
		rows[2].title = tr(PW_TEXT_OPTIONS_INSIDE);
		optionsList.set_items(rows);
		optionsList.set_focus(0);
		optionsList.enter();
		syncOptions();
		proton_say("options: %s", games[index].name.c_str());
	}

	void syncOptions()
	{
		const ProtonGame &game = games[optionsGame];
		const std::string mode = envWord(game, "WINE_PS5_GAME_MODE"), pad = envWord(game, "WINE_PS5_CONTROLLER");
		optionsList.item(0).badge = tr(mode == "1" ? PW_TEXT_OPTIONS_BADGE_ON : mode == "0" ? PW_TEXT_OPTIONS_BADGE_OFF : PW_TEXT_OPTIONS_BADGE_SETTINGS);
		optionsList.item(0).subtitle = tr(mode.empty() ? (gameMode ? PW_TEXT_OPTIONS_SETTINGS_ON : PW_TEXT_OPTIONS_SETTINGS_OFF) :
			mode == "1" ? PW_TEXT_OPTIONS_ON_HERE : PW_TEXT_OPTIONS_OFF_HERE);
		// A controller's name in the menu's words (the DualSense keeps its own)
		auto pad_name = [&](int kind) {
			return std::string(kind == PW_CONTROLLER_XBOX_360 ? tr(PW_TEXT_OPTIONS_PAD_360) : kind == PW_CONTROLLER_XBOX_ONE ?
				tr(PW_TEXT_OPTIONS_PAD_ONE) : pw_controller_name(kind));
		};
		const int kind = pw_controller_kind(pad.c_str());
		optionsList.item(1).badge = kind < 0 ? tr(PW_TEXT_OPTIONS_BADGE_SETTINGS) : "";
		optionsList.item(1).subtitle = kind < 0 ? home.fill(PW_TEXT_OPTIONS_SETTINGS_PAD, { pad_name(controller) }) : pad_name(kind);
		const bool inside = envWord(game, "WINE_PS5_TITLE_GAMES") != "0";
		optionsList.item(2).badge = tr(inside ? PW_TEXT_OPTIONS_BADGE_ON : PW_TEXT_OPTIONS_BADGE_OFF);
		optionsList.item(2).subtitle = tr(inside ? PW_TEXT_OPTIONS_INSIDE_ON : PW_TEXT_OPTIONS_INSIDE_OFF);
	}

	void optionsInput(const hui::InputFrame &input)
	{
		const auto event = optionsList.handle(input, feedback);
		if (event == hui::ui::Event::cancelled || input.is_pressed(hui::Action::back)) {
			page = Page::library;
			return;
		}
		if (event != hui::ui::Event::activated) return;
		ProtonGame &game = games[optionsGame];
		std::string name, value;
		if (optionsList.focus() == 0) {   // as Settings, then on, then off
			const std::string mode = envWord(game, "WINE_PS5_GAME_MODE");
			name = "WINE_PS5_GAME_MODE", value = mode.empty() ? "1" : mode == "1" ? "0" : "";
		} else if (optionsList.focus() == 1) {   // as Settings, then each controller
			const int kind = pw_controller_kind(envWord(game, "WINE_PS5_CONTROLLER").c_str());
			name = "WINE_PS5_CONTROLLER", value = kind + 1 < PW_CONTROLLER_KINDS ? pw_controller_word(kind + 1) : "";
		} else {
			name = "WINE_PS5_TITLE_GAMES", value = envWord(game, name) == "0" ? "" : "0";
		}
		// A scripted run previews (nothing written), as Settings' rows do
		const std::string problem = ps5.frameBudget ? std::string() : proton_game_set_env(game, name, value);
		if (!problem.empty()) {
			optionsNote = home.fill(PW_TEXT_OPTIONS_NOT_SAVED, { problem }), optionsSaveFailed = true;
			feedback.play(hui::audio::Cue::error);
			proton_say("options: %s not saved: %s", game.name.c_str(), problem.c_str());
			return;
		}
		game.env.erase(std::remove_if(game.env.begin(), game.env.end(), [&](const std::string &word) {
			return word.rfind(name + "=", 0) == 0; }), game.env.end());
		if (!value.empty()) game.env.push_back(name + "=" + value);
		for (auto &entry : allGames)
			if (entry.id == game.id) entry.env = game.env;
		optionsNote = home.fill(PW_TEXT_OPTIONS_APPLIES, { game.name }), optionsSaveFailed = false;
		feedback.play(hui::audio::Cue::saved);
		proton_say("options: %s %s %s=%s", game.name.c_str(), ps5.frameBudget ? "preview" : "saved", name.c_str(),
			value.empty() ? "(as Settings)" : value.c_str());
		syncOptions();
	}

	// What the WebUI changed (ps5/src/proton_webui.h): the settings and the library, read again as at the start.
	void reloadFromWebui()
	{
		pw_controls_load(&controls, proton_controls_path().c_str());
		pw_game_mode_load(&gameMode, proton_game_mode_path().c_str());
		pw_high_refresh_load(&highRefresh, proton_high_refresh_path().c_str());
		pw_detailed_logs_load(&detailedLogs, proton_logging_path().c_str());
		pw_controller_load(&controller, proton_controller_path().c_str());
		syncControls();
		syncGameMode();
		syncHighRefresh();
		syncLogging();
		syncController();
		// The theme and the music the page chose (title folder hui/theme.txt, hui/music.txt)
		std::string id;
		hui::save::read_file(proton_title_path(PS5_APP_ROOT "/hui/theme.txt"), &id, 64);
		while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
		const auto looks = hui::ui::themes();
		for (int i = 0; i < (int)looks.size(); ++i)
			if (id == looks[i].id && i != selectedTheme) {
				selectedTheme = i;
				ps5ui::set_active_theme(i);
				syncThemes();
			}
		if (music.remembered() != music.piece()) {
			music.play(music.remembered(), false);
			musicShown = 2.5f;
			syncMusic();
		}
		loadGames();
		syncLibraryRows(false);
		proton_say("webui: the launcher reloaded its settings and library");
	}

	// The library page's Web UI block, under the panel's words: its code and its address, where the eye finds it first.
	void drawWebuiBlock(hui::ui::Painter &paint, const hui::ui::Theme &look)
	{
		const std::string url = proton_webui_url();
		if (url.empty()) return;
		drawQr(url, 1240, 778, 164);
		paint.heading("Web UI", 1426, 806, 28, look.text);
		float y = 846;
		for (const auto &line : kit.fonts.regular.font->wrap(url, 21, 350.0f)) {
			paint.body(line, 1426, y, 21, look.text);
			y += 30;
		}
		for (const auto &line : kit.fonts.regular.font->wrap("Scan it with your phone to add games, back up saves and change settings.", 19, 350.0f)) {
			paint.body(line, 1426, y + 4, 19, look.text_muted);
			y += 27;
		}
	}

	// The entry's header art (the WebUI saves it: covers/<id>-header.img), uploaded again only when its file changes; 0: none.
	uint32_t coverTexture(const std::string &id)
	{
		const std::string path = proton_webui_data_root() + "/covers/" + id + "-header.img";
		struct stat status;
		const long long when = stat(path.c_str(), &status) == 0 ? (long long)status.st_mtime : 0;
		auto found = covers.find(id);
		if (found != covers.end() && found->second.first == when) return found->second.second;
		if (found != covers.end() && found->second.second) kit.renderer.destroy_texture(found->second.second);
		uint32_t texture = 0;
		std::string data;
		if (when && hui::save::read_file(path, &data, 4u << 20)) {
			int w = 0, h = 0, channels = 0;
			stbi_uc *rgba = stbi_load_from_memory((const stbi_uc *)data.data(), (int)data.size(), &w, &h, &channels, 4);
			if (rgba) {
				texture = kit.renderer.create_texture(w, h, rgba);
				stbi_image_free(rgba);
			}
		}
		covers[id] = { when, texture };
		return texture;
	}

	// A QR code of text, black modules on a white card size wide, at x, y; encoded once for each text (pw_qrcodegen).
	void drawQr(const std::string &text, float x, float y, float size)
	{
		if (text != qrText) {
			static uint8_t code[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
			qrText = text;
			qrSize = 0;
			qrModules.clear();
			if (qrcodegen_encodeText(text.c_str(), temp, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
					qrcodegen_Mask_AUTO, true)) {
				qrSize = qrcodegen_getSize(code);
				for (int row = 0; row < qrSize; ++row)
					for (int col = 0; col < qrSize; ++col) qrModules.push_back(qrcodegen_getModule(code, col, row) ? 1 : 0);
			}
		}
		if (!qrSize) return;
		scene.rounded_rect({ x, y, size, size }, size * 0.06f, hui::gfx::Color::rgb(0xffffff));
		const float quiet = size * 0.07f, module = (size - 2 * quiet) / qrSize;
		for (int row = 0; row < qrSize; ++row)
			for (int col = 0; col < qrSize; ++col)
				if (qrModules[size_t(row * qrSize + col)])
					scene.rounded_rect({ x + quiet + col * module, y + quiet + row * module, module + 0.5f, module + 0.5f }, 0,
						hui::gfx::Color::rgb(0x1b2430));
	}

	// The two Accounts rows' words, from what the files and the download thread say now (src/pw_steam.h's step words).
	void syncAccounts()
	{
		store = proton_steam_state();
		if (clientRow >= 0) {
			auto &row = library.item(clientRow);
			const PwSteamStep step = proton_steam_step(store);
			row.subtitle = pw_steam_step_subtitle(step);
			row.badge = pw_steam_step_badge(step);
		}
		if (aureliaList.items().size() < 2) return;
		auto &library_row = aureliaList.item(0);
		library_row.subtitle = aureliaAccount.empty() ? "Not signed in. Press CROSS, then scan the code with your phone"
			: "Signed in as " + aureliaAccount;
		library_row.badge = aureliaAccount.empty() ? "SIGN IN" : "SIGNED IN";
		const PwSteamStep step = proton_steam_step(store);
		auto &client_row = aureliaList.item(1);
		client_row.subtitle = pw_steam_step_subtitle(step);
		client_row.badge = pw_steam_step_badge(step);
	}

	// A session started from the Accounts rows comes back to the Aurelia page, on those rows (the startup hook reads this).
	void returnToAccounts()
	{
		if (!fromAccounts) return;   // pressed on the library's row: back to the library
		if (FILE *flag = fopen(PROTON_DATA_ROOT "/aurelia-open", "w")) {
			fputs("accounts\n", flag);
			fclose(flag);
		}
	}

	// CROSS on the Steam client row: until it is signed in, the Steam guide (src/pw_steam_guide.h) takes the user through
	// the download, the install and the sign-in; signed in, Steam opens in Big Picture.
	void pressSteam()
	{
		if (ps5.frameBudget && !clientTest) return;   // a scripted run asks nothing of the network
		syncAccounts();
		const PwSteamStep step = proton_steam_step(store);
		if (step != PW_STEAM_STEP_SIGNED_IN) {
			if (sessionBlocked()) return;   // the note says why
			clientNote.clear();
			returnToAccounts();
			// A test's press (the client-press flag) takes the guide past its first page: Start is pressed for it
			const bool busy = step == PW_STEAM_STEP_DOWNLOADING || step == PW_STEAM_STEP_INSTALLING;
			startSteamGuide(busy || clientTest ? PW_STEAM_PAGE_DOWNLOAD : PW_STEAM_PAGE_INTRO, PW_STEAM_FAIL_NONE);
			if (clientTest && !busy) proton_steam_download();
		} else if (sessionBlocked()) {
			// the note says why
		} else {
			ProtonGame client;
			std::string problem;
			if (proton_steam_game(&client, false, &problem, true)) {
				proton_say("launcher: %s in Big Picture", proton_steam_title());
				proton_queue_restart(&client, proton_launch().cycle, "steam");
				quit = true;
			} else {
				clientNote = std::string(proton_steam_title()) + " cannot start: " + problem;
			}
		}
		syncAccounts();
	}

	// A session cannot start while the runtime or /data is missing: say why on the page instead of restarting into a failure.
	bool sessionBlocked()
	{
		if (runtimeBlocked.empty()) return false;
		aureliaNote = clientNote = "Sessions cannot start: " + runtimeBlocked;
		return true;
	}

	// A press on an Accounts row: row 0 is the library account's QR sign-in, row 1 the store client's set-up.
	void accountAction(int row)
	{
		if (ps5.frameBudget) return;
		fromAccounts = true;
		if (row == 0) aureliaCommand("login --qr --json", "Aurelia sign-in");
		else pressSteam();
	}

	hui::ui::ListView &activeList()
	{
		switch (page) {
		case Page::aurelia: return aureliaList;
		case Page::themes: return themes;
		case Page::settings: return settingsList;
		case Page::controls: return controlsList;
		case Page::buttons: return buttonList;
		default: return library;
		}
	}

	void saveControls(const pw_controls &changed)
	{
		// Scripted UI runs preview choices without overwriting the player's file.
		const int error = ps5.frameBudget ? 0 : pw_controls_save(&changed, proton_controls_path().c_str());
		if (error) {
			controlsNotice = "Could not save controls. Your previous choices are unchanged.";
			proton_say("Controls: save failed (%d)", error);
		} else {
			controls = changed;
			controlsNotice = "Saved. Applies to every game you launch.";
			feedback.play(hui::audio::Cue::saved);
			proton_say("Controls: %s left=%s right=%s middle=%s keyboard=%s", ps5.frameBudget ? "preview" : "saved",
				pw_controls_button_id(controls.button[0]), pw_controls_button_id(controls.button[1]), pw_controls_button_id(controls.button[2]),
				pw_osk_trigger_id(controls.keyboard));
		}
		syncControls();
	}

	void update(float dt)
	{
		feedback.clear();
		const hui::InputFrame input = ps5.frameBudget ? kit.scripted("ps5proton", ps5.framesDrawn) : kit.input();
		clock += dt;
		applied = std::max(0.0f, applied - dt);
		if (const unsigned changes = proton_webui_changes(); changes != webuiSeen) {
			webuiSeen = changes;
			reloadFromWebui();
		}
		// A one-shot test hook: with /data/ps5proton/client-press present, CROSS is pressed on the
		// option on the first frame, so a run nobody sits at can take the option through its steps. It works under a
		// frame budget too (the budget then ends the launcher, and a restart the option queued is carried out).
		if (!clientTestTried) {
			clientTestTried = true;
			if (access(PROTON_DATA_ROOT "/client-press", F_OK) == 0) {
				clientTest = true;
				remove(PROTON_DATA_ROOT "/client-press");
				proton_say("launcher: the client-press flag pressed CROSS on the option");
				pressSteam();
			}
		}
		const bool clientBusy = store.phase == ProtonSteam::downloading || store.phase == ProtonSteam::downloaded;
		if ((page == Page::aurelia || clientBusy) && (clientPoll -= dt) <= 0.0f) {
			clientPoll = 0.5f;
			syncAccounts();
			// The download is done and checked: restart into the session that runs the installer, once.
			if (store.phase == ProtonSteam::downloaded && !store.installed && !clientInstallQueued && (!ps5.frameBudget || clientTest)) {
				clientInstallQueued = true;
				ProtonGame installer;
				std::string problem;
				if (proton_steam_game(&installer, true, &problem)) {
					proton_say("launcher: installing from the downloaded installer");
					returnToAccounts();
					proton_queue_restart(&installer, proton_launch().cycle, "install");
					quit = true;
				} else {
					clientNote = "The installer cannot run: " + problem;
					clientInstallQueued = false;
					proton_steam_fail("The installer cannot run: " + problem);   // the row offers CROSS to try again
				}
			}
		}
		// A downloaded update: R2 restarts into it now (the owner, 2026-10-31: "update should be seamless")
		if (page == Page::library && input.is_pressed(hui::Action::jump_next) && !ps5.frameBudget && proton_update_restartable()) {
			proton_update_restart_now();
		}
		if (page == Page::library && input.is_pressed(hui::Action::west)) {
			openThemes();
		} else if (page == Page::library) {
			int item = -1;
			const HomeGrid::Action action = home.update(input, dt, feedback, &item);
			homeAction(action, item);
		} else if (page == Page::files) {
			filesInput(input);
		} else if (page == Page::options) {
			optionsInput(input);
		} else if (page == Page::steamWarning) {
			steamWarningInput(input, dt);
		// Aurelia is parked (the owner, 2026-10-24): its page stays (the aurelia-open test hook still opens it), but R2 no
		// longer does and the hints no longer name it.
		} else if (page == Page::aurelia && input.is_pressed(hui::Action::west)) {
			aureliaCommand("login --qr --json", "Aurelia sign-in");
		} else if (page == Page::aurelia && input.is_pressed(hui::Action::north)) {
			aureliaCommand("list --json", "Aurelia refresh");

		} else if (page == Page::aurelia && aureliaAccounts) {
			// The Accounts rows. CIRCLE goes back to the shelf, or to the library when there is no shelf to go to.
			const auto event = aureliaList.handle(input, feedback);
			if (event == hui::ui::Event::cancelled || input.is_pressed(hui::Action::back)) {
				if (aureliaEntries.empty()) page = Page::library;
				else aureliaAccounts = false;
			} else if (event == hui::ui::Event::activated) {
				accountAction(aureliaList.focus());
			}
		} else if (page == Page::aurelia) {
			if (input.nav == hui::Direction::up) {
				aureliaAccounts = true;
			} else if (input.nav == hui::Direction::left) {
				aureliaFocus = std::max(0, aureliaFocus - 1);
			} else if (input.nav == hui::Direction::right) {
				aureliaFocus = std::min(int(aureliaEntries.size()) - 1, aureliaFocus + 1);
			} else if (input.is_pressed(hui::Action::back)) {
				page = Page::library;
			} else if (input.is_pressed(hui::Action::confirm)) {
				const AureliaEntry &entry = aureliaEntries[size_t(aureliaFocus)];
				if (ProtonGame *installed = aureliaInstalled(entry)) {
					if (!installed->complaint.empty()) {
						aureliaNote = installed->complaint;   // the library refuses such a game; so does the page
					} else {
						proton_say("launcher: Aurelia plays %s (%u)", entry.name.c_str(), entry.app_id);
						proton_queue_restart(installed, proton_launch().cycle, "aurelia play");
						quit = true;
					}
				} else {
					char command[96];
					snprintf(command, sizeof(command), "install %u --json", entry.app_id);
					aureliaCommand(command, "Aurelia install");
				}
			}
		} else {
			const auto event = activeList().handle(input, feedback);
			if (event == hui::ui::Event::cancelled || input.is_pressed(hui::Action::back)) {
				if (page == Page::buttons) page = Page::controls;
				else if (page == Page::controls) page = Page::settings;
				else page = Page::library;
			} else if (event == hui::ui::Event::activated) {
				switch (page) {
				case Page::themes:
					selectedTheme = themes.focus();
					ps5ui::set_active_theme(selectedTheme);
					syncThemes();
					applied = 2.5f;
					feedback.play(hui::audio::Cue::saved);
					proton_say("launcher theme: %s", theme().id);
					break;
				case Page::settings:
					if (settingsList.focus() == 5) {
						nextMusic();
					} else if (settingsList.focus() == 4) {
						// A re-run goes to its end like the first run (the owner, 2026-10-31): not done until Done shows
						// again, so a title closed on the way starts the tour again
						PwSetupRecord record;
						pw_setup_record_load(&record, SetupTour::recordPath);
						record.done = 0;
						if (const int error = pw_setup_record_save(&record, SetupTour::recordPath))
							proton_say("setup: record not saved (%d)", error);
						setupFacts = SetupTour::facts(dataAvailable, true, &setupSimulated);
						startSetup(PW_SETUP_WELCOME);
					} else if (settingsList.focus() == 1) {
						toggleGameMode();
					} else if (settingsList.focus() == 10) {
						toggleHighRefresh();
					} else if (settingsList.focus() == 11) {
						if (!ps5.frameBudget) proton_update_set_auto(!proton_update_auto_enabled());
						syncUpdates();
					} else if (settingsList.focus() == 7) {
						toggleLogging();
					} else if (settingsList.focus() == 8) {
						nextController();
					} else if (settingsList.focus() == 3) {
						// Look again each time (the folder may have been copied again), then start when the pieces are all there.
						if (!ps5.frameBudget && proton_install_find().phase == ProtonInstall::ready) proton_install_start();
						syncInstall();
					} else if (settingsList.focus() == 6) {
						// One press a step: look, then download what was found, then restart into it.
						const ProtonUpdate::Phase now = proton_update_state().phase;
						if (ps5.frameBudget || now == ProtonUpdate::checking || now == ProtonUpdate::downloading) {
							// a check or a download is running; a scripted run asks nothing of the network
						} else if (now == ProtonUpdate::available) {
							proton_update_download();
						} else if (now == ProtonUpdate::ready) {
							proton_update_restart();
						} else {
							proton_update_check();
						}
						syncUpdates();
					} else if (settingsList.focus() == 0) {
						page = Page::controls;
						controlsList.enter();
					}  // About (2) is read where it is
					break;
				case Page::controls:
					if (controlsList.focus() == 4) {
						pw_controls defaults;
						pw_controls_defaults(&defaults);
						saveControls(defaults);
					} else if (controlsList.focus() == 3) {
						// the on-screen keyboard's gesture: each press moves to the next, and Off follows the last
						pw_controls changed = controls;
						pw_controls_set_keyboard(&changed, (controls.keyboard + 1) % PW_OSK_TRIGGERS);
						saveControls(changed);
					} else {
						mouseAction = controlsList.focus();
						page = Page::buttons;
						buttonList.set_focus(controls.button[mouseAction]);
						buttonList.enter();
					}
					break;
				case Page::buttons: {
					pw_controls changed = controls;
					pw_controls_assign(&changed, mouseAction, buttonList.focus());
					saveControls(changed);
					page = Page::controls;
					break;
				}
				default:   // the main menu, Files, Options and the Aurelia page have their own branches above
					break;
				}
			}
		}
		leaving.label = tr(PW_TEXT_SET_CLOSE);
		if (!ps5.frameBudget && leaving.update(dt, (ps5_pad().held & PAD_OPTIONS) != 0)) quit = true;
		if (page == Page::aurelia) {
			static int marked = -1;
			if (marked != aureliaFocus) {
				marked = aureliaFocus;
				aureliaMark("focus", aureliaNote.c_str());
			}
		}
		for (auto *list : {&library, &aureliaList, &themes, &settingsList, &controlsList, &buttonList, &filesList, &optionsList}) {
			list->style.theme = list == &filesList || list == &optionsList ? homeLook() : theme();
			list->update(dt);
		}
		musicShown = std::max(0.0f, musicShown - dt);
		music.pump(dt);
		if (page == Page::settings) {
			syncInstall();
			syncUpdates();
		}
		// The launcher has drawn: this title started once, so a previous title an update replaced can go.
		if (!drawnOnce && ps5.framesDrawn > 0) {
			drawnOnce = true;
			proton_update_started();
			// The automatic update check: only the launcher asks, never a game; a scripted run asks nothing of the network
			if (dataAvailable && !ps5.frameBudget) proton_update_auto();
		}
		if (!ps5.frameBudget && (updateNoticePoll -= dt) <= 0.0f) {
			updateNoticePoll = 1.0f;
			updateNotice = proton_update_notice();
			updateRestart = proton_update_restartable();
		}
		kit.play(feedback, theme().sounds);
		kit.tick(dt);
	}

	// What plays, for a moment after it changes: a note and the piece's name, top right
	void drawMusic(const hui::ui::Theme &look)
	{
		if (musicShown <= 0.0f) return;
		const float shown = std::min(1.0f, std::min(musicShown / 0.3f, (2.5f - musicShown) / 0.2f + 0.0001f));
		const std::string label = music.title() ? home.fill(PW_TEXT_SET_MUSIC_NOW, { music.title() }) : std::string(tr(PW_TEXT_SET_MUSIC_OFF));
		const hui::ui::FontRef &face = homeLanguageReady ? home.face(label, true) : kit.fonts.semibold;
		const float w = face.measure(label, 24) + 92;
		const hui::gfx::Rect plate{ 1800 - w, 40 - (1.0f - shown) * 16.0f, w, 60 };
		scene.push_opacity(shown);
		scene.shadow(plate, 30, 22, look.shadow.a > 0 ? look.shadow.with_alpha(0.45f) : hui::gfx::Color::rgb(0x000000, 0.35f));
		scene.rounded_rect(plate, 30, look.surface.with_alpha(0.97f));
		const float x = plate.x + 34, cy = plate.y + 30;   // an eighth note: a head, a stem, a flag
		const auto ink = music.title() ? look.accent : look.text_muted;
		scene.circle(x - 4, cy + 8, 7, ink);
		scene.line(x + 2.5f, cy + 8, x + 2.5f, cy - 13, 3, ink);
		scene.line(x + 2.5f, cy - 13, x + 11, cy - 6, 3, ink);
		hui::ui::text(scene, face, label, plate.x + 62, cy + 8, 24, look.text);
		scene.pop_opacity();
	}

	void paragraph(hui::ui::Painter &paint, const std::string &text, float x, float &y,
		float size, hui::gfx::Color ink)
	{
		for (const auto &line : SetupTour::lines(homeFonts.regular, text, size, 500.0f)) {
			paint.body(line, x, y, size, ink);
			y += size * 1.45f;
		}
	}

	// The shelf: one row of large tiles, the focused one lifted and ringed, the row scrolling to keep it in view. The
	// theme's surface, radius, accent and ink style it, so all 30 looks carry over.
	void drawAureliaShelf()
	{
		const auto &look = theme();
		const float tile_w = 250, tile_h = 330, gap = 26, left = 120, top = 300, text_w = tile_w - 32;
		const int visible = 5;
		const int count = int(aureliaEntries.size());
		if (!count) return;
		const float target = std::max(0, aureliaFocus - 2);
		aureliaScroll += (target - aureliaScroll) * 0.25f;
		for (int i = 0; i < count; ++i) {
			const float offset = float(i) - aureliaScroll;
			if (offset < -0.7f || offset > visible - 0.3f) continue;
			const bool focused = i == aureliaFocus;
			const float x = left + offset * (tile_w + gap), lift = focused ? 10.0f : 0.0f;
			const float w = tile_w + (focused ? 18.0f : 0.0f), h = tile_h + (focused ? 18.0f : 0.0f);
			const hui::gfx::Rect tile{ x - (focused ? 9.0f : 0.0f), top - lift, w, h };
			if (focused) scene.rounded_rect(tile, look.radius_card, look.accent);
			scene.rounded_rect({ tile.x + (focused ? 2.0f : 0.0f), tile.y + (focused ? 2.0f : 0.0f), w - (focused ? 4.0f : 0.0f),
				h - (focused ? 4.0f : 0.0f) }, look.radius_card, focused ? look.surface_high : look.surface);
			const auto &entry = aureliaEntries[size_t(i)];
			const auto name = kit.fonts.semibold.font->wrap(entry.name, 26, text_w);
			float y = tile.y + 54;
			for (size_t line = 0; line < name.size() && line < 3; ++line, y += 34)
				hui::ui::text(scene, kit.fonts.semibold, name[line], x + 16, y, 26, look.text);
			hui::ui::text(scene, kit.fonts.regular, std::to_string(entry.minutes) + " min", x + 16, tile.y + h - 26, 20,
				look.text_muted);
			if (entry.update)
				hui::ui::text(scene, kit.fonts.regular, "UPDATE", x + 16, tile.y + h - 56, 18, look.accent);
			if (aureliaInstalled(entry))
				hui::ui::text(scene, kit.fonts.regular, "INSTALLED", x + 16, tile.y + h - 86, 18, look.success);
		}
	}

	// The main menu and its pages draw on the setup's sky, in the gloss look the setup uses
	bool onSky() const
	{
		return page == Page::library || page == Page::files || page == Page::options || page == Page::steamWarning;
	}
	static const hui::ui::Theme &homeLook()
	{
		static const int gloss = [] {
			const auto all = hui::ui::themes();
			for (size_t i = 0; i < all.size(); ++i) if (!strcmp(all[i].id, "gloss")) return (int)i;
			return 0;
		}();
		return hui::ui::themes()[gloss];
	}

	// What the menu says across its top: why the focused game cannot start, Steam's last problem, a newer release, the
	// payloads' memory; first that applies
	void homeBanner(std::string *text, hui::gfx::Color *color)
	{
		const auto &look = homeLook();
		const int game = home.focusedGame();
		const int row = game >= 0 ? homeRows[game] : -1;
		if (row >= 0 && !games[library.items()[row].tag].complaint.empty()) {
			*text = games[library.items()[row].tag].complaint, *color = look.danger;
		} else if (payloadsSkipped) {
			*text = tr(PW_TEXT_PAYLOAD_SKIPPED), *color = look.warning;
		} else if (!clientNote.empty()) {
			*text = clientNote, *color = look.danger;
		} else if (!runtimeBlocked.empty()) {
			*text = runtimeBlocked, *color = look.danger;
		} else if (!updateNotice.empty()) {   // a newer release (the owner, 2026-10-31): never blocking, never in a game
			*text = updateRestart ? updateNotice + " Press R2 to restart now." : updateNotice, *color = look.primary;
		} else if (!payloadsNote.empty()) {
			*text = payloadsNote, *color = hui::gfx::Color::rgb(0x2c5272, 0.85f);
		}
	}

	void composeHome()
	{
		const auto &look = homeLook();
		if (!homeLanguageReady) useHomeLanguage();
		scene.clear();
		hui::ui::Canvas canvas{ scene, homeFonts, 0, clock };
		const auto ink = HomeGrid::ink(), quiet = HomeGrid::quiet();
		if (page == Page::library) {
			std::vector<HomeGrid::Game> tiles;
			for (int row : homeRows) tiles.push_back(homeGame(row));
			std::string banner;
			hui::gfx::Color color{};
			homeBanner(&banner, &color);
			home.compose(scene, look, tiles, homeGame(homeMihawk), homeSteam(), proton_webui_url(),
				[&](const std::string &text, float x, float y, float size) { drawQr(text, x, y, size); }, banner, color,
				updateRestart);
		} else if (page == Page::files) {
			char where[4096];
			if (pw_drive_windows_path(filesAt.c_str(), "", where, sizeof(where))) snprintf(where, sizeof(where), "C:\\");
			hui::ui::text(scene, homeFonts.semibold, tr(PW_TEXT_HOME_FILES), 120, 145, 64, ink);
			hui::ui::text(scene, kit.fonts.mono, where, 122, 195, 24, quiet);
			home.panel(scene, look, { 120, 230, 1020, 740 });
			if (!filesEntries.empty()) filesList.draw(canvas);
			home.panel(scene, look, { 1200, 230, 600, 740 });
			float y = 300;
			if (!filesEntries.empty()) {
				const PwDriveEntry &entry = filesEntries[filesList.focus()];
				y = home.paragraph(scene, home.face(entry.name, false), entry.name, 1240, y, 34, 520, ink) + 6;
				char path[4096];
				if (!pw_drive_windows_path(filesAt.c_str(), entry.name, path, sizeof(path)))
					y = home.paragraph(scene, home.face(path, false), path, 1240, y, 20, 520, quiet) + 12;
				y = home.paragraph(scene, homeFonts.regular, tr(entry.dir ? PW_TEXT_FILES_FOLDER_BODY : PW_TEXT_FILES_PROGRAM_BODY),
					1240, y, 22, 520, quiet) + 12;
			}
			if (!filesNote.empty())
				home.paragraph(scene, home.face(filesNote, false), filesNote, 1240, y, 22, 520, filesRefused ? look.danger : ink);
			home.paragraph(scene, homeFonts.regular, tr(PW_TEXT_FILES_READ_ONLY), 1240, 900, 19, 520, quiet);
			home.drawHints(scene, { { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_OPEN) },
				{ hui::ui::Button::circle, tr(filesAt.empty() ? PW_TEXT_HINT_BACK : PW_TEXT_HINT_UP) } });
		} else if (page == Page::steamWarning) {
			// What uses the memory, what Steam needs, what the PS5 allows; then what to do, in order
			const hui::gfx::Rect card{ 240, 140, 1440, 800 };
			home.panel(scene, look, card);
			scene.circle(card.x + 86, card.y + 96, 34, look.warning);
			hui::ui::text(scene, homeFonts.semibold, "!", card.x + 86, card.y + 110, 40, hui::gfx::Color::rgb(0xffffff), hui::gfx::Align::center);
			HomeGrid::fitted(scene, homeFonts.semibold, tr(PW_TEXT_STEAM_MEM_TITLE), card.x + 140, card.y + 112, 46, card.w - 200, ink);
			float y = home.paragraph(scene, homeFonts.regular, home.fill(PW_TEXT_STEAM_MEM_LINE, { std::to_string(steamWarnCensus.system_mib),
				std::to_string(steamWarnNeed), std::to_string(PW_STEAM_SWAP_POINT_MIB) }), card.x + 70, card.y + 190, 27, card.w - 140, quiet) + 16;
			y = home.paragraph(scene, homeFonts.semibold, tr(PW_TEXT_STEAM_MEM_STEPS), card.x + 70, y, 27, card.w - 140, ink) + 10;
			std::vector<PwSetupText> steps = { PW_TEXT_STEAM_MEM_STEP_CLOSE, PW_TEXT_STEAM_MEM_STEP_RESTART,
				steamWarnSignedIn ? PW_TEXT_STEAM_MEM_STEP_OPEN : PW_TEXT_STEAM_MEM_STEP_INSTALL };
			if (proton_payloads().others) steps.insert(steps.begin() + 1, PW_TEXT_STEAM_MEM_STEP_PAYLOADS);   // payloads it counts as extra
			for (size_t i = 0; i < steps.size(); ++i) {
				scene.circle(card.x + 96, y - 9, 20, look.primary);
				hui::ui::text(scene, homeFonts.semibold, std::to_string(i + 1), card.x + 96, y, 24, look.on_primary, hui::gfx::Align::center);
				y = home.paragraph(scene, homeFonts.regular, tr(steps[i]), card.x + 136, y, 26, card.w - 210, ink) + 14;
			}
			SetupTour::holdRing(scene, homeFonts, look, card.x + card.w - 70 - 46, card.y + card.h - 70 - 46,
				steamWarnSeconds / PW_STEAM_WARNING_HOLD_SECONDS, hui::ui::Button::triangle, tr(PW_TEXT_PAYLOAD_SKIP_HOLD));
			home.drawHints(scene, { { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } });
		} else {
			const ProtonGame &game = games[optionsGame];
			HomeGrid::fitted(scene, home.face(game.name, true), game.name, 120, 145, 64, 1680, ink);
			hui::ui::text(scene, homeFonts.regular, tr(PW_TEXT_OPTIONS_TITLE), 122, 195, 24, quiet);
			home.panel(scene, look, { 120, 230, 1020, 740 });
			optionsList.draw(canvas);
			home.panel(scene, look, { 1200, 230, 600, 740 });
			const PwSetupText heads[] = { PW_TEXT_OPTIONS_GAME_MODE, PW_TEXT_OPTIONS_CONTROLLER, PW_TEXT_OPTIONS_INSIDE };
			const PwSetupText words[][2] = { { PW_TEXT_OPTIONS_GAME_MODE_BODY, PW_TEXT_OPTIONS_GAME_MODE_CYCLE },
				{ PW_TEXT_OPTIONS_CONTROLLER_BODY, PW_TEXT_OPTIONS_CONTROLLER_CYCLE },
				{ PW_TEXT_OPTIONS_INSIDE_BODY, PW_TEXT_OPTIONS_INSIDE_OFF_BODY } };
			const int focus = optionsList.focus();
			float y = home.paragraph(scene, homeFonts.semibold, tr(heads[focus]), 1240, 300, 38, 520, ink) + 16;
			y = home.paragraph(scene, homeFonts.regular, tr(words[focus][0]), 1240, y, 24, 520, quiet) + 12;
			y = home.paragraph(scene, homeFonts.regular, tr(words[focus][1]), 1240, y, 24, 520, quiet) + 12;
			if (!optionsNote.empty())
				home.paragraph(scene, home.face(optionsNote, false), optionsNote, 1240, y, 22, 520, optionsSaveFailed ? look.danger : ink);
			home.drawHints(scene, { { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_CHANGE) },
				{ hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } });
		}
		drawMusic(theme());
		leaving.draw(scene, homeFonts, look);
		kit.renderer.begin();
		if (!homeSky.ready()) {   // no sky pipeline: the gloss look's own backdrop
			auto backdrop = look.backdrop;
			backdrop.time = clock;
			kit.renderer.backdrop(backdrop);
		}
		kit.renderer.draw(scene);
	}

	void compose()
	{
		if (onSky()) {
			composeHome();
			return;
		}
		if (!homeLanguageReady) useHomeLanguage();
		const auto &look = theme();
		if (settingsList.items().size() > 9) {
			const std::string url = proton_webui_url();
			settingsList.item(9).subtitle = url.empty() ? tr(PW_TEXT_SET_WEBUI_WAITING) : url;
		}
		const auto glass = kit.renderer.glass_texture();
		scene.clear();
		hui::ui::Canvas canvas{ scene, homeFonts, glass, clock };
		hui::ui::Painter paint(scene, homeFonts, look, glass);
		const bool picking = page == Page::themes;
		const bool configuring = page != Page::library && !picking && page != Page::aurelia;
		const PwSetupText titles[] = { PW_TEXT_SET_CLICK_LEFT, PW_TEXT_SET_CLICK_RIGHT, PW_TEXT_SET_CLICK_MIDDLE };
		const char *heading = page == Page::aurelia ? "Aurelia" : picking ? tr(PW_TEXT_HINT_THEMES) : page == Page::settings ?
			tr(PW_TEXT_HOME_SETTINGS) : page == Page::controls ? tr(PW_TEXT_SET_CONTROLS) : page == Page::buttons ? tr(titles[mouseAction]) :
			PS5_TITLE_NAME;
		paint.heading(heading, 120, 145, 64, paint.page_text());
		const std::string subtitle = page == Page::aurelia
			? (aureliaAccounts ? std::string("Accounts: your library, and the Steam client your games run on")
				: aureliaAccount.empty() ? std::string("Steam, without a browser: sign in with the app on your phone")
				: std::string("Signed in as ") + aureliaAccount + " \u00b7 your library")
			: picking ? std::string(tr(PW_TEXT_SET_THEMES_SUBTITLE))
			: page == Page::settings ? std::string(tr(PW_TEXT_SET_SUBTITLE))
			: configuring ? std::string(tr(PW_TEXT_SET_CONTROLS_SUBTITLE))
			: std::string("Your Windows game library");
		paint.body(subtitle, 122, 195, 24, paint.page_text_muted());
		char count[64];
		if (page == Page::aurelia && aureliaAccounts) snprintf(count, sizeof(count), "Accounts");
		else if (page == Page::aurelia) snprintf(count, sizeof(count), "%zu game%s", aureliaEntries.size(), aureliaEntries.size() == 1 ? "" : "s");
		else if (picking) snprintf(count, sizeof(count), "%02d / %02zu", themes.focus() + 1, themes.items().size());
		else if (configuring) snprintf(count, sizeof(count), "%s", tr(PW_TEXT_SET_CRUMB));
		else if (!dataAvailable) snprintf(count, sizeof(count), "Library unavailable");
		else snprintf(count, sizeof(count), "%zu game%s", games.size(), games.size() == 1 ? "" : "s");
		paint.label(count, 1800, 145, 24, paint.page_text_muted(), hui::gfx::Align::right);
		if (page == Page::aurelia && !aureliaAccounts) drawAureliaShelf();
		else activeList().draw(canvas);

		const hui::gfx::Rect panel{ 1200, 230, 600, 740 };
		paint.panel(panel);
		float y = 300;
		if (picking) {
			paint.heading(themeWords(look, 0), 1240, y, 42, look.text);
			y += 48;
			paragraph(paint, themeWords(look, 1), 1240, y, 24, look.text_muted);
			y += 35;
			paragraph(paint, themeWords(look, 2), 1240, y, 24, look.text_muted);
			paint.label(tr(PW_TEXT_SET_PALETTE), 1240, 535, 20, look.text_muted);
			const char *labels[] = { tr(PW_TEXT_SET_SWATCH_BACKGROUND), tr(PW_TEXT_SET_SWATCH_PANELS), tr(PW_TEXT_SET_SWATCH_TEXT),
				tr(PW_TEXT_SET_SWATCH_ACCENT) };
			const hui::gfx::Color colors[] = { look.page, look.surface, look.text, look.accent };
			for (int i = 0; i < 4; ++i) {
				const float x = 1240.0f + i * 130.0f;
				scene.rounded_rect({ x, 560, 110, 90 }, 8, look.outline);
				scene.rounded_rect({ x + 1, 561, 108, 88 }, 7, colors[i]);
				paint.body(labels[i], x, 684, 19, look.text_muted);
			}
			paint.body(tr(selectedTheme == themes.focus() ? PW_TEXT_SET_THEME_CURRENT : PW_TEXT_SET_THEME_PREVIEW), 1240, 805, 26, look.text);
			if (applied > 0) paint.body(tr(PW_TEXT_SET_THEME_APPLIED), 1240, 855, 24, look.success);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_BROWSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_USE_THEME) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 1) {
			paint.heading(tr(PW_TEXT_OPTIONS_GAME_MODE), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_GAME_MODE_BODY_ON), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, tr(PW_TEXT_SET_GAME_MODE_BODY_OFF), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, controlsNotice, 1240, y, 24, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(gameMode ? PW_TEXT_HINT_TURN_OFF : PW_TEXT_HINT_TURN_ON) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 11) {
			const bool automatic = proton_update_auto_enabled();
			paint.heading(tr(PW_TEXT_SET_AUTO), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_AUTO_BODY), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, tr(PW_TEXT_SET_AUTO_PRIVACY), 1240, y, 24, look.text_muted);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(automatic ? PW_TEXT_HINT_TURN_OFF : PW_TEXT_HINT_TURN_ON) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 10) {
			paint.heading(tr(PW_TEXT_SET_HZ), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_HZ_BODY_ON), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, tr(PW_TEXT_SET_HZ_BODY_OFF), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, controlsNotice, 1240, y, 24, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(highRefresh ? PW_TEXT_HINT_TURN_OFF : PW_TEXT_HINT_TURN_ON) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 2) {
			// The Appropriate Legal Notices GPLv3 asks an interactive program to show (LICENSE, section 0); wording approved
			// by the owner on 2026-10-18.
			// (in each language since 2026-10-31: the same notices, translated in src/pw_setup.c)
			paint.heading(tr(PW_TEXT_SET_ABOUT), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_ABOUT_COPYRIGHT), 1240, y, 22, look.text_muted);
			y += 22;
			paragraph(paint, tr(PW_TEXT_SET_ABOUT_LICENCE), 1240, y, 22, look.text_muted);
			y += 22;
			paragraph(paint, home.fill(PW_TEXT_SET_ABOUT_TEXTS, { "/data/homebrew/PPSA99170/licenses",
				PROTON_DATA_ROOT "/runtime/ge11-7/share/licenses" }), 1240, y, 22, look.text_muted);
			y += 22;
			paragraph(paint, home.fill(PW_TEXT_SET_ABOUT_SOURCE, { "github.com/mihawk-99/PS5_MihawkOS" }), 1240, y, 22, look.text_muted);
			y += 22;
			paragraph(paint, tr(PW_TEXT_SET_ABOUT_AFFILIATION), 1240, y, 22, look.text_muted);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 3) {
			paint.heading(tr(PW_TEXT_SET_RUNTIME_TITLE), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, home.fill(PW_TEXT_SET_RUNTIME_BODY, { PROTON_DATA_ROOT "/install" }), 1240, y, 24, look.text_muted);
			y += 30;
			const auto &done = install.progress;
			if (install.phase == ProtonInstall::checking || install.phase == ProtonInstall::running) {
				const float share = done.to_write ? std::min(1.0f, (float)done.written / (float)done.to_write) : 0.0f;
				scene.rounded_rect({ 1240, y, 520, 18 }, 9, look.outline);
				scene.rounded_rect({ 1240, y, std::max(18.0f, 520 * share), 18 }, 9, look.accent);
				y += 50;
				char rate[32];
				snprintf(rate, sizeof(rate), "%.1f", install.seconds > 0 ? done.written / 1e6 / install.seconds : 0.0);
				std::string speed = rate;
				speed.replace(speed.find('.'), 1, tr(PW_TEXT_DECIMAL));
				const std::string line = home.fill(PW_TEXT_SET_RUNTIME_PROGRESS, { std::to_string((long long)(done.written / 1e6)),
					std::to_string((long long)(done.to_write / 1e6)), speed, std::to_string(done.chunks_done),
					std::to_string(done.chunks_needed) });
				paragraph(paint, install.phase == ProtonInstall::checking ? std::string(tr(PW_TEXT_SET_RUNTIME_CHECKING)) : line,
					1240, y, 24, look.text);
				y += 30;
				paragraph(paint, tr(PW_TEXT_SET_RUNTIME_KEEP), 1240, y, 22, look.text_muted);
			} else {
				paragraph(paint, install.message.empty() ? std::string(tr(PW_TEXT_SET_RUNTIME_PRESS)) : install.message,
					1240, y, 24, install.phase == ProtonInstall::failed ? look.danger :
					install.phase == ProtonInstall::done ? look.success : look.text);
			}
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_INSTALL) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 5) {
			paint.heading(tr(PW_TEXT_SET_MUSIC), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_MUSIC_BODY), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, music.title() ? home.fill(PW_TEXT_SET_MUSIC_PLAYING, { music.title() }) : std::string(tr(PW_TEXT_SET_OFF)),
				1240, y, 24, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_NEXT) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 4) {
			paint.heading(tr(PW_TEXT_SET_SETUP), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_SETUP_BODY), 1240, y, 24, look.text_muted);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HOME_START) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 6) {
			paint.heading(tr(PW_TEXT_SET_UPDATES_TITLE), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_UPDATES_BODY), 1240, y, 24, look.text_muted);
			y += 30;
			const auto &done = updates.progress;
			if (updates.phase == ProtonUpdate::downloading) {
				const float share = done.to_write ? std::min(1.0f, (float)done.written / (float)done.to_write) : 0.0f;
				scene.rounded_rect({ 1240, y, 520, 18 }, 9, look.outline);
				scene.rounded_rect({ 1240, y, std::max(18.0f, 520 * share), 18 }, 9, look.accent);
				y += 50;
				paragraph(paint, home.fill(PW_TEXT_SET_UPDATES_PROGRESS, { std::to_string((long long)(done.written / 1e6)),
					std::to_string((long long)(done.to_write / 1e6)) }), 1240, y, 24, look.text);
			} else {
				paragraph(paint, updates.message.empty() ? std::string(tr(PW_TEXT_SET_UPDATES_PRESS)) : updates.message, 1240,
					y, 24, updates.phase == ProtonUpdate::failed ? look.danger :
					updates.phase == ProtonUpdate::ready ? look.success : look.text);
			}
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) },
				  { hui::ui::Button::cross, tr(updates.phase == ProtonUpdate::available ? PW_TEXT_HINT_DOWNLOAD :
					updates.phase == ProtonUpdate::ready ? PW_TEXT_HINT_RESTART_UPDATE : PW_TEXT_HINT_CHECK) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 7) {
			paint.heading(tr(PW_TEXT_SET_LOGGING), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, home.fill(PW_TEXT_SET_LOG_BODY, { PW_DETAILED_LOGS_ROOT }), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, tr(PW_TEXT_SET_LOG_MORE), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, controlsNotice, 1240, y, 24, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(detailedLogs ? PW_TEXT_HINT_TURN_OFF : PW_TEXT_HINT_TURN_ON) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 9) {
			const std::string url = proton_webui_url();
			paint.heading(tr(PW_TEXT_SET_WEBUI), 1240, y, 38, look.text);
			y += 50;
			if (url.empty()) {
				paragraph(paint, tr(PW_TEXT_SET_WEBUI_CONNECT), 1240, y, 24, look.text_muted);
			} else {
				drawQr(url, 1240, y, 250);
				y += 296;
				hui::ui::text(scene, kit.fonts.semibold, url, 1240, y, 30, look.text);
				y += 44;
				paragraph(paint, tr(PW_TEXT_SET_WEBUI_BODY), 1240, y, 22, look.text_muted);
			}
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::settings && settingsList.focus() == 8) {
			paint.heading(tr(PW_TEXT_OPTIONS_CONTROLLER), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(PW_TEXT_SET_PAD_BODY), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, tr(PW_TEXT_SET_PAD_XBOX), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, tr(PW_TEXT_SET_PAD_DUALSENSE), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, controlsNotice, 1240, y, 24, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_CHANGE) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (configuring) {
			const bool keyboardRow = page == Page::controls && controlsList.focus() == 3;
			paint.heading(tr(keyboardRow ? PW_TEXT_SET_KEYBOARD : PW_TEXT_SET_MOUSE_TITLE), 1240, y, 38, look.text);
			y += 65;
			paragraph(paint, tr(keyboardRow ? PW_TEXT_SET_KEYBOARD_BODY : PW_TEXT_SET_MOUSE_BODY), 1240, y, 24, look.text_muted);
			y += 30;
			if (!keyboardRow) {
				paragraph(paint, tr(PW_TEXT_SET_MOUSE_ASSIGNED), 1240, y, 24, look.text_muted);
				y += 30;
			}
			paragraph(paint, tr(PW_TEXT_SET_MOUSE_SAVED), 1240, y, 24, look.text_muted);
			y += 30;
			paragraph(paint, controlsNotice, 1240, y, 24, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_SELECT) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::aurelia && aureliaAccounts) {
			scene.push_clip({ 1240, 250, 520, 700 });
			if (aureliaList.focus() == 0) {
				paint.heading("Library account", 1240, y, 34, look.text);
				y += 55;
				paragraph(paint, "What lists your games and installs them: no browser, so it costs little memory. Sign in with the "
					"Steam app on your phone: scan the code on the screen.", 1240, y, 22, look.text_muted);
				y += 150;
				paragraph(paint, aureliaAccount.empty() ? "Not signed in." : "Signed in as " + aureliaAccount + ".", 1240, y, 22, look.text);
			} else {
				paint.heading("Steam client", 1240, y, 34, look.text);
				y += 45;
				// What went wrong first: below the long paragraphs it fell outside the panel.
				if (!clientNote.empty()) paragraph(paint, clientNote, 1240, y, 22, look.danger);
				else if (store.phase == ProtonSteam::failed) paragraph(paint, store.message, 1240, y, 22, look.danger);
				for (const std::string &line : proton_steam_panel(store)) {
					paragraph(paint, line, 1240, y, 22, look.text_muted);
					y += 14;
				}
			}
			scene.pop_clip();
			float noteY = 880;
			if (!aureliaNote.empty() && aureliaList.focus() == 0) paragraph(paint, aureliaNote, 1240, noteY, 22, look.text);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_SELECT) },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else if (page == Page::aurelia) {
			if (!aureliaEntries.empty()) {
				const auto &entry = aureliaEntries[size_t(aureliaFocus)];
				paint.heading(entry.name, 1240, y, 34, look.text);
				y += 55;
				char line[128];
				snprintf(line, sizeof(line), "App %u  \u00b7  %u min played%s", entry.app_id, entry.minutes,
					entry.update ? "  \u00b7  update" : "");
				paragraph(paint, line, 1240, y, 24, look.text_muted);
				y += 32;
				const bool installed = aureliaInstalled(entry) != nullptr;
				paragraph(paint, installed ? "Installed: CROSS plays it." : "Not installed yet.", 1240, y, 24,
					installed ? look.success : look.text_muted);
				y += 32;
			}
			if (!aureliaNote.empty()) paragraph(paint, aureliaNote, 1240, y, 22, look.text);
			y += 60;
			paragraph(paint, "Press UP for your accounts: the library account and the Steam client.", 1240, y, 22, look.text_muted);
			ps5ui::draw_hints(scene, homeFonts, look,
				{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_BROWSE) }, { hui::ui::Button::cross, "Play" },
				  { hui::ui::Button::square, "Sign in" }, { hui::ui::Button::triangle, "Refresh" },
				  { hui::ui::Button::circle, tr(PW_TEXT_HINT_BACK) } }, 120, 1040);
		} else {
			scene.push_clip({ 1240, 250, 520, 500 });
			// The library always holds Mihawk OS (proton_read_games), so a reason a game cannot start is its complaint.
			const auto &item = library.items()[library.focus()];
			// The entry's art from the WebUI, across the panel's top, the words below it
			if (item.tag >= 0) {
				if (const uint32_t cover = coverTexture(games[item.tag].id)) {
					scene.image(cover, { 1240, 252, 520, 243 }, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff), 12);
					y = 545;
				}
			}
			paragraph(paint, item.title, 1240, y, 34, look.text);
			y += 30;
			if (item.tag < 0) {
				if (!clientNote.empty()) paragraph(paint, clientNote, 1240, y, 22, look.danger);
				else if (store.phase == ProtonSteam::failed) paragraph(paint, store.message, 1240, y, 22, look.danger);
				for (const std::string &line : proton_steam_panel(store)) {
					paragraph(paint, line, 1240, y, 22, look.text_muted);
					y += 14;
				}
			} else {
				paragraph(paint, item.subtitle, 1240, y, 24, look.text_muted);
				// Why this game cannot start, in red. What the last session logged (sessionNote, noticesNote) is not shown: it
				// stays in the title log (the owner, 2026-10-31: "Don't display the red log in main menu").
				if (!games[item.tag].complaint.empty()) {
					y += 30;
					paragraph(paint, games[item.tag].complaint, 1240, y, 22, look.danger);
				}
				if (!updateNotice.empty()) {   // a newer release (the owner, 2026-10-31): never blocking, never in a game
					y += 30;
					paragraph(paint, updateRestart ? updateNotice + " Press R2 to restart now; otherwise it is switched in the next "
						"time " PS5_TITLE_NAME " starts." : updateNotice, 1240, y, 22, look.primary);
				}
				if (!payloadsNote.empty()) {
					y += 30;
					paragraph(paint, payloadsNote, 1240, y, 22, look.text_muted);
				}
			}
			scene.pop_clip();
			drawWebuiBlock(paint, look);
			if (updateRestart)   // a downloaded update: one press restarts into it
				ps5ui::draw_hints(scene, homeFonts, look,
					{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_SELECT) },
					  { hui::ui::Button::square, "Themes" }, { hui::ui::Button::triangle, "Settings" },
					  { hui::ui::Button::r1, "Music" }, { hui::ui::Button::r2, "Restart now" },
					  { hui::ui::Button::options, "Hold to close" } }, 120, 1040);
			else
				ps5ui::draw_hints(scene, homeFonts, look,
					{ { hui::ui::Button::dpad, tr(PW_TEXT_HINT_CHOOSE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_SELECT) },
					  { hui::ui::Button::square, "Themes" }, { hui::ui::Button::triangle, "Settings" },
					  { hui::ui::Button::r1, "Music" },
					  { hui::ui::Button::options, "Hold to close" } }, 120, 1040);
		}
		drawMusic(look);
		leaving.draw(scene, homeFonts, look);
		auto backdrop = look.backdrop;
		backdrop.time = clock;
		kit.renderer.begin();
		kit.renderer.backdrop(backdrop);
		kit.renderer.glass();
		kit.renderer.draw(scene);
	}

	void selftestStep()
	{
		if (ps5.frameBudget || !proton_selftest_active()) return;
		const auto &launch = proton_launch();
		proton_selftest_note(launch.game ? "game" : "launcher", launch.cycle);
		if (launch.game) {
			proton_queue_restart(nullptr, launch.cycle + 1, "selftest");
			quit = true;
		} else if (launch.cycle < 2 && !games.empty()) {
			proton_queue_restart(&games[0], launch.cycle, "selftest");
			quit = true;
		} else {
			proton_say("selftest: done after %u cycle(s)", launch.cycle);
			remove(proton_title_path("/app0/restart-check").c_str());
		}
	}

public:
	VulkanExample()
	{
		title = PS5_TITLE_NAME;
		name = "ps5proton";
		settings.overlay = false;
		ps5.ownOverlay = true;
		dataAvailable = proton_data_grant(); // Cached startup result; never retries after threads start.
		// The checklist shows whenever it fails; prepare() adds a passing one at a start from the home screen
		payloadShown = !proton_payloads_check().pass;
		// A one-shot test hook, like aurelia-open: with /data/ps5proton/payload-check-show present the checklist shows even
		// when it passes, and its third frame is saved as payload-check.ppm beside the flag, for a picture of the screen.
		if (dataAvailable && remove(PROTON_DATA_ROOT "/payload-check-show") == 0) {
			payloadShown = true;
			payloadPicture = 3;
			proton_say("launcher: the payload-check-show flag shows the checklist");
		}
		loadGames();
		// A failed session's reason (ended=), or a session that never came back to say one (its marker is still there).
		const std::string marker = proton_title_path(PROTON_SESSION_MARKER);
		if (FILE *file = fopen(marker.c_str(), "rb")) {
			char name[128] = {};
			const size_t got = fread(name, 1, sizeof(name) - 1, file);
			fclose(file);
			std::string game(name, got);
			game.erase(std::remove(game.begin(), game.end(), '\n'), game.end());
			sessionNote = "The last session" + (game.empty() ? std::string() : " (" + game + ")") +
				" did not end normally: it crashed, froze or lost power. Its log: " PROTON_DATA_ROOT "/logs/title-last.log";
			// Its own logs say so where they stop (src/pw_capped_log.h): the next start moves them into sessions/1.
			const char *stopped = "(the session stopped here without its end line: the system ended the title, or the console "
				"froze or lost power; noted at the next start)\n";
			const int noted_title = pw_capped_log_note(PROTON_DATA_ROOT "/logs/title-last.log", PROTON_DATA_ROOT "/logs/title-end.log", stopped);
			const int noted_wine = pw_capped_log_note(PROTON_DATA_ROOT "/logs/wine-last.log", PROTON_DATA_ROOT "/logs/wine-end.log", stopped);
			proton_say("launcher: the stopped session's logs noted (title %d, wine %d)", noted_title, noted_wine);
			remove(marker.c_str());
		}
		if (!proton_launch().ended.empty()) sessionNote = "Last session ended: " + proton_launch().ended;
		if (!sessionNote.empty()) proton_say("launcher: %s", sessionNote.c_str());
		// The memory plan's startup tip (docs/PLAN.md, Part 4): on the launcher's own screen, never over a game.
		if (FILE *file = fopen(PROTON_PAYLOADS_FILE, "r")) {
			unsigned long long mib = 0;
			if (fscanf(file, "%llu", &mib) == 1 && pw_payloads_notice(mib))
				payloadsNote = "Payloads are using about " + std::to_string(mib) + " MiB; closing them frees room for games.";
			fclose(file);
			if (!payloadsNote.empty()) proton_say("launcher: %s", payloadsNote.c_str());
		}
		const std::string notices = proton_title_path(PROTON_SESSION_NOTICES);
		if (FILE *file = fopen(notices.c_str(), "rb")) {
			char line[512];
			while (fgets(line, sizeof(line), file)) {
				std::string text(line);
				text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
				if (!text.empty()) noticesNote += (noticesNote.empty() ? "During the last session: " : " ") + text;
			}
			fclose(file);
			remove(notices.c_str());
			if (!noticesNote.empty()) proton_say("launcher: %s", noticesNote.c_str());
		}
	}

	// The library: the catalog, filtered as the launcher shows it, each entry's complaint. Called again when the WebUI
	// changes an entry.
	void loadGames()
	{
		games = proton_read_games(&problem);
		allGames = games;   // the Aurelia page plays and installs from the whole catalog, not the library's filtered list
		proton_say("launcher: %zu game(s); %s", games.size(), problem.c_str());
		// The library lists every game (the owner, 2026-10-26: a game added in the WebUI belongs in it), but an entry whose
		// game.json says "launcher": false: that one starts from the desktop's shortcut, a tile or a profile only.
		// A file named library-show-all in the data folder lists those too.
		struct stat showAll;
		if (!(dataAvailable && stat(PROTON_DATA_ROOT "/library-show-all", &showAll) == 0)) {
			const size_t installed = games.size();
			games.erase(std::remove_if(games.begin(), games.end(), [](const ProtonGame &game) {
				return game.id != "mihawk-os" && !game.launcher; }), games.end());
			proton_say("launcher: library shows %zu of %zu (every entry but those with \"launcher\": false; "
				"%s/library-show-all shows them all)", games.size(), installed, PROTON_DATA_ROOT);
		}
		// Checked here, before LoadExec: a game that cannot start is shown why instead of being started.
		const std::string blocked = dataAvailable ? proton_runtime_problem() : proton_data_problem();
		runtimeBlocked = blocked;
		for (auto &game : games)
			if (game.complaint.empty()) game.complaint = blocked;
		for (auto &game : allGames)
			if (game.complaint.empty()) game.complaint = blocked;
		if (!blocked.empty()) proton_say("launcher: games cannot start: %s", blocked.c_str());
	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		// The guided setup: on the first launch, or with the runtime or the mihawk prefix missing (src/pw_setup.h). A
		// test run shows it only when asked: by the first-run marker, or on the PC by
		// PROTON_SETUP_TEST=<step>[:<fake install's scenario>][@<share held>], which shows a step for a picture.
		const char *setupTest = getenv("PROTON_SETUP_TEST");
		setupUnattended = proton_setup_unattended(ps5.frameBudget != 0);
		if (setupUnattended) proton_say("setup: unattended (scripted presses, real engine)");
		setupFacts = SetupTour::facts(dataAvailable || setupTest, false, &setupSimulated);
		setupStep = setupTest ? (PwSetupStep)atoi(setupTest) : pw_setup_start(&setupFacts);
		if (ps5.frameBudget && !setupTest && !setupSimulated && !setupUnattended) setupStep = PW_SETUP_NONE;
		if (setupStep != PW_SETUP_NONE) unlink(SetupTour::preparingMarker);
		prepareKit({ .music = false, .covers = false });
		music.init(kit.mixer, getAssetPath() + "music", ps5.frameBudget != 0);
		music.play(music.remembered(), false, 2.5f);
		selectedTheme = ps5ui::active_theme_index();
		{
			std::string why;
			std::vector<PwWebuiChoice> looks, pieces;
			for (const auto &look : hui::ui::themes()) looks.push_back({ look.id, look.name });
			for (int i = 0; i < Jukebox::kPieces; ++i) pieces.push_back({ Jukebox::kIds[i], Jukebox::kTitles[i] });
			if (proton_webui_start(&why, std::move(looks), std::move(pieces))) proton_say("webui: listening on port 6767");   // it refuses without /data
			else proton_say("webui: not started: %s", why.c_str());
		}
		syncLibraryRows(true);
		std::vector<hui::ui::ListItem> rows;
		for (const auto &look : hui::ui::themes()) {
			hui::ui::ListItem item;
			item.title = look.name;
			item.subtitle = look.family;
			rows.push_back(item);
		}
		themes.set_items(rows);
		rows.clear();
		hui::ui::ListItem item;
		item.title = "Controls";
		item.subtitle = "Mouse button assignments";
		rows.push_back(item);
		item = {};
		item.title = "Game Mode";
		rows.push_back(item);
		item = {};
		item.title = "About " PS5_TITLE_NAME;
		item.subtitle = "Licence, warranty and source";
		rows.push_back(item);
		item = {};
		item.title = "Install runtime";
		rows.push_back(item);
		item = {};
		item.title = "Guided setup";
		item.subtitle = "Language, install and the first start, step by step";
		rows.push_back(item);
		item = {};
		item.title = "Music";
		rows.push_back(item);
		item = {};
		item.title = "Check for updates";
		rows.push_back(item);
		item = {};
		item.title = "Logging";
		rows.push_back(item);
		item = {};
		item.title = "Controller";
		rows.push_back(item);
		item = {};
		item.title = "Web UI";
		item.subtitle = "Your library and settings in a browser";
		rows.push_back(item);
		item = {};
		item.title = "120 Hz output";
		rows.push_back(item);
		item = {};
		item.title = "Automatic update check";
		rows.push_back(item);
		settingsList.set_items(rows);
		rows.clear();
		for (const char *label : {"Left mouse click", "Right mouse click", "Middle mouse click", "On-screen keyboard", "Restore defaults"}) {
			item = {};
			item.title = label;
			rows.push_back(item);
		}
		controlsList.set_items(rows);
		rows.clear();
		for (unsigned i = 0; i < PW_CONTROLS_BUTTONS; ++i) {
			item = {};
			item.title = pw_controls_button_label(i);
			rows.push_back(item);
		}
		buttonList.set_items(rows);
		if (ps5.frameBudget) pw_controls_defaults(&controls);
		else pw_controls_load(&controls, proton_controls_path().c_str());
		syncControls();
		if (!ps5.frameBudget) pw_game_mode_load(&gameMode, proton_game_mode_path().c_str());
		syncGameMode();
		if (!ps5.frameBudget) pw_high_refresh_load(&highRefresh, proton_high_refresh_path().c_str());
		syncHighRefresh();
		if (!ps5.frameBudget) pw_detailed_logs_load(&detailedLogs, proton_logging_path().c_str());
		syncLogging();
		if (!ps5.frameBudget) pw_controller_load(&controller, proton_controller_path().c_str());
		syncController();
		syncInstall();
		syncMusic();
		syncUpdates();
		for (auto *list : { &library, &aureliaList, &themes, &settingsList, &controlsList, &buttonList, &filesList, &optionsList }) {
			list->style.panel = true;
			list->style.row_height = 84;
			list->style.title_size = 28;
			list->style.subtitle_size = 20;
			list->set_bounds({ 120, 230, 1020, 740 });
			list->enter();
		}
		filesList.style.panel = optionsList.style.panel = false;   // on the menu's own milk-glass panel
		syncThemes();
		proton_say("launcher UI: %zu themes, active=%s", themes.items().size(), theme().id);
		home.load(kit, getAssetPath());
		if (!makeSky(homeSky)) proton_say("home: the sky's pipeline failed; the gloss look's backdrop stands in");
		// A passing checklist shows at every start from the home screen (cycle 0); a return from a game and a test run's
		// launcher skip it. The frame budget is known here, not in the constructor: a scripted launcher case (controls-menu,
		// setup-tour, steam-guide-pages) met a passing page there and failed (alpha5-base-2026-10-29-02).
		if (proton_launch().cycle == 0 && !ps5.frameBudget) payloadShown = true;
		if (payloadShown) startPayloadCheck();
		else if (setupStep != PW_SETUP_NONE) startSetup(setupStep);
		else if (getenv("PROTON_STEAM_GUIDE_TEST")) steamGuideTest(getenv("PROTON_STEAM_GUIDE_TEST"));
		else if (ps5.frameBudget && startShowcase()) {}
		else if (dataAvailable && !ps5.frameBudget) resumeSteamGuide();
		prepared = true;
	}

	// The tour and its sky (the setup and the Steam guide draw over the same one)
	void makeTour()
	{
		tour = std::make_unique<SetupTour>();
		if (!makeSky(tour->sky)) proton_say("setup: the sky's pipeline failed; the theme's backdrop stands in");
	}

	bool makeSky(SetupSky &sky)
	{
		const VkPipelineShaderStageCreateInfo stages[2] = {
			loadShader(getShadersPath() + "ps5proton/aero.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "ps5proton/aero.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT) };
		return sky.init(device, physicalDevice, vulkanDevice->queueFamilyIndices.graphics, renderPass, pipelineCache, stages,
			maxConcurrentFrames);
	}

	void closeTour()
	{
		vkDeviceWaitIdle(device);
		tour->sky.release();
		tour.reset();
		page = Page::library;
		homeLanguageReady = false;   // the setup may have changed the language: read again when the menu shows
		// The tour may have installed this build's runtime (an updated title folder, then the library with no restart):
		// the games it held back can start now
		if (dataAvailable && !runtimeBlocked.empty() && proton_runtime_problem().empty()) {
			for (auto *list : { &games, &allGames })
				for (auto &game : *list) if (game.complaint == runtimeBlocked) game.complaint.clear();
			proton_say("launcher: the runtime is this build's now; games can start");
			runtimeBlocked.clear();
			syncLibraryRows(false);
		}
	}

	// ---- The payload checklist (src/pw_payload_check.h): a page in the tour's look ----
	void startPayloadCheck()
	{
		if (proton_payloads().pass) proton_say("launcher: the payload checklist shows a pass");
		else proton_say("launcher: the payload checklist refuses entry: %s", proton_payloads_problem().c_str());
		makeTour();
		tour->beginPayloads(kit, getAssetPath(), proton_payloads(), proton_payload_facts(), proton_payloads_problem());
		if (const char *language = getenv("PROTON_SETUP_LANGUAGE"); ps5.frameBudget && language) tour->preview_language(language);
	}

	// Check again shows the new report (a pass then counts down visibly); Continue, or a pass's countdown, closes the page
	// into the guided setup when it is due, else the launcher
	void payloadCheckOutcome()
	{
		if (tour->outcome == SetupTour::Outcome::payload_recheck) {
			proton_payloads_check();
			tour->payloads(proton_payloads(), proton_payload_facts(), proton_payloads_problem(), true);
			return;
		}
		if (tour->outcome == SetupTour::Outcome::payload_close) {
			proton_say("launcher: closed from the payload checklist");
			quit = true;
			return;
		}
		if (tour->outcome == SetupTour::Outcome::payload_skip) {
			payloadsSkipped = true;   // this start only: nothing is saved, so the next start checks again
			proton_say("launcher: the player went on past the payload checklist");
		} else if (tour->outcome != SetupTour::Outcome::payload_continue) {
			return;
		} else {
			proton_say("launcher: the payload checklist passes");
		}
		payloadShown = false;
		closeTour();
		if (setupStep != PW_SETUP_NONE) startSetup(setupStep);
	}

	// ---- The Steam guide (src/pw_steam_guide.h): its launcher pages are the tour's Steam mode ----
	void startSteamGuide(PwSteamPage page, PwSteamFailure failure, bool starting = false)
	{
		makeTour();
		tour->beginSteam(kit, getAssetPath(), page, failure, ps5.frameBudget != 0 && !clientTest, starting);
		steamPoll = 0.0f;
	}

	// The launcher starts where the guide's last session left it: the sign-in session after the install, the done page
	// after a sign-in, a failed page with the reason
	void resumeSteamGuide()
	{
		PwSteamGuideRecord record;
		if (pw_steam_guide_record_load(&record, PW_STEAM_GUIDE_RECORD) != 0) return;
		const ProtonSteam now = proton_steam_state();
		PwSteamFailure failure;
		const PwSteamResume resume = pw_steam_guide_resume(&record, now.installed, now.signed_in, &failure);
		proton_say("steam guide: back from its %s session (%s): resume %d, failure %d",
			record.stage == PW_STEAM_STAGE_INSTALL ? "install" : "sign-in", pw_steam_guide_result_word(record.result), (int)resume,
			(int)failure);
		switch (resume) {
		case PW_STEAM_RESUME_NOTHING: unlink(PW_STEAM_GUIDE_RECORD); break;
		case PW_STEAM_RESUME_SIGNIN:
			startSteamGuide(PW_STEAM_PAGE_DOWNLOAD, PW_STEAM_FAIL_NONE, true);
			tour->outcome = SetupTour::Outcome::steam_signin;
			break;
		case PW_STEAM_RESUME_DONE: startSteamGuide(PW_STEAM_PAGE_DONE, PW_STEAM_FAIL_NONE); break;
		case PW_STEAM_RESUME_FAILED: startSteamGuide(PW_STEAM_PAGE_FAILED, failure); break;
		}
	}

	// A test's pictures of the guide's pages on the console (tools/run-launcher.py --steam-guide-pictures): the hook
	// /data/ps5proton/steam-guide-showcase names a language ("1": all ten); every page in it is held 24 frames and the
	// 20th kept as /data/ps5proton/steam-guide-pictures/<page>-<language>.ppm.
	std::vector<std::pair<std::string, std::string>> showcase;   // (page as steamGuideTest takes it, language)
	size_t showcaseItem{ 0 };
	uint32_t showcaseFrame{ 0 };
	bool startShowcase()
	{
		char word[32] = {};
		FILE *hook = fopen(PROTON_DATA_ROOT "/steam-guide-showcase", "r");
		if (!hook) return false;
		if (!fgets(word, sizeof(word), hook)) word[0] = 0;
		fclose(hook);
		unlink(PROTON_DATA_ROOT "/steam-guide-showcase");
		word[strcspn(word, "\r\n")] = 0;
		size_t count = 0;
		const PwSetupLanguage *all = pw_setup_languages(&count);
		for (size_t i = 0; i < count; ++i)
			if (all[i].text && (!strcmp(word, "1") || !strcmp(word, all[i].tag)))
				for (const char *page : { "intro", "download@0.45", "starting", "failed:offline", "done#Mihawk" })
					showcase.push_back({ page, all[i].tag });
		mkdir(PROTON_DATA_ROOT "/steam-guide-pictures", 0777);
		proton_say("steam guide showcase: %zu pages", showcase.size());
		if (showcase.empty()) return false;
		steamGuideTest(showcase[0].first, showcase[0].second.c_str());
		return true;
	}
	void showcaseStep()
	{
		if (showcase.empty() || showcaseItem >= showcase.size()) return;
		if (++showcaseFrame == 20) {
			std::string name = showcase[showcaseItem].first.substr(0, showcase[showcaseItem].first.find_first_of(":@#"));
			ps5.capturePath = std::string(PROTON_DATA_ROOT "/steam-guide-pictures/") + name + "-" + showcase[showcaseItem].second +
				".ppm";
			proton_say("steam guide showcase: picture %zu is %s in %s", showcaseItem + 1, name.c_str(), showcase[showcaseItem].second.c_str());
		} else if (showcaseFrame >= 24 && showcaseItem + 1 < showcase.size()) {
			++showcaseItem;
			showcaseFrame = 0;
			closeTour();
			steamGuideTest(showcase[showcaseItem].first, showcase[showcaseItem].second.c_str());
		}
	}

	// PROTON_STEAM_GUIDE_TEST=<page>[:<failure>][@<share downloaded>][#<display name>]: a page held for a picture on
	// the PC (pages intro, download, starting, failed, done; failures as pw_steam_guide.h names them, lower case)
	void steamGuideTest(const std::string &test, const char *language = getenv("PROTON_SETUP_LANGUAGE"))
	{
		static const char *const pages[] = { "intro", "download", "failed", "done" };
		static const char *const failures[] = { "none", "offline", "clock", "download", "install", "stalled", "cancelled",
			"timeout", "guard", "ended" };
		const std::string name = test.substr(0, test.find_first_of(":@#"));
		PwSteamPage page = PW_STEAM_PAGE_INTRO;
		for (int i = 0; i < 4; ++i) if (name == pages[i]) page = (PwSteamPage)i;
		if (name == "starting") page = PW_STEAM_PAGE_DOWNLOAD;
		PwSteamFailure failure = PW_STEAM_FAIL_NONE;
		if (const size_t colon = test.find(':'); colon != std::string::npos) {
			const std::string word = test.substr(colon + 1, test.find_first_of("@#", colon) - colon - 1);
			for (int i = 0; i < 10; ++i) if (word == failures[i]) failure = (PwSteamFailure)i;
		}
		startSteamGuide(page, failure, name == "starting");
		const size_t at = test.find('@'), hash = test.find('#');
		tour->holdSteam(at == std::string::npos ? 0.0f : (float)atof(test.c_str() + at + 1),
			hash == std::string::npos ? nullptr : test.c_str() + hash + 1);
		if (language) tour->preview_language(language);
	}

	// The guide's facts, a few times a second (the client's files are read each time: proton_steam_state)
	void pollSteamGuide(float dt)
	{
		if ((steamPoll -= dt) > 0.0f) return;
		steamPoll = 0.25f;
		const ProtonSteam now = proton_steam_state();
		SetupTour::SteamFacts facts;
		facts.downloading = now.phase == ProtonSteam::downloading;
		facts.downloaded = now.phase == ProtonSteam::downloaded;
		facts.failed = now.phase == ProtonSteam::failed;
		facts.installed = now.installed;
		facts.signed_in = now.signed_in;
		facts.bytes = now.bytes;
		facts.fetch_status = now.fetch_status;
		facts.persona = now.persona;
		tour->steam(facts);
	}

	// What the guide asked for: the download, a session (its record written first, so the launcher knows on its return
	// where it was), Big Picture, or back to the library
	void steamGuideOutcome()
	{
		const auto outcome = tour->outcome;
		const bool live = !ps5.frameBudget || clientTest;
		if (outcome == SetupTour::Outcome::none) return;
		if (outcome == SetupTour::Outcome::steam_download) {
			tour->outcome = SetupTour::Outcome::none;
			if (live) proton_steam_download();
			return;
		}
		if (outcome == SetupTour::Outcome::library) {
			if (live) unlink(PW_STEAM_GUIDE_RECORD);
			closeTour();
			syncAccounts();
			return;
		}
		if (!live) return;   // a test's picture holds its page
		ProtonGame game;
		std::string problem;
		PwSteamGuideRecord record{};
		bool ok = false;
		const char *why = "steam", *said = "launcher: Steam in Big Picture";   // the words tools/run-steam-option.py follows
		if (outcome == SetupTour::Outcome::steam_install) {
			record.stage = PW_STEAM_STAGE_INSTALL;
			ok = proton_steam_game(&game, true, &problem);
			why = "install";
			said = "launcher: installing from the downloaded installer";
		} else if (outcome == SetupTour::Outcome::steam_signin) {
			record.stage = PW_STEAM_STAGE_SIGNIN;
			ok = proton_steam_game(&game, false, &problem);
			why = "sign-in";
			said = "launcher: Steam sign-in session";
		} else {
			unlink(PW_STEAM_GUIDE_RECORD);
			ok = proton_steam_game(&game, false, &problem, true);
		}
		if (ok && !runtimeBlocked.empty()) ok = false, problem = "sessions cannot start: " + runtimeBlocked;
		if (!ok) {
			proton_say("steam guide: the session cannot start: %s", problem.c_str());
			tour->failSteam(outcome == SetupTour::Outcome::steam_install ? PW_STEAM_FAIL_INSTALL : PW_STEAM_FAIL_ENDED);
			return;
		}
		if (record.stage) {
			const int error = pw_steam_guide_record_save(&record, PW_STEAM_GUIDE_RECORD);
			if (error) proton_say("steam guide: record not saved (%d)", error);
		}
		proton_say("%s", said);
		proton_queue_restart(&game, proton_launch().cycle, why);
		quit = true;
	}

	void startSetup(PwSetupStep step)
	{
		makeTour();
		// The installer's engine (proton_install.h); a test (PROTON_SETUP_TEST on the PC) or the first-run marker plays the
		// install with the fake one, so nothing of the console's runtime is written.
		const char *test = getenv("PROTON_SETUP_TEST");
		std::string scenario = test && strchr(test, ':') ? strchr(test, ':') + 1 : "ok";
		scenario = scenario.substr(0, scenario.find('@'));
		std::unique_ptr<SetupEngine> engine;
		if (test || setupSimulated) engine = std::make_unique<FakeSetupEngine>(scenario);
		else engine = std::make_unique<InstallSetupEngine>();
		tour->begin(kit, getAssetPath(), setupFacts, step, setupSimulated, ps5.frameBudget != 0 && !setupUnattended,
			std::move(engine));
		if (test && strstr(test, "@")) tour->fake()->hold(atof(strstr(test, "@") + 1));
		if (const char *language = getenv("PROTON_SETUP_LANGUAGE"); test && language) tour->preview_language(language);
		if (test && step == PW_SETUP_PREPARING && scenario == "cover") tour->cover();
	}

	void renderSetup()
	{
		showcaseStep();   // may put the next page's tour in place
		const auto &look = homeLook();
		const hui::InputFrame input = ps5.frameBudget ? kit.scripted("ps5proton", ps5.framesDrawn) : kit.input();
		if (tour->steamMode() && (!ps5.frameBudget || clientTest)) pollSteamGuide(std::min(frameTimer, 0.05f));
		tour->update(std::min(frameTimer, 0.05f), input, ps5.frameBudget ? nullptr : &ps5_pad());
		music.pump(std::min(frameTimer, 0.05f));
		scene.clear();
		tour->compose(scene, look);
		kit.renderer.begin();
		if (!tour->sky.ready()) {
			auto backdrop = look.backdrop;
			backdrop.time = tour->clock();
			kit.renderer.backdrop(backdrop);
		}
		kit.renderer.draw(scene);
		buildKitCommandBuffer([&](VkCommandBuffer cmd) { tour->sky.draw(cmd, width, height, tour->clock(), tour->calm()); },
			[&](VkCommandBuffer cmd) { tour->sky.reset(cmd, currentBuffer); });
		// Start's frame shows the first session's cover: kept for the title to show while the prefix is made (src/pw_setup.h).
		if (tour->outcome == SetupTour::Outcome::prepare && (!ps5.frameBudget || setupUnattended))
			ps5.capturePath = PROTON_DATA_ROOT "/" PW_SETUP_COVER_NAME;
		if (payloadPicture >= 0 && payloadPicture-- == 0) ps5.capturePath = PROTON_DATA_ROOT "/payload-check.ppm";
		submitFrame();
		if (tour->payloadMode()) {
			payloadCheckOutcome();
			return;
		}
		if (tour->steamMode()) {
			steamGuideOutcome();
			return;
		}
		const auto outcome = tour->outcome;
		if (outcome == SetupTour::Outcome::none) return;
		if (outcome == SetupTour::Outcome::steam_guide) {   // Done's Install Steam: the Steam row's guide, from its intro
			closeTour();
			if (!steamMemoryWarns(SteamFrom::setup, "the setup's Install Steam")) startSteamGuide(PW_STEAM_PAGE_INTRO, PW_STEAM_FAIL_NONE);
			return;
		}
		const auto mihawk = std::find_if(games.begin(), games.end(), [](const ProtonGame &game) { return game.id == "mihawk-os"; });
		if (outcome != SetupTour::Outcome::library && (!ps5.frameBudget || setupUnattended) && mihawk != games.end()) {
			proton_queue_restart(&*mihawk, proton_launch().cycle, outcome == SetupTour::Outcome::prepare ? "setup-prepare" : "setup");
			quit = true;
			return;
		}
		closeTour();
		// The main menu from Done: the focus on Mihawk OS, one press from the shell
		home.home();
	}

	void render() override
	{
		if (!prepared) return;
		prepareFrame();
		if (tour) {
			renderSetup();
			return;
		}
		update(std::min(frameTimer, 0.05f));
		compose();
		if (onSky() && homeSky.ready())
			buildKitCommandBuffer([&](VkCommandBuffer cmd) { homeSky.draw(cmd, width, height, clock, 0.0f); },
				[&](VkCommandBuffer cmd) { homeSky.reset(cmd, currentBuffer); });
		else
			buildKitCommandBuffer();
		submitFrame();
		selftestStep();
	}

	~VulkanExample() override
	{
		if (device) vkDeviceWaitIdle(device);
		if (tour) tour->sky.release();
		homeSky.release();
		// The audio thread reads the music's rings: it stops before they go (KitExample's own release comes after
		// this class's members are destroyed)
		kit.release();
	}
};
} // namespace
VULKAN_EXAMPLE_MAIN()
