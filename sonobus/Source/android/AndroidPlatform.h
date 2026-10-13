// Android only. What the Java side reports (PlatformMonitor, the notification
// actions); EngineState::androidPlatformVar() puts them in state.platform.
//
// The JNI callbacks in SonoBusActivity.h only store values here (atomics), so
// they are safe before the engine exists; the AndroidServiceController (message
// thread timer) reads them. Several tasks add keys to platformVar():
//   A10: notifications, backgroundRestricted   A14: phoneCall, micOffByCall
//   A8: outputRoute, echoRisk                  A9: micPermission (polled by the controller)

#pragma once

#include "JuceHeader.h"

#if JUCE_ANDROID

#include <atomic>
#include <functional>

namespace crosspoint::android {

struct Signals
{
    std::atomic<bool> notificationsEnabled { true };    // NotificationManagerCompat.areNotificationsEnabled()
    std::atomic<bool> backgroundRestricted { false };
    std::atomic<int>  micPermission { 1 };              // A9: 0 granted, 1 prompt (system dialog possible), 2 denied (Settings only)
    std::atomic<bool> phoneCall { false };              // PlatformMonitor: a cellular or VoIP call owns the audio
    std::atomic<int>  pendingAction { 0 };              // notification action: 1 = Mic off/on, 2 = Disconnect
    std::atomic<bool> shutdownRequested { false };      // swipe-away
    std::function<void()> onShutdown;                   // set by the app on the message thread

    // A17: a lock-screen player button. Java records which button was pressed and
    // the controller runs it on the message thread through the same command paths
    // the control API uses. An atomic int rather than a queue: a player is driven
    // by one person, and dropping a double-tap beats replaying a stale command.
    std::atomic<int> pendingPlayerButton { 0 };

    // A20: the Android Auto browse list. Tapping a station item selects it. A
    // String cannot go through an atomic, and the JNI callback already runs on the
    // Android main thread (= JUCE's message thread), so this is called directly
    // like onShutdown, rather than polled.
    std::function<void (const juce::String& stationId)> onSelectStation;

    const char* micPermissionName() const
    {
        const int m = micPermission.load();
        return m == 0 ? "granted" : m == 2 ? "denied" : "prompt";
    }

    static Signals& get() { static Signals s; return s; }
};

/** A17: the lock-screen player's buttons (values match CrosspointMediaSession's codes). */
enum class PlayerButton
{
    none = 0,
    mic,            // play/pause: Mic on/off
    previous,       // the previous station
    next,           // the next station
    mute,           // Mute the selected station
    solo,           // Solo the selected station (exclusive)
    connect,        // A20: the car's "Connect to {group}" browse item
    listen = 7      // Layout B centre: Listening pause / resume
};

/** A17: the owner id for player-issued commands. Session ids are positive and the
    Mac global hotkey uses -1, so a player button is not mistaken for either. */
constexpr int64 kPlayerCommandOwner = -2;

} // namespace crosspoint::android

#endif
