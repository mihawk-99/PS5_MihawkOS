#!/usr/bin/env bash
# PS5 Vulkan Template - the payload SDK this title builds with, at a pinned revision.
#
#   ps5/tools/setup-sdk.sh       install it into .deps/native/ps5-payload-sdk (once a revision)
#
# The SDK is my fork, ../PS5_PayloadSDK: the upstream release with the PS5
# platform layer (libps5platform.a, include/ps5platform) installed over it. The
# pinned revision is exported with git archive, so a build never depends on the
# fork's working tree, and the SDK directory records it, as PS5 RetroArch and
# PS5 vkQuake pin theirs. RADV's archive, its link recipe and the packaging
# tools stay PS5_Vulkan's.
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cache="$root/.deps/native"
sdk="$cache/ps5-payload-sdk"
sdk_fork="${PS5_PAYLOAD_SDK_FORK:-$root/../PS5_PayloadSDK}"
# e105cf6: fdv tracks 16384 files, not 8192, so a program can hold
# about 12,000 open (the 10,000-file probe stopped at 8294 on the Pro, 2026-10-31); 9d04878 below it: the title heap in
# 2 MiB segments; 611893f: /data through the Lapy daemon (ps5_elevation); e879f15: the open-file budget (ps5_fdv); 3752be7 before it: heap waits, shared memory availability; 6b63a2a: localeconv
sdk_revision=e105cf6f25801d9e9981812a89a3b74021d2d2a2

if [[ ! -f $sdk/.ps5-sdk-revision || $(<"$sdk/.ps5-sdk-revision") != "$sdk_revision" ]]; then
    git -C "$sdk_fork" cat-file -e "$sdk_revision^{commit}" 2>/dev/null || {
        echo "the payload SDK fork at $sdk_fork does not have $sdk_revision" >&2
        exit 2
    }
    mkdir -p "$cache"
    sdk_tree=$(mktemp -d)
    git -C "$sdk_fork" archive "$sdk_revision" | tar -x -C "$sdk_tree"
    bash "$sdk_tree/platform/tools/setup-sdk.sh" "$sdk" "$sdk_revision" "$cache" >&2
    rm -rf -- "$sdk_tree"
fi
[[ -x $sdk/bin/prospero-lld && -d $sdk/target/include ]] || { echo "the pinned PS5 payload SDK is incomplete" >&2; exit 2; }
echo "$sdk"
