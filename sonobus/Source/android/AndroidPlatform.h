// Android only. What the Java side reports (PlatformMonitor, the notification
// actions) and the `platform` object of the Console state (control-api 5.5).
//
// The JNI callbacks in SonoBusActivity.h only store values here (atomics), so
// they are safe before the engine exists; the AndroidServiceController (message
// thread timer) reads them. Several tasks add keys to platformVar():
//   A10: notifications, backgroundRestricted   A14: phoneCall, micOffByCall
//   A8: outputRoute, echoRisk                  A9: micPermission

#pragma once

#include "JuceHeader.h"

#if JUCE_ANDROID

#include "../SonobusPluginProcessor.h"

#include <atomic>
#include <functional>

namespace crosspoint::android {

struct Signals
{
    std::atomic<bool> notificationsEnabled { true };    // NotificationManagerCompat.areNotificationsEnabled()
    std::atomic<bool> backgroundRestricted { false };
    std::atomic<bool> phoneCall { false };              // PlatformMonitor: a cellular or VoIP call owns the audio
    std::atomic<int>  pendingAction { 0 };              // notification action: 1 = Mic off/on, 2 = Disconnect
    std::atomic<bool> shutdownRequested { false };      // swipe-away
    std::function<void()> onShutdown;                   // set by the app on the message thread

    static Signals& get() { static Signals s; return s; }
};

/** state.platform (message thread). Others add their keys here. */
inline var platformVar (const SonobusAudioProcessor& proc)
{
    auto* o = new DynamicObject();
    auto& sig = Signals::get();
    o->setProperty ("notifications", sig.notificationsEnabled.load() ? "granted" : "denied");
    o->setProperty ("backgroundRestricted", sig.backgroundRestricted.load());
    o->setProperty ("phoneCall", proc.isPhoneCall());
    o->setProperty ("micOffByCall", proc.isMicOffByCall());
    return var (o);
}

} // namespace crosspoint::android

#endif
