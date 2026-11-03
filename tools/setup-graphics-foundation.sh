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
# 8cc2e4d: 04c52ce plus an application's panel over every frame (ps5_gpu_broker_set_overlay): the own keyboard at the
# bottom of the screen with the game live above it (the Alpha 7 hotfix).
# 6224e4c: 8cc2e4d plus a capture of the frame shown again with the panel when the game waits (osk-own-open64).
# 47b3fc8: 6224e4c plus the panel's lock wait and re-show time in the broker's log (the keyboard's input lag).
# 98bf667: 47b3fc8 plus the panel's frame shadow: a re-show takes a few ms, not 137 (osk-lag-desktop-base-2026-11-01-01).
# 8bbd744: 98bf667 plus the see-through panel (the keyboard's Frosted Meadow look): blended over the frame blurred under it,
# bubbles in its margins, and a refresh that moves them while the game waits.
# 64e1050: 8bbd744 plus the panel's cost in the broker's log (gpu-overlay compose avg_us, max_us).
# a62b05d: 64e1050 plus a panel change shown at once over a waiting game (the bubbles' re-shows do not hold it back).
# 66dad9f: a62b05d plus the blur made every second frame (half the cost) and the rare glass sweep (the owner's picks).
# 1717280: 66dad9f plus FIFO held to the display (a FIFO present waits until its frame is shown; MAILBOX and
# IMMEDIATE offered and free), a fast-copy capture copied out whole, and a scaled frame scaled into rows then
# line-copied (PS5_Vulkan display-pacing; vkp-*-base-2026-11-02-01, display-pacing-base-2026-11-02-01).
# ebc931d: 1717280 plus the control overlay's statistics HUD (a second small panel, ps5_gpu_broker_set_hud, and the GPU's
# busy time for it) and a panel's bubbles in its own margins (the overlay's panel down the right of the screen).
# ace98a9: ebc931d plus the open panel kept to the 120 Hz budget: its blend cached and shown with the bubbles every frame,
# its blur made a band a frame (4 at 120 Hz, 2 at 60), so frames keep to the refresh (run-battery panel-120-title).
revision=ace98a99460c95a8d2821a9e33935c7b71070f7f
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
