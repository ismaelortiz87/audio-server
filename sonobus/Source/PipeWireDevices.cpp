// P2.11 -- see PipeWireDevices.h.

#if defined(__linux__)

#include "PipeWireDevices.h"
#include "JuceHeader.h"

#include <alsa/asoundlib.h>

#include <cstdlib>
#include <set>

namespace crosspoint {
namespace pipewire {

namespace {

// JUCE lists a PCM under its ALSA hint description (newlines -> "; "). These two
// are what the generated config uses, so the YAML names can be translated.
std::string inputDesc (const std::string& node)  { return "Crosspoint input: " + node; }
std::string outputDesc (const std::string& node) { return "Crosspoint output: " + node; }

// What the last prepare() pinned: YAML node name per direction (for display).
std::string pinnedIn, pinnedOut;
std::string pinnedInJuce, pinnedOutJuce;

bool isSink (const Node& n)   { return n.mediaClass == "Audio/Sink" || n.mediaClass == "Audio/Duplex"; }
bool isSource (const Node& n) { return n.mediaClass == "Audio/Source" || n.mediaClass == "Audio/Source/Virtual"
                                    || n.mediaClass == "Audio/Duplex"; }

// ALSA config strings: quote and escape.
std::string q (const std::string& s)
{
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') { o += "\\n"; continue; }
        o += c;
    }
    return o + "\"";
}

// Does `name` match a device JUCE's ALSA backend would list (P2.1 behaviour)?
bool matchesAlsaDeviceName (const std::string& name)
{
    if (name == "Default ALSA Output" || name == "Default ALSA Input"
        || name == "Pulseaudio output" || name == "Pulseaudio input")
        return true;

    bool found = false;
    void** hints = nullptr;
    if (snd_device_name_hint (-1, "pcm", &hints) == 0) {
        for (char** h = (char**) hints; *h != nullptr; ++h) {
            char* id = snd_device_name_get_hint (*h, "NAME");
            char* desc = snd_device_name_get_hint (*h, "DESC");
            if (id != nullptr && name == id) found = true;
            if (desc != nullptr) {
                std::string d = juce::String::fromUTF8 (desc).replace ("\n", "; ").toStdString();
                if (d == name) found = true;
            }
            std::free (id);
            std::free (desc);
        }
        snd_device_name_free_hint (hints);
    }
    return found;
}

std::string listing (const std::vector<Node>& in, const std::vector<Node>& out)
{
    std::string s = "Valid PipeWire input nodes (audio.input_device):\n";
    if (in.empty()) s += "  (none)\n";
    for (auto& n : in) s += "  " + n.name + "    - " + n.description + "\n";
    s += "Valid PipeWire output nodes (audio.output_device):\n";
    if (out.empty()) s += "  (none)\n";
    for (auto& n : out) s += "  " + n.name + "    - " + n.description + "\n";
    return s;
}

} // namespace

bool parseNodes (const std::string& json, std::vector<Node>& out, std::string& err)
{
    out.clear();
    juce::var root;
    if (juce::JSON::parse (juce::String (json), root).failed() || ! root.isArray()) {
        err = "pw-dump did not return a JSON array";
        return false;
    }
    for (auto& o : *root.getArray()) {
        if (o["type"].toString() != "PipeWire:Interface:Node") continue;
        auto props = o["info"]["props"];
        if (! props.isObject()) continue;
        Node n;
        n.name = props["node.name"].toString().toStdString();
        n.mediaClass = props["media.class"].toString().toStdString();
        n.description = props["node.description"].toString().toStdString();
        if (n.description.empty()) n.description = props["node.nick"].toString().toStdString();
        if (! n.name.empty() && n.mediaClass.rfind ("Audio/", 0) == 0) out.push_back (n);
    }
    return true;
}

int parseMaxQuantum (const std::string& json)
{
    juce::var root;
    if (juce::JSON::parse (juce::String (json), root).failed() || ! root.isArray()) return 0;
    int maxQ = 0, forceQ = 0;
    for (auto& o : *root.getArray()) {
        if (o["type"].toString() != "PipeWire:Interface:Metadata") continue;
        if (o["props"]["metadata.name"].toString() != "settings") continue;
        if (auto* items = o["metadata"].getArray())
            for (auto& m : *items) {
                const auto key = m["key"].toString();
                const int v = m["value"].toString().getIntValue();
                if (key == "clock.max-quantum") maxQ = v;
                if (key == "clock.force-quantum") forceQ = v;
            }
    }
    return forceQ > 0 ? forceQ : maxQ;
}

int minBufferFor (int requested, int maxQuantum)
{
    int b = juce::jmax (1, requested);
    while (maxQuantum > 0 && 2 * b < maxQuantum) b *= 2;
    return b;
}

std::vector<Node> inputCandidates (const std::vector<Node>& nodes)
{
    std::vector<Node> r;
    for (auto& n : nodes) {
        if (isSource (n)) { Node m = n; m.target = n.name; r.push_back (m); }
        if (isSink (n)) {
            Node m = n;
            m.target = n.name;
            m.name += ".monitor";
            m.description = "Monitor of " + n.description;
            r.push_back (m);
        }
    }
    return r;
}

std::vector<Node> outputCandidates (const std::vector<Node>& nodes)
{
    std::vector<Node> r;
    for (auto& n : nodes) if (isSink (n)) r.push_back (n);
    return r;
}

std::string makeAsoundConf (const std::optional<std::string>& captureNode,
                            const std::optional<std::string>& playbackNode,
                            const std::string& systemAlsaConf)
{
    std::string s = "# Generated by Crosspoint (P2.11) -- do not edit; rewritten at every start.\n"
                    "# Pins ALSA PCMs to PipeWire nodes named in audio.input_device / audio.output_device.\n"
                    "<" + systemAlsaConf + ">\n";
    if (captureNode)
        s += "pcm.crosspoint_in {\n  type pipewire\n  capture_node " + q (*captureNode)
           + "\n  hint { show on description " + q (inputDesc (*captureNode)) + " }\n}\n";
    if (playbackNode)
        s += "pcm.crosspoint_out {\n  type pipewire\n  playback_node " + q (*playbackNode)
           + "\n  hint { show on description " + q (outputDesc (*playbackNode)) + " }\n}\n";
    return s;
}

namespace {
bool readPwDump (std::string& json, std::string& err)
{
    juce::ChildProcess p;
    if (! p.start (juce::StringArray { "pw-dump" })) {
        err = "could not run pw-dump (is the pipewire-bin package installed?)";
        return false;
    }
    const auto text = p.readAllProcessOutput();
    p.waitForProcessToFinish (5000);
    if (p.getExitCode() != 0 || text.isEmpty()) {
        err = "pw-dump failed (is the PipeWire daemon running and XDG_RUNTIME_DIR set?)";
        return false;
    }
    json = text.toStdString();
    return true;
}
} // namespace

bool listNodes (std::vector<Node>& out, std::string& err)
{
    std::string json;
    return readPwDump (json, err) && parseNodes (json, out, err);
}

bool prepare (const std::optional<std::string>& inputDevice,
              const std::optional<std::string>& outputDevice,
              const std::string& confPath,
              Resolved& r, std::string& err, std::string& note)
{
    r = {};
    pinnedIn.clear(); pinnedInJuce.clear(); pinnedOut.clear(); pinnedOutJuce.clear();
    ::unsetenv ("PIPEWIRE_ALSA");
    if (! inputDevice && ! outputDevice) return true;

    std::vector<Node> nodes;
    std::string listErr, dump;
    bool listFailed = false;
    if (readPwDump (dump, listErr) && parseNodes (dump, nodes, listErr))
        r.graphMaxQuantum = parseMaxQuantum (dump);
    else {
        note = "PipeWire node lookup unavailable (" + listErr + "); audio devices are matched against the ALSA device list";
        listFailed = true;   // P2.5: PipeWire may just not be up yet at boot; names below are retried
        nodes.clear();
    }
    const auto ins = inputCandidates (nodes);
    const auto outs = outputCandidates (nodes);

    std::optional<std::string> capNode, playNode;
    bool capFromSink = false;
    std::string bad;

    if (inputDevice) {
        const Node* hit = nullptr;
        for (auto& n : ins) if (n.name == *inputDevice) hit = &n;
        if (hit != nullptr) {
            // A sink's ".monitor" is captured by pointing the stream at the sink
            // node itself, flagged stream.capture.sink (below) so WirePlumber links
            // it to that sink's monitor ports.
            capNode = hit->target;
            capFromSink = hit->target != hit->name;
        } else if (! matchesAlsaDeviceName (*inputDevice)) {
            bad += "audio.input_device '" + *inputDevice + "' is not a PipeWire node";
            // P2.5: pin the PCM to where the node WILL be, so that JUCE's one-time ALSA
            // scan already lists it and a later retry can open it (see reopenAudioDevices).
            const std::string suffix = ".monitor";
            const bool mon = inputDevice->size() > suffix.size()
                             && inputDevice->compare (inputDevice->size() - suffix.size(), suffix.size(), suffix) == 0;
            capNode = mon ? inputDevice->substr (0, inputDevice->size() - suffix.size()) : *inputDevice;
            capFromSink = mon;
        }
    }
    if (outputDevice) {
        const Node* hit = nullptr;
        for (auto& n : outs) if (n.name == *outputDevice) hit = &n;
        if (hit != nullptr) playNode = hit->name;
        else if (! matchesAlsaDeviceName (*outputDevice)) {
            if (! bad.empty()) bad += " and ";
            bad += "audio.output_device '" + *outputDevice + "' is not a PipeWire node";
            playNode = *outputDevice;   // P2.5: as above
        }
    }
    if (! bad.empty()) {   // P2.5: reported, not fatal; the caller decides
        r.missing = true;
        r.missingMsg = bad + " (nor an ALSA device name).\n"
                     + (listFailed ? "PipeWire could not be queried: " + listErr + "\n" : listing (ins, outs));
    }
    if (! capNode && ! playNode) return true; // only ALSA names: P2.1 behaviour

    // Include the system alsa.conf so every other PCM keeps working.
    std::string sysConf = "/usr/share/alsa/alsa.conf";
    if (const char* d = std::getenv ("ALSA_SYSTEM_CONFIG")) sysConf = d;
    juce::File f (confPath);
    if (! f.getParentDirectory().createDirectory()
        || ! f.replaceWithText (juce::String (makeAsoundConf (capNode, playNode, sysConf)))) {
        err = "could not write " + confPath;
        return false;
    }
    ::setenv ("ALSA_CONFIG_PATH", confPath.c_str(), 1);
    // The hint scan above may already have loaded the global ALSA config.
    snd_config_update_free_global();

    r.asoundConfPath = confPath;   // (r.missing/missingMsg set above survive)
    // The pipewire ALSA plugin reads this at every PCM open; the playback stream
    // ignores the flag. Without a monitor input it must not be set: with no target,
    // a stream.capture.sink stream defaults to the default sink's monitor.
    if (capFromSink) ::setenv ("PIPEWIRE_ALSA", "{ stream.capture.sink = true }", 1);
    else             ::unsetenv ("PIPEWIRE_ALSA");

    if (capNode) r.inputJuceName = inputDesc (*capNode);
    if (playNode) r.outputJuceName = outputDesc (*playNode);
    pinnedIn = capNode ? *inputDevice : std::string();
    pinnedInJuce = r.inputJuceName;
    pinnedOut = playNode ? *outputDevice : std::string();
    pinnedOutJuce = r.outputJuceName;
    return true;
}

std::string nodeForJuceName (const std::string& juceName, bool input)
{
    const auto& juce = input ? pinnedInJuce : pinnedOutJuce;
    if (! juce.empty() && juceName == juce) return input ? pinnedIn : pinnedOut;
    return juceName;
}

std::string juceNameForNode (const std::string& node, bool input)
{
    if (! input) return outputDesc (node);
    const std::string suffix = ".monitor";
    const bool mon = node.size() > suffix.size() && node.compare (node.size() - suffix.size(), suffix.size(), suffix) == 0;
    return inputDesc (mon ? node.substr (0, node.size() - suffix.size()) : node);
}

} // namespace pipewire
} // namespace crosspoint

#endif // __linux__
