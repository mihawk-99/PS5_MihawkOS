# PS5 Vulkan Template - the title's build (ps5/tools/build.sh drives it).
#
# Configured with the payload SDK's toolchain (toolchain/prospero.cmake), as
# PS5_Vulkan builds the CTS for the console. It compiles the base class, the
# samples listed in src/samples.cpp (each in a namespace of its own), the
# launcher and the third-party code into objects, then tools/link-title.sh
# links them with RADV and packages the title folder.
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(ps5_vulkan_samples C CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The title's name, from its identity: klog's prefix and the menu's heading
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/param.json param_json)
string(JSON PS5_TITLE_NAME GET "${param_json}" localizedParameters en-US titleName)
# The title id: the sandbox path of the title's own eboot, for the reload (proton_launch.cpp)
string(JSON PS5_TITLE_ID GET "${param_json}" titleId)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sce_sys/param.json)
set(PS5_VULKAN_DIR "${ROOT}/../PS5_Vulkan" CACHE PATH "the PS5_Vulkan checkout (RADV archive, link recipe, packaging)")
# PS5_PAYLOAD_SDK: the SDK the toolchain file came from (tools/setup-sdk.sh's pin)
# The host reference build: the same code on the PC's Vulkan driver, with a
# headless surface, reading and writing in PS5_HOST_APP_ROOT (tools/host-reference.sh)
option(PS5_HOST_REFERENCE "build for the PC, as a reference for the console's pictures" OFF)
option(PS5_CHILD_PROBE "include the experimental early native child probe" OFF)
option(PS5_SERVICE_PREFLIGHT "include service queries and rejected requests; no spawning" OFF)
option(PS5_GPU_BROKER "include the GPU broker that lets Wine programs in helper processes draw (the launcher layout)" ON)
option(PS5_SERVICE_NATIVE "include the isolated service-owned native helper test" OFF)
option(PS5_INSTALL_PROBE "include the self-install probe round (docs/SELF_INSTALL.md phase 0)" OFF)
option(PS5_TESTER_PROBE "the tester probe title PPSA99171 (native/proton_tester_probe.h); needs PS5_SERVICE_NATIVE" OFF)
set(PS5_HOST_APP_ROOT "${CMAKE_BINARY_DIR}/app" CACHE PATH "the host reference build's title folder")

# The samples: one SAMPLE(id, ...) line each in src/samples.cpp
file(READ src/samples.cpp samples_cpp)
string(REGEX MATCHALL "\n[ \t]*SAMPLE\\([a-z0-9_]+," sample_entries "${samples_cpp}")
set(PS5_SAMPLES)
foreach(entry IN LISTS sample_entries)
	string(REGEX REPLACE ".*SAMPLE\\(([a-z0-9_]+),$" "\\1" id "${entry}")
	list(APPEND PS5_SAMPLES ${id})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/samples.cpp)
message(STATUS "PS5 samples: ${PS5_SAMPLES}")

set(sample_sources)
foreach(id IN LISTS PS5_SAMPLES)
	set(ID ${id})
	set(SOURCE ${ROOT}/examples/${id}/${id}.cpp)
	if(EXISTS ${ROOT}/examples/${id}/main.cpp)
		set(SOURCE ${ROOT}/examples/${id}/main.cpp)
	endif()
	configure_file(src/wrap.cpp.in ${CMAKE_BINARY_DIR}/wrap/${id}.cpp @ONLY)
	list(APPEND sample_sources ${CMAKE_BINARY_DIR}/wrap/${id}.cpp)
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/samples.txt "${PS5_SAMPLES}")

file(GLOB base_sources ${ROOT}/base/*.cpp)
# The title's own TLS for its downloads (src/pw_https.c): the pinned Mbed TLS, unpacked by tools/setup-mbedtls.sh.
execute_process(COMMAND bash ${ROOT}/tools/setup-mbedtls.sh RESULT_VARIABLE mbedtls_status
	OUTPUT_VARIABLE mbedtls_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT mbedtls_status EQUAL 0)
	message(FATAL_ERROR "Cannot prepare the pinned Mbed TLS (tools/setup-mbedtls.sh)")
endif()
file(GLOB mbedtls_sources ${mbedtls_dir}/library/*.c)
# The WebUI's HTTP server (src/pw_webui_http.cpp): the pinned libmicrohttpd, built static for this build's target by
# tools/build-webui-http.sh (the title's SDK, or the host for the host reference build).
if(PS5_HOST_REFERENCE)
	set(webui_http_mode host)
else()
	set(webui_http_mode ps5)
endif()
execute_process(COMMAND bash ${ROOT}/tools/build-webui-http.sh ${webui_http_mode} RESULT_VARIABLE webui_http_status
	OUTPUT_VARIABLE webui_http_archive OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT webui_http_status EQUAL 0)
	message(FATAL_ERROR "Cannot build the pinned libmicrohttpd (tools/build-webui-http.sh)")
endif()
# microhttpd.h for the one file that serves HTTP: the folder also holds an MHD platform.h, which must not shadow the
# title's own.
set_source_files_properties(${ROOT}/src/pw_webui_http.cpp PROPERTIES INCLUDE_DIRECTORIES
	${ROOT}/.deps/webui/libmicrohttpd-1.0.10/src/include)
# Its internal headers for its own files only; the title sees include/ alone.
set_source_files_properties(${mbedtls_sources} PROPERTIES INCLUDE_DIRECTORIES ${mbedtls_dir}/library)
list(FILTER base_sources EXCLUDE REGEX "VulkanAndroid|VulkanOHOS")
set(third_party_sources
	${ROOT}/external/imgui/imgui.cpp
	${ROOT}/external/imgui/imgui_demo.cpp
	${ROOT}/external/imgui/imgui_draw.cpp
	${ROOT}/external/imgui/imgui_widgets.cpp
	# libktx reads the samples' .ktx files; its Vulkan calls (vkloader.c) go
	# through volk like the rest
	${ROOT}/external/ktx/lib/texture.c
	${ROOT}/external/ktx/lib/hashlist.c
	${ROOT}/external/ktx/lib/checkheader.c
	${ROOT}/external/ktx/lib/swap.c
	${ROOT}/external/ktx/lib/memstream.c
	${ROOT}/external/ktx/lib/filestream.c
	${ROOT}/external/ktx/lib/vkloader.c
	# The runtime install's decoder (src/pw_pack.c)
	${ROOT}/external/zstd/zstddeclib.c
	# The updates' signature check (src/pw_release.c)
	${ROOT}/external/monocypher/monocypher.c
	${ROOT}/external/monocypher/monocypher-ed25519.c
	${mbedtls_sources}
	third_party/volk/volk.c)
set_source_files_properties(${third_party_sources} PROPERTIES COMPILE_OPTIONS "-w")

if(NOT PS5_HOST_REFERENCE)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env PS5_VULKAN_DIR=${PS5_VULKAN_DIR}
        bash ${ROOT}/tools/setup-graphics-foundation.sh
        RESULT_VARIABLE graphics_status OUTPUT_VARIABLE graphics_foundation OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT graphics_status EQUAL 0)
        message(FATAL_ERROR "Cannot prepare the pinned graphics foundation")
    endif()
endif()
if(PS5_HOST_REFERENCE)
	set(platform_source src/platform_host.c)
	set(app_root ${PS5_HOST_APP_ROOT})
	set(surface_platform VK_USE_PLATFORM_HEADLESS_EXT PS5_HOST_REFERENCE "PS5_APP_ROOT=\"${PS5_HOST_APP_ROOT}\"")
else()
	set(platform_source src/platform.c)
	list(APPEND platform_source
		${ROOT}/native/proton_wine.cpp ${ROOT}/native/proton_loading.cpp ${ROOT}/src/pw_loading_art.c ${ROOT}/src/pw_steam_screen.c ${ROOT}/src/pw_vm_census.c ${ROOT}/native/proton_limits_probe.cpp ${ROOT}/native/proton_sched_probe.cpp ${ROOT}/native/pw_audio_ps5.c ${ROOT}/native/pw_osk_ps5.c ${ROOT}/src/pw_osk.c
		${ROOT}/src/pw_wine_start.c ${ROOT}/src/pw_environment.c ${ROOT}/src/pw_wine_launch.c ${ROOT}/src/pw_wine_profile.c ${ROOT}/src/pw_shader_cache.c ${ROOT}/src/pw_fd_census.c ${ROOT}/src/pw_pipe_memory.c ${ROOT}/src/pw_pad_script.c ${ROOT}/src/pw_memory_guard.c ${ROOT}/src/pw_desktop_session.c ${ROOT}/src/pw_app_environment.c ${ROOT}/src/pw_shell_link.c ${ROOT}/src/pw_game_folders.c ${ROOT}/src/pw_steam_appid.c ${ROOT}/src/pw_usb_input.c ${ROOT}/src/pw_aurelia.c ${ROOT}/src/pw_steamcmd.c ${ROOT}/src/pw_steamcmd_setup.c ${ROOT}/native/pw_usb_input_ps5.c
		${ROOT}/src/pw_elfldr.c ${ROOT}/wine/ps5/pw_wine_prx.c
		${ROOT}/src/pw_hang_breaker.c ${ROOT}/src/pw_start_failure.c ${ROOT}/src/pw_firmware.c ${ROOT}/native/pw_firmware_ps5.c ${ROOT}/src/pw_flight_probe.c ${ROOT}/native/proton_flight_recorder.cpp)
	# main selects the foundation's runtime resourcePath after elevation.
	# Keep the sandbox path as the compile-time fallback.
	set(app_root /app0)
	set(surface_platform _DIRECT2DISPLAY)
    if(PS5_CHILD_PROBE)
        list(APPEND platform_source ${ROOT}/native/proton_child_probe.cpp)
        list(APPEND surface_platform PS5_CHILD_PROBE)
    endif()
    if(PS5_INSTALL_PROBE)
        list(APPEND platform_source ${ROOT}/native/proton_install_probe.cpp)
        list(APPEND surface_platform PS5_INSTALL_PROBE)
    endif()
    if(PS5_GPU_BROKER AND NOT PS5_SERVICE_NATIVE)
        list(APPEND platform_source ${ROOT}/native/proton_gpu_server_probe.cpp ${ROOT}/native/proton_gpu_display.cpp
            ${graphics_foundation}/tooling/gpu-broker/server.cpp
            ${graphics_foundation}/tooling/gpu-broker/packet_io.c ${ROOT}/wine/ps5/pw_wine_process_io.c)
    endif()
    if(PS5_GPU_BROKER)
        list(APPEND platform_source ${ROOT}/src/pw_window_page.c ${ROOT}/src/pw_window_scene.c ${ROOT}/src/pw_window_compose.c ${ROOT}/src/pw_window_effects.c
            ${ROOT}/src/pw_frame_pacing.c ${ROOT}/src/pw_window_direct.c)
        list(APPEND surface_platform PS5_GPU_BROKER)
    endif()
    if(PS5_TESTER_PROBE AND NOT PS5_SERVICE_NATIVE)
        message(FATAL_ERROR "PS5_TESTER_PROBE needs PS5_SERVICE_NATIVE (its Sony helper)")
    endif()
    if(PS5_SERVICE_PREFLIGHT OR PS5_SERVICE_NATIVE)
        if(PS5_CHILD_PROBE OR (PS5_SERVICE_PREFLIGHT AND PS5_SERVICE_NATIVE))
            message(FATAL_ERROR "Enable only one isolated process probe")
        endif()
        list(APPEND platform_source ${ROOT}/native/proton_service_preflight.cpp)
        if(PS5_SERVICE_NATIVE)
            list(APPEND platform_source ${ROOT}/native/proton_gpu_broker_probe.cpp ${ROOT}/native/proton_gpu_memory_probe.cpp
                ${ROOT}/native/proton_fd_budget_probe.cpp ${ROOT}/native/proton_channel_probe.cpp
                ${ROOT}/native/proton_gpu_server_probe.cpp ${ROOT}/native/proton_gpu_display.cpp
                ${graphics_foundation}/tooling/gpu-broker/packet_io.c ${ROOT}/wine/ps5/pw_wine_process_io.c)
            # The broker's server (in proton_gpu_server_probe.cpp) composes windows and paces frames.
            if(NOT PS5_GPU_BROKER)
                list(APPEND platform_source ${ROOT}/src/pw_window_page.c ${ROOT}/src/pw_window_scene.c ${ROOT}/src/pw_window_compose.c
                    ${ROOT}/src/pw_window_effects.c ${ROOT}/src/pw_frame_pacing.c ${ROOT}/src/pw_window_direct.c)
            endif()
            list(APPEND surface_platform PS5_SERVICE_NATIVE)
            if(PS5_TESTER_PROBE)
                list(APPEND platform_source ${ROOT}/native/proton_tester_probe.cpp)
                list(APPEND surface_platform PS5_TESTER_PROBE)
            endif()
        else()
            list(APPEND surface_platform PS5_SERVICE_PREFLIGHT)
        endif()
    endif()
endif()
# The updates (src/proton_update.cpp): the public half of the owner's release key (tools/sign-release.py keygen) and
# where the owner publishes the signed manifest. Neither exists until the owner publishes; the title then says so.
# A rehearsal build names a throwaway key's file instead (cmake -DPROTON_RELEASE_KEY_FILE=...).
set(PROTON_RELEASE_KEY_FILE ${ROOT}/platform/update/release-key.txt CACHE FILEPATH "The public half of the release key")
set(release_key "")
if(EXISTS ${PROTON_RELEASE_KEY_FILE})
	file(STRINGS ${PROTON_RELEASE_KEY_FILE} release_key LIMIT_COUNT 1 REGEX "^[0-9a-f]+$")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROTON_RELEASE_KEY_FILE})
endif()
set(PROTON_UPDATE_URL "" CACHE STRING "Where the signed release manifest is published; empty until the owner publishes")
# The release this build is (VERSION, as the wallpaper's note shows it): the automatic update check offers only a newer
# GitHub release (ps5/src/proton_update.cpp).
file(STRINGS ${ROOT}/VERSION proton_version LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ROOT}/VERSION)
add_library(samples OBJECT
	${base_sources}
	${third_party_sources}
	${sample_sources}
	src/main.cpp
	src/launcher.cpp
	src/samples.cpp
	src/proton_launch.cpp
	src/proton_install.cpp
	src/proton_fetch.cpp
	src/proton_update.cpp
	src/proton_steam.cpp
	${ROOT}/src/pw_https.c
	${ROOT}/src/pw_release.c
	${ROOT}/src/pw_update.c
	${ROOT}/src/pw_zip.c
	${ROOT}/src/pw_update_flow.cpp
	${ROOT}/src/pw_steam.c
	${ROOT}/src/pw_steam_guide.c
	${ROOT}/src/pw_path_nocase.c
	${ROOT}/src/pw_pack.c
	${ROOT}/src/pw_first_run.c
	${ROOT}/src/pw_sha256.c
	${ROOT}/src/pw_pe_arch.c
	${ROOT}/src/pw_controls.c
	${ROOT}/src/pw_game_mode.c
	${ROOT}/src/pw_high_refresh.c
	${ROOT}/src/pw_controller.c
	${ROOT}/src/pw_detailed_logs.c
	${ROOT}/src/pw_log_sessions.c
	${ROOT}/src/pw_capped_log.c
	${ROOT}/src/pw_setup.c
	${ROOT}/src/pw_runtime_check.c
	${ROOT}/src/pw_payload_check.c
	${ROOT}/src/pw_qrcodegen.c
	${ROOT}/src/pw_webui.cpp
	${ROOT}/src/pw_drive_browse.c
	${ROOT}/src/pw_home_grid.c
	${ROOT}/src/pw_steam_memory.c
	${ROOT}/src/pw_webui_http.cpp
	src/proton_webui.cpp
	src/example_ps5.cpp
	${platform_source})
target_compile_definitions(samples PRIVATE
	VK_EXAMPLE_PS5
	"PS5_TITLE_NAME=\"${PS5_TITLE_NAME}\""
	"PS5_TITLE_ID=\"${PS5_TITLE_ID}\""
	${surface_platform}
	VK_NO_PROTOTYPES
	NDEBUG
	VK_EXAMPLE_ASSETS_DIR="${app_root}/assets/"
	VK_EXAMPLE_SHADERS_DIR="${app_root}/shaders/"
	"MBEDTLS_USER_CONFIG_FILE=\"${ROOT}/platform/tls/pw_mbedtls_config.h\""
	"PROTON_RELEASE_KEY=\"${release_key}\""
	"PROTON_UPDATE_URL=\"${PROTON_UPDATE_URL}\""
	"PROTON_VERSION=\"${proton_version}\"")
target_include_directories(samples PRIVATE
	${ROOT}/external
	${ROOT}/external/glm
	${ROOT}/external/imgui
	${ROOT}/external/tinygltf
	${ROOT}/external/ktx/include
	${ROOT}/external/ktx/other_include
	${ROOT}/base
	third_party/volk
	${mbedtls_dir}/include
	src)
if(PS5_SERVICE_NATIVE OR PS5_GPU_BROKER)
    target_include_directories(samples PRIVATE ${graphics_foundation}/tooling/gpu-broker)
endif()
set_target_properties(samples PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON C_STANDARD 11)
# Every unit sees volk's declarations first: the Vulkan commands are volk's
# pointers, filled from the RADV linked into the title.
# -fno-omit-frame-pointer: the console's crash backtraces are frame-pointer walks.
target_compile_options(samples PRIVATE
	-O2 -g1 -ffunction-sections -fdata-sections -fno-omit-frame-pointer
	"SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk/volk.h")

include(ui/ui.cmake)

if(PS5_HOST_REFERENCE)
	target_sources(samples PRIVATE ${ROOT}/src/pw_wine_launch.c ${ROOT}/src/pw_environment.c)
	add_executable(ps5-samples-host $<TARGET_OBJECTS:samples>)
	target_link_libraries(ps5-samples-host PRIVATE ${webui_http_archive} dl pthread)
	add_executable(ps5-proton-ui-test ${ROOT}/tests/test_proton_ui.cpp
		$<FILTER:$<TARGET_OBJECTS:samples>,EXCLUDE,/src/main.cpp.o$>)
	foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
		set_property(TARGET ps5-proton-ui-test PROPERTY ${property} $<TARGET_PROPERTY:samples,${property}>)
	endforeach()
	set_target_properties(ps5-proton-ui-test PROPERTIES CXX_STANDARD 20)
	target_link_libraries(ps5-proton-ui-test PRIVATE ${webui_http_archive} dl pthread)
	return()
endif()

# Link, sign and package: the title folder in dist/<TITLE_ID>/
add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/eboot.elf
	# every other file the link reads (the SDK's libraries, RADV, the tools): link-title.sh lists them
	DEPFILE ${CMAKE_BINARY_DIR}/eboot.elf.d
	COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/link-title.sh ${CMAKE_BINARY_DIR} ${PS5_VULKAN_DIR} ${PS5_PAYLOAD_SDK} "$<TARGET_OBJECTS:samples>" ${webui_http_archive}
	DEPENDS samples "$<TARGET_OBJECTS:samples>" ${CMAKE_CURRENT_SOURCE_DIR}/tools/link-title.sh ${webui_http_archive}
		${ROOT}/webui/index.html ${ROOT}/webui/app.css ${ROOT}/webui/app.js ${ROOT}/webui/i18n.js
		# the tiles' programs link-title.sh builds and copies (tools/build-forwarder.sh): a change to one alone
		# relinks, or the title folder keeps the old ones
		${ROOT}/forwarder/forwarder.h ${ROOT}/forwarder/tile.c ${ROOT}/forwarder/launch.c ${ROOT}/forwarder/worker.c
		${ROOT}/tools/build-forwarder.sh
		${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/param.json
	COMMAND_EXPAND_LISTS
	VERBATIM)
add_custom_target(title ALL DEPENDS ${CMAKE_BINARY_DIR}/eboot.elf)
