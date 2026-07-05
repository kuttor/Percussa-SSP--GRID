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
        slots_[i].advanceRetriggerGuard(numSamples);

        // Per-pad outputs must be silenced every block regardless of mute
        // state, otherwise stale data leaks out the physical pad output
        // when the slot is muted (the SSP host reads the buffer either way).
        if (padOutL_[i] != nullptr) {
            std::memset(padOutL_[i], 0, sizeof(float) * static_cast<size_t>(numSamples));
            std::memset(padOutR_[i], 0, sizeof(float) * static_cast<size_t>(numSamples));
        }

        if (muted_[i]) continue;

        // MIDI-CV pads (2.4.9): don't render audio. The pad's per-pad output
        // channel becomes a gate output written by PluginProcessor::processBlock.
        if (slots_[i].getMode() == PadMode::MidiCV) continue;

        if (padOutL_[i] != nullptr) {
            // Render into per-pad temp buffer (already zeroed above)
            slots_[i].process(padOutL_[i], padOutR_[i], numSamples);

            // Add to stereo mix if enabled
            if (slots_[i].getSendToMix()) {
                for (int s = 0; s < numSamples; ++s) {
                    outL[s] += padOutL_[i][s];
                    outR[s] += padOutR_[i][s];
                }
            }
        } else {
            // No per-pad routing — direct to mix (original path)
            slots_[i].process(outL, outR, numSamples);
        }
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

void GridEngine::triggerWithChokeAndVelocityAndOffset(int slot, float vel, int sampleOffset)
{
    if (slot < 0 || slot >= kNumPads) return;

    ChokeGroup grp = slots_[slot].getChokeGroup();
    if (grp != ChokeGroup::None) {
        for (int i = 0; i < kNumPads; ++i) {
            if (i != slot && slots_[i].getChokeGroup() == grp && slots_[i].isPlaying())
                slots_[i].stop();
        }
    }
    slots_[slot].triggerWithVelocityAndOffset(vel, sampleOffset);
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
