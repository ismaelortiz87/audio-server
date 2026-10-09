// config_test -- unit tests for Source/Config.{h,cpp} (P2.1). No JUCE, no audio.
//
//   cmake --build <builddir> --target config_test && <builddir>/config_test
//
// Exit code 0 = all passed, 1 = at least one failure.

#include "Config.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>

using crosspoint::Config;

namespace {

int g_checks = 0;
int g_failures = 0;

void check (bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf ("FAIL: %s\n", what.c_str());
    }
}

bool contains (const std::string& hay, const std::string& needle) { return hay.find (needle) != std::string::npos; }

// parse() must fail and the message must contain every `needles` entry.
void expectError (const std::string& name, const std::string& yaml, std::initializer_list<const char*> needles)
{
    Config c;
    std::string err;
    const bool ok = Config::parse (yaml, "t.yaml", c, err);
    check (!ok, name + ": should be rejected");
    check (!err.empty(), name + ": error message must not be empty");
    for (const char* n : needles)
        check (contains (err, n), name + ": message should mention '" + n + "', got: " + err);
}

std::string readFile (const std::string& p)
{
    std::ifstream in (p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool isSymlink (const std::string& p)
{
    struct stat st {};
    return ::lstat (p.c_str(), &st) == 0 && S_ISLNK (st.st_mode);
}

void writeFile (const std::string& p, const std::string& s)
{
    std::ofstream out (p, std::ios::binary);
    out << s;
}

const char* const kFull = R"YAML(# Crosspoint agent
server: vpn.example.net:43210
group: studio-a
password: "hunter2"
username: vdi-01
role: VDI
audio:
  input_device: Built-in Microphone
  output_device: "Built-in Output"
  sample_rate: 48000
  buffer: 256
codec: opus
bitrate: 128000
api:
  port: 9000
  bind: 127.0.0.1
  token: s3cret
  allowed_origins:
    - http://localhost:5173
    - "https://console.example.net"
)YAML";

void testParse()
{
    Config c;
    std::string err;
    check (Config::parse (kFull, "t.yaml", c, err), "full document parses: " + err);
    check (c.server == "vpn.example.net:43210", "server");
    check (c.group == "studio-a", "group");
    check (c.password == "hunter2", "password (quoted)");
    check (c.username == "vdi-01", "username");
    check (c.role == "vdi", "role is normalised to lower case");
    check (c.inputDevice == "Built-in Microphone", "audio.input_device");
    check (c.outputDevice == "Built-in Output", "audio.output_device");
    check (c.sampleRate == 48000, "audio.sample_rate");
    check (c.buffer == 256, "audio.buffer");
    check (c.codec == "opus", "codec");
    check (c.bitrate == 128000, "bitrate");
    check (c.api.present && c.api.port == 9000 && c.api.bind == "127.0.0.1" && c.api.token == "s3cret",
           "api scalars");
    check (c.api.allowedOrigins && c.api.allowedOrigins->size() == 2
               && (*c.api.allowedOrigins)[1] == "https://console.example.net",
           "api.allowed_origins");

    Config e;
    check (Config::parse ("", "t.yaml", e, err) && e == Config(), "empty file is an empty config");
    check (Config::parse ("# only a comment\n\n", "t.yaml", e, err) && e == Config(), "comment-only file");
    Config n;
    check (Config::parse ("group: g\npassword:\naudio:\n  input_device:\n", "t.yaml", n, err)
               && n.group == "g" && !n.password && !n.inputDevice,
           "null values mean 'not set'");
    Config num;
    check (Config::parse ("password: 123456\n", "t.yaml", num, err) && num.password == "123456",
           "a numeric-looking password stays a string");

    std::string host, e2;
    int port = 0;
    check (Config::splitServer ("a.b:99", host, port, e2) && host == "a.b" && port == 99, "splitServer host:port");
    check (Config::splitServer ("a.b", host, port, e2) && host == "a.b" && port == 0, "splitServer host only");
    check (!Config::splitServer ("a.b:0", host, port, e2), "splitServer rejects port 0");
}

void testErrors()
{
    expectError ("unknown top-level key", "group: g\nsevrer: x\n", { "t.yaml:2", "unknown key 'sevrer'", "valid keys" });
    expectError ("unknown audio key", "audio:\n  mic: x\n", { "t.yaml:2", "unknown key 'audio.mic'" });
    expectError ("unknown api key", "api:\n  prot: 1\n", { "unknown key 'api.prot'" });
    expectError ("sample_rate not a number", "audio:\n  sample_rate: fast\n", { "t.yaml:2", "audio.sample_rate", "integer", "'fast'" });
    expectError ("buffer out of range", "audio:\n  buffer: 1\n", { "audio.buffer", "out of range" });
    expectError ("server is a list", "server: [a, b]\n", { "server", "text value" });
    expectError ("audio is a scalar", "audio: loud\n", { "audio", "mapping" });
    expectError ("role", "role: boss\n", { "role", "'vdi' or 'console'", "'boss'" });
    expectError ("codec", "codec: mp3\n", { "codec", "'opus' or 'pcm'" });
    expectError ("pcm with bitrate", "codec: pcm\nbitrate: 96000\n", { "bitrate", "opus" });
    expectError ("bad port", "server: host:99999\n", { "server", "port" });
    expectError ("api.port", "api:\n  port: 0\n", { "api.port", "out of range" });
    expectError ("origins not a list", "api:\n  allowed_origins: nope\n", { "api.allowed_origins", "list" });
    expectError ("duplicate key", "group: a\ngroup: b\n", { "duplicate key 'group'" });
    expectError ("empty device name", "audio:\n  input_device: \"\"\n", { "audio.input_device", "empty" });
    expectError ("top level is a list", "- a\n- b\n", { "mapping" });
    expectError ("malformed YAML", "a: [1, 2\nb: oops\n", { "t.yaml", "malformed YAML" });
    expectError ("several problems at once", "foo: 1\nbar: 2\nrole: x\n", { "'foo'", "'bar'", "role" });

    Config c;
    std::string err;
    check (!Config::load ("/nonexistent/dir/none.yaml", c, err) && contains (err, "cannot read"), "load: missing file");
}

void testMerge()
{
    Config yaml, cli;
    std::string err;
    Config::parse ("server: yaml.example:1\ngroup: yaml-group\nrole: console\nusername: yamluser\n"
                   "audio:\n  input_device: YamlMic\n  buffer: 128\ncodec: opus\nbitrate: 64000\n",
                   "yaml", yaml, err);
    cli.role = "vdi";
    cli.group = "cli-group";
    const Config eff = Config::merge (cli, yaml);
    check (eff.role == "vdi", "CLI role beats YAML role");
    check (eff.group == "cli-group", "CLI group beats YAML group");
    check (eff.server == "yaml.example:1", "YAML fills what the CLI did not give");
    check (eff.username == "yamluser", "YAML username survives");
    check (eff.inputDevice == "YamlMic" && eff.buffer == 128 && eff.bitrate == 64000, "YAML audio/codec survive");
    check (!Config::merge (Config(), Config()).role, "nothing in either layer stays unset");
}

std::string applyOrDie (const std::string& yaml, std::optional<std::string> in, std::optional<std::string> out)
{
    std::string res, err;
    const bool ok = Config::applyAudioDevicesToText (yaml, in, out, res, err);
    check (ok, "applyAudioDevicesToText should succeed: " + err);
    return res;
}

void testTextEdit()
{
    // Replace in place: comments, blank lines, key order, quoting of untouched keys.
    const std::string src =
        "# Crosspoint agent -- do not remove this comment\n"
        "server: vpn.example.net:43210   # the VPN side\n"
        "\n"
        "audio:\n"
        "  # which devices\n"
        "  input_device: Built-in Microphone   # trailing note\n"
        "  output_device: \"Old Output\"\n"
        "  buffer: 256\n"
        "\n"
        "# footer comment\n"
        "group: studio-a\n";
    const std::string r = applyOrDie (src, std::string ("BlackHole 2ch"), std::string ("MacBook Pro Speakers"));
    const std::string expect =
        "# Crosspoint agent -- do not remove this comment\n"
        "server: vpn.example.net:43210   # the VPN side\n"
        "\n"
        "audio:\n"
        "  # which devices\n"
        "  input_device: BlackHole 2ch   # trailing note\n"
        "  output_device: \"MacBook Pro Speakers\"\n"
        "  buffer: 256\n"
        "\n"
        "# footer comment\n"
        "group: studio-a\n";
    check (r == expect, "in-place replace keeps comments/order/quoting. got:\n" + r);

    // Only one of the two.
    const std::string one = applyOrDie (src, std::nullopt, std::string ("Z"));
    check (contains (one, "input_device: Built-in Microphone   # trailing note\n") && contains (one, "output_device: \"Z\"\n"),
           "nullopt leaves a device untouched");

    // Unchanged value -> byte-identical text.
    check (applyOrDie (src, std::string ("Built-in Microphone"), std::nullopt) == src, "same value is a no-op");

    // Awkward names are quoted so they read back identically.
    for (const char* nm : { "Mic: USB #2", "He said \"hi\"", "back\\slash", "  padded ", "123", "true", "- dash", "it's" })
    {
        const std::string t = applyOrDie (src, std::string (nm), std::nullopt);
        Config c;
        std::string err;
        check (Config::parse (t, "t", c, err) && c.inputDevice == nm, std::string ("round-trips device name '") + nm + "': " + err);
    }

    // Single-quoted value keeps single quotes.
    const std::string sq = applyOrDie ("audio:\n  input_device: 'A'\n", std::string ("it's"), std::nullopt);
    check (sq == "audio:\n  input_device: 'it''s'\n", "single quotes stay single. got:\n" + sq);

    // Missing key is inserted at the audio section's indent, after its last child.
    const std::string ins = applyOrDie ("group: g\naudio:\n    buffer: 64\n    sample_rate: 48000   # sr\n# tail\nrole: vdi\n",
                                        std::string ("In"), std::string ("Out"));
    check (ins == "group: g\naudio:\n    buffer: 64\n    sample_rate: 48000   # sr\n    input_device: In\n    output_device: Out\n# tail\nrole: vdi\n",
           "missing keys are inserted after the last audio child. got:\n" + ins);

    // No audio section: appended at the end (also when the file has no final newline).
    const std::string app = applyOrDie ("group: g # c", std::string ("In"), std::nullopt);
    check (app == "group: g # c\naudio:\n  input_device: In\n", "audio section appended. got:\n" + app);
    check (applyOrDie ("", std::string ("In"), std::nullopt) == "audio:\n  input_device: In\n", "empty file gets an audio section");
    check (applyOrDie ("# just a comment\n", std::string ("In"), std::nullopt) == "# just a comment\naudio:\n  input_device: In\n",
           "comment-only file keeps its comment");

    // Empty audio: and a null device value.
    check (applyOrDie ("audio:   # devices\nrole: vdi\n", std::string ("In"), std::nullopt)
               == "audio:   # devices\n  input_device: In\nrole: vdi\n",
           "bare 'audio:' gets a child");
    check (applyOrDie ("audio:\n  input_device:\n  buffer: 32\n", std::string ("In"), std::nullopt)
               == "audio:\n  input_device: In\n  buffer: 32\n",
           "null device value is filled in");

    // CRLF files keep CRLF.
    const std::string crlf = applyOrDie ("group: g\r\naudio:\r\n  buffer: 64\r\n", std::string ("In"), std::nullopt);
    check (crlf == "group: g\r\naudio:\r\n  buffer: 64\r\n  input_device: In\r\n", "CRLF preserved. got:\n" + crlf);

    // Refusals: nothing is written and the reason is stated.
    std::string res, err;
    check (!Config::applyAudioDevicesToText ("audio: {buffer: 64}\n", std::string ("In"), std::nullopt, res, err)
               && contains (err, "flow style"),
           "flow-style audio is refused");
    check (!Config::applyAudioDevicesToText ("foo: 1\n", std::string ("In"), std::nullopt, res, err) && contains (err, "unknown key"),
           "an invalid config is refused");
    check (!Config::applyAudioDevicesToText ("a: 1\n", std::string(""), std::nullopt, res, err), "empty device name refused");
}

void testSave()
{
    char tmpl[] = "/tmp/config_test.XXXXXX";
    const std::string dir = ::mkdtemp (tmpl);
    const std::string f = dir + "/vdi.yaml";
    const std::string src = "# keep me\nserver: h:1\naudio:\n  # inputs\n  input_device: Old   # note\nrole: vdi\n";
    writeFile (f, src);
    ::chmod (f.c_str(), 0600);

    Config c;
    std::string err;
    check (Config::load (f, c, err), "save: load: " + err);
    check (c.path == f, "load records the path");
    c.setAudioDevices (std::string ("New Mic"), std::string ("New Out"));
    check (c.save (err), "save: " + err);

    const std::string after = readFile (f);
    check (after == "# keep me\nserver: h:1\naudio:\n  # inputs\n  input_device: New Mic   # note\n  output_device: New Out\nrole: vdi\n",
           "save keeps comments and order. got:\n" + after);

    struct stat st {};
    ::stat (f.c_str(), &st);
    check ((st.st_mode & 0777) == 0600, "save keeps the file permissions");

    int leftovers = 0;
    if (DIR* d = ::opendir (dir.c_str()))
    {
        while (dirent* e = ::readdir (d))
            if (std::string (e->d_name) != "." && std::string (e->d_name) != ".." && std::string (e->d_name) != "vdi.yaml")
                ++leftovers;
        ::closedir (d);
    }
    check (leftovers == 0, "no temp file is left behind");

    // A later human edit is not clobbered: save re-reads the file.
    writeFile (f, readFile (f) + "username: added-by-hand\n");
    c.setAudioDevices (std::string ("Newer"), std::nullopt);
    check (c.save (err), "second save: " + err);
    check (contains (readFile (f), "username: added-by-hand\n") && contains (readFile (f), "input_device: Newer"),
           "second save keeps the hand edit");

    // Saving through a symlink replaces the target, not the link.
    const std::string link = dir + "/link.yaml";
    if (::symlink (f.c_str(), link.c_str()) == 0)
    {
        Config lc;
        check (Config::load (link, lc, err), "symlink load");
        lc.setAudioDevices (std::string ("ViaLink"), std::nullopt);
        check (lc.save (err), "symlink save: " + err);
        check (isSymlink (link) && contains (readFile (f), "input_device: ViaLink"), "symlink is preserved, target updated");
    }

    // Failure leaves the file as it was.
    writeFile (f, "bogus_key: 1\n");
    c.setAudioDevices (std::string ("X"), std::nullopt);
    check (!c.save (err) && contains (err, "unknown key"), "save refuses an invalid file");
    check (readFile (f) == "bogus_key: 1\n", "refused save leaves the file untouched");

    Config nopath;
    check (!nopath.save (err), "save without a path fails cleanly");

    ::unlink (link.c_str());
    ::unlink (f.c_str());
    ::rmdir (dir.c_str());
}

} // namespace

int main()
{
    testParse();
    testErrors();
    testMerge();
    testTextEdit();
    testSave();

    if (g_failures != 0)
    {
        std::printf ("config_test: FAILED (%d of %d checks)\n", g_failures, g_checks);
        return 1;
    }
    std::printf ("config_test: OK (%d checks)\n", g_checks);
    return 0;
}
