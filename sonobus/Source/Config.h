// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
//
// Config.h -- the YAML configuration file behind `--config <file.yaml>` (P2.1).
//
// This file is deliberately free of JUCE: it is plain C++17 on top of the
// vendored rapidyaml, so it is unit-tested without building the app
// (sonobus/tests/config_test.cpp).
//
// Precedence of every setting, highest first (D11: the YAML is the source of
// truth for an agent):
//
//     command line  >  YAML (--config)  >  setup file (--load-setup)  >  saved state
//
// This file owns the first two levels: parsing/validation of the YAML and the
// field-wise merge of "CLI values" over "YAML values" (Config::merge). The
// setup-file and saved-state levels are enforced by the app, which applies the
// merged result after both have been loaded.
//
// Schema (all keys optional; unknown keys are an error):
//
//   server:   host[:port]         connection server
//   group:    name
//   password: secret              group password
//   username: name
//   role:     vdi | console
//   audio:
//     input_device:  name         JUCE device name (macOS/generic); on Linux a
//                                 PipeWire node name (P2.11)
//     output_device: name
//     sample_rate:   48000
//     buffer:        256          samples
//   codec:    opus | pcm
//   bitrate:  128000              Opus bits/s per channel; opus only
//   api:                          control API defaults (P4.1); --api-* flags override
//     port:            9000
//     bind:            127.0.0.1
//     token:           secret
//     allowed_origins: [http://localhost:5173]

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace crosspoint {

struct YamlApiSection
{
    bool present = false;                 // the api: section appeared at all
    std::optional<int> port;
    std::optional<std::string> bind;
    std::optional<std::string> token;
    std::optional<std::vector<std::string>> allowedOrigins;

    bool operator== (const YamlApiSection& o) const;
};

struct Config
{
    std::optional<std::string> server;    // as written, "host" or "host:port"
    std::optional<std::string> group;
    std::optional<std::string> password;
    std::optional<std::string> username;
    std::optional<std::string> role;      // "vdi" | "console" (lower case)

    std::optional<std::string> inputDevice;   // audio.input_device
    std::optional<std::string> outputDevice;  // audio.output_device
    std::optional<int> sampleRate;            // audio.sample_rate
    std::optional<int> buffer;                // audio.buffer

    std::optional<std::string> codec;     // "opus" | "pcm" (lower case)
    std::optional<int> bitrate;

    YamlApiSection api;

    // File this config was loaded from; empty for a purely in-memory Config.
    // save() writes back to it.
    std::string path;

    bool operator== (const Config& o) const;

    // Parses `text`. `label` is only used in messages (usually the file name).
    // On failure returns false and fills `error` with one or more
    // newline-separated, human-readable lines (never empty). On failure `out`
    // is left unchanged.
    static bool parse (const std::string& text, const std::string& label,
                       Config& out, std::string& error);

    // Reads and parses a file; sets out.path on success.
    static bool load (const std::string& path, Config& out, std::string& error);

    // Field-wise merge: every value present in `high` wins, otherwise the value
    // from `low` is used. This is the CLI-over-YAML step of the precedence rule.
    // `path` is taken from `low` when `high` has none.
    static Config merge (const Config& high, const Config& low);

    // Splits "host" / "host:port". Returns false (and sets `error`) if the port
    // is not an integer in 1..65535 or the host is empty. port is 0 when absent.
    static bool splitServer (const std::string& server, std::string& host, int& port,
                             std::string& error);

    // ---- write-back (P2.1 / used by agent.setInput/setOutput in P2.6) -------

    // Records new device names in this Config. Pass std::nullopt to leave one
    // unchanged. save() persists them.
    void setAudioDevices (std::optional<std::string> input, std::optional<std::string> output);

    // Writes inputDevice / outputDevice back into the file at `path`, editing it
    // IN PLACE: every comment, blank line, key order and the quoting style of
    // untouched values survive byte-for-byte. Only the two device values are
    // changed (a missing key is inserted into the audio: section, which is
    // created at the end of the file if absent). The write is atomic: a temp file
    // in the same directory is written, flushed and renamed over the original,
    // keeping the original's permissions. The file is re-read at save time, so
    // edits made by a human since load() are preserved. If nothing would change
    // the file is not touched.
    bool save (std::string& error) const;

    // The pure text transformation behind save(), exposed for tests. Applies the
    // (non-nullopt) device names to `yaml` and returns the new text. The result
    // is re-parsed and verified to differ from the input only in those values.
    static bool applyAudioDevicesToText (const std::string& yaml,
                                         const std::optional<std::string>& input,
                                         const std::optional<std::string>& output,
                                         std::string& result, std::string& error);
};

} // namespace crosspoint
