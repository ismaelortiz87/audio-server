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

## 12. Lock-screen player (A15; built in A18)

Crosspoint publishes an Android **media session**, as music apps do. That puts
it in Samsung's **Now Bar** and in the expanded lock-screen player (and the
media area of the shade), and its buttons work **without unlocking**. maelo
chose this on 2026-10-13 over a custom notification layout. Mock:
[design/prototypes/android-nowbar/](../../design/prototypes/android-nowbar/index.html).
The system draws the player; we supply the title, subtitle, artwork and
buttons.

### 12.1 Which notification

The MediaStyle notification **replaces** the §4 connection notification as the
foreground-service notification: same channel (§4: default importance, silent,
public on the lock screen), same lifecycle (§1: from connect until Disconnect
or swipe-away from Recents). There is never more than one Crosspoint
notification. Disconnect is not on the player; it's in the app (and
swipe-away still quits). On Android 12 and older, the notification shows the
same title, text and buttons in its own MediaStyle layout.

The playback state is **"playing" from connect until Disconnect**, whatever the
mic does, so the player never times out of the Now Bar.

### 12.2 Buttons

**Layout A** (the mock; preferred):

| Slot | Button | Icon (our monochrome glyph; the system tints it) | Label / accessibility |
|---|---|---|---|
| centre | **Mic on/off** | mic / crossed mic | "Mic on" / "Mic off" · "Turn your mic off/on" |
| previous | **Station ◀** | skip-previous | "Previous station" |
| next | **Station ▶** | skip-next | "Next station" |
| custom 1 | **Mute** (selected station) | speaker / speaker-crossed | "Mute" / "Muted" · "Mute {A}" / "Unmute {A}" |
| custom 2 | **Solo** (selected station) | target outline / target filled | "Solo" / "Soloed" · "Solo {A}: only {A} hears you, others dimmed" / "Release solo" |

**Layout B** (fallback, if the centre slot can only show the system's
play/pause icon):

| Slot | Button |
|---|---|
| centre (system play/pause) | **Listening**: pause silences station playback, play resumes. The playback state still follows this. The mic is turned off on pause and stays off on resume (§6 rule: the mic never comes back by itself) |
| previous slot (custom) | **Next station ↻** (one direction, wraps) |
| next slot (custom) | **Mic on/off** |
| custom 1 / 2 | **Mute**, **Solo** |

**How A18 picks:** a short spike on Android 13+ emulator images (34 and 33).
If a custom action can occupy the centre or primary slot with our mic icon,
use A. Otherwise use B. If the player is hidden while "paused", B's pause must
not set the paused state; keep "playing" and silence output instead. Record
the result in A18's *Result*. The final check is maelo's Samsung Now Bar
(A13).

**Spike result (2026-10-13):** on API 33 and 34 the system player always puts
its own play/pause in the centre and never lets a custom action take it
(without PLAY/PAUSE declared the centre is empty; the slot-reservation extras
only leave gaps), so **Layout B** ships. A paused session also drops off the
lock screen after 10 minutes, so Listening's pause keeps the session PLAYING
and the second line says "Listening paused".

**Rules for all buttons:**
- **Solo is exclusive** (P5.7): it dims the other stations, and only the
  soloed station hears you. **It never turns the mic on**; only the Mic button
  does. Releasing solo restores the talk paths.
- **Mic** changes from the player play the §5 earcon. The Mic button is hidden
  in **PTT mode** (a player button can't be held), during a **phone call**
  (§6), and while the mic is **not granted**. In that last case the Mic slot
  becomes "Allow mic", which opens the app (unlock required).
- With **no stations** (connecting, failed, or connected and empty), only Mic
  shows.
- The headset play/pause button goes to the media session. It toggles the
  mic in open-mic mode only (§5), whatever the layout.

### 12.3 Selected station

- **On start:** the soloed station if there is one, otherwise the first in
  `stationOrder`.
- **◀ ▶ walk** online and lost stations in `stationOrder`, wrapping around.
  Offline stations are skipped.
- **Stays selected** while the service runs. If the selected station leaves
  (forgotten or offline), the selection moves to the next one.
- The selection only exists in the player, so the in-app selection isn't
  changed.

### 12.4 Title, subtitle, artwork

| Field | Content |
|---|---|
| Title | selected station's **short name** (no `VDI-` prefix), e.g. "ACCT-07". With no stations: "Crosspoint" |
| Subtitle (artist line, shown in the Now Bar pill) | **who hears you**: exactly the §4 title copy ("Live: all 3 stations hear you", "Solo: only ACCT-07 hears you", "Mic off · nobody hears you", "Push to talk · nobody hears you", "Mic off during your phone call", "Microphone blocked · nobody hears you") |
| Second line (album, where the player shows it) | the selected station's state: "Clear · 32 ms", "Unstable · 2% loss", "Lost · reconnecting", "Muted", "Soloed", "Dimmed −18". Joined with " · " when several apply (e.g. "Soloed · Clear · 32 ms"). Not connected: §4 connection text ("Reconnecting to ops…", "Not connected: Wrong group password") |
| Artwork (512 × 512) | rounded tile in the selected station's identity colour (`s1`–`s4`) with the white Crosspoint mark. **Muted:** the colour at 35% over `panel`. **Lost:** `ink4`. **No station:** `panel` with the mark. **Your voice reaches at least one station:** a 24 px `live` inset border plus a small white mic glyph bottom-right. Android 13+ tints the player from the artwork, so live turns the whole player pink-tinted |

Text updates follow §4's rule: only when something changes, at most once a
second. Meters never update the player.

### 12.4b Artwork v2 (A25; replaces the artwork row of §12.4)

maelo's Samsung (2026-10-13) shows the artwork **maximised** when a control is
touched. Two problems with v1:
- **Black wedges in the corners.** We drew a rounded tile on a transparent
  bitmap, and One UI masks it with its own, larger radius.
- **A flat colour block** with no depth and no name.

**Canvas:** 1024 × 1024, **full-bleed, square, opaque**. No rounded corners
and no transparency: the phone applies its own mask. All content sits inside
the centred **70% safe square**, because players crop to rounded rectangles,
circles (Now Bar thumbnail) or wide strips.

**Background:** a diagonal gradient from the top-left in the station colour
(`s1`–`s4`) to the bottom-right in that colour mixed 55% with `bg` `#0b0d0f`.
Over it, the faint crosspoint matrix from the app icon: two horizontal and two
vertical lines at ⅓ and ⅔, 2% of the width thick, white at 7%. It gives
texture without competing.

**Content**, centred in the safe square, top to bottom:
1. The station's **short name** ("ACCT-07"): white, weight 650, sized to fit
   at most 78% of the canvas width, capped at 15% of the height. One line,
   never truncated: it shrinks instead.
2. A **state pill**, 16 px below, only when there's a state:
   - **LIVE:** filled `live` `#ff4f7b`, text `onLive` `#1a0008`
   - **SOLO:** filled `accentInk` `#8fc3ba`, text `#0b0d0f`
   - **MUTED:** filled `#ffffff2e`, text white
   - **LOST:** filled `#ffffff2e`, text white

   11% of the height, uppercase, 0.08 em tracking, fully rounded. LIVE wins
   over SOLO; MUTED and LOST show alone.
3. The Crosspoint **mark**, small: 9% of the canvas, white at 85%, top-left
   at the safe square's corner.

**State variants of the gradient:**
- **Voice reaching at least one station:** the bottom-right stop becomes
  `live` `#ff4f7b`, so the live state reads even as a tiny thumbnail.
- **Muted:** both stops are the colour at 35% over `panel`.
- **Lost:** `ink4` `#4a4e4b` → `bg`.
- **No station / not connected:** `panel` → `bg`, with the mark centred at 22%
  and "Crosspoint" under it in place of a name.

**Rendering:** drawn in code (Canvas), as now. Cache one bitmap per (station,
state) so updates stay cheap. Review it on maelo's Samsung, maximised, and in
the Now Bar thumbnail.

### 12.5 Other media apps

Crosspoint doesn't take audio focus (A8), so music keeps playing alongside it.
Phones show one or more players (Samsung: the most recent, swipeable).
Whichever app was active last gets the headset button; that's Android's rule
and we accept it.

### 12.6 Quick Settings tile (A19)

**Deferred.** The player covers Mic on/off from the lock screen. Revisit only
if A13 shows the player is hard to reach.

### 12.7 Review (A18)

Capture each of these on the lock screen (collapsed and expanded) and in the
shade:
- live to all
- solo on the selected station, mic live
- mic off
- muted station
- lost station
- PTT mode
- phone call
- mic not granted
- no stations
- reconnecting

Also confirm:
- A or B is applied per the spike
- every button changes the engine within a second, while locked
- earcons on Mic changes
- the player stays while the mic is off for 10 minutes

## 13. Android Auto (A21; built in A20)

The §12 lock-screen player, on the car screen. Android Auto draws its own
media UI (day/night themed, sized for driving). We supply the same session (A18)
plus a **browse list** of stations. Nothing here needs the phone screen while
driving. Talking uses the phone's mic (A22 is the Bluetooth/car mic option).

### 13.1 Screens

**Now playing** (Auto's media screen): artwork, title, one subtitle line, the
centre play/pause, and the custom actions. Auto shows about four action buttons
and puts the rest in an overflow (⋯).

| Element | Content |
|---|---|
| Centre (play/pause) | **Listening**, as §12.2 Layout B: pause silences stations and turns the mic off; play resumes and leaves the mic off |
| Action 1 | **Mic on/off** (hidden in PTT mode and during a call, as §12.2) |
| Action 2 | **Solo** (selected station; exclusive; never opens the mic) |
| Action 3 | **Mute** (selected station) |
| Action 4, or overflow | **Next station ↻**. In the car, picking from the browse list is easier, so this goes to the overflow first when space runs out |

Priority if Auto shows fewer buttons: Mic, Solo, Mute, then Next station. Icons
are the A18 `ic_player_*` vectors; Auto tints them for day and night.

**Browse list** (Auto's "Stations" tab): one item per station in
`stationOrder`, up to 8.

| Field | Content |
|---|---|
| Title | short name ("ACCT-07") |
| Subtitle | car-short state (§13.2), e.g. "Hears you · Clear", "Soloed", "Muted", "Lost", "Offline" |
| Icon | a rounded square in the station colour (128 px). While that station hears you it is blended toward `live` (55/45, same rule as §12.4). Offline: `ink4` |
| Tap | **selects** the station for the now-playing controls and opens Now playing. It doesn't change solo, mute or the mic |

Offline stations are listed (so the list matches the phone), but tapping one
only selects it.

### 13.2 Car-short text

Car screens truncate around 30 characters, and the driver should only glance.
These are shortened forms of the §4 lines, used **only on Auto**.

| State | Now-playing subtitle |
|---|---|
| live, all hear | "Live · all {n} hear you" (one station: "Live · {A} hears you") |
| live, some hear | "Live · {A and B} hear you" (more than two names: "Live · {n} of {m} hear you") |
| live, solo | "Live · solo {A}" |
| mic open, nobody hears | "Mic open · nobody hears you" |
| mic off | "Mic off" |
| PTT | "Push to talk · hold on phone" |
| phone call | "Mic off · on a call" |
| mic not granted | "Mic blocked · allow on phone" |
| listening paused | "Paused · mic off" |

Station state in the browse list: "Hears you · Clear", "Hears you ·
Unstable", "Clear" (talk off), "Soloed", "Muted", "Dimmed", "Lost",
"Offline". Joined with " · " when several apply, health last.

### 13.3 Not connected

When Auto opens Crosspoint and the phone isn't connected:
- With saved settings: the browse list shows one playable item, **"Connect
  to {group}"**. Tapping it connects with the saved values
  (`connection.connect {}`), and the station list follows once connected.
- With nothing saved (fresh install): Auto shows an error state, **"Set up
  Crosspoint on your phone first."**, with no action. Auto blocks phone setup
  while driving.
- **Connecting / reconnecting:** the subtitle is "Connecting to {group}…",
  and the station list keeps the last known stations.
- **Failed:** the subtitle is "Not connected · {short reason}" ("wrong
  password", "server unreachable"), with the "Connect to {group}" item to
  retry.

### 13.4 Rules

- **One session.** Auto and the phone's lock screen show the same session. A
  station selected in the car is the selected station on the lock screen, and
  vice versa.
- **Driving safety:** nothing to read beyond the title and one line. No
  numbers except "Clear/Unstable" words (no ms, no %), no meters, no settings.
- **Earcons** (§5) play through the car speakers like everything else.
- **Audio** goes through the car via Android. The mic is the phone's unless
  A22 is set to the headset or car mic.
- **Disconnect** isn't offered in the car. Turning off Auto doesn't
  disconnect either: the phone keeps the session, as it does when the screen
  turns off.

### 13.5 Review (A20)

DHU captures, day and night, of:
- Now playing: live to all; solo; mic off; PTT; on a call (simulated); mic
  not granted; listening paused
- the browse list with 4 stations (one live, one muted, one lost, one offline)
- the "Connect to {group}" item
- the fresh-install error

Also confirm: tapping a station selects it on the lock screen too, and every
action changes the engine within a second.

## 14. Bluetooth mic (A22)

**Why:** with classic Bluetooth earbuds or a car, Android plays media over the
stereo music channel, but records from the **phone's mic**. The headset's mic
is only reachable through the Bluetooth **call channel**, which carries audio
both ways in **mono at call quality** (usually 16 kHz) and at call volume.
maelo confirmed both cases on 2026-10-13:
- **Bluetooth buds:** his voice was not taken from the buds.
- **USB headset dongle:** stereo and mic together, which is a normal USB audio
  device with no such limit.

**Setting** (Settings sheet, under Microphone; shown only while a Bluetooth
headset or car is connected):

`Bluetooth mic`: segmented **Phone mic | Headset mic**. Default **Phone mic**,
which is today's behaviour: stereo and placement kept.

Note under it, changing with the choice:
- **Phone mic:** "Stereo, full quality. Talk toward the phone."
- **Headset mic:** "Hands-free, call quality. Both ears hear the same mix, so
  placement is off."

**Behaviour with "Headset mic":**
- While a Bluetooth headset or car is connected, Crosspoint switches to the
  communication route: `MODE_IN_COMMUNICATION` and
  `setCommunicationDevice(<the BT device>)` (API 31+), or SCO on older phones.
  Mic and playback both use that device.
- When it disconnects, Crosspoint goes back to normal media routing and the
  phone mic, and **the mic state doesn't change** (§6 rule).
- **LE Audio** devices that can do stereo and mic together keep stereo. In
  that case the note says "Hands-free, stereo" and placement stays on.
- The placement field and pucks in the Console show a quiet note while
  placement is off: "Placement is off while using the headset mic."
- **Calls (A14):** our own communication mode must not count as a phone call
  (`PlatformMonitor.appOwnsAudioMode`).
- **Device names (§6):** while the headset mic is in use, Microphone shows
  "Bluetooth · {name}".

**API:** `settings.bluetoothMic: "phone" | "headset"`, and
`platform.bluetoothRoute: null | { name, stereo }` (null when no Bluetooth
headset or car is connected).
