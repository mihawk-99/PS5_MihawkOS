#!/usr/bin/env bash
# Build the Sony local-process Wine helper (/app0/wine-local-child.bin).
#
#   tools/build-local-child.sh [output]
#
# Links the shared proton_wine_child.c runner into a headless PS5 local process
# SELF importing only libkernel.sprx and libSceLibcInternal.sprx (no graphics
# libraries, no libkernel_sys). Spawned by the title via
# sceSystemServiceAddLocalProcess so 64-bit games inherit the title's expanded
# page-table pool (up to 65,536 CPU PT) and 1 GiB flexible memory while staying
# isolated OS processes.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=$(bash "$root/ps5/tools/setup-sdk.sh")
vulkan=$(bash "$root/tools/setup-pinned-radv.sh" | tail -n 1)
helper_tool=$(python3 "$root/tools/build-helper-converter.py")
output=${1:-$root/build/wine-local-child.bin}
work="$root/build/local-child"
mkdir -p "$work" "$(dirname -- "$output")"

sources=(
    "$root/native/proton_local_child.c"
    "$root/src/pw_wine_start.c"
    "$root/src/pw_environment.c"
    "$root/wine/ps5/pw_wine_prx.c"
    "$root/wine/ps5/pw_wine_process_io.c"
    "$root/src/pw_memory_file.c"
    "$root/src/pw_memory_service.c"
    "$root/src/pw_vm_census.c"
    "$root/src/pw_helper_sched.c"
    "$root/src/pw_cpu_placement.c"
    "$root/src/pw_firmware.c"
    "$root/native/pw_firmware_ps5.c"
    "$root/src/pw_fd_census.c"
)

cflags=(
    -std=gnu11 -O2 -Wall -Wextra -g -fPIC -fno-omit-frame-pointer -funwind-tables
    -DPROTON_LOCAL_CHILD=1
    -I"$root/src" -I"$root/wine/ps5" -I"$root/native"
)

build_id=$("$sdk/bin/prospero-clang" -MM "${cflags[@]}" "${sources[@]}" |
    tr ' \\' '\n\n' | grep -E '\.(c|h|inc)$' | sort -u | xargs cat | sha256sum | cut -c1-16)
cflags+=(-DPW_HELPER_BUILD="\"$build_id\"")

objs=()
for src in "${sources[@]}"; do
    base=$(basename "$src" .c)
    obj="$work/$base.o"
    "$sdk/bin/prospero-clang" "${cflags[@]}" -c "$src" -o "$obj"
    objs+=("$obj")
done

"$sdk/bin/prospero-lld" -pie --eh-frame-hdr \
    -T "$vulkan/tooling/native/ps5-pie.ld" \
    --version-script "$vulkan/tooling/native/app-symbols.map" \
    -e _start --build-id=md5 \
    -o "$work/llvm-pie.elf" \
    "${objs[@]}" \
    "$sdk/target/lib/libps5platform.a" \
    --as-needed \
    "$sdk/target/lib/libkernel.so" \
    "$sdk/target/lib/libSceLibcInternal.so"

# Verify headless dependency policy: only libkernel.sprx and libSceLibcInternal.sprx.
needed=$(llvm-readelf -d "$work/llvm-pie.elf" | awk -F'[][]' '/\(NEEDED\)/{print $2}' | LC_ALL=C sort | tr '\n' ' ')
expected="libSceLibcInternal.sprx libkernel.sprx "
if [[ "$needed" != "$expected" ]]; then
    echo "wine-local-child.bin dependencies differ from policy: got [$needed], expected [$expected]" >&2
    exit 1
fi

"$helper_tool" link --in "$work/llvm-pie.elf" --out "$work/wine-local-child.elf" \
    --stub-dir "$sdk/target/lib" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 \
    --file-name wine-local-child.bin \
    --preload-prx-flags 0x8000000000000002

"$helper_tool" self --sign --in "$work/wine-local-child.elf" --out "$output" --magic 0x1D3D154F
cp "$work/wine-local-child.elf" "${output%.bin}.elf"
echo "built $output (build_id=$build_id)"
sha256sum "$output"
