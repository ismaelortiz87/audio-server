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

    /** A17: how a player button reaches the engine. Set by the app to
        ApiCommands::runLocalCommand, so the player runs the same command paths as
        the control API (routing rules and `hearsYou` stay engine-owned). */
    using CommandRunner = std::function<bool (const String& cmd, const var& args, String& error)>;
    void setCommandRunner (CommandRunner r) { runCommand = std::move (r); }

    /** A17: the selected station, for the player's title / Mute / Solo. Updated by
        the timer from `stationOrder` (android.md section 12.3). */
    String getSelectedStation() const { return selectedStation; }

    /** A17: up to 4 stations in `stationOrder`, as the JSON the Java player reads
        (section 12.4). Empty when there is nothing to show. */
    var getPlayerStations() const { return playerStations; }

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

    /** A18 (android.md section 12.2): the player's Mic button. 0 hidden (PTT mode or a phone call: a button
        cannot be held, and a call owns the mic), 3 "Allow mic" (not granted; opens the app), else 1 on / 2 off. */
    int playerMicState (const notification::Input& in) const
    {
        if (in.phoneCall || ! in.micOpenMode) return 0;
        if (! in.micGranted) return 3;
        return in.micOn ? 1 : 2;
    }

    void timerCallback() override
    {
        auto& sig = Signals::get();

        pollMic();

        // A14: a call starts / ends.
        proc.setPhoneCall (sig.phoneCall.load());

        // The notification's legacy actions.
        switch (sig.pendingAction.exchange (0))
        {
            case 1: toggleMicWithEarcon(); state.refreshNow(); break;
            case 2: disconnect(); state.refreshNow(); break;
            default: break;
        }

        const var st = state.getLastState();
        if (! st.isObject()) return;

        // A17: the lock-screen player. Keeps the selection and the pushed station
        // list current, then runs any button that was pressed while Java held it.
        updatePlayerStations (st);
        handlePlayerButton (static_cast<PlayerButton> (sig.pendingPlayerButton.exchange (0)));

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
        const int micUi = playerMicState (in);

        // The user allowed notifications after the service started: post it again so it shows now.
        const bool notifOk = sig.notificationsEnabled.load();
        if (notifOk != lastNotifOk) { lastNotifOk = notifOk; pushedActive = false; }

        const double now = Time::getMillisecondCounterHiRes();
        const bool startedOrStopped = serviceWanted != pushedActive;
        const bool changed = content != pushed || micUi != pushedMicUi;
        if (! startedOrStopped && ! (serviceWanted && changed)) return;
        if (! startedOrStopped && now - lastPushMs < 1000.0) return;      // at most once a second

        lastPushMs = now;
        pushed = content;
        pushedMicUi = micUi;
        pushedActive = serviceWanted;
        if (! serviceWanted && proc.isListeningPaused()) proc.setListeningPaused (false);    // Disconnect ends a pause
        juce::androidUpdateConnectionNotification (serviceWanted, String::fromUTF8 (content.title.c_str()),
                                                   String::fromUTF8 (content.text.c_str()), content.live, micUi);
    }

    SonobusAudioProcessor& proc;
    EngineState& state;
    std::function<void()> disconnect;

    std::function<void()> enableInput;
    bool inputOn = true, dialogUp = false, askedOnce = false, micEverAsked = false;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    bool serviceWanted = false, pushedActive = false, lastNotifOk = true;
    notification::Content pushed;
    int pushedMicUi = 0;
    double lastPushMs = 0;

    // ---- A17: the lock-screen player -----------------------------------------
    CommandRunner runCommand;
    String selectedStation;                 // id; empty when there are no stations
    var playerStations;                     // array of up to kPlayerMaxStations, for Java
    String lastPlayerJson, lastPlayerKey;   // change detection: only push when it differs
    double lastPlayerPushMs = 0;

    static constexpr int kPlayerMaxStations = 4;

    String shortName (const String& id) const
    {
        return id.startsWithIgnoreCase ("VDI-") ? id.substring (4) : id;
    }

    /** A JSON object variable. The engine's own `obj()` helper is local to
        EngineState.cpp, so this file needs its own. */
    static var makeObj (std::initializer_list<std::pair<const char*, var>> props)
    {
        auto* o = new DynamicObject();
        for (auto& p : props) o->setProperty (p.first, p.second);
        return var (o);
    }

    /** Section 12.3: keep the selection valid and the pushed state current. */
    void updatePlayerStations (const var& st)
    {
        const var order = st["stationOrder"], stations = st["stations"];

        // Rebuild the list the player reads: only what section 12.4 draws.
        Array<var> arr;
        StringArray ids;
        if (auto* o = order.getArray())
            for (auto& idv : *o)
            {
                const String id = idv.toString();
                const var s = stations.getProperty (id, var());
                if (! s.isObject()) continue;
                ids.add (id);
                if (arr.size() >= kPlayerMaxStations) continue;   // section 12.3: 4 stations at most
                arr.add (makeObj ({ { "id", id },
                                    { "name", shortName (id) },
                                    { "colorIndex", s["colorIndex"] },
                                    { "presence", s["presence"] },
                                    { "health", s["health"] },
                                    { "latencyMs", s["latencyMs"] },
                                    { "lossPct", s["lossPct"] },
                                    { "mute", s["mute"] },
                                    { "solo", s["solo"] },
                                    { "talk", s["talk"] },
                                    { "hearsYou", s["hearsYou"] } }));
            }
        playerStations = var (arr);

        // Selection: the soloed station if any, else the previous one if it is
        // still here, else the first (section 12.3). Offline stations are skipped
        // by the buttons, not by the selection, so a lost station can stay shown.
        // If the selected station went offline or was forgotten, move on to the next.
        String want;
        if (auto* o = order.getArray())
            for (auto& idv : *o)
            {
                const var s = stations.getProperty (idv.toString(), var());
                if (s.isObject() && static_cast<bool> (s["solo"])) { want = idv.toString(); break; }
            }
        if (want.isEmpty() && ids.contains (selectedStation)
            && stations.getProperty (selectedStation, var())["presence"].toString() != "offline")
            want = selectedStation;
        if (want.isEmpty())
        {
            const int from = jmax (0, ids.indexOf (selectedStation));
            for (int i = 0; i < ids.size() && want.isEmpty(); ++i)
            {
                const String id = ids[(from + i) % ids.size()];
                if (stations.getProperty (id, var())["presence"].toString() != "offline") want = id;
            }
            if (want.isEmpty() && ! ids.isEmpty()) want = ids[0];      // all offline: show the first, it says so
        }
        if (want != selectedStation) { selectedStation = want; lastPlayerJson.clear(); }

        const var conn = st["connection"], settings = st["settings"];
        const var doc = makeObj ({ { "selected", shortName (selectedStation) },
                                   { "connected", conn["state"].toString() == "connected" },
                                   { "listening", ! proc.isListeningPaused() },
                                   { "soloDimDb", settings.isObject() && ! settings["soloDimDb"].isVoid() ? settings["soloDimDb"] : var (-18) },
                                   { "stations", playerStations } });
        const String json = JSON::toString (doc, true);
        if (json == lastPlayerJson) return;

        // Section 4: only when something changed, at most once a second; meters never
        // go to the player. A button press changes the structure and goes out at once.
        const double now = Time::getMillisecondCounterHiRes();
        const String key = playerStructureKey (doc);
        if (key == lastPlayerKey && now - lastPlayerPushMs < 1000.0) return;
        lastPlayerJson = json;
        lastPlayerKey = key;
        lastPlayerPushMs = now;
        juce::androidUpdatePlayerState (json);
    }

    /** Everything in the player document except the latency / loss numbers. */
    static String playerStructureKey (const var& doc)
    {
        String k = doc["selected"].toString() + "|" + String ((int) static_cast<bool> (doc["connected"])) + "|"
                 + String ((int) static_cast<bool> (doc["listening"])) + "|" + doc["soloDimDb"].toString();
        if (auto* a = doc["stations"].getArray())
            for (auto& s : *a)
                k << "|" << s["name"].toString() << s["colorIndex"].toString() << s["presence"].toString()
                  << s["health"].toString() << (int) static_cast<bool> (s["mute"]) << (int) static_cast<bool> (s["solo"]);
        return k;
    }

    /** Section 12.2/12.3: run the pressed button through the API's command paths. */
    void handlePlayerButton (PlayerButton b)
    {
        if (b == PlayerButton::none || ! runCommand) return;
        String err;

        switch (b)
        {
            case PlayerButton::mic:
                toggleMicWithEarcon();                       // A14 earcon, open-mic mode only
                state.refreshNow();
                return;

            case PlayerButton::previous:
            case PlayerButton::next:
            {
                // Walk the stations in stationOrder, skipping offline ones, wrapping.
                const var st = state.getLastState();
                const var order = st["stationOrder"], stations = st["stations"];
                StringArray ids;
                if (auto* o = order.getArray())
                    for (auto& idv : *o)
                    {
                        const var s = stations.getProperty (idv.toString(), var());
                        if (s.isObject() && s["presence"].toString() != "offline") ids.add (idv.toString());
                    }
                if (ids.isEmpty()) return;
                int at = ids.indexOf (selectedStation);
                if (at < 0) at = 0;
                else at = (b == PlayerButton::next) ? (at + 1) % ids.size()
                                                    : (at - 1 + ids.size()) % ids.size();
                selectedStation = ids[at];
                lastPlayerJson.clear();                      // push the new title now
                state.refreshNow();
                return;
            }

            case PlayerButton::mute:
            case PlayerButton::solo:
            {
                if (selectedStation.isEmpty()) return;
                const var st = state.getLastState();
                const var s = st["stations"].getProperty (selectedStation, var());
                const bool on = s.isObject() && ! static_cast<bool> (s[b == PlayerButton::mute ? "mute" : "solo"]);
                const var args = b == PlayerButton::mute
                    ? makeObj ({ { "station", selectedStation }, { "on", on } })
                    : makeObj ({ { "station", selectedStation }, { "on", on }, { "exclusive", true } });   // P5.7
                runCommand (b == PlayerButton::mute ? "station.setMute" : "station.setSolo", args, err);
                state.refreshNow();
                return;
            }

            case PlayerButton::listen:
            {
                // Layout B centre. Pause: stop playing the stations and turn the mic off. Resume: play again;
                // the mic stays off (the mic never comes back by itself, section 6).
                const bool pause = ! proc.isListeningPaused();
                proc.setListeningPaused (pause);
                if (pause && proc.getMicMode() == SonobusAudioProcessor::MicMode::Open && proc.getMicOn())
                {
                    proc.setMicOn (false);
                    proc.playEarcon (false);
                }
                lastPlayerJson.clear();
                state.refreshNow();
                return;
            }

            case PlayerButton::disconnect:
                disconnect();
                state.refreshNow();
                return;

            case PlayerButton::none:
            default: return;
        }
    }
};

} // namespace crosspoint::android

#endif
