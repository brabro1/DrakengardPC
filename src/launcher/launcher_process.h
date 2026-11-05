#pragma once

// Windows services for the launcher (kept apart from raylib/ImGui code: windows.h clashes).

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dkg::launcher
{
    // File/folder pickers. Return an empty path when cancelled.
    std::filesystem::path pickIsoFile(const std::filesystem::path &start);
    std::filesystem::path pickFolder(const std::filesystem::path &start);

    void openUrl(const std::string &url);
    void openFolder(const std::filesystem::path &folder);

    // Starts a detached program (the game). Returns false if it could not start.
    bool launchDetached(const std::filesystem::path &exe, const std::wstring &arguments,
                        const std::filesystem::path &workingDir);

    // A child process whose stdout+stderr are collected line by line (no console window).
    class ChildProcess
    {
    public:
        ChildProcess() = default;
        ~ChildProcess();
        ChildProcess(const ChildProcess &) = delete;
        ChildProcess &operator=(const ChildProcess &) = delete;

        bool start(const std::wstring &commandLine, const std::filesystem::path &workingDir);
        bool running() const { return m_running.load(); }
        int exitCode() const { return m_exitCode.load(); }
        // Moves lines received since the last call into `out`.
        void takeLines(std::vector<std::string> &out);
        void kill();

    private:
        void readerLoop();

        void *m_process = nullptr;
        void *m_readPipe = nullptr;
        std::thread m_reader;
        std::mutex m_mutex;
        std::vector<std::string> m_lines;
        std::atomic<bool> m_running{false};
        std::atomic<int> m_exitCode{0};
    };

    // "python" from PATH, else the py launcher; empty when Python is not installed.
    std::wstring pythonCommand();
    // C:\msys64 (or another drive) when MSYS2 is installed.
    std::filesystem::path msys2Root();
    std::wstring quote(const std::filesystem::path &p);
}
