# P2.11 test image: Debian 13 (trixie) + PipeWire + a build of the Crosspoint
# Linux standalone. Context MUST be the monorepo root:
#   docker build -f tests/linux/Dockerfile -t crosspoint-pw-test .
# (tests/linux/pipewire-pin.sh does this for you.)

# --- stage 1: toolchain, build deps and the PipeWire stack (cacheable) --------
FROM debian:trixie AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config ca-certificates \
        libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev \
        libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
        libxrandr-dev libxrender-dev libopus-dev \
        pipewire pipewire-bin pipewire-pulse wireplumber pipewire-alsa \
        pulseaudio-utils alsa-utils dbus dbus-x11 jq python3 xvfb procps \
    && rm -rf /var/lib/apt/lists/*

# --- stage 2: compile the app ---------------------------------------------------
FROM base AS build
WORKDIR /src
COPY juce/    /src/juce/
COPY aoo/     /src/aoo/
COPY design/icon/ /src/design/icon/
COPY sonobus/ /src/sonobus/
RUN cmake -S /src/sonobus -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /src/build --target SonoBus_Standalone -j"$(nproc)" \
 && ls /src/build/SonoBus_artefacts/Release/Standalone/

# --- stage 3: runtime for the test ---------------------------------------------
FROM base AS test
COPY --from=build /src/build/SonoBus_artefacts/Release/Standalone/ /opt/crosspoint/
COPY tests/linux/container-test.sh /opt/container-test.sh
ENTRYPOINT ["/bin/bash", "/opt/container-test.sh"]
