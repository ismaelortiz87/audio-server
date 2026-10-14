# Known bugs — Crosspoint

Handoff-style register of defects: facts, evidence, how to reproduce, where
the code is, what's already done, and what's left. Tasks and phases live in
[TASKS.md](TASKS.md) and [ROADMAP.md](ROADMAP.md). This file is only for
defects. When a bug is fixed, move its entry to "Closed" with the fixing commit.
Open: **B12** and **B13** (Console UI, reported by maelo 2026-10-11, not scheduled). B15 closed 2026-10-12. **B16** open (reconnect with the same name). **B17**, **B18** and **B19** open — all A22 (the Android Bluetooth route) on real hardware, reported by maelo 2026-10-13; each is intermittent, unreproduced, or narrowed but unfixed. B1–B11 are closed (B4–B11 found on the first VDI deployment, below).

Last updated: 2026-10-14 by the A26/P5.9 session (review T4: B15/B16 added to the
summary table).
Repo state referenced: `task/a22-hardening` (PR #6).
B1, B2 and B3 are **closed**; their entries are kept below for the record.

| ID | Bug | Severity | Found in | Status |
|---|---|---|---|---|
| [B1](#b1--a-vdi-still-receives-and-decodes-streams-from-blocked-peers) | A VDI still receives and decodes the stream of a peer the routing matrix blocks | Medium (no audible leak) | F2.1 `matrix-mixed` | **Closed** — `bd8ede82` |
| [B2](#b2--the-default-global-push-to-talk-hotkey-is-never-registered) | The default global push-to-talk hotkey (⌥Space) is never registered | High for P6.5 (feature dead by default) | P6.5 WIP | **Closed** — `080e6c78` |
| [B3](#b3--steps-frozen-evaluator-self-test-is-intermittent) | `steps-frozen` evaluator self-test is intermittent | Low (test tooling only) | other agent's handoff | **Closed** — `d71a43a8` |
| [B4–B8](#first-vdi-deployment-maelosdebian-2026-10-10) | First real deployment: VDI audio never reached the Mac Console (five causes) | Critical | maelosdebian + Mac app | **Closed** — `f3f2dcef`, `fb91e5b6` |
| [B9](#b9--public-server-peering-is-unreliable-over-the-vpn) | Peers connected but silent after a restart (peer info sent once) | High | maelosdebian | **Closed** — `2303725a` |
| [B11](#b11--choppy-vdi-audio-buffer-smaller-than-the-pipewire-cycle) | Choppy VDI audio: agent buffer smaller than the PipeWire cycle | Critical | maelosdebian | **Closed** — `369ea89c` |
| [B12](#b12--the-web-console-offers-the-containers-audio-devices) | The web Console's device pickers list the container's devices, not the browser's | Medium (misleading; can break audio) | web Console | **Open**, not scheduled |
| [B13](#b13--station-card-controls-overflow-at-narrow-widths) | Station card controls overflow their card at narrow widths ("Will hear you") | Low (layout) | all Console UIs | **Open**, not scheduled |
| [B14](#b14--a-vdi-that-is-also-on-the-vpn-stays-offline-for-the-mac) | A VDI that is also on the Mac's VPN connects but exchanges no data with it | High | cebastian (.71) | **Closed** — `446dd3e2` |
| [B10](#b10--the-mac-console-does-not-reconnect-at-launch) | The Mac Console does not reconnect at launch | Medium | Mac app | **Closed** — see entry |
| [B15](#b15--phone-layout-segmented-controls-are-below-the-44-px-touch-size) | Phone layout: segmented controls are below the 44 px touch size | Low (mis-taps on phone) | A2 phone renders | **Closed** — `f94e083325` (entry kept below for the record) |
| [B16](#b16--reconnecting-with-the-same-name-fails-after-a-drop) | Reconnecting with the same name fails after a drop | Medium (blocks a reconnect until the name changes) | A18 emulator + local aooserver | **Open**, not reproduced by anyone else |
| [B17](#b17--an-intermittent-crash-right-after-reopening-the-app-android) | An intermittent crash right after reopening the app (Android) | High (a crash), seen once | A22 route builds | **Open** |
| [B18](#b18--the-headsets-mute-button-stops-working-while-the-route-is-held) | The headset's mute button stops working while the communication route is held | Medium (dead in the state where it is wanted) | A22 on real hardware | **Open** |
| [B19](#b19--noise-when-the-headset-drops-and-comes-back-with-the-route-held) | Noise when the headset drops and comes back while the route is held | Medium (recovers with a manual pick) | A22 on real hardware | **Open** |

Related, tracked elsewhere: **L2** (residual Linux audio gaps and dropouts) is a
task in TASKS.md, not repeated here.

The three fixes landed on `main` in two merges:
- `192e5f1f` — P6.1/P6.5 (WebKit Console shell + global PTT hotkey), carrying **B2**.
- `36f2c5ef` — F2.1 (routing/mute coverage), carrying **B1** and **B3**.

---

## B1 — A VDI still receives and decodes streams from blocked peers

**Status: CLOSED in `bd8ede82`** (fix option 1, the recommended one). Kept here for
the record; see the resolution at the end of this entry.

**Summary.** When a peer that advertises no role (stock SonoBus) is in the
group, the routing matrix correctly marks it blocked on the VDI, and **nothing
from it is audible**. But the VDI keeps **receiving and decoding** that peer's
audio stream. The blocking happens only at the playback gain, not at the
network or decode stage.

**Facts (measured 2026-10-10 on macOS with the real engine).** F2 scenario
`matrix-mixed` (1 Console `c1`, 1 VDI `v1`, 1 no-advert peer `u1`, run with
`--keep`). The VDI's own dump, for its view of `u1`:

| Field | Value |
|---|---|
| `role` | `unknown` |
| `recvAllow` / `recvActive` / `receivingAudio` | `false` / `false` / `false` |
| `preFaderPeakDb` | **−11.9** (the test tone is being decoded) |
| `postGainPeakDb` | **−100.0** (silent at the output) |
| `packetsReceived` | **10439** (and rising) |

From `u1`'s side, `v1` shows `recvAllow true`. `u1` thinks it is a Console
(no-advert only hides its role from others), so its own matrix allows
console→vdi and it **sends to v1**. The Console `c1` sees `u1` with 0 packets,
because nothing flows in that direction.

**Impact.**
- No audible leak: nothing reaches the VDI's output or virtual mic (post-gain −100 dB).
- Bandwidth and CPU are spent on a stream that is thrown away. A real stock
  SonoBus peer in the group would do the same to every VDI.
- Defence in depth is thin: the only barrier is one gain stage. A future bug in
  the playback gain path would make a stock peer's audio audible on the VDI.
- `preFaderPeakDb` (and, through P4.3, the Console's activity meters for such a
  peer, if it were ever listed) would show activity for a blocked peer.

**Why the test fails "every time".** It isn't flaky. The step
`known-open-unknown-blocked` asserts `v1 -> u1 preFaderPeakDb <= -60`, which is
the stricter rule, and the engine doesn't satisfy it. Failure output:
```
step 1 known-open-unknown-blocked FAIL: not satisfied within 25s
    FAIL v1 -> u1 preFaderPeakDb = -12.2: expected <= -60
```

**Likely root cause (hypothesis, not yet confirmed in code).** Blocking a peer
for receive (`setRemotePeerRecvAllow(false)`/`RecvActive(false)`) stops *our*
invite and sets the playback gain to 0, but the AOO sink still accepts and
decodes data from a remote **source that sends without being invited**. Since
P1.4 only gates our side, a peer that ignores the matrix (stock SonoBus) keeps
streaming. Look at `applyRoleMatrixToPeer` and `setRemotePeerRecvActive` in
`sonobus/Source/SonobusPluginProcessor.cpp`, and at the AOO sink's handling of
uninvited sources (`aoo/lib/src/sink.cpp`; check for an uninvite/decline call
or a per-source "ignore" option).

**Fix options (decision needed).**
1. **Recommended: refuse the stream at the sink** for role-blocked peers
   (uninvite/decline the remote source, or drop its packets before decode), so a
   blocked peer costs nothing and can't become audible by a later gain bug.
   Keep `matrix-mixed` asserting pre-fader silence. Add a counterfactual.
2. Accept the current behaviour and relax the test to `postGainPeakDb <= -60`
   plus `receivingAudio false`. Cheaper, but leaves the thin defence and the
   wasted bandwidth.

**Resolution (option 1, `bd8ede82`, merged to `main` in `36f2c5ef`).** The
hypothesis above was confirmed in code, with the exact mechanism pinned down:

- `aoo/lib/src/sink.cpp` `handle_data_message` (~line 613) auto-added a source
  **and called `request_format()`** for *any* unknown sender, so an uninvited
  peer got a decoder. Decode was never gated on invitation at all.
- `uninvite_source` (~line 75) only queued an OSC `/uninvite` (`sink.cpp` ~1639);
  it never stopped local decode. The app side
  (`setRemotePeerRecvAllow(false)` → `setRemotePeerRecvActive(false)`,
  `SonobusPluginProcessor.cpp` ~5991/5933) only changed the playback gain. So the
  register's "one gain stage is the only barrier" reading was correct.

New **opt-in** AOO sink option `aoo_opt_separate_unknown_sources` (default off, so
upstream behaviour is preserved) plus `aoo_sink_set_separate_unknown_sources`. A
source that was not invited is still *discovered* — its id still reaches
`AOO_SOURCE_ADD_EVENT`, which the app needs in order to invite it when the matrix
later allows it — but its format and data messages are ignored, so no decoder is
ever built for it. `uninvite_source`/`uninvite_all` also stop accepting locally
when the option is on. Enabled per peer sink at `SonobusPluginProcessor.cpp`
(~7173), disabled under `SONOBUS_NO_ROLE_BLOCK`.

**Measured result.** `tests/f2/run.sh --scenario matrix-mixed` passes with the
strict rule retained:
```
step 1 known-open-unknown-blocked PASS after 0.0s (held 5.0s)
    ok   v1 -> c1 preFaderPeakDb = -11.6   (permitted path still has audio)
    ok   c1 -> v1 preFaderPeakDb = -11.9
    ok   v1 -> u1 preFaderPeakDb = -100.0  <-- B1's assertion, was -11.4
```
with `v1 -> u1 packetsReceived = 0`, `receivingAudio = false` (was 10439 and
rising). Re-verified 5/5 on a clean rebuild of the merged tree.

**Counterfactual.** `tests/f2/counterfactual-b1.sh` with scenario
`matrix-mixed-ungated` (env `SONOBUS_NO_SINK_DECLINE=1`, same routing matrix,
gate off, same assertions) fails exactly as B1 did:
`FAIL v1 -> u1 preFaderPeakDb = -11.9: expected <= -60`, `packetsReceived = 10815`.
That is the evidence the sink gate, not the routing decision, is the fix.

**Correction to this entry.** "Fails every time" is true for the *gated* build's
strict assertion, but the pre-fix path is itself racy: which blocked direction
shows the decoded tone depends on message ordering (`c1 -> u1` was measured at
-100 dB in some gate-off runs), so the counterfactual runner retries up to 3×.

**Notes for review.**
- Only the `aoo/` copy was changed — the one `sonobus/CMakeLists.txt:431`
  compiles. `aooserver/deps/aoo/` was deliberately **not** mirrored: it is a
  git-subrepo import, and the server is a pure relay (`server.cpp` has no sink
  usage, nothing in `aooserver/` instantiates `isink`), so the option is
  unreachable there. The two copies are now intentionally divergent. Mirroring
  would need a `git subrepo` flow, not a plain edit.
- Side change needing review: `/invite` is ignored by a source that already has
  us as a sink (`source.cpp` ~1387, "sink already added"), so a re-allowed peer
  only restarts via the format request. `invite_source` now also calls
  `request_format()`. Required for correct unblock→reblock→unblock, but it is a
  behavioural change to a shared AOO path.
- Design tradeoff: the gate is opt-in per sink, and "accepting" is a second piece
  of state alongside `roleBlocked`. Every read site is conditional on the option,
  so an option-off sink is unaffected, but it is two pieces of state where one
  might do; worth weighing against handing the role decision down into AOO.

**Original repro (pre-fix).**
```sh
# on the F2.1 WIP branch (it has the scenario), after scripts/build-desktop.sh:
cd .claude/worktrees/agent-a5cb4145d0dcbd438
tests/f2/run.sh --scenario matrix-mixed --keep   # fails; dumps in the kept run dir
```

---

## B2 — The default global push-to-talk hotkey is never registered

**Status: CLOSED in `080e6c78`** (merged to `main` in `192e5f1f`). Kept here for
the record; see the resolution at the end of this entry.

**Summary.** In the P6.5 WIP build, launching the windowed Console logs
```
Warning: push-to-talk hotkey â¥Space not registered: the hotkey needs at least one modifier (ctrl, alt, shift or cmd)
```
So with default settings **the global hotkey does nothing.**

**Root cause (confirmed by reading the code).** The default binding is correctly
stored as UTF-8 bytes:
```cpp
// sonobus/Source/GlobalHotkey.h:22
static constexpr const char* kDefaultBinding = "\xE2\x8C\xA5" "Space";   // "⌥Space"
```
but it is assigned to a `juce::String` without `String::fromUTF8`:
```cpp
// sonobus/Source/SonoStandaloneFilterApp.cpp:569 (P6.5 WIP branch)
binding = crosspoint::GlobalHotkey::kDefaultBinding;
```
A `juce::String` built from a `const char*` treats the bytes as ASCII/Latin-1,
so "⌥" (U+2325) becomes the three characters `â`, `\x8C`, `¥`. The parser in
`GlobalHotkey.cpp` (around line 52, `c == 0x2325`) therefore never sees an
Option modifier, and line 62 rejects the binding.

**Fix.** `binding = String::fromUTF8 (crosspoint::GlobalHotkey::kDefaultBinding);`.
Audit any other `const char*` → `String` conversion of glyph strings in that
branch. `GlobalHotkey.cpp` lines 72–75 already use `fromUTF8` correctly.

**Resolution (`080e6c78`, merged to `main` in `192e5f1f`).** The fix is exactly as
proposed: `binding = String::fromUTF8 (crosspoint::GlobalHotkey::kDefaultBinding);`
at `sonobus/Source/SonoStandaloneFilterApp.cpp:569`.

**Verified.**
- The root cause was reproduced mechanically by driving the **real parser** with
  both spellings: the buggy `const char*` path yields the codepoints
  `U+00E2 U+008C U+00A5 ...` and `canonicalise` rejects with the exact message
  from this entry ("the hotkey needs at least one modifier…"); `fromUTF8` yields
  `U+2325` and canonicalises to `⌥Space`.
- The startup log no longer warns. Re-run independently on the merged tree:
  ```
  Control API listening on http://127.0.0.1:17099/
  Push-to-talk hotkey: ⌥Space
  ```
- `settings.pttHotkey` reads `"⌥Space"` (codepoint 2325).
- Held/released was exercised through a real `CGEventCreateKeyboardEvent` poster
  (Accessibility was available, so no unit-level fallback was needed): held →
  `mic.pttHeld=true`, `transmitting=true`; released → both false; open mode
  ignores the key. A negative control (rebinding to `ctrl+alt+T` and confirming
  the old binding no longer responds) showed only the *registered* key fires. An
  invalid binding (`"F5"`, no modifier) is rejected by `settings.set`.
- **Audit:** this was the only `const char*` → `juce::String` glyph conversion on
  the branch. `GlobalHotkey.cpp` lines 72–75 were already correct and were left
  untouched; the parser compares decoded codepoints, so it was never the defect.
  The other entry point, `settings.set { pttHotkey }`, arrives as JSON and
  round-trips UTF-8 correctly (`⌥T`, `⇧⌘F5`, `⌃⌥Space` all verified).
- **Regressions:** 4/4 F2 scenarios pass (`matrix-2v2`, `ptt`, `talk-gate`,
  `config-yaml`); API `p41` 20/20 and `p42` 6/6 pass.

**Caveat (not a blocker for this bug).** `p43p44` and `p72` each showed one
failure that was *not* proven pre-existing — the baseline was never measured, so
the argument is code-path only: both run `-q`/`--headless`, and
`startGlobalHotkey()` is gated on `!doHeadless`
(`SonoStandaloneFilterApp.cpp:1492`), so this change cannot execute in them. The
unfiltered `node --test tests/api/*.test.mjs` batch also hangs indefinitely in the
headless-Chrome CDP test on this machine; run those files per-file with
`--test-timeout`.

**Not verified on this branch** (unchanged from the original entry, still open as
P6.1 work rather than defects): window appearance, keyboard focus, geometry
persistence, the busy-port error page, and the `--legacy-ui` flag.

---

## B3 — `steps-frozen` evaluator self-test is intermittent

**Status: CLOSED in `d71a43a8`** (merged to `main` in `36f2c5ef`). Kept here for
the record; see the resolution at the end of this entry.

**Summary.** The `steps-frozen` case in `tests/f2/test-evaluate.sh` sometimes
fails because the expected failure reason is missing from the report.

**Facts.**
- The other agent saw `99 passed, 1 failed` (this case) on `main`, see
  [HANDOFF-vdi-package-web-console.md](HANDOFF-vdi-package-web-console.md) §5.4.
- Claude re-ran it on `main` @ `4783ed89`: **3/3 PASS (100/0)**, also PASS in
  the F2.1 tree. So it's **intermittent**, not consistently failing.

**Root cause (from the handoff, consistent with the intermittency).** In
`tests/f2/evaluate.py`'s step loop, an `increases` assertion captures its
baseline when the hold starts and **clears it on every failed poll**. The check
then oscillates (pass, fail, pass) until it times out, and whether the last
recorded poll is a failing one, and so whether the expected reason reaches the
report, depends on timing.

**Resolution (`d71a43a8`, merged to `main` in `36f2c5ef`).** The root cause above
was confirmed exactly. In `tests/f2/evaluate.py` `run_steps`, a baseline is now
captured only when one is not already present
(`if a.get("increases") and i not in baselines`), and the failed-poll branch no
longer clears `baselines` — it resets only `hold_start`. A stalled counter
therefore fails on every poll after the first, so the poll before the deadline is
always a failing one and the reason is always reported. A counter that genuinely
rises still passes, since the comparison itself is unchanged.

**Why "run it ≥10×" was not sufficient proof.** The pre-fix code passes most of
the time: 10 consecutive runs on untouched `main` gave **10/10 PASS (114/0)** with
the bug present. The bug is a poll-*parity* effect — the frozen case alternates
pass/fail each poll, so the outcome depends on whether the deadline lands on a
passing or failing phase. It was proven three ways instead:

1. A controlled poll interval: pre-fix, 0.10/0.12 report the reason while
   0.11/0.13 **lose** it; post-fix the reason is reported at 0.10–0.17, both
   parities.
2. New regression coverage run against the *unfixed* evaluator (fix reverted,
   tests kept): `passed: 110 failed: 4` —
   `FAIL steps-frozen-0.11/0.13/0.15 report does not mention: expected to increase
   from 10`, plus `FAIL steps-frozen-sweep`. With the fix: `passed: 114 failed: 0`.
3. 12 consecutive full `test-evaluate.sh` runs: 12/12 PASS; independently
   re-confirmed 3/3 on the merged tree.

**Added coverage** in `tests/f2/test-evaluate.sh`: the frozen case now runs at six
intervals (0.10–0.15) so poll parity cannot decide the outcome, plus a sweep case
asserting the reason was reported every time, plus a direct `check_assertion`
check that a genuine rise (40 > 10) still passes while a stall still fails with
`expected to increase from 40` — so keeping the baseline cannot degenerate into an
assertion that can never succeed. `steps_dump` gained an optional packet count and
`run_steps` an optional interval to support this.

---

## Previously paused work (now merged)

Both subagents were **stopped on 2026-10-10 at the user's request**, then
resumed to fix B1/B2/B3. **Both branches are now merged to `main`** (`192e5f1f`
for P6.1/P6.5, `36f2c5ef` for F2.1). The worktrees are kept. The test processes
the P6.1 agent had left running (an aooserver on :17888, a headless VDI on API
:17071, a windowed Console on :17070) were stopped by PID, and no project
process is left running.

### Resumed work: F2.1 (merged)

Routing/mute coverage gaps from the P1.4 review.

- **Branch / commits:** `worktree-agent-a5cb4145d0dcbd438`, `67f6e6e8` (WIP) →
  `bd8ede82` (**B1**) → `d71a43a8` (**B3**) → `6ec25b00` (B1 counterfactual).
  **Merged to `main` in `36f2c5ef`.**
- **Changed:**
  - `tests/f2/scenarios.json` (+470): `matrix-mixed`, `matrix-mixed-ungated`,
    `mute-held`, `mute-cycles`.
  - `sonobus/Source/SonobusPluginProcessor.cpp` (marked `F2.1 TEST ONLY`): env
    `SONOBUS_TEST_START_SEND_MUTE` / `SONOBUS_TEST_START_RECV_MUTE` and
    test-control keys `globalSendMute` / `globalRecvMute`, driving the real
    global mute parameters. These markings are intact after the merge.
  - `aoo/lib/aoo/aoo.h`, `aoo/lib/aoo/aoo.hpp`, `aoo/lib/src/sink.{cpp,hpp}`:
    the B1 sink option.
  - `tests/f2/evaluate.py`, `tests/f2/test-evaluate.sh`: the B3 fix and its
    coverage.
  - `tests/f2/counterfactual-b1.sh`, `matrix-mixed-ungated`: the B1
    counterfactual.
- **Measured state after the merge:** `matrix-mixed` 5/5 PASS on a clean rebuild
  (including B1's strict pre-fader assertion), counterfactual reproduces the
  original numbers, `test-evaluate.sh` 114/0. All 22 other F2 scenarios PASS
  (`mute-held`, `mute-cycles`, `matrix-2v2`, `mesh-stock`, `ptt`, `talk-gate`,
  `config-yaml`, and the rest).
- **Left:**
  1. The two counterfactuals that remove each P1.4 cache protection and show
     `mute-held` / `mute-cycles` fail — **still not done**.
  2. Design review of the B1 sink gate (see the notes in the B1 entry).

### Resumed work: P6.1 + P6.5 (merged)

Mac web-view Console shell and the global push-to-talk hotkey.

- **Branch / commits:** `worktree-agent-a397149f701937ace`, `f0c57a1c` +
  `709670ed` (the CMake `JUCE_WEB_BROWSER` fix) → `080e6c78` (**B2**).
  **Merged to `main` in `192e5f1f`.**
- **Changed (+656/−10 over 12 files):**
  - new `ConsoleShell.h` (the web-view window), `GlobalHotkey.{h,cpp}` and
    `GlobalHotkeyCarbon.{h,cpp}`;
  - `SonoStandaloneFilterApp.cpp`, `SonoStandaloneFilterWindow.h`;
  - small hooks in `ApiCommands` and `EngineState` (`pttHotkey`);
  - CMake bundling `console-ui/`.
- **Measured state:** builds ✅; `console-ui/index.html` bundled ✅; the windowed
  app's API is healthy and the page loads and connects ✅; **B2 fixed and
  verified** ✅ (see the B2 entry for the evidence).
- **Not verified — still open as P6.1 work, not defects:** what the window looks
  like (use `screencapture -l` with the window id of the process you started,
  **never a full-screen capture**), keyboard focus (Space, shortcuts), window size
  and position persistence, the error page when the API port is busy, and the
  `--legacy-ui` flag.
- **Regressions:** F2 `matrix-2v2`/`ptt`/`talk-gate`/`config-yaml` PASS; API
  `p41` 20/20 and `p42` 6/6. `p43p44`/`p72` have headless-Chrome failures not
  proven pre-existing (see the B2 caveat).

---

## First VDI deployment (maelosdebian, 2026-10-10)

Symptom: the Console's mic reached the VDI, but the VDI's audio (music in
Chrome) never reached the Mac. Five independent causes, all fixed and verified
live: the Console then metered the VDI at −4…−17 dB rms and played it on the
MacBook speakers.

| ID | Cause | Evidence | Fix |
|---|---|---|---|
| B4 | WirePlumber 0.5 links a capture stream to a sink's monitor only if the stream has `stream.capture.sink=true`; otherwise it **silently uses the default source** (on the VDIs, the `Crosspoint-Input` virtual mic). The agent sent silence. | `pw-link`: `alsa_capture.crosspoint` ← `Crosspoint-Input`. `arecord -D crosspoint_in` rms 0.017 without the flag, 0.297 with it. The container test passed only because it had no other source. | `f3f2dcef`: `prepare()` sets `PIPEWIRE_ALSA`; the container test gains a default-source decoy and checks against it |
| B5 | The agent page's pickers listed raw ALSA PCMs, the pinned `Crosspoint input: X` in **both** lists. Picking one was taken as a PipeWire node name → endless reopen loop, then `Default Audio Device` (= the virtual mic). | journal 20:55–20:58: `'Crosspoint input: Crosspoint-Output' is not a PipeWire node`; generated `capture_node "Crosspoint input: …"` | `f3f2dcef`: Linux pickers list PipeWire nodes; state reports the YAML node; legacy names map back; device meters and testTone follow |
| B6 | Mac app: SonoBus's global receive mute was saved on and restored. Crosspoint has no button for it, so every peer was refused. | `mastrecvmute = 1.0` in `~/Library/Application Support/Crosspoint/Crosspoint.settings`; a fresh headless Console on the same Mac received −15 dB from the same agent | `fb91e5b6`: global send/receive mutes are not restored |
| B7 | Mac app: on every server connect the hidden legacy editor auto-joined with its own stale connection info and re-saved it on top of recents. It raced the API's join, and its failed join **disconnected** the Console. | aooserver log: `GroupJoin` then `GroupLeave`+`UserLeave` in the same second; a stale public-server entry reappears 20 ms after the API's | `fb91e5b6`: the editor stands down when the API started the connection |
| B8 | `connection.connect` with only a new server kept no group password (lookup was by group **and** host). | saved VPN entry had an empty `groupPassword`; joins rejected | `fb91e5b6`: falls back to the group's saved password |

Also found: `.dockerignore` (P2.10) excluded `tests/`, so `tests/linux/pipewire-pin.sh`
could not build (fixed in `fb91e5b6`). And `.deb` versions now carry the commit time
(`1.7.2+git<YYYYmmddHHMM>.<sha>`), so apt sees newer builds as upgrades (`f3f2dcef`).

## B9 — Public-server peering is unreliable over the VPN

**Symptom.** After an agent or Console restart, peers showed `connected` but role
`unknown`, no peer info, no audio, sometimes for minutes. Stock SonoBus peered
fine on the same network and server.

**Cause (fork).** Our peers stay blocked until the remote's role arrives (P1.3),
but peer info was sent once, as a single UDP packet at the first ping. A lost
packet (typical while the NAT path is still being punched) left the pair
silent. A VPN-only server only hid it; that workaround was reverted (the public
server is the choice; the self-hosted server on a VDI is being removed).

**Fix `2303725a`.** Peer info carries `knowsYou`; each side keeps sending its
info every ping while it lacks the remote's role and replies when the remote
says it lacks ours. Verified: after restarts the Mac and maelosdebian re-peer
at once through `aoo.sonobus.net`.

## B11 — Choppy VDI audio: buffer smaller than the PipeWire cycle

**Symptom.** Cut and frozen VDI audio on the Console; stock SonoBus on the same
VDI and network was clean. **Cause.** The VDI's graph is locked at quantum 1024
(`clock.min-quantum` = `max-quantum` = 1024 at runtime, although
`90-low-latency.conf` says 256); stock SonoBus runs JACK at 1024. The agent ran
256-frame ALSA blocks, so its two-block ring (512) could not hold one cycle and
overran every cycle. **Evidence** (`tests/linux/vdi-gap-test.sh`, 440 Hz,
5 ms windows, 23 s): 256 → 30–36 % silent, 570–631 gaps; 512/1024 → 0.1–0.2 %,
1–6 gaps. A first theory (the L1 capture-backlog drop) was tested and wrong
(`719679fd`, reverted in `32c0d978`). **Fix `369ea89c`.** The agent reads the
graph's max/forced quantum and raises a too-small `audio.buffer` to fit, with
a log line. The container test locks its graph at 1024 and checks 256 → 512.

## B10 — The Mac Console does not reconnect at launch

The app starts with `reconnectlast = 0` (SonoBus's "reconnect to last"), so after
every launch the Console sits at "Not connected" until Connect is pressed. A
Console should rejoin its last server and group by itself. Also, while not
connected the state reported an empty server and group, so Settings showed blank
fields at every launch.

**Fixed:** a Console started without `-c/-g` rejoins the most recent saved
connection through the API (`ApiCommands::connectToMostRecent`), and while
disconnected the state shows the last saved server and group. Verified: after a
plain relaunch the app was `connected` to `10.248.233.7:10998` / `lagreca` with
no input. Note: it rejoins even if you pressed Disconnect before quitting.

## B12 — The web Console offers the container's audio devices

**Reported** by maelo, 2026-10-11. **Owner:** design is Claude (lead session).

**Facts.** Settings → Audio shows Microphone and Output pickers filled from the
engine's `devices` list (`console-ui/src/console/components.js:437-492`,
`devices.setInput` / `devices.setOutput`). In the web Console the engine runs
in the container, so the list is the container's PulseAudio devices
("Playback/recording through the PulseAudio sound server" and so on), not the
browser's. The browser's mic and speakers are chosen by the browser, and the
WebRTC gateway is wired to the engine's `engine_out` / `engine_in` null sinks.
Picking another engine device can therefore **silence** the web Console.

**Options.** (a) Hide the engine device fields whenever the page runs over
WebRTC (`ctx.rtc` available). (b) Replace them with the browser's own devices:
`navigator.mediaDevices.enumerateDevices()` (labels appear once the mic is
allowed), `getUserMedia({ audio: { deviceId } })` for the mic, and
`HTMLMediaElement.setSinkId()` for the output. `setSinkId` works in Chrome and
Edge; Firefox and Safari support is partial, so hide the Output field where it
is missing. (b) is possible and is what the user expects; (a) is the minimum.

## B13 — Station card controls overflow at narrow widths

**Reported** by maelo, 2026-10-11, in **all** Console UIs (Mac shell and web).
**Owner:** design is Claude (lead session).

**Facts.** At some compact window widths the per-station controls stop
fitting the station card and spill out of its frame instead of shrinking or
wrapping. The worst offender is the talk toggle in its longest state,
**"Will hear you"** + the `T` key hint (`components.js:338-345`; styles
`.tog.talk[data-state=…]` in `console-ui/src/styles/console.css:86-90`).
Its four labels differ a lot in length ("Hears you", "Will hear you",
"Doesn't hear", "Paused by solo"), so the row width jumps with the state.

**Left.** Reproduce the widths (step the window down, every talk state). Then
fix the card layout: let the control row wrap, give the talk toggle a fixed or
minimum width that fits its longest label, and drop to icon-only plus the key
hint below a breakpoint (the full label stays in the tooltip). Check the solo
and mute buttons and the level control at the same widths.

## B15 — Phone layout: segmented controls are below the 44 px touch size

**Status: closed** — `f94e083325` (see the summary table and the paragraph at the end
of this entry). The entry stays here for the record, which is why it sits above the
open ones.
**Found** by Claude (design), 2026-10-12, while rendering the Android
prototype (A2). It affects every phone-width Console: the PWA now, and the
Android APK (A7).
**Owner:** any + design review (Claude).

**Facts.** Spec §1.3 and §5: "on phone every tappable target is ≥ 44 × 44 px".
The `.seg` buttons ("Open mic | Push to talk" in the You bar, and "Spread |
All centre" on the placement field) render about 28–32 px tall at phone
width. Styles: `.seg button` in `console-ui/src/styles/base.css` (padding
5px 10px, 12 px text). The phone container query in `console.css` doesn't
override it.

**Closed** (Claude, 2026-10-12, branch `android-apk`). In the phone container
query, `.seg button`, the placement-field buttons (Spread, All centre; they are
`.btn.small`, not a segment) and the You bar's "Talk to all" now have
`min-height: var(--size-touchMin)`. Checked in headless renders at 390 px
(`everyday`, `twocalls`): the You bar wraps "Talk to all" onto its own row,
nothing clips. Desktop is unchanged (container query only).

## B16 — Reconnecting with the same name fails after a drop

**Found** by the A18 agent, 2026-10-13, on the Android emulator with a local
aooserver and headless peers. **Not yet reproduced** by anyone else.
**Owner:** any.

**Facts.** After an emulator or peer drops, the first `connection.connect`
with the **same user name** often ends `failed` / "Not connected to a server",
until a different name is used. The likely cause is the server still holding
the old session for that name until it times out. Unconfirmed.

**Left.** Reproduce it with the F2 harness (drop a peer without leaving the
group, reconnect at once with the same name), check the aooserver's
stale-client timeout, and decide between retry with backoff and "replace a
stale session with the same name". This matters on phones, which drop often
(network switches).

## B14 — A VDI that is also on the VPN stays offline for the Mac

**Symptom (2026-10-11).** In the Mac Console `cebastian` (.71) stayed offline
while `ada` and `alan` were online. Both ends were AOO-`connected`, but .71 had
sent 2344 packets to the Mac and received 0, with role `unknown`.

**Cause.** .71 is the only VDI that is itself a WireGuard peer (`wg0`), so there
are two paths between it and the Mac. Mac → `192.168.0.71` arrives at .71 from
`192.168.0.3` (the VPN server's NAT); .71 → the Mac's VPN address leaves via
`wg0` and arrives from `10.248.233.7` (measured with a UDP echo). Each side first
heard the other on an address the server never announced, and AOO's token
(symmetric-NAT) path made **both** adopt it at once: the Mac latched
`10.248.233.7`, .71 latched `192.168.0.3`, and each ignored what the other sent
(verbose log: `changing public address ... TO 10.248.233.7`, then
`ignoring UDP message /aoo/peer/ping from endpoint 192.168.0.71`). A first
theory (token-less established pings, `b1b11147`) was wrong and is reverted.

**Fix `446dd3e2`.** Only the side with the larger token adopts an unannounced
address at once. The other waits for a ping from a known address, which comes as
soon as the first side sends on its path, and adopts the unannounced one only
after 2 s (a genuine symmetric NAT). Verified: 4/4 probe restarts and the Mac
app peer with `cebastian` (~1830 packets like the others). `tests/api/p42` 6/6,
`p43p44` 9/9. Both ends need the fix: the Mac app has it, and .71 runs it from the
preview build until the `446dd3e2` .deb is installed.

## B17 — An intermittent crash right after reopening the app (Android)

**Found** by maelo, 2026-10-13, on the Pixel A22 with the on-demand Bluetooth
route in use (headset mic mode). Seen **once** in a session; opening the app again
straight away was fine, so it is not persistent. No trace was captured — he tests
on the phone, without adb.
**Owner:** any.

**Facts.** "I got excited [a crash] from the application after reopening … trying
again worked well, so it was nothing persistent." Nothing further: no trace, no
reproducible trigger.

**Nearby, already fixed (`e9fac9c4cf`)** — two reopen crashes that *were*
deterministic and are gone:
- the reopen ran on a 300 ms `Timer::callAfterDelay` holding a raw
  `AudioDeviceManager*`, and a route release is part of the exit path, so on exit
  the timer fired against a manager being destroyed (a crash right after closing
  the app);
- a failed reopen left the engine with no device *and* persisted that emptied
  setup, so the next launch had nothing to open.

This one is neither: one occurrence, and a clean retry.

**Hypotheses (unconfirmed).**
1. A route change landing during startup. The guard from `e9fac9c4cf` covers
   `shutdown()`, not startup, so a `closeAudioDevice()` + `setAudioDeviceSetup()`
   from the route path can still race the app's own initial device open — and the
   Java controller engages the route on its first tick whenever the mic is live
   and the setting is headset.
2. The Bluetooth controller and the media session both starting while the route is
   already engaged.

**Left.** Diagnose from a trace, not from reasoning: on the next occurrence, take
`adb logcat -b crash` together with `adb logcat -s Crosspoint JUCE AndroidRuntime`
while it is still in the buffer. An `A22 route engaged/released: reopened audio …`
line followed by `… FAILED (…) : no audio device open` puts it in the reopen; its
absence puts it elsewhere. If he has no adb, add an in-app handler that persists
the trace where he can read it in the Console UI — noting that a *native* crash
needs the tombstone, which a Java handler will not catch.
**Severity:** High (a crash). **Frequency:** rare. Not scheduled.

## B18 — The headset's mute button stops working while the route is held

**Found** by maelo, 2026-10-13, on the Pixel A22 with buds, in headset mic mode.
This is A22's "follow-up 1" (the button was dead in headset mic mode), now narrowed
to an exact condition.
**Owner:** any.

**Facts** (his conditions, verbatim in substance):
- The button works when the headset route is not in use at all (headset mic mode
  off, plain phone mic).
- In headset mic mode with the **mic muted** — i.e. the route has been released by
  the on-demand policy — the button **works**: that press unmutes.
- In headset mic mode with the **mic unmuted** — i.e. the route is held — the button
  does nothing, so it cannot mute.

**Reading.** The asymmetry is the diagnosis: the button reaches Crosspoint's
`MediaSession` as an ordinary media key only when the app is *not* on the call
route. While Crosspoint holds `setCommunicationDevice()` +
`MODE_IN_COMMUNICATION`, Android treats the HFP button as call control and does not
dispatch it to the media session. The route is held exactly while the mic transmits
(or on solo) — precisely when someone reaches for that button.

**Already in the build.** `CrosspointMediaSession` logs every media-button event
(`key`/`action`/`active`) under tag `Crosspoint`. That log *is* the test for the
reading above: with the mic unmuted there should be **no** line for the press (the
event never arrives); with the mic muted there should be one.

**Options (none chosen).**
1. Drop `MODE_IN_COMMUNICATION`, holding only `setCommunicationDevice()` + the SCO
   link. The mode was part of what made capture work in the first place, so this
   needs a build to test whether the buds mic still routes (the engine's input is a
   communication capture stream, which is what the device routing keys off).
   Cheapest path: put the mode behind a debug flag and A/B it on the phone.
2. Take the button through a self-managed Telecom `ConnectionService` — how a VoIP
   app receives HFP buttons. Correct, but invasive for a music app: a phone account
   and a call notification.
3. Document the button as unavailable while the route is held, with the on-screen
   control as the mute path. (For the user: "release on mute = off" keeps the route
   held, so the *unmute* direction is instant.)

**Left.** Choose (1) or (3) with one test build; (2) only if the button matters more
than the notification it brings. Not scheduled.

## B19 — Noise when the headset drops and comes back with the route held

**Found** by maelo, 2026-10-13, on the Pixel A22 with buds. Occasional (seen once or
twice in a session), not reproducible on demand; recovery takes a manual pick in the
Microphone dropdown.
**Owner:** any.

**Facts.** Disconnecting the headset and connecting it again sometimes produces the
same freeze/stuck noise the earlier route bugs made, with audio sitting on the wrong
device until he picks one by hand. Everything else about the route is confirmed on
hardware: engages on unmute/solo, releases ~3 s after a mute (the hysteresis), stereo
back on release.

**Hypothesis.** The headset vanishing *while the route is held* runs two transitions
at once — our release path reopening the app's audio device, and Android moving the
route back by itself. `fixName` in the reopen covers a device *name* that disappeared
from the list; what it does not cover is a device-change event landing *inside* that
reopen (a second transition against a device list that is mid-change), which is the
shape of an intermittent wrong-device outcome.

**Left.** Capture the lines around a drop before changing anything: the `Crosspoint`
route lines (engage/release, `platform clear=`, `mediaOut=`) plus the `JUCE`
`A22 route …: reopened audio in='…' out='…'` line, across a drop → reconnect. If the
two overlap, give a device disappearance its own path (settle the release and reopen
once, then re-engage) instead of letting the generic poll-driven route change drive
it. Not scheduled.

---

## Closed

| ID | Bug | Fixing commit | Merged |
|---|---|---|---|
| B1 | A VDI still receives and decodes streams from blocked peers | `bd8ede82` | `36f2c5ef` |
| B2 | The default global push-to-talk hotkey (⌥Space) is never registered | `080e6c78` | `192e5f1f` |
| B3 | `steps-frozen` evaluator self-test is intermittent | `d71a43a8` | `36f2c5ef` |
| B4, B5 | Agent captured the default source, not the pinned monitor; ALSA names in the Linux pickers | `f3f2dcef` | local `main` |
| B6, B7, B8 | Mac Console: restored global receive mute; legacy editor raced API connects; lost password on server change | `fb91e5b6` | local `main` |
| B9 | Peer info sent once: connected but silent after restarts | `2303725a` | local `main` |
| B10 | Console did not reconnect at launch | `4916472b` | local `main` |
| B11 | Agent buffer smaller than the PipeWire cycle: choppy audio | `369ea89c` | local `main` |
| B14 | Two paths between a VDI and the Mac: both adopted different addresses | `446dd3e2` | local `main` |

Earlier defects found and fixed during the build-out are recorded in the
TASKS.md Results. Examples: the double-applied peer gain (P1.5), the
digital-silence detection bug (P2.11 merge), the API placeholder-state crash in
the UIs (P2.6), and the JUCE ALSA capture backlog (L1).

**Known follow-ups from the B1/B2/B3 work** (not defects in these fixes):
- The two remaining F2.1 counterfactuals (P1.4 cache protections) are not written.
- A `stash@{0}` on the F2.1 branch holds an **unverified** variant that makes
  `uninvite_source`'s local stop conditional on the sink option. It reproduced the
  counterfactual more reliably (1/4 → 3/4) but was never regression-tested and is
  deliberately not merged.
- `p43p44`/`p72` API failures need a measured pre-fix baseline to classify.
