// DrakengardPC Launcher: settings, disc check, building the game from the player's disc, Play.
//
// Everything shown as "Coming soon" is planned but does not exist yet; those controls are
// disabled so the launcher never pretends a feature works.

#include "launcher/launcher_ui.h"

#include "disc/disc_info.h"
#include "launcher/launcher_process.h"
#include "settings/settings.h"

#include "extras/FA6FreeSolidFontData.h"
#include "extras/IconsFontAwesome6.h"
#include "imgui.h"
#include "raylib.h"

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace dkg::launcher
{
    namespace
    {
        constexpr const char *kVersion = "0.1 preview";
        constexpr const char *kRepoUrl = "https://github.com/brabro1/DrakengardPC";

        // Palette: near-black stone, parchment text, blood-red accent, muted gold.
        const ImVec4 kBg{0.055f, 0.051f, 0.063f, 1.0f};
        const ImVec4 kPanel{0.090f, 0.082f, 0.102f, 1.0f};
        const ImVec4 kPanelHi{0.130f, 0.118f, 0.145f, 1.0f};
        const ImVec4 kText{0.910f, 0.886f, 0.847f, 1.0f};
        const ImVec4 kMuted{0.600f, 0.580f, 0.560f, 1.0f};
        const ImVec4 kAccent{0.640f, 0.141f, 0.180f, 1.0f};
        const ImVec4 kAccentHi{0.760f, 0.200f, 0.240f, 1.0f};
        const ImVec4 kGold{0.784f, 0.643f, 0.353f, 1.0f};
        const ImVec4 kGood{0.420f, 0.720f, 0.420f, 1.0f};
        const ImVec4 kWarn{0.900f, 0.680f, 0.260f, 1.0f};
        const ImVec4 kBad{0.880f, 0.330f, 0.330f, 1.0f};

        enum class Page
        {
            Game, Display, Graphics, Controls, Audio, Saves, Enhancements, Build, About, Count
        };
        struct PageInfo
        {
            const char *icon;
            const char *name;
        };
        const PageInfo kPages[] = {
            {ICON_FA_COMPACT_DISC, "Game"},       {ICON_FA_DISPLAY, "Display"},
            {ICON_FA_MICROCHIP, "Graphics"},      {ICON_FA_GAMEPAD, "Controls"},
            {ICON_FA_VOLUME_HIGH, "Audio"},       {ICON_FA_FLOPPY_DISK, "Saves"},
            {ICON_FA_WAND_MAGIC_SPARKLES, "PC Features"}, {ICON_FA_HAMMER, "Build"},
            {ICON_FA_CIRCLE_INFO, "About"},
        };

        std::string u8(const fs::path &p)
        {
            const std::u8string s = p.u8string();
            return std::string(s.begin(), s.end());
        }
    }

    struct LauncherApp::State
    {
        fs::path root;
        fs::path iniPath;
        fs::path gameExe;
        Settings settings;
        bool dirty = false;

        disc::DiscInfo disc;
        bool discChecked = false;

        Page page = Page::Game;
        ImFont *fontBody = nullptr;
        ImFont *fontBold = nullptr;

        // Build
        ChildProcess build;
        enum class BuildState { Idle, Running, Done, Failed } buildState = BuildState::Idle;
        std::string buildTask; // "build" or "packages"
        std::vector<std::string> log;
        bool scrollLog = false;
        int step = 0, steps = 5;
        std::string stepText, buildError;

        // Tool check
        ChildProcess toolCheck;
        bool toolsChecking = false, toolsChecked = false;
        std::map<std::string, bool> tools;
        std::wstring python;

        // Controls
        int capturing = -1; // PadButton being rebound

        // Setup (the launcher alone, without the DrakengardPC folder)
        bool sourceMissing = false;
        ChildProcess download;
        bool downloading = false;

        // Saves
        std::string toast;
        double toastUntil = 0.0;

        void markDirty() { dirty = true; }
        void saveIfDirty()
        {
            if (dirty && !sourceMissing)
            {
                settings.save(iniPath);
                dirty = false;
            }
        }
        void notify(const std::string &text)
        {
            toast = text;
            toastUntil = GetTime() + 4.0;
        }
        void checkDisc()
        {
            discChecked = !settings.iso.empty();
            if (discChecked)
                disc = disc::identifyDisc(settings.resolve(root, settings.iso));
        }
        bool gameBuilt() const
        {
            std::error_code ec;
            return fs::exists(gameExe, ec);
        }
        bool canPlay() const
        {
            return discChecked && disc.status == disc::DiscStatus::Supported && gameBuilt() &&
                   buildState != BuildState::Running;
        }
    };

    fs::path findProjectRoot(const fs::path &start)
    {
        fs::path dir = start;
        for (int i = 0; i < 6 && !dir.empty(); ++i)
        {
            std::error_code ec;
            if (fs::exists(dir / "tools" / "build_game.py", ec))
                return dir;
            if (dir.parent_path() == dir)
                break;
            dir = dir.parent_path();
        }
        // Not inside a DrakengardPC folder: the launcher's Setup page downloads it next to the exe.
        return start / "DrakengardPC";
    }

    LauncherApp::LauncherApp(const fs::path &root) : m(std::make_unique<State>())
    {
        m->root = root;
        m->python = pythonCommand();
        reload();
    }

    void LauncherApp::reload()
    {
        std::error_code ec;
        m->sourceMissing = !fs::exists(m->root / "tools" / "build_game.py", ec);
        m->iniPath = m->root / kSettingsFileName;
        m->gameExe = m->root / "build" / "game" / "DrakengardPC.exe";
        m->settings = Settings();
        if (!m->sourceMissing && !m->settings.load(m->iniPath))
            m->dirty = true; // write the defaults once
        m->checkDisc();
    }

    LauncherApp::~LauncherApp() = default;

    void LauncherApp::initFonts()
    {
        ImGuiIO &io = ImGui::GetIO();
        const char *body = "C:/Windows/Fonts/segoeui.ttf";
        const char *bold = "C:/Windows/Fonts/segoeuib.ttf";
        if (FileExists(body) && FileExists(bold))
        {
            io.Fonts->Clear();
            m->fontBold = io.Fonts->AddFontFromFileTTF(bold, 18.0f);
            // Icons in the bold font too (rlImGui merges them only into the last font added).
            static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
            ImFontConfig icons;
            icons.MergeMode = true;
            icons.PixelSnapH = true;
            icons.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryCompressedTTF((void *)fa_solid_900_compressed_data, fa_solid_900_compressed_size,
                                                     16.0f, &icons, iconRanges);
            m->fontBody = io.Fonts->AddFontFromFileTTF(body, 18.0f); // last font: rlImGui merges icons here
            io.FontDefault = m->fontBody;
        }

        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 0.0f;
        s.ChildRounding = 6.0f;
        s.FrameRounding = 5.0f;
        s.GrabRounding = 5.0f;
        s.PopupRounding = 6.0f;
        s.TabRounding = 5.0f;
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(10, 9);
        s.WindowPadding = ImVec2(0, 0);
        s.ScrollbarSize = 12.0f;
        ImVec4 *c = s.Colors;
        c[ImGuiCol_WindowBg] = kBg;
        c[ImGuiCol_ChildBg] = kPanel;
        c[ImGuiCol_PopupBg] = kPanelHi;
        c[ImGuiCol_Text] = kText;
        c[ImGuiCol_TextDisabled] = ImVec4(0.42f, 0.40f, 0.40f, 1.0f);
        c[ImGuiCol_Border] = ImVec4(0.22f, 0.20f, 0.23f, 1.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.13f, 0.16f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.17f, 0.21f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.20f, 0.25f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.17f, 0.15f, 0.19f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.21f, 0.25f, 1.0f);
        c[ImGuiCol_ButtonActive] = kAccent;
        c[ImGuiCol_Header] = ImVec4(0.20f, 0.13f, 0.15f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.15f, 0.18f, 1.0f);
        c[ImGuiCol_HeaderActive] = kAccent;
        c[ImGuiCol_CheckMark] = kAccentHi;
        c[ImGuiCol_SliderGrab] = kAccent;
        c[ImGuiCol_SliderGrabActive] = kAccentHi;
        c[ImGuiCol_PlotHistogram] = kAccent;
        c[ImGuiCol_Separator] = ImVec4(0.22f, 0.20f, 0.23f, 1.0f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.13f, 0.12f, 0.15f, 1.0f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    }

    namespace
    {
        // ---- small UI helpers ----
        void heading(LauncherApp::State &st, const char *text, const char *sub = nullptr)
        {
            ImGui::PushFont(st.fontBold, 26.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopFont();
            if (sub)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
                ImGui::TextWrapped("%s", sub);
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }

        void sectionTitle(LauncherApp::State &st, const char *text)
        {
            ImGui::Spacing();
            ImGui::PushFont(st.fontBold, 19.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, kGold);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Separator();
        }

        void hint(const char *text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void chip(const char *text, ImVec4 color)
        {
            const ImVec2 pad(8, 2);
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec4 bg = color;
            bg.w = 0.18f;
            dl->AddRectFilled(p, ImVec2(p.x + size.x + pad.x * 2, p.y + size.y + pad.y * 2), ImGui::GetColorU32(bg), 10.0f);
            dl->AddRect(p, ImVec2(p.x + size.x + pad.x * 2, p.y + size.y + pad.y * 2), ImGui::GetColorU32(color), 10.0f);
            dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(color), text);
            ImGui::Dummy(ImVec2(size.x + pad.x * 2, size.y + pad.y * 2 + 2)); // reserve the space
        }

        // Settings table: label (+ help) on the left, control on the right.
        bool beginSettings(const char *id)
        {
            if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
                return false;
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            return true;
        }
        void row(const char *label, const char *help = nullptr)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (help)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(help);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
        }
        void comingSoonRow(const char *label, const char *help)
        {
            ImGui::BeginDisabled();
            row(label, help);
            ImGui::EndDisabled();
            chip("Coming soon", kGold);
        }

        // Combo over (value, label) pairs. Returns true when changed.
        bool choice(const char *id, std::string &value, const std::vector<std::pair<std::string, std::string>> &options)
        {
            std::string current = value;
            for (const auto &[v, label] : options)
                if (v == value)
                    current = label;
            bool changed = false;
            if (ImGui::BeginCombo(id, current.c_str()))
            {
                for (const auto &[v, label] : options)
                {
                    const bool selected = v == value;
                    if (ImGui::Selectable(label.c_str(), selected) && !selected)
                    {
                        value = v;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool accentButton(const char *label, ImVec2 size, bool enabled = true)
        {
            ImGui::BeginDisabled(!enabled);
            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHi);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentHi);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            return pressed;
        }

        void statusLine(const char *icon, ImVec4 color, const std::string &text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", text.c_str());
        }
    }

    // ------------------------------------------------------------------ pages

    namespace
    {
        void pageGame(LauncherApp::State &st)
        {
            heading(st, "Game", "Choose your own copy of Drakengard. Nothing from the game is included with DrakengardPC.");
            sectionTitle(st, "Disc image");
            std::string iso = st.settings.iso.empty() ? std::string("(none chosen)") : u8(st.settings.iso);
            ImGui::PushStyleColor(ImGuiCol_Text, st.settings.iso.empty() ? kMuted : kText);
            ImGui::TextWrapped("%s", iso.c_str());
            ImGui::PopStyleColor();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Choose disc image..."))
            {
                const fs::path picked = pickIsoFile(st.settings.resolve(st.root, st.settings.iso));
                if (!picked.empty())
                {
                    st.settings.iso = picked;
                    st.markDirty();
                    st.checkDisc();
                }
            }
            if (st.discChecked)
            {
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Check again"))
                    st.checkDisc();
                ImGui::Spacing();
                switch (st.disc.status)
                {
                case disc::DiscStatus::Supported:
                    statusLine(ICON_FA_CIRCLE_CHECK, kGood, st.disc.message);
                    break;
                case disc::DiscStatus::KnownUnsupported:
                    statusLine(ICON_FA_TRIANGLE_EXCLAMATION, kWarn, st.disc.message);
                    break;
                default:
                    statusLine(ICON_FA_CIRCLE_XMARK, kBad, st.disc.message);
                    break;
                }
            }
            else
            {
                hint("Make a disc image (.iso) from your Drakengard disc, then choose it here.");
            }

            sectionTitle(st, "Supported versions");
            if (ImGui::BeginTable("versions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                ImGui::TableSetupColumn("Version");
                ImGui::TableSetupColumn("Serial");
                ImGui::TableSetupColumn("Status");
                ImGui::TableHeadersRow();
                auto line = [](const char *name, const char *serial, const char *status, ImVec4 color)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(serial);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(status);
                    ImGui::PopStyleColor();
                };
                line("Drakengard (USA) v1.00", "SLUS-20732", ICON_FA_CIRCLE_CHECK "  Supported", kGood);
                line("Drakengard (Europe)", "SLES-52322", ICON_FA_CLOCK "  Planned", kWarn);
                line("Drag-On Dragoon (Japan)", "-", ICON_FA_CLOCK "  Planned", kWarn);
                ImGui::EndTable();
            }
            hint("Every release has different program code, so each one is translated and tested "
                 "separately. Europe and Japan come after the USA version.");

            sectionTitle(st, "Game files");
            if (st.gameBuilt())
                statusLine(ICON_FA_CIRCLE_CHECK, kGood, "The game is built and ready to play.");
            else
                statusLine(ICON_FA_TRIANGLE_EXCLAMATION, kWarn,
                           "The game has not been built from your disc yet. Open the Build page.");
            if (beginSettings("game_launcher"))
            {
                row("Close the launcher when the game starts");
                if (ImGui::Checkbox("##closeonplay", &st.settings.closeOnPlay))
                    st.markDirty();
                ImGui::EndTable();
            }
        }

        void pageDisplay(LauncherApp::State &st)
        {
            heading(st, "Display", "How the game window looks. Press F11 in the game to switch to fullscreen and back.");
            sectionTitle(st, "Window");
            if (beginSettings("display"))
            {
                row("Window mode");
                if (choice("##mode", st.settings.windowMode,
                           {{"windowed", "Windowed"}, {"borderless", "Borderless fullscreen"}, {"fullscreen", "Fullscreen"}}))
                    st.markDirty();

                row("Window size", "Used in windowed mode. 4:3 sizes fit the game best.");
                static const std::pair<int, int> sizes[] = {{960, 720}, {1280, 960}, {1440, 1080}, {1600, 1200}, {1920, 1440}};
                char current[32];
                std::snprintf(current, sizeof(current), "%d x %d", st.settings.windowWidth, st.settings.windowHeight);
                if (ImGui::BeginCombo("##size", current))
                {
                    for (const auto &[w, h] : sizes)
                    {
                        char label[32];
                        std::snprintf(label, sizeof(label), "%d x %d", w, h);
                        if (ImGui::Selectable(label, w == st.settings.windowWidth && h == st.settings.windowHeight))
                        {
                            st.settings.windowWidth = w;
                            st.settings.windowHeight = h;
                            st.markDirty();
                        }
                    }
                    ImGui::EndCombo();
                }

                row("Aspect ratio", "The PS2 showed the game on a 4:3 TV.");
                if (choice("##aspect", st.settings.aspect,
                           {{"4:3", "4:3 (like on a TV, recommended)"}, {"pixels", "Original pixels (slightly wide)"},
                            {"stretch", "Stretch to the window"}}))
                    st.markDirty();

                row("Image smoothing");
                if (choice("##filter", st.settings.filter, {{"linear", "Smooth"}, {"nearest", "Sharp pixels"}}))
                    st.markDirty();

                row("Show FPS counter");
                if (ImGui::Checkbox("##fps", &st.settings.showFps))
                    st.markDirty();
                ImGui::EndTable();
            }
            sectionTitle(st, "Coming soon");
            if (beginSettings("display_soon"))
            {
                comingSoonRow("Widescreen (16:9, 21:9)", "A wider view of the world instead of stretching the picture.");
                comingSoonRow("HUD position and size", "Keep the HUD in the corners on wide screens.");
                comingSoonRow("VSync and frame cap", "Match your monitor's refresh rate.");
                ImGui::EndTable();
            }
        }

        void pageGraphics(LauncherApp::State &st)
        {
            heading(st, "Graphics", "How the game is drawn. The graphics card draws the game through Vulkan.");
            sectionTitle(st, "Renderer");
            if (beginSettings("graphics"))
            {
                row("Renderer", "The GPU renderer is fast; the CPU renderer is only for comparison or troubleshooting.");
                if (choice("##renderer", st.settings.renderer,
                           {{"gpu", "GPU - Vulkan (recommended)"}, {"cpu", "CPU - software (slow)"}}))
                    st.markDirty();

                row("Anti-aliasing (supersampling)", "Smoother edges. Each step needs more from your graphics card.");
                std::string ssaa = std::to_string(st.settings.supersampling);
                ImGui::BeginDisabled(st.settings.renderer != "gpu");
                if (choice("##ssaa", ssaa, {{"1", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}}))
                {
                    st.settings.supersampling = std::stoi(ssaa);
                    st.markDirty();
                }
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            sectionTitle(st, "Performance");
            if (beginSettings("perf"))
            {
                row("Vector unit on its own CPU thread", "Recommended. Turn off only to troubleshoot.");
                if (ImGui::Checkbox("##vu1thread", &st.settings.vu1Thread))
                    st.markDirty();
                row("Compiled vector-unit code", "Recommended. Off runs the slow interpreter (for troubleshooting).");
                if (ImGui::Checkbox("##compiledvu", &st.settings.compiledVu))
                    st.markDirty();
                ImGui::EndTable();
            }
            sectionTitle(st, "Coming soon");
            if (beginSettings("graphics_soon"))
            {
                comingSoonRow("Internal resolution (720p - 4K)", "Draw the game at a higher resolution.");
                comingSoonRow("DLSS / FSR upscaling", "Sharper image at high resolutions.");
                comingSoonRow("Higher frame rate (60 / 120 / 144 FPS)", "Smoother motion than the original 30 FPS game.");
                comingSoonRow("Texture filtering", "Cleaner textures at a distance.");
                ImGui::EndTable();
            }
        }

        void pageControls(LauncherApp::State &st)
        {
            heading(st, "Controls", "A gamepad is used automatically when one is connected. The keyboard always works too.");
            sectionTitle(st, "Gamepad");
            if (beginSettings("pad"))
            {
                row("Gamepad");
                std::vector<std::pair<std::string, std::string>> pads = {{"auto", "First connected gamepad"}, {"off", "No gamepad"}};
                for (int i = 0; i < 4; ++i)
                {
                    std::string label = "Gamepad " + std::to_string(i + 1);
                    if (IsGamepadAvailable(i))
                        label += std::string(" - ") + GetGamepadName(i);
                    else
                        label += " (not connected)";
                    pads.emplace_back(std::to_string(i), label);
                }
                if (choice("##pad", st.settings.gamepad, pads))
                    st.markDirty();

                row("Stick dead zone", "Ignores small stick movements (worn sticks drift).");
                int dz = int(st.settings.deadzone * 100.0f + 0.5f);
                if (ImGui::SliderInt("##dz", &dz, 0, 50, "%d%%"))
                {
                    st.settings.deadzone = float(dz) / 100.0f;
                    st.markDirty();
                }
                ImGui::EndTable();
            }
            int connected = 0;
            for (int i = 0; i < 4; ++i)
                connected += IsGamepadAvailable(i) ? 1 : 0;
            if (connected == 0)
                hint("No gamepad detected right now.");
            hint("Gamepad layout: bottom face button = Cross, right = Circle, left = Square, top = Triangle; "
                 "bumpers = L1/R1, triggers = L2/R2, stick clicks = L3/R3, Back/View = Select, Start/Menu = Start.");

            sectionTitle(st, "Keyboard");
            if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                ImGui::TableSetupColumn("PS2 button", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.25f);
                ImGui::TableHeadersRow();
                for (int i = 0; i < int(PadButton::Count); ++i)
                {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(padButtonInfo(PadButton(i)).label);
                    ImGui::TableSetColumnIndex(1);
                    if (st.capturing == i)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
                        ImGui::TextUnformatted("Press a key...");
                        ImGui::PopStyleColor();
                    }
                    else
                    {
                        ImGui::TextUnformatted(keyName(st.settings.keys[i]));
                    }
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::SmallButton(st.capturing == i ? "Cancel" : "Change"))
                        st.capturing = st.capturing == i ? -1 : i;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                    {
                        st.settings.keys[i] = 0;
                        st.markDirty();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (st.capturing >= 0)
            {
                // Escape cancels; any other key is bound (and unbound from other buttons).
                for (int key = 32; key <= 348; ++key)
                {
                    if (!IsKeyPressed(key))
                        continue;
                    if (key != KEY_ESCAPE)
                    {
                        for (int j = 0; j < int(PadButton::Count); ++j)
                            if (st.settings.keys[j] == key)
                                st.settings.keys[j] = 0;
                        st.settings.keys[st.capturing] = key;
                        st.markDirty();
                    }
                    st.capturing = -1;
                    break;
                }
            }
            if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Restore default keys"))
            {
                const Settings defaults;
                for (int i = 0; i < int(PadButton::Count); ++i)
                    st.settings.keys[i] = defaults.keys[i];
                st.markDirty();
            }
            sectionTitle(st, "Coming soon");
            if (beginSettings("controls_soon"))
            {
                comingSoonRow("Gamepad button remapping", "Choose which gamepad button does what.");
                comingSoonRow("Mouse camera", "Turn the camera with the mouse.");
                comingSoonRow("On-screen button prompts", "Xbox / PlayStation / keyboard icons in the game.");
                ImGui::EndTable();
            }
        }

        void pageAudio(LauncherApp::State &st)
        {
            heading(st, "Audio", "Sound does not work in DrakengardPC yet. It is the next big feature.");
            sectionTitle(st, "Coming soon");
            if (beginSettings("audio"))
            {
                comingSoonRow("Master volume", nullptr);
                comingSoonRow("Music volume", nullptr);
                comingSoonRow("Effects volume", nullptr);
                comingSoonRow("Voice volume", nullptr);
                comingSoonRow("Output device", "Choose speakers or headphones.");
                ImGui::EndTable();
            }
        }

        void pageSaves(LauncherApp::State &st)
        {
            heading(st, "Saves", "Your saves live on a virtual memory card: a folder on your PC.");
            const fs::path card = st.settings.memcardDir(st.root);
            sectionTitle(st, "Memory card");
            ImGui::TextWrapped("%s", u8(card).c_str());
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open folder"))
                openFolder(card);
            ImGui::SameLine();
            if (ImGui::Button("Change folder..."))
            {
                const fs::path picked = pickFolder(card);
                if (!picked.empty())
                {
                    st.settings.memcard = picked;
                    st.markDirty();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Use the default folder"))
            {
                st.settings.memcard.clear();
                st.markDirty();
            }
            statusLine(ICON_FA_TRIANGLE_EXCLAMATION, kWarn,
                       "The game finds its memory card, but saving and loading have not been tested yet. "
                       "Make backups before trusting it with a long playthrough.");

            sectionTitle(st, "Backups");
            const fs::path backups = st.root / "save_backups";
            if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Back up the memory card now"))
            {
                std::error_code ec;
                const std::time_t now = std::time(nullptr);
                char stamp[32];
                std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
                const fs::path dst = backups / stamp;
                fs::create_directories(dst, ec);
                if (fs::exists(card, ec))
                    fs::copy(card, dst, fs::copy_options::recursive, ec);
                st.notify(ec ? "Backup failed: " + ec.message() : "Backup saved to save_backups\\" + std::string(stamp));
            }
            ImGui::SameLine();
            if (ImGui::Button("Open backups folder"))
                openFolder(backups);
            std::vector<std::string> names;
            std::error_code ec;
            if (fs::exists(backups, ec))
                for (const auto &e : fs::directory_iterator(backups, ec))
                    if (e.is_directory())
                        names.push_back(u8(e.path().filename()));
            std::sort(names.rbegin(), names.rend());
            if (names.empty())
                hint("No backups yet.");
            for (size_t i = 0; i < names.size() && i < 8; ++i)
                ImGui::BulletText("%s", names[i].c_str());

            sectionTitle(st, "Coming soon");
            if (beginSettings("saves_soon"))
            {
                comingSoonRow("Automatic backups", "A backup every time the game saves.");
                comingSoonRow("Import saves from PCSX2 or a PS2", "Use your existing save files.");
                ImGui::EndTable();
            }
        }

        void pageFeatures(LauncherApp::State &st)
        {
            heading(st, "PC Features", "What DrakengardPC can already do, and what is planned.");
            struct Feature
            {
                const char *name;
                const char *what;
                int state; // 0 done, 1 in progress, 2 planned
            };
            const Feature features[] = {
                {"Native PC code", "The game's program runs directly on your CPU, not in an emulator.", 0},
                {"GPU rendering (Vulkan)", "Your graphics card draws the game.", 0},
                {"Full speed", "60 frames per second display rate in everything tested so far.", 0},
                {"Multi-threaded", "Game logic, vector unit and drawing on separate CPU threads.", 0},
                {"Gamepad and keyboard", "Automatic gamepad, rebindable keyboard.", 0},
                {"Window options", "Windowed, borderless, fullscreen, 4:3 aspect, FPS counter.", 0},
                {"Sound and music", "The next major feature.", 2},
                {"Saving", "Memory card works; saving not tested yet.", 1},
                {"Europe and Japan versions", "Each release needs its own translation and testing.", 2},
                {"Higher resolutions (up to 4K)", "", 2},
                {"Widescreen and ultrawide", "", 2},
                {"High frame rates (120/144 FPS+)", "", 2},
                {"DLSS and FSR", "", 2},
                {"Gamepad remapping, mouse camera", "", 2},
                {"Mods (textures, files)", "", 2},
            };
            if (ImGui::BeginTable("features", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch, 0.72f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                for (const Feature &f : features)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushFont(st.fontBold, 18.0f);
                    ImGui::TextUnformatted(f.name);
                    ImGui::PopFont();
                    if (*f.what)
                        hint(f.what);
                    ImGui::TableSetColumnIndex(1);
                    if (f.state == 0)
                        chip(ICON_FA_CIRCLE_CHECK " Available", kGood);
                    else if (f.state == 1)
                        chip(ICON_FA_CLOCK " Not tested yet", kWarn);
                    else
                        chip("Planned", kMuted);
                }
                ImGui::EndTable();
            }
        }

        void startToolCheck(LauncherApp::State &st)
        {
            st.tools.clear();
            st.toolsChecked = false;
            if (st.python.empty())
            {
                st.toolsChecked = true;
                st.tools["python"] = false;
                return;
            }
            const std::wstring cmd = st.python + L" -I " + quote(st.root / "tools" / "build_game.py") + L" --check";
            st.toolsChecking = st.toolCheck.start(cmd, st.root);
        }

        void pollProcesses(LauncherApp::State &st)
        {
            std::vector<std::string> lines;
            st.toolCheck.takeLines(lines);
            for (const std::string &l : lines)
            {
                char name[32], state[16];
                if (std::sscanf(l.c_str(), "@@tool %31s %15s", name, state) == 2)
                    st.tools[name] = std::string(state) == "ok";
            }
            if (st.toolsChecking && !st.toolCheck.running())
            {
                lines.clear();
                st.toolCheck.takeLines(lines);
                for (const std::string &l : lines)
                {
                    char name[32], state[16];
                    if (std::sscanf(l.c_str(), "@@tool %31s %15s", name, state) == 2)
                        st.tools[name] = std::string(state) == "ok";
                }
                st.toolsChecking = false;
                st.toolsChecked = true;
            }

            lines.clear();
            st.build.takeLines(lines);
            for (const std::string &l : lines)
            {
                int a = 0, b = 0;
                char text[512] = {};
                if (std::sscanf(l.c_str(), "@@step %d/%d %511[^\n]", &a, &b, text) >= 2)
                {
                    st.step = a;
                    st.steps = b;
                    st.stepText = text;
                }
                else if (l.rfind("@@error ", 0) == 0)
                {
                    st.buildError = l.substr(8);
                }
                if (st.log.size() > 4000)
                    st.log.erase(st.log.begin(), st.log.begin() + 1000);
                st.log.push_back(l);
                st.scrollLog = true;
            }
            if (st.buildState == LauncherApp::State::BuildState::Running && !st.build.running())
            {
                lines.clear();
                st.build.takeLines(lines);
                for (const std::string &l : lines)
                    st.log.push_back(l);
                const bool ok = st.build.exitCode() == 0;
                st.buildState = ok ? LauncherApp::State::BuildState::Done : LauncherApp::State::BuildState::Failed;
                if (st.buildTask == "packages")
                {
                    st.notify(ok ? "Build tools installed." : "Installing the build tools failed; see the log.");
                    startToolCheck(st);
                }
                else
                {
                    st.notify(ok ? "The game is built. Press PLAY!" : "The build stopped; see the log.");
                }
            }
        }

        void toolRow(LauncherApp::State &st, const char *key, const char *label, const char *getLabel, const char *url)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            auto it = st.tools.find(key);
            if (!st.toolsChecked)
            {
                ImGui::TextDisabled("checking...");
            }
            else if (it != st.tools.end() && it->second)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kGood);
                ImGui::TextUnformatted(ICON_FA_CIRCLE_CHECK "  Installed");
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kBad);
                ImGui::TextUnformatted(ICON_FA_CIRCLE_XMARK "  Missing");
                ImGui::PopStyleColor();
                if (url)
                {
                    ImGui::SameLine();
                    ImGui::PushID(key);
                    if (ImGui::SmallButton(getLabel))
                        openUrl(url);
                    ImGui::PopID();
                }
            }
        }

        void pageBuild(LauncherApp::State &st)
        {
            heading(st, "Build", "DrakengardPC turns your own disc into the PC game, on your PC. "
                                 "This is needed once (and again after updates). It takes 10-30 minutes.");
            if (!st.toolsChecked && !st.toolsChecking)
                startToolCheck(st);

            sectionTitle(st, "1. Free build tools");
            if (ImGui::BeginTable("tools", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                ImGui::TableSetupColumn("Tool", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                toolRow(st, "python", "Python 3", "Get Python", "https://www.python.org/downloads/windows/");
                toolRow(st, "git", "Git", "Get Git", "https://git-scm.com/download/win");
                toolRow(st, "msys2", "MSYS2", "Get MSYS2", "https://www.msys2.org/");
                toolRow(st, "clang", "Compiler (Clang)", nullptr, nullptr);
                toolRow(st, "cmake", "CMake", nullptr, nullptr);
                toolRow(st, "ninja", "Ninja", nullptr, nullptr);
                toolRow(st, "ffmpeg", "FFmpeg", nullptr, nullptr);
                ImGui::EndTable();
            }
            const bool needPackages = st.toolsChecked && st.tools["msys2"] &&
                                      (!st.tools["clang"] || !st.tools["cmake"] || !st.tools["ninja"] || !st.tools["ffmpeg"]);
            if (st.toolsChecked && st.tools.count("python") && !st.tools["python"])
                hint("Install Python first (tick \"Add python.exe to PATH\" in its installer), then restart the launcher.");
            if (needPackages)
            {
                hint("MSYS2 is installed. The launcher can install the compiler, CMake, Ninja and FFmpeg into it (about 2 GB download).");
                if (accentButton(ICON_FA_HAMMER "  Install the build tools", ImVec2(260, 0),
                                 st.buildState != LauncherApp::State::BuildState::Running))
                {
                    const fs::path msys = msys2Root();
                    const std::wstring cmd = quote(msys / "usr" / "bin" / "bash.exe") +
                                             L" -lc \"pacman -S --needed --noconfirm mingw-w64-clang-x86_64-toolchain "
                                             L"mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja "
                                             L"mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-ffmpeg\"";
                    st.log.clear();
                    st.buildError.clear();
                    st.buildTask = "packages";
                    if (st.build.start(cmd, st.root))
                        st.buildState = LauncherApp::State::BuildState::Running;
                }
            }
            if (ImGui::SmallButton(ICON_FA_ARROWS_ROTATE "  Check again"))
                startToolCheck(st);

            sectionTitle(st, "2. Build the game from your disc");
            const bool discOk = st.discChecked && st.disc.status == disc::DiscStatus::Supported;
            bool allTools = st.toolsChecked && !st.tools.empty();
            for (const auto &[name, ok] : st.tools)
                allTools = allTools && ok;
            if (!discOk)
                statusLine(ICON_FA_TRIANGLE_EXCLAMATION, kWarn, "Choose your Drakengard (USA) disc image on the Game page first.");
            const bool running = st.buildState == LauncherApp::State::BuildState::Running;
            if (!running)
            {
                if (accentButton(st.gameBuilt() ? ICON_FA_HAMMER "  Rebuild the game" : ICON_FA_HAMMER "  Build the game",
                                 ImVec2(260, 40), discOk && allTools && !st.python.empty()))
                {
                    const std::wstring cmd = st.python + L" -I -u " + quote(st.root / "tools" / "build_game.py") +
                                             L" --iso " + quote(st.settings.resolve(st.root, st.settings.iso));
                    st.log.clear();
                    st.buildError.clear();
                    st.step = 0;
                    st.stepText = "Starting";
                    st.buildTask = "build";
                    st.saveIfDirty();
                    if (st.build.start(cmd, st.root))
                        st.buildState = LauncherApp::State::BuildState::Running;
                    else
                        st.buildError = "Could not start Python.";
                }
            }
            else
            {
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    st.build.kill();
            }
            if (running && st.buildTask == "build")
            {
                char overlay[600];
                std::snprintf(overlay, sizeof(overlay), "Step %d of %d: %s", st.step, st.steps, st.stepText.c_str());
                ImGui::ProgressBar(st.steps ? float(std::max(0, st.step - 1)) / float(st.steps) : 0.0f, ImVec2(-FLT_MIN, 0), overlay);
            }
            if (st.buildState == LauncherApp::State::BuildState::Done && st.buildTask == "build")
                statusLine(ICON_FA_CIRCLE_CHECK, kGood, "Build finished. Press PLAY.");
            if (!st.buildError.empty())
                statusLine(ICON_FA_CIRCLE_XMARK, kBad, st.buildError);

            if (!st.log.empty())
            {
                sectionTitle(st, "Log");
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.03f, 0.035f, 1.0f));
                ImGui::BeginChild("log", ImVec2(0, 220), ImGuiChildFlags_Borders);
                for (const std::string &l : st.log)
                    ImGui::TextUnformatted(l.c_str());
                if (st.scrollLog)
                {
                    ImGui::SetScrollHereY(1.0f);
                    st.scrollLog = false;
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }

        void pageAbout(LauncherApp::State &st)
        {
            heading(st, "About DrakengardPC", nullptr);
            ImGui::TextWrapped("An unofficial fan project that turns Drakengard (PlayStation 2, 2003) into a native "
                               "Windows PC game. The game's code is translated once into PC code and compiled, so your "
                               "CPU runs it directly and your graphics card draws it.");
            ImGui::Spacing();
            ImGui::Text("Version %s", kVersion);
            sectionTitle(st, "Credits");
            ImGui::BulletText("PS2Recomp by ran-j and contributors - the recompiler this port is built on");
            ImGui::BulletText("paraLLEl-GS by Arntzen Software - the GPU graphics renderer");
            ImGui::BulletText("Drakengard1Recompile by k1gs - a related project whose fixes helped");
            ImGui::BulletText("PCSX2 - the reference for how the PS2 behaves");
            ImGui::BulletText("Dear ImGui, raylib, Font Awesome - this launcher");
            sectionTitle(st, "Legal");
            ImGui::TextWrapped("DrakengardPC is free software under the GNU General Public License v3.0. "
                               "Drakengard is (c) Square Enix. This project is not made, approved or supported by "
                               "Square Enix or Cavia, and it contains no part of the game. You need your own copy.");
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_CIRCLE_INFO "  Project page"))
                openUrl(kRepoUrl);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "  DrakengardPC folder"))
                openFolder(st.root);
            ImGui::SameLine();
            if (ImGui::Button("Settings file"))
                openFolder(st.iniPath.parent_path());
        }
    }

    // ------------------------------------------------------------------ setup

    namespace
    {
        bool gitInstalled()
        {
            const char *path = std::getenv("PATH");
            std::string all = path ? path : "";
            size_t start = 0;
            while (start <= all.size())
            {
                const size_t end = all.find(';', start);
                const std::string dir = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
                std::error_code ec;
                if (!dir.empty() && fs::exists(fs::u8path(dir) / "git.exe", ec))
                    return true;
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return false;
        }

        // Returns true when the download finished successfully.
        bool pageSetup(LauncherApp::State &st)
        {
            heading(st, "Welcome to DrakengardPC",
                    "The launcher needs the DrakengardPC files (the recompiler and the port's source code) next to "
                    "it. They are downloaded once; your game disc is used later, on your own PC.");
            sectionTitle(st, "Download DrakengardPC");
            ImGui::TextWrapped("Destination: %s", u8(st.root).c_str());
            const bool git = gitInstalled();
            if (!git)
            {
                statusLine(ICON_FA_CIRCLE_XMARK, kBad, "Git is needed to download DrakengardPC.");
                if (ImGui::Button("Get Git"))
                    openUrl("https://git-scm.com/download/win");
                hint("Install Git with the default options, then restart the launcher.");
            }
            if (!st.downloading)
            {
                if (accentButton(ICON_FA_FOLDER_OPEN "  Download DrakengardPC", ImVec2(300, 44), git))
                {
                    const std::wstring cmd = L"git clone --recursive --progress " +
                                             std::wstring(kRepoUrl, kRepoUrl + std::strlen(kRepoUrl)) + L".git " +
                                             quote(st.root);
                    st.log.clear();
                    st.buildError.clear();
                    st.downloading = st.download.start(cmd, st.root.parent_path());
                    if (!st.downloading)
                        st.buildError = "Could not start git.";
                }
            }
            else
            {
                ImGui::ProgressBar(-1.0f * float(ImGui::GetTime()), ImVec2(-FLT_MIN, 0), "Downloading...");
            }
            bool finished = false;
            std::vector<std::string> lines;
            st.download.takeLines(lines);
            for (std::string &l : lines)
            {
                st.log.push_back(std::move(l));
                st.scrollLog = true;
            }
            if (st.downloading && !st.download.running())
            {
                st.downloading = false;
                if (st.download.exitCode() == 0)
                    finished = true;
                else
                    st.buildError = "The download failed; see the log below.";
            }
            if (!st.buildError.empty())
                statusLine(ICON_FA_CIRCLE_XMARK, kBad, st.buildError);
            if (!st.log.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.03f, 0.035f, 1.0f));
                ImGui::BeginChild("dllog", ImVec2(0, 240), ImGuiChildFlags_Borders);
                for (const std::string &l : st.log)
                    ImGui::TextUnformatted(l.c_str());
                if (st.scrollLog)
                {
                    ImGui::SetScrollHereY(1.0f);
                    st.scrollLog = false;
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            return finished;
        }
    }

    // ------------------------------------------------------------------ frame

    bool LauncherApp::frame()
    {
        State &st = *m;
        pollProcesses(st);

        const ImVec2 screen{float(GetScreenWidth()), float(GetScreenHeight())};
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(screen);
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoSavedSettings);

        // Header
        const float headerH = 92.0f;
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x, headerH), ImGui::GetColorU32(ImVec4(0.20f, 0.04f, 0.06f, 1)),
                                        ImGui::GetColorU32(kBg), ImGui::GetColorU32(kBg),
                                        ImGui::GetColorU32(ImVec4(0.20f, 0.04f, 0.06f, 1)));
            dl->AddLine(ImVec2(0, headerH), ImVec2(screen.x, headerH), ImGui::GetColorU32(kAccent), 2.0f);
            ImGui::SetCursorPos(ImVec2(28, 14));
            ImGui::PushFont(st.fontBold, 40.0f);
            ImGui::TextUnformatted("DRAKENGARD");
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::SetCursorPosY(31);
            ImGui::PushFont(st.fontBold, 22.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, kAccentHi);
            ImGui::TextUnformatted("PC");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SetCursorPos(ImVec2(30, 62));
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::Text("Unofficial native PC port  -  version %s", kVersion);
            ImGui::PopStyleColor();
        }

        if (st.sourceMissing)
        {
            ImGui::SetCursorPos(ImVec2(0, headerH + 2));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40, 26));
            ImGui::BeginChild("setup", ImVec2(screen.x, screen.y - headerH - 2), ImGuiChildFlags_AlwaysUseWindowPadding);
            const bool done = pageSetup(st);
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::End();
            if (done)
            {
                reload();
                m->page = Page::Game;
                m->notify("DrakengardPC downloaded. Choose your disc image.");
            }
            return true;
        }

        // Sidebar
        const float footerH = 78.0f;
        const float sideW = 220.0f;
        ImGui::SetCursorPos(ImVec2(0, headerH + 2));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
        ImGui::BeginChild("side", ImVec2(sideW, screen.y - headerH - footerH - 2), ImGuiChildFlags_None);
        ImGui::Dummy(ImVec2(0, 10));
        for (int i = 0; i < int(Page::Count); ++i)
        {
            const bool active = int(st.page) == i;
            ImGui::SetCursorPosX(12);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.30f, 0.09f, 0.12f, 1.0f) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImVec4(0.36f, 0.11f, 0.14f, 1.0f) : kPanelHi);
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            char label[96];
            std::snprintf(label, sizeof(label), "  %s   %s", kPages[i].icon, kPages[i].name);
            if (ImGui::Button(label, ImVec2(sideW - 24, 40)))
            {
                st.page = Page(i);
                st.capturing = -1;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Page
        ImGui::SetCursorPos(ImVec2(sideW, headerH + 2));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 22));
        ImGui::BeginChild("page", ImVec2(screen.x - sideW, screen.y - headerH - footerH - 2), ImGuiChildFlags_AlwaysUseWindowPadding);
        switch (st.page)
        {
        case Page::Game: pageGame(st); break;
        case Page::Display: pageDisplay(st); break;
        case Page::Graphics: pageGraphics(st); break;
        case Page::Controls: pageControls(st); break;
        case Page::Audio: pageAudio(st); break;
        case Page::Saves: pageSaves(st); break;
        case Page::Enhancements: pageFeatures(st); break;
        case Page::Build: pageBuild(st); break;
        case Page::About: pageAbout(st); break;
        default: break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Footer: status and PLAY
        bool quit = false;
        {
            const float y = screen.y - footerH;
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(0, y), ImVec2(screen.x, screen.y), ImGui::GetColorU32(kPanel));
            dl->AddLine(ImVec2(0, y), ImVec2(screen.x, y), ImGui::GetColorU32(ImVec4(0.22f, 0.20f, 0.23f, 1)), 1.0f);
            ImGui::SetCursorPos(ImVec2(28, y + 16));
            ImGui::BeginGroup();
            if (!st.discChecked)
                statusLine(ICON_FA_COMPACT_DISC, kMuted, "No disc image chosen yet.");
            else if (st.disc.status != disc::DiscStatus::Supported)
                statusLine(ICON_FA_TRIANGLE_EXCLAMATION, kWarn, st.disc.message);
            else if (st.buildState == State::BuildState::Running)
                statusLine(ICON_FA_HAMMER, kGold, st.buildTask == "packages" ? "Installing the build tools..." : "Building the game...");
            else if (!st.gameBuilt())
                statusLine(ICON_FA_HAMMER, kWarn, "Ready to build: open the Build page.");
            else
                statusLine(ICON_FA_CIRCLE_CHECK, kGood, "Drakengard (USA) - ready to play.");
            if (!st.toast.empty() && GetTime() < st.toastUntil)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kGold);
                ImGui::TextUnformatted(st.toast.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();

            ImGui::SetCursorPos(ImVec2(screen.x - 250, y + 15));
            ImGui::PushFont(st.fontBold, 24.0f);
            if (accentButton(ICON_FA_PLAY "   PLAY", ImVec2(220, 48), st.canPlay()))
            {
                st.saveIfDirty();
                if (launchDetached(st.gameExe, L"--settings " + quote(st.iniPath), st.root))
                {
                    st.notify("Starting Drakengard...");
                    if (st.settings.closeOnPlay)
                        quit = true;
                }
                else
                {
                    st.notify("The game could not be started.");
                }
            }
            ImGui::PopFont();
        }

        ImGui::End();

        // Settings are saved shortly after a change, so the game always sees the latest ones.
        static double lastSave = 0.0;
        if (st.dirty && GetTime() - lastSave > 0.5)
        {
            st.saveIfDirty();
            lastSave = GetTime();
        }
        return !quit;
    }

    void LauncherApp::setPage(int page)
    {
        if (page >= 0 && page < int(Page::Count))
            m->page = Page(page);
    }

    void LauncherApp::shutdown()
    {
        m->saveIfDirty();
    }
}
