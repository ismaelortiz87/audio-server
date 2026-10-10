#!/usr/bin/env bash
# P2.11 -- runs INSIDE the crosspoint-pw-test container (see tests/linux/Dockerfile
# and tests/linux/pipewire-pin.sh). Starts a headless PipeWire stack, creates two
# null sinks and proves that the engine's input/output are pinned to the nodes
# named in the YAML. Prints "CHECK n PASS|FAIL ..." lines and a final PASS/FAIL.
set -u

FAILS=0
pass() { echo "CHECK PASS: $*"; }
fail() { echo "CHECK FAIL: $*"; FAILS=$((FAILS + 1)); }
step() { echo; echo "=== $* ==="; }

export HOME=/root
export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
export DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork --print-address)"

APP="$(find /opt/crosspoint -maxdepth 1 -type f -perm -u+x | head -1)"
[ -x "$APP" ] || { echo "no app binary under /opt/crosspoint"; exit 2; }
echo "app: $APP"

W=/tmp/work; mkdir -p "$W"

step "start PipeWire + WirePlumber + pipewire-pulse"
pipewire > "$W/pipewire.log" 2>&1 &
for _ in $(seq 20); do pw-cli info 0 >/dev/null 2>&1 && break; sleep 0.5; done
wireplumber > "$W/wireplumber.log" 2>&1 &
pipewire-pulse > "$W/pipewire-pulse.log" 2>&1 &
for _ in $(seq 20); do pactl info >/dev/null 2>&1 && break; sleep 0.5; done
pactl load-module module-null-sink sink_name=loopback_sink sink_properties="device.description=Loopback-Sink node.driver=true" >/dev/null
pactl load-module module-null-sink sink_name=crosspoint_mic sink_properties="device.description=Crosspoint-Mic node.driver=true" >/dev/null
sleep 1
echo "sources:"; pactl list short sources
echo "sinks:";   pactl list short sinks

# --- YAML ------------------------------------------------------------------------
cat > "$W/good.yaml" <<'EOF'
server: 127.0.0.1:1
group: pwtest
role: vdi
username: vdi-pw
audio:
  input_device: loopback_sink.monitor
  output_device: crosspoint_mic
EOF
# A setup file with the engine's "dry" level at 1.0: the input is passed straight to the
# output, so a tone that enters at loopback_sink.monitor leaves at crosspoint_mic. (The
# engine has no peer to play anything else; dry defaults to 0.)
cat > "$W/dry.sonobus" <<'EOF2'
<?xml version="1.0" encoding="UTF-8"?>

<PROPERTIES>
  <VALUE name="filterStateXML" val="&lt;SonoBusAoO&gt;&lt;PARAM id=&quot;dry&quot; value=&quot;1.0&quot;/&gt;&lt;/SonoBusAoO&gt;"/>
</PROPERTIES>
EOF2
sed 's/input_device: .*/input_device: no_such_node/' "$W/good.yaml" > "$W/bad.yaml"

# --- check 3 (run first, it needs no engine): wrong node name ---------------------
# P2.5: a missing/unknown audio node is no longer fatal in headless mode; the agent
# stays up, reports it and lists the valid nodes (it keeps retrying). Config errors
# (bad YAML) still exit 1; tests/linux/deb-systemd.sh covers that.
step "wrong node name -> agent stays up, node missing reported, valid nodes listed"
"$APP" --headless --config "$W/bad.yaml" > "$W/bad.out" 2>&1 &
BADPID=$!
sleep 6
if kill -0 $BADPID 2>/dev/null; then alive=1; else alive=0; fi
kill $BADPID 2>/dev/null; wait $BADPID 2>/dev/null
cat "$W/bad.out"
if [ "$alive" -eq 1 ] && grep -q "no_such_node" "$W/bad.out" \
   && grep -q "loopback_sink.monitor" "$W/bad.out" && grep -q "crosspoint_mic" "$W/bad.out"; then
  pass "wrong node name: still running, valid input and output nodes listed"
else
  fail "wrong node name: alive=$alive (want 1) / listing missing"
fi

# --- start the engine --------------------------------------------------------------
step "start the engine pinned to loopback_sink.monitor / crosspoint_mic"
export SONOBUS_AGENT_SILENT_SECS=3
"$APP" --headless -l "$W/dry.sonobus" --config "$W/good.yaml" --dump-peers "$W/peers.json" > "$W/engine.log" 2>&1 &
ENGINE=$!
for _ in $(seq 40); do grep -q "Config: audio input=" "$W/engine.log" 2>/dev/null && break; kill -0 $ENGINE 2>/dev/null || break; sleep 0.5; done
cat "$W/engine.log"
if kill -0 $ENGINE 2>/dev/null && grep -q "Config: audio input='Crosspoint input: loopback_sink'" "$W/engine.log"; then
  pass "engine is running with the crosspoint_in/crosspoint_out PCMs"
else
  fail "engine did not start with the pinned PCMs"
fi
echo "--- generated ALSA config ($HOME/.config/crosspoint/asound.conf)"
cat "$HOME/.config/crosspoint/asound.conf"
sleep 2

# --- check 2a: links ----------------------------------------------------------------
step "PipeWire graph: who is linked to whom"
pw-link -l | tee "$W/links.txt"
if grep -A1 "^loopback_sink:monitor_FL" "$W/links.txt" | grep -q "|-> .*:input_FL"; then
  pass "capture stream is linked to loopback_sink (monitor ports)"
else
  fail "no capture stream linked to loopback_sink:monitor"
fi
if grep -B1 -E "^\s+\|-> crosspoint_mic:playback_FL" "$W/links.txt" | grep -q ":output_FL"; then
  pass "playback stream is linked to crosspoint_mic"
else
  fail "no playback stream linked to crosspoint_mic"
fi
# and nothing leaked to the other ends (wrong pinning would show here)
if grep -q "|-> loopback_sink:playback" "$W/links.txt"; then
  fail "something plays INTO loopback_sink (output not pinned to crosspoint_mic)"
else
  pass "nothing plays into loopback_sink"
fi
pw-dump | jq -r '.[] | select(.type=="PipeWire:Interface:Node") | select(.info.props["media.class"]|tostring|test("^Stream/")) | "stream \(.info.props["node.name"]) class=\(.info.props["media.class"]) target.object=\(.info.props["target.object"])"'

# --- check 2b: audio ----------------------------------------------------------------
# --- check 2b: audio ----------------------------------------------------------------
# selfAgent.input is "silent" when the input has been below -60 dBFS (but not exactly
# 0) for SONOBUS_AGENT_SILENT_SECS, and "ok" otherwise (P1.7). A quiet tone (-70 dBFS)
# therefore gives "silent"; a loud one gives "ok".
input_state() { jq -r '.selfAgent.input' "$W/peers.json" 2>/dev/null; }
wait_state() { # want timeout_s
  local s=""
  for _ in $(seq $(( $2 * 2 ))); do s="$(input_state)"; [ "$s" = "$1" ] && break; sleep 0.5; done
  echo "$s"
}
mktone() { # file amplitude seconds
python3 - "$1" "$2" "$3" <<'PY'
import math, struct, sys, wave
f, amp, secs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
w = wave.open(f, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
w.writeframes(b"".join(struct.pack("<hh", *(2 * [int(amp * math.sin(2 * math.pi * 440 * i / 48000))])) for i in range(48000 * secs)))
w.close()
PY
}
mktone "$W/quiet.wav" 10 14      # about -70 dBFS
mktone "$W/loud.wav" 12000 8

step "levels: quiet tone on loopback_sink -> selfAgent.input 'silent'; loud tone -> 'ok'"
pw-play --target loopback_sink "$W/quiet.wav" > "$W/pw-play-quiet.log" 2>&1 &
QPLAY=$!
state="$(wait_state silent 12)"
echo "selfAgent.input with a -70 dBFS tone on loopback_sink for >3 s: $state"
[ "$state" = "silent" ] && pass "input 'silent' while only a -70 dBFS signal arrives" || fail "expected silent, got '$state'"
kill $QPLAY 2>/dev/null; wait $QPLAY 2>/dev/null

# What reaches crosspoint_mic (the monitor of the sink the engine plays into):
pw-record --target crosspoint_mic -P stream.capture.sink=true --rate 48000 --channels 2 --format s16 "$W/mic.wav" > "$W/pw-record.log" 2>&1 &
REC=$!
sleep 1
pw-play --target loopback_sink "$W/loud.wav" > "$W/pw-play-loud.log" 2>&1 &
PLAY=$!
state="$(wait_state ok 8)"
echo "selfAgent.input with a loud tone on loopback_sink: $state"
[ "$state" = "ok" ] && pass "selfAgent.input silent -> ok when a tone is played into loopback_sink" || fail "expected ok, got '$state'"
wait $PLAY 2>/dev/null
sleep 1
kill -INT $REC 2>/dev/null; wait $REC 2>/dev/null

peak="$(python3 - "$W/mic.wav" <<'PY'
import array, sys
# Raw scan: tolerate a header whose sizes were never finalized (recorder killed).
try:
    d = open(sys.argv[1], "rb").read()
    i = d.find(b"data")
    a = array.array("h"); a.frombytes(d[i + 8:len(d) - ((len(d) - i - 8) % 2)])
    print(max(abs(x) for x in a) if len(a) else 0)
except Exception as e:
    print(0)
PY
)"
echo "peak (s16, max 32767) recorded on crosspoint_mic.monitor: $peak"
# dry=1 passes the engine's input to its output, so the tone that entered at
# loopback_sink.monitor leaves at crosspoint_mic: the chain
# loopback_sink -> engine -> crosspoint_mic proves both pins carry audio.
if [ "${peak:-0}" -gt 500 ]; then
  pass "audio put into loopback_sink came out of the engine at crosspoint_mic (peak $peak)"
else
  fail "no audio on crosspoint_mic.monitor (peak $peak)"
fi

kill $ENGINE 2>/dev/null; wait $ENGINE 2>/dev/null

echo
if [ "$FAILS" -eq 0 ]; then echo "PASS: P2.11 PipeWire pinning"; exit 0; else echo "FAIL: $FAILS check(s) failed"; exit 1; fi
