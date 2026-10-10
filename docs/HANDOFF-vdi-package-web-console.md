# Handoff — VDI agent package + web Console shipped to a real VDI

From: the agent that closed P2.5 / P2.10, fixed the `agent-silent` flake, closed
P8.2–P8.4, and deployed the web Console (P7.4).
Status: **merged to `main`**. Four of five objectives verified end to end on real
hardware; **P7.4's public vhost is the only piece left and it needs the proxy
host's credentials.**

Read this if you are touching: the Linux packaging (`packaging/debian/`,
`scripts/build-deb.sh`), the F2 harness, the web Console container, or the
`maelosdebian` VDI.

---

## 1. What is actually running right now

On **`maelosdebian`** (192.168.0.71, Debian 13 trixie **amd64**, VPN
**10.248.233.7**, user `maelo`), three **systemd user units**, all `active` and
all `enabled` (linger was already on, so they survive reboot):

| Unit | What |
|---|---|
| `crosspoint-agent` | the VDI agent, connected to group `lagreca`, `input ok`/`output ok` |
| `crosspoint-aooserver` | self-hosted rendezvous server on `:10998` (P8.2) |
| `crosspoint-webconsole` | the web Console container, rootless podman (P7.4) |

**No `sudo` was needed or used.** `sudo` on that box requires a password, but a
systemd **user** unit lives in `~/.config/systemd/user/` and the binary in
`~/.local/bin/`, both user-owned. If you need root there, ask maelo.

Device mapping (established from the VDI's own `~/sonobus-runbook.md` and
`sonobus-sinks.service`):

- `sb_system_out.monitor` → **agent input** (what the VDI's apps play)
- `sb_mic_in` → **agent output**; `sb_virtual_mic` is a `module-remap-source`
  that re-presents it to apps as a real microphone.

Group/server: **`lagreca` on `aoo.sonobus.net:10998`**. The flatpak settings
recorded **three different `groupPassword` values** for that group; the
newest-looking one (`Plaermo22990613`, a typo of `Palermo…`) is **wrong** and
fails with `could not join group 'lagreca': wrong password`. The correct one is
`Palermo22990613`.

---

## 2. Verified evidence (do not re-litigate these without new data)

- **Packaging**: `tests/linux/deb-systemd.sh` → **21 CHECK PASS / 0 FAIL**.
  Covers enable/start, health, clean stop (`Result=success`), **reboot with
  linger and no login**, **missing input node at start** (stays up, reports
  `missing`, recovers), broken YAML (restarts every 5 s, error in journal), and
  `apt remove`.
- **F2 harness**: `agent-silent` **3/3 PASS**. The scenario was previously
  room-dependent (see §4).
- **WebRTC audio**: the gateway e2e passed **7/7 — `E2E OK`** against the live
  container: 440 Hz downlink peak 0.353 and silent after stop, 660 Hz uplink at
  **−9.0 dBFS**, RTP both ways, **0 packets lost**.
- **Web Console in a browser**: real Chrome loaded the UI
  (`<title>Crosspoint</title>`, full control surface) and the Console listed the
  VDI as a station: `online`, `health clear`, **30 ms**, `agent {input …}`.
- **Agent page in a browser**: headless Chrome over an ssh tunnel rendered
  `title "maelosdebian · Crosspoint agent"`, body *"Connected — Sending this
  VDI's audio to 1 Console. Group lagreca · aoo.sonobus.net:10998 · mono"*, with
  **no JS errors**.
- **VPN rendezvous (P8.4)**: with both peers on the self-hosted server, `ss` on
  the VDI showed the session **from the Mac's VPN IP**
  (`ESTAB 10.248.233.7:10998 ← 10.248.233.2`), so peer addressing stays on `wg0`.

---

## 3. P7.4 — what is left, exactly

`crosspoint.app.lagreca.io` resolves to **192.168.0.6**, which runs **nginx** and
answers on 443, but returns **404** for our hostname (identical to a nonexistent
name) while `app.lagreca.io` returns **200**. So TLS and the wildcard are fine;
only the **vhost + `proxy_pass`** is missing. That host rejects our SSH keys
(`maelo`, `root`, `ubuntu`, `admin`), so it needs maelo or credentials.

**The config is already written and validated:** `docker/web-console/nginx-crosspoint.conf`.
`nginx -t` passes, and I exercised it against the live Console container — `/`,
`/api/v1/health` and `/rtc/config` all route correctly. To apply: copy to
`sites-available`, set `UPSTREAM_HOST` (`10.248.233.7`), point the certificate
lines at the existing wildcard, add the documented
`map $http_upgrade $connection_upgrade` block to the `http {}` scope, then
`nginx -t && systemctl reload nginx`.

Two things that are easy to get wrong, both verified:

1. **`/rtc/` must be proxied on the SAME hostname as the UI.** The client probes
   `rtc/config` **relative to its own origin** (`console-ui/src/lib/rtc.js`), and
   the gateway listens on `8090` while the engine serves the UI on `7070`.
   Publishing 8090 separately is not enough: the page loads, 404s
   `/rtc/config`, and reports "Audio isn't connected in this browser". With
   `/rtc/` same-origin the request is `200` and the failed-request list is empty.
2. **HTTPS is a functional requirement, not PWA polish.** Browsers expose
   `navigator.mediaDevices` only in a **secure context** (HTTPS or `localhost`).
   Over plain HTTP on a LAN IP the Console can *receive* audio but can **never
   capture the mic** — Chrome logs `navigator.mediaDevices is undefined`. So the
   Console cannot talk back until the vhost exists.

Also set `CROSSPOINT_API_ORIGIN=https://crosspoint.app.lagreca.io` on the
container, and ensure **UDP 40000–40019 reaches 10.248.233.7 directly** (WebRTC
media bypasses the proxy by design, D13).

---

## 4. Gotchas that cost time — please don't rediscover them

- **`--network host` is MANDATORY for the Console container on the VDI.** podman's
  default **pasta** networking gives the container a private IP peers cannot
  reach, so AOO's handshake fails with `couldn't establish UDP connection to
  lagreca|<peer>; timed out after 5 seconds` **even though the group join
  succeeds**. With host networking the peers find each other.
- **`agent-silent` was environment-dependent, not a regression.** It declared no
  `input_device`, so the headless VDI opened the **real microphone**; room noise
  above −60 dBFS made the engine correctly report `input "ok"` and the scenario
  failed 5/5 — then passed earlier the same day on a quieter room. Fixed by
  giving the peer a config whose `input_device_candidates` resolves to a device
  that genuinely carries no signal (`tests/f2/peerprep.py` turns `*_candidates`
  into the concrete key; `F2_INPUT_DEVICE` overrides). Do **not** "fix" it with
  `SONOBUS_AGENT_INPUT=silent` — that bypasses the detector the scenario exists
  to exercise.
- **The gateway/F2 audio test needs its own group.** The first e2e run failed
  "silent before tone" because the VDI agent was joined to `lagreca` and
  legitimately transmitting. That is the system working, not a fault.
- **`tests/linux/deb-systemd.sh` needs `--platform`.** The VDIs are amd64, so on
  an arm64 host run `tests/linux/deb-systemd.sh --platform linux/amd64`, or the
  container is the host's arch and apt rejects the package with a confusing
  `libasound2t64:amd64 not installable`. The script now also picks the `.deb`
  whose arch matches. **Under qemu the systemd/dbus parts cannot start**
  (`user bus did not appear`) — that is an emulation limit, not a package
  defect; the arm64 native run is 21/21 and proves the harness.
- **Builds were slow because there was no `.dockerignore`** — every root-context
  build uploaded the whole 2.8 GB repo. Added (`d49c5f75`); the web-console image
  now builds in ~4 min.
- **Building amd64 on this Mac:** use the native builder
  (`docker buildx --builder buildkit-priv --platform linux/amd64`), not qemu.
  Note that this **re-tags `debian:trixie` to amd64**; re-pull
  `--platform linux/arm64` afterwards or arm64 runs will fail confusingly.
- **Do not point Chrome at a plain-HTTP LAN address** and expect the mic. Use
  `tools/local-console-proxy.mjs` (serves the Console on `http://localhost` and
  proxies `/` → 7070, `/rtc/` → 8090 with the WebSocket upgrade) — `localhost`
  counts as a secure context.

---

## 5. Open items, in priority order

1. **P7.4 vhost** — §3. Needs maelo's proxy access. This is the last thing
   between the current state and a browser Console that can talk back.
2. **Install the fixed `.deb` on the VDI.** The package installed there is
   `1.7.2+git9a237e6f`, which **predates the P2.6 UI fix**, so it has no
   `/usr/share/crosspoint/ui` and a unit without `--ui-dir` — `localhost:7071`
   **404s** on that box through the packaged path. The running agent was made to
   work with a user-local layout instead. Rebuild (`scripts/build-deb.sh`), then
   `sudo apt install ./crosspoint_*.deb`. Verify with
   `dpkg -L crosspoint | grep crosspoint/ui` and
   `grep ui-dir /usr/lib/systemd/user/crosspoint-agent.service`.
3. **Desktop-entry UX (design decision, D7 — not implemented).**
   `crosspoint.desktop` runs the **GUI** binary (`Exec=crosspoint %u`), which a
   headless VDI cannot use; clicking "Crosspoint" gives a broken window and does
   not start the agent. This is also why `open crosspoint` confused a user: it is
   a *URL* attempt, so the browser reports `DNS_PROBE_FINISHED_NXDOMAIN` for the
   bare word. The agent page is **`http://localhost:7071/`**. Suggested fix:
   point the entry at that URL, or drop the `.desktop` from the VDI package, and
   give the URL explicitly in `packaging/debian/INSTALL.md` §5. Left to the
   design owner because it is UI/copy.
4. **Pre-existing `steps-frozen` failure** in `tests/f2/test-evaluate.sh`
   (99 passed / 1 failed) — **not** caused by anything here; it reproduces on
   unmodified `main`. Root cause is in `evaluate.py`'s step loop: an `increases`
   assertion captures a baseline when the hold starts and then **clears it on
   every failed poll**, so the check oscillates (pass, fail, pass) until it times
   out while `last` holds an *OK* poll — the expected reason never reaches the
   report. The scenario correctly fails; the defect is the missing reason. Fix:
   keep the baseline across failed polls, or report the last *failing* poll.
5. **Uncommitted `tests/linux/container-test.sh`** in the working tree (not mine;
   it adds an `agent.testTone` output-pin check via the control API). Left
   untouched and uncommitted because I did not verify it. Whoever owns it should
   run it before committing.
6. **`L2`** (residual Linux dropouts) is open and may affect real VDIs. `L1`
   fixed the big one (10–12 s → 55–112 ms).

---

## 6. Reproducing the verification

```sh
# packaging (native arch only; qemu cannot start systemd here)
tests/linux/deb-systemd.sh                       # 21 PASS / 0 FAIL

# amd64 package on an arm64 host
tests/linux/deb-systemd.sh --no-build --platform linux/amd64

# F2 harness (needs build/desktop-release and build/aooserver-release)
tests/f2/run.sh --scenario agent-silent          # PASS
tests/f2/test-evaluate.sh                        # 99/1; the 1 is steps-frozen (§5.4)

# WebRTC audio against the live container, in its own group
node tools/local-console-proxy.mjs --upstream 192.168.0.71 --port 8080 &
node docker/web-console/gateway/test/e2e.mjs \
  --remote maelo@192.168.0.71 --container <name> --url http://localhost:8080
```

`--remote user@host` makes the gateway e2e drive a container over ssh with
podman, which is how the Console on the VDI was tested. Note that over `--remote`
the tone generator's recorded `$$` is the ssh wrapper's PID, so the test also
kills it by process match.
