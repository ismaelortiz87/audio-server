// Engine side of the control API state (P4.2). See EngineState.h.

#include "EngineState.h"

#include <cmath>
#include <ctime>

namespace crosspoint {

using Proc = SonobusAudioProcessor;

namespace {

constexpr double kLostAfterMs        = 3000;    // spec 3.5: no packets from an online peer
constexpr double kHealthWindowMs     = 10000;   // spec 3.5: loss window, growth window, hysteresis
constexpr double kLossLimitPct       = 1.0;
constexpr double kLatencyLimitMs     = 120.0;
constexpr double kBufferGrowEpsMs    = 1.0;
constexpr int    kMinPacketsForLoss  = 20;      // do not call 1 of 3 packets "33 % loss"
constexpr double kDetailEveryMs      = 500;
constexpr int    kNumColors          = 4;

var obj (std::initializer_list<std::pair<const char*, var>> props)
{
    auto* o = new DynamicObject();
    for (auto& p : props) o->setProperty (p.first, p.second);
    return var (o);
}

var strOrNull (const String& s)           { return s.isEmpty() ? var() : var (s); }

var stringArrayVar (const StringArray& a)
{
    Array<var> out;
    for (auto& s : a) out.add (s);
    return var (out);
}

double round1 (double v)                  { return std::round (v * 10.0) / 10.0; }

String isoUtc (int64 unixSeconds)
{
    std::time_t t = (std::time_t) unixSeconds;
    std::tm tmv {};
   #if JUCE_WINDOWS
    gmtime_s (&tmv, &t);
   #else
    gmtime_r (&t, &tmv);
   #endif
    char buf[32];
    std::strftime (buf, sizeof (buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

/** Online stations show a lastSeen truncated to the minute so it does not
    produce a patch every second; lost/offline ones show the exact second. */
String lastSeenString (const Time& t, bool online)
{
    int64 s = t.toMilliseconds() / 1000;
    if (online) s -= s % 60;
    return isoUtc (s);
}

float gainToLevelDb (float gain)
{
    const float db = gain <= 0.0f ? -40.0f : Decibels::gainToDecibels (gain, -40.0f);
    return (float) round1 (jlimit (-40.0f, 6.0f, db));
}

float levelDbToGain (float db)            { return db <= -40.0f ? 0.0f : Decibels::decibelsToGain (jlimit (-40.0f, 6.0f, db)); }

String serverString (Proc& p)
{
    String host; int port = 0;
    if (!p.getServerEndpointInfo (host, port)) return {};
    return host + ":" + String (port);
}

} // namespace

//==============================================================================
String EngineState::escapePointer (const String& key)
{
    return key.replace ("~", "~0").replace ("/", "~1");
}

void EngineState::diffVars (const var& a, const var& b, const String& ptr, Array<var>& ops)
{
    auto* oa = a.getDynamicObject();
    auto* ob = b.getDynamicObject();

    if (oa != nullptr && ob != nullptr)
    {
        const auto& pa = oa->getProperties();
        const auto& pb = ob->getProperties();

        for (int i = 0; i < pa.size(); ++i)
        {
            const auto key = pa.getName (i);
            if (! pb.contains (key))
                ops.add (obj ({ { "op", "del" }, { "path", ptr + "/" + escapePointer (key.toString()) } }));
        }
        for (int i = 0; i < pb.size(); ++i)
        {
            const auto key = pb.getName (i);
            const auto path = ptr + "/" + escapePointer (key.toString());
            if (! pa.contains (key))
                ops.add (obj ({ { "op", "set" }, { "path", path }, { "value", pb.getValueAt (i) } }));
            else
                diffVars (*pa.getVarPointer (key), pb.getValueAt (i), path, ops);
        }
    }
    else if (JSON::toString (a, true) != JSON::toString (b, true))
    {
        // arrays and scalars are replaced whole
        ops.add (obj ({ { "op", "set" }, { "path", ptr }, { "value", b } }));
    }
}

//==============================================================================
EngineState::EngineState (Proc& p, AudioDeviceManager* dm, ApiServer& s, const Options& o)
    : processor (p), deviceManager (dm), server (s), options (o)
{
    startMs = Time::getMillisecondCounterHiRes();
    stationsTree = processor.getApiStationsTree();

    previous = options.role == "vdi" ? buildAgentState (startMs) : buildConsoleState (startMs);
    snapshot = previous;

    server.setStateProvider ([this]
    {
        std::lock_guard<std::mutex> g (snapshotLock);
        return snapshot;
    });

    startTimer (jmax (20, options.intervalMs));
}

EngineState::~EngineState()
{
    stopTimer();
    // The server outlives us: stop answering from a dangling `this`.
    server.setStateProvider (nullptr);   // back to the placeholder; no broadcast
}

void EngineState::timerCallback()   { tick(); }
void EngineState::refreshNow()      { tick(); }

void EngineState::tick()
{
    const double now = Time::getMillisecondCounterHiRes();
    var next = options.role == "vdi" ? buildAgentState (now) : buildConsoleState (now);

    Array<var> ops;
    diffVars (previous, next, {}, ops);
    if (ops.isEmpty()) return;

    server.publishPatch (var (ops), [this, next]
    {
        std::lock_guard<std::mutex> g (snapshotLock);
        snapshot = next;
    });
    previous = next;
}

//==============================================================================
// Persistence ("remembered stations", api 7 / P4.2)

void EngineState::loadPersistedStations()
{
    if (persistedLoaded) return;
    persistedLoaded = true;

    for (auto c : stationsTree)
    {
        const String id = c.getProperty ("id").toString();
        if (id.isEmpty() || stations.count (id) != 0) continue;

        auto s = std::make_unique<Station>();
        s->id = id;
        s->colorIndex = jlimit (0, kNumColors - 1, (int) c.getProperty ("colorIndex", 0));
        s->level = (float) (double) c.getProperty ("level", 0.0);
        s->pan   = (float) (double) c.getProperty ("pan", 0.0);
        s->mute  = (bool) c.getProperty ("mute", false);
        s->talk  = (bool) c.getProperty ("talk", true);
        s->known = true;
        s->lastSeen = Time::fromISO8601 (c.getProperty ("lastSeen", "").toString());
        if (s->lastSeen.toMilliseconds() == 0) s->lastSeen = Time::getCurrentTime();
        stations[id] = std::move (s);
    }
}

int EngineState::assignColorIndex() const
{
    // lowest free index among the remembered stations; with more than four,
    // the least used one (colours repeat, which the UI tolerates).
    int uses[kNumColors] = {};
    for (auto& kv : stations) ++uses[jlimit (0, kNumColors - 1, kv.second->colorIndex)];
    int best = 0;
    for (int i = 1; i < kNumColors; ++i)
        if (uses[i] < uses[best]) best = i;
    return best;
}

EngineState::Station& EngineState::stationFor (const String& id)
{
    auto it = stations.find (id);
    if (it != stations.end()) return *it->second;

    auto s = std::make_unique<Station>();
    s->id = id;
    s->colorIndex = assignColorIndex();
    s->lastSeen = Time::getCurrentTime();
    auto& ref = *s;
    stations[id] = std::move (s);
    return ref;
}

void EngineState::persist (const Station& s)
{
    ValueTree node;
    for (auto c : stationsTree)
        if (c.getProperty ("id").toString() == s.id) { node = c; break; }

    if (! node.isValid())
    {
        node = ValueTree ("Station");
        node.setProperty ("id", s.id, nullptr);
        stationsTree.appendChild (node, nullptr);
    }
    // setProperty only notifies when the value changed, so this is cheap.
    node.setProperty ("colorIndex", s.colorIndex, nullptr);
    node.setProperty ("level", (double) s.level, nullptr);
    node.setProperty ("pan", (double) s.pan, nullptr);
    node.setProperty ("mute", s.mute, nullptr);
    node.setProperty ("talk", s.talk, nullptr);
    node.setProperty ("lastSeen", lastSeenString (s.lastSeen, s.inPeers && s.everConnected && ! s.lost), nullptr);
}

bool EngineState::forgetStation (const String& id)
{
    auto it = stations.find (id);
    if (it == stations.end()) return false;
    if (it->second->inPeers && it->second->everConnected) return false;   // present: busy

    for (int i = stationsTree.getNumChildren(); --i >= 0;)
        if (stationsTree.getChild (i).getProperty ("id").toString() == id)
            stationsTree.removeChild (i, nullptr);
    stations.erase (it);
    refreshNow();
    return true;
}

//==============================================================================
// Health (spec 3.5): unstable if loss > 1 % over 10 s, latency > 120 ms, or the
// jitter buffer grew within the last 10 s; clear only after 10 s under all of
// them (hysteresis).

void EngineState::updateHealth (Station& s, double now, bool online)
{
    if (! online)
    {
        s.samples.clear();
        s.unstable = false;
        s.lastBufferMs = -1;
        return;
    }

    const int i = s.peerIndex;
    const Sample cur { now, (int64) processor.getRemotePeerPacketsReceived (i),
                            (int64) processor.getRemotePeerPacketsDropped (i) };
    if (! s.samples.empty() && (cur.recv < s.samples.back().recv || cur.dropped < s.samples.back().dropped))
        s.samples.clear();   // counters were reset
    s.samples.push_back (cur);
    while (s.samples.size() > 2 && s.samples[1].t <= now - kHealthWindowMs) s.samples.pop_front();

    double lossPct = 0.0;
    {
        const auto& f = s.samples.front();
        const int64 dRecv = cur.recv - f.recv, dDrop = cur.dropped - f.dropped;
        if (dDrop > 0 && dRecv + dDrop >= kMinPacketsForLoss)
            lossPct = 100.0 * (double) dDrop / (double) (dRecv + dDrop);
    }

    const double buf = processor.getRemotePeerBufferTime (i);
    if (s.lastBufferMs >= 0 && buf > s.lastBufferMs + kBufferGrowEpsMs) s.lastGrowMs = now;
    s.lastBufferMs = buf;

    if (now - s.lastDetailMs >= kDetailEveryMs)
    {
        s.lastDetailMs = now;
        Proc::LatencyInfo li;
        if (processor.getRemotePeerLatencyInfo (i, li))
        {
            // one-way latency of the audio we receive from this station
            const double ms = li.isreal ? li.incomingMs : li.pingMs * 0.5 + buf;
            const int v = (int) std::lround (ms);
            if (! s.hasLatency || std::abs (v - s.latencyMs) >= 2) s.latencyMs = v;
            s.hasLatency = true;
        }
        const int jb = (int) std::lround (buf);
        if (std::abs (jb - s.jitterBufferMs) >= 1) s.jitterBufferMs = jb;
    }

    const bool bad = lossPct > kLossLimitPct
                  || (s.hasLatency && s.latencyMs > kLatencyLimitMs)
                  || (now - s.lastGrowMs) < kHealthWindowMs;
    if (bad)                                                   { s.unstable = true; s.lastBadMs = now; }
    else if (s.unstable && now - s.lastBadMs >= kHealthWindowMs)  s.unstable = false;

    s.lossPctShown = round1 (lossPct);
}

//==============================================================================
// The Console mix model (P1.5): talk / hearsYou / mute / solo, mic.* and
// settings.soloDimDb all come from P1.5's processor API, sourced only here.

void EngineState::fillMixFields (const Station& s, MixFields& out) const
{
    // pan = channel 0 of group 0 (VDIs send mono, one group).
    const int i = s.peerIndex;
    out.levelDb = (float) round1 (processor.getRemotePeerLevelDb (i));
    out.pan     = (float) round1 (processor.getRemotePeerChannelPan (i, 0, 0));
    out.mute    = processor.getRemotePeerMuted (i);      // P1.5 playback-gain mute
    out.solo    = processor.getRemotePeerSoloed (i);
    out.talk    = processor.getRemotePeerTalk (i);
    out.hearsYou = processor.getRemotePeerHearsYou (i);  // the engine's own gate; the caller ANDs in "online"
}

void EngineState::applyMixFields (const Station& s, const MixFields& in) const
{
    // Restore a remembered station's mix on rejoin (level, pan, mute, talk).
    const int i = s.peerIndex;
    processor.setRemotePeerLevelDb (i, in.levelDb);
    processor.setRemotePeerChannelPan (i, 0, 0, in.pan);
    processor.setRemotePeerMuted (i, in.mute);
    processor.setRemotePeerTalk (i, in.talk);
}

void EngineState::fillMicFields (MicFields& out) const
{
    out.mode = processor.getMicMode() == Proc::MicMode::PushToTalk ? "ptt" : "open";
    out.on = processor.getMicOn();
    out.pttHeld = processor.getPttHeld();
    out.transmitting = processor.isMicTransmitting();
}

int EngineState::soloDimDb() const
{
    return (int) std::lround (processor.getSoloDimDb());
}

//==============================================================================
String EngineState::deviceName (bool input) const
{
    if (deviceManager == nullptr) return {};
    const auto setup = deviceManager->getAudioDeviceSetup();
    return input ? setup.inputDeviceName : setup.outputDeviceName;
}

var EngineState::devicesVar (bool agent)
{
    Array<var> ins, outs;
    if (deviceManager != nullptr)
    {
        if (auto* type = deviceManager->getCurrentDeviceTypeObject())
        {
            // names come from JUCE's cached scan; no rescan on the timer. Linux
            // PipeWire nodes are P2.11; for now these are the JUCE (ALSA) device names.
            for (auto& n : type->getDeviceNames (true))
                ins.add (agent ? obj ({ { "node", n }, { "description", n }, { "hint", "" } })
                               : obj ({ { "id", n }, { "name", n } }));
            for (auto& n : type->getDeviceNames (false))
                outs.add (agent ? obj ({ { "node", n }, { "description", n }, { "hint", "" } })
                                : obj ({ { "id", n }, { "name", n } }));
        }
    }
    return obj ({ { "inputs", var (ins) }, { "outputs", var (outs) } });
}

var EngineState::connectionVar (double now, bool agent)
{
    const bool connected = processor.isConnectedToServer();
    const String group = processor.getCurrentJoinedGroup();
    const String server = serverString (processor);
    if (connected) everConnectedToServer = true;

    String state, reason;
    var retry;
    if (connected && group.isNotEmpty())                  state = "connected";
    else if (connected)                                   state = "connecting";   // joining the group
    else if (processor.isRecoveringFromServerLoss())      { state = "reconnecting"; retry = 1; }
    else if (! everConnectedToServer && server.isNotEmpty() && now - startMs < 15000.0) state = "connecting";
    else                                                  { state = "failed"; reason = everConnectedToServer ? "Connection to the server was lost" : "Not connected to a server"; }

    if (agent)
        return obj ({ { "state", state == "failed" ? String ("error") : state }, { "server", server }, { "group", group },
                      { "attempt", var() }, { "retryInSec", retry },
                      { "error", state == "failed"
                                   ? obj ({ { "code", "server_unreachable" }, { "message", reason } })
                                   : var() } });

    bool pwSaved = false;
    if (group.isNotEmpty())
    {
        Array<AooServerConnectionInfo> recents;
        processor.getRecentServerConnectionInfos (recents);
        for (auto& r : recents)
            if (r.groupName == group && r.groupPassword.isNotEmpty()) { pwSaved = true; break; }
    }
    return obj ({ { "state", state }, { "server", server }, { "group", group }, { "passwordSaved", pwSaved },
                  { "reason", strOrNull (reason) }, { "retryInSec", retry } });
}

//==============================================================================
void EngineState::readPeers (double now, StringArray& unknownPeers, StringArray& otherConsoles)
{
    for (auto& kv : stations) kv.second->inPeers = false;

    const int n = processor.getNumberRemotePeers();
    for (int i = 0; i < n; ++i)
    {
        Proc::ApiPeerInfo info;
        if (! processor.getRemotePeerApiInfo (i, info) || info.userName.isEmpty()) continue;

        if (! info.hasRole || info.role == Proc::PeerRole::Unknown) { unknownPeers.addIfNotAlreadyThere (info.userName); continue; }
        if (info.role == Proc::PeerRole::Console)                   { otherConsoles.addIfNotAlreadyThere (info.userName); continue; }

        // a VDI station. Stations are only created once connected.
        auto it = stations.find (info.userName);
        if (it == stations.end() && ! info.connected) continue;
        auto& s = stationFor (info.userName);
        if (s.inPeers && s.info.connected) continue;   // duplicate entry: keep the connected one
        s.inPeers = true;
        s.peerIndex = i;
        s.info = info;
    }

    for (auto& kv : stations)
    {
        auto& s = *kv.second;
        if (! s.inPeers)
        {
            // gone: next join is a fresh one
            s.everConnected = false; s.restored = false; s.lost = false; s.lastBytes = -1; s.peerIndex = -1;
            updateHealth (s, now, false);
            continue;
        }

        const int64 bytes = processor.getRemotePeerBytesReceived (s.peerIndex);
        if (s.lastBytes < 0 || bytes != s.lastBytes) { s.lastPacketMs = now; s.lastBytes = bytes; }

        if (! s.everConnected && s.info.connected)
        {
            s.everConnected = true;
            s.lastPacketMs = now;
            s.lastBufferMs = -1;
            s.lastGrowMs = -1e12;
        }
        const bool online = s.everConnected && s.info.connected && (now - s.lastPacketMs) < kLostAfterMs;
        const bool lostNow = s.everConnected && ! online;
        if (lostNow && ! s.lost) s.lostSinceMs = now;
        s.lost = lostNow;

        if (s.everConnected && ! s.restored)
        {
            s.restored = true;
            // a station that rejoins gets its remembered level/pan/mute back;
            // a brand-new one adopts the engine's current values.
            if (s.known)
            {
                MixFields m; m.levelDb = s.level; m.pan = s.pan; m.mute = s.mute; m.talk = s.talk;
                applyMixFields (s, m);
            }
        }
        if (online) s.lastSeen = Time::getCurrentTime();
        updateHealth (s, now, online);
    }
}

//==============================================================================
var EngineState::buildConsoleState (double now)
{
    loadPersistedStations();

    StringArray unknownPeers, otherConsoles;
    readPeers (now, unknownPeers, otherConsoles);

    // mix values, then the engine-computed hearsYou (spec 3.2)
    std::map<String, MixFields> mix;
    bool anySolo = false;
    for (auto& kv : stations)
    {
        auto& s = *kv.second;
        MixFields m;
        if (s.inPeers && s.everConnected)
        {
            fillMixFields (s, m);
            s.level = m.levelDb; s.pan = m.pan; s.mute = m.mute; s.talk = m.talk;
            s.known = true;
        }
        else
        {
            m.levelDb = s.level; m.pan = s.pan; m.mute = s.mute; m.talk = s.talk; m.solo = false;
        }
        anySolo = anySolo || m.solo;
        mix[s.id] = m;
    }

    MicFields mic;
    fillMicFields (mic);

    // order: colour index, remembered (offline) stations last
    std::vector<Station*> order;
    for (auto& kv : stations) order.push_back (kv.second.get());
    auto presenceRank = [] (const Station* s) { return (s->inPeers && s->everConnected) ? 0 : 1; };
    std::stable_sort (order.begin(), order.end(), [&] (const Station* a, const Station* b)
    {
        if (presenceRank (a) != presenceRank (b)) return presenceRank (a) < presenceRank (b);
        if (a->colorIndex != b->colorIndex) return a->colorIndex < b->colorIndex;
        return a->id < b->id;
    });

    auto* stationObjs = new DynamicObject();
    StringArray orderIds;
    for (auto* sp : order)
    {
        auto& s = *sp;
        const bool present = s.inPeers && s.everConnected;
        const bool online = present && ! s.lost;
        const String presence = ! present ? "offline" : (s.lost ? "lost" : "online");
        auto& m = mix[s.id];
        // The engine's gate (P1.5) is authoritative; a station we consider lost
        // (no packets for 3 s) never shows "Hears you".
        m.hearsYou = m.hearsYou && online;

        var agent;
        if (present && s.info.hasAgent)
            agent = obj ({ { "input", s.info.agent.input }, { "inputNode", var() }, { "silentForMin", var() },
                           { "output", s.info.agent.output }, { "outputNode", var() },
                           { "paused", s.info.agent.paused }, { "configError", strOrNull (s.info.agent.configError) } });

        persist (s);

        stationObjs->setProperty (s.id, obj ({
            { "id", s.id }, { "name", s.id }, { "colorIndex", s.colorIndex },
            { "presence", presence },
            { "lostForSec", s.lost ? var ((int) jmax (0.0, std::floor ((now - s.lostSinceMs) / 1000.0))) : var() },
            { "lastSeen", lastSeenString (s.lastSeen, online) },
            { "health", online && s.unstable ? "unstable" : "clear" },
            { "latencyMs", online && s.hasLatency ? var (s.latencyMs) : var() },
            { "lossPct", online ? var (s.lossPctShown) : var() },
            { "jitterBufferMs", online ? var (s.jitterBufferMs) : var() },
            { "agent", agent },
            { "level", (double) m.levelDb }, { "pan", (double) m.pan },
            { "mute", m.mute }, { "solo", m.solo }, { "talk", m.talk }, { "hearsYou", m.hearsYou } }));
        orderIds.add (s.id);
    }

    // settings
    var codec = "opus", bitrate;
    {
        Proc::AudioCodecFormatInfo ci;
        if (processor.getAudioCodeFormatInfo (processor.getDefaultAudioCodecFormat(), ci))
        {
            codec = ci.codec == Proc::CodecOpus ? "opus" : "pcm";
            if (ci.codec == Proc::CodecOpus) bitrate = ci.bitrate / 1000;
        }
    }
    const bool autoBuf = processor.getDefaultAutoresizeBufferMode() != Proc::AutoNetBufferModeOff;
    const double defBuf = processor.getValueTreeState().getRawParameterValue (Proc::paramDefaultNetbufMs)->load();
    int bufNow = (int) std::lround (defBuf);
    for (auto* sp : order)
        if (sp->inPeers && sp->everConnected && ! sp->lost) { bufNow = sp->jitterBufferMs; break; }
    const double wet = processor.getValueTreeState().getRawParameterValue (Proc::paramWet)->load();

    String name = processor.getCurrentUsername();
    if (name.isEmpty()) name = options.selfName;

    return obj ({
        { "self", obj ({ { "name", name }, { "role", "console" }, { "kind", processor.getSelfConsoleKind() } }) },
        { "connection", connectionVar (now, false) },
        { "stationOrder", stringArrayVar (orderIds) },
        { "stations", var (stationObjs) },
        { "unknownPeers", stringArrayVar (unknownPeers) },
        { "otherConsoles", stringArrayVar (otherConsoles) },
        { "mic", obj ({ { "mode", mic.mode }, { "on", mic.on }, { "pttHeld", mic.pttHeld },
                        { "transmitting", mic.transmitting }, { "device", deviceName (true) } }) },
        { "output", obj ({ { "device", deviceName (false) }, { "level", (double) gainToLevelDb ((float) wet) } }) },
        { "settings", obj ({ { "soloDimDb", soloDimDb() }, { "pttHotkey", var() },
                             { "codec", codec }, { "bitrateKbps", bitrate },
                             { "networkBuffer", obj ({ { "mode", autoBuf ? "auto" : "manual" },
                                                       { "currentMs", bufNow } }) } }) },
        { "devices", devicesVar (false) } });
}

//==============================================================================
var EngineState::buildAgentState (double now)
{
    // consoles that are connected to us
    Array<var> consoles;
    const bool refreshLat = now - consoleLatencyAtMs >= kDetailEveryMs;
    if (refreshLat) consoleLatencyAtMs = now;

    const int n = processor.getNumberRemotePeers();
    for (int i = 0; i < n; ++i)
    {
        Proc::ApiPeerInfo info;
        if (! processor.getRemotePeerApiInfo (i, info) || info.userName.isEmpty()) continue;
        if (! info.hasRole || info.role != Proc::PeerRole::Console || ! info.connected) continue;

        if (refreshLat)
        {
            Proc::LatencyInfo li;
            if (processor.getRemotePeerLatencyInfo (i, li))
            {
                const double ms = li.isreal ? li.outgoingMs : li.pingMs * 0.5;   // our audio towards the Console
                const int v = (int) std::lround (ms);
                auto it = consoleLatency.find (info.userName);
                if (it == consoleLatency.end() || std::abs (it->second - v) >= 2) consoleLatency[info.userName] = v;
            }
        }
        auto it = consoleLatency.find (info.userName);
        const String kind = (info.kind == "mac" || info.kind == "web") ? info.kind : String ("other");
        // TODO(P4.x): "talking" needs a per-peer receive-activity signal; false until the engine has one.
        consoles.add (obj ({ { "name", info.userName }, { "kind", kind },
                             { "latencyMs", it != consoleLatency.end() ? var (it->second) : var() },
                             { "talking", false } }));
    }

    const auto health = processor.getSelfAgentHealth();
    const String sending = health.paused ? "paused" : (consoles.isEmpty() ? "idle" : "active");

    const String inName = deviceName (true), outName = deviceName (false);
    String name = processor.getCurrentUsername();
    if (name.isEmpty()) name = options.selfName;

    return obj ({
        { "self", obj ({ { "name", name }, { "role", "vdi" } }) },
        { "connection", connectionVar (now, true) },
        { "sending", sending },
        { "consoles", var (consoles) },
        { "input",  obj ({ { "node", inName },  { "description", inName },  { "status", health.input },
                           { "silentForMin", var() } }) },
        { "output", obj ({ { "node", outName }, { "description", outName }, { "status", health.output } }) },
        { "devices", devicesVar (true) },
        { "configPath", strOrNull (options.configPath) },
        { "configError", strOrNull (options.configError) } });
}

} // namespace crosspoint
