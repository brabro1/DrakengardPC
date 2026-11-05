// Per-build function bindings for Drakengard (USA) SLUS-20732 v1.00.
//
// Each binding replaces one guest SDK function with an existing native runtime
// handler. Every entry must say why the guest code cannot run as-is and why the
// native handler is equivalent for this game. Addresses are only valid for the
// ELF verified in main.cpp (sha1 652d95f1...).

#include "game_overrides.h"
#include "ps2_runtime.h"

#include <cstdint>
#include <iostream>

#if defined(DRAKENGARD_HAVE_VU1_AOT)
void drakengardRegisterVuPrograms();
#endif

namespace dkg::debug
{
    void installTraceHooks(PS2Runtime &runtime);
}

namespace
{
    struct Binding
    {
        uint32_t address;
        const char *guestName;
        const char *handler;
        const char *reason;
    };

    constexpr Binding kBindings[] = {
        {0x00306428u, "sceCdLayerSearchFile", "sceCdSearchFile",
         "guest libcdvd binds the CDVDFSV search RPC (SID 0x80000597), which the IOP "
         "BIOS provides and no disc module does, and spins forever; the disc is "
         "single-layer DVD-5 so the layer argument is always 0"},
    };

    void applyDrakengard(PS2Runtime &runtime)
    {
        for (const Binding &b : kBindings)
        {
            const bool ok = ps2_game_overrides::bindAddressHandler(runtime, b.address, b.handler);
            std::cout << "[dkg-override] " << (ok ? "bound " : "FAILED to bind ") << b.guestName << "@0x"
                      << std::hex << b.address << std::dec << " -> native " << b.handler << " (" << b.reason << ")"
                      << std::endl;
        }
#if defined(DRAKENGARD_HAVE_VU1_AOT)
        // VU1 microprograms compiled ahead of time (tools/vu_recomp.py).
        drakengardRegisterVuPrograms();
#endif
        // Debug-only wrappers (DKG_TRACE_FUNCS); installed last so they wrap the
        // bindings above as well.
        dkg::debug::installTraceHooks(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Drakengard (USA) SLUS-20732", "SLUS_207.32", 0x00120008u, 0u, applyDrakengard);
