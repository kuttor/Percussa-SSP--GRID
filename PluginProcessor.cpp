#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace grid {

PluginProcessor::PluginProcessor()
    : AudioProcessor(getBusesProperties())
{
    sampleRootPath_ = findSSPSamplePath();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Read gate CVs ────────────────────────────────────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = trigChannel(pad);
        if (ch >= numChannels) continue;

        bool triggered = false;
        for (int s = 0; s < numSamples; ++s)
        {
            bool high = (buffer.getSample(ch, s) > kTrigThreshold);
            if (high && !gateHigh_[pad]) {
                triggered = true;
                break;
            }
            gateHigh_[pad] = high;
        }
        gateHigh_[pad] = (buffer.getSample(ch, numSamples - 1) > kTrigThreshold);

        if (triggered)
            engine_.trigger(pad);
    }

    // ── Read pitch CVs ───────────────────────────────────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = pitchChannel(pad);
        if (ch >= numChannels) continue;

        float voct = buffer.getSample(ch, numSamples - 1);
        if (std::abs(voct) > 0.01f)
            engine_.getSlot(pad).setPitchSemitones(voct * 12.0f);
    }

    // ── Read clock input — track BPM ─────────────────────────────────────
    if (I_CLOCK < numChannels)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            bool high = (buffer.getSample(I_CLOCK, s) > kTrigThreshold);
            if (high && !clockHigh_) {
                // Rising edge — compute BPM from interval
                if (clockActive_ && samplesSinceClock_ > 0) {
                    float intervalSecs = (float)samplesSinceClock_ / (float)sampleRate_;
                    float newBPM = 60.0f / intervalSecs;
                    // Smooth to avoid jitter (simple IIR)
                    if (bpm_ < 1.0f)
                        bpm_ = newBPM;
                    else
                        bpm_ = bpm_ * 0.7f + newBPM * 0.3f;
                }
                clockActive_ = true;
                samplesSinceClock_ = 0;
            }
            clockHigh_ = high;
            samplesSinceClock_++;
        }
        // If no clock for 2 seconds, mark inactive
        if (samplesSinceClock_ > (int)(sampleRate_ * 2.0))
            clockActive_ = false;
    }

    // ── Clear ONLY output channels, then write audio ─────────────────────
    if (O_LEFT < numChannels)  buffer.clear(O_LEFT, 0, numSamples);
    if (O_RIGHT < numChannels) buffer.clear(O_RIGHT, 0, numSamples);

    float* outL = (O_LEFT < numChannels)  ? buffer.getWritePointer(O_LEFT)  : nullptr;
    float* outR = (O_RIGHT < numChannels) ? buffer.getWritePointer(O_RIGHT) : nullptr;

    if (outL && outR)
        engine_.process(outL, outR, numSamples);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace grid
