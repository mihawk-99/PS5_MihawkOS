/*
 * PS5 Vulkan Template - the title: the menu, its programs, and test runs.
 *
 * A launch by hand shows the menu (launcher.cpp) and runs the sample chosen
 * until OPTIONS is pressed, then shows the menu again. A title with one
 * program (src/samples.cpp) runs it at once instead, until it ends itself.
 *
 * A test run is a launch that finds /app0/test-run.txt, which ps5/tools/run.sh
 * writes just before it and the launch deletes:
 *
 *   frames 300                   frames each sample draws
 *   screenshot                   save each sample's last frame in /app0/screenshots/<id>.ppm
 *   samples all                  every sample linked in ("menu": the menu's; or ids;
 *                                "launcher": the menu itself, drawn for the budget)
 *
 * Each sample then runs in turn on a device of its own, and klog gets one line
 * for each ("sample <id>: ok, ..." or "sample <id>: FAILED, ...") and a summary.
 * The same lines go to /app0/test-results.txt as they happen, for a run with
 * no klog capture (ps5/tools/run.sh falls back to it).
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 * SPDX-License-Identifier: MIT
 */

#include "vulkanexamplebase.h"
#include "proton_launch.h"
#include "proton_update.h"
#include "ps5_samples.h"
#include "../../src/pw_first_run.h"
#include "../../src/pw_detailed_logs.h"
#include "../../src/pw_log_sessions.h"
#if !defined(PS5_HOST_REFERENCE)
#include "../../native/proton_wine.h"
#include "../../native/pw_usb_input_ps5.h"
#if defined(PS5_CHILD_PROBE)
#include "../../native/proton_child_probe.h"
#endif
#if defined(PS5_TESTER_PROBE)
#include "../../native/proton_tester_probe.h"
#endif
#if defined(PS5_SERVICE_PREFLIGHT) || defined(PS5_SERVICE_NATIVE)
#include "../../native/proton_service_preflight.h"
#endif
#if defined(PS5_INSTALL_PROBE)
#include "../../native/proton_install_probe.h"
#endif
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sstream>

#if !defined(PS5_HOST_REFERENCE)
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
#endif

/* The title's folder: /app0 on the console, a build folder in the host reference build */
#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

std::vector<Ps5Press> ps5Presses;
bool ps5TestRun = false;

namespace {

/* Resolved at run time: after /data is granted the title's own folder is no
 * longer /app0 but the sandbox's view of it (proton_title_path). */
std::string test_run_path() { return proton_title_path(PS5_APP_ROOT "/test-run.txt"); }
std::string results_path() { return proton_title_path(PS5_APP_ROOT "/test-results.txt"); }
FILE *results = nullptr;

/* A test run's line: to klog, and to the results file, flushed at once so a
 * run that crashes keeps what came before. */
__attribute__((format(printf, 1, 2))) void report(const char *format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	say("%s", line);
	if (results) {
		fprintf(results, "%s\n", line);
		fflush(results);
	}
}
std::string screenshot_dir() { return proton_title_path(PS5_APP_ROOT "/screenshots"); }

struct TestRun {
	uint32_t frames{ 0 };
	std::string identity;
	bool screenshot{ false };
	std::vector<std::string> samples;
};

bool readTestRun(TestRun &run)
{
	std::ifstream file(test_run_path());
	if (!file) {
		return false;
	}
	std::string line;
	while (std::getline(file, line)) {
		std::istringstream words(line);
		std::string key;
		if (!(words >> key)) {
			continue;
		}
		if (key == "frames") {
			words >> run.frames;
		} else if (key == "identity") {
			words >> run.identity;
		} else if (key == "screenshot") {
			run.screenshot = true;
		} else if (key == "press") {
			Ps5Press press;
			if (words >> press.program >> press.frame >> press.action) ps5Presses.push_back(press);
		} else if (key == "hold") {   // hold <program> <first frame> <last frame> <action>
			Ps5Press press;
			if (words >> press.program >> press.frame >> press.until >> press.action) ps5Presses.push_back(press);
		} else if (key == "samples") {
			for (std::string id; words >> id;) {
				run.samples.push_back(id);
			}
		} else {
			say("test run: unknown line \"%s\"", line.c_str());
		}
	}
	file.close();
	remove(test_run_path().c_str());
	if (run.frames == 0) {
		run.frames = 300;
	}
	return true;
}

struct Result {
	bool ok{ false };
	uint32_t frames{ 0 };
	double seconds{ 0.0 };
	double steadyFps{ 0.0 };
	std::string error;
};

double now()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* One sample from start to end, on an instance and a device of its own. A
 * fatal error in it (vks::tools::exitFatal) arrives here as an exception: the
 * sample is ended and the title carries on. */
Result runSample(const Ps5Sample &sample, uint32_t frameBudget, const std::string &screenshotPath,
	bool optionsEnds = true)
{
	Result result;
	VulkanExampleBase *example = nullptr;
	report("sample %s: starts", sample.id);
	const double start = now();
	try {
		example = sample.create();
		// A test run is deterministic: upstream's samples seed their random
		// generators with 0 when benchmark.active is set, and the render loop
		// steps time by 1/60 s a frame (base/vulkanexamplebase.cpp)
		example->benchmark.active = frameBudget > 0;
		example->ps5.frameBudget = frameBudget;
		example->ps5.screenshotPath = screenshotPath;
		example->ps5.optionsEnds = optionsEnds;
		if (!example->initVulkan()) {
			throw std::runtime_error("initVulkan failed");
		}
		example->prepare();
		const double loopStart = now();
		example->renderLoop();
		const double end = now();
		result.frames = example->ps5.framesDrawn;
		result.seconds = end - start;
		// The frame rate once the first frames (pipeline compiles, uploads) are
		// behind it, up to the last frame (whose screenshot takes its time)
		const auto &times = example->ps5;
		if (times.halfwayTime > 0.0 && times.lastFrameTime > times.halfwayTime) {
			result.steadyFps = (frameBudget - 1 - frameBudget / 2) / (times.lastFrameTime - times.halfwayTime);
		} else if (end > loopStart) {
			result.steadyFps = result.frames / (end - loopStart);
		}
		result.ok = true;
	} catch (const std::exception &e) {
		result.error = e.what();
		result.seconds = now() - start;
	}
	if (example) {
		if (example->vulkanDevice) {
			vkDeviceWaitIdle(example->vulkanDevice->logicalDevice);
		}
		delete example;
	}
	if (result.ok) {
		report("sample %s: ok, %u frames in %.2f s, %.1f fps after the first half", sample.id, result.frames,
			result.seconds, result.steadyFps);
	} else {
		report("sample %s: FAILED after %.2f s: %s", sample.id, result.seconds, result.error.c_str());
	}
	if (result.ok && (!frameBudget || proton_setup_unattended(true) || proton_restart_pending())) proton_finish_restart();
	return result;
}

int runTests(const TestRun &run)
{
	std::vector<const Ps5Sample *> chosen;
	const bool launcher = std::find(run.samples.begin(), run.samples.end(), "launcher") != run.samples.end();
	for (const std::string &id : run.samples) {
		for (size_t i = 0; i < ps5SampleCount; i++) {
			if (id == "all" || (id == "menu" && ps5Samples[i].inMenu) || id == ps5Samples[i].id) {
				chosen.push_back(&ps5Samples[i]);
			}
		}
	}
	if (chosen.empty() && !launcher) {
		say("test run: no sample matches");
		return 1;
	}
	if (run.screenshot) {
		mkdir(screenshot_dir().c_str(), 0777);
		chmod(screenshot_dir().c_str(), 0777);
		// No picture of an earlier run may pass for this one's
		for (const Ps5Sample *sample : chosen) {
			remove((screenshot_dir() + "/" + sample->id + ".ppm").c_str());
		}
	}
	results = fopen(results_path().c_str(), "w");
	if (results) {
		chmod(results_path().c_str(), 0666);
	}
	if (!run.identity.empty()) report("test run identity: %s", run.identity.c_str());
	report("test run: %zu samples, %u frames each%s", chosen.size() + (launcher ? 1 : 0), run.frames,
		run.screenshot ? ", a screenshot of each" : "");
	int failed = 0;
	if (launcher) {
		// The menu as a launch by hand shows it, ended by the budget instead of the pad
		report("sample launcher: starts");
		const double start = now();
		const std::string screenshot = run.screenshot ? screenshot_dir() + "/launcher.ppm" : "";
		if (run.screenshot) {
			remove(screenshot.c_str());
		}
		ps5_run_launcher(-1, "", run.frames, screenshot);
		report("sample launcher: ok, %u frames in %.2f s", run.frames, now() - start);
	}
	for (const Ps5Sample *sample : chosen) {
		const std::string screenshot = run.screenshot ? screenshot_dir() + "/" + sample->id + ".ppm" : "";
		if (!runSample(*sample, run.frames, screenshot).ok) {
			failed++;
		}
	}
	report("samples: %zu ok, %d failed", chosen.size() + (launcher ? 1 : 0) - failed, failed);
	return failed ? 1 : 0;
}

void runMenu()
{
	int selected = -1;
	std::string message;
	for (;;) {
		const int chosen = ps5_run_launcher(selected, message);
		if (chosen < 0) {
			return;
		}
		selected = chosen;
		const Result result = runSample(ps5Samples[chosen], 0, "");
		message = result.ok ? "" : std::string(ps5Samples[chosen].title) + " ended: " + result.error;
	}
}

} // namespace

// PROTON_PT_EXIT=<stage> on the launch line (a probe, never a default; docs/PLAN.md "Memory plan", Part 2): the title
// ends at that stage, to find which step of a session leaves the system's page-table count lower than it found it.
// The env= words come from the one-shot wine-run.txt, read only after these stages and never in argv: peek at it.
static void pt_exit_at(const char *stage)
{
	const std::string wanted = std::string("env=PROTON_PT_EXIT=") + stage;
	const std::string request = proton_title_path("/app0/wine-run.txt");
	std::ifstream file(request);
	for (std::string line; std::getline(file, line); )
		if (line == wanted) {
			file.close();
			remove(request.c_str());   // consumed, as a session consumes it, so it cannot replay on the next start
			say("page-table probe: the title ends at stage %s", stage);
			_exit(0);
		}
}

int main(int argc, char **argv)
{
	// The launch contract: no arguments shows the launcher, a game runs it
	proton_set_args(argc, argv);
	// Before /data is granted nothing of the title's say() lines is kept (klog does not carry them on every boot): this
	// file in the title folder shows where a title that restarts itself before /data exists goes.
	const int earlyLog = open(proton_title_path("/app0/title-early.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
	if (earlyLog >= 0) platform_say_file(earlyLog);
	say("title start: argc=%d uid=%d", argc, (int)getuid());
	for (int i = 1; i < argc; i++) say("  arg %d: %s", i, argv[i] ? argv[i] : "(null)");
	pt_exit_at("start");
#if defined(PS5_TESTER_PROBE) && !defined(PS5_HOST_REFERENCE)
	// The tester probe, title PPSA99171 (native/proton_tester_probe.h): it asks for the /data grant once itself and
	// leaves before dev.conf, the data folder and the retries below, which are MihawkOS's.
	const int testerElevation = proton_tester_probe_elevate();   // first: the daemon refuses a process with threads
	platform_init(PS5_TITLE_NAME);
	return proton_tester_probe_main(testerElevation);
#endif
	// /data is not visible to a title. The Lapy owned-root daemon grants it to a
	// process that asks (the platform layer's ps5_elevation_request: the request
	// file, then a real write and read under /data as the proof). This is the
	// process's first act, because the daemon refuses a target that has more than
	// one thread (PS5-Lapy-JB-Daemon source/owned_root_daemon.c,
	// private_process_credential: count != 1 -> EBUSY) and platform_init below
	// starts the klog reader thread. No daemon, no /data: the title then keeps to
	// its own folder and says so.
	bool retried = false;
	for (int i = 1; i < argc; i++) retried = retried || (argv[i] && !strcmp(argv[i], "elevation-retry=1"));
	// LoadExec does not always carry argv across (2026-10-13: argc was 1 in every restart, and a title whose daemon never
	// answered restarted itself without end), so the retry is also marked by a file in the title folder, consumed here.
	{
		const std::string marker = proton_title_path("/app0/elevation-retry");
		if (access(marker.c_str(), F_OK) == 0) {
			retried = true;
			remove(marker.c_str());
		}
	}
#if defined(PS5_INSTALL_PROBE) && !defined(PS5_HOST_REFERENCE)
	proton_install_probe_preroot();
#endif
#if !defined(PS5_HOST_REFERENCE)
	// The title sets itself up before it asks for root (src/pw_first_run.h): dev.conf for the daemon it may start, the
	// data folder, whose presence decides how it asks, and its logs/, where the daemon keeps logs/lapy-service.log. The
	// daemon no longer needs either: without them it runs unlogged (platform/lapy/optional-log-channel.patch).
	const int devConf = pw_first_run_dev_conf("/data/homebrew/dev.conf");
	const int dataFolder = mkdir(PROTON_DATA_ROOT, 0777) == 0 ? 0 : errno;
	const int logsFolder = mkdir(PROTON_DATA_ROOT "/logs", 0777) == 0 ? 0 : errno;
	// The keyboard and mouse modules load before the grant: after it, a standard PS5 on 13.60 refused the load
	// (0x80020063, a tester's report 2026-10-24); loaded first, the session read both devices there.
	pw_usb_input_ps5_preload([](const char *line) { say("%s", line); });
#endif
	pt_exit_at("preload");
	const bool data = proton_data_grant(retried);
	say("elevation: granted=%d status=%s uid=%d note=%s", (int)data, proton_data_status(), (int)getuid(), proton_data_note());
	pt_exit_at("granted");
#if !defined(PS5_HOST_REFERENCE)
	const auto folder = [](int error) { return error == 0 ? "made" : error == EEXIST ? "present" : strerror(error); };
	say("first run: dev.conf %s (%d), %s %s, %s/logs %s", devConf == PW_FIRST_RUN_WRITTEN ? "written" :
		devConf == PW_FIRST_RUN_OTHER ? "holds other settings, kept" : devConf == PW_FIRST_RUN_PRESENT ? "present" :
		"not written", devConf, PROTON_DATA_ROOT, folder(dataFolder), PROTON_DATA_ROOT, folder(logsFolder));
#endif
	proton_retry_elevation(argc, argv);
#if defined(PS5_SERVICE_PREFLIGHT) && !defined(PS5_HOST_REFERENCE)
	int serviceStatus = 0;
	if (proton_service_preflight(data, serviceStatus)) {
		platform_init(PS5_TITLE_NAME);
		say("service preflight ends: status %d; results in the title folder", serviceStatus);
		return serviceStatus;
	}
#endif
#if defined(PS5_SERVICE_NATIVE) && !defined(PS5_HOST_REFERENCE)
	int serviceNativeStatus = 0;
	if (proton_service_native(data, serviceNativeStatus)) {
		platform_init(PS5_TITLE_NAME);
		say("service native probe ends: status %d; results in the title folder", serviceNativeStatus);
		return serviceNativeStatus;
	}
#endif
#if defined(PS5_CHILD_PROBE) && !defined(PS5_HOST_REFERENCE)
	int childProbeStatus = 0;
	if (proton_child_probe(data, childProbeStatus)) {
		platform_init(PS5_TITLE_NAME);
		say("child probe ends: status %d; results in the title folder", childProbeStatus);
		return childProbeStatus;
	}
#endif
	platform_init(PS5_TITLE_NAME);
	pt_exit_at("platform");
	// The samples report on std::cout and std::cerr: both reach klog
	std::cout.rdbuf(std::cerr.rdbuf());
#if !defined(PS5_HOST_REFERENCE)
	say("elevation: %s%s", proton_data_status(), data ? " (filesystem access)" : " (sandbox only)");
	if (*proton_data_note()) {
		say("elevation: %s", proton_data_note());
	}
	// Elevation leaves the effective group apart from the real one, and Mesa turns
	// RADV's disk cache off for a process whose real and effective ids differ.
	if (data && getegid() != getgid() && setegid(getgid()) != 0) {
		say("elevation: effective group not matched; RADV's disk cache stays off");
	}
#endif
	// RADV's disk cache: its default is /app0, which an elevated process does not
	// have ("Failed to create /app0 for shader cache ... disabling"), so point it
	// at this title's own folder. Set before the instance exists, as RADV switches
	// must be, and only when /data is reachable.
	if (data) {
		const std::string cache = std::string(proton_app_root()) + "/radv-shader-cache";
		mkdir(cache.c_str(), 0777);
		chmod(cache.c_str(), 0777);
		if (setenv("MESA_SHADER_CACHE_DIR", cache.c_str(), 1) != 0) {
			say("shader cache: could not point RADV at %s", cache.c_str());
		}
	}
	say("data: %s", data ? "granted" : "not granted, the title's own folder only");
#if defined(PS5_INSTALL_PROBE) && !defined(PS5_HOST_REFERENCE)
	int installProbeStatus = 0;
	if (proton_install_probe(data, installProbeStatus)) {
		say("install probe ends: status %d; results in the title folder", installProbeStatus);
		return installProbeStatus;
	}
#endif
	// The data folder's layout, so a console the PC never set up has it too (docs/SELF_INSTALL.md, phase 1).
	// "" and "/logs" are made before root too, but only where the title sees /data then (src/pw_first_run.h).
	if (data)
		for (const char *folder : { "", "/logs", "/prefixes", "/games", "/caches", "/run" })
			mkdir((std::string(PROTON_DATA_ROOT) + folder).c_str(), 0777);
	// Settings > Logging (src/pw_detailed_logs.h): the folder the last session began gets that session's logs now, before
	// a session moves them, so a session that crashed or froze the console is covered too.
	if (data) {
		char folder[PW_DETAILED_LOGS_PATH];
		if (!pw_detailed_logs_pending(PW_DETAILED_LOGS_ROOT, folder, sizeof(folder))) {
			const char *sources[] = { PROTON_DATA_ROOT "/logs", PW_LOG_SESSIONS_ROOT "/0", PROTON_DATA_ROOT "/setup.txt" };
			unsigned copied = 0, left = 0;
			const int status = pw_detailed_logs_fill(folder, sources, 3, &copied, &left);
			say("Logging: the last session's folder %s: %u files copied, %u left out (status %d)", folder, copied, left,
				status);
		}
	}
	// A staged update is switched in before anything reads the title folder, then the title restarts into it
	// (proton_update.h). It returns when there is none, or the switch stopped (the old title goes on and says why).
	if (data) proton_update_on_start(argc, argv);
	// The foundation already supports a runtime resource root. /app0 remains
	// valid without elevation; after a grant the same files have a different path.
	vks::tools::resourcePath = proton_app_root();
	// The launcher and console harness share this one-shot handoff. Consume it
	// even when LoadExec preserved argv, so it cannot replay on the next launch.
	{
		const auto requestPath = proton_title_path("/app0/wine-run.txt");
		std::ifstream request(requestPath);
		if (request) {
			std::vector<std::string> words{ "ps5proton" };
			for (std::string line; std::getline(request, line); )
				if (!line.empty()) words.push_back(line);
			request.close();
			remove(requestPath.c_str());
			std::vector<char *> args;
			for (auto &word : words) args.push_back(word.data());
			proton_set_args((int)args.size(), args.data());
		}
	}
	pad_open();
	// Which build this is, at the top of the title's own log (the launcher's as much as a game's).
	say("Build: %s", proton_build_identity().c_str());
#if !defined(PS5_HOST_REFERENCE)
	if (proton_launch().game && !proton_selftest_active()) proton_run_wine();
#endif
#if defined(PS5_HOST_REFERENCE)
	// The PC's Vulkan loader
	if (volkInitialize() != VK_SUCCESS) {
		say("no Vulkan loader");
		return 1;
	}
#else
	// Vulkan's commands come from the RADV linked into the title, through volk
	volkInitializeCustom(vk_icdGetInstanceProcAddr);
#endif

	int status = 0;
	TestRun run;
	if (readTestRun(run)) {
		ps5TestRun = true;
		status = runTests(run);
	} else if (ps5SampleCount == 1) {
		// A title with one program: no menu, and the title ends with the program
		status = runSample(ps5Samples[0], 0, "", false).ok ? 0 : 1;
	} else {
		runMenu();
	}
	if (results) {
		report("ends: status %d", status);
		fclose(results);
	} else {
		say("ends: status %d", status);
	}
	return status;
}
