#!/usr/bin/env bash
# PS5 Vulkan Template - upload dist/<TITLE_ID>/ to the console.
#
#   ps5/tools/deploy.sh [--all]
#
# PS5_Vulkan's deploy tool uploads what changed, judged by size, over the
# console's FTP server, with the address from PS5_Vulkan's .env. eboot.bin,
# runtime-stamp and runtime-pack.txt go always: the stamp is 65 bytes whatever
# runtime it names (loud-failures-2026-10-18: a new stamp was skipped). So do the
# forwarder's programs: a change to a program's code can keep its file's size,
# as its segments are page-aligned. Generated files are built to be reproducible
# (tools/build-forwarder.sh, ps5/tools/generate_assets.py's faces). The console
# guard comes first (no write on storage damage, a dropped console or a running
# title) and last (ps5vkctl's sync); then tools/verify-title.py reads every
# file back by sha256, which the next launch requires.
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: MIT
set -euo pipefail
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}" && pwd)
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$ps5/sce_sys/param.json")
[[ -d $root/dist/$title_id ]] || { echo "no dist/$title_id: run ps5/tools/build.sh first" >&2; exit 2; }
# The console's address stays out of anything printed (AGENTS.md rule 5).
mask() { sed -uE 's/[0-9]{1,3}(\.[0-9]{1,3}){3}/<console>/g'; }
python3 "$root/tools/console_guard.py" begin-write title 2>&1 | mask
python3 "$vulkan/tools/deploy-title-folder.py" --always eboot.bin --always sce_sys/param.json --always runtime-stamp --always runtime-pack.txt \
    --always forwarder/launch.elf --always forwarder/worker.elf --always proton-payload-child.elf --always wine-local-child.bin "$root/dist/$title_id" "$@" 2>&1 | mask
python3 "$root/tools/console_guard.py" end-write 2>&1 | mask
python3 "$root/tools/verify-title.py" "$root/dist/$title_id" 2>&1 | mask
