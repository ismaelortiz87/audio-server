#!/usr/bin/env bash
# P2.5 / P2.10 -- builds the .deb (scripts/build-deb.sh), installs it with apt in a
# debian:trixie container that runs systemd as PID 1, and exercises the real
# systemd USER service: enable/start, health API, clean stop, reboot with linger,
# a missing audio node at start (service stays up, reports missing, recovers), a
# broken YAML (fails and restarts, error in the journal), and package removal.
#
#   tests/linux/deb-systemd.sh               build the deb, build the image, run
#   tests/linux/deb-systemd.sh --no-build    reuse build/deb/crosspoint_*.deb
#
# Needs Docker able to run a privileged systemd container (cgroupns=host, writable
# /sys/fs/cgroup). Legacy builder (DOCKER_BUILDKIT=0) by default, as on Docker 20.10/arm64.
# Prints "CHECK PASS|FAIL ..." lines and a final PASS/FAIL (exit 0/1).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_ID="$$-$(date +%s)"
IMAGE="crosspoint-deb-systemd-$RUN_ID"
C="crosspoint-deb-systemd-$RUN_ID"
CTX="$(mktemp -d "${TMPDIR:-/tmp}/crosspoint-deb-ctx.XXXXXX")"
FAILS=0
pass() { echo "CHECK PASS: $*"; }
fail() { echo "CHECK FAIL: $*"; FAILS=$((FAILS + 1)); }
step() { echo; echo "=== $* ==="; }
cleanup() {
  [ "${KEEP:-0}" = 1 ] && { echo "KEEP=1: container $C and image $IMAGE left running"; return; }
  docker rm -f "$C" >/dev/null 2>&1
  docker rmi "$IMAGE" >/dev/null 2>&1
  rm -rf "$CTX"
}
trap cleanup EXIT

if [ "${1:-}" != "--no-build" ]; then
  step "build the .deb"
  "$ROOT/scripts/build-deb.sh" || { echo "FAIL: build-deb.sh"; exit 1; }
fi
DEB="$(ls -t "$ROOT"/build/deb/crosspoint_*.deb 2>/dev/null | head -1)"
[ -f "$DEB" ] || { echo "FAIL: no build/deb/crosspoint_*.deb"; exit 1; }
echo "deb: $DEB"

step "build the systemd test image"
cp "$DEB" "$CTX/crosspoint.deb"
cp -R "$ROOT/aooserver" "$CTX/aooserver"
DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-0}" docker build -f "$ROOT/tests/linux/Dockerfile.systemd" -t "$IMAGE" "$CTX" \
  || { echo "FAIL: image build"; exit 1; }

# --- helpers --------------------------------------------------------------------
VUID=1000
X()  { docker exec "$C" "$@"; }                         # as root in the container
U()  { docker exec "$C" runuser -u vdi -- env XDG_RUNTIME_DIR=/run/user/$VUID \
         DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$VUID/bus "$@"; }   # as the vdi user
waitfor() { # description timeout_s command...
  local desc="$1" t="$2"; shift 2
  for _ in $(seq $((t * 2))); do "$@" >/dev/null 2>&1 && return 0; sleep 0.5; done
  echo "(timeout waiting for: $desc)"; return 1
}
svc_active()   { [ "$(U systemctl --user is-active crosspoint-agent 2>/dev/null)" = active ]; }
health()       { U curl -fsS --max-time 3 http://localhost:7071/api/v1/health; }
health_has()   { health | jq -e "$1" >/dev/null; }
state()        { docker exec -i "$C" python3 - 7071 < "$ROOT/tests/linux/ws-state.py"; }   # control-api 3.3 over WebSocket
state_has()    { state | jq -e "$1" >/dev/null; }
journal()      { U journalctl --user -u crosspoint-agent --no-pager -o cat "$@" 2>&1; }
boot_up() {    # wait for systemd + the user manager (linger) to be up
  waitfor "system manager" 90 sh -c "docker exec $C systemctl is-system-running 2>/dev/null | grep -Eq 'running|degraded'"
}

write_config() { # input_device [extra yaml line]
  X bash -c "cat > /home/vdi/.config/crosspoint/vdi.yaml <<EOF
server: 127.0.0.1:10998
group: debtest
role: vdi
username: vdi-deb
audio:
  input_device: $1
  output_device: crosspoint_mic
${2:-}
EOF
chown vdi:vdi /home/vdi/.config/crosspoint/vdi.yaml"
}

# --- start the systemd container -------------------------------------------------
step "start the container (systemd as PID 1)"
docker run -d --name "$C" --privileged --cgroupns=host -v /sys/fs/cgroup:/sys/fs/cgroup:rw \
  --tmpfs /run --tmpfs /run/lock --tmpfs /tmp "$IMAGE" /lib/systemd/systemd >/dev/null \
  || { echo "FAIL: cannot start a systemd container on this host"; exit 1; }
boot_up || { echo "FAIL: systemd did not come up"; X journalctl -b --no-pager | tail -30; exit 1; }
X systemctl is-system-running
pass "systemd is PID 1 in the container"

step "package: version, contents, files"
X crosspoint --version 2>&1 | head -3
X crosspoint --version >/dev/null 2>&1 && pass "crosspoint --version works" || fail "crosspoint --version"
X dpkg -L crosspoint
X test -f /usr/lib/systemd/user/crosspoint-agent.service \
  && X test -f /usr/share/doc/crosspoint/vdi.example.yaml \
  && X test -f /usr/share/doc/crosspoint/README.Debian \
  && pass "unit, example YAML and README.Debian are installed" || fail "package files missing"
U systemd-analyze --user verify /usr/lib/systemd/user/crosspoint-agent.service 2>&1 | head -5

step "user setup: user vdi, linger, PipeWire sinks (node.driver=true)"
X usermod -aG systemd-journal vdi
X loginctl enable-linger vdi
waitfor "user manager" 60 sh -c "docker exec $C test -S /run/user/$VUID/bus" || fail "user bus did not appear"
X bash -c "mkdir -p /home/vdi/.config/pipewire/pipewire.conf.d /home/vdi/.config/crosspoint
cat > /home/vdi/.config/pipewire/pipewire.conf.d/10-crosspoint-test.conf <<'EOF'
context.objects = [
  { factory = adapter
    args = { factory.name = support.null-audio-sink node.name = loopback_sink
             media.class = Audio/Sink node.driver = true audio.position = [ FL FR ]
             node.description = Loopback-Sink object.linger = true } }
  { factory = adapter
    args = { factory.name = support.null-audio-sink node.name = crosspoint_mic
             media.class = Audio/Sink node.driver = true audio.position = [ FL FR ]
             node.description = Crosspoint-Mic object.linger = true } }
]
EOF
chown -R vdi:vdi /home/vdi/.config"
U systemctl --user restart pipewire.service pipewire-pulse.service wireplumber.service
waitfor "pipewire sinks" 40 sh -c "docker exec $C runuser -u vdi -- env XDG_RUNTIME_DIR=/run/user/$VUID pactl list short sinks | grep -q crosspoint_mic" \
  && pass "PipeWire up as the user, sinks present" || fail "PipeWire sinks missing"
U pactl list short sources; U pactl list short sinks

# --- 1. enable + start ------------------------------------------------------------
step "1. config + systemctl --user enable --now crosspoint-agent"
write_config loopback_sink.monitor
U systemctl --user enable --now crosspoint-agent 2>&1
waitfor "service active" 30 svc_active && pass "service is active" || fail "service not active"
waitfor "health" 30 health_has '.role=="vdi"' && pass "health API on 7071 reports role vdi" || fail "health role vdi"
echo "health:"; health | jq -c .
waitfor "connected" 40 state_has '.connection.state=="connected"' \
  && pass "state: connection.state == connected (local aooserver)" || fail "agent state is not connected"
state | jq -c '{connection, input, output}'
echo "--- journal"; journal | tail -25
journal | grep -qi "connect" && pass "journal shows the connection" || fail "journal has no connection line"

# --- 2. clean stop ------------------------------------------------------------------
step "2. systemctl --user stop exits cleanly within TimeoutStopSec"
T0=$(date +%s)
U systemctl --user stop crosspoint-agent
T1=$(date +%s)
R="$(U systemctl --user show crosspoint-agent -p Result -p ExecMainStatus | tr '\n' ' ')"
echo "stop took $((T1 - T0)) s; Result/ExecMainStatus/ExecMainCode: $R"
[ $((T1 - T0)) -lt 10 ] && echo "$R" | grep -q "Result=success" && echo "$R" | grep -q "ExecMainStatus=0" && pass "stopped in $((T1 - T0)) s, Result=success" || fail "unclean stop ($R)"
journal | tail -6

# --- 3. reboot / linger ----------------------------------------------------------------
step "3. container reboot: service comes up without any login (linger)"
docker stop -t 60 "$C" >/dev/null && docker start "$C" >/dev/null
boot_up || fail "no systemd after reboot"
waitfor "service active after reboot" 90 svc_active && pass "service active after reboot, no login" || fail "service not active after reboot"
U loginctl list-sessions --no-legend 2>&1 | head -3
waitfor "health after reboot" 40 health_has '.role=="vdi"' && pass "health OK after reboot" || fail "health after reboot"

# --- 4. missing node at start -------------------------------------------------------------
step "4. input node missing at start: service stays up, reports missing, recovers"
write_config late_sink.monitor
U systemctl --user restart crosspoint-agent
sleep 12
svc_active && pass "service still active with a missing input node" || fail "service died on a missing node"
NR="$(U systemctl --user show crosspoint-agent -p NRestarts --value)"
[ "$NR" = 0 ] && pass "no restarts (NRestarts=0)" || fail "NRestarts=$NR"
waitfor "input missing" 30 state_has '.input.status=="missing"' \
  && pass "state: input.status == missing (service up)" || fail "state does not report input missing"
state | jq -c '{connection, input, output}'
journal | tail -8
U pactl load-module module-null-sink sink_name=late_sink sink_properties="device.description=Late-Sink node.driver=true" >/dev/null
waitfor "input recovered" 90 state_has '.input.status!="missing" and .output.status=="ok"' \
  && pass "recovered once the node appeared (input no longer missing)" || fail "did not recover"
state | jq -c '{connection, input, output}'
journal | tail -8

# --- 5. broken YAML ---------------------------------------------------------------------------
step "5. broken YAML: fails, restarts every 5 s, error in the journal"
write_config loopback_sink.monitor "bogus_key: 1"
U systemctl --user restart crosspoint-agent 2>&1 | tail -2
sleep 14
NR="$(U systemctl --user show crosspoint-agent -p NRestarts --value)"
ST="$(U systemctl --user is-active crosspoint-agent)"
echo "state=$ST NRestarts=$NR"
[ "${NR:-0}" -ge 2 ] && pass "restarted repeatedly (NRestarts=$NR)" || fail "no restart loop (NRestarts=$NR, state=$ST)"
journal | tail -8
journal | grep -q "Error in --config" && pass "the config error is in the journal" || fail "no config error in the journal"
write_config loopback_sink.monitor
U systemctl --user restart crosspoint-agent
waitfor "service back" 30 svc_active && pass "fixed config: active again" || fail "did not recover from the fixed config"

# --- 6. removal ----------------------------------------------------------------------------------
step "6. apt remove with the service running"
X apt-get remove -y crosspoint 2>&1 | tail -4
sleep 2
! U pgrep -x crosspoint >/dev/null && pass "no crosspoint process left after removal (prerm stopped it)" || fail "crosspoint still running after removal"
X test ! -e /usr/bin/crosspoint && pass "binary removed" || fail "binary still there"
X test -f /home/vdi/.config/crosspoint/vdi.yaml && pass "user config kept" || fail "user config deleted"

echo
if [ "$FAILS" -eq 0 ]; then echo "PASS: tests/linux/deb-systemd.sh"; exit 0; else echo "FAIL: $FAILS check(s) failed"; exit 1; fi
