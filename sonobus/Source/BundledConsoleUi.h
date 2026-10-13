// A6: unpack the Console UI that ships in the app as a zip (BinaryData) into the
// app's data directory. Phone platforms have no loose UI files for the ApiServer
// to serve, unlike the Mac bundle.
//
// Kept in its own header, with no dependency on the app, so that the host-side
// test (tests/android/a6-ui-extract.cpp, scripts/test-android-ui.sh) runs the very
// same code the phone runs.

#pragma once

#include <juce_core/juce_core.h>
#include <iostream>

namespace crosspoint
{

/** Unpacks `zipData` into `appDataDir`/console-ui and returns that directory (the
    ApiServer's web root, with index.html inside it).

    The zip is written with a top-level "console-ui/" folder. It is re-extracted
    whenever the stamp (size + MD5 of the zip) changes, so an upgrade replaces a
    stale UI (the service-worker cache is keyed separately, in console-ui/sw.js).

    Never extracts over a directory that already matches the stamp: this runs on
    every launch, and unzipping the UI each time would be wasted work. On failure it
    logs a warning and returns the directory anyway (the caller checks index.html). */
inline juce::File extractBundledConsoleUi (const void* zipData, size_t zipSize, const juce::File& appDataDir)
{
    auto root = appDataDir.getChildFile ("console-ui");
    auto stampFile = root.getParentDirectory().getChildFile ("console-ui.stamp");

    const juce::String stamp = juce::String::toHexString ((juce::int64) zipSize)
                             + "-" + juce::MD5 (zipData, zipSize).toHexString();

    if (root.getChildFile ("index.html").existsAsFile()
        && stampFile.existsAsFile()
        && stampFile.loadFileAsString().trim() == stamp)
        return root;

    juce::MemoryInputStream in (zipData, zipSize, false);
    juce::ZipFile zip (in);
    if (zip.getNumEntries() == 0)
    {
        std::cerr << "Warning: the bundled Console UI archive is empty" << std::endl;
        return root;
    }

    // Replace rather than merge: a renamed or deleted file must not survive.
    if (root.isDirectory())
        root.deleteRecursively();

    auto r = zip.uncompressTo (root.getParentDirectory(), true);
    if (r.failed())
    {
        std::cerr << "Warning: could not unpack the bundled Console UI: " << r.getErrorMessage() << std::endl;
        return root;
    }

    stampFile.replaceWithText (stamp);
    return root;
}

} // namespace crosspoint
