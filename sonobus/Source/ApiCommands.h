// Control API meters stream (P4.3) and commands (P4.4), docs/control-api.md
// sections 4 and 5. The behaviour (validation, clamping, error codes, PTT per
// session, spread/centerAll) follows console-ui/src/api/mock.js.
//
// Threading: everything here runs on the JUCE message thread (the API server
// posts every `cmd` there; the meters are a 30 Hz Timer). The audio thread is
// only touched through the processor's atomic setters and the meter sources it
// already updates.

#pragma once

#include "JuceHeader.h"
#include "ApiServer.h"
#include "EngineState.h"
#include "SonobusPluginProcessor.h"

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace crosspoint {

class ApiCommands : private Timer
{
public:
    /** Things only the application knows how to do (Config / YAML / PipeWire). */
    struct Hooks
    {
        /** agent.setInput / agent.setOutput. Switches the device live and persists it
            to the YAML. On failure returns false with an api error code and message. */
        std::function<bool (bool isInput, const String& node, String& previous,
                            String& errCode, String& errMessage)> setAgentDevice;
        /** agent.reloadConfig. */
        std::function<bool (String& errCode, String& errMessage)> reloadConfig;
        /** connection.connect/disconnect on a Console: stop the P2.4 connector that
            would otherwise reconnect behind the user's back. */
        std::function<void()> takeOverConnection;
        /** agent.retryNow when a connector owns the session; returns false if none. */
        std::function<bool()> retryNow;
        /** The connection the app was started with (headless -c/-g/-n or the YAML). */
        AooServerConnectionInfo initialConnection;
        bool hasInitialConnection = false;
    };

    ApiCommands (SonobusAudioProcessor& proc, AudioDeviceManager* deviceManager,
                 ApiServer& server, EngineState& state, const String& role, Hooks hooks);
    ~ApiCommands() override;

    /** Message thread. Connects to the server and joins the group once connected;
        the values are saved to the recent connections when the join was issued.
        Used by connection.connect, agent.retryNow and agent.reloadConfig. */
    void connectWith (const AooServerConnectionInfo& info);

    /** Frame builders, exposed for tests of the dB mapping. */
    static var levelPair (float peak, float rms);

private:
    struct CmdError { String code, message; };

    void handleCommand (std::shared_ptr<ApiSession> session, const var& message);
    var dispatch (ApiSession& session, const String& cmd, const var& args);
    var consoleCommand (ApiSession& session, const String& cmd, const var& a);
    var agentCommand (const String& cmd, const var& a);

    // console helpers
    void setStationMix (const String& id, std::optional<float> level, std::optional<float> pan,
                        std::optional<bool> mute, std::optional<bool> talk);
    void setStationSolo (const String& id, bool on);
    void requireStation (const var& a);
    void applyPtt();
    void connectionConnect (const var& a);
    void connectionDisconnect();
    void setDevice (bool isInput, const String& id);
    void setSettings (const var& a);

    // meters
    void timerCallback() override;
    void sendMeters();
    var readMeter (foleys::LevelMeterSource* src) const;
    void pollPendingConnect();

    SonobusAudioProcessor& processor;
    AudioDeviceManager* deviceManager;
    ApiServer& server;
    EngineState& state;
    const String role;
    Hooks hooks;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };

    std::set<int64> pttHolders;                 // session ids holding push-to-talk

    struct PendingConnect { AooServerConnectionInfo info; double startedMs = 0; };
    std::unique_ptr<PendingConnect> pending;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApiCommands)
};

} // namespace crosspoint
