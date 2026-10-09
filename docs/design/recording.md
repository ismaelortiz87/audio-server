# Recording, transcripts and recaps (UX addendum P10.3)

Status: **v1 design, ready for P10.1–P10.6** · Owner: Claude (design) ·
2026-10-09. Extends [spec.md](spec.md); everything in the spec still applies.
Prototype: [design/prototypes/recording/](../../design/prototypes/recording/index.html).

Open questions that don't change this design: Q7 (which transcription and
summary models, and whether audio may leave the infra) and Q8 (where files
live, retention). The UI shows whatever the backend reports.

## 1. Concepts the user sees

| Term in UI | What it is | Backend |
|---|---|---|
| **Recording** | one station, from Record to Stop | P10.1/P10.2 |
| **Includes my voice** | the recording also holds my mic, *only while that station could hear me* (talk, solo and PTT gates applied) | P10.2 session record |
| **Session** | a finished recording, plus its transcript and recap when ready | P10.4/P10.5 |
| **Recap** | summary · decisions · action items · open questions | P10.5 |

One choice, made once in Settings → Recording: **"Recordings include: This
station only / This station + my voice to it"** (default: + my voice, since
that's what makes recaps useful). Per-recording overrides would add a decision
at the worst moment (a call is starting), so there are none.

## 2. Indicator: REC (no new colour)

Recording is neither an error (critical red) nor my voice going out (live
pink), so it gets **no new hue**. Instead it is distinguished by shape and
label: a **filled `ink` circle + "REC" + elapsed time** (mono), the circle
pulsing slowly (2.4 s; static with reduced motion). Searching for a new
colour validated against every existing role only produced cyan, which the
palette forbids, or a weak pastel; neutral-bright reads as "something is
happening" without alarm.

| Where | Shows |
|---|---|
| Top bar (always visible while anything records) | `● REC 2` (count of recording stations) followed by the longest elapsed `12:04`; click → Sessions |
| Station card header, next to badges | `● REC 12:04` pill (`raised` bg, `ink3` border, `ink` text) |
| Placement puck | 6 px `ink` circle after the name |
| Agent localhost UI | nothing; the VDI doesn't record |

## 3. Starting and stopping

- Each station card gets a **Record** button in a fourth slot of the actions
  row (grid becomes `1fr 1fr 1.8fr auto`; on phone the row wraps to two
  lines). Label `● Record` → `■ Stop 12:04` while recording. Shortcut **R**
  toggles the selected station.
- **Consent reminder** before a station's first recording each day, as a
  modal: title "Recording a call?", body "Make sure everyone on the call has
  agreed to be recorded. Recording without consent is illegal in many
  places.", checkbox "Don't remind me again today", buttons [Cancel]
  [Start recording]. Default focus is Cancel; Enter doesn't start.
- Stopping shows a toast: "Saved · Transcribing…" with [Open].
- A lost station keeps recording silence and marks a gap; a station going
  offline stops its recording automatically ("Stopped: station went offline").
- Disconnecting or quitting while recording asks first: "2 recordings are
  running. Stop them and quit?"

## 4. Sessions view

A second top-level view, switched from the top bar: **`Console · Sessions`**
(segmented, desktop) or a tab bar at the bottom (phone). Shortcut **G then S**
isn't needed; the switch is two items.

**List**, grouped by day, newest first. Each row: station swatch + name ·
start time–end time · duration (mono) · status chip · [⋯].

| Status chip | Style |
|---|---|
| Recording | `● REC` style |
| Transcribing… | `ink3` text + subtle pulse |
| Recap ready | `accentInk` text (it's chrome: "ready/selected") |
| Failed: {reason} | circle `critical` + word, with [Retry] |

**Detail** (opens beside the list on desktop, full screen on phone):
1. Header: station, date, duration, "Includes my voice" or "Station only".
2. **Recap** card: Summary (paragraph) · Decisions (list) · Action items
   (checklist-styled list, owner in bold when stated; not interactive in v1)
   · Open questions. Footer meta: "Recap by {model} · {provider}" (Q7).
3. **Transcript**: time-stamped turns, speaker label in its colour role: the
   station's swatch + "VDI-ACCT-07" (or "Speaker 2" after diarization), and
   "Me" with a `live`-tinted mic glyph (my voice). Search box filters turns.
   Clicking a timestamp plays from there.
4. **Audio**: one player; tracks "Station" and "Me" with individual mute, so
   I can hear just one side.
5. Actions: Copy recap (Markdown) · Export (audio .flac, transcript .md/.json)
   · **Delete session** (confirm; removes audio, transcript and recap).
6. Retention line, `ink3`: "Audio is deleted after 30 days. The recap is kept."
   (values from the backend, Q8).

Empty state: "No sessions yet. Press Record on a station to capture a call;
its transcript and recap appear here."

## 5. API additions (for P10.1/P4.x, to add to control-api v1 as additive)

State: `stations{}.recording: null | { "since": iso, "includesMe": bool }`,
`settings.recordingIncludesMe: bool`, `recordingsRunning: n`.
Commands: `station.record { station, on }`, `settings.set { recordingIncludesMe }`.
Sessions come from a separate REST resource (they outlive the engine
connection): `GET /api/v1/sessions`, `GET /api/v1/sessions/{id}` (meta,
recap, transcript), `GET /api/v1/sessions/{id}/audio/{station|me}`,
`DELETE /api/v1/sessions/{id}`, `POST /api/v1/sessions/{id}/retry`.

## 6. Accessibility and copy rules
- REC is always circle + "REC" + time, never the circle alone.
- The consent dialog is `role="alertdialog"`, focus trapped, Escape = Cancel.
- Transcript speakers are text first; colour only reinforces.
- Elapsed times in mono, `H:MM:SS` above one hour.
