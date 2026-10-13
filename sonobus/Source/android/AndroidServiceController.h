// Android only (A10 + A14). The bridge between the engine and the phone:
//
//  - the foreground service and its Connection notification (android.md
//    section 4): the content is derived from the Console state (the data the live
//    band uses) by ConnectionNotification.h and pushed to Java only when title,
//    text, tint or actions change, at most once a second;
//  - the service lifetime (section 1): from connect until Disconnect or
//    swipe-away, whatever the connection state in between;
//  - the notification's Mic off/on and Disconnect actions, with the earcon (A14);
//  - phone calls (A14, section 6): Java reports them, this tells the processor.
//
// Message thread only. The JNI callbacks (SonoBusActivity.h) just set atomics in
// Signals; this timer picks them up.

#pragma once

#include "JuceHeader.h"

#if JUCE_ANDROID

#include "AndroidPlatform.h"
#include "SonoBusActivity.h"
#include "../ConnectionNotification.h"
#include "../EngineState.h"
#include "../SonobusPluginProcessor.h"

#include <functional>

namespace crosspoint::android {

class AndroidServiceController : private Timer
{
public:
    AndroidServiceController (SonobusAudioProcessor& p, EngineState& s, std::function<void()> disconnectNow)
        : proc (p), state (s), disconnect (std::move (disconnectNow))
    {
        startTimer (250);
    }

    ~AndroidServiceController() override
    {
        stopTimer();
        Signals::get().onShutdown = nullptr;
    }

    /** Swipe-away (Java onTaskRemoved / onDestroy while finishing): leave the group and persist. */
    void setShutdownHandler (std::function<void()> f)
    {
        Signals::get().onShutdown = std::move (f);
        if (Signals::get().shutdownRequested.load() && Signals::get().onShutdown) Signals::get().onShutdown();
    }

    /** Is the service (and so the notification) supposed to be running? Used by suspended(): never close the
        audio device while connected. */
    bool isServiceWanted() const { return serviceWanted; }

    /** A12 (headset button) and the notification action: toggle the mic in open mode, with the earcon.
        Returns false when the toggle is not allowed (PTT mode, a call, no mic permission). */
    bool toggleMicWithEarcon()
    {
        if (proc.getMicMode() != SonobusAudioProcessor::MicMode::Open || proc.isPhoneCall() || ! micGranted()) return false;
        const bool on = ! proc.getMicOn();
        proc.setMicOn (on);
        proc.playEarcon (on);
        return true;
    }

private:
    static bool micGranted()
    {
        return ! RuntimePermissions::isRequired (RuntimePermissions::recordAudio)
               || RuntimePermissions::isGranted (RuntimePermissions::recordAudio);
    }

    void timerCallback() override
    {
        auto& sig = Signals::get();

        // A14: a call starts / ends.
        proc.setPhoneCall (sig.phoneCall.load());

        // The notification's actions.
        switch (sig.pendingAction.exchange (0))
        {
            case 1: toggleMicWithEarcon(); state.refreshNow(); break;
            case 2: disconnect(); state.refreshNow(); break;
            default: break;
        }

        const var st = state.getLastState();
        if (! st.isObject()) return;

        const var conn = st["connection"];
        const String connState = conn["state"].toString(), reason = conn["reason"].toString();

        // Section 1: the service starts when a connection is tried and ends on Disconnect, whatever happens
        // to the connection in between. "Not connected to a server" is the idle state of a fresh install.
        const bool userDisconnected = connState == "failed" && reason == "Disconnected";
        const bool idle = connState == "failed" && reason == "Not connected to a server";
        if (userDisconnected)                         serviceWanted = false;
        else if (! idle)                              serviceWanted = true;

        notification::Input in;
        in.connState = connState.toStdString();
        in.group = conn["group"].toString().toStdString();
        in.reason = reason.toStdString();

        const var order = st["stationOrder"], stations = st["stations"];
        if (auto* ids = order.getArray())
            for (auto& idv : *ids)
            {
                const var s = stations[idv.toString()];
                if (! s.isObject()) continue;
                ++in.stationsKnown;
                if (s["presence"].toString() == "online") ++in.stationsOnline;
                if (static_cast<bool> (s["solo"])) in.anySolo = true;
                if (static_cast<bool> (s["hearsYou"]))
                {
                    String n = s["name"].toString();
                    if (n.startsWithIgnoreCase ("VDI-")) n = n.substring (4);       // console-ui shortName()
                    in.hearing.push_back (n.toStdString());
                }
            }

        const var mic = st["mic"], plat = st["platform"];
        in.micOpenMode = mic["mode"].toString() != "ptt";
        in.micOn = static_cast<bool> (mic["on"]);
        in.transmitting = static_cast<bool> (mic["transmitting"]);
        in.phoneCall = plat.isObject() && static_cast<bool> (plat["phoneCall"]);
        // A9 reports platform.micPermission; until it does, ask the system directly.
        const String perm = plat.isObject() ? plat["micPermission"].toString() : String();
        in.micGranted = perm.isNotEmpty() ? perm == "granted" : micGranted();

        const auto content = notification::build (in);

        const double now = Time::getMillisecondCounterHiRes();
        const bool startedOrStopped = serviceWanted != pushedActive;
        const bool changed = content != pushed;
        if (! startedOrStopped && ! (serviceWanted && changed)) return;
        if (! startedOrStopped && now - lastPushMs < 1000.0) return;      // at most once a second

        lastPushMs = now;
        pushed = content;
        pushedActive = serviceWanted;
        juce::androidUpdateConnectionNotification (serviceWanted, String::fromUTF8 (content.title.c_str()),
                                                   String::fromUTF8 (content.text.c_str()), content.live,
                                                   content.micAction == notification::MicAction::TurnOff ? 1
                                                 : content.micAction == notification::MicAction::TurnOn ? 2 : 0);
    }

    SonobusAudioProcessor& proc;
    EngineState& state;
    std::function<void()> disconnect;

    bool serviceWanted = false, pushedActive = false;
    notification::Content pushed;
    double lastPushMs = 0;
};

} // namespace crosspoint::android

#endif
