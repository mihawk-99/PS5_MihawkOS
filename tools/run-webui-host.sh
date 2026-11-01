#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The WebUI on this PC, for the page's checks in a browser: builds tests/webui_host_main.cpp with the title's routes
# and HTTP glue over the host build of libmicrohttpd, makes a sample data folder (a few library entries, a prefix with
# games, logs) and a title folder (the repository's webui/, the launcher's Inter, the icon), then serves them.
#
#   tools/run-webui-host.sh [PORT] [DATA]    default port 6767; DATA: a data folder to serve instead of the sample
#   WEBUI_HOST_BUILD_ONLY=1 tools/run-webui-host.sh    builds the server and prints its path (tests/test_webui_http.py)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
port=${1:-6767}
out="$root/build/webui-host"
mkdir -p "$out"
mhd=$(bash "$root/tools/build-webui-http.sh" host)
mhd_include="$root/.deps/webui/libmicrohttpd-1.0.10/src/include"
objects=()
# The launcher's C files it saves and browses through, as they are (their own tests hold them to -Werror)
for source in pw_controller pw_controls pw_detailed_logs pw_log_sessions pw_game_mode pw_high_refresh pw_drive_browse; do
    "${CC:-cc}" -std=gnu11 -O2 -g -w -c "$root/src/$source.c" -o "$out/$source.o"
    objects+=("$out/$source.o")
done
"${CXX:-c++}" -std=gnu++17 -O2 -g -Wall -Wextra -Werror -I"$root/src" -I"$root/external/tinygltf" -I"$mhd_include" \
    "$root/tests/webui_host_main.cpp" "$root/src/pw_webui.cpp" "$root/src/pw_webui_http.cpp" "${objects[@]}" "$mhd" \
    -lpthread -o "$out/webui-host"
if [[ ${WEBUI_HOST_BUILD_ONLY:-} == 1 ]]; then echo "$out/webui-host"; exit 0; fi
title="$out/title"
rm -rf "$title"
mkdir -p "$title/assets/hui/fonts" "$title/sce_sys"
# Copies, as ps5/tools/link-title.sh makes them: the server opens no symbolic link (restart it after a page edit)
cp -r "$root/webui" "$title/webui"
mkdir -p "$title/webui/flags"
cp "$root"/ps5/assets/flags/*.svg "$title/webui/flags/"
cp "$root/.deps/hui/third_party/fonts/Inter-Regular.ttf" "$root/.deps/hui/third_party/fonts/Inter-SemiBold.ttf" \
    "$title/assets/hui/fonts/"
cp "$root/ps5/sce_sys/icon0.png" "$title/sce_sys/icon0.png"
printf 'c8a3443d83d0000000000000\n' > "$title/runtime-stamp"
mkdir -p "$title/forwarder" "$title/sce_module"
printf 'TILE' > "$title/forwarder/eboot.bin"
printf 'ELF' > "$title/forwarder/launch.elf"
printf 'PRX' > "$title/sce_module/libc.prx"
data=${2:-}
if [[ -z $data ]]; then
    data="$out/data"
    rm -rf "$data"
    mkdir -p "$data/games/nightwalk" "$data/games/broken" "$data/logs/sessions/0" \
        "$data/prefixes/mihawk/drive_c/Games/Nightwalk/bin" "$data/prefixes/mihawk/drive_c/Games/Puzzle Box" \
        "$data/prefixes/mihawk/drive_c/users/Mihawk/Saved Games/Nightwalk" "$data/prefixes/mihawk/drive_c/users/Mihawk/AppData" \
        "$data/prefixes/second/drive_c/users/Mihawk" "$data/prefixes/mihawk/drive_c/users/Mihawk/AppData/Roaming/Nightwalk" \
        "$data/prefixes/mihawk/drive_c/users/Mihawk/Documents"
    printf 'MZ uninstaller' > "$data/prefixes/mihawk/drive_c/Games/Puzzle Box/unins000.exe"
    printf 'config' > "$data/prefixes/mihawk/drive_c/users/Mihawk/AppData/Roaming/Nightwalk/options.cfg"
    printf 'slot one' > "$data/prefixes/mihawk/drive_c/users/Mihawk/Saved Games/Nightwalk/slot1.sav"
    printf 'volume=8\n' > "$data/prefixes/mihawk/drive_c/users/Mihawk/Saved Games/Nightwalk/settings.ini"
    printf '{\n  "name": "Nightwalk",\n  "detail": "A sample entry",\n  "executable": "C:\\\\Games\\\\Nightwalk\\\\bin\\\\Nightwalk.exe",\n  "env": ["WINE_PS5_CONTROLLER=dualsense"]\n}\n' \
        > "$data/games/nightwalk/game.json"
    printf '{\n  "name": "Broken entry"\n}\n' > "$data/games/broken/game.json"
    printf 'MZ' > "$data/prefixes/mihawk/drive_c/Games/Nightwalk/bin/Nightwalk.exe"
    printf 'MZ' > "$data/prefixes/mihawk/drive_c/Games/Puzzle Box/PuzzleBox.exe"
    printf '[MihawkOS] starts\n[MihawkOS] launcher: 3 game(s) at 192.0.2.10\n' > "$data/logs/title-last.log"
    printf 'wine: ok\n' > "$data/logs/sessions/0/wine.log"
fi
echo "data: $data"
exec "$out/webui-host" "$data" "$title" "$port"
