#!/bin/sh
# One command: builds the beam-racing probe and runs every section into a
# dated folder. Git Bash or MSYS2 on Windows, from the repo root:
#   sh tests/probe/beam_race/run.sh [parent folder] [probe options...]
# The parent folder defaults to the current folder. Other options go to
# the probe (for example --no-flicker, --yes, --monitor 1). BUILD_DIR
# overrides the build folder (default build/beam_race).
# docs/beam_race_probe.md explains the output.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
parent=.
if [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; then parent=$1; shift; fi
parent=$(cd "$parent" && pwd)
stamp=$(date +%Y%m%d_%H%M%S)
out="$parent/ysp_beam_race_$stamp"
mkdir -p "$out"
echo "results: $out"

bin=${BUILD_DIR:-"$root/build/beam_race"}
if ! { cmake -S "$here" -B "$bin" && cmake --build "$bin" --config Release; } > "$out/build.log" 2>&1; then
    echo "build failed; the end of $out/build.log:"
    tail -20 "$out/build.log"
    exit 1
fi
exe="$bin/Release/beam_race_probe.exe"
[ -f "$exe" ] || exe="$bin/beam_race_probe.exe"

# Windows paths for the probe, which is not an MSYS program.
winout=$(cygpath -w "$out" 2>/dev/null || echo "$out")
"$exe" --out "$winout" "$@"
echo
echo "summary: $out/summary.txt"
