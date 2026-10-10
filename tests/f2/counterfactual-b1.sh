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
echo "=== 2/2: matrix-mixed-ungated (sink gate OFF) -- expect FAIL on the blocked peer"
OUT="$("$HERE/run.sh" --scenario matrix-mixed-ungated 2>&1)"
RC=$?
printf '%s\n' "$OUT" | grep -E "^(step|    (ok|FAIL))" | sed 's/^/    /'

# The ungated run must fail, and it must fail because the blocked peer's tone is
# decoded again -- not for some unrelated reason.
if [ "$RC" -eq 0 ]; then
    echo "FAIL the ungated counterfactual passed: it no longer reproduces B1"
    UNGATED_OK=0
elif printf '%s' "$OUT" | grep -qE 'FAIL v1 -> u1 preFaderPeakDb = -[0-9.]+: expected >= -16'; then
    echo "ok   ungated run failed exactly as B1: v1 -> u1 preFaderPeakDb is loud again"
    UNGATED_OK=1
else
    echo "FAIL the ungated run failed, but not on the blocked peer's pre-fader level"
    UNGATED_OK=0
fi

echo
if [ "$GATED_OK" -eq 1 ] && [ "$UNGATED_OK" -eq 1 ]; then
    echo "RESULT: PASS (gated silent, ungated decodes -- the sink gate is the fix)"
    exit 0
fi
echo "RESULT: FAIL"
exit 1
