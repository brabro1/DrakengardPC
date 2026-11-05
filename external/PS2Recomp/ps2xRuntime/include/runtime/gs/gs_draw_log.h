#pragma once

#include <atomic>
#include <cstdint>

struct GSPrimitiveBatch;

// Diagnostic GS draw log (software backend). Configure with
//   PS2X_GS_DRAW_LOG=<firstTick>:<lastTick>:<path>   one record per primitive, with per-draw
//                                                    pixel-pipeline counters
//   PS2X_GS_PROBE=<x>,<y>                            also log every fragment at framebuffer
//                                                    pixel (x, y) (window coordinates)
// The VSync tick is published by the EE scheduler; logging costs nothing while inactive.
namespace ps2x::gsdebug
{
    inline std::atomic<uint64_t> g_vsyncTick{0};

    // Set by the software backend while a draw log is configured; the frontend reports
    // complete primitives whose last vertex had ADC set (never drawn).
    inline void (*g_adcSkipHook)(const GSPrimitiveBatch &) = nullptr;

    inline void publishVSyncTick(uint64_t tick)
    {
        g_vsyncTick.store(tick, std::memory_order_relaxed);
    }
}
