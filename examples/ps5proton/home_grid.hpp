// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's main menu (Alpha 7, 2026-10-31), a Frutiger Aero tile grid: src/pw_home_grid.h's
// tiles on the setup's sky, a dock, and a pointer the left stick moves. This draws them and turns presses into what the
// launcher does (examples/ps5proton/ps5proton.cpp acts on them). Milk-glass tiles: a soft shadow, a milky body, a sheen on
// the upper half, a bright rim; the focused one lifts, with a thick blue rim and a glow.
#pragma once

#include <cmath>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../../src/pw_home_grid.h"
#include "../../src/pw_setup.h"

class HomeGrid
{
public:
	// One game tile's words and art, from the launcher's catalog (Mihawk OS is not one of them: it has its own tile)
	struct Game {
		std::string name, sub, badge;
		bool disabled{ false };
		uint32_t cover{ 0 };
	};
	enum class Action { none, mihawk, steam, game, webui, settings, files, payloads, options };

	void load(ps5ui::Kit &kit, const std::string &assets)
	{
		const char *names[] = { "aero-logo", "cursor", "dock-settings", "steam-tile", "orb-folder", "orb-document",
			"orb-app-browser", "orb-app-generic" };
		for (const char *name : names) {
			std::string png;   // ps5/tools/generate_assets.py's launcher generator
			const std::string path = assets + "launcher/" + (strncmp(name, "orb-", 4) ? "" : "orbs/") + name + ".png";
			if (!hui::save::read_file(path, &png, 4u << 20)) continue;
			int w = 0, h = 0, channels = 0;
			stbi_uc *rgba = stbi_load_from_memory((const stbi_uc *)png.data(), (int)png.size(), &w, &h, &channels, 4);
			if (rgba) textures_[name] = kit.renderer.create_texture(w, h, rgba), stbi_image_free(rgba);
		}
		const unsigned char white[4] = { 255, 255, 255, 255 };
		white_ = kit.renderer.create_texture(1, 1, white);
		if (textures_.size() < 8) proton_say("home: %zu of 8 pictures in assets/launcher; the menu draws without the others", textures_.size());
	}

	// The tiles for this many games, Steam's tile when shown; the focus stays on its tile when it can
	void sync(size_t games, bool steam)
	{
		const PwHomeTile was = focus_ >= 0 && (size_t)focus_ < layout_.count ? layout_.tiles[focus_] : PwHomeTile{};
		pw_home_layout(&layout_, games, steam ? 1 : 0);
		focus_ = 0;
		for (size_t i = 0; i < layout_.count; ++i)
			if (layout_.tiles[i].kind == was.kind && layout_.tiles[i].item == was.item) focus_ = (int)i;
		if (page_ >= layout_.pages) page_ = layout_.pages - 1;
		if (layout_.tiles[focus_].kind != PW_HOME_DOCK) page_ = layout_.tiles[focus_].page;
	}
	// Back to Mihawk OS's tile on the first page (the launcher's start, the setup's library tile)
	void home() { focus_ = 0, page_ = 0, pointerShown_ = 0.0f; }
	int focusedGame() const
	{
		const PwHomeTile &t = layout_.tiles[focus_];
		return t.kind == PW_HOME_GAME ? t.item : -1;
	}

	// One frame's presses: the D-pad and the pointer move the focus, L1/R1 the page, CROSS opens, TRIANGLE a game's options
	Action update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback, int *item)
	{
		*item = -1;
		const int was = focus_;
		glow_ += ((float)focus_ != glowFor_ ? -glow_ : 1.0f - glow_) * std::min(1.0f, dt * 12.0f);
		if (focus_ != (int)glowFor_) glowFor_ = (float)focus_, glow_ = 0.0f;
		if (input.nav != hui::Direction::none && !input.nav_from_stick) {
			const int dx = input.nav == hui::Direction::right ? 1 : input.nav == hui::Direction::left ? -1 : 0;
			const int dy = input.nav == hui::Direction::down ? 1 : input.nav == hui::Direction::up ? -1 : 0;
			focus_ = pw_home_move(&layout_, focus_, dx, dy, &page_);
			pointerShown_ = 0.0f;   // the D-pad's focus: the pointer hides until the stick moves again
		}
		if (input.is_pressed(hui::Action::page_next) || input.is_pressed(hui::Action::page_prev)) {
			const int before = page_;
			page_ = pw_home_page(&layout_, page_, input.is_pressed(hui::Action::page_next) ? 1 : -1, &focus_);
			if (page_ == before) feedback.play(hui::audio::Cue::error);
		}
		// The pointer: the left stick moves it, and what it is over takes the focus (the touchpad does not move it)
		if (std::fabs(input.stick_x) > 0.0f || std::fabs(input.stick_y) > 0.0f) {
			if (pointerShown_ <= 0.0f) {
				const PwHomeRect r = pw_home_rect(&layout_.tiles[focus_]);
				pointerX_ = r.x + r.w * 0.6f, pointerY_ = r.y + r.h * 0.6f;
			}
			pointerShown_ = 1.0f;
			pw_home_pointer(&pointerX_, &pointerY_, input.stick_x, input.stick_y, dt);
			const int over = pw_home_hit(&layout_, page_, pointerX_, pointerY_);
			if (over >= 0) focus_ = over;
		}
		if (focus_ != was) {
			feedback.play(hui::audio::Cue::focus);
			proton_say("home: focus on %s%s (page %d)", word(layout_.tiles[focus_]).c_str(), pointerShown_ > 0 ? " by the pointer" : "",
				page_ + 1);
		}
		const PwHomeTile &t = layout_.tiles[focus_];
		if (input.is_pressed(hui::Action::north) && t.kind == PW_HOME_GAME) {
			*item = t.item;
			return Action::options;
		}
		if (!input.is_pressed(hui::Action::confirm)) return Action::none;
		*item = t.item;
		switch (t.kind) {
		case PW_HOME_MIHAWK: return Action::mihawk;
		case PW_HOME_STEAM: return Action::steam;
		case PW_HOME_GAME: return Action::game;
		case PW_HOME_WEBUI: return Action::webui;
		case PW_HOME_DOCK: return t.item == PW_HOME_SETTINGS ? Action::settings : t.item == PW_HOME_FILES ? Action::files : Action::payloads;
		}
		return Action::none;
	}

	// The words' language (src/pw_setup.h's table, the player's choice in the setup) and its faces: ui draws the menu's
	// own words (the kit's, or the language's own CJK face in every role); names, the kit's, what is never translated
	// (game, file and Steam names, addresses) when ui lacks one of its characters
	void setLanguage(const PwSetupLanguage *language, const hui::ui::Fonts &ui, const hui::ui::Fonts &names)
	{
		language_ = language, ui_ = ui, names_ = names;
	}
	const char *tr(PwSetupText key) const { return pw_setup_text(language_, key); }
	std::string fill(PwSetupText key, std::initializer_list<std::string> args) const
	{
		std::vector<const char *> words;
		for (const auto &arg : args) words.push_back(arg.c_str());
		char out[512];
		pw_setup_fill(out, sizeof(out), tr(key), words.data(), words.size());
		return out;
	}
	const hui::ui::Fonts &ui() const { return ui_; }
	// A name's face: the menu's when it carries every character (so a name sits in the same face as its words), else the
	// kit's
	const hui::ui::FontRef &face(const std::string &text, bool bold) const
	{
		const hui::ui::FontRef &own = bold ? ui_.semibold : ui_.regular;
		for (size_t i = 0; i < text.size();)
			if (!own.font->has_glyph(hui::gfx::next_codepoint(text, &i))) return bold ? names_.semibold : names_.regular;
		return own;
	}

	// The page: the corner, the tiles, the pages' dots, the dock, the hints and the pointer. qr draws the Web UI's code (its
	// address, a square); banner, when not empty, is a note across the top in its colour (why the focused game cannot start,
	// a newer release, the payloads' memory); restart adds R2's hint.
	void compose(hui::gfx::DrawList &scene, const hui::ui::Theme &look, const std::vector<Game> &games, const Game &mihawk,
		const Game &steam, const std::string &url, const std::function<void(const std::string &, float, float, float)> &qr,
		const std::string &banner, hui::gfx::Color bannerColor, bool restart)
	{
		using hui::gfx::Color;
		const Color ink = Color::rgb(0x0e2b46), quiet = Color::rgb(0x2c5272);
		scene.image_gradient(white_, { 0, 0, 1920, 200 }, hui::gfx::kFullUv, Color::rgb(0xffffff, 0.5f), Color::rgb(0xffffff, 0.0f));
		scene.image_gradient(white_, { 0, 780, 1920, 300 }, hui::gfx::kFullUv, Color::rgb(0xffffff, 0.0f), Color::rgb(0xffffff, 0.6f));

		// The corner: the player's tile and name; the time on the right
		image(scene, "aero-logo", { 120, 54, 76, 76 }, 38);
		scene.bordered_rect({ 118, 52, 80, 80 }, 40, Color{ 0, 0, 0, 0 }, 3, Color::rgb(0xffffff, 0.9f));
		hui::ui::text(scene, names_.semibold, "Mihawk", 216, 100, 34, ink);
		const std::string count = fill(PW_TEXT_HOME_GAMES, { std::to_string(games.size()) }) + " · " +
			fill(PW_TEXT_HOME_PAGE, { std::to_string(page_ + 1), std::to_string(layout_.pages) });
		hui::ui::text(scene, face(count, false), count, 216, 130, 20, quiet);
		const std::time_t now = std::time(nullptr);
		if (const std::tm *local = std::localtime(&now)) {
			char clock[16];
			strftime(clock, sizeof(clock), "%H:%M", local);
			hui::ui::text(scene, names_.semibold, clock, 1800, 104, 46, ink, hui::gfx::Align::right);
			const std::string date = fill(PW_TEXT_HOME_DATE, { std::to_string(local->tm_mday), std::to_string(local->tm_mon + 1) });
			hui::ui::text(scene, face(date, false), date, 1800, 134, 22, quiet, hui::gfx::Align::right);
		}
		if (!banner.empty()) {   // between the corner and the clock: at most 1100 px, the words shrunk to fit
			const hui::ui::FontRef &font = face(banner, true);
			const float measured = font.measure(banner, 22), size = std::min(22.0f, 22.0f * 1040.0f / measured);
			const float w = font.measure(banner, size) + 60;
			const hui::gfx::Rect pill{ 960 - w / 2, 70, w, 52 };
			scene.shadow({ pill.x, pill.y + 6, pill.w, pill.h }, 26, 16, Color::rgb(0x03265e, 0.22f));
			scene.rounded_rect(pill, 26, bannerColor);
			sheen(scene, pill, 0.55f, 26);
			hui::ui::text(scene, font, banner, 960, pill.y + 26 + size * 0.36f, size, Color::rgb(0xffffff), hui::gfx::Align::center);
		}

		for (size_t i = 0; i < layout_.count; ++i) {
			const PwHomeTile &t = layout_.tiles[i];
			if (t.kind == PW_HOME_DOCK || t.page != page_) continue;
			const bool focus = (int)i == focus_;
			hui::gfx::Rect r = rect(t);
			switch (t.kind) {
			case PW_HOME_MIHAWK: featured(scene, look, r, mihawk, focus); break;
			case PW_HOME_STEAM:   // our own store bag with a controller, never Valve's mark (Legal, 2026-10-31)
				tile(scene, look, r, steam, focus, "steam-tile", Color::rgb(0x2a475e), Color::rgb(0x171a21));
				break;
			case PW_HOME_GAME:
				if ((size_t)t.item < games.size())
					tile(scene, look, r, games[t.item], focus, "orb-app-generic", Color::rgb(0x96cdfa), Color::rgb(0xebf7ff));
				break;
			case PW_HOME_WEBUI: webui(scene, look, r, url, qr, focus); break;
			default: break;
			}
		}

		// The pages: L1, a dot a page (the current one long), R1
		const float dy = PW_HOME_GRID_Y + PW_HOME_GRID_H + 44;
		if (layout_.pages > 1) {
			const float width = layout_.pages * 26.0f + 16;
			for (int p = 0; p < layout_.pages; ++p) {
				const bool on = p == page_;
				const float x = 960 - width / 2 + p * 26.0f + (on ? 0 : 8);
				scene.rounded_rect({ x, dy - 7, on ? 28.0f : 12.0f, 12 }, 6, Color::rgb(0x2a7bd6, on ? 1.0f : 0.35f));
			}
			hui::ui::GlyphStyle glyphs = hui::ui::GlyphStyle::light();
			hui::ui::draw_button(scene, names_, glyphs, hui::ui::Button::l1, 960 - width / 2 - 70, dy - 1, 30);
			hui::ui::draw_button(scene, names_, glyphs, hui::ui::Button::r1, 960 + width / 2 + 70 - hui::ui::button_width(hui::ui::Button::r1, 30),
				dy - 1, 30);
		}

		// The dock: the system's own tiles on a frosted shelf
		const hui::gfx::Rect dock{ 960 - 330, 852, 660, 132 };
		scene.shadow({ dock.x, dock.y + 10, dock.w, dock.h }, 44, 26, Color::rgb(0x03265e, 0.22f));
		scene.rounded_rect(dock, 44, Color::rgb(0xffffff, 0.55f));
		sheen(scene, dock, 0.55f, 44);
		scene.bordered_rect(dock, 44, Color{ 0, 0, 0, 0 }, 2, Color::rgb(0xffffff, 0.9f));
		const char *orbs[] = { "dock-settings", "orb-folder", "orb-document" };
		const PwSetupText labels[] = { PW_TEXT_HOME_SETTINGS, PW_TEXT_HOME_FILES, PW_TEXT_HOME_PAYLOADS };
		for (size_t i = layout_.count - PW_HOME_DOCK_ITEMS; i < layout_.count; ++i) {
			const PwHomeTile &t = layout_.tiles[i];
			const hui::gfx::Rect r = rect(t);
			if ((int)i == focus_) {
				scene.rounded_rect({ r.x + 14, r.y + 8, r.w - 28, r.h - 16 }, 30, look.primary.with_alpha(0.22f));
				scene.bordered_rect({ r.x + 14, r.y + 8, r.w - 28, r.h - 16 }, 30, Color{ 0, 0, 0, 0 }, 4, look.primary);
			}
			image(scene, orbs[t.item], { r.cx() - 42, r.y + 10, 84, 84 });
			fitted(scene, ui_.semibold, tr(labels[t.item]), r.cx(), r.y + 118, 20, r.w - 30, ink, hui::gfx::Align::center);
		}

		const PwHomeTile &f = layout_.tiles[focus_];
		std::vector<hui::ui::Hint> hints = { { hui::ui::Button::left_stick, tr(PW_TEXT_HINT_POINTER) },
			{ hui::ui::Button::dpad, tr(PW_TEXT_HINT_MOVE) }, { hui::ui::Button::cross, tr(PW_TEXT_HINT_OPEN) } };
		if (layout_.pages > 1) hints.push_back({ hui::ui::Button::l1, tr(PW_TEXT_HINT_PAGE), hui::ui::Button::r1 });
		if (f.kind == PW_HOME_GAME) hints.push_back({ hui::ui::Button::triangle, tr(PW_TEXT_HINT_OPTIONS) });
		hints.push_back({ hui::ui::Button::square, tr(PW_TEXT_HINT_THEMES) });
		if (restart) hints.push_back({ hui::ui::Button::r2, tr(PW_TEXT_HINT_RESTART) });
		hints.push_back({ hui::ui::Button::options, tr(PW_TEXT_HINT_CLOSE) });
		drawHints(scene, hints);

		// The pointer, drawn last, its fingertip at the point
		if (pointerShown_ > 0.0f) image(scene, "cursor", { pointerX_ - 42, pointerY_ - 14, 112, 112 });
	}

	// The row of button hints along the bottom, in the menu's ink and words, centred on the screen under the dock (the
	// owner, 2026-10-31: "I want them centered"); each glyph and its label share the row's centre line. A row too wide for
	// the screen (a long language with every hint) is drawn smaller, never cut.
	void drawHints(hui::gfx::DrawList &scene, const std::vector<hui::ui::Hint> &hints) const
	{
		hui::ui::GlyphStyle style = hui::ui::GlyphStyle::light();
		style.label = ink();
		hui::ui::HintLayout row;
		row.size = 34, row.text_size = 22, row.cy = 1042, row.item_gap = 40;
		float width = hui::ui::measure_hints(ui_, hints.data(), (int)hints.size(), row);
		if (width > kHintsRoom) {
			const float scale = kHintsRoom / width;
			row.size *= scale, row.text_size *= scale, row.item_gap *= scale;
			width = hui::ui::measure_hints(ui_, hints.data(), (int)hints.size(), row);
		}
		const float left = 960 - width / 2;
		hui::ui::draw_hints(scene, ui_, style, hints.data(), (int)hints.size(), left, false, row);
		if (std::fabs(left - hintsLeft_) > 0.5f) {   // the run log shows each new row: where it sits and whether it shrank
			hintsLeft_ = left;
			proton_say("home: hints %zu, x %.0f..%.0f, size %.0f", hints.size(), left, left + width, row.size);
		}
	}
	// Lines of the menu's words no wider than width, broken at spaces and between Chinese and Japanese characters
	// (the setup's own breaking); returns the y under the last
	float paragraph(hui::gfx::DrawList &scene, const hui::ui::FontRef &font, const std::string &words, float x, float y,
		float size, float width, hui::gfx::Color color, hui::gfx::Align align = hui::gfx::Align::left) const
	{
		for (const auto &line : SetupTour::lines(font, words, size, width)) {
			hui::ui::text(scene, font, line, x, y, size, color, align);
			y += size * 1.35f;
		}
		return y;
	}
	// One line, shrunk to fit width
	static void fitted(hui::gfx::DrawList &scene, const hui::ui::FontRef &font, const std::string &words, float x, float y,
		float size, float width, hui::gfx::Color color, hui::gfx::Align align = hui::gfx::Align::left)
	{
		const float measured = font.measure(words, size);
		hui::ui::text(scene, font, words, x, y, measured > width ? size * width / measured : size, color, align);
	}
	// A milk-glass panel at rest, for the pages under the menu (Files, Options)
	void panel(hui::gfx::DrawList &scene, const hui::ui::Theme &look, hui::gfx::Rect r) { glassTile(scene, look, r, false); }
	// Text on the sky, in the menu's two inks
	static hui::gfx::Color ink() { return hui::gfx::Color::rgb(0x0e2b46); }
	static hui::gfx::Color quiet() { return hui::gfx::Color::rgb(0x2c5272); }

private:
	static std::string word(const PwHomeTile &t)
	{
		switch (t.kind) {
		case PW_HOME_MIHAWK: return "Mihawk OS";
		case PW_HOME_STEAM: return "Steam";
		case PW_HOME_GAME: return "game " + std::to_string(t.item);
		case PW_HOME_WEBUI: return "the Web UI";
		case PW_HOME_DOCK: return t.item == PW_HOME_SETTINGS ? "Settings" : t.item == PW_HOME_FILES ? "Files" : "Payloads";
		}
		return "?";
	}
	static hui::gfx::Rect rect(const PwHomeTile &t)
	{
		const PwHomeRect r = pw_home_rect(&t);
		return { r.x, r.y, r.w, r.h };
	}
	void image(hui::gfx::DrawList &scene, const char *name, hui::gfx::Rect r, float radius = 0.0f)
	{
		auto found = textures_.find(name);
		if (found != textures_.end()) scene.image(found->second, r, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff), radius);
	}
	// The white sheen across the top of a glass shape, fading down
	void sheen(hui::gfx::DrawList &scene, hui::gfx::Rect r, float share, float radius)
	{
		scene.image_gradient(white_, { r.x, r.y, r.w, r.h * share }, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff, 0.6f),
			hui::gfx::Color::rgb(0xffffff, 0.0f), radius);
	}

	void glassTile(hui::gfx::DrawList &scene, const hui::ui::Theme &look, hui::gfx::Rect &r, bool focus)
	{
		using hui::gfx::Color;
		const float radius = 30;
		if (focus) {
			const float lift = 0.035f * std::min(1.0f, glow_ * 1.5f + 0.4f);
			r = { r.x - r.w * lift, r.y - r.h * lift, r.w * (1 + 2 * lift), r.h * (1 + 2 * lift) };
			scene.shadow({ r.x - 10, r.y - 10, r.w + 20, r.h + 20 }, radius + 10, 44, look.primary.with_alpha(0.95f));
			scene.shadow({ r.x - 4, r.y - 4, r.w + 8, r.h + 8 }, radius + 4, 16, Color::rgb(0xffffff, 0.9f));
		}
		scene.shadow({ r.x, r.y + 10, r.w, r.h }, radius, 22, Color::rgb(0x03265e, focus ? 0.35f : 0.25f));
		scene.rounded_rect(r, radius, Color::rgb(0xffffff, focus ? 0.86f : 0.74f));
		sheen(scene, r, 0.5f, radius);
		scene.bordered_rect(r, radius, Color{ 0, 0, 0, 0 }, focus ? 6.0f : 2.0f, focus ? look.primary : Color::rgb(0xffffff, 0.9f));
		if (focus) scene.bordered_rect({ r.x + 6, r.y + 6, r.w - 12, r.h - 12 }, radius - 6, Color{ 0, 0, 0, 0 }, 2.0f,
			Color::rgb(0xffffff, 0.95f));
	}

	// A game's tile (or Steam's): its art across the top (no art: its icon on a gradient), the name, a line, a badge
	void tile(hui::gfx::DrawList &scene, const hui::ui::Theme &look, hui::gfx::Rect r, const Game &game, bool focus,
		const char *icon, hui::gfx::Color top, hui::gfx::Color bottom)
	{
		using hui::gfx::Color;
		glassTile(scene, look, r, focus);
		const hui::gfx::Rect art{ r.x + 14, r.y + 14, r.w - 28, (r.w - 28) * 215.0f / 460.0f };
		if (game.cover) {
			scene.image(game.cover, art, hui::gfx::kFullUv, Color::rgb(0xffffff), 18);
		} else {
			scene.image_gradient(white_, art, hui::gfx::kFullUv, top, bottom, 18);
			image(scene, icon, { art.cx() - 52, art.cy() - 52, 104, 104 });
		}
		sheen(scene, { art.x, art.y, art.w, art.h }, 0.45f, 18);
		fitted(scene, face(game.name, true), game.name, r.x + 22, art.y + art.h + 46, 26, r.w - 44,
			game.disabled ? Color::rgb(0x0e2b46, 0.55f) : Color::rgb(0x0e2b46));
		const hui::ui::FontRef &badge = face(game.badge, true);
		const float badgeWidth = game.badge.empty() ? 0 : std::min(badge.measure(game.badge, 15) + 22, r.w * 0.55f);
		fitted(scene, face(game.sub, false), game.sub, r.x + 22, art.y + art.h + 76, 19, r.w - 44 - (badgeWidth ? badgeWidth + 14 : 0),
			Color::rgb(0x2c5272));
		if (!game.badge.empty()) {
			const hui::gfx::Rect pill{ r.x + r.w - 22 - badgeWidth, art.y + art.h + 58, badgeWidth, 26 };
			scene.rounded_rect(pill, 13, game.disabled ? look.danger : look.accent);
			fitted(scene, badge, game.badge, pill.cx(), pill.y + 18, 15, badgeWidth - 16, Color::rgb(0xffffff), hui::gfx::Align::center);
		}
	}

	// Mihawk OS's tile: a sky with a green rise, the Aero user tile, the name, a line, and Start
	void featured(hui::gfx::DrawList &scene, const hui::ui::Theme &look, hui::gfx::Rect r, const Game &mihawk, bool focus)
	{
		using hui::gfx::Color;
		glassTile(scene, look, r, focus);
		const hui::gfx::Rect inner{ r.x + 16, r.y + 16, r.w - 32, r.h - 32 };
		scene.image_gradient(white_, inner, hui::gfx::kFullUv, Color::rgb(0x96cdfa), Color::rgb(0xebf7ff), 24);
		scene.push_clip(inner);
		scene.circle(inner.x + inner.w * 0.35f, inner.y + inner.h + 330, 450, Color::rgb(0x8cd26e));
		scene.circle(inner.x + inner.w * 0.95f, inner.y + inner.h + 360, 420, Color::rgb(0x6ebe5a));
		scene.pop_clip();
		sheen(scene, inner, 0.42f, 24);
		const float logo = 210;
		scene.shadow({ inner.cx() - logo / 2, inner.y + 66, logo, logo }, 36, 26, Color::rgb(0x03265e, 0.35f));
		image(scene, "aero-logo", { inner.cx() - logo / 2, inner.y + 56, logo, logo }, 34);
		hui::ui::text(scene, names_.semibold, mihawk.name, inner.cx(), inner.y + 340, 58, Color::rgb(0x0e2b46), hui::gfx::Align::center);
		fitted(scene, ui_.regular, tr(PW_TEXT_HOME_MIHAWK_BODY), inner.cx(), inner.y + 382, 24, inner.w - 40, Color::rgb(0x2c5272),
			hui::gfx::Align::center);
		const hui::gfx::Rect pill{ inner.cx() - 120, inner.y + inner.h - 104, 240, 62 };
		scene.shadow({ pill.x, pill.y + 6, pill.w, pill.h }, 31, 14, Color::rgb(0x03265e, 0.3f));
		scene.rounded_rect(pill, 31, focus ? look.primary : Color::rgb(0xffffff, 0.8f));
		sheen(scene, pill, 0.55f, 31);
		hui::ui::GlyphStyle glyphs = hui::ui::GlyphStyle::light();
		hui::ui::draw_button(scene, names_, glyphs, hui::ui::Button::cross, pill.x + 30, pill.cy(), 34);
		fitted(scene, ui_.semibold, tr(PW_TEXT_HOME_START), pill.x + 138, pill.cy() + 10, 28, pill.w - 90,
			focus ? look.on_primary : Color::rgb(0x0e2b46), hui::gfx::Align::center);
	}

	// The Web UI's tile, two rows tall: what it is (the owner, 2026-10-31: not just "add games", the README's "Your browser
	// companion"), its QR code big enough to find from the couch, and its address for any computer
	void webui(hui::gfx::DrawList &scene, const hui::ui::Theme &look, hui::gfx::Rect r, const std::string &url,
		const std::function<void(const std::string &, float, float, float)> &qr, bool focus)
	{
		using hui::gfx::Color;
		glassTile(scene, look, r, focus);
		const Color ink = Color::rgb(0x0e2b46), quiet = Color::rgb(0x2c5272);
		const float room = r.w - 40;
		const float side = std::min(r.w - 70, 210.0f);
		const hui::gfx::Rect card{ r.cx() - side / 2, r.y + 238, side, side };
		// Each block of words fits its part of the tile in every language: the name and the sentence above the code,
		// and the sentence around the address below it, each shrunk as one (in steps of 5%, to 60% at most) until it ends
		// clear of the code and of the tile's edge. Japanese ran behind the code and off the tile (rc9-menu-ja-jp-pro).
		const auto tall = [&](const hui::ui::FontRef &font, const std::string &words, float size) {
			return words.empty() ? 0.0f : float(SetupTour::lines(font, words, size, room).size()) * size * 1.35f;
		};
		const auto shrink = [](float space, const std::function<float(float)> &needed) {
			float scale = 1.0f;
			while (scale > 0.62f && needed(scale) > space) scale -= 0.05f;
			return scale;
		};
		const std::string title = tr(PW_TEXT_HOME_COMPANION), body = tr(PW_TEXT_HOME_COMPANION_BODY);
		// From the name's first baseline to the code, less the last line's step and plus its descent
		const float above = shrink(card.y - 10 - (r.y + 120), [&](float s) {
			return tall(ui_.semibold, title, 27 * s) + 2 + tall(ui_.regular, body, 18 * s) - 18 * s * 1.35f + 18 * s * 0.35f; });
		image(scene, "orb-app-browser", { r.cx() - 32, r.y + 20, 64, 64 });
		float y = paragraph(scene, ui_.semibold, title, r.cx(), r.y + 120, 27 * above, room, ink, hui::gfx::Align::center);
		paragraph(scene, ui_.regular, body, r.cx(), y + 2, 18 * above, room, quiet, hui::gfx::Align::center);
		if (url.empty()) {
			scene.rounded_rect(card, 18, Color::rgb(0xffffff, 0.6f));
			paragraph(scene, ui_.regular, tr(PW_TEXT_HOME_CONNECT), r.cx(), card.y + 80, 20, side - 20, quiet, hui::gfx::Align::center);
			return;
		}
		scene.shadow({ card.x, card.y + 6, card.w, card.h }, 18, 14, Color::rgb(0x03265e, 0.25f));
		qr(url, card.x, card.y, card.w);
		// The sentence around the address ({1}), the address alone on its line in the kit's mono face
		const std::string scan = tr(PW_TEXT_HOME_SCAN), shown = url.rfind("http://", 0) == 0 ? url.substr(7) : url;
		const size_t at = scan.find("{1}");
		const std::string before = scan.substr(0, at), after = at == std::string::npos ? std::string() : scan.substr(at + 3);
		// From the first baseline under the code to the tile's edge: the sentence, the address's line, the rest
		const float below = shrink(r.y + r.h - 14 - (card.y + card.h + 34), [&](float s) {
			const float rest = tall(ui_.regular, after, 18 * s);
			return tall(ui_.regular, before, 18 * s) + (rest > 0 ? 28 * s + rest - 18 * s * 1.35f : 2) + 18 * s * 0.35f; });
		y = paragraph(scene, ui_.regular, before, r.cx(), card.y + card.h + 34, 18 * below, room, quiet, hui::gfx::Align::center);
		fitted(scene, names_.mono, shown, r.cx(), y + 2, 18 * below, room, ink, hui::gfx::Align::center);
		if (!after.empty())
			paragraph(scene, ui_.regular, after, r.cx(), y + 28 * below, 18 * below, room, quiet, hui::gfx::Align::center);
	}

	static constexpr float kHintsRoom = 1760.0f;   // the screen less 80 px each side
	mutable float hintsLeft_{ -1.0f };
	const PwSetupLanguage *language_{ nullptr };
	hui::ui::Fonts ui_{}, names_{};

	PwHomeLayout layout_{};
	int page_{ 0 }, focus_{ 0 };
	float glow_{ 0.0f }, glowFor_{ -1.0f };
	float pointerX_{ 960.0f }, pointerY_{ 540.0f }, pointerShown_{ 0.0f };
	std::map<std::string, uint32_t> textures_;
	uint32_t white_{ 0 };
};
