#include "game_identity.h"

#include <cstdio>
#include <cstring>

namespace dkg
{
    const GameIdentity &supportedGame()
    {
        // Values observed from a Redump-verified dump; see RESEARCH.md "Game build".
        static const GameIdentity id{
            "Drakengard",
            "SLUS-20732",
            "SLUS_207.32",
            4853316u,
            "652d95f1cc7f788e219948d52fc21ba2fffaefaa",
            0x9679D44Cu,
            "300f66970511bf6133cf3af11ffd2a13",
        };
        return id;
    }

    namespace
    {
        uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
    }

    std::string sha1Hex(const std::vector<uint8_t> &data)
    {
        uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
        std::vector<uint8_t> msg(data);
        const uint64_t bitLen = uint64_t(data.size()) * 8u;
        msg.push_back(0x80);
        while (msg.size() % 64 != 56)
            msg.push_back(0);
        for (int i = 7; i >= 0; --i)
            msg.push_back(uint8_t(bitLen >> (i * 8)));

        for (size_t chunk = 0; chunk < msg.size(); chunk += 64)
        {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(msg[chunk + i * 4]) << 24) | (uint32_t(msg[chunk + i * 4 + 1]) << 16) |
                       (uint32_t(msg[chunk + i * 4 + 2]) << 8) | uint32_t(msg[chunk + i * 4 + 3]);
            for (int i = 16; i < 80; ++i)
                w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i)
            {
                uint32_t f, k;
                if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
                else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
                else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
                const uint32_t t = rol(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        char out[41];
        for (int i = 0; i < 5; ++i)
            std::snprintf(out + i * 8, 9, "%08x", h[i]);
        return std::string(out, 40);
    }

    uint32_t pcsx2ElfCrc(const std::vector<uint8_t> &data)
    {
        uint32_t crc = 0;
        for (size_t i = 0; i + 4 <= data.size(); i += 4)
        {
            uint32_t w;
            std::memcpy(&w, data.data() + i, 4);
            crc ^= w;
        }
        return crc;
    }
}
