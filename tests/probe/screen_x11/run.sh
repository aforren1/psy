#!/bin/sh
# One command: builds the probe, records the system, runs the probe twice
# (idle, then under CPU and GPU load), and packs everything into one archive.
#   sh tests/probe/screen_x11/run.sh [parent folder]     default: the current folder
# PROBE_ARGS is added to both probe command lines (for example "--angle DIR").
# docs/screen_x11_probe.md explains the output.
set -u
here=$(cd "$(dirname "$0")" && pwd)
parent=$(cd "${1:-.}" && pwd)
stamp=$(date +%Y%m%d_%H%M%S)
name="ysp_x11_probe_$stamp"
out="$parent/$name"
mkdir -p "$out/bin" "$out/system"
echo "results: $out"

if ! sh "$here/build.sh" "$out/bin" > "$out/build.log" 2>&1; then
    echo "build failed; the end of $out/build.log:"
    tail -20 "$out/build.log"
    exit 1
fi
cp "$here/screen_x11_probe.c" "$out/bin/"

# System facts from standard tools. Each is optional.
sys="$out/system"
tool() {
    file=$1
    shift
    if command -v "$1" > /dev/null 2>&1; then "$@" > "$sys/$file.txt" 2>&1; else echo "$1: not installed" > "$sys/$file.txt"; fi
}
{ uname -a; cat /etc/os-release; } > "$sys/os.txt" 2>&1
env | grep -E '^(DISPLAY|XDG_|WAYLAND_|vblank_mode|adaptive_sync|LIBGL_|MESA_|__GL|__EGL|YSP_|SDL_)' > "$sys/env.txt"
tool glxinfo glxinfo
tool glxinfo_B glxinfo -B
tool eglinfo eglinfo -B
tool xrandr xrandr --verbose
tool xdpyinfo xdpyinfo
tool xset xset q
tool xprop_root xprop -root
tool vulkaninfo vulkaninfo --summary
tool lspci lspci -nnk
tool lscpu lscpu
tool sdl3 pkg-config --modversion sdl3
ps -eo pid,comm,args > "$sys/processes.txt" 2>&1
[ -r /var/log/Xorg.0.log ] && cp /var/log/Xorg.0.log "$sys/Xorg.0.var.log"
[ -r "$HOME/.local/share/xorg/Xorg.0.log" ] && cp "$HOME/.local/share/xorg/Xorg.0.log" "$sys/Xorg.0.home.log"
for f in /etc/X11/xorg.conf /etc/X11/xorg.conf.d/*.conf /usr/share/X11/xorg.conf.d/*.conf "$HOME/.drirc" /etc/drirc; do
    [ -r "$f" ] && { echo "== $f"; cat "$f"; }
done > "$sys/config.txt" 2>&1
for f in /sys/class/drm/card*/device/power_dpm_force_performance_level /sys/class/drm/card*/device/pp_power_profile_mode; do
    [ -r "$f" ] && { echo "== $f"; cat "$f"; }
done > "$sys/amdgpu_power.txt" 2>&1

# Ctrl+C stops the probe early; the archive is still made.
stopped=0
trap 'stopped=1' INT

echo "pass 1 of 2: idle. The screens go dark gray with a flickering square at the top left. Do not touch the mouse or keyboard."
"$out/bin/screen_x11_probe" --out "$out/idle" ${PROBE_ARGS:-}

if [ "$stopped" = 0 ]; then
    n=$(nproc 2>/dev/null || echo 4)
    echo "pass 2 of 2: $n busy threads and a heavy shader"
    "$out/bin/screen_x11_probe" --out "$out/load" --quick --load "$n" --gpu-heavy --skip-env --skip-vk --skip-sdl ${PROBE_ARGS:-}
fi

tar -czf "$out.tar.gz" -C "$parent" "$name"
echo
echo "send this file: $out.tar.gz"
