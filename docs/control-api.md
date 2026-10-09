# Crosspoint control API, v1 (P4.5)

Status: **v1 draft for implementation** · Author: Claude · 2026-10-09

The engine (Console role and VDI agent role) exposes this API to its web UIs:
the Console UI (P5, served by the Mac shell P6.1 or the web container P7) and
the agent localhost UI (P2.6). It is the contract that P4.1–P4.4 implement and
that `console-ui/` (P5.1) mocks. It covers everything in
[design/console-needs.md](design/console-needs.md); if the engine cannot
provide a field, keep the key and document the gap here instead of dropping
it.

Wire format: JSON text frames over one WebSocket. No binary frames in v1.

---

## 1. Transport

| What | Where |
|---|---|
| UI files | `GET /` (Console: `index.html`, agent: `agent.html`), static files under the same origin |
| WebSocket | `GET /api/v1/ws` (upgrade) |
| Liveness | `GET /api/v1/health` → `200 {"ok":true,"app":"Crosspoint","version":"1.7.2","role":"console"}` (for the proxy and monitoring; no auth, no state) |

**Binding** (P4.1): `127.0.0.1` by default. A non-loopback bind (the web
container, P7) is only allowed together with a token (see §2.2).

**Behind maelo's proxy** (D13): TLS terminates at the proxy; the engine
speaks plain HTTP/WS. The proxy must pass `Upgrade`/`Connection` headers and
not buffer. Clients connect to `wss://<origin>/api/v1/ws`, or `ws://` on
localhost.

**Origin check:** the server accepts the WS upgrade only from its own origin
(`Host` match), `http://localhost:<port>`, `http://127.0.0.1:<port>`, and any
origins in the config's `api.allowed_origins` (e.g.
`https://crosspoint.app.lagreca.io`). Otherwise it answers `403`.

## 2. Session

### 2.1 Handshake
On connect the server sends `hello` first:

```json
{ "t": "hello", "v": 1, "app": "Crosspoint", "version": "1.7.2",
  "role": "console", "auth": "none", "features": ["meters", "ptt"] }
```

- `role`: `"console"` or `"vdi"`. The UI checks it and refuses to run the wrong
  entry point ("This is a VDI agent; open /agent.html").
- `auth`: `"none"` (loopback) or `"token"` (see 2.2).
- `features`: optional capabilities. Clients must ignore unknown features and
  unknown message types.

Then (after auth, if required) the server sends a full `state` (§3) and
starts sending `patch` events. Meters are opt-in (§4).

### 2.2 Auth (non-loopback only)
If `hello.auth == "token"`, the client's first message must be:
```json
{ "t": "auth", "token": "<secret from config api.token>" }
```
Reply `{ "t": "auth", "ok": true }`, or `{ "t": "auth", "ok": false }` and
close with code `4401`. The web container stores the token in an `HttpOnly`
cookie set by the proxy-protected login page (P7); the UI never shows it.

### 2.3 Close codes
`4400` bad message, `4401` auth failed, `4403` origin refused, `4409` protocol
version unsupported, `1001` server shutting down. The UI reconnects with
backoff (0.5 s → 8 s) on anything but 4401/4403/4409.

### 2.4 Safety rules on disconnect
- **Push-to-talk is released** when the socket that pressed it disconnects. A
  dropped Wi-Fi link must never leave the mic transmitting.
- Solo state set by a client persists (it's engine state, not per-client).
- Several clients may be connected at once (e.g. Mac shell + a browser tab);
  all receive the same state. PTT is held while **any** client holds it.

## 3. State and patches

### 3.1 Messages
```json
{ "t": "state", "rev": 1042, "state": { ... } }
{ "t": "patch", "rev": 1043, "ops": [
    { "op": "set", "path": "/stations/VDI-ACCT-07/level", "value": -6 },
    { "op": "del", "path": "/unknownPeers/0" } ] }
```
- `rev` increases by 1 per `state`/`patch`. If a client sees a gap it sends
  `{ "t": "resync" }` and gets a fresh `state`.
- Paths are JSON Pointers (RFC 6901) into the state. `set` creates or replaces,
  `del` removes (objects by key, arrays by index). The server may coalesce
  changes (e.g. while a slider is dragged) but every patch must leave the state
  valid.
- **The UI renders from state only.** Command replies (§5) don't carry state;
  the resulting change arrives as a patch.

### 3.2 Console state (`role: "console"`)
```json
{
  "self": { "name": "maelo-mac", "role": "console", "kind": "mac" },
  "connection": {
    "state": "connected",
    "server": "aoo.sonobus.net:10998",
    "group": "ops",
    "passwordSaved": true,
    "reason": null,
    "retryInSec": null
  },
  "stationOrder": ["VDI-ACCT-07", "VDI-DEV-02"],
  "stations": {
    "VDI-ACCT-07": {
      "id": "VDI-ACCT-07",
      "name": "VDI-ACCT-07",
      "colorIndex": 0,
      "presence": "online",
      "lostForSec": null,
      "lastSeen": "2026-10-09T13:12:00Z",
      "health": "clear",
      "latencyMs": 32,
      "lossPct": 0.0,
      "jitterBufferMs": 24,
      "agent": {
        "input": "ok", "inputNode": "loopback_sink.monitor", "silentForMin": null,
        "output": "ok", "outputNode": "crosspoint_mic",
        "paused": false, "configError": null
      },
      "level": 0.0,
      "pan": -0.5,
      "mute": false,
      "solo": false,
      "talk": true,
      "hearsYou": true
    }
  },
  "unknownPeers": ["bob-laptop"],
  "otherConsoles": [],
  "mic": { "mode": "open", "on": true, "pttHeld": false, "transmitting": true, "device": "MacBook Pro Microphone" },
  "output": { "device": "AirPods Pro", "level": 0.0 },
  "settings": {
    "soloDimDb": -18,
    "pttHotkey": "⌥Space",
    "codec": "opus", "bitrateKbps": 96,
    "networkBuffer": { "mode": "auto", "currentMs": 24 }
  },
  "devices": {
    "inputs":  [{ "id": "builtin-mic", "name": "MacBook Pro Microphone" }],
    "outputs": [{ "id": "airpods", "name": "AirPods Pro" }]
  }
}
```

| Field | Type / values | Rules |
|---|---|---|
| `connection.state` | `connecting` `connected` `reconnecting` `failed` | `reason` (string) set when `failed`; `retryInSec` when `reconnecting` |
| `stationOrder` | ids | colour-index order, remembered offline stations last; stable (spec §2) |
| `stations{}.id` | string | the VDI's username; stable across reconnects |
| `colorIndex` | 0–3 | assigned once by the Console and persisted |
| `presence` | `online` `lost` `offline` | `lostForSec` when lost; `lastSeen` (ISO 8601 UTC) always for known stations |
| `health` | `clear` `unstable` | **computed by the engine** with the spec §3.5 thresholds and hysteresis; meaningful only when `online` |
| `latencyMs`, `lossPct`, `jitterBufferMs` | numbers or null | null when not online |
| `agent` | object or null | from the VDI's peer info (P1.7); null for VDIs that don't report it. `input`: `ok` `missing` `silent`; `output`: `ok` `missing` |
| `level` | dB, −40…+6 | persisted |
| `pan` | −1…+1 | persisted; any value allowed (UI snaps) |
| `mute`, `talk` | bool | persisted |
| `solo` | bool | not persisted across restarts |
| `hearsYou` | bool | **engine-computed:** `talk && presence=="online" && mic.transmitting && !(anySolo && !solo)`. The UI displays it and never re-derives it |
| `mic.transmitting` | bool | `mode=="open" ? on : pttHeld` |
| `settings.pttHotkey` | string or null | null where no global hotkey exists (web) |

Unknown peers (no role) appear only in `unknownPeers`. Other Consoles appear
only in `otherConsoles`. Neither is ever routed (P1.4/P1.6).

### 3.3 Agent state (`role: "vdi"`)
```json
{
  "self": { "name": "VDI-ACCT-07", "role": "vdi" },
  "connection": {
    "state": "connected", "server": "aoo.sonobus.net:10998", "group": "ops",
    "attempt": null, "retryInSec": null, "error": null
  },
  "sending": "active",
  "consoles": [{ "name": "maelo-mac", "kind": "mac", "latencyMs": 32, "talking": false }],
  "input":  { "node": "loopback_sink.monitor", "description": "Monitor of System Loopback", "status": "ok", "silentForMin": null },
  "output": { "node": "crosspoint_mic", "description": "Crosspoint Virtual Mic", "status": "ok" },
  "devices": {
    "inputs":  [{ "node": "loopback_sink.monitor", "description": "Monitor of System Loopback", "hint": "captures system audio" }],
    "outputs": [{ "node": "crosspoint_mic", "description": "Crosspoint Virtual Mic", "hint": "apps on this VDI pick it as their mic" }]
  },
  "configPath": "/home/maelo/.config/crosspoint/vdi.yaml",
  "configError": null
}
```

| Field | Values |
|---|---|
| `connection.state` | `connecting` `connected` `reconnecting` `error` |
| `connection.error` | null or `{ "code": "bad_password" \| "server_unreachable" \| "config", "message": "…" }` |
| `sending` | `active` (≥1 Console, not paused) · `paused` · `idle` (no Console listening) |
| `consoles[].kind` | `mac` `web` `other` (from the Console's peer info `self.kind`) |
| `input.status` | `ok` `missing` `silent` |
| `output.status` | `ok` `missing` |

## 4. Meters

Opt-in, because they're high-rate:
```json
{ "t": "sub", "topics": ["meters"] }
{ "t": "unsub", "topics": ["meters"] }
```
Server sends about 30 frames/s while subscribed, and pauses automatically when
the client reports `{ "t": "visibility", "hidden": true }` (resume with `false`).

Values are **dBFS rounded to 0.1, `null` for silence below −90 dBFS**, as
`[peak, rms]`.

Console:
```json
{ "t": "meters", "ts": 1760015520123,
  "stations": { "VDI-ACCT-07": [-12.3, -20.1], "VDI-DEV-02": [null, null] },
  "mic": [-18.0, -26.5], "output": [-10.2, -17.9] }
```
- `stations`: **pre-fader**, i.e. what the VDI sends, before my level, mute
  or dim (spec: activity stays visible when muted or dimmed). Online stations only.
- `mic`: **pre-gate**, so my level shows even while not transmitting.

Agent:
```json
{ "t": "meters", "ts": 1760015520123,
  "input": [-14.0, -22.0], "output": [null, null],
  "devices": { "loopback_sink.monitor": [-14.0, -22.0], "remote_mic": [-60.2, -70.0] } }
```
- `devices` (one level per available input node, for the picker) is sent
  only while the client is also subscribed to `"deviceMeters"`.

## 5. Commands

```json
{ "t": "cmd", "id": "c17", "cmd": "station.setLevel", "args": { "station": "VDI-ACCT-07", "db": -6 } }
{ "t": "ack", "id": "c17", "ok": true }
{ "t": "ack", "id": "c18", "ok": false, "error": { "code": "not_found", "message": "No station VDI-XYZ" } }
```
- `id` is chosen by the client and echoed. Every command gets exactly one `ack`.
- Commands that return data put it in `result`:
  `{ "t": "ack", "id": "c19", "ok": true, "result": { "previous": "loopback_sink.monitor" } }`.
- Continuous controls (level, pan, output) may be sent up to 30/s. The server
  applies the latest value and may coalesce patches.

### 5.1 Console commands
| `cmd` | `args` | Notes |
|---|---|---|
| `connection.connect` | `{}` or `{ server?, group?, password?, name? }` | uses saved values when omitted; saves on success |
| `connection.disconnect` | `{}` | |
| `station.setLevel` | `{ station, db }` | clamp −40…+6 |
| `station.setPan` | `{ station, pan }` | clamp −1…+1 |
| `station.setMute` | `{ station, on }` | |
| `station.setSolo` | `{ station, on }` | several solos allowed |
| `station.setTalk` | `{ station, on }` | stored even while paused by solo; `hearsYou` follows the gate |
| `station.forget` | `{ station }` | only for `offline` stations; error `busy` otherwise |
| `stations.talkToAll` | `{}` | `talk=true` on all |
| `stations.spread` | `{}` | pans evenly −1…+1 in `stationOrder` (1 station → 0) |
| `stations.centerAll` | `{}` | all pans 0 |
| `mic.setMode` | `{ mode: "open" \| "ptt" }` | switching to `ptt` releases any hold |
| `mic.setOn` | `{ on }` | open mode only; error `wrong_mode` in ptt |
| `mic.ptt` | `{ down: true \| false }` | per client; released on disconnect (§2.4) |
| `output.setLevel` | `{ db }` | |
| `devices.setInput` / `devices.setOutput` | `{ id }` | |
| `settings.set` | `{ soloDimDb?, pttHotkey?, codec?, bitrateKbps?, networkBuffer? }` | partial update |

### 5.2 Agent commands
| `cmd` | `args` | Notes |
|---|---|---|
| `agent.setInput` | `{ node }` | switches live **and** writes `audio.input_device` to the YAML (P2.1 `Config::save()`). The ack carries `{ "previous": "<node>" }` for Undo |
| `agent.setOutput` | `{ node }` | same, for `audio.output_device` |
| `agent.testTone` | `{ node }` | ~1.5 s, −18 dBFS, 440 Hz into that output node |
| `agent.pause` / `agent.resume` | `{}` | sending only |
| `agent.reloadConfig` | `{}` | errors land in `configError` and `connection.error` |
| `agent.retryNow` | `{}` | skip reconnect backoff |

### 5.3 Error codes
`bad_request` (malformed or wrong args) · `not_found` (unknown station or
node) · `wrong_mode` · `busy` (not allowed in the current state) ·
`not_supported` (feature not on this platform, e.g. global hotkey on web) ·
`io_error` (couldn't write the config) · `internal`.

## 6. Versioning
- `v` in `hello` and `/api/v1/` in paths. Additive changes (new fields, message
  types, commands, features) stay v1; clients ignore what they don't know.
- Renames or semantic changes need v2 (served alongside v1 for one release).
- The mock in `console-ui/src/api/mock.js` is the executable reference for
  this document; keep them in sync in the same PR.

## 7. Open points for the engine tasks
- **P4.1:** pick the WS implementation (small vendored lib vs JUCE sockets +
  minimal framing). Static files and `/api/v1/health` are served by the same
  listener.
- **P4.2:** remembered stations (colour index, level, pan, mute, talk,
  lastSeen) live in the Console's saved state; `forget` removes them.
- **P1.7 → P4.2:** the VDI's agent report is carried in peer info and copied
  into `stations{}.agent`.
- **Console `self.kind`** (`mac`/`web`) must be advertised in peer info so
  agents can show "Console · Mac" (add next to `role` in P1.7).
