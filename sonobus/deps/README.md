
The ff_meters sub-directory here is a managed subrepo from another github repo
(the upstream author's, https://github.com/ffAudio/ff_meters), using
git-subrepo (https://github.com/ingydotnet/git-subrepo).

NOTE (audio-server monorepo): this tree is no longer a standalone repo. The
aoo and juce/JUCE dependencies used to live here as subrepos and have been
hoisted to the monorepo root:

    ../aoo          (was deps/aoo,     essej/aoo  @ sono)
    ../juce         (was deps/juce,    essej/JUCE @ sono7good)
    ../sono6good    (was ../JUCE,      essej/JUCE @ sono6good, legacy)

All build files that referenced them (CMakeLists.txt, mobile/*.jucer, the
generated iOS/Android projects) were rewired to point at the new locations.
Everything under this directory is unchanged, so the paths to ff_meters and to
the prebuilt Opus libs below are still valid.

The Opus dependency is handled here with some pre-built static library
versions of Opus for Mac, Windows and iOS, which can be found in the
mac/windows/ios subdirectories here. For the linux build the opus library
must be installed and available via pkg-config.
