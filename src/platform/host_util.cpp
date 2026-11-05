#include "platform/host_util.h"

#include <cstdio>
#include <cstdlib>
#include <iterator>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace dkg::host
{
    fs::path exeDirectory()
    {
#if defined(_WIN32)
        wchar_t buffer[MAX_PATH * 4];
        const DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
        if (n > 0 && n < std::size(buffer))
            return fs::path(buffer).parent_path();
#endif
        return fs::current_path();
    }

    void showMessage(const std::string &title, const std::string &text, bool warning)
    {
#if defined(_WIN32)
        const std::wstring wTitle = fs::path(std::u8string(title.begin(), title.end())).wstring();
        const std::wstring wText = fs::path(std::u8string(text.begin(), text.end())).wstring();
        MessageBoxW(nullptr, wText.c_str(), wTitle.c_str(), MB_OK | (warning ? MB_ICONWARNING : MB_ICONINFORMATION));
#else
        (void)title;
        (void)text;
        (void)warning;
#endif
    }

    void redirectLogsIfDetached(const fs::path &logFile)
    {
#if defined(_WIN32)
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != nullptr && out != INVALID_HANDLE_VALUE)
            return;
        std::error_code ec;
        fs::create_directories(logFile.parent_path(), ec);
        if (FILE *f = _wfopen(logFile.wstring().c_str(), L"w")) // truncate once, then both append
            std::fclose(f);
        _wfreopen(logFile.wstring().c_str(), L"a", stdout);
        _wfreopen(logFile.wstring().c_str(), L"a", stderr);
        std::setvbuf(stdout, nullptr, _IOLBF, 4096);
#else
        (void)logFile;
#endif
    }

    void setEnv(const std::string &name, const std::string &value)
    {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }
}
