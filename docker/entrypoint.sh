#!/bin/bash
# Bring up a virtual display, the native SonoBus GUI, and a noVNC bridge so the
# app is reachable from a browser at http://localhost:6080/vnc.html
set -u

GEOMETRY="${SCREEN_GEOMETRY:-1600x1000x24}"

echo "[entrypoint] starting Xvfb on ${DISPLAY} (${GEOMETRY})"
Xvfb "${DISPLAY}" -screen 0 "${GEOMETRY}" -nolisten tcp &
XVFB_PID=$!
# Wait for the X socket to appear rather than sleeping a fixed amount.
for _ in $(seq 1 50); do
    [ -e "/tmp/.X11-unix/X${DISPLAY#:}" ] && break
    sleep 0.1
done

if ! kill -0 "$XVFB_PID" 2>/dev/null; then
    echo "[entrypoint] ERROR: Xvfb died" >&2
    exit 1
fi

# --- audio ------------------------------------------------------------------
# Containers have no /dev/snd, so JUCE's ALSA backend finds no soundcard and the
# engine never starts its audio callback. Give it a PulseAudio null sink and
# point ALSA's default PCM at it: the engine then runs at 48 kHz stereo with a
# real clock, and the signal graph is live (verify with
# `pactl list short sink-inputs` -> "ALSA plug-in [sonobus]").
#
# A second null sink acts as the engine's *microphone*: the browser bridge
# writes into it and the engine records its monitor, which is what lets a
# remote browser talk back into the session.
echo "[entrypoint] starting PulseAudio"
pulseaudio --start --exit-idle-time=-1 >/dev/null 2>&1 || true
for _ in $(seq 1 30); do
    pactl info >/dev/null 2>&1 && break
    sleep 0.2
done
pactl load-module module-null-sink sink_name="${SONOBUS_SINK:-sonobus}" >/dev/null 2>&1 || true
pactl load-module module-null-sink sink_name="${SONOBUS_MIC:-sonobusmic}" >/dev/null 2>&1 || true
pactl set-default-sink "${SONOBUS_SINK:-sonobus}" >/dev/null 2>&1 || true
pactl set-default-source "${SONOBUS_MIC:-sonobusmic}.monitor" >/dev/null 2>&1 || true
printf 'pcm.!default { type pulse }\nctl.!default { type pulse }\n' > /etc/asound.conf

echo "[entrypoint] starting x11vnc"
# -forever keeps the server alive across client disconnects; -shared allows
# more than one browser tab.
x11vnc -display "${DISPLAY}" -rfbport "${VNC_PORT:-5900}" -forever -shared -nopw -quiet &
X11VNC_PID=$!

# Wait for x11vnc so the bridge's websocket relay has something to connect to.
for _ in $(seq 1 50); do
    (echo > "/dev/tcp/127.0.0.1/${VNC_PORT:-5900}") >/dev/null 2>&1 && break
    sleep 0.1
done

echo "[entrypoint] starting SonoBus"
/app/sonobus/sonobus "$@" &
APP_PID=$!

# Give the engine a moment to appear as a PulseAudio client before the bridge
# starts capturing its monitor source.
sleep 2

echo "[entrypoint] starting audio bridge on :${BRIDGE_PORT:-6080}"
python3 /app/bridge/server.py &
BRIDGE_PID=$!

# Shut everything down together when the container stops.
term() {
    echo "[entrypoint] shutting down"
    kill "$BRIDGE_PID" "$APP_PID" "$X11VNC_PID" "$XVFB_PID" 2>/dev/null
    wait
}
trap term TERM INT

wait "$APP_PID"
