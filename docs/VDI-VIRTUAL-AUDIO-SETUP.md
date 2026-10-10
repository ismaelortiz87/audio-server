# Crosspoint Virtual Audio — Replication Recipe

Debian 13 (trixie) VDI virtual audio setup: a virtual **output** apps can play
into and a virtual **mic** that other apps can feed and capture. Captured
verbatim from the three production hosts (192.168.0.71 / 192.168.0.73 /
192.168.40.10) — the unit file is byte-identical on all of them.

## How it works

Three PulseAudio modules running inside `pipewire-pulse` (no ALSA dummy
modules, no hidden bus, no standalone PulseAudio):

| Node | Module | Role |
|---|---|---|
| `Crosspoint-Output` | `module-null-sink` | The output. Apps play here; `.monitor` lets others capture system audio. |
| `crosspoint_input_null` | `module-null-sink` | What apps play *into* to "speak" to the mic. |
| `Crosspoint-Input` | `module-remap-source` | The recordable mic: a source reading `crosspoint_input_null.monitor`. |

Defaults are set on every load: `Default Sink = Crosspoint-Output`,
`Default Source = Crosspoint-Input`.

**Naming rules (why it looks like it does):**

- `device.description` values use underscores, not spaces — Pulse/PipeWire
  argument parsers truncate on a space and quotes don't survive either.
  Plasma/pavucontrol render `Crosspoint_Output` as "Crosspoint Output".
- Never give two nodes **in the same Plasma list** (sinks, or sources) the
  same friendly name — Plasma disambiguates by appending the node id
  (that's the origin of the old `Crosspoint_Input-49`). The two
  "Crosspoint Input" entries here live in different lists (one sink, one
  source), so no suffix appears.
- The internal transport's `.monitor` nodes keep their own distinct names.

## 0. Packages

Stock trixie versions (what the three VDIs run):

```bash
sudo apt install -y pipewire pipewire-pulse wireplumber pulseaudio-utils alsa-utils
# pipewire 1.4.2-1, pipewire-pulse 1.4.2-1, wireplumber 0.5.8-2,
# pulseaudio-utils 17.0+dfsg1-2+b1, alsa-utils 1.2.14-1
```

(Add `libasound2-plugins:i386 libpulse0:i386 libsoxr0:i386 libcaca0:i386`
only if 32-bit apps need audio — `.40.10` has them, the others don't.)

## 1. Low-latency clock profile

`~/.config/pipewire/pipewire.conf.d/90-low-latency.conf`

```bash
mkdir -p ~/.config/pipewire/pipewire.conf.d
cat > ~/.config/pipewire/pipewire.conf.d/90-low-latency.conf <<'EOF'
context.properties = {
    default.clock.rate          = 48000
    default.clock.allowed-rates = [ 44100 48000 ]
    default.clock.quantum       = 256
    default.clock.min-quantum   = 64
    default.clock.max-quantum   = 1024
}
EOF
```

## 2. Crosspoint devices — user systemd unit

`~/.config/systemd/user/sonobus-sinks.service` (name kept for continuity)

```bash
mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/sonobus-sinks.service <<'EOF'
[Unit]
Description=Crosspoint virtual audio (Crosspoint Output / Crosspoint Input)
After=pipewire-pulse.service
Requires=pipewire-pulse.service

[Service]
Type=oneshot
RemainAfterExit=yes
Environment=XDG_RUNTIME_DIR=/run/user/%U
ExecStart=/bin/bash -c '\
  pactl load-module module-null-sink sink_name=Crosspoint-Output sink_properties=device.description=Crosspoint_Output && \
  pactl load-module module-null-sink sink_name=crosspoint_input_null sink_properties=device.description=Crosspoint_Input && \
  sleep 0.5 && \
  pactl load-module module-remap-source source_name=Crosspoint-Input source_properties=device.description=Crosspoint_Input master=crosspoint_input_null.monitor master_channel_map=front-left,front-right channels=2 && \
  pactl set-default-sink Crosspoint-Output && \
  pactl set-default-source Crosspoint-Input'
ExecStop=/bin/bash -c 'pactl unload-module module-null-sink 2>/dev/null; pactl unload-module module-remap-source 2>/dev/null; true'

[Install]
WantedBy=pipewire-pulse.service
EOF
chmod 600 ~/.config/systemd/user/sonobus-sinks.service
systemctl --user daemon-reload
systemctl --user enable --now sonobus-sinks.service
```

Details worth keeping:

- The **`sleep 0.5`** after loading `crosspoint_input_null` and before the
  `remap-source` is deliberate: the monitor must exist before the remap
  attaches to it. Don't remove it.
- `ExecStop` unloads **both** module types (the complete variant) so a
  restart leaves no dangling remap behind.
- `Type=oneshot` + `RemainAfterExit=yes` + `WantedBy=pipewire-pulse.service`:
  the modules load once when the user's `pipewire-pulse` comes up.

## Verify

```bash
systemctl --user is-active sonobus-sinks.service   # → active
pactl list short sinks     # Crosspoint-Output, crosspoint_input_null, .monitors
pactl list short sources   # Crosspoint-Input
pactl info | grep -E "Default (Sink|Source)"        # → Crosspoint-Output / Crosspoint-Input
```

Optional tone round-trip (mic path: tone into the transport sink, capture the
mic):

```bash
( speaker-test -t sine -f 440 -D pulse:crosspoint_input_null -c 2 -T 3 -Q >/dev/null 2>&1 & )
sleep 0.4
timeout 2 pw-record -a --rate=48000 --channels=2 --format=s16 --target=Crosspoint-Input > /tmp/cap.raw
ls -l /tmp/cap.raw   # non-trivial size = path works
```

## Notes & caveats

- **User-session service.** It runs when the user's login session (sddm
  desktop) is up — how all three VDIs are used. For a headless box, enable
  lingering instead: `sudo loginctl enable-linger $USER`.
- **Volumes are not persisted** (none of the three hosts keep
  `~/.local/state/pipewire/` state): after a reboot the endpoints come back
  at 100%. Current behavior on all three — a new box behaves the same.
- **Mono (if ever needed):** `module-null-sink` is hardcoded 2-channel; a
  mono endpoint must be a remap node. Mic: add
  `channels=1 channel-map=mono` to the existing `module-remap-source` line
  (and drop `master_channel_map`). Output: add a one-line
  `module-remap-sink channels=1 channel-map=mono master=Crosspoint-Output`
  in front of the output null-sink. Not part of the current setup.
- **WirePlumber 0.5.x rejects old-format Lua** (`main.lua.d/*.lua`) with a
  "NOT supported" warning — don't try to rename/hide nodes that way on this
  version; use PipeWire-level `device.description` properties instead.
