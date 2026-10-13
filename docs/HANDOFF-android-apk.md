# Handoff — native Android APK (P9.2, tasks A1–A14)

Written 2026-10-12 by Claude (lead/design session), because it was about to hit
its usage limit. Read this first, then docs/TASKS.md section "P9.2", then
docs/ANDROID.md (how to build) and docs/design/android.md (the design spec).

## Where things are

- **Branch:** `android-apk`, local only, **never pushed**. It forks from
  `main` @ `ad85d338`. Last merge: `615686eb` (A5–A7) plus this doc. Merge to
  `main` when maelo says so.
- **Decision D9** (ROADMAP): native Android APK as soon as possible. The phone
  runs the engine itself, and the Console UI shows in a WebView on the local
  API at `127.0.0.1:7070`.
- **The merged branch builds.** `scripts/build-android.sh` produced a debug APK
  (195 MB, 4 ABIs) at `615686eb`. Release is arm64-only, about 32 MB, and needs
  a keystore (A11).

| Task | State |
|---|---|
| A1 toolchain, A3 first APK, A4 identity (`io.lagreca.app.crosspoint`), A5 API on phone, A6 bundled UI, A8 audio path (VoiceCommunication AEC preset via small JUCE patch), A11 release signing | done, merged |
| A2 design addendum | done (Claude) |
| A7 web shell | merged, **design approved** with follow-ups (in the A9 agent's list) |
| A10 background service + notification, A14 phone calls + earcons | merged. Lock-screen notification approved. In-app call/settings screens not reviewed yet (were clipped by the old A7 layout bug) |
| **A9 mic permission + review fixes** | **a Sonnet subagent was running** in worktree `.claude/worktrees/agent-a6d6ce498dac75612` (branch `worktree-agent-a6d6ce498dac75612`) when this was written |
| A12 headset button | not started, optional. Skipped to save tokens |
| A13 on-phone acceptance | maelo (human) |
| B15 phone touch sizes | fixed, closed |

## The A9 agent's job

If it has finished, its branch has commits ahead of `android-apk`. Check with
`git log android-apk..worktree-agent-a6d6ce498dac75612`. If it died part-way,
look at the worktree's `git status` and continue from there.

1. **A9:** ask for the mic on the first connect (not at launch), then
   POST_NOTIFICATIONS. Add `platform.micPermission` and `platform.requestMic`,
   the "Mic blocked" states, and the Settings row (android.md §3, §7).
2. **Notification channel:** DEFAULT importance, no sound or vibration,
   alert only once, so it shows on the lock screen (spec updated in `23650c2d`).
3. **One known station:** "{A} hears you" / "Live: {A} hears you" in the live
   band and the notification.
4. **`echoRisk`:** must also require the voice to reach at least one station.
5. **Native error page:** the title must render bold, and the body needs
   ~1.45 line spacing (Android branch of `ConsoleShell.h`).
6. **Default "Your name":** the Android device name, not `u0_aNNN`.
7. **(If small)** an explicit Disconnect shouldn't report "Connection to the
   server was lost".
8. **Review captures** go to `/private/tmp/claude-501/a9-review/`.

## How to finish

1. **Merge the A9 branch** into `android-apk`.
   - **Conflict rule:** in the `.jucer`, keep both sides' settings, then run
     `scripts/build-android.sh --resave`. Never hand-merge files under
     `Builds/`; for generated files, take either side and resave.
   - **Force-add needed:** `sonobus/.gitignore` ignores `mobile/Builds`, so
     use `git add -f` for files there.
   - **Discard the resave noise:**
     `git checkout -- sonobus/mobile/Builds/iOS sonobus/mobile/Builds/Android/gradlew.bat sonobus/mobile/JuceLibraryCode/JucePluginDefines.h`
2. **Design review** (Claude only; design is never delegated): the A9 captures,
   plus the in-app call and settings problem-row screens from A10/A14. Approve
   or list fixes, and set the A7/A9/A10/A14 rows to `done` in TASKS.md.
3. **Run the checks:**
   - `cd console-ui && npm test` (22/22 at `615686eb`)
   - `scripts/test-android-ui.sh`
   - `scripts/build-desktop.sh --app-only`
   - `scripts/build-android.sh`
4. **Hand maelo A13:**
   - create a keystore (keytool command in docs/ANDROID.md, "Release APK";
     keep it outside the repo and back it up)
   - export the `CROSSPOINT_KEYSTORE*` variables
   - run `scripts/build-android.sh --release`, then `adb install` or sideload
   - the test checklist is in the A13 card and in the A8 Result (audio items)
   - the phone must be on the VPN
5. **Commit and merge:** commit with `Co-Authored-By` trailers, and merge
   `android-apk` → `main` only when maelo asks. Never push unasked; the repo is
   public (no keystores or passwords).

## Practical notes

- **Projucer:** `build/projucer/extras/Projucer/Projucer_artefacts/Release/Projucer.app/Contents/MacOS/Projucer`
  in the main checkout. Pass it as `PROJUCER_BIN` from worktrees.
- **SDK:** `~/Library/Android/sdk`. The JDK is the brew `openjdk@17`.
  `scripts/setup-android.sh --check` should be green.
- **Emulators:** AVDs `crosspoint-33`, `-b`, `-e`, `-s`, `-p`, `-m` (one per
  agent). Use `adb -s emulator-55xx`. If the host struggles, use
  `-cores 2 -gpu swiftshader_indirect`.
- **Disk is tight** (about 17 GB free).
  - An Android build uses about 3–4 GB: delete `Builds/Android/app/build` and
    `.cxx` afterwards.
  - Remove finished agent worktrees: `git worktree remove --force --force <path>`
    and `git branch -d <branch>`.
  - The locked worktree `agent-ac31250e362db7511` (A8, merged) can be removed.
- **WebView debugging:** create `<app data>/webdebug`
  (`adb shell run-as io.lagreca.app.crosspoint touch webdebug`), then forward
  the devtools socket.
- **Known limits:**
  - The earcon "never sent to stations" claim rests on code inspection plus a
    unit test.
  - AEC quality and latency cost are unmeasured; A13 checks them.
  - VoIP-call detection was not tested; only cellular emulation was.
  - The `crosspoint://` intent filter has no handler.
  - The `bundleIdentifier` change also changed the iOS bundle id (P9.3).
- **Memory rules:**
  - never capture the whole screen (adb screencap only)
  - use the public aoo server `aoo.sonobus.net:10998`
  - design work stays in the lead session
