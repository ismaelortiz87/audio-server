#!/usr/bin/env bash
# Build the desktop SonoBus app and the aooserver connection server.
#
#   scripts/build-desktop.sh                 # Release, native arch, app + server
#   scripts/build-desktop.sh --debug
#   scripts/build-desktop.sh --app-only | --server-only
#   scripts/build-desktop.sh --universal     # macOS: x86_64 + arm64 (slower)
#   scripts/build-desktop.sh --plugins       # also VST3/AU + instrument targets
#
# Outputs go under build/ at the repo root. Works on macOS (Command Line Tools
# are enough, Xcode not required) and Linux. Needs cmake; uses ninja if present.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG=Release
BUILD_APP=1
BUILD_SERVER=1
UNIVERSAL=OFF
PLUGINS=0

for arg in "$@"; do
    case "$arg" in
        --debug)       CONFIG=Debug ;;
        --app-only)    BUILD_SERVER=0 ;;
        --server-only) BUILD_APP=0 ;;
        --universal)   UNIVERSAL=ON ;;
        --plugins)     PLUGINS=1 ;;
        -h|--help)     sed -n '2,11p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || { echo "cmake not found (macOS: brew install cmake)" >&2; exit 1; }

# JUCE configures its juceaide helper as a separate CMake project that doesn't
# inherit SonoBus's deployment target, so on a macOS 15+ SDK it targets the host
# OS and fails on APIs obsoleted in 15 (CGWindowListCreateImage). Clang and
# CMake both honour this variable, so juceaide gets an older target too.
if [ "$(uname)" = Darwin ]; then
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
fi

GEN=()
if command -v ninja >/dev/null; then GEN=(-G Ninja); fi

if command -v nproc >/dev/null; then JOBS=$(nproc)
else JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4); fi

cfg_lower=$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')
APP_DIR="$ROOT/build/desktop-$cfg_lower"
SRV_DIR="$ROOT/build/aooserver-$cfg_lower"

# A cache made with another generator can't be reused; start that dir fresh.
fresh_if_generator_changed() {
    local dir="$1" want="${GEN[1]:-}"
    [ -f "$dir/CMakeCache.txt" ] || return 0
    local have
    have=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$dir/CMakeCache.txt")
    if [ -n "$want" ] && [ "$have" != "$want" ]; then rm -rf "$dir"; fi
}

if [ "$BUILD_APP" = 1 ]; then
    fresh_if_generator_changed "$APP_DIR"
    cmake -S "$ROOT/sonobus" -B "$APP_DIR" "${GEN[@]}" \
        -DCMAKE_BUILD_TYPE="$CONFIG" -DUniversalBinary="$UNIVERSAL"
    if [ "$PLUGINS" = 1 ]; then
        cmake --build "$APP_DIR" --config "$CONFIG" -j "$JOBS"
    else
        cmake --build "$APP_DIR" --config "$CONFIG" -j "$JOBS" --target SonoBus_Standalone
    fi
fi

if [ "$BUILD_SERVER" = 1 ]; then
    fresh_if_generator_changed "$SRV_DIR"
    cmake -S "$ROOT/aooserver" -B "$SRV_DIR" "${GEN[@]}" -DCMAKE_BUILD_TYPE="$CONFIG"
    cmake --build "$SRV_DIR" --config "$CONFIG" -j "$JOBS"
fi

echo
echo "Built ($CONFIG):"
if [ "$BUILD_APP" = 1 ]; then
    if [ "$(uname)" = Darwin ]; then
        app=$(find "$APP_DIR" -maxdepth 5 -name '*.app' -type d -path '*Standalone*' | head -1)
        echo "  app:    $app"
        echo "          binary: $app/Contents/MacOS/$(basename "$app" .app)"
    else
        echo "  app:    $(find "$APP_DIR/SonoBus_artefacts" -maxdepth 4 -type f -path '*Standalone*' -perm -u+x | head -1)"
    fi
fi
[ "$BUILD_SERVER" = 1 ] && echo "  server: $SRV_DIR/aooserver"
exit 0
