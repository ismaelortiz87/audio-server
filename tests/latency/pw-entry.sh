#!/usr/bin/env bash
# L1: entrypoint for the PipeWire backend of tests/latency/run.sh, inside the
# P2.11 image (tests/linux/Dockerfile). Starts PipeWire + WirePlumber +
# pipewire-pulse with the same two null sinks as the web Console container
# (engine_out: the engine plays here; engine_in: the test plays here and the
# engine records engine_in.monitor), then execs the command.
set -eu
export HOME=/root XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork --print-address)"
export DBUS_SESSION_BUS_ADDRESS
pipewire > /tmp/pipewire.log 2>&1 &
for _ in $(seq 40); do pw-cli info 0 >/dev/null 2>&1 && break; sleep 0.25; done
wireplumber > /tmp/wireplumber.log 2>&1 &
pipewire-pulse > /tmp/pipewire-pulse.log 2>&1 &
for _ in $(seq 40); do pactl info >/dev/null 2>&1 && break; sleep 0.25; done
pactl load-module module-null-sink sink_name=engine_out sink_properties="device.description=engine_out node.driver=true" rate=48000 channels=2 >/dev/null
pactl load-module module-null-sink sink_name=engine_in sink_properties="device.description=engine_in node.driver=true" rate=48000 channels=2 >/dev/null
for _ in $(seq 40); do pactl list short sinks | grep -q engine_in && break; sleep 0.25; done
pactl set-default-sink engine_out
pactl set-default-source engine_in.monitor
exec "$@"
