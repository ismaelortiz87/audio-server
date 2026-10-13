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

    const char* micPermissionName() const
    {
        const int m = micPermission.load();
        return m == 0 ? "granted" : m == 2 ? "denied" : "prompt";
    }

    static Signals& get() { static Signals s; return s; }
};

} // namespace crosspoint::android

#endif
