#pragma once

// Small Windows helpers kept out of files that include raylib (windows.h and raylib.h clash).

#include <filesystem>
#include <string>

namespace dkg::host
{
    std::filesystem::path exeDirectory();
    // Message box (UTF-8 text); also printed to stderr by the caller if wanted.
    void showMessage(const std::string &title, const std::string &text, bool warning);
    // Without a console or redirected output, send stdout/stderr to the given log file.
    void redirectLogsIfDetached(const std::filesystem::path &logFile);
    void setEnv(const std::string &name, const std::string &value);
}
