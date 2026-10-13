#!/usr/bin/env bash
# A1: install the Android build prerequisites on macOS.
#
#   scripts/setup-android.sh            # install everything (idempotent)
#   scripts/setup-android.sh --check    # report what is present, change nothing
#
# Installs: JDK 17 (Temurin, via Homebrew), Android cmdline-tools, platform-tools,
# platform 33, build-tools 33.0.2, NDK 25.2.9519653, CMake 3.22.1 (all pinned by
# sonobus/mobile/Builds/Android/app/build.gradle) and an arm64 emulator image.
#
# The SDK goes to $ANDROID_HOME, default $HOME/Library/Android/sdk. Nothing is
# written outside that directory, except the JDK, which Homebrew owns.
#
# Exports printed at the end belong in your shell profile; scripts/build-android.sh
# also reads them from this file's defaults, so a fresh shell still works.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
# Versions pinned by sonobus/mobile/Builds/Android/app/build.gradle.
NDK_VERSION="25.2.9519653"
CMAKE_VERSION="3.22.1"
PLATFORM="android-33"
BUILD_TOOLS="33.0.2"
SYSTEM_IMAGE="system-images;android-33;google_apis;arm64-v8a"
AVD_NAME="crosspoint-33"
JDK_FORMULA="temurin@17"

# cmdline-tools is a plain zip, not a brew formula. Override to update.
CMDLINE_TOOLS_URL="${CMDLINE_TOOLS_URL:-https://dl.google.com/android/repository/commandlinetools-mac-11076708_latest.zip}"

CHECK_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --check) CHECK_ONLY=1 ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok()   { printf '  \033[32mok\033[0m   %s\n' "$*"; }
miss() { printf '  \033[31mmissing\033[0m  %s\n' "$*"; }
warn() { printf '  \033[33mwarn\033[0m %s\n' "$*"; }

[ "$(uname)" = "Darwin" ] || { echo "This script is macOS-only (the project's Android builds run here)." >&2; exit 2; }

# ---------------------------------------------------------------- JDK --------
find_jdk_home() {
    # Prefer Temurin 17, then any 17, then whatever java_home reports.
    for candidate in \
        "/Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home" \
        "/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home" \
        "/usr/local/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"
    do
        [ -x "$candidate/bin/javac" ] && { echo "$candidate"; return 0; }
    done
    local jh
    jh="$(/usr/libexec/java_home -v 17 2>/dev/null || true)"
    [ -n "$jh" ] && [ -x "$jh/bin/javac" ] && { echo "$jh"; return 0; }
    return 1
}

say "JDK 17"
JAVA_HOME_DETECTED="$(find_jdk_home || true)"
if [ -n "$JAVA_HOME_DETECTED" ]; then
    ok "JDK 17 at $JAVA_HOME_DETECTED"
else
    miss "no JDK 17"
    if [ "$CHECK_ONLY" = "0" ]; then
        command -v brew >/dev/null || { echo "Homebrew is required to install the JDK (https://brew.sh)." >&2; exit 1; }
        echo "  installing $JDK_FORMULA with brew (this downloads ~200 MB)..."
        brew install --cask "$JDK_FORMULA"
        JAVA_HOME_DETECTED="$(find_jdk_home || true)"
        [ -n "$JAVA_HOME_DETECTED" ] || { echo "JDK install did not produce a usable java_home." >&2; exit 1; }
        ok "JDK 17 at $JAVA_HOME_DETECTED"
    fi
fi

# ------------------------------------------------------------- SDK tools -----
say "Android SDK at $ANDROID_HOME"
SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"

if [ -x "$SDKMANAGER" ]; then
    ok "cmdline-tools present"
else
    miss "cmdline-tools"
    if [ "$CHECK_ONLY" = "0" ]; then
        command -v unzip >/dev/null || { echo "unzip is required." >&2; exit 1; }
        tmp="$(mktemp -d)"
        trap 'rm -rf "$tmp"' EXIT
        echo "  downloading $CMDLINE_TOOLS_URL ..."
        curl -fL --retry 3 -o "$tmp/cmdline-tools.zip" "$CMDLINE_TOOLS_URL"
        rm -rf "$ANDROID_HOME/cmdline-tools/latest"
        mkdir -p "$ANDROID_HOME/cmdline-tools"
        unzip -q "$tmp/cmdline-tools.zip" -d "$tmp/extract"
        mv "$tmp/extract/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
        ok "cmdline-tools installed"
    fi
fi

if [ "$CHECK_ONLY" = "1" ]; then
    say "Packages"
    for pkg in "platform-tools" "platforms;$PLATFORM" "build-tools;$BUILD_TOOLS" \
               "ndk;$NDK_VERSION" "cmake;$CMAKE_VERSION" "emulator" "$SYSTEM_IMAGE"; do
        if [ -d "$ANDROID_HOME/$(echo "$pkg" | sed 's/;/ /g' | awk '{print $1}')" ] || \
           ls -d "$ANDROID_HOME"/*/"${pkg##*;}" >/dev/null 2>&1; then
            ok "$pkg"
        else
            miss "$pkg"
        fi
    done
    say "Emulator"
    if [ -x "$ANDROID_HOME/emulator/emulator" ] && \
       "$ANDROID_HOME/emulator/emulator" -list-avds 2>/dev/null | grep -qx "$AVD_NAME"; then
        ok "AVD $AVD_NAME"
    else
        miss "AVD $AVD_NAME"
    fi
    say "Summary"
    echo "  ANDROID_HOME=$ANDROID_HOME"
    echo "  JAVA_HOME=${JAVA_HOME_DETECTED:-<missing>}"
    echo "  Re-run without --check to install what is missing."
    exit 0
fi

# --------------------------------------------------------- SDK packages ------
say "SDK packages"
export JAVA_HOME="$JAVA_HOME_DETECTED"
export ANDROID_HOME
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/emulator:$PATH"

# sdkmanager refuses to run if it cannot find java; it also wants the license
# accepted before it will download anything.
echo "  accepting SDK licenses..."
yes | "$SDKMANAGER" --sdk_root="$ANDROID_HOME" --licenses >/dev/null 2>&1 || true

echo "  installing platform-tools, platforms;$PLATFORM, build-tools;$BUILD_TOOLS,"
echo "             ndk;$NDK_VERSION, cmake;$CMAKE_VERSION, emulator, $SYSTEM_IMAGE"
echo "  (the NDK alone is ~1.5 GB; this is the slow step)"
"$SDKMANAGER" --sdk_root="$ANDROID_HOME" --install \
    "platform-tools" "platforms;$PLATFORM" "build-tools;$BUILD_TOOLS" \
    "ndk;$NDK_VERSION" "cmake;$CMAKE_VERSION" "emulator" "$SYSTEM_IMAGE"
ok "packages installed"

# ---------------------------------------------------------------- AVD --------
say "Emulator AVD"
AVDMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager"
if "$ANDROID_HOME/emulator/emulator" -list-avds 2>/dev/null | grep -qx "$AVD_NAME"; then
    ok "AVD $AVD_NAME already exists"
else
    # Device ids vary between cmdline-tools releases, so pick the first that this
    # installation actually knows (pixel_6 gives the 1080x2400 phone of the
    # design's 390x844 reference at ~2.75x).
    DEVICE=""
    for candidate in pixel_6 pixel_5 pixel_4 pixel_2 5.4in WVGA_NEXUS_ONE; do
        if "$AVDMANAGER" list device 2>/dev/null | grep -qE "id: *[0-9]+ or \"$candidate\""; then
            DEVICE="$candidate"; break
        fi
    done
    [ -n "$DEVICE" ] || warn "no known device profile found; creating an AVD with defaults"
    echo "  creating $AVD_NAME (device: ${DEVICE:-default}) ..."
    # 'no' answers "do you want to create a custom hardware profile?".
    if [ -n "$DEVICE" ]; then
        echo "no" | "$AVDMANAGER" create avd --name "$AVD_NAME" --package "$SYSTEM_IMAGE" --device "$DEVICE" --force
    else
        echo "no" | "$AVDMANAGER" create avd --name "$AVD_NAME" --package "$SYSTEM_IMAGE" --force
    fi
    "$ANDROID_HOME/emulator/emulator" -list-avds 2>/dev/null | grep -qx "$AVD_NAME" \
        || { echo "AVD creation failed" >&2; exit 1; }
    ok "AVD $AVD_NAME created"
fi

say "Done"
cat <<EOF
  Add these to your shell profile (~/.zshrc):

    export ANDROID_HOME="$ANDROID_HOME"
    export ANDROID_SDK_ROOT="\$ANDROID_HOME"
    export JAVA_HOME="$JAVA_HOME_DETECTED"
    export PATH="\$JAVA_HOME/bin:\$ANDROID_HOME/platform-tools:\$ANDROID_HOME/emulator:\$ANDROID_HOME/cmdline-tools/latest/bin:\$PATH"

  Then:

    scripts/build-android.sh --emulator     # boot the AVD and install the debug APK
    scripts/build-android.sh                # resave + assembleDebug
EOF
