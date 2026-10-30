#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-build Wine's Unix side (ntdll.so, win32u.so, wineserver) for the PS5.
#
# The pinned Wine revision is copied into a private work tree, the patch
# series in wine/patches is applied in numeric order, and the copy is
# configured for FreeBSD with the PS5 payload SDK: the PS5 compiler defines
# __FreeBSD__ and ships FreeBSD headers, so Wine takes its FreeBSD paths
# (kqueue, sysctl) and __PROSPERO__ selects the PS5 patches. PE modules come
# from the host build (tools/build_wine_runtime.sh), whose tools/ directory
# also supplies winebuild and widl, except the ones whose sources the series
# changes or the host build's tree does not match, which are built here from
# the patched tree into <work>/pe (tools/wine-patched-modules.py says which).
#
# Patch numbers are owned by range: 0100-0499 services, loader and
# presentation; 0500-0899 execution core (signals, TEB, virtual memory);
# 0900-0999 what testing and diagnosis need (a program's standard output), and the fixes the conformance tests found;
# 1000-1999 the Mihawk OS shell and desktop (0900-0999 was full on 2026-10-10);
# 2000-2099 GE-Proton's ffmpeg + winedmo video rework, imported from the pinned fork's patches/ge-video-rework as
# it is, after the PS5 series it was rebased onto (each patch keeps GE's authorship and notes its rebase);
# 2300-2399 the flexible-memory work (branch fmem-codecache, docs/FLEXIBLE_MEMORY.md), clear of the other branches';
# 2400-2499 the guided store-client setup and the controller choice (src/pw_steam_guide.h, src/pw_controller.h).
#
# Two links follow. The ELF link builds Wine's own targets against the
# payload SDK and reports, rather than fails on, unresolved symbols. The PRX
# link is what the console loads: ntdll, win32u and the in-process
# wineserver (patch 0110) linked against the
# title's stub libraries with the PS5 shims in wine/ps5 and a generated
# export descriptor, then converted and signed with ps5-native-tool into
# <work>/prx/sce_module. Both are measured in <work>/report.json. Module
# conversion needs a foundation with module export publishing and name-form
# export hashes (its branch exp/prx-module, MODULE_EXPORTS_COMMIT); the
# title's pinned foundation predates it, so --prx-foundation names that checkout (tools/fetch-prx-tools.sh).
# Without it the build stops, unless PROSPERO_WINE_PS5_NO_PRX=1 asks for the ELF link alone (the PRX link is then
# skipped and the report says why).
#
# Vulkan (patches 0450-0460): win32u dlopens libvulkan.so, which pw_wine_dl
# turns into libvulkan.prx, the PS5 Vulkan driver (ps5vk) linked from its SDK
# (--ps5vk-sdk, a dist-sdk directory: lib/libps5vk.a, lib/libpsbc.a and the
# AGC import facades). Without it libvulkan.prx is skipped and Vulkan
# reports no driver; winevulkan.prx, Wine's Vulkan Unix side, is built
# either way. ps5vk is GPL-3.0-or-later: a title that ships libvulkan.prx
# ships a GPL work (see its SDK's LICENSE). --radv DIR links RADV (Mesa's
# AMD driver, MIT) instead: DIR is a PS5_Vulkan checkout whose
# tools/build-radv.sh release has built its pinned PS5_Mesa revision, linked
# by tools/link_radv_prx.sh. The two options are exclusive.
# OpenGL (patch 0660): --ps5-opengl-sdk names an installed
# installed ps5-opengl SDK prefix. Its GPL-3.0-or-later static archive is linked into
# win32u.prx; distributed builds must preserve the SDK's source and license
# obligations. SDK 0.6.0 provides a tested OpenGL compatibility profile for
# legacy WGL contexts, as well as the OpenGL 4.6 Core profile.
#
# Usage:
#   tools/build_wine_ps5.sh [--check-patches] [--patches DIR] [--work DIR]
#       [--source DIR] [--host-tools DIR] [--foundation DIR] [--sdk DIR]
#       [--prx-foundation DIR] [--ps5vk-sdk DIR | --radv DIR]
#       [--ps5-opengl-sdk DIR | --no-opengl] [--jobs N]
set -eu

WINE_COMMIT=1c78596625336a73dbfc035dbbcf0dbdd95b06eb
MODULE_EXPORTS_COMMIT=30597512539e7edfde079cbcaf4a626bc0a948c5
# FreeType draws Wine's text. It is built from a pinned release with the
# payload SDK and linked into libfreetype.prx, which Wine's own
# dlopen("libfreetype.so") loads through pw_wine_dl, next to ntdll.prx.
FREETYPE_VERSION=2.13.3
FREETYPE_SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
FREETYPE_URL=https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VERSION.tar.xz
# Only what Wine's fonts need: TrueType/CFF/Type 1 outlines, Windows .fon
# bitmaps, hinting and the two rasterisers; gzip (FreeType's own zlib copy)
# for the compressed WOFF tables sfnt reads.
FREETYPE_UNITS="base/ftsystem base/ftinit base/ftdebug base/ftbase base/ftbbox base/ftbitmap
 base/ftglyph base/ftmm base/ftsynth base/fttype1 base/ftfstype base/ftgasp base/ftwinfnt
 autofit/autofit truetype/truetype type1/type1 cff/cff winfonts/winfnt psaux/psaux
 psnames/psnames pshinter/pshinter sfnt/sfnt smooth/smooth raster/raster gzip/ftgzip"
# GnuTLS is Windows' TLS here (schannel, in secur32) and bcrypt's ciphers and keys; crypt32 reads PKCS#12 with it.
# Wine loads it with dlopen, so Wine needs only its headers to build, and libgnutls.prx beside ntdll.prx to run.
# Nettle, built with its own small GMP, is its cryptography; GnuTLS brings its own libtasn1 and libunistring. No
# p11-kit, no system trust store (Windows programs validate through crypt32's roots), no compression.
NETTLE_VERSION=3.10.1
NETTLE_SHA256=b0fcdd7fc0cdea6e80dcf1dd85ba794af0d5b4a57e26397eee3bc193272d9132
NETTLE_URL=https://ftp.gnu.org/gnu/nettle/nettle-$NETTLE_VERSION.tar.gz
GNUTLS_VERSION=3.8.9
GNUTLS_SHA256=69e113d802d1670c4d5ac1b99040b1f2d5c7c05daec5003813c049b5184820ed
GNUTLS_URL=https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/gnutls-$GNUTLS_VERSION.tar.xz
NETTLE_CONFIGURE="--enable-mini-gmp --disable-shared --enable-static --disable-documentation --disable-openssl
 --disable-assembler"
GNUTLS_CONFIGURE="--with-nettle-mini --with-included-libtasn1 --with-included-unistring --without-p11-kit
 --without-idn --without-zlib --without-brotli --without-zstd --without-tpm --without-tpm2 --without-leancrypto
 --disable-tools --disable-cxx --disable-doc --disable-manpages --disable-tests --disable-nls --disable-shared
 --enable-static --disable-hardware-acceleration --disable-padlock --disable-ktls --disable-libdane --disable-guile"

# The functions win32u and DirectWrite's FreeType backends load with dlsym.
FREETYPE_EXPORTS="FT_Done_Face FT_Get_Char_Index FT_Get_First_Char FT_Get_Next_Char
 FT_Get_Sfnt_Name FT_Get_Sfnt_Name_Count FT_Get_Sfnt_Table FT_Get_TrueType_Engine_Type
 FT_Get_WinFNT_Header FT_Init_FreeType FT_Library_SetLcdFilter FT_Library_Version
 FT_Load_Glyph FT_Load_Sfnt_Table FT_Matrix_Multiply FT_MulDiv FT_MulFix FT_New_Face
 FT_New_Memory_Face FT_Outline_Embolden FT_Outline_Get_Bitmap FT_Outline_Get_CBox
 FT_Outline_Transform FT_Outline_Translate FT_Property_Set FT_Render_Glyph FT_Set_Charmap
 FT_Set_Pixel_Sizes FT_Vector_Length FT_Vector_Transform FT_Vector_Unit
 FT_Activate_Size FT_Done_FreeType FT_Done_Glyph FT_Done_Size FT_Get_Glyph FT_Get_Kerning
 FT_Glyph_Copy FT_Glyph_Get_CBox FT_Glyph_Transform FT_New_Size FT_Outline_Copy
 FT_Outline_Decompose FT_Outline_Done FT_Outline_EmboldenXY FT_Outline_New"
TARGETS="dlls/ntdll/ntdll.so dlls/win32u/win32u.so server/wineserver dlls/winevulkan/winevulkan.so
 dlls/opengl32/opengl32.so dlls/ws2_32/ws2_32.so dlls/crypt32/crypt32.so dlls/winebus.sys/winebus.so
 dlls/bcrypt/bcrypt.so dlls/secur32/secur32.so dlls/dwrite/dwrite.so dlls/nsiproxy.sys/nsiproxy.so dlls/dnsapi/dnsapi.so
 dlls/mountmgr.sys/mountmgr.so"
# The module directories whose Unix side the PRX link below builds. A patch to
# any other module's Unix side would reach no console build, so
# tools/wine-patched-modules.py stops the build on one.
UNIX_MODULES="dlls/ntdll dlls/win32u dlls/winevulkan dlls/opengl32 dlls/ws2_32 dlls/crypt32 dlls/winebus.sys
 dlls/bcrypt dlls/secur32 dlls/xinput1_3 dlls/dwrite dlls/nsiproxy.sys dlls/dnsapi dlls/winedmo dlls/mountmgr.sys"
# Everything optional but FreeType (built below) is off: the console has none
# of these libraries, and a configure-time probe against the payload SDK must
# not pick up host headers.
CONFIGURE_ARGS="--host=x86_64-unknown-freebsd11 --build=x86_64-pc-linux-gnu
 --enable-win64 --enable-archs=i386,x86_64 --disable-tests --without-x
 --without-fontconfig --without-alsa --without-pulse
 --without-dbus --without-gstreamer --without-sdl --without-udev --without-usb
 --without-v4l2 --without-wayland --without-opengl --without-oss
 --without-pcap --without-pcsclite --without-sane --without-krb5 --without-gphoto
 --without-netapi --without-inotify --without-capi --without-cups --without-gssapi
 --without-ffmpeg"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bash "$root/tools/setup-graphics-foundation.sh" > /dev/null
patches=$root/wine/patches
work=${PROSPERO_WINE_PS5_WORK:-$root/.deps/wine-ps5}
source_dir=${PROSPERO_WINE_SOURCE:-$root/.deps/wine-proton}
host_tools=${PROSPERO_WINE_BUILD:-$root/.deps/wine-runtime/build}
foundation=${PS5_NATIVE_FOUNDATION:-$root/.deps/prx-foundation}
sdk=${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}
prx_foundation=${PS5_PRX_FOUNDATION:-}
ps5vk_sdk=${PS5VK_SDK:-}
ps5opengl_sdk=${PS5_OPENGL_SDK:-}
ps5opengl_lib=
no_opengl=0
radv=${PROSPERO_RADV:-}
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
check_only=0

fail() { echo "build_wine_ps5: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case $1 in
    --check-patches) check_only=1 ;;
    --patches) patches=$2; shift ;;
    --work) work=$2; shift ;;
    --source) source_dir=$2; shift ;;
    --host-tools) host_tools=$2; shift ;;
    --foundation) foundation=$2; shift ;;
    --sdk) sdk=$2; shift ;;
    --prx-foundation) prx_foundation=$2; shift ;;
    --ps5vk-sdk) ps5vk_sdk=$2; shift ;;
    --ps5-opengl-sdk) ps5opengl_sdk=$2; shift ;;
    --no-opengl) no_opengl=1 ;;
    --radv) radv=$2; shift ;;
    --jobs) jobs=$2; shift ;;
    *) fail "unknown argument $1" ;;
    esac
    shift
done

# The pinned OpenGL SDK is linked unless it is declined: a win32u.prx built without it has no OpenGL, and every OpenGL
# game (a .NET MonoGame game, a Java game) then fails at its first GL call, which looks like a regression elsewhere.
if [ -z "$ps5opengl_sdk" ] && [ "$no_opengl" = 0 ] && [ -f "$root/.deps/ps5-opengl-sdk-1.0.2/sdk/manifest.sha256" ]; then
    ps5opengl_sdk=$root/.deps/ps5-opengl-sdk-1.0.2/sdk
    echo "linking the pinned OpenGL SDK: $ps5opengl_sdk (--no-opengl to decline)"
fi
# Missing, it stops the build: only --no-opengl builds without it (the helper runtime, tools/build-production.sh).
[ -n "$ps5opengl_sdk" ] || [ "$no_opengl" = 1 ] || [ "$check_only" = 1 ] ||
    fail "no OpenGL SDK at $root/.deps/ps5-opengl-sdk-1.0.2/sdk: run tools/fetch-opengl-sdk.sh, or pass --no-opengl"
[ -z "$ps5vk_sdk" ] || [ -z "$radv" ] || fail "--ps5vk-sdk and --radv both name libvulkan.prx; give one"
[ -z "$ps5opengl_sdk" ] || [ -f "$ps5opengl_sdk/lib/libPS5OpenGL.a" ] ||
    [ -f "$ps5opengl_sdk/lib/libPS5OpenGLCore33.a" ] ||
    fail "no PS5 OpenGL SDK archive at $ps5opengl_sdk"
[ -z "$ps5opengl_sdk" ] || [ -f "$ps5opengl_sdk/manifest.sha256" ] ||
    fail "PS5 OpenGL SDK is missing manifest.sha256"
if [ -n "$ps5opengl_sdk" ]; then
    if [ -f "$ps5opengl_sdk/lib/libPS5OpenGL.a" ]; then
        ps5opengl_lib=PS5OpenGL
    else
        ps5opengl_lib=PS5OpenGLCore33
    fi
fi
[ -z "$ps5opengl_sdk" ] ||
    (cd "$ps5opengl_sdk" && sha256sum --status --check manifest.sha256) ||
    fail "PS5 OpenGL SDK does not match its SHA-256 manifest"

# The series: NNNN-lower-case-name.patch, unique numbers below 0900, each a
# mail-formatted patch with a subject. Printed in the order it is applied.
series() {
    [ -d "$patches" ] || fail "no patch directory $patches"
    last=-1
    for patch in $(cd "$patches" && ls | LC_ALL=C sort); do
        case $patch in
        README.md) continue ;;
        [0-9][0-9][0-9][0-9]-*.patch) ;;
        *) fail "unexpected file in the patch series: $patch" ;;
        esac
        printf '%s\n' "$patch" | grep -Eq '^[0-9]{4}-[a-z0-9][a-z0-9-]*\.patch$' ||
            fail "patch name must be NNNN-lower-case-words.patch: $patch"
        number=$(printf '%s' "$patch" | cut -c1-4 | sed 's/^0*//')
        number=${number:-0}
        [ "$number" -ge 100 ] && [ "$number" -le 2499 ] ||
            fail "patch number outside 0100-2499: $patch"
        [ "$number" -ne "$last" ] || fail "duplicate patch number: $patch"
        last=$number
        grep -q '^Subject: ' "$patches/$patch" || fail "patch has no Subject: $patch"
        printf '%s\n' "$patch"
    done
}

ordered=$(series)
if [ "$check_only" -eq 1 ]; then
    printf '%s\n' "$ordered"
    exit 0
fi

sdk=${sdk:-$foundation/.deps/native/ps5-payload-sdk}
[ -x "$sdk/bin/prospero-clang" ] || fail "no PS5 payload SDK at $sdk"
# ccache, when the host has it, in front of every compile: the shims below and Wine's own (wmake). It keys on the
# compiler's --version, since prospero-clang is a script that drives the host clang, which can change under it.
# PROSPERO_NO_CCACHE=1 compiles without it. The objects are the compiler's either way.
ccache=
[ -n "${PROSPERO_NO_CCACHE:-}" ] || ccache=$(command -v ccache || true)
export CCACHE_COMPILERCHECK='%compiler% --version'
[ -x "$host_tools/tools/winebuild/winebuild" ] ||
    fail "no host Wine tools at $host_tools (run tools/build_wine_runtime.sh)"
git -C "$source_dir" merge-base --is-ancestor "$WINE_COMMIT" HEAD ||
    fail "$source_dir is not the pinned Wine revision $WINE_COMMIT"

# The PRX link below needs the converter; without it the build is nothing the console loads, so it stops here, before
# the hours of building, unless PROSPERO_WINE_PS5_NO_PRX=1 asks for the ELF link alone.
prx_foundation=${prx_foundation:-$foundation}
tool=$prx_foundation/build/host/ps5-native-tool
pie=$prx_foundation/tooling/native/ps5-pie.ld
if [ ! -x "$tool" ] || [ ! -f "$pie" ]; then
    prx_status="skipped: no built ps5-native-tool in $prx_foundation"
elif ! git -C "$prx_foundation" merge-base --is-ancestor "$MODULE_EXPORTS_COMMIT" HEAD 2>/dev/null; then
    prx_status="skipped: $prx_foundation lacks module exports ($MODULE_EXPORTS_COMMIT)"
else
    prx_status=0
fi
case $prx_status in
skipped*) [ "${PROSPERO_WINE_PS5_NO_PRX:-0}" = 1 ] ||
    fail "PRX link not possible ($prx_status): run tools/fetch-prx-tools.sh, or set PROSPERO_WINE_PS5_NO_PRX=1" ;;
esac

# A private copy keeps the shared checkout pristine for the host build.
mkdir -p "$work"

# Only libraries available to game titles. WebKit and system-only imports
# can link successfully but resolve to NULL in the title process.
title_stubs=$work/title-stubs
mkdir -p "$title_stubs"
cp "$sdk"/target/lib/*.so "$title_stubs/"
rm -f "$title_stubs/libkernel_web.so" "$title_stubs/libkernel_sys.so" "$title_stubs/libScePosixForWebKit.so"

# FreeType, once per pinned release: a static archive for configure and the
# objects the PRX stage links. PROSPERO_FREETYPE_TARBALL names a local copy.
ft=$work/freetype
ft_stamp=$(printf '%s\n' "$FREETYPE_SHA256" "$FREETYPE_UNITS" | sha256sum | cut -c1-64)
if [ "$(cat "$ft/.prospero-stamp" 2>/dev/null)" != "$ft_stamp" ]; then
    rm -rf "$ft"
    mkdir -p "$ft/src" "$ft/obj" "$ft/config"
    tarball=${PROSPERO_FREETYPE_TARBALL:-$ft/freetype-$FREETYPE_VERSION.tar.xz}
    [ -f "$tarball" ] || curl -sSfL -o "$tarball" "$FREETYPE_URL" ||
        fail "cannot download $FREETYPE_URL"
    [ "$(sha256sum "$tarball" | cut -c1-64)" = "$FREETYPE_SHA256" ] ||
        fail "$tarball does not match FreeType $FREETYPE_VERSION's pinned SHA-256"
    tar -xJf "$tarball" -C "$ft/src" --strip-components=1
    cat > "$ft/config/ftmodule_ps5.h" <<'EOF'
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, winfnt_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
EOF
    for unit in $FREETYPE_UNITS; do
        $ccache "$sdk/bin/prospero-clang" -std=c99 -O2 -fPIC -DFT2_BUILD_LIBRARY \
            '-DFT_CONFIG_MODULES_H=<ftmodule_ps5.h>' -I"$ft/config" -I"$ft/src/include" \
            -c "$ft/src/src/$unit.c" -o "$ft/obj/$(basename "$unit").o" ||
            fail "cannot compile FreeType $unit.c"
    done
    "$sdk/bin/llvm-ar" rcs "$ft/libfreetype.a" "$ft"/obj/*.o
    echo "$ft_stamp" > "$ft/.prospero-stamp"
    echo "built FreeType $FREETYPE_VERSION"
fi

# Nettle and GnuTLS, once per pinned release and options: static archives and headers in $tls/install.
# PROSPERO_TLS_TARBALLS names a folder holding local copies of both tarballs.
tls=$work/gnutls
tls_stamp=$(printf '%s\n' "$NETTLE_SHA256" "$GNUTLS_SHA256" "$NETTLE_CONFIGURE" "$GNUTLS_CONFIGURE" "$sdk" |
    sha256sum | cut -c1-64)
if [ "$(cat "$tls/.prospero-stamp" 2>/dev/null)" != "$tls_stamp" ]; then
    rm -rf "$tls"
    mkdir -p "$tls/nettle" "$tls/gnutls" "$tls/install"
    for spec in "nettle-$NETTLE_VERSION.tar.gz $NETTLE_URL $NETTLE_SHA256" \
            "gnutls-$GNUTLS_VERSION.tar.xz $GNUTLS_URL $GNUTLS_SHA256"; do
        set -- $spec
        tarball=${PROSPERO_TLS_TARBALLS:-$tls}/$1
        [ -f "$tarball" ] || curl -sSfL -o "$tarball" "$2" || fail "cannot download $2"
        [ "$(sha256sum "$tarball" | cut -c1-64)" = "$3" ] || fail "$tarball does not match its pinned SHA-256"
        tar -xf "$tarball" -C "$tls/${1%%-*}" --strip-components=1
    done
    tls_cc="CC=$sdk/bin/prospero-clang AR=$sdk/bin/llvm-ar RANLIB=$sdk/bin/llvm-ranlib"
    # shellcheck disable=SC2086
    (cd "$tls/nettle" && env -u LDFLAGS ./configure --host=x86_64-unknown-freebsd11 --build=x86_64-pc-linux-gnu \
        $tls_cc CFLAGS="-O2 -fPIC" $NETTLE_CONFIGURE --prefix="$tls/install" &&
        make -j"$jobs" libnettle.a libhogweed.a && make install-headers install-static) > "$tls/nettle.log" 2>&1 ||
        fail "cannot build Nettle; see $tls/nettle.log"
    # shellcheck disable=SC2086
    (cd "$tls/gnutls" && env -u LDFLAGS ./configure --host=x86_64-unknown-freebsd11 --build=x86_64-pc-linux-gnu \
        $tls_cc CFLAGS="-O2 -fPIC" NETTLE_CFLAGS="-I$tls/install/include" NETTLE_LIBS="$tls/install/lib/libnettle.a" \
        HOGWEED_CFLAGS="-I$tls/install/include" HOGWEED_LIBS="$tls/install/lib/libhogweed.a" \
        GMP_CFLAGS="-I$tls/install/include" GMP_LIBS=" " $GNUTLS_CONFIGURE --prefix="$tls/install" &&
        make -j"$jobs" && make install) > "$tls/gnutls.log" 2>&1 || fail "cannot build GnuTLS; see $tls/gnutls.log"
    echo "$tls_stamp" > "$tls/.prospero-stamp"
    echo "built Nettle $NETTLE_VERSION and GnuTLS $GNUTLS_VERSION"
fi
# Standard raw DNS resolver; configured servers come from the owner at runtime.
cares=$work/cares
python3 "$root/tools/build-cares-ps5.py" --work "$cares" --sdk "$sdk" > "$work/cares.log" 2>&1 ||
    fail "cannot build pinned c-ares; see $work/cares.log"
# FFmpeg, winedmo's media backend (patches 2001-2062): static LGPL archives and headers in $ff/install.
ff=$work/ffmpeg
python3 "$root/tools/build-ffmpeg-ps5.py" --work "$ff" --sdk "$sdk" --jobs "$jobs" > "$work/ffmpeg.log" 2>&1 ||
    fail "cannot build pinned FFmpeg; see $work/ffmpeg.log and $ff/build.log"
tree=$work/source
if [ ! -d "$tree/.git" ]; then
    git clone -q --shared --no-checkout "$source_dir" "$tree"
fi
source_stamp=$( { printf '%s\n' "$WINE_COMMIT"; cat "$root/tools/generate-wine.sh"; for patch in $ordered; do cat "$patches/$patch"; done; } | sha256sum | cut -c1-64)
# The Makefiles are written from the patched tree and from the files this script copies into it: a copy line dropped
# from here leaves its file out of a fresh configure, so this script's own bytes are in what makedep is rerun for.
makefile_stamp=$(cat "$0" | sha256sum | cut -c1-64)
makefile_stamp=$(printf '%s\n' "$source_stamp" "$makefile_stamp" | sha256sum | cut -c1-64)
if [ "$(cat "$work/source-stamp" 2>/dev/null)" != "$source_stamp" ]; then
    # The patched source as a git tree, built in a private index: every patch
    # is applied in order from the pin, exactly as before, but nothing is
    # written to the work tree yet.
    next_index=$work/next.index
    rm -f "$next_index"
    GIT_INDEX_FILE=$next_index git -C "$tree" read-tree "$WINE_COMMIT"
    for patch in $ordered; do
        GIT_INDEX_FILE=$next_index git -C "$tree" apply --cached "$patches/$patch" ||
            fail "patch does not apply: $patch"
        echo "applied $patch"
    done
    next_tree=$(GIT_INDEX_FILE=$next_index git -C "$tree" write-tree)
    rm -f "$next_index"
    previous_tree=$(cat "$work/source-tree" 2>/dev/null || true)
    if [ -z "${PROSPERO_WINE_CLEAN:-}" ] && [ -n "$previous_tree" ] &&
            git -C "$tree" cat-file -e "$previous_tree^{tree}" 2>/dev/null; then
        # Write only the files whose patched content changed, so make
        # rebuilds what depends on them and nothing else.
        git -C "$tree" diff --name-only --no-renames "$previous_tree" "$next_tree" > "$work/changed-files"
        git -C "$tree" read-tree "$next_tree"
        while IFS= read -r path; do
            if git -C "$tree" cat-file -e "$next_tree:$path" 2>/dev/null; then
                git -C "$tree" checkout-index -f -- "$path"
            else
                rm -f "$tree/$path"
            fi
        done < "$work/changed-files"
        echo "updated $(wc -l < "$work/changed-files") changed source files"
    else
        git -C "$tree" checkout -q --force "$WINE_COMMIT"
        git -C "$tree" clean -q -fdx
        git -C "$tree" read-tree "$next_tree"
        git -C "$tree" checkout-index -a -f
        # checkout-index writes what the series keeps; what it deletes (winegstreamer, patch 2001) would stay on disk.
        git -C "$tree" diff --name-only --no-renames --diff-filter=D "$WINE_COMMIT" "$next_tree" |
            (cd "$tree" && xargs -r rm -f --)
    fi
    # The generators rewrite their outputs; an output whose bytes did not
    # change keeps its time (tools/preserve-mtimes.py).
    python3 "$root/tools/preserve-mtimes.py" snapshot "$tree" "$work/mtimes.json"
    bash "$root/tools/generate-wine.sh" "$tree" > "$work/generate.log" 2>&1
    python3 "$root/tools/preserve-mtimes.py" restore "$tree" "$work/mtimes.json"
    echo "$next_tree" > "$work/source-tree"
    echo "$source_stamp" > "$work/source-stamp"
fi

# The theme generators write shared folders (.deps/windows11-theme, windows11-cursors, mihawk-icons) every run, and the
# copies below read them: one build at a time runs the two (tools/build-production.sh runs the title's and the
# broker's builds side by side), or one could copy a file the other is rewriting.
exec 9> "$root/.deps/theme.lock"
flock 9
# Approved object icons keep Wine's shell resource IDs. These generated build
# inputs come from repository-owned SVGs with verified hashes, never a hand edit
# of the dependency checkout. Unchanged icon bytes retain their timestamps.
python3 "$root/tools/build-mihawk-icons.py" --wine-tree "$tree"
python3 "$root/tools/build-wine-theme.py"
python3 "$root/tools/build-cursors.py"
python3 "$root/tools/build-shell-sprites.py"
python3 "$root/tools/build-shell-glyphs.py"
python3 "$root/tools/build-explorer-sprites.py"
python3 "$root/tools/build-menu-sprites.py"
python3 "$root/tools/build-desktop-sprites.py"
python3 "$root/tools/build-tooltip-glyphs.py"
# Every pw_* file copied into the tree below is (re)written by this run: one an earlier run left would hide a copy
# line that is gone, and a fresh configure would then fail where this tree builds (patch 2065, 788b0ec). The mark is
# a second old, so a copy made in the same second still counts as this run's.
touch -d "@$(($(date +%s) - 1))" "$work/copies-start"
# Explorer consumes the same generated palette/geometry and licensed icons as
# the appearance application. These are build inputs, not vendor-tree edits.
cp -p "$root/.deps/windows11-theme/appearance.h" "$tree/programs/explorer/appearance.h"
cp -p "$root/.deps/mihawk-icons/original/folder.ico" "$tree/programs/explorer/mihawk-folder.ico"
cp -p "$root/.deps/mihawk-icons/icons.h" "$tree/programs/explorer/icons.h"
cp -p "$root/wine/ps5/pw_shell_style.h" "$tree/programs/explorer/pw_shell_style.h"
cp -p "$root/wine/ps5/pw_shell_start.c" "$tree/programs/explorer/pw_shell_start.c"
cp -p "$root/wine/ps5/pw_shell_flyouts.c" "$tree/programs/explorer/pw_shell_flyouts.c"
# Wine's makedep does not track these copied headers: when one changed, the file that includes it must still be rebuilt.
for header in wine/ps5/pw_shell_wallpaper.h wine/ps5/pw_shell_desktop_view.h wine/ps5/pw_shell_cursors.h \
        wine/ps5/pw_shell_monospace.h .deps/windows11-cursors/pw_cursor_scheme.h wine/ps5/pw_shell_desktop_menus.h \
        .deps/windows11-theme/pw_menu_icons.h; do
    cmp -s "$root/$header" "$tree/programs/explorer/${header##*/}" || touch "$tree/programs/explorer/desktop.c"
    cp -p "$root/$header" "$tree/programs/explorer/${header##*/}"
done
for header in wine/ps5/pw_shell_explorer.h src/pw_explorer_layout.h .deps/windows11-theme/pw_explorer_sprites.h; do
    cmp -s "$root/$header" "$tree/programs/explorer/${header##*/}" || touch "$tree/programs/explorer/explorer.c"
    cp -p "$root/$header" "$tree/programs/explorer/${header##*/}"
done
# The desktop's icons as the prototype draws them: comctl32's list view (patch 1105) and shell32's grid (patch 1102);
# every program's tooltips (patch 1210).
for header in src/pw_desktop_icons.h src/pw_text_glyphs.h .deps/windows11-theme/pw_desktop_sprites.h src/pw_tooltip.h \
        .deps/windows11-theme/pw_tooltip_glyphs.h; do
    cmp -s "$root/$header" "$tree/dlls/comctl32/${header##*/}" || touch "$tree/dlls/comctl32/listview.c" "$tree/dlls/comctl32/tooltips.c"
    cp -p "$root/$header" "$tree/dlls/comctl32/${header##*/}"
done
for header in src/pw_desktop_icons.h src/pw_text_glyphs.h; do
    cmp -s "$root/$header" "$tree/dlls/shell32/${header##*/}" || touch "$tree/dlls/shell32/shlview.c"
    cp -p "$root/$header" "$tree/dlls/shell32/${header##*/}"
done
for header in wine/ps5/pw_shell_taskbar.h src/pw_taskbar_layout.h src/pw_taskbar_logo.h .deps/windows11-theme/pw_shell_sprites.h \
        src/pw_text_glyphs.h .deps/windows11-theme/pw_shell_glyphs.h; do
    cmp -s "$root/$header" "$tree/programs/explorer/${header##*/}" ||
        touch "$tree/programs/explorer/systray.c" "$tree/programs/explorer/pw_shell_start.c" "$tree/programs/explorer/pw_shell_flyouts.c"
    cp -p "$root/$header" "$tree/programs/explorer/${header##*/}"
done

# Every program's popup menus (patch 1190) and the desktop's menu icons (patch 1191).
for header in src/pw_menu_style.h .deps/windows11-theme/pw_menu_sprites.h src/pw_text_glyphs.h \
        .deps/windows11-theme/pw_menu_glyphs.h; do
    cmp -s "$root/$header" "$tree/dlls/win32u/${header##*/}" || touch "$tree/dlls/win32u/menu.c"
    cp -p "$root/$header" "$tree/dlls/win32u/${header##*/}"
done

# The PS5 OpenGL SDK is optional. When supplied, Wine's generic EGL/WGL
# frontend binds directly to its static EGL symbols and the win32u PRX links
# the SDK into the runtime.
# Wine's makedep resolves unlisted headers beside the including source, independently
# of compiler CFLAGS. Install the repository-owned interface headers as build
# inputs; implementation stays in wine/ps5, never edited in the vendor tree.
for header in pw_wine_section.h pw_wine_section_ps5.h pw_wine_alias.h pw_wine_mmu4k.h pw_wine_xattr.h; do
    cp -p "$root/wine/ps5/$header" "$tree/dlls/ntdll/unix/$header"
done
cp -p "$root/wine/ps5/pw_wine_perf.h" "$tree/dlls/winedmo/pw_wine_perf.h"   # patch 2065's media counters
cp -p "$root/src/pw_umip.h" "$tree/dlls/ntdll/unix/pw_umip.h"   # patch 2102's UMIP emulation
cp -p "$root/src/pw_window_snapshot.h" "$tree/server/pw_window_snapshot.h"
cp -p "$root/wine/ps5/pw_wine_cursor.h" "$tree/server/pw_wine_cursor.h"
cp -p "$root/wine/ps5/pw_wine_xattr.h" "$tree/server/pw_wine_xattr.h"   # a reparse point's data (patch 2092)
# cp -p keeps the source's time but sets the copy's change time, so what this run did not copy is older than the mark:
# removed, an include of it stops make ("No rule to make target") as a fresh configure would stop.
copies=$(cd "$tree" && git ls-files --others -- '*pw_*') || fail "cannot list the copies in $tree"
for copy in $copies; do
    if [ -z "$(find "$tree/$copy" -cnewer "$work/copies-start")" ]; then
        rm -f "$tree/$copy"
        echo "removed a copy this run did not make: $copy"
    fi
done
flock -u 9
opengl_cflags=${CFLAGS:--g -O2}
if [ -n "$ps5opengl_sdk" ]; then opengl_cflags="$opengl_cflags -DWINE_PS5_OPENGL"; fi

# A clean configure whenever the build's inputs other than the source change
# (arguments, compiler, flags). Source changes, configure.ac included, are
# make's: its Makefile reruns config.status --recheck when configure is newer
# and makedep when a Makefile.in is, so the objects that did not change stay.
stamp=$(
    printf '%s\n' "$WINE_COMMIT" "$CONFIGURE_ARGS" "$sdk" "$FREETYPE_SHA256" "$tls_stamp" \
        "$ps5opengl_sdk" "$opengl_cflags" | sha256sum | cut -c1-64)
build=$work/build
# FreeType is found by its flags; its soname is the name Wine dlopens, which
# pw_wine_dl turns into libfreetype.prx beside ntdll.prx. Configure runs here
# and only here, without the link flags the targets are built with: a
# config.status --recheck started by make would inherit LDFLAGS
# (--warn-unresolved-symbols makes every link test pass) and write a config.h
# that claims Linux and macOS functions.
run_configure() {
    # shellcheck disable=SC2086
    (cd "$build" && env -u LDFLAGS -u MAKEFLAGS -u MFLAGS "$tree/configure" $CONFIGURE_ARGS \
        CC="$sdk/bin/prospero-clang" CFLAGS="$opengl_cflags" \
        FREETYPE_CFLAGS="-I$ft/src/include" FREETYPE_LIBS="$ft/libfreetype.a" \
        ac_cv_lib_soname_freetype=libfreetype.so ac_cv_lib_soname_vulkan=libvulkan.so \
        GNUTLS_CFLAGS="-I$tls/install/include" \
        GNUTLS_LIBS="$tls/install/lib/libgnutls.a $tls/install/lib/libhogweed.a $tls/install/lib/libnettle.a" \
        ac_cv_lib_soname_gnutls=libgnutls.so ac_cv_func_gnutls_cipher_init=yes \
        --with-wine-tools="$host_tools" > "$work/configure.log" 2>&1) ||
        fail "configure failed; see $work/configure.log"
    sha256sum "$tree/configure" | cut -c1-64 > "$build/.configure-digest"
    echo "$makefile_stamp" > "$build/.makefile-source"
}
# PROSPERO_WINE_CLEAN=1 forces a from-nothing source tree and build.
if [ ! -f "$build/Makefile" ] || [ "$(cat "$build/.prospero-stamp" 2>/dev/null)" != "$stamp" ] ||
        [ -n "${PROSPERO_WINE_CLEAN:-}" ]; then
    rm -rf "$build"
    mkdir -p "$build"
    run_configure
    echo "$stamp" > "$build/.prospero-stamp"
elif [ "$(sha256sum "$tree/configure" | cut -c1-64)" != "$(cat "$build/.configure-digest" 2>/dev/null)" ]; then
    # A patch changed configure.ac: configure again in place, keeping objects.
    run_configure
elif [ "$(cat "$build/.makefile-source" 2>/dev/null)" != "$makefile_stamp" ]; then
    # A patch added an #include or a file without touching configure: makedep reads every source's includes only when
    # config.status writes the Makefiles, so they are written again from any patched tree they were not written from.
    # Without it 788b0ec built only on a tree configured before its patch 2065, and a fresh configure failed.
    (cd "$build" && env -u LDFLAGS -u MAKEFLAGS -u MFLAGS ./config.status > "$work/makedep.log" 2>&1) ||
        fail "makedep failed; see $work/makedep.log"
    echo "$makefile_stamp" > "$build/.makefile-source"
fi

# make with ccache in front of Wine's three compilers, and the PE modules linked without a link time (lld-link's
# /timestamp:0, as the theme DLLs): the values configure wrote, read once from the Makefile, so adding either needs
# no reconfigure; the commands are otherwise the same. Without the time stamp a module relinked from the same objects
# is the same bytes, so a clean build gives the incremental one's (docs/BUILD_SPEED.md).
make_cc=$(sed -n 's/^CC = //p' "$build/Makefile")
make_x86_64_cc=$(sed -n 's/^x86_64_CC = //p' "$build/Makefile")
make_i386_cc=$(sed -n 's/^i386_CC = //p' "$build/Makefile")
make_x86_64_ldflags=$(sed -n 's/^x86_64_LDFLAGS = //p' "$build/Makefile")
make_i386_ldflags=$(sed -n 's/^i386_LDFLAGS = //p' "$build/Makefile")
pe_link=-Wl,/timestamp:0
wmake() {
    if [ -n "$ccache" ]; then
        set -- CC="$ccache $make_cc" x86_64_CC="$ccache $make_x86_64_cc" i386_CC="$ccache $make_i386_cc" "$@"
    fi
    make x86_64_LDFLAGS="$make_x86_64_ldflags $pe_link" i386_LDFLAGS="$make_i386_ldflags $pe_link" "$@"
}
# A module linked before the link options were these keeps its old bytes (make does not see options): every linked PE
# image in the build is removed once, so each is linked again with them when it is next built.
if [ "$(cat "$build/.pe-link" 2>/dev/null)" != "$pe_link" ]; then
    python3 - "$build" <<'PY_RELINK'
import sys
from pathlib import Path
for path in Path(sys.argv[1]).glob('*/*/*-windows/*'):
    if not path.is_file() or path.is_symlink():
        continue
    with open(path, 'rb') as file:
        head = file.read(64)
        if len(head) == 64 and head[:2] == b'MZ':
            file.seek(int.from_bytes(head[60:64], 'little'))
            if file.read(4) == b'PE\0\0':
                path.unlink()
PY_RELINK
    echo "$pe_link" > "$build/.pe-link"
fi

# The SDK's libc carries the math functions, but win32u links -lm by name;
# libunwind.a names pthread as a dependent library, which the title's
# libkernel stubs provide.
mkdir -p "$work/ps5lib"
for library in libm libpthread; do
    [ -f "$work/ps5lib/$library.a" ] || "$sdk/bin/llvm-ar" rcs "$work/ps5lib/$library.a"
done
# The heap (wine/ps5) serves the malloc family. ntdll.so links and exports
# it, so win32u.so binds to the same instance through its ntdll.so import;
# the in-process wineserver has its own copy.
mkdir -p "$work/heap"
heap=""
for unit in pw_wine_heap pw_wine_heap_libc; do
    $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/$unit.c" -o "$work/heap/$unit.o" || fail "cannot compile $unit.c"
    heap="$heap $work/heap/$unit.o"
done
# ntdll's anonymous memory comes from direct memory (patch 0600); every release of it forgets its aliases first
# (pw_wine_alias, docs/MMU4K.md); an image's code is shared between helpers (pw_wine_codeshare) and held to a budget
# (pw_wine_code_budget).
dmem=""
for unit in pw_wine_dmem pw_wine_dmem_ps5 pw_wine_section pw_wine_section_ps5 pw_wine_alias pw_wine_codeshare pw_wine_code_budget; do
    $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/$unit.c" -o "$work/heap/$unit.o" || fail "cannot compile $unit.c"
    dmem="$dmem $work/heap/$unit.o"
done
# The 4 KiB MMU (docs/MMU4K.md, entered by patch 1134): a run executes the program's instructions with
# its vector registers live, so the engine is built without them, and its object may name no vector or x87 register
# and call nothing but its own routines (pw_wine_mmu4k_slot.S) and the decoder, which it reaches through
# pw_wine_mmu4k_call. Either check failing fails the build.
mmu4k_objdump=$(command -v llvm-objdump-18 || command -v llvm-objdump || echo "$sdk/bin/llvm-objdump")
$ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC -mgeneral-regs-only -ffreestanding -fno-builtin \
    -fno-stack-protector -c "$root/wine/ps5/pw_wine_mmu4k.c" -o "$work/heap/pw_wine_mmu4k.o" ||
    fail "cannot compile pw_wine_mmu4k.c"
mmu4k_listing=$("$mmu4k_objdump" -d --no-show-raw-insn "$work/heap/pw_wine_mmu4k.o") ||
    fail "cannot disassemble pw_wine_mmu4k.o"
if printf '%s\n' "$mmu4k_listing" | grep -qE '%(x|y|z)mm|%st|%mm[0-7]|%k[0-7]'; then
    fail "pw_wine_mmu4k.o names a vector or x87 register"
fi
mmu4k_outside=$("$sdk/bin/llvm-nm" --undefined-only "$work/heap/pw_wine_mmu4k.o" | awk '{print $NF}' |
    grep -vxE 'pw_wine_mmu4k_(start|exit_trap|exec|recover|read|write|copy|call|guarded_begin|guarded_end|guarded_fail)|pw_ia32_decode_mode|pw_ia32_access|_GLOBAL_OFFSET_TABLE_' ||
    true)
[ -z "$mmu4k_outside" ] || fail "pw_wine_mmu4k.o calls outside its allow-list: $mmu4k_outside"
$ccache "$sdk/bin/prospero-clang" -c "$root/wine/ps5/pw_wine_mmu4k_slot.S" -o "$work/heap/pw_wine_mmu4k_slot.o" ||
    fail "cannot assemble pw_wine_mmu4k_slot.S"
dmem="$dmem $work/heap/pw_wine_mmu4k.o $work/heap/pw_wine_mmu4k_slot.o"
# src/pw_umip.c: what an instruction the CPU refuses under UMIP would have stored (patch 2102); it uses the decoder.
for source in wine/ps5/pw_wine_mmu4k_ps5.c src/pw_ia32_isa.c src/pw_ia32_decode.c src/pw_umip.c; do
    unit=$(basename "$source" .c)
    $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/$source" -o "$work/heap/$unit.o" || fail "cannot compile $source"
    dmem="$dmem $work/heap/$unit.o"
done
process_shims=""
# pw_wine_xattr: the extended attributes the console's file system has not (patch 2090)
for unit in pw_wine_process pw_wine_process_io pw_wine_threads pw_wine_prefix pw_wine_vm pw_wine_xattr; do
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/$unit.c" -o "$work/heap/$unit.o" || fail "cannot compile $unit.c"
    process_shims="$process_shims $work/heap/$unit.o"
done
$ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
    -c "$root/src/pw_wine_launch.c" -o "$work/heap/pw_wine_launch.o" || fail "cannot compile pw_wine_launch.c"
$ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
    -c "$root/src/pw_environment.c" -o "$work/heap/pw_environment.o" || fail "cannot compile pw_environment.c"
# The helper spawn's upload (wine/ps5/pw_payload_spawn.h) through the title's one encoder.
$ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
    -c "$root/src/pw_elfldr.c" -o "$work/heap/pw_elfldr.o" || fail "cannot compile pw_elfldr.c"
process_shims="$process_shims $work/heap/pw_wine_launch.o $work/heap/pw_environment.o $work/heap/pw_elfldr.o"
$ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
    -c "$root/wine/ps5/pw_wine_wallpaper.c" -o "$work/heap/pw_wine_wallpaper.o" || fail "cannot compile wallpaper"
base="-L$work/ps5lib -lunwind -Wl,--warn-unresolved-symbols"
status=0
: > "$work/make.log"
# LDFLAGS is not a make dependency, so relink the three targets every run.
(cd "$build" && rm -f $TARGETS)
for step in "dlls/ntdll/ntdll.so|$heap $dmem $process_shims" "dlls/win32u/win32u.so|$work/heap/pw_wine_wallpaper.o" "server/wineserver|$heap $work/heap/pw_wine_xattr.o" \
        "dlls/winevulkan/winevulkan.so|" "dlls/opengl32/opengl32.so|" "dlls/ws2_32/ws2_32.so|" \
        "dlls/crypt32/crypt32.so|" "dlls/winebus.sys/winebus.so|" "dlls/bcrypt/bcrypt.so|" "dlls/secur32/secur32.so|" \
        "dlls/dwrite/dwrite.so|" "dlls/nsiproxy.sys/nsiproxy.so|" "dlls/dnsapi/dnsapi.so|" "dlls/mountmgr.sys/mountmgr.so|"; do
    target=${step%%|*}; objects=${step#*|}
    wmake -C "$build" -k -j"$jobs" LDFLAGS="$objects $base" "$target" \
        >> "$work/make.log" 2>&1 || status=$?
done
# The patched PE modules, built by the PE cross compilers into <work>/pe.
# Which ones: every module whose sources the series changes, and every module
# whose sources differ from the host build's tree, so the console never runs a
# host-built module a patch did not reach (hidclass.sys and dinput did, until
# this list stopped being kept by hand). Each is built, or the build stops; a
# module this configuration leaves out is left out of the host build too, or
# the build stops.
unix_args=""
for module in $UNIX_MODULES; do unix_args="$unix_args --unix-module $module"; done
# shellcheck disable=SC2086
pe_modules=$(python3 "$root/tools/wine-patched-modules.py" $unix_args --runtime-source "$host_tools/../source" \
    "$tree" "$WINE_COMMIT" "$(cat "$work/source-tree")") || fail "cannot tell which PE modules to build"
rm -rf "$work/pe"
# i386 too: a 32-bit program loads the i386 modules, which need the same patches (docs/IA32_DESIGN.md, M4 step 2).
for arch in x86_64 i386; do
    mkdir -p "$work/pe/$arch-windows"
    # Parse each large Makefile once and let one Make process share its dependency
    # graph across all selected targets. The old per-DLL loop spent minutes doing
    # repeated up-to-date checks; the target set and failure policy are identical.
    targets=$(python3 - "$build/Makefile" "$host_tools/Makefile" "$arch" $pe_modules <<'PY_TARGETS'
import re, sys
from pathlib import Path
available, host = [set(re.findall(r'^([^ \t:=]+):', Path(path).read_text(), re.M)) for path in sys.argv[1:3]]
for module in sys.argv[4:]:
    directory, name = module.rsplit('/', 1)
    target = f'{directory}/{sys.argv[3]}-windows/{name}'
    if target in available:
        print(target)
    elif target in host:
        sys.exit(f'{target}: the host build makes it, this configuration does not, and its sources differ')
    else:
        print(f'not configured in either build: {target}', file=sys.stderr)
PY_TARGETS
    ) || fail "cannot select $arch PE targets"
    if [ -n "$targets" ]; then
        wmake -C "$build" -j"$jobs" $targets >> "$work/make.log" 2>&1 ||
            fail "cannot build $arch PE modules; see $work/make.log"
        for target in $targets; do cp "$build/$target" "$work/pe/$arch-windows/"; done
    fi
    echo "PE modules built from the patched tree: $(ls "$work/pe/$arch-windows" | tr '\n' ' ')"
done
# wine.inf from the patched tree: it says which modules wineboot registers in a prefix (patch 2006 registers winedmo's
# classes in place of winegstreamer's), and the host build's is made from the unpatched one.
wmake -C "$build" loader/wine.inf >> "$work/make.log" 2>&1 || fail "cannot make wine.inf; see $work/make.log"
cp "$build/loader/wine.inf" "$work/pe/wine.inf"

# The PRX link. Each module takes the objects of its ELF link (read back
# from make.log), the shims, and a descriptor naming what its loader
# resolves: the title calls into ntdll, and ntdll's dlsym into win32u.
prx=$work/prx
rm -rf "$prx"
mkdir -p "$prx/obj" "$prx/sce_module"
link_objects() {
    python3 - "$work/make.log" "$1" <<'PY'
import shlex, sys
text = open(sys.argv[1], errors="replace").read().replace("\\\n", " ")
lines = [line for line in text.splitlines() if f"-o {sys.argv[2]} " in line]
if lines:
    print(" ".join(arg for arg in shlex.split(lines[-1])
                   if arg.endswith(".o") and not arg.startswith("/")))
PY
}
# link_prx NAME TARGET "EXTRA OBJECTS" ["IMPORTED MODULES"] ["EXTRA STUB DIR"];
# TARGET "-" takes only the extra objects (a module that is not one of
# Wine's own targets).
link_prx() {
    name=$1; objects=; stubs=; link_options=
    if [ "$name" = dnsapi ] || [ "$name" = winedmo ]; then
        # c-ares and FFmpeg are private implementation: retain only the Unix API tables and
        # module descriptor. Unused service-database APIs must not become imports.
        cat > "$prx/obj/$name.exports" <<'EOF'
{ global: __wine_unix_call_funcs; __wine_unix_call_wow64_funcs; pw_prx_exports; module_start; module_stop; local: *; };
EOF
        link_options="--gc-sections --version-script=$prx/obj/$name.exports"
    fi
    # The mount manager reads and makes the prefix's dosdevices links, which a title keeps in ntdll's virtual link
    # table (symlink() is libkernel_sys, pw_wine_cwd.h): its path and descriptor calls go through ntdll's wraps.
    # win32u opens every font file by its Unix name, dosdevices/c:/... (freetype.c unix_face_create): without the
    # wraps the prefix's Fonts and every font a program added (AddFontResourceEx: gdi32 font.c:2118, 6300, 7080,
    # 2026-10-27) failed to open, and only the runtime's share/wine/fonts copies loaded.
    # shellcheck disable=SC2154
    [ "$name" != mountmgr ] && [ "$name" != win32u ] || link_options="$cwd_wraps"
    for module in ${4:-}; do stubs="$stubs --stub $module"; done
    [ "$2" = - ] || objects=$(link_objects "$2")
    log=$prx/$name.link.log
    [ -n "$objects$3" ] || { echo "no ELF link of $2 in make.log" > "$log"; prx_status=1; return; }
    if [ "$name" = win32u ] && [ -n "$ps5opengl_sdk" ]; then
        opengl_stubs=$prx/opengl-stubs
        mkdir -p "$opengl_stubs"
        cp "$sdk"/target/lib/*.so "$opengl_stubs/"
        # A title has libkernel and libSceLibcInternal, not the WebKit
        # process's libkernel_web and libScePosixForWebKit: symbols bound to
        # those resolve to NULL in the title (pthread_getspecific in
        # win32u's first call). Leave them out so everything binds to the
        # title's own libraries, and a symbol only they have stays unresolved.
        rm -f "$opengl_stubs/libkernel_web.so" "$opengl_stubs/libkernel_sys.so" "$opengl_stubs/libScePosixForWebKit.so"
        for import in "$ps5opengl_sdk"/lib/*.so; do
            [ -e "$import" ] || continue
            [ -e "$opengl_stubs/$(basename "$import")" ] || cp "$import" "$opengl_stubs/"
        done
        # The OpenGL SDK's consumer graph includes C++ objects. Use the SDK's
        # C++ runtime archives, but do not pull a second copy of libc into
        # win32u: its allocator and system imports belong to the existing Wine
        # PRX/module contracts, and the SDK payload libc adds raw syscalls.
        # shellcheck disable=SC2086
        # Match the upstream Core33 link options: force its weak AGC Gate2
        # entry point into the archive so the draw submit path is available.
        (cd "$build" && "$sdk/bin/prospero-clang++" -shared -nodefaultlibs -Wl,--build-id=none \
            -Wl,-Bsymbolic -Wl,-T,"$pie" -Wl,-T,"$root/wine/ps5/prx_eh_frame.ld" \
            -Wl,--eh-frame-hdr -Wl,-soname,"$name.prx" -Wl,-z,defs \
            -Wl,--warn-unresolved-symbols $(for option in $link_options; do printf -- '-Wl,%s ' "$option"; done) \
            -L"$work/ps5lib" -L"$ps5opengl_sdk/lib" \
            -Wl,-u,ps5_agc_gate2_run -o "$prx/$name.shared.elf" $objects $3 ${4:-} \
            -Wl,--start-group -l"$ps5opengl_lib" -Wl,--end-group \
            -Wl,--start-group "$sdk/target/lib/libunwind.a" \
            "$sdk/target/lib/libc++abi.a" "$sdk/target/lib/libc++.a" -Wl,--end-group \
            -lSceAgc -lSceAgcDriver -lSceVideoOut -lkernel -lSceSystemService \
            -Wl,--as-needed "$opengl_stubs"/*.so) > "$log" 2>&1 &&
        stubs_dir=$opengl_stubs
    else
        # shellcheck disable=SC2086
        (cd "$build" && "$sdk/bin/prospero-lld" --shared -Bsymbolic -T "$pie" \
            -T "$root/wine/ps5/prx_eh_frame.ld" --eh-frame-hdr -soname "$name.prx" -z defs \
            --warn-unresolved-symbols $link_options -L"$work/ps5lib" -o "$prx/$name.shared.elf" $objects $3 ${4:-} \
            "$sdk/target/lib/libunwind.a" --as-needed "${5:-$title_stubs}"/*.so) > "$log" 2>&1
        stubs_dir=${5:-$title_stubs}
    fi &&
    "$tool" link --module --in "$prx/$name.shared.elf" --out "$prx/$name.elf" \
        --stub-dir "$stubs_dir" $stubs --module-sdk 0x02000009 \
        --companion-sdk 0x08050001 --file-name "$name.prx" >> "$log" 2>&1 &&
    "$tool" self --sign --in "$prx/$name.elf" --out "$prx/sce_module/$name.prx" >> "$log" 2>&1 ||
        prx_status=1
}
if [ "$prx_status" = 0 ]; then
    for unit in pw_wine_prx pw_wine_dl pw_wine_dl_libc pw_wine_compat pw_wine_compat_libc pw_wine_net_libc \
            pw_wine_threads pw_wine_threads_libc pw_wine_sink pw_wine_window pw_wine_pages pw_wine_pages_ps5 pw_wine_blocks pw_wine_dsection pw_wine_dmap pw_wine_perf pw_wine_cwd pw_wine_cwd_libc \
            pw_wine_fdv pw_wine_fdv_libc pw_dmem_trace pw_wine_process pw_wine_process_io pw_wine_prefix pw_wine_vm pw_wine_wallpaper \
            pw_wine_xattr; do
        $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
            -c "$root/wine/ps5/$unit.c" -o "$prx/obj/$unit.o" || fail "cannot compile $unit.c"
    done
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_wine_launch.c" -o "$prx/obj/pw_wine_launch.o" || fail "cannot compile pw_wine_launch.c"
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_environment.c" -o "$prx/obj/pw_environment.o" || fail "cannot compile pw_environment.c"
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_elfldr.c" -o "$prx/obj/pw_elfldr.o" || fail "cannot compile pw_elfldr.c"
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_window_page.c" -o "$prx/obj/pw_window_page.o" || fail "cannot compile pw_window_page.c"
    # The in-process synchronization device (src/pw_ntsync.c) and its table
    # for Wine's ntsync code, built against the patched tree's
    # wine/ps5_ntsync.h (patch 0890), which it must match.
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_ntsync.c" -o "$prx/obj/pw_ntsync.o" || fail "cannot compile pw_ntsync.c"
    # The ring a helper plays its sound through, which the sink writes in a helper and mixes in the title.
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_audio_ring.c" -o "$prx/obj/pw_audio_ring.o" || fail "cannot compile pw_audio_ring.c"
    # The title's side of the memory helper, which makes the memory the title shares with helpers.
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_memory_helper.c" -o "$prx/obj/pw_memory_helper.o" || fail "cannot compile pw_memory_helper.c"
    # The title's side of a helper's memory service, which copies another process's memory for the server.
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/src/pw_memory_service.c" -o "$prx/obj/pw_memory_service.o" || fail "cannot compile pw_memory_service.c"
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC -I"$tree/include" \
        -c "$root/wine/ps5/pw_wine_ntsync.c" -o "$prx/obj/pw_wine_ntsync.o" ||
        fail "cannot compile pw_wine_ntsync.c"
    # ntdll: the loader, compat, the title's present and input sink, the
    # runtime counters and the sync device; the server registers parent
    # threads while each headless child keeps its own native thread registry.
    shims="$prx/obj/pw_elfldr.o $prx/obj/pw_environment.o $prx/obj/pw_wine_vm.o $prx/obj/pw_wine_prefix.o $prx/obj/pw_wine_launch.o $prx/obj/pw_ntsync.o $prx/obj/pw_wine_ntsync.o $prx/obj/pw_wine_process.o $prx/obj/pw_wine_process_io.o $prx/obj/pw_wine_threads.o $prx/obj/pw_audio_ring.o $prx/obj/pw_memory_helper.o $prx/obj/pw_memory_service.o"
    for unit in pw_wine_prx pw_wine_dl pw_wine_dl_libc pw_wine_compat pw_wine_compat_libc pw_wine_net_libc \
            pw_wine_sink pw_wine_window pw_wine_pages pw_wine_pages_ps5 pw_wine_blocks pw_wine_dsection pw_wine_dmap pw_window_page pw_wine_perf pw_wine_cwd pw_wine_cwd_libc pw_wine_fdv pw_wine_fdv_libc pw_wine_xattr; do
        shims="$shims $prx/obj/$unit.o"
    done
    # A title cannot chdir: ntdll and wineserver each keep their own working
    # directory, and their path calls go through it (wine/ps5/pw_wine_cwd.h).
    cwd_wraps=""
    for name in $(grep -oh '__wrap_[a-z_0-9]*' "$root/wine/ps5/pw_wine_cwd_libc.c" "$root/wine/ps5/pw_wine_fdv_libc.c" |
            sed 's/^__wrap_//' | LC_ALL=C sort -u); do
        cwd_wraps="$cwd_wraps --wrap=$name"
    done
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/ntdll_desc.c" \
        __wine_main pw_wine_dl_configure pw_wine_dl_adopt dlopen dlsym dlerror \
        pw_wine_heap_stats pw_wine_heap_malloc pw_wine_heap_free \
        pw_wine_set_present_sink pw_wine_set_frame_allocator pw_wine_post_input pw_wine_sink_stats pw_wine_present_origin pw_ps5_set_frame_sink pw_ps5_set_window_sink pw_wine_window_present pw_wine_window_present_surface pw_wine_window_drop pw_ps5_set_resolver pw_wine_resolve pw_ps5_set_dns_config pw_wine_dns_config \
        pw_wine_present pw_wine_next_input pw_wine_input_fd \
        pw_wine_set_dualsense pw_wine_dualsense pw_wine_input_note pw_wine_input_stats pw_wine_set_pad pw_wine_rumble pw_wine_pad pw_wine_set_rumble pw_wine_set_suspend \
        pw_wine_set_audio_sink pw_wine_audio_available pw_wine_audio_output \
        __wine_virtual_stats __wine_file_write_stats __wine_file_call_stats __wine_ps5_section_stats __wine_ps5_mmu4k_stats __wine_virtual_image_stats __wine_virtual_fault_top __wine_ps5_set_output_sink __wine_ps5_memory_stats __wine_ps5_image_read_stats __wine_ps5_load_timing pw_ps5_code_round_timing pw_wine_dl_timing __wine_ps5_dmem_holder __wine_ps5_exec_stats __wine_ps5_code_share_stats __wine_ps5_code_share_mapped_kib __wine_ps5_code_cache_stats __wine_ps5_foreign_stats __wine_ps5_exec_convert __wine_ps5_exec_convert_image __wine_ps5_host_allows __wine_ps5_exec_refused __wine_ps5_set_payload_memory __wine_ps5_set_payload_low_image __wine_ps5_set_payload_direct_memory __wine_ps5_set_payload_module __wine_ps5_set_payload_sigaction __wine_ps5_set_payload_thread_start pw_cwd_set \
        pw_wine_set_display_release pw_wine_release_display \
        pw_wine_perf_set_tsc_hz pw_wine_perf_snapshot pw_wine_perf_frame_percentile pw_wine_perf_vlock_read \
        pw_wine_threads_sample pw_wine_describe_thread pw_wine_perf_ticks pw_wine_perf_frame pw_wine_perf_media pw_wine_server_flush pw_wine_server_terminate ps5_thread_is_terminating \
        pw_wine_add_exit_hook pw_wine_stop_audio pw_wine_fdv_stats pw_wine_add_thread_exit_hook \
        pw_wine_server_snapshot pw_ntsync_dump pw_ntsync_counters pw_ntsync_region_bytes pw_ntsync_process_gone pw_wine_ntsync_start pw_ps5_child_start pw_ps5_child_exit pw_ps5_child_doomed pw_ps5_child_doom \
        pw_ps5_set_initial_environment pw_ps5_initial_environment pw_ps5_start_root pw_ps5_start_memory_helper pw_ps5_memory_lock_busy pw_ps5_helpers pw_ps5_shared_alloc pw_ps5_transport_alloc pw_ps5_shared_touch pw_ps5_dmem_alloc pw_ps5_dmem_free pw_ps5_dmem_grant pw_ps5_dsection_register pw_ps5_dsection_release pw_ps5_dsection_collect_now pw_ps5_end_spawns pw_ps5_dsection_page pw_ps5_memory_census pw_ps5_start_times pw_ps5_code_share_duplicate_bytes pw_ps5_child_memory pw_ps5_child_protocol pw_ps5_root_result pw_ps5_root_present pw_ps5_unload_root pw_ps5_wait_children pw_ps5_remote_signal pw_ps5_remote_close pw_ps5_set_graphics_start pw_ps5_set_title_client_start pw_ps5_set_application_lookup pw_ps5_set_fixed_base_lookup pw_ps5_set_child_notice pw_ps5_set_gpu_broker
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/win32u_desc.c" __wine_unix_call_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/wineserver_desc.c" \
        pw_wineserver_connect pw_wine_thread_register pw_wine_thread_unregister pw_wineserver_set_ntsync pw_wineserver_set_ntsync_region pw_wineserver_flush pw_wineserver_terminate \
        pw_wineserver_call_direct pw_wineserver_snapshot pw_wine_threads_set_remote_signal pw_wineserver_input pw_wineserver_cursor pw_wineserver_foreground pw_wineserver_windows pw_wineserver_set_shared_memory pw_wineserver_set_dsection_fd pw_wineserver_set_remote_memory \
        pw_module_share pw_wineserver_user_processes
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/wineps5_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/xinput_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/winevulkan_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/opengl32_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/ws2_32_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/winebus_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/crypt32_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/bcrypt_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/secur32_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/dnsapi_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/nsiproxy_desc.c" __wine_unix_call_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/mountmgr_desc.c" __wine_unix_call_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/winedmo_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/dwrite_desc.c" \
        __wine_unix_call_funcs __wine_unix_call_wow64_funcs
    # The GnuTLS functions bcrypt, secur32 and crypt32 look up with dlsym (wine/ps5/gnutls-exports.txt).
    # shellcheck disable=SC2046
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/libgnutls_desc.c" $(cat "$root/wine/ps5/gnutls-exports.txt")
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/libvulkan_desc.c" \
        vkGetInstanceProcAddr vkGetDeviceProcAddr
    # shellcheck disable=SC2086
    python3 "$root/tools/gen_prx_descriptor.py" "$prx/obj/libfreetype_desc.c" $FREETYPE_EXPORTS
    for unit in ntdll_desc win32u_desc wineserver_desc wineps5_desc \
            libfreetype_desc xinput_desc winevulkan_desc opengl32_desc ws2_32_desc crypt32_desc winebus_desc \
            libvulkan_desc bcrypt_desc secur32_desc dwrite_desc nsiproxy_desc mountmgr_desc dnsapi_desc libgnutls_desc winedmo_desc; do
        $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC -I"$root/wine/ps5" \
            -c "$prx/obj/$unit.c" -o "$prx/obj/$unit.o" || fail "cannot compile $unit.c"
    done
    # The open-file budget is the platform layer's (src/fdv.c); only that object is pulled from its archive.
    link_prx ntdll dlls/ntdll/ntdll.so "$heap $dmem $shims $prx/obj/ntdll_desc.o $cwd_wraps $sdk/target/lib/libps5platform.a"
    if [ -n "$ps5opengl_sdk" ]; then
        # Mesa's embedded diagnostics name these libc APIs, but a title has no
        # process launcher or syslog daemon. Keep those paths inert and supply
        # only the SDK's TLS helper; do not pull its syscall-bearing libc.a.
        $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
            -c "$root/wine/ps5/pw_opengl_libc.c" -o "$prx/obj/pw_opengl_libc.o" ||
            fail "cannot compile OpenGL libc shims"
        (cd "$prx/obj" && "$sdk/bin/llvm-ar" x "$sdk/target/lib/libc.a" emutls.o) ||
            fail "no emutls.o in the payload SDK's libc.a"
        link_prx win32u dlls/win32u/win32u.so \
            "$prx/obj/win32u_desc.o $prx/obj/pw_wine_wallpaper.o $prx/obj/pw_opengl_libc.o $prx/obj/emutls.o $prx/obj/pw_dmem_trace.o \
             -Wl,--wrap=sceKernelAllocateDirectMemory -Wl,--wrap=sceKernelReleaseDirectMemory" \
            "$prx/ntdll.shared.elf"
    else
        link_prx win32u dlls/win32u/win32u.so "$prx/obj/win32u_desc.o $prx/obj/pw_wine_wallpaper.o" "$prx/ntdll.shared.elf"
    fi
    # ntdll loads it with its own dlopen; it has its own heap, needs no
    # dlfcn of its own, and signals threads through the registry ntdll fills.
    link_prx wineserver server/wineserver "$heap $prx/obj/pw_wine_compat.o \
        $prx/obj/pw_wine_compat_libc.o $prx/obj/pw_wine_net_libc.o $prx/obj/pw_wine_threads.o \
        $prx/obj/pw_wine_threads_libc.o $prx/obj/wineserver_desc.o \
        $prx/obj/pw_wine_cwd.o $prx/obj/pw_wine_cwd_libc.o $prx/obj/pw_wine_fdv_libc.o $prx/obj/pw_wine_xattr.o $cwd_wraps"
    # ws2_32 uses emulated TLS; no i386 CPU backend is built.
    (cd "$prx/obj" && "$sdk/bin/llvm-ar" x "$sdk/target/lib/libc.a" emutls.o) ||
        fail "no emutls.o in the payload SDK libc.a"
    link_prx libfreetype - "$ft/obj/*.o $prx/obj/libfreetype_desc.o"
    # Upstream libresolv is unavailable on Sony. Keep its PE parser and Unix
    # contract, supplied by our small adapter over pinned c-ares.
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -fPIC -D__WINESRC__ -DWINE_UNIX_LIB -D_REENTRANT \
        -I"$build/include" -I"$tree/include" -I"$tree/dlls/dnsapi" -I"$root/wine/ps5" \
        -I"$cares/c-ares-1.34.8/include" -I"$cares/build" \
        -c "$root/wine/ps5/pw_dns_unix.c" -o "$prx/obj/pw_dns_unix.o" || fail "cannot compile DNSAPI adapter"
    link_prx dnsapi - "$prx/obj/dnsapi_desc.o $prx/obj/pw_dns_unix.o $cares/build/lib/libcares.a" \
        "$prx/ntdll.shared.elf"
    mkdir -p "$prx/licenses/cares"
    cp "$cares/LICENSE.md" "$prx/licenses/cares/LICENSE.md"
    # winedmo's Unix side, GE-Proton's media backend (patches 2001-2062: demuxing, decoding, conversion and muxing for
    # Media Foundation, DirectShow and the WM reader), over the pinned FFmpeg; its sources are the module's files Wine
    # marks as Unix side.
    winedmo=""
    for unit in $(sed -n '/^SOURCES/,/^$/p' "$tree/dlls/winedmo/Makefile.in" | grep -o '[A-Za-z0-9_/-]*\.c'); do
        grep -q 'pragma makedep unix' "$tree/dlls/winedmo/$unit" || continue
        object=$prx/obj/winedmo_$(echo "$unit" | tr / _ | sed 's/\.c$//').o
        $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -fPIC -ffunction-sections -fdata-sections -D__WINESRC__ \
            -DWINE_UNIX_LIB -D_REENTRANT -D_WINEDMO -DHAVE_FFMPEG=1 -DHAVE_LIBAVCODEC_BSF_H=1 -I"$tree/dlls/winedmo" \
            -I"$build/dlls/winedmo" -I"$build/include" -I"$tree/include" -I"$ff/install/include" -I"$root/wine/ps5" \
            -c "$tree/dlls/winedmo/$unit" -o "$object" || fail "cannot compile winedmo's $unit"
        winedmo="$winedmo $object"
    done
    $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/pw_winedmo_libc.c" -o "$prx/obj/pw_winedmo_libc.o" || fail "cannot compile pw_winedmo_libc.c"
    link_prx winedmo - "$winedmo $prx/obj/winedmo_desc.o $prx/obj/pw_winedmo_libc.o $ff/install/lib/libavformat.a \
        $ff/install/lib/libavcodec.a $ff/install/lib/libswscale.a $ff/install/lib/libswresample.a \
        $ff/install/lib/libavutil.a" "$prx/ntdll.shared.elf"
    mkdir -p "$prx/licenses/ffmpeg"
    cp "$ff/licenses/"* "$prx/licenses/ffmpeg/"
    link_prx nsiproxy dlls/nsiproxy.sys/nsiproxy.so \
        "$prx/obj/nsiproxy_desc.o" "$prx/ntdll.shared.elf"
    # The mount manager (winedevice.exe's mountmgr.sys) gives drive letters their \??\C: devices and volumes:
    # without its Unix side it does not start, and QueryDosDevice, NT and GUID paths fail (ntdll file, 2026-10-25).
    link_prx mountmgr dlls/mountmgr.sys/mountmgr.so "$prx/obj/mountmgr_desc.o" "$prx/ntdll.shared.elf"
    # DirectWrite parses fonts in its PE DLL but rasterizes them in its Unix
    # FreeType backend. Stage both native/WoW64 call tables, like other Unix DLLs.
    link_prx dwrite dlls/dwrite/dwrite.so "$prx/obj/dwrite_desc.o" "$prx/ntdll.shared.elf"
    # The audio driver (wine/wineps5): mmdevapi loads it by name, and it
    # mixes into the title's audio sink, which ntdll exports.
    # mmdevapi's interfaces are IDL; the ELF link built only what ntdll,
    # win32u and the server include.
    wmake -C "$build" include/audioclient.h include/mmdeviceapi.h include/devicetopology.h \
        include/propsys.h include/propidl.h include/objidl.h include/objidlbase.h \
        include/unknwn.h include/wtypes.h include/oaidl.h >> "$work/make.log" 2>&1 ||
        fail "cannot generate mmdevapi's headers; see $work/make.log"
    audio=""
    for unit in wine/wineps5/unix.c src/pw_audio_mix.c; do
        object=$prx/obj/wineps5_$(basename "$unit" .c).o
        $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -fPIC -D__WINESRC__ -DWINE_UNIX_LIB -D_REENTRANT \
            -I"$build/include" -I"$tree/include" -I"$tree/dlls/mmdevapi" -I"$root/wine/ps5" \
            -I"$root/src" -I"$root/include" -c "$root/$unit" -o "$object" || fail "cannot compile $unit"
        audio="$audio $object"
    done
    link_prx wineps5 - "$audio $prx/obj/wineps5_desc.o" "$prx/ntdll.shared.elf"
    # xinput's Unix library (patch 0470), which every xinput loads by name:
    # it reads the title's controller through ntdll's dlsym.
    $ccache "$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Werror -fPIC -D__WINESRC__ -DWINE_UNIX_LIB \
        -D_REENTRANT -I"$tree/dlls/xinput1_3" -I"$build/include" -I"$tree/include" \
        -c "$tree/dlls/xinput1_3/unixlib.c" -o "$prx/obj/xinput_unixlib.o" ||
        fail "cannot compile xinput's unixlib.c"
    link_prx xinput1_3 - "$prx/obj/xinput_unixlib.o $prx/obj/xinput_desc.o" "$prx/ntdll.shared.elf"
    # Wine's Vulkan Unix side: it calls into ntdll and into win32u's Vulkan
    # driver (__wine_get_vulkan_driver).
    link_prx winevulkan dlls/winevulkan/winevulkan.so "$prx/obj/winevulkan_desc.o" \
        "$prx/ntdll.shared.elf $prx/win32u.shared.elf"
    # OpenGL's Unix side, without OpenGL (the console has none): wined3d, which
    # Wine's d3d10.dll imports even over DXVK's d3d10core, needs opengl32 to
    # initialise, and it does so with no driver.
    link_prx opengl32 dlls/opengl32/opengl32.so "$prx/obj/opengl32_desc.o" \
        "$prx/ntdll.shared.elf $prx/win32u.shared.elf"
    link_prx winebus dlls/winebus.sys/winebus.so "$prx/obj/winebus_desc.o" "$prx/ntdll.shared.elf"

    # Winsock's Unix side: games import ws2_32 even when they never go online
    # (Warcraft III's does), and it does not initialise without it.
    $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/pw_ws2_32_libc.c" -o "$prx/obj/pw_ws2_32_libc.o" ||
        fail "cannot compile pw_ws2_32_libc.c"
    link_prx ws2_32 dlls/ws2_32/ws2_32.so \
        "$prx/obj/ws2_32_desc.o $prx/obj/pw_ws2_32_libc.o $prx/obj/emutls.o" "$prx/ntdll.shared.elf"
    # CryptoAPI's Unix side: crypt32 does not initialise without it, and
    # FFmpeg's avformat imports crypt32 (LAV Filters, the DirectShow filters
    # Warcraft III's cinematics play through). Without GnuTLS it keeps what a
    # game needs: stores, certificates, hashes; PFX import reports failure.
    link_prx crypt32 dlls/crypt32/crypt32.so "$prx/obj/crypt32_desc.o" "$prx/ntdll.shared.elf"
    # Windows' TLS (schannel) and bcrypt's ciphers and keys, over GnuTLS, which they load as libgnutls.so: the
    # library and the cryptography under it as one module, beside ntdll.prx.
    link_prx bcrypt dlls/bcrypt/bcrypt.so "$prx/obj/bcrypt_desc.o" "$prx/ntdll.shared.elf"
    link_prx secur32 dlls/secur32/secur32.so "$prx/obj/secur32_desc.o" "$prx/ntdll.shared.elf"
    $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/pw_gnutls_libc.c" -o "$prx/obj/pw_gnutls_libc.o" || fail "cannot compile pw_gnutls_libc.c"
    link_prx libgnutls - "$prx/obj/libgnutls_desc.o $prx/obj/pw_gnutls_libc.o $tls/install/lib/libgnutls.a \
        $tls/install/lib/libhogweed.a $tls/install/lib/libnettle.a $prx/obj/emutls.o"
    # The Vulkan driver itself, from ps5vk's SDK: only what its two entry
    # points reach, since the archive repeats a member. Its import facades
    # join the SDK's stubs.
    if [ -n "$ps5vk_sdk" ] && [ -f "$ps5vk_sdk/lib/libps5vk.a" ]; then
        mkdir -p "$prx/vkstubs"
        cp "$sdk"/target/lib/*.so "$ps5vk_sdk"/lib/libSceAgc*.so "$prx/vkstubs/"
        $ccache "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
            -c "$root/wine/ps5/pw_vulkan_libc.c" -o "$prx/obj/pw_vulkan_libc.o" ||
            fail "cannot compile pw_vulkan_libc.c"
        link_prx libvulkan - "-S -u vkGetInstanceProcAddr -u vkGetDeviceProcAddr \
            $prx/obj/libvulkan_desc.o $prx/obj/pw_vulkan_libc.o $prx/obj/emutls.o \
            $ps5vk_sdk/lib/libps5vk.a $ps5vk_sdk/lib/libpsbc.a $sdk/target/lib/libc++.a \
            $sdk/target/lib/libc++abi.a" "" "$prx/vkstubs"
        vulkan_status="libvulkan.prx from $ps5vk_sdk"
    elif [ -n "$radv" ]; then
        # RADV, from a PS5_Vulkan checkout's release archive and its own link
        # recipe (tools/link_radv_prx.sh).
        if revision=$(bash "$root/tools/link_radv_prx.sh" "$radv" "$prx" "$tool" "$pie"); then
            vulkan_status="libvulkan.prx from RADV, PS5_Mesa $revision"
        else
            prx_status=1
            vulkan_status="RADV link failed (see $prx/libvulkan.link.log)"
        fi
    else
        vulkan_status="skipped: no --ps5vk-sdk with lib/libps5vk.a, and no --radv"
    fi
    echo "vulkan: $vulkan_status"
    # Wine's own fonts, staged under share/wine/fonts beside the runtime.
    mkdir -p "$prx/fonts"
    cp "$tree"/fonts/*.ttf "$prx/fonts/"
fi

if python3 - "$build" "$work/make.log" "$work/report.json" "$sdk" "$WINE_COMMIT" "$prx" "$prx_status" \
    $ordered <<'PY'
import hashlib, json, re, shutil, subprocess, sys
from pathlib import Path
build, log, report, sdk, commit, prx, prx_status = sys.argv[1:8]
patches = sys.argv[8:]
text = Path(log).read_text(errors="replace")
owners = {"dlls/ntdll/": "dlls/ntdll/ntdll.so", "dlls/win32u/": "dlls/win32u/win32u.so",
          "server/": "server/wineserver", "dlls/winevulkan/": "dlls/winevulkan/winevulkan.so",
          "dlls/opengl32/": "dlls/opengl32/opengl32.so", "dlls/ws2_32/": "dlls/ws2_32/ws2_32.so",
          "dlls/crypt32/": "dlls/crypt32/crypt32.so", "dlls/winebus.sys/": "dlls/winebus.sys/winebus.so",
          "dlls/bcrypt/": "dlls/bcrypt/bcrypt.so", "dlls/secur32/": "dlls/secur32/secur32.so",
          "dlls/dnsapi/": "dlls/dnsapi/dnsapi.so", "dlls/nsiproxy.sys/": "dlls/nsiproxy.sys/nsiproxy.so", "dlls/dwrite/": "dlls/dwrite/dwrite.so",
          "dlls/mountmgr.sys/": "dlls/mountmgr.sys/mountmgr.so"}
unresolved = {target: set() for target in owners.values()}
# lld prints each unresolved symbol, then ">>> referenced by" lines whose
# continuation names the object ("dir/file.o:(function)"); the object's
# directory names the target whose link reported it.
symbol = None
for line in text.splitlines():
    match = re.search(r"undefined symbol: (\S+)", line)
    if match:
        symbol = match.group(1)
        continue
    match = re.search(r">>>\s+(\S+\.o):", line)
    if match and symbol:
        for prefix, target in owners.items():
            if match.group(1).startswith(prefix):
                unresolved[target].add(symbol)
readelf = shutil.which("llvm-readelf-18") or shutil.which("llvm-readelf") or f"{sdk}/bin/llvm-readelf"
result = {"schema": 1, "wine_commit": commit, "patches": patches, "targets": {}}
for target in owners.values():
    path = Path(build) / target
    entry = {"built": path.is_file(), "unresolved": sorted(unresolved[target])}
    if path.is_file():
        data = path.read_bytes()
        entry.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        dynamic = subprocess.run([readelf, "-d", str(path)], capture_output=True, text=True).stdout
        entry["needed"] = re.findall(r"Shared library: \[([^\]]+)\]", dynamic)
        symbols = subprocess.run([f"{sdk}/bin/llvm-nm", "-D", str(path)],
                                 capture_output=True, text=True).stdout.split("\n")
        kinds = {line.split()[-2] for line in symbols
                 if line.split() and line.split()[-1] == "malloc" and len(line.split()) >= 2}
        entry["malloc"] = "defines" if kinds & {"T", "t"} else "imports" if "U" in kinds else "absent"
    result["targets"][target] = entry
result["errors"] = sorted(set(re.findall(r"error: (.+)", text)))
# Per PRX: what the stub link left unresolved, the tool's refusals, the
# modules it imports from, and its data imports, since a data import
# between application PRXs faults on the console (measured, FW 12.02).
result["prx"] = {"status": prx_status, "modules": {}}
def exports(library, directory=f"{sdk}/target/lib"):
    listing = subprocess.run([f"{sdk}/bin/llvm-nm", "-D", "--defined-only", f"{directory}/{library}"],
                             capture_output=True, text=True).stdout
    return {line.split()[-1].split("@")[0] for line in listing.splitlines() if line.split()}
title_exports = exports("libkernel.so") | exports("libSceLibcInternal.so")
objdump = shutil.which("llvm-objdump-18") or shutil.which("llvm-objdump") or f"{sdk}/bin/llvm-objdump"
SYSCALL_ALLOWED = {"__wine_syscall_dispatcher", "__wine_unix_call_dispatcher"}
ntdll_exports = exports("ntdll.shared.elf", prx) if (Path(prx) / "ntdll.shared.elf").is_file() else set()
for name in ("ntdll", "win32u", "wineserver", "wineps5", "libfreetype", "xinput1_3",
             "winevulkan", "opengl32", "ws2_32", "crypt32", "winebus", "libvulkan", "bcrypt", "secur32",
             "libgnutls", "dwrite", "nsiproxy", "mountmgr", "dnsapi", "winedmo") if not prx_status.startswith("skipped") else ():
    module = Path(prx) / "sce_module" / f"{name}.prx"
    link_log = Path(prx) / f"{name}.link.log"
    if name == "libvulkan" and not link_log.is_file():
        continue    # neither a ps5vk SDK nor RADV given
    link_text = link_log.read_text(errors="replace") if link_log.is_file() else ""
    entry = {"built": module.is_file(),
             "unresolved": sorted(set(re.findall(r"undefined symbol: (\S+)", link_text))),
             "errors": sorted(set(re.findall(r"error: (.+)", link_text)))}
    shared = Path(prx) / f"{name}.shared.elf"
    if shared.is_file():
        dynamic = subprocess.run([readelf, "-d", str(shared)], capture_output=True, text=True).stdout
        entry["needed"] = re.findall(r"Shared library: \[([^\]]+)\]", dynamic)
        symbols = subprocess.run([readelf, "--dyn-syms", "-W", str(shared)],
                                 capture_output=True, text=True).stdout
        entry["data_imports"] = sorted({fields[7] for fields in (line.split() for line in symbols.splitlines())
                                        if len(fields) >= 8 and fields[3] == "OBJECT" and fields[6] == "UND"})
        # A game title does not get libkernel_sys: imports only it exports
        # stay 0 and a call to one jumps to 0 (measured, FW 12.02).
        imports = {fields[7].split("@")[0] for fields in (line.split() for line in symbols.splitlines())
                   if len(fields) >= 8 and fields[6] == "UND"}
        provided = title_exports | (ntdll_exports if name in ("win32u", "wowprospero", "wineps5",
                                                              "xinput1_3", "winevulkan", "opengl32", "ws2_32",
                                                              "crypt32", "winebus", "bcrypt", "secur32", "dwrite", "nsiproxy", "mountmgr", "dnsapi",
                                                              "winedmo") else set())
        entry["title_unbound"] = sorted((imports & exports("libkernel_sys.so")) - provided)
        # The kernel kills a title that executes a syscall instruction outside
        # libkernel (measured: SYSTEM_ILLEGAL_FUNCTION_CALL). Wine's dispatchers
        # keep one on the FS-base restore path, which patch 0500 never takes.
        listing = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(shared)],
                                 capture_output=True, text=True).stdout
        function, found = None, set()
        for line in listing.splitlines():
            match = re.match(r"[0-9a-f]+ <(.+)>:$", line)
            if match:
                function = match.group(1)
            elif re.search(r"\tsyscall\b", line) and function not in SYSCALL_ALLOWED:
                found.add(function)
        entry["raw_syscalls"] = sorted(found)
        # A Unix call table the module defines must be in its descriptor, where the PE side looks it up by name:
        # the 32-bit audio driver found no wow64 table that wineps5 defined (m4s2-2026-10-07-01: c0000139).
        defined = subprocess.run([f"{sdk}/bin/llvm-nm", "--defined-only", str(shared)],
                                 capture_output=True, text=True).stdout
        tables = sorted({line.split()[-1] for line in defined.splitlines() if line.split() and
                         line.split()[-1] in ("__wine_unix_call_funcs", "__wine_unix_call_wow64_funcs")})
        descriptor = Path(prx) / "obj" / f"{'xinput' if name == 'xinput1_3' else name}_desc.c"
        listed = descriptor.read_text() if descriptor.is_file() else ""
        entry["unix_tables"] = tables
        unlisted = [table for table in tables if f'"{table}"' not in listed]
        if unlisted:
            entry["errors"].append("defined but not in the descriptor: " + ",".join(unlisted))
    if module.is_file():
        data = module.read_bytes()
        entry.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    result["prx"]["modules"][name] = entry
# The patched PE modules, beside the PRXs.
result["pe"] = {str(path.relative_to(Path(prx).parent / "pe")): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted((Path(prx).parent / "pe").glob("*-windows/*"))}
Path(report).write_text(json.dumps(result, indent=2) + "\n")
for target, entry in result["targets"].items():
    print(f"{target}: built={entry['built']} malloc={entry.get('malloc', '-')} "
          f"unresolved={','.join(entry['unresolved']) or 'none'}")
if prx_status.startswith("skipped"):
    print(f"prx: {prx_status}")
for name, entry in result["prx"]["modules"].items():
    print(f"{name}.prx: built={entry['built']} needed={','.join(entry.get('needed', [])) or '-'} "
          f"unresolved={','.join(entry['unresolved']) or 'none'} "
          f"data_imports={','.join(entry.get('data_imports', [])) or 'none'}")
    for error in entry["errors"]:
        print(f"  error: {error}")
    if entry.get("title_unbound"):
        print(f"  unbound in a title (libkernel_sys only): {','.join(entry['title_unbound'])}")
    if entry.get("raw_syscalls"):
        print(f"  raw syscall instructions (fatal in a title): {','.join(entry['raw_syscalls'])}")
print(f"pe: {', '.join(result['pe']) or 'none'}")
modules = result["prx"]["modules"].values()
sys.exit(3 if any(e.get("title_unbound") for e in modules) else
         4 if any(e.get("raw_syscalls") for e in modules) else 0)
PY
then title_status=0; else title_status=$?; fi
[ "$status" -eq 0 ] || fail "build failed; see $work/make.log"
[ "$title_status" -ne 3 ] || fail "PRX imports only libkernel_sys exports, which a title does not get"
[ "$title_status" -ne 4 ] || fail "PRX contains raw syscalls; see report.json"
[ "$prx_status" != 1 ] || fail "PRX link failed; see $prx/*.link.log"
echo "report: $work/report.json"
