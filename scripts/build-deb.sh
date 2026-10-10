#!/usr/bin/env bash
# P2.10 -- build crosspoint_<version>_<arch>.deb for Debian 13 (trixie) inside a
# debian:trixie container, so the result does not depend on the host.
#
#   scripts/build-deb.sh                       # arch of the Docker host
#   PLATFORM=linux/amd64 scripts/build-deb.sh  # force an arch (needs a capable
#                                              # builder: native amd64 or qemu;
#                                              # a full emulated build takes hours)
#
# Output: build/deb/crosspoint_<version>_<arch>.deb (+ lintian.txt)
# Env: CROSSPOINT_DEB_VERSION overrides the version (default: <CMake version>+git<sha>).
# Uses the legacy builder (DOCKER_BUILDKIT=0): Docker 20.10 on arm64 needs it here.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BASE="$(sed -n 's/^project(SonoBus VERSION \([0-9.]*\)).*/\1/p' sonobus/CMakeLists.txt)"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
VERSION="${CROSSPOINT_DEB_VERSION:-${BASE}+git${SHA}}"
SDE="$(git log -1 --format=%ct 2>/dev/null || echo 1700000000)"
TAG="crosspoint-deb-build-$$"
OUT="$ROOT/build/deb"
mkdir -p "$OUT"

PLAT=()
[ -n "${PLATFORM:-}" ] && PLAT=(--platform "$PLATFORM")

DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-0}" docker build ${PLAT[@]+"${PLAT[@]}"} \
  -f packaging/debian/Dockerfile.build --build-arg "DEB_VERSION=$VERSION" --build-arg "SDE=$SDE" -t "$TAG" .

CID="$(docker create ${PLAT[@]+"${PLAT[@]}"} "$TAG")"
trap 'docker rm -f "$CID" >/dev/null 2>&1 || true; docker rmi "$TAG" >/dev/null 2>&1 || true' EXIT
docker cp "$CID:/out/." "$OUT/"
echo
ls -l "$OUT"
