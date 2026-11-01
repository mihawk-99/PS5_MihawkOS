#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Every Windows link takes /timestamp:0, as the theme DLLs do: the same source links to the same bytes, so a
# rebuild changes nothing it should not (docs/BUILD_SPEED.md).
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
wine=$root/.deps/wine-runtime
mkdir -p "$root/.deps/probes"
python3 "$root/tools/build-mihawk-icons.py"
python3 "$root/tools/build-wine-theme.py"
python3 "$root/tools/build-cursors.py"
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows resource_target=i686; else target=x86_64-windows arch=x86_64-windows resource_target=x86_64; fi
    printf '1 24 "%s/src/pw_appearance.manifest"\n' "$root" > "$root/.deps/probes/appearance.rc"
    "$root/.deps/llvm-mingw/bin/$resource_target-w64-mingw32-windres" \
        "$root/.deps/probes/appearance.rc" "$root/.deps/probes/appearance$bits.o"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -I"$root/.deps/windows11-theme" -I"$root/.deps/mihawk-icons" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
        "$root/src/pw_appearance.c" "$root/.deps/probes/appearance$bits.o" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/uxtheme/$arch/libuxtheme.a" "$wine/build/dlls/comctl32/$arch/libcomctl32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/appearance$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -I"$root/.deps/windows11-theme" -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
        "$root/tests/theme-notification-probe.c" "$root/.deps/probes/appearance$bits.o" \
        "$wine/build/dlls/kernel32/$arch/libkernel32.a" "$wine/build/dlls/user32/$arch/libuser32.a" \
        "$wine/build/dlls/gdi32/$arch/libgdi32.a" "$wine/build/dlls/uxtheme/$arch/libuxtheme.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/theme-notify$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" -I"$root/.deps/windows11-theme" -I"$root/src" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/shell-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/advapi32/$arch/libadvapi32.a" "$wine/build/dlls/shell32/$arch/libshell32.a" \
        "$wine/build/dlls/shlwapi/$arch/libshlwapi.a" "$wine/build/dlls/uxtheme/$arch/libuxtheme.a" \
        "$wine/build/dlls/ole32/$arch/libole32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        "$wine/build/dlls/comctl32/$arch/libcomctl32.a" "$wine/build/dlls/dwmapi/$arch/libdwmapi.a" \
        -o "$root/.deps/probes/shell$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/work-area-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/work-area$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" -I"$root/src" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/window-snapshot-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/dwmapi/$arch/libdwmapi.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/window-snapshot$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" -I"$root/src" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/window-compose-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/window-compose$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" -I"$root/src" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/window-effects-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/dwmapi/$arch/libdwmapi.a" "$wine/build/dlls/uxtheme/$arch/libuxtheme.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/window-effects$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/initial-environment-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/initial-env$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/locale-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/locale$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/display-modes-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/display-modes$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/wallpaper-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/advapi32/$arch/libadvapi32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/wallpaper$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" -I"$root/.deps/windows11-cursors" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/cursor-scheme-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/advapi32/$arch/libadvapi32.a" "$wine/build/dlls/user32/$arch/libuser32.a" \
        "$wine/build/dlls/gdi32/$arch/libgdi32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/cursor-scheme$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" -I"$root/.deps/windows11-theme" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/theme-parts-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/uxtheme/$arch/libuxtheme.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/theme-parts$bits-probe.exe"
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/monospace-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/advapi32/$arch/libadvapi32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/monospace$bits-probe.exe"
done
cp "$root/.deps/probes/appearance64-probe.exe" "$root/.deps/probes/mihawk-appearance.exe"
# No Windows mihawk.exe: the title runs the desktop session itself (src/pw_desktop_session.h).
rm -f "$root/.deps/probes/mihawk.exe"
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/desktop-session-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/dwmapi/$arch/libdwmapi.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/desktop-session$bits-probe.exe"
done
# The shell's surfaces opened from the taskbar and captured, for tools/run-shell-surfaces.py.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/shell-surfaces-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/advapi32/x86_64-windows/libadvapi32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/shell-surfaces64-probe.exe"
# Every prototype state the native shell can reach, captured in light and dark, for tools/run-shell-scorecard.py.
L=$wine/build/dlls
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console "$root/tests/shell-scorecard-probe.c" \
    "$L/kernel32/x86_64-windows/libkernel32.a" "$L/user32/x86_64-windows/libuser32.a" "$L/gdi32/x86_64-windows/libgdi32.a" \
    "$L/advapi32/x86_64-windows/libadvapi32.a" "$L/shell32/x86_64-windows/libshell32.a" "$L/shlwapi/x86_64-windows/libshlwapi.a" \
    "$L/ole32/x86_64-windows/libole32.a" "$L/comdlg32/x86_64-windows/libcomdlg32.a" "$L/dwmapi/x86_64-windows/libdwmapi.a" \
    "$L/msvcrt/x86_64-windows/libmsvcrt.a" -o "$root/.deps/probes/shell-scorecard64-probe.exe"
# The composed desktop's cost and pacing in scenes ended by captures, for tools/run-frame-pacing.py.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/frame-pacing-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/frame-pacing64-probe.exe"
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/proton-ui-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        -o "$root/.deps/probes/proton-ui$bits-probe.exe"
done
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/memory-class-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/memory-class-probe.exe"
# DNSAPI configuration, records and cleanup in both architectures.
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/dnsapi-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/ws2_32/$arch/libws2_32.a" "$wine/build/dlls/user32/$arch/libuser32.a" \
        "$wine/build/dlls/dnsapi/$arch/libdnsapi.a" "$wine/build/dlls/iphlpapi/$arch/libiphlpapi.a" \
        -o "$root/.deps/probes/dnsapi$bits-probe.exe"
done
# Shared Winsock semantics and real loopback traffic in both architectures.
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/socket-options-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/ws2_32/$arch/libws2_32.a" "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/socket-options$bits-probe.exe"
done
# NSI-backed Windows adapter discovery, with private interface data omitted.
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/network-adapters-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/ws2_32/$arch/libws2_32.a" "$wine/build/dlls/user32/$arch/libuser32.a" \
        "$wine/build/dlls/iphlpapi/$arch/libiphlpapi.a" \
        -o "$root/.deps/probes/network-adapters$bits-probe.exe"
done
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/late-library-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/late-library-probe.exe"
# The grouped battery's sequence: one launch runs many probes, each its own process (tools/run-battery.py --grouped).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/gate-sequence-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/gate-sequence.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/desktop-overlap-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    -o "$root/.deps/probes/desktop-overlap-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/guest-address-space-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/guest-address-space-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/wine-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/wine-probe.exe"
# The 32-bit probe (docs/IA32_DESIGN.md, M3.4), from the runtime's i386 import libraries.
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/pe32-probe.c" "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/ntdll/i386-windows/libntdll.a" \
    -o "$root/.deps/probes/pe32-probe.exe"
# An old installer's link (/fixed at 0x400000): the gate's fixed-base32 case (tools/run-battery.py).
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console -Wl,/fixed -Wl,/base:0x400000 \
    "$root/tests/fixed-base32-probe.c" "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    -o "$root/.deps/probes/fixed-base32-probe.exe"
# What a 32-bit program needs beyond its process (docs/IA32_DESIGN.md, M4 step 2): child, registry, audio, XInput.
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/wow64-probe.c" "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/advapi32/i386-windows/libadvapi32.a" "$wine/build/dlls/winmm/i386-windows/libwinmm.a" \
    -o "$root/.deps/probes/wow64-probe.exe"
# Vulkan from a Windows program, 32- and 64-bit (docs/IA32_DESIGN.md, M4 step 3): mapped memory and a GPU fill.
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/vk-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        -o "$root/.deps/probes/vk$bits-probe.exe"
done
# One crossing's cost by kind (docs/IA32_DESIGN.md, M4 step 1): the same source, 32- and 64-bit.
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/cross-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/ntdll/$arch/libntdll.a" \
        -o "$root/.deps/probes/cross$bits-probe.exe"
done
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d11-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/d3d11/x86_64-windows/libd3d11.a" \
    "$wine/build/dlls/d3dcompiler_47/x86_64-windows/libd3dcompiler.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/d3d11-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/memory-cap-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/memory-cap64-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/desktop-items-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/shell32/x86_64-windows/libshell32.a" "$wine/build/dlls/ole32/x86_64-windows/libole32.a" \
    "$wine/build/dlls/shlwapi/x86_64-windows/libshlwapi.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    -o "$root/.deps/probes/desktop-items64-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/game-focus-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/shell32/x86_64-windows/libshell32.a" "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/game-focus64-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/hid-enum-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/ntdll/x86_64-windows/libntdll.a" "$wine/build/dlls/setupapi/x86_64-windows/libsetupapi.a" "$wine/build/dlls/hid/x86_64-windows/libhid.a" \
    "$wine/build/dlls/dinput8/x86_64-windows/libdinput8.a" "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/hid-enum64-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/agent-permissions-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/advapi32/x86_64-windows/libadvapi32.a" \
    -o "$root/.deps/probes/agent-permissions64-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/desktop-view-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/shell32/x86_64-windows/libshell32.a" "$wine/build/dlls/advapi32/x86_64-windows/libadvapi32.a" \
    "$wine/build/dlls/ole32/x86_64-windows/libole32.a" "$wine/build/dlls/shlwapi/x86_64-windows/libshlwapi.a" \
    "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    -o "$root/.deps/probes/desktop-view64-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/section-memory-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/section-memory-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/gpu-host-memory-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/d3d11/x86_64-windows/libd3d11.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/gpu-host-memory-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/xinput-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/xinput-probe.exe"

clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/hid-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/setupapi/x86_64-windows/libsetupapi.a" \
    "$wine/build/dlls/hid/x86_64-windows/libhid.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/hid-probe.exe"
# The same, as a 32-bit program: the DualSense through the i386 HID drivers in its own process (M4 step 2).
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/hid-probe.c" \
    "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/user32/i386-windows/libuser32.a" \
    "$wine/build/dlls/setupapi/i386-windows/libsetupapi.a" \
    "$wine/build/dlls/hid/i386-windows/libhid.a" \
    "$wine/build/dlls/msvcrt/i386-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/hid32-probe.exe"

for probe in sync-probe exit-probe priority-probe hang-probe vm-protect-probe; do
    clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/$probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
        -o "$root/.deps/probes/$probe.exe"
done
# A game folder with no game.json (tools/run-games-autodetect.py): where it runs from, then it closes its session.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/games-autodetect-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" -o "$root/.deps/probes/games-autodetect-probe.exe"
# A program opened from File Explorer starts in its own folder (shell32 1184): the probe opens itself through an
# ExplorerBrowser's view, or a named program the same way.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/shell-open-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/shell32/x86_64-windows/libshell32.a" \
    "$wine/build/dlls/shlwapi/x86_64-windows/libshlwapi.a" "$wine/build/dlls/ole32/x86_64-windows/libole32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" -o "$root/.deps/probes/shell-open-probe.exe"
# The same protection cases in a 32-bit program (WoW64 code conversion, ProcessExecuteFlags); libntdll.a supplies
# __alldiv and __chkstk.
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/vm-protect-probe.c" "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/ntdll/i386-windows/libntdll.a" \
    -o "$root/.deps/probes/vm-protect32-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/window-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/window-probe.exe"
# Executable read-write memory, its own code patched, and a suspended child's memory set up before it runs
# (tests/remote-memory-probe.c), as Chromium's sandbox, crash reporters and hooking libraries do.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/remote-memory-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/remote-memory-probe.exe"
# Names and a TCP connection through Winsock (tests/dns-probe.c).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/dns-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/ws2_32/x86_64-windows/libws2_32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/dns-probe.exe"
# HTTPS through WinHTTP: schannel over GnuTLS and crypt32's root store (tests/tls-probe.c).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/tls-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/winhttp/x86_64-windows/libwinhttp.a" \
    "$wine/build/dlls/crypt32/x86_64-windows/libcrypt32.a" "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/tls-probe.exe"
# A prefix wineboot made, the service manager and an MSI installer (tests/prefix-probe.c, tools/run-prefix-test.py).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/prefix-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/advapi32/x86_64-windows/libadvapi32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/prefix-probe.exe"
# A GDI window for the title's desktop, the pointer and clicks (tests/desktop-probe.c).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/desktop-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    -o "$root/.deps/probes/desktop-probe.exe"
# A window that holds system memory until the title's guard asks it to close (tests/memory-guard-probe.c).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/memory-guard-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    -o "$root/.deps/probes/memory-guard-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/fs-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/fs-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/stack-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/stack-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/gl-exit-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    "$wine/build/dlls/opengl32/x86_64-windows/libopengl32.a" \
    -o "$root/.deps/probes/gl-exit-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/gl-present-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    "$wine/build/dlls/opengl32/x86_64-windows/libopengl32.a" \
    -o "$root/.deps/probes/gl-present-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/gl-sprite-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    "$wine/build/dlls/opengl32/x86_64-windows/libopengl32.a" \
    -o "$root/.deps/probes/gl-sprite-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/mem-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/mem-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/gl-vertex-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    "$wine/build/dlls/opengl32/x86_64-windows/libopengl32.a" \
    -o "$root/.deps/probes/gl-vertex-probe.exe"
# 32-bit rendering (docs/IA32_DESIGN.md, M4 step 3): the same two probes built for i386, OpenGL through the i686
# Mesa/Zink and Direct3D 11 through the i386 DXVK, run in a helper.
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/gl-vertex-probe.c" "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/user32/i386-windows/libuser32.a" "$wine/build/dlls/gdi32/i386-windows/libgdi32.a" \
    "$wine/build/dlls/opengl32/i386-windows/libopengl32.a" \
    -o "$root/.deps/probes/gl-vertex32-probe.exe"
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d11-probe.c" \
    "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/user32/i386-windows/libuser32.a" \
    "$wine/build/dlls/d3d11/i386-windows/libd3d11.a" \
    "$wine/build/dlls/d3dcompiler_47/i386-windows/libd3dcompiler.a" \
    "$wine/build/dlls/msvcrt/i386-windows/libmsvcrt.a" \
    "$wine/build/dlls/ntdll/i386-windows/libntdll.a" \
    -o "$root/.deps/probes/d3d11-32-probe.exe"
# Direct3D 12's feature levels, as a game gets them (tests/d3d12-levels-probe.c, platform/vkd3d-proton/patches/0001).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d12-levels-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/d3d12/x86_64-windows/libd3d12.a" \
    "$wine/build/dlls/dxgi/x86_64-windows/libdxgi.a" \
    -o "$root/.deps/probes/d3d12-levels-probe.exe"
# How many files a program may hold open for writing (tests/many-files-probe.c, wine/ps5/pw_wine_codeshare.h's release).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/many-files-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/many-files-probe.exe"
# Whether a game's Wine output keeps the end of the session's logs (tests/log-flood-probe.c, src/pw_capped_log.h).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/log-flood-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/log-flood-probe.exe"
# A game whose DLL is missing (tests/missing-import-probe.c): its import library names a DLL no folder has.
printf 'LIBRARY pw-missing-import.dll\nEXPORTS\npw_missing_import\n' >"$root/.deps/probes/pw-missing-import.def"
llvm-dlltool -m i386:x86-64 -d "$root/.deps/probes/pw-missing-import.def" -l "$root/.deps/probes/pw-missing-import.lib"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/missing-import-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/d3d11/x86_64-windows/libd3d11.a" "$root/.deps/probes/pw-missing-import.lib" \
    -o "$root/.deps/probes/missing-import-probe.exe"
# What a game in the title learns of its live parent in another helper (tests/parent-query-probe.c, Wine patch 2426).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/parent-query-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/d3d11/x86_64-windows/libd3d11.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/parent-query-probe.exe"
# How many Direct3D 12 reserved resources a game may hold (tests/d3d12-sparse-probe.c, graphics patch 0044).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d12-sparse-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/d3d12/x86_64-windows/libd3d12.a" \
    -o "$root/.deps/probes/d3d12-sparse-probe.exe"
# Direct3D 12's shared resources without Win32 external memory (tests/d3d12-shared-probe.c, Wine patch 2407).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d12-shared-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/d3d12/x86_64-windows/libd3d12.a" \
    -o "$root/.deps/probes/d3d12-shared-probe.exe"
# Direct3D 12's heaps over a program's own memory (tests/d3d12-existing-heap-probe.c, Wine patch 2425).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d12-existing-heap-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/d3d12/x86_64-windows/libd3d12.a" \
    -o "$root/.deps/probes/d3d12-existing-heap-probe.exe"
# A texture shared between a Direct3D 9Ex and a Direct3D 11 device of one process (tests/d3d9ex-d3d11-share-probe.c).
# The on-screen keyboard's text reaching a game window without a person (tests/osk-typed-probe.c; WINE_PS5_OSK_TEST=type).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/osk-typed-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" -o "$root/.deps/probes/osk-typed-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/d3d9ex-d3d11-share-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/d3d9/x86_64-windows/libd3d9.a" \
    "$wine/build/dlls/d3d11/x86_64-windows/libd3d11.a" -o "$root/.deps/probes/d3d9ex-d3d11-share-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/stdout-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/stdout-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/crash-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/crash-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/nt-file-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/nt-file-probe.exe"
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/thread-leak-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/thread-leak-probe.exe"

# Large texture uploads, pixel readback and release/reuse through production Zink, identical PE32 and PE64 workloads.
clang --target=i686-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:windows \
    "$root/tests/mouse-controls-probe.c" "$wine/build/dlls/kernel32/i386-windows/libkernel32.a" \
    "$wine/build/dlls/user32/i386-windows/libuser32.a" "$wine/build/dlls/gdi32/i386-windows/libgdi32.a" \
    -o "$root/.deps/probes/mouse-controls32-probe.exe"

for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/input-message-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/win32u/$arch/libwin32u.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/input-message$bits-probe.exe"
done

for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/gl-upload-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/ntdll/$arch/libntdll.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/opengl32/$arch/libopengl32.a" \
        -o "$root/.deps/probes/gl-upload$bits-probe.exe"
done

# Independent cross-process GPU HWNDs, canonical clipping and normal teardown.
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/gpu-window-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/opengl32/$arch/libopengl32.a" "$wine/build/dlls/d3d11/$arch/libd3d11.a" \
        "$wine/build/dlls/dxgi/$arch/libdxgi.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/gpu-window$bits-probe.exe"
done

# Game mode on the desktop: a full-screen GPU window flipped directly, Explorer unloaded and restored.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/game-mode-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" "$wine/build/dlls/gdi32/x86_64-windows/libgdi32.a" \
    "$wine/build/dlls/opengl32/x86_64-windows/libopengl32.a" "$wine/build/dlls/d3d11/x86_64-windows/libd3d11.a" \
    "$wine/build/dlls/dxgi/x86_64-windows/libdxgi.a" "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" \
    -o "$root/.deps/probes/game-mode64-probe.exe"

# The same prefix/font contract in both architectures, including real glyph pixels.
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/virtual-query-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/virtual-query$suffix-probe.exe"
done

# WriteFile means what it means on Windows (the PS5 writes through a mapping): contents, sizes, other handles, a child.
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/file-write-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/file-write$suffix-probe.exe"
done

# Directories opened relative to a handle while the title's server holds over 4096 descriptors (pw_wine_cwd's path table).
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/nt-relative-dir-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/relative-dir$suffix-probe.exe"
done

# The descriptor-table instructions the console's CPU refuses in user mode (UMIP), emulated by patch 2102.
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/umip-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/umip$suffix-probe.exe"
done

# What file calls and plain system calls cost a program in a helper (tests/file-cost-probe.c, tests/syscall-cost-probe.c),
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    # and thousands of files and handles held together, renamed and deleted while open, deep paths in the other case
    # and a cancelled overlapped pipe read that must leave the pipe working (ntdll pipe.c:1405 on the console)
    for probe in file-cost syscall-cost handles-paths pipe-cancel; do
        clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
            -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
            -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
            "$root/tests/$probe-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
            "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" -o "$root/.deps/probes/$probe$suffix-probe.exe"
    done
done

# Byte-range locks as structured storage takes them, and StgOpenStorage's time (tests/lock-probe.c): ole32 storage32
# ran past 90 s on the console, 2 s on the PC (2026-10-25).
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/lock-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" "$wine/build/dlls/ole32/$arch/libole32.a" \
        "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" "$wine/build/dlls/ntdll/$arch/libntdll.a" \
        -o "$root/.deps/probes/lock$suffix-probe.exe"
done

# The longest path the console lets a program make (tests/path-limit-probe.c): 33 Unix components, 2026-10-25.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/path-limit-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" -o "$root/.deps/probes/path-limit64-probe.exe"

# NtContinue back into the code that called it, ntdll exception's test_continue alone (tests/continue-probe.c): the test
# ended in an access violation on the console after its first NtContinue (2026-10-26).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/continue-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" -o "$root/.deps/probes/continue64-probe.exe"

# A deep stack keeps its exception handling (tests/stack-limit-probe.c): kernel32 heap's 512 KiB of locals ended in
# "Exception frame is not in stack limits" on the console (2026-10-26).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -mno-stack-arg-probe -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/stack-limit-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" -o "$root/.deps/probes/stack-limit64-probe.exe"

# A stand-in for a session's program (tests/session-fixture-probe.c), under the two names the title chooses a session by: the
# Aurelia and Steam-runtime screens drawn from its output, with no signed-in account (2026-10-26).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/session-fixture-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    -o "$root/.deps/probes/aurelia_windows-fixture.exe"
cp "$root/.deps/probes/aurelia_windows-fixture.exe" "$root/.deps/probes/steamcmd-fixture.exe"

# The persistent test session: Wine's tests from a queue, many at a time, in one title launch (tools/run-wine-session.py).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Wno-missing-field-initializers -Werror \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/wt-session.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/msvcrt/x86_64-windows/libmsvcrt.a" -o "$root/.deps/probes/wt-session.exe"

# How fast data moves: recv at several sizes, a select loop, four connections, the disk.
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/net-throughput-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/ws2_32/$arch/libws2_32.a" "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/net-throughput$suffix-probe.exe"
done

# A registry value set and kept (the title saves the registry every 30 s).
for arch in x86_64-windows; do
    clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/registry-flush-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/advapi32/$arch/libadvapi32.a" \
        -o "$root/.deps/probes/registry-flush64-probe.exe"
done

# The memory figures a program reads (a game launcher's "Your RAM: 665 MB").
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/system-memory-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/system-memory$suffix-probe.exe"
done

# A written image page reports PAGE_READWRITE to VirtualProtect (the CEF renderer's startup check).
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/image-protect-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/image-protect$suffix-probe.exe"
done

# A small shared writable section off the host page maps private and the image starts (Wine patch 2406; GameMaker's
# .mydata). The layout is checked here: a .mydata whose start and size were both 16 KiB aligned would test nothing.
for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Wextra -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console -Xlinker /section:.mydata,RWS \
        "$root/tests/shared-section-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" \
        -o "$root/.deps/probes/shared-section$suffix-probe.exe"
    llvm-objdump -h "$root/.deps/probes/shared-section$suffix-probe.exe" | awk '
        $2 == ".mydata" { found = 1; vma = strtonum("0x" $4) % 0x100000000; size = strtonum("0x" $3)
                          size = int((size + 4095) / 4096) * 4096; if (vma % 16384 == 0 && size % 16384 == 0) exit 2 }
        END { if (!found) exit 3 }' || { echo "shared-section$suffix-probe.exe: .mydata missing or 16 KiB aligned" >&2; exit 1; }
done

# A thread killed mid-conversion keeps no lock (patch 1171).
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/kill-convert-probe.c" "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    -o "$root/.deps/probes/kill-convert-probe.exe"

for arch in x86_64-windows i386-windows; do
    target=x86_64-windows; suffix=64
    if [[ $arch == i386-windows ]]; then target=i686-windows; suffix=32; fi
    clang --target="$target" --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/prefix-baseline-probe.c" \
        "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/gdi32/$arch/libgdi32.a" \
        "$wine/build/dlls/dwrite/$arch/libdwrite.a" \
        -o "$root/.deps/probes/prefix-baseline$suffix-probe.exe"
done

# SEH faults are intentional: preserve asynchronous exception unwind records.
clang --target=x86_64-windows --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 \
    -fms-extensions -fexceptions -fasync-exceptions -funwind-tables \
    -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
    -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
    "$root/tests/section-view-probe.c" \
    "$wine/build/dlls/kernel32/x86_64-windows/libkernel32.a" \
    "$wine/build/dlls/user32/x86_64-windows/libuser32.a" \
    "$wine/build/dlls/ntdll/x86_64-windows/libntdll.a" \
    -o "$root/.deps/probes/section-view-probe.exe"
# Media Foundation playback of a generated MP4 (H.264 + AAC) in both architectures, and the clip with what the host
# decodes from it (tools/make-mf-video-clip.py).
python3 "$root/tools/make-mf-video-clip.py" --out "$root/.deps/probes"
for bits in 32 64; do
    if [[ $bits == 32 ]]; then target=i686-windows arch=i386-windows; else target=x86_64-windows arch=x86_64-windows; fi
    clang --target=$target --no-default-config -fuse-ld=lld -Wl,/timestamp:0 -nostdlib -O2 -Wall -Werror \
        -I"$wine/source/include" -I"$wine/build/include" -I"$wine/source/include/msvcrt" \
        -Wl,/entry:mainCRTStartup -Wl,/subsystem:console \
        "$root/tests/mf-video-probe.c" "$wine/build/dlls/kernel32/$arch/libkernel32.a" \
        "$wine/build/dlls/user32/$arch/libuser32.a" "$wine/build/dlls/ole32/$arch/libole32.a" "$wine/build/dlls/mfplat/$arch/libmfplat.a" \
        "$wine/build/dlls/mfreadwrite/$arch/libmfreadwrite.a" "$wine/build/libs/mfuuid/$arch/libmfuuid.a" \
        "$wine/build/libs/uuid/$arch/libuuid.a" "$wine/build/dlls/msvcrt/$arch/libmsvcrt.a" \
        -o "$root/.deps/probes/mf-video$bits-probe.exe"
done
