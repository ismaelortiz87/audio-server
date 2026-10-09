# Self-hosted AOO connection server (aooserver) -- P8.1.
#
# Build context is the aooserver/ directory (small: ~29 MB, no build/ or JUCE
# app trees), so this works with BuildKit and the legacy builder alike:
#
#   docker build -f docker/aooserver/Dockerfile -t crosspoint-aooserver aooserver
#
# The server is built from aooserver/'s OWN vendored AOO (deps/aoo), not the
# top-level aoo/ -- only that snapshot has the server-side IP blocklist.

# ---- build ----------------------------------------------------------------
FROM debian:trixie AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config libcurl4-openssl-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . /src
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /build \
 && strip /build/aooserver

# ---- runtime --------------------------------------------------------------
FROM debian:trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libcurl4t64 libstdc++6 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10998 --home-dir /nonexistent --shell /usr/sbin/nologin aoo \
 && mkdir -p /var/log/aooserver && chown aoo:aoo /var/log/aooserver
COPY --from=build /build/aooserver /usr/local/bin/aooserver

ENV AOO_PORT=10998
# Optional: path (inside the container) to an IP blocklist file, one address per
# line ("1.2.3.4" or "1.2.3.4,public"). Mount it and set AOO_BLOCKLIST.
ENV AOO_BLOCKLIST=""
USER aoo
VOLUME /var/log/aooserver
EXPOSE 10998/tcp 10998/udp

# TCP connect to the listening port using bash's /dev/tcp (no extra tools).
HEALTHCHECK --interval=15s --timeout=3s --start-period=5s --retries=3 \
  CMD ["bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/${AOO_PORT:-10998}"]

ENTRYPOINT ["/bin/sh", "-c", "exec aooserver -p \"${AOO_PORT:-10998}\" -l /var/log/aooserver ${AOO_BLOCKLIST:+-b \"$AOO_BLOCKLIST\"}"]
