#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace grid {

juce::AudioProcessor::BusesProperties PluginProcessor::getDefaultBusesProperties()
{
    return BusesProperties()
        #if SSP_TARGET
        .withInput ("Input",  juce::AudioChannelSet::discreteChannels(kNumInputs),  true)
        .withOutput("Output", juce::AudioChannelSet::discreteChannels(kNumOutputs), true)
        #else
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        #endif
        ;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(getDefaultBusesProperties())
{
    sampleRootPath_ = findSSPSamplePath();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // SSP buffer layout: outputs first (0,1), then inputs (2+)
    const int inputOffset = kNumOutputs;

    // ── Read gate CVs: scan ALL samples for rising edge ───────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int inputIdx = IN_GATE_1 + pad;
        int bufCh = inputOffset + inputIdx;
        if (!inputEnabled_[inputIdx] || bufCh >= numChannels) continue;

        const float* gateIn = buffer.getReadPointer(bufCh);
        bool triggered = false;

        for (int s = 0; s < numSamples; ++s)
        {
            bool high = (gateIn[s] > 0.1f);
            if (high && !gateHigh_[pad]) {
                triggered = true;
                break;
            }
            gateHigh_[pad] = high;
        }

        gateHigh_[pad] = (gateIn[numSamples - 1] > 0.1f);

        if (triggered)
            engine_.trigger(pad);
    }

    // ── Read pitch CVs: 1V/oct, 0V = original pitch ─────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int inputIdx = IN_PITCH_1 + pad;
        int bufCh = inputOffset + inputIdx;
        if (!inputEnabled_[inputIdx] || bufCh >= numChannels) continue;

        const float* pitchIn = buffer.getReadPointer(bufCh);
        float voct = pitchIn[numSamples - 1];
        float semitones = voct * 12.0f;
        engine_.getSlot(pad).setPitchSemitones(semitones);
    }

    // ── Clear and write output (channels 0,1) ────────────────────────────
    buffer.clear();

    float* outL = buffer.getWritePointer(0);
    float* outR = (numChannels > 1) ? buffer.getWritePointer(1) : outL;

    engine_.process(outL, outR, numSamples);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace grid
