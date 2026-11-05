#include "disc/disc_info.h"

#include "disc/iso9660.h"
#include "game_identity.h"

namespace dkg::disc
{
    namespace
    {
        // "cdrom0:\SLUS_207.32;1" -> "SLUS_207.32"
        std::string bootElfFromCnf(const std::string &cnf)
        {
            const size_t boot = cnf.find("BOOT2");
            if (boot == std::string::npos)
                return {};
            size_t start = cnf.find('\\', boot);
            if (start == std::string::npos)
                start = cnf.find(':', boot);
            if (start == std::string::npos)
                return {};
            ++start;
            size_t end = start;
            while (end < cnf.size() && cnf[end] != ';' && cnf[end] != '\r' && cnf[end] != '\n' && cnf[end] != ' ')
                ++end;
            return cnf.substr(start, end - start);
        }

        std::string regionOf(const std::string &elf)
        {
            const std::string prefix = elf.substr(0, 4);
            if (prefix == "SLUS" || prefix == "SCUS")
                return "USA";
            if (prefix == "SLES" || prefix == "SCES")
                return "Europe";
            if (prefix == "SLPM" || prefix == "SLPS" || prefix == "SCPS")
                return "Japan";
            return {};
        }
    }

    DiscInfo identifyDisc(const std::filesystem::path &image)
    {
        DiscInfo info;
        Iso9660 iso;
        std::string error;
        if (!iso.open(image, error))
        {
            info.message = "This file could not be read as a disc image (" + error + ").";
            return info;
        }
        auto cnf = iso.find("SYSTEM.CNF");
        std::vector<uint8_t> cnfData;
        if (!cnf || !iso.readFile(*cnf, cnfData))
        {
            info.message = "This is not a PlayStation 2 game disc image.";
            return info;
        }
        info.bootElf = bootElfFromCnf(std::string(cnfData.begin(), cnfData.end()));
        info.region = regionOf(info.bootElf);
        if (info.bootElf.size() >= 11)
            info.serial = info.bootElf.substr(0, 4) + "-" + info.bootElf.substr(5, 3) + info.bootElf.substr(9, 2);

        const GameIdentity &game = supportedGame();
        if (info.bootElf == game.bootElfName)
        {
            auto elf = iso.find(game.bootElfName);
            std::vector<uint8_t> elfData;
            if (!elf || !iso.readFile(*elf, elfData))
            {
                info.status = DiscStatus::NotPs2Disc;
                info.message = "The game program is missing from this image; the dump may be damaged.";
                return info;
            }
            info.elfSha1 = sha1Hex(elfData);
            if (info.elfSha1 == game.bootElfSha1 && elfData.size() == game.bootElfSize)
            {
                info.status = DiscStatus::Supported;
                info.message = "Drakengard (USA) v1.00 - supported.";
            }
            else
            {
                info.status = DiscStatus::KnownUnsupported;
                info.message = "Drakengard (USA), but a different revision than v1.00 - not supported yet.";
            }
            return info;
        }
        if (info.bootElf == "SLES_523.22")
        {
            info.status = DiscStatus::KnownUnsupported;
            info.message = "Drakengard (Europe) - recognised, not supported yet. Only the USA version runs for now.";
            return info;
        }
        if (info.region == "Japan")
        {
            // The Japanese release (Drag-On Dragoon) serial is not recorded yet; any Japanese disc
            // gets this message.
            info.status = DiscStatus::KnownUnsupported;
            info.message = "A Japanese PS2 disc (" + info.serial +
                           "). Drag-On Dragoon is not supported yet; only Drakengard (USA) runs for now.";
            return info;
        }
        info.status = DiscStatus::OtherGame;
        info.message = "This PS2 disc (" + (info.serial.empty() ? std::string("unknown serial") : info.serial) +
                       ") is not Drakengard (USA).";
        return info;
    }
}
