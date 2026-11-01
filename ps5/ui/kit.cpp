/*
 * PS5 Vulkan Template - the UI module: PS5_VKHomebrewUI's kit on the base class.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "kit.hpp"

#include "platform.h"
#include "proton_launch.h"

#include "core/save_file.hpp"

#include <span>

namespace ps5ui {

namespace {

// The faces in ui::Fonts order, as the kit bakes them
const char *const faceFiles[6] = {
	"inter-regular.huifont", "inter-semibold.huifont", "montserrat-medium.huifont",
	"dejavu-sans-mono.huifont", "press-start-2p.huifont", "patrick-hand.huifont",
};

// The audio thread renders the mixer; posting to it never blocks the frame
void fillFromMixer(int16_t *frames, int count, void *user)
{
	static_cast<hui::audio::Mixer *>(user)->render(frames, count);
}

} // namespace

Kit::~Kit()
{
	release();
}

bool Kit::init(const hui::gfx::VkRendererConfig &config, const std::string &assets, const Options &options)
{
	this->assets = assets;
	// A button already down as this program starts (the CROSS that chose it on
	// the screen before, still held or still buffered) is not a press for it:
	// the tracker starts from the title's pad state, which pad_poll carries
	// from program to program, as the samples' own input does (example_ps5.cpp)
	hui::PadSample current;
	current.buttons = ps5_pad().held;
	current.connected = true;
	tracker.update(std::span<const hui::PadSample>(&current, 1), (std::uint64_t)(now_seconds() * 1e6));
	if (!renderer.init(config)) {
		say("ui kit: renderer failed, VkResult %d", (int)renderer.last_error());
		return false;
	}
	ready_ = true;
	hui::ui::FontRef *refs[6] = { &fonts.regular, &fonts.semibold, &fonts.display, &fonts.mono, &fonts.pixel, &fonts.hand };
	for (int i = 0; i < 6; i++) {
		std::string data;
		const std::string path = assets + "/fonts/" + faceFiles[i];
		if (!hui::save::read_file(path, &data) || !faces_[i].load(data)) {
			say("ui kit: font %s: %s", path.c_str(), faces_[i].error().c_str());
			return false;
		}
		refs[i]->font = &faces_[i];
		refs[i]->texture = renderer.create_font_texture(faces_[i]);
		if (refs[i]->texture == 0) {
			say("ui kit: font %s: no texture, VkResult %d", faceFiles[i], (int)renderer.last_error());
			return false;
		}
	}
	if (options.covers && !catalog.build_covers(renderer, fonts)) {
		say("ui kit: cover art failed, VkResult %d", (int)renderer.last_error());
		return false;
	}
	if (options.sound) {
		// The music attaches to the mixer before the audio thread starts
		const int songs = options.music ? music.init(mixer, assets + "/audio/music", options.seed) : 0;
		const auto bank = sounds.load(assets + "/audio/sfx");
		audio = audio_start(fillFromMixer, &mixer);
		say("ui kit: %d songs, %d sounds (%d rejected), output %s", songs, bank.files, bank.rejected,
			audio ? "playing" : "none");
	}
	return true;
}

void Kit::release()
{
	if (audio) {
		audio_stop();
		audio = false;
	}
	if (rumble_left_ > 0.0f) {
		pad_vibrate(0.0f, 0.0f);
		rumble_left_ = 0.0f;
	}
	if (ready_) {
		renderer.release();
		ready_ = false;
	}
}

hui::InputFrame Kit::input()
{
	const pad_reading *readings = nullptr;
	const int count = pad_readings(&readings);
	samples_.resize(count);
	for (int i = 0; i < count; i++) {
		const pad_reading &r = readings[i];
		samples_[i] = { r.buttons, r.left_x, r.left_y, r.right_x, r.right_y, r.l2, r.r2, r.connected, r.timestamp_us };
	}
	return tracker.update(std::span<const hui::PadSample>(samples_), (std::uint64_t)(now_seconds() * 1e6));
}

hui::InputFrame Kit::scripted(const char *program, uint32_t frame) const
{
	hui::InputFrame input;
	input.connected = true;
	for (const Ps5Press &press : ps5Presses) {
		if (press.program != program) {
			continue;
		}
		if (press.until && frame > press.frame && frame <= press.until && (press.action == "cross" || press.action == "triangle")) {
			// a hold: down since its first frame
			input.held |= hui::action_bit(press.action == "cross" ? hui::Action::confirm : hui::Action::north);
			continue;
		}
		if (press.frame != frame) {
			continue;
		}
		if (press.action == "left") {
			input.nav = hui::Direction::left;
		} else if (press.action == "right") {
			input.nav = hui::Direction::right;
		} else if (press.action == "up") {
			input.nav = hui::Direction::up;
		} else if (press.action == "down") {
			input.nav = hui::Direction::down;
		} else if (press.action == "cross") {
			input.pressed |= hui::action_bit(hui::Action::confirm);
		} else if (press.action == "circle") {
			input.pressed |= hui::action_bit(hui::Action::back);
		} else if (press.action == "square") {
			input.pressed |= hui::action_bit(hui::Action::west);
		} else if (press.action == "triangle") {
			input.pressed |= hui::action_bit(hui::Action::north);
		} else if (press.action == "r1") {
			input.pressed |= hui::action_bit(hui::Action::page_next);
		} else if (press.action == "l1") {
			input.pressed |= hui::action_bit(hui::Action::page_prev);
		} else if (press.action == "r2") {
			input.pressed |= hui::action_bit(hui::Action::jump_next);
		} else if (press.action == "l2") {
			input.pressed |= hui::action_bit(hui::Action::jump_prev);
		} else if (press.action == "touch") {
			input.pressed |= hui::action_bit(hui::Action::touch);
		} else if (press.action == "options") {
			input.pressed |= hui::action_bit(hui::Action::menu);
		} else if (press.action.rfind("stick-", 0) == 0) {   // the left stick pushed all the way that frame (the menu's pointer)
			const std::string way = press.action.substr(6);
			input.stick_x = way == "right" ? 1.0f : way == "left" ? -1.0f : 0.0f;
			input.stick_y = way == "down" ? 1.0f : way == "up" ? -1.0f : 0.0f;
		}
		input.held |= input.pressed;
	}
	return input;
}

void Kit::play(const hui::ui::Feedback &feedback, hui::audio::SoundSet set, bool haptics)
{
	for (const hui::audio::CueEvent &event : feedback.cues) {
		sounds.play(mixer, event.set == hui::audio::SoundSet::count ? set : event.set, event);
		if (event.cue == hui::audio::Cue::complete || event.cue == hui::audio::Cue::welcome) {
			music.duck();
		}
	}
	if (haptics && feedback.rumble_strength > 0.0f && feedback.rumble_seconds > 0.0f) {
		// The small motor gives the crisp tick a UI wants; the large one adds
		// weight only to strong effects (the kit's own pad does the same)
		const float strength = std::min(feedback.rumble_strength, 1.0f);
		pad_vibrate(strength > 0.6f ? (strength - 0.6f) * 2.5f : 0.0f, strength);
		rumble_left_ = feedback.rumble_seconds;
	}
}

void Kit::tick(float dt)
{
	if (rumble_left_ > 0.0f) {
		rumble_left_ -= dt;
		if (rumble_left_ <= 0.0f) {
			pad_vibrate(0.0f, 0.0f);
		}
	}
	music.pump(dt);
}

void Kit::apply(const hui::Settings &settings)
{
	mixer.set_bus_gain(hui::audio::Bus::music, hui::Settings::gain(settings.music_volume));
	mixer.set_bus_gain(hui::audio::Bus::sfx, hui::Settings::gain(settings.sfx_volume));
	mixer.set_bus_gain(hui::audio::Bus::ui, hui::Settings::gain(settings.ui_volume));
	hui::InputSettings input = tracker.settings();
	input.swap_confirm = settings.swap_confirm;
	tracker.set_settings(input);
}

void Kit::light_bar(hui::gfx::Color color)
{
	const auto channel = [](float value) { return (uint8_t)(std::clamp(value, 0.0f, 1.0f) * 255.0f); };
	const uint8_t r = channel(color.r), g = channel(color.g), b = channel(color.b);
	const uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	if (packed != light_bar_) {
		light_bar_ = packed;
		pad_light_bar(r, g, b);
	}
}

namespace {

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif
std::string themeFolder() { return proton_title_path(PS5_APP_ROOT "/hui"); }
std::string themePath() { return themeFolder() + "/theme.txt"; }
// Proton starts in Gloss; a saved stable theme id overrides it.
const char *const defaultTheme = "gloss";

int themeIndex(const std::string &id)
{
	const auto all = hui::ui::themes();
	for (size_t i = 0; i < all.size(); i++) {
		if (id == all[i].id) {
			return (int)i;
		}
	}
	return -1;
}

int loadedTheme = -2; // -2: not read yet

} // namespace

int active_theme_index()
{
	if (ps5TestRun) {
		return themeIndex(defaultTheme);
	}
	if (loadedTheme == -2) {
		std::string id;
		hui::save::read_file(themePath(), &id, 64);
		while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) {
			id.pop_back();
		}
		loadedTheme = themeIndex(id);
		if (loadedTheme < 0) {
			loadedTheme = themeIndex(defaultTheme);
		}
	}
	return loadedTheme;
}

const hui::ui::Theme &active_theme()
{
	const int index = active_theme_index();
	return index >= 0 ? hui::ui::themes()[index] : hui::ui::default_theme();
}

void set_active_theme(int index)
{
	const auto all = hui::ui::themes();
	if (index < 0 || index >= (int)all.size()) {
		return;
	}
	loadedTheme = index;
	if (ps5TestRun) {
		return; // a test run leaves the title's choice alone
	}
	hui::save::ensure_directory(themeFolder());
	chmod(themeFolder().c_str(), 0777);
	const std::string error = hui::save::write_atomic(themePath(), std::string(all[index].id) + "\n");
	if (error.empty()) {
		chmod(themePath().c_str(), 0666);
	}
	say("theme: %s%s%s", all[index].id, error.empty() ? "" : ", not saved: ", error.c_str());
}

float draw_hints(hui::gfx::DrawList &list, const hui::ui::Fonts &fonts, const hui::ui::Theme &theme,
	std::initializer_list<hui::ui::Hint> hints, float x, float cy, int align, float size)
{
	hui::ui::GlyphStyle style = theme.dark ? hui::ui::GlyphStyle::dark() : hui::ui::GlyphStyle::light();
	style.label = theme.page_text_muted.a > 0.0f ? theme.page_text_muted : theme.text_muted;
	hui::ui::HintLayout layout;
	layout.size = size;
	layout.text_size = size * 0.62f;
	layout.cy = cy;
	layout.item_gap = size * 1.1f;
	const hui::ui::Hint *row = hints.begin();
	const int count = (int)hints.size();
	if (align == 0) {
		x -= hui::ui::measure_hints(fonts, row, count, layout) * 0.5f;
	}
	return hui::ui::draw_hints(list, fonts, style, row, count, x, align > 0, layout);
}

bool HoldToLeave::update(float dt, bool held)
{
	held_ = held ? held_ + dt : 0.0f;
	return held_ >= seconds;
}

void HoldToLeave::draw(hui::gfx::DrawList &list, const hui::ui::Fonts &fonts, const hui::ui::Theme &theme) const
{
	// A press is the program's: the plate shows only once the hold is clearly meant
	const float delay = 0.15f;
	if (held_ < delay) {
		return;
	}
	const float shown = std::min(1.0f, (held_ - delay) / 0.12f);
	const float progress = std::min(1.0f, held_ / seconds);
	const float width = 120.0f + hui::ui::button_width(hui::ui::Button::options, 34.0f) + fonts.semibold.font->measure(label, 26.0f);
	const hui::gfx::Rect plate{ 960.0f - width * 0.5f, 44.0f - (1.0f - shown) * 20.0f, width, 76.0f };
	list.push_opacity(shown);
	list.shadow(plate, 38.0f, 26.0f, theme.shadow.a > 0.0f ? theme.shadow.with_alpha(0.5f) : hui::gfx::Color::rgb(0x000000, 0.4f));
	list.rounded_rect(plate, 38.0f, theme.surface.with_alpha(0.96f));
	const float cx = plate.x + 46.0f, cy = plate.y + plate.h * 0.5f;
	list.ring(cx, cy, 24.0f, 6.0f, theme.surface_high);
	list.arc(cx, cy, 24.0f, 6.0f, 0.0f, 6.2831853f * progress, theme.accent);
	hui::ui::GlyphStyle glyphs = theme.dark ? hui::ui::GlyphStyle::dark() : hui::ui::GlyphStyle::light();
	hui::ui::draw_button(list, fonts, glyphs, hui::ui::Button::options, plate.x + 88.0f, cy, 34.0f);
	hui::ui::text(list, fonts.semibold, label, plate.x + 100.0f + hui::ui::button_width(hui::ui::Button::options, 34.0f),
		cy + 9.0f, 26.0f, theme.text);
	list.pop_opacity();
}

KitExample::~KitExample()
{
	// Before the base class destroys the device
	kit.release();
}

void KitExample::prepareKit(Kit::Options options)
{
	hui::gfx::VkRendererConfig config;
	config.physical_device = physicalDevice;
	config.device = device;
	config.queue = queue;
	config.queue_family = vulkanDevice->queueFamilyIndices.graphics;
	config.pipeline_cache = pipelineCache;
	config.frames_in_flight = maxConcurrentFrames;
	if (useDynamicRendering) {
		config.color_format = swapChain.colorFormat;
		config.depth_format = depthFormat;
		config.stencil_format = vks::tools::formatHasStencil(depthFormat) ? depthFormat : VK_FORMAT_UNDEFINED;
	} else {
		config.render_pass = renderPass;
	}
	if (benchmark.active) {
		options.seed = 0;
	} else if (options.seed == 0) {
		options.seed = (std::uint64_t)(now_seconds() * 1e6);
	}
	if (!kit.init(config, getAssetPath() + "hui", options)) {
		vks::tools::exitFatal("The UI kit could not start (klog has why)", -1);
	}
}

void KitExample::buildKitCommandBuffer(const std::function<void(VkCommandBuffer)> &scene,
	const std::function<void(VkCommandBuffer)> &offscreen)
{
	VkCommandBuffer cmd = drawCmdBuffers[currentBuffer];
	VkCommandBufferBeginInfo beginInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &beginInfo));
	// The program's own passes first: the glass copies may replay what they drew
	if (offscreen) {
		offscreen(cmd);
	}
	// Uploads the frame's shapes and makes the glass copies, outside the pass
	kit.renderer.prepare(cmd, currentBuffer);
	if (useDynamicRendering) {
		beginDynamicRendering(cmd);
	} else {
		VkClearValue clearValues[2]{};
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };
		VkRenderPassBeginInfo passInfo = vks::initializers::renderPassBeginInfo();
		passInfo.renderPass = renderPass;
		passInfo.framebuffer = frameBuffers[currentImageIndex];
		passInfo.renderArea.extent = { width, height };
		passInfo.clearValueCount = 2;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
	}
	if (scene) {
		scene(cmd);
	}
	kit.renderer.draw(cmd, (int)width, (int)height);
	drawUI(cmd);
	if (useDynamicRendering) {
		endDynamicRendering(cmd);
	} else {
		vkCmdEndRenderPass(cmd);
	}
	VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
}

} // namespace ps5ui
