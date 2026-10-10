#!/bin/bash
# Headless PulseAudio with the two virtual devices, then the gateway.
set -eu
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
# A restarted container keeps its writable layer: a stale pid file from the
# previous PulseAudio (same PID in the new PID namespace) makes the daemon refuse to start.
rm -rf "$XDG_RUNTIME_DIR/pulse"

# -n: skip default.pa (no hardware probing, no autospawn surprises).
# engine_out : the engine PLAYS here; the gateway reads engine_out.monitor.
# engine_in  : the gateway PLAYS the browser mic here; the engine RECORDS
#              from engine_in.monitor.
pulseaudio -n --daemonize=yes --exit-idle-time=-1 --log-target=stderr \
    -L "module-native-protocol-unix" \
    -L "module-null-sink sink_name=engine_out sink_properties=device.description=engine_out rate=48000 channels=2" \
    -L "module-null-sink sink_name=engine_in sink_properties=device.description=engine_in rate=48000 channels=2"
for _ in $(seq 1 50); do pactl info >/dev/null 2>&1 && break; sleep 0.1; done
pactl set-default-sink engine_out
pactl set-default-source engine_in.monitor

if [ "$#" -gt 0 ]; then exec "$@"; fi
exec python3 /app/gateway.py
