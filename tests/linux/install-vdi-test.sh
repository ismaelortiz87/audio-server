#!/usr/bin/env bash
# Runs packaging/debian/install-vdi.sh the way a VDI owner would, in a
# debian:trixie container with systemd as PID 1:
#   - production device layout (docs/VDI-VIRTUAL-AUDIO-SETUP.md: Crosspoint-Output,
#     crosspoint_input_null, Crosspoint-Input), user session up, linger OFF;
#   - leftovers of the earlier hand-made install (a user unit in
#     ~/.config/systemd/user that shadows the packaged one, ~/.local/bin/crosspoint)
#     and a stock SonoBus autostart entry;
#   - run as the user (sudo inside), then re-run (must be idempotent), then reboot.
#   tests/linux/install-vdi-test.sh [path/to/crosspoint_*_<host-arch>.deb]
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCH="$(uname -m)"; case "$ARCH" in x86_64) ARCH=amd64;; aarch64|arm64) ARCH=arm64;; esac
DEB="${1:-$(ls -t "$ROOT"/build/deb/crosspoint_*_"$ARCH".deb 2>/dev/null | head -1)}"
[ -f "$DEB" ] || { echo "FAIL: no $ARCH .deb (build one with scripts/build-deb.sh)"; exit 1; }
ID="$$-$(date +%s)"; IMAGE="crosspoint-install-vdi-$ID"; C="$IMAGE"
CTX="$(mktemp -d "${TMPDIR:-/tmp}/xp-install-vdi.XXXXXX")"; FAILS=0
pass() { echo "CHECK PASS: $*"; }; fail() { echo "CHECK FAIL: $*"; FAILS=$((FAILS + 1)); }
step() { echo; echo "=== $* ==="; }
cleanup() { [ "${KEEP:-0}" = 1 ] && { echo "KEEP=1: $C left running"; return; }; docker rm -f "$C" >/dev/null 2>&1; docker rmi "$IMAGE" >/dev/null 2>&1; rm -rf "$CTX"; }
trap cleanup EXIT
echo "deb: $DEB"
cp "$DEB" "$CTX/"; cp "$ROOT/packaging/debian/install-vdi.sh" "$CTX/"; cp -R "$ROOT/aooserver" "$CTX/aooserver"
DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-0}" docker build -q -f "$ROOT/tests/linux/Dockerfile.install-vdi" -t "$IMAGE" "$CTX" >/dev/null || { echo "FAIL: image build"; exit 1; }

X() { docker exec "$C" "$@"; }
U() { docker exec "$C" runuser -u vdi -- env XDG_RUNTIME_DIR=/run/user/1000 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus HOME=/home/vdi "$@"; }
waitfor() { local d="$1" t="$2"; shift 2; for _ in $(seq $((t * 2))); do "$@" >/dev/null 2>&1 && return 0; sleep 0.5; done; echo "(timeout: $d)"; return 1; }
boot_up() { waitfor "systemd" 90 sh -c "docker exec $C systemctl is-system-running 2>/dev/null | grep -Eq 'running|degraded'"; }

step "boot (systemd PID 1), start a desktop-like user session with linger OFF"
docker run -d --name "$C" --privileged --cgroupns=host -v /sys/fs/cgroup:/sys/fs/cgroup:rw \
  --tmpfs /run --tmpfs /run/lock --tmpfs /tmp "$IMAGE" /lib/systemd/systemd >/dev/null || { echo "FAIL: systemd container"; exit 1; }
boot_up || { echo "FAIL: systemd didn't come up"; exit 1; }
X systemctl start user@1000.service
waitfor "user bus" 60 X test -S /run/user/1000/bus || fail "no user bus"
[ "$(X loginctl show-user vdi -p Linger --value)" = no ] && pass "linger is off before the script" || fail "linger already on"
# the null sinks need a clock driver in a container (no hardware); production
# sessions have one. Then the production layout, verbatim from the doc.
X bash -c "mkdir -p /home/vdi/.config/pipewire/pipewire.conf.d && cat > /home/vdi/.config/pipewire/pipewire.conf.d/05-test-driver.conf <<'EOF'
context.objects = [ { factory = spa-node-factory args = { factory.name = support.node.driver node.name = Test-Driver node.group = pipewire.dummy priority.driver = 20000 } } ]
EOF
chown -R vdi:vdi /home/vdi/.config"
U systemctl --user restart pipewire.service pipewire-pulse.service wireplumber.service
waitfor "pipewire-pulse" 30 U pactl info || fail "pipewire-pulse not up"
U pactl load-module module-null-sink sink_name=Crosspoint-Output sink_properties=device.description=Crosspoint_Output >/dev/null
U pactl load-module module-null-sink sink_name=crosspoint_input_null sink_properties=device.description=Crosspoint_Input >/dev/null
sleep 0.5
U pactl load-module module-remap-source source_name=Crosspoint-Input source_properties=device.description=Crosspoint_Input master=crosspoint_input_null.monitor master_channel_map=front-left,front-right channels=2 >/dev/null
U pactl list short sources | grep -q '^.*Crosspoint-Output.monitor' && pass "production device layout present" || fail "device layout"

step "leftovers: shadowing unit, ExecStart drop-in, ~/.local/bin/crosspoint, stock SonoBus (Flatpak-style process + autostart)"
X bash -c "mkdir -p /home/vdi/.config/systemd/user /home/vdi/.local/bin /home/vdi/.config/autostart
printf '[Service]\nExecStart=/bin/false\n[Install]\nWantedBy=default.target\n' > /home/vdi/.config/systemd/user/crosspoint-agent.service
printf '#!/bin/sh\necho old\n' > /home/vdi/.local/bin/crosspoint; chmod +x /home/vdi/.local/bin/crosspoint
printf '[Desktop Entry]\nName=SonoBus\nExec=flatpak run net.sonobus.SonoBus\n' > /home/vdi/.config/autostart/net.sonobus.SonoBus.desktop
mkdir -p /home/vdi/.config/systemd/user/crosspoint-agent.service.d
printf '[Service]\nExecStart=\nExecStart=/bin/false\n' > /home/vdi/.config/systemd/user/crosspoint-agent.service.d/zz-test-build.conf
printf 'secret-group-pw\n' > /home/vdi/pw; chmod 600 /home/vdi/pw
chown -R vdi:vdi /home/vdi"

# The Flatpak's process is a bare "sonobus" (no path, no app id in its args).
docker exec -d "$C" runuser -u vdi -- bash -c 'mkdir -p ~/fake && cp /bin/sleep ~/fake/sonobus; cd ~/fake; exec -a sonobus ./sonobus 600'
sleep 2; X pgrep -u vdi -x sonobus >/dev/null && pass "fake Flatpak-style 'sonobus' process running" || fail "could not start the fake stock process"

step "run install-vdi.sh (as the user)"
U bash -lc "cd ~/dist && ./install-vdi.sh --server 127.0.0.1:10998 --group installtest --username vdi-test --password-file ~/pw --stop-stock-sonobus --yes" > "$CTX/run1.log" 2>&1; rc=$?
cat "$CTX/run1.log" | tail -25
[ $rc -eq 0 ] && pass "script exit 0" || fail "script exit $rc"
grep -q "secret-group-pw" "$CTX/run1.log" && fail "password echoed in output" || pass "password not printed"
[ "$(X loginctl show-user vdi -p Linger --value)" = yes ] && pass "linger enabled" || fail "linger not enabled"
FP="$(U systemctl --user show crosspoint-agent -p FragmentPath --value)"
[ "$FP" = /usr/lib/systemd/user/crosspoint-agent.service ] && pass "packaged unit in use (shadowing unit retired)" || fail "unit in use: $FP"
X test ! -e /home/vdi/.local/bin/crosspoint && pass "old ~/.local/bin/crosspoint moved aside" || fail "old binary still in PATH"
X test -e /home/vdi/.config/autostart/net.sonobus.SonoBus.desktop.disabled-by-crosspoint && pass "stock SonoBus autostart disabled" || fail "autostart not disabled"
X pgrep -u vdi -x sonobus >/dev/null && fail "stock 'sonobus' process still running" || pass "stock 'sonobus' process stopped"
X test ! -e /home/vdi/.config/systemd/user/crosspoint-agent.service.d && pass "ExecStart drop-in moved aside" || fail "drop-in still active"
[ "$(U systemctl --user show crosspoint-agent -p ExecStart --value | grep -o '/usr/bin/crosspoint' | head -1)" = /usr/bin/crosspoint ] && pass "agent runs /usr/bin/crosspoint" || fail "agent ExecStart not the packaged binary"
[ "$(X stat -c %a /home/vdi/.config/crosspoint/vdi.yaml)" = 600 ] && pass "config mode 600" || fail "config mode"
X grep -q 'input_device: "Crosspoint-Output.monitor"' /home/vdi/.config/crosspoint/vdi.yaml && X grep -q 'output_device: "crosspoint_input_null"' /home/vdi/.config/crosspoint/vdi.yaml \
  && pass "devices auto-detected: Crosspoint-Output.monitor -> crosspoint_input_null" || fail "device detection"
grep -q "connection : connected" "$CTX/run1.log" && pass "script reports the agent connected" || fail "not reported connected"
grep -Eq "input      : .*Crosspoint-Output.*\[ok\]" "$CTX/run1.log" && pass "input ok (pinned to Crosspoint-Output)" || fail "input not ok"
grep -Eq "output     : .*crosspoint_input_null.*\[ok\]" "$CTX/run1.log" && pass "output ok (pinned to crosspoint_input_null)" || fail "output not ok"
U pw-link -l 2>/dev/null | grep -A1 "Crosspoint-Output:monitor_FL" | grep -qi "crosspoint\|alsa_capture" && pass "capture linked to Crosspoint-Output.monitor" || echo "(note: link listing not conclusive)"

step "re-run (must be idempotent, keep the config)"
SUM1="$(X sha256sum /home/vdi/.config/crosspoint/vdi.yaml)"
U bash -lc "cd ~/dist && ./install-vdi.sh --yes" > "$CTX/run2.log" 2>&1; rc=$?
[ $rc -eq 0 ] && pass "second run exit 0" || { fail "second run exit $rc"; tail -15 "$CTX/run2.log"; }
[ "$SUM1" = "$(X sha256sum /home/vdi/.config/crosspoint/vdi.yaml)" ] && pass "config kept unchanged" || fail "config changed"
grep -q "connection : connected" "$CTX/run2.log" && pass "still connected after re-run" || fail "not connected after re-run"

step "reboot: agent comes back without a login"
docker stop -t 60 "$C" >/dev/null && docker start "$C" >/dev/null; boot_up || fail "no systemd after reboot"
waitfor "agent after reboot" 90 sh -c "docker exec $C runuser -u vdi -- env XDG_RUNTIME_DIR=/run/user/1000 curl -fs http://127.0.0.1:7071/api/v1/health" \
  && pass "agent API up after reboot, no login" || fail "agent not up after reboot"

echo; [ "$FAILS" -eq 0 ] && { echo "PASS: install-vdi.sh"; exit 0; } || { echo "FAIL: $FAILS check(s)"; exit 1; }
