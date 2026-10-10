// P2.4 -- connect on launch and keep the headless agent connected.
//
// One message-thread state machine owns the server session of a headless peer:
//
//     Backoff --(delay elapsed)--> Connecting --(login ok)--> Joining --(group ok)--> Up
//        ^                              |                         |                    |
//        +------------- failure / timeout / connection lost ------+--------------------+
//
// Delays: 1 s, 2 s, 4 s ... capped at 30 s, each shortened by up to 20% random
// jitter (so a fleet of VDIs does not stampede a restarted server, and the cap
// is a hard maximum). A rejected password is not worth retrying quickly: the
// next attempt waits the full cap and the agent reports config_error.
//
// Events from the AOO client arrive on its event thread; the listener only
// queues them and the 200 ms timer (message thread) does all the work, so no
// connect/join call is ever made from the AOO threads.
//
// While this runs the stock ServerReconnectTimer is switched off
// (SonobusAudioProcessor::setAgentManagedReconnect), so the two cannot fight.
#pragma once

#include "SonobusPluginProcessor.h"

namespace crosspoint
{

class AgentConnector : private juce::Timer,
                       private SonobusAudioProcessor::ClientListener
{
public:
    AgentConnector (SonobusAudioProcessor& proc, const AooServerConnectionInfo& target);
    ~AgentConnector() override;

    /** Backoff delay in seconds (before test scaling) for the n-th consecutive failure, n >= 1.
        `rand01` in [0,1) is the jitter source; public so the unit test can pin it. */
    static double backoffSeconds (int failures, double rand01);

    /** TEST ONLY: SONOBUS_BACKOFF_SCALE in (0,1] shrinks every delay; 1 otherwise. */
    static double testScale();
    /** One stderr line, "Crosspoint agent [t=<secs>s]: ...". Shared with the device watcher. */
    static void log (const String& line);

private:
    enum class Phase { Backoff, Connecting, Joining, Up };
    struct Ev { enum Kind { Connected, Disconnected, Joined } kind; bool ok; String msg; };

    void timerCallback() override;
    void aooClientConnected (SonobusAudioProcessor*, bool success, const String& msg) override;
    void aooClientDisconnected (SonobusAudioProcessor*, bool success, const String& msg) override;
    void aooClientGroupJoined (SonobusAudioProcessor*, bool success, const String& group, const String& msg) override;

    void handle (const Ev& e, double now);
    void beginAttempt (double now);
    void failed (const String& why, double now);
    void publish (double now);

    SonobusAudioProcessor& proc;
    AooServerConnectionInfo target;
    const double scale;

    CriticalSection evLock;
    Array<Ev> events;

    Phase phase = Phase::Backoff;
    double phaseStartMs = 0, nextAttemptMs = 0;
    int attempt = 0;             // attempts since the last time we were Up
    int failures = 0;
    bool everUp = false;
    int ups = 0;
    String lastError, lastCode;
    juce::Random rng;
};

/** P2.4 -- watches the audio device of a headless peer. When it disappears or
    fails to open, reports input/output "missing" through the agent health
    (P1.7) and retries `reopen` with the same 1, 2, 4 ... 30 s backoff. */
class AgentDeviceWatcher : private juce::Timer
{
public:
    /** `reopen` returns "" on success, else why it failed. */
    AgentDeviceWatcher (SonobusAudioProcessor& proc, juce::AudioDeviceManager& dm,
                        std::function<juce::String()> reopen);
    ~AgentDeviceWatcher() override;

    /** TEST ONLY (F2 control file "audioLoss"): close the device now and make
        every reopen fail until cleared. */
    void setTestDeviceGone (bool gone);

private:
    void timerCallback() override;

    SonobusAudioProcessor& proc;
    juce::AudioDeviceManager& dm;
    std::function<juce::String()> reopen;
    const double scale;
    juce::Random rng;
    bool testGone = false;
    bool lost = false;
    int badTicks = 0;
    int attempts = 0;
    double nextAttemptMs = 0;
};

} // namespace crosspoint
