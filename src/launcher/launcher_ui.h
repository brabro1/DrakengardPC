#pragma once

#include <filesystem>
#include <memory>

namespace dkg::launcher
{
    class LauncherApp
    {
    public:
        explicit LauncherApp(const std::filesystem::path &root);
        ~LauncherApp();
        void initFonts();
        // Draws one frame of the UI (between rlImGuiBegin/End). Returns false to quit.
        bool frame();
        void shutdown();
        void setPage(int page); // for screenshots in tests
        void reload(); // re-reads the DrakengardPC folder and its settings

        struct State; // public so the page functions in launcher_ui.cpp can use it

    private:
        std::unique_ptr<State> m;
    };

    // The DrakengardPC folder: the nearest folder at or above `start` that contains
    // tools/build_game.py; otherwise `start`/DrakengardPC, where the Setup page downloads it.
    std::filesystem::path findProjectRoot(const std::filesystem::path &start);
}
