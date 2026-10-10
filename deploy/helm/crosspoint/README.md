# Crosspoint Helm Chart

Single chart deploying the two parts of the Crosspoint (SonoBus) audio stack:

| Component | What it is | One-per | Notes |
|-----------|-----------|---------|-------|
| `aooserver` | Headless AOO connection server (rendezvous only; audio stays peer-to-peer). TCP+UDP on the same port (10998). | cluster (shared) | VDI peers connect to it, not to each other. |
| `console`   | One Web Console: native engine (headless, `role: console`) + WebRTC audio gateway, one PulseAudio. | **user** | The UI is where a user signs in; VDI peers dial the AOO group. |

Both components are enabled by default and each is independently toggleable via values (`aooserver.enabled`, `console.enabled`) or an overlay file (see below).

This chart follows the LGC helm standards: `values.yaml` lives in git, optional secrets are an Ansible-vault-encrypted `secrets.yml` overlay, and `install.sh` is the operational entrypoint.

---

## Layout

```
crosspoint/
├── Chart.yaml
├── values.yaml            # in git; everything non-secret
├── secrets.yml            # OPTIONAL — vault-encrypted (NOT in git); see "Secrets"
├── install.sh             # entrypoint (LGC standard)
└── templates/
    ├── _helpers.tpl
    ├── aooserver-deployment.yaml
    ├── aooserver-service.yaml      # none / nodePort / loadBalancer
    ├── aooserver-pvc.yaml          # only if persistence.enabled
    ├── console-deployment.yaml     # hostNetwork by default
    ├── console-service.yaml        # nodePort / ingress / none
    ├── rtc-udp-service.yaml        # only if hostNetwork: false
    ├── console-configmap.yaml      # ConfigMap, or Secret when a group password is set
    ├── console-secret.yaml         # api token (only when a token is provided)
    ├── ingress.yaml                # only if exposure == ingress
    └── NOTES.txt
```

---

## Quick start

```bash
cd deploy/helm/crosspoint
./install.sh
```

The script prints the kubectl context it will act on, a config summary, and asks
for confirmation. Defaults: namespace `crosspoint`, release `crosspoint`,
domain `crosspoint.app.lagreca.io`, both components enabled, console on
`hostNetwork` (WebRTC media binds on the node).

**Requirements:** `kubectl` and `helm` on `PATH`. **Recommended:**
`yq` (mikefarah) — the script then prints a fully merged config summary (values
+ profile + secrets overlays) and runs a pre-flight check of the fixed
NodePorts against the cluster `nodePortRange` (hard-fail if out of range).
Without `yq` it still works, but the summary shows chart defaults only and the
port pre-flight is skipped (the cluster validates the values at install time).

### Environment variables

| Variable | Default | Meaning |
|----------|---------|---------|
| `NAMESPACE` | `crosspoint` | Target namespace (created if missing). |
| `HELM_RELEASE_NAME` | `crosspoint` | Helm release name (becomes the in-cluster DNS prefix). |
| `VALUES_FILE` | `values.yaml` | Primary values file, relative to the chart dir. |
| `SECRETS_FILE` | `secrets.yml` | Optional secrets overlay, relative to the chart dir. |
| `PROFILE` | *(unset)* | Optional per-user overlay file name (e.g. `alice.yml`), relative to the chart dir. Applied *after* `values.yaml`, *before* `secrets.yml`. |
| `DOMAIN` | `crosspoint.app.lagreca.io` | Public URL shown in output (external nginx owns TLS on it). |
| `AUTO_APPROVE` | *(unset)* | `yes`/`true`/`1` skips the interactive yes/no prompt. |
| `SKIP_WAIT` | *(unset)* | `1` skips `helm --wait` (use when the images aren't pushed yet). |

### One release per user

A console pod binds the WebRTC media range on its node, and two consoles with
the same AOO identity on the same node would clash. The standard pattern is one
release **per user**, each with a per-user overlay that gives it a disjoint UDP
range and its own group:

```bash
# alice.yml (chart dir):
#   console:
#     config: { group: alice, username: alice }
#     rtcUdpMin: 40020
#     rtcUdpMax: 40039
#     rtcPublicIP: 192.168.70.100
#
NAMESPACE=users HELM_RELEASE_NAME=alice PROFILE=alice.yml ./install.sh
```

(Or, with a shared namespace, just different release names + profiles.)

---

## Secrets

No `secrets.yml` is shipped (per LGC note 8 — don't force a secrets file if the
app has none). Create one from the lgc-onpremises-iac repo:

```bash
# in lgc-onpremises-iac/kubernetes/helm/
./vault-values.sh <path-to-crosspoint>   # creates an empty encrypted secrets.yml
```

Then fill it with any of the keys below and encrypt it in place:

```bash
ansible-vault encrypt crosspoint/secrets.yml
```

Recognized overlay keys:

```yaml
secrets:
  apiToken: "..."      # engine control-API token -> console apiToken (REQUIRED;
                       # the container exits 64 without it)
  groupPassword: "..." # AOO group password -> console.config.password
```

Behavior:

- **No group password** → `console.yaml` is rendered into a **ConfigMap**.
- **Group password present** (either `secrets.groupPassword` or
  `console.config.password` in values) → the full `console.yaml` is rendered
  into a **Secret** instead, mounted `0440` with `fsGroup: 1000` so only the
  `console` user (uid 1000) can read it.
- **`apiToken` / `secrets.apiToken`** → rendered into an `Opaque` Secret and
  exposed to the engine as `CROSSPOINT_API_TOKEN` (the sign-in credential).

`install.sh` detects the `$ANSIBLE_VAULT;` header, decrypts with
`ansible-vault view --output` into a `0600` temp file (honoring
`ANSIBLE_VAULT_PASSWORD_FILE`), and passes that as the second `--values` file.
The encrypted file is left untouched.

---

## Values reference

### Global

| Key | Default | Notes |
|-----|---------|-------|
| `imagePullSecrets` | `[]` | e.g. `[{name: dockerhub-maelops}]` if the registry is private. |
| `commonLabels` | `{}` | Added to every resource's labels. |
| `nameOverride` / `fullnameOverride` | `""` | Standard helm name overrides. |

### `aooserver`

| Key | Default | Notes |
|-----|---------|-------|
| `enabled` | `true` | |
| `image.repository` / `image.tag` / `image.pullPolicy` | `maelops/crosspoint-aooserver` / `2026.10` / `IfNotPresent` | |
| `port` | `10998` | TCP (login/groups) and UDP (hole-punching) share this number. |
| `exposure` | `nodePort` | `none` (ClusterIP only), `nodePort`, or `loadBalancer` (MetalLB). |
| `nodePort` | `31998` | For `exposure: nodePort`. Must be inside the apiserver `nodePortRange`. `install.sh` checks this and hard-fails otherwise. |
| `loadBalancerIP` | `""` | For `exposure: loadBalancer`. |
| `blocklist.enabled` / `blocklist.mount` | `false` / `{}` | Mount a file and it is passed to `aooserver -b` (one address per line; `1.2.3.4,public` allows that address in private groups). |
| `persistence.enabled` / `existingClaim` / `storageClass` / `size` | `false` / `""` / `nfs-truenas` / `1Gi` | The image ships a `VOLUME` for logs; default is `emptyDir`. |
| `resources` | 100m/64Mi → 1/256Mi | |
| `podSecurityContext` | uid/gid/fsGroup `10998`, nonRoot | The image runs as the `aoo` user. |
| `securityContext` | drop `ALL` caps | |
| `nodeSelector` / `tolerations` / `affinity` | `{}` / `[]` / `{}` | |
| `livenessProbe` / `readinessProbe` | `tcpSocket` on port `aoo` | The port is the server's only interface; the image's HEALTHCHECK does the same TCP connect. |

### `console`

| Key | Default | Notes |
|-----|---------|-------|
| `enabled` | `true` | |
| `image.repository` / `image.tag` / `image.pullPolicy` | `maelops/crosspoint-web-console` / `2026.10` / `IfNotPresent` | |
| `replicaCount` | `1` | |
| `strategy.type` | `Recreate` | PulseAudio socket state is per-pod; a rolling update would briefly run two consoles with the same AOO identity. |
| `apiPort` | `7070` | Engine control API (UI + `/api/` + WS). |
| `rtcHttpPort` | `8090` | WebRTC signalling (gateway). |
| `apiOrigin` | `https://crosspoint.app.lagreca.io` | The one allowed browser origin (CORS/WS). Must equal the public URL. |
| `config.server` | `""` | Empty ⇒ auto-filled with `<release>-aooserver.<ns>.svc.cluster.local:<port>` when `aooserver.enabled`. |
| `config.group` / `config.username` | `studio-a` / `console` | Your AOO group; username is what VDI peers see. |
| `config.password` | `""` | Prefer `secrets.groupPassword` in the overlay; setting it here switches the config to a Secret (see Secrets). |
| `config.audio.sample_rate` / `.buffer` | `48000` / `256` | Must match the gateway's null sinks. |
| `config.codec` / `config.bitrate` | `opus` / `128000` | |
| `apiToken` | `""` | Engine control-API token (the sign-in credential). Prefer `secrets.apiToken`; **required** — the container exits 64 without it. |
| `hostNetwork` | `true` | See "WebRTC media & firewall" below — this is the only exposure that works on a cluster whose `nodePortRange` excludes the media ports (e.g. the LGC lacreca cluster: default `30000-32767`). |
| `dnsPolicy` | `ClusterFirstWithHostNet` | Keeps service DNS working under `hostNetwork`. |
| `rtcUdpMin` / `rtcUdpMax` | `40000` / `40019` | The range the gateway **binds and advertises as-is**. The browser dials exactly these numbers, so they cannot be remapped. |
| `rtcUdpNodePortMin` / `rtcUdpNodePortMax` | `32600` / `32619` | Only used when `hostNetwork: false` (the UDP NodePort service's service ports — which the browser dials instead). |
| `rtcPublicIP` | `192.168.70.100` | Address the browser reaches for media; the gateway rewrites its host candidate to it. Empty ⇒ autodetect (all interfaces advertised). |
| `exposure` | `nodePort` | `ingress`, `nodePort`, or `none`. |
| `nodePort` / `rtcNodePort` | `32100` / `32101` | For `exposure: nodePort`; the external nginx is pointed at `<node-ip>:32100` (UI/api) and `:32101` (`/rtc/`). |
| `ingress.className` / `ingress.host` | `traefik` / `crosspoint.app.lagreca.io` | Only rendered when `exposure: ingress`. |
| `ingress.paths` | `/`→engine, `/api/`→engine, `/rtc/`→rtc | The UI fetches all of them from the same origin. |
| `ingress.annotations` | `{}` | **No TLS block, no cert-manager** — the external nginx terminates TLS in front of traefik; the cluster has no cert-manager. |
| `resources` | 500m/1Gi → 4/4Gi | PulseAudio + GStreamer + engine. |
| `podSecurityContext` | uid/gid/fsGroup `1000`, nonRoot | `fsGroup 1000` lets the `console` user read the `0440` `console.yaml` Secret. |
| `securityContext` | drop `ALL` caps | |
| `nodeSelector` / `tolerations` / `affinity` | `{}` / `[]` / `{}` | Use `affinity`/`nodeSelector` to spread per-user releases across nodes (media ranges must be disjoint per node). |
| `livenessProbe` / `readinessProbe` | `httpGet /api/v1/health` on `engine` | Unauthenticated endpoint, 200 as soon as the process is up. If either the engine or the gateway dies, `supervisor.sh` exits the container and the pod restarts. |

---

## WebRTC media & firewall

Media is **never proxied**. The gateway (port 8090) hands the browser an
ICE/WebRTC answer whose host candidate is `rtcPublicIP`, and the browser then
dials the UDP range `rtcUdpMin..rtcUdpMax` **directly**.

**Default: `hostNetwork: true`.** The console pod uses the node's network
namespace, so the media ports bind directly on the node — the kubernetes
equivalent of the compose file's 1:1 host publish. Reachability:

- media: `<node-ip>:40000-40019` (the pod's own bind; no service involved)
- UI/api + `/rtc/`: external nginx → `<node-ip>:7070` / `:8090`
  (or the nodePort service `32100` / `32101`), or in-cluster via the ClusterIP
  service / traefik Ingress

Consequences to keep in mind:

- The media range must be free on the node and allowed through any firewall.
- Two console releases on the same node need **disjoint** `rtcUdp` ranges
  (each engine binds `0.0.0.0`), or schedule them on different nodes.
- `rtcPublicIP` should be the node IP the pod lands on, or a VIP that routes
  there (e.g. the LB `192.168.70.100`).

**`hostNetwork: false`** keeps the pod on the cluster network and publishes the
media via a UDP NodePort service. But a NodePort service's *service ports* must
sit inside the apiserver `nodePortRange` (default `30000-32767`), while the
browser dials the *advertised* ports (`rtcUdpNodePortMin..Max` = `32600-32619`
by default, which the gateway does *not* bind). This only works if you
reconfigure the apiserver (`-service-node-port-range`) to include the range the
gateway binds, or reconfigure the gateway to advertise a different range. On
the LGC lacreca cluster (no `nodePortRange` override) this path is
broken-by-default — which is why `hostNetwork: true` is the default.
`install.sh` warns (not fails) when `hostNetwork: false` + the media range is
outside `nodePortRange`.

---

## Images

The chart references:

- `maelops/crosspoint-aooserver:2026.10`
- `maelops/crosspoint-web-console:2026.10`

Build from the repo root (Dockerfiles in `docker/`; see their build-context
notes):

```bash
cd <repo-root>

docker build -f docker/aooserver/Dockerfile \
    -t maelops/crosspoint-aooserver:2026.10 aooserver/

docker build -f docker/web-console/Dockerfile \
    -t maelops/crosspoint-web-console:2026.10 .

docker push maelops/crosspoint-aooserver:2026.10
docker push maelops/crosspoint-web-console:2026.10
```

> **Status:** as of this writing neither image has been pushed to any registry,
> so a live deploy is not possible yet. `install.sh SKIP_WAIT=1` can be used to
> pre-flight the wiring without waiting on pods; the `ImagePullBackOff` check
> will flag the missing images.

If the `maelops` Docker Hub namespace is private, add an `imagePullSecrets`
entry to values (or the overlay).

---

## Validating without a cluster

```bash
helm lint deploy/helm/crosspoint
helm template crosspoint deploy/helm/crosspoint -n crosspoint
helm template crosspoint deploy/helm/crosspoint -n crosspoint --set aooserver.enabled=false
helm template crosspoint deploy/helm/crosspoint -n crosspoint --set console.enabled=false
```

---

## Operations

```bash
kubectl -n crosspoint get pods,svc,ing
kubectl -n crosspoint logs crosspoint-console -f        # engine + gateway, one stream
kubectl -n crosspoint logs crosspoint-aooserver -f
# show the sign-in token:
kubectl -n crosspoint get secret crosspoint-console-secret \
    -o jsonpath='{.data.api-token}' | base64 -d
```

Uninstall:

```bash
helm uninstall crosspoint -n crosspoint
```
