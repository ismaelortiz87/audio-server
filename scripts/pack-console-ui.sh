#!/usr/bin/env bash
# A6: package console-ui/ into the zip that gets embedded in the app as JUCE
# BinaryData, so the APK (and iOS, P9.3) can serve the Console UI without any
# files on disk.
#
#   scripts/pack-console-ui.sh
#
# Output: sonobus/Resources/crosspoint-ui.zip
#
# The zip is a build artifact and is git-ignored. It must exist *before* the
# Projucer resaves the .jucer, because the .jucer lists it as a binary resource
# and the Projucer refuses to save when a resource file is missing. That is why
# scripts/build-android.sh runs this first.
#
# The archive is stored with no extra folder level and no OS metadata, so the
# paths inside are exactly the console-ui/ layout (index.html at the root).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/console-ui"
OUT_DIR="$ROOT/sonobus/Resources"
OUT="$OUT_DIR/crosspoint-ui.zip"

[ -d "$SRC" ] || { echo "console-ui/ not found at $SRC" >&2; exit 1; }
command -v zip >/dev/null || { echo "zip not found (macOS: it ships with the OS)" >&2; exit 1; }

mkdir -p "$OUT_DIR"

# The UI has no build step (console-ui/README.md): the files are served as-is,
# exactly what the Mac bundle copies. README/package/test/scripts are not part
# of what the ApiServer serves, so they are excluded.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/console-ui"
for f in index.html agent.html manifest.webmanifest agent.webmanifest sw.js; do
    [ -f "$SRC/$f" ] && cp "$SRC/$f" "$STAGE/console-ui/"
done
for d in src icons; do
    [ -d "$SRC/$d" ] && cp -R "$SRC/$d" "$STAGE/console-ui/"
done

[ -f "$STAGE/console-ui/index.html" ] || { echo "console-ui/index.html missing from the staged copy" >&2; exit 1; }

rm -f "$OUT"
# -X drops extra file attributes, -D drops directory entries and -q is quiet, so
# the archive is byte-identical for identical input (a stable stamp for A6).
( cd "$STAGE" && zip -q -X -D -r "$OUT" console-ui )

echo "Packed Console UI:"
echo "  $OUT"
ls -lh "$OUT" | awk '{print "  size: "$5}'
echo "  files: $(unzip -l "$OUT" | tail -1 | awk '{print $2}')"
