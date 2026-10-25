#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Every one-time input tools/build-production.sh reads from .deps/ and does not make itself, each pinned by revision,
# URL and SHA-256. A step whose result is already there is skipped, so running it again resumes.
#
#   tools/fetch-all.sh            fetch and build what is missing (the host Wine build takes hours)
#   tools/fetch-all.sh --check    change nothing: each pinned file's SHA-256, read from .deps/ or the cache, and a HEAD
#                                 request to each URL. Exit 0 every pin present, 2 something to fetch, 1 a pin is broken
#
# PS5_PROTON_CACHE=DIR names a folder of files downloaded before (looked up by file name): one there is copied in once
# its SHA-256 matches, and nothing is downloaded for it. A file whose hash differs is ignored.
#
# In order:
#   1 downloads     llvm-mingw (unpacked into .deps/llvm-mingw), the seven DXVK source archives of
#                   platform/dxvk/pins.json, GE-Proton11-7 (fonts, ICU and licences for the prefix baseline), the
#                   OpenGL SDK (tools/fetch-opengl-sdk.sh), the payload SDK v0.42 and prospero-win's ps5log.h for the
#                   Lapy daemon, ImageMagick 7.1.2-32 (Arch's package) for the Mihawk OS icons, CEF 150 for MihawkBrowser
#                   (tools/fetch-cef.py)
#   2 prx tools     tools/fetch-prx-tools.sh: the PRX converter at its pin, built with the host clang++
#   3 lapy          tools/build-lapy-validation.py --resident (the host clang; the pin needs clang/LLD 23.1.1)
#   4 wine          tools/fetch-runtime.sh, then tools/build-wine-runtime.sh, the host Wine build
#   5 prefix        .deps/prefix-probe, the template of the tools' check prefix (tools/stage-wine.py): host wineboot
#                   of step 4's build, with Wine Mono and Gecko left out (the runtime installs its own)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps=$root/.deps
check=false
case ${1:-} in
--check) check=true ;;
'') ;;
*) echo "usage: tools/fetch-all.sh [--check]" >&2; exit 2 ;;
esac
status=0   # 0 all present, 2 something to fetch, 1 a pin is broken
mkdir -p "$deps"

sha_ok() { [[ $(sha256sum "$2" | cut -c1-64) == "$1" ]]; }
head_ok() {
    local code
    code=$(curl -sIL -o /dev/null -w '%{http_code}' --retry 2 "$1" || true)
    echo "  HEAD $code $1"
    [[ $code == 200 ]] || status=1
}

# fetch DEST URL SHA256: DEST once its SHA-256 matches; from PS5_PROTON_CACHE when a file there matches; else the URL.
fetch() {
    local dest=$1 url=$2 sha=$3 name cached
    if $check; then head_ok "$url"; fi
    if [[ -f $dest ]]; then
        if sha_ok "$sha" "$dest"; then echo "ok        ${dest#"$root"/}"; return; fi
        echo "${dest#"$root"/} does not match its pinned SHA-256 $sha" >&2
        if $check; then status=1; return; fi
        exit 1
    fi
    for name in "${dest##*/}" "${url##*/}"; do
        cached=${PS5_PROTON_CACHE:-/nonexistent}/$name
        [[ -f $cached ]] && sha_ok "$sha" "$cached" || continue
        if $check; then echo "cached    ${dest#"$root"/} ($cached)"; return; fi
        mkdir -p "${dest%/*}"
        cp --reflink=auto "$cached" "$dest.part"
        mv "$dest.part" "$dest"
        echo "cached    ${dest#"$root"/}"
        return
    done
    if $check; then echo "absent    ${dest#"$root"/} ($url)"; [[ $status == 1 ]] || status=2; return; fi
    mkdir -p "${dest%/*}"
    echo "fetching  $url"
    curl -fsSL --retry 3 -o "$dest.part" "$url"
    sha_ok "$sha" "$dest.part" || { rm -f "$dest.part"; echo "$url does not match its pinned SHA-256 $sha" >&2; exit 1; }
    mv "$dest.part" "$dest"
}

# A step that builds something, once: skipped when its result is there (delete it to redo the step).
step() {
    local name=$1 present=$2; shift 2
    if [[ -e $present ]]; then echo "ok        $name"; return; fi
    if $check; then echo "absent    $name"; [[ $status == 1 ]] || status=2; return; fi
    "$@"
}

# 1. Downloads.
# llvm-mingw 20260922 (ucrt): the Windows cross compiler of DXVK, Mesa's WGL, the icons, the theme and the probes.
mingw_sha256=bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21
fetch "$deps/llvm-mingw.tar.xz" \
    https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz \
    "$mingw_sha256"
# Unpacked beside its folder and renamed when complete, so a cut-off unpack is never taken for a finished one.
unpack_mingw() {
    rm -rf "$deps/llvm-mingw" "$deps/llvm-mingw.partial"
    mkdir -p "$deps/llvm-mingw.partial"
    tar -xJf "$deps/llvm-mingw.tar.xz" -C "$deps/llvm-mingw.partial" --strip-components=1
    mv "$deps/llvm-mingw.partial" "$deps/llvm-mingw"
}
step "llvm-mingw (.deps/llvm-mingw)" "$deps/llvm-mingw/bin/x86_64-w64-mingw32-clang" unpack_mingw
# DXVK and its six submodule sources, as tools/build-dxvk.py names them.
while IFS=' ' read -r archive url sha; do
    fetch "$deps/$archive" "$url" "$sha"
done < <(python3 -c 'import json, sys
for pin in json.load(open(sys.argv[1])):
    print(pin["archive"], "https://codeload.github.com/%s/tar.gz/%s" % (pin["repo"], pin["revision"]), pin["sha256"])' \
    "$root/platform/dxvk/pins.json")
# GE-Proton11-7, the release tools/stage-wine.py takes the baseline's fonts, ICU and licences from (564 MB).
ge_sha256=$(sed -n "s/^GE_SHA256 = '\([0-9a-f]*\)'$/\1/p" "$root/tools/stage-wine.py")
fetch "$deps/GE-Proton11-7-x86_64.tar.gz" \
    https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-7/GE-Proton11-7-x86_64.tar.gz \
    "$ge_sha256"
# The OpenGL SDK linked into the title's win32u.prx; tools/fetch-opengl-sdk.sh holds its pin and unpacks it.
opengl_sha256=$(sed -n 's/^digest=//p' "$root/tools/fetch-opengl-sdk.sh")
fetch "$deps/ps5-opengl-sdk-1.0.2.tar.gz" \
    https://github.com/darkxex/ps5-opengl/releases/download/v1.0.2/ps5-opengl-sdk-1.0.2.tar.gz "$opengl_sha256"
$check || bash "$root/tools/fetch-opengl-sdk.sh"
# The Lapy daemon's inputs, pinned in tools/build-lapy-validation.py: the upstream payload SDK v0.42, and the logger
# header at prospero-win e665a2c (that revision carries no licence, so it is fetched, never vendored).
lapy_sdk_sha256=$(sed -n "s/^SDK_SHA = '\([0-9a-f]*\)'$/\1/p" "$root/tools/build-lapy-validation.py")
fetch "$deps/ps5-payload-sdk-v0.42.zip" \
    https://github.com/ps5-payload-dev/sdk/releases/download/v0.42/ps5-payload-sdk.zip "$lapy_sdk_sha256"
log_sha256=$(sed -n "s/^LOG_SHA = '\([0-9a-f]*\)'$/\1/p" "$root/tools/build-lapy-validation.py")
fetch "$deps/prospero-win-e665a2c/native/ps5log/ps5log.h" \
    https://raw.githubusercontent.com/mpereiraesaa/prospero-win/e665a2c1f9168c911cf1f6131b832f89a5582fb4/native/ps5log/ps5log.h \
    "$log_sha256"
# CEF 150's Windows x64 minimal distribution for MihawkBrowser; tools/fetch-cef.py holds its pin and unpacks it.
if $check; then
    python3 "$root/tools/fetch-cef.py" --check || { [[ $status == 1 ]] || status=2; }
    head_ok "$(python3 "$root/tools/fetch-cef.py" --url)"
else
    python3 "$root/tools/fetch-cef.py"
fi
# ImageMagick 7.1.2-32 Q16-HDRI with its librsvg delegate, the renderer tools/build-mihawk-icons.py requires (it uses
# .deps/magick-7.1.2-32/bin/magick when present). One already there or on PATH is kept. Otherwise Arch Linux's package
# of that release is unpacked there, the way the owner's copy was made from CachyOS's imagemagick-7.1.2.32-1.1-x86_64_v3
# (SHA-256 252770005de078fc5dcc22078f1bfdc025fad10c23a8e2fddce3989d4f95d85d; no longer on CachyOS's mirrors). It uses
# the host's librsvg, cairo and other libraries, so it runs on Arch-based hosts. Upstream's AppImage of the same release
# has no librsvg: it drew the coloured icons black, and the icon build refuses it.
magick_ok() { "$1" -version 2>/dev/null | grep -q 'ImageMagick 7.1.2-32 Q16-HDRI' &&
    "$1" -version | grep -q '^Delegates.* rsvg'; }
magick=$deps/magick-7.1.2-32
package=imagemagick-7.1.2.32-1-x86_64.pkg.tar.zst
if magick_ok "$magick/bin/magick" || magick_ok magick; then
    echo "ok        ImageMagick 7.1.2-32 Q16-HDRI (librsvg)"
    if $check; then head_ok "https://archive.archlinux.org/packages/i/imagemagick/$package"; fi
else
    fetch "$deps/$package" "https://archive.archlinux.org/packages/i/imagemagick/$package" \
        8eb08d75d74071afee8b70161bbed4e06a8a041281a1f0cb43aefa78061ec4e5
    if ! $check; then
        rm -rf "$magick" "$magick.partial"
        mkdir -p "$magick.partial/bin"
        tar --zstd -xf "$deps/$package" -C "$magick.partial" usr etc
        cat > "$magick.partial/bin/magick" <<'EOF'
#!/bin/sh
# The pinned renderer (tools/build-mihawk-icons.py), unpacked by tools/fetch-all.sh, not installed.
root=$(cd "$(dirname "$0")/.." && pwd)
export LD_LIBRARY_PATH=$root/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export MAGICK_CODER_MODULE_PATH=$root/usr/lib/ImageMagick-7.1.2/modules-Q16HDRI/coders
export MAGICK_CODER_FILTER_PATH=$root/usr/lib/ImageMagick-7.1.2/modules-Q16HDRI/filters
export MAGICK_CONFIGURE_PATH=$root/etc/ImageMagick-7:$root/usr/lib/ImageMagick-7.1.2/config-Q16HDRI
exec "$root/usr/bin/magick" "$@"
EOF
        chmod +x "$magick.partial/bin/magick"
        # Checked before the move: a copy that does not run must not shadow one put on PATH afterwards.
        magick_ok "$magick.partial/bin/magick" || { rm -rf "$magick.partial"; echo "$package does not run here: put" \
            "an ImageMagick 7.1.2-32 Q16-HDRI built with librsvg on PATH (README.md, Host tools)" >&2; exit 1; }
        mv "$magick.partial" "$magick"
    fi
fi

# 2. The PRX converter (ps5-native-app-boilerplate at its pin).
step "prx tools (.deps/prx-foundation)" "$deps/prx-foundation/build/host/ps5-native-tool" \
    bash "$root/tools/fetch-prx-tools.sh"
if $check; then head_ok https://github.com/mpereiraesaa/ps5-native-app-boilerplate/commit/30597512539e7edfde079cbcaf4a626bc0a948c5; fi

# 3. The resident Lapy daemon the title ships, from ../PS5-Lapy-JB-Daemon at its pin.
daemon=$deps/lapy-validation/source/build/owned_root_daemon-service-verified-client
step "lapy daemon (${daemon#"$root"/})" "$daemon/manifest.json" \
    python3 "$root/tools/build-lapy-validation.py" --resident --sdk-zip "$deps/ps5-payload-sdk-v0.42.zip"
if [[ -f $daemon/lapy-root-daemon.elf ]] &&
        ! sha_ok "$(sed -n 's/^daemon_sha256=//p' "$root/tools/build-production.sh")" "$daemon/lapy-root-daemon.elf"; then
    echo "  the Lapy daemon differs from tools/build-production.sh's pin (README.md, 'Known gaps')" >&2
    if $check; then status=1; fi   # tools/build-production.sh refuses it
fi

# 4. The Wine sources at their pins, and the host Wine build (PE modules and host tools).
if $check; then
    bash "$root/tools/fetch-runtime.sh" --check || case $? in 2) [[ $status == 1 ]] || status=2 ;; *) status=1 ;; esac
    while IFS='|' read -r _ repo commit; do head_ok "$repo/commit/$commit"; done < <(
        sed -n 's/^    "\([^"]*\)"$/\1/p' "$root/tools/fetch-runtime.sh")
else
    bash "$root/tools/fetch-runtime.sh"   # quick when every pin is there
fi
step "host wine build (.deps/wine-runtime/stage)" "$deps/wine-runtime/stage" bash "$root/tools/build-wine-runtime.sh"

# 5. The check prefix's template, once: wineboot of the host build at .deps/wine-runtime/build.
make_prefix() {
    local wine=$deps/wine-runtime/build
    trap 'rm -rf "$deps/prefix-probe"' ERR
    WINEPREFIX=$deps/prefix-probe WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=' "$wine/wine" wineboot --init
    WINEPREFIX=$deps/prefix-probe "$wine/server/wineserver" -w
    trap - ERR
    [[ -f $deps/prefix-probe/system.reg ]] || { echo "wineboot made no prefix" >&2; exit 1; }
    cat "$deps/wine-runtime/revision" > "$deps/prefix-probe.revision"
}
step "check prefix template (.deps/prefix-probe)" "$deps/prefix-probe/system.reg" make_prefix
if $check; then exit "$status"; fi
echo "every input of tools/build-production.sh is in .deps/"
