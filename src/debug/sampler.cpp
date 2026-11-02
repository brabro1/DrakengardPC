// Sampling profiler for host-side performance work (Windows).
//
//   DKG_PROFILE=<file>       enable; histogram written at exit
//   DKG_PROFILE_US=1000      sampling interval in microseconds (default 1000)
//
// A background thread suspends every other thread of the process, records its
// instruction pointer and resumes it. Nothing is allocated while a thread is
// suspended (it may hold the heap lock). Samples are kept per thread; addresses
// inside DrakengardPC.exe are written as image offsets (RVAs), which
// tools/profile_report.py rebases and resolves with llvm-symbolizer. Other
// modules are reported by name. (The loader rewrites the in-memory ImageBase on
// relocation, so the preferred base must come from the file, not the header.)

#include "debug/sampler.h"

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dkg::debug
{
#if defined(_WIN32)
    namespace
    {
        std::atomic<bool> g_running{false};
        std::thread g_thread;
        std::mutex g_mutex;
        std::map<DWORD, std::unordered_map<uint64_t, uint32_t>> g_hits; // thread id -> rip -> samples
        uint64_t g_totalSamples = 0;
        std::string g_path;

        void sampleLoop(unsigned intervalUs)
        {
            const DWORD self = GetCurrentThreadId();
            const DWORD pid = GetCurrentProcessId();
            std::vector<std::pair<DWORD, uint64_t>> rips;
            std::vector<std::pair<DWORD, HANDLE>> threads;
            auto lastRefresh = std::chrono::steady_clock::time_point{};
            while (g_running.load(std::memory_order_acquire))
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastRefresh > std::chrono::seconds(1))
                {
                    for (auto &entry : threads)
                        CloseHandle(entry.second);
                    threads.clear();
                    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                    THREADENTRY32 te{};
                    te.dwSize = sizeof(te);
                    if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te))
                    {
                        do
                        {
                            if (te.th32OwnerProcessID == pid && te.th32ThreadID != self)
                            {
                                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
                                if (h)
                                    threads.emplace_back(te.th32ThreadID, h);
                            }
                        } while (Thread32Next(snap, &te));
                    }
                    if (snap != INVALID_HANDLE_VALUE)
                        CloseHandle(snap);
                    lastRefresh = now;
                }

                rips.clear();
                for (auto &[tid, h] : threads)
                {
                    if (SuspendThread(h) == static_cast<DWORD>(-1))
                        continue;
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_CONTROL;
                    const bool ok = GetThreadContext(h, &ctx) != 0;
                    ResumeThread(h);
                    if (ok)
                        rips.emplace_back(tid, ctx.Rip);
                }
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    for (const auto &[tid, rip] : rips)
                        ++g_hits[tid][rip];
                    g_totalSamples += rips.size();
                }
                std::this_thread::sleep_for(std::chrono::microseconds(intervalUs));
            }
            for (auto &entry : threads)
                CloseHandle(entry.second);
        }
    }

    void startSampler()
    {
        const char *path = std::getenv("DKG_PROFILE");
        if (!path || !*path)
            return;
        const char *interval = std::getenv("DKG_PROFILE_US");
        const unsigned us = interval ? static_cast<unsigned>(std::strtoul(interval, nullptr, 10)) : 1000u;
        g_path = path;
        g_running.store(true, std::memory_order_release);
        g_thread = std::thread(sampleLoop, us ? us : 1000u);
        std::fprintf(stderr, "[profile] sampling every %u us -> %s\n", us, path);
    }

    void stopSamplerAndWrite()
    {
        if (!g_running.exchange(false))
            return;
        if (g_thread.joinable())
            g_thread.join();

        const HMODULE exe = GetModuleHandleW(nullptr);
        const uint64_t actualBase = reinterpret_cast<uint64_t>(exe);
        FILE *out = std::fopen(g_path.c_str(), "w");
        if (!out)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::fprintf(out, "# total_samples %llu\n", static_cast<unsigned long long>(g_totalSamples));
        for (const auto &[tid, hits] : g_hits)
        {
            std::map<std::string, uint64_t> otherModules;
            for (const auto &[rip, count] : hits)
            {
                HMODULE module = nullptr;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(rip), &module) &&
                    module == exe)
                {
                    std::fprintf(out, "t %lu exe 0x%llx %u\n", static_cast<unsigned long>(tid),
                                 static_cast<unsigned long long>(rip - actualBase), count);
                    continue;
                }
                char name[MAX_PATH] = "?";
                if (module)
                    GetModuleBaseNameA(GetCurrentProcess(), module, name, sizeof(name));
                otherModules[name] += count;
                if (module)
                    std::fprintf(out, "t %lu modrva %s 0x%llx %u\n", static_cast<unsigned long>(tid), name,
                                 static_cast<unsigned long long>(rip - reinterpret_cast<uint64_t>(module)), count);
            }
            for (const auto &[name, count] : otherModules)
                std::fprintf(out, "t %lu module %s %llu\n", static_cast<unsigned long>(tid), name.c_str(),
                             static_cast<unsigned long long>(count));
        }
        std::fclose(out);
        std::fprintf(stderr, "[profile] wrote %llu samples to %s\n", static_cast<unsigned long long>(g_totalSamples),
                     g_path.c_str());
    }
#else
    void startSampler() {}
    void stopSamplerAndWrite() {}
#endif
}
