// DrakengardPC Launcher entry point: a raylib window running the Dear ImGui launcher UI.

#include "launcher/launcher_ui.h"
#include "platform/host_util.h"

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include <cstdlib>

int main()
{
    const auto root = dkg::launcher::findProjectRoot(dkg::host::exeDirectory());

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1180, 760, "DrakengardPC Launcher");
    SetWindowMinSize(940, 640);
    SetExitKey(KEY_NULL); // Escape must not close the launcher (used to cancel key capture)
    SetTargetFPS(60);

    dkg::launcher::LauncherApp app(root);
    rlImGuiBeginInitImGui();
    app.initFonts();
    rlImGuiEndInitImGui();
    ImGui::GetIO().IniFilename = nullptr; // no imgui.ini next to the exe

    // Test mode: DKG_LAUNCHER_SCREENSHOT=<png> [DKG_LAUNCHER_PAGE=<n>] saves one frame and exits.
    const char *shot = std::getenv("DKG_LAUNCHER_SCREENSHOT");
    if (const char *page = std::getenv("DKG_LAUNCHER_PAGE"))
        app.setPage(std::atoi(page));
    int frames = 0;

    bool running = true;
    while (running && !WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(Color{14, 13, 16, 255});
        rlImGuiBegin();
        running = app.frame();
        rlImGuiEnd();
        EndDrawing();
        if (shot && (app.busy() ? (frames = 0, false) : ++frames == 90))
        {
            Image image = LoadImageFromScreen();
            ExportImage(image, shot);
            UnloadImage(image);
            break;
        }
    }
    app.shutdown();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
