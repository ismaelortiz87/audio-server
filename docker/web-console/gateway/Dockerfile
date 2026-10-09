# WebRTC Opus audio gateway sidecar (P7.3).
#
#   docker build -t crosspoint-rtc-gateway docker/web-console/gateway
#   docker run --rm -e RTC_PUBLIC_IP=127.0.0.1 -p 8090:8090 \
#       -p 127.0.0.1:40000-40019:40000-40019/udp crosspoint-rtc-gateway
#   open http://127.0.0.1:8090/rtc/test
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        pulseaudio pulseaudio-utils \
        python3 python3-gi python3-aiohttp \
        gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 gir1.2-gst-plugins-bad-1.0 \
        gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad gstreamer1.0-nice gstreamer1.0-pulseaudio \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Everything (PulseAudio, gateway, and later the engine) runs as one
# unprivileged user so they share one PulseAudio socket.
RUN useradd -m -u 1000 console
WORKDIR /app
COPY gateway.py test.html entrypoint.sh /app/
RUN chmod +x /app/entrypoint.sh
USER console
ENV XDG_RUNTIME_DIR=/tmp/runtime HTTP_PORT=8090 RTC_UDP_MIN=40000 RTC_UDP_MAX=40019
EXPOSE 8090/tcp 40000-40019/udp
ENTRYPOINT ["/app/entrypoint.sh"]
