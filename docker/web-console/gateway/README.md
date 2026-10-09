# WebRTC Opus audio gateway (P7.3)

Standalone sidecar that bridges the container's PulseAudio devices to **one
browser** over WebRTC (Opus, both directions). Audio is UDP straight between
browser and container on the VPN (ROADMAP D13); only signalling goes through
the HTTP reverse proxy.

```
engine ──plays──▶ PA null sink engine_out ─.monitor─▶ pulsesrc ▶ opusenc ▶ rtpopuspay ▶ webrtcbin ═UDP═▶ browser <audio>
engine ◀─records─ engine_in.monitor ◀─ PA null sink engine_in ◀ pulsesink ◀ opusdec ◀ rtpopusdepay ◀ webrtcbin ◀═UDP═ browser mic
```

## Tech choice
GStreamer `webrtcbin` driven from Python (`gi`), `aiohttp` for HTTP. Chosen over
Go+pion because libnice/webrtcbin/opus/pulse are packaged in Debian trixie, so
the image has no compile step, and the jitter buffer, NACK, FEC and RTCP stats
come for free. The uplink uses a null sink + `.monitor` (rather than a pipe
source) because a null sink has a real clock, so the engine's recorder never
starves or needs a writer to be present (it just reads silence).

## HTTP API (default port 8090)
| Route | |
|---|---|
| `POST /rtc/offer` | JSON `{type:"offer",sdp}` -> `{type:"answer",sdp}`. Non-trickle: answer is returned after ICE gathering. One active session; a new offer replaces the old one. |
| `POST /rtc/close` | tear down the session |
| `GET /rtc/stats` | JSON: `state`, `rtt_s`, `inbound[]`/`outbound[]` (bytes, packets, lost, jitter), `remote_inbound[]`, `raw` |
| `GET /rtc/config` | `{iceServers, udpRange, frameMs}` for the browser's RTCPeerConnection |
| `GET /rtc/test` | test page (getUserMedia -> offer -> play; `window.__level`, `window.__micLevel`) |
| `GET /rtc/health` | liveness |

## Environment
| Var | Default | |
|---|---|---|
| `HTTP_PORT` | 8090 | signalling/HTTP port |
| `RTC_UDP_MIN` / `RTC_UDP_MAX` | 40000 / 40019 | ICE UDP port range (publish exactly this range) |
| `RTC_PUBLIC_IP` | autodetect | address advertised as host candidate (container's VPN IP). If unset, all container interface addresses are advertised. |
| `RTC_TURN_URL`, `RTC_TURN_USER`, `RTC_TURN_PASS` | unset | optional TURN (`turn:host:3478?transport=udp`); also returned by `/rtc/config` |
| `RTC_SRC_DEVICE` | `engine_out.monitor` | downlink source |
| `RTC_SINK_DEVICE` | `engine_in` | uplink sink |
| `OPUS_FRAME_MS` | 10 | 2.5/5/10/20/40/60 |
| `OPUS_BITRATE` | 128000 | downlink bitrate (stereo, restricted-lowdelay, FEC on) |
| `RTC_JITTER_MS` | 30 | webrtcbin jitter buffer latency |
| `PA_BUFFER_US` / `PA_LATENCY_US` | 40000 / 10000 | Pulse buffer / latency-time |

## Run
```
docker build -t crosspoint-rtc-gw docker/web-console/gateway
docker run --rm -e RTC_PUBLIC_IP=<vpn-ip> -p 8090:8090 \
  -p <vpn-ip>:40000-40019:40000-40019/udp crosspoint-rtc-gw
```
Entry point starts a headless PulseAudio (user `console`, `-n`, only
`module-native-protocol-unix` and the two null sinks), sets the defaults, then
runs the gateway. Pass a command to the container to run something else after
audio is up (e.g. the engine).

## Connecting the engine (P7.1)
Run the engine as the same user/container so it uses the same Pulse socket
(`XDG_RUNTIME_DIR=/tmp/runtime`). Engine **output** device = Pulse sink
`engine_out` (already the default sink); engine **input** device = Pulse source
`engine_in.monitor` (already the default source). If the engine uses ALSA via
the Pulse plugin, `pcm.!default { type pulse }` is enough.
Both devices are 48 kHz stereo; the browser mic is mono and appears on both channels.

## Proxy / network notes
- Only `/rtc/*` (plain HTTP, no WebSocket needed, no buffering concerns) goes through the reverse proxy on the same origin.
- **UDP does not go through the proxy.** The browser must reach `RTC_PUBLIC_IP:RTC_UDP_MIN..MAX` directly (VPN). Publish that exact range in compose.
- Non-trickle signalling means candidates are fixed in the answer; when `RTC_PUBLIC_IP` is set the host candidates are rewritten to it (IPv6 dropped).
- If direct UDP is impossible, set the `RTC_TURN_*` vars (TURN server is not shipped).
- Browsers on `https://` origins need the mic permission (getUserMedia needs a secure context).

## Test
`node docker/web-console/gateway/test/e2e.mjs [--container cp-rtc-gw]` with the container running as above
(`--name cp-rtc-gw -e RTC_PUBLIC_IP=127.0.0.1`, ports published on 127.0.0.1). Drives headless Chrome over CDP:
440 Hz tone into `engine_out` must raise `window.__level` and silence lower it; Chrome's fake mic (660 Hz wav) must
appear at `engine_in.monitor` with RMS well above silence and a 660 Hz peak. Needs Node >= 22 and Chrome
(`--chrome PATH`). Chrome's `--disable-features=AudioServiceSandbox` is required for the fake-capture wav to be readable on macOS.
