# Building Crosspoint for Android

The phone app runs the whole engine: the audio engine (AOO P2P, Opus) runs in
the app, the control API listens on `127.0.0.1:7070`, and a web view shows the
Console UI from it. Audio stays native and goes straight to the VDIs.

Task cards: [TASKS.md](TASKS.md) P9.2 (A1–A14). Design:
[design/android.md](design/android.md).

## What is installed where

Everything below is installed by `scripts/setup-android.sh`. Re-running it is
safe; `--check` only reports.

| Thing | Version | Where |
|---|---|---|
| JDK | OpenJDK 17 (Homebrew formula, no sudo) | `/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home` |
| Android SDK | — | `$ANDROID_HOME`, default `~/Library/Android/sdk` |
| cmdline-tools | latest | `$ANDROID_HOME/cmdline-tools/latest` |
| platform | android-33 | `$ANDROID_HOME/platforms/android-33` |
| build-tools | 33.0.2 | `$ANDROID_HOME/build-tools/33.0.2` |
| NDK | 25.2.9519653 | `$ANDROID_HOME/ndk/25.2.9519653` |
| CMake | 3.22.1 | `$ANDROID_HOME/cmake/3.22.1` |
| Emulator image | android-33 google_apis arm64-v8a | `$ANDROID_HOME/system-images/...` |
| AVD | `crosspoint-33` (Pixel 6) | `~/.android/avd/` |
| Projucer | built from `juce/` (7.0.8) | `build/projucer/.../Projucer.app` |

The versions are not free choices: `app/build.gradle` pins the NDK and CMake,
and AGP 7.3.0 needs JDK 11–17. The Projucer **must** be the one built from the
vendored `juce/` (7.0.8), not a downloaded release, or the projects it generates
drift from the engine.

## Shell setup

`scripts/setup-android.sh` prints these; add them to `~/.zshrc`:

```sh
export ANDROID_HOME="$HOME/Library/Android/sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export JAVA_HOME="/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

`scripts/build-android.sh` finds all of this on its own when the variables are
unset (JDK 17 via `java_home`, SDK at the default path), so a bare shell works
too.

## Building

```sh
scripts/build-projucer.sh              # once: build the Projucer from juce/
scripts/build-android.sh               # resave the .jucer, then assembleDebug
scripts/build-android.sh --install     # ...and install on the attached device
scripts/build-android.sh --emulator    # boot crosspoint-33, build, install
scripts/build-android.sh --release     # signed arm64 release APK (needs a keystore, see below)
```

The debug APK lands at
`sonobus/mobile/Builds/Android/app/build/outputs/apk/debug_/debug/app-debug_-debug.apk`.

### The `.jucer` owns `Builds/Android/`

`sonobus/mobile/SonoBusMobile.jucer` is the source of truth. Everything under
`sonobus/mobile/Builds/Android/` is **generated** — including
`app/build.gradle`, `app/CMakeLists.txt` and the manifest. Edit the `.jucer`
and re-run `--resave`; never hand-edit a generated file, and never hand-merge
one (resolve the `.jucer` conflict, then regenerate).

This is why A3, A4, A6, A7, A10 and A11 all edit the same file: keep each
change small and rebase before merging.

### `--resave` noise

Re-saving a clean checkout is idempotent, but it does touch files that do not
belong to a task. Expect and discard:

- `Builds/iOS/**` — the Projucer always rewrites the iOS project, including
  line-ending churn in `Info-*.plist` and `Contents.json`. Android tasks do not
  care about iOS; leave those files alone.
- `JuceLibraryCode/JucePluginDefines.h` — `JucePlugin_ARADocumentArchiveID`
  tracks the `.jucer` version; a real change only when the version does.
- `JuceLibraryCode/BinaryData*.cpp`, `BinaryData.h` — generated from the
  resources, including the console-ui zip. Untracked (git-ignored); every build
  regenerates them.
- `Builds/Android/gradlew.bat` — CRLF noise from git, not a content change.
- `Builds/Android/local.properties` — regenerated from the Projucer's stored SDK
  path. It is git-ignored (A3) and `build-android.sh` rewrites it with
  `$ANDROID_HOME` after every resave, so the committed
  `sdk.dir=/Users/jesse/...` can never come back.

## Emulator

```sh
$ANDROID_HOME/emulator/emulator -avd crosspoint-33 &
adb wait-for-device
adb shell getprop sys.boot_completed      # 1 when up
```

Install and launch:

```sh
scripts/build-android.sh --install
adb shell am start -n io.lagreca.app.crosspoint/.SonoBusActivity
```

Screenshots for design review come from the emulator, never a desktop capture
([design/android.md](design/android.md) §11):

```sh
adb exec-out screencap -p > shot.png
adb shell settings put system font_scale 1.3   # 130% font review; 1.0 restores
```

### Reaching the phone's API from the Mac

The API is loopback-only, so forward the port (A5):

```sh
adb forward tcp:7070 tcp:7070
curl -s http://127.0.0.1:7070/api/v1/state
```

This is how the `console-ui` smoke test runs against the real phone engine.

## Gotchas

- **`__linux__` is defined on Android.** The NDK's clang defines it, so a
  `#if defined(__linux__)` block compiles on Android too — the PipeWire code
  would have been pulled in and failed to link (no ALSA headers). Those guards
  are now `#if defined(__linux__) && !defined(__ANDROID__)` (A3). JUCE's own
  `JUCE_LINUX`/`JUCE_ANDROID` macros are fine: JUCE checks `JUCE_ANDROID`
  first, so `JUCE_LINUX` stays 0.
- **The APK cannot need the upstream keystore.** Upstream's
  `~/.android/sonouploadnew.jks` is not in the repo. The Projucer points every
  build type at `signingConfigs.juceSigning`, so the `.jucer` adds a
  `buildTypes { debug { signingConfig signingConfigs.debug } }` override (A3).
  Release uses a keystore you supply (A11, below).
- **Identity (A4).** The app is `io.lagreca.app.crosspoint`, label
  "Crosspoint", so it installs next to stock SonoBus (`com.sonosaurus.sonobus`).
  The Java classes live in `io.lagreca.app.crosspoint`. The adaptive icon and
  `ic_stat_crosspoint` are hand-kept under `Builds/Android/app/src/main/res/`
  (`mobile/Builds` is ignored by version control: add new files there with
  `git add -f`). A custom `<intent-filter>` in the `.jucer` manifest replaces
  the Projucer's MAIN/LAUNCHER one, so the `.jucer` declares it too.
- **Android 9+ blocks cleartext HTTP.** The web shell talks to
  `http://127.0.0.1:7070`, which needs `usesCleartextTraffic` for loopback only
  — `res/xml/network_security_config.xml` (A7).
- **`JUCE_WEB_BROWSER="0"`** in the `.jucer` excludes `juce_gui_extra`'s web
  view; A7 turns it on and sets `hardwareAccelerated` (the manifest currently
  says `false`, which a WebView cannot use).

## Release APK

`scripts/build-android.sh --release` builds a signed, optimised, **arm64-v8a
only** APK (debug keeps all four ABIs). It will not run without a keystore, and
the keystore never goes in the repo (it is public).

### 1. Create the real keystore, once

```sh
mkdir -p ~/.crosspoint
keytool -genkeypair -v -keystore ~/.crosspoint/crosspoint-release.jks \
    -alias crosspoint -keyalg RSA -keysize 4096 -validity 36500
```

`keytool` is in the JDK (`/opt/homebrew/opt/openjdk@17/bin/keytool`). Keep the
`.jks` and its passwords somewhere outside this repo and back them up: **if you
lose it you can never update an installed Crosspoint, only reinstall** (Android
refuses an update signed with a different key).

### 2. Tell the build where it is

Either environment variables:

```sh
export CROSSPOINT_KEYSTORE=~/.crosspoint/crosspoint-release.jks
export CROSSPOINT_KEYSTORE_PASSWORD=...
export CROSSPOINT_KEY_ALIAS=crosspoint
export CROSSPOINT_KEY_PASSWORD=...        # optional, defaults to the keystore password
```

or the same values in `~/.gradle/gradle.properties` (outside the repo):

```properties
crosspointKeystore=/Users/you/.crosspoint/crosspoint-release.jks
crosspointKeystorePassword=...
crosspointKeyAlias=crosspoint
crosspointKeyPassword=...
```

With nothing configured the script stops with a message listing these.

### 3. Build and install

```sh
scripts/build-android.sh --release
```

The APK is at
`sonobus/mobile/Builds/Android/app/build/outputs/apk/release_/release/app-release_-release.apk`.
Install it on a phone with USB debugging on:

```sh
adb install -r path/to/app-release_-release.apk
```

`-r` updates in place and keeps the app's data, as long as it is signed with
the same keystore. To **sideload without a computer**, copy the APK to the phone
(a cable, a drive or a file share), open it from the Files app, and allow
"Install unknown apps" for that app when Android asks (Settings, Apps, Special
access, Install unknown apps). Nothing publishes the APK anywhere; distributing
it is up to you.

### Versions

`versionName` is the `version` of the `.jucer` (`<JUCERPROJECT version="...">`)
and `versionCode` is `androidVersionCode` on the `<ANDROIDSTUDIO>` exporter in
`sonobus/mobile/SonoBusMobile.jucer`. To release: raise both (`versionCode` must
go up by at least 1 for every APK that should install over the previous one),
then run `scripts/build-android.sh --release`, which regenerates the project.
Check with `aapt dump badging <apk> | head -1`.

## Build facts (verified 2026-10-12)

- Cold `assembleDebug`: about 2m15s on an Apple-silicon Mac (all four ABIs);
  incremental about 50s. `scripts/build-android.sh` always re-runs the resave
  and Gradle reports `40 executed` each time, so expect ~50s even for a no-op.
- The debug APK is 194 MB (arm64-v8a, armeabi-v7a, x86, x86_64). Restrict
  `abiFilters` in the `.jucer` for faster iteration if needed; not done.
- Debug signing uses Gradle's own debug key; the keystore properties resolve to
  empty strings via `findProperty`, so nothing about the upstream keystore is
  needed. Release signs with your own keystore (see below).
- `brew install --cask temurin@17` needs sudo, so `setup-android.sh` installs
  `openjdk@17` instead.
- Mic permission: grant it from the dialog or `adb shell pm grant
  io.lagreca.app.crosspoint android.permission.RECORD_AUDIO`.
- Drive the engine without the UI: forward 7070 and talk to the WebSocket
  (`{"t":"cmd","id":"c1","cmd":"connection.connect","args":{...}}`), Node 22+
  has a global `WebSocket`.
- JUCE 7.0.8 Android WebView reports subresource HTTP errors as page errors, and
  its `Process::hide()` aborts the app (invalid local ref). Both worked around
  in `ConsoleShell.h` and `SonoStandaloneFilterApp.cpp`; see A3.

## Android Auto (A20)

Crosspoint is declared as an Android Auto **media app**, so it appears in the
car launcher and draws the same player as the phone's lock screen
([design/android.md](design/android.md) §13). No extra Gradle dependency: the
platform `android.service.media.MediaBrowserService` and
`Notification.MediaStyle` are in `android.jar` at minSdk 24.

### Testing on the Desktop Head Unit (DHU)

```sh
scripts/setup-android.sh --auto       # installs extras;google;auto
```

Then, with the phone or emulator attached and **Android Auto installed**:

```sh
# 1. On the device: enable Developer settings in Android Auto
#    (Android Auto → Settings → tap "Version" 10 times), then
#    ⋮ → Developer settings → "Start head unit server".
# 2. Forward the DHU port and run it:
adb forward tcp:5277 tcp:5277
$ANDROID_HOME/extras/google/auto/desktop-head-unit -i touch
# Add -h for headless (no window).
```

**Sideloaded apps need Unknown sources in Auto:** Android Auto → Settings →
Developer settings → **Unknown sources** (the wording varies by version; on
some it is "Add new apps to Android Auto" / a per-app toggle). Without it a
sideloaded Crosspoint never appears in the car launcher, however correct the
manifest is.

**Gotcha:** the `google_apis_playstore` system images ship **Android Auto as a
stub** (`versionName=1.2.531830-stub`) with no Head Unit Server, so the DHU
cannot connect to them and `START_HEAD_UNIT_SERVER` does not resolve. The real
app has to come from the Play Store, which needs an interactive sign-in. If the
DHU is unavailable, the Auto-facing surface can still be verified directly: bind
a `MediaBrowser` to `CrosspointBrowserService` and read the browse tree and
session token, which is what Auto consumes (this is how A20 was checked).

**Client-side note:** Android 11+ package visibility hides another app's
services unless the *client* declares `<queries>` (or is a system app like Auto
itself). A test client that omits it fails with "Unable to start service … not
found", which looks like a server-side bug and is not.

## Where the debug build stands

| Task | State |
|---|---|
| A1 toolchain + Projucer | done |
| A2 design | done (`docs/design/android.md`) |
| A3 debug APK builds the engine | done: builds, installs, launches on the emulator, joins a group on aoo.sonobus.net (via the control API) |
| A4 identity | done: `io.lagreca.app.crosspoint`, "Crosspoint", adaptive icon, `crosspoint://` filter |
| A5 control API on the phone | done: smoke-tested over `adb forward`, settings persist across a relaunch |
| A6 Console UI in the APK | done: fresh install and upgrade both serve the current UI |
| A7 Android web shell | renders on the emulator; open: duplicated top inset/empty band, clipped station list |
| A9 mic permission | not started (UI states + `platform.micPermission`) |
| A14 phone calls + earcons | call state, mic stays off, local earcons; verified on the emulator (wip, design review) |
| A10 background service | foreground service + Connection notification, swipe-away quits; verified on the emulator (wip, design review) |
| A11 release APK | done: `--release`, see "Release APK" |

### Tests

```sh
scripts/test-android-ui.sh     # A6: the bundled UI unpacks + stamp behaviour
cd console-ui && npm test      # 21/21, including the Android mock scenarios
clang++ -std=c++17 -I sonobus/Source tests/android/a10-notification.cpp && ./a.out   # A10 copy tables
clang++ -std=c++17 -I sonobus/Source tests/android/a14-earcon.cpp && ./a.out         # A14 earcon
```
