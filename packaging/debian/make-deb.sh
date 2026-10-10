#!/bin/bash
# Runs INSIDE the build container (see Dockerfile.build): lays out the package
# tree from the built binary and the packaging files, computes Depends with
# dpkg-shlibdeps, and writes /out/crosspoint_<version>_<arch>.deb.
# Env: BIN (built binary), VERSION (deb version), SRC (repo root in the image).
set -euo pipefail
: "${BIN:?}" "${VERSION:?}" "${SRC:?}"
ARCH="$(dpkg --print-architecture)"
P=/tmp/pkgroot
rm -rf "$P"
mkdir -p "$P/DEBIAN" "$P/usr/bin" "$P/usr/lib/systemd/user" \
  "$P/usr/share/doc/crosspoint" "$P/usr/share/icons/hicolor/256x256/apps" "$P/usr/share/applications" \
  "$P/usr/share/crosspoint/ui"

install -m 0755 "$BIN" "$P/usr/bin/crosspoint"
strip --strip-unneeded "$P/usr/bin/crosspoint"
install -m 0644 "$SRC/packaging/debian/crosspoint-agent.service" "$P/usr/lib/systemd/user/crosspoint-agent.service"
install -m 0644 "$SRC/sonobus/vdi.example.yaml" "$P/usr/share/doc/crosspoint/vdi.example.yaml"
install -m 0644 "$SRC/packaging/debian/README.Debian" "$P/usr/share/doc/crosspoint/README.Debian"
install -m 0644 "$SRC/design/icon/crosspoint-256.png" "$P/usr/share/icons/hicolor/256x256/apps/crosspoint.png"
install -m 0644 "$SRC/packaging/debian/crosspoint.desktop" "$P/usr/share/applications/crosspoint.desktop"
# The agent's localhost web UI (P2.6). The engine serves whatever --ui-dir points
# at, and crosspoint-agent.service passes /usr/share/crosspoint/ui, so the VDI's
# http://localhost:7071/ shows the real agent page instead of 404.
cp -R "$SRC/console-ui/." "$P/usr/share/crosspoint/ui/"
find "$P/usr/share/crosspoint/ui" -type d -exec chmod 0755 {} +
find "$P/usr/share/crosspoint/ui" -type f -exec chmod 0644 {} +
cat > "$P/usr/share/doc/crosspoint/copyright" <<'C'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Crosspoint (fork of SonoBus)
Source: https://github.com/sonosaurus/sonobus

Files: *
Copyright: 2020-2026 Jesse Chappell and the SonoBus contributors; Crosspoint changes by their authors
License: GPL-3+
 This program is free software: you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the Free
 Software Foundation, either version 3 of the License, or (at your option)
 any later version.
 .
 On Debian systems, the full text of the GNU General Public License version 3
 can be found in /usr/share/common-licenses/GPL-3.
C
printf 'crosspoint (%s) trixie; urgency=medium\n\n  * Crosspoint VDI agent package (P2.10).\n\n -- Crosspoint <noreply@crosspoint.invalid>  %s\n' \
  "$VERSION" "$(date -R -d "@${SOURCE_DATE_EPOCH:-0}")" | gzip -9n > "$P/usr/share/doc/crosspoint/changelog.gz"

# Depends: computed from the binary, plus the audio stack the agent talks to
# (pw-dump from pipewire-bin is used for node lookup, P2.11).
mkdir -p /tmp/shl/debian
cd /tmp/shl
printf 'Source: crosspoint\n\nPackage: crosspoint\nArchitecture: any\n' > debian/control
SHL="$(dpkg-shlibdeps -O -e"$P/usr/bin/crosspoint" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')"
DEPENDS="$SHL, pipewire-audio, pipewire-bin, ca-certificates"
SIZE="$(du -sk "$P" | cut -f1)"

cat > "$P/DEBIAN/control" <<CTL
Package: crosspoint
Version: $VERSION
Architecture: $ARCH
Maintainer: Crosspoint <noreply@crosspoint.invalid>
Installed-Size: $SIZE
Depends: $DEPENDS
Recommends: dbus-user-session, libpam-systemd
Section: sound
Priority: optional
Homepage: https://github.com/sonosaurus/sonobus
Description: low-latency audio agent for virtual desktops (SonoBus fork)
 Headless Crosspoint agent that connects a virtual desktop's PipeWire audio
 (a loopback input and a virtual-mic output) to a Crosspoint group over the
 network. Configured from a YAML file and run as a systemd user service.
 .
 The service is not enabled automatically; see README.Debian.
CTL

install -m 0755 "$SRC/packaging/debian/postinst" "$P/DEBIAN/postinst"
install -m 0755 "$SRC/packaging/debian/prerm" "$P/DEBIAN/prerm"
install -m 0755 "$SRC/packaging/debian/postrm" "$P/DEBIAN/postrm"

find "$P" -type d -exec chmod 0755 {} +
(cd "$P" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums)
mkdir -p /out
dpkg-deb --root-owner-group -Zxz --build "$P" "/out/crosspoint_${VERSION}_${ARCH}.deb"
ls -l /out
