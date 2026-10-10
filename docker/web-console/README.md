# Crosspoint web Console container (P7.1)

One container = one user's Console: the native engine running **headless in
Console role** (it joins the AOO group, mixes, serves the control API and the
Console UI) plus the WebRTC audio gateway ([`gateway/`](gateway/README.md)),
both on one PulseAudio server. The browser is only a remote control and an
audio endpoint.

```
                          HTTPS reverse proxy (maelo's, D13)
browser ── / , /api/ (+WS) ───────────▶ :7070  engine: Console UI (--ui-dir) + control API
        ── /rtc/*  (plain HTTP) ──────▶ :8090  gateway: WebRTC signalling
        ══ WebRTC Opus media, UDP ═════▶ VPN-IP:40000-40019 (bypasses the proxy)

engine ──plays──▶ Pulse sink engine_out ─.monitor─▶ gateway ═▶ browser
engine ◀─records─ Pulse source engine_in.monitor ◀─ gateway ◀═ browser mic
engine ◀══ AOO audio/control (UDP/TCP) ══▶ VDI peers; rendezvous via aooserver (P8.1)
```

## Files
| File | |
|---|---|
| `Dockerfile` | build context = **repo root**. Stage 1 builds `SonoBus_Standalone` on `debian:trixie`; stage 2 is the gateway runtime (PulseAudio, GStreamer, Python) + the engine at `/opt/crosspoint/bin/crosspoint`, `console-ui/` at `/opt/crosspoint/ui`, `/etc/asound.conf` (ALSA default PCM/ctl -> Pulse). |
| `supervisor.sh` | runs gateway + engine; **if either exits the other is stopped and the container exits non-zero** (compose `restart: unless-stopped` then restarts both). SIGTERM is forwarded to both (the engine quits cleanly in headless mode). |
| `gateway/entrypoint.sh` | unchanged: starts PulseAudio with the two null sinks, then `exec`s the command (our `supervisor.sh`). `tini` is PID 1. |
| `console.example.yaml` | engine config, mounted at `/config/console.yaml`. |
| `docker-compose.yml` | one service; copy per user (P7.5). |

Everything runs as the unprivileged user `console` (uid 1000),
`XDG_RUNTIME_DIR=/tmp/runtime`.

## Build and run
```
DOCKER_BUILDKIT=0 docker build -f docker/web-console/Dockerfile -t crosspoint-web-console .   # ~5 min, arm64 on Apple silicon
CROSSPOINT_API_TOKEN=$(openssl rand -hex 24) RTC_PUBLIC_IP=10.8.0.2 \
  docker compose -f docker/web-console/docker-compose.yml up -d
```
(`DOCKER_BUILDKIT=0` only if a remote buildx builder is selected.) The first
`apt-get`/compile is slow; later builds reuse the cache until `sonobus/` changes.

## Environment
| Var | Default | |
|---|---|---|
| `CROSSPOINT_API_TOKEN` | **required** | bearer token for the control API (`--api-token`). The container refuses to start without it. |
| `CROSSPOINT_API_ORIGIN` | unset | the one browser origin allowed for CORS / WebSocket (`--api-allow-origin`), e.g. `https://crosspoint.app.lagreca.io` |
| `CROSSPOINT_CONFIG` | `/config/console.yaml` | engine YAML |
| `CROSSPOINT_API_PORT` / `_BIND` | 7070 / 0.0.0.0 | |
| `CROSSPOINT_UI_DIR` | `/opt/crosspoint/ui` | |
| `RTC_PUBLIC_IP`, `RTC_UDP_MIN/MAX`, `RTC_*`, `OPUS_*` | see gateway README | WebRTC media. `RTC_PUBLIC_IP` = the VPN IP the browser reaches. |

## Reverse proxy
- `/` and `/api/` -> `http://host:7070`, with **WebSocket upgrade** (`/api/v1/ws`), `proxy_read_timeout` long, no buffering.
- `/rtc/` -> `http://host:8090` (plain HTTP POST/GET).
- UDP `RTC_UDP_MIN..MAX` is **not proxied**: the browser connects to `RTC_PUBLIC_IP` directly over the VPN.
- Set `CROSSPOINT_API_ORIGIN` to the public origin so the engine's Origin check passes.

## Security
- The API binds `0.0.0.0` inside the container, hence the mandatory token. Publish 7070/8090 only on loopback or the proxy-facing interface (`HTTP_BIND`), never on a public address.
- The token authenticates the WebSocket (`hello` then `auth`). `/rtc/offer` has **no auth** (gateway README): protect it at the proxy or keep it VPN-only.
- The config file holds the group password: mount it read-only, `chmod 600`.
- The UDP range is published on the VPN IP only.

## Not done here (still needed)
- **P7.2**: the UI's API/WS base URL and token handoff; proxy route examples for the final hostname; the UI currently has to be told the token by the user.
- **P7.4**: the actual proxy at `crosspoint.app.lagreca.io`, and a real-VPN check that ICE picks the direct path (TURN otherwise).
- **P7.5**: per-user compose generation (names, ports, token, config), docs.
- **P7.6**: remove the old VNC `docker/` files.
- Latency on the real VPN is unmeasured; aooserver must be reachable from the container.

## Test
- Engine smoke: `GET /api/v1/health` -> role console; `GET /` -> the Console UI; WebSocket `/api/v1/ws` sends `hello` with `auth:"token"`, then `{"t":"auth","token":...}`. The WS `Origin` must be `CROSSPOINT_API_ORIGIN` (or the engine's own `http://localhost:7070`).
  `docker exec <c> pactl list short sink-inputs` / `source-outputs` must show the engine's `ALSA plug-in [crosspoint]` playback (to `engine_out`) and capture (from `engine_in.monitor`) streams.
- Audio through the real engine, `test/e2e-engine.mjs`: an aooserver and a second container of this image acting as the VDI
  (`docker run ... crosspoint-web-console /opt/crosspoint/bin/crosspoint --headless --config vdi.yaml --api-port 0`, role vdi, same group, on one Docker network), then
  `node docker/web-console/test/e2e-engine.mjs --url http://127.0.0.1:<published 8090>`. A 440 Hz tone is played into the VDI's `engine_in`; headless Chrome must hear it over WebRTC; Chrome's fake mic (660 Hz) must appear at the VDI's `engine_out.monitor`.
- A host (macOS) VDI peer cannot reach the container's engine: Docker Desktop does not route to container IPs, and AOO peers connect to each other directly over UDP (aooserver only introduces them). Use containers on one network, or run it on a Linux host/VPN.

## Latency (L1, fixed)
Engine<->engine latency between two containers of this image is ~50-85 ms, both directions (`tests/latency/run.sh`, which also covers the P2.11 PipeWire path). It used to be seconds and grow with uptime (4-6 s after a minute, ~11-13 s after hours): the alsa-plugins `pulse` capture PCM queues up to 4 MiB (~11 s of float stereo) behind an `avail` clamped to the ALSA buffer, so every stall of the engine's read loop (device start-up; Pulse renegotiating latency when another client such as the gateway connects to a null sink) became permanent input latency. The engine's JUCE ALSA backend now drops that backlog whenever the capture delay exceeds two ALSA buffers (`juce_ALSA_linux.cpp`, `dropCaptureBacklog`). `PULSE_LATENCY_MSEC` is not needed.


## Sign-in (P7.2)

Browsers sign in once: the Console page shows "Sign in to this Console", the
user pastes `CROSSPOINT_API_TOKEN`, and the engine answers `POST
/api/v1/session` with an HttpOnly, SameSite=Strict cookie (30 days). The cookie
is derived from the token, so rotating the token signs every browser out. The
proxy must forward `/api/` (including `/api/v1/session`) to 7070 and should set
`X-Forwarded-Proto: https`, so the cookie also gets the `Secure` flag.
