#!/usr/bin/env bash
# PS5_Proton - build and run every host test (tests/test_*.c) with the host C compiler.
#
#   tools/host-tests.sh            all of them
#   tools/host-tests.sh perf sync  the ones whose names contain these words
#
# Each test is compiled with the portable sources it covers (the table below),
# -Werror, and run; the script fails on the first compile error or failing test.
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/tools/setup-graphics-foundation.sh" > /dev/null
out=${HOST_TEST_DIR:-$root/.deps/host-tests}
mkdir -p "$out"
declare -A sources=(
    [test_pw_control_watch]=""
    [test_pw_payload_close]=""
    [test_pw_warm_helper]=""
    [test_pw_payload_rendezvous]="wine/ps5/pw_wine_process_io.c"
    [test_pw_frame_pacing]="src/pw_frame_pacing.c"
    [test_pw_aurelia]="src/pw_aurelia.c"
    [test_pw_steam]="src/pw_steam.c"
    [test_pw_steam_guide]="src/pw_steam_guide.c"
    [test_pw_steam_screen]="src/pw_steam_screen.c src/pw_loading_art.c src/pw_setup.c"
    [test_pw_path_nocase]="src/pw_path_nocase.c"
    [test_pw_steamcmd]="src/pw_steamcmd.c"
    [test_pw_steamcmd_setup]="src/pw_steamcmd_setup.c src/pw_steamcmd.c"
    [test_pw_gpu_timing]=""
    [test_pw_display_copy]=""
    [test_pw_display_capture]=""
    [test_pw_log_drain]=""
    [test_pw_wine_inherit]=""
    [test_pw_wine_title_game]=""
    [test_pw_wine_pe_imports]=""
    [test_forwarder_worker]=""
    [test_pw_speed_defaults]=""
    [test_pw_display_rate]=""
    [test_pw_present_retry]=""
    [test_pw_suspend]=""
    [test_pw_window_direct]="src/pw_window_direct.c src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_desktop_session]="src/pw_desktop_session.c"
    [test_pw_app_environment]="src/pw_app_environment.c src/pw_environment.c"
    [test_pw_shell_link]="src/pw_shell_link.c"
    [test_pw_browser_policy]="src/pw_browser_policy.c"
    [test_pw_browser_chrome]="src/pw_browser_chrome.c"
    [test_proton_dualsense]=""
    [test_proton_touchpad]=""
    [test_pw_controls]="src/pw_controls.c"
    [test_pw_osk]="src/pw_osk.c native/pw_osk_ps5.c"
    [test_pw_usb_input]="src/pw_usb_input.c"
    [test_pw_game_mode]="src/pw_game_mode.c"
    [test_pw_high_refresh]="src/pw_high_refresh.c"
    [test_pw_controller]="src/pw_controller.c"
    [test_pw_setup]="src/pw_setup.c"
    [test_pw_drive_browse]="src/pw_drive_browse.c"
    [test_pw_home_grid]="src/pw_home_grid.c"
    [test_pw_steam_memory]="src/pw_steam_memory.c"
    [test_pw_runtime_check]="src/pw_runtime_check.c src/pw_setup.c"
    [test_pw_payload_check]="src/pw_payload_check.c"
    [test_pw_loading_art]="src/pw_loading_art.c"
    [test_pw_taskbar_layout]=""
    [test_pw_text_glyphs]=""
    [test_pw_desktop_icons]=""
    [test_pw_tooltip]=""
    [test_pw_explorer_layout]=""
    [test_pw_menu_style]=""
    [test_pw_vm_census]="src/pw_vm_census.c"
    [test_pw_audio_mix]="src/pw_audio_mix.c"
    [test_pw_audio_ring]="src/pw_audio_ring.c" [test_pw_memory_helper]="src/pw_memory_helper.c" [test_pw_memory_service]="src/pw_memory_service.c"
    [test_pw_wine_dmem_regions]="wine/ps5/pw_wine_dmem.c"
    [test_pw_wine_heap]="wine/ps5/pw_wine_heap.c"
    [test_pw_wine_dmem_ps5]="wine/ps5/pw_wine_dmem_ps5.c wine/ps5/pw_wine_dmem.c wine/ps5/pw_wine_heap.c wine/ps5/pw_wine_pages.c wine/ps5/pw_wine_vm.c wine/ps5/pw_wine_alias.c
        wine/ps5/pw_wine_perf.c wine/ps5/pw_wine_codeshare.c wine/ps5/pw_wine_code_budget.c"
    [test_pw_wine_alias]="wine/ps5/pw_wine_alias.c"
    [test_pw_wine_mmu4k]="wine/ps5/pw_wine_mmu4k.c wine/ps5/pw_wine_mmu4k_slot.S wine/ps5/pw_wine_alias.c src/pw_ia32_isa.c
        src/pw_ia32_decode.c"
    [test_pw_window_gpu_planes]="src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_gpu_clips]="src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_gpu_scene]="src/pw_window_scene.c src/pw_window_page.c src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_page]="src/pw_window_page.c"
    [test_pw_window_effects]="src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_materials]="src/pw_window_compose.c src/pw_window_effects.c src/pw_window_scene.c src/pw_window_page.c"
    [test_pw_window_thumbnails]="src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_compose]="src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_shapes]="src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_alpha]="src/pw_window_page.c src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_window_scene]="src/pw_window_scene.c src/pw_window_page.c src/pw_window_compose.c src/pw_window_effects.c"
    [test_pw_wine_cursor]=""
    [test_pw_wine_window]="wine/ps5/pw_wine_window.c wine/ps5/pw_wine_pages.c src/pw_window_page.c"
    [test_pw_environment]="src/pw_environment.c"
    [test_pw_wine_launch]="src/pw_wine_launch.c"
    [test_pw_wine_profile]="src/pw_wine_profile.c"
    [test_pw_wine_cwd_links]="wine/ps5/pw_wine_cwd.c wine/ps5/pw_wine_cwd_libc.c"
    [test_pw_wine_xattr]="wine/ps5/pw_wine_xattr.c"
    [test_pw_wine_prefix]="wine/ps5/pw_wine_prefix.c src/pw_wine_launch.c"
    [test_pw_civil_time]=""
    [test_pw_wine_wallpaper]="wine/ps5/pw_wine_wallpaper.c"
    [test_pw_wine_prx]="wine/ps5/pw_wine_prx.c"
    [test_pw_wine_section_cluster]="wine/ps5/pw_wine_section.c"
    [test_pw_wine_section]="wine/ps5/pw_wine_section.c"
    [test_pw_wine_dsection]="wine/ps5/pw_wine_dsection.c"
    [test_pw_wine_codeshare]="wine/ps5/pw_wine_codeshare.c"
    [test_pw_wine_code_budget]="wine/ps5/pw_wine_code_budget.c"
    [test_pw_wine_section_data]="wine/ps5/pw_wine_section.c wine/ps5/pw_wine_blocks.c"
    [test_pw_wine_section_blocks]="wine/ps5/pw_wine_section.c wine/ps5/pw_wine_blocks.c"
    [test_pw_wine_section_dmem]="wine/ps5/pw_wine_section.c wine/ps5/pw_wine_dmap.c wine/ps5/pw_wine_dsection.c"
    [test_pw_memory_file]="src/pw_memory_file.c"
    [test_pw_physical_shared]=""
    [test_pw_wine_vm]="wine/ps5/pw_wine_vm.c"
    [test_pw_wine_start]="src/pw_wine_start.c wine/ps5/pw_wine_prx.c"
    [test_pw_prx_elf]="src/pw_prx_elf.c wine/ps5/pw_wine_prx.c"
    [test_pw_wine_perf]="wine/ps5/pw_wine_perf.c"
    [test_pw_gpu_broker]=""
    # Graphics patch 0018's staging.h, from the applied foundation (setup-graphics-foundation.sh, above).
    [test_pw_gpu_staging]=""
    [test_pw_fd_census]="src/pw_fd_census.c"
    [test_pw_pipe_memory]="src/pw_pipe_memory.c"
    [test_pw_memory_guard]="src/pw_memory_guard.c"
    [test_pw_hang_breaker]="src/pw_hang_breaker.c"
    [test_pw_log_sessions]="src/pw_log_sessions.c"
    [test_pw_parked]=""
    [test_pw_missing_dll]=""
    [test_pw_spawn_wait]=""
    [test_pw_capped_log]="src/pw_capped_log.c"
    [test_pw_detailed_logs]="src/pw_detailed_logs.c src/pw_log_sessions.c"
    [test_pw_flight_probe]="src/pw_flight_probe.c"
    [test_pw_helper_sched]="src/pw_helper_sched.c"
    [test_pw_cpu_placement]="src/pw_cpu_placement.c"
    [test_pw_elfldr]="src/pw_elfldr.c"
    [test_pw_sha256]="src/pw_sha256.c"
    [test_pw_pack]="src/pw_pack.c src/pw_sha256.c external/zstd/zstddeclib.c"
    [test_pw_first_run]="src/pw_first_run.c"
    [test_pw_update]="src/pw_update.c src/pw_zip.c src/pw_pack.c src/pw_sha256.c external/zstd/zstddeclib.c"
    [test_pw_release]="src/pw_release.c external/monocypher/monocypher.c external/monocypher/monocypher-ed25519.c"
    [test_pw_pe_arch]="src/pw_pe_arch.c"
    [test_pw_start_failure]="src/pw_start_failure.c"
    [test_pw_wine_dl]="wine/ps5/pw_wine_dl.c wine/ps5/pw_wine_prx.c"
    [test_pw_firmware]="src/pw_firmware.c"
    [test_pw_gpu_identity]=""
    [test_pw_wine_gpu_keep]=""
    [test_pw_game_folders]="src/pw_game_folders.c src/pw_pe_arch.c"
    [test_pw_steam_appid]="src/pw_steam_appid.c src/pw_path_nocase.c"
    [test_pw_pad_script]="src/pw_pad_script.c"
    [test_pw_thread_alert]=""
    [test_pw_ntsync]="src/pw_ntsync.c"
    [test_pw_wine_sink]="wine/ps5/pw_wine_sink.c wine/ps5/pw_wine_window.c wine/ps5/pw_wine_pages.c src/pw_window_page.c src/pw_audio_ring.c"
    [test_pw_wine_pages]="wine/ps5/pw_wine_pages.c"
    [test_pw_wine_process_io]="wine/ps5/pw_wine_process_io.c"
    [test_pw_wine_threads]="wine/ps5/pw_wine_threads.c"
    [test_pw_ws2_32_libc]="wine/ps5/pw_ws2_32_libc.c"
    [test_pw_ia32_decode]="src/pw_ia32_isa.c src/pw_ia32_decode.c src/pw_ia32_print.c"
    [test_pw_ia32_interp]="src/pw_ia32_isa.c src/pw_ia32_decode.c src/pw_ia32_interp.c"
    [test_pw_umip]="src/pw_ia32_isa.c src/pw_ia32_decode.c src/pw_umip.c"
    [test_pw_user_folder]=""
    [test_pw_dialog_keys]=""
    [test_wine_fault_served]=""
    [test_wine_virtual_guard]=""
    [test_process_rules]="src/pw_steam.c"
    [test_process_hid_link]=""
    [test_crash_replays]="tests/fake_ps5_kernel.c wine/ps5/pw_wine_dmem_ps5.c wine/ps5/pw_wine_dmem.c wine/ps5/pw_wine_heap.c
        wine/ps5/pw_wine_pages.c wine/ps5/pw_wine_pages_ps5.c wine/ps5/pw_wine_vm.c wine/ps5/pw_wine_prx.c wine/ps5/pw_wine_alias.c
        wine/ps5/pw_wine_mmu4k_ps5.c wine/ps5/pw_wine_mmu4k.c wine/ps5/pw_wine_mmu4k_slot.S src/pw_ia32_isa.c src/pw_ia32_decode.c
        wine/ps5/pw_wine_perf.c wine/ps5/pw_wine_codeshare.c wine/ps5/pw_wine_code_budget.c .deps/graphics-foundation/tooling/gpu-broker/client.c"
)
# Extra compiler flags: the resolver's names are prefixed so the test reaches it, not glibc's.
declare -A flags=(
    # The payload SDK's memory header, as declarations only (the glue includes it; its libc headers are the SDK's).
    [test_pw_wine_dmem_ps5]="-I$root/tests/include"
    # The 4 KiB MMU's engine runs with the program's vector registers live: built as on the console, and without
    # ASan's stack handling or UBSan's runtime calls, either of which would break that contract.
    [test_pw_wine_mmu4k]="-mgeneral-regs-only -fno-sanitize=address,undefined"
    # Patch 1127's fault hook, extracted from the applied Wine tree into $out (below).
    [test_wine_fault_served]="-I$out"
    # Patch 1128's guard in virtual.c, extracted from the applied Wine tree into $out (below).
    [test_wine_virtual_guard]="-I$out"
    [test_process_rules]="-I$out"
    [test_process_hid_link]="-I$out"
    # The shared fake kernel owns console addresses as host addresses, which below 0x2008fff7000 lie in AddressSanitizer's
    # shadow gap: no ASan here (UBSan stays). The wrapped libc calls reach the fake; Wine's extracted code is built with
    # Wine's warnings, and the host's mincore takes unsigned char where the console's takes char.
    [test_crash_replays]="-I$root/tests/include -I$out -fno-sanitize=address -Wno-unused-parameter -Wno-unused-function
        -Wno-sign-compare -Wno-pointer-sign
        -Wl,--wrap=mmap,--wrap=munmap,--wrap=mprotect,--wrap=mlock,--wrap=munlock,--wrap=mincore,--wrap=madvise
        -Wl,--wrap=msync,--wrap=open,--wrap=fork,--wrap=pread,--wrap=pwrite"
    # The clock's glyphs and Chromium's strings, generated from themes/windows11/taskbar/clock-*.json (below).
    [test_pw_text_glyphs]="-I$root/src -I$root/.deps/windows11-theme"
    # The menus' measured pixels, generated from themes/windows11/menus/*.json.gz (below).
    [test_pw_menu_style]="-I$root/src -I$root/.deps/windows11-theme"
    # The desktop's plates, band and label glyphs, and Chromium's labels, from themes/windows11/desktop (below).
    [test_pw_desktop_icons]="-I$root/src -I$root/.deps/windows11-theme"
    # Tooltips' glyphs and Chromium's tooltips, from themes/windows11/tooltip (below).
    [test_pw_tooltip]="-I$root/src -I$root/.deps/windows11-theme"
    [test_pw_ws2_32_libc]="-D_DEFAULT_SOURCE -Dgetaddrinfo=pw_test_getaddrinfo -Dfreeaddrinfo=pw_test_freeaddrinfo
        -Dgetnameinfo=pw_test_getnameinfo -Dgethostbyname=pw_test_gethostbyname -Dgethostbyaddr=pw_test_gethostbyaddr"
)
# The libc calls pw_wine_cwd_libc.c wraps, as tools/build-wine-ps5.sh links them, so the test's own calls reach them.
flags[test_pw_wine_cwd_links]="-I$root/wine/ps5 $(grep -oh '__wrap_[a-z_0-9]*' "$root/wine/ps5/pw_wine_cwd_libc.c" |
    sed 's/__wrap_//' | grep . | sort -u | sed 's/^/-Wl,--wrap=/' | tr '\n' ' ')"
ran=0
for test in "$root"/tests/test_*.c; do
    name=$(basename "$test" .c)
    if (( $# )); then
        match=0
        for word in "$@"; do [[ $name == *"$word"* ]] && match=1; done
        (( match )) || continue
    fi
    if [[ $name == test_pw_dns_unix ]]; then
        python3 "$root/tools/test-dns-backend.py"
        ran=$((ran + 1))
        continue
    fi
    [[ -v sources[$name] ]] || { echo "$name: no entry in tools/host-tests.sh" >&2; exit 1; }
    if [[ $name == test_wine_fault_served ]]; then
        # shortcut: reads the tree the last build-wine-ps5.sh applied, not the patch series; build after editing 1127.
        virtual=$root/.deps/wine-ps5/source/dlls/ntdll/unix/virtual.c
        [[ -f $virtual ]] || { echo "$name: skipped (no applied Wine tree; run tools/build-wine-ps5.sh)"; continue; }
        python3 "$root/tests/extract_fault_hook.py" "$virtual" "$out/fault_hook.inc"
    fi
    if [[ $name == test_crash_replays ]]; then
        # shortcut: reads the tree the last build-wine-ps5.sh applied, not the patch series; build after editing virtual.c.
        virtual=$root/.deps/wine-ps5/source/dlls/ntdll/unix/virtual.c
        [[ -f $virtual ]] || { echo "$name: skipped (no applied Wine tree; run tools/build-wine-ps5.sh)"; continue; }
        python3 "$root/tests/extract_crash_replays.py" "$virtual" "$out/crash_replays.inc" \
            "$root/.deps/graphics-foundation/tooling/gpu-broker/server.cpp" "$out/broker_shared_object.inc"
    fi
    if [[ $name == test_pw_text_glyphs ]]; then
        python3 "$root/tools/build-shell-glyphs.py" > /dev/null
    fi
    if [[ $name == test_pw_menu_style ]]; then
        python3 "$root/tools/build-menu-sprites.py" > /dev/null
    fi
    if [[ $name == test_pw_desktop_icons ]]; then
        python3 "$root/tools/build-desktop-sprites.py" > /dev/null
    fi
    if [[ $name == test_pw_tooltip ]]; then
        python3 "$root/tools/build-tooltip-glyphs.py" > /dev/null
    fi
    if [[ $name == test_process_rules ]]; then
        python3 "$root/tests/extract_process_rules.py" "$root"/wine/patches/2070-*.patch "$out/process_rules.inc" \
            "$root"/wine/patches/2104-*.patch
    fi
    if [[ $name == test_process_hid_link ]]; then
        python3 "$root/tests/extract_process_rules.py" "$root"/wine/patches/2401-*.patch "$out/process_hid_link.inc"
    fi
    if [[ $name == test_wine_virtual_guard ]]; then
        # shortcut: reads the tree the last build-wine-ps5.sh applied, not the patch series; build after editing 1128.
        virtual=$root/.deps/wine-ps5/source/dlls/ntdll/unix/virtual.c
        [[ -f $virtual ]] || { echo "$name: skipped (no applied Wine tree; run tools/build-wine-ps5.sh)"; continue; }
        python3 "$root/tests/extract_virtual_guard.py" "$virtual" "$out/virtual_guard.inc"
    fi
    files=()
    for source in ${sources[$name]}; do files+=("$root/$source"); done
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined ${flags[$name]:-} -o "$out/$name" \
        "$test" "${files[@]}" -lm -lpthread
    arguments=()
    if [[ $name == test_pw_pad_script ]]; then
        arguments=("$root/tests/pad-scripts/desktop-pointer.txt" "$root/tests/pad-scripts/mouse-controls.txt")
    fi
    [[ $name == test_pw_setup ]] && arguments=("$root")
    if [[ $name == test_pw_steam_screen ]]; then
        # Inter from the kit's export, the Chinese and Japanese cuts from the last build's assets (skipped before one)
        cuts=$root/build/ps5/generated/setup/fonts
        [[ -f $cuts/noto-sans-sc.ttf && -f $cuts/noto-sans-jp.ttf ]] || cuts=-
        arguments=("$root/.deps/hui/third_party/fonts" "$cuts")
    fi
    "$out/$name" "${arguments[@]}" > "$out/$name.log" 2>&1 || { cat "$out/$name.log" >&2; echo "$name: FAILED" >&2; exit 1; }
    echo "$name: ok"
    ran=$((ran + 1))
    if [[ $name == test_pw_memory_helper ]]; then
        # Exercise the native-selection branch with the existing POSIX oracle;
        # console physical semantics remain independently qualified.
        "${CC:-cc}" -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
            -DPW_MEMORY_HELPER_PHYSICAL -DPW_PHYSICAL_SHARED_ORACLE_ONLY \
            "$test" "$root/src/pw_memory_helper.c" "$root/tests/test_pw_physical_shared.c" \
            -lpthread -o "$out/test_pw_memory_helper_physical"
        "$out/test_pw_memory_helper_physical" > "$out/test_pw_memory_helper_physical.log" 2>&1 || \
            { cat "$out/test_pw_memory_helper_physical.log" >&2; exit 1; }
        echo "test_pw_memory_helper_physical: ok (POSIX allocator oracle)"
        ran=$((ran + 1))
    fi
done
# The WebUI's core (src/pw_webui.cpp) is C++ over nlohmann::json; the C files it saves the settings through are
# compiled as C, then linked with it.
if (( $# == 0 )) || [[ "$*" == *webui* ]]; then
    webui_objects=()
    for source in pw_controller pw_controls pw_detailed_logs pw_log_sessions pw_game_mode pw_high_refresh pw_drive_browse; do
        "${CC:-cc}" -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -c "$root/src/$source.c" \
            -o "$out/webui_$source.o"
        webui_objects+=("$out/webui_$source.o")
    done
    "${CXX:-c++}" -std=gnu++17 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$root/src" \
        -I"$root/external/tinygltf" "$root/tests/test_pw_webui.cpp" "$root/src/pw_webui.cpp" "${webui_objects[@]}" \
        -o "$out/test_pw_webui"
    "$out/test_pw_webui" > "$out/test_pw_webui.log" 2>&1 || { cat "$out/test_pw_webui.log" >&2; echo "test_pw_webui: FAILED" >&2; exit 1; }
    echo "test_pw_webui: ok"
    ran=$((ran + 1))
fi
if (( $# == 0 )) || [[ "$*" == *console_guard* ]]; then
    for test in console_guard battery_runner gate_quick shell_capture window_compose_runner appearance_runner profile_report \
            desktop_session_runner desktop_input_runner tls_fixture telemetry_teardown \
            window_material_tokens window_effects_runner frame_pacing_runner deploy_folder console_setup sched_watch run_console_verdicts \
            placed_unmap_order module_wiring helper_opengl privacy_check dll_overrides prx_exports taskbar_logo stage_licences publication_check title_icon pack_runtime pw_https mihawk_wallpaper aero_wallpapers mihawk_music browser_lines sign_release update_rehearsal package_install check_title_imports carrier_image_probe payload_firmware lapy_patches memory_breakdown run_shared_charge webui_http webui_i18n deploy_wine_retire start_stages wineboot_install_sentinel build_stamp pw_foreign_process pw_command_line; do
        python3 "$root/tests/test_$test.py"
        ran=$((ran + 1))
    done
fi
echo "host tests: $ran passed"
