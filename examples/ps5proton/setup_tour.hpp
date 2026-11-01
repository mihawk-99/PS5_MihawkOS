/*
 * MihawkOS - the first-launch guided setup (docs/GUIDED_SETUP.md): Welcome, Language, how to install, the install,
 * the first Mihawk OS session, Done. Its rules (when it shows, where each step goes, its text) are src/pw_setup.h;
 * this file draws it with the kit in the Gloss theme over a Frutiger Aero sky (shaders/glsl/ps5proton/aero.*),
 * plays the glass sounds, takes the pad and the touchpad's pointer, and drives the install engine (SetupEngine).
 * The Steam guide's launcher pages (src/pw_steam_guide.h) are a mode of the same tour: its card, buttons and sounds.
 * ps5proton.cpp includes it, so it is compiled in the launcher's namespace (ps5/src/wrap.cpp.in).
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "stb_image.h"

#include <cerrno>
#include <cmath>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/pw_steam_guide.h"
#include "../../src/pw_runtime_check.h"
#include "../../src/pw_payload_check.h"

// ---- The install engine, as the tour sees it ----------------------------------------------------------------------
// The installer's ps5/src/proton_install.h (SELF_INSTALL phase 5) fills this; until it lands, FakeSetupEngine does,
// for host renders and for a simulated first run on the console (the setup-first-run marker). Every problem is typed,
// so the tour says it in the chosen language with a way out.
struct SetupInstall {
	enum Phase { unavailable, ready, checking, running, done, failed } phase{ unavailable };
	enum Problem { none, no_manifest, no_pieces, partial, no_space, read_failed, damaged, storage_full, write_failed,
		cancelled } problem{ none };
	uint64_t total{ 0 };                    // bytes the runtime holds (ready)
	uint64_t needed{ 0 }, available{ 0 };   // no_space
	size_t have{ 0 }, pieces{ 0 };          // partial
	uint64_t written{ 0 }, to_write{ 0 };   // checking, running, done
	double seconds{ 0.0 };                  // since start
};

class SetupEngine
{
public:
	virtual ~SetupEngine() = default;
	// Look for the runtime's pieces in the title folder; not while an install runs
	virtual SetupInstall find() = 0;
	// Start the install find() found ready; returns at once
	virtual void start() = 0;
	// Stop between pieces; what is installed stays (phase failed, problem cancelled)
	virtual void cancel() = 0;
	virtual SetupInstall state() = 0;
};

// Plays an install without touching the disk: "ok" (1.19 GB at about 180 MB/s), or a problem by its name
// ("no-pieces", "partial", "damaged", ...), the first time the folder is checked or the install runs.
class FakeSetupEngine : public SetupEngine
{
public:
	explicit FakeSetupEngine(std::string scenario) : scenario_(std::move(scenario)) {}
	SetupInstall find() override
	{
		state_ = {};
		state_.total = 1189920691ull;
		const auto problem = named();
		if (problem == SetupInstall::no_pieces || problem == SetupInstall::partial || problem == SetupInstall::no_space ||
			problem == SetupInstall::no_manifest) {
			state_.phase = SetupInstall::unavailable;
			state_.problem = problem;
			state_.have = 41, state_.pieces = 52;
			state_.needed = 2700000000ull, state_.available = 1300000000ull;
			scenario_ = "ok"; // Try again finds it
		} else {
			state_.phase = SetupInstall::ready;
		}
		return state_;
	}
	void start() override
	{
		if (state_.phase != SetupInstall::ready && state_.phase != SetupInstall::failed) return;
		state_.phase = SetupInstall::checking;
		state_.to_write = state_.total;
		started_ = clock_;
	}
	void cancel() override
	{
		if (state_.phase == SetupInstall::checking || state_.phase == SetupInstall::running) fail(SetupInstall::cancelled);
	}
	SetupInstall state() override
	{
		if (state_.phase == SetupInstall::checking || state_.phase == SetupInstall::running) {
			state_.seconds = clock_ - started_;
			state_.phase = state_.seconds < 0.6 ? SetupInstall::checking : SetupInstall::running;
			const double share = std::clamp((state_.seconds - 0.6) / 6.6, 0.0, 1.0);
			state_.written = (uint64_t)(share * (double)state_.to_write);
			if (share > 0.45 && named() != SetupInstall::none) {
				fail(named());
				scenario_ = "ok";
			} else if (share >= 1.0) {
				state_.phase = SetupInstall::done;
			}
		}
		return state_;
	}
	void tick(double dt) { clock_ += dt; }
	// A test run holds the install at this share, for a picture of it under way
	void hold(double share) { started_ = clock_ - 0.6 - share * 6.6; held_ = true; }
	bool held() const { return held_; }

private:
	SetupInstall::Problem named() const
	{
		static const char *const names[] = { "", "no-manifest", "no-pieces", "partial", "no-space", "read-failed", "damaged",
			"storage-full", "write-failed", "cancelled" };
		for (int i = 1; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
			if (scenario_ == names[i]) return (SetupInstall::Problem)i;
		return SetupInstall::none;
	}
	void fail(SetupInstall::Problem problem)
	{
		state_.phase = SetupInstall::failed;
		state_.problem = problem;
	}
	std::string scenario_;
	SetupInstall state_;
	double clock_{ 0.0 }, started_{ 0.0 };
	bool held_{ false };
};

// The installer's engine (ps5/src/proton_install.h, SELF_INSTALL phase 5), from the title folder's own pieces alone,
// never a download. Its phases and problems are the tour's, in the same order.
class InstallSetupEngine : public SetupEngine
{
public:
	SetupInstall find() override { return convert(proton_install_find("title")); }
	void start() override { proton_install_start(); }
	void cancel() override { proton_install_cancel(); }
	SetupInstall state() override { return convert(proton_install_state()); }

private:
	static_assert((int)ProtonInstall::failed == (int)SetupInstall::failed && (int)ProtonInstall::cancelled == (int)SetupInstall::cancelled);
	static SetupInstall convert(const ProtonInstall &from)
	{
		SetupInstall to;
		to.phase = (SetupInstall::Phase)from.phase;
		// A download's problems cannot come from a drive (the tour's Internet route waits for a release)
		to.problem = from.problem <= ProtonInstall::cancelled ? (SetupInstall::Problem)from.problem : SetupInstall::read_failed;
		to.needed = from.needed, to.available = from.available, to.have = from.have, to.pieces = from.total;
		to.written = from.progress.written, to.to_write = from.progress.to_write, to.seconds = from.seconds;
		to.total = from.bytes;   // the manifest's size
		return to;
	}
};

// ---- The sky: one fragment shader under the kit's layers, and its GPU time ----------------------------------------
class SetupSky
{
public:
	float gpu_ms{ -1.0f }; // the last average; negative until measured

	bool init(VkDevice device, VkPhysicalDevice physical, uint32_t queueFamily, VkRenderPass pass, VkPipelineCache cache,
		const VkPipelineShaderStageCreateInfo stages[2], uint32_t slots)
	{
		device_ = device;
		VkPushConstantRange range{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push) };
		VkPipelineLayoutCreateInfo layoutInfo = vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &range;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) return false;
		VkPipelineVertexInputStateCreateInfo vertices = vks::initializers::pipelineVertexInputStateCreateInfo();
		auto assembly = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		auto raster = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
			VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		auto blendAttachment = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		auto blend = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachment);
		auto depth = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
		auto viewport = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		auto multisample = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		auto dynamic = vks::initializers::pipelineDynamicStateCreateInfo(dynamics, 2, 0);
		VkGraphicsPipelineCreateInfo info = vks::initializers::pipelineCreateInfo(layout_, pass, 0);
		info.pVertexInputState = &vertices;
		info.pInputAssemblyState = &assembly;
		info.pRasterizationState = &raster;
		info.pColorBlendState = &blend;
		info.pDepthStencilState = &depth;
		info.pViewportState = &viewport;
		info.pMultisampleState = &multisample;
		info.pDynamicState = &dynamic;
		info.stageCount = 2;
		info.pStages = stages;
		if (vkCreateGraphicsPipelines(device, cache, 1, &info, nullptr, &pipeline_) != VK_SUCCESS) return false;
		// The timer, where the queue keeps time stamps
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical, &properties);
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
		if (queueFamily < familyCount && families[queueFamily].timestampValidBits && properties.limits.timestampPeriod > 0) {
			period_ = properties.limits.timestampPeriod;
			VkQueryPoolCreateInfo queries{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queries.queryCount = slots * 2;
			if (vkCreateQueryPool(device, &queries, nullptr, &queries_) == VK_SUCCESS) written_.assign(slots, false);
		}
		return true;
	}

	// Outside the pass, once the slot's last frame is done: read its time, then reset its two stamps
	void reset(VkCommandBuffer cmd, uint32_t slot)
	{
		if (!queries_ || slot >= written_.size()) return;
		uint64_t stamps[2];
		if (written_[slot] && vkGetQueryPoolResults(device_, queries_, slot * 2, 2, sizeof(stamps), stamps, sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && stamps[1] >= stamps[0]) {
			total_ += (double)(stamps[1] - stamps[0]) * period_ * 1e-6;
			if (++frames_ == 240) {
				gpu_ms = (float)(total_ / frames_);
				proton_say("setup sky: %.3f ms GPU a frame (average of %u frames)", gpu_ms, frames_);
				total_ = 0.0;
				frames_ = 0;
			}
		}
		vkCmdResetQueryPool(cmd, queries_, slot * 2, 2);
		written_[slot] = false;
		slot_ = slot;
	}

	void draw(VkCommandBuffer cmd, uint32_t width, uint32_t height, float time, float calm)
	{
		if (!pipeline_) return;
		const bool timed = queries_ && slot_ < written_.size();
		if (timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries_, slot_ * 2);
		VkViewport viewport{ 0, 0, (float)width, (float)height, 0, 1 };
		VkRect2D scissor{ { 0, 0 }, { width, height } };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
		const Push push{ { (float)width, (float)height }, time, calm };
		vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		if (timed) {
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, slot_ * 2 + 1);
			written_[slot_] = true;
		}
	}

	bool ready() const { return pipeline_ != VK_NULL_HANDLE; }

	void release()
	{
		if (!device_) return;
		if (queries_) vkDestroyQueryPool(device_, queries_, nullptr);
		if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
		if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
		queries_ = VK_NULL_HANDLE, pipeline_ = VK_NULL_HANDLE, layout_ = VK_NULL_HANDLE;
		device_ = VK_NULL_HANDLE;
	}

private:
	struct Push { float size[2]; float time; float calm; };
	VkDevice device_{ VK_NULL_HANDLE };
	VkPipelineLayout layout_{ VK_NULL_HANDLE };
	VkPipeline pipeline_{ VK_NULL_HANDLE };
	VkQueryPool queries_{ VK_NULL_HANDLE };
	std::vector<bool> written_;
	uint32_t slot_{ 0 }, frames_{ 0 };
	float period_{ 0.0f };
	double total_{ 0.0 };
};

// ---- The tour ------------------------------------------------------------------------------------------------------
class SetupTour
{
public:
	// steam_*: the Steam guide asks the launcher to download the installer, or to restart into its install session, its
	// sign-in session or the client in Big Picture
	enum class Outcome { none, library, steam_guide, prepare, steam_download, steam_install, steam_signin, steam_big_picture,
		payload_recheck, payload_continue, payload_close, payload_skip };

	SetupSky sky;
	Outcome outcome{ Outcome::none };

	// Where the record and the markers are (/data/ps5proton), and the title's assets folder (the flags, the QR code)
	static constexpr const char *recordPath = PROTON_DATA_ROOT "/setup.txt";
	static constexpr const char *firstRunMarker = PROTON_DATA_ROOT "/setup-first-run";
	static constexpr const char *preparingMarker = PROTON_DATA_ROOT "/setup-preparing";

	// What the console has now. The first-run marker plays a first run on a set-up console: the tour shows the runtime
	// and the prefix as missing, and installs with the fake engine, so nothing of the owner's is replaced.
	static PwSetupFacts facts(bool data, bool asked, bool *simulated)
	{
		PwSetupFacts facts{};
		facts.data = data;
		facts.asked = asked;
		if (!data) return facts;
		PwSetupRecord record;
		pw_setup_record_load(&record, recordPath);
		facts.done = record.done;
		// Installed means this build's runtime (src/pw_runtime_check.h): another build's gets the install, as a missing one.
		facts.runtime = proton_runtime_state() == PW_RUNTIME_THIS_BUILD;
		facts.prefix = access(PROTON_DATA_ROOT "/prefixes/mihawk/system.reg", R_OK) == 0;
		facts.returning = access(preparingMarker, F_OK) == 0;
		*simulated = access(firstRunMarker, F_OK) == 0;
		if (*simulated) {
			facts.asked = 1;
			if (!facts.returning) facts.runtime = facts.prefix = 0;
		}
		return facts;
	}

	// test: a test run's preview (nothing is saved); scenario: the fake engine's, when one is used
	void begin(ps5ui::Kit &kit, const std::string &assets, const PwSetupFacts &facts, PwSetupStep step, bool simulated,
		bool test, std::unique_ptr<SetupEngine> engine)
	{
		kit_ = &kit;
		assets_ = assets;
		facts_ = facts;
		simulated_ = simulated;
		test_ = test;
		engine_ = std::move(engine);
		PwSetupRecord record;
		pw_setup_record_load(&record, recordPath);
		language_ = pw_setup_language(record.language) ? pw_setup_language(record.language) : pw_setup_default_language();
		size_t count = 0;
		const PwSetupLanguage *all = pw_setup_languages(&count);
		for (size_t i = 0; i < count; ++i) {
			if (!all[i].text) continue;
			shown_.push_back(&all[i]);
			std::string png;
			hui::save::read_file(assets + "setup/flags/" + all[i].flag + ".png", &png, 1 << 20);
			int w = 0, h = 0, channels = 0;
			stbi_uc *rgba = png.empty() ? nullptr :
				stbi_load_from_memory((const stbi_uc *)png.data(), (int)png.size(), &w, &h, &channels, 4);
			flags_.push_back(rgba ? kit.renderer.create_texture(w, h, rgba) : 0);
			if (rgba) stbi_image_free(rgba);
			else proton_say("setup: no flag for %s (%s)", all[i].tag, all[i].flag);
		}
		for (const auto *language : shown_) loadFace(language);
		if (step == PW_SETUP_PREPARING && facts.returning) prepareFailed_ = true;
		useLanguage(language_);
		go(step, false);
		feedback_.play(hui::audio::Cue::welcome);
		proton_say("setup: starts at step %d (record done=%d runtime=%d prefix=%d returning=%d%s), %zu languages, language %s",
			(int)step, facts.done, facts.runtime, facts.prefix, facts.returning, simulated ? ", simulated first run" : "",
			shown_.size(), language_->tag);
	}

	// A picture's language, without saving it
	void preview_language(const char *tag)
	{
		if (const PwSetupLanguage *language = pw_setup_language(tag)) {
			loadFace(language);
			useLanguage(language);
		}
	}

	// ---- The Steam guide's pages (src/pw_steam_guide.h) ----
	// What the launcher reads of the client (ps5/src/proton_steam.h), a few times a second
	struct SteamFacts {
		bool downloading{ false }, downloaded{ false }, failed{ false }, installed{ false }, signed_in{ false };
		unsigned long long bytes{ 0 };
		int fetch_status{ 0 };   // the download's PW_HTTPS_* status when it failed
		std::string persona;     // the signed-in display name
	};

	// In the setup's language, at page. starting: the download page says "Installed. Starting Steam" (the launcher
	// restarts into the sign-in session); test: a test run's preview.
	void beginSteam(ps5ui::Kit &kit, const std::string &assets, PwSteamPage page, PwSteamFailure failure, bool test,
		bool starting = false)
	{
		kit_ = &kit;
		assets_ = assets;
		test_ = test;
		steam_ = true;
		starting_ = starting;
		failure_ = failure;
		PwSetupRecord record;
		pw_setup_record_load(&record, recordPath);
		language_ = pw_setup_language(record.language) ? pw_setup_language(record.language) : pw_setup_default_language();
		loadFace(language_);
		useLanguage(language_);
		goSteam(page, false);
		proton_say("steam guide: starts at page %d (failure %d%s), language %s", (int)page, (int)failure,
			starting ? ", starting the sign-in" : "", language_->tag);
	}
	void steam(const SteamFacts &facts) { steamFacts_ = facts; }
	bool steamMode() const { return steam_; }
	// A test's picture: the download held at share, the done page's countdown stopped, a display name
	void holdSteam(float share, const char *persona)
	{
		held_ = true;
		steamFacts_.bytes = (unsigned long long)(share * (float)PW_STEAM_INSTALLER_BYTES);
		if (persona) steamFacts_.persona = persona;
	}
	// The launcher could not start the session the guide asked for: the guide says so, with Try again
	void failSteam(PwSteamFailure failure)
	{
		failure_ = failure;
		outcome = Outcome::none;
		feedback_.play(hui::audio::Cue::error);
		goSteam(PW_STEAM_PAGE_FAILED, true);
	}

	// ---- The payload checklist (src/pw_payload_check.h): a page of the setup's look. Failing, it stays until Check again
	// (Outcome::payload_recheck) passes; passing, it says so and continues by itself after kPayloadContinueS, or at
	// CROSS (Outcome::payload_continue). CROSS counts only once it has been seen up: the press that started the title
	// must not act on the page (the owner, 2026-10-29: "The payload check closes the screen immediately"). Failing, TRIANGLE
	// held PW_PAYLOAD_SKIP_SECONDS goes on anyway (Outcome::payload_skip, this start only). English only, but for that hold.
	void beginPayloads(ps5ui::Kit &kit, const std::string &assets, const PwPayloadReport &report, const PwPayloadFacts &facts,
		const std::string &advice)
	{
		kit_ = &kit;
		assets_ = assets;
		payloads_ = true;
		PwSetupRecord record;
		pw_setup_record_load(&record, recordPath);
		language_ = pw_setup_language(record.language) ? pw_setup_language(record.language) : pw_setup_default_language();
		loadFace(language_);
		useLanguage(language_);
		payloads(report, facts, advice, false);
		payloadArmed_ = false;
		feedback_.play(hui::audio::Cue::welcome);
	}
	// A new check's report: the page comes in again; an error sound when it still fails
	void payloads(const PwPayloadReport &report, const PwPayloadFacts &facts, const std::string &advice, bool again)
	{
		skipSeconds_ = 0.0f;
		payloadReport_ = report;
		payloadFacts_ = facts;
		payloadAdvice_ = advice;
		outcome = Outcome::none;
		payloadCountdown_ = kPayloadContinueS;
		entered_ = again ? 0.0f : 1.0f;
		if (again && !report.pass) feedback_.play(hui::audio::Cue::error);
		layout();
		focus_ = T_PAYLOAD_CHECK;
	}
	bool payloadMode() const { return payloads_; }

	PwSetupStep step() const { return step_; }
	float calm() const
	{
		return (steam_ ? steamPage_ == PW_STEAM_PAGE_DOWNLOAD : step_ == PW_SETUP_INSTALLING) ? 1.0f : 0.0f;
	}
	float clock() const { return clock_; }
	FakeSetupEngine *fake() { return dynamic_cast<FakeSetupEngine *>(engine_.get()); }

	// The first session's cover (src/pw_setup.h): what the minute of making the prefix shows, without a button, the hints
	// or the pointer; the title draws each stage's mark over it. The launcher keeps it as a picture when Start is
	// pressed; a test shows it with PROTON_SETUP_TEST=5:cover.
	void cover()
	{
		cover_ = true;
		layout();
		focus_ = T_NONE;
		entered_ = 1.0f;
		pointerShown_ = 0.0f;
	}

	// One frame. pad: the console's pad, for the touchpad's pointer (nullptr in a test run)
	void update(float dt, const hui::InputFrame &input, const struct pad *pad)
	{
		feedback_.clear();
		clock_ += dt;
		entered_ = std::min(1.0f, entered_ + dt / (step_ == PW_SETUP_WELCOME ? 0.9f : 0.35f));
		press_ = std::max(0.0f, press_ - dt * 6.0f);
		if (auto *fake = this->fake()) fake->tick(dt);
		if (steam_) steamTick(dt);
		if (engine_ && (step_ == PW_SETUP_INSTALLING)) {
			const auto before = install_.phase;
			install_ = engine_->state();
			if (install_.phase != before) {
				proton_say("setup: install %d problem %d, %llu of %llu bytes in %.1f s", (int)install_.phase,
					(int)install_.problem, (unsigned long long)install_.written, (unsigned long long)install_.to_write,
					install_.seconds);
				if (install_.phase == SetupInstall::failed) feedback_.play(hui::audio::Cue::error);
				if (install_.phase == SetupInstall::done) {
					feedback_.play(hui::audio::Cue::notify);
					doneAt_ = clock_;
				}
			}
			if (install_.phase == SetupInstall::done && clock_ - doneAt_ > 1.4f && outcome == Outcome::none) {
				facts_.runtime = 1;
				const PwSetupStep next = pw_setup_next(PW_SETUP_INSTALLING, &facts_);
				if (next == PW_SETUP_NONE) finish(Outcome::library, "runtime updated");
				else go(next, true);
			}
		}
		layout();
		// A step whose buttons changed (an install that failed) keeps a focus: its first button
		if (!target(focus_) && !targets_.empty()) focus_ = targets_.front().id;
		pointer(dt, pad);
		if (input.nav != hui::Direction::none) {
			pointerShown_ = 0.0f;
			const int to = navigate(input.nav);
			if (to != focus_) {
				focus_ = to;
				feedback_.play(hui::audio::Cue::focus);
			}
		}
		if (payloads_ && !payloadArmed_) payloadArmed_ = !input.is_held(hui::Action::confirm);
		if (payloads_) {
			if (payloadArmed_ && input.is_pressed(hui::Action::confirm)) activate(focus_);
			else if (input.is_pressed(hui::Action::back) && outcome == Outcome::none) {
				feedback_.play(hui::audio::Cue::back);
				outcome = Outcome::payload_close;   // the user closes MihawkOS; nothing closes it for them
			} else if (payloadReport_.pass && outcome == Outcome::none && (payloadCountdown_ -= dt) <= 0.0f) {
				outcome = Outcome::payload_continue;
				proton_say("payload check: continues by itself");
			}
			// A failing check's way through: TRIANGLE held PW_PAYLOAD_SKIP_SECONDS (CROSS checks again), from a press made here
			if (pw_payload_skip_offered(&payloadReport_) && outcome == Outcome::none) {
				const bool held = input.is_held(hui::Action::north);
				if (!held) skipArmed_ = true;
				const float was = skipSeconds_;
				if (pw_payload_skip_hold(&skipSeconds_, held && skipArmed_, dt)) {
					char line[512];
					pw_payload_skip_line(&payloadReport_, line, sizeof(line));
					proton_say("checklist: %s", line);
					feedback_.play(hui::audio::Cue::saved);
					outcome = Outcome::payload_skip;
				} else if (was > 0.0f && skipSeconds_ == 0.0f) {
					proton_say("checklist: hold let go at %.1f s", was);
				}
			}
		} else if (input.is_pressed(hui::Action::confirm)) activate(focus_);
		else if (steam_) {
			if (input.is_pressed(hui::Action::back) && steamPage_ != PW_STEAM_PAGE_DOWNLOAD && outcome == Outcome::none) {
				feedback_.play(hui::audio::Cue::back);
				leaveSteam("back");
			}
		} else if (input.is_pressed(hui::Action::back) && pw_setup_back(step_) != PW_SETUP_NONE && !busy()) {
			feedback_.play(hui::audio::Cue::back);
			go(pw_setup_back(step_), true);
		}
		if (!steam_ && !payloads_ && step_ == PW_SETUP_LIMITS && outcome == Outcome::none) {
			// The limits page: cross held PW_SETUP_HOLD_SECONDS goes on, letting go starts over, and nothing else goes on.
			// A hold counts from a press made here (cross still down from the step before arms nothing).
			const bool held = input.is_held(hui::Action::confirm);
			if (!held) holdArmed_ = true;
			const float was = holdSeconds_;
			holdSeconds_ = pw_setup_hold(holdSeconds_, held && holdArmed_, dt);
			if (was > 0.0f && holdSeconds_ == 0.0f) proton_say("setup: hold let go at %.1f s", was);
			if (holdSeconds_ >= PW_SETUP_HOLD_SECONDS) {
				feedback_.play(hui::audio::Cue::saved);
				proton_say("setup: held %.1f s", holdSeconds_);
				go(PW_SETUP_DONE, true);
			}
		}
		for (auto &[id, amount] : glow_) amount += ((id == focus_ ? 1.0f : 0.0f) - amount) * std::min(1.0f, dt * 14.0f);
		if (!glow_.count(focus_)) glow_[focus_] = 0.0f;
		kit_->play(feedback_, hui::audio::SoundSet::glass);
		kit_->tick(dt); // the rumble's end, and the music decoded ahead
	}

	void compose(hui::gfx::DrawList &list, const hui::ui::Theme &look)
	{
		const auto &fonts = fonts_;
		hui::ui::Painter paint(list, fonts, look);
		const auto ink = paint.page_text(), muted = paint.page_text_muted();
		paint.heading("MihawkOS", 96, 92, 32, ink);
		if (steam_) {
			const int stage = steamPage_ == PW_STEAM_PAGE_DOWNLOAD ? (starting_ ? 1 : 0) : steamPage_ == PW_STEAM_PAGE_DONE ? 3 : -1;
			if (stage >= 0) progressDots(list, paint, look, stage, 4);
		} else if (step_ != PW_SETUP_WELCOME && !payloads_) {
			progressDots(list, paint, look);
		}
		const float ease = 1.0f - (1.0f - entered_) * (1.0f - entered_) * (1.0f - entered_);
		list.push_opacity(ease);
		list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - ease) * (step_ == PW_SETUP_WELCOME ? 36.0f : 20.0f));
		if (cover_) {
			card(list, paint, text(PW_TEXT_BUILD_TITLE), text(PW_TEXT_BUILD_BODY));
			const PwSetupText stages[] = { PW_TEXT_BUILD_SETTINGS, PW_TEXT_BUILD_DOTNET, PW_TEXT_BUILD_FINISHING };
			for (int i = 0; i < 3; ++i)
				hui::ui::text(list, fonts.semibold, text(stages[i]), PW_SETUP_COVER_MARK_X + 52,
					PW_SETUP_COVER_ROW_Y + i * PW_SETUP_COVER_ROW_STEP + 11, 30, look.text);
		} else if (steam_) {
			composeSteam(list, paint, look);
		} else if (payloads_) {
			composePayloads(list, paint, look);
		} else switch (step_) {
		case PW_SETUP_WELCOME:
			paint.heading(text(PW_TEXT_WELCOME_TITLE), 960, 470, fitted(paint, text(PW_TEXT_WELCOME_TITLE), 92, 1640), ink,
				hui::gfx::Align::center);
			wrapped(list, fonts.regular, text(PW_TEXT_WELCOME_BODY), 960, 538, 32, 1500, muted, hui::gfx::Align::center);
			break;
		case PW_SETUP_LANGUAGE:
			card(list, paint, text(PW_TEXT_LANGUAGE_TITLE), text(PW_TEXT_LANGUAGE_BODY));
			break;
		case PW_SETUP_INSTALLING: {
			card(list, paint, text(PW_TEXT_INSTALLING_TITLE), nullptr);
			const float share = install_.to_write ? std::min(1.0f, (float)install_.written / (float)install_.to_write) : 0.0f;
			char percent[16];
			snprintf(percent, sizeof(percent), "%d%%", (int)(share * 100.0f));
			paint.heading(percent, kCard.x + 72, kCard.y + 290, 104, look.text);
			paint.progress({ kCard.x + 72, kCard.y + 330, kCard.w - 144, 26 }, share);
			char line[256] = "";
			if (install_.phase == SetupInstall::checking) {
				snprintf(line, sizeof(line), "%s", text(PW_TEXT_INSTALLING_CHECKING));
			} else if (install_.to_write) {
				char done[32], total[32], speed[32];
				snprintf(done, sizeof(done), "%llu", (unsigned long long)(install_.written / 1000000ull));
				snprintf(total, sizeof(total), "%llu", (unsigned long long)(install_.to_write / 1000000ull));
				snprintf(speed, sizeof(speed), "%.0f", install_.seconds > 0.0 ? install_.written / 1e6 / install_.seconds : 0.0);
				const char *args[] = { done, total, speed };
				pw_setup_fill(line, sizeof(line), text(PW_TEXT_INSTALLING_STATS), args, 3);
			}
			hui::ui::text(list, fonts.semibold, line, kCard.x + 72, kCard.y + 404, 28, look.text);
			// A problem (pieces missing or damaged, a full disk) stays until Try again finds none: no way past it
			if (install_.problem != SetupInstall::none)
				status(list, problem().c_str(), look.danger, kCard.y + 500);
			else if (install_.phase == SetupInstall::done)
				status(list, text(PW_TEXT_INSTALLED), look.success, kCard.y + 500);
			else
				wrapped(list, fonts.regular, text(PW_TEXT_INSTALLING_BODY), kCard.x + 72, kCard.y + 470, 26, 1100, look.text_muted);
			break;
		}
		case PW_SETUP_PREPARING: {
			card(list, paint, text(PW_TEXT_PREPARE_TITLE), text(PW_TEXT_PREPARE_BODY));
			const hui::gfx::Rect note{ kCard.x + 72, kCard.y + 330, kCard.w - 144, 150 };
			list.rounded_rect(note, look.radius, look.warning.with_alpha(0.14f));
			list.bordered_rect(note, look.radius, hui::gfx::Color{ 0, 0, 0, 0 }, 1.5f, look.warning.with_alpha(0.7f));
			list.circle(note.x + 48, note.y + 50, 20, look.warning);
			hui::ui::text(list, fonts.semibold, "!", note.x + 48, note.y + 60, 28, hui::gfx::Color::rgb(0x1b2430), hui::gfx::Align::center);
			wrapped(list, fonts.regular, text(PW_TEXT_PREPARE_NOTE), note.x + 92, note.y + 60, 26, note.w - 130, look.text);
			if (prepareFailed_) status(list, text(PW_TEXT_PREPARE_FAILED), look.danger, kCard.y + 545);
			break;
		}
		case PW_SETUP_DONE:
			card(list, paint, text(PW_TEXT_DONE_TITLE), text(PW_TEXT_DONE_BODY));
			break;
		case PW_SETUP_LIMITS: {
			card(list, paint, text(PW_TEXT_LIMITS_TITLE), text(PW_TEXT_LIMITS_BODY));
			// The games it cannot run, in the warning note the Preparing page uses: a heading, two points, what happens.
			// Under the body's last line (three in German), as card() wraps it.
			const float bodyEnd = kCard.y + 166 + lines(fonts.regular, text(PW_TEXT_LIMITS_BODY), 27, 1120).size() * 27 * 1.42f;
			const hui::gfx::Rect note{ kCard.x + 72, std::max(kCard.y + 268, bodyEnd + 12), kCard.w - 144, 268 };
			list.rounded_rect(note, look.radius, look.warning.with_alpha(0.14f));
			list.bordered_rect(note, look.radius, hui::gfx::Color{ 0, 0, 0, 0 }, 1.5f, look.warning.with_alpha(0.7f));
			list.circle(note.x + 48, note.y + 50, 20, look.warning);
			hui::ui::text(list, fonts.semibold, "!", note.x + 48, note.y + 60, 28, hui::gfx::Color::rgb(0x1b2430), hui::gfx::Align::center);
			hui::ui::text(list, fonts.semibold, text(PW_TEXT_LIMITS_NOTE_TITLE), note.x + 92, note.y + 60, 28, look.text);
			float y = note.y + 108;
			for (const PwSetupText point : { PW_TEXT_LIMITS_DRM, PW_TEXT_LIMITS_ANTICHEAT }) {
				list.circle(note.x + 104, y - 8, 5, look.text);
				y = wrapped(list, fonts.regular, text(point), note.x + 124, y, 26, note.w - 164, look.text) + 6;
			}
			wrapped(list, fonts.regular, text(PW_TEXT_LIMITS_RESULT), note.x + 92, y + 8, 26, note.w - 130, look.text_muted);
			// Hold cross: a ring around its glyph fills over PW_SETUP_HOLD_SECONDS; the words before it, the glyph where
			// the text says {1}, with the text's own spaces around it (none before a comma: "Удерживайте ✕, чтобы")
			const float cx = kCard.x + kCard.w - 72 - 52, cy = kCard.y + kCard.h - 72 - 40;
			holdRing(list, fonts_, look, cx, cy, holdSeconds_ / PW_SETUP_HOLD_SECONDS, hui::ui::Button::cross, text(PW_TEXT_LIMITS_HOLD));
			break;
		}
		default:
			break;
		}
		for (const auto &target : targets_) drawTarget(list, paint, look, target);
		list.pop_transform();
		list.pop_opacity();
		if (!cover_) hints(list, look);
		if (pointerShown_ > 0.0f && !cover_) {
			list.push_opacity(std::min(1.0f, pointerShown_));
			list.shadow({ pointerX_ - 13, pointerY_ - 10, 26, 26 }, 13, 10, hui::gfx::Color::rgb(0x1b2430, 0.35f));
			list.circle(pointerX_, pointerY_, 13, hui::gfx::Color::rgb(0xffffff, 0.92f));
			list.ring(pointerX_, pointerY_, 13, 3, look.primary);
			list.pop_opacity();
		}
	}

private:
	enum Id { T_NONE = 0, T_START, T_CONTINUE, T_CHECK, T_STOP, T_BACK, T_PREPARE,
		T_DONE_STEAM, T_DONE_LIBRARY, T_STEAM_START, T_STEAM_NOT_NOW, T_STEAM_RETRY, T_PAYLOAD_CHECK, T_LANGUAGE = 100 };
	struct Target {
		int id;
		hui::gfx::Rect rect;
		bool disabled{ false };
	};
	static constexpr hui::gfx::Rect kCard{ 300, 170, 1320, 760 };
	static constexpr float kTileH = 220;
	static constexpr float kStatusY = 930 - 190;

	const char *text(PwSetupText key) const { return pw_setup_text(language_, key); }
	bool busy() const
	{
		return step_ == PW_SETUP_INSTALLING &&
			(install_.phase == SetupInstall::checking || install_.phase == SetupInstall::running || install_.phase == SetupInstall::done);
	}

	// A language's own face (Chinese, Japanese), once: every role of the kit's set points at it (one weight)
	void loadFace(const PwSetupLanguage *language)
	{
		if (!language->font || faceRefs_.count(language->font)) return;
		std::string data;
		faces_.emplace_back();
		hui::gfx::Font &face = faces_.back();
		if (!hui::save::read_file(assets_ + "setup/fonts/" + language->font + ".huifont", &data) || !face.load(data)) {
			proton_say("setup: no face %s; %s keeps the launcher's", language->font, language->tag);
			faces_.pop_back();
			return;
		}
		faceRefs_[language->font] = { &face, kit_->renderer.create_font_texture(face) };
	}

	void useLanguage(const PwSetupLanguage *language)
	{
		language_ = language;
		fonts_ = kit_->fonts;
		if (language->font && faceRefs_.count(language->font)) {
			const auto face = faceRefs_.at(language->font);
			fonts_.regular = fonts_.semibold = fonts_.display = fonts_.mono = face;
		}
	}

	void go(PwSetupStep step, bool animate)
	{
		step_ = step;
		entered_ = animate ? 0.0f : 1.0f;
		holdSeconds_ = 0.0f, holdArmed_ = false;
		install_ = {};
		if (step == PW_SETUP_INSTALLING) install();   // nothing to choose: the pieces are the title's own
		layout();
		focus_ = targets_.empty() ? T_NONE : targets_.front().id;
		if (step == PW_SETUP_LANGUAGE)
			for (size_t i = 0; i < shown_.size(); ++i) if (shown_[i] == language_) focus_ = T_LANGUAGE + (int)i;
		if (step == PW_SETUP_DONE) save(true);
		proton_say("setup: step %d, %d of 6", (int)step, stepIndex() + 1);   // what the counter shows
	}

	void save(bool done)
	{
		PwSetupRecord record;
		pw_setup_record_load(&record, recordPath);
		snprintf(record.language, sizeof(record.language), "%s", language_->tag);
		record.done = record.done || done;
		if (test_) return; // a test run previews; the owner's record stays as it is
		const int error = pw_setup_record_save(&record, recordPath);
		if (error) proton_say("setup: record not saved (%d)", error);
	}

	void finish(Outcome outcome, const char *why)
	{
		save(true);
		if (simulated_ && !test_) unlink(firstRunMarker);
		this->outcome = outcome;
		proton_say("setup: ends (%s)", why);
	}

	// The step's focusable things, laid out
	void layout()
	{
		targets_.clear();
		if (cover_) return; // nothing to choose
		const float right = kCard.x + kCard.w - 72, bottom = kCard.y + kCard.h - 72;
		if (payloads_) {
			targets_.push_back({ T_PAYLOAD_CHECK, { right - 300, bottom - 68, 300, 68 } });
			return;
		}
		if (steam_) {
			// The secondary button left of the primary one, which is laid out last (it takes the focus: goSteam)
			if (steamPage_ == PW_STEAM_PAGE_INTRO) targets_.push_back({ T_STEAM_NOT_NOW, { right - 632, bottom - 68, 300, 68 } });
			if (steamPage_ == PW_STEAM_PAGE_FAILED) targets_.push_back({ T_BACK, { right - 632, bottom - 68, 300, 68 } });
			if (steamPage_ == PW_STEAM_PAGE_INTRO) targets_.push_back({ T_STEAM_START, { right - 300, bottom - 68, 300, 68 } });
			if (steamPage_ == PW_STEAM_PAGE_FAILED) targets_.push_back({ T_STEAM_RETRY, { right - 300, bottom - 68, 300, 68 } });
			if (steamPage_ == PW_STEAM_PAGE_DONE) targets_.push_back({ T_CONTINUE, { right - 300, bottom - 68, 300, 68 } });
			return;
		}
		switch (step_) {
		case PW_SETUP_WELCOME:
			targets_.push_back({ T_START, { 960 - 200, 596, 400, 84 } });
			break;
		case PW_SETUP_LANGUAGE: {
			// Up to four large tiles in a row; more in two rows (nine: five and four), six at most a row
			const int count = (int)shown_.size(), columns = count <= 4 ? count : std::min(6, (count + 1) / 2);
			const float gap = count <= 4 ? 32 : 22;
			const float w = count <= 4 ? 268 : std::min(220.0f, (kCard.w - 144 - (columns - 1) * gap) / columns);
			const float h = count <= 4 ? 250 : w * 0.86f;
			for (int i = 0; i < count; ++i) {
				const int row = i / columns, inRow = std::min(columns, count - row * columns);
				const float x0 = kCard.x + (kCard.w - (inRow * w + (inRow - 1) * gap)) * 0.5f;
				const int rows = (count + columns - 1) / columns;
				const float top = kCard.y + 250 + (kCard.h - 250 - 72 - (rows * h + (rows - 1) * gap)) * 0.5f;
				targets_.push_back({ T_LANGUAGE + i, { x0 + (i % columns) * (w + gap), top + row * (h + gap), w, h } });
			}
			break;
		}
		case PW_SETUP_INSTALLING:
			if (install_.problem != SetupInstall::none) {
				targets_.push_back({ T_CHECK, { right - 300, bottom - 68, 300, 68 } });   // Try again, and nothing else
			} else if (install_.phase != SetupInstall::done) {
				targets_.push_back({ T_STOP, { right - 260, bottom - 68, 260, 68 } });
			}
			break;
		case PW_SETUP_PREPARING:
			targets_.push_back({ T_PREPARE, { right - 300, bottom - 68, 300, 68 } });
			break;
		case PW_SETUP_DONE: {
			// What to do first, two tiles: Steam's guide or the library (the shell is a press away from it). Taller than the
			// method's: a line runs to three in French, German and Japanese.
			const float w = (kCard.w - 144 - 32) / 2.0f, h = kTileH + 40, y = kCard.y + 250 + (kCard.h - 250 - 72 - h) * 0.5f;
			targets_.push_back({ T_DONE_STEAM, { kCard.x + 72, y, w, h } });
			targets_.push_back({ T_DONE_LIBRARY, { kCard.x + 72 + w + 32, y, w, h } });
			break;
		}
		default:
			break;
		}
	}

	const Target *target(int id) const
	{
		for (const auto &t : targets_) if (t.id == id) return &t;
		return nullptr;
	}

	// The nearest thing in the pressed direction, favouring what lines up with the focus
	int navigate(hui::Direction direction) const
	{
		const Target *from = target(focus_);
		if (!from) return targets_.empty() ? T_NONE : targets_.front().id;
		const float dx = direction == hui::Direction::left ? -1.0f : direction == hui::Direction::right ? 1.0f : 0.0f;
		const float dy = direction == hui::Direction::up ? -1.0f : direction == hui::Direction::down ? 1.0f : 0.0f;
		int best = focus_;
		float bestScore = 1e9f;
		for (const auto &t : targets_) {
			const float vx = t.rect.cx() - from->rect.cx(), vy = t.rect.cy() - from->rect.cy();
			const float along = vx * dx + vy * dy, across = std::fabs(vx * dy - vy * dx);
			if (t.id == focus_ || along <= 1.0f) continue;
			const float score = along + across * 2.5f;
			if (score < bestScore) bestScore = score, best = t.id;
		}
		return best;
	}

	void pointer(float dt, const struct pad *pad)
	{
		pointerShown_ = std::max(0.0f, pointerShown_ - dt * (pointerShown_ > 1.0f ? 0.2f : 1.0f));
		if (!pad || !pad->connected) return;
		if (pad->touch_count) {
			if (touching_) {
				const float mx = (float)pad->touch_x - lastTouchX_, my = (float)pad->touch_y - lastTouchY_;
				if (mx || my) {
					pointerX_ = std::clamp(pointerX_ + mx * 1.4f, 0.0f, 1919.0f);
					pointerY_ = std::clamp(pointerY_ + my * 1.4f, 0.0f, 1079.0f);
					pointerShown_ = 4.0f;
					for (const auto &t : targets_) {
						const auto &r = t.rect;
						if (pointerX_ >= r.x && pointerX_ < r.x + r.w && pointerY_ >= r.y && pointerY_ < r.y + r.h && t.id != focus_) {
							focus_ = t.id;
							feedback_.play(hui::audio::Cue::focus);
						}
					}
				}
			}
			touching_ = true;
			lastTouchX_ = pad->touch_x, lastTouchY_ = pad->touch_y;
		} else {
			touching_ = false;
		}
		// A click of the touchpad presses what the pointer is on
		if ((pad->pressed & PAD_TOUCH_PAD) && pointerShown_ > 0.0f) {
			for (const auto &t : targets_) {
				const auto &r = t.rect;
				if (pointerX_ >= r.x && pointerX_ < r.x + r.w && pointerY_ >= r.y && pointerY_ < r.y + r.h) activate(t.id);
			}
		}
	}

	void activate(int id)
	{
		const Target *t = target(id);
		if (!t) return;
		if (t->disabled) {
			feedback_.play(hui::audio::Cue::error);
			return;
		}
		press_ = 1.0f;
		pressed_ = id;
		feedback_.play(hui::audio::Cue::select);
		if (steam_) {
			activateSteam(id);
			return;
		}
		if (payloads_) {
			if (id == T_PAYLOAD_CHECK) outcome = payloadReport_.pass ? Outcome::payload_continue : Outcome::payload_recheck;
			return;
		}
		if (id >= T_LANGUAGE) {
			useLanguage(shown_[id - T_LANGUAGE]);
			save(false);
			proton_say("setup: language %s", language_->tag);
			feedback_.play(hui::audio::Cue::saved);
			go(pw_setup_next(step_, &facts_), true);
			return;
		}
		switch (id) {
		case T_START: go(PW_SETUP_LANGUAGE, true); break;
		case T_CHECK: install(); break;
		case T_STOP: if (engine_) engine_->cancel(); break;
		case T_PREPARE:
			if (!test_) {
				if (FILE *marker = fopen(preparingMarker, "w")) fclose(marker);
			}
			cover();
			this->outcome = Outcome::prepare;
			proton_say("setup: the first Mihawk OS session starts");
			break;
		case T_DONE_STEAM: finish(Outcome::steam_guide, "Install Steam"); break;
		case T_DONE_LIBRARY: finish(Outcome::library, "the library"); break;
		default: break;
		}
	}

	void goSteam(PwSteamPage page, bool animate)
	{
		steamPage_ = page;
		entered_ = animate ? 0.0f : 1.0f;
		if (page == PW_STEAM_PAGE_DONE) countdown_ = 5.0f;
		layout();
		focus_ = targets_.empty() ? T_NONE : targets_.back().id;
		proton_say("steam guide: page %d", (int)page);
	}

	void leaveSteam(const char *why)
	{
		outcome = Outcome::library;
		proton_say("steam guide: leaves (%s)", why);
	}

	void activateSteam(int id)
	{
		switch (id) {
		case T_STEAM_START:
			outcome = Outcome::steam_download;
			proton_say("steam guide: Start");
			goSteam(PW_STEAM_PAGE_DOWNLOAD, true);
			break;
		case T_STEAM_NOT_NOW: leaveSteam("not now"); break;
		case T_BACK: leaveSteam("back"); break;
		case T_STEAM_RETRY:
			proton_say("steam guide: try again after failure %d", (int)failure_);
			if (failure_ <= PW_STEAM_FAIL_INSTALL) {   // the download's and the install's failures: from the download
				outcome = Outcome::steam_download;
				goSteam(PW_STEAM_PAGE_DOWNLOAD, true);
			} else {
				outcome = Outcome::steam_signin;       // the sign-in session's: that session again
			}
			break;
		case T_CONTINUE:
			outcome = Outcome::steam_big_picture;
			proton_say("steam guide: Big Picture opens (Continue)");
			break;
		default: break;
		}
	}

	// What moves by itself: the download's end or failure, the done page's countdown
	void steamTick(float dt)
	{
		if (outcome != Outcome::none || held_) return;
		const SteamFacts &f = steamFacts_;
		if (steamPage_ == PW_STEAM_PAGE_DOWNLOAD && !starting_) {
			if (f.failed && !f.installed) {
				failure_ = pw_steam_guide_fetch_failure(f.fetch_status);
				if (failure_ == PW_STEAM_FAIL_NONE) failure_ = PW_STEAM_FAIL_DOWNLOAD;
				feedback_.play(hui::audio::Cue::error);
				goSteam(PW_STEAM_PAGE_FAILED, true);
			} else if (f.downloaded || f.installed) {
				outcome = f.installed ? Outcome::steam_signin : Outcome::steam_install;
				proton_say("steam guide: %s", f.installed ? "installed; the sign-in session is next" : "downloaded; the install is next");
			}
		}
		if (steamPage_ == PW_STEAM_PAGE_DONE && (countdown_ -= dt) <= 0.0f) {
			outcome = Outcome::steam_big_picture;
			proton_say("steam guide: Big Picture opens (countdown)");
		}
	}

	// Whether every letter of a display name is in the face: otherwise the page says "You're signed in" alone
	bool drawable(const std::string &value) const
	{
		const hui::gfx::Font *face = fonts_.semibold.font;
		if (!face || value.empty()) return false;
		for (size_t i = 0; i < value.size();) {
			const unsigned char c = (unsigned char)value[i];
			const int n = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 0;
			if (!n || i + n > value.size()) return false;
			uint32_t code = n == 1 ? c : n == 2 ? c & 0x1f : n == 3 ? c & 0x0f : c & 0x07;
			for (int k = 1; k < n; ++k) code = code << 6 | ((unsigned char)value[i + k] & 0x3f);
			if (code != ' ' && !face->has_glyph(code)) return false;
			i += n;
		}
		return true;
	}

	// bytes in megabytes to one decimal, in the language's way ("1.2", "1,2")
	void megabytes(unsigned long long bytes, char *out, size_t size) const
	{
		const unsigned long long tenths = (bytes + 50000) / 100000;
		snprintf(out, size, "%llu%s%llu", tenths / 10, text(PW_TEXT_DECIMAL), tenths % 10);
	}

	// The Aero glass tube in the kit's shapes: a frosted track, green liquid to fraction, a highlight on its upper half
	void glassBar(hui::gfx::DrawList &list, const hui::ui::Theme &look, hui::gfx::Rect at, float fraction)
	{
		const float r = at.h * 0.5f;
		list.rounded_rect(at, r, look.surface_high);
		list.bordered_rect(at, r, hui::gfx::Color{ 0, 0, 0, 0 }, 1.5f, look.outline);
		const float w = std::max(at.h, at.w * std::clamp(fraction, 0.0f, 1.0f));
		if (fraction > 0.0f) {
			list.rounded_rect({ at.x + 2, at.y + 2, w - 4, at.h - 4 }, r - 2, hui::gfx::Color::rgb(0x55d52a));
			list.rounded_rect({ at.x + 8, at.y + 5, std::max(0.0f, w - 16), (at.h - 4) * 0.38f }, (at.h - 4) * 0.19f,
				hui::gfx::Color::rgb(0xffffff, 0.45f));
		}
		list.rounded_rect({ at.x + 10, at.y + 3, at.w - 20, 2 }, 1, hui::gfx::Color::rgb(0xffffff, 0.5f));
	}

	void composeSteam(hui::gfx::DrawList &list, hui::ui::Painter &paint, const hui::ui::Theme &look)
	{
		const auto &fonts = fonts_;
		const SteamFacts &f = steamFacts_;
		switch (steamPage_) {
		case PW_STEAM_PAGE_INTRO: {
			card(list, paint, text(PW_TEXT_STEAM_TITLE), text(PW_TEXT_STEAM_BODY));
			// The four stages in a row: a number, a name, a line
			const PwSetupText stages[4][2] = { { PW_TEXT_STEAM_STEP_DOWNLOAD, PW_TEXT_STEAM_STEP_DOWNLOAD_BODY },
				{ PW_TEXT_STEAM_STEP_INSTALL, PW_TEXT_STEAM_STEP_INSTALL_BODY },
				{ PW_TEXT_STEAM_STEP_UPDATE, PW_TEXT_STEAM_STEP_UPDATE_BODY },
				{ PW_TEXT_STEAM_STEP_SIGNIN, PW_TEXT_STEAM_STEP_SIGNIN_BODY } };
			const float gap = 24, w = (kCard.w - 144 - 3 * gap) / 4, y = kCard.y + 236, h = 250;
			for (int i = 0; i < 4; ++i) {
				const hui::gfx::Rect r{ kCard.x + 72 + i * (w + gap), y, w, h };
				paint.surface(r, look.radius_card, look.surface, look.outline, 1.0f);
				list.circle(r.x + 46, r.y + 48, 22, look.primary);
				char number[4];
				snprintf(number, sizeof(number), "%d", i + 1);
				hui::ui::text(list, fonts.semibold, number, r.x + 46, r.y + 57, 24, hui::ui::Painter::on(look.primary),
					hui::gfx::Align::center);
				// The name beside its number; one too long for the line (Polish's first start) takes two at a readable size
				const char *name = text(stages[i][0]);
				const float nameWidth = r.w - 106;
				if (fonts.semibold.measure(name, 26) <= nameWidth)
					hui::ui::text(list, fonts.semibold, name, r.x + 82, r.y + 57, 26, look.text);
				else
					wrapped(list, fonts.semibold, name, r.x + 82, r.y + 44, 21, nameWidth, look.text);
				wrapped(list, fonts.regular, text(stages[i][1]), r.x + 26, r.y + 116, 21, r.w - 52, look.text_muted);
			}
			wrapped(list, fonts.regular, text(PW_TEXT_STEAM_NOTE), kCard.x + 72, y + h + 50, 22, kCard.w - 144, look.text_muted);
			break;
		}
		case PW_STEAM_PAGE_DOWNLOAD: {
			card(list, paint, text(starting_ ? PW_TEXT_STEAM_STARTING : PW_TEXT_STEAM_DOWNLOADING),
				starting_ ? nullptr : text(PW_TEXT_STEAM_DOWNLOADING_BODY));
			glassBar(list, look, { kCard.x + 72, kCard.y + 330, kCard.w - 144, 34 },
				starting_ ? 1.0f : pw_steam_guide_download_fraction(f.bytes));
			if (!starting_) {
				char a[24], b[24], line[96];
				megabytes(f.bytes, a, sizeof(a));
				megabytes(PW_STEAM_INSTALLER_BYTES, b, sizeof(b));
				const char *args[] = { a, b };
				pw_setup_fill(line, sizeof(line), text(PW_TEXT_STEAM_MEGABYTES), args, 2);
				hui::ui::text(list, fonts.semibold, line, kCard.x + 72, kCard.y + 412, 26, look.text);
			}
			break;
		}
		case PW_STEAM_PAGE_FAILED:
			card(list, paint, text(PW_TEXT_STEAM_FAIL_TITLE), nullptr);
			status(list, text((PwSetupText)pw_steam_guide_failure_text(failure_)), look.danger, kCard.y + 230);
			break;
		case PW_STEAM_PAGE_DONE: {
			char title[256], seconds[8], line[96];
			if (drawable(f.persona)) {
				const char *args[] = { f.persona.c_str() };
				pw_setup_fill(title, sizeof(title), text(PW_TEXT_STEAM_SIGNED_IN_AS), args, 1);
			} else {
				snprintf(title, sizeof(title), "%s", text(PW_TEXT_STEAM_SIGNED_IN));
			}
			card(list, paint, title, text(PW_TEXT_STEAM_DONE_BODY));
			snprintf(seconds, sizeof(seconds), "%d", std::max(1, (int)std::ceil(countdown_)));
			const char *args[] = { seconds };
			pw_setup_fill(line, sizeof(line), text(PW_TEXT_STEAM_OPENING_IN), args, 1);
			status(list, line, look.success, kCard.y + 380);
			break;
		}
		}
	}

	// The runtime from the title folder's pieces (the owner, 2026-10-31: one folder to copy): look, and start at once;
	// Try again does the same after a problem
	void install()
	{
		install_ = engine_ ? engine_->find() : SetupInstall{};
		proton_say("setup: title folder check: phase %d problem %d, %llu bytes", (int)install_.phase, (int)install_.problem,
			(unsigned long long)install_.total);
		if (install_.phase == SetupInstall::ready) {
			engine_->start();
			install_ = engine_->state();
			proton_say("setup: install starts");
		} else {
			feedback_.play(hui::audio::Cue::error);
		}
		layout();
		focus_ = targets_.empty() ? T_NONE : targets_.front().id;
	}

	std::string problem() const
	{
		static const PwSetupText keys[] = { PW_TEXT_COUNT, PW_TEXT_ERR_BUILD, PW_TEXT_ERR_NO_PIECES, PW_TEXT_ERR_PARTIAL,
			PW_TEXT_ERR_NO_SPACE, PW_TEXT_ERR_READ, PW_TEXT_ERR_DAMAGED, PW_TEXT_ERR_FULL, PW_TEXT_ERR_WRITE, PW_TEXT_ERR_CANCELLED };
		if (install_.problem == SetupInstall::none) return "";
		char a[32], b[32], out[512];
		if (install_.problem == SetupInstall::partial) {
			snprintf(a, sizeof(a), "%zu", install_.have);
			snprintf(b, sizeof(b), "%zu", install_.pieces);
		} else {
			pw_setup_gigabytes(language_, install_.needed, a, sizeof(a));
			pw_setup_gigabytes(language_, install_.available, b, sizeof(b));
		}
		const char *args[] = { a, b };
		pw_setup_fill(out, sizeof(out), text(keys[install_.problem]), args, 2);
		return out;
	}

	// ---- drawing ----
public:   // the main menu breaks its lines the same way (home_grid.hpp)
	// (the launcher's Steam memory warning draws the same one)
	// A hold's ring around its button's glyph, filled by share; the words before it, the glyph where the text says {1}, with
	// the text's own spaces around it (none before a comma: "Удерживайте ✕, чтобы")
	static void holdRing(hui::gfx::DrawList &list, const hui::ui::Fonts &fonts, const hui::ui::Theme &look, float cx, float cy,
		float share, hui::ui::Button button, const char *value)
	{
		list.ring(cx, cy, 46, 8, look.primary.with_alpha(0.18f));
		if (share > 0.0f) list.arc(cx, cy, 46, 8, -1.5707963f, 6.2831853f * std::min(1.0f, share), look.primary);
		hui::ui::GlyphStyle glyphs = look.dark ? hui::ui::GlyphStyle::dark() : hui::ui::GlyphStyle::light();
		hui::ui::draw_button(list, fonts, glyphs, button, cx - hui::ui::button_width(button, 46) * 0.5f, cy, 46);
		const std::string words = value;
		const size_t at = std::min(words.find("{1}"), words.size());
		const std::string before = words.substr(0, at), after = words.substr(std::min(at + 3, words.size()));
		const float size = 30, glyph = 36, pad = 4, wb = fonts.semibold.measure(before.c_str(), size),
			wa = fonts.semibold.measure(after.c_str(), size), wg = hui::ui::button_width(button, glyph);
		const float x = cx - 46 - 32 - (wb + pad + wg + pad + wa);
		hui::ui::text(list, fonts.semibold, before.c_str(), x, cy + size * 0.36f, size, look.text);
		hui::ui::draw_button(list, fonts, glyphs, button, x + wb + pad, cy, glyph);
		hui::ui::text(list, fonts.semibold, after.c_str(), x + wb + pad + wg + pad, cy + size * 0.36f, size, look.text);
	}

	// Lines no wider than width. Breaks at spaces, and between Chinese and Japanese characters, which have no spaces;
	// closing punctuation stays on the line before it (the kit's own wrap breaks at spaces alone)
	static std::vector<std::string> lines(const hui::ui::FontRef &font, std::string_view text, float size, float width)
	{
		auto wide = [](uint32_t c) { return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF); };
		auto closing = [](uint32_t c) {
			return c == 0x3001 || c == 0x3002 || c == 0xFF0C || c == 0xFF0E || c == 0xFF09 || c == 0x300D || c == 0x300F ||
				c == 0xFF01 || c == 0xFF1F || c == 0xFF1A || c == 0xFF1B || c == 0x2026 || c == 0x201D;
		};
		std::vector<std::string> units; // pieces a line may end after
		enum { none, space, cjk, word } previous = none;
		for (size_t i = 0; i < text.size();) {
			const size_t start = i;
			const uint32_t c = hui::gfx::next_codepoint(text, &i);
			const auto piece = text.substr(start, i - start);
			const auto kind = c == ' ' ? space : wide(c) ? cjk : word;
			// A space or closing punctuation ends the piece before it; a letter goes on its word
			const bool joins = !units.empty() && (kind == space || closing(c) || (kind == word && previous == word));
			if (joins) units.back() += piece;
			else units.emplace_back(piece);
			previous = kind;
		}
		std::vector<std::string> out;
		std::string line;
		for (const auto &unit : units) {
			std::string trial = line + unit;
			std::string_view trimmed(trial);
			while (!trimmed.empty() && trimmed.back() == ' ') trimmed.remove_suffix(1);
			if (!line.empty() && font.measure(trimmed, size) > width) {
				while (!line.empty() && line.back() == ' ') line.pop_back();
				out.push_back(line);
				line = unit;
			} else {
				line = trial;
			}
		}
		while (!line.empty() && line.back() == ' ') line.pop_back();
		if (!line.empty()) out.push_back(line);
		return out;
	}

private:
	float wrapped(hui::gfx::DrawList &list, const hui::ui::FontRef &font, const char *value, float x, float y, float size,
		float width, hui::gfx::Color color, hui::gfx::Align align = hui::gfx::Align::left) const
	{
		for (const auto &line : lines(font, value, size, width)) {
			hui::ui::text(list, font, line, x, y, size, color, align);
			y += size * 1.42f;
		}
		return y;
	}

	// A heading at size, or smaller when it would be wider than width
	static float fitted(const hui::ui::Painter &paint, const char *value, float size, float width)
	{
		const float measured = paint.heading_width(value, size);
		return measured > width ? size * width / measured : size;
	}

	// The checklist: the required payloads left, the optional ones and any other right, each with a glass mark (a tick when
	// it is ready, "!" when it is not, a dot for an optional one), and what to do in a status box beside Check again
	void composePayloads(hui::gfx::DrawList &list, hui::ui::Painter &paint, const hui::ui::Theme &look)
	{
		const auto &fonts = fonts_;
		const PwPayloadReport &r = payloadReport_;
		const PwPayloadFacts &f = payloadFacts_;
		card(list, paint, "Payload check", PS5_TITLE_NAME " runs with these payloads only, so games keep the memory they need.");
		const auto mib = [](uint64_t bytes) { return std::to_string((unsigned)((bytes + (1u << 19)) >> 20)) + " MiB"; };
		const auto row = [&](float x, float y, const char *name, const std::string &state, int mark, hui::gfx::Color tone) {
			list.circle(x + 15, y - 8, 15, tone);
			if (mark > 0) {
				list.line(x + 8, y - 8, x + 13, y - 3, 3.5f, hui::gfx::Color::rgb(0xffffff));
				list.line(x + 13, y - 3, x + 22, y - 14, 3.5f, hui::gfx::Color::rgb(0xffffff));
			} else if (mark < 0) {
				hui::ui::text(list, fonts.semibold, "!", x + 15, y + 1, 24, hui::gfx::Color::rgb(0xffffff), hui::gfx::Align::center);
			}
			hui::ui::text(list, fonts.semibold, name, x + 46, y, 25, look.text);
			hui::ui::text(list, fonts.regular, state, x + 46, y + 26, 20, mark < 0 ? hui::gfx::mix(tone, hui::gfx::Color::rgb(0x1b2430), 0.45f) :
				look.text_muted);
		};
		const float left = kCard.x + 72, right = kCard.x + 72 + 640, top = kCard.y + 240;
		hui::ui::text(list, fonts.semibold, "REQUIRED", left, top, 19, look.text_muted);
		for (int k = 0; k < PW_PAYLOAD_REQUIRED; ++k) {
			const char *version = k == PW_PAYLOAD_SHADOWMOUNT ? f.shadowmount_version : k == PW_PAYLOAD_WEBSRV ? f.websrv_version : "";
			std::string state;
			if (r.ok[k]) state = std::string("Ready") + (*version ? std::string(" \u00b7 ") + version : "");
			else if (!r.running[k] && !r.listed) state = "Not checked: no access to /data yet";
			else if (!r.running[k] && k == PW_PAYLOAD_NANODNS) state = "Not detected: optional, nothing to do";
			else if (!r.running[k]) state = "Missing: load it";
			else state = "Not answering";
			if (r.running[k] && r.listed) state += " \u00b7 " + mib(r.resident[k]);   // a loopback answer has no size
			// not checked, or the optional NanoDNS: a neutral mark, never "missing"
			const bool unseen = (!r.listed || k == PW_PAYLOAD_NANODNS) && !r.running[k];
			row(left, top + 44 + k * 56, pw_payload_label(k), state, r.ok[k] ? 1 : unseen ? 0 : -1,
				r.ok[k] ? look.success : unseen ? look.outline : look.danger);
		}
		hui::ui::text(list, fonts.semibold, "OPTIONAL", right, top, 19, look.text_muted);
		int at = 0;
		for (int k = PW_PAYLOAD_REQUIRED; k < PW_PAYLOAD_KNOWN; ++k, ++at)
			row(right, top + 44 + at * 56, pw_payload_label(k), r.running[k] ? "Running \u00b7 " + mib(r.resident[k]) : r.listed ? "Not loaded" : "Not checked", 0,
				r.running[k] ? look.primary : look.outline);
		// What the kernel lists only as "payload": one warning row, never a guess at what it is
		if (r.unnamed)
			row(right, top + 44 + at++ * 56, (std::to_string(r.unnamed) + (r.unnamed == 1 ? " unknown payload" : " unknown payloads")).c_str(),
				"Shown only as \"payload\" \u00b7 " + mib(r.unnamed_resident), -1, look.warning);
		// Each extra payload by name, as room allows (two rows, one beside an unknown one); more share the last row
		const unsigned room = r.unnamed ? 1 : 2, named = r.others <= room ? r.others : room - 1;
		for (unsigned k = 0; k < named; ++k, ++at)
			row(right, top + 44 + at * 56, r.other_names[k], "Not allowed: close it \u00b7 " + mib(r.other_each[k]), -1, look.danger);
		if (r.others > named) {
			uint64_t rest = r.other_resident;
			for (unsigned k = 0; k < named; ++k) rest -= r.other_each[k];
			row(right, top + 44 + at * 56, (std::to_string(r.others - named) + " more payloads").c_str(),
				"Not allowed: close them \u00b7 " + mib(rest), -1, look.danger);
		}
		const float statusWidth = kCard.w - 144 - 300 - 24;
		// A pass may still carry a warning (no NanoDNS, an unknown payload): it reads as one, not as a failure
		if (!payloadAdvice_.empty())
			status(list, payloadAdvice_.c_str(), r.pass ? look.warning : look.danger, kStatusY + 10, statusWidth);
		else status(list, "Everything " PS5_TITLE_NAME " needs is ready.", look.success, kStatusY + 10, statusWidth);
		// Failing: hold TRIANGLE to go on anyway, the limits page's ring (its look and fill) on the hints' line at the right,
		// clear of the card (a failing check's advice can fill its status box)
		if (pw_payload_skip_offered(&r))
			holdRing(list, fonts_, look, 1920 - 96 - 46, 1010, skipSeconds_ / PW_PAYLOAD_SKIP_SECONDS, hui::ui::Button::triangle,
				text(PW_TEXT_PAYLOAD_SKIP_HOLD));
	}

	void card(hui::gfx::DrawList &list, hui::ui::Painter &paint, const char *title, const char *body)
	{
		paint.panel(kCard);
		paint.heading(title, kCard.x + 72, kCard.y + 112, fitted(paint, title, 52, kCard.w - 144), paint.theme().text);
		if (body) wrapped(list, fonts_.regular, body, kCard.x + 72, kCard.y + 166, 27, 1120, paint.theme().text_muted);
	}

	// A line that reports an outcome: a tinted box, a mark, and the words in the colour darkened toward the ink, so
	// they read at 4.5:1 or better on the box
	void status(hui::gfx::DrawList &list, const char *value, hui::gfx::Color color, float y, float width = kCard.w - 144)
	{
		const auto &fonts = fonts_;
		const float textWidth = width - 92;
		const int lines = (int)this->lines(fonts.semibold, value, 25, textWidth).size();
		const hui::gfx::Rect box{ kCard.x + 72, y - 34, width, 44 + lines * 25 * 1.42f };
		list.rounded_rect(box, 12, color.with_alpha(0.12f));
		list.bordered_rect(box, 12, hui::gfx::Color{ 0, 0, 0, 0 }, 1.5f, color.with_alpha(0.55f));
		list.circle(box.x + 38, box.y + 40, 15, color);
		const bool good = color.g > color.r;
		if (good) {
			list.line(box.x + 31, box.y + 40, box.x + 36, box.y + 45, 3.5f, hui::gfx::Color::rgb(0xffffff));
			list.line(box.x + 36, box.y + 45, box.x + 45, box.y + 34, 3.5f, hui::gfx::Color::rgb(0xffffff));
		} else {
			hui::ui::text(list, fonts.semibold, "!", box.x + 38, box.y + 49, 24, hui::gfx::Color::rgb(0xffffff), hui::gfx::Align::center);
		}
		wrapped(list, fonts.semibold, value, box.x + 70, box.y + 49, 25, textWidth, hui::gfx::mix(color, hui::gfx::Color::rgb(0x1b2430), 0.45f));
	}

	// Where the step stands among the setup's six on the counter: Welcome, Language, the install, Preparing, Before you
	// play, Done
	int stepIndex() const
	{
		return step_ == PW_SETUP_WELCOME ? 0 : step_ == PW_SETUP_LANGUAGE ? 1 : step_ == PW_SETUP_INSTALLING ? 2 :
			step_ == PW_SETUP_PREPARING ? 3 : step_ == PW_SETUP_LIMITS ? 4 : 5;
	}

	// The setup's six steps (stepIndex), or the Steam guide's four stages
	void progressDots(hui::gfx::DrawList &list, hui::ui::Painter &paint, const hui::ui::Theme &look, int index = -1,
		int count = 6)
	{
		if (index < 0) index = stepIndex();
		char a[4], b[4], line[64];
		snprintf(a, sizeof(a), "%d", index + 1);
		snprintf(b, sizeof(b), "%d", count);
		const char *args[] = { a, b };
		pw_setup_fill(line, sizeof(line), text(PW_TEXT_STEP_OF), args, 2);
		hui::ui::text(list, fonts_.semibold, line, 1824, 92, 24, paint.page_text_muted(), hui::gfx::Align::right);
		float x = 1824;
		for (int i = count - 1; i >= 0; --i) {
			const float w = i == index ? 30.0f : 10.0f;
			x -= w;
			list.rounded_rect({ x, 112, w, 10 }, 5, i == index ? look.primary : i < index ? look.primary.with_alpha(0.45f) : paint.page_text_muted().with_alpha(0.35f));
			x -= 10;
		}
	}

	void drawTarget(hui::gfx::DrawList &list, hui::ui::Painter &paint, const hui::ui::Theme &look, const Target &t)
	{
		const float focus = glow_.count(t.id) ? glow_.at(t.id) : 0.0f;
		const hui::ui::Look state{ focus, t.id == pressed_ ? press_ : 0.0f, t.disabled };
		const auto &fonts = fonts_;
		auto button = [&](const char *label, hui::ui::ButtonKind kind, float size) {
			paint.button(t.rect, "", kind, state);
			const auto color = kind == hui::ui::ButtonKind::primary ? look.on_primary : kind == hui::ui::ButtonKind::ghost ? look.primary : look.on_secondary;
			hui::ui::text(list, fonts.semibold, label, t.rect.cx(), t.rect.cy() + size * 0.36f, size, color, hui::gfx::Align::center);
		};
		if (t.id >= T_LANGUAGE) {
			const int index = t.id - T_LANGUAGE;
			const bool big = shown_.size() <= 4;
			const float lift = focus * 6.0f;
			const hui::gfx::Rect r{ t.rect.x, t.rect.y - lift, t.rect.w, t.rect.h };
			paint.surface(r, look.radius_card, look.surface, look.outline, 1.0f);
			const float fw = big ? 192 : r.w * 0.62f, fh = fw * 0.75f;
			const hui::gfx::Rect flag{ r.cx() - fw * 0.5f, r.y + (big ? 30 : 20), fw, fh };
			list.shadow({ flag.x, flag.y + 2, flag.w, flag.h }, 8, 6, look.shadow.with_alpha(0.3f));
			if (flags_[index]) list.image(flags_[index], flag, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff), 8);
			list.bordered_rect(flag, 8, hui::gfx::Color{ 0, 0, 0, 0 }, 1.0f, look.outline.with_alpha(0.35f));
			const auto own = shown_[index]->font && faceRefs_.count(shown_[index]->font) ? faceRefs_.at(shown_[index]->font) : kit_->fonts.semibold;
			hui::ui::text(list, own, shown_[index]->name, r.cx(), flag.y + fh + (big ? 46 : 34), big ? 28 : 22, look.text,
				hui::gfx::Align::center);
			if (shown_[index] == language_) {
				list.circle(r.x + r.w - 26, r.y + 26, 15, look.accent);
				list.line(r.x + r.w - 33, r.y + 26, r.x + r.w - 28, r.y + 31, 3.5f, hui::gfx::Color::rgb(0xffffff));
				list.line(r.x + r.w - 28, r.y + 31, r.x + r.w - 19, r.y + 20, 3.5f, hui::gfx::Color::rgb(0xffffff));
			}
			paint.focus_ring(r, look.radius_card, focus);
			return;
		}
		switch (t.id) {
		case T_START: button(text(PW_TEXT_GET_STARTED), hui::ui::ButtonKind::primary, 30); break;
		case T_CONTINUE: button(text(PW_TEXT_CONTINUE), hui::ui::ButtonKind::primary, 26); break;
		case T_CHECK: button(text(PW_TEXT_RETRY), hui::ui::ButtonKind::primary, 26); break;
		case T_STOP: button(text(PW_TEXT_STOP), hui::ui::ButtonKind::secondary, 26); break;
		case T_BACK: button(text(PW_TEXT_HINT_BACK), hui::ui::ButtonKind::secondary, 26); break;
		case T_PREPARE: button(text(PW_TEXT_PREPARE_START), hui::ui::ButtonKind::primary, 26); break;
		case T_STEAM_START: button(text(PW_TEXT_STEAM_START), hui::ui::ButtonKind::primary, 26); break;
		case T_STEAM_NOT_NOW: button(text(PW_TEXT_STEAM_NOT_NOW), hui::ui::ButtonKind::secondary, 26); break;
		case T_STEAM_RETRY: button(text(PW_TEXT_RETRY), hui::ui::ButtonKind::primary, 26); break;
		case T_PAYLOAD_CHECK: {
			char label[32];
			snprintf(label, sizeof(label), "Continue (%d)", std::max(1, (int)std::ceil(payloadCountdown_)));
			button(payloadReport_.pass ? label : "Check again", hui::ui::ButtonKind::primary, 26);
			break;
		}
		case T_DONE_STEAM:
		case T_DONE_LIBRARY: {
			// Two in a row: the icon and the name on top, a short line under them
			const auto &r = t.rect;
			paint.surface(r, look.radius_card, look.surface, look.outline, 1.0f - (t.id == pressed_ ? press_ : 0.0f));
			const float cx = r.x + 66, cy = r.y + 68, k = 0.8f;   // the icons, drawn at 0.8 of their first size
			list.circle(cx, cy, 44, look.primary.with_alpha(0.12f));
			if (t.id == T_DONE_STEAM) {
				// A download: an arrow into a tray
				list.rounded_rect({ cx - 4 * k, cy - 32 * k, 8 * k, 36 * k }, 4 * k, look.primary);
				list.line(cx - 16 * k, cy - 6 * k, cx, cy + 10 * k, 7 * k, look.primary);
				list.line(cx + 16 * k, cy - 6 * k, cx, cy + 10 * k, 7 * k, look.primary);
				list.line(cx - 28 * k, cy + 14 * k, cx - 28 * k, cy + 30 * k, 5 * k, look.primary);
				list.line(cx - 28 * k, cy + 30 * k, cx + 28 * k, cy + 30 * k, 5 * k, look.primary);
				list.line(cx + 28 * k, cy + 30 * k, cx + 28 * k, cy + 14 * k, 5 * k, look.primary);
			} else {
				// The library: four game tiles
				for (int i = 0; i < 4; ++i)
					list.rounded_rect({ cx + (i % 2 ? 3 : -27) * k, cy + (i / 2 ? 3 : -27) * k, 24 * k, 24 * k }, 5 * k, look.primary);
			}
			const PwSetupText word[2] = { t.id == T_DONE_STEAM ? PW_TEXT_DONE_STEAM : PW_TEXT_DONE_LIBRARY,
				t.id == T_DONE_STEAM ? PW_TEXT_DONE_STEAM_BODY : PW_TEXT_DONE_LIBRARY_BODY };
			const char *name = text(word[0]);
			const float nameWidth = r.w - 150, measured = fonts.semibold.measure(name, 32);
			hui::ui::text(list, fonts.semibold, name, r.x + 128, r.y + 80, measured > nameWidth ? 32 * nameWidth / measured : 32, look.text);
			wrapped(list, fonts.regular, text(word[1]), r.x + 32, r.y + 150, 21, r.w - 64, look.text_muted);
			paint.focus_ring(r, look.radius_card, focus);
			break;
		}
		default: break;
		}
	}

	void hints(hui::gfx::DrawList &list, const hui::ui::Theme &look)
	{
		std::vector<hui::ui::Hint> row;
		if (targets_.size() > 1) row.push_back({ hui::ui::Button::dpad, text(PW_TEXT_HINT_MOVE) });
		if (!targets_.empty()) row.push_back({ hui::ui::Button::cross, text(PW_TEXT_HINT_SELECT) });
		if (payloads_) {
			row.push_back({ hui::ui::Button::circle, "Close " PS5_TITLE_NAME });
		} else if (steam_) {
			if (steamPage_ != PW_STEAM_PAGE_DOWNLOAD) row.push_back({ hui::ui::Button::circle, text(PW_TEXT_HINT_BACK) });
		} else {
			if (pw_setup_back(step_) != PW_SETUP_NONE && !busy()) row.push_back({ hui::ui::Button::circle, text(PW_TEXT_HINT_BACK) });
		}
		hui::ui::GlyphStyle style = look.dark ? hui::ui::GlyphStyle::dark() : hui::ui::GlyphStyle::light();
		style.label = hui::ui::Painter(list, fonts_, look).page_text(); // full ink: the row lies on the green hill
		hui::ui::HintLayout layout;
		layout.size = 34;
		layout.text_size = 22;
		layout.cy = 1022;
		layout.item_gap = 38;
		hui::ui::draw_hints(list, fonts_, style, row.data(), (int)row.size(), 96, false, layout);
	}

	ps5ui::Kit *kit_{ nullptr };
	hui::ui::Fonts fonts_;                                        // the chosen language's: the kit's, or its own face
	std::deque<hui::gfx::Font> faces_;                            // the CJK faces (kept where they are: FontRefs point in)
	std::unordered_map<std::string, hui::ui::FontRef> faceRefs_;
	std::unique_ptr<SetupEngine> engine_;
	PwSetupFacts facts_{};
	PwSetupStep step_{ PW_SETUP_WELCOME };
	const PwSetupLanguage *language_{ nullptr };
	std::vector<const PwSetupLanguage *> shown_;
	std::vector<uint32_t> flags_;
	std::vector<Target> targets_;
	std::unordered_map<int, float> glow_;
	hui::ui::Feedback feedback_;
	SetupInstall install_;
	int focus_{ T_NONE }, pressed_{ T_NONE };
	float clock_{ 0.0f }, entered_{ 1.0f }, press_{ 0.0f }, doneAt_{ 0.0f };
	float holdSeconds_{ 0.0f };   // the limits page's hold (pw_setup_hold)
	float skipSeconds_{ 0.0f };   // the payload checklist's hold through a fail (pw_payload_skip_hold)
	bool skipArmed_{ false };     // triangle was up since the page showed
	bool holdArmed_{ false };     // cross was up since the page showed
	float pointerX_{ 960.0f }, pointerY_{ 540.0f }, pointerShown_{ 0.0f }, lastTouchX_{ 0.0f }, lastTouchY_{ 0.0f };
	bool touching_{ false }, prepareFailed_{ false }, simulated_{ false }, test_{ false }, cover_{ false };
	std::string assets_;                                          // the title's assets folder (the faces, the flags)
	// The Steam guide's mode
	bool steam_{ false }, starting_{ false }, held_{ false };
	PwSteamPage steamPage_{ PW_STEAM_PAGE_INTRO };
	PwSteamFailure failure_{ PW_STEAM_FAIL_NONE };
	SteamFacts steamFacts_;
	float countdown_{ 0.0f };                                     // the done page's seconds to Big Picture
	// The payload checklist's mode
	bool payloads_{ false }, payloadArmed_{ false };
	static constexpr float kPayloadContinueS = 3.0f;
	float payloadCountdown_{ kPayloadContinueS };                 // a passing page's seconds before it continues
	PwPayloadReport payloadReport_{};
	PwPayloadFacts payloadFacts_{};
	std::string payloadAdvice_;
};
