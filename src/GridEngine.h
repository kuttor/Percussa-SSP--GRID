#pragma once
#include "SampleSlot.h"

namespace grid {

class GridEngine {
public:
    GridEngine() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void process(float* outL, float* outR, int numSamples);

    void trigger(int slot);
    void forceTrigger(int slot);
    void triggerWithChoke(int slot);  // trigger + stop other pads in same choke group
    void stop(int slot);
    void stopAll();

    // Mute
    bool isMuted(int slot) const { return muted_[juce::jlimit(0, kNumPads - 1, slot)]; }
    void setMuted(int slot, bool m) { muted_[juce::jlimit(0, kNumPads - 1, slot)] = m; }
    void toggleMute(int slot) { int i = juce::jlimit(0, kNumPads - 1, slot); muted_[i] = !muted_[i]; }

    SampleSlot& getSlot(int index) { return slots_[juce::jlimit(0, kNumPads - 1, index)]; }
    const SampleSlot& getSlot(int index) const { return slots_[juce::jlimit(0, kNumPads - 1, index)]; }

private:
    SampleSlot slots_[kNumPads];
    bool muted_[kNumPads] = {};
    double sampleRate_ = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridEngine)
};

} // namespace grid
