#include "runtime/gs/ps2_gif_arbiter.h"
#include <algorithm>
#include <cstring>

GifArbiter::GifArbiter(ProcessPacketFn processFn)
    : m_processFn(std::move(processFn))
{
}

bool GifArbiter::isImagePacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    // FLG=3 ("disabled" in the GS manual) behaves exactly like IMAGE: NLOOP qwords of image data.
    return flg == 2u || flg == 3u;
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!data || sizeBytes < 16 || !m_processFn)
        return;

    if (m_count == m_queue.size())
        m_queue.emplace_back();
    GifArbiterPacket &pkt = m_queue[m_count++];
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.data.assign(data, data + sizeBytes);
}

void GifArbiter::drain()
{
    if (!m_processFn)
        return;

    auto before = [](const GifArbiterPacket &a, const GifArbiterPacket &b)
    {
        // DIRECTHL cannot preempt PATH3 IMAGE transfers.
        if (a.path2DirectHl != b.path2DirectHl || a.path3Image != b.path3Image)
        {
            if (a.path3Image && b.path2DirectHl)
                return true;
            if (a.path2DirectHl && b.path3Image)
                return false;
        }
        return pathPriority(a.pathId) < pathPriority(b.pathId);
    };
    // Stable insertion sort (a handful of packets; no temporary allocation).
    for (size_t i = 1; i < m_count; ++i)
        for (size_t j = i; j > 0 && before(m_queue[j], m_queue[j - 1]); --j)
            std::swap(m_queue[j], m_queue[j - 1]);

    for (size_t i = 0; i < m_count; ++i)
    {
        auto &pkt = m_queue[i];
        if (!pkt.data.empty())
        {
            m_currentPath = pkt.pathId;
            m_processFn(pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
        }
    }
    m_count = 0;
}

uint8_t GifArbiter::pathPriority(GifPathId id)
{
    return static_cast<uint8_t>(id);
}
