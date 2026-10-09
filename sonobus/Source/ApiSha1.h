// Minimal SHA-1 (FIPS 180-4) for the control API's WebSocket handshake
// (RFC 6455 section 4.2.2: Sec-WebSocket-Accept). Not for any security use.
// Header-only and free of JUCE so sonobus/tests/sha1_test.cpp can build it alone.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crosspoint {

class Sha1
{
public:
    Sha1() { reset(); }

    void reset()
    {
        h[0] = 0x67452301u; h[1] = 0xEFCDAB89u; h[2] = 0x98BADCFEu;
        h[3] = 0x10325476u; h[4] = 0xC3D2E1F0u;
        totalBytes = 0;
        used = 0;
    }

    void update (const void* data, size_t len)
    {
        auto* p = static_cast<const uint8_t*> (data);
        totalBytes += len;
        while (len > 0)
        {
            const size_t n = (64 - used < len) ? (64 - used) : len;
            std::memcpy (block + used, p, n);
            used += n; p += n; len -= n;
            if (used == 64) { compress(); used = 0; }
        }
    }

    std::array<uint8_t, 20> finish()
    {
        const uint64_t bits = totalBytes * 8;
        const uint8_t pad80 = 0x80, zero = 0;
        update (&pad80, 1);
        while (used != 56) update (&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = (uint8_t) (bits >> (56 - 8 * i));
        update (lenBytes, 8);

        std::array<uint8_t, 20> out {};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 4; ++j)
                out[(size_t) (i * 4 + j)] = (uint8_t) (h[i] >> (24 - 8 * j));
        return out;
    }

    static std::array<uint8_t, 20> hash (const void* data, size_t len)
    {
        Sha1 s; s.update (data, len); return s.finish();
    }

private:
    static uint32_t rol (uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void compress()
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t) block[i * 4] << 24) | ((uint32_t) block[i * 4 + 1] << 16)
                 | ((uint32_t) block[i * 4 + 2] << 8) | (uint32_t) block[i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            const uint32_t t = rol (a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol (b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    uint32_t h[5];
    uint64_t totalBytes = 0;
    uint8_t block[64];
    size_t used = 0;
};

} // namespace crosspoint
