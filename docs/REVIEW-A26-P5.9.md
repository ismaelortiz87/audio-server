# Review: PR #6 (A26 + P5.9) and the merged A20 fix

**Reviewer:** Claude, 2026-10-14, for maelo. **Branch:** `task/a22-hardening` at
`e862075e21` (2 commits on `main`: `c173fe050a` A26, `e862075e21` P5.9). Also covers
`3793d68900` (A20 fixes, already merged to `main` with PR #5).

**For the next agent:** the work is in "To do", in order. Every item says where, what's wrong,
how to fix it and how to check it. Keep working on `task/a22-hardening` (PR #6), and don't
merge it. maelo merges.

**Status (Claude, 2026-10-14): all six items are done** in `a653c8b742` on the same branch,
which is still **not merged**. The full account, with the evidence and one correction to
T4, is in "Resolution" at the end of this file.

## Verdict

| Item | Verdict |
|---|---|
| A20 fixes (`3793d68900`, merged) | Correct. Car strings and the error state are not verified on a car or DHU |
| A26 (`c173fe050a`) | Fixes both earlier review points, but adds **two new defects** (T1, T2) |
| P5.9 (`e862075e21`) | Code correct. Integration test not reproduced by the reviewer (T5) |
| Tracker | Mostly accurate. **Two errors** (T3, T4), one missing note (T6) |

## To do

### T1 — LE Audio release can clobber another app's VoIP call (A26, must fix)

**Where:** `sonobus/Source/android/java/io/lagreca/app/crosspoint/BluetoothRoute.java`:
`engage()` LE branch (line ~311–316) and `releaseRoute()` (line ~392–415).

**Defect:** on the LE path `engage()` sets `sHeld = true` without calling `setMode()` or
`setCommunicationDevice()`. `releaseRoute()` is gated only on `sHeld`, so it still runs
`clearCommunicationDevice()` and `setMode(MODE_NORMAL)`, which undoes platform state the app
never set.

**Scenario:** LE buds, setting "Headset mic", mic live, then a WhatsApp/Meet call starts
(`MODE_IN_COMMUNICATION`, not ours, so A14 detects it and turns the mic off). Three seconds
later the route is released (`RELEASE_HOLD_MS`), and `releaseRoute()` resets the mode to
`NORMAL` underneath the other app's live call.

**Fix:** track what the app actually changed, e.g. `private static boolean sOwnsMode` set to
true right after the app's own `am.setMode(MODE_IN_COMMUNICATION)` calls (both the API 31+
branch and `classicSco()`). In `releaseRoute()`, only call `clearCommunicationDevice()`,
`stopBluetoothSco()`, `setBluetoothScoOn(false)` and `setMode(MODE_NORMAL)` when `sOwnsMode` is
true, then clear it. The LE hold should then release by just clearing `sHeld`. Reset
`sOwnsMode` in `stop()`.

**Check:** the debug APK builds. Read the code path for LE engage → release and confirm that no
`AudioManager` setter is reached. The hardware check (LE buds plus a VoIP call) goes on A26's
list for maelo.

### T2 — a failed classic engage leaves the mode stuck (A26, must fix)

**Where:** same file, `classicSco()` catch block (line ~386–388). The same issue applies to
the API 31+ branch when `setCommunicationDevice()` is refused or throws and `classicSco()`
then also fails.

**Defect:** `setMode(MODE_IN_COMMUNICATION)` and `PlatformMonitor.appOwnsAudioMode = true`
happen first. If `startBluetoothSco()` / `setBluetoothScoOn()` throws, `sHeld` stays false.
On the next tick, `inRealCall` sees `MODE_IN_COMMUNICATION && !sHeld` and treats it as
*another app's* VoIP call. As a result:
- the route is never retried and never released (`releaseRoute()` now requires `sHeld`), so
  `stop()` won't clean it up either;
- the phone stays in voice mode until the process dies;
- `appOwnsAudioMode` stays true, so A14 (`PlatformMonitor`, line ~72) stops detecting real
  VoIP calls for the rest of the session.

**Fix:** on any engage failure after the app's own `setMode()`, undo it: `setMode(MODE_NORMAL)`
(only if the mode is still `MODE_IN_COMMUNICATION`), `appOwnsAudioMode = false`, and
`sOwnsMode = false` (from T1). Put this in one helper that both failure paths call. Keep the
`Log.w`.

**Check:** the APK builds. Read the code: every path out of `engage()` ends with either
`sHeld == true`, or the mode and the flag restored.

### T3 — P5.9 is marked `done` before merge and design review (tracker)

**Where:** `docs/TASKS.md`, the P5.9 index row (line ~122) and the P5.9 card.

**Defect:** the status is `done`, but PR #6 is unmerged and the row's gate is "any + design
review", with no review recorded. P5.9 adds a new talk-button state ("Muted: doesn't hear
you", `console-ui/src/console/components.js` `stationCard`, plus the new row in
`docs/design/spec.md` §3.5) that needs that review.

**Fix:** set the status to `wip, PR #6, awaiting design review`. Keep the Result text.

### T4 — B15 and B16 missing from the BUGS.md summary table (tracker)

**Where:** `docs/BUGS.md`, the table at the top (lines ~13–30). The entries exist at
`## B15` (~473) and `## B16` (~494).

**Fix:** add two rows in the table's format. Severity and status come from each entry: both
are open. B15 is phone layout, segmented controls under 44 px. B16 is reconnecting with the
same name failing after a drop; it was found by the A18 agent and hasn't been reproduced.

### T5 — confirm `tests/api/p59.test.mjs` on a machine with audio (verification)

The reviewer couldn't run it: the review sandbox has no PulseAudio/PipeWire, so both engines
time out at "waiting for the send gate to open". That is setup, not an assertion. The commit
claims 3/3 on Linux.

**Run (on maelo's box, with a sound server up):**
```
SONOBUS_APP=build/desktop-release/SonoBus_artefacts/Release/Standalone/sonobus \
AOOSERVER=build/aooserver-release/aooserver \
node --test tests/api/p59.test.mjs
```
Rebuild the desktop app first if the binary predates `e862075e21`. If the default device
doesn't work, set `P59_IN_DEVICE` / `P59_OUT_DEVICE`. Record the result on the P5.9 card.

### T6 — A20 row: say the car strings are unverified (tracker, small)

**Where:** `docs/TASKS.md`, A20 row (line ~163) and its card.

**Add:** the `3793d68900` strings and the fresh-install `STATE_ERROR` message were checked
by reading only (no DHU, no bound MediaBrowser). Check them at the next car test (A26's car
check).

## Reviewed and fine (no action)

- **A20 `3793d68900`:** "Soloed · Dimmed" is fixed at the call site and inside
  `stationStateFor` (`dimmed && !solo`). The health word is always added while online, and
  §12.4 filters "Hears you", so "Clear · 32 ms" is back. `connecting` is read with
  `optBoolean`. The fresh-install message moved to the session's `STATE_ERROR`, and it only
  applies when disconnected with nothing saved, so the phone lock screen is unaffected (the
  session is inactive then).
- **A26:** `releaseRoute()` gated on `sHeld`. `MODE_RINGTONE` and a foreign
  `MODE_IN_COMMUNICATION` count as a real call. `appOwnsAudioMode` is set only where the app
  calls `setMode()`. The known limit is correctly documented in the PR: a VoIP call that
  starts *while* the app holds the route can't be told apart from it.
- **P5.9:**
  - The engine send gate (`processBlock`, `&& !remote->playMuted`) and `computeHearsYou()`
    agree, and the check sits after the vdi-role early return, which is correct. Mute wins
    over solo, and talk is untouched.
  - The mock, `control-api.md` and `spec.md` match.
  - console-ui tests pass: 34/34, reviewer re-ran them.

## After this PR (context, not part of the fixes)

- **A26 hardware work (maelo):**
  - B17: capture `adb logcat -b crash` on the next crash.
  - B18: the media-button log with the mic live.
  - B19: disconnect the buds while the route is held.
  - The car check with Headset mic, including the A20 strings (T6).

  That closes A22 and A26.
- **A9:** recapture the Settings problem rows after PR #6 merges. Then P9.2 can close.
- **Next Console work:** P5.8, then P3.x and L2. P2.8 and P7.4 are blocked on maelo.

---

## Resolution (Claude, 2026-10-14) — `a653c8b742` on `task/a22-hardening`

All six items are done on the same branch/PR (#6), which stays **unmerged**: maelo merges.

| Item | Status | Where |
|---|---|---|
| T1 | **Fixed** | `BluetoothRoute.java`: `sOwnsMode` set only where the app itself calls `setMode()`, `releaseRoute()` gates its teardown on it, LE release drops just the hold, reset in `stop()` |
| T2 | **Fixed** | same file: `classicSco()` returns success and `undoFailedEngage()` restores the mode + flags on both failure paths, `Log.w` kept |
| T3 | **Fixed** | `docs/TASKS.md`: P5.9 → `wip, PR #6, awaiting design review`, Result text kept |
| T4 | **Fixed, with one correction** | `docs/BUGS.md`: B15 and B16 rows added; **B15 is closed, not open** — detail below |
| T5 | **Confirmed** | `tests/api/p59.test.mjs` **3/3** on a box with a sound server — command and output below |
| T6 | **Fixed** | A20 row + card: the `3793d68900` strings and the fresh-install `STATE_ERROR` message are read-verified only, both to be checked at A26's car test |

### T1/T2 as implemented

`BluetoothRoute.java` gained `sOwnsMode` ("did *we* change the audio mode and select the
communication device?"). It is set **only** right after the app's own
`am.setMode(MODE_IN_COMMUNICATION)` — in the API 31+ branch and in `classicSco()` — and
nowhere else. Consequences, which are the two fixes:

- `releaseRoute()` runs `clearCommunicationDevice()`, `stopBluetoothSco()`,
  `setBluetoothScoOn(false)` and `setMode(MODE_NORMAL)` **only when `sOwnsMode` is true**,
  then clears it. The **LE** path never sets it (it does not call `setMode` or
  `setCommunicationDevice` at all), so an LE release now drops the hold and touches no
  `AudioManager` setter — nothing can be undone on top of another app's call.
- `classicSco()` returns whether the hold was taken. When it fails, `undoFailedEngage()`
  runs: it restores `MODE_NORMAL` **only if the mode is still ours**
  (`getMode() == MODE_IN_COMMUNICATION`) so a foreign call is never reset, then clears
  `appOwnsAudioMode` and `sOwnsMode` (and `sHeld`). The `Log.w` lines stay. The API 31+
  path falls through to `classicSco()`, so a refusal there plus a SCO failure now ends
  with the mode and the flag restored instead of stuck.

Read of the branches confirms the property the review asked for: every path out of
`engage()` ends either with `sHeld == true` or with the mode and `appOwnsAudioMode`
restored. `stop()` resets `sOwnsMode` alongside `sHeld`, so a stopped service cannot
leave the ownership flag behind.

### T4 correction — B15 is closed

The review lists B15 and B16 as "both open". **B16 is open; B15 is closed.** Evidence:

- `f94e083325` — "fix(console): phone touch targets for segments and small buttons (B15)"
  — is an ancestor of `main` (`git log main -S size-touchMin` lists it).
- The register's header already says "B15 closed 2026-10-12".
- The entry's own last paragraph is `**Closed** (Claude, 2026-10-12, branch android-apk)`,
  naming the fix (`min-height: var(--size-touchMin)` on `.seg button` and the
  placement-field buttons, phone container query only).

What misled the review is placement, not content: B15's entry sits **above** the open
ones because this register keeps closed entries for the record (B1, B2 and B3 do the
same). Nothing in the entry was changed; it now opens with a status line saying it is
closed, with the commit, and why it sits there. Both rows are now in the summary table:
B15 → **Closed — `f94e083325`**, B16 → **Open, not reproduced by anyone else**. The
header metadata (`Last updated`, `Repo state referenced`) was refreshed at the same time.

### T5 evidence

```
SONOBUS_APP=build/desktop-release/SonoBus_artefacts/Release/Standalone/sonobus \
AOOSERVER=build/aooserver-release/aooserver \
node --test tests/api/p59.test.mjs
  ok 1 - P5.9: muting a station closes your mic to it, and leaves its talk flag alone
  ok 2 - P5.9: mute wins over solo
  ok 3 - P5.9: un-muting restores what solo alone gives
  # tests 3   # pass 3   # fail 0   # duration_ms 20138
```

No rebuild was needed: the binary (2026-10-13 23:31:36) is newer than
`SonobusPluginProcessor.cpp` (23:28:17), so it carries P5.9. The run is kept silent and
capture-free with a temporary null sink — `pactl load-module module-null-sink
sink_name=p59_test`, defaults repointed to it and its monitor, both defaults restored and
the module unloaded afterwards — so nothing is recorded and nothing is audible. (In the
review's sandbox there is no PulseAudio/PipeWire at all, which is why both engines time
out in *setup*: the peer only comes online once its aoo streams exist, and those come
from the audio callback. The `P59_IN_DEVICE` / `P59_OUT_DEVICE` overrides cover a box
whose device names differ.)

### T1/T2 build and artifact

- The debug APK builds with both changes: `BUILD SUCCESSFUL in 34s`.
- The compiled class carries them: `sOwnsMode` (9 references), `undoFailedEngage`
  present, `classicSco` returning `boolean`, and `releaseRoute()` reading `sOwnsMode`
  first — checked with `javap -c` on
  `app/build/intermediates/javac/debug_Debug/classes/.../BluetoothRoute.class`.
- These two are hardware-only, so the build is on the share, named for the change:
  **`crosspoint-debug-btroute-le-mode-a653c8b742.apk`** in
  `//192.168.0.50/coldstorage/crosspoint` — 258,566,215 B, md5
  `905e0ad96af612d69a562ad22045dff2`, read back from the share and compared.

### Left for maelo (hardware)

A **VoIP call arriving while an LE headset holds the route** (T1/T2 — the new one);
B17's `adb logcat -b crash`; B18's media-button log with the mic live; B19's buds drop
while the route is held; and the car check with Headset mic, which also covers T6's
strings and closes A22 and A26.
