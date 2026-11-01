#pragma once

// Read-only ISO9660 access to the user's disc image. The image is opened for
// reading only; nothing in the port ever writes to it.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace dkg::disc
{
    struct IsoEntry
    {
        std::string path; // upper-case, '/'-separated, no ";1" version suffix
        uint32_t lbn = 0;
        uint32_t size = 0;
        bool directory = false;
    };

    class Iso9660
    {
    public:
        static constexpr uint32_t kSectorSize = 2048;

        bool open(const std::filesystem::path &imagePath, std::string &error);
        const std::vector<IsoEntry> &entries() const { return m_entries; }
        std::optional<IsoEntry> find(std::string path) const;
        bool readFile(const IsoEntry &entry, std::vector<uint8_t> &out);
        uint64_t imageSize() const { return m_imageSize; }

    private:
        bool readSectors(uint32_t lbn, uint32_t count, std::vector<uint8_t> &out);
        void walk(uint32_t lbn, uint32_t size, const std::string &prefix, int depth);

        std::ifstream m_file;
        uint64_t m_imageSize = 0;
        std::vector<IsoEntry> m_entries;
    };
}
