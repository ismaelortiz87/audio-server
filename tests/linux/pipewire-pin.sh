#!/usr/bin/env bash
# P2.11 -- Linux PipeWire device pinning test.
#
# Builds tests/linux/Dockerfile (debian:trixie + PipeWire + a Linux build of the
# standalone; the first build takes ~5-10 min, later ones reuse cached layers),
# runs it, and prints PASS / FAIL. The checks live in container-test.sh.
#
#   tests/linux/pipewire-pin.sh            build + run
#   tests/linux/pipewire-pin.sh --no-build run the last built image again
#
# Uses the legacy builder (DOCKER_BUILDKIT=0) by default: Docker 20.10 on arm64
# needs it here. Override with DOCKER_BUILDKIT=1 if you prefer BuildKit.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="${CROSSPOINT_PW_IMAGE:-crosspoint-pw-test}"

if [ "${1:-}" != "--no-build" ]; then
  DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-0}" docker build -f "$ROOT/tests/linux/Dockerfile" -t "$IMAGE" "$ROOT" || {
    echo "FAIL: docker build failed"; exit 1; }
fi

# --init reaps the PipeWire daemons; no privileges or sound devices are needed.
docker run --rm --init "$IMAGE"
rc=$?
if [ "$rc" -eq 0 ]; then echo "PASS: tests/linux/pipewire-pin.sh"; else echo "FAIL: tests/linux/pipewire-pin.sh (exit $rc)"; fi
exit "$rc"
