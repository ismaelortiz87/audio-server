# Crosspoint UI spec (UX4)

Status: **v1, ready to build** · Owner: Claude (design) · Date: 2026-10-09

This spec is what P2.6 (agent localhost UI), P5.x (Console UI), P6.1 (Mac
shell) and P7.7 (PWA) are built and reviewed against. Where this spec and a
prototype disagree, **this spec wins**; where the spec is silent, the
prototype is the reference. If neither answers a question, stop and ask: set
the task to `blocked` and write the question in its *Result* (see
[TASKS.md → Design ownership](../TASKS.md#design-ownership)).

| Input | File |
|---|---|
| Brief, principles, decisions | [brief.md](brief.md) |
| Tokens (colours, type, sizes, motion, audio constants) | [tokens.json](tokens.json) |
| What the engine provides | [console-needs.md](console-needs.md) |
| Console prototype | [design/prototypes/console/](../../design/prototypes/console/index.html) (`?scenario=everyday\|twocalls\|problem\|four\|connecting&view=phone`) |
| Agent prototype | [design/prototypes/vdi/](../../design/prototypes/vdi/index.html) (`?scenario=listening\|talking\|waiting\|paused\|reconnecting\|password\|nodevice`) |
| App icon | [design/icon/](../../design/icon/) |

---

## 1. Foundations

### 1.1 Colour roles (strict)
Every colour has one job. No role borrows another's colour.

| Role | Tokens | Used for | Never for |
|---|---|---|---|
| Surfaces | `bg rail panel panel2 raised line line2` | backgrounds, borders | meaning |
| Text | `ink ink2 ink3 ink4` | all text | — |
| Chrome accent | `accent accentInk accentWash` | focus ring, selected station, primary button, Solo-on, selected picker row | station identity, status |
| Status | `good warning serious critical` | link health, agent problems | identity, decoration |
| Station identity | `station.s1–s4` | swatch, activity strip, placement puck swatch | text, status |
| Live | `live liveWash liveLine liveInk onLive` | my voice is going somewhere (mic button hot, live band, "Hears you", "TALKING") | errors, anything else |

**Shapes carry the role as well as colour:** station identity is a 10 px
**rounded square** (`radius.swatch` 3 px), and status is an 8 px **circle**
that is always followed by a word. This keeps them apart for every colour
vision (see the validation note in `tokens.json`).

**Station colour assignment:** the first time the Console sees a station, it
gets the lowest free index 0–3, which is stored and never reshuffled. A 5th
station (beyond the planned maximum) reuses colours in order and shows its
name. Names are always visible, so identity never depends on colour alone.

### 1.2 Type
System sans for UI, mono with tabular figures for every number (ms, dB, %,
times, ports, paths). Scale in `tokens.json → type.scale`. Labels are 11 px
uppercase with 0.09 em tracking, in `ink3`.

### 1.3 Spacing, radius, sizes
4-pt scale (`space`). Panels 10 px radius, controls 8 px. Desktop controls are
≥ 38 px tall; **on phone every tappable target is ≥ 44 × 44 px**.

### 1.4 Motion
150–200 ms ease for state changes, pulses only for "in progress" and LIVE. With
`prefers-reduced-motion: reduce`: no pulses, no puck slide animation, and
state changes are instant. Meters keep updating (they are data, not
decoration) but must not ease.

### 1.5 Theme
Dark only. Body background `bg`. `color-scheme: dark`.

---

## 2. Console layout

```
┌ Top bar: Crosspoint · connection status ───────────────── ⚙ ┐
├ Live band (only when relevant) ─────────────────────────────┤
│ Placement field                                [Spread][All centre] │
│ Notices (unknown peers)                                     │
│ Stations grid: one card per station (online first, then offline) │
├ You bar: [Mic button] meter [Open mic|Push to talk] targets … Output ┤
└ Key hint (desktop only) ────────────────────────────────────┘
```

- Width is measured on the **app container**, not the viewport (CSS container
  queries). **Phone ≤ 640 px.**
- Desktop: stations grid `repeat(auto-fit, minmax(min(240px,100%), 1fr))`,
  16 px gaps. With 4 stations at 1280 px they sit in one row.
- Phone: stations stack in one column (10 px gaps). The You bar puts the mic
  button full width on top, then meter + mode + targets, then Output.
- Main area scrolls; the top bar, live band and You bar are fixed.
- Station order: by colour index (stable), then remembered offline stations
  last. Never reorder on activity.

---

## 3. Console components

### 3.1 Top bar
`Crosspoint` (brand, 600) · connection status · spacer · settings button.

| State | Shows |
|---|---|
| connecting | warning circle (pulsing) + "Connecting to **{group}**…" |
| connected | good circle + "Connected" + `· {group} · {online} of {known} stations` (group/count hidden on phone) |
| reconnecting | warning circle (pulsing) + "Reconnecting… next try in {s}s" |
| failed | critical circle + reason ("Wrong group password", "Server unreachable") + **[Settings]** |

### 3.2 Live band
A full-width strip under the top bar: the single loudest element.

| Condition (engine state) | Style | Text |
|---|---|---|
| transmitting, every known station hears me | live | `LIVE` pill + "All {n} stations hear you" |
| transmitting, some hear me (the rest includes lost/offline stations) | live | `LIVE` + "{A and B} hear(s) you · {C} doesn't / {C and D} don't" |
| transmitting, solo narrowing | live | `LIVE` + "Solo: only {A} hears you" |
| transmitting, nobody hears me | quiet | "Mic open, but no station is set to hear you." |
| open mode, mic off | quiet | "Mic off. No station hears you." |
| PTT mode, not held | quiet | "Push to talk: hold the mic button or Space. Nobody hears you now." |
| connecting / failed | hidden | — |

Live style: `liveWash` background, 2 px `live` bottom border, `liveInk` text
600, pill filled `live` with `onLive` text. Quiet: `panel` background, `line`
border, `ink2` text, no pill. `role="status" aria-live="polite"`.
Names use the short station name (without a `VDI-` prefix).

### 3.3 Placement field
The stereo field as one horizontal track with 5 ticks (L, ½L, C, ½R, R; C
taller). The ends are labelled `L` and `R` inside an inset (`--pad` 56 px
desktop, 52 px phone) so pucks at ±1 are never clipped.

**Puck** = pill with swatch + short name (+ live mic glyph when that station
hears me). States: default · **selected** (2 px `accentInk` outline) ·
**dimmed** (`ink3` text, swatch 50%) · **muted** (`ink4`, line-through,
swatch 30%) · **live** (pink mic glyph only, no pink border). Pucks are
opaque, so ticks never show through.

- **Drag:** pointer capture, value = position mapped through the inset, clamp
  −1…1, **snap** to a tick within ±0.07, otherwise round to 0.01. No easing
  while dragging; 180 ms ease otherwise.
- **Stacking:** pucks whose pixel extents would overlap go to the next row,
  computed from real widths (+6 px gap), up to 4 rows. Recompute on resize.
- **Keyboard:** each puck is `role="slider"` with `aria-valuetext` (e.g. "L 50",
  "Centre"); ←/→ jump to the previous/next snap.
- **Spread** sets stations evenly from −1 to +1 in station order (1 station →
  centre). **All centre** sets every pan to 0. Both are single actions.

### 3.4 Notices
Panel with a 3 px `ink3` left border. Unknown peer: "**{name}** joined the
group without a role, so no audio is exchanged with it. *It's probably running
stock SonoBus.*" One notice per unknown peer, newest last.

### 3.5 Station card
`panel` background, 1 px `line` border, 10 px radius, 14 px padding (12 on
phone), 12 px vertical gap.

**Header:** swatch · **full name** (15/650, wraps, never truncates) · badges ·
then on its own line, indented to the name, the **health line**.

| Badge | When | Style |
|---|---|---|
| `DIMMED −18` | another station is soloed and this one isn't muted | `panel2` + `line` border, `ink2`, 10 px/700, uses the configured dim |
| `MUTED` | muted | `panel2` + `line` border, `ink3` |

No LIVE badge on cards: the talk button carries it.

**Health line** (circle + word + mono numbers in `ink3`; numbers truncate first):

| Presence/health | Circle | Word | Numbers | Tooltip |
|---|---|---|---|---|
| online · clear | good | Clear | `{lat} ms` | "Latency {lat} ms · loss {loss}% · buffer {buf} ms" |
| online · unstable | warning | Unstable | `{lat} ms · {loss}% loss` | "Audio may break up. The network buffer is growing to compensate." |
| lost | critical, pulsing | Lost | `reconnecting {s}s` | "No audio from this station. Retrying automatically." |
| offline | `ink4` | Offline | `last seen {HH:MM}` (or `{date}` if not today) | "A station this Console knows, but not connected now. Its settings are remembered." |

**Health thresholds** (one place, engine or UI; P4.5 decides which side):
`unstable` if loss > 1% over 10 s, or latency > 120 ms, or the jitter buffer
grew in the last 10 s; back to `clear` after 10 s under all thresholds
(hysteresis). `lost` after 3 s with no packets from an online peer. `offline`
when not in the group.

**VDI issue line** (from agent health, P1.7), shown under the header when any
agent field is not ok: warning-bordered box (`rgba(warning,.45)` border, 3 px
warning left border, 7% warning wash), text `ink2`, lead-in **"VDI reports:"**
in `#ffd98a`. Copy:

| Agent state | Text |
|---|---|
| input missing | "Input device "{node}" isn't on the VDI. No system audio is being sent." |
| input silent | "Input silent for {n} min. The loopback device on the VDI may be wrong or the VDI is muted." |
| output missing | "Virtual mic "{node}" isn't on the VDI. Apps there can't hear you." |
| paused | "Sending is paused on the VDI." |
| config error | "Config problem: {message}" |

**Activity strip:** 72 px desktop / 36 px phone, `bg` background, 6 px radius,
64 bars scrolling right-to-left at ~20 fps, from the **pre-fader** station
level. Fill = station colour; alpha 0.9 normal, **0.4 dimmed**, **0.18
muted** (still moving), `ink4` 0.5 when lost/offline. Overlay text (11 px/600,
centred): "Muted" (`ink2`), "No audio" (lost), "Not connected" (offline).

**Place row:** label `PLACE` (64 px column, 52 phone) · 5-segment snap control
`L · C · R` (labels `L`, `·`, `C`, `·`, `R`; `role="radiogroup"`, each a
`radio` with `aria-label` "Left 100"…"Right 100"; a sliding thumb marks the
current value; values between snaps put the thumb at the nearest snap but the
readout shows the exact value) · readout `L 100` / `Centre` / `R 50` (mono).

**Level row:** label `LEVEL` · range −40…+6 dB, step 0.5, filled track to the
thumb · readout `−6.0 dB` (mono, with true minus sign). Double-click/tap the
slider resets to 0 dB. ↑/↓ and PageUp/PageDown when focused.

**Actions row** (grid 1fr 1fr 1.8fr, 8 px gap, no wrapping):

| Button | States |
|---|---|
| **Mute** `M` | off: `panel2`/`ink2` · on: `#2a2f35`, `ink`, `ink3` border, `aria-pressed=true` |
| **Solo** `S` | off · on: `accentWash`, `accentInk`, `accent` border. Tooltip "Dims the others by {n} dB, and only soloed stations hear your mic" |
| **Talk** `T` | see table |

| Talk button state | Condition | Label | Style |
|---|---|---|---|
| hears you (live) | `hearsYou` | mic + "Hears you" | `liveWash`, `liveInk`, `liveLine` border, pink mic glyph |
| will hear you | talk on, mic not transmitting, or station lost/offline | mic + "Will hear you" | `ink`, `ink3` border |
| doesn't hear | talk off | crossed mic + "Doesn't hear" | `ink3`, dashed border |
| paused by solo | talk on, another station soloed | crossed mic + "Paused by solo" | `ink3`, dotted border, no background, **disabled** (click does nothing), tooltip "Another station is soloed, so only it hears you. Un-solo to restore." |

Key hints (`<kbd>`) show on desktop only.

**Card states:** selected = 1 px `accent` border + inset 1 px `accent` ring;
lost/offline = whole card at 70% opacity (controls still work and persist).

### 3.6 You bar
`rail` background, top `line` border, 12 px/16 px padding, wraps.

**Mic button** (min 190 px wide, 52 px desktop / 64 px phone and full width,
12 px radius, 2 px border):

| Mode / state | Label | Sub | Style |
|---|---|---|---|
| open · on · ≥1 hears | mic + "Mic live" | "Click to turn off" | filled `live`, `onLive` text |
| open · on · nobody hears | mic + "Mic live" | "Click to turn off" | `panel2`, `ink4` border, `ink` text |
| open · off | crossed mic + "Mic off" | "Click to open mic" | `panel2`, `line` border, `ink2` |
| ptt · idle | crossed mic + "Hold to talk" | "or hold Space" | idle style |
| ptt · held | mic + "Talking…" | "Release to stop" | filled `live` |

PTT: pointer down = held; up / leave / cancel = released. Space (keyboard)
the same. On touch, press-and-hold with no long-press menu
(`touch-action: none`, no text selection).

**Mic meter:** 120 px × 6 px (fills remaining width on phone); fill `live`
when transmitting to ≥1 station, else `ink3` (local level only).

**Mode** segmented: `Open mic | Push to talk`. **Targets** text: "Talking to
**all stations**" / "Talking to **{A and B}**" / "Talking to **nobody**".
**[Talk to all]** button appears only when at least one station has talk off
(not when stations are only paused by solo). **Output** label + slider (same
as Level) + readout.

**Key hint line** (desktop, 11 px, `ink4`): "Keys: 1–4 pick station · ← →
place · M mute · S solo · T hears you · ` mic on/off · hold Space to talk
(push-to-talk mode)".

### 3.7 Settings sheet
Right sheet, `raised` background, max 380 px (full width on phone), scrim
`#0009`; Esc or scrim click closes. Fields: Server · Group (+ "Password saved")
· Your name · Microphone · Output · Solo dims others by · Push-to-talk key
(Mac only: "works in other apps") · **Advanced** (collapsed): Codec, Network
buffer ("Auto (currently {n} ms)") · [Disconnect] · Remembered stations list
with **Forget** per offline station.

### 3.8 Connecting / empty
While connecting with no known stations: 3 dashed skeleton cards "Waiting for
stations…". Connected with no stations: one card "No stations connected yet.
VDIs appear here when their agent joins **{group}**."

### 3.9 Web audio link (web Console / PWA only)
Shown only when the origin has an audio gateway (`rtc/config` answers, i.e. the
web container), directly under the top bar. Hidden once audio is connected
with a working mic. Browsers allow mic capture and playback only after a user
gesture, which is why this exists at all.

| State | Circle | Text | Button |
|---|---|---|---|
| idle | `ink4` | "Audio isn't connected in this browser." | **Start audio** (primary) |
| starting | warning, pulsing | "Connecting audio…" | — |
| failed | critical | "Audio connection failed: {reason}. Retrying…" | Retry now |
| connected, mic blocked | warning | "Microphone blocked: you can listen, but stations can't hear you. Allow the mic in this site's settings." | — |

Mic capture uses echo cancellation and noise suppression (speech, possibly
phone speakers). The browser always sends its mic to the gateway; who hears it
is still decided by the engine (§4.1). Reconnects with backoff (1 s → 15 s, 5
tries) while the user wants audio.

---

## 4. Console behaviour

### 4.1 Routing is the engine's job
The UI **never decides** who hears me. It shows `hearsYou` from the engine
(console-needs §1.2) and sends user choices (`talk`, `solo`, mic mode/on, PTT).
Rules the engine implements (P1.5), restated here for review:
`hearsYou = talk && online && micTransmitting && !(anySolo && !solo)`.

### 4.2 Selection and keyboard
One station is selected (starts at the first). Clicking anywhere on a card or
puck selects it. Shortcuts are ignored while typing in an input.

| Key | Action |
|---|---|
| `1`–`4` | select station n |
| `←` / `→` | selected station to previous / next snap |
| `M` / `S` / `T` | toggle mute / solo / talk on selected (T ignored while paused by solo) |
| `` ` `` | mic on/off (open mode) |
| `Space` (hold) | talk (PTT mode) |
| `Esc` | close settings |
| global hotkey (Mac, P6.5) | hold to talk, works in other apps |

### 4.3 Persistence
Engine-owned (console-needs §3). Solo resets on restart. Mic mode persists; the
mic's on/off state persists in open mode (the default is open, per brief).

---

## 5. Accessibility checklist (design review uses this)
- Text contrast ≥ 4.5:1 on its surface (ink/ink2 pass on all surfaces; `ink3`
  is for labels and metadata only, never for body copy or values you must read).
- Non-text marks ≥ 3:1 (all station, status and live colours pass on `panel`).
- Nothing relies on colour alone: swatch shape + name, status circle + word,
  live = label + mic glyph, mute/dim = badge + motion change.
- Visible focus: 2 px `accentInk` outline, 2 px offset, on every control.
- Roles: toggles use `aria-pressed`; place control `radiogroup`; pucks
  `slider` with `aria-valuetext`; live band `role="status" aria-live="polite"`;
  sliders labelled "Level for {name}".
- Phone: targets ≥ 44 px; no hover-only information (tooltips duplicate visible
  text or are reachable by tap on the health line).
- Reduced motion honoured (§1.4).

---

## 6. Agent localhost UI (P2.6)

Served by the agent at `http://localhost:<port>` (default 7071), localhost
only. Same tokens and components as the Console. Single column, max 440 px,
centred; on wide screens nothing stretches. PWA manifest included (installable
on localhost).

**Layout:** identity row (swatch in `ink4`, because the VDI doesn't know the
colour a Console assigned it; agent name 20/700; chip "Crosspoint agent") ·
**state card** · Listening now · Audio (Sends / Plays your voice into) ·
footer (Pause/Resume sending · Reload config · config path, truncated from the
left so the file name stays visible).

**State card** (circle + big word + sentence + meta + inline actions):

| State | Circle | Word | Sentence | Actions |
|---|---|---|---|---|
| connected, ≥1 Console | good | Connected | "Sending this VDI's audio to {n} Console(s)." | — |
| connected, no Console | `ink4` | Waiting for a Console | "Connected and ready. Audio starts the moment a Console joins." | — |
| paused | warning | Sending paused | "Consoles hear silence from this VDI. You can still be heard through the virtual mic." | — |
| reconnecting | warning, pulsing | Reconnecting… | "Can't reach the connection server. Consoles can't hear this VDI until it's back." meta: "{server} · attempt {n} · next try in {s}s" | Retry now |
| bad password | critical | Needs attention | "The server rejected the password for group "{group}"." meta: "Fix `password:` in {path}, then reload. Nothing is sent until then." | Open config · Reload config |
| device missing | critical | Needs attention | "The input node in vdi.yaml, "{node}", isn't on this machine." meta: "Pick the device that carries this VDI's system audio below. Your choice is saved to vdi.yaml." | (picker opens inline) |

Border tint: warning states `rgba(warning,.45)`, critical `rgba(critical,.6)`.

**Listening now:** one row per Console: status circle (good) or `TALKING`
pill (live) when its voice is arriving, then name, kind ("Console · Mac",
"Console · web"), latency (mono). Empty: dashed "No Console connected right
now." Hidden while reconnecting or in error.

**Device panels:**
- **Sends** ("system audio → Consoles"): device selector (name; missing =
  struck-through in `#f0a2a2` with a critical border on the panel) · meter ·
  foot line + dB readout. **The meter is `accentInk` only when audio is
  really being sent; `ink3` when the level is local only** (the agent doesn't
  know its Console-assigned station colour, so it never uses station colours) (paused, offline,
  no Console). Foot copy: "Mono to Consoles" / "Paused: local level only,
  Consoles hear silence" / "Not sending: local level only" / "Local level:
  sending starts when a Console joins".
- **Plays your voice into** ("virtual mic for apps on this VDI"): selector ·
  `live` meter of incoming voice · foot "{console} is talking" / "Silent: no
  Console is talking" · **Play test tone**.
- **Picker** (opens under the selector): one row per PipeWire node with radio,
  description and node hint. **Inputs show a live mini-meter per row**;
  **outputs show a "Test tone" button per row** (outputs can't be metered).
  Selecting switches live, saves to the YAML, closes the picker and shows
  "✓ Saved to vdi.yaml · Undo" (Undo restores the previous device). No Undo
  when the previous device was missing, since switching back to it would only fail.

**Behaviour:** Pause affects sending only. Reload re-reads the YAML and shows
errors in the state card. Everything here is also reflected on the Console
via P1.7 (D11).

---

## 7. App icon and PWA

**Icon** ([design/icon/](../../design/icon/)): a crosspoint, i.e. one active
route (in from the left, out the bottom, `ink2`) through a lit node
(`accentInk` ring) on a faint matrix, on a `panel` tile. Files:

| File | Use |
|---|---|
| `crosspoint.svg`, `crosspoint-1024.png` | source; macOS app icon (`ICON_BIG`) |
| `crosspoint-256.png` | `ICON_SMALL`, Linux `.desktop`/`.deb` |
| `crosspoint-32.png` | favicon |
| `pwa-192.png`, `pwa-512.png` | manifest `any` |
| `crosspoint-maskable.svg`, `pwa-maskable-512.png` | manifest `maskable` (art inside the 80% safe zone) |

Wiring the icon into `sonobus/CMakeLists.txt` (`ICON_BIG`/`ICON_SMALL`) is a
one-line follow-up to P3.9, to do when nobody else has CMake open.

**Manifest** (Console, P7.7; agent, P2.6):

```json
{
  "name": "Crosspoint",
  "short_name": "Crosspoint",
  "id": "/",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#0b0d0f",
  "theme_color": "#0d1013",
  "icons": [
    { "src": "icons/pwa-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any" },
    { "src": "icons/pwa-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any" },
    { "src": "icons/pwa-maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable" }
  ]
}
```
Agent: `"name": "Crosspoint agent"`, same icons.

---

## 8. Implementation notes

- **One UI codebase, `console-ui/`** (P5.1), with two entry points: `console`
  and `agent`. Start from the prototypes' HTML/CSS: they already use the
  tokens. Turn tokens into CSS custom properties generated from
  `tokens.json`, never hand-copied.
- Keep it light: plain TypeScript + small components, or a minimal framework if
  it pays for itself. No CSS framework. No icon font (inline SVG for the 3
  glyphs: mic, crossed mic, gear).
- Activity strips and meters draw on `<canvas>` at device pixel ratio, fed
  by the meters stream (console-needs §1.5), ~30 fps, drawing paused when the
  page is hidden.
- The mock API (P5.1) implements the same messages as P4.5 and reproduces the
  prototype scenarios, so every state in this spec can be screenshotted for
  review.
- Mac shell (P6.1): the web view loads the bundled `console-ui` and talks to the
  local engine. Window minimum 360 × 560; title "Crosspoint".

## 9. Design review checklist (for every `design review` task)
1. Screenshots of every state listed for the components touched, desktop and
   phone (Console) or the agent width.
2. Colour roles respected (§1.1); no new colours.
3. Copy matches §3/§6 exactly, or the PR lists changes for approval.
4. Station names never truncated; numbers in mono.
5. Accessibility checklist §5.
6. Reduced motion checked.
7. Keyboard map §4.2 works (Console).
