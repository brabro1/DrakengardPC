#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

// Host time spent blocked at each wait site (microseconds), reported by [run:sample].
namespace ps2x::waitstats
{
    enum Site : uint32_t
    {
        Pacing = 0, // EE scheduler: event reached before its wall-clock deadline
        Idle,       // EE scheduler: no runnable guest thread
        GsDrain,    // GS backend: caller waits for the GS worker queue to empty
        Count
    };
    inline std::atomic<uint64_t> g_waitMicros[Count]{};
    // EE dispatcher transfers (C++ exception unwinds), by site: 0 exit 1 reschedule 2 invoke 3 sequence 4 block.
    inline std::atomic<uint64_t> g_transfers[5]{};

    struct Scope
    {
        explicit Scope(Site site) : s(site), start(std::chrono::steady_clock::now()) {}
        ~Scope()
        {
            g_waitMicros[s].fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count()),
                                      std::memory_order_relaxed);
        }
        Site s;
        std::chrono::steady_clock::time_point start;
    };
}
