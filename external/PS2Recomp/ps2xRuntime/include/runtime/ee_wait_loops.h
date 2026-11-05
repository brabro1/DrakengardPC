#pragma once

#include <cstdint>

// Game-registered EE wait loops. Recompiled code charges a fixed small cycle count at each
// loop checkpoint; a loop that only polls for an interrupt-set flag then iterates far more
// often than on hardware before emulated time reaches the interrupt. Registering the loop's
// checkpoint pc charges `cycles` per pass there instead (bounded by the next scheduled event).
namespace ps2x
{
    void registerEeWaitLoop(uint32_t checkpointPc, uint32_t cyclesPerPass);
}
