#pragma once

// Identifies a disc image for the launcher and the game: which PS2 game and region it is, and
// whether this build can run it.

#include <filesystem>
#include <string>

namespace dkg::disc
{
    enum class DiscStatus
    {
        Supported,          // Drakengard (USA) v1.00: the build supports it
        KnownUnsupported,   // another Drakengard release (Europe, Japan): recognised, not supported yet
        OtherGame,          // a PS2 disc, but not Drakengard
        NotPs2Disc,         // no SYSTEM.CNF / unreadable image
    };

    struct DiscInfo
    {
        DiscStatus status = DiscStatus::NotPs2Disc;
        std::string serial;     // e.g. "SLUS-20732"
        std::string region;     // "USA", "Europe", "Japan" or empty
        std::string bootElf;    // e.g. "SLUS_207.32"
        std::string elfSha1;    // only when the boot ELF was read
        std::string message;    // one sentence for the player
    };

    // Reads SYSTEM.CNF and, for Drakengard (USA), hashes the boot ELF (a few MB).
    DiscInfo identifyDisc(const std::filesystem::path &image);
}
