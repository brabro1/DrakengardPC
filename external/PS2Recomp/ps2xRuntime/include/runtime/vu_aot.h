#pragma once
// Ahead-of-time compiled VU microprograms.
//
// tools/vu_recomp.py (DrakengardPC) translates every VU1 microprogram the game uploads with
// VIF MPG into C++ functions that run against this context. Semantics follow
// VU1Interpreter: FMAC values are computed in float with round-toward-zero (the caller sets
// the rounding mode), flags come from the exact result, and the timing model reproduces the
// interpreter's stalls (per-lane VF/VI/ACC ready cycles), the 4-cycle MAC/status/clip flag
// latency, the Q (FDIV) and P (EFU) latencies, the same-pair read-before-write rule and the
// VI branch hazard. XGKICK sends the GIF packet when it issues.

#include "runtime/ps2_vu1.h"

#include <cmath>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <limits>

namespace vuaot
{
    enum Exit : uint32_t
    {
        ExitEnd = 0,    // E-bit program end (pipelines flushed into the state)
        ExitLeave = 1,  // execution left the compiled program; continue at ctx.pc
        ExitBudget = 2, // cycle budget reached at a taken branch; continue at ctx.pc
        ExitFallback = 3, // construct the compiler does not handle; interpret the pair at ctx.pc
    };

    struct FlagEntry
    {
        uint32_t ready;
        uint32_t mac;
        uint32_t status;
        uint32_t extraSticky;
        uint32_t clip;
        uint8_t kind; // 1 = MAC+status (FMAC), 2 = sticky (FSSET), 4 = clip
    };

    struct Context
    {
        VU1State *s = nullptr;
        uint8_t *mem = nullptr;
        uint32_t memMask = 0x3FFF;
        uint32_t pc = 0;
        uint32_t cyc = 0;
        uint32_t budget = 0;

        uint32_t vfReady[32][4]{};
        uint32_t viReady[16]{};
        uint32_t accReady[4]{};

        // In-flight flag writes: a ring of the 16 newest entries in issue order (ready cycles are
        // non-decreasing). At most two flag writes issue per cycle and they become visible after
        // 4 cycles, so 16 entries span at least 8 cycles and an evicted entry is always due.
        FlagEntry flags[16]{};
        uint32_t flagHead = 0;  // next slot to write
        uint32_t flagCount = 0; // uncommitted entries (the newest flagCount slots)
        uint32_t workingClip = 0;

        bool qPending = false;
        uint32_t qReady = 0;
        float qValue = 0.0f;
        uint32_t qDi = 0;

        bool pPending = false;
        uint32_t pReady = 0;
        float pValue = 0.0f;
        uint32_t efuResource = 0;

        bool ebitPending = false; // E-bit seen: the next executed pair ends the program
        bool brTaken = false;     // taken branch whose delay slot has not executed yet
        uint32_t brTarget = 0;
        // VI branch hazard: a branch in the pair issued right after an integer write that
        // delays branch reads sees the old value (VU1Interpreter::readBranchVi).
        uint32_t bkCyc = 0xFFFFFFFFu;
        uint8_t bkReg = 0;
        int32_t bkVal = 0;

        void (*xgkick)(void *user, const uint8_t *packet, uint32_t bytes) = nullptr;
        void *xgkickUser = nullptr;
        uint8_t kickBuffer[0x10000];
    };

    using ProgramFn = uint32_t (*)(Context &ctx, uint32_t pc);

    struct Program
    {
        uint32_t base;  // VU code byte address the payload is uploaded to
        uint32_t bytes; // payload size
        uint64_t hash;  // FNV-1a 64 of the payload
        ProgramFn fn;
    };

    inline uint64_t fnv1a(const uint8_t *data, uint32_t bytes)
    {
        uint64_t h = 14695981039346656037ull;
        for (uint32_t i = 0; i < bytes; ++i)
            h = (h ^ data[i]) * 1099511628211ull;
        return h;
    }

    // ---- value helpers (VU1Interpreter::normalizeOperand / normalizeResult) ----
    inline float nop_(float v)
    {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        const uint32_t e = (b >> 23) & 0xFF;
        if (e == 0)
            b &= 0x80000000u;
        else if (e == 0xFF)
            b = (b & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&v, &b, 4);
        return v;
    }
    inline float normRes(float v)
    {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        const uint32_t e = (b >> 23) & 0xFF;
        if ((b & 0x7FFFFFFFu) != 0 && e == 0)
            b &= 0x80000000u;
        else if (e == 0xFF)
            b = (b & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&v, &b, 4);
        return v;
    }

    // Exact-result lane flags (Z=1 S=2 U=4 O=8) and value fix-up, as
    // VU1Interpreter::normalizeFmacExactResult. `exact` is computed in double, which under
    // round-toward-zero truncates to the same float as the interpreter's long double path.
    inline uint32_t laneFlags(float &value, double exact)
    {
        const bool neg = std::signbit(exact);
        const double mag = std::fabs(exact);
        uint32_t f = neg ? 2u : 0u;
        const uint32_t sign = neg ? 0x80000000u : 0u;
        if (mag == 0.0)
        {
            f |= 1u;
            std::memcpy(&value, &sign, 4);
        }
        else if (mag > double(std::numeric_limits<float>::max()))
        {
            f |= 8u;
            const uint32_t b = sign | 0x7F7FFFFFu;
            std::memcpy(&value, &b, 4);
        }
        else if (mag < double(std::numeric_limits<float>::min()))
        {
            f |= 5u;
            std::memcpy(&value, &sign, 4);
        }
        return f;
    }
    // Fast path for laneFlags: a normal, non-saturated float result has exactly the exact
    // result's sign and no Z/U/O, and needs no fix-up. Returns 0xFF when the exact check is needed.
    inline uint32_t fastFlags(float value)
    {
        uint32_t b;
        std::memcpy(&b, &value, 4);
        const uint32_t e = (b >> 23) & 0xFFu;
        if (e - 1u < 254u && (b & 0x7FFFFFFFu) != 0x7F7FFFFFu)
            return (b >> 31) ? 2u : 0u;
        return 0xFFu;
    }
    inline uint32_t productSticky(float a, float b)
    {
        float p = a * b;
        return laneFlags(p, double(a) * double(b)) & 0xFu;
    }

    // ---- pipelines ----
    inline void applyFlags(Context &c, const FlagEntry &e)
    {
        if (e.kind & 1u)
        {
            c.s->mac = e.mac;
            const uint32_t cur = e.status & 0xFu;
            c.s->status = (c.s->status & 0xFF0u) | cur | ((cur | e.extraSticky) << 6);
        }
        if (e.kind & 2u)
            c.s->status = (c.s->status & 0x03Fu) | (e.status & 0xFC0u);
        if (e.kind & 4u)
            c.s->clip = e.clip;
    }
    inline void commitFlags(Context &c, uint32_t upTo)
    {
        while (c.flagCount != 0u)
        {
            const FlagEntry &e = c.flags[(c.flagHead - c.flagCount) & 15u];
            if (e.ready > upTo)
                break;
            applyFlags(c, e);
            --c.flagCount;
        }
    }
    inline void pushFlags(Context &c, const FlagEntry &e)
    {
        if (c.flagCount == 16u)
        {
            applyFlags(c, c.flags[c.flagHead & 15u]); // oldest; already due (see Context::flags)
            --c.flagCount;
        }
        c.flags[c.flagHead & 15u] = e;
        c.flagHead = (c.flagHead + 1u) & 15u;
        ++c.flagCount;
    }
    inline void fmacFlags(Context &c, uint32_t mac, uint32_t status, uint32_t extraSticky)
    {
        pushFlags(c, FlagEntry{c.cyc + 4u, mac, status, extraSticky, 0u, 1u});
    }
    inline void clip(Context &c, uint32_t flags)
    {
        c.workingClip = ((c.workingClip << 6) | (flags & 0x3Fu)) & 0xFFFFFFu;
        pushFlags(c, FlagEntry{c.cyc + 4u, 0u, 0u, 0u, c.workingClip, 4u});
    }
    inline void fcset(Context &c, uint32_t imm)
    {
        c.workingClip = imm & 0xFFFFFFu;
        for (uint32_t i = 1; i <= c.flagCount; ++i)
        {
            FlagEntry &e = c.flags[(c.flagHead - i) & 15u];
            if ((e.kind & 4u) && e.ready == c.cyc + 4u)
                e.kind &= ~4u;
        }
        pushFlags(c, FlagEntry{c.cyc + 4u, 0u, 0u, 0u, c.workingClip, 4u});
    }
    inline void fsset(Context &c, uint32_t imm)
    {
        for (uint32_t i = 1; i <= c.flagCount; ++i)
        {
            FlagEntry &e = c.flags[(c.flagHead - i) & 15u];
            if ((e.kind & 1u) && e.ready == c.cyc + 4u)
                e.kind &= ~1u;
        }
        pushFlags(c, FlagEntry{c.cyc + 4u, 0u, imm & 0xFC0u, 0u, 0u, 2u});
    }
    inline void syncQ(Context &c)
    {
        if (c.qPending && c.qReady <= c.cyc)
        {
            c.s->q = c.qValue;
            c.s->status = (c.s->status & 0xFCFu) | c.qDi | (c.qDi << 6);
            c.qPending = false;
        }
    }
    inline void queueQ(Context &c, float v, uint32_t latency, uint32_t di)
    {
        c.qPending = true;
        c.qReady = c.cyc + latency;
        c.qValue = normRes(v);
        c.qDi = di & 0x30u;
    }
    inline void syncP(Context &c)
    {
        if (c.pPending && c.pReady <= c.cyc)
        {
            c.s->p = c.pValue;
            c.pPending = false;
        }
    }
    inline void queueP(Context &c, float v, uint32_t latency)
    {
        c.pPending = true;
        c.pReady = c.cyc + latency;
        c.pValue = normRes(v);
        c.efuResource = c.cyc + (latency > 0u ? latency - 1u : 0u);
    }
    inline void flushAll(Context &c)
    {
        commitFlags(c, 0xFFFFFFFFu);
        if (c.qPending)
        {
            c.qReady = c.cyc;
            syncQ(c);
        }
        if (c.pPending)
        {
            c.pReady = c.cyc;
            syncP(c);
        }
    }

    // ---- memory ----
    inline uint8_t *qw(Context &c, int32_t qword)
    {
        return c.mem + ((static_cast<uint32_t>(qword) * 16u) & c.memMask);
    }

    // ---- XGKICK: copy the GIF packet chain (until EOP) and submit it as PATH1 ----
    inline void xgkick(Context &c, uint32_t qwordAddress)
    {
        uint32_t src = (qwordAddress * 16u) & c.memMask;
        uint32_t out = 0;
        for (uint32_t guard = 0; guard < 4096u; ++guard)
        {
            if (out + 16u > sizeof(c.kickBuffer))
                break;
            uint64_t tag;
            std::memcpy(c.kickBuffer + out, c.mem + src, 16);
            std::memcpy(&tag, c.kickBuffer + out, 8);
            out += 16u;
            src = (src + 16u) & c.memMask;
            const uint32_t nloop = static_cast<uint32_t>(tag & 0x7FFFu);
            const uint32_t fmt = static_cast<uint32_t>((tag >> 58) & 3u);
            uint32_t nreg = static_cast<uint32_t>((tag >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;
            uint32_t qwords = fmt == 0u ? nloop * nreg : fmt == 1u ? (nloop * nreg + 1u) / 2u : nloop;
            if (out + qwords * 16u > sizeof(c.kickBuffer))
                break;
            for (uint32_t i = 0; i < qwords; ++i)
            {
                std::memcpy(c.kickBuffer + out, c.mem + src, 16);
                out += 16u;
                src = (src + 16u) & c.memMask;
            }
            if ((tag >> 15) & 1u)
                break;
        }
        if (c.xgkick && out != 0u)
            c.xgkick(c.xgkickUser, c.kickBuffer, out);
    }

    // ---- EFU helpers shared with the interpreter (ps2_vu1_lower.cpp) ----
    float efuAtan(float x);
    float efuSin(float x);
    float efuExp(float x);

    // ---- program registry and VU1 code-memory ownership (vu_aot.cpp) ----
    void registerPrograms(const Program *programs, uint32_t count);
    // Called after every VIF1 MPG upload into VU1 micro memory.
    void onVu1Upload(uint32_t destBytes, const uint8_t *data, uint32_t bytes);
    // Compiled program covering pc in VU1 micro memory, or nullptr.
    ProgramFn lookupVu1(uint32_t pc);
    // Same for VU0 micro memory (VIF0 MPG uploads; VU0 runs via VCALLMS/VIF0 MSCAL).
    void onVu0Upload(uint32_t destBytes, const uint8_t *data, uint32_t bytes);
    ProgramFn lookupVu0(uint32_t pc);
    bool enabled();

    // Pairs executed by the interpreter instead of compiled code ([0]=VU0, [1]=VU1).
    inline std::atomic<uint64_t> g_interpretedPairs[2]{};
    inline std::atomic<uint64_t> g_compiledCycles{0};
    // Entries at a pc the generator did not predict (see stallAll) that had to stall.
    inline std::atomic<uint64_t> g_entryStalls{0};

    // Entry at a pc the generator did not mark as an entry point: its static hazard analysis
    // assumed only fall-through arrival there, so wait until every in-flight VF/VI write is
    // visible. (Free on a fresh invocation, where all ready cycles are 0.)
    void noteEntryStall(uint32_t pc);
    inline void stallAll(Context &c, uint32_t pc)
    {
        uint32_t m = c.cyc;
        for (const auto &reg : c.vfReady)
            for (uint32_t v : reg)
                m = v > m ? v : m;
        for (uint32_t v : c.viReady)
            m = v > m ? v : m;
        if (m != c.cyc)
        {
            g_entryStalls.fetch_add(1, std::memory_order_relaxed);
            noteEntryStall(pc);
            c.cyc = m;
        }
    }
}
