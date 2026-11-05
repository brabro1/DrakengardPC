#pragma once

#include "runtime/gs/gs_backend.h"

#include <memory>

struct GSRegisters;

// GS backend built on paraLLEl-GS (Vulkan compute, LGPL-3.0, external/parallel-gs).
// It consumes the raw GIF stream (all paths) and the privileged CRTC registers, renders on
// the GPU, and returns scanned-out frames. The frontend's decoded primitive/transfer calls are
// ignored because paraLLEl-GS decodes the same packets itself.
class GSParallelBackend final : public GSRasterBackend
{
public:
    GSParallelBackend();
    ~GSParallelBackend() override;

    // Creates the Vulkan device and paraLLEl-GS state. Returns false (and logs why) when no
    // suitable Vulkan device exists; the caller then keeps the CPU backend.
    bool create(uint32_t superSampling);

    void Initialize(uint8_t *vram, uint32_t vramSize) override;
    void Reset() override;

    void Submit(const GSPrimitiveBatch &) override {}
    void LoadClut(const GSTex0Reg &, const GSTexClutReg &) override {}
    void BeginTransfer(const GSTransferCommand &command) override;
    void UploadImage(const uint8_t *, uint32_t) override {}

    void Flush() override;
    void TextureFlush() override {}
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest &request) override;

    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override;

    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t> &out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

    bool WantsRawGif() const override { return true; }
    void RawGifPacket(uint32_t path, const uint8_t *data, uint32_t sizeBytes) override;
    void RawRegisterWrite(uint8_t addr, uint64_t value) override;
    void SetPrivilegedRegisters(const GSRegisters *regs) override { m_privRegs = regs; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    const GSRegisters *m_privRegs = nullptr;
};
