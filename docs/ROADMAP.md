# Roadmap — VDI remote audio

Turn this SonoBus fork from a jamming app into a two-way audio link between
VDIs and the devices I listen from (Mac, Android, web browser).

Phase status lives here. **Task status, dependencies and acceptance
criteria live in [TASKS.md](TASKS.md)**, which is the work queue for anyone
(people or agents) picking up work. Each item has an ID (`P1.2`) so commits and
PRs can reference it, e.g. `feat(roles): ... (P1.2)`.

**Status legend:** `todo` · `wip` · `done` · `blocked` · `deferred`

## Target system

Two roles. Every peer declares its role when it joins.

- **VDI** — runs on a VDI that already has a loopback device (captures system
  output) and a virtual mic (fed by SonoBus output). Sends mono system audio,
  receives the Console's mic. YAML-configured, no mixing UI.
- **Console** — where I listen. Mixes the VDIs (volume, pan, mute, solo) and
  talks back with my mic. Runs on Mac, Android, and in a container served to a
  browser.

Routing matrix (who hears whom):

| From ↓ / To → | VDI | Console |
|---|---|---|
| **VDI**     | ✗ | ✓ system audio, mono |
| **Console** | ✓ mic → VDI virtual mic | ✗ |

No monitor fader in either role.

## Overview

| Phase | Scope | Effort | Status |
|---|---|---|---|
| [UX](#ux--experience-design) | Experience design from scratch: brief, Console + VDI prototypes, spec (**Claude only**) | 1.5–2 w | todo |
| [P0](#p0--validation-spike) | Validation spike: VDI ↔ Mac over the VPN with stock app | — | done |
| [P1](#p1--roles-and-routing) | Roles and routing in the engine | 3–5 d | todo |
| [P2](#p2--vdi-agent-mode) | VDI agent mode: YAML, auto-connect, mono, status UI | 1–1.5 w | todo |
| [P3](#p3--strip-jam-features) | Remove jamming features | 3–5 d | todo |
| [P4](#p4--control-api) | Local control API on the engine | ~1 w | todo |
| [P5](#p5--console-ui-html) | Console UI, written once in HTML | 1.5–2 w | todo |
| [P6](#p6--native-shell-mac) | Native Mac shell embeds the Console UI + global PTT hotkey (Android/iOS native deferred, D9) | 3–4 d | todo |
| [P7](#p7--web-console-container) | Web Console container (replaces current `docker/`) | 1.5–2 w | todo |
| [P8](#p8--self-hosted-connection-server-deferred) | Self-hosted `aooserver` on the VPN | ~0.5 d | deferred |
| [P10](#p10--recording-and-meeting-recaps-deferred) | Per-station recording, session records (station + my mic while it hears me), transcription and meeting recaps | 2–3 w | deferred |

Order: UX starts immediately and runs alongside P0/P1. P0 → P1 → P2 gives a
usable Mac ↔ VDI system on the existing UI. P3 can run alongside P2. The
API schema (P4.5) waits for the UX2 design, then P4 → P5 → (P6, P7 in parallel).
Mobile goes through P7: the installed PWA (P7.7) is the phone Console for now (D9).
P8 whenever the public server becomes a problem (see P0 findings).

## Decisions

| ID | Decision | Why |
|---|---|---|
| D1 | Roles are enforced client-side (each app applies the routing matrix). No server enforcement. | All devices are mine and configured by me; group password is enough access control. |
| D2 | Keep the public connection server `aoo.sonobus.net` for now; self-host later (P8). | Lowest effort to start. Server is only rendezvous — audio is always peer-to-peer. |
| D3 | Role names: **VDI** and **Console**. | "Remote headset" undersells the mixing and talk-back side. |
| D4 | Console UI is built once in HTML, driven by a local control API; Mac/Android embed it in a web view, the web container serves it. | One UI to design and maintain across three platforms; the API is needed for web anyway. |
| D5 | Web = headless engine (Console role) in a container + HTML UI + WebRTC/Opus audio to the browser. VNC GUI and raw-PCM WebSocket from the current `docker/` are dropped. | Proper jitter handling, echo cancellation, a fraction of the bandwidth; no desktop-in-a-browser. |
| D6 | Role is advertised in the existing peer-info JSON (`SONOBUS_FULLMSG_PEERINFO`). New peers start send/recv-blocked until their role is known. | Reuses an existing channel; no audio leaks during the join window. |
| D7 | **All UX and visual design is done by Claude (lead session)**: brief, prototypes, spec, and approval of every UI implementation. The UI is designed from scratch for VDI remote audio; nothing in the SonoBus editor carries over by default. Helper agents implement to the spec and don't make design decisions. | One coherent design owner; the purpose changed completely, so the old jam UI isn't a starting point. |
| D10 | **VDI agent = native headless engine + local web UI.** The agent is the native engine (JUCE build per OS) running without its own window, started at boot. Its screens are an HTML page served by the agent on `localhost` (installable as a PWA, since Chrome treats localhost as secure) and built from the same components as the Console. A minimal native tray icon is a bonus, never required (GNOME shows no tray by default, and JUCE's Linux tray uses the old X11 tray that many desktops ignore). No Docker and no browser-based engine on VDIs. | Needs real device access, unattended start and direct UDP to Consoles. Docker can't reach host audio on Windows/macOS and is often not allowed on VDIs; a browser can't do UDP. Portability comes from building per OS. One UI technology across Console and agent. |
| D11 | **The Console is where VDI problems show up.** VDI agents run as services from fixed YAML config, which avoids picking the wrong device by hand. They report their own health to Consoles (P1.7), and the Console remembers known VDIs so a missing one shows as offline. The VDI's own UI (P2.6) and tray (P2.9) are secondary. | maelo checks everything from the Console, not on the VDIs. |
| D9 | **Mobile is the PWA for now.** Phones (Android, and iOS via Safari "Add to Home Screen") use the web Console installed as a PWA (P7.7). Native Android and iOS apps are deferred to P9. The Mac keeps its native shell (P6.1) for direct P2P audio and the global PTT hotkey. | Saves the Android build pipeline and native shells; the PWA reuses the same UI and container. Cost: one extra hop and less control over background audio. Revisit if P7.7's background test fails. |
| D8 | **Mesh, no hub.** VDIs connect directly to Consoles. The Ubuntu Studio + Carla server is retired once P2 replaces it (P2.8). | The hub's only purpose was to emulate the VDI/Console roles; roles make it redundant, and removing it saves a network hop of latency and a machine to maintain. Scale is small: 3 VDIs today, 4 at most. |

## Open questions

| ID | Question | Affects | Default if unanswered |
|---|---|---|---|
| Q1 | VDI OS — Windows, Linux, or both? | P2.5 (run as service), packaging | Build for both, service for whichever comes first |
| Q2 | Can several Consoles be connected at once (Mac + phone)? If yes, does the VDI virtual mic mix all of them? | P1 routing, P2 | Allowed; VDI mixes all Console mics |
| Q3 | ~~Does my mic go to all VDIs, or only selected ones?~~ **Answered:** open mic to all by default, with a per-VDI "hears you" toggle and push-to-talk as an option. Revisit after real use. | P1.5, P5 | — |
| Q4 | TLS for the web Console on a VPN IP — internal CA or a real domain? | P7.4 | Internal CA |
| Q7 | Recaps: which external transcription + summary models, and is meeting audio allowed to leave my infra? (Self-hosted ASR such as Whisper keeps the audio in-house; a hosted API is less work.) | P10.4, P10.5 | Undecided |
| Q8 | Where do recordings and recaps live: on the Console machine, or in a central store on my infra (needed for web/PWA sessions, whose engine runs in the container)? Retention? | P10.1, P10.6 | Central store on my infra, 30-day audio retention, recaps kept |
| Q6 | ~~Could the installed PWA replace the native Android app?~~ **Answered (D9):** yes, PWA first; native mobile deferred. Revisit if P7.7's latency or background-audio results are poor. | P6, P7 | — |

## Current setup (as of 2026-10-09)

Daily use today, built from stock SonoBus:

```
VDI-1 ─┐                Ubuntu Studio "audio server"
VDI-2 ─┼─ SonoBus ──►   Carla (JACK) hosting N SonoBus plugin instances,
VDI-n ─┘  (1 per VDI)   patched together by hand
                              │ one SonoBus instance sends a multichannel
                              ▼ stream (one channel per VDI)
                          Mac SonoBus session  (mixes the channels; mic goes
                                                back through Carla to the VDIs)
```

The hub exists only to emulate the VDI/Console roles (no processing in
Carla). Per D8 it is replaced by a direct mesh: each VDI reaches the Console
as its own named, mixable peer, which preserves what the multichannel stream
gives today. Scale: 3 VDIs, possibly 4.

## Known risks

- **Public server + VPN addressing.** A peer reports as its "local" address the
  interface it used to reach the server (`aoo/lib/src/client.cpp:640`). With the
  public server, that's the LAN/internet path, not the VPN IP, so peers may
  connect over the internet (hole punching) instead of the VPN — or fail behind
  corporate NAT. P0 checks this; P8 fixes it.
- **Tangled code.** `SonobusPluginEditor.cpp` (~6k lines) and
  `SonobusPluginProcessor.cpp` (~10k lines) mix jam features with core paths;
  P3 needs care.
- **Upstream merges.** Once P3 lands, `git subtree pull` from upstream SonoBus
  will conflict heavily. Treat the fork as diverged after P3.

---

## UX — Experience design

Owned by Claude (lead session), never delegated. See D7 and
[TASKS.md → Design ownership](TASKS.md#design-ownership).

| ID | Item | Status |
|---|---|---|
| UX1 | Design brief: purpose, contexts of use, jobs, principles, palette review | todo |
| UX2 | Console experience design + clickable prototype (desktop + phone) | todo |
| UX3 | VDI agent experience design + prototype | todo |
| UX4 | Implementation spec + component kit (tokens, states, a11y, copy) | todo |

**Done when:** the spec is approved and implementers can build P2.6 and P5
without asking design questions.

## P0 — Validation spike

Prove the network path before building anything. Stock app, no code changes.

| ID | Item | Status |
|---|---|---|
| P0.1 | Install stock SonoBus on one VDI; join a private group from the Mac via `aoo.sonobus.net` | done |
| P0.2 | Confirm which IPs the peers actually use: skipped, proven by daily use over the VPN | done |
| P0.3 | Route VDI loopback → SonoBus input, SonoBus output → VDI virtual mic; verify both directions by ear | done |
| P0.4 | Record findings: network OK, P8 stays deferred, Carla hub found → resolved as D8 (mesh) | done |

**Done when:** two-way audio works Mac ↔ VDI and the network path is known.

## P1 — Roles and routing

Engine-level, UI unchanged. Code: `sonobus/Source/SonobusPluginProcessor.{h,cpp}`.

| ID | Item | Status |
|---|---|---|
| P1.1 | `Role` enum (`VDI`, `Console`) on the processor; settable from CLI/config | todo |
| P1.2 | Add `"role"` to peer-info JSON in `sendRemotePeerInfoUpdate`; parse it in `handleRemotePeerInfoUpdate`; store on `RemotePeer` | todo |
| P1.3 | New peers start with send + recv disallowed until role known (`setRemotePeerSendAllow` / `setRemotePeerRecvAllow`) | todo |
| P1.4 | Apply the routing matrix when a peer's role arrives: same role → block both ways; opposite role → allow | todo |
| P1.5 | Per-VDI talk toggle on Console (gates Console → VDI send per peer) — pending Q3 | todo |
| P1.6 | Peers with no role (stock SonoBus) are blocked and shown as "unknown" | todo |
| P1.7 | VDI agent health in peer info: input device OK/missing, input signal present/silent, sending paused, config error; shown on the Console | todo |

**Done when:** two Consoles and two VDIs in one group produce exactly the matrix above.

## P2 — VDI agent mode

Code: `sonobus/Source/SonoStandaloneFilterApp.cpp` (existing CLI: `--group`,
`--username`, `--password`, `--server`, `--load-setup`, `--headless`).

| ID | Item | Status |
|---|---|---|
| P2.1 | `--config <file.yaml>`: server, group, password, username, role, input device, output device, codec/bitrate; device changes from the UI are written back | todo |
| P2.2 | YAML parser dependency (vendored, small — e.g. `yaml-cpp` or `rapidyaml`) | todo |
| P2.3 | VDI role locks: mono send, input monitor forced to 0, no mixing controls | todo |
| P2.4 | Auto-connect on launch; auto-reconnect with backoff on disconnect or device loss | todo |
| P2.5 | Start at boot, unattended: Windows scheduled task/service, systemd user unit on Linux (pending Q1) | todo |
| P2.6 | VDI agent web UI on `localhost`, an occasional repair tool with device pickers (D10, D11) | deferred |
| P2.7 | Example `vdi.example.yaml` + docs | todo |
| P2.8 | Cut daily use over from the Carla hub to the mesh; retire the Ubuntu Studio server | todo |
| P2.9 | Minimal native tray icon (not a priority: the agent is a service with fixed config, and problems surface on the Console) | deferred |
| P2.10 | Packages per VDI OS: Windows (installer/zip), Linux (AppImage or .deb), pending Q1 | todo |

**Done when:** a VDI boots, connects with no interaction, and survives a network drop.

## P3 — Strip jam features

Remove from both UI and engine paths where safe.

| ID | Item | Status |
|---|---|---|
| P3.1 | Metronome | todo |
| P3.2 | Soundboard | todo |
| P3.3 | Chat | todo |
| P3.4 | File playback (remove) and recording **UI** (remove); keep the recording engine for P10 | todo |
| P3.5 | Monitor faders / input monitoring UI | todo |
| P3.6 | Latency match, beat grid, other jam-only options | todo |
| P3.7 | Trim effects to what the UX2 design keeps | todo |
| P3.8 | Jitter buffer + codec settings: keep what UX2 exposes, fix the rest to defaults | todo |

**Done when:** desktop and Android builds compile and P1/P2 behaviour is unchanged.

## P4 — Control API

Local WebSocket + JSON API on the engine, used by every Console UI.

| ID | Item | Status |
|---|---|---|
| P4.1 | Embedded WebSocket server in the engine, bound to localhost by default | todo |
| P4.2 | State snapshot + change events: connection, peers (name, role, latency, health), per-peer volume/pan/mute/solo/talk | todo |
| P4.3 | Meters stream (~30 fps) | todo |
| P4.4 | Commands: connect/disconnect, set volume/pan/mute/solo/talk, master volume, mic mute / push-to-talk, device selection | todo |
| P4.5 | API schema documented in `docs/`, covering everything UX2 needs (written before P4.1) | todo |

**Done when:** a script can drive a full Console session without the GUI.

## P5 — Console UI (HTML)

Implements the UX2/UX4 design, from scratch rather than adapted from the
SonoBus editor. Every item needs design review by Claude before it's done.

| ID | Item | Status |
|---|---|---|
| P5.1 | Scaffold `console-ui/` from the UX4 prototype + mock API | todo |
| P5.2 | Mixer view per spec | todo |
| P5.3 | Talk-back controls per spec | todo |
| P5.4 | Connection / onboarding flow per spec | todo |
| P5.5 | Phone layout per spec | todo |
| P5.6 | Wired to the P4 API | todo |

**Done when:** the UI drives a real Console engine through the API and passes
design review.

## P6 — Native shell (Mac)

| ID | Item | Status |
|---|---|---|
| P6.1 | Mac: replace the JUCE editor with a web view loading the bundled Console UI | todo |
| P6.2 | Android shell → moved to P9.2 (D9) | deferred |
| P6.3 | Android build pipeline → moved to P9.2 (D9) | deferred |
| P6.4 | Android mic/background audio → moved to P9.2 (D9) | deferred |
| P6.5 | Mac global push-to-talk hotkey (works while another app is in front) | todo |

**Done when:** the Mac Console works end to end against a VDI.

## P9 — Later

| ID | Item | Status |
|---|---|---|
| P9.1 | Mac menu-bar panel: station activity, talk toggles, mic state | deferred |
| P9.2 | Native Android Console (was P6.2–P6.4): build pipeline, web-view shell, mic + background audio | deferred |
| P9.3 | Native iOS Console: Xcode build from the monorepo, web-view shell, background audio | deferred |

## P7 — Web Console container

Replaces the current `docker/` job (VNC + raw PCM bridge).

| ID | Item | Status |
|---|---|---|
| P7.1 | Container runs the engine headless in Console role, joins over the VPN | todo |
| P7.2 | Serves the P5 Console UI; proxies the P4 API | todo |
| P7.3 | WebRTC (Opus) audio between browser and engine, both directions | todo |
| P7.4 | HTTPS with a trusted cert so the browser allows the mic — pending Q4 | todo |
| P7.5 | One container per web user; compose file + docs | todo |
| P7.6 | Remove the VNC / Xvfb / raw-PCM bridge pieces | todo |
| P7.7 | Installable PWA: manifest + service worker, "Install" in Chrome on Mac and Android | todo |

**Done when:** a browser on the VPN mixes VDIs and talks back with no install.

## P10 — Recording and meeting recaps (deferred)

On the roadmap, not scheduled. Builds on SonoBus's existing per-user recording
(`RecordIndividualUsers`, `RecordSelf`), which P3.4 keeps in the engine.

**Session record (the key idea):** for each station, record a time-aligned
pair:
1. the station's audio (pre-fader, so my mute, dim and level don't affect it);
2. my mic, **only while that station actually hears me** (the same talk
   gate as P1.5, so talk toggles, solo and push-to-talk all apply). Silence
   otherwise.

That gives one clean conversation per station even with two calls running at
once: what that station said, and what I said *to it*, never what I said to
the other call. Two sources per file also makes speaker attribution simple
("Me" vs the station). Separating several remote participants inside the
station track still needs diarization.

| ID | Item | Status |
|---|---|---|
| P10.1 | Per-station recording: each station to its own file, pre-fader (FLAC) | deferred |
| P10.2 | Session record mode: per station, 2 aligned tracks (station + my gated mic), from the P1.5 talk gate | deferred |
| P10.3 | UX addendum (Claude): record controls per station and global, an unmistakable recording indicator, consent reminder, browsing sessions and recaps | deferred |
| P10.4 | Transcription: external ASR per track with timestamps, merged into a dialogue ("Me" / station name), diarization inside the station track | deferred |
| P10.5 | Meeting recap: external model turns each station's transcript into a recap (summary, decisions, action items, open questions) | deferred |
| P10.6 | Storage, retention and privacy: where files live (Q8), retention, provider data policy (Q7), deletion | deferred |

**Constraints:**
- Recording other people's calls needs their consent in many jurisdictions;
  the UI should make recording visible and remind me (P10.3).
- Web/PWA sessions record in the container, not on the phone.

## P8 — Self-hosted connection server (deferred)

Move rendezvous off `aoo.sonobus.net` onto my infra, reachable at a VPN IP, so
peers exchange VPN addresses and audio stays inside the VPN. Not a relay —
audio still goes peer-to-peer; `aoo` here has no relay.

| ID | Item | Status |
|---|---|---|
| P8.1 | Dockerfile for `aooserver/` (Linux build, port 10998 TCP+UDP) | deferred |
| P8.2 | Deploy on a VPN-reachable host; logging via `-l` | deferred |
| P8.3 | Point VDI YAML + Console defaults at it | deferred |
| P8.4 | Verify peers connect over VPN IPs (repeat P0.2) | deferred |

**Trigger to start:** P0 shows peers connecting over the internet or failing
behind NAT, or I want group metadata off the public server.
