#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A game that ships the Direct3D 12 Agility SDK (it exports D3D12SDKVersion and D3D12SDKPath and carries its own
# D3D12Core.dll in D3D12\x64, and in a second case beside the exe too) must still get VKD3D-Proton's d3d12 and
# d3d12core from system32: tests/d3d12-agility-game.c with a dummy D3D12Core.dll (tests/d3d12-agility-dummy-core.c),
# run by the host's Wine (.deps/wine-runtime) in a copy of the check prefix (.deps/prefix-probe) holding
# tools/build-vkd3d.py's DLLs, with the title's overrides for d3d12 and d3d12core (dxgi stays Wine's: the prefix has
# no DXVK, and d3d12core imports dxgi). No device is made and no Vulkan driver can be found
# (VK_ICD_FILENAMES names none), so the PC's GPU is never touched. PASS when both cases load d3d12 and d3d12core from
# system32, SetSDKVersion answers S_OK, and the dummy in D3D12\x64 is never loaded.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
wine=$root/.deps/wine-runtime
vkd3d=$root/.deps/vkd3d-patched/x86_64
[[ -f $vkd3d/d3d12core.dll ]] || { echo "no $vkd3d: run tools/build-vkd3d.py" >&2; exit 2; }
[[ -f $root/.deps/prefix-probe/system.reg ]] || { echo "no .deps/prefix-probe: run tools/fetch-all.sh" >&2; exit 2; }
work=$root/.deps/d3d12-agility-test
rm -rf "${work:?}"
mkdir -p "$work"
flags=(--target=x86_64-windows --no-default-config -fuse-ld=lld -nostdlib -O2 -Wall -Wextra -Werror
       -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt")
libs=("$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a")
clang "${flags[@]}" -Wl,/entry:mainCRTStartup -Wl,/subsystem:console "$root/tests/d3d12-agility-game.c" "${libs[@]}" \
    -o "$work/agility-game.exe"
clang "${flags[@]}" -shared -Wl,/entry:DllMainCRTStartup -Wl,/dll "$root/tests/d3d12-agility-dummy-core.c" "${libs[@]}" \
    -o "$work/D3D12Core.dll"
# Its contents, never the link: in a worktree .deps/prefix-probe is often another checkout's, which must not change.
mkdir "$work/prefix"
cp -a "$root/.deps/prefix-probe/." "$work/prefix/"
cp "$vkd3d/d3d12.dll" "$vkd3d/d3d12core.dll" "$work/prefix/drive_c/windows/system32/"
status=0
for layout in subfolder beside; do
    game=$work/prefix/drive_c/Games/$layout
    mkdir -p "$game/D3D12/x64"
    cp "$work/agility-game.exe" "$game/"
    cp "$work/D3D12Core.dll" "$game/D3D12/x64/"
    [[ $layout == beside ]] && cp "$work/D3D12Core.dll" "$game/"
    out=$(cd "$game" && env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$work/prefix" WINEDEBUG=-all \
          WINEDLLOVERRIDES='d3d12,d3d12core=n' \
          VK_ICD_FILENAMES=/nonexistent.json VK_DRIVER_FILES=/nonexistent.json \
          timeout 120 "$wine/build/wine" "C:\\Games\\$layout\\agility-game.exe" 2>/dev/null || true)
    printf '%s\n' "$out" | sed "s/^/$layout: /"
    grep -qi 'd3d12=C:\\windows\\system32\\d3d12.dll' <<<"$out" &&
        grep -qi 'd3d12core=C:\\windows\\system32\\d3d12core.dll' <<<"$out" &&
        grep -q 'SetSDKVersion hr=00000000' <<<"$out" &&
        ! grep -qi 'loaded from .*D3D12\\x64' <<<"$out" || { echo "$layout: FAIL"; status=1; }
done
WINEPREFIX="$work/prefix" "$wine/build/server/wineserver" -k 2>/dev/null || true
[[ $status == 0 ]] && echo "d3d12 agility: PASS (VKD3D-Proton's d3d12 and d3d12core in both layouts)"
exit $status
