// P2.11 -- Linux: pin the engine's audio input / output to PipeWire nodes (D12).
//
// On Debian 13 JUCE only sees ALSA PCMs, and PipeWire nodes (a loopback sink's
// monitor, a virtual-mic sink) are not listed individually. So on Linux
// `audio.input_device` / `audio.output_device` in the YAML name PipeWire nodes
// (`node.name`, as printed by `pw-dump` / `pactl list short sources|sinks`):
//
//   1. the node names are validated against `pw-dump` (JSON); on a miss the
//      valid input and output nodes are listed and the caller exits 1;
//   2. an ALSA config is generated that defines `pcm.crosspoint_in` /
//      `pcm.crosspoint_out` of `type pipewire`, each pinned to its node, with an
//      ALSA `hint` so JUCE's ALSA scan (snd_device_name_hint) lists them;
//   3. ALSA_CONFIG_PATH is pointed at that file (it first includes the system
//      alsa.conf, so every other PCM keeps working). This MUST happen before
//      libasound loads its global config, i.e. before the AudioDeviceManager
//      exists, so the app calls prepare() while reading --config;
//   4. the YAML names are then translated to the JUCE device names of those two
//      PCMs, and the unchanged applyAudioConfig() opens them.
//
// A name that is not a PipeWire node but IS the name of a JUCE/ALSA device
// keeps the P2.1 behaviour (matched against JUCE's device list).
//
// Linux only; the file compiles to nothing elsewhere.

#pragma once

#if defined(__linux__) && !defined(__ANDROID__)

#include <optional>
#include <string>
#include <vector>

namespace crosspoint {
namespace pipewire {

struct Node
{
    std::string name;         // node.name
    std::string description;  // node.description
    std::string mediaClass;   // media.class, e.g. Audio/Sink
    std::string target;       // input candidates: the node to hand to capture_node
};

// What prepare() decided; the JUCE device names to select, per direction
// (empty = not pinned: leave the YAML value alone).
struct Resolved
{
    std::string inputJuceName;
    std::string outputJuceName;
    std::string asoundConfPath;   // generated file, when anything was pinned
    // P2.5: a requested node is not there (yet). prepare() still succeeds, pins what
    // it can, and leaves the printable reason here; the caller decides whether that
    // is fatal (GUI) or retried (headless agent, AgentDeviceWatcher).
    bool missing = false;
    std::string missingMsg;
    // The graph's largest quantum (clock.force-quantum, else clock.max-quantum, from
    // the "settings" metadata); 0 when unknown. The engine's ALSA ring (two blocks)
    // must hold one graph cycle, or it overruns every cycle: see minBufferFor().
    int graphMaxQuantum = 0;
};

// Pure helpers (unit-testable without PipeWire).
// `nodes` -> the names usable as an input: Audio/Source nodes by node.name and,
// for every Audio/Sink, "<node.name>.monitor" (the pactl spelling).
std::vector<Node> inputCandidates (const std::vector<Node>& nodes);
std::vector<Node> outputCandidates (const std::vector<Node>& nodes);
// Parses `pw-dump` JSON; false (with err) if it is not the expected array.
bool parseNodes (const std::string& json, std::vector<Node>& out, std::string& err);
// The "settings" metadata's clock.force-quantum (if > 0) else clock.max-quantum; 0 if absent.
int parseMaxQuantum (const std::string& json);
// Smallest engine buffer (power of two, >= requested) whose two-block ALSA ring
// holds one graph cycle of `maxQuantum` frames. 256 in a 1024 graph -> 512.
int minBufferFor (int requested, int maxQuantum);
// The text of the generated ALSA config.
std::string makeAsoundConf (const std::optional<std::string>& captureNode,
                            const std::optional<std::string>& playbackNode,
                            const std::string& systemAlsaConf);

// Lists nodes via `pw-dump`. False (err set) when pw-dump is missing or no
// PipeWire daemon answers.
bool listNodes (std::vector<Node>& out, std::string& err);

// Validates the requested names, writes `confPath`, sets ALSA_CONFIG_PATH and
// fills `r`. A name that is neither a PipeWire node nor an ALSA device (or a
// PipeWire that cannot be queried) is NOT an error here (P2.5): r.missing is
// set with a printable r.missingMsg (including the list of valid nodes).
// Returns false only for a real failure (e.g. cannot write confPath).
// When PipeWire cannot be queried at all, nothing is pinned and true is
// returned (P2.1 behaviour), with `note` explaining why.
//
// A sink's ".monitor" as input also sets PIPEWIRE_ALSA to
// { stream.capture.sink = true }: without it WirePlumber 0.5 refuses to link a
// capture stream to a sink and silently falls back to the default source.
bool prepare (const std::optional<std::string>& inputDevice,
              const std::optional<std::string>& outputDevice,
              const std::string& confPath,
              Resolved& r, std::string& err, std::string& note);

// The node name to show for an open JUCE device: the YAML node behind the last
// pinned PCM ("Crosspoint input: X" -> "X.monitor" when that is what was asked
// for), else the JUCE name unchanged.
std::string nodeForJuceName (const std::string& juceName, bool input);

// Pure: the pinned PCM's JUCE name for a node in that direction.
std::string juceNameForNode (const std::string& node, bool input);

} // namespace pipewire
} // namespace crosspoint

#endif // __linux__
