# Crosspoint web Console container (P7.1): the native engine running headless
# in Console role + the WebRTC audio gateway (P7.3), one PulseAudio, one user.
#
#   DOCKER_BUILDKIT=0 docker build -f docker/web-console/Dockerfile -t crosspoint-web-console .
#
# The build context MUST be the monorepo root (sonobus/CMakeLists.txt resolves
# ../juce and ../aoo). See docker/web-console/README.md.

# ==============================================================================
# Stage 1: compile the Linux standalone engine
# ==============================================================================
FROM debian:trixie AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config ca-certificates \
        libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
        libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
        libxinerama-dev libxrandr-dev libxrender-dev libopus-dev \
    && rm -rf /var/lib/apt/lists/*
# JUCE_WEB_BROWSER=0 and JUCE_USE_CURL=0 in sonobus/CMakeLists.txt: no webkit/curl.
WORKDIR /src
COPY juce/    /src/juce/
COPY aoo/     /src/aoo/
COPY sonobus/ /src/sonobus/
# sonobus/CMakeLists.txt references ../design/icon/*.png (bundle icons).
COPY design/icon/ /src/design/icon/
RUN cmake -S /src/sonobus -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /src/build --target SonoBus_Standalone -j"$(nproc)" \
 && test -x /src/build/SonoBus_artefacts/Release/Standalone/sonobus

# ==============================================================================
# Stage 2: runtime = gateway runtime (PulseAudio + GStreamer + Python) + engine
# ==============================================================================
FROM debian:trixie AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        pulseaudio pulseaudio-utils \
        python3 python3-gi python3-aiohttp \
        gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 gir1.2-gst-plugins-bad-1.0 \
        gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad gstreamer1.0-nice gstreamer1.0-pulseaudio \
        ca-certificates tini \
        libasound2t64 libasound2-plugins libfreetype6 libfontconfig1 libx11-6 \
        libxcomposite1 libxcursor1 libxext6 libxinerama1 libxrandr2 libxrender1 \
        libopus0 \
    && rm -rf /var/lib/apt/lists/*

# ALSA -> Pulse: the engine (JUCE/ALSA) talks to the same Pulse server as the gateway.
RUN printf 'pcm.!default { type pulse }\nctl.!default { type pulse }\n' > /etc/asound.conf

RUN useradd -m -u 1000 console && mkdir -p /config /opt/crosspoint/bin /app \
 && chown console /config
COPY --from=builder /src/build/SonoBus_artefacts/Release/Standalone/sonobus /opt/crosspoint/bin/crosspoint
COPY console-ui/ /opt/crosspoint/ui/
COPY docker/web-console/gateway/gateway.py docker/web-console/gateway/test.html \
     docker/web-console/gateway/entrypoint.sh /app/
COPY docker/web-console/supervisor.sh /app/supervisor.sh
RUN chmod +x /app/entrypoint.sh /app/supervisor.sh /opt/crosspoint/bin/crosspoint

USER console
WORKDIR /app
ENV XDG_RUNTIME_DIR=/tmp/runtime HTTP_PORT=8090 RTC_UDP_MIN=40000 RTC_UDP_MAX=40019 \
    CROSSPOINT_CONFIG=/config/console.yaml CROSSPOINT_API_PORT=7070
EXPOSE 7070/tcp 8090/tcp 40000-40019/udp
# tini reaps zombies and forwards SIGTERM; entrypoint.sh starts PulseAudio then
# execs the supervisor, which runs the gateway and the engine.
ENTRYPOINT ["/usr/bin/tini", "--", "/app/entrypoint.sh"]
CMD ["/app/supervisor.sh"]
