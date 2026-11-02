#pragma once

namespace dkg::debug
{
    // Starts the sampling profiler when DKG_PROFILE is set (see sampler.cpp).
    void startSampler();
    // Stops sampling and writes the histogram; safe to call when not started.
    void stopSamplerAndWrite();
}
