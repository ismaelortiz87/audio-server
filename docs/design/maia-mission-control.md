# Maia Mission Control — colour palette (SonoBus / audio-server)

Status: **design reference only.** Nothing in this file is wired into code yet. It is
the agreed palette for a future appearance/UX pass on SonoBus.

Source of truth for values: [`tokens.json`](./tokens.json) — keep the two in sync.

A dark-only, calm operations-console look: warm off-white text on near-black
graphite surfaces. No cyan, no neon, no gradients on chrome. Colour carries
meaning; everything else stays neutral.

## Surfaces — darkest to lightest, slightly cool graphite

| Token | Hex | Use |
|-------|-----|-----|
| `bg` | `#0b0d0f` | the page / app background |
| `rail` | `#0d1013` | rail, sidebar, toolbars |
| `panel` | `#12161a` | cards and panels |
| `panel2` | `#161b20` | insets, hover fills, column backgrounds |
| `raised` | `#1c2228` | popovers, tooltips, floating cards |
| `line` | `#232b32` | borders and dividers |
| `line2` | `#1a2026` | subtle inner dividers, grid lines |

## Text — warm off-white, four steps

| Token | Hex | Use |
|-------|-----|-----|
| `ink` | `#e8e4da` | primary text, titles, key numbers |
| `ink2` | `#a8a69d` | secondary text, body copy |
| `ink3` | `#71746e` | muted labels, metadata, axis labels |
| `ink4` | `#4a4e4b` | disabled, placeholders, faint marks |

## Accent — chrome only, never used to encode data

| Token | Value | Use |
|-------|-------|-----|
| `accent` | `#2e6b66` | deep petrol teal: active borders, focus rings, selected controls |
| `accentInk` | `#8fc3ba` | soft sea-glass: accent text, active icons, "now" markers |
| `accentWash` | `rgba(46,107,102,0.18)` | selected-row and active-item backgrounds |

## Status — reserved for health and urgency only

Always paired with a text label or icon, never colour alone.

| Token | Hex | Meaning |
|-------|-----|---------|
| `good` | `#0ca30c` | healthy |
| `warning` | `#fab219` | degraded |
| `serious` | `#ec835a` | serious |
| `critical` | `#d03b3b` | critical |

Natural fits in SonoBus: connection/link state, network statistics (jitter, packet
loss, buffer underruns), recorder state, and server-reachability indicators.

## Categorical — one colour per thing, colour-blind safe on `panel`

The palette's categorical set was defined against other product surfaces. In
SonoBus the equivalent role is **per-channel-group / per-peer identity**, which is
where these belong; keep them off chrome and out of status duty.

| Token | Hex | Note |
|-------|-----|------|
| `series1` | `#9085e9` | soft violet |
| `series2` | `#199e70` | jade green |
| `series3` | `#c98500` | ochre / amber |

## Rules

1. Dark theme only (`color-scheme: dark`).
2. The petrol accent is for interface chrome, never for a data series.
3. Status colours are never reused as categorical colours.
4. Text always uses the ink tokens, never a series colour; a small coloured dot or
   mark next to the text carries the category.
5. Keep marks thin and saturation restrained; let surfaces stay quiet so the few
   coloured elements stand out.

## Type and shape (reference)

- Sans: `ui-sans-serif` / SF Pro / Inter.
- Mono, for numbers, ids and times, with tabular figures: SF Mono / JetBrains Mono.
  SonoBus is dense with numbers (latency, bitrate, channel counts, buffer sizes) —
  this is the main typographic change to make.
- Panel corner radius: 10px.

## Dropped from the original spec

- The named categorical sources (Teams / Calendar / Boards / Collector) — those
  are dashboard-specific. Only the three validated values are carried over, as a
  generic `series*` set (see above).
- The "Collector uses the status colours" rule — no equivalent object in SonoBus.

## Where this would land in the code

Not part of this change, listed so the redesign has a starting point:

- `sonobus/Source/SonoLookAndFeel.cpp` / `.h` — panel, background, outline,
  slider and button chrome; the single highest-leverage place to apply surfaces,
  lines and the accent.
- `sonobus/Source/SoundboardButtonColors.h` — soundboard pad colours (categorical).
- `sonobus/Source/ChannelGroup.cpp` / `ChannelGroupsView.cpp` — per-group identity
  colours (categorical).
- `sonobus/Source/JitterBufferMeter.cpp`, `PeersContainerView.cpp` — network
  health readouts (status).
- `sonobus/Source/ChatView.*` — message text and metadata (ink steps).
