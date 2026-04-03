#include "GridEngine.h"

namespace grid {

void GridEngine::prepare(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    for (int i = 0; i < kNumPads; ++i)
        slots_[i].setOutputSampleRate(sampleRate);
}

void GridEngine::process(float* outL, float* outR, int numSamples)
{
    std::memset(outL, 0, sizeof(float) * static_cast<size_t>(numSamples));
    std::memset(outR, 0, sizeof(float) * static_cast<size_t>(numSamples));

    for (int i = 0; i < kNumPads; ++i) {
        slots_[i].advanceRetriggerGuard(numSamples);  // always advance, even muted
        if (!muted_[i])
            slots_[i].process(outL, outR, numSamples);
    }
}

void GridEngine::trigger(int slot)
{
    if (slot >= 0 && slot < kNumPads)
        slots_[slot].trigger();
}

void GridEngine::forceTrigger(int slot)
{
    if (slot >= 0 && slot < kNumPads) {
        slots_[slot].trigger();
    }
}

void GridEngine::triggerWithChoke(int slot)
{
    if (slot < 0 || slot >= kNumPads) return;

    ChokeGroup grp = slots_[slot].getChokeGroup();
    if (grp != ChokeGroup::None) {
        for (int i = 0; i < kNumPads; ++i) {
            if (i != slot && slots_[i].getChokeGroup() == grp && slots_[i].isPlaying())
                slots_[i].stop();
        }
    }
    slots_[slot].trigger();
}

void GridEngine::triggerWithChokeAndVelocity(int slot, float vel)
{
    if (slot < 0 || slot >= kNumPads) return;

    ChokeGroup grp = slots_[slot].getChokeGroup();
    if (grp != ChokeGroup::None) {
        for (int i = 0; i < kNumPads; ++i) {
            if (i != slot && slots_[i].getChokeGroup() == grp && slots_[i].isPlaying())
                slots_[i].stop();
        }
    }
    slots_[slot].triggerWithVelocity(vel);
}

void GridEngine::triggerWithChokeAndOffset(int slot, int sampleOffset)
{
    if (slot < 0 || slot >= kNumPads) return;

    ChokeGroup grp = slots_[slot].getChokeGroup();
    if (grp != ChokeGroup::None) {
        for (int i = 0; i < kNumPads; ++i) {
            if (i != slot && slots_[i].getChokeGroup() == grp && slots_[i].isPlaying())
                slots_[i].stop();
        }
    }
    slots_[slot].triggerWithOffset(sampleOffset);
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
