# SonoBus in the browser — single multi-stage build.
#
#   docker build -f docker/Dockerfile -t sonobus-web .
#   docker run --rm -p 6080:6080 sonobus-web
#   open http://localhost:6080/vnc.html
#
# Context MUST be the monorepo root: sonobus/CMakeLists.txt resolves ../juce
# and ../aoo, so juce/, aoo/ and sonobus/ all have to be present.

# ==============================================================================
# Stage 1 — compile the native Linux standalone
# ==============================================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        ca-certificates \
        libasound2-dev \
        libjack-jackd2-dev \
        libfreetype6-dev \
        libfontconfig1-dev \
        libx11-dev \
        libxcomposite-dev \
        libxcursor-dev \
        libxext-dev \
        libxinerama-dev \
        libxrandr-dev \
        libxrender-dev \
        libopus-dev \
    && rm -rf /var/lib/apt/lists/*
# No webkit2gtk / libcurl: sonobus/CMakeLists.txt sets JUCE_WEB_BROWSER=0 and
# JUCE_USE_CURL=0.

WORKDIR /src
COPY juce/    /src/juce/
COPY aoo/     /src/aoo/
COPY sonobus/ /src/sonobus/

RUN cmake -S /src/sonobus -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /src/build --target SonoBus_Standalone -j"$(nproc)" \
 && test -x /src/build/SonoBus_artefacts/Release/Standalone/sonobus

# ==============================================================================
# Stage 2 — virtual display + VNC bridge + audio plumbing
# ==============================================================================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        xvfb \
        x11vnc \
        novnc \
        x11-utils \
        imagemagick \
        pulseaudio \
        pulseaudio-utils \
        libasound2-plugins \
        alsa-utils \
        python3 \
        python3-aiohttp \
        libasound2 \
        libfreetype6 \
        libfontconfig1 \
        libx11-6 \
        libxcomposite1 \
        libxcursor1 \
        libxext6 \
        libxinerama1 \
        libxrandr2 \
        libxrender1 \
        libopus0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/SonoBus_artefacts/Release/Standalone/sonobus /app/sonobus/sonobus
RUN chmod +x /app/sonobus/sonobus

# Audio bridge (WebSocket <-> PulseAudio) and its browser client.
COPY docker/bridge/ /app/bridge/
COPY docker/web/ /app/web/

ENV DISPLAY=:99
ENV SCREEN_GEOMETRY=1600x1000x24

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

EXPOSE 6080
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
