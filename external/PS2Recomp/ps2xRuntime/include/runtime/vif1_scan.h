#pragma once

#include <atomic>
#include <cstdint>

// EE-side scan of VIF1/GIF DMA data queued for the VIF1 worker (PS2Memory::startVif1Worker).
//
// When VIF1 ran on the EE thread, a GS FINISH carried in the data was latched in CSR during the
// DMA kick, so the game's GS interrupt handler ran right after the kick. Drakengard depends on
// that: delivering FINISH when the worker reaches it (later, in guest time) costs it ~18% of its
// frames. The EE therefore scans what it queues, with the same parsing rules as
// PS2Memory::processVIF1Data and the GS frontend, latches each FINISH at kick time and leaves a
// credit; the frontend consumes the credit instead of latching the same FINISH again.
namespace ps2x::vif1scan
{
    struct State
    {
        uint32_t cycle = 0;           // VIF1 CYCLE (STCYCL): UNPACK source sizes depend on CL/WL
        uint32_t pendingImageQwc = 0; // raw PATH2 IMAGE data still expected after a DIRECT
    };

    struct Result
    {
        uint32_t finishes = 0;  // FINISH writes in PATH2 (VIF1 DIRECT) packets
        bool needsSync = false; // SIGNAL/LABEL, or FINISH on PATH3 (MSKPATH3 can delay it)
    };

    // VIF1 data exactly as processVIF1Data parses it (truncated commands end the chunk).
    void scanVif1(State &state, const uint8_t *data, uint32_t sizeBytes, Result &out);
    // A GIF PATH3 packet.
    void scanGifPath3(const uint8_t *data, uint32_t sizeBytes, Result &out);
    // The EE's copy of the state above; resynchronised from the real VIF1 state whenever the EE
    // touches VIF1 registers (the worker is idle then).
    State &eeState();

    // FINISH events already latched in CSR by the EE (see above).
    inline std::atomic<uint32_t> g_finishCredits{0};

    inline bool consumeFinishCredit()
    {
        uint32_t credits = g_finishCredits.load(std::memory_order_acquire);
        while (credits != 0u)
        {
            if (g_finishCredits.compare_exchange_weak(credits, credits - 1u, std::memory_order_acq_rel))
                return true;
        }
        return false;
    }
}
