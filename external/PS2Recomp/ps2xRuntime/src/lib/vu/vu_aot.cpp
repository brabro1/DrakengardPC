#include "runtime/vu_aot.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace vuaot
{
    // EFU approximations (moved from ps2_vu1_lower.cpp so interpreter and compiled code share them).
    float efuAtan(float value)
    {
        constexpr float coefficients[] = {
            0.999999344348907f, -0.333298563957214f, 0.199465364217758f, -0.13085337519646f,
            0.096420042216778f, -0.055909886956215f, 0.021861229091883f, -0.004054057877511f};
        constexpr float quarterPi = 0.785398185253143f;
        const float squared = value * value;
        float polynomial = coefficients[7];
        for (int index = 6; index >= 0; --index)
            polynomial = coefficients[index] + squared * polynomial;
        return quarterPi + value * polynomial;
    }

    float efuSin(float value)
    {
        constexpr float coefficients[] = {1.0f, -0.166666567325592f, 0.008333025500178f, -0.000198074136279f,
                                          0.000002601886990f};
        const float squared = value * value;
        float polynomial = coefficients[4];
        for (int index = 3; index >= 0; --index)
            polynomial = coefficients[index] + squared * polynomial;
        return value * polynomial;
    }

    float efuExp(float value)
    {
        constexpr float coefficients[] = {0.249998688697815f, 0.031257584691048f, 0.002591371303424f,
                                          0.000171562001924f, 0.000005430199963f, 0.000000690600018f};
        float polynomial = coefficients[5];
        for (int index = 4; index >= 0; --index)
            polynomial = coefficients[index] + value * polynomial;
        polynomial = 1.0f + value * polynomial;
        polynomial *= polynomial;
        polynomial *= polynomial;
        return polynomial != 0.0f ? 1.0f / polynomial : std::numeric_limits<float>::max();
    }

    namespace
    {
        constexpr uint32_t kVu1Pairs = 0x4000u / 8u;

        std::vector<Program> &programs()
        {
            static std::vector<Program> list;
            return list;
        }

        struct Owner
        {
            ProgramFn fn = nullptr;
            int32_t program = -1;
        };
        Owner g_maps[2][kVu1Pairs];
        // Payload of each program once an upload matched its hash: later uploads are compared
        // with memcmp (vectorised) instead of re-hashing byte by byte.
        std::vector<std::vector<uint8_t>> g_verified;
        uint64_t g_matched = 0, g_unmatched[2] = {0, 0};

        void onUpload(uint32_t unit, uint32_t destBytes, const uint8_t *data, uint32_t bytes);
    }

    void registerPrograms(const Program *list, uint32_t count)
    {
        auto &all = programs();
        all.insert(all.end(), list, list + count);
    }

    bool enabled()
    {
        static const bool on = []
        {
            const char *value = std::getenv("PS2X_VU_AOT");
            const bool disabled = value && value[0] == '0';
            if (!programs().empty())
                std::cerr << "[vu:aot] " << programs().size() << " compiled VU1 programs"
                          << (disabled ? " (disabled by PS2X_VU_AOT=0)" : "") << std::endl;
            return !disabled && !programs().empty();
        }();
        return on;
    }

    void onVu1Upload(uint32_t destBytes, const uint8_t *data, uint32_t bytes) { onUpload(1u, destBytes, data, bytes); }
    void onVu0Upload(uint32_t destBytes, const uint8_t *data, uint32_t bytes) { onUpload(0u, destBytes, data, bytes); }

    namespace
    {
    void onUpload(uint32_t unit, uint32_t destBytes, const uint8_t *data, uint32_t bytes)
    {
        const uint32_t pairsInMemory = unit == 1u ? kVu1Pairs : kVu1Pairs / 4u;
        Owner *map = g_maps[unit];
        if (programs().empty() || bytes == 0u)
            return;
        const auto &all = programs();
        if (g_verified.size() != all.size())
            g_verified.resize(all.size());
        int32_t match = -1;
        uint64_t hash = 0;
        bool hashed = false;
        for (size_t i = 0; i < all.size() && match < 0; ++i)
        {
            if (all[i].base != destBytes || all[i].bytes != bytes)
                continue;
            const std::vector<uint8_t> &known = g_verified[i];
            if (!known.empty())
            {
                if (std::memcmp(known.data(), data, bytes) == 0)
                    match = static_cast<int32_t>(i);
                continue;
            }
            if (!hashed)
            {
                hash = fnv1a(data, bytes);
                hashed = true;
            }
            if (all[i].hash == hash)
            {
                match = static_cast<int32_t>(i);
                g_verified[i].assign(data, data + bytes);
            }
        }
        const uint32_t first = destBytes / 8u;
        const uint32_t count = bytes / 8u;
        // Re-upload of a program that is still mapped: a partial overwrite always unmaps a whole
        // program, so an intact first pair means the whole range is intact.
        if (match >= 0 && map[first % pairsInMemory].program == match)
        {
            ++g_matched;
            return;
        }
        // A program partly overwritten is no longer valid anywhere (its internal jumps would
        // reach the new code through stale labels).
        for (uint32_t i = 0; i < count; ++i)
        {
            const int32_t old = map[(first + i) % pairsInMemory].program;
            if (old >= 0 && old != match)
            {
                const Program &p = all[static_cast<size_t>(old)];
                for (uint32_t j = 0; j < p.bytes / 8u; ++j)
                {
                    Owner &o = map[(p.base / 8u + j) % pairsInMemory];
                    if (o.program == old)
                        o = Owner{};
                }
            }
        }
        for (uint32_t i = 0; i < count; ++i)
            map[(first + i) % pairsInMemory] =
                match >= 0 ? Owner{all[static_cast<size_t>(match)].fn, match} : Owner{};
        // Diagnostic: PS2X_VU_UPLOAD_DUMP=<dir> saves each distinct unmatched payload (game data).
        static const char *s_dumpDir = std::getenv("PS2X_VU_UPLOAD_DUMP");
        if (match < 0 && s_dumpDir && *s_dumpDir)
        {
            char name[600];
            std::snprintf(name, sizeof(name), "%s/vu%u_%04x_%u_%016llx.bin", s_dumpDir, unit, destBytes, bytes,
                          static_cast<unsigned long long>(fnv1a(data, bytes)));
            if (FILE *existing = std::fopen(name, "rb"))
                std::fclose(existing);
            else if (FILE *f = std::fopen(name, "wb"))
            {
                std::fwrite(data, 1, bytes, f);
                std::fclose(f);
            }
        }
        if (match >= 0)
            ++g_matched;
        else if (++g_unmatched[unit] <= 8u)
            std::cerr << "[vu:aot] VU" << unit << " MPG to 0x" << std::hex << destBytes << " (" << std::dec << bytes
                      << " bytes) has no compiled program; it will be interpreted" << std::endl;
    }

    }

    ProgramFn lookupVu1(uint32_t pc)
    {
        return g_maps[1][(pc / 8u) % kVu1Pairs].fn;
    }

    ProgramFn lookupVu0(uint32_t pc)
    {
        return g_maps[0][(pc / 8u) % (kVu1Pairs / 4u)].fn;
    }
}

namespace vuaot
{
    // Diagnostics: the first stall at each unpredicted entry pc (PS2X_VU_AOT_ENTRY_LOG=1).
    void noteEntryStall(uint32_t pc)
    {
        static const bool on = [] { const char *v = std::getenv("PS2X_VU_AOT_ENTRY_LOG"); return v && *v == '1'; }();
        static bool seen[0x4000 / 8]{};
        if (!on || seen[(pc / 8) & 0x7FFu])
            return;
        seen[(pc / 8) & 0x7FFu] = true;
        std::fprintf(stderr, "[vu-aot:entry-stall] pc=0x%04x\n", pc);
    }
}
