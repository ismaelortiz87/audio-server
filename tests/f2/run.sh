#!/usr/bin/env bash
# tests/f2/run.sh -- local multi-peer SonoBus test harness.
#
# Starts an AOO connection server on a free port, launches N headless SonoBus
# peers (each with its own isolated CFFIXED_USER_HOME, username and dump file),
# waits for the group to settle, then compares the observed routing state against
# a scenario expectation and exits non-zero on mismatch.
#
# Usage:
#   tests/f2/run.sh --list
#   tests/f2/run.sh --scenario mesh-stock
#   tests/f2/run.sh --scenario matrix-2v2 --app tests/f2/fake-peer.sh
#   tests/f2/run.sh --scenario mesh-stock --count 2 --timeout 20 --verbose
#   tests/f2/run.sh --scenario matrix-2v2 --app tests/f2/fake-peer.sh --fault bad-send-allow
#
# Flags:
#   --scenario NAME     scenario from scenarios.json (default mesh-stock)
#   --app PATH          SonoBus binary (or the fake-peer stub). Default: the
#                       worktree's build/desktop-release standalone binary.
#                       Peers get --role <role> from the scenario; the stub gets
#                       --fake-role instead.
#   --server PATH       aooserver binary. Default: build/aooserver-release/aooserver
#   --server-addr H:P   use an already-running server (e.g. a container) instead
#                       of starting a local one; skips the aooserver build check
#   --count N           only launch the first N peers of the scenario
#   --peers a,b         evaluate only these peers (subset of the scenario)
#   --timeout SEC       settle timeout in seconds (default 30)
#   --interval SEC      settle poll interval in seconds (default 1)
#   --peer-arg ARG      extra arg appended per peer; %NAME%, %ROLE%, %ROUTING%,
#                       %PORT% and %GROUP% are substituted. Repeatable. Defaults
#                       to "--fake-role %ROLE% --fake-routing %EXPECT%" when the
#                       app looks like the stub.
#   --fault NAME        FAKE_PEER_FAULT for every peer (stub only; used to prove
#                       the harness fails loudly)
#   --expect MODE       override the scenario's expectation for this run:
#                       mesh | matrix | all-blocked
#   --strict-fields     also require sendActive/recvActive/receivingAudio
#   --require-connected also assert connected=true. Off by default: the real app
#                       reports `connected` asymmetrically and unstably, because
#                       it tracks AOO invite timing rather than the routing matrix.
#   --run-dir DIR       use this run directory instead of a fresh mktemp dir
#   --keep              keep the run directory and per-peer logs after the run
#   --list              list scenarios and exit
#   --verbose           verbose harness + evaluator output
#   -h | --help         this help
#
# Scenarios with "steps" (P1.5) start every peer with --test-control and, once
# routing has settled, run `evaluate.py steps` (see README "Control steps").
#
# Only processes this script started are ever signalled, and each is verified to
# still be ours (via lsof cwd) before a forced kill. No pkill/killall is used.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCENARIOS_FILE="$SCRIPT_DIR/scenarios.json"
# Peers are launched with their own directory as cwd, so any path handed to them
# (the app and the server) must be absolute before we get there.
INVOCATION_DIR="$(pwd)"

# The default binaries are the worktree's own build outputs. They may not exist
# yet; the build check below reports that clearly.
# P3.9: the app is built as "Crosspoint" (CMake APP_NAME); the target dir keeps the SonoBus name.
DEFAULT_APP="$WORKTREE_ROOT/build/desktop-release/SonoBus_artefacts/Release/Standalone/Crosspoint.app/Contents/MacOS/Crosspoint"
DEFAULT_SERVER="$WORKTREE_ROOT/build/aooserver-release/aooserver"

SCENARIO="mesh-stock"
APP=""
SERVER=""
SERVER_ADDR=""
SERVER_HOST="127.0.0.1"
COUNT=""
PEER_FILTER=""
TIMEOUT=30
INTERVAL=1
FAULT=""
STRICT=0
REQUIRE_CONNECTED=0
EXPECT_OVERRIDE=""
RUN_DIR=""
KEEP=0
VERBOSE=0
LIST=0

# Scenario tables (parallel indexed arrays; bash 3.2 has no associative arrays).
PEER_NAMES=()
PEER_ROLES=()   # the peer's own role, or "default" when --role must be omitted
PEER_ENVS=()    # comma separated KEY=VALUE env vars for this peer
PEER_EXTRAS=()  # the peer's scenario entry as compact JSON (config/setup_role/cli_*, see peerprep.py)
# Extra per-peer arguments.
PEER_ARGS=()
PEER_ARGS_SET=0

usage() { sed -n '2,48p' "$0"; exit "${1:-0}"; }

log()  { printf '>>> %s\n' "$*" >&2; }
vlog() { [ "$VERBOSE" = 1 ] && printf '>>> %s\n' "$*" >&2 || true; }
# Exit codes (documented in README.md): 0 match, 1 mismatch/did-not-settle,
# 2 usage or configuration error. die() is for the latter, so callers can tell
# "the harness is misconfigured" from "the routing expectation failed".
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 2; }
# For runtime failures that are not the caller's mistake (build missing, server
# would not start): still an error, but not a routing mismatch.
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --scenario)   SCENARIO="${2:?}"; shift 2 ;;
        --app)        APP="${2:?}"; shift 2 ;;
        --server)     SERVER="${2:?}"; shift 2 ;;
        --server-addr) SERVER_ADDR="${2:?}"; shift 2 ;;
        --count)      COUNT="${2:?}"; shift 2 ;;
        --peers)      PEER_FILTER="${2:?}"; shift 2 ;;
        --timeout)    TIMEOUT="${2:?}"; shift 2 ;;
        --interval)   INTERVAL="${2:?}"; shift 2 ;;
        --peer-arg)   PEER_ARGS[${#PEER_ARGS[@]}]="${2:?}"; PEER_ARGS_SET=1; shift 2 ;;
        --fault)      FAULT="${2:?}"; shift 2 ;;
        --expect)     EXPECT_OVERRIDE="${2:?}"; shift 2 ;;
        --strict-fields) STRICT=1; shift ;;
        --require-connected) REQUIRE_CONNECTED=1; shift ;;
        --run-dir)    RUN_DIR="${2:?}"; shift 2 ;;
        --keep)       KEEP=1; shift ;;
        --list)       LIST=1; shift ;;
        --verbose)    VERBOSE=1; shift ;;
        -h|--help)    usage 0 ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
done

# ---------------------------------------------------------------------------
# teardown: signal only PIDs we started, and only while they are still ours
# ---------------------------------------------------------------------------
SERVER_PID=""
PEER_PIDS=()
TEARDOWN_DONE=0

# A PID is "ours" if it is alive and its cwd is still inside the run directory.
# This guards against PID reuse on a long run and ensures we can never touch an
# unrelated process such as the user's real /Applications/SonoBus.app.
#
# Note: the cwd must be compared with symlinks resolved. RUN_DIR is already
# canonical (see the mktemp handling below), but on macOS /tmp is a symlink to
# /private/tmp, so a raw lsof result would otherwise never match.
pid_is_ours() {
    local pid="$1" cwd
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    cwd="$(lsof -a -p "$pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p' | head -1)"
    [ -n "$cwd" ] || return 1
    case "$cwd" in
        "$RUN_DIR"|"$RUN_DIR"/*) return 0 ;;
        *) return 1 ;;
    esac
}

# Every PID whose cwd is inside the run directory. This is how orphaned children
# of a peer are found without ever looking outside our own run tree.
run_tree_pids() {
    [ -d "$RUN_DIR" ] || return 0
    lsof -nP +D "$RUN_DIR" 2>/dev/null | awk 'NR > 1 { print $2 }' | sort -u
}

# TERM a tracked PID's whole process group, give it a moment, then KILL if it is
# still ours. The group matters: killing only the wrapper PID can leave the
# app's own children (or a TERM-ignoring child) running as orphans.
stop_pid() {
    local pid="$1" label="$2" i
    [ -n "$pid" ] || return 0
    if ! kill -0 "$pid" 2>/dev/null; then
        vlog "$label (pid $pid) already gone"
        return 0
    fi
    # Negative PID targets the process group (each peer/server is started in its
    # own group via `set -m`), which also reaches children that ignore TERM.
    kill -TERM "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
    i=0
    while [ $i -lt 30 ]; do
        kill -0 "$pid" 2>/dev/null || { vlog "$label (pid $pid) stopped"; return 0; }
        sleep 0.1
        i=$((i + 1))
    done
    vlog "$label (pid $pid) ignored SIGTERM; escalating"
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    fi
}

# Final sweep: kill anything still alive whose cwd is inside the run directory.
# This catches grandchildren that outlived their parent peer or the server, and
# it is scoped strictly to our own run tree, so it can never touch an unrelated
# process (least of all the user's real SonoBus).
sweep_run_tree() {
    local pid sweep_deadline
    [ -d "$RUN_DIR" ] || return 0
    for pid in $(run_tree_pids); do
        kill -KILL "$pid" 2>/dev/null || true
    done
    # Give the kernel a moment, then retry once in case one was mid-spawn.
    sleep 0.2
    for pid in $(run_tree_pids); do
        # Only report after a second look, to avoid noise from exiting PIDs.
        if [ -r "/proc/$pid" ] || kill -0 "$pid" 2>/dev/null; then
            if kill -KILL "$pid" 2>/dev/null; then
                log "swept leftover process (pid $pid)"
            fi
        fi
    done
    return 0
}

teardown() {
    [ "$TEARDOWN_DONE" = 1 ] && return 0
    TEARDOWN_DONE=1
    local i
    # Neutralise set -u's dislike of empty arrays: teardown must always run to
    # completion, even if we failed before starting the server or any peer.
    for i in ${!PEER_PIDS[@]+"${!PEER_PIDS[@]}"}; do
        stop_pid "${PEER_PIDS[$i]}" "peer ${PEER_NAMES[$i]:-peer$i}"
    done
    stop_pid "${SERVER_PID:-}" "aooserver"
    # Anything still alive inside the run tree is an orphan (e.g. a grandchild
    # that outlived a wrapper). Clear it before the directory disappears.
    sweep_run_tree
    if [ -n "$RUN_DIR" ] && [ -d "$RUN_DIR" ]; then
        if [ "$KEEP" = 1 ]; then
            log "kept run directory: $RUN_DIR"
        else
            rm -rf "$RUN_DIR"
        fi
    fi
}
# Trap on every exit path, including errors and Ctrl-C.
trap teardown EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Give every background job its own process group, so teardown can signal a
# whole peer (and its children) with one negative-PID kill, and so a group kill
# can never reach this harness itself. Must be set before anything is spawned.
set -m

# ---------------------------------------------------------------------------
# scenario lookup
# ---------------------------------------------------------------------------
if [ "$LIST" = 1 ]; then
    exec python3 "$SCRIPT_DIR/evaluate.py" list --scenarios "$SCENARIOS_FILE"
fi

[ -f "$SCENARIOS_FILE" ] || die "missing scenarios file: $SCENARIOS_FILE"
command -v python3 >/dev/null || die "python3 is required but not on PATH"

# Read "KIND<TAB>..." records describing the scenario, in scenario order.
# For each peer we emit its own role (selfRole) separately from whether it
# advertises a role, plus a comma-separated list of KEY=VALUE env vars. The
# real app defaults a missing --role to console, which is recorded as "default"
# so the launcher knows not to pass --role at all for that peer.
SCENARIO_LINES="$(python3 - "$SCENARIOS_FILE" "$SCENARIO" <<'PY'
import json, sys
path, name = sys.argv[1], sys.argv[2]
with open(path) as fh:
    data = json.load(fh)
scens = data.get("scenarios", {})
if name not in scens:
    sys.stderr.write("unknown scenario %r; known: %s\n"
                     % (name, ", ".join(sorted(scens)) or "(none)"))
    sys.exit(1)
scen = scens[name]


def env_pairs(*sources):
    """Flatten env objects to KEY=VALUE tokens (newline free)."""
    out = []
    for src in sources:
        if not isinstance(src, dict):
            continue
        for key, value in src.items():
            if isinstance(value, bool):
                value = "1" if value else "0"
            out.append("%s=%s" % (key, value))
    return ",".join(out)


scen_env = scen.get("env")
print("GROUP\t%s" % (scen.get("group") or ""))
print("ROUTING\t%s" % (scen.get("routing") or "mesh"))
print("EXPECT\t%s" % (scen.get("expect") or scen.get("routing") or "mesh"))
print("DESC\t%s" % (scen.get("description") or ""))
# P1.5: scenarios with timed control steps need --test-control on every peer.
print("STEPS\t%d" % len(scen.get("steps") or []))
for entry in scen.get("peers", []):
    role = entry.get("role")
    advertise = entry.get("advertise", True)
    env = env_pairs(scen_env, entry.get("env"))
    # advertise:false is expressed through SONOBUS_NO_ROLE_ADVERT for the app.
    if advertise is False and "SONOBUS_NO_ROLE_ADVERT" not in env:
        env = (env + ",SONOBUS_NO_ROLE_ADVERT=1").lstrip(",")
    # empty fields are written as "-": read with a tab IFS would collapse them
    print("PEER\t%s\t%s\t%s\t%s\t%s"
          % (entry["name"], "default" if role is None else role,
             "1" if advertise else "0", env or "-",
             json.dumps(entry, separators=(",", ":"))))
PY
)" || die "could not read scenario '$SCENARIO'"

GROUP=""
ROUTING=""
EXPECT=""
DESC=""
NSTEPS=0
while IFS=$'\t' read -r kind a b c d e; do
    case "$kind" in
        GROUP)   GROUP="$a" ;;
        ROUTING) ROUTING="$a" ;;
        EXPECT)  EXPECT="$a" ;;
        DESC)    DESC="$a" ;;
        STEPS)   NSTEPS="$a" ;;
        PEER)
            PEER_NAMES[${#PEER_NAMES[@]}]="$a"
            PEER_ROLES[${#PEER_ROLES[@]}]="$b"
            [ "$d" = "-" ] && d=""
            PEER_ENVS[${#PEER_ENVS[@]}]="$d"
            PEER_EXTRAS[${#PEER_EXTRAS[@]}]="$e"
            ;;
    esac
done <<EOF
$SCENARIO_LINES
EOF

[ "${#PEER_NAMES[@]}" -gt 0 ] || die "scenario '$SCENARIO' defines no peers"
[ -n "$GROUP" ] || die "scenario '$SCENARIO' has no group name"

# The effective model for this run: --expect wins over the scenario's own
# expectation. A stub peer emulates this, so a stub run stays self-consistent
# whatever the scenario declares (e.g. --expect matrix to preview P1.4).
EFFECTIVE_EXPECT="$EXPECT"
[ -n "$EXPECT_OVERRIDE" ] && EFFECTIVE_EXPECT="$EXPECT_OVERRIDE"

# --peers / --count select which scenario peers take part in this run.
if [ -n "$PEER_FILTER" ]; then
    WANTED=()
    IFS=',' read -r -a WANTED <<< "$PEER_FILTER"
    SELECTED=()
    for want in "${WANTED[@]}"; do
        want="$(printf '%s' "$want" | tr -d ' ')"
        [ -n "$want" ] || continue
        found=0
        for i in "${!PEER_NAMES[@]}"; do
            if [ "${PEER_NAMES[$i]}" = "$want" ]; then found=1; break; fi
        done
        [ "$found" = 1 ] || die "--peers names a peer not in scenario '$SCENARIO': $want"
        SELECTED[${#SELECTED[@]}]="$want"
    done
    [ "${#SELECTED[@]}" -gt 0 ] || die "--peers selected no peers"
    NEW_NAMES=(); NEW_ROLES=(); NEW_ENVS=(); NEW_EXTRAS=()
    for want in "${SELECTED[@]}"; do
        for i in "${!PEER_NAMES[@]}"; do
            if [ "${PEER_NAMES[$i]}" = "$want" ]; then
                NEW_NAMES[${#NEW_NAMES[@]}]="${PEER_NAMES[$i]}"
                NEW_ROLES[${#NEW_ROLES[@]}]="${PEER_ROLES[$i]}"
                NEW_ENVS[${#NEW_ENVS[@]}]="${PEER_ENVS[$i]}"
                NEW_EXTRAS[${#NEW_EXTRAS[@]}]="${PEER_EXTRAS[$i]}"
                break
            fi
        done
    done
    PEER_NAMES=("${NEW_NAMES[@]}"); PEER_ROLES=("${NEW_ROLES[@]}"); PEER_ENVS=("${NEW_ENVS[@]}"); PEER_EXTRAS=("${NEW_EXTRAS[@]}")
elif [ -n "$COUNT" ]; then
    case "$COUNT" in ''|*[!0-9]*) die "--count must be a positive integer" ;; esac
    [ "$COUNT" -ge 1 ] || die "--count must be >= 1"
    [ "$COUNT" -le "${#PEER_NAMES[@]}" ] || die "--count $COUNT exceeds ${#PEER_NAMES[@]} peers in scenario '$SCENARIO'"
    PEER_NAMES=("${PEER_NAMES[@]:0:$COUNT}")
    PEER_ROLES=("${PEER_ROLES[@]:0:$COUNT}")
    PEER_ENVS=("${PEER_ENVS[@]:0:$COUNT}")
    PEER_EXTRAS=("${PEER_EXTRAS[@]:0:$COUNT}")
fi

TOTAL_PEERS="${#PEER_NAMES[@]}"
# The evaluator only checks the peers that actually took part.
EVAL_PEERS="$(IFS=,; echo "${PEER_NAMES[*]}")"

# ---------------------------------------------------------------------------
# build check
# ---------------------------------------------------------------------------
[ -n "$APP" ] || APP="$DEFAULT_APP"
[ -n "$SERVER" ] || SERVER="$DEFAULT_SERVER"

# Resolve user-supplied binaries against the invocation directory: a relative
# path would otherwise be resolved against each peer's own working directory.
absolutize() {
    local p="$1"
    case "$p" in
        /*) printf '%s' "$p" ;;
        *) printf '%s/%s' "$INVOCATION_DIR" "$p" ;;
    esac
}
APP="$(absolutize "$APP")"
SERVER="$(absolutize "$SERVER")"

build_check() {
    local rc=0
    if [ ! -x "$APP" ]; then
        printf 'ERROR: SonoBus app binary not found or not executable:\n  %s\n' "$APP" >&2
        printf 'Build it with scripts/build-desktop.sh --app-only, or point the harness\n' >&2
        printf 'at the fake stub:  %s --app %s/fake-peer.sh\n' "$0" "$SCRIPT_DIR" >&2
        rc=1
    fi
    if [ -z "$SERVER_ADDR" ] && [ ! -x "$SERVER" ]; then
        printf 'ERROR: aooserver binary not found or not executable:\n  %s\n' "$SERVER" >&2
        printf 'Build it with scripts/build-desktop.sh --server-only.\n' >&2
        rc=1
    fi
    return $rc
}
build_check || fail "build check failed"

# The real app takes --role, which is passed to every non-stub peer above. The
# stub additionally takes --fake-routing to pick the model it should emulate;
# a scenario role of "default" is passed through as "console", matching the
# real app's own default.
APP_IS_STUB=0
case "$(basename "$APP")" in
    *fake-peer*|*fake_peer*|*stub*) APP_IS_STUB=1 ;;
esac
if [ "$PEER_ARGS_SET" = 0 ] && [ "$APP_IS_STUB" = 1 ]; then
    PEER_ARGS=("--fake-role" "%ROLE%" "--fake-routing" "%EXPECT%")
fi
# P1.5: timed steps drive the app through --test-control and assert mix/level
# fields only the real app produces, so the stub cannot run them.
if [ "$NSTEPS" -gt 0 ] && [ "$APP_IS_STUB" = 1 ]; then
    die "scenario '$SCENARIO' has $NSTEPS control step(s), which need the real app (--test-control); the stub cannot run it"
fi

# ---------------------------------------------------------------------------
# run directory
# ---------------------------------------------------------------------------
if [ -z "$RUN_DIR" ]; then
    RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/f2-harness.XXXXXX")"
else
    mkdir -p "$RUN_DIR"
fi
# Canonicalise with symlinks resolved (pwd -P): on macOS /tmp is a symlink to
# /private/tmp, and lsof reports the physical path, so a logical RUN_DIR would
# never match in pid_is_ours and no peer would be recognised as ours.
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
DUMP_DIR="$RUN_DIR/dumps"
CONTROL_DIR="$RUN_DIR/control"
SERVER_LOG_DIR="$RUN_DIR/server-logs"
mkdir -p "$DUMP_DIR" "$CONTROL_DIR" "$SERVER_LOG_DIR"

log "scenario : $SCENARIO ($ROUTING, group=$GROUP)"
[ -n "$DESC" ] && log "desc     : $DESC"
log "app      : $APP"
if [ -n "$SERVER_ADDR" ]; then log "server   : external $SERVER_ADDR"; else log "server   : $SERVER"; fi
log "run dir  : $RUN_DIR"
if [ "$VERBOSE" = 1 ]; then
    for i in "${!PEER_NAMES[@]}"; do
        vlog "peer ${PEER_NAMES[$i]} role=${PEER_ROLES[$i]}"
    done
fi

if [ -n "$SERVER_ADDR" ]; then
    case "$SERVER_ADDR" in
        *:*) SERVER_HOST="${SERVER_ADDR%:*}"; PORT="${SERVER_ADDR##*:}" ;;
        *) die "--server-addr must be HOST:PORT" ;;
    esac
    case "$PORT" in ''|*[!0-9]*) die "--server-addr port must be numeric" ;; esac
    log "port     : $PORT (external server $SERVER_HOST)"
else
# ---------------------------------------------------------------------------
# pick a free port (the server binds TCP and UDP on the same port)
# ---------------------------------------------------------------------------
PORT=""
attempt=0
while [ "$attempt" -lt 10 ]; do
    attempt=$((attempt + 1))
    candidate="$(python3 "$SCRIPT_DIR/ports.py")" || fail "could not find a free port"
    if lsof -nP -iTCP:"$candidate" -sTCP:LISTEN >/dev/null 2>&1; then
        vlog "port $candidate lost the race (TCP in use), retrying"
        continue
    fi
    PORT="$candidate"
    break
done
[ -n "$PORT" ] || fail "could not find a free TCP+UDP port after $attempt attempts"
log "port     : $PORT"

# ---------------------------------------------------------------------------
# start the server
# ---------------------------------------------------------------------------
( cd "$RUN_DIR" && exec "$SERVER" -p "$PORT" -l "$SERVER_LOG_DIR" ) \
    >"$RUN_DIR/server.out" 2>"$RUN_DIR/server.err" &
SERVER_PID=$!
vlog "aooserver pid $SERVER_PID"

server_accepting() {
    python3 - "$PORT" <<'PY'
import socket, sys
s = socket.socket()
s.settimeout(0.5)
try:
    rc = s.connect_ex(("127.0.0.1", int(sys.argv[1])))
finally:
    s.close()
sys.exit(0 if rc == 0 else 1)
PY
}

wait_for_server() {
    local deadline logfile
    deadline=$(( $(date +%s) + 15 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            printf 'ERROR: aooserver exited during startup (pid %s)\n' "$SERVER_PID" >&2
            [ -f "$RUN_DIR/server.err" ] && sed 's/^/  server.err: /' "$RUN_DIR/server.err" >&2
            [ -f "$RUN_DIR/server.out" ] && sed 's/^/  server.out: /' "$RUN_DIR/server.out" >&2
            return 1
        fi
        # Readiness is logged as a CSV line "... ,ServerStart,<port>".
        logfile="$(ls -1 "$SERVER_LOG_DIR"/aooserver_log_*.txt 2>/dev/null | head -1 || true)"
        if [ -n "$logfile" ] && grep -q "ServerStart,$PORT" "$logfile" 2>/dev/null; then
            # The log line is written before listen() completes; confirm the socket.
            if server_accepting; then
                vlog "server ready ($logfile)"
                return 0
            fi
        fi
        sleep 0.2
    done
    printf 'ERROR: aooserver did not become ready within 15s (port %s)\n' "$PORT" >&2
    [ -f "$RUN_DIR/server.err" ] && sed 's/^/  server.err: /' "$RUN_DIR/server.err" >&2
    return 1
}
wait_for_server || fail "server failed to start"

fi

# ---------------------------------------------------------------------------
# start the peers
# ---------------------------------------------------------------------------
subst_peer_arg() {
    # %NAME% -> peer name, %ROLE% -> scenario role, %ROUTING%/%EXPECT% -> models
    # (EXPECT is the effective one, honouring --expect), %PORT%/%GROUP% -> run values.
    # A scenario role of "default" is the real app's own default role, so the
    # stub is told "console" to match what the app would advertise; a peer that
    # should advertise nothing does so via SONOBUS_NO_ROLE_ADVERT instead.
    local arg="$1" name="$2" role="$3"
    [ "$role" = "default" ] && role="console"
    arg="${arg//%NAME%/$name}"
    arg="${arg//%ROLE%/$role}"
    arg="${arg//%ROUTING%/$ROUTING}"
    arg="${arg//%EXPECT%/$EFFECTIVE_EXPECT}"
    arg="${arg//%PORT%/$PORT}"
    arg="${arg//%GROUP%/$GROUP}"
    printf '%s' "$arg"
}

for i in "${!PEER_NAMES[@]}"; do
    name="${PEER_NAMES[$i]}"
    role="${PEER_ROLES[$i]}"
    peer_env="${PEER_ENVS[$i]:-}"
    peer_dir="$RUN_DIR/peers/$name"
    # Fresh, private settings home per peer. On macOS JUCE resolves its settings
    # via NSHomeDirectory(), which ignores HOME, so CFFIXED_USER_HOME is the
    # only thing that keeps peers off the user's real ~/Library settings (whose
    # reconnectlast=1.0 would make them auto-reconnect with the wrong identity
    # and fail with "login failed: access denied").
    home_dir="$peer_dir/home"
    mkdir -p "$home_dir"

    # P2.1: per-peer config:/setup_role:/cli_* scenario fields (see peerprep.py).
    prep_nocli=0; prep_role="keep"; prep_args=()
    if [ "$APP_IS_STUB" = 0 ]; then
        mkdir -p "$peer_dir"
        while IFS=$'\t' read -r pk pv; do
            case "$pk" in
                NOCLI) prep_nocli=1 ;;
                ROLE)  prep_role="$pv" ;;
                ARG)   prep_args[${#prep_args[@]}]="$pv" ;;
            esac
        done < <(python3 "$SCRIPT_DIR/peerprep.py" "${PEER_EXTRAS[$i]}" "$peer_dir" "$SERVER_HOST" "$PORT" "$GROUP" "$name")
    fi

    cmd=( "$APP" -q --dump-peers "$DUMP_DIR/$name.json" )
    if [ "$prep_nocli" = 0 ]; then
        cmd+=( -c "$SERVER_HOST:$PORT" -g "$GROUP" -n "$name" )
    fi
    if [ "${#prep_args[@]}" -gt 0 ]; then
        cmd+=( "${prep_args[@]}" )
    fi
    # --role is the real app's flag. "default" means the scenario did not
    # specify one, so the flag is omitted and the app uses its own default
    # (console). The stub accepts --role too, but for a stub run it gets
    # --fake-role from PEER_ARGS below, so it is not passed twice.
    if [ "$APP_IS_STUB" = 0 ]; then
        case "$prep_role" in
            keep) if [ "$role" != "default" ]; then cmd+=( --role "$role" ); fi ;;
            none) ;;
            *)    cmd+=( --role "$prep_role" ) ;;
        esac
    fi
    # P1.5: the control file need not exist yet; evaluate.py writes it per step.
    if [ "$NSTEPS" -gt 0 ]; then
        cmd+=( --test-control "$CONTROL_DIR/$name.json" )
    fi
    if [ "${#PEER_ARGS[@]}" -gt 0 ]; then
        for extra in "${PEER_ARGS[@]}"; do
            [ -n "$extra" ] || continue
            cmd+=( "$(subst_peer_arg "$extra" "$name" "$role")" )
        done
    fi

    # Assemble the peer environment: the private home, then scenario env vars.
    env_assign=( "CFFIXED_USER_HOME=$home_dir" )
    if [ -n "$FAULT" ]; then
        env_assign+=( "FAKE_PEER_FAULT=$FAULT" )
    fi
    if [ -n "$peer_env" ]; then
        IFS=',' read -r -a extra_env <<< "$peer_env"
        for kv in ${extra_env[@]+"${extra_env[@]}"}; do
            [ -n "$kv" ] || continue
            case "$kv" in
                *=*) env_assign+=( "$kv" ) ;;
                *) die "scenario env entry is not KEY=VALUE: $kv" ;;
            esac
        done
    fi

    if [ "$VERBOSE" = 1 ]; then
        # Note the trailing spaces: bash's %q does not add a separator, so
        # "printf '%q' a b" would print "ab".
        printf '>>> peer %s cmd: ' "$name" >&2
        printf '%q ' "${env_assign[@]}" >&2
        printf '%q ' "${cmd[@]}" >&2
        printf '\n' >&2
    fi

    # cd into the peer dir so lsof cwd identifies the process as ours.
    ( cd "$peer_dir" && exec env "${env_assign[@]}" "${cmd[@]}" ) \
        >"$peer_dir/peer.out" 2>"$peer_dir/peer.err" &
    PEER_PIDS[$i]=$!
    vlog "peer $name pid ${PEER_PIDS[$i]} role=$role home=$home_dir"
    sleep 0.3   # stagger launches so the server sees distinct join times
done

log "started $TOTAL_PEERS peer(s); waiting up to ${TIMEOUT}s to settle"

# ---------------------------------------------------------------------------
# settle, then evaluate
# ---------------------------------------------------------------------------
# A peer that dies early is a hard failure; report its log rather than waiting
# the whole timeout for a dump that will never arrive.
sleep 1
for i in "${!PEER_NAMES[@]}"; do
    pid="${PEER_PIDS[$i]}"
    if ! kill -0 "$pid" 2>/dev/null; then
        name="${PEER_NAMES[$i]}"
        log "peer $name (pid $pid) exited during startup"
        [ -f "$RUN_DIR/peers/$name/peer.err" ] && sed 's/^/  peer.err: /' "$RUN_DIR/peers/$name/peer.err" >&2
        [ -f "$RUN_DIR/peers/$name/peer.out" ] && sed 's/^/  peer.out: /' "$RUN_DIR/peers/$name/peer.out" >&2
        fail "peer $name failed to stay running"
    fi
done

EVAL_EXTRA=()
[ "$STRICT" = 1 ] && EVAL_EXTRA[${#EVAL_EXTRA[@]}]="--strict-fields"
[ "$REQUIRE_CONNECTED" = 1 ] && EVAL_EXTRA[${#EVAL_EXTRA[@]}]="--require-connected"
[ "$VERBOSE" = 1 ] && EVAL_EXTRA[${#EVAL_EXTRA[@]}]="--verbose"
[ "$VERBOSE" = 1 ] && EVAL_EXTRA[${#EVAL_EXTRA[@]}]="--progress"
[ -n "$EXPECT_OVERRIDE" ] && EVAL_EXTRA[${#EVAL_EXTRA[@]}]="--expect" && EVAL_EXTRA[${#EVAL_EXTRA[@]}]="$EXPECT_OVERRIDE"
RC=0
set +e
python3 "$SCRIPT_DIR/evaluate.py" settle \
    --dir "$DUMP_DIR" \
    --scenario "$SCENARIO" \
    --scenarios "$SCENARIOS_FILE" \
    --peers "$EVAL_PEERS" \
    --timeout "$TIMEOUT" \
    --interval "$INTERVAL" \
    ${EVAL_EXTRA[@]+"${EVAL_EXTRA[@]}"}
RC=$?
# P1.5: once the routing model holds, run the scenario's timed control steps.
if [ "$RC" -eq 0 ] && [ "$NSTEPS" -gt 0 ]; then
    log "routing settled; running $NSTEPS control step(s)"
    python3 "$SCRIPT_DIR/evaluate.py" steps \
        --dir "$DUMP_DIR" \
        --control-dir "$CONTROL_DIR" \
        --scenario "$SCENARIO" \
        --scenarios "$SCENARIOS_FILE" \
        --peers "$EVAL_PEERS" \
        --interval "$INTERVAL"
    RC=$?
fi
set -e

if [ -n "$FAULT" ]; then
    log "fault injection was active: FAKE_PEER_FAULT=$FAULT"
fi

# Summarise where the peers' logs went before teardown may remove them.
if [ "$RC" -ne 0 ]; then
    log "FAILED (exit $RC). Per-peer logs:"
    for name in "${PEER_NAMES[@]}"; do
        err="$RUN_DIR/peers/$name/peer.err"
        if [ -s "$err" ]; then
            printf '>>>   %s: %s\n' "$name" "$(tail -n 3 "$err" | tr '\n' ' ')" >&2
        fi
    done
    if [ "$KEEP" = 1 ]; then
        log "run directory kept for inspection: $RUN_DIR"
    else
        log "re-run with --keep to inspect the run directory"
    fi
else
    log "PASS: scenario $SCENARIO matched (exit 0)"
fi

exit "$RC"
