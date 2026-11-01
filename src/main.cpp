// DrakengardPC host entry point.
//
// Verifies the user's disc is the supported revision, stages the few small boot
// files the IOP side loads by name (ELF, IRX modules), points the runtime at the
// disc image for everything else, then runs the statically recompiled game code.

#include "disc/iso9660.h"
#include "game_identity.h"

#include "ps2_runtime.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct Options
    {
        fs::path iso;
        fs::path dataDir = fs::current_path() / "work";
        int maxSeconds = 0; // 0 = run until the window closes
    };

    void usage()
    {
        std::cerr << "usage: DrakengardPC --iso <Drakengard (USA).iso> [--data <dir>] [--max-seconds N]\n";
    }

    bool parseArgs(int argc, char **argv, Options &opt)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
            if (a == "--iso") { if (const char *v = next()) opt.iso = v; else return false; }
            else if (a == "--data") { if (const char *v = next()) opt.dataDir = v; else return false; }
            else if (a == "--max-seconds") { if (const char *v = next()) opt.maxSeconds = std::atoi(v); else return false; }
            else { std::cerr << "unknown argument: " << a << "\n"; return false; }
        }
        return !opt.iso.empty();
    }

    bool writeFile(const fs::path &path, const std::vector<uint8_t> &data)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
        return bool(out);
    }

    // Returns the staged ELF path, or empty on failure.
    fs::path verifyAndStage(const Options &opt)
    {
        const dkg::GameIdentity &game = dkg::supportedGame();
        dkg::disc::Iso9660 iso;
        std::string error;
        if (!iso.open(opt.iso, error))
        {
            std::cerr << "[disc] " << error << "\n";
            return {};
        }

        auto cnf = iso.find("SYSTEM.CNF");
        std::vector<uint8_t> cnfData;
        if (!cnf || !iso.readFile(*cnf, cnfData))
        {
            std::cerr << "[disc] SYSTEM.CNF not found: this is not a PS2 game disc\n";
            return {};
        }
        const std::string cnfText(cnfData.begin(), cnfData.end());
        if (cnfText.find(game.bootElfName) == std::string::npos)
        {
            std::cerr << "[disc] SYSTEM.CNF does not boot " << game.bootElfName
                      << "; this build only supports " << game.title << " (" << game.serial << ")\n";
            return {};
        }

        auto elf = iso.find(game.bootElfName);
        std::vector<uint8_t> elfData;
        if (!elf || !iso.readFile(*elf, elfData))
        {
            std::cerr << "[disc] boot executable " << game.bootElfName << " missing from disc\n";
            return {};
        }
        const std::string sha1 = dkg::sha1Hex(elfData);
        const uint32_t crc = dkg::pcsx2ElfCrc(elfData);
        std::cout << "[disc] " << game.bootElfName << " size=" << elfData.size() << " sha1=" << sha1
                  << " crc=" << std::hex << std::uppercase << crc << std::dec << std::nouppercase << "\n";
        if (elfData.size() != game.bootElfSize || sha1 != game.bootElfSha1)
        {
            std::cerr << "[disc] unsupported revision: expected sha1 " << game.bootElfSha1
                      << ". The recompiled code only matches " << game.serial << " v1.00.\n";
            return {};
        }

        // Stage only small files that are opened by name through the IOP file
        // system. Large archives (*.BIN) are read from the image at their real LBNs.
        const fs::path root = opt.dataDir / "disc_root";
        size_t staged = 0;
        for (const dkg::disc::IsoEntry &e : iso.entries())
        {
            // The ELF is 4.7 MB and the smallest archive is 152 MB.
            if (e.directory || e.size > 16u * 1024u * 1024u)
                continue;
            const fs::path dst = root / fs::path(e.path);
            std::error_code ec;
            if (fs::exists(dst, ec) && fs::file_size(dst, ec) == e.size)
                continue;
            std::vector<uint8_t> data;
            if (!iso.readFile(e, data) || !writeFile(dst, data))
            {
                std::cerr << "[disc] failed to stage " << e.path << "\n";
                return {};
            }
            ++staged;
        }
        std::cout << "[disc] staged " << staged << " new boot file(s) in " << root.string() << "\n";
        return root / game.bootElfName;
    }
}

int main(int argc, char **argv)
{
    Options opt;
    if (!parseArgs(argc, argv, opt))
    {
        usage();
        return 2;
    }

    const fs::path elfPath = verifyAndStage(opt);
    if (elfPath.empty())
        return 1;

    if (opt.maxSeconds > 0)
    {
        // Bounded runs for automated boot tests: flush logs and leave.
        std::thread([s = opt.maxSeconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(s));
            std::cout << "\n[host] --max-seconds " << s << " reached, exiting" << std::endl;
            std::cerr.flush();
            std::_Exit(0);
        }).detach();
    }

    try
    {
        PS2Runtime runtime;
        if (!runtime.initialize("DrakengardPC"))
        {
            std::cerr << "[host] runtime initialization failed\n";
            return 1;
        }
        if (!runtime.loadELF(elfPath.string()))
        {
            std::cerr << "[host] failed to load " << elfPath.string() << "\n";
            return 1;
        }

        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        paths.cdImage = fs::absolute(opt.iso);
        paths.mcRoot = opt.dataDir / "memcard" / "mc0";
        fs::create_directories(paths.mcRoot);
        PS2Runtime::setIoPaths(paths);

        runtime.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[host] fatal exception: " << e.what() << "\n";
        std::cerr.flush();
        std::_Exit(1);
    }
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(0);
}
