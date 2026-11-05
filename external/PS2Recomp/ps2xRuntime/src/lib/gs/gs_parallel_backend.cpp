// GS backend on paraLLEl-GS (https://github.com/Arntzen-Software/parallel-gs, LGPL-3.0+).
// paraLLEl-GS decodes the GIF stream and renders with Vulkan compute; this file adapts the
// runtime's GSRasterBackend interface to its GSInterface and reads scanned-out frames back
// into the host presentation buffer.

#include "runtime/gs/gs_parallel_backend.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "runtime/ps2_memory.h"
#include "runtime/wait_stats.h"

#include "context.hpp"
#include "device.hpp"
#include "gs_interface.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    constexpr uint32_t kHostFrameWidth = 640u;
    constexpr uint32_t kHostFrameHeight = 512u;
    constexpr uint32_t kVramSize = 4u * 1024u * 1024u;
}

struct GSParallelBackend::Impl
{
    Vulkan::Context context;
    Vulkan::Device device;
    ParallelGS::GSInterface iface;
    // Pipelined readback: each Present submits a copy of the new scanout and returns the
    // previous one, so neither the host nor the game thread waits for in-flight GPU work.
    struct Readback
    {
        Vulkan::BufferHandle buffer;
        Vulkan::Fence fence;
        uint32_t width = 0, height = 0, stride = 0;
        bool bgra = false;
        bool pending = false;
    };
    Readback readbacks[3];
    uint32_t readbackIndex = 0;
    mutable std::mutex mutex; // held by whoever drives paraLLEl-GS (the worker, or a drained caller)
    uint32_t trxdir = 3u;
    bool loggedClear = false;

    // GS worker thread (as PCSX2's MTGS): the game thread only appends commands to a byte
    // batch; the worker swaps batches (both keep their capacity, so nothing is allocated per
    // packet). Record: CommandHeader, then `size` payload bytes padded to 16.
    enum Kind : uint32_t { Gif = 0, Reg = 1, Vsync = 2 };
    struct CommandHeader
    {
        uint32_t kind;
        uint32_t path;
        uint32_t size;
        uint32_t pad;
        uint64_t value;
        uint64_t pad2;
    };
    struct VsyncPayload
    {
        uint64_t regs[15];
        GSPresentationRequest request;
    };
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::condition_variable idleCv;
    std::vector<uint8_t> pending;
    bool busy = false;
    bool stop = false;
    std::thread worker;

    std::mutex frameMutex;
    PresentationFrame latestFrame;

    void push(uint32_t kind, uint32_t path, uint64_t value, const void *data, uint32_t size)
    {
        const uint32_t padded = (size + 15u) & ~15u;
        bool wake = kind == Vsync;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            const size_t at = pending.size();
            pending.resize(at + sizeof(CommandHeader) + padded);
            CommandHeader header{kind, path, size, 0u, value, 0u};
            std::memcpy(pending.data() + at, &header, sizeof(header));
            if (size)
                std::memcpy(pending.data() + at + sizeof(header), data, size);
            wake = wake || pending.size() >= (256u << 10);
        }
        // Waking the worker is a kernel call on Windows; do it per batch, not per packet.
        // The worker also polls every millisecond.
        if (wake)
            queueCv.notify_one();
    }
    void drain()
    {
        ps2x::waitstats::Scope waitScope(ps2x::waitstats::GsDrain);
        queueCv.notify_one();
        std::unique_lock<std::mutex> lock(queueMutex);
        idleCv.wait(lock, [&] { return pending.empty() && !busy; });
    }
};

GSParallelBackend::GSParallelBackend() = default;
GSParallelBackend::~GSParallelBackend()
{
    if (m_impl)
    {
        {
            std::lock_guard<std::mutex> lock(m_impl->queueMutex);
            m_impl->stop = true;
        }
        m_impl->queueCv.notify_all();
        if (m_impl->worker.joinable())
            m_impl->worker.join();
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->iface.flush();
        m_impl->device.wait_idle();
    }
}

bool GSParallelBackend::create(uint32_t superSampling)
{
    if (!Vulkan::Context::init_loader(nullptr))
    {
        std::cerr << "[gs:parallel] Vulkan loader not found; using the CPU GS" << std::endl;
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->context.set_num_thread_indices(1);
    if (!impl->context.init_instance_and_device(nullptr, 0, nullptr, 0,
                                                Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
                                                    Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
                                                    Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT))
    {
        std::cerr << "[gs:parallel] no usable Vulkan device; using the CPU GS" << std::endl;
        return false;
    }
    impl->device.set_context(impl->context);
    impl->device.init_frame_contexts(4);

    ParallelGS::GSOptions options{};
    options.vram_size = kVramSize;
    switch (superSampling)
    {
    case 2: options.super_sampling = ParallelGS::SuperSampling::X2; break;
    case 4: options.super_sampling = ParallelGS::SuperSampling::X4; break;
    case 8: options.super_sampling = ParallelGS::SuperSampling::X8; break;
    case 16: options.super_sampling = ParallelGS::SuperSampling::X16; break;
    default: options.super_sampling = ParallelGS::SuperSampling::X1; break;
    }
    if (!impl->iface.init(&impl->device, options))
    {
        std::cerr << "[gs:parallel] paraLLEl-GS init failed (missing Vulkan features); using the CPU GS" << std::endl;
        return false;
    }

    std::cerr << "[gs:parallel] paraLLEl-GS on " << impl->device.get_gpu_properties().deviceName
              << " ssaa=" << static_cast<uint32_t>(options.super_sampling) << std::endl;
    m_impl = std::move(impl);
    m_impl->worker = std::thread([this] { workerLoop(); });
    return true;
}

void GSParallelBackend::workerLoop()
{
    Impl &s = *m_impl;
    std::vector<uint8_t> batch;
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(s.queueMutex);
            s.busy = false;
            s.idleCv.notify_all();
            while (!s.stop && s.pending.empty())
                s.queueCv.wait_for(lock, std::chrono::milliseconds(1));
            if (s.stop && s.pending.empty())
                return;
            batch.swap(s.pending);
            s.busy = true;
        }
        std::lock_guard<std::mutex> lock(s.mutex);
        size_t at = 0;
        while (at + sizeof(Impl::CommandHeader) <= batch.size())
        {
            Impl::CommandHeader header;
            std::memcpy(&header, batch.data() + at, sizeof(header));
            const uint8_t *payload = batch.data() + at + sizeof(header);
            at += sizeof(header) + ((header.size + 15u) & ~15u);
            switch (header.kind)
            {
            case Impl::Gif:
                s.iface.gif_transfer(header.path, payload, header.size);
                break;
            case Impl::Reg:
                s.iface.write_register(ParallelGS::RegisterAddr(header.path), header.value);
                break;
            case Impl::Vsync:
            {
                Impl::VsyncPayload vsync;
                std::memcpy(&vsync, payload, sizeof(vsync));
                PresentationFrame frame = scanout(vsync.regs, vsync.request);
                if (frame)
                {
                    std::lock_guard<std::mutex> frameLock(s.frameMutex);
                    s.latestFrame = std::move(frame);
                }
                break;
            }
            default:
                break;
            }
        }
        batch.clear();
    }
}

void GSParallelBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    if (!m_impl || !vram || vramSize == 0u)
        return;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const uint32_t bytes = std::min(vramSize, kVramSize);
    void *dst = m_impl->iface.map_vram_write(0, bytes);
    std::memcpy(dst, vram, bytes);
    m_impl->iface.end_vram_write(0, bytes);
}

void GSParallelBackend::Reset()
{
    if (!m_impl)
        return;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->iface.reset_context_state();
}

void GSParallelBackend::BeginTransfer(const GSTransferCommand &command)
{
    if (m_impl)
        m_impl->trxdir = command.direction;
}

void GSParallelBackend::Flush()
{
    // paraLLEl-GS batches work itself; submission happens at presentation and readback.
}

void GSParallelBackend::Sync(GSSyncReason reason)
{
    if (!m_impl || reason != GSSyncReason::Reset)
        return;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->iface.flush();
}

void GSParallelBackend::RawGifPacket(uint32_t path, const uint8_t *data, uint32_t sizeBytes)
{
    if (!m_impl || !data || sizeBytes < 16u)
        return;
    m_impl->push(Impl::Gif, std::min<uint32_t>(path, 3u), 0u, data, sizeBytes & ~15u);
}

void GSParallelBackend::RawRegisterWrite(uint8_t addr, uint64_t value)
{
    if (!m_impl)
        return;
    m_impl->push(Impl::Reg, addr, value, nullptr, 0u);
}

PresentationFrame GSParallelBackend::Present(const GSPresentationRequest &request)
{
    if (!m_impl)
        return {};
    // Queue a scanout with the CRTC registers as they are now and hand back the newest frame
    // the worker has finished (one or two VSyncs behind).
    Impl::VsyncPayload vsync{};
    vsync.request = request;
    if (m_privRegs)
    {
        const uint64_t regs[15] = {m_privRegs->pmode, m_privRegs->smode1, m_privRegs->smode2, m_privRegs->srfsh,
                                   m_privRegs->synch1, m_privRegs->synch2, m_privRegs->syncv, m_privRegs->dispfb1,
                                   m_privRegs->display1, m_privRegs->dispfb2, m_privRegs->display2, m_privRegs->extbuf,
                                   m_privRegs->extdata, m_privRegs->extwrite, m_privRegs->bgcolor};
        std::memcpy(vsync.regs, regs, sizeof(regs));
    }
    m_impl->push(Impl::Vsync, 0u, 0u, &vsync, sizeof(vsync));
    std::lock_guard<std::mutex> frameLock(m_impl->frameMutex);
    return m_impl->latestFrame;
}

PresentationFrame GSParallelBackend::scanout(const uint64_t (&regs)[15], const GSPresentationRequest &request)
{
    PresentationFrame frame{};
    auto &iface = m_impl->iface;
    auto &device = m_impl->device;

    // Privileged registers: same order and 16-byte stride as the PS2 0x12000000 block.
    auto &priv = iface.get_priv_register_state();
    if (m_privRegs)
    {
        for (uint32_t i = 0; i < 15u; ++i)
            priv.qwords_lo[i * 2u] = regs[i];
        if (regs[1] == 0u)
        {
            // SMODE1 never programmed (no SetGsCrt/ResetGraph seen yet): scan out as NTSC.
            priv.qwords_lo[2] = 0x740834504ull;
        }
    }
    else
    {
        priv.qwords_lo[0] = request.pmode;
        priv.qwords_lo[4] = request.smode2;
        priv.qwords_lo[14] = request.dispfb1;
        priv.qwords_lo[16] = request.display1;
        priv.qwords_lo[18] = request.dispfb2;
        priv.qwords_lo[20] = request.display2;
        priv.qwords_lo[28] = request.bgcolor;
    }

    iface.flush();
    ParallelGS::VSyncInfo info{};
    info.phase = static_cast<uint32_t>(request.vsyncTick & 1u);
    info.dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
    info.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    info.force_progressive = true;
    info.anti_blur = true;
    info.raw_circuit_scanout = true;
    auto scanout = iface.vsync(info);
    static uint32_t s_reports = 0u;
    if (s_reports < 8u && (!scanout.image || s_reports == 0u))
    {
        ++s_reports;
        std::cerr << "[gs:parallel] vsync tick=" << request.vsyncTick << " image=" << (scanout.image ? 1 : 0)
                  << " size=" << (scanout.image ? scanout.image->get_width() : 0u) << "x"
                  << (scanout.image ? scanout.image->get_height() : 0u) << std::hex << " pmode=0x" << priv.qwords_lo[0]
                  << " smode2=0x" << priv.qwords_lo[4] << " dispfb1=0x" << priv.qwords_lo[14] << " display1=0x"
                  << priv.qwords_lo[16] << " dispfb2=0x" << priv.qwords_lo[18] << " display2=0x" << priv.qwords_lo[20]
                  << std::dec << std::endl;
    }
    if (!scanout.image)
    {
        device.next_frame_context();
        return frame;
    }

    auto &rb = m_impl->readbacks;
    auto &slot = rb[m_impl->readbackIndex];
    const uint32_t imageWidth = scanout.image->get_width();
    const uint32_t imageHeight = scanout.image->get_height();
    const VkDeviceSize bytes = VkDeviceSize(imageWidth) * imageHeight * 4u;
    if (slot.pending)
        slot.fence->wait(); // three slots: this is the frame from two presents ago
    if (!slot.buffer || slot.buffer->get_create_info().size < bytes)
    {
        Vulkan::BufferCreateInfo info{};
        info.size = bytes;
        info.domain = Vulkan::BufferDomain::CachedHost;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        slot.buffer = device.create_buffer(info);
    }
    {
        auto cmd = device.request_command_buffer();
        cmd->copy_image_to_buffer(*slot.buffer, *scanout.image, 0, {0, 0, 0}, {imageWidth, imageHeight, 1}, 0, 0,
                                  {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
        cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        slot.fence.reset();
        device.submit(cmd, &slot.fence);
        const VkFormat format = scanout.image->get_format();
        slot.bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
        slot.width = std::min(imageWidth, kHostFrameWidth);
        slot.height = std::min(imageHeight, kHostFrameHeight);
        slot.stride = imageWidth * 4u;
        slot.pending = true;
    }
    m_impl->readbackIndex = (m_impl->readbackIndex + 1u) % 3u;

    // Return the oldest completed copy (one present behind).
    auto &done = rb[(m_impl->readbackIndex + 1u) % 3u];
    uint32_t width = 0, height = 0;
    if (done.pending && done.fence)
    {
        done.fence->wait();
        const auto *src = static_cast<const uint8_t *>(device.map_host_buffer(*done.buffer, Vulkan::MEMORY_ACCESS_READ_BIT));
        frame.pixels.assign(size_t(kHostFrameWidth) * kHostFrameHeight * 4u, 0u);
        for (uint32_t y = 0; y < done.height; ++y)
        {
            const uint8_t *s = src + size_t(y) * done.stride;
            uint8_t *d = frame.pixels.data() + size_t(y) * kHostFrameWidth * 4u;
            for (uint32_t x = 0; x < done.width; ++x, s += 4, d += 4)
            {
                d[0] = done.bgra ? s[2] : s[0];
                d[1] = s[1];
                d[2] = done.bgra ? s[0] : s[2];
                d[3] = 0xFFu;
            }
        }
        device.unmap_host_buffer(*done.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
        done.pending = false;
        width = done.width;
        height = done.height;
    }
    device.next_frame_context();

    frame.width = width;
    frame.height = height;
    frame.displayFbp = static_cast<uint32_t>(request.dispfb1 & 0x1FFu);
    frame.sourceFbp = frame.displayFbp;
    return frame;
}

bool GSParallelBackend::ClearFramebuffer(const GSContext &, uint32_t)
{
    if (m_impl && !m_impl->loggedClear)
    {
        m_impl->loggedClear = true;
        std::cerr << "[gs:parallel] ClearFramebuffer (HLE clear helper) not implemented on the GPU backend" << std::endl;
    }
    return false;
}

uint32_t GSParallelBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    if (!m_impl || !dst || maxBytes < 16u)
        return 0u;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const uint32_t words = maxBytes / 16u;
    m_impl->iface.read_transfer_fifo(dst, words);
    return words * 16u;
}

uint32_t GSParallelBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    if (!m_impl)
        return 0u;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto *vram = static_cast<uint8_t *>(const_cast<void *>(m_impl->iface.map_vram_read(0, kVramSize)));
    switch (psm)
    {
    case GS_PSM_CT32: return GSMem::ReadCT32(vram, base, bw, x, y);
    case GS_PSM_CT24: return GSMem::ReadCT32(vram, base, bw, x, y) & 0x00FFFFFFu;
    default: return 0u;
    }
}

void GSParallelBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    if (!m_impl)
        return;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto *vram = static_cast<uint8_t *>(m_impl->iface.map_vram_write(0, kVramSize));
    switch (psm)
    {
    case GS_PSM_CT32: GSMem::WriteCT32(vram, base, bw, x, y, value); break;
    case GS_PSM_CT24: GSMem::WriteCT24(vram, base, bw, x, y, value); break;
    case GS_PSM_CT16: GSMem::WriteCT16(vram, base, bw, x, y, value); break;
    default: break;
    }
    m_impl->iface.end_vram_write(0, kVramSize);
}

void GSParallelBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    out.clear();
    if (!m_impl)
        return;
    m_impl->drain();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto *vram = static_cast<const uint8_t *>(m_impl->iface.map_vram_read(0, kVramSize));
    out.assign(vram, vram + kVramSize);
}

GSTransferSnapshot GSParallelBackend::GetTransferSnapshot() const
{
    GSTransferSnapshot snapshot{};
    if (m_impl)
        snapshot.direction = m_impl->trxdir;
    return snapshot;
}
