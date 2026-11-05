#pragma once

// DrakengardPC.ini: the player's settings, shared by the launcher and the game.
//
// The file sits in the DrakengardPC folder (next to the launcher). Paths in it may be
// relative to that folder. Unknown keys are kept when the file is saved again.

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dkg
{
    // PS2 controller buttons that can be bound to keyboard keys, in display order.
    enum class PadButton
    {
        Up, Down, Left, Right,
        Cross, Circle, Square, Triangle,
        L1, R1, L2, R2, L3, R3,
        Start, Select,
        StickUp, StickDown, StickLeft, StickRight, // left analog stick
        Count
    };

    struct PadButtonInfo
    {
        const char *key;   // INI key, e.g. "cross"
        const char *label; // shown in the launcher
        int defaultKey;    // raylib KeyboardKey
    };
    const PadButtonInfo &padButtonInfo(PadButton button);

    // raylib KeyboardKey <-> readable name ("X", "ENTER", "LEFT_SHIFT", ...).
    const char *keyName(int key);
    int keyFromName(const std::string &name);

    struct Settings
    {
        // [game]
        std::filesystem::path iso;     // the player's disc image
        std::filesystem::path data;    // generated data (default: work)
        std::filesystem::path memcard; // memory card folder (default: <data>/memcard/mc0)

        // [display]
        std::string windowMode = "windowed"; // windowed | borderless | fullscreen
        int windowWidth = 1280;
        int windowHeight = 960;
        std::string aspect = "4:3";          // 4:3 | stretch | pixels
        std::string filter = "linear";       // linear | nearest
        bool showFps = false;

        // [graphics]
        std::string renderer = "gpu";        // gpu (Vulkan) | cpu
        int supersampling = 1;               // 1, 2, 4, 8, 16 (GPU renderer)

        // [controls]
        std::string gamepad = "auto";        // auto | off | 0..3
        float deadzone = 0.15f;              // 0..0.9, analog sticks
        int keys[int(PadButton::Count)]{};   // raylib KeyboardKey per button (0 = unbound)

        // [advanced]
        bool vu1Thread = true;               // VIF1/VU1 on a worker thread
        bool compiledVu = true;              // compiled VU microprograms

        // [launcher]
        bool closeOnPlay = false;            // close the launcher when the game starts

        Settings();

        // folder: the DrakengardPC folder that holds the INI.
        bool load(const std::filesystem::path &file);
        bool save(const std::filesystem::path &file) const;

        std::filesystem::path resolve(const std::filesystem::path &folder, const std::filesystem::path &p) const;
        std::filesystem::path dataDir(const std::filesystem::path &folder) const;
        std::filesystem::path memcardDir(const std::filesystem::path &folder) const;

        // Environment variables the runtime reads (PS2X_*), as name/value pairs.
        std::vector<std::pair<std::string, std::string>> runtimeEnvironment() const;

    private:
        std::map<std::string, std::string> m_extra; // unknown "section.key" entries, kept on save
    };

    // The settings file used by the game: --settings, else DrakengardPC.ini in the exe folder or
    // up to two folders above it (build/game/ inside the DrakengardPC folder).
    std::filesystem::path findSettingsFile(const std::filesystem::path &exeDir);
    inline constexpr const char *kSettingsFileName = "DrakengardPC.ini";
}
