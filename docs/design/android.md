# Android app (UX addendum A2)

Status: **v1 design, ready for A5–A12** · Owner: Claude (design) · 2026-10-11.
Extends [spec.md](spec.md); everything there still applies. The phone layout
(spec §2, P5.5) is the Android layout. This file only adds what a native
phone app has and the Mac, PWA and web Console don't.
Prototype: [design/prototypes/android/](../../design/prototypes/android/index.html)
(`?screen=` any id in §9).

Not on Android: spec §3.9 (web audio link) and §3.10 (sign-in). The app
talks to its own engine on loopback, so it has no gateway and no token.

## 1. How the app behaves, in one paragraph

Opening Crosspoint connects to the saved group (brief, job 6). **It keeps
running until you press Disconnect or swipe it away from Recents.** Going
home, locking the phone or switching apps keeps audio flowing both ways, and
a persistent notification always says whether your mic is live. Swiping the
app away from Recents disconnects it and stops the engine: that's how people
quit apps, and an invisible hot mic is the worst mistake this product can
allow (brief, principle 2).

## 2. Launch and system chrome

| Item | Design |
|---|---|
| Splash (Android 12+ system splash) | background `bg` `#0b0d0f`, the adaptive icon (P3.10 maskable art), no text |
| Before the page paints | the shell and the web view paint `bg`. **Never a white frame**: set the web view background before loading |
| First run (nothing saved) | the P5.4 onboarding, unchanged |
| Orientation | portrait on phones (smallest width < 600 dp); tablets rotate, and the layout follows container width as usual |
| Status bar | `rail` (the top bar's colour), light icons. Content never draws under it |
| Navigation bar / gesture area | `rail` (the You bar's colour), light icons |
| Edge-to-edge (forced from targetSdk 35) | same result: the insets are padding painted `rail`, the display cutout counts as status bar |
| Font size | honours Android's font scale (web view text zoom). Review at 100% and 130%: nothing clips, and mic button labels may wrap but are never cut |
| Back gesture/button | closes the settings sheet or an open picker (like Esc). Otherwise it sends the app to the background, like Home. **Never disconnects.** No "press back again to exit" |
| Keyboard | when a settings field has focus, the sheet resizes so the field stays visible above the keyboard |
| Name default | "Your name" is pre-filled with the Android device name (e.g. "Pixel 8"); agents show it as "Console · Android" |

**Touch copy** (any touch-first device, so the PWA on a phone too: use
`(pointer: coarse)`; mouse and keyboard keep the spec §3.6 copy):

| Where | Desktop (spec) | Touch |
|---|---|---|
| Mic button sub, open · on | "Click to turn off" | "Tap to turn off" |
| Mic button sub, open · off | "Click to open mic" | "Tap to open mic" |
| Mic button sub, PTT · idle | "or hold Space" | "Press and hold" |
| Live band, PTT · not held | "Push to talk: hold the mic button or Space. Nobody hears you now." | "Push to talk: hold the mic button. Nobody hears you now." |

**Haptics** (one hand, often without looking): a short tick when your voice
starts going out (PTT pressed, or mic turned on), and a lighter tick when it
stops. Nothing for other controls. Follow the system touch-feedback setting.

## 3. Microphone permission

**When to ask:** the first time the Console would send your voice, i.e. on
the first connect in open-mic mode (the default) or on the first PTT press.
Never at launch before onboarding. **No custom pre-prompt:** the system
dialog arrives in context (you just connected an intercom), so it explains
itself. Right after the mic answer, ask for notifications (Android 13+) in
the same moment. Ask each one once; after that, the app only shows the
states below.

States (engine reports them, §8): `granted` · `prompt` (can still show the
system dialog) · `denied` (only Android Settings can change it).

While the mic is not granted:

| Element | Shows |
|---|---|
| Mic button | crossed mic + **"Mic blocked"** · sub "Tap to allow" (`prompt`) or "Allow in Android settings" (`denied`). Idle style (`panel2`, `line`, `ink2`), never live. Tap: `prompt` → system dialog; `denied` → the app's Android settings page. Same in PTT mode (holding does nothing) |
| Live band | quiet: **"Microphone blocked. You can listen, but no station hears you."** It replaces the other quiet texts |
| Mic meter | empty |
| Talk buttons | unchanged rules ("Will hear you" where talk is on), because the engine reports `transmitting: false` |
| Settings | the problem row in §7 |

No extra banner: the live band says it, and the mic button is the fix.
When the user grants the mic in Android Settings and comes back, the mic
returns to the state they left it in (open mode + on → live). They went
there to turn it on.

## 4. Background: notification and service

The foreground service runs from connect until Disconnect or swipe-away,
whatever the connection state (connecting, reconnecting and failed included,
so the notification can say so).

**Channel:** "Connection" (id `connection`), importance **default** with **no
sound and no vibration** (`setSound(null, null)`, vibration off), and
`setOnlyAlertOnce(true)`. Ongoing (can't be dismissed), lock-screen visibility
**public**. Not "low": Android 12+ hides silent notifications from the lock
screen by default, and mic state must be readable there (A10 review,
2026-10-12). Default importance shows on the lock screen and in the status bar
without a pop-up.

**Small icon:** [design/icon/crosspoint-notification.svg](../../design/icon/crosspoint-notification.svg)
(the route and lit node, monochrome, as a 24 dp vector). **Icon tint**
(`setColor`): `accent` normally; `live` while your voice reaches at least
one station. Colour only reinforces: the title says it in words.

**Title = who hears you** (the same question as the live band, so the same
copy), **text = connection**:

| Mic situation | Title |
|---|---|
| transmitting, all hear | "Live: all {n} stations hear you" (one known station: "Live: {A} hears you") |
| transmitting, some hear | "Live: {A and B} hear(s) you" |
| transmitting, solo | "Live: solo, only {A} hears you" |
| transmitting, nobody | "Mic open, but no station is set to hear you" |
| open mode, mic off | "Mic off · nobody hears you" |
| PTT, not held | "Push to talk · nobody hears you" |
| mic not granted | "Microphone blocked · nobody hears you" |
| phone call (§6) | "Mic off during your phone call" |

| Connection | Text |
|---|---|
| connected | "Listening to {online} of {known} stations · {group}" |
| connecting / reconnecting | "Connecting to {group}…" / "Reconnecting to {group}…" (no countdown: it would update every second) |
| failed | "Not connected: {reason}" |

**Actions** (at most two, in this order):

| Action | When |
|---|---|
| **Mic off** / **Mic on** | open-mic mode with the mic granted, and not during a phone call. Plays the earcon (§5) |
| **Disconnect** | always. Leaves the group, stops the service and removes the notification. No confirmation: it's safe and reversible |

No mic action in PTT mode: a notification can't be held. Tapping the
notification opens the Console. Update the notification only when its title,
text or actions change, and at most once a second.

**If notifications are blocked,** the service still runs, but your mic state
isn't visible outside the app. Settings shows the row in §7. The app doesn't
ask again.

**Battery:** if A10 or A13 show that Android stops Crosspoint while it's
"Optimized", Settings shows the background row in §7. Otherwise it never
appears.

## 5. Earcons and the headset button

**Earcons:** short local cues for mic changes made **without looking at the
app**: the headset button, the notification action, and a phone call
switching the mic off. They play to your output only and are **never sent to
stations**.
- mic on: two rising tones, 660 Hz then 880 Hz, 70 ms each, 20 ms gap
- mic off: the same, falling (880 → 660)
- level −18 dBFS, 5 ms fades, mixed over station audio

Touching the in-app mic button doesn't play them; the screen and haptics
confirm it.

**Headset button (A12):** one press of a wired or Bluetooth headset's
play/pause button toggles the mic **in open-mic mode**, with the earcon.
- In PTT mode it does nothing. Most Bluetooth headsets send one click on
  release rather than a press and a release, so holding to talk can't be
  done reliably, and a latched "talk until pressed again" would be an easy
  way to leave a mic live.
- It only takes the button while connected. When disconnected, the button
  goes back to other apps.
- Double press and long press are ignored (phones use them for calls and the
  assistant).
- The Connection notification stays a normal notification, not a media-style
  one with play/pause.
- Setting in §7. Default: **Mic on/off**.

## 6. Phone calls, other apps, output

**A phone or VoIP call starts:** the engine turns the mic off and silences
playback for the call. Activity strips keep moving, since they are
pre-fader.
- Mic button: crossed mic + "Mic off" · sub "During your phone call",
  disabled.
- Live band (quiet): **"Mic off during your phone call."**
- Notification title: the same.

**The call ends:** playback comes back. **The mic stays off.** A mic never
comes back on by itself after your attention went elsewhere.
- Live band (quiet, open mode): **"Mic off since your phone call. Tap the mic
  button to talk again."**
- That text clears as soon as the mic is turned on or the mode changes.
- The mic-off earcon plays when the call ends, not when it starts (during the
  call the phone owns the audio).

**Other apps' audio** (music, video): Crosspoint mixes with it. It doesn't
pause other apps and isn't paused by them. If A8 finds the voice path forces
audio-focus behaviour, A8 records it and asks before changing this.

**Device names** (Settings → Microphone / Output; the engine reports them in
plain words):

| Output | Microphone |
|---|---|
| Phone speaker · Earpiece · Wired headphones · Bluetooth · {name} · USB · {name} | Phone mic · Headset mic · Bluetooth · {name} · USB · {name} |

- **Default output** with nothing plugged in: **Phone speaker** (phone in
  hand or on the desk), unless A8's measurements say otherwise.
- Plugging in headphones moves the output to them, and unplugging moves it
  back. The mic state never changes because of routing.

**Echo notice:** only if A8 finds that speaker + mic echoes (no working
platform echo cancelling). While the output is the phone speaker and your
voice reaches at least one station, a notice (spec §3.4 style) shows at the
top of the main area:
**"Phone speaker:** stations may hear themselves through your mic. *Use
headphones to talk.*"
It isn't blocking and has no button. If A8 finds echo cancelling works,
there is no notice.

## 7. Settings sheet on Android

The spec §3.7 fields, with these changes:
- **"Push-to-talk key"** is replaced by **Headset button**: segmented
  `Mic on/off | Off`, note "One press, in open-mic mode."
- **Problem rows** sit at the top of the sheet, above Server, and only appear
  when something needs fixing (calm until it matters). Each row is a `panel`
  box with a 3 px `warning` left border, a warning circle, `ink2` text and a
  `.btn` action under the text, at least 44 px tall (spec §5 touch size):

| Row (when) | Text | Action |
|---|---|---|
| mic `prompt` | "Microphone blocked. Stations can't hear you." | **Allow microphone** |
| mic `denied` | "Microphone blocked. Stations can't hear you." | **Open Android settings** |
| notifications blocked | "Notifications are off, so you can't see whether your mic is live outside the app." | **Open Android settings** |
| background restricted (only if needed, §4) | "Android may stop Crosspoint in the background." | **Allow background use** |

## 8. Engine error page (native, phone)

This is the P6.1 native page laid out for a phone, for when the web view
can't show the Console:
- inside the safe area, 24 px side padding, content starting at 1/4 height
- title 20/700 `ink`, body 15/1.45 `ink2`
- one full-width primary button, 48 px tall, `accent` background, `ink`
  label, 10 px radius
- `bg` background, painted under the system bars too

| Case | Title | Body | Button |
|---|---|---|---|
| API couldn't start | "Crosspoint couldn't start" | "Its audio engine couldn't open its local connection (port {port} is in use by another app). Close that app or restart the phone, then try again." | **Try again** (restarts the engine's API) |
| page didn't load | "The Console didn't load" | "Crosspoint's engine didn't answer. {error}" | **Try again** (reloads) |
| UI files missing | "This copy of Crosspoint is incomplete" | "Some of its files are missing. Reinstall Crosspoint." | none |

No CLI flags in the copy: Android has no command line.

## 9. Prototype screens

`design/prototypes/android/?screen=` shows each in a 390 × 844 phone frame:

| id | Shows |
|---|---|
| `live` | connected, open mic live to all (reference: unchanged phone layout inside Android chrome) |
| `blocked` | mic `prompt` after a refusal: mic button, live band |
| `call` | during a phone call |
| `aftercall` | after the call, mic stays off |
| `echo` | phone speaker echo notice (conditional on A8) |
| `settings` | settings sheet with problem rows and Headset button |
| `notif` | lock screen with the Connection notification in its main states |
| `error` | engine error page |
| `board` | all of the above side by side (review overview) |

## 10. API additions (additive v1; to implement in A5, A9, A10, A12)

State (Console, Android only; `null` on other platforms; comments are
annotations, not part of the JSON):
```jsonc
"self": { "kind": "android" },
"platform": {
  "micPermission": "granted",          // granted | prompt | denied
  "notifications": "granted",          // granted | denied
  "backgroundRestricted": false,
  "phoneCall": false,                  // a call owns the audio right now
  "micOffByCall": false,               // true from call start until the mic is turned on or the mode changes
  "outputRoute": "speaker",            // speaker | earpiece | wired | bluetooth | usb
  "echoRisk": false                    // engine: outputRoute == speaker && no working AEC && transmitting
},
"settings": { "headsetButton": "mic" } // mic | off
```

Commands: `platform.requestMic {}` (system dialog when `prompt`, app
settings page when `denied`), `platform.openSettings { page: "app" |
"notifications" | "battery" }`, `platform.allowBackground {}`,
`settings.set { headsetButton }`. On other platforms they return
`not_supported`.

Peer info `self.kind` gains `android`, and the agent UI shows
"Console · Android" (spec §6, `consoles[].kind`).

## 11. Review checklist additions (A7, A9, A10, A12)

Spec §9 applies, plus:
1. Screenshots come from the emulator (`adb exec-out screencap -p`), at 100%
   and 130% font scale, for every screen in §9 that the task touches.
2. No white frame at launch: record a screen video of a cold start.
3. Back never disconnects; swipe-away always does.
4. The notification title matches the live band in every state in §4.
5. Earcons play for the headset button, the notification action and a call
   ending, and never reach a station (check on the F2 VDI peer).
