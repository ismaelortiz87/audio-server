# Crosspoint VDI agent on Debian 13 (trixie)

The agent is a headless program configured from one YAML file and run as a
**systemd user service** (PipeWire runs per user, so the agent must too).

## 1. Build the package (once, on any machine with Docker)

```sh
scripts/build-deb.sh                        # -> build/deb/crosspoint_<version>_<arch>.deb
PLATFORM=linux/amd64 scripts/build-deb.sh   # force amd64 (see below)
```

The build runs in a `debian:trixie` container and uses the Docker host's
architecture. For an amd64 `.deb` run the script on an amd64 machine, or on an
arm64 host with qemu binfmt (`docker run --privileged --rm tonistiigi/binfmt
--install amd64`); the emulated compile takes hours, so prefer a native amd64
builder (CI, or any x86-64 Linux box).

## 2. Install

```sh
sudo apt install ./crosspoint_<version>_<arch>.deb
```

`apt` pulls in the runtime dependencies (ALSA, freetype, curl, X11 client
libraries JUCE links against, and the PipeWire stack: `pipewire-audio`,
`pipewire-bin`). Check: `crosspoint --version`.

Nothing is enabled and no service starts by itself.

## 3. Configure

As the VDI user (not root):

```sh
mkdir -p ~/.config/crosspoint
cp /usr/share/doc/crosspoint/vdi.example.yaml ~/.config/crosspoint/vdi.yaml
$EDITOR ~/.config/crosspoint/vdi.yaml
```

Set `server`, `group`, `password`, `username`, `role: vdi`. The audio keys name
**PipeWire nodes** (`node.name`):

```sh
pactl list short sources     # input candidates: a sink's "<name>.monitor", or a source
pactl list short sinks       # output candidates
pw-cli ls Node               # same, with all properties
```

Example for a VDI whose apps play into `loopback_sink` and which exposes a
virtual microphone sink `crosspoint_mic`:

```yaml
audio:
  input_device: loopback_sink.monitor
  output_device: crosspoint_mic
```

Virtual sinks should have `node.driver=true` so they run without a hardware
device (see P2.11).

Behaviour at start: a **bad YAML file, unknown key or invalid value** stops the
agent with exit code 1 (the error is in the journal and systemd retries every
5 s). A **missing audio node** does not: the agent stays up, reports
`input`/`output: missing` (Console and `/api/v1/health`) and retries until the
node appears.

## 4. Enable

```sh
systemctl --user enable --now crosspoint-agent
sudo loginctl enable-linger "$USER"     # start at boot, without anyone logging in
```

Debian needs `dbus-user-session` for `systemctl --user` over ssh/console
(the package recommends it).

## 5. Check

```sh
systemctl --user status crosspoint-agent
journalctl --user -u crosspoint-agent -f     # logs (stderr goes to the journal)
curl http://localhost:7071/api/v1/health     # role "vdi", audio and connection state
```

If `journalctl --user` shows nothing, the user is not allowed to read the
journal: make journald persistent (`sudo mkdir -p /var/log/journal`, the
Debian default) or add the user to the `systemd-journal` group.

Reboot test: after `reboot`, the agent should reach "Connected" with no login.

## 6. Reload config, upgrade, uninstall

- Config change: edit the YAML, then `systemctl --user restart crosspoint-agent`.
- Upgrade: `sudo apt install ./crosspoint_<newversion>_<arch>.deb`, then
  `systemctl --user restart crosspoint-agent` (the package stops running
  instances before replacing the binary; start it again afterwards).
- Uninstall:

  ```sh
  systemctl --user disable --now crosspoint-agent     # per user, before removing
  sudo apt remove crosspoint                          # keeps ~/.config/crosspoint
  rm -rf ~/.config/crosspoint                         # optional: config and settings
  sudo loginctl disable-linger "$USER"                # optional
  ```

  Per-user enablement symlinks and config belong to the user and are never
  touched by the package scripts.
