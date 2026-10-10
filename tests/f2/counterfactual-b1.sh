#!/usr/bin/env bash
# Counterfactual for B1: show that the sink-level gate is what silences a
# role-blocked peer, not the routing decision.
#
# `matrix-mixed` must PASS: the VDI refuses the blocked peer's stream, so
# preFaderPeakDb for it is silent (-100 dB) and packetsReceived stays 0.
#
# `matrix-mixed-ungated` runs the identical routing matrix on the same binary,
# but with SONOBUS_NO_SINK_DECLINE=1 on every peer, which switches the sink gate
# off again (the AOO sink goes back to auto-accepting uninvited sources). Its
# assertions are inverted -- the blocked peer's tone must be loud again -- so it
# must FAIL. A passing run there would mean the counterfactual is not exercising
# the old behaviour and proves nothing.
#
# Usage: tests/f2/counterfactual-b1.sh
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== 1/2: matrix-mixed (sink gate ON) -- expect PASS, blocked peer silent"
if "$HERE/run.sh" --scenario matrix-mixed; then
    echo "ok   matrix-mixed passed (blocked peer's stream refused at the sink)"
    GATED_OK=1
else
    echo "FAIL matrix-mixed did not pass"
    GATED_OK=0
fi

echo
echo "=== 2/2: matrix-mixed-ungated (sink gate OFF) -- expect FAIL on a blocked peer"
# The pre-fix behaviour is not itself perfectly deterministic: which blocked
# direction shows the decoded tone depends on message ordering between the peers,
# so the ungated scenario can occasionally pass. That does NOT weaken the
# counterfactual -- the point is that the ungated build *can* decode a blocked
# peer while the gated build never does -- so allow a few attempts and require
# the B1 failure to appear in one of them.
UNGATED_OK=0
for attempt in 1 2 3; do
    OUT="$("$HERE/run.sh" --scenario matrix-mixed-ungated 2>&1)"
    RC=$?
    if [ "$RC" -ne 0 ] && printf '%s' "$OUT" | grep -qE 'FAIL (c1|v1) -> u1 preFaderPeakDb = -[0-9.]+: expected <= -60'; then
        echo "ok   ungated attempt $attempt failed exactly as B1: a blocked peer's preFaderPeakDb is loud again"
        printf '%s\n' "$OUT" | grep -E 'FAIL (c1|v1) -> u1 preFaderPeakDb' | sed 's/^/     /'
        printf '%s\n' "$OUT" | grep -E 'packetsReceived' | sed 's/^/     /'
        UNGATED_OK=1
        break
    fi
    echo "     ungated attempt $attempt did not reproduce the loud-tone failure (rc=$RC)"
done
if [ "$UNGATED_OK" -eq 0 ]; then
    echo "FAIL the ungated build never reproduced B1 in 3 attempts: the counterfactual is not exercising the old behaviour"
fi

echo
if [ "$GATED_OK" -eq 1 ] && [ "$UNGATED_OK" -eq 1 ]; then
    echo "RESULT: PASS (gated silent, ungated decodes -- the sink gate is the fix)"
    exit 0
fi
echo "RESULT: FAIL"
exit 1
