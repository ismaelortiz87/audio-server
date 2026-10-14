# Review: PR #6 (A26 + P5.9) and the merged A20 fix

**Reviewer:** Claude, 2026-10-14, for maelo. **Branch:** `task/a22-hardening` at
`e862075e21` (2 commits on `main`: `c173fe050a` A26, `e862075e21` P5.9). Also covers
`3793d68900` (A20 fixes, already merged to `main` with PR #5).

**For the next agent:** the work is in "To do", in order. Every item says where, what's wrong,
how to fix it and how to check it. Keep working on `task/a22-hardening` (PR #6), and don't
merge it. maelo merges.

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
