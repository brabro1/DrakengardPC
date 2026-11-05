#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Diagnostic: PS2X_VU_CLIP_TRACE=<path>[:<limit>] records VU CLIP instructions and clip-flag
// reads (FCEQ/FCAND/FCOR), at most <limit> lines per (unit, pc, kind) site (default 4).
namespace ps2x::vutrace
{
    struct ClipTrace
    {
        FILE *out = nullptr;
        uint32_t perSite = 4;
        uint16_t counts[2][2048] = {}; // unit x (pc / 8)

        ClipTrace()
        {
            const char *spec = std::getenv("PS2X_VU_CLIP_TRACE");
            if (!spec || !*spec)
                return;
            char path[512] = {};
            std::strncpy(path, spec, sizeof(path) - 1);
            if (char *colon = std::strrchr(path, ':'); colon && colon != path + 1)
            {
                perSite = static_cast<uint32_t>(std::strtoul(colon + 1, nullptr, 10));
                *colon = '\0';
            }
            out = std::fopen(path, "w");
        }

        bool take(uint32_t unit, uint32_t pc)
        {
            if (!out)
                return false;
            uint16_t &n = counts[unit & 1u][(pc >> 3) & 2047u];
            if (n >= perSite)
                return false;
            ++n;
            return true;
        }
    };

    inline ClipTrace &clipTrace()
    {
        static ClipTrace trace;
        return trace;
    }
}

inline void vuClipTrace(uint32_t unit, uint32_t pc, const char *kind, uint32_t fs, uint32_t ft,
                        const float *vs, float w, uint32_t flags)
{
    auto &trace = ps2x::vutrace::clipTrace();
    if (!trace.take(unit, pc))
        return;
    std::fprintf(trace.out, "vu%u pc %04x %s vf%u (%g %g %g %g) vs |vf%u.w| %g -> %02x\n",
                 unit, pc, kind, fs, vs[0], vs[1], vs[2], vs[3], ft, w < 0 ? -w : w, flags);
    std::fflush(trace.out);
}

inline void vuFlagReadTrace(uint32_t unit, uint32_t pc, const char *kind, uint32_t imm, uint32_t clip, uint32_t result)
{
    auto &trace = ps2x::vutrace::clipTrace();
    if (!trace.take(unit, pc + 4u))
        return;
    std::fprintf(trace.out, "vu%u pc %04x %s imm %06x clip %06x -> %u\n", unit, pc, kind, imm, clip, result);
    std::fflush(trace.out);
}
