#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build everything the production install holds, from this checkout's pins:
#
#   the runtime and its helper modules   tools/build-wine-ps5.sh twice, from one patch series: with the pinned
#                                        OpenGL SDK for the title (.deps/wine-ps5), without it for programs in
#                                        helper processes (.deps/wine-gpu-broker; a helper cannot use the
#                                        driver's system libraries)
#   the GPU broker's Vulkan client       tools/link_gpu_client.sh (.deps/gpu-wsi-client), RADV for a helper
#   the helper-process loader            tools/build-payload-loader.sh (a payload; stage-wine.py stages it)
#   the title                            ps5/tools/build.sh, with the GPU broker (PS5_GPU_BROKER)
#   Mesa's WGL over Zink                 tools/build-mesa-windows-zink.sh (.deps/mesa-foundation-wgl), OpenGL in helpers
#   ...and its i686 build                tools/build-mesa-windows-zink-i686.sh, OpenGL for 32-bit programs
#   VKD3D-Proton and the release DXVK    tools/extract-dxvk.py (.deps/dxvk-release), from the pinned GE-Proton archive
#   the staged runtime and test prefix   tools/stage-wine.py (.deps/staged)
#   the runtime's install pieces         tools/pack-runtime.py (dist/runtime, the manifest in the title folder)
#
# The slow stages run through tools/build-stamp.py, which skips one whose inputs and outputs have the content they had
# after its last good run (docs/BUILD_SPEED.md): a title change leaves the two Wine builds, the probes and the staging
# alone. BUILD_STAMP_FORCE=1 runs them all; the output is the same bytes either way.
#
# Then ps5/tools/deploy.sh puts the title folder on the console and tools/deploy-wine.py the runtime.
#
#   tools/build-production.sh [--jobs N]
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
jobs=12
while [ $# -gt 0 ]; do
    case $1 in
    --jobs) jobs=$2; shift ;;
    *) echo "usage: tools/build-production.sh [--jobs N]" >&2; exit 2 ;;
    esac
    shift
done
title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
# The title starts the resident Lapy daemon itself when no daemon answers its /data request
# (proton_data_grant), so the library is there on every launch: the pinned resident build ships in
# the title folder. tools/build-lapy-validation.py --resident builds it (platform/lapy/README.md),
# checked first, so a build that cannot ship stops before hours of building.
daemon=$root/.deps/lapy-validation/source/build/owned_root_daemon-service-verified-client
daemon_sha256=76481d3d2ea5f9e588111e74281ae47eea283b0f23481ef8fb71c2402c6e7fc0
python3 - "$daemon" "$daemon_sha256" "$title" <<'EOF'
import hashlib, json, sys
from pathlib import Path
build, pinned, title = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
if not (build / 'manifest.json').is_file():
    sys.exit('no resident Lapy daemon: run tools/fetch-all.sh')
manifest = json.loads((build / 'manifest.json').read_text())
actual = hashlib.sha256((build / 'lapy-root-daemon.elf').read_bytes()).hexdigest()
if (actual != pinned or manifest['elf_sha256'] != pinned or manifest['mode'] != 'service'
        or manifest['max_requests'] is not None or manifest['target_title'] != title):
    sys.exit('the resident Lapy daemon is not the pinned build (tools/fetch-all.sh keeps one that is there): '
             'rebuild it from the checkout with python3 tools/build-lapy-validation.py --resident '
             '--sdk-zip .deps/ps5-payload-sdk-v0.42.zip (clang/LLD 23.1.3; README.md, Host tools)')
EOF
# PS5_Vulkan at the pinned revision, with its RADV release archive built there (tools/setup-pinned-radv.sh): the
# sibling checkout's own working tree moves on by itself.
vulkan=$(bash "$root/tools/setup-pinned-radv.sh")
foundation=$root/.deps/prx-foundation
sdk=$(bash "$root/ps5/tools/setup-sdk.sh")

# stage NAME [build-stamp options] -- COMMAND...: COMMAND, unless nothing it reads or writes changed. What a stage
# reads besides the repository is named with --input; the host's compilers and the environment the scripts read are
# in its key too.
host=$(clang --version | sed -n 1p; python3 -c 'import sys, PIL; print(sys.version, PIL.__version__)'; make --version | sed -n 1p)
environment=$(env | { grep -E '^(PROSPERO_|PS5_|CFLAGS=|LDFLAGS=|CC=|CXX=|JOBS=)' || true; } | LC_ALL=C sort)
stage() { python3 "$root/tools/build-stamp.py" run "$1" --text "$host" --text "$environment" "${@:2}"; }
# The probes, DXVK's extraction and the runtime pack read nothing in wine/ (each stage's scripts, checked by grep).
# The title's own sources, which no stage below reads (their products, the loader and the title, are read by content).
title_sources=(--repo-except ps5/src/ --repo-except ps5/ui/ --repo-except ps5/tools/ --repo-except ps5/assets/
    --repo-except ps5/assets.json --repo-except ps5/screenshots/ --repo-except ps5/CMakeLists.txt --repo-except examples/
    --repo-except native/ --repo-except forwarder/ --repo-except webui/)
# Wine reads none of the title's own folders (ps5/, examples/, native/, forwarder/, webui/) or the tests; what it
# reads outside the repository is the SDKs, the converter, RADV, the host build and the theme tools.
wine_inputs=(--repo-except ps5/ --repo-except examples/ --repo-except native/ --repo-except forwarder/
    --repo-except webui/ --repo-except tests/ --repo-except skills/ --repo-except .github/
    --input "$sdk" --input "$foundation" --input "$vulkan" --input "$root/.deps/ps5-opengl-sdk-1.0.2"
    --input "$root/.deps/wine-runtime/build" --input "$root/.deps/wine-runtime/source" --input "$root/.deps/llvm-mingw"
    --input "$root/.deps/magick-7.1.2-32")
# The theme folders both Wine builds and the probes write (with the same bytes) are the probes' outputs, the last of
# the three to write them, alone, so a deleted or changed one runs it again.
theme=(--output "$root/.deps/windows11-theme" --output "$root/.deps/windows11-cursors" --output "$root/.deps/mihawk-icons")
# The two Wine builds side by side: most of a Wine build is single-threaded (patching, generators, the PRX links, its
# report), and the two share only the theme generators, which they take turns at (build-wine-ps5.sh's theme.lock).
# The graphics foundation both export is set up first, once. Each one's output goes to its own log.
bash "$root/tools/setup-graphics-foundation.sh" > /dev/null
logs=$root/.deps/build-stamps
mkdir -p "$logs"
stage wine-ps5 "${wine_inputs[@]}" --output "$root/.deps/wine-ps5" -- \
    "$root/tools/build-wine-ps5.sh" --work "$root/.deps/wine-ps5" --prx-foundation "$foundation" --radv "$vulkan" \
    --jobs "$jobs" > "$logs/wine-ps5.log" 2>&1 &
wine_job=$!
stage wine-gpu-broker "${wine_inputs[@]}" --output "$root/.deps/wine-gpu-broker" -- \
    "$root/tools/build-wine-ps5.sh" --work "$root/.deps/wine-gpu-broker" --no-opengl --prx-foundation "$foundation" \
    --radv "$vulkan" --jobs "$jobs" > "$logs/wine-gpu-broker.log" 2>&1 &
broker_job=$!
wine_status=0 broker_status=0
wait "$wine_job" || wine_status=$?
wait "$broker_job" || broker_status=$?
for build in wine-ps5:$wine_status wine-gpu-broker:$broker_status; do
    if [ "${build#*:}" != 0 ]; then
        tail -n 30 "$logs/${build%:*}.log" >&2
        echo "the ${build%:*} build failed; its whole log is $logs/${build%:*}.log" >&2
    else
        grep -h '^build-stamp:' "$logs/${build%:*}.log" || true
    fi
done
[ "$wine_status" = 0 ] && [ "$broker_status" = 0 ] || exit 1
cmp -s "$root/.deps/wine-ps5/source-stamp" "$root/.deps/wine-gpu-broker/source-stamp" ||
    { echo "the two runtime builds are not from one patch series" >&2; exit 1; }

bash "$root/tools/link_gpu_client.sh" "$vulkan" "$root/.deps/gpu-wsi-client" "$foundation/build/host/ps5-native-tool" \
    "$sdk" 0 0 1

bash "$root/tools/build-payload-loader.sh"
bash "$root/tools/build-graphics-process-probe.sh"
stage wine-probes "${title_sources[@]}" --repo-except wine/ --input "$root/.deps/wine-runtime/build" --input "$root/.deps/wine-runtime/source" \
    --input "$root/.deps/llvm-mingw" --input "$root/.deps/magick-7.1.2-32" --output "$root/.deps/probes" "${theme[@]}" -- \
    bash "$root/tools/build-wine-probe.sh"
bash "$root/tools/build-mihawk-browser.sh"

# A build folder configured before the broker was the default keeps it off in its cache. A fresh one is configured by
# ps5/tools/build.sh (Ninja, the broker on by default): configuring it here first chose Makefiles, and build.sh's Ninja
# then refused the folder (build-production-own-3, 2026-10-11).
if [ -f "$root/build/ps5/build.ninja" ]; then
    cmake -S "$root/ps5" -B "$root/build/ps5" -DPS5_GPU_BROKER=ON -DPS5_VULKAN_DIR="$vulkan" > /dev/null
fi
PS5_VULKAN_DIR="$vulkan" bash "$root/ps5/tools/build.sh"
# The helper loader is the payload one (tools/build-payload-loader.sh, staged by stage-wine.py); the title folder
# carries no other since the Sony-managed route was removed.
rm -f "$root/dist/$title/wine-child.bin"

cp "$daemon/lapy-root-daemon.elf" "$root/dist/$title/lapy-root-daemon.elf"
# Mesa's WGL over Zink, OpenGL for programs in helper processes; stage-wine.py puts it in the runtime.
bash "$root/tools/build-mesa-windows-zink.sh"
bash "$root/tools/build-mesa-windows-zink-i686.sh"
# The title's own licence and notices, and those of what eboot.bin links and the Lapy daemon (tools/stage-licences.py).
# After Mesa's build: the title's Mesa licence is read from its source, which a fresh clone has only from then on.
python3 "$root/tools/stage-licences.py" title "$root/dist/$title"
python3 "$root/tools/build-dxvk.py" --jobs "$jobs"
# VKD3D-Proton's d3d12 and d3d12core from source with platform/vkd3d-proton/patches (feature level 12_0 on tier 1 tiled
# resources); stage-wine.py stages them over GE's prebuilt builds of the same revision.
python3 "$root/tools/build-vkd3d.py" --jobs "$jobs"
# The release DXVK and VKD3D-Proton, from the pinned GE-Proton archive (tools/fetch-all.sh fetches it).
stage dxvk-release "${title_sources[@]}" --repo-except wine/ --input "$root/.deps/GE-Proton11-7-x86_64.tar.gz" --output "$root/.deps/dxvk-release" -- \
    python3 "$root/tools/extract-dxvk.py"
# Read-only harness adapter built from the production control parser.
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/src" \
    "$root/tools/controls-id.c" "$root/src/pw_controls.c" -o "$root/.deps/controls-id"
# Staging reads most of .deps (tools/stage-wine.py's DEPS paths), the helper loader and Wine's licences: all of it but
# what is written from here on, and of the Wine checkouts the licences, is its input.
staging_inputs=(--input "$root/build/proton-payload-child.elf" --input "$root/.deps/wine-proton/COPYING.LIB"
    --input "$root/.deps/wine-proton/LICENSE" --input "$root/.deps/wine-proton/AUTHORS")
for entry in "$root"/.deps/*; do
    case ${entry##*/} in
    staged | runtime-pack | build-stamps | gate | tmp | wine-proton | wine-staging) ;;
    # Of Meson's builds it reads the products, never the build folders' own records (rewritten by every run).
    dxvk-patched | vkd3d-patched) staging_inputs+=(--input "$entry/manifest.json" --input "$entry/source"
        --input "$entry/x86_64" --input "$entry/i386") ;;
    mesa-foundation-wgl*) staging_inputs+=(--input "$entry/manifest.json" --input "$entry/source"
        --input "$entry/build/src/gallium/targets") ;;
    *) staging_inputs+=(--input "$entry") ;;
    esac
done
stage staged "${title_sources[@]}" "${staging_inputs[@]}" --output "$root/.deps/staged" -- python3 "$root/tools/stage-wine.py"
# Every payload ELF the install ships must accept the same firmwares in its crt, incl. 10.01 and 13.40: a payload
# whose crt rejects the running firmware faults in _start before main, so the console silently never gets /data
# (the v0.40 daemon on 13.40, 2026-10-20). tests/test_payload_firmware.py covers the reader on the host.
python3 "$root/tools/check-payload-firmware.py" --check \
    "$root/dist/$title/lapy-root-daemon.elf" "$root/.deps/staged/run/proton-payload-child.elf"
# The runtime this title was built with: the launcher compares the installed runtime's stamp with this copy.
cp "$root/.deps/staged/runtime/ge11-7/runtime-stamp" "$root/dist/$title/runtime-stamp"
# The title's own helper: the title starts this copy (native/proton_wine.cpp), never what an older install left in
# /data/ps5proton/run, so a title and its helper are always one build.
cp "$root/.deps/staged/run/proton-payload-child.elf" "$root/dist/$title/proton-payload-child.elf"
# The runtime's pieces (dist/runtime) and their manifest, pinned in the title folder: what the title installs from.
stage runtime-pack "${title_sources[@]}" --repo-except wine/ --input "$root/.deps/staged" --output "$root/dist/runtime" --output "$root/dist/$title/runtime-pack.txt" -- \
    python3 "$root/tools/pack-runtime.py" --title "$root/dist/$title"
# The source this build was made from, for the commit gate (tools/gate.py).
python3 "$root/tools/gate.py" record
echo "production build: dist/$title, dist/runtime, .deps/staged"
