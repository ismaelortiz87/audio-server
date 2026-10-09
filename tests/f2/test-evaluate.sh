#!/usr/bin/env bash
# Unit tests for tests/f2/evaluate.py, using hand-written mock dump files.
#
# Each case builds a dump directory, runs evaluate.py check, and asserts both the
# exit code and that the report mentions the expected detail. No processes or
# network are involved, so this runs anywhere python3 does.
#
#   tests/f2/test-evaluate.sh            # run all cases
#   tests/f2/test-evaluate.sh --verbose  # also print each report
#
# The mock dumps deliberately match the REAL app's output shape, including the
# extra "hasRole" field and the fact that a peer which does not advertise still
# reports a real selfRole while others observe "unknown".
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EVAL="$SCRIPT_DIR/evaluate.py"
SCENARIOS="$SCRIPT_DIR/scenarios.json"
VERBOSE=0
[ "${1:-}" = "--verbose" ] && VERBOSE=1

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/f2-eval-tests.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

PASS_COUNT=0
FAIL_COUNT=0
LAST_OUT=""

# check_case NAME EXPECTED_RC SCENARIO [extra evaluate.py args...]
# The dump directory is $WORK/<NAME> and must already exist.
check_case() {
    local name="$1" expected_rc="$2" scenario="$3"; shift 3
    local dir="$WORK/$name"
    local out rc=0
    set +e
    out="$(python3 "$EVAL" check --dir "$dir" --scenario "$scenario" \
        --scenarios "$SCENARIOS" "$@" 2>&1)"
    rc=$?
    set -e
    if [ "$rc" -ne "$expected_rc" ]; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
        printf 'FAIL %-26s expected exit %s, got %s\n' "$name" "$expected_rc" "$rc"
        printf '%s\n' "$out" | sed 's/^/       | /'
        return 0
    fi
    LAST_OUT="$out"
    PASS_COUNT=$((PASS_COUNT + 1))
    printf 'ok   %-26s exit %s\n' "$name" "$rc"
    if [ "$VERBOSE" = 1 ]; then printf '%s\n' "$out" | sed 's/^/       | /'; fi
}

# assert_contains NAME NEEDLE -- fails the suite when $LAST_OUT lacks NEEDLE.
assert_contains() {
    local name="$1" needle="$2"
    if printf '%s' "$LAST_OUT" | grep -qF -- "$needle"; then
        PASS_COUNT=$((PASS_COUNT + 1))
        printf 'ok   %-26s report mentions %s\n' "$name" "$needle"
    else
        FAIL_COUNT=$((FAIL_COUNT + 1))
        printf 'FAIL %-26s report does not mention: %s\n' "$name" "$needle"
        printf '%s\n' "$LAST_OUT" | sed 's/^/       | /'
    fi
}

# ---------------------------------------------------------------------------
# Helpers that write dumps in the contract shape (real-app-like, with hasRole).
# ---------------------------------------------------------------------------
# write_dump DIR SELF SELFROLE PEER:ROLE:SEND:RECV ...
write_dump() {
    local dir="$1" self="$2" selfrole="$3"; shift 3
    mkdir -p "$dir"
    python3 - "$dir/$self.json" "$self" "$selfrole" "$@" <<'PY'
import json, sys
path, self_name, self_role = sys.argv[1], sys.argv[2], sys.argv[3]
peers = []
for spec in sys.argv[4:]:
    name, role, send, recv = spec.split(":")
    peers.append({
        "name": name, "role": role, "hasRole": role != "unknown",
        "connected": True,
        "sendAllow": send == "true", "recvAllow": recv == "true",
        "sendActive": True, "recvActive": False, "receivingAudio": False,
    })
doc = {"self": self_name, "selfRole": self_role, "peers": peers}
with open(path, "w") as fh:
    json.dump(doc, fh, indent=2)
PY
}

# matrix_allowed A B -- the routing matrix from evaluate.py.
matrix_allowed() {
    local a="$1" b="$2"
    if [ "$a" = unknown ] || [ "$b" = unknown ]; then printf false; return; fi
    if [ "$a" = "$b" ]; then printf false; return; fi
    printf true
}

# mesh_dump DIR SELF SELFROLE OTHER...
# mesh-stock scenario: role null => console, and SONOBUS_NO_ROLE_BLOCK makes
# every direction open, so both flags are true for every pair.
mesh_dump() {
    local dir="$1" self="$2" role="$3"; shift 3
    local specs=() other
    for other in "$@"; do
        specs+=("$other:$role:true:true")
    done
    write_dump "$dir" "$self" "$role" ${specs[@]+"${specs[@]}"}
}

# matrix_dump DIR SELF SELFROLE OTHER:ROLE ...
# Derives each peer entry from the routing matrix. `observed` is the role other
# peers see: use "unknown" for a peer that does not advertise.
matrix_dump() {
    local dir="$1" self="$2" selfrole="$3"; shift 3
    local specs=() spec oname orole send recv
    for spec in "$@"; do
        oname="${spec%%:*}"; orole="${spec##*:}"
        send="$(matrix_allowed "$selfrole" "$orole")"
        recv="$(matrix_allowed "$orole" "$selfrole")"
        specs+=("$oname:$orole:$send:$recv")
    done
    write_dump "$dir" "$self" "$selfrole" ${specs[@]+"${specs[@]}"}
}

# all_blocked_dump DIR SELF SELFROLE OTHER:ROLE ...
# The current real-app behaviour before P1.4: roles are known but nothing routes.
all_blocked_dump() {
    local dir="$1" self="$2" selfrole="$3"; shift 3
    local specs=() spec oname orole
    for spec in "$@"; do
        oname="${spec%%:*}"; orole="${spec##*:}"
        specs+=("$oname:$orole:false:false")
    done
    write_dump "$dir" "$self" "$selfrole" ${specs[@]+"${specs[@]}"}
}

# agent_dump DIR SELF SELFROLE AGENT_INPUT AGENT_OUTPUT OTHER:ROLE ...
# P1.7: a peer that routes by the matrix and reports agent health for the peers
# it observes. `selfrole` decides selfAgent (only a VDI has one), and observed
# peers get an agent object only when they are VDIs -- matching the real app.
agent_dump() {
    local dir="$1" self="$2" selfrole="$3" ain="$4" aout="$5"; shift 5
    local specs=() spec oname orole send recv
    for spec in "$@"; do
        oname="${spec%%:*}"; orole="${spec##*:}"
        send="$(matrix_allowed "$selfrole" "$orole")"
        recv="$(matrix_allowed "$orole" "$selfrole")"
        specs+=("$oname:$orole:$send:$recv")
    done
    mkdir -p "$dir"
    python3 - "$dir/$self.json" "$self" "$selfrole" "$ain" "$aout" ${specs[@]+"${specs[@]}"} <<'PY'
import json, sys
path, self_name, self_role, ain, aout = sys.argv[1:6]
peers = []
for spec in sys.argv[6:]:
    name, role, send, recv = spec.split(":")
    entry = {
        "name": name, "role": role, "hasRole": role != "unknown",
        "connected": True,
        "sendAllow": send == "true", "recvAllow": recv == "true",
        "sendActive": True, "recvActive": False, "receivingAudio": False,
    }
    if role == "vdi":
        entry["hasAgent"] = True
        entry["agent"] = {"input": ain, "output": aout,
                          "paused": False, "config_error": None}
    else:
        entry["hasAgent"] = False
    peers.append(entry)
doc = {"self": self_name, "selfRole": self_role, "peers": peers}
if self_role == "vdi":
    doc["selfAgent"] = {"input": ain, "output": aout,
                        "paused": False, "config_error": None}
with open(path, "w") as fh:
    json.dump(doc, fh, indent=2)
PY
}

echo "== evaluate.py unit tests =="
echo

# ---------------------------------------------------------------------------
# 1. mesh-stock passes (scenario expect=mesh, null roles => console)
# ---------------------------------------------------------------------------
D="$WORK/mesh-ok"
mesh_dump "$D" p1 console p2 p3
mesh_dump "$D" p2 console p1 p3
mesh_dump "$D" p3 console p1 p2
check_case mesh-ok 0 mesh-stock
assert_contains mesh-ok "PASS"

# ---------------------------------------------------------------------------
# 2. matrix-target-check asserts the matrix directly and must PASS on correct
#    cross-role dumps.
# ---------------------------------------------------------------------------
D="$WORK/matrix-target-ok"
matrix_dump "$D" c1 console v1:vdi
matrix_dump "$D" v1 vdi c1:console
check_case matrix-target-ok 0 matrix-target-check
assert_contains matrix-target-ok "PASS"

# Same-role blocking, checked via --expect matrix on the 2v2 scenario.
D="$WORK/matrix-2v2-target-ok"
matrix_dump "$D" c1 console c2:console v1:vdi v2:vdi
matrix_dump "$D" c2 console c1:console v1:vdi v2:vdi
matrix_dump "$D" v1 vdi c1:console c2:console v2:vdi
matrix_dump "$D" v2 vdi c1:console c2:console v1:vdi
check_case matrix-2v2-target-ok 0 matrix-2v2 --expect matrix
assert_contains matrix-2v2-target-ok "PASS"

# ---------------------------------------------------------------------------
# 3. P1.4 is implemented, so matrix-1v1 / matrix-2v2 now expect the matrix
#    directly: correct cross-role dumps PASS with no --expect override. The
#    pre-P1.4 all-blocked state is now the WRONG answer for these scenarios and
#    must fail, which is the regression guard that the scenarios really flipped.
# ---------------------------------------------------------------------------
D="$WORK/matrix-1v1-ok"
matrix_dump "$D" c1 console v1:vdi
matrix_dump "$D" v1 vdi c1:console
check_case matrix-1v1-ok 0 matrix-1v1
assert_contains matrix-1v1-ok "PASS"

D="$WORK/matrix-2v2-ok"
matrix_dump "$D" c1 console c2:console v1:vdi v2:vdi
matrix_dump "$D" c2 console c1:console v1:vdi v2:vdi
matrix_dump "$D" v1 vdi c1:console c2:console v2:vdi
matrix_dump "$D" v2 vdi c1:console c2:console v1:vdi
check_case matrix-2v2-ok 0 matrix-2v2
assert_contains matrix-2v2-ok "PASS"

# The old all-blocked state must no longer satisfy these scenarios.
D="$WORK/matrix-1v1-blocked-wrong"
all_blocked_dump "$D" c1 console v1:vdi
all_blocked_dump "$D" v1 vdi c1:console
check_case matrix-1v1-blocked-wrong 1 matrix-1v1
assert_contains matrix-1v1-blocked-wrong "MISMATCH"
assert_contains matrix-1v1-blocked-wrong "sendAllow: expected true, got false"

D="$WORK/matrix-2v2-blocked-wrong"
all_blocked_dump "$D" c1 console c2:console v1:vdi v2:vdi
all_blocked_dump "$D" c2 console c1:console v1:vdi v2:vdi
all_blocked_dump "$D" v1 vdi c1:console c2:console v2:vdi
all_blocked_dump "$D" v2 vdi c1:console c2:console v1:vdi
check_case matrix-2v2-blocked-wrong 1 matrix-2v2
assert_contains matrix-2v2-blocked-wrong "MISMATCH"

# ---------------------------------------------------------------------------
# 4. blocked-unknown passes: peers observe "unknown" and nothing routes.
#    Note selfRole stays console (the app default) while others see unknown.
# ---------------------------------------------------------------------------
D="$WORK/blocked-unknown-ok"
all_blocked_dump "$D" u1 console u2:unknown
all_blocked_dump "$D" u2 console u1:unknown
check_case blocked-unknown-ok 0 blocked-unknown
assert_contains blocked-unknown-ok "PASS"

# ---------------------------------------------------------------------------
# 4b. P1.7 agent health.
# ---------------------------------------------------------------------------
# Correct health passes, and both sides are checked: the Console's view of the
# VDI and the VDI's own selfAgent.
D="$WORK/agent-ok"
agent_dump "$D" c1 console missing ok v1:vdi
agent_dump "$D" v1 vdi missing ok c1:console
check_case agent-ok 0 agent-health
assert_contains agent-ok "PASS"

# A wrong input state must fail, in both the observed and the self direction.
D="$WORK/agent-wrong-input"
agent_dump "$D" c1 console ok ok v1:vdi
agent_dump "$D" v1 vdi missing ok c1:console
check_case agent-wrong-input 1 agent-health
assert_contains agent-wrong-input 'agent.input: expected "missing", got "ok"'

# A VDI that does not report its own health must fail too.
D="$WORK/agent-missing-self"
agent_dump "$D" c1 console missing ok v1:vdi
write_dump "$D" v1 vdi c1:console:true:true
check_case agent-missing-self 1 agent-health
assert_contains agent-missing-self "selfAgent: missing"

# "silent" is an accepted state and is asserted by the silent scenario.
D="$WORK/agent-silent-ok"
agent_dump "$D" c1 console silent ok v1:vdi
agent_dump "$D" v1 vdi silent ok c1:console
check_case agent-silent-ok 0 agent-silent
assert_contains agent-silent-ok "PASS"

# A malformed agent fails loudly even in a scenario that does not assert health,
# because a present object is always type-checked.
D="$WORK/agent-bad-enum"
python3 - "$D" <<'PY'
import json, os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def dump(name, role, other, orole, agent):
    json.dump({"self": name, "selfRole": role, "peers": [
        {"name": other, "role": orole, "hasRole": True, "connected": True,
         "sendAllow": True, "recvAllow": True, "agent": agent, "hasAgent": True}]},
        open(os.path.join(d, name + ".json"), "w"))
bad = {"input": "bogus", "output": "ok", "paused": "yes", "config_error": 7}
dump("c1", "console", "v1", "vdi", bad)
dump("v1", "vdi", "c1", "console", bad)
PY
check_case agent-bad-enum 1 matrix-1v1
assert_contains agent-bad-enum 'agent.input: expected one of'
assert_contains agent-bad-enum "agent.paused: expected a boolean"
assert_contains agent-bad-enum "agent.config_error: expected a string or null"

# An agent object missing a contract key fails even when the scenario does not
# assert that peer's health.
D="$WORK/agent-missing-key"
python3 - "$D" <<'PY'
import json, os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def dump(name, role, other, orole):
    json.dump({"self": name, "selfRole": role, "peers": [
        {"name": other, "role": orole, "hasRole": True, "connected": True,
         "sendAllow": True, "recvAllow": True, "hasAgent": True,
         "agent": {"input": "ok", "output": "ok"}}]},
        open(os.path.join(d, name + ".json"), "w"))
dump("c1", "console", "v1", "vdi")
dump("v1", "vdi", "c1", "console")
PY
check_case agent-missing-key 1 matrix-1v1
assert_contains agent-missing-key "agent.paused: missing (contract field)"
assert_contains agent-missing-key "agent.config_error: missing (contract field)"

# ---------------------------------------------------------------------------
# 5. wrong expectation: same-role peers claim they may send to each other.
# ---------------------------------------------------------------------------
D="$WORK/matrix-wrong"
# c1 is a console that wrongly allows sending to c2 (another console).
write_dump "$D" c1 console c2:console:true:true v1:vdi:true:true v2:vdi:true:true
matrix_dump "$D" c2 console c1:console v1:vdi v2:vdi
matrix_dump "$D" v1 vdi c1:console c2:console v2:vdi
matrix_dump "$D" v2 vdi c1:console c2:console v1:vdi
check_case matrix-wrong 1 matrix-2v2 --expect matrix
assert_contains matrix-wrong "MISMATCH"
assert_contains matrix-wrong "sendAllow: expected false, got true"

# A mesh peer that blocks another peer must fail too.
D="$WORK/mesh-wrong"
write_dump "$D" p1 console p2:console:true:true p3:console:false:true
mesh_dump "$D" p2 console p1 p3
mesh_dump "$D" p3 console p1 p2
check_case mesh-wrong 1 mesh-stock
assert_contains mesh-wrong "sendAllow: expected true, got false"

# A peer that wrongly reports its own role fails.
D="$WORK/self-role-wrong"
matrix_dump "$D" c1 vdi v1:vdi
matrix_dump "$D" v1 vdi c1:console
check_case self-role-wrong 1 matrix-target-check
assert_contains self-role-wrong 'selfRole: expected "console", got "vdi"'

# ---------------------------------------------------------------------------
# 6. missing peer: dump absent entirely, and peer absent from the peers array
# ---------------------------------------------------------------------------
D="$WORK/matrix-missing-dump"
matrix_dump "$D" c1 console v1:vdi
# v1.json deliberately never created
check_case matrix-missing-dump 1 matrix-target-check
assert_contains matrix-missing-dump "UNREADABLE"
assert_contains matrix-missing-dump "v1"

D="$WORK/matrix-missing-entry"
write_dump "$D" c1 console v1:vdi:true:true
# v1 knows only about itself, so c1 is absent from its peers array
write_dump "$D" v1 vdi
check_case matrix-missing-entry 1 matrix-target-check
assert_contains matrix-missing-entry "missing from peers"
assert_contains matrix-missing-entry "c1"

# ---------------------------------------------------------------------------
# 7. mid-rewrite / empty / malformed files must fail, not crash
# ---------------------------------------------------------------------------
D="$WORK/empty-file"
matrix_dump "$D" c1 console v1:vdi
: > "$D/v1.json"
check_case empty-file 1 matrix-target-check
assert_contains empty-file "file is empty"

D="$WORK/truncated-json"
matrix_dump "$D" c1 console v1:vdi
printf '{"self": "v1", "selfRole": "vd' > "$D/v1.json"
check_case truncated-json 1 matrix-target-check
assert_contains truncated-json "invalid JSON"

D="$WORK/whitespace-only"
matrix_dump "$D" c1 console v1:vdi
printf '   \n\t\n' > "$D/v1.json"
check_case whitespace-only 1 matrix-target-check
assert_contains whitespace-only "file is empty"

D="$WORK/wrong-json-type"
matrix_dump "$D" c1 console v1:vdi
printf '[]' > "$D/v1.json"
check_case wrong-json-type 1 matrix-target-check
assert_contains wrong-json-type "expected an object"

# A malformed dump must not stop the *other* peers being reported correctly.
D="$WORK/one-bad-one-good"
matrix_dump "$D" c1 console v1:vdi
printf '{' > "$D/v1.json"
check_case one-bad-one-good 1 matrix-target-check
assert_contains one-bad-one-good "peer c1"
assert_contains one-bad-one-good "OK"

# ---------------------------------------------------------------------------
# 8. role reporting: unknown role strings, and observed-vs-self asymmetry
# ---------------------------------------------------------------------------
D="$WORK/unknown-role-string"
write_dump "$D" c1 console v1:bogus:true:true
write_dump "$D" v1 vdi c1:console:true:true
check_case unknown-role-string 1 matrix-target-check
assert_contains unknown-role-string "expected one of"

# A peer observed as "unknown" when the scenario says it advertises a real role.
D="$WORK/observed-unknown"
write_dump "$D" c1 console v1:unknown:false:false
write_dump "$D" v1 vdi c1:console:true:true
check_case observed-unknown 1 matrix-target-check
assert_contains observed-unknown "v1 role: expected \"vdi\", got \"unknown\""

# The no-advert case is correct when the scenario declares advertise:false.
D="$WORK/observed-unknown-ok"
all_blocked_dump "$D" u1 console u2:unknown
all_blocked_dump "$D" u2 console u1:unknown
check_case observed-unknown-ok 0 blocked-unknown
assert_contains observed-unknown-ok "PASS"

D="$WORK/unexpected-peer"
write_dump "$D" c1 console v1:vdi:true:true ghost:vdi:true:true
write_dump "$D" v1 vdi c1:console:true:true
check_case unexpected-peer 1 matrix-target-check
assert_contains unexpected-peer "unexpected peer"

# ---------------------------------------------------------------------------
# 9. `connected` is lenient by default and strict with --require-connected.
#    (The real app reports it asymmetrically, so it must not fail a run.)
# ---------------------------------------------------------------------------
D="$WORK/connected-lenient"
python3 - "$D" <<'PY'
import json, os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def dump(name, role, other, orole, connected):
    json.dump({"self": name, "selfRole": role, "peers": [
        {"name": other, "role": orole, "hasRole": True, "connected": connected,
         "sendAllow": True, "recvAllow": True, "sendActive": True,
         "recvActive": False, "receivingAudio": False}]},
        open(os.path.join(d, name + ".json"), "w"))
dump("c1", "console", "v1", "vdi", False)
dump("v1", "vdi", "c1", "console", True)
PY
check_case connected-lenient 0 matrix-target-check
assert_contains connected-lenient "PASS"
check_case connected-lenient 1 matrix-target-check --require-connected
assert_contains connected-lenient "connected: expected true, got false"

# ---------------------------------------------------------------------------
# 10. strict field checking
# ---------------------------------------------------------------------------
D="$WORK/strict-lenient"
python3 - "$D" <<'PY'
import json, os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
# Only the four routing fields are present; the activity fields are omitted.
json.dump({"self": "c1", "selfRole": "console", "peers": [
    {"name": "v1", "role": "vdi", "connected": True, "sendAllow": True,
     "recvAllow": True}]}, open(os.path.join(d, "c1.json"), "w"))
json.dump({"self": "v1", "selfRole": "vdi", "peers": [
    {"name": "c1", "role": "console", "connected": True, "sendAllow": True,
     "recvAllow": True}]}, open(os.path.join(d, "v1.json"), "w"))
PY
check_case strict-lenient 0 matrix-target-check
assert_contains strict-lenient "PASS"
check_case strict-lenient 1 matrix-target-check --strict-fields
assert_contains strict-lenient "receivingAudio: missing (contract field)"

# A hasRole that disagrees with the scenario's advertised role is caught in
# strict mode. v1 is declared as advertising "vdi", but c1 reports hasRole=false
# while still naming the role, so only the hasRole cross-check should trip.
D="$WORK/strict-hasrole"
python3 - "$D" <<'PY'
import json, os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def dump(name, role, other, orole, hasrole):
    json.dump({"self": name, "selfRole": role, "peers": [
        {"name": other, "role": orole, "hasRole": hasrole, "connected": True,
         "sendAllow": True, "recvAllow": True, "sendActive": True,
         "recvActive": False, "receivingAudio": False}]},
        open(os.path.join(d, name + ".json"), "w"))
dump("c1", "console", "v1", "vdi", False)
dump("v1", "vdi", "c1", "console", True)
PY
check_case strict-hasrole 0 matrix-target-check
assert_contains strict-hasrole "PASS"
check_case strict-hasrole 1 matrix-target-check --strict-fields
assert_contains strict-hasrole "hasRole"

# A present-but-non-boolean activity field is always wrong.
D="$WORK/strict-bad-type"
python3 - "$D" <<'PY'
import json, os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def dump(name, role, other, orole):
    json.dump({"self": name, "selfRole": role, "peers": [
        {"name": other, "role": orole, "connected": True, "sendAllow": True,
         "recvAllow": True, "sendActive": "yes", "recvActive": False,
         "receivingAudio": False}]}, open(os.path.join(d, name + ".json"), "w"))
dump("c1", "console", "v1", "vdi")
dump("v1", "vdi", "c1", "console")
PY
check_case strict-bad-type 1 matrix-target-check
assert_contains strict-bad-type "expected a boolean"

# ---------------------------------------------------------------------------
# 11. --peers restricts the checked set (used by --count runs)
# ---------------------------------------------------------------------------
D="$WORK/subset-peers"
matrix_dump "$D" c1 console v1:vdi
matrix_dump "$D" v1 vdi c1:console
# c2/v2 are broken but excluded from this run's participant list.
write_dump "$D" c2 console c1:console:true:true
check_case subset-peers 0 matrix-2v2 --peers c1,v1 --expect matrix
assert_contains subset-peers "PASS"

# ---------------------------------------------------------------------------
# 12. CLI robustness: unknown scenario, bad config, settle timeout
# ---------------------------------------------------------------------------
check_case unknown-scenario 2 no-such-scenario

D="$WORK/not-a-dir"
check_case missing-dir 1 matrix-target-check

# settle must give up (non-zero) on a directory that never becomes correct.
D="$WORK/settle-timeout"
matrix_dump "$D" c1 console v1:vdi
: > "$D/v1.json"
set +e
START=$(date +%s)
OUT="$(python3 "$EVAL" settle --dir "$D" --scenario matrix-target-check --scenarios "$SCENARIOS" \
    --timeout 2 --interval 1 2>&1)"
RC=$?
ELAPSED=$(( $(date +%s) - START ))
set -e
if [ "$RC" -ne 1 ]; then
    FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s expected exit 1, got %s\n' settle-timeout "$RC"
else
    PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s exit %s after %ss\n' settle-timeout "$RC" "$ELAPSED"
fi
if [ "$ELAPSED" -lt 2 ]; then
    FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s returned before the timeout (%ss)\n' settle-timeout "$ELAPSED"
else
    PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s honoured the timeout\n' settle-timeout
fi
if printf '%s' "$OUT" | grep -qF "did not settle within"; then
    PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s reported the timeout\n' settle-timeout
else
    FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s did not report the timeout\n' settle-timeout
fi

# settle succeeds once the peer file becomes valid, proving it retries.
D="$WORK/settle-recovers"
matrix_dump "$D" c1 console v1:vdi
: > "$D/v1.json"
( sleep 1.5
  matrix_dump "$D" v1 vdi c1:console
) &
FIXER=$!
set +e
OUT="$(python3 "$EVAL" settle --dir "$D" --scenario matrix-target-check --scenarios "$SCENARIOS" \
    --timeout 15 --interval 0.5 2>&1)"
RC=$?
set -e
wait "$FIXER" 2>/dev/null || true
if [ "$RC" -eq 0 ] && printf '%s' "$OUT" | grep -qF "settled after"; then
    PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s recovered once the dump appeared\n' settle-recovers
else
    FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s exit %s\n' settle-recovers "$RC"
    printf '%s\n' "$OUT" | sed 's/^/       | /'
fi

# ---------------------------------------------------------------------------
# 4c. P1.7 console kind: peers must see the kind each Console advertises.
# ---------------------------------------------------------------------------
kind_dump() {  # DIR SELF SELFROLE SELFKIND C1KIND C2KIND
    local dir="$1" self="$2" selfrole="$3" selfkind="$4" k1="$5" k2="$6"
    mkdir -p "$dir"
    python3 - "$dir" "$self" "$selfrole" "$selfkind" "$k1" "$k2" <<'PY'
import json, os, sys
d, self_name, self_role, self_kind, k1, k2 = sys.argv[1:7]
def allowed(s, r):
    return s != r and "unknown" not in (s, r)
roles = {"c1": "console", "c2": "console", "v1": "vdi"}
kinds = {"c1": k1, "c2": k2, "v1": ""}
peers = []
for n, r in roles.items():
    if n == self_name:
        continue
    peers.append({"name": n, "role": r, "hasRole": True, "connected": True,
                  "kind": kinds[n],
                  "sendAllow": allowed(self_role, r), "recvAllow": allowed(r, self_role),
                  "hasAgent": False})
doc = {"self": self_name, "selfRole": self_role, "peers": peers}
if self_kind:
    doc["selfKind"] = self_kind
json.dump(doc, open(os.path.join(d, self_name + ".json"), "w"))
PY
}
D="$WORK/kind-ok"
kind_dump "$D" c1 console web "" mac
kind_dump "$D" c2 console mac web ""
kind_dump "$D" v1 vdi "" web mac
check_case kind-ok 0 console-kind
assert_contains kind-ok "PASS"

D="$WORK/kind-wrong"
kind_dump "$D" c1 console web "" mac
kind_dump "$D" c2 console mac web ""
kind_dump "$D" v1 vdi "" mac mac
check_case kind-wrong 1 console-kind
assert_contains kind-wrong 'v1 -> c1 kind: expected "web", got "mac"'

# ---------------------------------------------------------------------------
# 13. P1.5 timed control steps (evaluate.py steps). A private scenarios file
# keeps the timeouts short; the dumps are static mock files.
# ---------------------------------------------------------------------------
STEPS_SCEN="$WORK/steps-scenarios.json"
cat > "$STEPS_SCEN" <<'JSON'
{ "scenarios": {
  "dim": { "group": "g", "routing": "matrix", "peers": [
      {"name": "c1", "role": "console"}, {"name": "v2", "role": "vdi"}],
    "steps": [
      {"name": "base", "hold": 0, "timeout": 1, "expect": [
        {"peer": "c1", "of": "v2", "field": "preFaderPeakDb", "min": -16, "capture": "pre"}]},
      {"name": "solo", "hold": 0.2, "timeout": 1,
       "control": {"c1": {"solo": ["v1"], "mic": {"mode": "open"}}},
       "expect": [
        {"peer": "c1", "of": "v2", "field": "postGainPeakDb",
         "near": {"field": "preFaderPeakDb", "offset": -18, "tol": 2}},
        {"peer": "c1", "of": "v2", "field": "preFaderPeakDb", "near": {"capture": "pre", "tol": 2}},
        {"peer": "c1", "of": "v2", "field": "hearsYou", "eq": false},
        {"peer": "c1", "field": "micTransmitting", "eq": true}]},
      {"name": "merge", "hold": 0, "timeout": 1,
       "control": {"c1": {"mic": {"on": false}}},
       "expect": [{"peer": "c1", "field": "soloDimDb", "eq": -18}]}]},
  "counter": { "group": "g", "routing": "matrix", "peers": [
      {"name": "c1", "role": "console"}, {"name": "v2", "role": "vdi"}],
    "steps": [{"hold": 0.3, "timeout": 1.5, "expect": [
        {"peer": "c1", "of": "v2", "field": "packetsReceived", "increases": true}]}]},
  "badstep": { "group": "g", "routing": "matrix", "peers": [
      {"name": "c1", "role": "console"}, {"name": "v2", "role": "vdi"}],
    "steps": [{"expect": [{"peer": "c1", "field": "x", "eq": 1}], "bogus": 1}]}
}}
JSON
steps_dump() {  # DIR POST PRE
    mkdir -p "$1"
    python3 - "$1" "$2" "$3" <<'PY'
import json, os, sys
d, post, pre = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
json.dump({"self": "c1", "selfRole": "console", "micTransmitting": True, "soloDimDb": -18,
           "peers": [{"name": "v2", "role": "vdi", "hearsYou": False, "packetsReceived": 10,
                      "postGainPeakDb": post, "preFaderPeakDb": pre}]},
          open(os.path.join(d, "c1.json"), "w"))
PY
}
# run_steps NAME EXPECTED_RC SCENARIO NEEDLE
run_steps() {
    local name="$1" want="$2" scen="$3" needle="$4" out rc
    mkdir -p "$WORK/$name/ctl"
    set +e
    out="$(python3 "$EVAL" steps --dir "$WORK/$name" --control-dir "$WORK/$name/ctl" \
        --scenario "$scen" --scenarios "$STEPS_SCEN" --peers c1 --interval 0.1 2>&1)"
    rc=$?
    set -e
    LAST_OUT="$out"
    if [ "$rc" -ne "$want" ]; then
        FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s expected exit %s, got %s\n' "$name" "$want" "$rc"
        printf '%s\n' "$out" | sed 's/^/       | /'
    else
        PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s exit %s\n' "$name" "$rc"
    fi
    assert_contains "$name" "$needle"
}

steps_dump "$WORK/steps-ok" -30 -12
run_steps steps-ok 0 dim "PASS: all 3 step(s) held"
# Controls are deep-merged across steps and written for the app to poll.
if python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d=={"solo":["v1"],"mic":{"mode":"open","on":False}} else 1)' \
        "$WORK/steps-ok/ctl/c1.json"; then
    PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s control file deep-merged\n' steps-ok
else
    FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s control file: %s\n' steps-ok "$(cat "$WORK/steps-ok/ctl/c1.json")"
fi

# A cut (-100) instead of a -18 dB dim must fail and say why.
steps_dump "$WORK/steps-cut" -100 -12
run_steps steps-cut 1 dim "expected preFaderPeakDb-18 = -30.0 +/- 2"

# A packet counter that never moves fails "increases".
steps_dump "$WORK/steps-frozen" -12 -12
run_steps steps-frozen 1 counter "expected to increase from 10"

# Malformed steps are a usage error.
steps_dump "$WORK/steps-bad" -12 -12
run_steps steps-bad 2 badstep "unknown key 'bogus'"

# The stub cannot run step scenarios; run.sh refuses before starting anything.
set +e
OUT="$("$SCRIPT_DIR/run.sh" --scenario talk-gate --app "$SCRIPT_DIR/fake-peer.sh" 2>&1)"
RC=$?
set -e
if [ "$RC" -eq 2 ] && printf '%s' "$OUT" | grep -qF "need the real app"; then
    PASS_COUNT=$((PASS_COUNT + 1)); printf 'ok   %-26s stub refused (exit 2)\n' steps-stub
else
    FAIL_COUNT=$((FAIL_COUNT + 1)); printf 'FAIL %-26s exit %s\n' steps-stub "$RC"
    printf '%s\n' "$OUT" | sed 's/^/       | /'
fi

# ---------------------------------------------------------------------------
echo
echo "passed: $PASS_COUNT  failed: $FAIL_COUNT"
if [ "$FAIL_COUNT" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
