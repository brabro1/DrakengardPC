// Function trace hooks for investigating guest behaviour without regenerating code.
//
//   DKG_TRACE_FUNCS=0x2ee7d0,0x2ef3dc   addresses of recompiled functions to wrap
//   DKG_TRACE_LIMIT=64                  log lines per function (default 64)
//   DKG_TRACE_SKIP=0                    calls to skip per function before logging
//
// Each wrapped function logs its argument registers and caller on entry and, when
// it returns to its caller, $v0. Every guest call goes through the runtime's
// function table, so replacing the table entry also catches direct JAL calls.

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <sstream>
#include <string>

namespace dkg::debug
{
    namespace
    {
        constexpr size_t kMaxHooks = 32;

        struct Hook
        {
            uint32_t address = 0;
            PS2Runtime::RecompiledFunction original = nullptr;
            uint32_t logged = 0;
            uint64_t calls = 0;
        };

        std::array<Hook, kMaxHooks> g_hooks{};
        uint32_t g_limit = 64;
        uint64_t g_skip = 0;
        std::vector<uint32_t> g_watch; // DKG_TRACE_MEM: guest words printed with each entry

        void printWatch(const uint8_t *rdram)
        {
            if (g_watch.empty())
                return;
            std::cout << "        mem";
            for (uint32_t address : g_watch)
            {
                uint32_t value = 0;
                std::memcpy(&value, rdram + (address & 0x01FFFFFCu), sizeof(value));
                std::cout << std::hex << " [0x" << address << "]=0x" << value << std::dec << "("
                          << static_cast<int32_t>(value) << ")";
            }
            std::cout << std::endl;
        }

        template <size_t N>
        void trampoline(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            Hook &hook = g_hooks[N];
            const uint32_t ra = GPR_U32(ctx, 31);
            const bool log = hook.calls++ >= g_skip && hook.logged < g_limit;
            if (log)
            {
                ++hook.logged;
                std::cout << std::hex << "[trace] 0x" << hook.address << " ra=0x" << ra << " a0=0x" << GPR_U32(ctx, 4)
                          << " a1=0x" << GPR_U32(ctx, 5) << " a2=0x" << GPR_U32(ctx, 6) << " a3=0x" << GPR_U32(ctx, 7)
                          << " t0=0x" << GPR_U32(ctx, 8) << " t1=0x" << GPR_U32(ctx, 9)
                          << " t2=0x" << GPR_U32(ctx, 10) << " t3=0x" << GPR_U32(ctx, 11) << std::dec << std::endl;
                printWatch(rdram);
            }
            hook.original(rdram, ctx, runtime);
            if (log && ctx->pc == ra)
            {
                std::cout << std::hex << "[trace] 0x" << hook.address << " -> v0=0x" << GPR_U32(ctx, 2) << std::dec
                          << " (" << static_cast<int32_t>(GPR_U32(ctx, 2)) << ")" << std::endl;
            }
        }

        template <size_t... I>
        constexpr std::array<PS2Runtime::RecompiledFunction, sizeof...(I)> makeTrampolines(std::index_sequence<I...>)
        {
            return {&trampoline<I>...};
        }

        constexpr auto kTrampolines = makeTrampolines(std::make_index_sequence<kMaxHooks>{});
    }

    void installTraceHooks(PS2Runtime &runtime)
    {
        const char *list = std::getenv("DKG_TRACE_FUNCS");
        if (!list || !*list)
            return;
        if (const char *limit = std::getenv("DKG_TRACE_LIMIT"))
            g_limit = static_cast<uint32_t>(std::strtoul(limit, nullptr, 0));
        if (const char *skip = std::getenv("DKG_TRACE_SKIP"))
            g_skip = std::strtoull(skip, nullptr, 0);
        if (const char *mem = std::getenv("DKG_TRACE_MEM"))
        {
            std::stringstream words(mem);
            std::string word;
            while (std::getline(words, word, ','))
                g_watch.push_back(static_cast<uint32_t>(std::strtoul(word.c_str(), nullptr, 16)));
        }

        std::stringstream items(list);
        std::string item;
        size_t next = 0;
        while (std::getline(items, item, ',') && next < kMaxHooks)
        {
            const uint32_t address = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
            PS2Runtime::RecompiledFunction original = runtime.lookupFunction(address);
            if (!original || !runtime.hasFunction(address))
            {
                std::cout << "[trace] no recompiled function at 0x" << std::hex << address << std::dec << std::endl;
                continue;
            }
            g_hooks[next] = Hook{address, original, 0, 0};
            runtime.replaceFunction(address, kTrampolines[next]);
            std::cout << "[trace] hooked 0x" << std::hex << address << std::dec << std::endl;
            ++next;
        }
    }
}
