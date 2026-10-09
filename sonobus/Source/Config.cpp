// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
//
// Config.cpp -- see Config.h. Plain C++17 + the vendored rapidyaml; no JUCE.

#include "Config.h"

#include <ryml_all.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <tuple>

#if defined(_WIN32)
 #define NOMINMAX
 #include <windows.h>
 #include <io.h>
 #include <process.h>
 #define CP_FILENO _fileno
 #define CP_FSYNC _commit
 #define CP_GETPID _getpid
#else
 #include <limits.h>
 #include <stdlib.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #define CP_FILENO fileno
 #define CP_FSYNC fsync
 #define CP_GETPID getpid
#endif

namespace crosspoint {

bool ApiConfig::operator== (const ApiConfig& o) const
{
    return std::tie (present, port, bind, token, allowedOrigins)
        == std::tie (o.present, o.port, o.bind, o.token, o.allowedOrigins);
}

bool Config::operator== (const Config& o) const
{
    return std::tie (server, group, password, username, role, inputDevice, outputDevice,
                     sampleRate, buffer, codec, bitrate, api)
        == std::tie (o.server, o.group, o.password, o.username, o.role, o.inputDevice,
                     o.outputDevice, o.sampleRate, o.buffer, o.codec, o.bitrate, o.api);
}

namespace {

std::string str (ryml::csubstr s) { return std::string (s.str, s.len); }

// Holds one parse: a private mutable copy of the text (ryml filters scalars in
// place) plus the original, which stays pristine for offset-based editing.
struct Doc
{
    const std::string& text;
    std::string buf;
    ryml::Tree tree;
    ryml::ConstNodeRef root;   // the top-level mapping; invalid() for an empty document
    bool hasRoot = false;

    explicit Doc (const std::string& t) : text (t), buf (t) {}

    // Byte offset of a scalar inside the document, or npos if it does not point
    // into the parsed buffer (e.g. a scalar ryml had to copy elsewhere).
    size_t offsetOf (ryml::csubstr s) const
    {
        if (s.str == nullptr || buf.empty()) return std::string::npos;
        if (s.str < buf.data() || s.str > buf.data() + buf.size()) return std::string::npos;
        return (size_t) (s.str - buf.data());
    }

    int lineOf (ryml::csubstr s) const
    {
        const size_t off = offsetOf (s);
        if (off == std::string::npos) return 0;
        return 1 + (int) std::count (text.begin(), text.begin() + (std::ptrdiff_t) std::min (off, text.size()), '\n');
    }
};

// Parses into `doc`. Returns false and fills `error` on malformed YAML or a
// non-mapping top level. A blank/comment-only file is a valid empty config.
bool parseDoc (Doc& doc, const std::string& label, std::string& error)
{
    try
    {
        if (doc.buf.find_first_not_of (" \t\r\n") == std::string::npos)
            return true; // empty
        doc.tree = ryml::parse_in_place (ryml::to_substr (doc.buf));
    }
    catch (const std::exception& e)
    {
        std::string msg = e.what();
        // ryml's message is multi-line and carries a source excerpt; the first
        // line is the useful one-line summary.
        const auto nl = msg.find ('\n');
        if (nl != std::string::npos) msg.resize (nl);
        error = label + ": malformed YAML: " + msg;
        return false;
    }

    if (doc.tree.empty())
        return true;

    ryml::ConstNodeRef r = doc.tree.crootref();
    if (r.is_stream())
    {
        if (r.num_children() != 1)
        {
            error = label + ": the file contains " + std::to_string (r.num_children())
                  + " YAML documents; exactly one is supported";
            return false;
        }
        r = r.child (0);
    }
    if (!r.is_map() && !r.is_seq() && r.num_children() == 0 && (!r.has_val() || r.val_is_null()))
        return true; // a document with no content (e.g. only comments)
    if (!r.is_map())
    {
        error = label + ": the top level must be a mapping of keys (server:, group:, audio:, ...)";
        return false;
    }
    doc.root = r;
    doc.hasRoot = true;
    return true;
}

struct Errors
{
    const Doc& doc;
    const std::string& label;
    std::vector<std::string> list;

    void add (ryml::csubstr where, const std::string& msg)
    {
        const int line = doc.lineOf (where);
        list.push_back (label + (line > 0 ? ":" + std::to_string (line) : std::string()) + ": " + msg);
    }
};

std::string describe (ryml::ConstNodeRef n)
{
    if (n.is_map()) return "a mapping";
    if (n.is_seq()) return "a list";
    return "'" + str (n.val()) + "'";
}

// Reads a scalar string. Returns false if absent/null (unset, not an error) or
// invalid (error recorded). `ok` is cleared on error.
bool readString (Errors& e, ryml::ConstNodeRef n, const std::string& path,
                 std::optional<std::string>& out)
{
    if (n.is_container())
    {
        e.add (n.has_key() ? n.key() : ryml::csubstr(), path + ": expected a text value, got "
                                                       + std::string (n.is_map() ? "a mapping" : "a list"));
        return false;
    }
    if (n.val_is_null())
        return false; // "key:" with nothing after it means "not set"
    out = str (n.val());
    return true;
}

bool readInt (Errors& e, ryml::ConstNodeRef n, const std::string& path, long lo, long hi,
              std::optional<int>& out)
{
    if (n.is_container())
    {
        e.add (n.key(), path + ": expected an integer, got "
                            + std::string (n.is_map() ? "a mapping" : "a list"));
        return false;
    }
    if (n.val_is_null())
        return false;
    const std::string s = str (n.val());
    long v = 0;
    const char* b = s.data();
    const char* en = s.data() + s.size();
    auto r = std::from_chars (b, en, v);
    if (s.empty() || r.ec != std::errc() || r.ptr != en)
    {
        e.add (n.val(), path + ": expected an integer, got '" + s + "'");
        return false;
    }
    if (v < lo || v > hi)
    {
        e.add (n.val(), path + ": " + s + " is out of range (" + std::to_string (lo) + " to "
                            + std::to_string (hi) + ")");
        return false;
    }
    out = (int) v;
    return true;
}

std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

// Checks that every child key of `map` is one of `allowed`, and none repeats.
void checkKeys (Errors& e, ryml::ConstNodeRef map, const std::string& prefix,
                const std::vector<std::string>& allowed)
{
    std::vector<std::string> seen;
    for (ryml::ConstNodeRef c : map.children())
    {
        if (!c.has_key()) continue;
        const std::string k = str (c.key());
        if (std::find (allowed.begin(), allowed.end(), k) == allowed.end())
        {
            std::string valid;
            for (size_t i = 0; i < allowed.size(); ++i)
                valid += (i ? ", " : "") + allowed[i];
            e.add (c.key(), "unknown key '" + prefix + k + "' (valid keys here: " + valid + ")");
        }
        if (std::find (seen.begin(), seen.end(), k) != seen.end())
            e.add (c.key(), "duplicate key '" + prefix + k + "'");
        seen.push_back (k);
    }
}

ryml::ConstNodeRef findKey (ryml::ConstNodeRef map, const char* key)
{
    for (ryml::ConstNodeRef c : map.children())
        if (c.has_key() && c.key() == ryml::to_csubstr (key))
            return c;
    return ryml::ConstNodeRef();
}

} // namespace

bool Config::splitServer (const std::string& server, std::string& host, int& port, std::string& error)
{
    port = 0;
    const auto colon = server.rfind (':');
    host = colon == std::string::npos ? server : server.substr (0, colon);
    if (host.empty() || host.find_first_of (" \t") != std::string::npos)
    {
        error = "expected host or host:port, got '" + server + "'";
        return false;
    }
    if (colon != std::string::npos)
    {
        const std::string p = server.substr (colon + 1);
        long v = 0;
        auto r = std::from_chars (p.data(), p.data() + p.size(), v);
        if (p.empty() || r.ec != std::errc() || r.ptr != p.data() + p.size() || v < 1 || v > 65535)
        {
            error = "the port in '" + server + "' must be an integer from 1 to 65535";
            return false;
        }
        port = (int) v;
    }
    return true;
}

bool Config::parse (const std::string& text, const std::string& label, Config& out, std::string& error)
{
    Doc doc (text);
    if (!parseDoc (doc, label, error))
        return false;

    Config cfg;
    cfg.path = out.path;
    if (!doc.hasRoot)
    {
        out = cfg;
        return true;
    }

    Errors e { doc, label, {} };
    ryml::ConstNodeRef root = doc.root;

    checkKeys (e, root, "", { "server", "group", "password", "username", "role", "audio", "codec",
                              "bitrate", "api" });

    for (ryml::ConstNodeRef c : root.children())
    {
        if (!c.has_key()) continue;
        const std::string k = str (c.key());

        if (k == "server")
        {
            if (readString (e, c, k, cfg.server))
            {
                std::string host, err;
                int port = 0;
                if (!splitServer (*cfg.server, host, port, err))
                {
                    e.add (c.val(), "server: " + err);
                    cfg.server.reset();
                }
            }
        }
        else if (k == "group")
        {
            if (readString (e, c, k, cfg.group) && cfg.group->empty())
            {
                e.add (c.val(), "group: must not be empty");
                cfg.group.reset();
            }
        }
        else if (k == "password") readString (e, c, k, cfg.password);
        else if (k == "username")
        {
            if (readString (e, c, k, cfg.username) && cfg.username->empty())
            {
                e.add (c.val(), "username: must not be empty");
                cfg.username.reset();
            }
        }
        else if (k == "role")
        {
            if (readString (e, c, k, cfg.role))
            {
                cfg.role = lower (*cfg.role);
                if (*cfg.role != "vdi" && *cfg.role != "console")
                {
                    e.add (c.val(), "role: must be 'vdi' or 'console', got '" + str (c.val()) + "'");
                    cfg.role.reset();
                }
            }
        }
        else if (k == "codec")
        {
            if (readString (e, c, k, cfg.codec))
            {
                cfg.codec = lower (*cfg.codec);
                if (*cfg.codec != "opus" && *cfg.codec != "pcm")
                {
                    e.add (c.val(), "codec: must be 'opus' or 'pcm', got '" + str (c.val()) + "'");
                    cfg.codec.reset();
                }
            }
        }
        else if (k == "bitrate") readInt (e, c, k, 1, 10000000, cfg.bitrate);
        else if (k == "audio")
        {
            if (c.val_is_null() && !c.is_container()) continue;
            if (!c.is_map())
            {
                e.add (c.key(), "audio: expected a mapping (input_device, output_device, sample_rate, buffer), got "
                                    + describe (c));
                continue;
            }
            checkKeys (e, c, "audio.", { "input_device", "output_device", "sample_rate", "buffer" });
            for (ryml::ConstNodeRef a : c.children())
            {
                if (!a.has_key()) continue;
                const std::string ak = "audio." + str (a.key());
                if (ak == "audio.input_device" || ak == "audio.output_device")
                {
                    auto& target = ak == "audio.input_device" ? cfg.inputDevice : cfg.outputDevice;
                    if (readString (e, a, ak, target) && target->empty())
                    {
                        e.add (a.val(), ak + ": must not be empty (omit the key to use the default device)");
                        target.reset();
                    }
                }
                else if (ak == "audio.sample_rate") readInt (e, a, ak, 8000, 384000, cfg.sampleRate);
                else if (ak == "audio.buffer") readInt (e, a, ak, 16, 16384, cfg.buffer);
            }
        }
        else if (k == "api")
        {
            cfg.api.present = true;
            if (c.val_is_null() && !c.is_container()) continue;
            if (!c.is_map())
            {
                e.add (c.key(), "api: expected a mapping (port, bind, token, allowed_origins), got "
                                    + describe (c));
                continue;
            }
            checkKeys (e, c, "api.", { "port", "bind", "token", "allowed_origins" });
            for (ryml::ConstNodeRef a : c.children())
            {
                if (!a.has_key()) continue;
                const std::string ak = "api." + str (a.key());
                if (ak == "api.port") readInt (e, a, ak, 1, 65535, cfg.api.port);
                else if (ak == "api.bind") readString (e, a, ak, cfg.api.bind);
                else if (ak == "api.token") readString (e, a, ak, cfg.api.token);
                else if (ak == "api.allowed_origins")
                {
                    if (a.val_is_null() && !a.is_container()) continue;
                    if (!a.is_seq())
                    {
                        e.add (a.key(), ak + ": expected a list of origins, got " + describe (a));
                        continue;
                    }
                    std::vector<std::string> list;
                    for (ryml::ConstNodeRef o : a.children())
                    {
                        if (o.is_container() || o.val_is_null())
                            e.add (o.has_val() ? o.val() : a.key(), ak + ": every entry must be a text value");
                        else
                            list.push_back (str (o.val()));
                    }
                    cfg.api.allowedOrigins = list;
                }
            }
        }
    }

    // Cross-field checks.
    if (cfg.bitrate && cfg.codec && *cfg.codec == "pcm")
    {
        ryml::ConstNodeRef b = findKey (root, "bitrate");
        e.add (b.invalid() ? ryml::csubstr() : b.key(),
               "bitrate: only applies to codec 'opus' (codec is 'pcm')");
    }

    if (!e.list.empty())
    {
        error.clear();
        for (size_t i = 0; i < e.list.size(); ++i)
            error += (i ? "\n" : "") + e.list[i];
        return false;
    }

    out = cfg;
    return true;
}

bool Config::load (const std::string& path, Config& out, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (!in)
    {
        error = path + ": cannot read the config file";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Config cfg;
    cfg.path = path;
    if (!parse (ss.str(), path, cfg, error))
        return false;
    out = cfg;
    return true;
}

Config Config::merge (const Config& high, const Config& low)
{
    Config r = low;
    auto pick = [] (auto& dst, const auto& hi) { if (hi) dst = hi; };
    pick (r.server, high.server);
    pick (r.group, high.group);
    pick (r.password, high.password);
    pick (r.username, high.username);
    pick (r.role, high.role);
    pick (r.inputDevice, high.inputDevice);
    pick (r.outputDevice, high.outputDevice);
    pick (r.sampleRate, high.sampleRate);
    pick (r.buffer, high.buffer);
    pick (r.codec, high.codec);
    pick (r.bitrate, high.bitrate);
    if (high.api.present)
    {
        r.api.present = true;
        pick (r.api.port, high.api.port);
        pick (r.api.bind, high.api.bind);
        pick (r.api.token, high.api.token);
        pick (r.api.allowedOrigins, high.api.allowedOrigins);
    }
    if (!high.path.empty()) r.path = high.path;
    return r;
}

//==============================================================================
// Write-back

namespace {

bool isQuoteChar (char c) { return c == '"' || c == '\''; }

// True if `s` can be written as a plain (unquoted) YAML scalar and read back
// unchanged as a string.
bool plainSafe (const std::string& s)
{
    if (s.empty()) return false;
    if (std::isspace ((unsigned char) s.front()) || std::isspace ((unsigned char) s.back())) return false;
    if (std::strchr ("-?:,[]{}#&*!|>'\"%@`", s.front()) != nullptr) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char) s[i];
        if (c < 0x20 || c == 0x7f) return false;
        if (c == ':' && (i + 1 == s.size() || s[i + 1] == ' ')) return false;
        if (c == '#' && i > 0 && s[i - 1] == ' ') return false;
    }
    // Things YAML would read as another type.
    const std::string l = lower (s);
    if (l == "null" || l == "~" || l == "true" || l == "false" || l == "yes" || l == "no"
        || l == "on" || l == "off")
        return false;
    char* endp = nullptr;
    std::strtod (s.c_str(), &endp);
    if (endp != nullptr && *endp == '\0') return false;
    return true;
}

std::string quoteDouble (const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s)
    {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\x%02x", c); o += b; }
        else o += (char) c;
    }
    return o + "\"";
}

std::string quoteSingle (const std::string& s)
{
    std::string o = "'";
    for (char c : s) o += (c == '\'') ? std::string ("''") : std::string (1, c);
    return o + "'";
}

// How to spell `value`, following the quote style of the scalar it replaces
// (`oldQuote` is '"', '\'' or 0 for plain/new).
std::string spell (const std::string& value, char oldQuote)
{
    const bool hasCtl = std::any_of (value.begin(), value.end(),
                                     [] (unsigned char c) { return c < 0x20 || c == 0x7f; });
    if (oldQuote == '\'' && !hasCtl) return quoteSingle (value);
    if (oldQuote == '"') return quoteDouble (value);
    return plainSafe (value) ? value : quoteDouble (value);
}

// End of the content of the line containing `from`, before its "\n" / "\r\n".
size_t contentEnd (const std::string& t, size_t from)
{
    size_t nl = t.find ('\n', from);
    if (nl == std::string::npos) return t.size();
    if (nl > 0 && t[nl - 1] == '\r') --nl;
    return nl;
}

// Extent [start, end) of a scalar's source text, including its quotes.
bool scalarSpan (const Doc& d, ryml::ConstNodeRef n, size_t& start, size_t& end, char& quote)
{
    const size_t off = d.offsetOf (n.val());
    if (off == std::string::npos) return false;
    quote = 0;
    start = off;
    if (off > 0 && isQuoteChar (d.text[off - 1]) && n.is_val_quoted())
    {
        quote = d.text[off - 1];
        start = off - 1;
        size_t i = off;
        while (i < d.text.size())
        {
            if (quote == '"' && d.text[i] == '\\') { i += 2; continue; }
            if (d.text[i] == quote)
            {
                if (quote == '\'' && i + 1 < d.text.size() && d.text[i + 1] == '\'') { i += 2; continue; }
                end = i + 1;
                return true;
            }
            ++i;
        }
        return false;
    }
    end = off + (size_t) n.val().len;
    // A plain scalar must be on one line and unchanged by ryml's filtering.
    return end <= d.text.size() && d.text.compare (off, (size_t) n.val().len, str (n.val())) == 0;
}

// Position just after "key:" for a key with no value.
size_t afterColon (const Doc& d, ryml::ConstNodeRef key)
{
    size_t i = d.offsetOf (key.key());
    if (i == std::string::npos) return i;
    i += (size_t) key.key().len;
    while (i < d.text.size() && d.text[i] != ':' && d.text[i] != '\n') ++i;
    return (i < d.text.size() && d.text[i] == ':') ? i + 1 : std::string::npos;
}

// Start of the line containing offset `o`.
size_t lineStart (const std::string& t, size_t o)
{
    const size_t nl = o == 0 ? std::string::npos : t.rfind ('\n', o - 1);
    return nl == std::string::npos ? 0 : nl + 1;
}

// Sets audio.<key> = value in `text`. One key at a time; re-parses each time.
bool setOneAudioKey (const std::string& text, const std::string& key, const std::string& value,
                     std::string& out, std::string& error)
{
    const std::string eol = text.find ("\r\n") != std::string::npos ? "\r\n" : "\n";
    Doc d (text);
    if (!parseDoc (d, "config", error))
        return false;

    auto appendSection = [&] ()
    {
        out = text;
        if (!out.empty() && out.back() != '\n') out += eol;
        out += "audio:" + eol + "  " + key + ": " + spell (value, 0) + eol;
        return true;
    };

    if (!d.hasRoot)
        return appendSection();

    if (d.root.type().is_flow())
    {
        error = "the top-level mapping is written in flow style ({...}); in-place editing needs block style";
        return false;
    }

    ryml::ConstNodeRef audio = findKey (d.root, "audio");
    if (audio.invalid())
        return appendSection();

    // audio: with nothing under it
    if (!audio.is_container() && audio.val_is_null())
    {
        size_t pos = afterColon (d, audio);
        if (pos == std::string::npos) { error = "cannot locate the 'audio:' line"; return false; }
        const size_t ce = contentEnd (text, pos);
        // "audio: null" would need rewriting; only a bare "audio:" (+ comment) is handled.
        const std::string rest = text.substr (pos, ce - pos);
        const auto firstNonSpace = rest.find_first_not_of (" \t");
        if (firstNonSpace != std::string::npos && rest[firstNonSpace] != '#')
        {
            error = "'audio:' has an explicit null value; replace it with a mapping";
            return false;
        }
        out = text.substr (0, ce) + eol + "  " + key + ": " + spell (value, 0) + text.substr (ce);
        if (ce == text.size()) out += eol;
        return true;
    }

    if (!audio.is_map())
    {
        error = "'audio' must be a mapping";
        return false;
    }
    if (audio.type().is_flow())
    {
        error = "the 'audio' mapping is written in flow style ({...}); in-place editing needs block style";
        return false;
    }

    ryml::ConstNodeRef k = findKey (audio, key.c_str());
    if (!k.invalid())
    {
        if (k.is_container())
        {
            error = "audio." + key + " is not a text value";
            return false;
        }
        if (k.val_is_null())
        {
            const size_t pos = afterColon (d, k);
            if (pos == std::string::npos) { error = "cannot locate 'audio." + key + ":'"; return false; }
            out = text;
            out.insert (pos, " " + spell (value, 0));
            return true;
        }
        size_t s = 0, e = 0;
        char q = 0;
        if (!scalarSpan (d, k, s, e, q))
        {
            error = "audio." + key + " uses a YAML scalar style that cannot be edited in place "
                    "(multi-line?); write it on one line";
            return false;
        }
        out = text.substr (0, s) + spell (value, q) + text.substr (e);
        return true;
    }

    // Missing key: add it after the last child of audio, with the children's indent.
    ryml::ConstNodeRef first = audio.first_child();
    ryml::ConstNodeRef last = audio.last_child();
    const size_t firstOff = d.offsetOf (first.key());
    if (firstOff == std::string::npos) { error = "cannot locate the audio section"; return false; }
    size_t indentStart = lineStart (text, firstOff);
    size_t kpos = firstOff;
    if (first.is_key_quoted() && kpos > 0) --kpos;
    const std::string indent = text.substr (indentStart, kpos - indentStart);

    size_t after;
    if (last.is_container())
    {
        error = "audio." + str (last.key()) + " is not a text value";
        return false;
    }
    if (last.val_is_null())
    {
        after = afterColon (d, last);
        if (after == std::string::npos) { error = "cannot locate the audio section"; return false; }
    }
    else
    {
        size_t s = 0, e = 0;
        char q = 0;
        if (!scalarSpan (d, last, s, e, q))
        {
            error = "audio." + str (last.key()) + " uses a YAML scalar style that cannot be edited in place";
            return false;
        }
        after = e;
    }
    const size_t ce = contentEnd (text, after);
    out = text.substr (0, ce) + eol + indent + key + ": " + spell (value, 0) + text.substr (ce);
    if (ce == text.size()) out += eol;
    return true;
}

} // namespace

bool Config::applyAudioDevicesToText (const std::string& yaml, const std::optional<std::string>& input,
                                      const std::optional<std::string>& output, std::string& result,
                                      std::string& error)
{
    Config before;
    if (!parse (yaml, "config", before, error))
        return false;

    std::string text = yaml;
    auto apply = [&] (const char* key, const std::optional<std::string>& v)
    {
        if (!v) return true;
        if (v->empty())
        {
            error = std::string ("audio.") + key + ": a device name must not be empty";
            return false;
        }
        std::string next;
        if (!setOneAudioKey (text, key, *v, next, error))
            return false;
        text = next;
        return true;
    };
    if (!apply ("input_device", input) || !apply ("output_device", output))
        return false;

    // Safety net: the edited text must parse, carry exactly the requested
    // values and be otherwise identical to the original.
    Config after;
    std::string perr;
    if (!parse (text, "config", after, perr))
    {
        error = "internal error: the edited config no longer parses (" + perr + "); not written";
        return false;
    }
    Config expected = before;
    if (input) expected.inputDevice = input;
    if (output) expected.outputDevice = output;
    if (!(after == expected))
    {
        error = "internal error: the edit changed more than the device names; not written";
        return false;
    }
    result = text;
    return true;
}

void Config::setAudioDevices (std::optional<std::string> input, std::optional<std::string> output)
{
    if (input) inputDevice = std::move (input);
    if (output) outputDevice = std::move (output);
}

bool Config::save (std::string& error) const
{
    if (path.empty())
    {
        error = "this config was not loaded from a file; nowhere to save it";
        return false;
    }

    // Follow symlinks so the rename replaces the real file, not the link.
    std::string target = path;
   #if !defined(_WIN32)
    {
        char resolved[PATH_MAX];
        if (::realpath (path.c_str(), resolved) != nullptr)
            target = resolved;
    }
   #endif

    std::string current;
    {
        std::ifstream in (target, std::ios::binary);
        if (!in)
        {
            error = path + ": cannot read the config file to update it";
            return false;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        current = ss.str();
    }

    std::string updated;
    if (!applyAudioDevicesToText (current, inputDevice, outputDevice, updated, error))
    {
        error = path + ": " + error;
        return false;
    }
    if (updated == current)
        return true;

    // Temp file next to the target (same filesystem, so the rename is atomic).
    const std::string tmp = target + ".tmp." + std::to_string ((long) CP_GETPID());
    {
        std::FILE* f = std::fopen (tmp.c_str(), "wb");
        if (f == nullptr)
        {
            error = tmp + ": cannot create the temporary file";
            return false;
        }
        const bool ok = std::fwrite (updated.data(), 1, updated.size(), f) == updated.size()
                        && std::fflush (f) == 0 && CP_FSYNC (CP_FILENO (f)) == 0;
        const bool closed = std::fclose (f) == 0;
        if (!ok || !closed)
        {
            std::remove (tmp.c_str());
            error = tmp + ": write failed";
            return false;
        }
    }

   #if defined(_WIN32)
    if (! MoveFileExA (tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::remove (tmp.c_str());
        error = path + ": cannot replace the config file";
        return false;
    }
   #else
    // Keep the original's permissions (it may hold a password and be 0600).
    struct stat st;
    if (::stat (target.c_str(), &st) == 0)
        ::chmod (tmp.c_str(), st.st_mode & 07777);

    if (std::rename (tmp.c_str(), target.c_str()) != 0)
    {
        const std::string why = std::strerror (errno);
        std::remove (tmp.c_str());
        error = path + ": cannot replace the config file (" + why + ")";
        return false;
    }
   #endif
    return true;
}

} // namespace crosspoint
