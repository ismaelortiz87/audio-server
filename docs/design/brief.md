# Design brief — VDI remote audio

Status: **draft for review** (UX1). Owner: Claude (design). Inputs:
[ROADMAP.md](../ROADMAP.md) D1–D8, [maia-mission-control.md](maia-mission-control.md).

## What this product is now

A private **intercom between me and a few remote machines**. Each VDI plays
its system audio to me and hears my voice as its microphone. I sit at one end
(Mac, phone or browser) and the VDIs sit at the other. It is not a jam
session, a mixing desk or a conferencing app, and it should look like none of
them.

The closest real-world model is a **broadcast intercom key panel**: one key
per remote station, each with *listen* and *talk*, built so the operator
always knows who they can hear and who can hear them. We use that mental
model, not the shape of the hardware.

## Who and where

One person (me), with all devices on one VPN and 3–4 VDIs.

| Context | Device | Situation | What it means for design |
|---|---|---|---|
| Desk | Mac | Working all day in other apps; VDI audio in the background | Must live quietly beside other work. Readable at a glance, and the main actions work without bringing a window forward (shortcuts, menu bar). |
| Away from the desk | Android phone | One hand, short sessions, sometimes noisy surroundings | Big touch targets for talking; listening controls one tap away; nothing that needs precision dragging. |
| Any machine | Browser | Same jobs as the Mac, through the web container | Identical behaviour and layout to the Mac; nothing to install. |
| On the VDI | VDI agent | Unattended; I look at it only when something is wrong | Says plainly "running and connected" or what's broken; almost no controls. |

## Jobs to be done

**Console**
1. **Hear all my VDIs at once and tell them apart.** Each one sits in its
   own place in the stereo field (that's what pan is for) and has its own
   level.
2. **Focus on one** when something matters, without losing track of the
   others (solo).
3. **Silence one** that's noisy or irrelevant (mute).
4. **Talk into one VDI, or into all of them, and be certain where my voice
   is going.** My mic is a microphone inside a remote session. A hot mic in
   the wrong place is the most damaging mistake this product can allow.
5. **Know each link is healthy**, and be told, in words, when one isn't.
6. **Get connected with no effort.** Opening the app means I'm connected to
   my VDIs.

**VDI agent**
7. **Confirm it's running and connected**, and see who's listening.
8. **Diagnose a broken setup**: wrong device, wrong password, server
   unreachable. The message says what's wrong and where to fix it.
9. **Pick the input and output devices on screen**: input = the loopback
   that captures system audio (sent to Consoles); output = the virtual mic
   the VDI's apps use (receives my voice). Each picker shows a live level so
   I can confirm I chose the right device. A choice is saved back to the
   YAML config, which stays the single source of truth. If a configured
   device is missing, the picker is what the error state offers.

## Design principles

1. **Stations, not channels.** Each VDI is a named place I listen to and
   talk to, not a numbered strip. Its name, its activity and its state are
   the main content; controls come second.
2. **The mic is never ambiguous.** Whether I'm talking, and to whom, is
   visible from across the room, at all times, on every screen size. Per
   maelo's call, the default is an **open mic to all stations**. Because of
   that, taking one station out of my talk path, and seeing who hears me,
   must each be a single, obvious action. Push-to-talk is an option, not the
   default.
3. **Glance, don't study.** Health is shown in words first ("Clear",
   "Unstable", "Lost"), with numbers like latency, jitter and loss one step
   away. Activity shows as motion on the station itself, not as a separate
   meter bridge.
4. **Built for four, not forty.** All stations are always visible: no
   scrolling, no paging, no list management. Every station gets generous
   space.
5. **Calm until it matters.** In normal operation the screen is quiet and
   nearly monochrome. Colour and motion are spent on three things only:
   station identity, live talk, and problems.
6. **Nothing to set up in daily use.** Server, group, devices and codec are
   configuration, not daily controls. They live in one settings place and
   are pre-filled from config.
7. **Same panel everywhere.** One design that adapts from a phone to a wide
   desktop window. The phone version isn't a cut-down mode, just a
   rearrangement.

## What we carry over, and what we don't

- **Nothing from the SonoBus editor carries over by default.** No faders
  bank, no peer list with expanding panels, no options sheets.
- Concepts that do carry over, redesigned: per-VDI **level**, **placement**
  (pan), **mute**, **solo**; my mic level; connection to a group on a server.
- **Out of scope for now:** recording and recaps (roadmap P10, with its
  own UX addendum, P10.3). **Out of scope:** chat, soundboard, metronome, file playback,
  effects racks, monitor fades, latency matching, public groups, multiple
  channel groups per peer, and any stereo send from VDIs.

## Visual direction: palette review

The Maia Mission Control palette fits this product: a calm operations
console, dark, with restrained colour. Decisions:

| Keep | Change or add |
|---|---|
| Surfaces, ink steps, the petrol accent for chrome, the status set and its rules, the mono-numbers type rule, dark only. | **Station identity needs 4 colours**, and Maia validates only 3 (`series1–3`). Add and validate a 4th against `panel` for colour-blind safety. |
| "Status never reused as categorical." | **Add a dedicated `live` token** for the talking state. It must not be `critical` red, because "I am talking" is not an error. It must still be the most conspicuous thing on screen, and remain distinguishable without colour (shape, label, motion). |
| | Revisit dark-only for the **phone** in daylight. Decide in UX2 after testing contrast; dark stays the default. |

## Key use case: two calls at once

Sometimes two VDIs each have a live call. I need to:
- place them apart (one left, one right) in a single quick gesture;
- dim one to follow the other;
- answer in one call without the other hearing me.

Pan, solo (dim) and per-station talk must all be fast, direct controls: no
menus, no fine-adjust dialogs.

## Decisions from review (2026-10-09)

- **Talk:** the mic is open to all stations by default. Each station has its
  own "hears you" toggle, and push-to-talk is an optional mode. (Revisit
  after real use.)
- **Solo:** dims the other stations (default −18 dB), it doesn't cut them.
  It also **narrows the mic**: while any station is soloed, only soloed
  stations hear me. The others' talk toggles show "paused by solo" and come
  back when solo is released. (maelo, after the first prototype.)
- **Global push-to-talk hotkey on Mac:** in scope, P6.5 (small: Carbon
  `RegisterEventHotKey` gives press and release events).
- **Menu-bar panel:** wanted, but more than trivial. It's on the roadmap
  for later (P9).

## Vocabulary (to confirm in UX2)

Use plain words a non-audio person understands:
- **Listen level**: volume.
- **Position**: pan, shown as left/centre/right placement, not a knob.
- **Mute** and **Solo**: you already use these words, so keep them.
- **Talk**: momentary push-to-talk.
- **Talk latch**: an open mic that stays on.
- **Clear / Unstable / Lost**: link health.

## Open questions for UX2

All four from the first draft are answered; see "Decisions from review".
