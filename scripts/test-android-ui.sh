#!/usr/bin/env bash
# A6: build and run the bundled-Console-UI test.
#
#   scripts/test-android-ui.sh
#
# Proves the zip that ships in the APK unpacks to the layout the ApiServer
# serves, and that the extract-once stamp works (so a relaunch does not re-unzip
# and an upgrade does replace a stale UI).
#
# Runs on the Mac: the extractor is the same algorithm as the Android one, but
# the Android binary itself is not needed (and cannot run here).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/tests/android"
BUILD="$ROOT/build/android-test"

command -v cmake >/dev/null || { echo "cmake not found (macOS: brew install cmake)" >&2; exit 1; }

# The test embedded the real artifact, so build it first.
"$ROOT/scripts/pack-console-ui.sh"

if [ "$(uname)" = "Darwin" ]; then
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
fi

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

if [ -f "$BUILD/CMakeCache.txt" ]; then
    have=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt")
    [ "$have" = "$DIR" ] || rm -rf "$BUILD"
fi

cmake -S "$DIR" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target a6check -j "$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)" >/dev/null

echo
# The test needs a writable scratch directory; JUCE's File::tempDirectory points
# outside the repo, which a sandboxed run may deny. Give it one under build/.
"$BUILD/a6check_artefacts/Release/a6check" "$ROOT/build/android-test-sandbox"
