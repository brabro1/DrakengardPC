#include "disc/iso9660.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace dkg::disc
{
    namespace
    {
        uint32_t le32(const uint8_t *p)
        {
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }

        std::string normalize(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            if (auto colon = path.find(':'); colon != std::string::npos)
                path.erase(0, colon + 1); // drop "cdrom0:"
            if (auto semi = path.find(';'); semi != std::string::npos)
                path.erase(semi);
            while (!path.empty() && path.front() == '/')
                path.erase(path.begin());
            for (char &c : path)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return path;
        }
    }

    bool Iso9660::open(const std::filesystem::path &imagePath, std::string &error)
    {
        m_entries.clear();
        m_file.open(imagePath, std::ios::binary);
        if (!m_file)
        {
            error = "cannot open disc image " + imagePath.string();
            return false;
        }
        std::error_code ec;
        m_imageSize = std::filesystem::file_size(imagePath, ec);

        std::vector<uint8_t> pvd;
        if (!readSectors(16, 1, pvd) || pvd[0] != 1 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0)
        {
            error = "not an ISO9660 image (no primary volume descriptor at sector 16)";
            return false;
        }
        const uint8_t *root = pvd.data() + 156;
        walk(le32(root + 2), le32(root + 10), "", 0);
        return true;
    }

    bool Iso9660::readSectors(uint32_t lbn, uint32_t count, std::vector<uint8_t> &out)
    {
        out.assign(size_t(count) * kSectorSize, 0);
        m_file.clear();
        m_file.seekg(std::streamoff(lbn) * kSectorSize);
        m_file.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
        return m_file.gcount() == std::streamsize(out.size());
    }

    void Iso9660::walk(uint32_t lbn, uint32_t size, const std::string &prefix, int depth)
    {
        if (depth > 8)
            return;
        std::vector<uint8_t> dir;
        if (!readSectors(lbn, (size + kSectorSize - 1) / kSectorSize, dir))
            return;
        uint32_t off = 0;
        while (off < size)
        {
            const uint8_t len = dir[off];
            if (len == 0)
            {
                off = (off / kSectorSize + 1) * kSectorSize; // records never straddle sectors
                continue;
            }
            const uint8_t *rec = dir.data() + off;
            const uint8_t nameLen = rec[32];
            std::string name(reinterpret_cast<const char *>(rec + 33), nameLen);
            off += len;
            if (nameLen == 1 && (name[0] == '\0' || name[0] == '\1'))
                continue;
            IsoEntry e;
            e.path = normalize(prefix.empty() ? name : prefix + "/" + name);
            e.lbn = le32(rec + 2);
            e.size = le32(rec + 10);
            e.directory = (rec[25] & 2) != 0;
            m_entries.push_back(e);
            if (e.directory)
                walk(e.lbn, e.size, e.path, depth + 1);
        }
    }

    std::optional<IsoEntry> Iso9660::find(std::string path) const
    {
        path = normalize(std::move(path));
        for (const IsoEntry &e : m_entries)
            if (e.path == path)
                return e;
        return std::nullopt;
    }

    bool Iso9660::readFile(const IsoEntry &entry, std::vector<uint8_t> &out)
    {
        if (!readSectors(entry.lbn, (entry.size + kSectorSize - 1) / kSectorSize, out))
            return false;
        out.resize(entry.size);
        return true;
    }
}
