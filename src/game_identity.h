#pragma once

// The only game revision this build supports. Generated code, function
// addresses and patches are tied to this exact boot executable; any other
// release must be refused rather than run with mismatched code.

#include <cstdint>
#include <string>
#include <vector>

namespace dkg
{
    struct GameIdentity
    {
        const char *title;
        const char *serial;
        const char *bootElfName;   // as named in SYSTEM.CNF BOOT2
        uint32_t bootElfSize;
        const char *bootElfSha1;   // lower-case hex
        uint32_t pcsx2Crc;         // XOR of all 32-bit words, as PCSX2 reports it
        const char *discMd5;       // Redump, for documentation; not checked at start
    };

    const GameIdentity &supportedGame();

    std::string sha1Hex(const std::vector<uint8_t> &data);
    uint32_t pcsx2ElfCrc(const std::vector<uint8_t> &data);
}
