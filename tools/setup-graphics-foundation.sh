#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Export only the pinned graphics foundation; never use its mutable working tree.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# The git checkout the pinned revision is exported from (read only); never a build folder.
fork=${PS5_VULKAN_GIT:-$root/../PS5_Vulkan}
# e71d115: f410e67 plus the 44 graphics patches PS5_Proton carried in platform/graphics/patches until 2026-11-01, as
# PS5_Vulkan's own commits (the owner's rule: fixes go to his upstream repos), and the broker client's Mesa patches read
# from PS5_Mesa's ps5/broker overlay (d62271ca) instead of copies here. Any later carried patch still applies here.
# 6c52e6f0: e71d115 plus a client in the owner's process giving host memory to the GPU (Mesa overlay 2a53a8e, PS5_Mesa's
# host-memory-import branch; Wine 2425 keeps the access). Not pushed until the console proves d3d12-existing-heap64-title.
# 04c52ce: 6c52e6f0 plus captures that read the frame scanned out when the fast copy is on (the fast-copy-capture case).
revision=04c52ce988565e1358e0a67242b728804c666c10
out=$root/.deps/graphics-foundation
shopt -s nullglob
patches=("$root/platform/graphics/patches/"*.patch)
shopt -u nullglob
patch_stamp=$(cat "${patches[@]}" "$root/src/pw_window_snapshot.h" "$root/src/pw_window_page.h" "$root/src/pw_window_scene.h" "$root/src/pw_window_effects.h" "$root/src/pw_frame_pacing.h" "$root/src/pw_window_direct.h" "$root/src/pw_gpu_timing.h" "$root/src/pw_display_copy.h" "$root/src/pw_display_capture.h" "$root/src/pw_log_drain.h" "$root/src/pw_display_rate.h" "$root/src/pw_present_retry.h" "$root/src/pw_gpu_identity.h" | sha256sum | cut -d' ' -f1)
if [[ ! -f $out/.revision || $(cat "$out/.revision") != "$revision" || ! -f $out/.patches-sha256 ||
      $(cat "$out/.patches-sha256") != "$patch_stamp" || ! -f $out/tooling/gpu-broker/radv_ps5_platform.h ]]; then
    mkdir -p "$root/.deps"
    staging=$(mktemp -d "$root/.deps/graphics-foundation.XXXXXX")
    git -C "$fork" archive "$revision" tooling/gpu-broker tools/build-gpu-broker-client.sh \
        tools/build-mesa-windows-zink.sh tools/radv-link.sh tooling/native/ps5-pie.ld | tar -x -C "$staging"
    git -C "$fork/../PS5_Mesa" show 1ba9874f478657ccf6da72ce55f0260164cc0bd0:src/amd/vulkan/winsys/ps5/radv_ps5_platform.h > "$staging/tooling/gpu-broker/radv_ps5_platform.h"
    for patch in "${patches[@]}"; do
        patch --batch --forward -d "$staging" -p1 < "$patch" >&2
    done
    cp "$root/src/pw_window_snapshot.h" "$root/src/pw_window_page.h" "$root/src/pw_window_scene.h" "$root/src/pw_window_effects.h" "$root/src/pw_frame_pacing.h" "$root/src/pw_window_direct.h" "$root/src/pw_gpu_timing.h" "$root/src/pw_display_copy.h" "$root/src/pw_display_capture.h" "$root/src/pw_log_drain.h" "$root/src/pw_display_rate.h" "$root/src/pw_present_retry.h" "$root/src/pw_gpu_identity.h" "$staging/tooling/gpu-broker/"
    printf '%s\n' "$revision" > "$staging/.revision"
    printf '%s\n' "$patch_stamp" > "$staging/.patches-sha256"
    rm -rf "$out"
    mv "$staging" "$out"
fi
printf '%s\n' "$out"
