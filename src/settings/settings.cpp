#include "settings/settings.h"

#include "raylib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace dkg
{
    namespace
    {
        // Defaults match the runtime's original fixed keyboard mapping.
        const PadButtonInfo kButtons[] = {
            {"up", "D-pad Up", KEY_UP},
            {"down", "D-pad Down", KEY_DOWN},
            {"left", "D-pad Left", KEY_LEFT},
            {"right", "D-pad Right", KEY_RIGHT},
            {"cross", "Cross (X)", KEY_X},
            {"circle", "Circle (O)", KEY_C},
            {"square", "Square", KEY_Z},
            {"triangle", "Triangle", KEY_V},
            {"l1", "L1", KEY_Q},
            {"r1", "R1", KEY_E},
            {"l2", "L2", KEY_ONE},
            {"r2", "R2", KEY_THREE},
            {"l3", "L3", KEY_LEFT_CONTROL},
            {"r3", "R3", KEY_RIGHT_CONTROL},
            {"start", "Start", KEY_ENTER},
            {"select", "Select", KEY_RIGHT_SHIFT},
            {"stick_up", "Left stick Up", KEY_W},
            {"stick_down", "Left stick Down", KEY_S},
            {"stick_left", "Left stick Left", KEY_A},
            {"stick_right", "Left stick Right", KEY_D},
        };
        static_assert(sizeof(kButtons) / sizeof(kButtons[0]) == size_t(PadButton::Count));

        struct KeyNameEntry
        {
            int key;
            const char *name;
        };
        const KeyNameEntry kKeyNames[] = {
            {KEY_SPACE, "SPACE"}, {KEY_ENTER, "ENTER"}, {KEY_TAB, "TAB"}, {KEY_BACKSPACE, "BACKSPACE"},
            {KEY_ESCAPE, "ESCAPE"}, {KEY_INSERT, "INSERT"}, {KEY_DELETE, "DELETE"}, {KEY_HOME, "HOME"},
            {KEY_END, "END"}, {KEY_PAGE_UP, "PAGE_UP"}, {KEY_PAGE_DOWN, "PAGE_DOWN"},
            {KEY_UP, "UP"}, {KEY_DOWN, "DOWN"}, {KEY_LEFT, "LEFT"}, {KEY_RIGHT, "RIGHT"},
            {KEY_LEFT_SHIFT, "LEFT_SHIFT"}, {KEY_RIGHT_SHIFT, "RIGHT_SHIFT"},
            {KEY_LEFT_CONTROL, "LEFT_CTRL"}, {KEY_RIGHT_CONTROL, "RIGHT_CTRL"},
            {KEY_LEFT_ALT, "LEFT_ALT"}, {KEY_RIGHT_ALT, "RIGHT_ALT"},
            {KEY_COMMA, "COMMA"}, {KEY_PERIOD, "PERIOD"}, {KEY_SLASH, "SLASH"}, {KEY_SEMICOLON, "SEMICOLON"},
            {KEY_APOSTROPHE, "APOSTROPHE"}, {KEY_MINUS, "MINUS"}, {KEY_EQUAL, "EQUAL"},
            {KEY_LEFT_BRACKET, "LEFT_BRACKET"}, {KEY_RIGHT_BRACKET, "RIGHT_BRACKET"},
            {KEY_BACKSLASH, "BACKSLASH"}, {KEY_GRAVE, "GRAVE"},
            {KEY_KP_0, "NUMPAD_0"}, {KEY_KP_1, "NUMPAD_1"}, {KEY_KP_2, "NUMPAD_2"}, {KEY_KP_3, "NUMPAD_3"},
            {KEY_KP_4, "NUMPAD_4"}, {KEY_KP_5, "NUMPAD_5"}, {KEY_KP_6, "NUMPAD_6"}, {KEY_KP_7, "NUMPAD_7"},
            {KEY_KP_8, "NUMPAD_8"}, {KEY_KP_9, "NUMPAD_9"}, {KEY_KP_ENTER, "NUMPAD_ENTER"},
            {KEY_KP_ADD, "NUMPAD_ADD"}, {KEY_KP_SUBTRACT, "NUMPAD_SUBTRACT"},
        };

        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
                ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                --b;
            return s.substr(a, b - a);
        }
        std::string lower(std::string s)
        {
            for (char &c : s)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        bool parseBool(const std::string &v, bool fallback)
        {
            const std::string l = lower(v);
            if (l == "1" || l == "true" || l == "yes" || l == "on")
                return true;
            if (l == "0" || l == "false" || l == "no" || l == "off")
                return false;
            return fallback;
        }
        int parseInt(const std::string &v, int fallback, int lo, int hi)
        {
            try
            {
                return std::clamp(std::stoi(v), lo, hi);
            }
            catch (...)
            {
                return fallback;
            }
        }
        std::string pick(const std::string &v, std::initializer_list<const char *> allowed, const std::string &fallback)
        {
            const std::string l = lower(v);
            for (const char *a : allowed)
                if (l == a)
                    return l;
            return fallback;
        }
        // Paths are stored as UTF-8.
        std::string pathText(const fs::path &p)
        {
            const std::u8string u = p.u8string();
            return std::string(u.begin(), u.end());
        }
        fs::path textPath(const std::string &s)
        {
            return fs::path(std::u8string(s.begin(), s.end()));
        }
    }

    const PadButtonInfo &padButtonInfo(PadButton button) { return kButtons[int(button)]; }

    const char *keyName(int key)
    {
        static char buffer[8];
        if (key >= KEY_A && key <= KEY_Z)
        {
            std::snprintf(buffer, sizeof(buffer), "%c", char(key));
            return buffer;
        }
        if (key >= KEY_ZERO && key <= KEY_NINE)
        {
            std::snprintf(buffer, sizeof(buffer), "%c", char(key));
            return buffer;
        }
        if (key >= KEY_F1 && key <= KEY_F12)
        {
            std::snprintf(buffer, sizeof(buffer), "F%d", key - KEY_F1 + 1);
            return buffer;
        }
        for (const KeyNameEntry &e : kKeyNames)
            if (e.key == key)
                return e.name;
        return key == 0 ? "NONE" : "?";
    }

    int keyFromName(const std::string &rawName)
    {
        std::string name = rawName;
        for (char &c : name)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
            return name[0];
        if (name.size() >= 2 && name[0] == 'F')
        {
            const int n = std::atoi(name.c_str() + 1);
            if (n >= 1 && n <= 12)
                return KEY_F1 + n - 1;
        }
        for (const KeyNameEntry &e : kKeyNames)
            if (name == e.name)
                return e.key;
        return 0;
    }

    Settings::Settings()
    {
        for (int i = 0; i < int(PadButton::Count); ++i)
            keys[i] = kButtons[i].defaultKey;
    }

    bool Settings::load(const fs::path &file)
    {
        std::ifstream in(file);
        if (!in)
            return false;
        std::string section;
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            // Whole-line comments only: values (paths) may contain ';' or '#'.
            if (line.empty() || line.front() == ';' || line.front() == '#')
                continue;
            if (line.front() == '[' && line.back() == ']')
            {
                section = lower(trim(line.substr(1, line.size() - 2)));
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = lower(trim(line.substr(0, eq)));
            const std::string value = trim(line.substr(eq + 1));
            const std::string full = section + "." + key;

            if (full == "game.iso") iso = textPath(value);
            else if (full == "game.data") data = textPath(value);
            else if (full == "game.memcard") memcard = textPath(value);
            else if (full == "display.mode") windowMode = pick(value, {"windowed", "borderless", "fullscreen"}, windowMode);
            else if (full == "display.width") windowWidth = parseInt(value, windowWidth, 320, 16384);
            else if (full == "display.height") windowHeight = parseInt(value, windowHeight, 240, 16384);
            else if (full == "display.aspect") aspect = pick(value, {"4:3", "stretch", "pixels"}, aspect);
            else if (full == "display.filter") filter = pick(value, {"linear", "nearest"}, filter);
            else if (full == "display.show_fps") showFps = parseBool(value, showFps);
            else if (full == "graphics.renderer") renderer = pick(value, {"gpu", "cpu"}, renderer);
            else if (full == "graphics.supersampling") supersampling = parseInt(value, supersampling, 1, 16);
            else if (full == "controls.gamepad") gamepad = pick(value, {"auto", "off", "0", "1", "2", "3"}, gamepad);
            else if (full == "controls.deadzone")
            {
                try { deadzone = std::clamp(std::stof(value), 0.0f, 0.9f); } catch (...) {}
            }
            else if (full == "advanced.vu1_thread") vu1Thread = parseBool(value, vu1Thread);
            else if (full == "advanced.compiled_vu") compiledVu = parseBool(value, compiledVu);
            else if (full == "launcher.close_on_play") closeOnPlay = parseBool(value, closeOnPlay);
            else
            {
                bool handled = false;
                if (section == "keys")
                {
                    for (int i = 0; i < int(PadButton::Count); ++i)
                    {
                        if (key == kButtons[i].key)
                        {
                            keys[i] = keyFromName(value);
                            handled = true;
                        }
                    }
                }
                if (!handled)
                    m_extra[full] = value;
            }
        }
        return true;
    }

    bool Settings::save(const fs::path &file) const
    {
        std::ostringstream out;
        out << "; DrakengardPC settings. Written by the launcher; you can also edit it by hand.\n"
               "; Paths may be relative to this folder.\n\n";
        out << "[game]\n"
            << "iso = " << pathText(iso) << "\n"
            << "data = " << pathText(data) << "\n"
            << "memcard = " << pathText(memcard) << "\n\n";
        out << "[display]\n"
            << "; mode: windowed | borderless | fullscreen;  aspect: 4:3 | stretch | pixels;  filter: linear | nearest\n"
            << "mode = " << windowMode << "\n"
            << "width = " << windowWidth << "\n"
            << "height = " << windowHeight << "\n"
            << "aspect = " << aspect << "\n"
            << "filter = " << filter << "\n"
            << "show_fps = " << (showFps ? "true" : "false") << "\n\n";
        out << "[graphics]\n"
            << "; renderer: gpu (Vulkan) | cpu;  supersampling: 1 2 4 8 16\n"
            << "renderer = " << renderer << "\n"
            << "supersampling = " << supersampling << "\n\n";
        out << "[controls]\n"
            << "; gamepad: auto | off | 0-3;  deadzone: 0.0-0.9\n"
            << "gamepad = " << gamepad << "\n"
            << "deadzone = " << deadzone << "\n\n";
        out << "; keys: keyboard key for each PS2 button (letters, digits, F1-F12, UP, ENTER, LEFT_SHIFT, ...)\n";
        out << "[keys]\n";
        for (int i = 0; i < int(PadButton::Count); ++i)
            out << kButtons[i].key << " = " << keyName(keys[i]) << "\n";
        out << "\n[advanced]\n"
            << "vu1_thread = " << (vu1Thread ? "true" : "false") << "\n"
            << "compiled_vu = " << (compiledVu ? "true" : "false") << "\n\n";
        out << "[launcher]\n"
            << "close_on_play = " << (closeOnPlay ? "true" : "false") << "\n";
        std::string lastSection;
        for (const auto &[full, value] : m_extra)
        {
            const size_t dot = full.find('.');
            const std::string section = full.substr(0, dot);
            if (section != lastSection)
            {
                out << "\n[" << section << "]\n";
                lastSection = section;
            }
            out << full.substr(dot + 1) << " = " << value << "\n";
        }
        std::error_code ec;
        if (file.has_parent_path())
            fs::create_directories(file.parent_path(), ec);
        std::ofstream f(file, std::ios::trunc);
        f << out.str();
        return bool(f);
    }

    fs::path Settings::resolve(const fs::path &folder, const fs::path &p) const
    {
        if (p.empty() || p.is_absolute())
            return p;
        return folder / p;
    }

    fs::path Settings::dataDir(const fs::path &folder) const
    {
        return data.empty() ? folder / "work" : resolve(folder, data);
    }

    fs::path Settings::memcardDir(const fs::path &folder) const
    {
        return memcard.empty() ? dataDir(folder) / "memcard" / "mc0" : resolve(folder, memcard);
    }

    std::vector<std::pair<std::string, std::string>> Settings::runtimeEnvironment() const
    {
        std::vector<std::pair<std::string, std::string>> env;
        env.emplace_back("PS2X_GS_RENDERER", renderer);
        env.emplace_back("PS2X_GS_SSAA", std::to_string(supersampling));
        env.emplace_back("PS2X_VU1_THREAD", vu1Thread ? "1" : "0");
        env.emplace_back("PS2X_VU_AOT", compiledVu ? "1" : "0");
        env.emplace_back("PS2X_WINDOW_MODE", windowMode);
        env.emplace_back("PS2X_WINDOW_SIZE", std::to_string(windowWidth) + "x" + std::to_string(windowHeight));
        env.emplace_back("PS2X_ASPECT", aspect);
        env.emplace_back("PS2X_FILTER", filter);
        env.emplace_back("PS2X_SHOW_FPS", showFps ? "1" : "0");
        env.emplace_back("PS2X_GAMEPAD", gamepad);
        char dz[32];
        std::snprintf(dz, sizeof(dz), "%.3f", deadzone);
        env.emplace_back("PS2X_PAD_DEADZONE", dz);
        std::string keymap;
        for (int i = 0; i < int(PadButton::Count); ++i)
        {
            if (!keymap.empty())
                keymap += ',';
            keymap += std::string(kButtons[i].key) + "=" + std::to_string(keys[i]);
        }
        env.emplace_back("PS2X_KEYMAP", keymap);
        return env;
    }

    fs::path findSettingsFile(const fs::path &exeDir)
    {
        fs::path dir = exeDir;
        for (int up = 0; up < 3 && !dir.empty(); ++up)
        {
            std::error_code ec;
            if (fs::exists(dir / kSettingsFileName, ec))
                return dir / kSettingsFileName;
            if (!dir.has_parent_path() || dir.parent_path() == dir)
                break;
            dir = dir.parent_path();
        }
        return {};
    }
}
