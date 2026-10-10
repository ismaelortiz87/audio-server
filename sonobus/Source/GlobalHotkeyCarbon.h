// The Carbon part of the global hotkey (P6.5), kept free of JUCE headers because
// Carbon's global type names (Component, Point, ...) clash with JUCE's.
#pragma once

#include <string>

namespace crosspoint { namespace carbon {

// Modifier masks of the Carbon event API (cmdKey, shiftKey, optionKey, controlKey).
enum : unsigned { modCommand = 0x100, modShift = 0x200, modOption = 0x800, modControl = 0x1000 };

typedef void (*HotkeyCallback) (void* context, bool pressed);

/** Registers the one app-wide hotkey (replacing a previous one). `keyCode` is a
    macOS virtual key code (kVK_*). The callback runs on the main thread. */
bool registerHotkey (unsigned keyCode, unsigned modifiers, HotkeyCallback cb, void* context, std::string& error);
void unregisterHotkey();

}} // namespace
