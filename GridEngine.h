#pragma once
#include "SampleSlot.h"

namespace grid {

class GridEngine {
public:
    GridEngine() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void process(float* outL, float* outR, int numSamples);

    void trigger(int slot);
    void stop(int slot);
    void stopAll();

    SampleSlot& getSlot(int index) { return slots_[juce::jlimit(0, kNumPads - 1, index)]; }
    const SampleSlot& getSlot(int index) const { return slots_[juce::jlimit(0, kNumPads - 1, index)]; }

private:
    SampleSlot slots_[kNumPads];
    double sampleRate_ = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridEngine)
};

} // namespace grid
