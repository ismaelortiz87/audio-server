#!/bin/bash
# Runs the WebRTC gateway and the headless engine side by side (PulseAudio is
# already up, started by gateway/entrypoint.sh). If EITHER dies the other is
# stopped and the container exits non-zero, so `restart: unless-stopped` in
# compose restarts both together (a half-working Console is worse than a restart).
set -u

: "${CROSSPOINT_CONFIG:=/config/console.yaml}"
: "${CROSSPOINT_API_PORT:=7070}"
: "${CROSSPOINT_API_BIND:=0.0.0.0}"
: "${CROSSPOINT_UI_DIR:=/opt/crosspoint/ui}"

if [ -z "${CROSSPOINT_API_TOKEN:-}" ]; then
    echo "supervisor: CROSSPOINT_API_TOKEN is required (the API binds to a non-loopback address)" >&2
    exit 64
fi
if [ ! -f "$CROSSPOINT_CONFIG" ]; then
    echo "supervisor: config $CROSSPOINT_CONFIG not found (mount one, see console.example.yaml)" >&2
    exit 64
fi

ENGINE_ARGS=(--headless --role console --config "$CROSSPOINT_CONFIG"
    --api-port "$CROSSPOINT_API_PORT" --api-bind "$CROSSPOINT_API_BIND"
    --api-token "$CROSSPOINT_API_TOKEN" --ui-dir "$CROSSPOINT_UI_DIR")
[ -n "${CROSSPOINT_API_ORIGIN:-}" ] && ENGINE_ARGS+=(--api-allow-origin "$CROSSPOINT_API_ORIGIN")

python3 /app/gateway.py & GW=$!
/opt/crosspoint/bin/crosspoint "${ENGINE_ARGS[@]}" & ENG=$!

stop() {
    trap '' TERM INT
    kill -TERM "$ENG" "$GW" 2>/dev/null
    wait "$ENG" "$GW" 2>/dev/null
}
trap 'stop; exit 143' TERM INT

# wait -n: returns when the first child exits.
wait -n "$ENG" "$GW"
rc=$?
if kill -0 "$ENG" 2>/dev/null; then echo "supervisor: gateway exited ($rc)" >&2
else echo "supervisor: engine exited ($rc)" >&2; fi
stop
exit $(( rc == 0 ? 1 : rc ))
