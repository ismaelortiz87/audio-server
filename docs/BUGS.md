# Known bugs — Crosspoint

Handoff-style register of **open** bugs: facts, evidence, how to reproduce, where
the code is, what's already done, and what's left. Tasks and phases live in
[TASKS.md](TASKS.md) and [ROADMAP.md](ROADMAP.md). This file is only for
defects. When a bug is fixed, move its entry to "Closed" with the fixing commit.

Last updated: 2026-10-10 by Claude (lead session).
Repo state referenced: `main` @ `4783ed89` (local, ahead of `origin/main`).

| ID | Bug | Severity | Found in | Status |
|---|---|---|---|---|
| [B1](#b1--a-vdi-still-receives-and-decodes-streams-from-blocked-peers) | A VDI still receives and decodes the stream of a peer the routing matrix blocks | Medium (no audible leak) | F2.1 `matrix-mixed` | Open, needs a decision |
| [B2](#b2--the-default-global-push-to-talk-hotkey-is-never-registered) | The default global push-to-talk hotkey (⌥Space) is never registered | High for P6.5 (feature dead by default) | P6.5 WIP | Open, fix identified |
| [B3](#b3--steps-frozen-evaluator-self-test-is-intermittent) | `steps-frozen` evaluator self-test is intermittent | Low (test tooling only) | other agent's handoff | Open, cause identified |

Related, tracked elsewhere: **L2** (residual Linux audio gaps and dropouts) is a
task in TASKS.md, not repeated here.

---

## B1 — A VDI still receives and decodes streams from blocked peers

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

**Repro.**
```sh
# on the F2.1 WIP branch (it has the scenario), after scripts/build-desktop.sh:
cd .claude/worktrees/agent-a5cb4145d0dcbd438
tests/f2/run.sh --scenario matrix-mixed --keep   # fails; dumps in the kept run dir
```

**Where the work is.** Branch `worktree-agent-a5cb4145d0dcbd438`, worktree
`.claude/worktrees/agent-a5cb4145d0dcbd438`, WIP commit `67f6e6e8` (on base
`6f3f6dd0`). Not merged. See [Paused work](#paused-work-f21).

---

## B2 — The default global push-to-talk hotkey is never registered

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

**Verify after the fix.**
- the startup log no longer warns;
- `settings.pttHotkey` in the control-API state reads `⌥Space`;
- holding the hotkey in push-to-talk mode sets `mic.pttHeld`/`transmitting` true
  and releasing clears them, while open mode ignores it;
- simulate the key with a small `CGEventCreateKeyboardEvent` helper (posting may
  need Accessibility for the helper; if so, test the handler through a
  unit-level hook).

**Where the work is.** Branch `worktree-agent-a397149f701937ace`, worktree
`.claude/worktrees/agent-a397149f701937ace`. See [Paused work](#paused-work-p61--p65).

---

## B3 — `steps-frozen` evaluator self-test is intermittent

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

**Fix.** Keep the baseline across failed polls (or report the last *failing*
poll), then run `test-evaluate.sh` ≥10× to show it's stable. This was added to
the F2.1 scope before the subagent was stopped; not started.

---

## Paused work

Both subagents were **stopped on 2026-10-10 at the user's request**. Nothing
from them is merged. Their worktrees are kept; resume by rebasing onto `main`.
The test processes the P6.1 agent had left running (an aooserver on :17888, a
headless VDI on API :17071, a windowed Console on :17070, all from its worktree
build) were stopped by PID.

### Paused work: F2.1

Routing/mute coverage gaps from the P1.4 review.

- **Branch / commit:** `worktree-agent-a5cb4145d0dcbd438`, WIP `67f6e6e8`
  (base `6f3f6dd0`, already rebased over the other agent's F2 changes).
- **Changed:**
  - `tests/f2/scenarios.json` (+432): `matrix-mixed`, `mute-held`,
    `mute-cycles`.
  - `sonobus/Source/SonobusPluginProcessor.cpp` (+21, marked `F2.1 TEST ONLY`):
    env `SONOBUS_TEST_START_SEND_MUTE` / `SONOBUS_TEST_START_RECV_MUTE` and
    test-control keys `globalSendMute` / `globalRecvMute`, driving the real
    global mute parameters.
- **Measured state:** `mute-held` 2/2 PASS, `mute-cycles` 2/2 PASS,
  `matrix-mixed` 0/2 (that's **B1**), `test-evaluate.sh` PASS.
- **Left:**
  1. Decide B1, then implement the chosen fix or relax the assertion.
  2. The two counterfactuals (remove each P1.4 cache protection and show
     `mute-held` / `mute-cycles` fail).
  3. Fix B3.
  4. The full F2 suite and a clean commit.

  Estimate: 1–2 h of agent time.

### Paused work: P6.1 + P6.5

Mac web-view Console shell and the global push-to-talk hotkey.

- **Branch / commits:** `worktree-agent-a397149f701937ace`, WIP `f0c57a1c` +
  `709670ed` (the CMake `JUCE_WEB_BROWSER` fix, committed by Claude so it
  isn't lost). Base `6f3f6dd0`.
- **Changed (+656/−10 over 12 files):**
  - new `ConsoleShell.h` (the web-view window), `GlobalHotkey.{h,cpp}` and
    `GlobalHotkeyCarbon.{h,cpp}`;
  - `SonoStandaloneFilterApp.cpp`, `SonoStandaloneFilterWindow.h`;
  - small hooks in `ApiCommands` and `EngineState` (`pttHotkey`);
  - CMake bundling `console-ui/`.
- **Measured state:**
  - builds ✅;
  - `Crosspoint.app/Contents/Resources/console-ui/index.html` is bundled ✅;
  - the windowed app's API is healthy, and WebKit's networking process opens a
    connection to it, so the page loads and connects ✅;
  - **B2** ❌.
- **Not verified:** what the window looks like (use `screencapture -l` with
  the window id of the process you started, **never a full-screen capture**),
  keyboard focus (Space, shortcuts), window size and position persistence, the
  error page when the API port is busy, the `--legacy-ui` flag, and the hotkey
  under simulated key events. Headless and API regression tests
  (`node --test tests/api/*.test.mjs`, F2 `matrix-2v2`/`ptt`/`talk-gate`/
  `config-yaml`) were not run on this branch.
- **Left:** fix B2; the verification above; Claude's design review (spec §8
  and §9). Estimate: 2–3 h.

---

## Closed

None yet. Earlier defects found and fixed during the build-out are recorded in
the TASKS.md Results. Examples: the double-applied peer gain (P1.5), the
digital-silence detection bug (P2.11 merge), the API placeholder-state crash in
the UIs (P2.6), and the JUCE ALSA capture backlog (L1).
