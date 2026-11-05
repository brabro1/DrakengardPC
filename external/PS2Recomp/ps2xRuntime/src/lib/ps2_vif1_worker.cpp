// VIF1/GIF worker thread (an analogue of PCSX2's MTVU, see runtime/ps2_memory.h).
//
// On the PS2 the EE, the VIF1/VU1 pair and the GIF run in parallel. The runtime used to process
// every VIF1 DMA transfer (VIF commands, VU1 microprograms, XGKICK) synchronously on the EE
// thread, which made VU1 a large share of the EE thread's time. Here those transfers, and GIF
// PATH3 transfers (so the order between GIF paths is unchanged), are copied at DMA kick time and
// processed on one worker thread in kick order. DMA completion is still reported to the EE at
// kick time, as before: from the EE's point of view VU1 work was already "instant".
//
// Ownership: VIF1 registers, VU1 memory and VU1 state, the GIF arbiter and the PATH3 mask
// belong to the worker while it runs. The EE waits for it (syncVif1Worker) before reading or
// writing any of them: VIF1/GIF registers and FIFOs, VU1 memory (EE loads/stores, DMA from VU
// memory), GS readback and EE-side GS register writes.
//
// PS2X_VU1_THREAD=0 keeps the synchronous behaviour.

#include "runtime/ps2_memory.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/wait_stats.h"
#include "runtime/vif1_scan.h"

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

struct PS2Memory::Vif1Worker
{
    explicit Vif1Worker(PS2Memory &memory) : mem(memory) {}

    struct Header
    {
        uint32_t kind;
        uint32_t size;
    };

    PS2Memory &mem;
    std::mutex mutex;
    std::condition_variable workCv;
    std::condition_variable idleCv;
    std::vector<uint8_t> pending; // records: Header, then `size` bytes padded to 8
    bool busy = false;
    bool stop = false;
    bool sleeping = false;
    std::thread thread;
    std::thread::id threadId;

    void push(uint32_t kind, const uint8_t *data, uint32_t size)
    {
        const uint32_t padded = (size + 7u) & ~7u;
        bool wake;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const size_t at = pending.size();
            pending.resize(at + sizeof(Header) + padded);
            const Header header{kind, size};
            std::memcpy(pending.data() + at, &header, sizeof(header));
            if (size != 0u)
                std::memcpy(pending.data() + at + sizeof(header), data, size);
            wake = sleeping;
        }
        if (wake)
            workCv.notify_one();
    }

    void sync()
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (pending.empty() && !busy)
            return;
        ps2x::waitstats::Scope wait(ps2x::waitstats::Vif1Sync);
        idleCv.wait(lock, [&] { return pending.empty() && !busy; });
    }

    void loop()
    {
#if defined(_WIN32)
        SetThreadDescription(GetCurrentThread(), L"VIF1/VU1 worker");
#endif
        std::vector<uint8_t> batch;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(mutex);
                busy = false;
                if (pending.empty())
                    idleCv.notify_all();
                sleeping = true;
                workCv.wait(lock, [&] { return stop || !pending.empty(); });
                sleeping = false;
                if (stop && pending.empty())
                    return;
                batch.swap(pending);
                busy = true;
            }
            size_t at = 0;
            while (at + sizeof(Header) <= batch.size())
            {
                Header header;
                std::memcpy(&header, batch.data() + at, sizeof(header));
                const uint8_t *payload = batch.data() + at + sizeof(header);
                at += sizeof(header) + ((header.size + 7u) & ~7u);
                try
                {
                    mem.runVif1Work(header.kind, payload, header.size);
                }
                catch (const std::exception &e)
                {
                    // On the EE thread this would have reached the dispatcher; here it must not
                    // terminate the process. Logged (rate-limited) and the record is dropped.
                    static unsigned s_reported = 0;
                    if (s_reported++ < 16u)
                        std::cerr << "[vif1:worker] exception in work kind " << header.kind << ": " << e.what() << std::endl;
                }
            }
            batch.clear();
        }
    }
};

void PS2Memory::startVif1Worker()
{
    if (m_vif1Worker)
        return;
    const char *env = std::getenv("PS2X_VU1_THREAD");
    if (env && env[0] == '0')
    {
        std::cerr << "[vif1:worker] disabled by PS2X_VU1_THREAD=0 (VIF1/VU1 run on the EE thread)" << std::endl;
        return;
    }
    auto *worker = new Vif1Worker(*this);
    worker->thread = std::thread([worker] { worker->loop(); });
    worker->threadId = worker->thread.get_id();
    ps2x::vif1scan::eeState() = {vif1_regs.cycle, m_vif1PendingPath2ImageQwc};
    m_vif1Worker = worker;
    std::cerr << "[vif1:worker] VIF1/VU1/GIF processing on a worker thread (PS2X_VU1_THREAD=0 to disable)" << std::endl;
}

void PS2Memory::stopVif1Worker()
{
    Vif1Worker *worker = m_vif1Worker;
    if (!worker)
        return;
    worker->sync();
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->stop = true;
    }
    worker->workCv.notify_one();
    if (worker->thread.joinable())
        worker->thread.join();
    m_vif1Worker = nullptr;
    delete worker;
}

bool PS2Memory::onVif1WorkerThread() const
{
    return m_vif1Worker && std::this_thread::get_id() == m_vif1Worker->threadId;
}

void PS2Memory::syncVif1Worker() const
{
    if (!m_vif1Worker || std::this_thread::get_id() == m_vif1Worker->threadId)
        return;
    m_vif1Worker->sync();
}

void PS2Memory::enqueueVif1Work(uint32_t kind, const uint8_t *data, uint32_t size)
{
    if (!m_vif1Worker || onVif1WorkerThread())
    {
        runVif1Work(kind, data, size);
        return;
    }
    if (m_vif1EnqueueHook)
        m_vif1EnqueueHook();
    ps2x::vif1scan::Result scan;
    if (kind == Vif1WorkVif1Data)
        ps2x::vif1scan::scanVif1(ps2x::vif1scan::eeState(), data, size, scan);
    else if (kind == Vif1WorkGifPath3)
        ps2x::vif1scan::scanGifPath3(data, size, scan);
    if (scan.needsSync)
        m_vif1GsIrqPending = true;
    m_vif1Worker->push(kind, data, size);
    if (scan.finishes != 0u)
    {
        // Latch FINISH now, as the GS did when VIF1 ran on the EE (see runtime/vif1_scan.h).
        ps2x::vif1scan::g_finishCredits.fetch_add(scan.finishes, std::memory_order_acq_rel);
        gs_regs.csr.fetch_or(0x2u, std::memory_order_acq_rel);
    }
}

void PS2Memory::syncVif1WorkerIfGsInterrupt()
{
    // SIGNAL/LABEL (and FINISH on PATH3) cannot be latched ahead of the worker: wait for it, so
    // the guest's GS interrupt handler still runs right after the kick that carried them.
    if (!m_vif1GsIrqPending)
        return;
    m_vif1GsIrqPending = false;
    static const bool s_trace = [] { const char *v = std::getenv("PS2X_GS_FINISH_TRACE"); return v && *v == '1'; }();
    if (s_trace)
    {
        static uint32_t s_syncs = 0;
        if ((++s_syncs % 300u) == 0u)
            std::cerr << "[vif1:worker] GS-interrupt syncs: " << s_syncs << std::endl;
    }
    syncVif1Worker();
}

void PS2Memory::runVif1Work(uint32_t kind, const uint8_t *data, uint32_t size)
{
    switch (kind)
    {
    case Vif1WorkVif1Data:
        processVIF1Data(data, size);
        break;
    case Vif1WorkGifPath3:
        submitGifPacket(GifPathId::Path3, data, size, false);
        break;
    case Vif1WorkGifDrain:
        if (m_gifArbiter)
            m_gifArbiter->drain();
        break;
    default:
        break;
    }
}
