#include "GridEngine.h"

namespace grid {

void GridEngine::prepare(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
}

void GridEngine::process(float* outL, float* outR, int numSamples)
{
    // Clear output
    std::memset(outL, 0, sizeof(float) * static_cast<size_t>(numSamples));
    std::memset(outR, 0, sizeof(float) * static_cast<size_t>(numSamples));

    // Sum all playing slots
    for (int i = 0; i < kNumPads; ++i)
        slots_[i].process(outL, outR, numSamples);
}

void GridEngine::trigger(int slot)
{
    if (slot >= 0 && slot < kNumPads)
        slots_[slot].trigger();
}

void GridEngine::stop(int slot)
{
    if (slot >= 0 && slot < kNumPads)
        slots_[slot].stop();
}

void GridEngine::stopAll()
{
    for (int i = 0; i < kNumPads; ++i)
        slots_[i].stop();
}

} // namespace grid
