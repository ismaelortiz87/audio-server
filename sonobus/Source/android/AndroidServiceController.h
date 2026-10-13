// Android only (A10 + A14). The bridge between the engine and the phone:
//
//  - the foreground service and its Connection notification (android.md
//    section 4): the content is derived from the Console state (the data the live
//    band uses) by ConnectionNotification.h and pushed to Java only when title,
//    text, tint or actions change, at most once a second;
//  - the service lifetime (section 1): from connect until Disconnect or
//    swipe-away, whatever the connection state in between;
//  - the notification's Mic off/on and Disconnect actions, with the earcon (A14);
//  - phone calls (A14, section 6): Java reports them, this tells the processor;
//  - the microphone permission (A9, sections 3 and 7): asked on the first connect in
//    open-mic mode or on the first PTT press (never at launch), then notifications;
//    reported as platform.micPermission; the audio input is switched on when it is granted.
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
    /** `enableAudioInput`: re-open the audio device with the microphone, once it is granted after launch. */
    AndroidServiceController (SonobusAudioProcessor& p, EngineState& s, std::function<void()> disconnectNow,
                              std::function<void()> enableAudioInput = {}, bool audioInputAlreadyOn = true)
        : proc (p), state (s), disconnect (std::move (disconnectNow)),
          enableInput (std::move (enableAudioInput)), inputOn (audioInputAlreadyOn)
    {
        pollMic();
        startTimer (250);
    }

    ~AndroidServiceController() override
    {
        *alive = false;
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

    /** `platform.requestMic` (android.md sections 3 and 10): prompt -> the system dialog, denied -> the app's
        Android settings page. Message thread. */
    bool requestMic (String& code, String& msg)
    {
        pollMic();
        if (micGranted()) return true;
        if (Signals::get().micPermission.load() == 2)
        {
            if (! juce::androidOpenSettingsPage ("app")) { code = "internal"; msg = "Android could not open the app's settings"; return false; }
            return true;
        }
        showMicDialog();
        return true;
    }

private:
    static bool micGranted() { return Signals::get().micPermission.load() == 0; }

    /** 0 granted, 1/3 prompt, 2 denied (SonoBusActivity.micPermissionState); "prompt" while our dialog is up. */
    void pollMic()
    {
        const int raw = juce::androidMicPermissionState();
        micEverAsked = raw != 1;
        const int shown = raw == 0 ? 0 : raw == 2 && ! dialogUp ? 2 : 1;
        Signals::get().micPermission.store (shown);
        proc.setMicBlocked (shown != 0);
        if (shown == 0 && ! inputOn && enableInput)
        {
            inputOn = true;       // granted late (from the dialog or in Android Settings): open the audio input
            enableInput();
        }
    }

    void showMicDialog()
    {
        if (dialogUp) return;
        dialogUp = true;
        juce::androidMarkMicAsked();
        auto flag = alive;
        RuntimePermissions::request (RuntimePermissions::recordAudio, [this, flag] (bool)
        {
            if (! *flag) return;
            dialogUp = false;
            pollMic();
            juce::androidRequestNotificationPermission();      // android.md section 3: right after the mic answer
        });
    }

    /** The one moment the app asks (section 3): the first connect in open-mic mode, or the first PTT press. */
    void askOnFirstUse (bool connectTried)
    {
        if (askedOnce) return;
        const bool wouldSend = connectTried && proc.getMicMode() == SonobusAudioProcessor::MicMode::Open;
        const bool pttPress = proc.getPttHeld();
        if (! wouldSend && ! pttPress) return;
        askedOnce = true;
        if (Signals::get().micPermission.load() == 1 && ! micEverAsked) showMicDialog();
        else juce::androidRequestNotificationPermission();      // mic already answered: notifications, once (Java guards it)
    }

    void timerCallback() override
    {
        auto& sig = Signals::get();

        pollMic();

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
        askOnFirstUse (serviceWanted);

        notification::Input in;
        in.connState = connState.toStdString();
        in.group = conn["group"].toString().toStdString();
        in.reason = reason.toStdString();

        const var order = st["stationOrder"], stations = st["stations"];
        if (auto* ids = order.getArray())
            for (auto& idv : *ids)
            {
                const var s = stations.getProperty (idv.toString(), var());
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
        in.micGranted = micGranted();

        const auto content = notification::build (in);

        // The user allowed notifications after the service started: post it again so it shows now.
        const bool notifOk = sig.notificationsEnabled.load();
        if (notifOk != lastNotifOk) { lastNotifOk = notifOk; pushedActive = false; }

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

    std::function<void()> enableInput;
    bool inputOn = true, dialogUp = false, askedOnce = false, micEverAsked = false;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    bool serviceWanted = false, pushedActive = false, lastNotifOk = true;
    notification::Content pushed;
    double lastPushMs = 0;
};

} // namespace crosspoint::android

#endif
