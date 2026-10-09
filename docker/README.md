# SonoBus in the browser (Docker)

Run the real, unmodified SonoBus desktop app inside a container, drive its GUI
from a browser, and **send and receive its audio** through the browser. This is
the native application — same binary, same GUI, same AOO engine — not a
reimplementation.

## Quick start

```bash
docker compose -f docker/docker-compose.yml up --build

open http://localhost:6080/          # audio client: connect, monitor, talk
open http://localhost:6080/vnc.html  # the app's own GUI: pick a group, connect
```

The first build compiles JUCE + AOO + SonoBus from source (~4 minutes on an
M-series Mac with 8 jobs). Later runs reuse the cached layer.

**Typical session:** open the GUI view, press *Connect* and join a group, then
open the audio client, press **Connect audio**, and **Enable microphone** if you
want to talk back. Browsers require that button press before audio can start —
it is not a bug.

### If you're on Apple Silicon

Build for your native architecture. If an amd64 builder is selected, Docker
falls back to qemu emulation: the image still works, but it is far slower and
the audio device may not attach at all. Check with:

```bash
docker image inspect sonobus-web:latest --format '{{.Architecture}}'   # want: arm64
```

If it reports `amd64`, build with the native builder instead:

```bash
DOCKER_BUILDKIT=0 docker build -f docker/Dockerfile -t sonobus-web:latest .
```

This bit us during development: a `buildkit-priv` kubernetes builder that only
advertises `linux/amd64` was active, producing an emulated image where `pactl`
never showed the app's audio stream.

## Verified working

Checked against this tree on macOS with Docker Desktop (native arm64 image):

| Check | Result |
|-------|--------|
| Linux build of `juce/` + `aoo/` + `sonobus/` from the hoisted layout | `[183/183]` link, exit 0 |
| `docker compose up --build` from a clean image cache | exit 0 |
| GUI renders in the container's virtual display | confirmed by screenshot |
| GUI reachable in a browser | `http://localhost:6080/vnc.html` → HTTP 200 |
| VNC transport through the bridge's relay | receives `RFB 003.008` handshake |
| Audio engine opens a device with no `/dev/snd` | `pactl` shows uncorked `ALSA plug-in [sonobus]`, `float32le 2ch 48000Hz` |
| **Downlink**: engine audio reaches a WebSocket client | 100 frames, 3840 B/frame, peak 12000 / RMS 8485 matching the injected 440 Hz tone |
| **Uplink**: browser PCM reaches the engine's mic source | 2.00 s sent, captured back with peak 9000 / RMS 3414 at 660 Hz |
| Playback ring buffer correctness | rate-matched simulation: bit-exact, 0 underruns, 0 drops, 21 ms latency |
| Real Chrome loads the client and streams | `status: streaming`, `48.0 kHz`, `buffered: 37 ms`, bridge reports `clients: 1` |
| Joins a real AOO group through `aoo.sonobus.net` | connected as `probebot` to `dockerprobe`, timer running |

## How it works

One multi-stage `Dockerfile`:

1. **`builder`** — Ubuntu 22.04 with the Linux toolchain and audio/UI dev
   headers. Copies `juce/`, `aoo/` and `sonobus/` to `/src` preserving the
   monorepo layout, because `sonobus/CMakeLists.txt` resolves `../juce` and
   `../aoo`. Builds the `SonoBus_Standalone` target and asserts the binary.
2. **`runtime`** — Ubuntu 22.04 plus Xvfb, x11vnc, noVNC, PulseAudio and
   `python3-aiohttp`. Copies only the compiled binary from the builder, plus the
   bridge and its browser client, then runs `docker/entrypoint.sh`.

The entrypoint starts, in order: Xvfb → PulseAudio (two null sinks) → x11vnc →
SonoBus → the audio bridge. Everything is torn down together on `SIGTERM`.

### Two container problems, and how they're solved

**No X server.** SonoBus's GUI mode segfaults without a display; only
`--headless` survives. Rather than strip the GUI, Xvfb provides a real display
so the actual app runs unmodified, and x11vnc + noVNC put it in the browser.

**No `/dev/snd`.** JUCE's ALSA backend enumerates soundcards, finds none, and
never starts its audio callback. The entrypoint creates two PulseAudio null
sinks and points ALSA's `default` PCM at them:

- `sonobus` — the engine's **output**. Its monitor source is what the browser
  hears.
- `sonobusmic` — the engine's **input**. Its monitor is set as the default
  recording source, so the engine records whatever the browser sends.

### The audio bridge (`docker/bridge/server.py`)

A small aiohttp app on port 6080 that serves three things:

| Route | Purpose |
|-------|---------|
| `/` | The audio client page |
| `/ws` | Bidirectional PCM websocket (downlink + uplink) |
| `/websockify` | Relays the browser's VNC websocket to x11vnc, so the whole app needs only one port |
| `/static/*` | The two AudioWorklet modules |

Audio is raw interleaved **signed 16-bit little-endian stereo at 48 kHz**, the
rate the engine runs at internally:

```
downlink   sonobus.monitor --parec--> /ws --> AudioWorklet --> speakers
uplink     microphone --> AudioWorklet --> /ws --pacat--> sonobusmic
```

Details that matter for live audio:

- **One capture, many clients.** The monitor is read once and fanned out to
  every connected browser via bounded queues, so a slow client drops its own
  oldest frames instead of stalling the capture loop.
- **One `pacat` per client.** They all write into the same null sink, which
  PulseAudio mixes, so several browsers can talk at once.
- **20 ms frames** — a deliberate balance between header overhead and latency.
- **The playback worklet is a ring buffer**, not a pass-through. Network jitter
  would otherwise cause dropouts; buffering decouples arrival from playback. It
  prebuffers ~40 ms and reports buffered/underrun/dropped counters to the page.
- **The AudioContext is pinned to 48 kHz.** Left at the hardware default (often
  44.1 kHz) the browser would resample and everything would play at the wrong
  pitch.
- **Uplink has backpressure**: if `ws.bufferedAmount` grows, capture blocks are
  dropped rather than queued without bound.

## Options

`SCREEN_GEOMETRY` (default `1600x1000x24`) sets the virtual screen; the browser
view scales to fit. Extra argv after the image name goes to the binary:

```bash
docker run --rm -p 6080:6080 sonobus-web:latest -g mygroup -n myname
```

The usual flags apply: `-g|--group`, `-n|--username`, `-p|--group-password`,
`-s|--server`, `-l|--load-setup`, `-q|--headless`.

## Limitations

- **Latency is additive.** Browser audio goes through PulseAudio, a websocket,
  and the Web Audio API. Expect tens to low hundreds of milliseconds on top of
  what SonoBus itself adds. Fine for monitoring and casual conversation; not
  suitable for tight remote jamming, which is what the native app is for.
- **No echo cancellation across the loop.** The mic path uses the browser's
  AEC, but if you monitor the engine's output on speakers while sending your
  mic, you can create a feedback loop. Wear headphones.
- **Single instance per container.** The bridge assumes one engine.
- **`--headless` bypasses all of this** — it starts no GUI and connects only a
  preconfigured group, with no browser audio path.

## Why not WebAssembly?

Considered and rejected for now: JUCE 7.0.8 ships `juce_SystemStats_wasm.cpp`
but **no `juce_audio_devices` WASM backend at all**, and AOO's transport is raw
BSD sockets (`AF_INET`/`SOCK_STREAM`, SLIP-framed UDP) which the browser sandbox
forbids. A WASM build would mean writing a new WebRTC-data-channel transport
inside the engine — much larger than the bridge above, for lower fidelity than
the native path.
