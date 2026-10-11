#!/usr/bin/env bash
# Crosspoint VDI agent installer (Debian 13 trixie).
#
# Run it ON the VDI, as the desktop user who owns the PipeWire session (not as
# root; it uses sudo only for apt and loginctl):
#
#   ./install-vdi.sh                       # uses the crosspoint_*_<arch>.deb next to this script
#   ./install-vdi.sh path/to/crosspoint_X_amd64.deb
#
# Options:
#   --server HOST:PORT     connection server      (default: keep current / aoo.sonobus.net:10998)
#   --group NAME           group to join          (asked if not given)
#   --username NAME        name shown on the Console (default: this host's name)
#   --password-file FILE   read the group password from FILE (otherwise asked, hidden)
#   --input NODE           PipeWire node the agent SENDS from   (default: auto-detect)
#   --output NODE          PipeWire node the agent PLAYS into   (default: auto-detect)
#   --reconfigure          rewrite ~/.config/crosspoint/vdi.yaml even if it exists
#   --stop-stock-sonobus   stop a running stock SonoBus and disable its autostart
#   --yes                  don't ask for confirmation
#
# What it does: installs the .deb (apt resolves dependencies); removes an older
# user-local agent install that would shadow the packaged one; writes the config
# (chmod 600); enables linger so the agent starts at boot without a login;
# enables and starts the systemd user service; checks it's connected.
set -euo pipefail

say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33mWARNING:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

DEB="" SERVER="" GROUP="" USERNAME_="" PWFILE="" IN_NODE="" OUT_NODE=""
RECONF=0 STOPSTOCK=0 YES=0
while [ $# -gt 0 ]; do
  case "$1" in
    --server) SERVER="${2:?}"; shift 2 ;;
    --group) GROUP="${2:?}"; shift 2 ;;
    --username) USERNAME_="${2:?}"; shift 2 ;;
    --password-file) PWFILE="${2:?}"; shift 2 ;;
    --input) IN_NODE="${2:?}"; shift 2 ;;
    --output) OUT_NODE="${2:?}"; shift 2 ;;
    --reconfigure) RECONF=1; shift ;;
    --stop-stock-sonobus) STOPSTOCK=1; shift ;;
    --yes|-y) YES=1; shift ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    -*) die "unknown option $1 (see --help)" ;;
    *) DEB="$1"; shift ;;
  esac
done

confirm() { [ "$YES" = 1 ] && return 0; read -r -p "$1 [Y/n] " a; [ -z "$a" ] || [[ "$a" =~ ^[Yy] ]]; }

# --- preflight ----------------------------------------------------------------
[ "$(id -u)" -ne 0 ] || die "run this as the VDI's desktop user, not root (it calls sudo itself)"
. /etc/os-release 2>/dev/null || true
[ "${ID:-}" = debian ] || warn "this is ${PRETTY_NAME:-an unknown OS}; the package targets Debian 13 (trixie)"
ARCH="$(dpkg --print-architecture)"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$DEB" ]; then
  DEB="$(ls -1t "$HERE"/crosspoint_*_"$ARCH".deb 2>/dev/null | head -1 || true)"
  [ -n "$DEB" ] || die "no crosspoint_*_${ARCH}.deb next to this script; pass its path"
fi
[ -f "$DEB" ] || die "package not found: $DEB"
case "$DEB" in *_"$ARCH".deb) ;; *) die "$DEB is not for this machine's architecture ($ARCH)";; esac
command -v pactl >/dev/null || die "pactl not found: install pulseaudio-utils (see docs/VDI-VIRTUAL-AUDIO-SETUP.md)"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
pactl info >/dev/null 2>&1 || die "can't talk to PipeWire/PulseAudio as $USER (is the desktop session's pipewire-pulse running?)"

say "Installing $(basename "$DEB") for user $USER on $(hostname)"

# --- stock SonoBus check ---------------------------------------------------------
# Matches a native install (/usr/bin/sonobus) and the Flatpak, whose process is a
# bare `sonobus` under bwrap (the args hold no path or app id).
STOCK_RE='net\.sonobus\.SonoBus|(^|/)sonobus( |$)'
stock_running=0
pgrep -u "$USER" -fi "$STOCK_RE" >/dev/null 2>&1 && stock_running=1
autostarts="$(grep -lis 'sonobus' "$HOME"/.config/autostart/*.desktop 2>/dev/null || true)"
if [ "$stock_running" = 1 ] || [ -n "$autostarts" ]; then
  warn "stock SonoBus is running and/or set to autostart on this VDI."
  echo "         Running it next to the Crosspoint agent on the same devices doubles the audio."
  if [ "$STOPSTOCK" = 1 ]; then
    command -v flatpak >/dev/null 2>&1 && flatpak kill net.sonobus.SonoBus 2>/dev/null || true
    pkill -u "$USER" -fi "$STOCK_RE" 2>/dev/null || true
    for _ in 1 2 3 4 5; do pgrep -u "$USER" -fi "$STOCK_RE" >/dev/null 2>&1 || break; sleep 1; done
    if pgrep -u "$USER" -fi "$STOCK_RE" >/dev/null 2>&1; then
      warn "stock SonoBus is still running; close it yourself."
    elif [ "$stock_running" = 1 ]; then
      echo "         stopped stock SonoBus"
    fi
    for f in $autostarts; do mv -f "$f" "$f.disabled-by-crosspoint"; echo "         disabled autostart: $f"; done
  else
    echo "         Re-run with --stop-stock-sonobus to stop it and disable its autostart, or do it yourself."
  fi
fi

# --- package --------------------------------------------------------------------
say "apt update + install (sudo)"
# Refresh the package lists first: with empty/stale lists apt can't resolve the
# dependencies (libfreetype6, pipewire-audio, ...) and refuses the package.
sudo apt-get update -qq
sudo apt-get install -y "$DEB"

# An earlier hand-made install put a user unit in ~/.config/systemd/user, which
# SHADOWS the packaged /usr/lib/systemd/user unit, and a binary in ~/.local/bin.
STAMP="$(date +%Y%m%d-%H%M%S)"
U="$HOME/.config/systemd/user/crosspoint-agent.service"
if [ -e "$U" ] || [ -L "$U" ]; then
  say "Retiring the user-local unit that shadows the packaged one: $U"
  systemctl --user stop crosspoint-agent 2>/dev/null || true
  systemctl --user disable crosspoint-agent 2>/dev/null || true
  mv -f "$U" "$U.bak-$STAMP"
fi
rm -f "$HOME/.config/systemd/user/default.target.wants/crosspoint-agent.service"
# Drop-ins can override ExecStart (e.g. a test build) and would outlive the upgrade.
DD="$HOME/.config/systemd/user/crosspoint-agent.service.d"
if [ -d "$DD" ] && [ -n "$(ls -A "$DD" 2>/dev/null)" ]; then
  mv -f "$DD" "$DD.bak-$STAMP"
  echo "    moved user drop-ins aside: $DD.bak-$STAMP"
fi
[ -e "$HOME/.local/bin/crosspoint" ] && { mv -f "$HOME/.local/bin/crosspoint" "$HOME/.local/bin/crosspoint.bak-$STAMP"; echo "    moved old ~/.local/bin/crosspoint aside"; }
systemctl --user daemon-reload

# --- devices ----------------------------------------------------------------------
sinks="$(pactl list short sinks | awk '{print $2}')"
sources="$(pactl list short sources | awk '{print $2}')"
has_source() { grep -qx -- "$1" <<<"$sources"; }
has_sink()   { grep -qx -- "$1" <<<"$sinks"; }
if [ -z "$IN_NODE" ]; then
  for c in Crosspoint-Output.monitor sb_system_out.monitor; do has_source "$c" && { IN_NODE="$c"; break; }; done
fi
if [ -z "$OUT_NODE" ]; then
  for c in crosspoint_input_null sb_mic_in; do has_sink "$c" && { OUT_NODE="$c"; break; }; done
fi
if [ -z "$IN_NODE" ] || [ -z "$OUT_NODE" ]; then
  echo; echo "Couldn't find the standard Crosspoint devices (see docs/VDI-VIRTUAL-AUDIO-SETUP.md)."
  echo "Sources (what the agent can SEND from):"; sed 's/^/    /' <<<"$sources"
  echo "Sinks (what the agent can PLAY into, i.e. what feeds the VDI's virtual mic):"; sed 's/^/    /' <<<"$sinks"
  [ -n "$IN_NODE" ]  || read -r -p "Input node (system audio, usually <sink>.monitor): " IN_NODE
  [ -n "$OUT_NODE" ] || read -r -p "Output node (sink behind the virtual mic): " OUT_NODE
fi
has_source "$IN_NODE" || warn "input node '$IN_NODE' doesn't exist right now; the agent will report it missing and keep retrying"
has_sink "$OUT_NODE"  || warn "output node '$OUT_NODE' doesn't exist right now; the agent will report it missing and keep retrying"
say "Devices: sends from '$IN_NODE', plays into '$OUT_NODE'"

# --- config -----------------------------------------------------------------------
CFG_DIR="$HOME/.config/crosspoint"; CFG="$CFG_DIR/vdi.yaml"
yq() { local s="${1//\\/\\\\}"; s="${s//\"/\\\"}"; printf '"%s"' "$s"; }   # YAML double-quoted string
if [ -f "$CFG" ] && [ "$RECONF" = 0 ] && [ -z "$GROUP$SERVER$USERNAME_$PWFILE" ]; then
  say "Keeping the existing $CFG (use --reconfigure to rewrite it)"
  grep -qE '^\s*input_device:' "$CFG" || warn "$CFG has no audio.input_device: the agent will open the default device"
else
  cur() { [ -f "$CFG" ] && sed -n "s/^$1:[[:space:]]*\"\{0,1\}\([^\"#]*\)\"\{0,1\}.*/\1/p" "$CFG" | head -1 | sed 's/[[:space:]]*$//'; }
  SERVER="${SERVER:-$(cur server)}"; SERVER="${SERVER:-aoo.sonobus.net:10998}"
  GROUP="${GROUP:-$(cur group)}"
  [ -n "$GROUP" ] || read -r -p "Group to join: " GROUP
  [ -n "$GROUP" ] || die "a group is required"
  USERNAME_="${USERNAME_:-$(cur username)}"; USERNAME_="${USERNAME_:-$(hostname -s)}"
  if [ -n "$PWFILE" ]; then PW="$(head -n1 "$PWFILE")"
  else read -r -s -p "Password for group '$GROUP' (empty for none): " PW; echo; fi
  mkdir -p "$CFG_DIR"; chmod 700 "$CFG_DIR"
  [ -f "$CFG" ] && cp -p "$CFG" "$CFG.bak-$STAMP"
  umask 077
  {
    echo "# Crosspoint VDI agent config, written by install-vdi.sh on $(date -Iseconds)."
    echo "# Edit, then: systemctl --user restart crosspoint-agent"
    echo "role: vdi"
    echo "server: $(yq "$SERVER")"
    echo "group: $(yq "$GROUP")"
    [ -n "$PW" ] && echo "password: $(yq "$PW")"
    echo "username: $(yq "$USERNAME_")"
    echo "audio:"
    echo "  input_device: $(yq "$IN_NODE")     # what this VDI sends (system audio)"
    echo "  output_device: $(yq "$OUT_NODE")   # where the Console's voice is played (the virtual mic's sink)"
  } > "$CFG"
  chmod 600 "$CFG"
  unset PW
  say "Wrote $CFG (mode 600)"
fi

# --- start at boot ------------------------------------------------------------------
if [ "$(loginctl show-user "$USER" -p Linger --value 2>/dev/null)" != yes ]; then
  say "Enabling linger so the agent starts at boot without a login (sudo)"
  sudo loginctl enable-linger "$USER"
fi
say "Enabling and (re)starting the crosspoint-agent user service"
systemctl --user enable crosspoint-agent >/dev/null
systemctl --user restart crosspoint-agent

# --- check -------------------------------------------------------------------------
ok=0
for _ in $(seq 1 30); do curl -fs http://127.0.0.1:7071/api/v1/health >/dev/null 2>&1 && { ok=1; break; }; sleep 1; done
echo
if [ "$ok" = 1 ]; then
  say "Agent is up: $(curl -fs http://127.0.0.1:7071/api/v1/health)"
else
  warn "the agent's API didn't answer on :7071 within 30 s"
fi
if command -v python3 >/dev/null; then
  # Read the agent's state (control API) until it is connected or reports an error.
  python3 - <<'PY' || true
import base64, json, os, socket, struct, time
def state():
    s = socket.create_connection(("127.0.0.1", 7071), timeout=5)
    s.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % base64.b64encode(os.urandom(16)).decode()).encode())
    b = b""
    while b"\r\n\r\n" not in b: b += s.recv(4096)
    b = b.split(b"\r\n\r\n", 1)[1]
    while True:
        while len(b) < 2: b += s.recv(65536)
        n, o = b[1] & 0x7F, 2
        if n == 126: n, o = struct.unpack(">H", b[2:4])[0], 4
        elif n == 127: n, o = struct.unpack(">Q", b[2:10])[0], 10
        while len(b) < o + n: b += s.recv(65536)
        m = json.loads(b[o:o + n]); b = b[o + n:]
        if m.get("t") == "state": s.close(); return m["state"]
st = None
for _ in range(15):
    try:
        st = state()
        if st.get("connection", {}).get("state") in ("connected", "error"): break
    except Exception: pass
    time.sleep(2)
if st and "connection" in st:
    c = st["connection"]
    print("    connection : %s  (server %s, group %s)%s" % (c["state"], c.get("server"), c.get("group"),
          ("  ERROR: " + c["error"].get("message", "")) if c.get("error") else ""))
    print("    input      : %s  [%s]" % (st["input"]["node"], st["input"]["status"]))
    print("    output     : %s  [%s]" % (st["output"]["node"], st["output"]["status"]))
    print("    consoles   : %s" % (", ".join(x["name"] for x in st.get("consoles", [])) or "none listening yet"))
else:
    print("    (couldn't read the agent state; check the logs below)")
PY
fi
cat <<EOF

Done.
  Agent page : http://localhost:7071/   (or the "Crosspoint agent" menu entry)
  Logs       : journalctl --user -u crosspoint-agent -f
  Config     : $CFG   then  systemctl --user restart crosspoint-agent
  Stop/start : systemctl --user stop|start crosspoint-agent
EOF
