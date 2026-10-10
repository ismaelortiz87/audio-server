# Known bugs — Crosspoint

Handoff-style register of defects: facts, evidence, how to reproduce, where
the code is, what's already done, and what's left. Tasks and phases live in
[TASKS.md](TASKS.md) and [ROADMAP.md](ROADMAP.md). This file is only for
defects. When a bug is fixed, move its entry to "Closed" with the fixing commit.
**There are currently no open bugs**; B1–B3 are closed below.

Last updated: 2026-10-10 by Claude (lead session).
Repo state referenced: `main` @ `36f2c5ef` (local, ahead of `origin/main`).
B1, B2 and B3 are **closed**; their entries are kept below for the record.

| ID | Bug | Severity | Found in | Status |
|---|---|---|---|---|
| [B1](#b1--a-vdi-still-receives-and-decodes-streams-from-blocked-peers) | A VDI still receives and decodes the stream of a peer the routing matrix blocks | Medium (no audible leak) | F2.1 `matrix-mixed` | **Closed** — `bd8ede82` |
| [B2](#b2--the-default-global-push-to-talk-hotkey-is-never-registered) | The default global push-to-talk hotkey (⌥Space) is never registered | High for P6.5 (feature dead by default) | P6.5 WIP | **Closed** — `080e6c78` |
| [B3](#b3--steps-frozen-evaluator-self-test-is-intermittent) | `steps-frozen` evaluator self-test is intermittent | Low (test tooling only) | other agent's handoff | **Closed** — `d71a43a8` |

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

## Closed

| ID | Bug | Fixing commit | Merged |
|---|---|---|---|
| B1 | A VDI still receives and decodes streams from blocked peers | `bd8ede82` | `36f2c5ef` |
| B2 | The default global push-to-talk hotkey (⌥Space) is never registered | `080e6c78` | `192e5f1f` |
| B3 | `steps-frozen` evaluator self-test is intermittent | `d71a43a8` | `36f2c5ef` |

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
