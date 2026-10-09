# aooserver in a container (P8.1)

The headless AOO rendezvous server. Peers use it only to find each other;
**audio never passes through it**. It is built from `aooserver/`'s own vendored
AOO (`aooserver/deps/aoo`), not the top-level `aoo/` (see the top-level README:
only that snapshot has the IP blocklist).

Image: multi-stage, build on `debian:trixie`, run on `debian:trixie-slim` as the
non-root user `aoo`; only `libcurl4t64`, `libstdc++6` and CA certificates at runtime.

## Build

The build context is the `aooserver/` directory (not the repo root), which keeps
it at ~29 MB and avoids sending `build/` or the JUCE trees:

    docker build -f docker/aooserver/Dockerfile -t crosspoint-aooserver aooserver

On an arm64 Mac this gives an arm64 image. For an x86_64 host, add
`--platform linux/amd64` (needs a builder that supports it). If a remote buildx
builder is active, use `DOCKER_BUILDKIT=0 docker build ...` or
`docker buildx use desktop-linux` to build locally.

## Run

    docker run -d --name crosspoint-aooserver --restart unless-stopped \
      -p 10998:10998/tcp -p 10998:10998/udp \
      -v aooserver-logs:/var/log/aooserver \
      crosspoint-aooserver

or `docker compose up -d --build` in this directory. Both publish **TCP and UDP**.

- `AOO_PORT` (default `10998`): port inside the container (TCP and UDP). If you
  change it, publish the same port.
- `AOO_BLOCKLIST`: path to a blocklist file inside the container (one IP per
  line, `1.2.3.4,public` to still allow private groups). Mount it read-only.
- Logs: timestamped files in the `/var/log/aooserver` volume, and also stdout
  (`docker logs`). Look for the `ServerStart,<port>` line.
- Health: the image's `HEALTHCHECK` does a TCP connect to the port with bash's
  `/dev/tcp`; `docker ps` shows `(healthy)`.

### Binding to the VPN address only

Publish on the host's VPN IP so the server is not exposed on other interfaces:

    -p 10.x.y.z:10998:10998/tcp -p 10.x.y.z:10998:10998/udp

(see the comment in `docker-compose.yml`).

## Firewall

Open **TCP and UDP 10998** (or your `AOO_PORT`) from the VPN subnet to the host.
Clients use both; a TCP-only rule leaves them unable to finish connecting.

## Pointing clients at it

- Console / CLI: `--server host:10998` (e.g. `Crosspoint --server 10.x.y.z:10998`).
- VDI agent YAML (key added by P2.1):

      server: 10.x.y.z:10998

- Quick check from a client machine: `nc -vz 10.x.y.z 10998` (TCP).

## Why this puts peers on the VPN

A peer reports as its "local" address the interface it used to reach the server
(`aoo/lib/src/client.cpp`). With the public `aoo.sonobus.net` that is the
LAN/internet path, so peers may connect over the internet or fail behind
corporate NAT. With a server on a VPN address, the interface used to reach it
is the VPN interface, so every peer announces its VPN address and the
peer-to-peer audio stays inside the VPN (ROADMAP D2, P8, "Known risks").
