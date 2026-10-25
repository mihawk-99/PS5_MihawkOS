#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
version=1.0.2
digest=30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4
archive="$root/.deps/ps5-opengl-sdk-$version.tar.gz"
prefix="$root/.deps/ps5-opengl-sdk-$version"
mkdir -p "$root/.deps"
if [[ ! -f $archive ]]; then
    curl -fL --retry 3 -o "$archive.part" \
        "https://github.com/blackbearreloaded/ps5-opengl/releases/download/v$version/ps5-opengl-sdk-$version.tar.gz"
    printf '%s  %s\n' "$digest" "$archive.part" | sha256sum -c -
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha256sum -c -
# Unpacked beside its folder and renamed when complete, so a cut-off unpack is never taken for a finished one.
if [[ ! -d $prefix ]]; then
    rm -rf "$prefix.partial"
    mkdir -p "$prefix.partial"
    tar -xzf "$archive" -C "$prefix.partial" --strip-components=1
    mv "$prefix.partial" "$prefix"
fi
(cd "$prefix/sdk" && sha256sum --status -c manifest.sha256) || {
    echo "$prefix differs from its manifest: delete it to unpack it again" >&2; exit 1; }
printf 'Verified OpenGL SDK: %s/sdk\n' "$prefix"
