// yaml_smoke — proves the vendored rapidyaml actually compiles, links and runs.
//
// This is build-time proof only (P2.2). It is NOT wired into the app; P2.1 owns
// the real `--config` loader. The document below mirrors the key set P2.1 is
// specified to consume, so a successful run also exercises the traversal the
// loader will need (nested maps, scalars, numbers, booleans).
//
// Exit codes: 0 = parsed as expected, 1 = parse/traversal/expectation failure.

#include <ryml_all.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* const kYaml = R"YAML(
# SonoBus VDI agent config (P2.1 schema preview)
server: vpn.example.net:43210
group: studio-a
password: "hunter2"
username: vdi-01
role: vdi
audio:
  input_device: Built-in Microphone
  output_device: Built-in Output
  sample_rate: 48000
  buffer: 256
codec:
  type: opus
  bitrate: 128000
)YAML";

int g_failures = 0;

void fail(const std::string& what)
{
    std::printf("FAIL: %s\n", what.c_str());
    ++g_failures;
}

std::string to_string(ryml::csubstr s)
{
    return std::string(s.str, s.len);
}

// Flatten the tree into dotted "path = value" lines so the output is stable and
// human-checkable.
void walk(ryml::ConstNodeRef node, const std::string& prefix,
          std::vector<std::pair<std::string, std::string>>& out)
{
    if (node.is_map())
    {
        for (ryml::ConstNodeRef child : node.children())
        {
            if (!child.has_key())
                continue;
            const std::string key = to_string(child.key());
            walk(child, prefix.empty() ? key : prefix + "." + key, out);
        }
        return;
    }

    if (node.has_val())
        out.emplace_back(prefix, to_string(node.val()));
    else
        out.emplace_back(prefix, "");
}

// Looks up a dotted path such as "audio.sample_rate". Returns false if absent.
bool lookup(ryml::ConstNodeRef root, const std::string& path, std::string& value)
{
    ryml::ConstNodeRef cur = root;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t dot = path.find('.', start);
        const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);

        ryml::ConstNodeRef next = cur.find_child(ryml::to_csubstr(key));
        if (next.invalid())
            return false;

        cur = next;
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }

    if (!cur.has_val())
        return false;
    value = to_string(cur.val());
    return true;
}

void expect(ryml::ConstNodeRef root, const char* path, const char* expected)
{
    std::string got;
    if (!lookup(root, path, got))
        return fail(std::string("missing key '") + path + "'");
    if (got != expected)
        return fail(std::string("key '") + path + "' = '" + got + "', expected '" + expected + "'");
    std::printf("  %-24s = %s\n", path, got.c_str());
}

} // namespace

int main()
{
    std::printf("vendored ryml version: %s\n", to_string(ryml::version()).c_str());

    const std::string malformed = "a: [1, 2\nb: oops"; // unterminated flow sequence

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    // The vendored implementation defines RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS,
    // so a syntax error surfaces as std::runtime_error, not as abort().
    try
    {
        ryml::Tree bad = ryml::parse_in_arena(ryml::to_csubstr(malformed));
        fail("malformed YAML was accepted (expected a parse error)");
    }
    catch (const std::exception& e)
    {
        std::printf("malformed input correctly rejected: %s\n", e.what());
    }
#else
    std::printf("note: exceptions disabled, skipping malformed-input check\n");
#endif

    ryml::Tree tree;
    try
    {
        tree = ryml::parse_in_arena(ryml::to_csubstr(kYaml));
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL: cannot parse test document: %s\n", e.what());
        return 1;
    }

    ryml::ConstNodeRef root = tree.crootref();
    if (!root.is_map())
    {
        std::printf("FAIL: root of test document is not a map\n");
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> values;
    walk(root, "", values);

    std::printf("parsed %zu leaf value(s):\n", values.size());
    for (const auto& kv : values)
        std::printf("  %-24s = %s\n", kv.first.c_str(), kv.second.c_str());

    std::printf("checking expected values:\n");
    expect(root, "server", "vpn.example.net:43210");
    expect(root, "group", "studio-a");
    expect(root, "password", "hunter2");
    expect(root, "username", "vdi-01");
    expect(root, "role", "vdi");
    expect(root, "audio.input_device", "Built-in Microphone");
    expect(root, "audio.output_device", "Built-in Output");
    expect(root, "audio.sample_rate", "48000");
    expect(root, "audio.buffer", "256");
    expect(root, "codec.type", "opus");
    expect(root, "codec.bitrate", "128000");

    if (values.size() != 11)
        fail("expected 11 leaf values, got " + std::to_string(values.size()));

    if (g_failures != 0)
    {
        std::printf("yaml_smoke: FAILED (%d problem(s))\n", g_failures);
        return 1;
    }

    std::printf("yaml_smoke: OK\n");
    return 0;
}
