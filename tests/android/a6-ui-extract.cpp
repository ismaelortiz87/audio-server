// A6 test: the Console UI zip that ships as BinaryData really unpacks, and the
// stamp logic really skips a second extraction.
//
// This runs the *same* algorithm as
// SonoStandaloneFilterApp::extractBundledConsoleUi (JUCE ZipFile + MD5 stamp),
// compiled against a desktop JUCE and the real
// sonobus/Resources/crosspoint-ui.zip, because the Android extractor itself
// cannot run on the Mac host. If this test and that function drift apart the
// test stops meaning anything -- change both in the same commit.
//
//   scripts/test-android-ui.sh

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <BinaryData.h>

#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

// Mirror of SonoStandaloneFilterApp::extractBundledConsoleUi (A6), with the app
// data directory passed in so the test is hermetic.
static juce::File extractBundledConsoleUi (const juce::File& appDataDir)
{
    auto root = appDataDir.getChildFile ("console-ui");
    auto stampFile = root.getParentDirectory().getChildFile ("console-ui.stamp");

    const juce::String stamp = juce::String::toHexString ((juce::int64) BinaryData::crosspointui_zipSize)
                             + "-" + juce::MD5 (BinaryData::crosspointui_zip, (size_t) BinaryData::crosspointui_zipSize).toHexString();

    if (root.getChildFile ("index.html").existsAsFile()
        && stampFile.existsAsFile()
        && stampFile.loadFileAsString().trim() == stamp)
        return root;

    juce::MemoryInputStream in (BinaryData::crosspointui_zip, (size_t) BinaryData::crosspointui_zipSize, false);
    juce::ZipFile zip (in);
    if (zip.getNumEntries() == 0)
        return root;

    if (root.isDirectory())
        root.deleteRecursively();

    auto r = zip.uncompressTo (root.getParentDirectory(), true);
    if (r.failed())
        return root;

    stampFile.replaceWithText (stamp);
    return root;
}

int main (int argc, char** argv)
{
    std::cout << "A6 bundled Console UI" << std::endl;

    check (BinaryData::crosspointui_zipSize > 0, "the zip is embedded in BinaryData");

    // The Projucer mangles "crosspoint-ui.zip" to crosspointui_zip; renaming it
    // in the .jucer would silently break the app, so check the bytes really are
    // a readable zip.
    juce::MemoryInputStream probe (BinaryData::crosspointui_zip, (size_t) BinaryData::crosspointui_zipSize, false);
    check (juce::ZipFile (probe).getNumEntries() > 0, "the embedded bytes are a readable zip");

    // Sandbox in a writable place. JUCE's File::tempDirectory resolves outside
    // the source tree, which a sandboxed CI run may not allow, so the caller
    // (scripts/test-android-ui.sh) passes a directory it knows is writable.
    auto sandbox = argc > 1 ? juce::File (juce::String (argv[1]))
                            : juce::File::getCurrentWorkingDirectory().getChildFile ("a6-ui-extract-sandbox");
    if (sandbox.isDirectory())
        sandbox.deleteRecursively();
    if (! sandbox.createDirectory())
    {
        std::cout << "FAIL: cannot create the sandbox " << sandbox.getFullPathName() << std::endl;
        return 1;
    }

    auto root = extractBundledConsoleUi (sandbox);
    check (root.getChildFile ("index.html").existsAsFile(), "index.html lands at console-ui/index.html");
    check (root.getChildFile ("src/api/client.js").existsAsFile(), "nested src/ files land too");
    check (root.getChildFile ("icons/pwa-512.png").existsAsFile(), "icons land too");

    auto stampFile = sandbox.getChildFile ("console-ui.stamp");
    check (stampFile.existsAsFile(), "a stamp file is written");
    const auto firstStamp = stampFile.loadFileAsString();

    // A second call must be a no-op: overwrite index.html with a sentinel and
    // confirm the stamp path returns early instead of re-extracting.
    root.getChildFile ("index.html").replaceWithText ("<html>sentinel</html>");
    auto second = extractBundledConsoleUi (sandbox);
    check (second.getChildFile ("index.html").loadFileAsString().contains ("sentinel"),
           "a matching stamp skips re-extraction");
    check (stampFile.loadFileAsString() == firstStamp, "the stamp is unchanged on the skip path");

    // A stale stamp must re-extract and overwrite the sentinel (the upgrade path
    // in A6: a newer APK must never keep serving the old UI).
    stampFile.replaceWithText ("stale");
    auto third = extractBundledConsoleUi (sandbox);
    check (! third.getChildFile ("index.html").loadFileAsString().contains ("sentinel"),
           "a stale stamp re-extracts over the old UI");

    // The ApiServer serves uiDir as the web root, so the archive must not leave a
    // doubly-nested console-ui/console-ui folder.
    check (! root.getChildFile ("console-ui").isDirectory(), "no doubly-nested console-ui folder");

    sandbox.deleteRecursively();

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": " << failures << " failure(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
