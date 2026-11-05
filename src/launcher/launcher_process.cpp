#include "launcher/launcher_process.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace dkg::launcher
{
    fs::path pickIsoFile(const fs::path &start)
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        std::wstring initialDir;
        if (!start.empty())
            initialDir = (fs::is_directory(start) ? start : start.parent_path()).wstring();
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = L"PS2 disc images (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = DWORD(std::size(buffer));
        ofn.lpstrTitle = L"Choose your Drakengard (USA) disc image";
        ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn))
            return {};
        return fs::path(buffer);
    }

    fs::path pickFolder(const fs::path &start)
    {
        (void)start;
        BROWSEINFOW bi{};
        bi.lpszTitle = L"Choose the memory card folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl)
            return {};
        wchar_t buffer[MAX_PATH * 4] = {};
        const bool ok = SHGetPathFromIDListW(pidl, buffer) != FALSE;
        CoTaskMemFree(pidl);
        return ok ? fs::path(buffer) : fs::path();
    }

    void openUrl(const std::string &url)
    {
        const std::wstring w(url.begin(), url.end());
        ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void openFolder(const fs::path &folder)
    {
        std::error_code ec;
        fs::create_directories(folder, ec);
        ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    std::wstring quote(const fs::path &p) { return L"\"" + p.wstring() + L"\""; }

    bool launchDetached(const fs::path &exe, const std::wstring &arguments, const fs::path &workingDir)
    {
        std::wstring commandLine = quote(exe) + L" " + arguments;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            workingDir.wstring().c_str(), &si, &pi))
            return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    ChildProcess::~ChildProcess()
    {
        kill();
        if (m_reader.joinable())
            m_reader.join();
        if (m_process)
            CloseHandle(static_cast<HANDLE>(m_process));
    }

    bool ChildProcess::start(const std::wstring &commandLine, const fs::path &workingDir)
    {
        if (m_reader.joinable())
            m_reader.join();
        if (m_process)
        {
            CloseHandle(static_cast<HANDLE>(m_process));
            m_process = nullptr;
        }
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
            return false;
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = commandLine;
        const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                       workingDir.wstring().c_str(), &si, &pi);
        CloseHandle(writePipe);
        if (!ok)
        {
            CloseHandle(readPipe);
            return false;
        }
        CloseHandle(pi.hThread);
        m_process = pi.hProcess;
        m_readPipe = readPipe;
        m_exitCode = 0;
        m_running = true;
        m_reader = std::thread([this] { readerLoop(); });
        return true;
    }

    void ChildProcess::readerLoop()
    {
        std::string partial;
        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(static_cast<HANDLE>(m_readPipe), buffer, sizeof(buffer), &got, nullptr) && got > 0)
        {
            partial.append(buffer, got);
            size_t nl;
            while ((nl = partial.find_first_of("\r\n")) != std::string::npos)
            {
                std::string line = partial.substr(0, nl);
                partial.erase(0, nl + 1);
                if (line.empty())
                    continue;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lines.push_back(std::move(line));
            }
        }
        if (!partial.empty())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lines.push_back(partial);
        }
        CloseHandle(static_cast<HANDLE>(m_readPipe));
        m_readPipe = nullptr;
        WaitForSingleObject(static_cast<HANDLE>(m_process), INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);
        m_exitCode = int(code);
        m_running = false;
    }

    void ChildProcess::takeLines(std::vector<std::string> &out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::string &l : m_lines)
            out.push_back(std::move(l));
        m_lines.clear();
    }

    void ChildProcess::kill()
    {
        // The build runs cmake/ninja/clang below Python: end the whole tree.
        if (m_running && m_process)
        {
            const DWORD pid = GetProcessId(static_cast<HANDLE>(m_process));
            std::wstring cmd = L"taskkill /T /F /PID " + std::to_wstring(pid);
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            {
                WaitForSingleObject(pi.hProcess, 10000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }

    std::wstring pythonCommand()
    {
        wchar_t found[MAX_PATH];
        if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, found, nullptr))
        {
            // The Microsoft Store alias (WindowsApps) opens the Store instead of running Python.
            if (std::wstring(found).find(L"WindowsApps") == std::wstring::npos)
                return quote(found);
        }
        if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, found, nullptr))
            return quote(found) + L" -3";
        return {};
    }

    fs::path msys2Root()
    {
        for (wchar_t drive = L'C'; drive <= L'G'; ++drive)
        {
            const fs::path root = std::wstring(1, drive) + L":\\msys64";
            std::error_code ec;
            if (fs::exists(root / "usr" / "bin" / "bash.exe", ec))
                return root;
        }
        return {};
    }
}
