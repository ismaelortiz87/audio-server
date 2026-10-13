#!/usr/bin/env bash
# A1: build the Projucer from the vendored JUCE.
#
# The Projucer version must match the vendored JUCE (7.0.8), or its generated
# Android/Xcode projects drift from what the engine expects. We therefore build
# it from juce/ itself rather than installing JUCE's own download.
#
#   scripts/build-projucer.sh              # Release (default)
#   scripts/build-projucer.sh --debug
#
# Output: build/projucer/extras/Projucer/Projucer_artefacts/<cfg>/Projucer.app
# Prints the path of the binary at the end.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG=Release

for arg in "$@"; do
    case "$arg" in
        --debug) CONFIG=Debug ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || { echo "cmake not found (macOS: brew install cmake)" >&2; exit 1; }

# Same workaround as scripts/build-desktop.sh: JUCE's juceaide sub-build ignores
# its parent's deployment target and fails against the macOS 15+ SDK.
if [ "$(uname)" = "Darwin" ]; then
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
fi

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

DIR="$ROOT/build/projucer"

# A cache generated from another source dir or generator cannot be reused.
if [ -f "$DIR/CMakeCache.txt" ]; then
    have_src=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$DIR/CMakeCache.txt")
    have_gen=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$DIR/CMakeCache.txt")
    if [ "$have_src" != "$ROOT/juce" ] || { [ -n "${GEN[1]:-}" ] && [ "$have_gen" != "${GEN[1]}" ]; }; then
        echo "build dir was made differently; starting fresh"
        rm -rf "$DIR"
    fi
fi

# JUCE_BUILD_EXTRAS is what exposes the Projucer target.
cmake -S "$ROOT/juce" -B "$DIR" "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DJUCE_BUILD_EXTRAS=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}" >/dev/null

cmake --build "$DIR" --target Projucer --config "$CONFIG" -j "$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

BIN="$DIR/extras/Projucer/Projucer_artefacts/$CONFIG/Projucer.app/Contents/MacOS/Projucer"
[ -x "$BIN" ] || { echo "build finished but $BIN is missing" >&2; exit 1; }

echo
echo "Projucer: $BIN"
"$BIN" --help >/dev/null 2>&1 && echo "Projucer runs."
