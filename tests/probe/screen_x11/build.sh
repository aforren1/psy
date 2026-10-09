#!/bin/sh
# Builds screen_x11_probe.
#   build.sh [output dir]        default: build/ beside this script
#   CC=clang build.sh            another compiler
# EXTRA_CFLAGS and EXTRA_LDFLAGS are added to the command line.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/build"}
mkdir -p "$out"
CC=${CC:-cc}

pkgs="x11 xcb xcb-present xcb-randr gl"
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists $pkgs 2>/dev/null; then
    cflags=$(pkg-config --cflags $pkgs)
    libs=$(pkg-config --libs $pkgs)
else
    echo "build.sh: pkg-config did not find all of: $pkgs; trying plain -l flags" >&2
    cflags=""
    libs="-lX11 -lxcb -lxcb-present -lxcb-randr -lGL"
fi
# Vulkan is loaded at run time; only its headers are needed, and without
# them the Vulkan section is left out.
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan 2>/dev/null; then
    cflags="$cflags $(pkg-config --cflags vulkan)"
fi

set -x
$CC -std=c11 -O2 -g -Wall -Wextra ${EXTRA_CFLAGS:-} $cflags \
    -o "$out/screen_x11_probe" "$here/screen_x11_probe.c" \
    $libs -ldl -lpthread -lm ${EXTRA_LDFLAGS:-}
