#include "runtime/vu_aot.h"

#include <cstdlib>
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
        Owner g_vu1Map[kVu1Pairs];
        uint64_t g_matched = 0, g_unmatched = 0;
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

    void onVu1Upload(uint32_t destBytes, const uint8_t *data, uint32_t bytes)
    {
        if (programs().empty() || bytes == 0u)
            return;
        const uint64_t hash = fnv1a(data, bytes);
        int32_t match = -1;
        const auto &all = programs();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (all[i].base == destBytes && all[i].bytes == bytes && all[i].hash == hash)
            {
                match = static_cast<int32_t>(i);
                break;
            }
        }
        const uint32_t first = destBytes / 8u;
        const uint32_t count = bytes / 8u;
        // A program partly overwritten is no longer valid anywhere (its internal jumps would
        // reach the new code through stale labels).
        for (uint32_t i = 0; i < count; ++i)
        {
            const int32_t old = g_vu1Map[(first + i) % kVu1Pairs].program;
            if (old >= 0 && old != match)
            {
                const Program &p = all[static_cast<size_t>(old)];
                for (uint32_t j = 0; j < p.bytes / 8u; ++j)
                {
                    Owner &o = g_vu1Map[(p.base / 8u + j) % kVu1Pairs];
                    if (o.program == old)
                        o = Owner{};
                }
            }
        }
        for (uint32_t i = 0; i < count; ++i)
            g_vu1Map[(first + i) % kVu1Pairs] =
                match >= 0 ? Owner{all[static_cast<size_t>(match)].fn, match} : Owner{};
        if (match >= 0)
            ++g_matched;
        else if (++g_unmatched <= 8u)
            std::cerr << "[vu:aot] MPG to 0x" << std::hex << destBytes << " (" << std::dec << bytes
                      << " bytes) has no compiled program; it will be interpreted" << std::endl;
    }

    ProgramFn lookupVu1(uint32_t pc)
    {
        return g_vu1Map[(pc / 8u) % kVu1Pairs].fn;
    }
}
