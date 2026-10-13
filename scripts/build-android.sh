#!/usr/bin/env bash
# A1/A3/A11: build the Crosspoint Android app.
#
#   scripts/build-android.sh                # resave the .jucer, then assembleDebug
#   scripts/build-android.sh --resave       # only regenerate Builds/Android from the .jucer
#   scripts/build-android.sh --release      # assembleRelease (needs a keystore, see A11)
#   scripts/build-android.sh --install      # build debug, then install on the running device
#   scripts/build-android.sh --emulator     # boot the AVD (if needed), then --install
#   scripts/build-android.sh --clean        # gradle clean first
#
# Prerequisites come from scripts/setup-android.sh. This script finds the SDK via
# $ANDROID_HOME / $ANDROID_SDK_ROOT, or the default location, and the JDK via
# $JAVA_HOME then /usr/libexec/java_home -v 17.
#
# IMPORTANT: the Projucer owns everything under sonobus/mobile/Builds/Android/.
# Edit sonobus/mobile/SonoBusMobile.jucer, never the generated files, and let
# this script regenerate them. Never hand-merge generated files: resolve the
# .jucer and re-run --resave.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCER="$ROOT/sonobus/mobile/SonoBusMobile.jucer"
ANDROID_DIR="$ROOT/sonobus/mobile/Builds/Android"

CONFIG=debug
DO_RESAVE_ONLY=0
DO_INSTALL=0
DO_EMULATOR=0
DO_CLEAN=0
AVD_NAME="${AVD_NAME:-crosspoint-33}"
PROJUCER_BIN="${PROJUCER_BIN:-$ROOT/build/projucer/extras/Projucer/Projucer_artefacts/Release/Projucer.app/Contents/MacOS/Projucer}"

for arg in "$@"; do
    case "$arg" in
        --resave)   DO_RESAVE_ONLY=1 ;;
        --release)  CONFIG=release ;;
        --install)  DO_INSTALL=1 ;;
        --emulator) DO_EMULATOR=1; DO_INSTALL=1 ;;
        --clean)    DO_CLEAN=1 ;;
        -h|--help)  sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

# ------------------------------------------------------------- toolchain -----
find_jdk_home() {
    if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/javac" ]; then echo "$JAVA_HOME"; return 0; fi
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

if [ "$DO_RESAVE_ONLY" = "0" ]; then
    JAVA_HOME_FOUND="$(find_jdk_home)" || die "no JDK 17 found. Run scripts/setup-android.sh."
    export JAVA_HOME="$JAVA_HOME_FOUND"

    if [ -z "${ANDROID_HOME:-}" ]; then
        for candidate in "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
            [ -d "$candidate" ] && { ANDROID_HOME="$candidate"; break; }
        done
    fi
    [ -n "${ANDROID_HOME:-}" ] || die "Android SDK not found. Run scripts/setup-android.sh, or set ANDROID_HOME."
    export ANDROID_HOME ANDROID_SDK_ROOT="$ANDROID_HOME"
    export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/emulator:$PATH"
fi

# --------------------------------------------------------------- resave ------
resave() {
    [ -x "$PROJUCER_BIN" ] || die "Projucer not found at $PROJUCER_BIN. Run scripts/build-projucer.sh."
    # A6: the .jucer lists console-ui as a binary resource, and the Projucer
    # refuses to save when a listed resource file is missing. Pack it first.
    say "Packing console-ui"
    "$ROOT/scripts/pack-console-ui.sh"
    say "Regenerating Builds/Android from SonoBusMobile.jucer"
    "$PROJUCER_BIN" --resave "$JUCER"
    # The upstream project committed a hard-coded sdk.dir for another machine.
    # local.properties is machine-local and must never be committed (A3).
    echo "sdk.dir=$ANDROID_HOME" > "$ANDROID_DIR/local.properties"
    echo "  wrote local.properties (sdk.dir=$ANDROID_HOME)"
}

if [ "$DO_RESAVE_ONLY" = "1" ]; then
    # --resave alone needs no SDK, only the Projucer.
    ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
    resave
    echo
    echo "Resaved. Review 'git status sonobus/mobile' before committing."
    exit 0
fi

if [ "$DO_EMULATOR" = "1" ]; then
    say "Emulator"
    if ! adb devices 2>/dev/null | grep -qE 'emulator-[0-9]+\s+device'; then
        "$ANDROID_HOME/emulator/emulator" -list-avds | grep -qx "$AVD_NAME" \
            || die "AVD '$AVD_NAME' does not exist. Run scripts/setup-android.sh."
        echo "  booting $AVD_NAME ..."
        "$ANDROID_HOME/emulator/emulator" -avd "$AVD_NAME" -no-boot-anim -no-snapshot-load \
            >/dev/null 2>&1 &
        echo "  waiting for the device to come up (up to 3 min)..."
        "$ANDROID_HOME/platform-tools/adb" wait-for-device
        deadline=$(( $(date +%s) + 180 ))
        until [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; do
            [ "$(date +%s)" -lt "$deadline" ] || die "emulator did not finish booting in 3 minutes"
            sleep 3
        done
        echo "  booted"
    else
        echo "  a device is already attached, using it"
    fi
fi

resave

cd "$ANDROID_DIR"
[ "$DO_CLEAN" = "1" ] && { say "gradle clean"; ./gradlew clean; }

say "Gradle assemble$(echo "$CONFIG" | sed 's/^./\U&/')"
if [ "$CONFIG" = "release" ]; then
    ./gradlew assembleRelease
else
    ./gradlew assembleDebug
fi

# Print the APK path (the product flavor is debug_/release_, see app/build.gradle).
APK="$(find "$ANDROID_DIR/app/build/outputs/apk" -name '*.apk' -newermt '-10 minutes' 2>/dev/null | head -1)"
[ -n "$APK" ] || APK="$(find "$ANDROID_DIR/app/build/outputs/apk" -name '*'"$CONFIG"'*.apk' 2>/dev/null | head -1)"
[ -n "$APK" ] || die "no APK produced"

say "APK"
echo "  $APK"
ls -lh "$APK" | awk '{print "  size: "$5}'

if [ "$DO_INSTALL" = "1" ]; then
    say "Install"
    adb devices | grep -qE '\sdevice$' || die "no device/emulator attached (start one, or use --emulator)"
    adb install -r "$APK"
    echo "  installed. Launch with: adb shell am start -n io.lagreca.app.crosspoint/com.sonosaurus.sonobus.SonoBusActivity"
fi
