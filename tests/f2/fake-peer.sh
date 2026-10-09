#!/usr/bin/env bash
# Fake SonoBus peer for tests/f2.
#
# Accepts (a superset of) the real app's CLI so the harness orchestration can be
# exercised end to end without the C++ --dump-peers implementation:
#
#   fake-peer.sh -q -c 127.0.0.1:<port> -g <group> -n <name> \
#                [-p <group-password>] [-l <setup>] --dump-peers <file> \
#                [--fake-role console|vdi|unknown] \
#                [--fake-routing mesh|matrix] [--fake-delay <seconds>]
#
# It binds nothing and never talks to the server: it only pretends to be a peer
# by writing the dump JSON described in evaluate.py, atomically, about once a
# second. Other peers are "discovered" by listing sibling dump files in the same
# directory, and each peer appears only after a short, per-peer staggered delay
# so that the harness's settle logic is genuinely exercised.
#
# Fault injection (environment, used by test-evaluate.sh and run.sh --fault):
#   FAKE_PEER_FAULT=never-appear     never list other peers (never settles)
#   FAKE_PEER_FAULT=no-dump          never write a dump at all
#   FAKE_PEER_FAULT=empty-dump       always leave an empty (mid-rewrite) dump
#   FAKE_PEER_FAULT=bad-send-allow   answer sendAllow=true for every peer
#   FAKE_PEER_FAULT=self-role-unknown  advertise selfRole "unknown" regardless
#
# Exits 0 on SIGTERM/SIGINT, removing its dump first.
set -euo pipefail

usage() {
    sed -n '2,26p' "$0"
    exit "${1:-0}"
}

USERNAME=""
GROUP=""
SERVER=""
ROLE_OVERRIDE=""
ROUTING="matrix"
DELAY=""
VERBOSE=0

# Parse the real app's options plus our --fake-* extensions. The real app's
# flags are accepted (and where meaningful, honoured) so run.sh can pass the
# same command shape to the stub and to the real binary.
while [ $# -gt 0 ]; do
    case "$1" in
        -q|--headless)      shift ;;
        -c|--connectionserver) SERVER="${2:-}"; shift 2 ;;
        -g|--group)         GROUP="${2:-}"; shift 2 ;;
        -n|--username)      USERNAME="${2:-}"; shift 2 ;;
        -p|--group-password) shift 2 ;;
        -l|--load-setup)    shift 2 ;;
        --dump-peers)       DUMP_FILE="${2:-}"; shift 2 ;;
        # The real app's own role flag: records our selfRole, exactly as the app
        # does. --fake-role (below) is the stub-specific override.
        --role)             ROLE_OVERRIDE="${2:-}"; shift 2 ;;
        --fake-role)        ROLE_OVERRIDE="${2:-}"; shift 2 ;;
        --fake-routing)     ROUTING="${2:-}"; shift 2 ;;
        --fake-delay)       DELAY="${2:-}"; shift 2 ;;
        --verbose)          VERBOSE=1; shift ;;
        -v|--version)       echo "fake-peer (stand-in for SonoBus)"; exit 0 ;;
        -h|--help)          usage 0 ;;
        *) echo "fake-peer: unexpected argument: $1" >&2; exit 2 ;;
    esac
done

case "$ROUTING" in
    mesh|matrix|all-blocked) ;;
    *) echo "fake-peer: --fake-routing must be mesh, matrix or all-blocked, got '$ROUTING'" >&2; exit 2 ;;
esac

case "${ROLE_OVERRIDE:-}" in
    ""|console|vdi|unknown) ;;
    *) echo "fake-peer: --role must be console, vdi or unknown, got '$ROLE_OVERRIDE'" >&2; exit 2 ;;
esac

[ -n "$USERNAME" ] || { echo "fake-peer: -n/--username is required" >&2; exit 2; }
[ -n "${DUMP_FILE:-}" ] || { echo "fake-peer: --dump-peers is required" >&2; exit 2; }

DUMP_DIR="$(cd "$(dirname "$DUMP_FILE")" && pwd)"
DUMP_PATH="$DUMP_DIR/$USERNAME.json"
TMP_PATH="$DUMP_PATH.tmp.$$"
FAULT="${FAKE_PEER_FAULT:-none}"

# Mirror the real app's defaulting: when no role is supplied it acts as
# "console", even if it does not advertise that role to others.
ROLE="console"
if [ -n "$ROLE_OVERRIDE" ]; then
    ROLE="$ROLE_OVERRIDE"
fi
if [ "$FAULT" = "self-role-unknown" ]; then
    ROLE="unknown"
fi

# The two real-app env vars are honoured so scenario env blocks exercise the
# same code path for the stub and the real app (see tests/f2/README.md):
#   SONOBUS_NO_ROLE_ADVERT=1 -> we are seen as "unknown" by other peers.
#   SONOBUS_NO_ROLE_BLOCK=1  -> open by default: mesh-style allow flags.
case "${SONOBUS_NO_ROLE_ADVERT:-}" in
    1|true|yes|TRUE|YES) ADVERTISE=0 ;;
    *) ADVERTISE=1 ;;
esac
case "${SONOBUS_NO_ROLE_BLOCK:-}" in
    1|true|yes|TRUE|YES) NO_ROLE_BLOCK=1 ;;
    *) NO_ROLE_BLOCK=0 ;;
esac

# Stagger discovery so peers really do appear over time, like the real thing.
# The value is derived from the peer name so a run is deterministic.
if [ -z "$DELAY" ]; then
    n=$(printf '%s' "$USERNAME" | cksum | awk '{print $1}')
    DELAY=$(( (n % 3) + 1 ))
fi

EXIT_REQUESTED=0
cleanup() {
    rm -f "$TMP_PATH" 2>/dev/null || true
    rm -f "$DUMP_PATH" 2>/dev/null || true
    rm -f "$DUMP_PATH.flags" 2>/dev/null || true
}
on_signal() {
    EXIT_REQUESTED=1
}
trap on_signal TERM INT

json_bool() { [ "$1" = "true" ] && printf 'true' || printf 'false'; }

# Whether A may send to B, mirroring evaluate.py's model:
#   mesh (or SONOBUS_NO_ROLE_BLOCK) -> always allowed
#   all-blocked                     -> never allowed (the pre-P1.4 real state)
#   matrix -> console<->vdi only; same role or either side unknown is blocked
allowed() {
    local a="$1" b="$2"
    if [ "$ROUTING" = "all-blocked" ]; then
        printf 'false'
        return
    fi
    if [ "$ROUTING" = "mesh" ] || [ "$NO_ROLE_BLOCK" = 1 ]; then
        printf 'true'
        return
    fi
    [ "$a" = "unknown" ] && { printf 'false'; return; }
    [ "$b" = "unknown" ] && { printf 'false'; return; }
    [ "$a" = "$b" ] && { printf 'false'; return; }
    printf 'true'
}

# Read the role a sibling *advertises*, defensively: a dump may be missing,
# empty, or mid-rewrite, in which case we treat it as unknown.
#
# Why the sidecar: the real app reports selfRole as its own role even when it
# does not advertise (a no-advert peer still says selfRole "console", while
# others observe "unknown" and hasRole false). A single stub script cannot infer
# that from the JSON, so each stub also drops a tiny "<dump>.flags" marker
# beside its dump. That keeps the dump JSON exactly to contract (the marker is
# not a .json file, so nothing that globs *.json sees it).
read_role() {
    local file="$1" role="" advert=""
    [ -s "$file" ] || { printf 'unknown'; return; }
    advert="$(cat "$file.flags" 2>/dev/null || true)"
    case "$advert" in
        advertise=0) printf 'unknown'; return ;;
    esac
    role=$(sed -n 's/.*"selfRole"[[:space:]]*:[[:space:]]*"\([a-z]*\)".*/\1/p' "$file" 2>/dev/null | head -1)
    case "$role" in
        console|vdi|unknown) printf '%s' "$role" ;;
        *) printf 'unknown' ;;
    esac
}

write_dump() {
    local now elapsed self_role entry name other_role r s send recv first
    now=$(date +%s)
    elapsed=$(( now - START_TIME ))
    self_role="$ROLE"

    # Peers become visible only once both sides have passed their own delay.
    local peers_json=""
    first=1
    for entry in "$DUMP_DIR"/*.json; do
        [ -e "$entry" ] || continue
        name="$(basename "$entry" .json)"
        [ "$name" = "$USERNAME" ] && continue

        # The other peer's own dump exists, so it has started; it appears to us
        # "late" too, which is what makes the settle loop meaningful.
        other_role="$(read_role "$entry")"

        if [ "$FAULT" = "never-appear" ]; then
            continue
        fi

        r="$(allowed "$other_role" "$self_role")"        # may they send to us
        s="$(allowed "$self_role" "$other_role")"        # may we send to them
        send="$s"; recv="$r"
        if [ "$FAULT" = "bad-send-allow" ]; then
            send="true"
        fi

        [ $first -eq 1 ] || peers_json="$peers_json,"
        first=0
        peers_json="$peers_json
    { \"name\": \"$name\", \"role\": \"$other_role\", \"connected\": true, \"sendAllow\": $(json_bool "$send"), \"recvAllow\": $(json_bool "$recv"), \"sendActive\": true, \"recvActive\": false, \"receivingAudio\": false }"
    done

    # Atomic publish: write a temp file in the same directory, then rename.
    {
        printf '{\n'
        printf '  "self": "%s",\n' "$USERNAME"
        printf '  "selfRole": "%s",\n' "$self_role"
        if [ -n "$peers_json" ]; then
            printf '  "peers": [%s\n  ]\n' "$peers_json"
        else
            printf '  "peers": []\n'
        fi
        printf '}\n'
    } > "$TMP_PATH"
    mv -f "$TMP_PATH" "$DUMP_PATH"
    # Sidecar flag consumed by other stubs via read_role (see above).
    printf 'advertise=%s\n' "$ADVERTISE" > "$DUMP_PATH.flags"

    [ "$VERBOSE" = 1 ] && echo "fake-peer $USERNAME: elapsed=${elapsed}s role=$self_role advertise=$ADVERTISE dump=$DUMP_PATH" >&2
    return 0
}

START_TIME=$(date +%s)
[ "$VERBOSE" = 1 ] && echo "fake-peer $USERNAME: server=${SERVER:-none} group=${GROUP:-none} dir=$DUMP_DIR delay=${DELAY}s fault=$FAULT" >&2

# Do not publish anything until our stagger delay has elapsed.
sleep "$DELAY"

if [ "$FAULT" = "no-dump" ]; then
    # Never write; just stay alive like a wedged app.
    while [ "$EXIT_REQUESTED" = 0 ]; do sleep 0.2; done
    exit 0
fi

if [ "$FAULT" = "empty-dump" ]; then
    : > "$DUMP_PATH"
    while [ "$EXIT_REQUESTED" = 0 ]; do
        : > "$DUMP_PATH"
        sleep 0.5
    done
    cleanup
    exit 0
fi

# Main loop: refresh the dump roughly once a second.
while [ "$EXIT_REQUESTED" = 0 ]; do
    write_dump
    # Sleep in small slices so signals are handled promptly.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        [ "$EXIT_REQUESTED" = 1 ] && break
        sleep 0.1
    done
done

cleanup
exit 0
