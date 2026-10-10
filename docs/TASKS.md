# Task list — VDI remote audio

The work queue for [ROADMAP.md](ROADMAP.md), in dependency order. Task IDs match
the roadmap items, plus `F*` foundation tasks that unblock verification.
**This file is the source of truth for task status**; the roadmap tracks phases.

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
| F2.1 | F2 coverage gaps from the P1.4 review: mixed known + unknown, mute interaction | F2, P1.4 | S | any | wip | Claude (subagent N, sonnet) |
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
| P2.8 | Cut over from Carla hub to mesh; retire hub | P2.10 | S | human | wip | |
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
| P6.1 | Mac shell: web view hosting Console UI | P5.6 | M | any + design review | wip | Claude (subagent O, sonnet) |
| P6.5 | Mac global push-to-talk hotkey | P6.1 | S | any | wip | Claude (subagent O, sonnet) |
| P7.1 | Container: headless engine in Console role | P2.1, P1.4 | M | any | done | Claude (subagent G, sonnet); latency blocker → L1 |
| P7.3 | WebRTC (Opus) audio gateway browser ↔ engine | P7.1 | L | any | done | Claude (subagent E, sonnet) |
| P7.2 | Serve Console UI + proxy API from container | P7.1, P5.6 | S | any | done | Claude |
| P7.4 | Publish via maelo's proxy (TLS there), WS + WebRTC UDP (D13) | P7.2 | S | any | todo | |
| P7.5 | One container per user: compose + docs | P7.2, P7.3, P7.4 | S | any | done | Claude |
| P7.6 | Remove VNC / Xvfb / raw-PCM bridge | P7.5 | S | any | done | Claude |
| P7.7 | Installable PWA, the phone Console (manifest, icons, service worker) | P7.4, UX4 | S | any + design review | todo | |
| P8.1 | Dockerfile for `aooserver/` | F1 | S | any | done | Claude (subagent D, sonnet) |
| P8.2 | Deploy aooserver on a VPN-reachable host | P8.1 | S | human | done | |
| P8.3 | Point VDI YAML + Console defaults at it | P8.2, P2.1 | S | any | done | |
| P8.4 | Verify peers use VPN IPs | P8.3 | S | human | done | |
| P9.1 | Mac menu-bar panel | P6.1, P6.5 | M | any + design review | deferred | |
| P9.2 | Native Android Console (was P6.2–P6.4) | P5.6 | L | any | deferred | |
| P9.3 | Native iOS Console | P5.6 | L | any | deferred | |
| P10.1 | Per-station recording (pre-fader, one file per station) | P3.4, P4.4 | M | any | deferred | |
| P10.2 | Session record: station + my mic gated by the talk path | P10.1, P1.5 | M | any | deferred | |
| P10.3 | UX addendum: recording + recaps | UX4 | M | design | done | Claude |
| P10.6 | Storage, retention, privacy (Q7, Q8) | P10.1 | S | any | deferred | |
| P10.4 | Transcription pipeline (external ASR) | P10.2, P10.6, Q7 | L | any | deferred | |
| P10.5 | Meeting recap generation (external model) | P10.4 | M | any | deferred | |

**Parallel tracks:** UX1–UX4 run from day one, alongside F1/F2/P1. Once P1.4
is done: P2 (config/agent) · P3 (removal) · P4 (API). Mobile is the PWA
(P7.7); native Android/iOS are deferred to P9 (D9). P3 tasks all touch `SonobusPluginEditor.cpp`, so run
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
  `--version` → 1.7.2; aooserver listens on TCP+UDP. **Linux not yet run
  through the script**; the app's Linux build is proven by `docker/`, but the
  new aooserver CMake on Linux is untested.

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
  **Linux not yet run** (F2's card asks for it).

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
- **Result:**

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
  live mic is present. **Do:** make it deterministic — give the VDI peer a
  `config` with an `input_device` that genuinely carries no signal, so
  `silent` comes from the signal path rather than from luck. Do **not** mask
  it with `SONOBUS_AGENT_INPUT=silent`: that override bypasses the very
  detector this scenario exists to exercise.
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
  still links. **Verified on Linux** in `ubuntu:22.04` with the project's exact
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
  (`Plaermo22990613`, a typo of `Palermo…`) is wrong and gives
  `could not join group 'lagreca': wrong password`. The correct one is
  `Palermo22990613`.
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

Android and iOS native apps are deferred (ROADMAP D9); phones use the PWA
(P7.7). The old Android cards are kept under P9.2.

### P6.1 — Mac shell · M
- **Depends on:** P5.6
- **Do:** replace the JUCE editor (Console role) with a `WebBrowserComponent`
  loading the bundled `console-ui/` build, talking to the engine's local API.
- **Done when:** Mac app works end to end against a VDI (or F2 VDI peer).
- **Result:**

### P6.5 — Mac global push-to-talk hotkey · S
- **Depends on:** P6.1
- **Do:** register a global hotkey with Carbon `RegisterEventHotKey`
  (press + release events, no Accessibility permission needed). Hold = talk
  while in push-to-talk mode. The key is configurable in settings, per UX4.
- **Done when:** PTT works while another app is in front.
- **Result:**

## P7 — Web Console container

Location: replaces `docker/`. Keep the old files until P7.6.

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
  the VPN IP** (media bypasses the proxy). **Remaining unknown:** where the
  container will actually run — it must be a Linux host on the VPN (Docker
  Desktop on macOS cannot route peers to container UDP, P7.1), and neither
  `maelosdebian` (192.168.0.71) nor the proxy host is known to have Docker yet.
  Blocked on that host choice + proxy credentials; then the on-phone ICE check.

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

### P9.1 — Mac menu-bar panel · M · deferred
- **Depends on:** P6.1, P6.5
- **Do:** a menu-bar item showing station activity, talk toggles and mic
  state, as a compact view of the same Console UI. Needs a UX addendum first.
- **Result:**

### P9.2 — Native Android Console · L · deferred
Phones use the PWA for now (D9). Revive this if P7.7's latency or
background-audio results are poor. Former cards:

#### (was P6.3) — Android build pipeline · L
- **Depends on:** F1 · **Touches:** `sonobus/mobile/`
- **Do:** get the Android build working from the hoisted layout
  (`sonobus/mobile/Builds/Android`), scripted (`scripts/build-android.sh`),
  producing a debug APK. Stock UI is fine for this task.
- **Done when:** APK installs and joins a group.
- **Result:**

#### (was P6.2) — Android shell · M
- **Depends on:** P6.3, P5.6
- **Result:**

#### (was P6.4) — Android mic permission + background audio · M · human
- **Depends on:** P6.2
- **Done when:** audio keeps flowing with the screen off on my phone.
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
