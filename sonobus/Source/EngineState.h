// Engine side of the control API state (P4.2, docs/control-api.md section 3).
//
// Builds the Console state (3.2) or the VDI agent state (3.3) from the
// processor, keeps the previous snapshot, diffs it into JSON-pointer set/del ops
// (same semantics as console-ui/src/lib/patch.js diff(): objects are walked key
// by key, arrays and scalars are replaced whole) and publishes them through
// ApiServer::publishPatch(). New sessions get the last snapshot through the
// server's state provider.
//
// Threading: everything here runs on the JUCE message thread (a 10 Hz Timer).
// The only code that runs elsewhere is the state provider, which just returns
// the last published snapshot (an immutable var tree) under a tiny mutex.
// Nothing touches the audio thread; the processor is read through its existing
// thread-safe getters.
//
// P4.4 hooks: forgetStation(), and the stations tree that the command handler
// changes through the processor setters (EngineState picks changes up on the
// next tick and persists them).

#pragma once

#include "JuceHeader.h"
#include "ApiServer.h"
#include "SonobusPluginProcessor.h"

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace crosspoint {

class EngineState : private Timer
{
public:
    struct Options
    {
        String role = "console";         // "console" or "vdi"
        String selfName;                 // fallback when the processor has no username yet
        String configPath;               // agent: the loaded YAML, else ""
        String configError;              // agent: last config load/reload error, else ""
        int intervalMs = 100;
        std::function<String()> pttHotkey;   // P6.5: the global PTT binding ("⌥Space"), "" or unset = none (settings.pttHotkey null)
    };

    /** `deviceManager` may be null (no device info is reported then). Registers
        itself as the server's state provider and starts the timer. */
    EngineState (SonobusAudioProcessor& proc, AudioDeviceManager* deviceManager,
                 ApiServer& server, const Options& options);
    ~EngineState() override;

    /** Message thread. Removes a remembered station. Returns false when the
        station is unknown or still present (online or lost); the command layer
        answers `busy`/`not_found` (api 5.1). */
    bool forgetStation (const String& id);

    // P4.4 helpers (message thread) -------------------------------------------
    /** True for any remembered or present station. */
    bool knowsStation (const String& id) const;
    /** True while the station is in the peer table and has connected (online or lost);
        only then do the processor setters reach it. */
    bool stationIsPresent (const String& id) const;
    /** Stores new mix values of a station that is NOT present (offline); they are
        persisted and applied when it rejoins. Unset optionals keep their value. */
    void updateRemembered (const String& id, std::optional<float> levelDb, std::optional<float> pan,
                           std::optional<bool> mute, std::optional<bool> talk);
    /** stationOrder of the last published state. */
    StringArray getStationOrder() const;
    /** Stations whose last published presence is "online" (P4.3 meters). */
    StringArray getOnlineStations() const;
    /** connection.disconnect: shows reason "Disconnected" instead of "lost" until the next connect. */
    void setUserDisconnected (bool disconnected)
    {
        userDisconnected = disconnected;
        if (disconnected) userDisconnectedAtMs = Time::getMillisecondCounterHiRes();
    }
    /** agent.reloadConfig result: shown as state.configError and connection.error. */
    void setConfigError (const String& err)         { configError = err; }

    /** Message thread. The state as of the last tick (A10: the Android notification is
        derived from the same data the Console shows). */
    var getLastState() const                        { return previous; }

    /** Message thread. Builds the state now and publishes the difference. Called
        by the timer; tests and P4.4 can call it after a change to avoid waiting
        for the next tick. */
    void refreshNow();

    // Pure helpers, exposed for the unit-style checks in the test.
    static void diffVars (const var& before, const var& after, const String& pointer, Array<var>& ops);
    static String escapePointer (const String& key);

private:
    //==========================================================================
    struct Sample { double t; int64 recv; int64 dropped; };

    struct Station
    {
        String id;
        int colorIndex = 0;
        // persisted (ExtraState/Stations)
        float level = 0.0f, pan = 0.0f;
        bool mute = false, talk = true;
        Time lastSeen;                   // exact (ms)
        // runtime
        bool inPeers = false;            // listed in the processor's peer table this tick
        bool everConnected = false;      // seen connected since it (re)joined
        bool restored = false;           // saved level/pan/mute pushed back into the engine
        bool known = false;              // has remembered values (from disk or an earlier join)
        double lossPctShown = 0.0;
        int peerIndex = -1;
        double lastPacketMs = 0;         // last time its received byte counter moved
        int64 lastBytes = -1;
        bool lost = false;
        double lostSinceMs = 0;
        std::deque<Sample> samples;
        double lastBufferMs = -1;
        double lastGrowMs = -1e12;
        bool unstable = false;
        double lastBadMs = -1e12;
        double lastDetailMs = -1e12;     // latency/jitter are refreshed at 2 Hz
        int latencyMs = 0, jitterBufferMs = 0;
        bool hasLatency = false;
        SonobusAudioProcessor::ApiPeerInfo info;
        // what fillMixFields() reads from the engine
    };

    // The mix-related values of one station, read from / written to the engine.
    // P1.5 owns the semantics of talk, mute, solo and hearsYou; see fillMixFields().
    struct MixFields
    {
        float levelDb = 0.0f;
        float pan = 0.0f;
        bool mute = false, solo = false, talk = true;
        bool hearsYou = false;           // filled in a second pass (needs all stations)
    };

    struct MicFields
    {
        String mode = "open";
        bool on = true, pttHeld = false, transmitting = true;
    };

    void timerCallback() override;
    void tick();

    var buildConsoleState (double nowMs);
    var buildAgentState (double nowMs);

    // station bookkeeping (Console)
    void loadPersistedStations();
    Station& stationFor (const String& id);
    int assignColorIndex() const;
    void persist (const Station& s);
    void updateHealth (Station& s, double nowMs, bool connected);
    void readPeers (double nowMs, StringArray& unknownPeers, StringArray& otherConsoles);

    // P1.5 switch-over point: ALL sourcing of talk / hearsYou / mute / solo / mic.* /
    // settings.soloDimDb lives in these functions (EngineState.cpp, clearly marked).
    void fillMixFields (const Station& s, MixFields& out) const;
    void applyMixFields (const Station& s, const MixFields& in) const;
    void fillMicFields (MicFields& out) const;
    int  soloDimDb() const;

    var connectionVar (double nowMs, bool agent);
    var devicesVar (bool agent);
    String deviceName (bool input) const;
    String agentNode (bool input) const;          // the node name shown for the open device
    String nodeDescription (bool input, const String& node);

   #if JUCE_ANDROID
    // A8: the phone's audio route and echo state (EngineState.cpp)
    void refreshAndroidRoute (double nowMs);
    const char* androidOutputRoute() const;
    var androidDevicesVar();
    var androidPlatformVar (bool anyHearsYou);
    int androidDefaultOut = 0, androidDefaultIn = 0;   // AudioDeviceInfo types "System Default" resolves to
    double androidRouteAtMs = -1e12;
    bool aecChecked = false, aecWorking = false;
   #endif

    // Linux agent (P2.11): PipeWire nodes for the pickers, from pw-dump, cached.
    struct PwNode { String node, description; };
    std::vector<PwNode> pwInputs, pwOutputs;
    double pwListedAtMs = -1e12;
    bool pwListed = false;
    void refreshPipeWireNodes (double nowMs);

    //==========================================================================
    SonobusAudioProcessor& processor;
    AudioDeviceManager* deviceManager;
    ApiServer& server;
    const Options options;

    ValueTree stationsTree;              // the processor's ExtraState/Stations handle
    std::map<String, std::unique_ptr<Station>> stations;
    bool persistedLoaded = false;

    double startMs = 0;
    std::map<String, int> consoleLatency;   // agent role: latency per Console, 2 Hz with hysteresis
    double consoleLatencyAtMs = -1e12;
    bool everConnectedToServer = false;
    bool userDisconnected = false;       // P4.4
    double userDisconnectedAtMs = 0;     // the socket may still look connected for a moment after an explicit Disconnect
    String configError;                  // P4.4: starts as options.configError

    var previous;                        // last published snapshot (message thread)
    bool published = false;

    std::mutex snapshotLock;             // guards `snapshot` (read by the provider thread)
    var snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineState)
};

} // namespace crosspoint
