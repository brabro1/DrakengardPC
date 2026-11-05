// Based on Blackline Interactive implementation
#include "runtime/ps2_memory.h"
#include "runtime/vu_aot.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

enum VIFCmd : uint8_t
{
    VIF_NOP = 0x00,
    VIF_STCYCL = 0x01,
    VIF_OFFSET = 0x02,
    VIF_BASE = 0x03,
    VIF_ITOP = 0x04,
    VIF_STMOD = 0x05,
    VIF_MSKPATH3 = 0x06,
    VIF_MARK = 0x07,
    VIF_FLUSHE = 0x10,
    VIF_FLUSH = 0x11,
    VIF_FLUSHA = 0x13,
    VIF_MSCAL = 0x14,
    VIF_MSCALF = 0x15,
    VIF_MSCNT = 0x17,
    VIF_STMASK = 0x20,
    VIF_STROW = 0x30,
    VIF_STCOL = 0x31,
    VIF_MPG = 0x4A,
    VIF_DIRECT = 0x50,
    VIF_DIRECTHL = 0x51,
};

namespace
{
    constexpr uint8_t kGifFmtImage = 2u;

    // VIF UNPACK shared by VIF0 and VIF1 (PCSX2 Vif_Unpack.cpp semantics). Returns the source
    // bytes the command consumes (word aligned). Data is written only when the whole payload is
    // present, as before. addrMask is the channel's quadword address mask (VU0 0xFF, VU1 0x3FF).
    uint32_t vifUnpack(VIFRegisters &regs, uint8_t *vuData, uint32_t vuDataSize, uint32_t addrMask,
                       bool topsRelative, uint8_t opcode, uint8_t num, uint16_t imm,
                       const uint8_t *data, uint32_t available)
    {
        const uint8_t vn = (opcode >> 2) & 0x3;
        const uint8_t vl = opcode & 0x3;
        const bool maskEnable = (opcode & 0x10u) != 0u;
        const uint32_t components = vn + 1u;
        uint32_t bitsPerComponent = 32u;
        switch (vl)
        {
        case 1:
            bitsPerComponent = 16u;
            break;
        case 2:
            bitsPerComponent = 8u;
            break;
        case 3:
            bitsPerComponent = (vn == 3u) ? 4u : 16u;
            break;
        default:
            break;
        }
        const uint32_t bitsPerVector = (vl == 3u && vn == 3u) ? 16u : (components * bitsPerComponent);
        const uint32_t bytesPerVector = (bitsPerVector + 7u) / 8u;
        // NUM is 8-bit and NUM==0 means 256 vectors (writes).
        const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);

        // STCYCL controls write cycles for UNPACK.
        uint32_t cl = regs.cycle & 0xFFu;
        uint32_t wl = (regs.cycle >> 8) & 0xFFu;
        if (cl == 0u)
            cl = 1u;
        if (wl == 0u)
            wl = 1u;

        uint32_t sourceVectorCount = writeVectorCount;
        if (cl < wl)
        {
            const uint32_t fullBlocks = writeVectorCount / wl;
            uint32_t remainder = writeVectorCount % wl;
            if (remainder > cl)
                remainder = cl;
            sourceVectorCount = fullBlocks * cl + remainder;
        }

        uint32_t totalBytes = sourceVectorCount * bytesPerVector;
        totalBytes = (totalBytes + 3u) & ~3u;

        uint32_t vuAddr = static_cast<uint32_t>(imm) & addrMask;
        if (topsRelative && (imm & 0x8000u) != 0u)
            vuAddr = (vuAddr + (regs.tops & 0x3FFu)) & addrMask;

        const bool zeroExtend = (imm & 0x4000u) != 0u;

        // Diagnostic: PS2X_VIF_WATCH=<qword> logs every UNPACK whose destination covers it.
        static const long s_watch = []
        {
            const char *value = std::getenv("PS2X_VIF_WATCH");
            return value ? std::strtol(value, nullptr, 0) : -1L;
        }();
        if (s_watch >= 0)
        {
            const uint32_t span = (cl >= wl) ? ((writeVectorCount + wl - 1u) / wl) * cl : writeVectorCount;
            const uint32_t w = static_cast<uint32_t>(s_watch);
            const uint32_t rel = (w - vuAddr) & addrMask;
            if (rel < span)
            {
                uint32_t first[4] = {};
                std::memcpy(first, data, std::min<uint32_t>(16u, available));
                std::fprintf(stderr, "[vif:watch] vif%u qw %u unpack op %02x num %u imm %04x addr %u tops %u cl %u wl %u mode %u mask %08x row %08x %08x %08x %08x complete %u data %08x %08x %08x %08x\n",
                             addrMask == 0x3FFu ? 1u : 0u, w, opcode, writeVectorCount, imm, vuAddr, regs.tops, cl, wl,
                             regs.mode, regs.mask, regs.row[0], regs.row[1], regs.row[2], regs.row[3],
                             (vuData && totalBytes != 0u && totalBytes <= available) ? 1u : 0u,
                             first[0], first[1], first[2], first[3]);
            }
        }
        if (!vuData || totalBytes == 0u || totalBytes > available)
            return totalBytes;

        auto extend16 = [&](uint16_t raw) -> uint32_t
        {
            if (zeroExtend)
                return static_cast<uint32_t>(raw);
            return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
        };
        auto extend8 = [&](uint8_t raw) -> uint32_t
        {
            if (zeroExtend)
                return static_cast<uint32_t>(raw);
            return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(raw)));
        };

        uint32_t srcIndex = 0u;
        for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex)
        {
            const uint32_t cyclePos = writeIndex % wl;
            const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);
            const uint32_t destVec = (cl >= wl) ? ((vuAddr + (writeIndex / wl) * cl + cyclePos) & addrMask)
                                                : ((vuAddr + writeIndex) & addrMask);
            const uint32_t destOff = destVec * 16u;
            if (destOff + 16u > vuDataSize)
            {
                if (sourceAvailable && srcIndex < sourceVectorCount)
                    ++srcIndex;
                continue;
            }

            uint32_t lanes[4] = {0u, 0u, 0u, 0u};
            std::memcpy(lanes, vuData + destOff, sizeof(lanes));
            uint32_t decompressed[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};

            const uint8_t *srcVec = nullptr;
            bool decoded = false;
            if (sourceAvailable && srcIndex < sourceVectorCount)
            {
                srcVec = data + srcIndex * bytesPerVector;
                ++srcIndex;
                decoded = true;
            }

            bool handledFormat = decoded;
            if (decoded && vl <= 2u)
            {
                auto component = [&](uint32_t c) -> uint32_t
                {
                    if (vl == 0u)
                    {
                        uint32_t scalar = 0;
                        std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                        return scalar;
                    }
                    if (vl == 1u)
                    {
                        uint16_t raw = 0;
                        std::memcpy(&raw, srcVec + c * 2u, sizeof(raw));
                        return extend16(raw);
                    }
                    return extend8(srcVec[c]);
                };
                if (components == 1u)
                {
                    const uint32_t scalar = component(0u);
                    decompressed[0] = decompressed[1] = decompressed[2] = decompressed[3] = scalar;
                }
                else
                {
                    for (uint32_t c = 0; c < components; ++c)
                        decompressed[c] = component(c);
                }
            }
            else if (decoded && vn == 3u)
            {
                // V4-5: RGBA5551 expanded to 8 bits per channel (PCSX2 UNPACK_V4_5).
                uint16_t packed = 0;
                std::memcpy(&packed, srcVec, sizeof(packed));
                decompressed[0] = (packed & 0x001Fu) << 3;
                decompressed[1] = (packed & 0x03E0u) >> 2;
                decompressed[2] = (packed & 0x7C00u) >> 7;
                decompressed[3] = (packed & 0x8000u) >> 8;
            }
            else
            {
                handledFormat = false;
            }

            // Reserved format (vl==3, vn!=3) fallback: preserve the legacy raw copy.
            if (!handledFormat && decoded && !maskEnable && (regs.mode == 0u || regs.mode == 3u))
            {
                const uint32_t copyBytes = (bytesPerVector < 16u) ? bytesPerVector : 16u;
                std::memcpy(vuData + destOff, srcVec, copyBytes);
                continue;
            }

            const bool canAdd = (vl != 3u || vn != 3u);
            const uint32_t mode = regs.mode & 3u;
            const uint32_t colIdx = (cyclePos > 3u) ? 3u : cyclePos;
            const uint32_t maskCycle = (cyclePos > 3u) ? 3u : cyclePos;

            for (uint32_t field = 0u; field < 4u; ++field)
            {
                uint32_t maskSpec = 0u;
                if (maskEnable)
                {
                    const uint32_t shift = ((maskCycle * 4u) + field) * 2u;
                    maskSpec = (regs.mask >> shift) & 0x3u;
                }

                // In fill-write cycles with suspended source reads, treat raw-data selections as row-fill.
                if (!decoded && maskSpec == 0u)
                    maskSpec = 1u;

                uint32_t writeVal = lanes[field];
                if (maskSpec == 0u)
                {
                    if (handledFormat)
                    {
                        writeVal = decompressed[field];
                        if (canAdd && (mode == 1u || mode == 2u))
                        {
                            writeVal = writeVal + regs.row[field];
                            if (mode == 2u)
                                regs.row[field] = writeVal;
                        }
                    }
                }
                else if (maskSpec == 1u)
                {
                    writeVal = regs.row[field];
                }
                else if (maskSpec == 2u)
                {
                    writeVal = regs.col[colIdx];
                }
                else
                {
                    continue; // write-protect
                }

                lanes[field] = writeVal;
            }

            std::memcpy(vuData + destOff, lanes, sizeof(lanes));
        }
        return totalBytes;
    }

    uint32_t pendingGifImageQwc(const uint8_t *data, uint32_t sizeBytes)
    {
        if (!data || sizeBytes < 16u)
            return 0u;

        uint32_t offset = 0u;
        while (offset + 16u <= sizeBytes)
        {
            uint64_t tagLo = 0u;
            std::memcpy(&tagLo, data + offset, sizeof(tagLo));
            offset += 16u;

            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;

            uint64_t payloadBytes = 0u;
            if (flg == 0u) // PACKED
            {
                payloadBytes = static_cast<uint64_t>(nloop) * nreg * 16ull;
            }
            else if (flg == 1u) // REGLIST, padded to a quadword
            {
                payloadBytes = static_cast<uint64_t>(nloop) * nreg * 8ull;
                payloadBytes = (payloadBytes + 15ull) & ~15ull;
            }
            else if (flg == kGifFmtImage || flg == 3u) // FLG=3 ("disabled" in the GS manual) behaves exactly like IMAGE: NLOOP qwords of image data.
            {
                payloadBytes = static_cast<uint64_t>(nloop) * 16ull;
                const uint64_t availableBytes = sizeBytes - offset;
                if (payloadBytes > availableBytes)
                {
                    return static_cast<uint32_t>((payloadBytes - availableBytes) / 16ull);
                }
            }
            else
            {
                return 0u;
            }

            if (payloadBytes > static_cast<uint64_t>(sizeBytes - offset))
                return 0u;
            offset += static_cast<uint32_t>(payloadBytes);
        }

        return 0u;
    }
}

void PS2Memory::processVIF0Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF0Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF0Data(const uint8_t *data, uint32_t sizeBytes)
{
    if (sizeBytes == 0u)
        return;

    uint32_t pos = 0;
    while (pos + 4 <= sizeBytes)
    {
        uint32_t cmd = 0u;
        std::memcpy(&cmd, data + pos, sizeof(cmd));
        pos += 4u;

        const uint8_t opcode = static_cast<uint8_t>((cmd >> 24) & 0x7Fu);
        const uint16_t imm = static_cast<uint16_t>(cmd & 0xFFFFu);
        const uint8_t num = static_cast<uint8_t>((cmd >> 16) & 0xFFu);
        const bool irq = (cmd & 0x80000000u) != 0u;

        vif0_regs.code = cmd;
        vif0_regs.num = num;
        if (irq)
            vif0_regs.stat |= (1u << 11);

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif0_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            vif0_regs.itops = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif0_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            vif0_regs.mark = imm;
            vif0_regs.stat |= (1u << 6);
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            if (pos + 4u > sizeBytes)
                break;
            std::memcpy(&vif0_regs.mask, data + pos, sizeof(vif0_regs.mask));
            pos += 4u;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.row, data + pos, 16u);
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.col, data + pos, 16u);
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            const uint32_t destAddr = static_cast<uint32_t>(imm & 0x1FFu) * 8u;
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            uint32_t copyBytes = 0u;
            if (m_vu0Code && destAddr < PS2_VU0_CODE_SIZE && mpgBytes > 0u)
            {
                copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU0_CODE_SIZE)
                    copyBytes = PS2_VU0_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes)
                {
                    std::memcpy(m_vu0Code + destAddr, data + pos, copyBytes);
                    markVU0CodeModified();
                    vuaot::onVu0Upload(destAddr, data + pos, copyBytes);
                }
            }

            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if ((opcode & 0x60u) == 0x60u)
        {
            // VIF0: 256 quadwords of VU0 data memory, no TOPS-relative double buffering.
            pos += vifUnpack(vif0_regs, m_vu0Data, PS2_VU0_DATA_SIZE, 0xFFu, false,
                             opcode, num, imm, data + pos, sizeBytes - pos);
            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            break;
        }
    }
}

void PS2Memory::processVIF1Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF1Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF1Data(const uint8_t *data, uint32_t sizeBytes)
{
    if (sizeBytes == 0u)
        return;

    uint32_t pos = 0;

    while (pos + 4 <= sizeBytes)
    {
        if (m_vif1PendingPath2ImageQwc != 0u)
        {
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            if (availableQw == 0u)
            {
                break;
            }

            const uint32_t chunkQw = std::min<uint32_t>(m_vif1PendingPath2ImageQwc, availableQw);
            std::vector<uint8_t> imagePacket(16u + static_cast<size_t>(chunkQw) * 16u, 0u);
            const uint64_t imageTag =
                static_cast<uint64_t>(chunkQw & 0x7FFFu) |
                ((m_vif1PendingPath2ImageQwc == chunkQw) ? (1ull << 15) : 0ull) |
                (static_cast<uint64_t>(kGifFmtImage) << 58);
            std::memcpy(imagePacket.data(), &imageTag, sizeof(imageTag));
            std::memcpy(imagePacket.data() + 16u, data + pos, static_cast<size_t>(chunkQw) * 16u);
            submitGifPacket(GifPathId::Path2, imagePacket.data(), static_cast<uint32_t>(imagePacket.size()), true, m_vif1PendingPath2DirectHl);

            pos += chunkQw * 16u;
            m_vif1PendingPath2ImageQwc -= chunkQw;
            if (m_vif1PendingPath2ImageQwc == 0u)
            {
                m_vif1PendingPath2DirectHl = false;
            }
            continue;
        }

        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        pos += 4;

        uint8_t opcode = (cmd >> 24) & 0x7F;
        uint16_t imm = cmd & 0xFFFF;
        uint8_t num = (cmd >> 16) & 0xFF;
        const bool irq = (cmd & 0x80000000u) != 0u;

        // Track most-recent command for VIFn_CODE emulation.
        vif1_regs.code = cmd;
        vif1_regs.num = num;
        if (irq)
            vif1_regs.stat |= (1u << 11); // INT

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif1_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_OFFSET)
        {
            // VIF double-buffer setup. OFFSET clears DBF and resets TOPS to BASE.
            // Do not rewrite BASE from the previous TOPS value.
            vif1_regs.ofst = imm & 0x3FFu;
            vif1_regs.tops = vif1_regs.base & 0x3FFu;
            vif1_regs.stat &= ~(1u << 7); // clear DBF
            continue;
        }
        else if (opcode == VIF_BASE)
        {
            // BASE only updates the base register. TOPS changes on OFFSET/MSCAL.
            vif1_regs.base = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            // ITOP VIFcode writes pending ITOPS; VU XITOP observes it after MSCAL/MSCNT.
            vif1_regs.itops = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif1_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MSKPATH3)
        {
            // VIF command docs: MSKPATH3 uses IMMEDIATE bit 15.
            const bool wasMasked = m_path3Masked;
            m_path3Masked = (imm & 0x8000u) != 0u;
            if (wasMasked && !m_path3Masked)
                flushMaskedPath3Packets();
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            vif1_regs.mark = imm;
            vif1_regs.stat |= (1u << 6); // MRK
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_MSCAL || opcode == VIF_MSCALF)
        {
            uint32_t startPC = (uint32_t)imm * 8u;

            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscalCallback)
                m_vu1MscalCallback(startPC, runTop, runItop);
            continue;
        }
        else if (opcode == VIF_MSCNT)
        {
            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscntCallback)
                m_vu1MscntCallback(runTop, runItop);
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            if (pos + 4 > sizeBytes)
                break;
            uint32_t maskValue = 0;
            std::memcpy(&maskValue, data + pos, sizeof(maskValue));
            vif1_regs.mask = maskValue;
            pos += 4;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16 > sizeBytes)
                break;
            std::memcpy(vif1_regs.row, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16 > sizeBytes)
                break;
            std::memcpy(vif1_regs.col, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            uint32_t destAddr = (uint32_t)imm * 8u;
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            if (m_vu1Code && destAddr < PS2_VU1_CODE_SIZE && mpgBytes > 0)
            {
                uint32_t copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU1_CODE_SIZE)
                    copyBytes = PS2_VU1_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes)
                {
                    std::memcpy(m_vu1Code + destAddr, data + pos, copyBytes);
                    markVU1CodeModified();
                    vuaot::onVu1Upload(destAddr, data + pos, copyBytes);
                }
            }
            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL)
        {
            uint32_t qwCount = imm;
            if (qwCount == 0)
                qwCount = 65536;
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            const bool truncated = qwCount > availableQw;
            if (qwCount > availableQw)
                qwCount = availableQw;

            if (qwCount > 0)
            {
                const bool directHl = (opcode == VIF_DIRECTHL);
                submitGifPacket(GifPathId::Path2, data + pos, qwCount * 16, true, directHl);

                const uint32_t pendingImageQw = pendingGifImageQwc(data + pos, qwCount * 16u);
                if (pendingImageQw != 0u)
                {
                    m_vif1PendingPath2ImageQwc = pendingImageQw;
                    m_vif1PendingPath2DirectHl = directHl;
                }
            }

            pos += qwCount * 16;
            if (truncated)
            {
                pos = sizeBytes;
                break;
            }
            continue;
        }
        else if ((opcode & 0x60) == 0x60)
        {
            pos += vifUnpack(vif1_regs, m_vu1Data, PS2_VU1_DATA_SIZE, 0x3FFu, true,
                             opcode, num, imm, data + pos, sizeBytes - pos);
            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            continue;
        }
    }
}
