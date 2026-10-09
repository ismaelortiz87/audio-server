# What the UIs need from the engine

Status: **final for UX2/UX3** (owner: Claude, design). Input to **P4.5**, the
control API schema. It lists what the Console UI and the VDI agent's
localhost UI must be able to *show* and *do*, not how the API encodes it.
P4.5 decides the wire format. If the engine can't provide an item, flag it in
P4.5; don't drop it silently.

Sources: Console prototype `design/prototypes/console/` and agent prototype
`design/prototypes/vdi/`, brief decisions, ROADMAP D10–D13.

## 1. Console: state to show

### 1.1 Connection
| Field | Notes |
|---|---|
| `state` | `connecting` · `connected` · `reconnecting` · `failed` (with reason: bad password, server unreachable…) |
| `server` | host:port as configured |
| `group` | group name (password never sent to the UI, only "saved: yes/no") |
| `self.name` | our username |
| `self.role` | `console` (sanity: UI refuses to run as Console UI on a VDI peer) |

### 1.2 Stations (one per VDI, **including remembered offline ones**)
| Field | Notes |
|---|---|
| `id` | stable key across reconnects (username is fine if unique; P4.5 decides) |
| `name` | full name, never truncated in UI |
| `colorIndex` | 0–3, **assigned once and remembered**, so a station keeps its colour |
| `role` | `vdi` · `unknown` (unknown peers are listed separately, see 1.4) |
| `presence` | `online` · `lost` (was online, reconnecting; with `lostForSec`) · `offline` (known, not connected; with `lastSeen` timestamp) |
| `health` | `clear` · `unstable` · derived from link stats below; engine or UI may derive, P4.5 decides, but **thresholds live in one place** |
| `latencyMs` | round-trip / 2 or one-way estimate, the number shown next to health |
| `lossPct` | packet loss over the last ~10 s |
| `jitterBufferMs` | current network buffer (shown in tooltips / Advanced) |
| `agent` | VDI self-report (P1.7): `input` (`ok`·`missing`·`silent` + `silentForMin`), `output` (`ok`·`missing`), `paused` (bool), `configError` (string or null). The UI turns any non-ok value into the "VDI reports: …" line |
| `level` | dB, −40…+6 (listen level; persisted) |
| `pan` | −1…+1 (placement; persisted; UI snaps to −1, −0.5, 0, 0.5, 1 but the engine accepts any value) |
| `mute` | bool (persisted) |
| `solo` | bool (not persisted across restarts) |
| `talk` | bool, the user's "hears you" choice (persisted) |
| `hearsYou` | bool, **effective** talk gate computed by the engine: `talk && online && !(anySolo && !solo) && micTransmitting`. The UI shows it; it never re-derives routing on its own |

### 1.3 Me (mic and output)
| Field | Notes |
|---|---|
| `mic.mode` | `open` · `ptt` |
| `mic.on` | bool (open mode) |
| `mic.pttHeld` | bool (true while any PTT source is held: UI button, Space, global hotkey P6.5) |
| `mic.transmitting` | effective: `mode==open ? on : pttHeld` |
| `mic.device` | name |
| `output.device` | name |
| `output.level` | master, dB −40…+6 |
| `soloDimDb` | default −18; setting |
| `pttHotkey` | display string (Mac only, P6.5) |

### 1.4 Other peers
| Field | Notes |
|---|---|
| `unknownPeers[]` | name of each group member with no role (stock SonoBus). No audio is exchanged; UI shows a notice |
| `otherConsoles[]` | names of other Consoles in the group (not mixed, not heard), for a future "also connected" hint. Low priority |

### 1.5 Meters (streamed, ~30 fps, separate from state)
| Field | Notes |
|---|---|
| `station[id].activity` | **pre-fader** peak and RMS of what the VDI sends. The UI shows activity even when the station is muted or dimmed, by design |
| `mic` | peak/RMS of my mic (pre-gate, so I can see my level while not transmitting) |
| `output` | master peak (optional) |

## 2. Console: commands

| Command | Notes |
|---|---|
| `connect` / `disconnect` | connect uses saved server/group/password |
| `station.setLevel(id, dB)` | |
| `station.setPan(id, -1…1)` | |
| `station.setMute(id, bool)` | |
| `station.setSolo(id, bool)` | multiple solos allowed |
| `station.setTalk(id, bool)` | ignored by the engine's gate while solo-held, but the stored choice still changes (UI disables the control in that state) |
| `talkToAll()` | sets `talk=true` on all stations |
| `stations.spread()` / `stations.centerAll()` | convenience; may be done UI-side as N `setPan` calls |
| `mic.setMode(open|ptt)` | |
| `mic.setOn(bool)` | open mode |
| `mic.ptt(down|up)` | from UI button or Space; engine treats any held source as held |
| `output.setLevel(dB)` | |
| `settings.set(soloDimDb, devices, pttHotkey…)` | |
| `station.forget(id)` | remove a remembered offline station |

Every command answers with success or a readable error. State changes arrive as
events; the UI updates from events, not from command replies.

## 3. Console: persistence the engine owns
Per station: colour index, level, pan, mute, talk, last seen. Global: mic mode,
master level, solo dim, devices, server/group/password. Solo is not persisted.

## 4. VDI agent localhost UI (P2.6): state and commands

| State | Notes |
|---|---|
| `name`, `colorIndex` | the same identity the Console shows (colour assigned by the Console isn't known to the VDI; P4.5: the agent may show a neutral dot) |
| `connection` | `connected` · `waiting` (no Console) · `reconnecting` (attempt, next retry in s) · `error` (bad password, server unreachable) |
| `sending` | `active` · `paused` · `idle` (no Console listening) |
| `consoles[]` | name, kind (`mac`·`web`·…), latencyMs, `talking` (bool, voice arriving now) |
| `input` | configured PipeWire node, status `ok`·`missing`·`silent`, live level |
| `output` | configured PipeWire node, status `ok`·`missing`, live incoming-voice level |
| `devices.inputs[]` / `devices.outputs[]` | available PipeWire nodes: id, description, live level for inputs |
| `configPath` | path of the YAML in use |
| `configError` | string or null |

| Command | Notes |
|---|---|
| `setInput(node)` / `setOutput(node)` | switch live **and** write back to the YAML (P2.1 `Config::save()`); returns the previous value for Undo |
| `testTone(node)` | ~1.5 s tone into an output node |
| `pause()` / `resume()` | sending only |
| `reloadConfig()` | re-read YAML; report errors |
| `retryNow()` | skip reconnect backoff |

The agent UI binds to localhost only (D10).

## 5. Explicitly not needed
Chat, soundboard, metronome, file playback, effects racks, monitor fades,
latency match, public group lists, per-peer channel groups, stereo send from
VDIs. Recording and recaps come later (P10, own UX addendum P10.3).
