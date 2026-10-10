// Control API meters (P4.3) and commands (P4.4). See ApiCommands.h.

#include "ApiCommands.h"

#include <cmath>

namespace crosspoint {

using Proc = SonobusAudioProcessor;

namespace {

constexpr int    kMeterIntervalMs   = 33;       // ~30 frames/s
constexpr double kSilenceDb         = -90.0;
constexpr double kPendingTimeoutMs  = 20000;

var obj (std::initializer_list<std::pair<const char*, var>> props)
{
    auto* o = new DynamicObject();
    for (auto& p : props) o->setProperty (p.first, p.second);
    return var (o);
}

double round1 (double v)                { return std::round (v * 10.0) / 10.0; }
double round2 (double v)                { return std::round (v * 100.0) / 100.0; }

bool isNumber (const var& v)
{
    return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v);
}

} // namespace

//==============================================================================
var ApiCommands::levelPair (float peak, float rms)
{
    auto one = [] (float g) -> var
    {
        const double db = Decibels::gainToDecibels ((double) g, -100.0);
        return db < kSilenceDb ? var() : var (round1 (db));
    };
    Array<var> a;
    a.add (one (peak));
    a.add (one (rms));
    return var (a);
}

ApiCommands::ApiCommands (Proc& p, AudioDeviceManager* dm, ApiServer& s, EngineState& st,
                          const String& r, Hooks h)
    : processor (p), deviceManager (dm), server (s), state (st), role (r), hooks (std::move (h))
{
    auto flag = alive;
    server.setCommandHandler ([this, flag] (std::shared_ptr<ApiSession> session, const var& msg)
    {
        if (flag->load()) handleCommand (std::move (session), msg);
    });
    server.setSessionClosedHandler ([this, flag] (int64 id)
    {
        if (! flag->load()) return;
        if (pttHolders.erase (id) > 0) applyPtt();     // api 2.4: a dropped client releases its hold
    });
    startTimer (kMeterIntervalMs);
}

ApiCommands::~ApiCommands()
{
    stopTimer();
    alive->store (false);
    server.setCommandHandler (nullptr);
    server.setSessionClosedHandler (nullptr);
}

//==============================================================================
// P4.3 meters

var ApiCommands::readMeter (foleys::LevelMeterSource* src) const
{
    if (src == nullptr) return levelPair (0.0f, 0.0f);
    float peak = 0.0f, rms = 0.0f;
    try
    {
        const int n = src->getNumChannels();
        for (int ch = 0; ch < n; ++ch)
        {
            peak = jmax (peak, src->getMaxLevel (ch));
            rms  = jmax (rms,  src->getRMSLevel (ch));
        }
    }
    catch (const std::out_of_range&) {}     // resized while we read: skip this frame
    return levelPair (peak, rms);
}

void ApiCommands::timerCallback()
{
    pollPendingConnect();
    sendMeters();
}

void ApiCommands::sendMeters()
{
    if (! server.hasSubscribers ("meters")) return;

    auto* frame = new DynamicObject();
    var frameVar (frame);
    frame->setProperty ("t", "meters");
    frame->setProperty ("ts", (int64) Time::currentTimeMillis());

    if (role == "vdi")
    {
        const var input = readMeter (&processor.getInputMeterSource());
        frame->setProperty ("input", input);
        frame->setProperty ("output", readMeter (&processor.getOutputMeterSource()));

        const auto wantsDevices = [] (const ApiSession& s) { return s.isSubscribed ("deviceMeters"); };
        const auto noDevices    = [] (const ApiSession& s) { return ! s.isSubscribed ("deviceMeters"); };

        server.broadcastTopic ("meters", JSON::toString (frameVar, true), noDevices);

        if (server.hasSubscribers ("meters", wantsDevices))
        {
            // One level per available input node. The engine measures only the device
            // it has open, so that is the only entry (the others have no meter).
            auto* devs = new DynamicObject();
            if (deviceManager != nullptr)
            {
                const auto name = deviceManager->getAudioDeviceSetup().inputDeviceName;
                if (name.isNotEmpty()) devs->setProperty (name, input);
            }
            frame->setProperty ("devices", var (devs));
            server.broadcastTopic ("meters", JSON::toString (frameVar, true), wantsDevices);
        }
        return;
    }

    auto* stations = new DynamicObject();
    for (auto& id : state.getOnlineStations())
    {
        const int idx = processor.getRemotePeerIndexByName (id);     // re-fetched every frame
        if (idx < 0) continue;
        stations->setProperty (id, readMeter (processor.getRemotePeerPreFaderMeterSource (idx)));
    }
    frame->setProperty ("stations", var (stations));
    frame->setProperty ("mic", readMeter (&processor.getSendMeterSource()));        // pre-gate (sendWorkBuffer)
    frame->setProperty ("output", readMeter (&processor.getOutputMeterSource()));
    server.broadcastTopic ("meters", JSON::toString (frameVar, true));
}

//==============================================================================
// P4.4 commands

void ApiCommands::handleCommand (std::shared_ptr<ApiSession> session, const var& message)
{
    const String id = message.getProperty ("id", var()).toString();
    const String cmd = message.getProperty ("cmd", var()).toString();
    try
    {
        var args = message.getProperty ("args", var());
        if (args.isVoid()) args = obj ({});
        if (args.getDynamicObject() == nullptr)
            throw CmdError { "bad_request", "args must be an object" };

        var result = dispatch (*session, cmd, args);
        session->sendAck (id, true, {}, {}, result);
    }
    catch (const CmdError& e)
    {
        session->sendAck (id, false, e.code, e.message);
    }
    catch (const std::exception& e)
    {
        session->sendAck (id, false, "internal", e.what());
    }
    state.refreshNow();       // so the resulting patch goes out now, not at the next 10 Hz tick
}

var ApiCommands::dispatch (ApiSession& session, const String& cmd, const var& a)
{
    if (role == "vdi")
    {
        if (cmd.startsWith ("agent.")) return agentCommand (cmd, a);
        throw CmdError { "bad_request", "Unknown command " + cmd };
    }
    if (cmd.startsWith ("agent.")) throw CmdError { "bad_request", "Unknown command " + cmd };
    return consoleCommand (session, cmd, a);
}

namespace {

double needNumber (const var& a, const char* key)
{
    const var v = a.getProperty (key, var());
    if (! isNumber (v)) throw std::invalid_argument (String (key).toStdString() + " must be a number");
    return (double) v;
}

bool needBool (const var& a, const char* key)
{
    const var v = a.getProperty (key, var());
    if (! v.isBool()) throw std::invalid_argument (String (key).toStdString() + " must be true or false");
    return (bool) v;
}

String needString (const var& a, const char* key)
{
    const var v = a.getProperty (key, var());
    if (! v.isString() || v.toString().isEmpty()) throw std::invalid_argument (String (key).toStdString() + " must be a non-empty string");
    return v.toString();
}

} // namespace

// Argument validation throws std::invalid_argument; map it to bad_request here.
#define P44_VALIDATE(expr) [&] { try { return (expr); } catch (const std::invalid_argument& e) { throw CmdError { "bad_request", e.what() }; } }()

void ApiCommands::requireStation (const var& a)
{
    const var v = a.getProperty ("station", var());
    if (! v.isString()) throw CmdError { "bad_request", "station must be a string" };
    if (! state.knowsStation (v.toString())) throw CmdError { "not_found", "No station " + v.toString() };
}

void ApiCommands::setStationMix (const String& id, std::optional<float> level, std::optional<float> pan,
                                 std::optional<bool> mute, std::optional<bool> talk)
{
    const int idx = state.stationIsPresent (id) ? processor.getRemotePeerIndexByName (id) : -1;
    if (idx >= 0)
    {
        if (level) processor.setRemotePeerLevelDb (idx, *level);
        if (pan)   processor.setRemotePeerChannelPan (idx, 0, 0, *pan);
        if (mute)  processor.setRemotePeerMuted (idx, *mute);
        if (talk)  processor.setRemotePeerTalk (idx, *talk);
    }
    else
    {
        // offline: remembered values, applied when the station rejoins
        state.updateRemembered (id, level, pan, mute, talk);
    }
}

void ApiCommands::setStationSolo (const String& id, bool on)
{
    const int idx = state.stationIsPresent (id) ? processor.getRemotePeerIndexByName (id) : -1;
    if (idx < 0)
    {
        if (! on) return;      // un-solo of an offline station: nothing to undo
        throw CmdError { "busy", "Station " + id + " is offline; only a connected station can be soloed" };
    }
    processor.setRemotePeerSoloed (idx, on);
}

void ApiCommands::applyPtt()
{
    // A hold only counts in ptt mode (mock: pttHeld = mode == ptt && any holder).
    processor.setPttHeld (processor.getMicMode() == Proc::MicMode::PushToTalk && ! pttHolders.empty());
}

var ApiCommands::consoleCommand (ApiSession& session, const String& cmd, const var& a)
{
    // ---- connection --------------------------------------------------------
    if (cmd == "connection.connect")    { connectionConnect (a); return {}; }
    if (cmd == "connection.disconnect") { connectionDisconnect(); return {}; }

    // ---- stations ----------------------------------------------------------
    if (cmd == "station.setLevel")
    {
        requireStation (a);
        const float v = (float) jlimit (-40.0, 6.0, P44_VALIDATE (needNumber (a, "db")));
        setStationMix (a["station"].toString(), v, std::nullopt, std::nullopt, std::nullopt);
        return {};
    }
    if (cmd == "station.setPan")
    {
        requireStation (a);
        const float v = (float) jlimit (-1.0, 1.0, P44_VALIDATE (needNumber (a, "pan")));
        setStationMix (a["station"].toString(), std::nullopt, v, std::nullopt, std::nullopt);
        return {};
    }
    if (cmd == "station.setMute")
    {
        requireStation (a);
        const bool v = P44_VALIDATE (needBool (a, "on"));
        setStationMix (a["station"].toString(), std::nullopt, std::nullopt, v, std::nullopt);
        return {};
    }
    if (cmd == "station.setTalk")
    {
        requireStation (a);
        const bool v = P44_VALIDATE (needBool (a, "on"));
        setStationMix (a["station"].toString(), std::nullopt, std::nullopt, std::nullopt, v);
        return {};
    }
    if (cmd == "station.setSolo")
    {
        requireStation (a);
        const bool v = P44_VALIDATE (needBool (a, "on"));
        setStationSolo (a["station"].toString(), v);
        return {};
    }
    if (cmd == "station.forget")
    {
        requireStation (a);
        if (! state.forgetStation (a["station"].toString()))
            throw CmdError { "busy", "Only offline stations can be forgotten" };
        return {};
    }
    if (cmd == "stations.talkToAll")
    {
        for (auto& id : state.getStationOrder())
            setStationMix (id, std::nullopt, std::nullopt, std::nullopt, true);
        return {};
    }
    if (cmd == "stations.centerAll")
    {
        for (auto& id : state.getStationOrder())
            setStationMix (id, std::nullopt, 0.0f, std::nullopt, std::nullopt);
        return {};
    }
    if (cmd == "stations.spread")
    {
        const auto ids = state.getStationOrder();
        const int n = ids.size();
        for (int i = 0; i < n; ++i)
        {
            const double pan = n == 1 ? 0.0 : round2 (-1.0 + (2.0 * i) / (n - 1));
            setStationMix (ids[i], std::nullopt, (float) pan, std::nullopt, std::nullopt);
        }
        return {};
    }

    // ---- mic ---------------------------------------------------------------
    if (cmd == "mic.setMode")
    {
        const String m = a.getProperty ("mode", var()).toString();
        if (m != "open" && m != "ptt") throw CmdError { "bad_request", "mode must be open or ptt" };
        pttHolders.clear();                       // switching releases any hold (api 5.1)
        processor.setMicMode (m == "ptt" ? Proc::MicMode::PushToTalk : Proc::MicMode::Open);
        processor.setPttHeld (false);
        return {};
    }
    if (cmd == "mic.setOn")
    {
        if (processor.getMicMode() != Proc::MicMode::Open)
            throw CmdError { "wrong_mode", "mic.setOn only works in open-mic mode" };
        processor.setMicOn (P44_VALIDATE (needBool (a, "on")));
        return {};
    }
    if (cmd == "mic.ptt")
    {
        const bool down = P44_VALIDATE (needBool (a, "down"));
        if (down) pttHolders.insert (session.getId()); else pttHolders.erase (session.getId());
        applyPtt();
        return {};
    }

    // ---- output, devices, settings ----------------------------------------
    if (cmd == "output.setLevel")
    {
        const double db = jlimit (-40.0, 6.0, P44_VALIDATE (needNumber (a, "db")));
        const float gain = db <= -40.0 ? 0.0f : Decibels::decibelsToGain ((float) db);
        if (auto* p = processor.getValueTreeState().getParameter (Proc::paramWet))
            p->setValueNotifyingHost (p->convertTo0to1 (gain));
        return {};
    }
    if (cmd == "devices.setInput" || cmd == "devices.setOutput")
    {
        setDevice (cmd == "devices.setInput", P44_VALIDATE (needString (a, "id")));
        return {};
    }
    if (cmd == "settings.set") { setSettings (a); return {}; }

    throw CmdError { "bad_request", "Unknown command " + cmd };
}

//==============================================================================
void ApiCommands::setDevice (bool isInput, const String& id)
{
    if (deviceManager == nullptr) throw CmdError { "not_supported", "No audio device manager" };
    auto* type = deviceManager->getCurrentDeviceTypeObject();
    if (type == nullptr || ! type->getDeviceNames (isInput).contains (id))
        throw CmdError { "not_found", "No device " + id };

    auto setup = deviceManager->getAudioDeviceSetup();
    const auto before = setup;
    (isInput ? setup.inputDeviceName : setup.outputDeviceName) = id;
    const String err = deviceManager->setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty())
    {
        deviceManager->setAudioDeviceSetup (before, true);
        throw CmdError { "internal", "Could not open the device: " + err };
    }
}

void ApiCommands::setSettings (const var& a)
{
    // All-or-nothing: validate every key first.
    auto* o = a.getDynamicObject();
    std::optional<float> dim;
    std::optional<int> format;
    String unsupported;

    for (auto& kv : o->getProperties())
    {
        const String key = kv.name.toString();
        if (key == "soloDimDb")
        {
            if (! isNumber (kv.value)) throw CmdError { "bad_request", "soloDimDb must be a number" };
            dim = (float) (double) kv.value;
        }
        else if (key == "pttHotkey" || key == "networkBuffer")
        {
            unsupported << (unsupported.isEmpty() ? "" : ", ") << key;
        }
        else if (key != "codec" && key != "bitrateKbps")
        {
            throw CmdError { "bad_request", "Unknown setting " + key };
        }
    }

    if (unsupported.isNotEmpty())
        throw CmdError { "not_supported", "Not supported by this engine: " + unsupported
                                          + (unsupported.contains ("pttHotkey") ? " (no global hotkey here)" : "") };

    if (o->hasProperty ("codec") || o->hasProperty ("bitrateKbps"))
    {
        const var codecV = o->getProperty ("codec"), brV = o->getProperty ("bitrateKbps");
        if (o->hasProperty ("codec") && codecV.toString() != "opus" && codecV.toString() != "pcm")
            throw CmdError { "bad_request", "codec must be opus or pcm" };
        if (o->hasProperty ("bitrateKbps") && ! isNumber (brV))
            throw CmdError { "bad_request", "bitrateKbps must be a number" };

        Proc::AudioCodecFormatInfo cur;
        const bool curOk = processor.getAudioCodeFormatInfo (processor.getDefaultAudioCodecFormat(), cur);
        const bool wantPcm = o->hasProperty ("codec") ? codecV.toString() == "pcm" : (curOk && cur.codec == Proc::CodecPCM);
        if (wantPcm && o->hasProperty ("bitrateKbps"))
            throw CmdError { "bad_request", "bitrateKbps only applies to opus" };

        int found = -1;
        String supported;
        const int wantBitrate = o->hasProperty ("bitrateKbps") ? (int) std::lround ((double) brV) * 1000
                              : (curOk && cur.codec == Proc::CodecOpus ? cur.bitrate : 96000);
        for (int i = 0; i < processor.getNumberAudioCodecFormats(); ++i)
        {
            Proc::AudioCodecFormatInfo info;
            if (! processor.getAudioCodeFormatInfo (i, info)) continue;
            if (wantPcm) { if (info.codec == Proc::CodecPCM && info.bitdepth == 2) { found = i; break; } }
            else if (info.codec == Proc::CodecOpus)
            {
                supported << (supported.isEmpty() ? "" : ", ") << info.bitrate / 1000;
                if (info.bitrate == wantBitrate) found = i;
            }
        }
        if (found < 0) throw CmdError { "bad_request", "Unsupported bitrate; supported kbps: " + supported };
        format = found;
    }

    if (dim) processor.setSoloDimDb (*dim);
    if (format) processor.setDefaultAudioCodecFormat (*format);   // the send format of new connections
}

//==============================================================================
// connection.*

void ApiCommands::connectWith (const AooServerConnectionInfo& info)
{
    pending.reset();
    if (processor.isConnectedToServer() && ! processor.isRecoveringFromServerLoss())
    {
        const auto g = processor.getCurrentJoinedGroup();
        if (g.isNotEmpty()) processor.leaveServerGroup (g);
        processor.disconnectFromServer();
    }
    state.setUserDisconnected (false);
    if (! processor.connectToServer (info.serverHost, info.serverPort, info.userName, info.userPassword))
        throw CmdError { "internal", "Could not start the connection" };

    pending = std::make_unique<PendingConnect>();
    pending->info = info;
    pending->startedMs = Time::getMillisecondCounterHiRes();
}

void ApiCommands::pollPendingConnect()
{
    if (pending == nullptr) return;
    if (processor.isConnectedToServer())
    {
        auto info = pending->info;
        pending.reset();
        processor.setWatchPublicGroups (false);
        processor.joinServerGroup (info.groupName, info.groupPassword, info.groupIsPublic);
        info.timestamp = Time::getCurrentTime().toMilliseconds();
        processor.addRecentServerConnectionInfo (info);        // "saves on success", like the UI
    }
    else if (Time::getMillisecondCounterHiRes() - pending->startedMs > kPendingTimeoutMs)
    {
        pending.reset();                                        // the state shows the failure
    }
}

void ApiCommands::connectionConnect (const var& a)
{
    if (hooks.takeOverConnection) hooks.takeOverConnection();
    for (auto* k : { "server", "group", "password", "name" })
        if (a.hasProperty (k) && ! a.getProperty (k, var()).isString())
            throw CmdError { "bad_request", String (k) + " must be a string" };
    if (a.hasProperty ("group") && a["group"].toString().isEmpty())
        throw CmdError { "bad_request", "Group is required" };

    // saved values: what is live now, else the most recent connection, else how we started
    Array<AooServerConnectionInfo> recents;
    processor.getRecentServerConnectionInfos (recents);
    AooServerConnectionInfo base;
    base.serverPort = DEFAULT_SERVER_PORT;
    if (hooks.hasInitialConnection) base = hooks.initialConnection;
    if (! recents.isEmpty()) base = recents.getReference (0);

    String host = base.serverHost;
    int port = base.serverPort;
    String liveHost; int livePort = 0;
    if (processor.getServerEndpointInfo (liveHost, livePort) && liveHost.isNotEmpty()) { host = liveHost; port = livePort; }
    String group = processor.getCurrentJoinedGroup();
    if (group.isEmpty()) group = base.groupName;
    String user = processor.getCurrentUsername();
    if (user.isEmpty()) user = base.userName;

    if (a.hasProperty ("server"))
    {
        String s = a["server"].toString().trim();
        const int colon = s.lastIndexOfChar (':');
        port = DEFAULT_SERVER_PORT;
        if (colon > 0 && s.substring (colon + 1).containsOnly ("0123456789") && s.substring (colon + 1).isNotEmpty())
        {
            port = s.substring (colon + 1).getIntValue();
            s = s.substring (0, colon);
        }
        if (s.isEmpty() || port < 1 || port > 65535) throw CmdError { "bad_request", "Invalid server address" };
        host = s;
    }
    if (a.hasProperty ("group")) group = a["group"].toString().trim();
    if (a.hasProperty ("name") && a["name"].toString().trim().isNotEmpty()) user = a["name"].toString().trim();

    if (host.isEmpty()) throw CmdError { "bad_request", "Server is required" };
    if (group.isEmpty()) throw CmdError { "bad_request", "Group is required" };
    if (user.isEmpty()) user = SystemStats::getComputerName();

    String password;
    if (a.hasProperty ("password")) password = a["password"].toString();
    else
    {
        for (auto& r : recents)
            if (r.groupName == group && r.serverHost == host) { password = r.groupPassword; break; }
        if (password.isEmpty() && hooks.hasInitialConnection && hooks.initialConnection.groupName == group)
            password = hooks.initialConnection.groupPassword;
    }

    AooServerConnectionInfo info;
    info.serverHost = host; info.serverPort = port;
    info.userName = user;   info.userPassword = {};
    info.groupName = group; info.groupPassword = password; info.groupIsPublic = false;
    connectWith (info);
}

void ApiCommands::connectionDisconnect()
{
    if (hooks.takeOverConnection) hooks.takeOverConnection();
    pending.reset();
    const auto g = processor.getCurrentJoinedGroup();
    if (g.isNotEmpty()) processor.leaveServerGroup (g);
    processor.disconnectFromServer();
    state.setUserDisconnected (true);
}

//==============================================================================
// agent.*

var ApiCommands::agentCommand (const String& cmd, const var& a)
{
    if (cmd == "agent.setInput" || cmd == "agent.setOutput")
    {
        const bool isInput = cmd == "agent.setInput";
        const String node = P44_VALIDATE (needString (a, "node"));
        String previous, code, message;
        if (! hooks.setAgentDevice)
            throw CmdError { "not_supported", "Device switching is not available" };
        if (! hooks.setAgentDevice (isInput, node, previous, code, message))
            throw CmdError { code, message };
        return obj ({ { "previous", previous } });
    }
    if (cmd == "agent.testTone")
    {
        const String node = P44_VALIDATE (needString (a, "node"));
        // The engine plays through the one output it has open; a tone into another
        // node would first have to switch to it.
        const String current = deviceManager != nullptr ? deviceManager->getAudioDeviceSetup().outputDeviceName : String();
        auto* type = deviceManager != nullptr ? deviceManager->getCurrentDeviceTypeObject() : nullptr;
        if (node != current)
        {
            if (type == nullptr || ! type->getDeviceNames (false).contains (node))
                throw CmdError { "not_found", "No output " + node };
            throw CmdError { "not_supported", "The test tone only plays on the active output (" + current + ")" };
        }
        processor.armApiTestTone (1.5);
        return {};
    }
    if (cmd == "agent.pause")  { processor.setSendPaused (true);  return {}; }
    if (cmd == "agent.resume") { processor.setSendPaused (false); return {}; }
    if (cmd == "agent.reloadConfig")
    {
        String code, message;
        if (! hooks.reloadConfig) throw CmdError { "not_supported", "Started without --config" };
        if (! hooks.reloadConfig (code, message)) throw CmdError { code, message };
        return {};
    }
    if (cmd == "agent.retryNow")
    {
        if (hooks.retryNow && hooks.retryNow()) return {};     // P2.4 connector skips its backoff
        // already connected and in the group: nothing to skip
        if (processor.isConnectedToServer() && processor.getCurrentJoinedGroup().isNotEmpty()) return {};
        Array<AooServerConnectionInfo> recents;
        processor.getRecentServerConnectionInfos (recents);
        if (! recents.isEmpty())                 connectWith (recents.getReference (0));
        else if (hooks.hasInitialConnection)     connectWith (hooks.initialConnection);
        else throw CmdError { "busy", "Nothing to retry: no server was configured" };
        return {};
    }
    throw CmdError { "bad_request", "Unknown command " + cmd };
}

} // namespace crosspoint
