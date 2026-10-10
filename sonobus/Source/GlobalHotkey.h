// Global push-to-talk hotkey (P6.5). macOS: Carbon RegisterEventHotKey, which
// delivers press AND release events system-wide and needs no Accessibility
// permission. Other platforms get an inert stub (supported() == false), so the
// callers need no #ifs.
//
// Binding strings are what the control API shows in settings.pttHotkey: the
// modifier symbols in the fixed order control, option, shift, command, then the
// key, e.g. "⌥Space", "⌃⌥T", "⇧⌘F5". parse() also accepts words ("alt+space").
//
// Everything runs on the JUCE message thread.

#pragma once

#include "JuceHeader.h"
#include <functional>

namespace crosspoint {

class GlobalHotkey
{
public:
    static constexpr const char* kDefaultBinding = "\xE2\x8C\xA5" "Space";   // "⌥Space" (UTF-8 spelled out: the source encoding of macros is not guaranteed)

    GlobalHotkey();
    ~GlobalHotkey();

    /** True where a global hotkey can exist (macOS). */
    static bool supported();

    /** Validates `spec` and returns the canonical spelling. On failure returns an
        empty string and fills `error`. Needs at least one modifier, so a bare
        letter cannot take over typing in every app. */
    static String canonicalise (const String& spec, String& error);

    /** Registers (or re-registers) the binding. A key that is held down at that
        moment is released first (onChange(false)). */
    bool setBinding (const String& spec, String& error);

    /** The canonical binding currently registered, "" when none. */
    String getBinding() const { return binding; }

    /** Releases the hotkey; fires onChange(false) if it was held. */
    void clear();

    /** True between a press and its release. */
    bool isDown() const { return down; }

    /** Press (true) and release (false), message thread. */
    std::function<void (bool down)> onChange;

    /** TEST HOOK: runs the same path a Carbon event takes (the real keyboard
        needs a poster with Accessibility rights to drive it from a script). */
    void simulate (bool pressed) { handleEvent (pressed); }

    // internal: called by the Carbon handler
    void handleEvent (bool pressed);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    String binding;
    bool down = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalHotkey)
};

} // namespace crosspoint
