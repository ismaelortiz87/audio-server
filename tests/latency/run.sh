#!/usr/bin/env bash
# L1 -- engine<->engine audio latency, measured end to end in containers.
#
# Starts an aooserver, a VDI engine and a Console engine (headless, roles from
# YAML, same group) as three containers on one private Docker network, then for
# each direction plays tone bursts into the sender's Pulse sink `engine_in`
# (the sender engine records engine_in.monitor) and timestamps their onsets at
# the sender's engine_in.monitor and at the receiver's engine_out.monitor (the
# Pulse sink the receiver engine plays into). The onset delay between the two
# is the whole chain: Pulse in -> engine capture -> AOO send -> AOO recv ->
# engine output -> Pulse out. Both listeners use the same probe and read the
# same kernel clock (containers on one Docker VM), so probe latency cancels.
#
#   tests/latency/run.sh [--backend pulse|pipewire] [--runs 3] [--build]
#                        [--image IMG] [--engine LINUX_BINARY] [--warmup 8]
#                        [--name PREFIX] [--env K=V]... [--keep]
#
#   --backend pulse     (default) the web Console image (docker/web-console):
#                       PulseAudio null sinks, engine on ALSA's default PCM ->
#                       the alsa-plugins `pulse` PCM.
#   --backend pipewire  the P2.11 image (tests/linux): PipeWire + pipewire-pulse
#                       null sinks, engine pinned by YAML audio.input_device /
#                       output_device -> the generated `type pipewire` PCMs (the
#                       Debian VDI path).
#   --build             (re)build the image first (default only if it is missing).
#   --engine BIN        copy this Linux engine binary into both engine
#                       containers instead of the image's (quick A/B tests).
#   --env K=V           extra environment for both engines (repeatable).
#   --warmup S          seconds to let the pair run before measuring (default 8).
#   --keep              leave the containers and network running.
#   --target MS         pass threshold for every run's median (default 150).
#   --name PREFIX       container/network name prefix (default cp-l1-<pid>);
#                       nothing is published on host ports.
#   CROSSPOINT_AOO_IMAGE  aooserver image (default crosspoint-aooserver, built
#                       from docker/aooserver/Dockerfile when missing).
#
# Each run and direction plays 5 bursts of 200 ms (1 kHz, -6 dBFS, 1 s apart)
# and a 3 s continuous tone. It reports the onset delay (median/min/max of the
# bursts) and "dropouts": silent runs >= 1 ms inside the receiver's tones that
# were not already at the sender (a cut/split tail counts as one more).
# Prints one line per run and direction and a summary; exits 0 when every
# run delivered every burst and every median is under --target, 1 otherwise,
# 2 on setup errors. Measured (L1): ~10 s before the fix, ~50-90 ms after.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/tests/latency"

BACKEND=pulse RUNS=3 BUILD=0 IMAGE="" ENGINE="" WARMUP=8 KEEP=0 TARGET=150
NAME="cp-l1-$$"
BURSTS=5 GAP=1.0 BURST_MS=200 TAIL=3 HZ=1000
EXTRA_ENV=()
while [ $# -gt 0 ]; do
  case "$1" in
    --backend) BACKEND="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --build) BUILD=1; shift ;;
    --image) IMAGE="$2"; shift 2 ;;
    --engine) ENGINE="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --name) NAME="$2"; shift 2 ;;
    --env) EXTRA_ENV+=(-e "$2"); shift 2 ;;
    --target) TARGET="$2"; shift 2 ;;
    --keep) KEEP=1; shift ;;
    -h|--help) sed -n '2,43p' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done

case "$BACKEND" in
  pulse)    IMAGE="${IMAGE:-crosspoint-web-console}"; DOCKERFILE="docker/web-console/Dockerfile"
            BIN=/opt/crosspoint/bin/crosspoint ;;
  pipewire) IMAGE="${IMAGE:-crosspoint-pw-test}";     DOCKERFILE="tests/linux/Dockerfile"
            BIN=/opt/crosspoint/sonobus ;;
  *) echo "--backend must be pulse or pipewire" >&2; exit 2 ;;
esac
AOO_IMAGE="${CROSSPOINT_AOO_IMAGE:-crosspoint-aooserver}"

have_image() { docker image inspect "$1" >/dev/null 2>&1; }
if [ "$BUILD" = 1 ] || ! have_image "$IMAGE"; then
  echo "== building $IMAGE ($DOCKERFILE)"
  DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-0}" docker build -q -f "$ROOT/$DOCKERFILE" -t "$IMAGE" "$ROOT" >/dev/null \
    || { echo "ERROR: build of $IMAGE failed" >&2; exit 2; }
fi
if ! have_image "$AOO_IMAGE"; then
  echo "== building $AOO_IMAGE (docker/aooserver/Dockerfile)"
  DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-0}" docker build -q -f "$ROOT/docker/aooserver/Dockerfile" -t "$AOO_IMAGE" "$ROOT/aooserver" >/dev/null \
    || { echo "ERROR: build of $AOO_IMAGE failed" >&2; exit 2; }
fi

NET="$NAME-net" AOO="$NAME-aoo" VDI="$NAME-vdi" CON="$NAME-con"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/cp-l1.XXXXXX")"
cleanup() {
  if [ "$KEEP" = 1 ]; then echo "kept: containers $AOO $VDI $CON, network $NET"; else
    docker rm -f "$VDI" "$CON" "$AOO" >/dev/null 2>&1
    docker network rm "$NET" >/dev/null 2>&1
  fi
  rm -rf "$TMP"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

docker network create "$NET" >/dev/null || exit 2
docker run -d --name "$AOO" --network "$NET" "$AOO_IMAGE" >/dev/null || exit 2

# --- engine containers --------------------------------------------------------
yaml() { # role name
  cat <<EOF
server: $AOO:10998
group: l1-$NAME
username: $2
role: $1
audio:
  sample_rate: 48000
EOF
  if [ "$BACKEND" = pipewire ]; then
    printf '  input_device: engine_in.monitor\n  output_device: engine_out\n'
  fi
}
start_engine() { # container role name
  local c="$1" d="$TMP/$1"
  mkdir -p "$d"; yaml "$2" "$3" > "$d/cfg.yaml"; cp "$HERE/probe.py" "$HERE/pw-entry.sh" "$d/"
  chmod -R a+rX "$d"
  local args=(--headless --config /tmp/l1/cfg.yaml --api-port 0 --dump-peers /tmp/peers.json)
  if [ "$BACKEND" = pulse ]; then
    docker create --name "$c" --network "$NET" ${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"} "$IMAGE" \
      /app/entrypoint.sh "$BIN" "${args[@]}" >/dev/null || return 1
  else
    # The environment is set here (not only in pw-entry.sh) so `docker exec`
    # (parec/pacat in measure) reaches the same pipewire-pulse server.
    docker create --name "$c" --network "$NET" --init --entrypoint /bin/bash \
      -e HOME=/root -e XDG_RUNTIME_DIR=/tmp/xdg ${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"} "$IMAGE" \
      /tmp/l1/pw-entry.sh "$BIN" "${args[@]}" >/dev/null || return 1
  fi
  docker cp "$d" "$c:/tmp/l1" >/dev/null || return 1
  if [ -n "$ENGINE" ]; then docker cp "$ENGINE" "$c:$BIN" >/dev/null || return 1; fi
  docker start "$c" >/dev/null
}
start_engine "$VDI" vdi l1-vdi || { echo "ERROR: could not start $VDI" >&2; exit 2; }
start_engine "$CON" console l1-con || { echo "ERROR: could not start $CON" >&2; exit 2; }

# Ready when each side's dump shows the other connected and receiving.
ready() { docker exec "$1" sh -c 'cat /tmp/peers.json 2>/dev/null' | python3 -c '
import json,sys
try: d=json.load(sys.stdin)
except Exception: sys.exit(1)
sys.exit(0 if any(p.get("connected") and p.get("recvAllow") and p.get("sendAllow") for p in d.get("peers",[])) else 1)'; }
ok=0
for _ in $(seq 60); do
  if ready "$VDI" && ready "$CON"; then ok=1; break; fi
  for c in "$VDI" "$CON"; do
    [ "$(docker inspect -f '{{.State.Running}}' "$c" 2>/dev/null)" = true ] || { echo "ERROR: $c exited:" >&2; docker logs "$c" 2>&1 | tail -20 >&2; exit 2; }
  done
  sleep 1
done
[ "$ok" = 1 ] || { echo "ERROR: the engines did not connect within 60 s" >&2; docker logs "$VDI" 2>&1 | tail -20 >&2; exit 2; }
echo "== $BACKEND: $VDI <-> $CON connected; warming up ${WARMUP}s"
sleep "$WARMUP"
for c in "$VDI" "$CON"; do
  docker exec "$c" python3 /tmp/l1/probe.py burst /tmp/burst.raw "$BURSTS" "$GAP" "$BURST_MS" "$HZ" "$TAIL"
done

# --- one measurement: bursts into $1's engine_in, onsets at $1 and $2 ------------
LISTEN_S=$(python3 -c "print(int($BURSTS*($GAP+$BURST_MS/1000)+$GAP+$TAIL+1+30))")
WANT=$((BURSTS + 1))
measure() { # src dst label
  local s="$TMP/src.jsonl" r="$TMP/dst.jsonl"
  docker exec "$1" python3 /tmp/l1/probe.py listen engine_in.monitor "$LISTEN_S" 0.03 "$WANT" > "$s" & local p1=$!
  docker exec "$2" python3 /tmp/l1/probe.py listen engine_out.monitor "$LISTEN_S" 0.03 "$WANT" > "$r" & local p2=$!
  sleep 1
  docker exec "$1" pacat --raw --format=s16le --rate=48000 --channels=1 --latency-msec=20 -d engine_in /tmp/burst.raw
  wait "$p1" "$p2"
  python3 - "$s" "$r" "$3" "$BURSTS" "$TAIL" <<'PY'
import json, sys, statistics
src = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
dst = [json.loads(l) for l in open(sys.argv[2]) if l.strip()]
label, n, tail = sys.argv[3], int(sys.argv[4]), float(sys.argv[5])
# The k-th short burst at the sender pairs with the k-th at the receiver (works
# for any delay, even many bursts long). The continuous tail comes after them;
# a gap of >= 50 ms splits it into more "bursts", which is reported as a split
# and does not disturb the pairing.
d = [round((b["t"] - a["t"]) * 1000.0, 1) for a, b in zip(src[:n], dst[:n])]
ok = len(src) >= n and len(dst) >= n and (tail <= 0 or len(dst) > n)
# Gaps already present at the sender's input (harness/Pulse hiccups) are not the chain's.
gaps = max(0, sum(b["gaps"] for b in dst) - sum(a["gaps"] for a in src))
tail_ms = dst[n]["len_ms"] if ok and tail > 0 else None
if ok and tail > 0 and (len(dst) > n + 1 or tail_ms < tail * 1000 - 50):
    gaps += 1   # the tail was cut short or split: a dropout of >= 50 ms
med = round(statistics.median(d), 1) if d else None
print(json.dumps({"dir": label, "median_ms": med, "min_ms": min(d) if d else None, "max_ms": max(d) if d else None,
                  "bursts_sent": len(src), "bursts_recv": len(dst), "dropouts": gaps,
                  "tail_ms": tail_ms, "peak": dst[0]["peak"] if dst else None, "delays_ms": d, "complete": ok}))
PY
}

pulse_info() { # container: latency fields of the engine's ALSA streams
  docker exec "$1" sh -c 'pactl list sink-inputs; pactl list source-outputs' 2>/dev/null \
    | grep -E '^(Sink Input|Source Output)|application.name|(Buffer|Sink|Source) Latency' \
    | sed 's/^[[:space:]]*/    /'
}

RES="$TMP/results.jsonl"; : > "$RES"
for i in $(seq "$RUNS"); do
  for dir in "vdi->console" "console->vdi"; do
    if [ "$dir" = "vdi->console" ]; then line="$(measure "$VDI" "$CON" "$dir")"; else line="$(measure "$CON" "$VDI" "$dir")"; fi
    echo "$line" >> "$RES"
    echo "run $i $line"
  done
done
echo "== Pulse stream latencies (engine streams), VDI then Console:"
pulse_info "$VDI"; pulse_info "$CON"

python3 - "$RES" "$TARGET" "$BACKEND" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
target, backend = float(sys.argv[2]), sys.argv[3]
bad = False
for d in ("vdi->console", "console->vdi"):
    r = [x for x in rows if x["dir"] == d]
    meds = [x["median_ms"] for x in r]
    drop = sum(x["dropouts"] for x in r)
    inc = sum(not x["complete"] for x in r)
    print(f"SUMMARY {backend} {d}: medians_ms={meds} dropouts={drop} incomplete_runs={inc}")
    if inc or any(m is None or m > target for m in meds):
        bad = True
print(("PASS" if not bad else "FAIL") + f": engine<->engine latency ({backend}), target < {target:g} ms")
sys.exit(1 if bad else 0)
PY
