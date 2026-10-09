// Known-answer test for Source/ApiSha1.h. Build and run with:
//   cmake --build <builddir> --target sha1_test && <builddir>/sha1_test
#include "../Source/ApiSha1.h"
#include <algorithm>
#include <cstdio>
#include <string>

static std::string hex (const std::array<uint8_t, 20>& d)
{
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (auto b : d) { s += digits[b >> 4]; s += digits[b & 15]; }
    return s;
}

static int failures = 0;
static void check (const std::string& name, const std::string& got, const std::string& want)
{
    const bool ok = got == want;
    std::printf ("%s %s\n", ok ? "ok  " : "FAIL", name.c_str());
    if (! ok) { std::printf ("  got  %s\n  want %s\n", got.c_str(), want.c_str()); ++failures; }
}

int main()
{
    using crosspoint::Sha1;
    auto h = [] (const std::string& s) { return hex (Sha1::hash (s.data(), s.size())); };

    // FIPS 180 / RFC 3174 vectors
    check ("empty", h (""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check ("abc", h ("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    check ("448-bit", h ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    check ("million a", h (std::string (1000000, 'a')), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    // RFC 6455 section 1.3 handshake example input
    check ("rfc6455 key+guid", h ("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11"),
           "b37a4f2cc0624f1690f64606cf385945b2bec4ea");
    // incremental update across block boundaries
    Sha1 s; std::string big (1000000, 'a');
    for (size_t i = 0; i < big.size(); i += 777) s.update (big.data() + i, std::min<size_t> (777, big.size() - i));
    check ("million a chunked", hex (s.finish()), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    std::printf (failures == 0 ? "all passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
