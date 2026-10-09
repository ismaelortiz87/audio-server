# audio-server

Monorepo with the complete SonoBus audio stack — the multi-platform app, the
audio transport library it speaks, the JUCE forks it builds against, and the
headless connection server.

Everything that upstream SonoBus kept as nested `git-subrepo` directories is
hoisted to the repository root here, so there is exactly one copy of each
dependency and one place to patch it.

## Layout

- `sonobus/` — the app: desktop standalone (macOS/Windows/Linux), iOS, Android,
  AU/VST3 plugin. Imported with full upstream history.
  Upstream: `sonosaurus/sonobus` branch `main` @ `35f1062` (v1.7.2)
- `aoo/` — Audio-over-OSC: the low-latency P2P audio transport plus the
  login/group/rendezvous server side.
  Upstream: `essej/aoo` branch `sono` @ `9960da7`
- `juce/` — the JUCE fork the CMake builds use.
  Upstream: `essej/JUCE` branch `sono7good` @ `925e37b`, content = the
  conflict-resolved tree vendored in SonoBus 1.7.2 (see note below)
- `sono6good/` — legacy JUCE 6 fork branch, kept for reference.
  Upstream: `essej/JUCE` branch `sono6good` @ `9415d09`
- `aooserver/` — headless AOO connection server (rendezvous only, audio stays
  peer-to-peer). Upstream: `essej/aooserver` branch `master` @ `81a4cf9`.
  It keeps its own vendored AOO snapshot under `aooserver/deps/aoo` **on
  purpose**: that snapshot carries server API the SonoBus fork dropped — e.g.
  the IP blocklist (`add_blocked_address` / `is_address_blocked`, used by the
  server's `-b/--blocklist` flag). The AOO that SonoBus vendors in `aoo/`
  (branch `sono`) has a reduced `server.cpp` because the app only runs the
  small embedded rendezvous server. Pointing `aooserver/` at the top-level
  `aoo/` would silently strip that functionality. Its JUCE is self-contained
  in `aooserver/JuceLibraryCode/modules`.

`sono6good/` is not referenced by any build file in the current tree. Upstream's
README still claims SonoBus is built on the `sono6good` (JUCE 6) fork, but the
code actually builds against `deps/juce`, i.e. `sono7good`. It is imported only
because it is part of the public upstream story; nothing links to it.

## What differs from upstream SonoBus

1. **Dependencies hoisted.** `sonobus/deps/juce`, `sonobus/deps/aoo` and the
   root `sonobus/JUCE` are gone; their content lives in `juce/`, `aoo/` and
   `sono6good/`. No duplicated copies, no subrepo bookkeeping files.
2. **Path references rewired** (mechanically: every `deps/juce` → `../juce` and
   `deps/aoo` → `../aoo`, i.e. exactly one extra `..` per reference, since the
   trees moved up one level). Files touched:
   - `sonobus/CMakeLists.txt` — desktop builds (macOS/Windows/Linux)
   - `sonobus/mobile/SonoBusMobile.jucer` — Projucer source of truth for iOS/Android
   - `sonobus/mobile/Builds/Android/app/CMakeLists.txt` and `.../app/build.gradle`
   - `sonobus/mobile/Builds/iOS/SonoBus.xcodeproj/project.pbxproj`

   The `add_subdirectory()` of the sibling JUCE tree also gets an explicit
   binary dir — `add_subdirectory(../juce juce EXCLUDE_FROM_ALL)` — which CMake
   requires for an out-of-tree source directory.
3. **`juce/` carries the conflict-resolved tree, not the raw upstream commit.**
   Commit `925e37b` of `essej/JUCE` (`sono7good`) has committed merge-conflict
   markers in `modules/juce_gui_basics/native/juce_FileChooser_ios.mm`, plus
   older ALSA/CoreAudio code. The tree vendored under `deps/juce` in upstream
   SonoBus is the resolved state that 1.7.2 actually compiles, so that is what
   is used here.
4. `sonobus/deps/` still holds everything that is *not* a separate repo:
   `ff_meters`, the prebuilt Opus static libs (`mac/`, `windows/`, `ios/`,
   `android/`). Their paths are unchanged.

## History / pulling upstream changes

- `sonobus/` was imported with full upstream history via `git subtree`, so
  `git subtree pull -P sonobus https://github.com/sonosaurus/sonobus.git main`
  still merges upstream work.
- The dependency dirs were imported as single squashed commits at pinned
  upstream commits (provenance in the commit message), so
  `git subtree pull --squash -P juce https://github.com/essej/JUCE.git sono7good`
  works when a JUCE bump is wanted.

## Building

One command builds the desktop app and the connection server on macOS or
Linux (macOS needs only the Command Line Tools plus `brew install cmake`; Xcode
is not required):

```bash
scripts/build-desktop.sh            # Release, native arch -> build/desktop-release, build/aooserver-release
scripts/build-desktop.sh --help     # --debug, --app-only, --server-only, --universal, --plugins
```

On Linux, install the prerequisites first (`sonobus/linux/deb_get_prereqs.sh`
or `fedora_get_prereqs.sh`). `aooserver` builds via `aooserver/CMakeLists.txt`,
which mirrors its Projucer Linux makefile.

Upstream's own route also still works, desktop (Linux shown):

```bash
cd sonobus/linux
sudo ./deb_get_prereqs.sh        # Debian/Ubuntu; fedora_get_prereqs.sh on Fedora
./build.sh                       # setupcmake.sh + buildcmake.sh from sonobus/
../build/SonoBus_artefacts/Release/sonobus
```

CMake picks JUCE up from `../juce` and AOO from `../aoo` — no extra flags.

iOS/Android build from `sonobus/mobile/SonoBusMobile.jucer` via Projucer, which
regenerates the Xcode/Android projects. The generated projects in this repo were
rewired by script; re-save from Projucer on a Mac and sanity-check before
trusting a mobile build, so the generated projects and the `.jucer` are
guaranteed in sync.

Server (self-contained Projucer project; default port 10998, TCP+UDP):

```bash
cd aooserver/Builds/LinuxMakefile
CONFIG=Release make -j4          # -> Builds/LinuxMakefile/build/aooserver
./build/aooserver -h             # -p port, -l logdir, -b blocklist
```

## Browser / Docker

`docker/` builds the app for Linux and serves it to a browser — the real
unmodified application in a virtual display, plus a WebSocket audio bridge so
the browser can hear the engine and talk back into it.

```bash
docker compose -f docker/docker-compose.yml up --build
open http://localhost:6080/          # audio client
open http://localhost:6080/vnc.html  # the app's own GUI
```

See `docker/README.md` for the architecture, what was verified and how, and the
known limitations (notably: added latency, so it suits monitoring and
conversation rather than tight remote jamming).

## Design

The palette agreed for the next appearance pass on the app lives in
`docs/design/` — `maia-mission-control.md` (spec + where each token lands in the
code) and `tokens.json` (machine-readable values). Design reference only,
nothing is wired into code yet.

## Licensing

Unchanged from upstream: SonoBus is GPLv3 (`sonobus/LICENSE` + `LICENSE_EXCEPTION`),
AOO is BSD-style (`aoo/LICENSE`), JUCE has its own licence (`juce/LICENSE.md`).
This monorepo is a fork/repackage, not a relicence.
