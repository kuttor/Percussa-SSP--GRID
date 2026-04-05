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
    void triggerWithChoke(int slot);
    void triggerWithChokeAndVelocity(int slot, float vel);
    void triggerWithChokeAndOffset(int slot, int sampleOffset);
    void triggerWithChokeAndVelocityAndOffset(int slot, float vel, int sampleOffset);
    void stop(int slot);
    void stopAll();

    // Mute (with fade support)
    bool isMuted(int slot) const { return muted_[juce::jlimit(0, kNumPads - 1, slot)]; }
    void setMuted(int slot, bool m) {
        int i = juce::jlimit(0, kNumPads - 1, slot);
        muted_[i] = m;
        slots_[i].setMuteTarget(m ? 0.0f : 1.0f);
    }
    void toggleMute(int slot) {
        int i = juce::jlimit(0, kNumPads - 1, slot);
        muted_[i] = !muted_[i];
        slots_[i].setMuteTarget(muted_[i] ? 0.0f : 1.0f);
    }
    void setMuteFadeMs(float ms) {
        for (int i = 0; i < kNumPads; ++i) slots_[i].setMuteFadeMs(ms);
    }

    // Solo: mute everything except soloed pads
    void applySolo(const bool* soloed) {
        for (int i = 0; i < kNumPads; ++i) {
            muted_[i] = !soloed[i];
            slots_[i].setMuteTarget(soloed[i] ? 1.0f : 0.0f);
        }
    }
    void clearSolo() {
        for (int i = 0; i < kNumPads; ++i) {
            muted_[i] = false;
            slots_[i].setMuteTarget(1.0f);
        }
    }

    SampleSlot& getSlot(int index) { return slots_[juce::jlimit(0, kNumPads - 1, index)]; }
    const SampleSlot& getSlot(int index) const { return slots_[juce::jlimit(0, kNumPads - 1, index)]; }

private:
    SampleSlot slots_[kNumPads];
    bool muted_[kNumPads] = {};
    double sampleRate_ = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridEngine)
};

} // namespace grid
