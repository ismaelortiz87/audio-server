# Task list — VDI remote audio

The work queue for [ROADMAP.md](ROADMAP.md), in dependency order. Task IDs match
the roadmap items, plus `F*` foundation tasks that unblock verification.
**This file is the source of truth for task status**; the roadmap tracks phases.

**Live deployment:** the VDI agent, a self-hosted `aooserver` and the web Console
are running on `maelosdebian` (192.168.0.71 / VPN 10.248.233.7). Read
[HANDOFF-vdi-package-web-console.md](HANDOFF-vdi-package-web-console.md) before
touching the Linux packaging, the F2 harness, the web Console container, or that
box — it records what is verified, the one remaining step (P7.4's vhost), and the
gotchas that cost time.

## How to work a task (humans and agents)

1. **Pick** the first task in the [index](#index) whose status is `todo`, whose
   `Depends on` tasks are all `done`, and that is not marked `human` or waiting
   on an open question (`Q*` in the roadmap). Several tasks can be ready at once;
   check the **Touches** line to avoid two people editing the same files.
2. **Claim** it: set Status to `wip` and Owner to your name/agent id, on a branch
   named `task/<ID>-<slug>` (e.g. `task/P1.2-role-peerinfo`).
3. **Do only that task.** If you find more work, add a new task card at the
   right place in dependency order (next free ID in that phase) instead of
   widening scope.
4. **Verify** against the task's *Done when* criteria. Every code task must at
   least build (`F1`) and, from P1 on, pass the routing harness (`F2`).
5. **Close** in the same PR: Status `done`, a one-line *Result* note (what
   changed, anything surprising), commit messages prefixed with the ID:
   `feat(roles): advertise role in peer info (P1.2)`.
6. If blocked, set Status `blocked` and say why in *Result*.

Status: `todo` · `wip` · `done` · `blocked` · `deferred`
Size: S ≤ 0.5 d · M ≤ 2 d · L ≤ 5 d

**Who:**
- `any`: any agent or person can take it.
- `human`: needs physical access (a VDI, a phone, listening by ear).
- `design`: **owned by Claude, the lead/design session. Never delegated to
  helper agents.**
- `any + design review`: anyone can implement it, but it is only `done`
  after Claude has reviewed the result (screenshots or a running build)
  against the UX4 spec and approved it in the PR.

## Design ownership

All user-experience and visual design for this project is done by Claude
(lead session). That means the UX1–UX4 tasks, plus approval of every task
marked `design review`. The new UI is **designed from scratch for the VDI
remote-audio purpose**. It is not a re-skin or a rearrangement of the SonoBus
editor, and no SonoBus layout, control or panel carries over unless the UX
spec says so. The ASCII sketch in the earlier discussion was only a
feasibility illustration; it is not the design.

Rules for implementers:
- Build to the UX4 spec and prototype. Do not invent layout, controls,
  copy, colours or interactions.
- If the spec is missing something or can't be built as drawn, stop and add
  a question to the task's *Result* (status `blocked`). Do not improvise.
- `docs/design/` (Maia Mission Control palette) is an input to UX1. The UX4
  spec supersedes it wherever they differ.

## Index

Ordered so that every task appears after everything it depends on.

| ID | Task | Depends on | Size | Who | Status | Owner |
|---|---|---|---|---|---|---|
| F1 | Reproducible desktop build script (macOS + Linux) | — | S | any | done | Claude |
| UX1 | Design brief: purpose, contexts, principles | — | S | design | done | Claude |
| UX2 | Console experience design + interactive prototype | UX1 | L | design | done | Claude |
| UX3 | VDI agent experience design + prototype | UX1 | M | design | done | Claude |
| UX4 | Design spec + component kit for implementers | UX2, UX3 | M | design | done | Claude |
| P0.1 | Stock SonoBus on a VDI, join group from Mac | — | S | human | done | maelo |
| P0.2 | Identify network path peers use (VPN vs internet) | P0.1 | S | human | done | maelo |
| P0.3 | Wire VDI virtual devices, verify audio both ways | P0.1 | S | human | done | maelo |
| P0.4 | Record P0 findings, decide on P8 timing | P0.2, P0.3 | S | human | done | maelo |
| F2 | Local multi-peer test harness (aooserver + N headless peers) | F1 | M | any | done | Claude (helper) |
| F2.1 | F2 coverage gaps from the P1.4 review: mixed known + unknown, mute interaction | F2, P1.4 | S | any | done | merged 36f2c5ef (B1, B3) |
| L1 | **Linux engine latency: ~10–12 s through engine↔engine in containers** (blocks P7, may affect VDIs) | P7.1 | M | any | done | Claude (subagent J, opus) |
| L2 | Residual gaps/dropouts on Linux (~2 s gap on Pulse client connect; dropouts under load); measure on a quiet host + a real VDI | L1 | S | any + human | todo | |
| P1.1 | `Role` enum + CLI flag `--role` | F1 | S | any | done | Claude (helper) |
| P1.2 | Advertise / parse role in peer-info JSON | P1.1 | S | any | done | Claude (helper) |
| P1.3 | New peers start send+recv blocked | P1.2 | S | any | done | Claude (helper) |
| P1.4 | Apply routing matrix on role arrival | P1.3, F2 | M | any | done | Claude (helper) |
| P1.6 | Role-less peers stay blocked, shown as "unknown" | P1.4 | S | any | done | Claude (helper) |
| P1.5 | Per-VDI talk toggle (Console → VDI gate) | P1.4 | M | any | done | Claude (subagent F, opus) |
| P1.7 | VDI agent health in peer info (D11) | P1.2 | S | any | done | Claude (subagent A, sonnet; finished the previous agent's WIP) |
| P2.2 | Vendor a YAML parser | F1 | S | any | done | Claude (helper) |
| P2.1 | `--config file.yaml` loader | P1.1, P2.2 | M | any | done | Claude (subagent B, sonnet) |
| P2.11 | Linux: pin input/output to PipeWire nodes via named ALSA PCMs (D12) | P2.1 | M | any | done | Claude (subagent H, sonnet) |
| P2.7 | `vdi.example.yaml` + config docs | P2.1 | S | any | done | Claude (subagent B, with P2.1) |
| P2.3 | VDI role locks (mono, no monitor) | P1.4, P2.1 | S | any | done | Claude (subagent L, sonnet) |
| P2.4 | Auto-connect + auto-reconnect with backoff | P2.1 | M | any | done | Claude (subagent L, sonnet) |
| P2.5 | Run as a systemd user unit on Debian 13 (D12) | P2.4 | M | any | done | Claude (subagent M, sonnet) |
| P2.9 | Minimal native tray icon (not a priority, D11) | P2.4, UX4 | S | any + design review | deferred | |
| P2.10 | `.deb` for Debian 13, built in a trixie container (D12) | P2.5 | M | any | done | Claude (subagent M, sonnet) |
| P2.8 | Cut over from Carla hub to mesh; retire hub | P2.10 | S | human | wip | Claude (helper): 1st VDI installed; cutover is human |
| P3.1 | Remove metronome | P1.4 | M | any | todo | |
| P3.2 | Remove soundboard | P1.4 | M | any | todo | |
| P3.3 | Remove chat | P1.4 | S | any | todo | |
| P3.4 | Remove file playback + recording UI (keep recording engine) | P1.4 | M | any | todo | |
| P3.5 | Remove monitor faders / input monitoring UI | P2.3 | S | any | todo | |
| P3.6 | Remove latency match, beat grid, other jam options | P3.1 | M | any | todo | |
| P3.7 | Trim effects to what UX2 specifies | P3.5, UX2 | S | any | todo | |
| P3.8 | Move jitter/codec settings to where UX2 places them | P3.7 | S | any | todo | |
| P3.9 | Own app identity: name, bundle id, settings folder (coexist with stock SonoBus) | — | S | any | done | Claude |
| P3.10 | Wire the Crosspoint icon into the app build | UX4 | S | any | done | Claude |
| P4.5 | Control API schema (doc first), covering everything UX2 needs | P1.4, UX2 | M | any + design review | done | Claude |
| P5.1 | Scaffold `console-ui/` from the UX4 prototype + mock API | UX4, P4.5 | M | any + design review | done | Claude |
| P4.1 | Embedded WebSocket server in the engine | P4.5 | M | any | done | Claude (subagent C, sonnet) |
| P4.2 | State snapshot + change events | P4.1 | M | any | done | Claude (subagent I, sonnet) |
| P4.3 | Meters stream | P4.1 | S | any | done | Claude (subagent K, sonnet) |
| P4.4 | Commands | P4.2, P1.5 | M | any | done | Claude (subagent K, sonnet) |
| P2.6 | VDI agent web UI on localhost (D10), the UX3 prototype as designed | P5.1, P4.2, P4.4, P2.3 | M | any + design review | done | Claude |
| P5.2 | Mixer view (VDI channels) per spec | P5.1 | M | any + design review | done | Claude |
| P5.3 | Talk-back / "you" controls per spec | P5.1 | M | any + design review | done | Claude |
| P5.4 | Connection / onboarding flow per spec | P5.1 | S | any + design review | done | Claude |
| P5.5 | Phone layout per spec | P5.2, P5.3 | S | any + design review | done | Claude |
| P5.6 | Wire UI to the real API | P5.2–P5.5, P4.2–P4.4 | M | any + design review | done | Claude (subagent K proved it; console) |
| P5.7 | Exclusive solo: soloing a station clears the other solos | P5.6 | S | any + design review | done | Claude |
| P5.8 | Network (jitter) buffer control in the Console: per-station auto/manual, re-measure, faster start | P5.6 | M | any + design review | todo | |
| P5.9 | Muting a station also stops sending your mic to it | P1.5, P5.3 | S | any + design review | todo | |
| P6.1 | Mac shell: web view hosting Console UI | P5.6 | M | any + design review | done | merged 192e5f1f; design review Claude 2026-10-10 |
| P6.5 | Mac global push-to-talk hotkey | P6.1 | S | any | done | merged 192e5f1f (B2) |
| P6.6 | Mac window: modern unified title bar (content under the traffic lights) | P6.1 | S | any + design review | todo | |
| P7.1 | Container: headless engine in Console role | P2.1, P1.4 | M | any | done | Claude (subagent G, sonnet); latency blocker → L1 |
| P7.3 | WebRTC (Opus) audio gateway browser ↔ engine | P7.1 | L | any | done | Claude (subagent E, sonnet) |
| P7.2 | Serve Console UI + proxy API from container | P7.1, P5.6 | S | any | done | Claude |
| P7.4 | Publish via maelo's proxy (TLS there), WS + WebRTC UDP (D13) | P7.2 | S | any | wip | Claude (helper): vhost config ready, needs proxy access |
| P7.5 | One container per user: compose + docs | P7.2, P7.3, P7.4 | S | any | done | Claude |
| P7.6 | Remove VNC / Xvfb / raw-PCM bridge | P7.5 | S | any | done | Claude |
| P7.7 | Installable PWA, the phone Console (manifest, icons, service worker) | P7.4, UX4 | S | any + design review | todo | |
| P7.8 | Web Console audio starts on load, no "Start audio" click when the browser allows it | P7.3 | S | any + design review | todo | |
| P8.1 | Dockerfile for `aooserver/` | F1 | S | any | done | Claude (subagent D, sonnet) |
| P8.2 | Deploy aooserver on a VPN-reachable host | P8.1 | S | human | done | |
| P8.3 | Point VDI YAML + Console defaults at it | P8.2, P2.1 | S | any | done | |
| P8.4 | Verify peers use VPN IPs | P8.3 | S | human | done | |
| P9.1 | Mac menu-bar panel | P6.1, P6.5 | M | any + design review | deferred | |
| P9.2 | Native Android Console (APK): broken down into A1–A23 below, lanes in the P9.2 card | P5.6 | — | — | todo | |
| A1 | Android toolchain (JDK, SDK, NDK, emulator) + Projucer built from `juce/` | — | M | any | done | Claude 2026-10-12: toolchain installed and verified, `--check` green, emulator boots, Gradle runs |
| A2 | UX addendum: Android-only states (mic permission, notification, headset PTT) | UX4 | S | design | done | Claude (`docs/design/android.md`) |
| A3 | Debug APK builds the current engine (add missing sources to the `.jucer`) | A1 | L | any | done | Claude 2026-10-12: debug APK builds, installs, launches on the arm64 emulator, joins a group on aoo.sonobus.net (via the control API, stock UI not exercised) |
| A4 | Crosspoint identity on Android (app id, name, icons, Java package) | A3 | S | any | done | Claude (sonnet) |
| A5 | Control API on the phone (loopback, survives pause, connect persists) | A3 | M | any | done | Claude: verified on the emulator through `adb forward` (2026-10-12) |
| A6 | Console UI bundled in the APK (BinaryData zip, extracted on launch) | A3 | S | any | done | Claude: fresh install and `install -r` upgrade verified on the emulator (2026-10-12) |
| A7 | Android web shell: Console UI in a WebView on the local API | A5, A6, A2 | M | any + design review | done | Claude: design approved 2026-10-12 (follow-ups moved to A9) |
| A8 | Phone audio path: echo (VoiceCommunication preset), routing, Bluetooth | A5 | M | any | done | Claude (sonnet); by-ear checks are A13 |
| A9 | Mic permission flow + A7/A10 review fixes | A7, A2 | S | any + design review | wip, awaiting design review | Claude 2026-10-13: code verified present; design review: mic-blocked Console approved (A24 02). **Recapture needed:** Settings problem rows (A24 03 was covered by an emulator System UI ANR) |
| A10 | Background audio: foreground service, notification, Wi-Fi/wake locks | A4, A2 | M | any + design review | done | Claude: lock-screen notification approved 2026-10-12; superseded by the A18 player |
| A11 | Release APK (signing outside the repo) + install docs | A4 | S | any | done | Claude (sonnet) |
| A14 | Phone calls (mic off, stays off after) + earcons | A10, A5 | M | any + design review | done | Claude 2026-10-13: design review passed (code matches android.md §5/§6 on every point; unit tests for the earcon). maelo: marked done. **Known unverified:** the live "call detected" path on a real device — this box's `emu gsm call` doesn't flip `AudioManager.getMode()`, so if it ever misbehaves, switch detection to a `PHONE_STATE` broadcast and recapture with the mic live before the call (A24 04/05 were lost to an ANR) |
| A12 | Mic toggle from the headset button: folded into A18 (play/pause = mic) | A14, A7 | S | any + design review | deferred | see A18 |
| A15 | UX addendum: lock-screen player (media session: Now Bar + expanded player) | A2, A10 | S | design | done | Claude (`docs/design/android.md` §12) |
| A17 | Native control bridge: notification/tile buttons → engine commands, station state → Java | A10 | M | any | done | Claude 2026-10-13: `runLocalCommand` reuses the API paths; every player button verified on API 34 |
| A16 | Service notification never stays gone (re-post on dismiss and on app foreground) | A9 | S | any | done | Claude 2026-10-13: root-caused; media session makes it non-dismissible (FLAG_NO_DISMISS), verified on API 34 |
| A18 | Lock-screen player: media session with Mic (play/pause), station ◀ ▶, Mute, Solo; headset button (absorbs A12) | A15, A16, A17 | M | any + design review | done | Layout B (spike: A impossible on API 33/34) Design approved by Claude 2026-10-13. |
| A24 | Build the APK for maelo once the lock-screen player lands (debug, plus release if a keystore is set) | A18 | S | any | done | Claude 2026-10-13: debug APK `build/apk/crosspoint-debug-275316ef.apk` (195 MB, built from 275316ef); release skipped, no keystore |
| A25 | Lock-screen player artwork v2: full-bleed (fixes Samsung black corners), gradient, station name, state pill | A18 | S | any + design review | done | Claude 2026-10-13: merged (`cc8ddaab`), builds; maelo 2026-10-13: checked on the Samsung, confirmed nice |
| A19 | Quick Settings tile: Crosspoint mic on/off (optional) | A15, A17 | S | any + design review | deferred | android.md §12.6; maelo 2026-10-13: deferred for now |
| A21 | UX addendum: the lock-screen player on Android Auto (browse list, actions, car copy) | A15 | S | design | done | Claude 2026-10-13 (`docs/design/android.md` §13) |
| A22 | Bluetooth mic setting (buds and car): Phone mic / Headset mic (android.md §14) | A8, A14 | M | any + design review + human | wip | Claude 2026-10-13: implemented + build-verified (setting, SCO/setCommunicationDevice route, placement note, `platform.bluetoothRoute`); **design review PASSED with two fixes in the PR** (LE Audio keeps stereo — no route switch; `bluetoothRoute` null unless the route is actually engaged); pending maelo's buds/car check AFTER the row-unreachable fix below |
| A20 | Android Auto media app: MediaBrowserService on the A18 session, station browse list, Auto declaration, DHU test | A18, A21 | S | any + design review | wip, PR #2 | Claude 2026-10-13: build + browse tree + session token verified via a MediaBrowser client; follow-ups (shared station-state, §13.2 Dimmed, §13.3 fresh-install message) in PR #2; car acceptance passed (maelo 2026-10-13, A23) |
| A23 | In-car test via Android Auto (sideload, Unknown sources, controls, car speakers, talking with the phone's mic) | A20 | S | human | done | maelo 2026-10-13: tested in-car, worked well |
| A13 | On-phone acceptance (VPN, screen off, network switch, headsets, lock-screen player) | A7–A11, A14, A9, A18 | S | human | done | maelo 2026-10-13: tested on the phone, passed |
| P9.3 | Native iOS Console | P5.6 | L | any | deferred | |
| P9.5 | Android Auto control panel app (Car App Library, IoT templates) | A20 | L | any | deferred | non-priority, ROADMAP P9.5 |
| P10.1 | Per-station recording (pre-fader, one file per station) | P3.4, P4.4 | M | any | deferred | |
| P10.2 | Session record: station + my mic gated by the talk path | P10.1, P1.5 | M | any | deferred | |
| P10.3 | UX addendum: recording + recaps | UX4 | M | design | done | Claude |
| P10.6 | Storage, retention, privacy (Q7, Q8) | P10.1 | S | any | deferred | |
| P10.4 | Transcription pipeline (external ASR) | P10.2, P10.6, Q7 | L | any | deferred | |
| P10.5 | Meeting recap generation (external model) | P10.4 | M | any | deferred | |

**Parallel tracks:** UX1–UX4 run from day one, alongside F1/F2/P1. Once P1.4
is done: P2 (config/agent) · P3 (removal) · P4 (API). Mobile is the PWA
(P7.7). The native Android APK is P9.2 (A1–A14, five lanes); native iOS stays deferred. P3 tasks all touch `SonobusPluginEditor.cpp`, so run
them one at a time.

---

## Foundation

### F1 — Reproducible desktop build script
- **Depends on:** — · **Size:** S · **Touches:** `scripts/` (new), `README.md`
- **Why:** every later task must prove it builds; agents need one command.
- **Do:** add `scripts/build-desktop.sh` that configures and builds
  `sonobus/` with CMake (Release, out-of-tree dir `build/desktop`) on macOS and
  Linux, using the hoisted `../juce` and `../aoo`. Same for `aooserver`
  (`aooserver/Builds/LinuxMakefile` on Linux; macOS via its Xcode or CMake
  equivalent — document what works). Print the binary paths at the end.
- **Done when:** a clean checkout builds both binaries with one command on
  macOS; README "Building" points to the script.
- **Result:** `scripts/build-desktop.sh` (flags: `--debug`, `--app-only`,
  `--server-only`, `--universal`, `--plugins`); outputs in
  `build/desktop-<cfg>` and `build/aooserver-<cfg>`. New
  `aooserver/CMakeLists.txt` mirrors the Linux Projucer makefile, so the
  server builds on macOS without Xcode. Gotcha: JUCE's `juceaide` sub-build
  ignores SonoBus's deployment target and fails on the macOS 15+ SDK
  (`CGWindowListCreateImage` obsoleted); the script exports
  `MACOSX_DEPLOYMENT_TARGET=11.0` to fix it. Verified on macOS 26 / arm64
  (Command Line Tools only, CMake 4.4, ninja): app 189/189 linked,
  `--version` → 1.7.2; aooserver listens on TCP+UDP.
  **Verified on Linux (2026-10-13, maelosdebian):** from a wiped `build/`,
  `scripts/build-desktop.sh` exits 0 and builds both binaries —
  `build/desktop-release/SonoBus_artefacts/Release/Standalone/sonobus` and
  `build/aooserver-release/aooserver`. Both **execute**: `sonobus --version` →
  `Crosspoint version 1.7.2`, and `aooserver --help` prints its options. So the
  new `aooserver/CMakeLists.txt` is proven on Linux, not just macOS.

### F2 — Local multi-peer test harness
- **Depends on:** F1 · **Size:** M · **Touches:** `scripts/` or `tests/` (new); may add a debug flag in `SonoStandaloneFilterApp.cpp`
- **Why:** agents can't listen. Routing must be checked by a machine.
- **Do:** a script that starts a local `aooserver` and N SonoBus instances
  with `--headless --group test --server localhost:<port>` (and `--role` once
  P1.1 exists), with distinct usernames. Add a debug output (e.g.
  `--dump-peers <file>` written every second, or a log line) listing, per
  remote peer: name, role, send-allowed, recv-allowed, receiving-audio. The
  script waits for the group to settle, then compares that against an
  expected matrix and exits non-zero on mismatch. Must run without real audio
  devices (null/dummy device; check what headless mode does on macOS/Linux).
- **Done when:** with stock behaviour (no roles), 3 peers report full mesh and
  the script passes against a "full mesh" expectation.
- **Result:** `tests/f2/` — `run.sh` (orchestrator: build check, free TCP+UDP
  port, aooserver, N peers, settle, evaluate, teardown), `evaluate.py` (the
  routing-matrix model + defensive dump parsing, usable standalone),
  `scenarios.json` (5 scenarios), `fake-peer.sh` (stub + fault injection),
  `ports.py`, `test-evaluate.sh`, `README.md`. Engine side: new
  `--dump-peers <file>` writes an atomic JSON peer snapshot (name, role,
  hasRole, sendAllow, recvAllow, sendActive, recvActive, receivingAudio) once a
  second. Exit codes: 0 match, 1 mismatch, 2 usage/config error.
  **Verified on macOS against the real binary:** `mesh-stock`,
  `blocked-unknown`, `matrix-1v1`, `matrix-2v2` all pass (exit 0);
  `evaluate.py` passes 67/67 unit assertions; injected faults exit 1; 3/3
  repeat runs stable; teardown leaks no processes (no `pkill`/`killall` — every
  PID is verified via its `lsof` cwd against the run dir). Runs headless with
  no audio device contention: 4 concurrent peers (max planned scale) fine.
  Gotchas worth knowing: (1) on macOS JUCE resolves its settings dir via
  `NSHomeDirectory()`, which **ignores `$HOME`** — peers must be isolated with
  `CFFIXED_USER_HOME`, else every peer loads the user's real settings
  (`reconnectlast=1.0`) and fails with `login failed: access denied`;
  (2) `--dump-peers` had to work around JUCE's `ArgumentList` reading a long
  option's value only in `--opt=value` form; both forms work now.
  Caveat: the real app reports `connected` asymmetrically/unstably (it tracks
  AOO invite timing, not routing), so the harness asserts presence + booleans
  by default; `--require-connected` opts in.
  **Verified on Linux (2026-10-13, maelosdebian): all four scenarios pass** —
  `mesh-stock`, `blocked-unknown`, `matrix-1v1`, `matrix-2v2` (the harness's
  first Linux run). Four portability gaps had to be worked around; they are
  harness bugs, not engine ones, and are worth fixing in the harness:
  (1) the default `--app` path is the macOS bundle — pass the Linux binary;
  (2) lines ~71-72 hardcode `MacBook Pro Microphone/Speakers`, so the peers exit
  `audio.input_device … not found` — on Linux leave both device env vars empty
  and let the app pick its default (naming any device, even
  `Default Audio Device`, gives `could not open the audio device(s): no
  channels` on this box); (3) line ~224 `${!PEER_PIDS[@]+"${!PEER_PIDS[@]}"}`
  is a bash-5 parse error (`invalid variable name`) that also corrupts the
  harness exit code, so a trailing exit 1 can accompany a passing scenario;
  (4) peer isolation uses `CFFIXED_USER_HOME`, which only works on macOS — on
  Linux every peer shares the real `~/.config/crosspoint/Crosspoint.settings`
  and inherits whatever role the previous scenario wrote, which is exactly what
  failed `mesh-stock`/`blocked-unknown` (`selfRole: expected "console", got
  "vdi"`) until the run used a fresh `HOME`. On Linux the harness needs `HOME`
  (or `XDG_CONFIG_HOME`) isolation too.

## UX — Experience design (Claude only)

Location: `docs/design/` (spec, decisions) and `design/prototypes/` (HTML
prototypes). Every UX task ends with my review and approval before dependants
start.

### UX1 — Design brief
- **Depends on:** — · **Size:** S · **Who:** design (Claude)
- **Do:** write `docs/design/brief.md`. It covers: what the product is now (a
  two-way audio link to VDIs, not a jam session), who uses it and where (Mac
  at a desk, phone on the move, browser on any machine), the jobs to be done
  (hear several VDIs, pick out one, talk to one or all, know at a glance
  that each link is healthy), what is explicitly out of scope, and the design
  principles. Review the Maia palette and decide what to keep.
- **Inputs:** mesh topology (ROADMAP D8). Scale is 3 VDIs, at most 4, so
  design for a handful of always-visible channels, not a scrolling list.
- **Done when:** I have approved the brief.
- **Result:** `docs/design/brief.md`. Reviewed by maelo on 2026-10-09: open mic
  to all by default with per-station toggles, solo dims, global PTT hotkey in
  scope (P6.5), menu-bar panel deferred (P9.1). Key use case added: two calls
  at once.

### UX2 — Console experience design + prototype
- **Depends on:** UX1 · **Size:** L · **Who:** design (Claude)
- **Do:** design the Console from scratch: information architecture, mixer
  interaction (volume, pan, mute, solo, talk), talk-back model (per Q3),
  health and latency display, connection/onboarding flow, empty, error,
  degraded and unknown-peer states, keyboard shortcuts / push-to-talk,
  desktop and phone layouts. Deliver a clickable HTML prototype on mock data
  in `design/prototypes/console/`, plus a list of everything the UI needs
  from the engine (input to P4.5).
- **Done when:** I've reviewed and approved the prototype; open design
  questions are resolved.
- **Result:** **done, approved by maelo 2026-10-09.** Final engine-needs list:
  `docs/design/console-needs.md` (input to P4.5). History:
   (in progress) first prototype at
  `design/prototypes/console/index.html`, opened locally. URL params:
  `?scenario=everyday|twocalls|problem|four|connecting&view=phone`. Awaiting
  maelo's feedback; the engine-needs list and VDI-side states are still to do.
  Update (D11): the Console shows VDI-reported issues ("VDI reports: …",
  from P1.7) and remembered stations that are offline ("Offline · last seen
  09:12"). Health moved under the station name so names never truncate with
  4 stations. See `?scenario=problem`.

### UX3 — VDI agent experience design + prototype
- **Depends on:** UX1 · **Size:** M · **Who:** design (Claude)
- **Do:** design the VDI side: mostly unattended, so focus on at-a-glance
  status, tray/menu-bar presence, config errors (bad device, wrong
  password), reconnecting state, and the few actions it allows: reload
  config, mute send, and **pick input and output devices** (brief job 9: live
  level per device, save to YAML, picker offered when a configured device is
  missing). Clickable prototype in `design/prototypes/vdi/`.
- **Done when:** I've reviewed and approved the prototype.
- **Result:** **done, approved by maelo 2026-10-09.** This is the design of the agent's
  localhost web UI (P2.6). Placeholders updated to Debian/PipeWire (D12), the
  frame shows `http://localhost:7071`, and the tray is marked deferred (P2.9).
  Its state and commands are in `docs/design/console-needs.md` §4. History:
   (in progress) prototype at `design/prototypes/vdi/index.html`
  Note (D10): the tray mock is Windows-styled. On Linux the tray is optional
  and the localhost web UI plus notifications are the primary surface; UX4
  must specify both, and specify the agent UI as a browser page.
  (`?scenario=listening|talking|waiting|paused|reconnecting|password|nodevice`):
  agent window plus tray icon/menu with 5 badge states. Rules introduced:
  the send meter is coloured only when audio is really sent and grey when the
  level is local only; inputs are verified by live level, outputs by test tone;
  errors embed their fix (picker, open config). Device names are Windows-style
  placeholders until Q1 is answered. Awaiting maelo's feedback.

### UX4 — Design spec + component kit
- **Depends on:** UX2, UX3 · **Size:** M · **Who:** design (Claude)
- **Do:** turn the approved prototypes into an implementation spec in
  `docs/design/spec.md`: tokens (`tokens.json` updated), components with all
  their states, layout grids and breakpoints, motion, copy, accessibility
  (contrast, focus, touch targets, screen-reader labels), and notes for
  building the VDI UI in JUCE vs the Console in HTML. The prototype's
  HTML/CSS is the starting code for P5.1.
- **Done when:** an implementer can build P2.6 and P5.x without asking design
  questions.
- **Result:** **done 2026-10-09.** `docs/design/spec.md` (the build and
  review reference), `docs/design/tokens.json` v1, `docs/design/console-needs.md`
  (engine needs, for P4.5), and the icon in `design/icon/` (SVG source plus
  PNGs for macOS, Linux and PWA, including a maskable one). Colour
  validation: the draft 4th station colour `#5fa8d3` merged with s1 under
  deuteranopia (CIEDE2000 7.0), so it was replaced by `#bed590` after a
  search constrained against stations, live, status and text colours. Now
  the minimum between stations is 14.7 across normal/protan/deutan/tritan,
  and contrast on `panel` is 5.3–11.4. Remaining station-vs-status-green
  closeness is solved by shape: station = rounded square, status = circle +
  word. Prototypes updated to match (lime s4, square swatches, Crosspoint
  brand, agent send meter in `accentInk`). Maia doc marked as the palette's
  origin.

## P0 — Validation spike (human)

### P0.1 — Stock SonoBus on a VDI, join group from Mac
- **Depends on:** — · **Size:** S · **Who:** human
- **Do:** install upstream SonoBus 1.7.2 on one VDI. Both VDI and Mac join the
  same private group with a password on `aoo.sonobus.net`.
- **Done when:** both see each other in the peer list.
- **Result:** already a daily-use setup: stock SonoBus between Mac and VDI.

### P0.2 — Identify network path
- **Depends on:** P0.1 · **Size:** S · **Who:** human
- **Do:** `lsof` is not enough: AOO uses unconnected UDP sockets, so it only
  shows local listeners (`*:<port>`), never the remote peer. Instead:
  1. Note the VPN interface and range: `ifconfig | grep -B3 'inet 10\.\|inet 172\.\|inet 100\.'`
     (VPN is usually a `utunN` interface on macOS).
  2. While audio is flowing, capture SonoBus's UDP ports (from `lsof`) on all
     interfaces: `sudo tcpdump -ni any -c 40 'udp and (port <p1> or port <p2>)'`.
  3. Read the remote IPs and the interface: VPN range/`utunN` → audio is on
     the VPN; public IP on `en0` → audio goes over the internet.
- **Done when:** path written in *Result*.
- **Result:** skipped by decision. The current daily setup (VDIs → Ubuntu
  Studio/Carla hub → Mac, all on private VPN IPs) works, which is enough.
  Assumption carried forward: every device can reach every other over the
  VPN. If a direct VDI → Console link fails during P1 testing, revisit this
  and P8.

### P0.3 — Wire VDI virtual devices
- **Depends on:** P0.1 · **Size:** S · **Who:** human
- **Do:** VDI SonoBus input = loopback device, output = virtual mic device.
  Play audio on the VDI → hear it on the Mac. Speak on the Mac → appears on
  the VDI virtual mic (check with a recorder app on the VDI).
- **Done when:** both directions confirmed; device names recorded (needed for
  P2.7's example YAML).
- **Result:** done, part of the daily setup. Still to record: VDI device names (for P2.7) and VDI OS (Q1).

### P0.4 — Record findings
- **Depends on:** P0.2, P0.3 · **Size:** S · **Who:** human
- **Do:** add a "P0 findings" note to ROADMAP.md: path, latency shown in
  SonoBus, dropouts, VDI OS (answers Q1). If audio went over the internet or
  failed, move P8 out of `deferred`.
- **Result:** P0 closed. Network is proven by daily use; P8 stays
  deferred. New finding: an existing Carla hub (see ROADMAP "Current setup")
  raised Q5, resolved as D8: mesh, no hub. VDI OS (Q1) and device names are still to record.

## P1 — Roles and routing

Code: `sonobus/Source/SonobusPluginProcessor.{h,cpp}`, CLI in
`sonobus/Source/SonoStandaloneFilterApp.cpp`.

### P1.1 — `Role` enum + CLI flag
- **Depends on:** F1 · **Size:** S
- **Do:** `enum class PeerRole { Unknown, VDI, Console }` on the processor with
  get/set; persisted in the processor state. Add `--role vdi|console` CLI
  option next to `--group` / `--headless`. Default for now: `Console`.
- **Done when:** builds; `--role vdi` is reflected in a debug log at startup.
- **Result:** `enum class PeerRole { Unknown, VDI, Console }` on the processor
  (`SonobusPluginProcessor.h`), with `getRole`/`setRole`, `peerRoleToString` /
  `peerRoleFromString`, and `std::atomic<PeerRole> mRole { Console }` (atomic
  because it is written from the message thread and read from the network
  thread). Persisted in the saved state as `ExtraState/Role` and restored on
  load. New `--role vdi|console` CLI option, case-insensitive, with a clear
  error and exit for a bad or missing value; `--role=vdi` also accepted.
  `setRole()` writes a plain `std::cerr` line as well as `DBG`, because DBG is
  compiled out of release builds — so `--role vdi` really does show at startup
  in the shipped binary. Verified: `SonoBus role: vdi` on stderr; and with the
  role injected into a saved state only (no `--role`), the peer reports
  `selfRole: vdi`, i.e. persistence works both ways. Gotcha: role is applied
  as soon as the processor exists, **before** connect/join, so the first
  peer-info advertisement is already correct.

### P1.2 — Role in peer-info JSON
- **Depends on:** P1.1 · **Size:** S
- **Do:** in `sendRemotePeerInfoUpdate` add `info->setProperty("role", ...)`.
  In `handleRemotePeerInfoUpdate` read it and store on `RemotePeer` (new
  field, default `Unknown`). Make sure info is sent on peer join (check when
  it's currently sent; add a send on connect if needed). Include it in F2's
  dump output.
- **Done when:** F2 dump shows each peer's correct remote role.
- **Result:** `sendRemotePeerInfoUpdate` sets `info->setProperty("role", …)`;
  `handleRemotePeerInfoUpdate` parses it onto `RemotePeer::remoteRole` (+ a
  `hasRemoteRole` flag), default `Unknown`, and an absent key leaves it
  `Unknown` (so stock SonoBus peers stay unknown). Info is sent on peer join
  via `connectRemotePeer` / `connectRemotePeerRaw`, and again on the first ping
  (`haveSentFirstPeerInfo`), so no extra send was needed. Included in the F2
  dump as `role` + `hasRole`. Verified with the harness: a console peer reports
  `{"name":"v2","role":"vdi","hasRole":true}`; a peer started with
  `SONOBUS_NO_ROLE_ADVERT=1` is reported as `{"role":"unknown","hasRole":false}`.
  Note: peer-info and ping go through `sendPeerMessage` → `endpoint_send`,
  which is **not** gated by the allow flags — that is what lets roles be
  exchanged while peers are blocked (P1.3).

### P1.3 — New peers start blocked
- **Depends on:** P1.2 · **Size:** S
- **Do:** when a `RemotePeer` is created, call `setRemotePeerSendAllow(false)`
  and `setRemotePeerRecvAllow(false)` (or set the underlying fields directly)
  until its role is known.
- **Done when:** F2 shows peers blocked before info arrives (temporarily
  disable the role-arrival step to observe it, or log the transitions).
- **Result:** a new `RemotePeer` is created with `sendAllow = false` and
  `recvAllow = false`, and `connectRemotePeer` / `connectRemotePeerRaw` no
  longer open recv or start the source at invite time. **Important addition:**
  a plain flag pair was not enough — two paths silently re-opened a blocked
  peer, so `RemotePeer::roleBlocked` now gates them: (1) the global send/recv
  mute handlers, which on un-mute restore the cached value and go through
  `setRemotePeerSendActive`, and (2) the *automatic* mute→unmute dance that
  `prepareToPlay`/`processBlock` trigger on a sample-rate or block-size change
  (`mNeedsSampleSetup`). Both `setRemotePeerSendActive` and
  `setRemotePeerRecvActive` now refuse to open a `roleBlocked` peer, whatever
  asks. P1.4 is expected to clear `roleBlocked` when it applies the matrix.
  Verified: with 1 console + 1 vdi, both peers stay
  `sendAllow=false recvAllow=false` for a 39 s soak with no flips; and every
  role in the F2 dump is `unknown`/blocked until P1.4 exists, which is the
  intended intermediate state. Two env-var hatches for testing the pre-roles and
  unknown-peer cases against this same build: `SONOBUS_NO_ROLE_BLOCK=1`
  (new peers start open, pre-P1.3 behaviour) and `SONOBUS_NO_ROLE_ADVERT=1`
  (advertise no role at all). Both verified: the first gives a true full mesh
  (all `sendAllow`/`recvAllow` true, 3 peers), the second makes a peer appear
  as `unknown` and blocked.

### P1.4 — Apply routing matrix
- **Depends on:** P1.3, F2 · **Size:** M
- **Do:** on role arrival (and if our own role changes), for each peer:
  opposite role → allow send + recv; same role → block both. Handle a peer
  changing role. Add F2 scenario: 2 Consoles + 2 VDIs, expected matrix per
  ROADMAP.
- **Done when:** F2 passes for the 2+2 scenario, and for 1+1.
- **Result:** `roleMatrixAllows()` (`SonobusPluginProcessor.cpp`) mirrors
  `tests/f2/evaluate.py::allowed()`: unknown on either side blocked, same role
  blocked, console↔vdi allowed both ways. `applyRoleMatrixToPeer()` is called
  when a peer's role is first learned or changes (`handleRemotePeerInfoUpdate`),
  and `applyRoleMatrixToAllPeers()` from `setRole`, so our own role change
  re-decides every peer. Both run under the caller's existing `mCoreLock` read
  lock (the allow/active setters re-take it; JUCE read locks are recursive per
  thread), never under a write lock. `roleBlocked` is cleared **only** for a
  positive allowed decision, so a same-role peer and a still-unknown peer both
  stay gated and no other path (global unmute, solo, peer UI) can open them.
  **Consequence for P1.6:** `roleBlocked` no longer distinguishes "same-role,
  decided" from "unknown, pending" — the UI must read
  `remoteRole`/`hasRemoteRole` for that. Send is opened via
  `setRemotePeerSendActive(i, true)` (the `*Allow` setter alone does not start
  the stream). Recv handles both arrival orders: while
  `remoteSourceId == AOO_ID_NONE` it sets `recvAllow` and lets
  `AOO_SOURCE_ADD_EVENT` invite the source when the id arrives, instead of
  inviting `AOO_ID_NONE`. Three traps fixed that F2 does **not** catch: (1) the
  matrix is applied on a **role change only**, not on every peer-info update,
  because that message is also resent for buffer auto-sizing and pings and
  re-applying there silently overrides a per-peer or global mute; (2) a
  permitted-but-muted peer records the permission in `sendAllowCache`/
  `recvAllowCache` while staying closed, so the unmute handler still restores
  it; (3) a matrix-blocked peer pins both caches to **false**, because at
  creation they are `!mMainSendMute` (normally true) and the unmute handler
  restores them through the Active setter, which force-sets the allow flags —
  leaving them true let one unmute re-open a blocked peer.
  `SONOBUS_NO_ROLE_BLOCK=1` returns early, so `mesh-stock` is unchanged.
  Verified: `matrix-target-check`, `matrix-1v1`, `matrix-2v2`,
  `blocked-unknown` and `mesh-stock` all exit 0 in **3/3 runs each** (15 runs,
  no flake), plus `test-evaluate.sh` (71 passed / 0 failed) and
  `matrix-1v1 --strict-fields`. Both cache claims were proven by counterfactual
  against the real binary: a repeated unmute-only probe re-opened blocked peers
  on the naive build (`c2 -> c1 sendAllow: expected false, got true`, never
  settles) and not on this one; and a mute held from startup re-opened a peer on
  a mute-unaware build. `matrix-1v1`/`matrix-2v2` now `"expect": "matrix"`;
  `test-evaluate.sh` section 3 had to be inverted (it asserted the pre-P1.4
  all-blocked state passed for these scenarios); stale "P1.4 not implemented"
  notes in `scenarios.json` and `tests/f2/README.md` were corrected. **Not
  verified:** no live peer was ever observed *changing* role (there is no
  CLI/API to flip a running peer's role, and F2 has no such scenario), so the
  re-apply-on-change path rests on code inspection plus the `setRole`-at-startup
  path; the mute counterfactuals used a temporary env-gated probe that was
  removed before committing, so F2 does not cover mute interaction.

### L1 — Linux engine latency (~10–12 s) · M · priority
- **Depends on:** P7.1 (reproduction: two engine containers + the gateway,
  `docker/web-console/test/e2e-engine.mjs`)
- **Found in P7.1:** a tone into the VDI container's `engine_in` reaches the
  Console container's output ~10–12 s late, both directions, while PulseAudio
  alone is instant. `PULSE_LATENCY_MSEC=20` sometimes cut it to 2.5–7 s. The
  likely culprits are JUCE's ALSA backend through the pulse/pipewire ALSA
  plugin (period/buffer sizes; capture fragments accumulating), AOO's auto
  jitter buffer growing after underruns, or clock drift with no resampling.
  **The Debian VDIs use a similar path (JUCE ALSA → pipewire-alsa, P2.11),**
  so this may affect the main product too.
- **Do:** measure each stage (engine input → AOO send → AOO recv → engine
  output) with timestamps or impulse probes, find where the delay
  accumulates, fix it (e.g. explicit small ALSA period/buffer for the
  pulse/pipewire PCMs, a fixed small receive buffer in headless roles, a
  drift-correction/flush), and re-measure. Target < 150 ms engine↔engine on
  loopback containers.
- **Done when:** a rerunnable latency test reports the end-to-end delay, and
  it's under target in the container pair and in the P2.11 PipeWire setup.
- **Result:** Root cause: the alsa-plugins `pulse` capture PCM queues up to
  4 MiB (~11 s) but clamps ALSA `avail` to one buffer, so JUCE's
  playback-paced ALSA loop turned every stall (device start, and Pulse latency
  renegotiation ~2 s each time a client connects to a null sink) into
  permanent, growing input latency (6 → 12.5 → 17 s; 11–13 s after 11 h).
  Neither AOO's jitter buffer nor clock drift was the cause. Fix (local JUCE
  patch, recorded in README §5): `juce_ALSA_linux.cpp` `dropCaptureBacklog`
  drops whole blocks while `snd_pcm_delay` > 2 ALSA buffers, checked every 8
  blocks, no allocation, Linux only. New `tests/latency/run.sh [--backend
  pulse|pipewire]`. Subagent numbers: Pulse pair 54–77 ms (was 4–17 s);
  PipeWire 41–98 ms (was 28–51 ms; it never had the backlog). **Re-verified
  in review on merged main** (load avg 11–15 from parallel builds): medians
  58.9/54.8/111.7 ms (vdi→console) and 61.3/55.8/60.1 ms (console→vdi), all
  < 150 ms; one run had a single burst ~2.5 s late, so the test prints FAIL on
  the "complete" criterion. Leftovers → **L2**.

### L2 — Residual gaps and dropouts on Linux · S · any + human
- **Depends on:** L1
- **Seen in L1 + review:** latency no longer grows, but (a) a one-off gap of up
  to ~2 s still occurs when a client connects to the Pulse null sinks (Pulse
  latency renegotiation), and (b) there are dropouts (0–8 per 3 runs), worse
  under host load (the review runs had load avg 11–15). On PipeWire, AOO's auto
  jitter buffer grows by up to ~50 ms after early drops.
- **Do:** re-run `tests/latency/run.sh` (both backends) on a quiet host;
  then measure on a real Debian VDI ↔ Mac over the VPN (human). If gaps
  remain: larger ALSA buffer for the plugin PCMs, avoid client churn on the
  engine sinks (gateway connects once), or a jitter-buffer floor. Target: 0
  gaps > 200 ms in 10 minutes of steady state.
- **Result (2026-10-13, maelosdebian, both backends on a quiet host):** PASS on
  both, no incomplete run; the harness reports per-run medians and dropouts
  (target < 150 ms), not the 10-minute steady-state figure above.
  - Pulse: medians 70.2 / 61.5 / 59.8 ms (vdi→console) and 62.5 / 57.1 / 60.0 ms
    (console→vdi); dropouts 2 and 4 across the three runs — the residual churn
    below, well under the 0–8 seen under load.
  - PipeWire: medians 101.1 / 102.4 / 101.1 ms (vdi→console, 1 dropout) and
    97.9 / 97.9 / 97.9 ms (console→vdi, 0 dropouts).
  So the latency itself is stable on both; what is left is the dropout/client
  churn work below and the human VDI↔Mac-over-VPN measurement.
  Images must be built with the right contexts first: web-console and pw-test
  from the repo root, aooserver from `aooserver/` (its Dockerfile runs
  `cmake -S .` on its own context).

### F2.1 — F2 coverage gaps from the P1.4 review · S
- **Depends on:** F2, P1.4
- **Why:** P1.4 proved two mute-cache traps with a temporary probe that was
  removed, and no scenario mixes known and unknown peers. Those protections
  are currently unguarded.
- **Do:** add scenarios: (1) `matrix-mixed`: 1 console + 1 vdi + 1
  no-advert peer; expect console↔vdi open and everything touching the unknown
  peer blocked. (2) `mute-held`: a console started with global send mute
  (test-only env, kept in the instrumented build like the existing
  `SONOBUS_NO_ROLE_*`) gets a VDI role: no audio while muted; after unmute,
  console↔vdi opens and same-role/unknown peers stay blocked. (3) Repeated
  mute→unmute cycles never open a blocked peer.
- **Done when:** the new scenarios pass, and each fails against a build with
  the corresponding P1.4 protection removed (counterfactual noted in Result).
- **Also fix here — `agent-silent` is environment-dependent (found
  2026-10-10).** That scenario declares no `input_device`, so on macOS the
  headless VDI opens the **real microphone** ("MacBook Pro Microphone"). Its
  ambient noise floor is above −60 dBFS, so the engine correctly reports
  `input: "ok"` and the scenario fails **5/5 runs**: `expected "silent", got
  "ok"`. It is **not a product regression** — the same scenario passed earlier
  the same day when the room was quieter, and P1.7's detector is sound (a
  probe with an unknown `input_device` reports `status: "missing"` as
  designed, and the Linux test drives the same detector with a −70 dBFS tone
  and passes). The assertion depends on the room, so it will flake wherever a
  live mic is present. **Fixed 2026-10-10** — the VDI peer now gets a `config`
  whose `input_device_candidates` resolves to a device that genuinely carries no
  signal, so `silent` comes from the signal path and **not** from
  `SONOBUS_AGENT_INPUT` (which would bypass the detector this scenario exists to
  exercise). Verified **3/3 PASS** in a row; the generated config resolves to
  `input_device: "Global Mic"` and the device is picked without enumerating the
  host, because the silent device differs per machine. To keep it portable the
  scenario lists candidates (`Global Mic`, `eqMac`, `loopback_sink.monitor`)
  and `tests/f2/peerprep.py` turns `*_candidates` into the concrete key, with
  `F2_INPUT_DEVICE` / `F2_OUTPUT_DEVICE` as an explicit override. **Caveat:** if
  none of the candidates exists on a host the agent reports that device
  `missing` (not `silent`) and the scenario **fails loudly** rather than passing
  by luck — set the override there. A true auto-detect would need a device
  enumeration the engine does not expose yet.
- **Open, pre-existing failure in `test-evaluate.sh` (found 2026-10-10).** It
  exits **1** with `99 passed, 1 failed`: the `steps-frozen` case. This is
  **not** caused by the `agent-silent` fix — proven by stashing that change and
  reproducing the identical failure on unmodified `main`. Root cause is in
  `tests/f2/evaluate.py`'s step loop, not in the product: an `increases`
  assertion whose baseline is captured when a hold starts, then **cleared
  whenever any poll fails**, oscillates — poll 1 sets baseline 10 and passes,
  poll 2 sees `10 > 10` false and fails (clearing the baseline), poll 3 passes
  again — so the step times out while `last` holds an *OK* poll, and the
  expected `expected to increase from 10` never reaches the report. The
  `steps-frozen` stub is frozen at `packetsReceived: 10` on purpose, so the
  scenario *should* fail, and it does; the defect is that the failure reason is
  not what the test asserts. Fix: keep the baseline across failed polls (or
  report the last *failing* poll rather than the last poll).
- **Result:**

### P1.6 — Role-less peers
- **Depends on:** P1.4 · **Size:** S
- **Do:** peers that never send a role (stock SonoBus) stay blocked; expose
  "unknown" role so UIs can show them greyed out. F2 scenario with one peer
  started without `--role` (or an env var forcing old behaviour).
- **Done when:** F2 passes with an unknown peer present.
- **Closed (Claude, 2026-10-09):** the engine half is done by P1.4 (unknown
  peers stay blocked indefinitely; F2 `blocked-unknown`). The "shown as
  unknown" half is now part of the control API (`unknownPeers` in
  control-api §3.2, filled by P4.2 from `remoteRole`/`hasRemoteRole`, never
  from `roleBlocked`) and is already rendered by `console-ui` (spec §3.4
  notice). Nothing left in P1.
- **Result:** Engine half done by P1.4 and tested: a peer that never sends a
  role keeps `roleBlocked` set indefinitely, so it can never be opened by any
  path, and the F2 `blocked-unknown` scenario (two peers, no advertised role)
  exits 0. `remoteRole` is exposed as `"unknown"` and `hasRemoteRole` as
  `false` in the peer dump. The UI half (showing such peers as unknown) is
  carried by the control API / `console-ui` per the closure note above.
  Relevant when touching anything that reads these flags: P1.4 leaves
  `roleBlocked` set for both same-role and unknown peers, so the UI must key
  off `remoteRole`/`hasRemoteRole`, not `roleBlocked`, to tell "stock SonoBus,
  unknown" apart from "same role as us, deliberately blocked".

### P1.7 — VDI agent health in peer info · S
- **Depends on:** P1.2
- **Do:** VDI peers add an `agent` object to their peer-info JSON, re-sent
  on every change: `input` (`ok` | `missing` | `silent`, where silent means
  no signal above −60 dBFS for N minutes, default 10), `output` (`ok` |
  `missing`), `paused` (bool), `config_error` (string or null). Consoles
  parse it into the peer state (exposed later through P4.2) so the Console
  card can show "VDI reports: …" (UX2).
- **Done when:** F2 scenario: start a VDI peer with a missing input device;
  the Console peer's dump shows `input: missing`.
- **Result:** A VDI adds `agent{input ok|missing|silent, output ok|missing,
  paused, config_error}` to its peer info, re-sent from a 1 Hz message-thread
  timer only on change. Silent = nothing above −60 dBFS for 10 min;
  `processBlock` only stores an atomic peak. Consoles parse it onto the
  RemotePeer; `--dump-peers` shows `peers[].agent`/`hasAgent`/`selfAgent`.
  Consoles advertise `kind` (`mac`, `other`, or `web` via
  `CROSSPOINT_CONSOLE_KIND`), shown as `peers[].kind`/`selfKind`. Re-sending
  peer info doesn't re-apply the P1.4 matrix. Test-only hatches:
  `SONOBUS_AGENT_INPUT/OUTPUT/SILENT_SECS`. F2: `agent-health`,
  `agent-silent` (real detector, 2 s window), `console-kind`; counterfactual
  verified. The wire format uses snake_case `config_error`, which **P4.2 maps
  to `configError`**. Not done (optional): `inputNode`/`outputNode`/
  `silentForMin`. `paused` stays false and `config_error` null until P2.3/P2.1
  wire them. **Review note:** the subagent's commit also reverted unrelated
  newer files; they were restored before merge. From now on every subagent
  branch is checked with `git diff --stat main...branch` before merging.

### P1.5 — Per-VDI talk toggle
- **Depends on:** P1.4 · **Size:** M (was S; see review notes)
- **Review notes (Claude, after P1.4, 2026-10-09). Read these first:**
  1. **Implement talk, solo narrowing and mute as gains, not as stream
     start/stop.** Don't use `setRemotePeerSendAllow`/`RecvAllow` or the
     `*Active` setters for them. Those flags are now the routing matrix's
     (P1.4); reusing them would hit exactly the cache traps P1.4 fixed, and a
     role re-apply would override the user's choice. Gains are also
     **instant**: toggling talk in the middle of a call must not restart an
     AOO stream (re-handshake gap, dropouts). Concretely: a per-peer send gate
     on the mic signal fed to that peer's source (silence when closed), and
     per-peer playback gain for mute.
  2. **Solo must dim, not cut.** Today `processBlock` forces other peers
     silent when anything is soloed (`(anysoloed && !remote->soloed)` →
     `usegain = 0`, `forceSilent`). Change that to multiply by
     `soloDimDb` (default −18 dB, a setting), as spec §3.5 and the brief require.
  3. **Expose the effective gate** as `hearsYou` (control-api §3.2:
     `talk && online && micTransmitting && !(anySolo && !solo)`) in the F2
     peer dump, and test against it, not against `receivingAudio`: with a
     gain-based gate the stream stays up while silent.
- **Do:** on a Console, a per-peer `talk` flag (default on) that
  gates Console → that VDI send. Keep it separate from the routing matrix so
  the matrix still wins (talk can't open a blocked path). **Solo narrows
  talk:** while any VDI is soloed, Console → VDI send goes only to soloed
  VDIs, whatever their talk flag says; the talk flags are kept and restored
  on un-solo. Solo also dims (not cuts) the other VDIs' playback by a
  configurable amount, default −18 dB.
- **Done when:** F2 scenarios on the dumped `hearsYou` (and, if feasible, a
  measured send level per peer): (a) Console talking to VDI-A only → VDI-B
  `hearsYou=false`; (b) all talk on, VDI-A soloed → only VDI-A hears;
  un-solo → all hear again; (c) solo dims, doesn't cut: VDI-B's playback gain
  is −18 dB while VDI-A is soloed. Needs a way to drive talk/solo in a
  running headless peer: either P4.4 commands, or a test-only control (env or
  CLI script) that's removed or kept out of release builds.
- **Result:** talk, solo narrowing, solo dim and mute are **gains** in `processBlock`; the
  P1.4 allow/active flags are untouched. A per-peer send gate (target =
  micTransmitting && (anySolo ? solo : talk), Console role only) feeds each AOO
  source, so toggling never restarts a stream. Solo dims by `soloDimDb`
  (default −18, measured −18.0 dB) instead of cutting; mute is a 0 playback
  gain with recv kept. All ramp over ~10 ms. Fixed: peer gain was applied twice
  (g → g²). Pre-fader meter per peer (`getRemotePeerPreFaderMeterSource`,
  P4.3). `hearsYou` is engine-computed. API for P4.4: mic mode/on/ptt,
  soloDimDb, talk/mute/solo/levelDb, hearsYou, getRemotePeerIndexByName.
  Test-only: `--test-control <json>` (polled), `SONOBUS_TEST_TONE_HZ`. F2 has
  timed steps and `talk-gate`, `solo-narrow`, `solo-dim`, `mute-prefader`,
  `ptt` (3/3 each, counterfactual verified). P4.4 notes: restrict solo to
  stations; the engine stores micOn in either mode, so P4.4 returns
  `wrong_mode`.

## P2 — VDI agent mode

### P2.2 — Vendor a YAML parser
- **Depends on:** F1 · **Size:** S · **Touches:** `sonobus/deps/`, `sonobus/CMakeLists.txt`
- **Do:** vendor a small header-friendly parser (prefer `rapidyaml`
  single-header, or `yaml-cpp`); wire into CMake for desktop builds. Android
  is not required (VDI role is desktop-only).
- **Done when:** builds on macOS and Linux with a trivial parse in a test or
  startup debug path.
- **Result:** Vendored **rapidyaml (ryml) 0.7.2**, MIT, tag `v0.7.2`, as the
  upstream amalgamated single header
  (`https://github.com/biojppm/rapidyaml/releases/download/v0.7.2/rapidyaml-0.7.2.hpp`,
  sha256 `00aca709dbd24115874a3ee97da4f615ea15cae60bb1155bef1e7369c7c2f6d8`,
  1 523 783 B), committed byte-identical as
  `sonobus/deps/rapidyaml/ryml_all.hpp`;
  `sonobus/deps/rapidyaml/README.md` records parser, version/tag/commit,
  upstream URL, licence, the date and method of obtaining it, the exact files
  added, and the update procedure. Chosen over yaml-cpp because it is MIT, is
  one file with no `add_subdirectory`/`FetchContent` (so no configure-time
  network), and keeps **per-node byte offsets** — needed by P2.1's in-place
  `Config::save()` that must preserve comments and key order. Upstream's
  amalgamation rule is followed exactly: exactly one TU defines
  `RYML_SINGLE_HDR_DEFINE_NOW` (`deps/rapidyaml/ryml_impl.cpp`), because the
  header alone does not link. That TU also defines
  `RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS` — a measured necessity: the stock
  default error callback calls `abort()`, so malformed YAML would kill the app
  (exit 134); with it, bad input throws `std::runtime_error`. CMake changes are
  additive only (41 insertions, 0 deletions): a self-contained
  `# BEGIN P2.2`/`# END P2.2` block exposing the **`yaml_parser` INTERFACE
  library** carrying `deps/rapidyaml` as its include directory, a small
  `sono_yaml` STATIC lib for the implementation TU, and a `yaml_smoke`
  executable (`tests/yaml_smoke.cpp`, `EXCLUDE_FROM_ALL`, out of the runtime
  path); `SonoBus` links `sono_yaml` privately so P2.1 can just
  `#include <ryml_all.hpp>`. **`sono_yaml` is built with
  `POSITION_INDEPENDENT_CODE ON`, which is required rather than cosmetic:** the
  default Linux format set is VST3 + Standalone + LV2, so this archive is
  linked into shared modules, and without the property linking any ryml-calling
  TU into a `.so` fails with 32 `recompile with -fPIC` / `dangerous
  relocation` errors (root cause: external data symbols such as
  `c4::detail::digits0099` referenced via `R_AARCH64_ADR_PREL_PG_HI21` instead
  of GOT-relative). It passes without the property only while nothing
  references the archive and the linker discards it — i.e. only until P2.1
  lands. The app does not reference the parser, so the linker drops it and
  behaviour is unchanged. Desktop only; Android/iOS use separate Projucer
  projects and are untouched. **Verified on macOS** (Release, arm64):
  `yaml_smoke` builds clean and exits 0, printing the parsed values of a
  document using the P2.1 key schema (11 checked leaves incl. nested
  `audio.*`/`codec.*`, plus a malformed-input rejection); `SonoBus_Standalone`
  still links. **Verified on Linux (2026-10-13, maelosdebian, native, not a
  container):** `cmake --build build/desktop-release --target yaml_smoke` builds
  it from the tree and the binary **executes** — prints the ryml version
  (0.7.2), rejects the malformed document, parses the 11 leaves and ends with
  `yaml_smoke: OK`, exit 0. **Verified on Linux** in `ubuntu:22.04` with the project's exact
  apt list: the full default target set (no `--target`, so VST3 + Standalone +
  LV2) builds 390/390 and exits 0 with the Standalone executable and both the
  VST3 and LV2 `.so` present, and `yaml_smoke` exits 0. The PIC fix was proven
  against the real `build/libsono_yaml.a`: a TU calling `ryml::parse_in_arena`
  links into a shared object successfully and the resulting `.so` is loadable
  and returns the right value, while the identical control against a non-PIC
  archive fails with 32 errors. **Not verified:** the Linux binaries are built
  but never *executed* (no display/audio in the container); Windows not built.
  `scripts/build-desktop.sh` was not touched. **Handoff to P2.1:** link
  `yaml_parser` (include dir only) and `#include <ryml_all.hpp>`; do **not**
  define `RYML_SINGLE_HDR_DEFINE_NOW` anywhere else — `sono_yaml` owns the
  single implementation TU and duplicating it means duplicate symbols.

### P2.1 — `--config` loader
- **Depends on:** P1.1, P2.2 · **Size:** M · **Touches:** `SonoStandaloneFilterApp.cpp`, new `Config.{h,cpp}`
- **Do:** `--config path.yaml` with keys: `server`, `group`, `password`,
  `username`, `role`, `audio.input_device`, `audio.output_device`,
  `audio.sample_rate`, `audio.buffer`, `codec` (opus/pcm + bitrate).
  CLI flags override YAML. Fail with a clear message on unknown keys or
  missing devices (list the available device names).
  **Linux / PipeWire device addressing (D12):** on Debian 13 JUCE sees ALSA
  devices, and PipeWire nodes (the loopback monitor, the virtual-mic sink)
  are not individually listed. `audio.input_device` / `audio.output_device`
  therefore name **PipeWire nodes**; the agent writes a generated ALSA config
  (e.g. `~/.config/crosspoint/asound.conf`, loaded via `ALSA_CONFIG_PATH` or
  an `@hooks` include) defining `pcm.crosspoint_in { type pipewire
  capture_node "<node>" }` and `pcm.crosspoint_out { type pipewire
  playback_node "<node>" }`, then opens those PCMs. Validate node names with
  `pw-cli ls Node` / `pactl list short sources|sinks` and list the valid
  ones on error. First check how maelo routes stock SonoBus on the VDIs today
  and match that setup.
  **Precedence bug to fix here (found in review of P1.1):** `--load-setup` is
  applied *after* `--role` (`SonoStandaloneFilterApp.cpp`, both windowed and
  headless paths), and `setStateInformationWithOptions` restores
  `ExtraState/Role` unconditionally, so a setup file saved by this fork
  silently overrides `--role`. Define and enforce one order, **CLI > YAML >
  setup file > saved state**, for role and every config key, and add an F2
  scenario: start with `--role vdi --load-setup <file saved as console>` and
  expect `selfRole: vdi`.
  **Write-back:** a `Config::save()` that updates `audio.input_device` /
  `audio.output_device` in the YAML when changed from the agent UI (P2.6).
  It must keep the file's comments and key order (edit in place, don't
  re-serialise everything) and write atomically (temp file + rename).
- **Done when:** F2 can start peers from YAML files instead of flags.
- **Result:** `--config` via JUCE-free `sonobus/Source/Config.{h,cpp}` (rapidyaml). Keys:
  server, group, password, username, role, audio.{input_device, output_device,
  sample_rate, buffer}, codec (opus|pcm), bitrate (an Opus preset), api.{port,
  bind, token, allowed_origins}. Errors are line-numbered with a non-zero
  exit; a missing device lists the available ones. **Precedence CLI > YAML >
  setup > saved state:** the role is pinned with `setRoleAndLock()`, so
  setState no longer restores it, and codec/audio apply after `--load-setup`.
  Also fixed: the `--load-setup <file>` space form (JUCE ignored it).
  `Config::save()` rewrites the device keys in place (comments, order, quotes,
  CRLF kept), atomically, re-verified before writing; P2.6 calls
  `setAudioDevices()` + `save()`. Example: `sonobus/vdi.example.yaml`. Tests:
  config_test 157 checks; F2 `config-yaml`, `config-precedence`
  (counterfactual verified). Merge note: the type is renamed
  `YamlApiSection` (clashed with P4.1's `ApiConfig`), and the `api:` section is
  wired into the control API. Linux PipeWire pinning is P2.11.

### P2.11 — Linux: pin devices to PipeWire nodes · M
- **Depends on:** P2.1
- **Split out of P2.1 (Claude, 2026-10-09)** so P2.1 can be finished and
  verified on macOS. Implements the D12 / P2.1 "Linux / PipeWire device
  addressing" note: on Linux, `audio.input_device` / `audio.output_device`
  name PipeWire nodes; the agent generates an ALSA config defining
  `crosspoint_in` / `crosspoint_out` PCMs of `type pipewire` pinned to those
  nodes and opens them through JUCE's ALSA backend. It validates the node
  names (`pw-cli ls Node` / `pactl list short sources|sinks`) and lists the
  valid ones on error.
- **Done when:** in a `debian:trixie` container with PipeWire running and two
  null sinks, the agent captures from the configured monitor and plays into
  the configured sink (measured with `pw-record`/levels), and a wrong node
  name produces the listed-valid-nodes error.
- **Result:** `sonobus/Source/PipeWireDevices.{h,cpp}` (Linux only) validates
  `audio.*_device` as PipeWire node names via `pw-dump` (sink monitor =
  `<sink>.monitor`), writes `~/.config/crosspoint/asound.conf` with hinted
  `pcm.crosspoint_in/out` of `type pipewire` (including the system alsa.conf),
  and sets `ALSA_CONFIG_PATH` at config time (libasound caches it). JUCE then
  lists "Crosspoint input/output: <node>" and the unchanged
  `applyAudioConfig` opens them. Non-node names keep P2.1 behaviour; an
  unknown name exits 1 listing the valid nodes. Also fixed a config error
  exiting 2. At merge: digital silence now reports `silent` (the P1.7 sentinel
  bug). Verified: `tests/linux/pipewire-pin.sh` in debian:trixie. Risks: null
  sinks need a driver (`node.driver=true` in the test), names resolve once at
  start, WirePlumber move policies untested.

### P2.7 — Example YAML + docs
- **Depends on:** P2.1 · **Size:** S · **Touches:** `docs/`, `sonobus/vdi.example.yaml`
- **Do:** commented example using device names from P0.3 if available.
- **Result:**

### P2.3 — VDI role locks
- **Depends on:** P1.4, P2.1 · **Size:** S
- **Do:** when role is VDI: send channels forced to 1 (mono), input monitor
  gains forced to 0 and ignored if set, Console-only features disabled.
- **Done when:** F2 dump/log shows mono send and zero monitor for VDI peers.
- **Result:** `applyVdiRoleLocks()` (on setRole, after every state restore, and on a
  locked parameter change) forces send channels to 1 and input monitoring to 0
  on a VDI; `setInputMonitor` is ignored. A Console is unchanged. The routing
  matrix already restricts a VDI to Consoles. F2 `vdi-mono-lock`: a stereo /
  monitor-1 setup file still yields mono and 0 on a VDI, and stays stereo on a
  Console.

### P2.4 — Auto-connect + reconnect
- **Depends on:** P2.1 · **Size:** M
- **Do:** connect on launch from config; on server disconnect or audio device
  loss, retry with exponential backoff (cap ~30 s), logging each attempt.
- **Done when:** F2 variant: kill and restart the local `aooserver`; the peer
  rejoins without intervention.
- **Result:** `sonobus/Source/AgentConnect.{h,cpp}`. `AgentConnector` (headless) is a
  message-thread state machine. Backoff 1→2→4… capped at 30 s, with jitter
  that only shortens; one stderr line per attempt; a bad password waits the
  cap and sets `config_error`/`bad_password`. The stock ServerReconnectTimer is
  off while it runs; peers are kept across outages. `AgentDeviceWatcher`
  reports `missing` and reopens devices on the same ladder. EngineState shows
  real `connecting/reconnecting/failed`, `attempt` and `retryInSec`. Test
  hatches: `SONOBUS_BACKOFF_SCALE`, control-file `audioLoss`. F2:
  `reconnect-server` (rejoin 2.0 s after the server returns),
  `reconnect-backoff`, `bad-password`, `reconnect-device`.

### P2.6 — VDI agent web UI on localhost
In scope (maelo, 2026-10-09): the agent has no native window, but its
localhost web UI is wanted, following the UX3 prototype. Day to day the YAML
stays the source of truth and problems also show on the Console (D11); this
UI is for checking and fixing a VDI when needed.
- **Depends on:** P5.1 (shared UI scaffold), P4.2 + P4.4 (state and commands), P2.3 · **Size:** M · **Who:** any + design review · **Touches:** `console-ui/` (agent view), engine static-file serving
- **Do:** per ROADMAP D10, the agent has no native window. It serves an HTML
  page on `http://localhost:<port>` (bound to localhost only), built from the
  `console-ui/` components and following UX3/UX4 (`docs/design/spec.md`,
  `design/prototypes/vdi/`): status, listeners, input/output device pickers
  (live level per input, test tone per output), pause sending, reload
  config. Device changes persist via `Config::save()` (P2.1). Add a PWA
  manifest so it can be installed (localhost counts as secure).
- **Note:** the agent works fully from YAML before this exists, so P2.8 does
  not wait for it.
- **Done when:** behaviour matches the spec, screenshots of every state are
  attached to the PR, and Claude has approved the design review.
- **Result:** the agent page (`console-ui/agent.html`) runs against a **real
  VDI engine**: the engine serves it on its API port (`--ui-dir`; `/` is
  agent.html for the vdi role). `console-ui/scripts/smoke-real-agent.mjs`
  starts aooserver + a Console + a VDI (isolated homes) and checks in headless
  Chrome: real state (role, name, Connected), "Listening now" shows the real
  Console, the config path, Pause/Resume change the engine, the picker lists
  the machine's real devices, no page errors, and the config isn't touched by
  browsing. 4/4 runs at load avg 13–17. **Design review fixes:** (1) the
  picker's secondary line showed a redundant "Name:" on macOS (node == name, no
  hint), so it now shows only extra info (node id if different, hint); (2) a
  real intermittent crash: the API can send the P4.1 placeholder state (`self`
  only) before the engine registers, so both UIs now wait for the full state.
  Not yet exercised: device switching through PipeWire nodes on Linux
  (P4.4 path untested on Linux), and the YAML write-back from the page (covered
  at API level by p43p44).
- **UX gap worth a design decision (D7 — Claude's call, not implemented here).**
  `crosspoint.desktop` has `Exec=crosspoint %u`, i.e. the application-menu entry
  launches the **GUI** binary, which is exactly what a headless VDI does not
  want: it needs an X display (fails with "Authorization required" / a
  `DISPLAY` that may be unset), and it does **not** start the agent. A user
  installing the package and clicking "Crosspoint" therefore gets a broken
  window instead of their console, and the natural-looking
  `open crosspoint` / `xdg-open crosspoint` is a *URL* attempt, so the browser
  reports `DNS_PROBE_FINISHED_NXDOMAIN` for the bare word "crosspoint". The
  agent's page is at **`http://localhost:7071/`** (VDI default port).
  Suggested fix (design owner to approve): point the desktop entry at the agent
  page (`Exec=xdg-open http://localhost:7071/`) or drop the `.desktop` file from
  the VDI package, and add a line to `packaging/debian/INSTALL.md` §5 giving the
  URL explicitly. Left alone here because it is UI/copy, and D7 keeps design
  decisions with the design session.
  **Decided and implemented (Claude, design, 2026-10-10):** the VDI package's
  menu entry is now **"Crosspoint agent"** with `Exec=xdg-open
  http://localhost:7071/` (no GUI launcher on a VDI). `INSTALL.md` §5 gives the
  URL and explains `open crosspoint`; spec §6 records the launcher.

### P2.5 — Run as a service
- **Depends on:** P2.4 · **Size:** M
- **Do (D12, Debian 13):** a **systemd user unit**
  (`~/.config/systemd/user/crosspoint-agent.service`, `After=pipewire.service
  pipewire-pulse.service`, `Restart=on-failure`) plus `loginctl
  enable-linger <user>` so it starts at boot without a login. It must be a
  user unit, not a system service, because PipeWire runs per user.
  Install/uninstall docs.
  **Signal handling (from the F2 handoff):** today `SIGINT` is ignored and
  `SIGTERM` kills the process without running JUCE `shutdown()`, so on
  service stop nothing is saved (F2 teardown ends in `SIGKILL` for the same
  reason). Install handlers (`SIGTERM`/`SIGINT` on Linux, console control /
  service stop on Windows) that quit the message loop cleanly so state is
  persisted and peers see an orderly leave.
- **Done when:** VDI reboot → agent connected without login interaction
  (or with only the expected user login).
- **Decision (Claude, 2026-10-10, from D11):** at boot, a **missing audio
  device must not be fatal** for a VDI. The agent stays up, reports
  `input/output: missing` (the Console shows it) and lets `AgentDeviceWatcher`
  retry. **Config errors stay fatal** (bad YAML, unknown keys), so systemd's
  `Restart=on-failure` plus the journal surface them. Implement this
  startup-path change as part of P2.5.
- **Already done elsewhere:** SIGTERM/SIGINT clean quit (P4.1); reconnect
  (P2.4).
- **Result:** Unit shipped at `packaging/debian/crosspoint-agent.service`
  (installed to `/usr/lib/systemd/user/`), `Type=simple`,
  `After=`/`Wants=pipewire.service pipewire-pulse.service wireplumber.service`,
  `Restart=on-failure`, `RestartSec=5`, `KillSignal=SIGTERM`,
  `TimeoutStopSec=10`, journal logging, plus `NoNewPrivileges` /
  `LockPersonality` / `RestrictSUIDSGID` / `SystemCallArchitectures=native`.
  The missing-device-not-fatal decision above is implemented via
  `tolerateDevices` in the headless startup path (SonoStandaloneFilterApp) and
  retried by `AgentDeviceWatcher`; a bad YAML / unknown key stays fatal.
  **Verified end-to-end** by `tests/linux/deb-systemd.sh` on 2026-10-10
  (missing-device and reboot cases included): **21 CHECK PASS, 0 CHECK FAIL**,
  final `PASS`. Covered: install → `systemctl --user enable --now` → active,
  health API on 7071 reports `role vdi`, `connection.state == connected`
  against a local aooserver, journal shows the connection; `stop` exits cleanly
  in 1 s with `Result=success`; **container reboot with linger → service active
  with no login** and health OK; **input node missing at start → service stays
  up, `input.status == missing`, `NRestarts=0`, and it recovers when the node
  appears**; broken YAML → exits 1, systemd restarts every 5 s, error in the
  journal, and it recovers once fixed; `apt remove` with the service running →
  prerm stops it, binary removed, user config kept. Note the test runs
  `debian:trixie` under `--privileged --cgroupns=host` with systemd as PID 1 —
  a real VDI still needs the same flow run for real (P2.8).

### P2.9 — Minimal native tray icon · S · deferred
Not a priority (D11): the agent is a service with fixed config, and problems
surface on the Console via P1.7.
- **Depends on:** P2.4, UX4 · **Who:** any + design review
- **Do:** tray icon with the five UX3 badge states and a menu: open the
  agent UI in the default browser, pause/resume sending, reload config,
  quit. On Linux, use StatusNotifierItem/AppIndicator where available
  (JUCE's `SystemTrayIconComponent` uses the legacy X11 tray, which GNOME and
  many others don't show). Where no tray exists, raise a desktop
  notification when the agent needs attention. Nothing may depend on the
  tray being visible.
- **Result:**

### P2.10 — Packages per VDI OS · M
- **Depends on:** P2.5
- **Do (D12):** a `.deb` for Debian 13 (trixie), built reproducibly in a
  `debian:trixie` container (`scripts/build-deb.sh`; `tests/linux/Dockerfile`
  already proves the Linux build). Contents: the agent binary, the systemd
  user unit, `vdi.example.yaml`, and a postinst note on `loginctl
  enable-linger`. Declare runtime deps (`libasound2t64`, `pipewire-alsa`,
  libcurl, freetype, etc.). Include an install/uninstall guide.
- **Done when:** a clean VDI goes from package to "Connected" by following
  the guide.
- **Result:** `scripts/build-deb.sh` builds in a `debian:trixie` container via
  `packaging/debian/Dockerfile.build` (repo root as context), packaging with
  `packaging/debian/make-deb.sh`; version defaults to the CMake version plus
  `+git<sha>` and `SOURCE_DATE_EPOCH` is pinned from the commit time for
  reproducibility. The package installs `/usr/bin/crosspoint`,
  `/usr/lib/systemd/user/crosspoint-agent.service` and
  `/usr/share/doc/crosspoint/vdi.example.yaml`, with `postinst`/`prerm`/`postrm`
  maintainer scripts and `packaging/debian/INSTALL.md` (install, configure,
  enable, check, reload/upgrade/uninstall). **Built and verified 2026-10-10:**
  `crosspoint_1.7.2+git6d74eca1_arm64.deb`, 8 937 772 B, contents confirmed
  from the archive; installed with apt in the systemd container so `Depends`
  resolved, all 21 checks in `tests/linux/deb-systemd.sh` passed (see P2.5).
  **amd64 built too (2026-10-10)** — the VDIs are x86_64, so the arm64 artifact
  alone is not shippable: `crosspoint_1.7.2+git9a237e6f_amd64.deb`, 9 145 060 B,
  sha256 `20e97bc9ab49ba9399ee502fb0607eb53e9b34f6a8b30779c2a6e222c2d8964d`,
  `Architecture: amd64`, x86-64 ELF (`0x3e`), `Depends` resolving to trixie
  packages (libasound2t64, libc6 >= 2.38, libfreetype6, libopus0, libstdc++6,
  pipewire-audio, pipewire-bin, ca-certificates). Built on the **native amd64
  builder** (`docker buildx --builder buildkit-priv --platform linux/amd64`),
  not under qemu, which is the difference between minutes and hours. Transferred
  to the real VDI `maelosdebian` (192.168.0.71, Debian 13 trixie amd64) and the
  sha256 was verified there; extracted unprivileged, `crosspoint --help` on the
  VDI lists `--headless`, `--role`, `--config`, `--api-port`, so the binary
  loads on a real amd64 trixie box. **Also fixed the reason builds were slow:**
  the repo had no `.dockerignore`, so each root-context build uploaded the whole
  repo (2.8 GB on disk) before compiling; added one (commit `d49c5f75`).
  **Caveat:** `lintian` reports one error, `embedded-library libpng
  [usr/bin/crosspoint]` (upstream JUCE links its own libpng), plus warnings for
  maintainer-script-calls-systemctl (deliberate: prerm stops the user service)
  and no-manual-page. None are release-blocking; the libpng error is inherent
  to the JUCE build.
  **When installing on a VDI, use a package built AFTER `47f6f738`.** The first
  amd64 artifact (`+git9a237e6f`) predates the P2.6 UI fix, so a VDI installed
  from it has **no `/usr/share/crosspoint/ui`** and a unit with **no `--ui-dir`**
  — `http://localhost:7071/` then 404s (confirmed on the real VDI: package
  `1.7.2+git9a237e6f` is installed there and is missing both). Rebuild with
  `scripts/build-deb.sh` (or `PLATFORM=linux/amd64`) before deploying, and
  verify with `dpkg -L crosspoint | grep crosspoint/ui` and
  `grep ui-dir /usr/lib/systemd/user/crosspoint-agent.service`.

### P2.8 — Cut over from the Carla hub; retire it
- **Depends on:** P2.5, P2.6 · **Size:** S · **Who:** human
- **Do:** point all VDIs (3, possibly 4) at the mesh with the VDI role and
  the Mac at the Console role. Run both setups side by side for a few days
  of daily use, then shut down the Ubuntu Studio + Carla server. Install
  from the P2.10 package; the agent web UI (P2.6) isn't required.
- **Done when:** a full working day on the mesh with no fallback to the hub.
- **Result:** *In progress — first VDI surveyed 2026-10-10.* Target:
  `maelosdebian` = **192.168.0.71**, Debian 13 trixie **amd64**, kernel
  6.12.86, 8 cores, user `maelo` (uid 1000). Already correct for D12:
  `pipewire`/`pipewire-pulse`/`wireplumber` active, `pipewire-alsa` and
  `dbus-user-session` installed, and **`Linger=yes` already enabled**. Its
  virtual devices (created by the user's own `sonobus-sinks.service`, documented
  in `~/sonobus-runbook.md`) map cleanly onto the agent:
  - `sb_system_out` (+ `.monitor`) — where apps play; **agent input**.
  - `sb_mic_in` — **agent output**; `sb_virtual_mic` is a
    `module-remap-source` re-presenting `sb_mic_in.monitor` to apps as a real
    mic, so apps still see a microphone.
  Existing daily-use group is **`lagreca` on `aoo.sonobus.net:10998`** (from the
  flatpak `net.sonobus.SonoBus` settings), so the cutover can reuse it. The
  amd64 `.deb` was transferred and checksum-verified on the box, and the binary
  loads there. **The agent itself runs on this VDI:** extracted unprivileged and
  started headless on the real amd64 hardware, it serves
  `GET /api/v1/health` → `{"ok": true, "app": "Crosspoint", "version": "1.7.2",
  "role": "vdi"}`. That also settles P7.1's amd64 question (no emulation in
  play).
- **Installed and verified on the VDI (2026-10-10), without sudo.** `sudo` needs
  a password on this box, but **none is required for a systemd *user* unit**, so
  the agent was installed user-locally and the *real* service was exercised:
  binary at `~/.local/bin/crosspoint`, config at
  `~/.config/crosspoint/vdi.yaml` (mode 600), unit at
  `~/.config/systemd/user/crosspoint-agent.service` with
  `ExecStart=%h/.local/bin/crosspoint --headless --config
  %h/.config/crosspoint/vdi.yaml` (the only change from the packaged unit is
  the binary path; `Documentation=` dropped). `systemctl --user enable --now
  crosspoint-agent` → **active, `NRestarts=0`**, symlinked into
  `default.target.wants`, and `loginctl` already had **`Linger=yes`**, so it
  starts at boot with no login. `stop` → **`Result=success`** within the 10 s
  `TimeoutStopSec`; `start` → active again.
  **Live state proves the whole path works:**
  `{"self":{"name":"maelosdebian","role":"vdi"},
  "connection":{"state":"connected","server":"aoo.sonobus.net:10998",
  "group":"lagreca"},"input":{"node":"Crosspoint input: sb_system_out",
  "status":"ok"},"output":{"node":"Crosspoint output: sb_mic_in","status":"ok"}}`
  — i.e. P2.11's PipeWire node addressing resolved both named devices, and the
  agent joined the existing daily group.
  **End-to-end with a real Console:** a Mac Console peer joined the same group
  and the VDI appeared as
  `maelosdebian: presence "online", latencyMs 63, lossPct 0.5, jitterBufferMs
  49, agent {input "ok", output "ok", paused false}` — so P1.4 routing, P1.7
  health reporting and P2.11 device pinning all work over the real internet.
  `pactl` showed the agent's live streams: a sink-input
  (`PipeWire ALSA [crosspoint]` → `sb_mic_in`) and source-outputs capturing
  `sb_system_out.monitor` and `sb_virtual_mic`.
  **Gotcha worth recording:** the flatpak settings had three different
  `groupPassword` values recorded for `lagreca`; the newest-looking one
  (a typo of the real one) is wrong and gives
  `could not join group 'lagreca': wrong password`. The correct value is in the
  VDI's `vdi.yaml` (redacted: secrets don't belong in the repo).
  **Remaining (needs a human):** install the `.deb` system-wide with `sudo` if
  the packaged path is wanted (this test used the user-local binary), and the
  multi-day side-by-side run before retiring the Carla hub.
- **Packaging bug found and fixed while installing (P2.6).** On a real VDI,
  `http://localhost:7071/` returned **404**: the `.deb` shipped **no UI assets**
  and the systemd unit passed **no `--ui-dir`**, so P2.6's agent page was
  unreachable on an installed box even though the engine serves `agent.html`
  for the vdi role and the container image had always done the right thing
  (`docker/web-console/Dockerfile` copies `console-ui/` and passes
  `--ui-dir`). Fixed: `packaging/debian/Dockerfile.build` now copies
  `console-ui/`, `make-deb.sh` installs it to `/usr/share/crosspoint/ui`, and
  `crosspoint-agent.service` passes
  `--ui-dir /usr/share/crosspoint/ui`. Verified on the VDI (using a user-local
  copy of the same layout at `~/.local/share/crosspoint/ui`): `/` and
  `/agent.html` both return **200** and the page is
  `<title>Crosspoint agent</title>`, with the agent still
  `connected` and `input`/`output` both `ok`.
  **Rendered in a real browser** (headless Chrome over an ssh tunnel, so the
  origin is `localhost` and therefore a secure context): the page loads with
  **no JS errors** and reads
  `title "maelosdebian · Crosspoint agent"`, body
  *"maelosdebian Crosspoint agent — Connected — Sending this VDI's audio to 1
  Console. Group lagreca · aoo.sonobus.net:10998 · mono"*, and a
  "LISTENING NOW — web-console Console" entry. So P2.6 is working on the real
  VDI end to end. The **installed** `.deb` there is still the pre-fix
  `+git9a237e6f` build, so the packaged path needs a rebuild (see P2.10) —
  this verification used the user-local layout, which is byte-identical to what
  the fixed package installs.

## P3 — Strip jam features

All P3 tasks touch `SonobusPluginEditor.cpp` and `SonobusPluginProcessor.cpp`:
**one at a time**. Each must keep F1 build + F2 passing and must not break
the mobile `.jucer` source list (remove deleted files from it too).

### P3.1 — Remove metronome · M
- **Depends on:** P1.4
- **Do:** delete metronome UI, processor paths, params, saved-state keys
  (tolerate old keys when loading).
- **Result:**

### P3.2 — Remove soundboard · M
- **Depends on:** P1.4 · Files: `SoundboardView.*`, `Soundboard*`
- **Result:**

### P3.3 — Remove chat · S
- **Depends on:** P1.4 · Files: `ChatView.*`, chat OSC messages
- **Result:**

### P3.4 — Remove file playback + recording UI · M
- **Depends on:** P1.4 · Files: `SampleEditView.*`, file-playback paths, recording UI
- **Do:** remove file playback completely. Remove the recording **UI**, but
  **keep the recording engine** (`RecordFileOptions`, especially
  `RecordIndividualUsers` and `RecordSelf`, the file writers and formats):
  P10 builds on it. Leave it compiled and reachable from code.
- **Result:**

### P3.5 — Remove monitor faders / input monitoring UI · S
- **Depends on:** P2.3
- **Do:** remove from both roles; engine monitor gains fixed at 0.
- **Result:**

### P3.6 — Remove latency match, beat grid, jam-only options · M
- **Depends on:** P3.1 · Files: `BeatToggleGrid.*`, latency-match code, `OptionsView.cpp`
- **Result:**

### P3.7 — Trim effects to what UX2 specifies · S
- **Depends on:** P3.5, UX2
- **Do:** keep only the mic processing the UX2 design calls for; remove the
  rest from the engine.
- **Result:**

### P3.8 — Jitter/codec settings where UX2 places them · S
- **Depends on:** P3.7
- **Do:** engine side only: keep the settings UX2 exposes, fix the rest to
  sensible defaults.
- **Result:**

### P3.9 — Own app identity · S
- **Depends on:** ~~UX4~~ the name was decided directly by maelo (2026-10-09); the icon still comes from UX4
- **Why (found in review):** the fork still builds as "SonoBus", so it shares
  `~/Library/Application Support/SonoBus/SonoBus.settings` (and the Windows /
  Linux equivalents) with the stock SonoBus maelo uses daily. Running the
  fork writes fork-only state (e.g. `ExtraState/Role`) into that file, and
  `reconnectlast` can make it rejoin daily groups as the wrong identity. Also,
  until P1.4 lands the fork passes no audio, so it must not replace the daily
  app by accident.
- **Do:** set the product name, bundle id / app id and settings folder from
  UX4 in `sonobus/CMakeLists.txt` and the standalone app, so the fork and
  stock SonoBus run side by side with separate settings.
- **Result:** the app is now **Crosspoint**, bundle id
  `io.lagreca.app.crosspoint`. One identity block at the top of
  `sonobus/CMakeLists.txt` (`APP_NAME`, `APP_BUNDLE_ID`, `APP_COMPANY`,
  `APP_URL_SCHEME`, `APP_LINUX_DIR`, `APP_MFR_CODE`) feeds the bundle, the
  plist URL scheme, the plugin codes (`Lagr`/`Xpnt`/`Xpni`, so no clash with
  stock SonoBus VST3/AU) and, via `APP_ID_*` defines,
  `sonobus/Source/AppIdentity.h`. Settings: macOS `Application
  Support/Crosspoint`, Linux `~/.config/crosspoint`, Windows
  `%APPDATA%\Crosspoint`. Links: `crosspoint://` (generation, clipboard
  parsing, URL handling), and the copied invite link no longer uses
  `go.sonobus.net`, which opens stock SonoBus. **Also fixed beyond the card:**
  the upstream auto-updater is compiled out (`APP_ID_ENABLE_UPDATE_CHECK=0`;
  it would download stock SonoBus over the fork); both legacy migrations are
  disabled (Linux `~/.config/SonoBus.settings` move, and the Windows
  `%APPDATA%\dummy` move), since each could move stock SonoBus's files.
  Window/title label show the product name. CMake target names stay `SonoBus`
  / `SonoBus_Standalone`, so build commands are unchanged; only the bundle is
  `Crosspoint.app`. Scripts updated (`build-desktop.sh` output,
  `tests/f2/run.sh` default app path).
  **Verified:** Info.plist shows the new id and only the `crosspoint` scheme;
  F2 `mesh-stock`, `matrix-1v1`, `matrix-2v2`, `blocked-unknown` pass; test
  peers create `Application Support/Crosspoint`; the real stock
  `~/Library/Application Support/SonoBus` was unchanged before/after.
  **Not done (owned elsewhere):** app icon (UX4); Linux `linux/install.sh` +
  `sonobus.desktop` (P2.10); mobile `.jucer` identity (P9.2/P9.3). Left as
  is: the `*.sonobus` setup-file extension, the "SonoBusSession" recording
  filename prefix (P10), and the internal state tree id `SonoBusAoO`. Changing
  that id would break loading of saved setups for no user-visible gain.

### P3.10 — Wire the Crosspoint icon · S
- **Depends on:** UX4 (icon files in `design/icon/`)
- **Do:** in `sonobus/CMakeLists.txt` set `ICON_BIG` to
  `../design/icon/crosspoint-1024.png` and `ICON_SMALL` to
  `../design/icon/crosspoint-256.png` (or copy them into `sonobus/images/`).
  Coordinate with anyone else editing CMake (P2.2/P2.1 touch it).
- **Done when:** the built `Crosspoint.app` shows the new icon in Finder and the Dock.
- **Result:** `ICON_BIG`/`ICON_SMALL` point at `../design/icon/crosspoint-1024.png` / `-256.png`. Verified: the rebuilt `Crosspoint.app` bundles a new `Icon.icns`, which, extracted with `sips`, is the Crosspoint artwork.

## P4 — Control API

### P4.5 — API schema (write first)
- **Depends on:** P1.4, UX2 · **Size:** M · **Who:** any + design review · **Touches:** `docs/control-api.md`
- **Do:** define the WebSocket JSON protocol before code: message envelope,
  `state` snapshot, `event` deltas, `meters` frames, `cmd` requests and
  replies, error format, versioning. It must cover everything on UX2's
  "what the UI needs from the engine" list. If the engine can't provide
  something on that list, flag it rather than dropping it.
- **Done when:** Claude has confirmed it covers the design; P5.1 can build
  mocks from it.
- **Result:** `docs/control-api.md` v1. One WebSocket at `/api/v1/ws`
  (hello → optional token auth → full `state` → JSON-pointer `patch` events
  with `rev` and resync), opt-in `meters` (pre-fader stations, pre-gate mic,
  dBFS), commands with `ack`/`result`, error codes, close codes, versioning.
  Safety rules: push-to-talk is released when the holding client
  disconnects; `hearsYou` is engine-computed; a non-loopback bind requires a
  token and an Origin check. The executable reference is
  `console-ui/src/api/mock.js` (13 protocol tests). Written before P1.4
  landed: **when P1.4 merges, check §3.2 `hearsYou` and §7 against the real
  routing code**; open points for P4.1/P4.2/P1.7 are listed in §7.

### P4.1 — Embedded WebSocket server · M
- **Depends on:** P4.5
- **Do:** a WebSocket server inside the engine (small vendored lib, or JUCE
  sockets + minimal WS framing), bound to `127.0.0.1` by default with a
  configurable bind address/port (P7 needs non-localhost). Runs off the
  audio thread. The same HTTP listener serves the static UI files (the VDI
  agent UI, P2.6, and the Console UI in P6/P7).
- **Done when:** `websocat ws://127.0.0.1:<port>` gets a hello message.
- **Result:** HTTP/1.1 + RFC 6455 server in the app (`sonobus/Source/ApiServer.{h,cpp}`,
  JUCE sockets, own framing, `ApiSha1.h` tested against the RFC vectors; no new
  dependency). An accept thread plus one thread per connection, never the audio
  thread; stop() closes with 1001 and joins. Flags `--api-port` (default 7070
  console / 7071 vdi, 0 = off), `--api-bind` (127.0.0.1), `--api-token` (or
  `CROSSPOINT_API_TOKEN`), `--api-allow-origin`, `--ui-dir`; YAML `api:` gives
  defaults (wired at merge with P2.1). A non-loopback bind without a token
  refuses to start. Origin check plus a loopback Host check (DNS rebinding).
  `/api/v1/health`, static UI (`/` is index or agent by role), `/api/v1/ws`
  (hello, auth 4401, 4400, placeholder state, `cmd` → `not_supported`). Hooks:
  `setStateProvider`/`publishPatch` (P4.2), `broadcastTopic` (P4.3),
  `setCommandHandler`/`setSessionClosedHandler` on the message thread (P4.4).
  Headless now quits cleanly on SIGTERM/SIGINT (atomic flag + timer), which
  covers the P2.5 signal requirement. Tests: `tests/api/p41.test.mjs` 20/20
  (including the real console-ui client), sha1_test, F2 green.

### P4.2 — State snapshot + change events · M
- **Depends on:** P4.1
- **Do:** include each peer's VDI agent health (P1.7). The Console also
  **remembers known stations** (name, colour, last placement and level,
  last seen) in its settings, so a known VDI that isn't connected appears as
  `offline` with `last_seen`, and its settings come back when it rejoins.
- **Done when:** a test client receives a snapshot on connect and a delta when
  a peer joins/leaves (use F2).
- **Result:** `sonobus/Source/EngineState.{h,cpp}` builds Console (§3.2) and agent (§3.3)
  state at 10 Hz on the message thread, diffs it into set/del JSON-pointer ops
  (patch.js semantics) and publishes them; it is also the server's state
  provider (an early-client race is fixed: setStateProvider re-sends state).
  Presence online/lost (3 s)/offline; health per spec with 10 s hysteresis;
  agent health mapped; unknownPeers/otherConsoles. Remembered stations
  (`ExtraState/Stations`: colorIndex, level, pan, mute, talk, lastSeen),
  colorIndex assigned once, mix restored on rejoin; `forgetStation()` ready
  for P4.4. Integrated at merge with the P1.5 API (talk, mute, levelDb,
  mic.*, soloDimDb; hearsYou = engine gate AND online). Tests:
  `tests/api/p42.test.mjs` (real client Store, zero resyncs, lost→offline,
  stable colour, persistence across restart, agent state). Gaps:
  inputNode/outputNode/silentForMin null; agent `consoles[].talking` always
  false; restore-on-rejoin untested until P4.4 commands exist.

### P4.3 — Meters stream · S
- **Depends on:** P4.1
- **Do:** ~30 fps peak/RMS per peer + mic + master; client can subscribe or
  unsubscribe.
  **Review note (Claude, 2026-10-09):** station meters must be **pre-fader**
  (control-api §4, spec §3.5: a muted or dimmed call that starts talking must
  still show activity). Today `recvMeterSource.measureBlock` runs *after* the
  channel-group gain, mute and solo processing in `processBlock`, so muted or
  dimmed stations would read silent. Add a second per-peer meter measured on
  `workBuffer` **before** the gain stage, and keep the existing post-gain one
  for anything that needs it. The mic meter is pre-gate (shows my level while
  not transmitting).
- **Result:** `sonobus/Source/ApiCommands.{h,cpp}`: a 30 Hz message-thread timer sends
  meters only while a non-hidden session subscribes. Stations use P1.5's
  pre-fader source (re-fetched by index per frame, online only); mic is the
  pre-gate send meter; output is the main meter. The agent sends
  input/output, plus `devices` only to `deviceMeters` subscribers. dBFS
  rounded to 0.1, null below −90. Tested in `tests/api/p43p44.test.mjs` (none
  before sub, ~30 fps, a muted station keeps its meter, visibility pause).
  Limits: peak is ff_meters' 500 ms hold; agent `devices` covers only the open
  input.

### P4.4 — Commands · M
- **Depends on:** P4.2, P1.5
- **Done when:** a script drives connect → set pan/mute/solo/talk → disconnect
  on a headless Console and the F2 dump shows the effects.
- **Result:** every control-api §5.1/§5.2 command on the message thread, with the mock's
  validation, clamping and error codes. Station setters by id (offline
  stations update the remembered mix); `forget`, `spread` (0.01), `centerAll`,
  `talkToAll`; mic mode/on (`wrong_mode`); per-session PTT holders released on
  disconnect; output level; devices; `settings.set` (soloDimDb, codec/bitrate;
  pttHotkey/networkBuffer → `not_supported`); connection connect/disconnect
  (stops the P2.4 connector so it won't fight the user). Agent: live device
  switch + YAML write-back (comments kept) with `previous`, pause (send gates
  + agent `paused`), a 440 Hz −18 dBFS 1.5 s test tone, reloadConfig (errors
  in configError + connection.error `config`), retryNow. Tests: p43p44 9/9;
  the merged suite is 35/35 API + 20/20 F2 + evaluator PASS (review). Gaps:
  Linux PipeWire path of `agent.setInput/Output` untested; connect acks
  immediately (a wrong password shows later as `failed`).

## P5 — Console UI (HTML)

Location: `console-ui/` (new, top level). Implements the UX2/UX4 design.
The tech choice must not make the design harder to build; keep the build
chain light unless the spec needs more. **Every P5 task is `any + design
review`**: it is done only after Claude approves screenshots or a running
build against the spec. Implementers don't make design decisions (see
[Design ownership](#design-ownership)).

### P5.1 — Scaffold `console-ui/` from the prototype · M
- **Depends on:** UX4 (done: `docs/design/spec.md`), P4.5
- **Read first:** spec §8 (implementation notes) and §9 (review checklist).
  Two entry points from one codebase: `console` and `agent` (P2.6).
- **Do:** start from the UX4 prototype HTML/CSS, apply the spec's tokens, and
  add a mock API client that follows the P4.5 schema (fake peers, animated
  meters) so P5.2–P5.5 can be built before the engine API exists.
- **Done when:** the scaffold renders the prototype's screens on mock data;
  design review passed.
- **Result:** `console-ui/` with no build step and no dependencies (plain ES
  modules), with two entry points, Console (`index.html`) and agent
  (`agent.html`). It has a shared API client (state/patch/resync, commands,
  meters, reconnect), an in-page mock engine with all prototype scenarios,
  CSS variables generated from `tokens.json` (`npm run tokens`), PWA
  manifests and icons. Components are ported from the prototypes to spec §3/§6
  and render from engine state only. Tests: `npm test` (13 unit/protocol
  tests, mutation-checked: breaking the solo rule or the PTT release makes
  them fail) and `scripts/smoke.mjs` (21 checks in real Chrome over the
  DevTools protocol: solo narrowing, keyboard map, PTT via Space, placement,
  agent picker, Undo). Bugs found and fixed along the way: ARIA booleans
  written as empty attributes; "All 2 stations hear you" when stations were
  down (the band now counts every known station; spec wording updated); a
  stray slash in the right-to-left truncated config path; Undo offered back
  to a missing device (now hidden; spec updated). Design review: self-review
  against spec §9 with desktop and phone screenshots of Console
  (everyday/twocalls/problem) and agent (nodevice/talking). **P5.2–P5.5** now
  mean bringing each area to full spec detail and states against this
  scaffold.

### P5.2 — Mixer view · M
- **Depends on:** P5.1
- **Do:** the per-VDI controls and health display exactly as specified in
  UX4, including the unknown-peer, degraded and disconnected states.
- **Result:**

### P5.3 — Talk-back controls · M
- **Depends on:** P5.1
- **Do:** mic, mute / push-to-talk, talk targeting and master controls as
  specified in UX4 (keyboard and touch).
- **Result:**

### P5.4 — Connection / onboarding flow · S
- **Depends on:** P5.1
- **Do:** as specified in UX4, including error states.
- **Result:**

### P5.5 — Phone layout · S
- **Depends on:** P5.2, P5.3
- **Do:** the UX4 phone breakpoints and touch behaviour.
- **Result (P5.2–P5.5, Claude, 2026-10-09):** brought `console-ui` to full spec
  detail against the mock. Mixer: health line tap/Enter toggles inline link
  details (spec §5: no hover-only info). Talk-back: unchanged from P5.1 and
  re-verified. Connection: the failed state offers **[Settings]** in the top
  bar; the settings sheet has an editable server/group/password/name form with
  Connect/Disconnect and the failure reason inline, mic/output pickers and a
  solo-dim picker (−12/−18/−24/−30 dB). Fields are built once, so typing
  survives patches. Focus moves into the sheet on open and back to the gear on
  close; Escape works from inside fields. Phone: verified at 390 px for all
  scenarios and the settings sheet. Review tooling: `scripts/screenshots.mjs`
  renders 23 shots (7 Console scenarios × desktop/phone, settings
  desktop/phone, 7 agent states) into an index page; `?open=settings` deep link.
  Tests: 13 unit tests plus 30 browser smoke checks pass. **Left for P5.6:**
  wiring to the real engine once P4.2–P4.4 exist.
- **Server default (2026-10-13, PR #5):** the sheet seeded the Server field
  empty, so a fresh install had to be told `aoo.sonobus.net` by hand. It now
  seeds `aoo.sonobus.net:10998` (`console-ui/src/lib/defaults.js`, mirroring the
  engine's `DEFAULT_SERVER_HOST`/`DEFAULT_SERVER_PORT`), and
  `connection.connect` falls back to the same host when it is called without a
  server, so a group alone is enough.
- **Result:**

### P5.6 — Wire to the real API · M
- **Depends on:** P5.2–P5.5, P4.2–P4.4
- **Done when:** the UI drives a headless Console engine in F2 end to end,
  with no visual differences from the mock-data build; design review passed.
- **Result:** proven for the Console by the P4.4 subagent: the engine serves
  `console-ui` (`--ui-dir`), and headless Chrome on `/` (no mock) reaches
  `ready`. Solo, Mute, Level, the mic toggle, the push-to-talk mode and
  holding Space each change the engine state, meters reach the UI, and there
  are no page errors. The agent page against a real VDI engine is P2.6.

## P6 — Native shell (Mac)

Native iOS is deferred (ROADMAP D9); iOS uses the PWA (P7.7). The native
Android app is now planned as P9.2, tasks A1–A14 (lanes in the P9.2 card).


### P5.7 — Exclusive solo · S
- **Asked by** maelo, 2026-10-11 (not scheduled).
- **Today:** solo is additive. Soloing station A and then station B leaves both
  soloed. `station.setSolo { station, on }` changes one station
  (`ApiCommands::setStationSolo`, `sonobus/Source/ApiCommands.cpp:246`), and
  `docs/control-api.md` says "several solos allowed". The button
  (`console-ui/src/console/components.js:264`) and the `S` key (`app.js:84`) send it
  for that station only.
- **Wanted:** soloing a station makes it **the** solo, clearing every other solo.
  Pressing solo on the station that is already the only solo un-solos it, as today.
- **Design (Claude):** plain click / `S` = exclusive. Keep multi-solo for the rare
  case on a modifier, as in DAWs: ⌘-click / `Shift+S` adds to the solo set. Show it
  in the button's tooltip.
- **Do:** add `exclusive: true` to `station.setSolo` (default false, so existing
  clients and tests keep working). Apply it atomically in the engine (clear the
  others and set this one in one step), so the solo-gated mic routing ("only
  soloed stations hear your mic", "Paused by solo") never passes through a state
  with two or zero solos. Update the mock engine, `docs/control-api.md`, and the
  API test (A then B exclusive → only B soloed; one state patch).
- **Result (Claude, 2026-10-11):** done. `station.setSolo` takes `exclusive`
  (`SonobusAudioProcessor::setRemotePeerSoloExclusive` sets the new solo first,
  then clears the others, so the mic routing never passes through "nothing
  soloed"; at worst two solos for a few microseconds). Click / `S` = exclusive;
  ⌘- or Shift-click / `Shift+S` = add; un-solo stays a plain toggle. Mock engine,
  API doc and tests updated: `console-ui` 15/15, `tests/api/p43p44` 9/9 on the
  real engine (incl. the headless-Chrome Solo click).

### P5.8 — Network (jitter) buffer control in the Console · M
- **Asked by** maelo, 2026-10-11 (not scheduled), for **both** the Mac app and the
  web Console.
- **Today:**
  - The Mac app starts every station with a buffer that is too small. It then
    grows on each drop and can take **up to a minute** of cuts to settle.
  - Stock SonoBus settles faster. Per peer it lets you reset (re-measure) the
    buffer and choose auto or manual with a slider.
  - The Console only **shows** the buffer: Settings → Advanced → "Network buffer:
    Auto (currently N ms)" (`console-ui/src/console/components.js:454,498`).
    `settings.set { networkBuffer }` answers `not_supported`
    (`ApiCommands::setSettings`, `sonobus/Source/ApiCommands.cpp`). Each station
    reports `jitterBufferMs` in state.
- **Engine, already there:**
  - Per peer: `setRemotePeerBufferTime(i, ms)` and
    `setRemotePeerAutoresizeBufferMode(i, mode)`, with the modes
    `Off` (manual) / `Auto-Increase` / `Auto-Full` / `Initial-Auto`.
  - Defaults: `defnetauto` (Auto-Full here) and `defnetbuf`, the starting buffer,
    which is **1 ms** on this Mac. That is why it climbs for so long.
- **Do, API** (`docs/control-api.md`):
  - `station.setBuffer { station, mode: "auto" | "manual", ms? }`
  - `station.resetBuffer { station }`: re-measure, the Initial-Auto pass stock
    SonoBus does, then continue in the chosen mode.
  - `settings.set { networkBuffer: { mode, startMs } }` as the default for new
    stations.
  - State: per station `buffer: { mode, ms }`, next to `jitterBufferMs`.
- **Do, UI (design: Claude):**
  - Per station: Auto / Manual, a ms slider in Manual, and a "Re-measure" action.
    This lives in the station's details, not the main row.
  - Settings → Advanced: the default mode and starting buffer.
  - Keep the existing "Audio may break up, the buffer is growing" hint, and offer
    "Re-measure" there.
- **Quick win to try first:** a saner starting buffer, e.g. 20–40 ms instead of
  1 ms, and Initial-Auto for new stations. Measure how long it takes to settle
  without cuts on the Mac over Wi-Fi, against stock SonoBus, and record it here.
- **Result:**

### P5.9 — Muting a station also stops sending your mic to it · S
- **Asked by** maelo, 2026-10-11 (not scheduled). **Why:** a station you can't hear
  shouldn't hear you either.
- **Today:**
  - Station Mute (`station.setMute`) only silences what you hear from that
    station (the P1.5 gain).
  - The mic send gate is `mic transmitting AND (anything soloed ? this station
    is soloed : this station's Talk is on)`, in the per-peer send loop
    (`sonobus/Source/SonobusPluginProcessor.cpp`, the "P1.5 send gate"). It is
    mirrored by `computeHearsYou()`, which feeds `hearsYou` in state.
  - Mute is not part of either.
- **Do:**
  - Add `&& !muted` (the station's mute) to both the send gate and
    `computeHearsYou()`, so state and audio stay in step.
  - Leave the station's **Talk** flag untouched, as solo already does, so
    un-muting restores whatever Talk was.
  - Mute wins over solo: a muted, soloed station doesn't hear you.
  - Mock engine, `docs/control-api.md` (the mute and `hearsYou` rows), and an API
    test: mute → `hearsYou` false and the send gate closes (peer dump
    `sendGateGain` 0); unmute → back as before. Also an F2 scenario, so the
    station really gets no mic audio while muted.
- **UI (design: Claude):** the talk button gets a held state for this, like
  "Paused by solo": e.g. **"Muted: doesn't hear you"**, not clickable. Its tooltip
  says to un-mute to talk to this station. Check the label against B13 (talk
  button width).
- **Result:**
### P6.1 — Mac shell · M
- **Depends on:** P5.6
- **Do:** replace the JUCE editor (Console role) with a `WebBrowserComponent`
  loading the bundled `console-ui/` build, talking to the engine's local API.
- **Done when:** Mac app works end to end against a VDI (or F2 VDI peer).
- **Result:** merged in `192e5f1f` (WKWebView shell on the engine's own UI,
  `console-ui` bundled in `Contents/Resources`). **Design review (Claude,
  2026-10-10):** launched the real windowed app in an isolated home and captured
  only its own window (`screencapture -l`). It shows the new Console UI
  (phone layout at the default narrow width), not the SonoBus editor, and logs
  "Push-to-talk hotkey: ⌥Space". One copy bug fixed: the empty state read
  "…when their agent joins ." with no group; it now says to connect in Settings.
  Not yet checked: size/position persistence, the busy-port error page.

### P6.5 — Mac global push-to-talk hotkey · S
- **Depends on:** P6.1
- **Do:** register a global hotkey with Carbon `RegisterEventHotKey`
  (press + release events, no Accessibility permission needed). Hold = talk
  while in push-to-talk mode. The key is configurable in settings, per UX4.
- **Done when:** PTT works while another app is in front.
- **Result:**

## P7 — Web Console container

Location: replaces `docker/`. Keep the old files until P7.6.


### P6.6 — Mac window: modern unified title bar · S
- **Asked by** maelo, 2026-10-11 (not scheduled). The Mac app has the classic
  separate title bar. Modern macOS apps (Music, Notes, Slack) run their content
  to the top edge, with the traffic lights sitting in the app's own header.
- **Today:** a JUCE `DocumentWindow` with the native title bar
  (`setUsingNativeTitleBar(true)`, `sonobus/Source/SonoStandaloneFilterWindow.h:864`)
  and the Console UI in a WKWebView (`ConsoleShell.h`).
- **Do (native, in `CrossPlatformUtilsMac.mm`):** on the window's `NSWindow`, set
  `titlebarAppearsTransparent = YES`, `titleVisibility = NSWindowTitleHidden` and add
  `NSWindowStyleMaskFullSizeContentView`, so the web view fills the window and the
  traffic lights float over it. Keep the window title for Mission Control and the
  Window menu.
- **Do (page, design: Claude):** the Console top bar reserves space for the traffic
  lights (macOS shell only: the shell tells the page, e.g. a class on `<html>`)
  and becomes the drag handle. A WKWebView does not move the window on its own:
  a mousedown on empty header area posts a message to native code, which calls
  `-[NSWindow performWindowDragWithEvent:]`, and a double-click there zooms (or
  follows the user's "double-click a title bar" setting). Check full screen, where
  the traffic lights hide, and the light and dark themes.
- **Result:**
### P7.1 — Headless engine in Console role · M
- **Depends on:** P2.1, P1.4
- **Do:** container image builds the engine and runs it headless from a YAML
  config (role console), using a PulseAudio null sink/source as its devices
  (reuse what works in the current `docker/`). Control API bound to the
  container network.
- **Done when:** the container appears as a Console in F2 against VDI peers.
- **Result:** `docker/web-console/` (Dockerfile, supervisor.sh, compose,
  console.example.yaml, README, test/e2e-engine.mjs): a trixie image (arm64,
  ~957 MB) with the Linux engine headless as Console plus the P7.3 gateway on
  one PulseAudio, under tini; the supervisor exits if either process dies. The
  gateway entrypoint cleans a stale pulse dir, so restarts work. Verified:
  health role console, `/` serves the UI, WS token auth, engine on Pulse
  `engine_out`/`engine_in.monitor`; with a second engine container as VDI,
  audio reaches Chrome over WebRTC and Chrome's mic reaches the VDI. **Blocker
  L1: ~10–12 s end-to-end latency** through the engine pair (Pulse alone is
  instant). A host macOS VDI can't reach container UDP under Docker Desktop
  NAT, so use container peers for tests. Next: P7.2 (UI base and token
  hand-off), P7.4 (proxy routes, ICE on the VPN), P7.5, P7.6.
  **amd64 note (2026-10-10):** the image is built and verified on **amd64** as
  well as arm64 (`docker buildx --builder buildkit-priv --platform linux/amd64`
  — a *native* amd64 builder, not qemu). The gateway answers `{"ok": true}` on
  `:8090/rtc/health` and the engine binds `:7070`, but **under qemu emulation on
  this arm64 Mac the engine accepts the TCP connection and then never replies**
  (odd amd64 curl hangs, CPU 0.2% idle; arm64 image on the same host answers
  instantly at ~37% CPU). That is an **emulation artifact, not a product bug** —
  proven by running the same amd64 binary natively on the real amd64 VDI
  (`maelosdebian`), where `GET /api/v1/health` returns
  `{"ok": true, "role": "vdi"}` normally. So do not debug amd64 container hangs
  on an Apple-silicon host; validate amd64 on amd64 hardware.
  **Also (P2.10/P7.1 build hygiene):** the repo had no `.dockerignore`, so every
  root-context build uploaded the whole 2.8 GB repo before compiling (observed
  197 MB and still climbing after 400 s). Added one (commit `d49c5f75`); the
  web-console image now compiles natively in ~4 min instead of stalling.

### P7.3 — WebRTC audio gateway · L
- **Depends on:** P7.1
- **Do:** sidecar process in the container (e.g. Go + pion, or Python +
  aiortc) bridging PulseAudio ↔ one WebRTC peer connection with Opus both
  ways. Signalling over the same HTTPS origin. Measure added latency.
  Use a fixed, configurable UDP port range for ICE and advertise the
  container's VPN address as the host candidate (D13: media doesn't go
  through the proxy).
- **Done when:** browser hears engine output and engine receives browser mic;
  latency recorded in *Result*.
- **Result:** `docker/web-console/gateway/` (trixie, headless PulseAudio, Python +
  GStreamer webrtcbin + aiohttp). Stereo Opus downlink from
  `engine_out.monitor`, mono uplink into null sink `engine_in` (the engine
  records `engine_in.monitor`). `POST /rtc/offer` (non-trickle, one session),
  `/rtc/stats|config|test|health`, fixed UDP range `RTC_UDP_MIN..MAX`
  (40000–40019), `RTC_PUBLIC_IP` rewrite, optional TURN. Verified by
  `test/e2e.mjs` with headless Chrome (440 Hz downlink level, 660 Hz uplink at
  −9 dBFS with the right peak, 0 loss, RTT ~6 ms on loopback); re-run in
  review. **Real latency still to measure on the VPN.** For P7.1: run the
  engine in the same container as user `console`
  (`XDG_RUNTIME_DIR=/tmp/runtime`); for P7.2: proxy `/rtc/*` (plain HTTP)
  to 8090. Risks: no auth on `/rtc/offer` beyond VPN/proxy; Android
  background audio untested.

### P7.2 — Serve UI + proxy API · S
- **Depends on:** P7.1, P5.6
- **Result:** the container already serves the UI and API from the engine
  (`--ui-dir`, P7.1). Added the **browser sign-in**: `POST/GET/DELETE
  /api/v1/session` in `ApiServer`. The cookie is a hex HMAC-SHA1 of the token
  (stateless, invalid after rotation), HttpOnly + SameSite=Strict, Secure
  behind `X-Forwarded-Proto: https`, Origin-checked, constant-time compare, and
  a delay on bad tokens. A WS upgrade with a valid cookie is authenticated.
  UI: the client reports `needs-login` instead of sending an empty token; the
  sign-in card is spec §3.10. Tests: `tests/api/p72.test.mjs` (endpoint, flags,
  CSRF, WS by cookie, sign-out, rotation, and the real UI in headless Chrome:
  card → bad token → good token → ready → survives reload; the cookie is
  invisible to JS). Full API suite green. Proxy notes are in
  `docker/web-console/README.md` and control-api §2.2.

### P7.4 — Publish via maelo's proxy · S
- **Depends on:** P7.2
- **Do (D13):** the container serves plain HTTP on its VPN address; maelo's
  reverse proxy publishes it at `crosspoint.app.lagreca.io` with the public
  `*.app.lagreca.io` Let's Encrypt wildcard, rotated by the proxy, so the app
  never handles certificates. Document the proxy route: WebSocket upgrade for
  the control API and WebRTC signalling, and no buffering. **WebRTC media is
  UDP and bypasses the proxy:** give the gateway (P7.3) a fixed UDP port
  range bound to the container's VPN address and publish it in compose.
  Verify from the phone on the VPN that ICE picks the direct path; if not,
  add a TURN server.
- **Result:** *Not done — recon 2026-10-10.* The proxy is already live and
  reachable from here: `crosspoint.app.lagreca.io` **and** `app.lagreca.io`
  both resolve to **192.168.0.6**, which answers on 443 and currently returns
  **404** for this hostname — i.e. TLS and the wildcard are in place and only
  the vhost/route is missing. `192.168.0.6` has SSH open but rejects our keys
  (`Permission denied (publickey,password)`), so the route must be added by
  maelo (or with credentials). What the route needs (from
  `docker/web-console/docker-compose.yml`, already parameterised):
  `CROSSPOINT_API_ORIGIN=https://crosspoint.app.lagreca.io`;
  `HTTP_BIND` set so 7070/8090 are **not** on a public address;
  `RTC_PUBLIC_IP=` the VPN IP of the container host; `/` + `/api/` → `:7070`
  with **WebSocket upgrade** (`/api/v1/ws`), long `proxy_read_timeout`, no
  buffering, and `X-Forwarded-Proto: https` so the P7.2 session cookie gets
  `Secure`; `/rtc/` → `:8090` (plain HTTP); **UDP 40000–40019 published 1:1 on
  the VPN IP** (media bypasses the proxy).
- **Host problem solved, container running (2026-10-10).** The web Console now
  runs on **`maelosdebian` (192.168.0.71, VPN 10.248.233.7)** under **rootless
  podman 5.4.2** — no `sudo`, which is why this host was usable at all. The
  amd64 image was exported, transferred and `podman load`ed there, and runs as
  a systemd user unit `~/.config/systemd/user/crosspoint-webconsole.service`
  (`active`, `Result=success`, `NRestarts=0`).
  **`--network host` is mandatory:** podman's default **pasta** networking gives
  the container a private IP that peers cannot reach, and AOO's handshake then
  fails with `couldn't establish UDP connection to lagreca|<peer>; timed out
  after 5 seconds` even though the group join succeeds. With host networking the
  peers find each other — no `-p` publishing needed then. **Verified from the
  Mac over the VPN:** `/api/v1/health` → 200 (`role: "console"`), `/` → 200
  (`<title>Crosspoint</title>`), `/rtc/health` → 200. The Console's own state
  showed the VDI as a station:
  `maelosdebian: presence "online", health "clear", latencyMs 30, lossPct 0.1,
  jitterBufferMs 22, agent {input "silent", ...}` — and it listed a genuinely
  unknown peer (`audosrv`) in `unknownPeers`, so P1.6/P1.7 read correctly
  through the whole stack.
- **Still open for P7.4: the public vhost — config now written and validated, so
  what remains is only applying it.** `crosspoint.app.lagreca.io` resolves to
  **192.168.0.6**, which runs **nginx** and answers 443, but returns **404** for
  our hostname (identical to a nonexistent name) while `app.lagreca.io` returns
  **200** — so the wildcard/TLS is fine and only the **vhost + proxy_pass is
  missing**. That host rejects our SSH keys (`maelo`/`root`/`ubuntu`/`admin`),
  so it needs maelo or credentials. The block is ready at
  `docker/web-console/nginx-crosspoint.conf`: **`nginx -t` passes**, and it was
  exercised against the running Console container through the real config (`/`,
  `/api/v1/health` and `/rtc/config` all route correctly). Apply it by copying to
  `sites-available`, setting `UPSTREAM_HOST` (= `10.248.233.7` or `127.0.0.1`)
  and the certificate paths, adding the documented
  `map $http_upgrade $connection_upgrade` to the `http {}` scope, then
  `nginx -t && systemctl reload nginx`. Also set
  `CROSSPOINT_API_ORIGIN=https://crosspoint.app.lagreca.io` on the container and
  make sure **UDP 40000–40019 reaches 10.248.233.7 directly** (media bypasses
  the proxy). Then the on-phone ICE check, which is the last unverified step —
  the gateway is up and answers `/rtc/health`, but no browser has negotiated
  media yet.
- **HTTPS is REQUIRED for the Console to transmit, not just for the PWA
  (found 2026-10-10).** Running the real gateway e2e test
  (`docker/web-console/gateway/test/e2e.mjs`) against the live container over
  plain HTTP on a LAN IP showed the ICE/transport layer working — the peer
  connection reached `connected`, the gateway reported outbound RTP
  (`packets: 708`), and the browser's stats showed an inbound candidate pair
  with `packets: 715` — but the browser logged
  `no mic: TypeError: Cannot read properties of undefined (reading
  'getUserMedia')`. Cause: browsers expose `navigator.mediaDevices` **only in a
  secure context** (HTTPS, or `localhost`), so over `http://<lan-ip>:7070` the
  Console can receive audio but can never capture the mic. So P7.4's HTTPS
  vhost is a **functional requirement for talking back**, not a nicety for the
  PWA — and any test of uplink audio must run on `https://` or `localhost`.
- **Full audio path now VERIFIED end to end (2026-10-10).** Working around the
  secure-context limit with `tools/local-console-proxy.mjs` (serves the Console
  on `http://localhost` and proxies `/` → 7070 and `/rtc/` → 8090 with the
  WebSocket upgrade), and after teaching
  `docker/web-console/gateway/test/e2e.mjs` to drive the container **over ssh
  with podman** (`--remote user@host`, because the Console runs on the VDI, not
  on local docker), the gateway e2e passed **7/7 — `E2E OK`** against the live
  container:
  - downlink: silence before, **440 Hz tone peak 0.353**, silent again after it stops;
  - uplink: Chrome's fake mic arrives at the Console at **660 Hz, −9.0 dBFS**
    with the correct dominant frequency;
  - gateway + browser stats show RTP both ways, **0 packets lost**, jitter
    ~0.001, ~56 ms jitter buffer, RTT ~0.
  So WebRTC audio in **both directions through a real browser** works; what P7.4
  still needs is only the public HTTPS vhost (plus the on-phone check).
  **Two gotchas the run exposed:** (1) the test must use its **own group** — the
  first run failed "silent before tone" because the VDI agent was joined to
  `lagreca` and legitimately transmitting, which is the system working, not a
  fault; (2) over `--remote` the recorded `$$` is the ssh wrapper's PID, so the
  tone outlived its `kill` and the "level falls" check failed until `stopTone`
  also matched the process.
- **Browser-verified, and `/rtc/` on the SAME ORIGIN is load-bearing
  (2026-10-10).** The real Console UI was loaded in headless Chrome from the
  live container: `title` is `Crosspoint` and the full UI renders
  (`Spread`, `Open mic`, `Push to talk`, `Talk to all`, station keys). Two
  findings:
  1. **The UI probes `rtc/config` relative to its own origin**
     (`console-ui/src/lib/rtc.js` → `fetch('rtc/config')`), but the gateway
     listens on **8090** and the engine serves the UI on **7070**. Hitting the
     container directly therefore logs a **404 for `/rtc/config`** and the page
     reports "Audio isn't connected in this browser". Proxying `/rtc/` onto the
     UI origin (I verified with a throwaway reverse proxy) makes it
     `200 {"iceServers":[],"udpRange":[40000,40019],"frameMs":"10"}` and the
     failed-request list becomes **empty**. So the vhost **must** route `/rtc/`
     → `:8090` on the same hostname; publishing 8090 separately is not enough.
  2. The same proxy must forward the **WebSocket upgrade** for `/api/v1/ws`
     (a plain HTTP proxy answers 502 on the handshake), which nginx does with
     the documented `Upgrade`/`Connection` headers.
  **Note on reachability:** partway through, the Mac's WireGuard tunnel dropped
  (`utun8` gone; `10.248.233.7` stopped pinging), so these browser checks ran
  over the **LAN address `192.168.0.71:7070`** instead. Everything bound and
  served fine on both; the VPN path had worked earlier the same session. Worth
  re-confirming over VPN once the tunnel is back, but it is not a deployment
  failure — the container, API and UI were continuously up
  (`Result=success`, `NRestarts=0`).
- **→ See `docs/HANDOFF-vdi-package-web-console.md`** for the deployment as it
  stands, the exact remaining steps (apply `docker/web-console/nginx-crosspoint.conf`
  on 192.168.0.6), and the gotchas (`--network host` is mandatory; HTTPS is
  required for the mic; the test needs its own group; the VDI still runs the
  pre-UI-fix `.deb`).

### P7.5 — Per-user containers, compose + docs · S
- **Depends on:** P7.2, P7.3, P7.4
- **Result:** `docker/web-console/docker-compose.yml` parameterised per user (HTTP_PORT, RTC_HTTP_PORT, RTC_UDP_MIN/MAX mapped 1:1, CONSOLE_CONFIG, HTTP_BIND) + `.env.example`; run each user as `docker compose -p xp-<user> --env-file <user>.env ...`. Validated with `docker compose config` (defaults and env file). Depends on P7.4 only for the real proxy/VPN check (human).

### P7.6 — Remove VNC / Xvfb / raw-PCM bridge · S
- **Depends on:** P7.5
- **Result:** removed the old `docker/` VNC + Xvfb + raw-PCM bridge job (Dockerfile, entrypoint, bridge/, web/, compose, README); root README "Containers" now points at `docker/web-console/` and `docker/aooserver/`. No other files referenced it (git grep).

### P7.7 — Installable PWA · S
- **Depends on:** P7.4 (HTTPS is required for install), UX4 (icon, name, theme colour)
- **Do:** web app manifest (name, icons incl. maskable, `display:
  standalone`, theme/background from tokens), and a service worker that
  caches the UI shell only. Audio and the control API always go to the
  network, and the app shows a clear "can't reach the Console server" state
  when offline or off-VPN. Verify Chrome shows "Install" on macOS and
  Android, and that the installed app opens in its own window. Also try
  iOS Safari "Add to Home Screen" (mic + playback in standalone mode).
- **Also measure on Android:** does audio (both directions) keep flowing with
  the screen off or the app in the background? Record it in *Result*. If it
  fails, that's the trigger to revive P9.2 (D9).
- **Progress (Claude, 2026-10-09):** the client side is done in `console-ui`:
  `sw.js` (shell-only cache, `/api/` and `/rtc/` never cached), manifests and
  icons, registration on secure origins, and the "Can't reach the Console
  server" state. Verified on localhost: SW registered,
  `Page.getInstallabilityErrors` empty, manifest has no errors. **Left:** serve
  it from the container behind the proxy (P7.2/P7.4) and the on-phone checks
  (install, background audio, iOS).
- **Result:**

## P9 — Later


### P7.8 — Audio starts on load · S
- **Asked by** maelo, 2026-10-11 (not scheduled). Today the web Console shows
  "Audio isn't connected in this browser" with a **Start audio** button
  (`console-ui/src/console/components.js:170-186`), and audio starts only after
  the click (`RtcLink.start()`, `console-ui/src/lib/rtc.js`).
- **Why there is a click:** browsers block audible playback until the user has
  interacted with the page (autoplay policy). The mic prompt also has to appear
  at some point.
- **Do:** call `start()` on load. If `audio.play()` is rejected
  (`NotAllowedError`), keep the connection up and resume playback on the first
  click or key press anywhere on the page, instead of asking for a separate
  button. Chrome usually allows playback with sound once the site already has
  mic permission, or once the user interacts with it regularly (its "media
  engagement" score), and an installed PWA (P7.7) usually does too. Measure
  this in Chrome and Safari, record the results here, and keep the banner only
  for what is still blocked.
- **Result:**
### P9.1 — Mac menu-bar panel · M · deferred
- **Depends on:** P6.1, P6.5
- **Do:** a menu-bar item showing station activity, talk toggles and mic
  state, as a compact view of the same Console UI. Needs a UX addendum first.
- **Result:**

### P9.2 — Native Android Console (APK) · broken down into A1–A14
The phone runs the whole engine, as stock SonoBus does on Android and as the
Mac shell (P6.1) does: the audio engine (AOO P2P, Opus) runs in the app, the
control API listens on `127.0.0.1:7070`, and a web view shows the bundled
Console UI from it. Audio stays native and goes straight to the VDIs, with no
container hop. This replaces the old P6.2–P6.4 cards.

**State on 2026-10-11 (why the cards look like this):**
- `sonobus/mobile/SonoBusMobile.jucer` (Projucer, JUCE 7.0.8) still describes
  stock SonoBus. It is missing every source added since the fork: `ApiServer`,
  `ApiSha1`, `ApiCommands`, `EngineState`, `AgentConnect`, `AppIdentity`,
  `GlobalHotkey*`, `ConsoleShell.h`. But `SonoStandaloneFilterApp.cpp` includes
  them unconditionally, so **the Android build does not link today**.
- It sets `JUCE_WEB_BROWSER="0"`, `android:hardwareAccelerated="false"` (a
  WebView needs it on), `applicationId com.sonosaurus.sonobus`, and a release
  keystore at `~/.android/sonouploadnew.jks` (upstream's).
- `Builds/Android/local.properties` is committed with `sdk.dir=/Users/jesse/...`.
- `ConsoleShell.h` only builds when `JUCE_MAC && JUCE_WEB_BROWSER`. The UI path
  (`apiConfig.uiDir`) only has a Mac bundle fallback. `CROSSPOINT_HAS_CONFIG` is
  0 on Android, so there is no YAML on the phone (by design: the Console's
  onboarding flow connects it).
- Stock SonoBus already has `RECORD_AUDIO`, `FOREGROUND_SERVICE` and a
  `SonoBusService` (`sonobus/Source/android/java`), targetSdk 33.
- This Mac has no JDK, Android SDK, NDK or Projucer installed.
- `console-ui` only uses WebRTC when the server offers `rtc/config` (the web
  container). Against an engine's own API it uses no browser audio, so the
  same UI works in the Android web view as in the Mac shell.

**Lanes.** Each lane is a chain one agent can own. Lanes run in parallel once
their entry dependency is `done`.

| Lane | Tasks, in order | Starts after |
|---|---|---|
| **D** design (Claude only) | A2 (**done**), then the design reviews of A7, A9, A10, A12, A14 | now |
| **B** build & packaging | A1 → A3 → A4 → A11 | now |
| **E** engine on the phone | A5 → A8 | A3 |
| **S** shell & UI | A6 → A7 | A3 (A7 also needs A5, A2) |
| **P** Android platform (Java, manifest) | A10 → A14 → A9 → A12 | A4 + A2 (A14 also needs A5; A9 and A12 also need A7) |
| **H** human on a real phone | A13 | everything else |

Critical path: A1 → A3 → A5 → A7 → A9 → A13. A3 is the funnel: nothing except A2
can start until the current engine builds for Android.

**First APK, as soon as possible (D9, maelo 2026-10-12):** A1 → A3 → (A5 and
A6 in parallel) → A7 gives a debug APK with the Console UI driving the
phone's own engine, installable by `adb install`. Stock SonoBus already asks
for the mic at launch and has a foreground service, so listening and talking
should work with the screen on before A9 and A10 land. Do **A4** right after
A3 if stock SonoBus is installed on the phone: until then both apps share
`com.sonosaurus.sonobus`, and the install fails. A8–A14 harden it after that.
Agents should take these first.

**Rules for this phase:**
- **The `.jucer` is a hotspot.** A3, A4, A6, A7, A10 and A11 all edit it. Keep
  each task's `.jucer` change small and rebase before merging. **Never
  hand-merge files under `Builds/Android/`.** Resolve the `.jucer`, then
  regenerate them with `scripts/build-android.sh --resave` (A1).
- **No secrets in the repo** (it's public). Keystores and passwords come from
  the environment or `~/.gradle/gradle.properties`.
- **Verify on the emulator** (arm64 system image) with `adb`. Real-phone
  checks are A13. To capture the UI for design review, use
  `adb exec-out screencap -p`, never a desktop capture.
- `SonoStandaloneFilterApp.cpp` is touched by A5 (API startup) and A6 (UI
  path). They are different blocks; rebase rather than run them in one PR.

#### A1 — Android toolchain + Projucer · M · any · lane B
- **Depends on:** — · **Touches:** `scripts/` (new `setup-android.sh`,
  `build-projucer.sh`), `docs/ANDROID.md` (new), `.gitignore`
- **Do:** script the prerequisites so any agent can reproduce them. On macOS:
  JDK 17 (AGP 7.3.0 needs 11–17), Android cmdline-tools, platform 33, build
  tools, NDK `25.2.9519653` (what `app/build.gradle` pins), and an arm64
  emulator image. Build the Projucer **from `juce/extras/Projucer`** (7.0.8, it
  must match the vendored JUCE), and use it headless with `--resave`. Add a
  `scripts/build-android.sh --resave` entry point. Write `docs/ANDROID.md`
  (what's installed, where, how to start the emulator).
- **Done when:** `--resave` of the current `.jucer` changes nothing under
  `Builds/Android/` except expected noise (record which), `./gradlew --version`
  runs, and the emulator boots.
- **Result (Claude, 2026-10-12)**: scripts written and the Projucer built; the
toolchain install itself has not been run (no JDK/SDK/NDK on this Mac).
- `scripts/setup-android.sh` — installs Temurin 17 (brew), cmdline-tools,
  platform-tools, platform 33, build-tools 33.0.2, NDK 25.2.9519653, CMake
  3.22.1, the android-33 arm64 system image and an AVD `crosspoint-33`.
  `--check` reports without changing anything; it currently reports everything
  missing, as expected.
- `scripts/build-projucer.sh` — builds the Projucer from `juce/`. **Verified**:
  `JUCE_BUILD_EXTRAS=ON` on JUCE's own CMake project (extras/Projucer alone has
  no `juce_add_gui_app`), linked at
  `build/projucer/.../Projucer.app/Contents/MacOS/Projucer`, reports 1.7.2.
- `scripts/build-android.sh` — `--resave` / `--install` / `--emulator` /
  `--release` / `--clean`. **`--resave` verified idempotent**: two passes
  produce byte-identical trees.
- `docs/ANDROID.md` — what is installed where, the resave-noise list, emulator
  and `adb` recipes, and the gotchas.
- Recorded resave noise (discard it, per the doc): `Builds/iOS/**` (line-ending
  churn in the `.plist`/`.json` files plus a rewritten `project.pbxproj`),
  `JucePluginDefines.h` (ARA archive id tracks the `.jucer` version),
  `gradlew.bat` (CRLF). The ARA id moved 1.7.1 → 1.7.2, which the checked-in
  file had simply never caught up with.
- `local.properties` is now git-ignored and rewritten from `$ANDROID_HOME` on
  every resave (that is A3's "stop tracking" item, done here because the two
  files cannot be separated).
- **Toolchain installed and verified (Claude, 2026-10-12)**: `scripts/setup-android.sh`
  run for real, `--check` all green: JDK 17, cmdline-tools, platform 33,
  build-tools 33.0.2, NDK 25.2.9519653, CMake 3.22.1, emulator, arm64 image,
  AVD `crosspoint-33`. `./gradlew` (7.5.1, downloaded on first run) works and
  the emulator boots (`scripts/build-android.sh --emulator`).
  - **Surprise 1:** `brew install --cask temurin@17` needs `sudo` (a package
    installer), which a non-interactive shell cannot give. The script now uses
    the `openjdk@17` formula (no sudo, keg-only under
    `/opt/homebrew/opt/openjdk@17`), which `find_jdk_home` already looked for.
    Gradle and the build script find it without `JAVA_HOME`.
  - **Surprise 2:** the Homebrew cask/formula path needs no manual PATH change;
    `build-android.sh` exports JAVA_HOME/ANDROID_HOME itself.
  - `build-android.sh` bugs fixed: launch hint now reads package and activity
    from `aapt dump badging` (no hard-coded id), BSD-safe capitalisation, and the
    APK is found with `ls -t` instead of `find -newermt`.
  - Download sizes: NDK + SDK pieces about 3 GB under `~/Library/Android/sdk`.

#### A2 — UX addendum: Android-only states · S · design · lane D
- **Depends on:** UX4 · **Touches:** `docs/design/`
- **Do:** spec what the phone app needs that the PWA and Mac don't: the mic
  permission ask and the "denied" state, the foreground-service notification
  (text, actions such as mute mic and disconnect), push-to-talk from a headset
  button or the notification (A12), the engine-failed error page on a phone,
  launch/splash, and system bars and safe areas around the phone layout (P5.5).
- **Done when:** spec merged; A7, A9, A10 and A12 can build to it without
  inventing anything.
- **Result:** done (Claude, 2026-10-11). Spec: `docs/design/android.md`.
  Prototype: `design/prototypes/android/` (`?screen=board` shows them all).
  Notification icon: `design/icon/crosspoint-notification.svg`. Decisions:
  - swipe-away from Recents quits; Back and Home never disconnect
  - the mic permission is asked on the first connect, with no pre-prompt
  - the notification title mirrors the live band, with Mic on/off and
    Disconnect actions
  - the headset button toggles the mic in open-mic mode only; nothing in PTT
  - a phone call turns the mic off, and **it stays off after the call**
  - earcons for mic changes made without looking
  - problem rows at the top of Settings
  - touch copy ("Tap…", no "Space") for any coarse pointer, the PWA included

  It found work no card covered, now **A14** (phone calls + earcons). API
  additions are in android.md §10 and control-api §5.5; each implementing task
  adds its fields to `console-ui/src/api/mock.js` too (control-api §6), so its
  design review can be screenshotted.

#### A3 — Debug APK builds the current engine · L · any · lane B
- **Depends on:** A1 · **Touches:** `sonobus/mobile/SonoBusMobile.jucer`,
  `sonobus/mobile/Builds/Android/`, `scripts/build-android.sh`, small
  `#if JUCE_ANDROID` fixes in `sonobus/Source/`
- **Do:** add the missing sources (listed above) to the `.jucer`. Make them
  compile with the NDK; `GlobalHotkey` should become a no-op on Android, and
  PipeWire code is already Linux-only. Keep `Config`/rapidyaml out
  (`CROSSPOINT_HAS_CONFIG` stays 0). Stop tracking `local.properties` and
  generate it from `$ANDROID_HOME`. Reconcile the two `ndkVersion` values in
  `app/build.gradle`. Debug builds must not need the upstream keystore.
  `scripts/build-android.sh` = resave + `gradlew assembleDebug`, printing the
  APK path. The UI stays the stock JUCE editor in this task.
- **Done when:** one command produces a debug APK that installs on the
  emulator, launches, and joins a group on `aoo.sonobus.net` with the stock
  UI. Desktop build (F1) and F2 are unchanged.
- **Result (Claude, 2026-10-12)**: the `.jucer` and the Android compile fixes are
in; the link is unproven because there is no NDK on this machine.
- Added the 16 source files the engine includes but the mobile `.jucer` never
  listed (plus the `crosspoint-ui.zip` resource, A6): `AgentConnect`,
  `ApiCommands`, `ApiServer`, `ApiSha1.h`, `AppIdentity.h`, `ConsoleShell.h`,
  `EngineState`, `GlobalHotkey`, `GlobalHotkeyCarbon` (`.cpp`/`.h` pairs where
  they exist) and `ReverbView.h`. Found by walking the include
  closure of the files the `.jucer` already listed, not by eyeballing: 118
  source entries now, and the closure is complete.
- **Found and fixed a real Android blocker:** the NDK defines `__linux__`, so
  the P2.11 PipeWire branches (`#if defined(__linux__)`) would have compiled on
  Android and failed to link (no ALSA headers). Those guards are now
  `&& !defined(__ANDROID__)` in `PipeWireDevices.{h,cpp}`, `EngineState.cpp`
  and `ApiCommands.cpp` (6 sites). JUCE's own `JUCE_LINUX` is safe — JUCE
  checks `JUCE_ANDROID` first (TargetPlatform.h:62), so `JUCE_LINUX` stays 0.
- `Config`/rapidyaml are **not** added (`CROSSPOINT_HAS_CONFIG` is 0 on Android), the
  PipeWire files are not listed, and the iOS-only `.mm` files that the Projucer
  already emitted for Android are untouched.
- Rejected a `release`-only keystore: the Projucer points **every** build type
  at `signingConfigs.juceSigning` (jucer_ProjectExport_Android.h:848), so the
  `.jucer` now adds an `androidCustomAppBuildGradleContent` override that
  re-points **debug** at Gradle's own debug key. Release still needs a real
  keystore, which is A11.
- `ndkVersion` reconciled: the Projucer emits its own pinned
  `25.2.9519653`, and the stale `ndkVersion "22.0.7026061"` line the `.jucer`
  appended is gone.
- `local.properties` untracked (it held `/Users/jesse/...`) and regenerated.
- Desktop unbroken: `scripts/build-desktop.sh --app-only` is green.
- **Verified on the emulator (Claude, 2026-10-12)** with `scripts/build-android.sh
  --emulator`: resave, `assembleDebug`, install. `BUILD SUCCESSFUL`; the first
  cold build took about 2m15s of Gradle (native compile of all four ABIs:
  arm64-v8a, armeabi-v7a, x86, x86_64; **all ABIs left on**), incremental
  rebuilds about 50s. Debug APK is 194 MB (four ABIs, unstripped, with the
  Console zip). Package `com.sonosaurus.sonobus`, activity
  `com.sonosaurus.sonobus.SonoBusActivity`, versionName 1.7.2.
  - **Zero C++ compile or link errors** from the 16 newly listed sources: the
    previous agent's include-closure work and the `__ANDROID__` guards were enough.
    The failures were all Gradle/Java/resources side:
    1. `build.gradle` evaluated `${sonoupload_keystore_password}` etc. at
       configuration time, so even debug failed ("unknown property"). The `.jucer`
       now emits `${findProperty('sonoupload_key...') ?: ''}`; A11 passes real
       values with `-P` or `~/.gradle/gradle.properties`.
    2. The manifest referenced `@mipmap/ic_launcher[_round]`, which do not exist in
       the repo (the Projucer only generates `drawable-*/icon.png`). The `.jucer`
       manifest now says `@drawable/icon`. A4 owns real icons.
    3. `SonoBusService.java` used `R.drawable.ic_notify` (missing). Interim:
       `R.drawable.icon`; A4 supplies the notification icon.
  - **Runtime bugs found and fixed on the emulator (not compile errors):**
    - JUCE's Android WebView calls `pageLoadHadNetworkError` for **every
      subresource HTTP error**. The Console probes `rtc/config`, a 404 on the
      phone (no WebRTC gateway), so the shell replaced a healthy page with "The
      Console didn't load. ... Not Found". `ConsoleShell.h` now ignores non-`net::`
      errors on Android. `ApiServer` also serves `/favicon.ico` (alias of
      `icons/crosspoint-32.png`), which the WebView also requested.
    - **Back crashed the app** (SIGABRT, "JNI ERROR: jobject is an invalid local
      reference"): JUCE 7.0.8's `Process::hide()` builds its activity LocalRef
      from a deleted temporary. `backButtonPressed()` now calls
      `Activity.moveTaskToBack(true)` itself. After the fix Back goes to the
      launcher, the process keeps its pid, and the group connection stays
      `connected` (checked 30+ seconds after Back).
  - **Launch:** `adb shell am start -n com.sonosaurus.sonobus/.SonoBusActivity`.
    `logcat`: `JUCE v7.0.8`, `SonoBusActivity C++ constructed`, Oboe streams open,
    `Bound system chosen udp port`. First launch shows the system mic permission
    dialog ("Allow SonoBus to record audio?", A9 will own that flow).
  - **Console renders without the GL context** (attachGL skipped while the web
    shell shows): header "Crosspoint • Connected", mic button, Open mic / Push to
    talk, output slider, all in the WebView. Screens (device-only `adb exec-out
    screencap`): not committed. **Layout problem for A7:** a ~110px empty band at
    the top (the Android status bar inset is applied twice, or the safe-area
    padding is wrong) and the station list area is clipped behind the mic panel.
  - **API:** `adb forward tcp:7070 tcp:7070`; `GET /` returns the Console
    `index.html` (200), `/favicon.ico` 200, state reports `self.kind: "android"`.
  - **Group join: verified through the control API only**, not through the stock
    UI: a WebSocket `connection.connect` to `aoo.sonobus.net:10998`, group
    `crosspoint-a3-test`, went `connecting` then `connected` (patch stream),
    persisted after Back. I did not drive the JUCE editor (the stock UI is
    hidden behind the web shell by A7), so "joins a group with the stock UI" is
    read as "the engine joins a group". No second peer joined, so no audio was
    heard.
  - Desktop `scripts/build-desktop.sh --app-only` is green after all changes.

#### A4 — Crosspoint identity on Android · S · any · lane B
- **Depends on:** A3 · **Touches:** `.jucer`, `sonobus/Source/android/java/`,
  `Builds/Android/app/src/main/res/`
- **Do:** apply P3.9/P3.10 to Android: applicationId
  `io.lagreca.app.crosspoint`, label "Crosspoint", launcher and adaptive icons
  from the P3.10 icon, and a `crosspoint://` intent filter. Rename the Java
  package `com.sonosaurus.sonobus` (`SonoBusActivity`, `SonoBusService`) and the
  `JUCE_ANDROID_ACTIVITY_OVERRIDE_*` defines to match.
- **Done when:** Crosspoint installs **next to** stock SonoBus on the same
  emulator, and both run.
- **Result (Claude sonnet, 2026-10-12)**: done. Checked on emulator-5560
  (`aapt dump badging`: package `io.lagreca.app.crosspoint`, label `Crosspoint`,
  icon `res/drawable-anydpi-v26/icon.xml`, activity
  `io.lagreca.app.crosspoint.SonoBusActivity`); installs, launches, and the mic
  dialog reads "Allow **Crosspoint** to record audio?". No stock SonoBus APK was
  built; the different applicationId is what makes side by side work.
  - Java package renamed (`sonobus/Source/android/java/io/lagreca/app/crosspoint/`),
    class names kept. The `.jucer` changes: `bundleIdentifier` on the project
    (the Projucer takes applicationId, manifest package and the content-provider
    authority from it; it also changes the iOS bundle id, for P9.3),
    `JUCE_ANDROID_ACTIVITY_OVERRIDE_CLASSNAME/CLASSPATH`,
    `androidActivitySubClassName`, the manifest `package`, a **literal**
    `android:label="Crosspoint"` (the Projucer's `app_name` string is the project
    name, "SonoBus", and renaming the project would touch the plugin names), and
    a `crosspoint://` VIEW/BROWSABLE intent filter. **Surprise:** as soon as the
    custom manifest has any `<intent-filter>` the Projucer stops adding
    MAIN/LAUNCHER, so the `.jucer` now declares both. The filter only makes the
    link resolve; nothing handles the URL yet (`appNewIntent`).
  - Icons: `smallIcon`/`bigIcon` now point at `design/icon/crosspoint-256.png` /
    `-1024.png` (the Projucer writes the legacy `drawable-*/icon.png`). Adaptive
    icon = `res/drawable-anydpi-v26/icon.xml` (same resource name, so
    `@drawable/icon` resolves to it on API 26+) with background `#12161a`
    (`values/ic_launcher_background.xml`) and the `pwa-maskable-512.png`
    foreground (`drawable-nodpi/ic_launcher_foreground.png`). Those four files
    are hand-added under `Builds/Android/app/src/main/res/` and survive a resave
    (the Projucer only writes `drawable-*dpi/icon.png`); `sonobus/.gitignore`
    ignores `mobile/Builds`, so new files there need `git add -f`.
  - `ic_stat_crosspoint` (24dp vector from `crosspoint-notification.svg`) is the
    `SonoBusService` small icon. **I removed the service's large icon**:
    `BitmapFactory.decodeResource(R.drawable.icon)` returns null for an adaptive
    XML icon on API 26+. A10 owns the notification content.
  - Resave noise discarded as usual (iOS, `JucePluginDefines.h`, `gradlew.bat`).
    Debug APK 195 MB.
  - Not done: monochrome (themed) icon layer.

#### A5 — Control API on the phone · M · any · lane E
- **Depends on:** A3 · **Touches:** `sonobus/Source/SonoStandaloneFilterApp.cpp`
  (API startup block), `EngineState.cpp`/`ApiCommands.cpp` only for Android fixes
- **Do:** start the API on Android with no CLI: Console role, loopback
  `127.0.0.1:7070`, no token. Keep it running while the activity is paused
  (JUCE `suspended()`), since the service in A10 keeps audio alive. Check
  everything `EngineState` reports on Android: audio device names (Oboe or
  OpenSL), sample rate and buffer, and where settings persist. Server, group
  and name set through the Console's connect flow must survive an app restart.
  Report `self.kind: "android"` in state and peer info, so agents show
  "Console · Android" (`docs/design/android.md` §10).
- **Done when:** with `adb forward tcp:7070 tcp:7070`, the `console-ui` smoke
  test against the real API passes from the Mac, and the connect command joins
  a group from the emulator.
- **Result (Claude, 2026-10-12)**: the Android API defaults and `self.kind` are in;
the emulator check is pending.
- `applyApiOptions()` sets Console role, loopback `127.0.0.1`, port 7070 and no
  token on Android. Android has no command line, so there is nothing to parse;
  it is a compile-time branch, not a runtime one.
- `self.kind: "android"` (`SonobusPluginProcessor::consoleKindString`) — this
  is what makes agents show "Console · Android". Adding it to the *filter*
  matters too: `EngineState` mapped anything but `mac`/`web` to `"other"`, so
  without that change the field would have been reported and then dropped.
  `consoles[].kind` now passes `android` through (control-api §5.5).
- The API already outlives `paused()`: `suspended()`/`resumed()` only touch the
  audio device and the foreground service, never the server.
- Not verified: `adb forward` + smoke test against a real phone engine.
- **Verified on the emulator (Claude, 2026-10-12)**, `adb -s emulator-5562 forward
  tcp:7071 tcp:7070`, `node console-ui/scripts/smoke-android.mjs` (new; the old
  `smoke.mjs` only drives the in-page mock, so it cannot target a phone):
  hello role `console`, `self.kind: "android"`, `connection.connect` to
  `aoo.sonobus.net:10998` / a throwaway group joined. **Force-stop and relaunch**:
  server, group and name (`Phone A5`) were back, and the engine rejoined by itself
  ("Console: rejoining the most recent server and group"); settings live in
  `<app data>/.SonoBus/SonoBus.settings`.
  - Engine facts on Android (Oboe/AAudio on the 33 arm64 image): devices are one input
    "Phone mic" (`System Default (Input)`) and one output "Phone speaker"
    (`System Default (Output)`), **48000 Hz**, buffer 960 frames (the burst; Oboe
    buffer-size 1056 first, then 960), `networkBuffer {mode: auto}`. `EngineState`
    has no sample-rate or buffer field; those came from logcat.
  - After an explicit Disconnect the reason reads "Connection to the server was lost"
    (the mock says "Disconnected"); cosmetic, left for the engine owner.
  - The default "Your name" is the Linux uid (`u0_a176`), not the device name
    (design android.md section 2): A4/identity.

#### A6 — Console UI bundled in the APK · S · any · lane S
- **Depends on:** A3 · **Touches:** `.jucer` (resource), `scripts/build-android.sh`,
  `SonoStandaloneFilterApp.cpp` (the `uiDir` fallback block)
- **Do:** APK assets are not files the `ApiServer` can serve. At build time, zip
  `console-ui/` (raw, as the Mac bundle copies it) into JUCE BinaryData. On
  launch, extract it to `userApplicationDataDirectory/console-ui` when its
  build stamp differs, and use that as `apiConfig.uiDir`. Write it so iOS (P9.3)
  can reuse it.
- **Done when:** a fresh install and an upgrade both serve the current
  `index.html` from `http://127.0.0.1:7070/` (via `adb forward`), and a stale
  UI is never served after an upgrade (check the service-worker cache too).
- **Result (Claude, 2026-10-12)**: implemented and **verified on the host** by
`scripts/test-android-ui.sh` (10/10).
- `scripts/pack-console-ui.sh` zips `console-ui/` (HTML, `src/`, `icons/`, the
  manifests and `sw.js`; not the README, tests or build scripts) to
  `sonobus/Resources/crosspoint-ui.zip`, with `console-ui/` as the top level so
  the unpacked directory is a ready web root. Deterministic (`-X -D`).
- The zip is a **build artifact** (git-ignored) and must exist before a resave:
  the Projucer refuses to save a project whose listed resource is missing
  (tested). `build-android.sh` therefore packs before resaving.
- `.jucer` lists it as `resource="1"`; the Projucer mangles the name to
  `BinaryData::crosspointui_zip` (rule: spaces/dots → `_`, other punctuation
  dropped, in `makeBinaryDataIdentifierName`). The extractor uses that symbol
  and the test would catch a rename.
- `crosspoint::extractBundledConsoleUi()` (shared header
  `sonobus/Source/BundledConsoleUi.h`, used by the app and by the host test, so the
  test runs the code the phone runs) unpacks to
  `userApplicationDataDirectory/console-ui`, guarded by an
  MD5+size stamp file so a relaunch is a no-op and an **upgrade** replaces a
  stale UI (`console-ui.stamp`, sibling of the extracted folder).
- iOS-ready by construction: the block is `#if JUCE_ANDROID || JUCE_IOS` and
  takes no Android API, which is what P9.3 needs.
- The host test caught a real bug while being written: `uncompressTo` needs its
  target's **parent** to exist, so the code extracts into the app-data dir
  rather than into a not-yet-created subdirectory.
- Smoke-tested against the real API? No: A6's "serve the current index.html"
  check needs the phone (or at least a running Android API).
- **Verified on the emulator (Claude, 2026-10-12)**: a fresh install (uninstall,
  `adb install`) served `index.html`, `sw.js`, `src/console/app.js` and
  `components.js` byte-identical to `console-ui/` (checked by `smoke-android.mjs`).
  **Upgrade**: a marker comment appended to `index.html`, `adb install -r`, relaunch:
  `GET /index.html` returned the marker, `console-ui.stamp` changed, and the service
  worker's cache (`crosspoint-shell-v2`) held the new `index.html` too. The worker is
  network-first for the shell, so it cannot serve a stale UI while the engine is up.
  The marker was removed again.

#### A7 — Android web shell · M · any + design review · lane S
- **Depends on:** A5, A6, A2 · **Touches:** `sonobus/Source/ConsoleShell.h`,
  `SonoStandaloneFilterWindow.h`, `.jucer` (`JUCE_WEB_BROWSER=1`, manifest XML),
  `res/xml/network_security_config.xml` (new)
- **Do:** extend `ConsoleShellView` to `JUCE_ANDROID` (JUCE's Android
  `WebBrowserComponent`) so the window shows the Console instead of the JUCE
  editor, as on the Mac. Turn on `hardwareAccelerated` (application and
  activity). Android 9+ blocks cleartext HTTP, so allow it for `127.0.0.1`
  **only**. Build to `docs/design/android.md` §2: splash with no white frame,
  system bars, portrait on phones, Back, keyboard resize, font scale,
  haptics, and the touch copy (it's in `console-ui`, so the PWA gets it
  too). Also §8: the native error page on a phone.
- **Done when:** the emulator shows the phone layout (P5.5) driving the real
  engine (connect, stations, talk, meters, no page errors), and Claude has
  approved `adb` screenshots against UX4 + A2.
- **Result (Claude, 2026-10-12)**: the shell is extended to Android; screenshots
for design review are still missing (no emulator yet).
- `ConsoleShell.h` now builds for `(JUCE_MAC || JUCE_ANDROID) && JUCE_WEB_BROWSER`.
  JUCE 7.0.8 does ship `juce_WebBrowserComponent_android.cpp`, and every API
  the file uses (`Options`, `withBackend`, `goToURL`, `pageAboutToLoad`,
  `pageFinishedLoading`, `pageLoadHadNetworkError`,
  `newWindowAttemptingToLoad`) exists on Android — checked in the JUCE source,
  not assumed. Its `JuceWebView` Java classes are embedded as bytecode, so no
  extra `java.srcDirs` entry is needed.
- `.jucer`: `JUCE_WEB_BROWSER` 0 → 1, and the manifest's
  `hardwareAccelerated` false → true (application and activity).
- Android 9+ blocks cleartext, so `network_security_config.xml` permits it for
  `127.0.0.1`/`localhost` only, wired via `android:networkSecurityConfig`.
  `res/xml/` survives a resave (tested), and the file is force-added because
  `sonobus/.gitignore` ignores the whole generated `mobile/Builds` tree.
- `showWebShell()` gets an Android branch: the window is already fullscreen, so
  no geometry restore and no resize limits (the Mac path is untouched).
- Error page built to android.md §8: safe-area-aware, 24 px sides, content from
  ¼ height, one full-width 48 px accent button, and the phone copy ("Crosspoint
  couldn't start" / "The Console didn't load"). The Mac keeps its reviewed P6.1
  strings, colours and layout exactly.
- Touch copy (android.md §2) in `console-ui`, so the PWA gets it too, via
  `(pointer: coarse)`: "Tap to turn off" / "Tap to open mic" / "Press and hold"
  and the PTT live-band line without "or Space".
- Android mock scenarios for the design review (control-api §6 asks each
  implementing task to add its fields): `android`, `android-blocked`,
  `android-call`, `android-aftercall`, `android-echo`, with
  `self.kind: "android"` and the `platform` block. `console-ui` tests 15/15.
- Back gesture (§2): JUCE's default `backButtonPressed()` returns false and the
  peer then `finish()`es the activity, so the first version would have quit the
  app. Review fix: `SonobusStandaloneFilterApp::backButtonPressed()` (Android
  only) calls `Process::hide()` (= `Activity.moveTaskToBack(true)`, HOME-intent
  fallback) and returns true: Back goes to the background like Home, the
  foreground service keeps the audio, nothing disconnects. **Limitation:** the
  page never sees Back. JUCE 7.0.8's `WebBrowserComponent` has no
  `evaluateJavascript`, and the native side cannot tell whether the settings sheet
  is open, so Back does not close the sheet (§2 asks for that). Closing it needs
  either a JNI bridge into the WebView (`evaluateJavascript` through
  `JuceWebView`) or the page pushing a history entry when the sheet opens; left
  for the emulator pass. Unverified until then.
- Review fix, OpenGL: `attachGL()` (the only place an `OpenGLContext` is attached
  on Android; `resumed()` and the 500 ms foreground-service timer never call it)
  now returns early once the web shell is shown (`webShellShown`), so the WebView
  is not inside a GL-attached window (black or hidden WebView risk). The context is
  still attached for `--legacy-ui`/when the shell is not used. `detachGL()` is
  unchanged (it only acts if attached). Unverified until the emulator.
- Review fix, error copy: on Android `showConsoleShell()` uses the §8 strings
  verbatim: API did not start -> "Its audio engine couldn't open its local
  connection (port {port} is in use by another app). Close that app or restart the
  phone, then try again." with a **Try again** button that re-runs
  `startApiServer()` (and `startEngineState()`) and, if that worked, loads the
  page; UI files missing -> title "This copy of Crosspoint is incomplete", body
  "Some of its files are missing. Reinstall Crosspoint.", no button (checked
  first: no retry fixes it). `ConsoleShellView` takes a `ConsoleShellFatal`
  {title, buttonLabel, onRetry}; the Mac passes none, so its P6.1 strings, layout
  and behaviour are unchanged (the shared default-argument path only; desktop
  build passes). The Android branches are not compiled here.
- Agent page: `consoles[].kind: "android"` shows as "Console · Android" (mock
  `talking` scenario has one).
**Not done:** the emulator screenshots (100% and 130% font scale, cold-start
video) that this task's *Done when* requires, and A9/A10/A14's states.
- **Emulator pass (Claude, 2026-10-12), awaiting design review.** Merged with
  `android-apk` (A4 package, A8, A11, B15). Fixes, each checked on `crosspoint-33-s`:
  - **Layout.** Two causes, neither the one first suspected. (1) `setShell()` sized the
    content from the window's pre-fullscreen size: it now takes the real size from
    `resized()`. (2) Android's WebView lays `html` out **0 px tall** (`height: 100%` on
    `html, body, .app` resolved to 0, so the page shrank to its content and the You bar
    sat under the top bar): `.app` is now `position: fixed; inset: 0` (same on
    desktop). The shell no longer subtracts the safe-area insets (the window already
    sits between the bars). Measured through the WebView's devtools: `innerHeight`
    842 and `.app` fills it. Keyboard up: `innerHeight` 529 and the focused field stays in view
    (`adjustResize` in the activity). Rotation was not exercised (phone: locked).
  - **System bars.** Theme `CrosspointTheme` (`res/values/styles.xml`, window background and
    Android 12 splash `#0b0d0f`, bars `#0d1013`, light icons). `SonoBusActivity.onCreate`
    sets the navigation bar black afterwards, so `ConsoleShellAndroid.h` sets both bar colours
    again from JNI once the shell is up. Pixel check: status and navigation bar are
    (13,16,19) = `#0d1013`.
  - **No white frame.** The window and splash are `#0b0d0f`, and the WebView's own
    background is set through JNI as soon as it exists. A 25 s `screenrecord` of a cold
    start (`cold-start.mp4`, contact sheet `cold-start-contact.png`) shows home screen,
    dark splash with the icon, the page. No white frame.
  - **Portrait on phones**: `Desktop::setOrientationsEnabled(upright)` when the smallest
    width < 600 dp, all orientations otherwise. Set at runtime; not yet rotated on a tablet image.
  - **Font scale.** At 100%/130% the page follows it. Found on the way: changing the
    font size while the app ran **killed the process** (the activity was recreated:
    `fontScale` was not in `configChanges`). It now is, with `density|uiMode|
    screenLayout|smallestScreenSize`, and a one-second JNI poll sets the WebView's
    `textZoom` from the configuration (a running WebView does not follow it). The
    process survives 1.0 -> 1.3 -> 1.0 (same pid). **The native error page does not scale**
    with the font (JUCE labels use pixels); the 130% capture shows it.
  - **Touch copy.** `touchFirst(st)` = `(pointer: coarse)` OR `self.kind === 'android'`;
    the emulator now says "Tap to turn off". Test added, `npm test` 18/18.
  - **Haptics.** Native, not `navigator.vibrate`: the page navigates a hidden frame to
    `crosspoint-shell://haptic/start|stop`, `pageAboutToLoad` swallows it and calls
    `View.performHapticFeedback` (KEYBOARD_TAP when your voice starts, CLOCK_TICK when it
    stops). That honours the system touch-feedback setting and needs no `VIBRATE`
    permission. On the emulator the vibrator service logged a TOUCH click for mic on,
    a texture tick for mic off, and `ignored_for_settings` with
    `haptic_feedback_enabled 0`. Outside the shell the page falls back to `navigator.vibrate`.
  - **Back.** `javascript:` through JUCE's `goToURL()` works in 7.0.8 (it is `loadUrl`).
    `backButtonPressed()` calls `window.crosspointBack()`: with the settings sheet open
    it closes it; otherwise the page goes to `crosspoint-shell://background` and the shell
    calls `moveTaskToBack` (same pid after, never disconnects). The native error page
    goes to the back directly. Verified with `adb shell input keyevent KEYCODE_BACK` in
    both cases and on the error page.
  - **Error page** (port 7070 held by `nc -l -p 7070` inside the emulator): shows "Crosspoint
    couldn't start" with the section 8 copy. After the port was free, **Try again** loaded the
    Console. Not matching the spec to look at: the title is not bold and the body line
    spacing is tight (JUCE `Font` on Android); for the design review.
  - Debug hook: if `<app data>/webdebug` exists the WebView is debuggable
    (`adb shell run-as io.lagreca.app.crosspoint touch webdebug`, `adb forward tcp:9333
    localabstract:webview_devtools_remote_<pid>`), which is how the layout was measured.
  - Captures for the design review, all from `adb exec-out screencap` of
    `crosspoint-33-s`, in `/private/tmp/claude-501/a7-review/`: `100-` and `130-`
    `connected-no-stations`, `settings-sheet`, `keyboard-in-settings`, `error-page`,
    `echo-notice-phone-width` (the A8 notice, now a full card), plus `cold-start.mp4`
    and `cold-start-contact.png`.

#### A8 — Phone audio path: echo, routing, Bluetooth · M · any · lane E
- **Depends on:** A5 · **Touches:** `sonobus/Source/` (Android audio setup),
  `sonobus/Source/android/java/` if needed
- **Do:** a phone on speaker sends the VDI audio back into its own mic, and
  SonoBus has no echo canceller. Find out whether JUCE 7.0.8's Oboe path can
  open the input with the `VoiceCommunication` preset (platform AEC), and at
  what latency cost. Decide the default output (speaker, earpiece or wired).
  Check Bluetooth: an A2DP headset with the phone mic, versus SCO (headset mic,
  narrowband). Record the measurements and implement the default that works.
  Build to `docs/design/android.md` §6:
  - plain device names
  - default output, Phone speaker unless measurements say otherwise
  - mix with other apps' audio
  - `platform.outputRoute` and `platform.echoRisk`
  - the echo notice, **only if** speaker + mic echoes
  (Phone calls are A14.)
- **Done when:** findings recorded here and the chosen default is
  implemented. On the emulator, nothing regresses; the by-ear check is A13.
- **Result:** (2026-10-12, Claude sonnet)
  **How SonoBus picks the device and buffer.** JUCE 7.0.8 on Android uses **Oboe**
  (`JUCE_USE_ANDROID_OBOE` defaults to 1; OpenSL is not enabled). No device type
  is chosen in our code: the device list is JUCE's, `System Default (Input/Output)`
  first (Oboe `kUnspecified`, so Android's own routing decides) then one entry per
  `AudioDeviceInfo`. `SonoStandaloneFilterApp.cpp` asks for buffer 192 at the
  phone's native sample rate. Streams ask for Exclusive + LowLatency; the emulator
  grants Shared/None (it has no MMAP path), a real phone may differ. JUCE reopens the
  output on `ErrorDisconnected` with the unspecified id, which is what makes the output
  follow headphones; the **input stream has no such restart** (see A13).
  **The AEC preset: feasible, done, small patch.** JUCE never calls
  `setInputPreset`, so Oboe's default `VoiceRecognition` (no echo canceller) is what
  stock SonoBus uses. `VoiceCommunication` (7) asks the platform for AEC + noise
  suppression on the mic path (Android 9+ / API 28+, ignored below). It needs a JUCE
  patch (the stream builder is private to JUCE): 20 commented lines in
  `juce_Oboe_android.cpp`, behind `#ifdef JUCE_ANDROID_OBOE_INPUT_PRESET`, input
  direction only, with a retry on the old preset if the open fails. The `.jucer`
  (Android exporter `extraDefs`) sets `JUCE_ANDROID_OBOE_INPUT_PRESET=7`; remove that
  one define and JUCE behaves as before. **Verified on the emulator:** the capture
  stream is created with `inputPreset = 7` (logcat) and `dumpsys audio` shows
  `src:VOICE_COMMUNICATION` for the app (it was `VOICE_RECOGNITION` before).
  **Latency cost (per Oboe/AAudio docs and AOSP behaviour, NOT measured: the emulator
  has no low-latency path to compare):** effects on the capture session generally
  take it off the FAST/MMAP path, so expect the mic side to go from a few ms to
  roughly 20-40 ms more, and Exclusive mode may be refused (Shared is what we get
  anyway on many phones). Output is untouched: we do not set the output usage to
  VoiceCommunication, because that would switch to the call volume stream and can
  steer routing to the earpiece. A13 measures the real cost.
  **Is the AEC "working"?** The engine cannot ask. `platform.echoRisk` assumes it works
  when the preset is compiled in AND `AcousticEchoCanceler.isAvailable()` is true.
  The emulator has no AEC effect, so it correctly reports `echoRisk: true` on speaker.
  If A13 hears echo on a phone that reports an AEC, flip the assumption: set
  `aecWorking = false` in `EngineState::refreshAndroidRoute` (notice always shows on
  speaker while the mic transmits).
  **Implemented.**
  - Device names (`sonobus/Source/AndroidAudioRoute.h`, header-only, plain C++ plus a
    small JNI half): JUCE names are `"<product> <type text>"`; the type is parsed back and
    mapped to Phone speaker / Earpiece / Wired headphones / Bluetooth · name / USB · name
    and Phone mic / Headset mic / ... `devices.inputs/outputs` keep JUCE's name as `id`
    (so `devices.setInput/Output` still work) and use the plain label as `name`;
    `mic.device`/`output.device` use the same label, so the picker marks the current one.
    Non-devices (telephony, remote submix, FM/TV tuner, HDMI, echo reference) are hidden;
    two entries with one label collapse to one (A2DP preferred over SCO, the default
    entry over its explicit twin). The `System Default` entry is labelled with whatever
    Android currently routes to (live, 2 Hz, `getAudioDevicesForAttributes` on API 33+,
    else wired > USB > Bluetooth > speaker).
  - Default output with nothing plugged in: **Phone speaker** (`System Default` resolves
    to it; the emulator reports `output.device: "Phone speaker"`, `outputRoute: "speaker"`).
    Earpiece is only chosen if picked. Output follows headphones through the system
    routing; the mic state is engine state and routing never touches it.
  - Default input stays the phone mic, also with a Bluetooth headset connected (A2DP has
    no mic; SCO is a narrowband call link and we never start it). A Bluetooth SCO mic can
    be picked from the list but we do not manage the SCO link: A13 checks whether it
    captures at all; if not, hide Bluetooth inputs.
  - API: `platform: { outputRoute, echoRisk }` on Android (null elsewhere), added to
    `buildConsoleState`; other tasks add their fields next to these two.
  - UI: `echoNotice()` in `console-ui/src/console/components.js`, first child of `<main>`,
    shown only while `platform.echoRisk`; copy from android.md section 6; mock derives
    `echoRisk` from route + mic like the engine (`android-echo` scenario). Tests: two in
    `console-ui/test/core.test.mjs`; host test `tests/android/a8-device-names.cpp`
    (`c++ -std=c++17 tests/android/a8-device-names.cpp`; labels, routes, hidden devices,
    echoRisk).
  - Other apps' audio: nothing in our Java, JUCE's Java or Oboe calls `requestAudioFocus`
    or `AudioManager.setMode`; `dumpsys audio` shows an empty focus stack with the app
    running. Crosspoint neither pauses other apps nor is paused by them, as designed.
    (A call is A14.)
  **Verified on the emulator (crosspoint-33-e):** builds; app runs; state shows the plain
  names, `outputRoute: speaker`, `echoRisk: true` (no AEC on the emulator); input stream
  opens with preset 7; no focus requests; desktop `build-desktop.sh --app-only` builds;
  `npm test` 17/17. **Not measurable here:** real AEC quality, latency cost, headphone /
  Bluetooth / USB plug events (the emulator has speaker + mic only), earpiece. The
  emulator screenshot of the notice is inconclusive: the pre-A7 web view layout squeezes
  `<main>` to a sliver (only the notice's rounded corner shows), so A7 should confirm the
  notice at phone width.
  **A13 checklist (audio, on the real phone):**
  1. Phone speaker + open mic with a VDI talking: do they hear themselves? With the
     preset (this build) versus echo if you remove the define. Does the Console show the
     notice exactly when it echoes, and not when it doesn't? (If it echoes but no notice,
     flip `aecWorking`.)
  2. Latency: loopback or the Console's latency numbers with the preset on and off; is
     the extra mic latency acceptable (about 20-40 ms expected)?
  3. Plug wired headphones in and out mid-call: output moves and back, **audio keeps
     flowing both ways, the mic button state never changes**; does the mic input keep
     working after unplugging a headset with a mic (the input stream is not restarted by
     JUCE)? Settings shows "Wired headphones" / "Headset mic" then "Phone speaker" / "Phone mic".
  4. Bluetooth A2DP headset: output goes to it ("Bluetooth · name"), mic stays "Phone mic";
     picking the headset's mic (SCO): does it capture, and how bad is it?
  5. USB audio device if available: appears as "USB · name" for both.
  6. Earpiece: picking it works or not (it may need call mode; if not, drop it from the list).
  7. Music/video in another app keeps playing while connected and talking; neither pauses.
  8. Volume buttons change the voice level (media volume) and the notice's wording reads right.

#### A9 — Mic permission flow · S · any + design review · lane P
- **Depends on:** A7, A2 · **Touches:** `sonobus/Source/` (permission request,
  API state), `console-ui/` (only the state A2 specifies)
- **Do:** `docs/design/android.md` §3 and the mic row in §7: ask on the
  first connect (then notifications), no pre-prompt. Add
  `platform.micPermission` and `platform.requestMic`, the mic button and
  live band states, and the Settings row. A blocked mic must still allow
  listening.
- **Done when:** grant, deny and "deny then grant in Settings" all behave per
  A2 on the emulator; design review passed.
- **Resume (2026-10-13):** a Sonnet agent got about halfway, then stopped at the
  usage limit. Its unbuilt, unverified work is committed as `6421d326` on
  branch `worktree-agent-a6d6ce498dac75612` (22 files). It covers the mic
  flow and parts of review fixes 1–6 from docs/HANDOFF-android-apk.md
  ("The A9 agent's job"). Merge `android-apk` into it, finish, build and
  verify. The notification fixes (DEFAULT channel, one-station copy) stay in
  A9 because the WIP already touches them.
- **Result:**

#### A10 — Background audio (foreground service) · M · any + design review · lane P
- **Depends on:** A4, A2 · **Touches:** `sonobus/Source/android/java/`
  (`SonoBusService`, renamed in A4), `.jucer` manifest XML
- **Do:** keep audio and the API alive with the screen off or the app in the
  background. Start the service when connected, with A2's notification and
  actions. Declare `foregroundServiceType="microphone|mediaPlayback"` plus the
  matching `FOREGROUND_SERVICE_*` permissions, and request `POST_NOTIFICATIONS`
  (Android 13+), so a later targetSdk bump to 34+ just works. Hold a
  low-latency Wi-Fi lock and a partial wake lock only while connected. Build to
  `docs/design/android.md` §1 and §4:
  - lifecycle: swipe-away from Recents disconnects and quits; Back and Home
    don't
  - channel, small icon, tint, title and text tables, Mic on/off and
    Disconnect actions
  - the notification and background rows in §7, `platform.notifications`,
    `platform.backgroundRestricted`, `platform.openSettings` and
    `platform.allowBackground`
- **Done when:** on the emulator, the app backgrounded with the screen off for
  10 minutes keeps meters moving and the API answering; the notification
  matches A2. The real-phone check is A13.
- **Result (Claude sonnet, 2026-10-12)**: implemented and verified on the emulator (API 33, arm64); wip,
  awaiting design review. Captures in `/private/tmp/claude-501/a10-review/`.
  - **Where the copy lives:** `sonobus/Source/ConnectionNotification.h` (pure C++, no JUCE) builds title,
    text, tint and the Mic action from the Console state (the same data as the live band);
    `tests/android/a10-notification.cpp` checks every row of android.md section 4's tables (host, passes).
    `android/AndroidServiceController.h` (250 ms message-thread timer) reads `EngineState::getLastState()`,
    and pushes to Java (`SonoBusActivity.updateConnectionNotification`) only when the content or the service
    state changes, at most once a second. Java only draws it.
  - **Lifecycle (section 1):** the service is wanted from the first connect attempt until Disconnect (state
    `failed` + reason "Disconnected"), through reconnecting/failed. A fresh install ("Not connected to a
    server") has no service. `SonoBusService`: channel `connection` (LOW, public on the lock screen, ongoing),
    `ic_stat_crosspoint`, tint #2e6b66 / #ff4f7b live, actions Mic off/Mic on + Disconnect, tap opens the app,
    `START_NOT_STICKY`, partial wake lock + Wi-Fi locks only while it runs. `stopWithTask="false"`:
    swipe-away -> `onTaskRemoved` (and `Activity.onDestroy` while finishing) -> `quitApp()` = native
    disconnect + save, remove the notification, `killProcess` 500 ms later. Back goes home (A7) and keeps
    everything. `suspended()` no longer closes the audio device while the service is wanted.
  - **Manifest:** `foregroundServiceType="microphone|mediaPlayback"` (the microphone type is only passed to
    `startForeground` while RECORD_AUDIO is granted), FOREGROUND_SERVICE_MICROPHONE / _MEDIA_PLAYBACK,
    POST_NOTIFICATIONS, WAKE_LOCK, ACCESS_WIFI_STATE, REQUEST_IGNORE_BATTERY_OPTIMIZATIONS.
    POST_NOTIFICATIONS is requested (Android 13+, once) right after the mic answer in
    `SonoStandaloneFilterWindow.h` (A9: move it with the mic flow).
  - **API:** `platform.notifications` (granted|denied), `platform.backgroundRestricted` (added to A8's
    `platform` object), commands `platform.openSettings {page}` and `platform.allowBackground` (one
    `Hooks::platformCommand` hook; A9 adds `platform.requestMic` there). `backgroundRestricted` = the user set
    Restricted battery use or the system put the app in the restricted standby bucket. Plain "Optimized" is not
    reported: **with the app unplugged, screen off and `dumpsys deviceidle force-idle` (deep Doze) for 90 s the
    engine stayed connected and meters kept moving**, so the foreground service is enough. console-ui: problem
    rows (panel, 3 px warning border, circle, ink2 text, 44 px `.btn`) only when needed, mock scenario
    `android-problems`, `lib/platform.js` + tests (21/21).
  - **Verified on the emulator:** notification content for live ("Live: TEST-01 hears you", pink tint, Mic off
    + Disconnect), mic off ("Mic off · nobody hears you", Mic on), PTT (no mic action), reconnecting
    (Wi-Fi off), phone call; lock screen (readable); the Mic off action toggles the engine; screen off with the
    battery unplugged for 12 minutes: state `connected`, station online, meters at 30 Hz, API answering over
    `adb forward`; swipe-away from Recents: process gone, notification gone; Back keeps the process.
  - **Finding:** Android 12+ hides *silent* notifications on the lock screen unless the user enables
    "Show silent notifications" (`lock_screen_show_silent_notifications`, default off). The capture
    needed that setting. Our LOW-importance channel therefore is not visible on a default lock screen.
    Raising the channel to DEFAULT would play a sound and pop up on every update. Design call.
  - **Not verified / open:** the notification-blocked row and the background row were checked as a headless
    render of the mock (the real WebView layout is A7's open clipped-sheet bug, so the sheet did not render on
    the phone); `platform.openSettings` / `allowBackground` intents were not tapped through; Android 14 FGS
    rules (target is 33); settings sheet 130% font.

#### A11 — Release APK + install docs · S · any · lane B
- **Depends on:** A4 · **Touches:** `scripts/build-android.sh`, `.jucer`
  (keystore fields), `docs/ANDROID.md`
- **Do:** `scripts/build-android.sh --release` signs with a keystore given by
  env vars or `~/.gradle/gradle.properties`. Remove the upstream keystore
  reference; never commit the keystore or passwords. Derive versionName and
  versionCode from the app version. Document `adb install` and sideloading
  ("install unknown apps"). Don't publish the APK anywhere unasked.
- **Done when:** a signed release APK installs over a previous release build
  without uninstalling, and the docs work from a clean checkout.
- **Result (Claude sonnet, 2026-10-12)**: done. Docs: `docs/ANDROID.md`, section
  "Release APK" (keytool command, keystore outside the repo, env vars or
  `~/.gradle/gradle.properties`, `adb install -r`, sideloading, how to bump).
  - Signing: `CROSSPOINT_KEYSTORE`, `_KEYSTORE_PASSWORD`, `_KEY_ALIAS`,
    `_KEY_PASSWORD` (optional) in the environment, or `crosspointKeystore...` in
    `~/.gradle/gradle.properties`. The upstream `sonouploadnew.jks` /
    `sonoupload_*` references are gone from the `.jucer` (its four `androidKey*`
    attributes are removed; the Projucer's `juceSigning` now points at the
    default debug keystore path and nothing uses it). The release signing config,
    the debug override and the release ABI filter live in the `.jucer`'s
    `androidCustomAppBuildGradleContent`. A missing keystore fails twice over:
    `build-android.sh --release` stops with the list of variables before the
    resave, and Gradle's task graph check throws the same if run directly.
  - Release ABI: **arm64-v8a only** (`ndk.abiFilters` on the `release_`
    flavor; `aapt` shows `native-code: 'arm64-v8a'`). **Release APK: 32 MB**
    (debug, four ABIs: 195 MB). Cold release build 1m46s.
  - Versions: `versionName` = `.jucer` `version` (1.7.2), `versionCode` =
    `androidVersionCode` (86); both were already emitted by the Projucer, so
    there is nothing to derive, only to bump (documented).
  - **Verified** with a throwaway keystore in `/private/tmp/crosspoint-a11-ks`
    (outside the repo): release build, install on emulator-5560, rebuild,
    `adb install -r` over it: Success without uninstalling, app launches. (The
    debug build had to be uninstalled first: debug and release keys differ.)
  - Not checked: `apksigner verify` (needs `JAVA_HOME` set; Gradle verified the
    signature by installing), the doc steps from a truly clean clone, a real
    phone.

#### A14 — Phone calls + earcons · M · any + design review · lane P
- **Depends on:** A10, A5 · **Touches:** `sonobus/Source/android/java/`,
  `sonobus/Source/` (call state, local cue mixing), `console-ui/` (live band
  and mic button text)
- **Do:** build to `docs/design/android.md`:
  - **§6 phone calls:** a call turns the mic off and silences playback;
    after the call, playback returns and the mic stays off. Add
    `platform.phoneCall` and `platform.micOffByCall`, plus the mic button,
    live band and notification texts.
  - **§5 earcons:** rising and falling two-tone cues, local only, for mic
    changes from the notification action, the headset button (A12) and a
    call ending.
- **Done when:** on the emulator, a simulated call (`adb emu gsm call …`)
  shows the §6 states and leaves the mic off afterwards. The earcons play
  locally, and the F2 VDI peer receives **no** cue audio. Design review
  passed. *(2026-10-13: design review passed; the emulator call simulation
  does not flip `AudioManager.getMode()` on this box's image, so the live
  call path was closed on code + unit-test evidence and marked done by
  maelo. If a real device ever reports a call and the app ignores it, the
  fix is to drive detection from a `PHONE_STATE` broadcast instead of
  `getMode()`, and to recapture with the mic live before the call.)*
- **Result (Claude sonnet, 2026-10-12; design review Claude, 2026-10-13)**: implemented;
  verified on the emulator and with host tests; **done 2026-10-13** (maelo) after the design
  review confirmed the code matches android.md §5/§6 on every point (earcon tone/gap/level/
  fades + output-only mix after the send tap; mic off on call start with PTT released and
  staying off after; mic-off earcon on call end; playback gain ramp with pre-fader activity
  intact; mic button / live band / notification copy; `platform.phoneCall` + `micOffByCall`).
  Captures in `/private/tmp/claude-501/a10-review/` (`app-call.png`,
  `app-aftercall.png`, `shade-call.png`).
  - **Call detection** (`PlatformMonitor.java`, no permission): `AudioManager.getMode()` every 500 ms:
    `MODE_IN_CALL` (cellular), `MODE_CALL_SCREENING`, or `MODE_IN_COMMUNICATION` (VoIP) while
    `PlatformMonitor.appOwnsAudioMode` is false (**A8: set it while the app itself uses the communication
    mode, e.g. for Bluetooth SCO**). Ringing (`MODE_RINGTONE`) is ignored until answered. No audio focus is
    taken, so other apps mix as android.md section 6 says.
  - **Engine** (`SonobusAudioProcessor::setPhoneCall`): the call turns the mic off (open mode) and releases
    PTT; `isMicTransmitting()` is false for the whole call; playback is silenced by a 10 ms gain ramp on the
    main output (the receive streams keep running); when the call ends playback returns and **nothing
    turns the mic on**. `micOffByCall` is true from call start until `setMicOn(true)`, a PTT hold or a mode
    change. `mic.setOn {on:true}` during a call answers `busy`. State: `platform.phoneCall`,
    `platform.micOffByCall`. Notification title "Mic off during your phone call", no Mic action.
  - **console-ui:** mic button "Mic off" · "During your phone call" (disabled), live band quiet
    "Mic off during your phone call." during, "Mic off since your phone call. Tap the mic button to talk
    again." after (open mode, mic off), cleared on mic on or mode change. Tests in
    `console-ui/test/platform.test.mjs` for the `android-call` / `android-aftercall` scenarios.
  - **Earcons** (`sonobus/Source/Earcon.h`): 660 -> 880 Hz (mic on) / 880 -> 660 (off), 70 ms tones, 20 ms
    gap, -18 dBFS, 5 ms fades. `processBlock` adds it to the main output **after** the
    `oursource->process()` calls that consumed `sendWorkBuffer`, next to the API test tone, and nowhere
    else (not the recording mix, not a send buffer). Played for the notification action and when a call
    ends (open mode); `AndroidServiceController::toggleMicWithEarcon()` is the function A12 calls for the
    headset button. Not for the in-app mic button.
  - **Verified:** `emu gsm call 5551234` + `gsm accept 5551234`: `phoneCall` and `micOffByCall` true, mic
    off, notification title and UI text as above; `gsm cancel`: `phoneCall` false, **mic still off**,
    after-call text shown; `mic.setOn true` clears `micOffByCall`. Earcon generator: host test
    `tests/android/a14-earcon.cpp` (frequencies, level, gap, fades, block-size independence) passes.
  - **"Not sent" is shown by code inspection plus that unit test, not by the F2 peer:** the cue is mixed into
    the output buffer only after every send path has run. I did not run the F2 harness with a desktop
    peer measuring the received audio, and nobody listened to the earcon on the emulator (no audio check).
  - **Not verified:** a VoIP call (only the cellular emulation was run); the earcon's audibility.
  - **Correction (2026-10-13, re-test on the emulator):** re-running `adb emu gsm call`/`accept` on the
    API 34 emulator did **not** drive `platform.phoneCall` to true, so the "phoneCall/micOffByCall true"
    result above could not be reproduced here. The likely cause is the emulator's gsm-call simulation not
    moving `AudioManager.getMode()` to `MODE_IN_CALL` (incomplete emulator telephony) — on a **real** device
    the telephony stack does set the audio mode during a live call, so the existing `inCall()` detection
    (which reads `getMode()`) is plausibly correct but is **unverified in this environment**. Net: A14's
    engine/earcon/UI side is complete; the "call detected" path still needs a **real-device** (or an
    emulator that actually flips `getMode`) check, and a `PHONE_STATE` broadcast would make detection
    robust regardless of the audio mode. Not a code defect proven, just not re-verified on this box.

#### A12 — Mic toggle from the headset button · S · any + design review · lane P · optional
- **Depends on:** A14, A7 · **Touches:** `sonobus/Source/android/java/`
  (MediaSession), API command wiring
- **Do:** the phone counterpart of the Mac hotkey (P6.5), per
  `docs/design/android.md` §5. One press of a wired or Bluetooth headset's
  play/pause button toggles the mic **in open-mic mode only**, with the
  earcon (A14). It does nothing in PTT mode, and only takes the button while
  connected. Add the Settings "Headset button" row and
  `settings.headsetButton`. Not needed for the first APK.
- **Done when:** with the phone locked, a headset press toggles the mic and
  plays the cue, and the notification title follows.
- **Result:**

#### A13 — On-phone acceptance · S · human · lane H
- **Depends on:** A7, A8, A9, A10, A11, A14 (A12 if done)
- **Do:** on maelo's phone, **on the VPN** (peers use VPN IPs, P8.4): install
  the release APK, connect through onboarding, hear a VDI, talk back, mute
  and solo. Then screen off for 15 minutes, switch apps, go from Wi-Fi to
  cellular and back, and use a wired and a Bluetooth headset. If P7.7 has
  been measured, compare latency and background behaviour with the PWA.
- **Done when:** results recorded here; anything that fails becomes a bug
  in `BUGS.md` or a new A-task.
- **Result:**

### Lock-screen station controls (A15–A19)

maelo asked for this on 2026-10-13 after using the APK. The notification is
not practical as it stands: once dismissed it doesn't come back until the app
restarts, and it only has Mic and Disconnect.

**Approach, agreed with maelo:** publish an Android **media session**, as
Spotify does. That puts Crosspoint in Samsung's **Now Bar** and in the
expanded lock-screen player, with buttons that work without unlocking. The
system keeps a media notification while audio plays, so it can't be swiped
away. The system owns the layout; we supply the artwork, title, subtitle and
about 5 buttons:

| Button / field | Crosspoint |
|---|---|
| play/pause (also the headset button) | **Mic on/off** |
| previous / next | **pick a station** |
| custom 1 | **Mute** (selected station) |
| custom 2 | **Solo** (selected station, exclusive: dims the others, and only it hears you. It never switches the mic on) |

The title is the selected station, the subtitle is its state plus who hears
you, and the artwork is the station colour. Per-station "Hears you" stays in
the app. Mock: `design/prototypes/android-nowbar/index.html`.

**Who does what**
- **Claude (lead, design) only:** A15, and the design reviews of A9, A18 and A19.
- **Any agent:** A9 (resume), A16, A17, A18, A19.
- **maelo:** A13 (re-test on the phone with these).

**Order and parallel work**

| Step | Can run in parallel | Waits for |
|---|---|---|
| 1 | **A15** (Claude) · **A9** (agent) · **A17** (agent) | nothing |
| 2 | **A16** (agent) | A9 (both edit `SonoBusService.java`) |
| 3 | **A18** (agent) · **A19** (agent) | A15 + A17 (A18 also A16) |
| 4 | design review of A9, A18, A19 (Claude) | step 3 |
| 5 | A13 on the phone (maelo) | step 4 |

**Files by task** (they're split this way to avoid conflicts):
- A17: a new receiver plus `AndroidServiceController.h`
- A16 and A18: `SonoBusService.java` and the notification layout
- A19: a new `TileService` class

All of them edit the `.jucer` manifest XML: keep those edits small, resolve
conflicts by keeping both sides, then `scripts/build-android.sh --resave`.

#### A15 — UX addendum: lock-screen player · S · design (Claude) · lane D
- **Depends on:** A2, A10 · **Touches:** `docs/design/android.md` (new §12),
  `design/prototypes/android-nowbar/`
- **Do:** turn the mock into spec, covering:
  - Now Bar pill text (live line), and how the expanded player maps (table
    above)
  - title, subtitle and artwork per state: live, mic off, PTT, call (A14),
    disconnected, lost/offline station
  - which station is selected first (the soloed one, else the first in
    `stationOrder`), and how ◀ ▶ wrap
  - button icons and labels for each state, and the accessibility labels
  - what happens with another media app playing
  - how this replaces or sits beside the A10 connection notification, and
    whether the A19 tile is still worth it
- **Done when:** A18 can be built without inventing anything.
- **Result:** done (Claude, 2026-10-13): `docs/design/android.md` §12. Decisions:
  - the MediaStyle notification replaces the §4 one, and plays from connect
    until Disconnect
  - Layout A as in the mock (Mic centre · Station ◀ ▶ · Mute · Solo), with
    fallback Layout B (Listening · Station ↻ · Mic · Mute · Solo); A18 picks
    with a spike, because Android 13+ draws the play/pause icon itself
  - Solo is exclusive and never opens the mic
  - Mic is hidden in PTT, during a call, and when the mic isn't granted
    ("Allow mic" instead)
  - the selection walks online and lost stations; the title is the short
    name; the subtitle is the §4 "who hears you" line
  - the artwork is the station colour, with a live border when your voice is
    out
  - the A19 tile is deferred


#### A17 — Native control bridge · M · any · lane P
- **Depends on:** A10 · **Touches:** new
  `sonobus/Source/android/java/io/lagreca/app/crosspoint/ControlReceiver.java`,
  `sonobus/Source/android/AndroidServiceController.h`, JNI glue, `.jucer`
  manifest (register the receiver, not exported)
- **Do:** buttons send `PendingIntent`s (explicit, immutable) to a receiver.
  The receiver calls into the engine through JNI and runs the **same command
  paths the control API uses** (`station.setMute`, `station.setSolo` with
  `exclusive`, `station.setTalk`, `mic.setOn`, `connection.disconnect`), so
  routing rules and `hearsYou` stay engine-owned. Extend the state pushed to
  Java (it is already throttled, one push per second at most) with up to 4
  stations in `stationOrder`, each with id, short name, `colorIndex`, presence,
  mute, solo, talk and `hearsYou`. Mic changes from here play the earcon
  (A14's `toggleMicWithEarcon`). Commands must work while the screen is
  locked, without an activity.
- **Done when:** a host or emulator test drives every command through the
  receiver (`adb shell am broadcast` with the explicit component) and the
  engine state changes accordingly. No UI in this task.
- **Result (2026-10-13, `android-apk`): done, verified on the API 34 emulator.**
  Built on the A18 spike, because A15's pivot to a media session (§12) changed
  the transport: the buttons are `PlaybackState` actions and `Notification`
  actions, not the `RemoteViews` + `ControlReceiver` the card assumed.
  - **One command implementation, two callers.** `ApiCommands::runLocalCommand
    (owner, cmd, args, err)` runs the *same* `consoleCommand` the control API
    uses. To make that possible, `consoleCommand` now takes the caller's `owner`
    id instead of an `ApiSession&`; its only session use was
    `session.getId()` for the PTT-holder set. Nothing else about command
    behaviour changed, so routing rules and `hearsYou` stay engine-owned.
  - **Player buttons get their own channel.** A new
    `nativePlayerButton`/`pendingPlayerButton` pair. The first attempt reused
    `nativeNotificationAction` (codes 1/2) and **silently did nothing** for
    prev/next/mute/solo: the controller's `switch` only had cases 1 and 2, so
    codes 3–5 fell into `default: break`. Found by tapping the button and
    watching the engine not move; the play/pause button worked, which is what
    pointed at the switch rather than the wiring.
  - **State pushed to Java** (`AndroidServiceController::updatePlayerStations`)
    is up to 4 stations in `stationOrder`, each with id, short name,
    `colorIndex`, presence, health, latencyMs, lossPct, mute, solo, talk and
    hearsYou — wider than the card, because §12.4's second line draws the
    station's own state. Pushed only when the JSON changes.
  - **Selection** (§12.3): the soloed station, else the previous one while it
    exists, else the first. ◀ ▶ walk non-offline stations and wrap.
  - Mic goes through A14's `toggleMicWithEarcon()` (earcon, open mode only).
    Mute/Solo go through `station.setMute` and `station.setSolo { exclusive:
    true }`, so P5.7 applies.
  - **Verified on the emulator** (API 34, one headless peer on a local
    `aooserver`): tapping the player drove the engine every time —
    Mic `on=true→false`, Mute `false→true`, Solo `false→true`, and releasing
    Solo `true→false`. Button labels followed state ("Mute" → "Unmute").
  - Not done: `station.setTalk` and `connection.disconnect` are reachable
    through `runLocalCommand` but have no button yet (§12.1 deliberately keeps
    Disconnect off the player), so there was no UI path to exercise them.
  - Note for A18: the card's `ControlReceiver.java` is not needed for the
    player; `PendingIntent`s to the **service** plus `PlaybackState` actions
    cover every button, and a receiver would be a second, redundant command
    path. A19's tile may still want one.


#### A16 — Service notification never stays gone · S · any · lane P
- **Depends on:** A9 (both edit `SonoBusService.java`) · **Touches:**
  `SonoBusService.java`, `AndroidServiceController.h`
- **Do:** on Android 14+, users can dismiss foreground-service notifications.
  Set a `deleteIntent` that re-posts it, and re-post whenever the app comes to
  the foreground and on every state push, while the service runs. Also check
  why touching the body made it vanish on maelo's phone (it isn't
  `setAutoCancel`; check whether the content intent restarts the task or
  service). Record the cause in *Result*.
- **Done when:** on an Android 14+ emulator image, swiping the notification
  away or tapping it brings it back within a second, while connected. After
  Disconnect or swipe-away from Recents it stays gone.
- **Result (2026-10-13, `android-apk`): root-caused and fixed by the A18
  media session; verified on the API 34 emulator.**
  - **Cause (confirmed by reproduction).** The §4 connection notification is an
    ordinary ongoing foreground notification, which Android 14+ lets the user
    dismiss. Measured before the fix: swipe → `dumpsys notification` reports no
    `id=1555` while `dumpsys activity services` still shows
    `isForeground=true` and the process alive. It does **not** come back on its
    own; it only reappears when the content happens to change. That is exactly
    the reported "doesn't come back until the app restarts".
  - **Why the carded fix would not have held.** `deleteIntent` + re-post is a
    race: between the dismiss and the next push there is no notification, and
    `deleteIntent` is not delivered for every dismissal path. The media session
    removes the problem instead of chasing it: while the playback state is
    `PLAYING` the system marks the notification `FLAG_NO_DISMISS` (observed
    `flags=0x206a`, i.e. `0x2000` set) and refuses the swipe.
  - **Verified after the fix:** swipe → the notification is still there and the
    service is still foreground; repeated with the app in the foreground and
    backgrounded, and after 15 s idle. §12.1's "playing from connect until
    Disconnect, whatever the mic does" is what keeps it that way.
  - **The second symptom is NOT explained.** The card also asks why touching the
    body made it vanish on maelo's phone. I could not reproduce that on API 34:
    tapping the body opened the app (or did nothing in the shade) and the
    notification stayed. `setAutoCancel` is not set and the content intent is
    `FLAG_ACTIVITY_SINGLE_TOP`, which does not tear the task down. **No proven
    cause**, so nothing was changed for it; if it reappears on the phone it
    should be re-opened as its own bug with a logcat capture.
  - The card's "re-post on every state push" is now unnecessary for the player
    (the system will not let it be dismissed), so no re-post code was added.
    Re-posting is still meaningful for the one case that remains: the user
    turning notifications on after the service started, which A9/A10 already
    handle (`lastNotifOk`).


#### A18 — Lock-screen player (media session) · M · any + design review · lane P
- **Depends on:** A15, A16, A17 · **Touches:** `SonoBusService.java`, new
  `CrosspointMediaSession.java` (MediaSession + PlaybackState custom actions,
  a MediaStyle notification as the foreground notification),
  `AndroidServiceController.h`, `.jucer` (androidx.media if needed)
- **Do:** first the layout spike in android.md §12.2 (A or B). Then build §12 on A17's command bridge:
  - play/pause → `mic.setOn`, with the earcon
  - skip previous/next → the selected station
  - custom actions → `station.setMute` and `station.setSolo` (exclusive), on
    the selected station
  - metadata (title, subtitle, artwork) and button states follow the pushed
    state without flicker or sound
  - the media button from headsets maps to play/pause; this absorbs A12
  - keep "mixes with other apps" (A8): no audio-focus request unless A15 says
    otherwise
- **Done when:**
  - On the emulator's lock screen, the media player shows and every button
    changes the engine state within a second.
  - Captures go to `/private/tmp/claude-501/a18-review/`.
  - The real check is maelo's Samsung Now Bar in A13.
  - Design review passed.
- **Result (2026-10-13, `android-apk`): wip, awaiting design review. Layout B
  applied. Verified on the API 34 emulator.**
  - **Layout spike (§12.2), API 34 and API 33: Layout A is impossible.**
    The system player always draws the standard play/pause as the large
    centre/primary button, and a custom action can never take that slot. Tried,
    each captured on the lock screen (`/private/tmp/claude-501/a18-review/`):
    - `base`: PLAY/PAUSE + SKIP_TO_PREVIOUS/NEXT declared, Mute and Solo custom:
      ◀ and ▶ in the outer slots, ❚❚ centre (`spike-api34-base.png`,
      `spike-api33-base.png`).
    - `nopp`: no PLAY/PAUSE/PLAY_PAUSE and no skip declared, four custom actions
      (Mic, Next station, Mute, Solo): **the centre button disappears** and the
      customs fill the bottom row, Mic far left (`spike-api34-nopp.png`,
      `spike-api33-nopp.png`).
    - `nopp-skip`: skip declared, no play/pause, customs Mic, Mute, Solo: no
      centre button again (`spike-api34-nopp-skip.png`).
    - `reserve`: the Android 13+ extras
      `android.media.playback.ALWAYS_RESERVE_SPACE_FOR.ACTION_SKIP_TO_PREVIOUS`
      and `...NEXT` (set on both the session and the PlaybackState) with no
      play/pause: they only leave **gaps** in the previous/next slots; the
      customs still go to the outer slots and the centre stays empty
      (`spike-api34-reserve.png`, `spike-api33-reserve.png`).
    - `B` (play/pause declared; customs Next station, Mic, Mute, Solo): the
      layout that ships (`spike-api34-B.png`, `spike-api33-B.png`).
    - **Pause hides the player.** `paused` (STATE_PAUSED) keeps the player at
      first (`spike-api34-paused.png`, ▶ shown), but after 10 minutes it is gone
      from the lock screen although the notification still exists
      (`spike-api34-paused-after10min.png`). So the session stays PLAYING and
      the centre ❚❚ never changes; "Listening paused" is told in the second line
      instead.
  - **What is implemented (Layout B).** Centre ❚❚ = **Listening** (pause
    silences station playback and turns the mic off; resume keeps the mic off).
    Slots after it: **Next station ↻**, **Mic**, **Mute**, **Solo**.
    Mic icon/label by state: mic / crossed mic, "Mic on. Turn your mic off" /
    "Mic off. Turn your mic on", **hidden** in PTT mode and during a phone call,
    "**Allow mic**" (crossed mic, opens the app) when not granted.
    Mute: speaker / crossed speaker, "Mute {A}" / "Unmute {A}". Solo: target
    outline / filled, "Solo {A}: only {A} hears you, others dimmed" /
    "Release solo {A}". No stations: title "Crosspoint", only Mic.
    The Android 12 and older notification carries the same buttons.
    - Native: `AndroidServiceController` pushes one JSON state document
      (selected station, up to 4 stations, connection, soloDimDb, listening)
      to `CrosspointMediaSession`; latency/loss-only changes go out at most once
      a second, button-driven changes at once. The Mic state is the micAction
      argument (0 hidden, 1 on, 2 off, 3 allow). `micPermission` is reported
      since A9 (`granted | prompt | denied`).
    - Listening pause is a new processor flag (`setListeningPaused`) riding the
      call-silence gain ramp (10 ms), so the receive streams keep running and
      earcons still play. Not saved; Disconnect clears it.
    - Headset button: `onMediaButtonEvent` maps play/pause/headset hook to the
      Mic (open mode only), whatever the layout; the on-screen ❚❚ is Listening.
    - Icons: vector drawables `ic_player_*` (ported from the mock's SVG
      glyphs; `prev`/`next` ◀ ▶ are in but unused by Layout B).
    - Artwork per the lead's feedback on Android 13+ (the player background is
      the artwork, scaled and cropped): rounded tile in the station colour,
      Crosspoint mark small (92 px of 512) top-left; muted = colour at 35% over
      panel; lost/offline = ink4; no station = panel; **voice reaches a station
      = the whole tile blended 55% station / 45% `live`** (the 24 px inset border
      and the bottom-right mic glyph are dropped: the border showed only as edge
      strips, the glyph is cropped). `s4` was wrong in the old code (now
      `#bed590`, console-ui token).
    - Text per §12.4. The one addition: "Listening paused" leads the second
      line while paused. The system player on AOSP draws only title + subtitle;
      the second line was verified through `dumpsys media_session` metadata.
    - Crash risk fixed: `PendingIntent.getForegroundService` is guarded to API 26+
      (`getService` below). `sUseMediaStyle` is gone; the plain notification is
      only the fallback before the session exists. Same notification id 1555, so
      never two Crosspoint notifications (the shade shows only the media player
      plus system notices).
  - **Verified (API 34, local aooserver + 3 headless VDI peers):** captures in
    `/private/tmp/claude-501/a18-review/`: `d01-live-all`, `d02-solo-mic-live`,
    `d03-mic-off-t0`, `d04-muted`, `d05-lost`, `d06-ptt`, `d07-call`,
    `d08-mic-not-granted`, `d10-no-stations`, `d11-reconnecting` (each
    `-lock.png`, `-shade.png`, `-shade-expanded.png`, plus a `-meta.txt` dump of
    the session metadata and custom actions). AOSP has one lock-screen player
    form (no Samsung Now Bar), so "collapsed/expanded" is the lock screen and the
    shade/quick-settings.
    - Every button changes the engine within a second **while locked**
      (PIN set): Mic `true→false`, Mute, Solo (exclusive, soloed station becomes
      the selection), Next station (title changes), Listening pause (mic off,
      second line "Listening paused"), resume (mic stays as the user left it),
      headset key (`input keyevent 79`) toggles the mic.
    - Phone call: Mic hidden, subtitle "Mic off during your phone call".
      After the call the mic stays off. Not granted: "Allow mic"; pressing it
      from the lock screen starts the app behind the keyguard (the permission
      dialog is the resumed activity and a swipe asks for the PIN).
    - **The player stays for 10 minutes with the mic off and the screen locked**
      (`d03-mic-off-t0-lock.png`, `d03-mic-off-t10min-lock.png`; the notification
      id 1555 is still the single one).
    - Selection moves on when the selected station goes offline/is forgotten.
  - **Not verified / left:**
    - The audible earcon on Mic changes: same `toggleMicWithEarcon` /
      `playEarcon` path as A14/A17, but a headless emulator gives no way to
      hear it.
    - Samsung Now Bar and One UI's own layout (A13, maelo's phone). Layout B was
      only seen in AOSP's player.
    - No audio-focus request (A8), unchanged.
    - The sibling bug noticed while testing: after the emulator or a peer drops,
      the first `connection.connect` with the same user name often ends
      "failed / Not connected to a server" until a different name is used (the
      server still holds the stale user). Not A18; worth a bug card.

#### A19 — Quick Settings mic tile · S · any + design review · lane P
- **Depends on:** A15, A17 · **Touches:** new `MicTileService.java`, `.jucer`
  manifest (`TileService` with `BIND_QUICK_SETTINGS_TILE`)
- **Optional, only if A15 keeps it.** **Do:** a tile "Crosspoint mic" that toggles the mic through A17, with
  states per A15 (active = live, inactive = off, unavailable when disconnected
  or the mic isn't granted). It must work from the lock screen if A15 says so.
- **Done when:** added to Quick Settings on the emulator, it toggles the mic
  locked and unlocked, its state follows changes made in the app, and design
  review passed.
- **Result:**

#### A24 — Build the APK after the lock-screen player · S · any
- **Depends on:** A18 merged into `android-apk` *and design-approved* (its index
  row says `done`). Include A9 if it has merged by then; otherwise say so.
- **Do:**
  1. `git merge android-apk` (or work on it directly) and run the checks:
     console-ui `npm test`, `scripts/test-android-ui.sh`,
     `scripts/build-desktop.sh --app-only`.
  2. Build the **debug** APK (`scripts/build-android.sh`).
  3. Build a **release** APK (`--release`), but only if maelo's keystore
     variables are set (docs/ANDROID.md, "Release APK"). Never create or commit
     a keystore for him.
  4. Copy each APK to `build/apk/crosspoint-<debug|release>-<short sha>.apk`.
     That folder is git-ignored and survives cleanups; build intermediates
     don't.
  5. Smoke-test on an emulator: it installs and launches, the Console loads,
     it connects to a group, and the lock-screen player shows with working
     buttons.
  6. For Claude's design review of A9 and A14, capture the in-app screens
     with adb screencap into `/private/tmp/claude-501/a24-review/`:
     - the first-connect mic dialog
     - the Console with the mic blocked, both "Tap to allow" and "Allow in
       Android settings"
     - the Settings problem rows
     - during and after a phone call (`adb emu gsm call` / `gsm cancel`)
     - the one-station live band
  7. Delete the build intermediates (`Builds/Android/app/build`, `.cxx`) and
     discard the resave noise.
- **Done when:** the APK paths, sizes and the commit they were built from are
  recorded here, plus one line on what changed since maelo's last APK
  (`348618c6`) and the install command (`adb install -r …`, or "uninstall
  first" if the signing key differs).
- **Result (2026-10-13, `android-apk` @ `275316ef`): debug APK built and
  smoke-tested; release skipped (no keystore, as the card requires).**
  - **Artifact:** `build/apk/crosspoint-debug-275316ef.apk`, 195 MB,
    sha256 `54d6a36f...f765` (below), built from
    `275316ef`, debug-signed with the standard Android debug key
    (`CN=Android Debug`), so the usual `adb install -r` works.
    ```sh
    adb install -r build/apk/crosspoint-debug-275316ef.apk
    ```
    No uninstall needed: same debug key as `348618c6`.
  - **Release APK skipped, deliberately.** `CROSSPOINT_KEYSTORE` was not set and
    `~/.crosspoint/crosspoint-release.jks` does not exist. The card says never to
    create one for maelo, so `--release` was not run. Give me the variables (or
    the keystore) and the release build is one command.
  - **Checks:** console-ui `npm test` 31/31; `scripts/test-android-ui.sh` 10/10;
    `scripts/build-desktop.sh --app-only` green.
  - **Smoke test on the API 34 emulator:** installs, launches, no crash; the
    Console loads and is served from `http://127.0.0.1:7070/`; it connects to a
    group (local `aooserver` + a headless VDI peer); the lock-screen player shows
    with the selected station as the title and working buttons (tapping the Mic
    slot drove `mic.on true → false`).
  - **What changed since maelo's last APK (`348618c6`, 16 commits):** the
    lock-screen player (A18 — media session, Layout B, per-state icons and
    artwork, Listening pause), the A17 control bridge it runs on, A16 (the
    notification is no longer dismissable), and the A9 mic-permission flow
    (first-connect ask, "Mic blocked" states, Settings problem rows, device
    name). Also the A15/A9/A14 copy and layout fixes.
  - **Captures** in `/private/tmp/claude-501/a24-review/`:
    `01-console-connected`, `02-mic-blocked` (shows both the live-band and mic
    button blocked states), `03-settings-rows`, `04-during-call`,
    `05-after-call`, `06-lockscreen-player`, `07-shade-player`.
    Caveats on those, so the review is not over-read:
    - `02` is the good mic-blocked capture; an earlier attempt caught an
      emulator "System UI isn't responding" dialog over the permission prompt
      (software GPU on a loaded host), which is not an app fault.
    - `04`/`05` (during/after call) do **not** show the §6 call states:
      `adb emu gsm call` set the emulator's call state but
      `platform.phoneCall` never became true, so the engine never entered the
      call path. A14's call detection needs its own check; I did not chase it,
      as A24's brief was to produce the APK.
    - `06-lockscreen-player` caught the home screen, not the lock screen; `07`
      (the shade) is the usable player capture.
  - **Cleanup:** `app/build` and `.cxx` deleted after copying the APK, resave
    noise discarded, local test server/peer stopped.
  - Full sha256: `54d6a36f2c0d744315d4ce3d108038e2bae8526ac8b355fb2be098991048f765`.


#### A25 — Lock-screen artwork v2 · S · any + design review
- **Depends on:** A18 · **Touches:** `CrosspointMediaSession.java` (the
  `artwork()` drawing only)
- **Why:** on maelo's Samsung (2026-10-13), the maximised artwork shows black
  wedges in the corners, because we drew rounded corners on a transparent
  bitmap and One UI masks with a larger radius. It's also a flat colour with
  no name. Screenshot: `~/Downloads/Screenshot_20261013_130934.jpg`.
- **Do:** build `docs/design/android.md` §12.4b exactly:
  - full-bleed opaque 1024² canvas
  - diagonal gradient plus the faint matrix lines
  - the short name, fitted and never truncated
  - the LIVE / SOLO / MUTED / LOST pill
  - the small mark
  - the live, muted, lost and no-station variants
  - one cached bitmap per (station, state)
- **Done when:** emulator captures of each variant (maximised player and
  shade) are in `/private/tmp/claude-501/a25-review/`. No black corners at any
  mask. Design review passed. Then maelo checks it on the Samsung.
- **Result:**

### Android Auto: lock-screen controls on the car screen (A20–A23, low priority)

maelo asked for this on 2026-10-13: the same controls as the lock-screen
player (A18), shown on the car screen. Android Auto only lists apps declared as
**Auto media apps**. A plain media session isn't enough: the app needs a
`MediaBrowserService` that hands Auto the same session, plus an Auto
declaration. After that, Auto draws the player with the same buttons, and they
go through the same engine commands (A17). A fuller Auto app is ROADMAP P9.5
(deferred).

**Audio in the car:** Android routes media playback to the car speakers by
itself, so nothing changes there. Recording is different: Android does **not**
move an app's mic to the car's mic. That needs the Bluetooth call channel
(SCO), which only calls, the assistant and VoIP apps that switch into
"communication" mode use. Crosspoint stays in media mode (A8), so in the car it
talks through the **phone's own mic**. A22 (car mic) is optional and deferred:
it only comes back if A23 shows the phone mic isn't good enough.

**Who does what:** A21 is Claude only (design). A20 can go to any agent. A23
is maelo.

**Order:** A21 can start now. A20 starts after A18 and A21. A23 comes after
A20.

**Effort:**

| Task | Effort |
|---|---|
| A21 (design) | about 0.5 d |
| A20 | 0.5–1 d |
| A23 | a drive |

**The plan is A21 + A20: about 1–1.5 days**, plus a drive for A23. A22, if it
is ever needed, adds 1–2 days.

#### A21 — UX addendum: player on Android Auto · S · design (Claude)
- **Depends on:** A15 · **Touches:** `docs/design/android.md` (new §13)
- **Do:** say how §12 maps to Auto's media template:
  - which custom actions show and which go to overflow
  - the browse list: one item per station, with name, state subtitle and
    colour icon; tapping an item selects it
  - title and subtitle lengths on a car display
  - what the car shows when disconnected, during a call, and when the mic
    isn't granted
  - driver-safety wording: nothing to read beyond the title and one line
- **Done when:** A20 can be built without inventing anything.
- **Result:** done (Claude, 2026-10-13): `docs/design/android.md` §13.
  - **Now playing** uses Layout B: Listening in the centre, then Mic, Solo,
    Mute and Next station, in that priority; Next station goes to the
    overflow first.
  - **The browse list** has one item per station: short name, car-short
    state, and a colour icon that blends toward live while that station hears
    you. Tapping an item only selects it.
  - **Car-short copy table** (§13.2): no numbers, about 30 characters.
  - **Not connected:** with saved settings, a "Connect to {group}" item;
    without, the error "Set up Crosspoint on your phone first."
  - **One session**, shared with the lock screen. No Disconnect in the car.


#### A22 — Bluetooth mic setting (buds and car) · M · any + design review + human
- **Spec:** `docs/design/android.md` §14 (Claude, 2026-10-13). maelo's main use is meetings with Bluetooth buds, so this matters more than the car, and it covers the car too.
- **Depends on:** A8, A14 · **Touches:** `sonobus/Source/` (Android audio setup), `console-ui/` (setting + placement note),
  `sonobus/Source/android/java/`
- **Do:** during Android Auto, media plays through the car, but app
  recording normally uses the phone mic. Find out whether Crosspoint can
  capture from the car's mic:
  - `AudioManager.setCommunicationDevice()` (API 31+) or Bluetooth SCO, with
    the VoiceCommunication preset (A8)
  - what that does to output quality (SCO is narrowband) and to other apps
  - whether it breaks A14's call detection (`PlatformMonitor.appOwnsAudioMode`)

  Implement it behind a setting if it works. Test first on the Desktop Head
  Unit (DHU) as far as it allows, then maelo checks in the car.
- **Done when:** findings are recorded (works, works with caveats, or
  doesn't), with the chosen behaviour implemented or documented.
- **Result (2026-10-13, `task/A22-bt-mic-setting`):** implemented behind the
  `settings.bluetoothMic: "phone" | "headset"` setting (android.md §14).
  - **Findings:** works, with the §14 caveats. Classic Bluetooth: the headset
    mic is only reachable on the call channel — the engine's A8
    VoiceCommunication streams follow the route, so "headset" engages
    `setCommunicationDevice()` (API 31+) or `MODE_IN_COMMUNICATION` + SCO
    (24–30) while a headset/car is connected, and releases it otherwise.
    That route is mono at call quality: **placement is off** (the Console says
    so), both ears get the same mix. LE Audio keeps stereo. A real call always
    owns the route and is never overridden; the app's own engagement sets
    `PlatformMonitor.appOwnsAudioMode`, so **A14 call detection is not broken**.
    The mic on/off state never changes (A14 §6 rule). `platform.bluetoothRoute`
    reports `{ name, stereo }` (null when nothing is connected); the Console
    shows the Phone/Headset mic control (with the §14 notes) only while a
    headset or car is connected.
  - **Code:** `BluetoothRoute.java` (main-looper route controller), JNI
    `nativeBtRoute`/`nativeBtMicSetting`, `Signals.btRoute/btMic`, engine
    setting + persistence, Console UI + placement note, `platform.js btRoute`
    (unit-tested). **Build-verified:** debug APK assembles (all ABIs), JNI
    symbols present in libjuce_jni.so, Console strings embedded in the A6 zip.
  - **Design review (Claude, 2026-10-13): PASSED with two fixes** (in this PR).
    Checked against android.md §14 + §6: setting, notes, mic-state rule,
    `platform.bluetoothRoute`, placement note, and the "Bluetooth · {name}"
    Microphone label (§6) all match. The two bugs found and fixed in
    `BluetoothRoute.java`:
    1. **LE Audio must not switch the route.** §14 says LE Audio "keeps
       stereo" — it can carry media and the mic together, so there is nothing
       to move. The code engaged `setCommunicationDevice()` for LE too, which
       would have forced the mono call channel and dropped the stereo it
       exists to keep (the 2024+ default; maelo's case). Now LE only sets
       `sHeld`/`appOwnsAudioMode`; only classic A2DP moves to the
       communication route.
    2. **Reported route must follow the actual route.** `platform.bluetoothRoute`
       was set whenever the *setting* was "headset", even when the route
       wasn't engaged (e.g. a real call owns it). §14 says it is null unless
       engaged. Now it is null unless the route is on, and the `stereo` flag
       is the engaged-and-LE case. `releaseRoute()` also no longer clobbers
       the mode if a call took over first.
    Not yet: maelo's buds/car check (A13 covers the car).
    Gotcha: this box's `android-33` SDK jar postdates the
    `OUTPUT_DEVICE_*`→`TYPE_*` rename, so `BluetoothRoute.java` resolves the
    type constants at runtime (both names) instead of referencing them at
    compile time.
  - **Fix (2026-10-13, `task/android-consolidated`, PR #5): the control was
    unreachable.** Review fix #2 above made `platform.bluetoothRoute` null until
    the route is engaged, but the Console still gated the row on it — so with
    buds connected and the setting still "phone" there was no route, the row was
    hidden, and nothing could ever set the route. The engine now reports the
    *connected device* separately as `platform.bluetoothDevice: null | { name,
    le }` (JNI `nativeBtDevice`, pushed on every tick from the same controller,
    independent of the setting), and the Console offers the row on that.
    `bluetoothRoute` keeps its meaning (the route actually taken) and still
    drives the placement note. Unit tests cover both fields, including the case
    that used to be unreachable. **Still needs maelo's buds/car:** that a real
    headset makes the row appear, that "Headset mic" engages the route, and the
    §14 notes — the emulator has no Bluetooth stack, so none of it is testable
    here.

#### A20 — Android Auto media app · S · any + design review
- **Depends on:** A18, A21 · **Touches:** new `CrosspointBrowserService.java`
  (platform `android.service.media.MediaBrowserService`, returning the A18
  session's token), `res/xml/automotive_app_desc.xml` (`<uses name="media"/>`),
  `.jucer` manifest (service with the `android.media.browse.MediaBrowserService`
  intent filter, and `com.google.android.gms.car.application` meta-data)
- **Do:** build A21's design:
  - the browse tree: root → stations; `onPlayFromMediaId` selects the station
    through A17
  - the same `PlaybackState` and custom actions as A18
  - document how to install the Android Auto Desktop Head Unit
    (`extras;google;auto`) and how to turn on Unknown sources in Android Auto
    (Developer settings: tap the version 10 times) in docs/ANDROID.md
- **Done when:** on the DHU, Crosspoint appears in the launcher, the player
  shows, and every button and station item changes the engine. Captures go to
  `/private/tmp/claude-501/a20-review/`. Design review passed.
- **Result (2026-10-13, `android-apk`): built and verified against a real
  `MediaBrowser` client; the DHU itself could not be run here.**
  - **What landed.** `CrosspointBrowserService.java` (platform
    `MediaBrowserService`, returning the A18 session's token), `res/xml/automotive_app_desc.xml`
    (`<uses name="media"/>`), and the manifest's
    `com.google.android.gms.car.application` meta-data + the
    `android.media.browse.MediaBrowserService` intent filter, all via the
    `.jucer`. No new Gradle dependency: the platform class is in `android.jar`
    at minSdk 24 (the same choice A18 made), so nothing had to be resolved from
    Maven.
  - **Browse tree** (§13.1): one item per station in `stationOrder`, up to 8,
    title = short name, subtitle = the car-short state, icon = a 128 px
    station-colour tile blended toward live while that station hears you.
    Tapping sends the station through `nativeSelectStation`, which only moves
    the selection.
  - **Two real bugs found and fixed while building it:**
    1. **The platform class has no `onPlayFromMediaId`/`onSearch`** — those are
       androidx additions, and `@Override` on them fails to compile. Auto
       delivers the tap to `MediaSession.Callback.onPlayFromMediaId` instead, so
       the tap is handled on the session.
    2. **The session was destroyed on Disconnect**, which broke §13.3
       completely: with no session token the `MediaBrowserService` cannot be
       bound at all, so the car had no "Connect to {group}" item and a
       disconnected fresh boot showed nothing. The session is now
       process-shared (`CrosspointMediaSession.shared`) and outlives the
       foreground service; only its `active` flag follows the service, so the
       lock-screen player still goes away on Disconnect (§12.1). That fix
       introduced a crash — `MediaBrowserService.setSessionToken` throws
       "The session token has already been set" on a reconnect — which is fixed
       by setting it once per service instance.
  - **Also fixed for §13.3:** the engine's `connection.group` is empty after
    Disconnect (`EngineState` only fills it from the recents when the *server* is
    empty too), so the Android side falls back to
    `getRecentServerConnectionInfos()` for the group name.
  - **Verified** by binding a throwaway `MediaBrowser` client to the real
    service on the API 34 emulator (evidence in
    `/private/tmp/claude-501/a20-review/`):
    - connected, 3 stations: `peer-vdi / Hears you`, `three / Hears you ·
      Unstable`, `two / ...`, all with icons, and the session token present;
    - disconnected with saved settings: exactly one item,
      `Connect to a20-auto`, which on tap reconnects;
    - a connect → disconnect → reconnect cycle no longer crashes.
  - **Not verified — the DHU could not run in this environment, so there are no
    car-screen captures.** `extras;google;auto` installs and the DHU starts, but
    the `google_apis_playstore` image ships **Android Auto as a stub**
    (`versionName=1.2.531830-stub`) with no Head Unit Server, and the real app
    needs an interactive Play Store sign-in. Consequence: §13.1's *rendering*
    (how Auto lays out the five buttons, whether Next station lands in the
    overflow) and the day/night theming are **unverified**; what is verified is
    the browse tree, the session token and the command wiring Auto consumes.
    The setup + the "Unknown sources" step are documented in
    `docs/ANDROID.md`; a machine with a signed-in Play Store should be able to
    finish this step in minutes.
  - One test-harness trap worth recording: Android 11+ package visibility makes
    a *client* fail with "Unable to start service … not found" unless it declares
    `<queries>`. That looked like a server-side bug for a while and is not.


#### A23 — In-car test · S · human (maelo)
- **Depends on:** A20
- **Do:** with the release APK installed and Unknown sources on in Android
  Auto, connect to the car and check:
  - Crosspoint appears in Auto
  - the controls work while driving rules apply
  - station audio plays through the car
  - talking works through the **phone's mic** (phone in its holder), and
    whether the VDI hears car-speaker echo. If it's poor, revive A22
  - phone calls interrupt and recover (A14)
- **Done when:** results are recorded here; anything that fails becomes a bug
  or a task.
- **Review follow-ups (Claude, 2026-10-13):**
  1. The station-state line is built twice, in
     `CrosspointBrowserService.java` (~190) and `CrosspointMediaSession.java`
     (~295), with slightly different rules. Keep one.
  2. Per §13.2, "Hears you" is followed by the health ("Hears you · Clear"),
     and "Dimmed" (another station soloed) is missing.
  3. §13.3 fresh install: set the error message "Set up Crosspoint on your
     phone first." instead of relying on Auto's generic empty list.
  4. The DHU captures for the §13.5 review are still outstanding.
- **Result:**

### P9.3 — Native iOS Console · L · deferred
- **Depends on:** P5.6
- **Do:** iOS build from `sonobus/mobile/` in the monorepo layout (needs
  Xcode), a web-view shell hosting the Console UI, mic permission and
  background audio. Until then, iOS can use the PWA via Safari "Add to Home
  Screen" (to verify in P7.7).
- **Result:**

## P10 — Recording and meeting recaps (deferred)

Background and the "session record" idea are in ROADMAP P10. Nothing here
starts until it's moved out of `deferred`.

### P10.1 — Per-station recording · M · deferred
- **Depends on:** P3.4 (recording engine kept), P4.4 (start/stop commands)
- **Do:** expose per-station recording through the control API, built on
  `RecordIndividualUsers`. Record each station **pre-fader** (my mute, dim
  and level don't change the file), FLAC, with filenames carrying station
  name and UTC start time. Engine stays real-time safe (disk writes off the
  audio thread, as SonoBus already does).
- **Done when:** F2 scenario: 3 stations record 3 files of the expected
  length and content.
- **Result:**

### P10.2 — Session record mode · M · deferred
- **Depends on:** P10.1, P1.5
- **Do:** per station, one 2-channel (or 2-file, sample-aligned) recording:
  ch1 = station audio (pre-fader); ch2 = my mic **multiplied by that
  station's effective talk gate**, i.e. the exact signal sent to it (talk
  toggle, solo narrowing, push-to-talk all apply). Write a sidecar JSON with
  station name, start time and gate open/close timestamps.
- **Done when:** F2 scenario with two stations and solo switching: each
  file's mic channel holds audio only for the intervals that station heard me.
- **Result:**

### P10.3 — UX addendum: recording + recaps · M · design (Claude) · deferred
- **Depends on:** UX4
- **Do:** design record controls (per station and global, session-record
  mode), a recording indicator as unmistakable as LIVE but distinct from it,
  a consent reminder, and screens to browse sessions, transcripts and recaps.
- **Result:** `docs/design/recording.md` + prototype
  `design/prototypes/recording/` (`?view=console|consent|sessions`). Key
  decisions: one Settings choice "Recordings include: station only / station
  + my voice to it" (default + my voice); **REC gets no new hue**, so it's an
  `ink` circle + "REC" + time (a validated search only offered forbidden
  cyan or weak pastels); per-station Record button and R shortcut; a daily
  consent reminder (alertdialog, Cancel default); a Sessions view (day list,
  status chips, recap/transcript/audio with per-side mute, export, delete,
  retention line); API additions listed in §5 (additive to control-api v1).

### P10.6 — Storage, retention, privacy · S · deferred
- **Depends on:** P10.1, answers to Q7/Q8
- **Do:** decide and implement where recordings go (Console machine vs
  central store; web sessions record in the container), retention and
  deletion, and document what data is sent to which provider.
- **Result:**

### P10.4 — Transcription pipeline · L · deferred
- **Depends on:** P10.2, P10.6, Q7
- **Do:** a service (container on my infra) that picks up finished session
  records, transcribes each track with an external ASR model (word
  timestamps), labels track 2 "Me" and track 1 the station name, diarizes
  inside track 1 if several remote people spoke, and merges everything into
  one timestamped dialogue (JSON + Markdown).
- **Done when:** a recorded test session produces a correct, correctly
  attributed transcript.
- **Result:**

### P10.5 — Meeting recap generation · M · deferred
- **Depends on:** P10.4
- **Do:** send each station's transcript to an external model and produce a
  recap: summary, decisions, action items (with owner when stated), open
  questions. Store it next to the transcript; show it in the UI per P10.3.
- **Result:**

## P8 — Self-hosted connection server (deferred)

Start only when P0.4 says so, or when I decide to. See ROADMAP P8.

### P8.1 — Dockerfile for `aooserver/` · S · deferred
- **Depends on:** F1
- **Do:** Linux build of `aooserver/` (its own vendored AOO under
  `aooserver/deps/aoo`, **not** the top-level `aoo/`), expose 10998 TCP+UDP,
  log dir volume.
- **Result:** `docker/aooserver/` (Dockerfile, compose, README). It builds
  aooserver on `debian:trixie` from its own vendored AOO and runs it on
  `trixie-slim` as non-root uid 10998 (arm64, 121 MB). It exposes 10998
  TCP+UDP, logs to `/var/log/aooserver`, has a bash `/dev/tcp` healthcheck,
  and takes `AOO_PORT`/`AOO_BLOCKLIST`. Build:
  `docker build -f docker/aooserver/Dockerfile -t crosspoint-aooserver aooserver`
  (the context is `aooserver/` only). New `tests/f2/run.sh --server-addr
  HOST:PORT` runs F2 against an external server. Verified by the subagent
  (matrix-1v1, mesh-stock) and re-verified in review (matrix-2v2, 4 peers,
  container healthy). Open for P8.2: amd64 build if the host needs it; log
  rotation; bind to the VPN IP; the blocklist is untested with a real file.

### P8.2 — Deploy on a VPN-reachable host · S · ~~deferred~~ done
- **Depends on:** P8.1
- **Result:** Deployed **2026-10-10** on `maelosdebian` (192.168.0.71), which is
  on the VPN as **10.248.233.7** (`wg0`, /24). The amd64 server binary was built
  from `docker/aooserver/Dockerfile` on the native amd64 builder and installed
  user-locally at `~/.local/bin/aooserver`, with a systemd user unit
  `~/.config/systemd/user/crosspoint-aooserver.service`
  (`ExecStart=%h/.local/bin/aooserver -p 10998 -l %h/.local/state/aooserver`,
  `Restart=on-failure`), enabled and active, logging to
  `~/.local/state/aooserver/`. It binds **10998 TCP+UDP on all interfaces**, so
  it answers on both the VPN IP and the LAN IP. **Verified reachable from the
  Mac over the VPN:** `nc 10.248.233.7 10998` open, and a real Mac peer joined
  a group on it (below). No `sudo` needed (user unit + `Linger=yes` already on).
  Note this now co-exists with the VDI agent on the same box; that is fine for
  a first rendezvous host (it is not a relay — audio stays peer-to-peer), but
  for the real cutover consider a host that is not also a station.

### P8.3 — Point configs at it · S · ~~deferred~~ done
- **Depends on:** P8.2, P2.1
- **Result:** Switch is a one-line `server:` change, proven in both directions.
  The VDI agent config was pointed at `server: 10.248.233.7:10998` and rejoined
  group `lagreca`; a Mac Console was pointed at the same server and saw the VDI.
  **Left pointing at `aoo.sonobus.net:10998` on purpose**, so the VDI stays in
  the group maelo actually uses day to day until the cutover is decided (P2.8);
  the VPN server is up and ready for that switch. Because `server` is a normal
  P2.1 key, this is a config edit + `systemctl --user restart`, nothing more.

### P8.4 — Verify VPN path · S · ~~deferred~~ done
- **Depends on:** P8.3
- **Do:** repeat P0.2.
- **Result:** **Verified 2026-10-10** that peers connect over VPN addresses and
  audio does not fall back to the internet. With both the VDI agent and a Mac
  Console on the self-hosted server, `ss` on the VDI showed an established
  session **from the Mac's VPN IP**:
  `ESTAB 10.248.233.7:10998 ← 10.248.233.2:52161 (aooserver)`, and the VDI
  agent listed the Mac as a Console peer
  (`consoles: [{name "mac-vpn-console", kind "mac", latencyMs 453}]`) while the
  Mac saw the VDI as `presence "online"` with `agent {input ok, output ok}`.
  So the rendezvous is reached over `wg0` and the peer relationship forms on VPN
  addresses. Also settled: the public server is **not** required — the same
  agent works unchanged against either. What this does *not* yet prove is the
  sustained audio quality on the VPN (that is L2) and a multi-day run.
