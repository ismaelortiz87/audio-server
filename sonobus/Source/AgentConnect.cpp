// P2.4 -- see AgentConnect.h.
#include "AgentConnect.h"

#include <cmath>
#include <iostream>

namespace crosspoint
{

static constexpr double kCapSeconds = 30.0;
static constexpr double kAttemptTimeoutSeconds = 15.0;

double AgentConnector::testScale()
{
    // TEST ONLY: SONOBUS_BACKOFF_SCALE=0.2 shrinks every delay and timeout 5x so
    // F2 can see the whole 1,2,4...cap ladder in seconds. The shape is unchanged.
    const auto v = SystemStats::getEnvironmentVariable ("SONOBUS_BACKOFF_SCALE", "").trim();
    const double d = v.getDoubleValue();
    return (d > 0.0 && d <= 1.0) ? d : 1.0;
}

double AgentConnector::backoffSeconds (int failures, double rand01)
{
    const int n = juce::jmax (1, failures);
    const double base = juce::jmin (kCapSeconds, std::pow (2.0, (double) juce::jmin (n - 1, 10)));
    return base * (1.0 - 0.2 * juce::jlimit (0.0, 1.0, rand01));   // jitter only shortens: the cap is a maximum
}

AgentConnector::AgentConnector (SonobusAudioProcessor& p, const AooServerConnectionInfo& t)
    : proc (p), target (t), scale (testScale())
{
    proc.setAgentManagedReconnect (true);
    proc.addClientListener (this);
    nextAttemptMs = Time::getMillisecondCounterHiRes();
    publish (nextAttemptMs);
    startTimer (200);
}

AgentConnector::~AgentConnector()
{
    stopTimer();
    proc.removeClientListener (this);
    proc.setAgentManagedReconnect (false);
}

void AgentConnector::log (const String& line)
{
    std::cerr << "Crosspoint agent [t=" << String (Time::getMillisecondCounterHiRes() / 1000.0, 3)
              << "s]: " << line << std::endl;
}

// AOO event thread: queue only.
void AgentConnector::aooClientConnected (SonobusAudioProcessor*, bool success, const String& msg)
{
    const ScopedLock sl (evLock);
    events.add ({ Ev::Connected, success, msg });
}
void AgentConnector::aooClientDisconnected (SonobusAudioProcessor*, bool success, const String& msg)
{
    const ScopedLock sl (evLock);
    events.add ({ Ev::Disconnected, success, msg });
}
void AgentConnector::aooClientGroupJoined (SonobusAudioProcessor*, bool success, const String&, const String& msg)
{
    const ScopedLock sl (evLock);
    events.add ({ Ev::Joined, success, msg });
}

void AgentConnector::retryNow()
{
    if (phase == Phase::Backoff) nextAttemptMs = Time::getMillisecondCounterHiRes();
}

void AgentConnector::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();

    Array<Ev> batch;
    {
        const ScopedLock sl (evLock);
        batch.swapWith (events);
    }
    for (auto& e : batch) handle (e, now);

    switch (phase)
    {
        case Phase::Up:
            if (! proc.isConnectedToServer())
                failed ("connection to the server was lost", now);
            break;
        case Phase::Connecting:
        case Phase::Joining:
            if (now - phaseStartMs > juce::jmax (8.0, kAttemptTimeoutSeconds * scale)   /* > the AOO client's own 5 s handshake timeout */ * 1000.0)
            {
                lastCode = "server_unreachable";
                failed (phase == Phase::Connecting ? "timed out connecting to the server" : "timed out joining the group", now);
            }
            break;
        case Phase::Backoff:
            if (now >= nextAttemptMs) beginAttempt (now);
            break;
    }
    publish (now);
}

void AgentConnector::handle (const Ev& e, double now)
{
    switch (e.kind)
    {
        case Ev::Connected:
            if (phase != Phase::Connecting) break;
            if (e.ok)
            {
                phase = Phase::Joining;
                phaseStartMs = now;
                proc.setWatchPublicGroups (false);
                proc.joinServerGroup (target.groupName, target.groupPassword, target.groupIsPublic);
            }
            else
            {
                lastCode = e.msg.containsIgnoreCase ("wrong password") ? "bad_password" : "server_unreachable";
                failed (e.msg.isEmpty() ? String ("could not connect to the server") : e.msg, now);
            }
            break;

        case Ev::Joined:
            if (phase != Phase::Joining) break;
            if (e.ok)
            {
                phase = Phase::Up;
                everUp = true;
                ++ups;
                const int n = attempt;
                attempt = 0;
                failures = 0;
                lastError.clear();
                lastCode.clear();
                proc.setAgentRecovering (false);
                proc.setAgentConfigError ({});
                auto info = target;
                info.timestamp = Time::getCurrentTime().toMilliseconds();
                proc.addRecentServerConnectionInfo (info);
                log ("connected to " + target.serverHost + ":" + String (target.serverPort) + " group '" + target.groupName
                     + "' as '" + target.userName + "' (attempt " + String (n) + ")");
            }
            else
            {
                lastCode = (e.msg.containsIgnoreCase ("wrong password") || e.msg.containsIgnoreCase ("permission denied"))
                               ? "bad_password" : "server_unreachable";
                failed ("could not join group '" + target.groupName + "': " + (e.msg.isEmpty() ? String ("unknown error") : e.msg), now);
            }
            break;

        case Ev::Disconnected:
            if (e.ok) break;   // we asked for it
            // On macOS a refused TCP connect is only noticed by the AOO handshake
            // timeout (5 s), which arrives as a Disconnected event: count it.
            if (phase == Phase::Up || phase == Phase::Joining || phase == Phase::Connecting)
            {
                lastCode = "server_unreachable";
                failed ("disconnected from the server" + (e.msg.isNotEmpty() ? " (" + e.msg + ")" : String()), now);
            }
            break;
    }
}

void AgentConnector::beginAttempt (double now)
{
    ++attempt;
    phaseStartMs = now;
    log ("connect attempt " + String (attempt) + " to " + target.serverHost + ":" + String (target.serverPort)
         + " group '" + target.groupName + "' as '" + target.userName + "'");

    if (proc.isConnectedToServer())
    {
        // Logged in already (a failed group join): retry only the join.
        phase = Phase::Joining;
        proc.joinServerGroup (target.groupName, target.groupPassword, target.groupIsPublic);
        return;
    }

    phase = Phase::Connecting;
    if (! proc.connectToServer (target.serverHost, target.serverPort, target.userName, target.userPassword))
    {
        lastCode = "server_unreachable";
        failed ("the connect request was refused by the engine", now);
    }
}

void AgentConnector::failed (const String& why, double now)
{
    if (phase == Phase::Up) proc.setAgentRecovering (true);   // keep peers across the outage, like stock SonoBus

    ++failures;
    lastError = why;
    const bool badPassword = lastCode == "bad_password";

    // A rejected password will not fix itself: go straight to the cap and say so.
    const double delay = (badPassword ? kCapSeconds : backoffSeconds (failures, rng.nextDouble())) * scale;

    proc.setAgentConfigError (badPassword ? "server rejected the group/user password: " + why : String());

    phase = Phase::Backoff;
    phaseStartMs = now;
    nextAttemptMs = now + delay * 1000.0;
    log ((attempt == 0 ? String ("connection lost") : "attempt " + String (attempt) + " failed")
         + " (" + why + "); retrying in " + String (delay, 2) + " s");
}

void AgentConnector::publish (double now)
{
    SonobusAudioProcessor::AgentConnStatus s;
    s.ups = ups;
    switch (phase)
    {
        case Phase::Up:
            s.state = "connected";
            break;
        case Phase::Connecting:
        case Phase::Joining:
            s.state = everUp ? "reconnecting" : "connecting";
            s.attempt = attempt;
            break;
        case Phase::Backoff:
            s.attempt = attempt;
            if (failures == 0) { s.state = "connecting"; break; }
            s.state = lastCode == "bad_password" ? "failed" : "reconnecting";
            s.retryInSec = juce::jmax (0.0, (nextAttemptMs - now) / 1000.0);
            s.errorCode = lastCode;
            s.reason = lastError;
            break;
    }
    proc.setAgentConnStatus (s);
}

//==============================================================================
AgentDeviceWatcher::AgentDeviceWatcher (SonobusAudioProcessor& p, AudioDeviceManager& d, std::function<String()> r)
    : proc (p), dm (d), reopen (std::move (r)), scale (AgentConnector::testScale())
{
    startTimer (1000);
}

AgentDeviceWatcher::~AgentDeviceWatcher() { stopTimer(); }

void AgentDeviceWatcher::setTestDeviceGone (bool gone)
{
    if (gone == testGone) return;
    testGone = gone;
    if (gone) dm.closeAudioDevice();   // what a vanished device looks like to us
}

void AgentDeviceWatcher::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    auto* dev = dm.getCurrentAudioDevice();
    const bool open = ! testGone && dev != nullptr && dev->isOpen() && dev->isPlaying();

    if (open)
    {
        badTicks = 0;
        if (lost)
        {
            lost = false;
            proc.setAgentDevicesOpen (true, true);
            AgentConnector::log ("audio device is back (reopen attempt " + String (attempts) + ")");
            attempts = 0;
        }
        return;
    }

    // Ignore a blip (the device manager briefly stops while it reconfigures).
    if (! lost && ! testGone && ++badTicks < 3) return;

    if (! lost)
    {
        lost = true;
        attempts = 0;
        nextAttemptMs = now;
        proc.setAgentDevicesOpen (false, false);
        AgentConnector::log ("audio device lost: input/output now reported missing");
    }

    if (now < nextAttemptMs) return;

    ++attempts;
    const String err = testGone ? String ("device not found (test)") : reopen();
    if (err.isEmpty() && dm.getCurrentAudioDevice() != nullptr)
    {
        // The next tick confirms it is playing; if it is not, the next attempt
        // waits the normal backoff rather than reopening every second.
        AgentConnector::log ("audio reopen attempt " + String (attempts) + " opened the device");
        nextAttemptMs = now + AgentConnector::backoffSeconds (attempts + 1, 1.0) * scale * 1000.0;
        return;
    }

    const double delay = AgentConnector::backoffSeconds (attempts, rng.nextDouble()) * scale;
    nextAttemptMs = now + delay * 1000.0;
    AgentConnector::log ("audio reopen attempt " + String (attempts) + " failed (" + err.upToFirstOccurrenceOf ("\n", false, false)
                         + "); retrying in " + String (delay, 2) + " s");
}

} // namespace crosspoint
