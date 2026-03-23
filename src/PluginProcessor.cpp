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

    // ── Read gate CVs: rising edge triggers pad ──────────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int gateCh = IN_GATE_1 + pad;
        if (!inputEnabled_[gateCh] || gateCh >= numChannels) continue;

        const float* gateIn = buffer.getReadPointer(gateCh);
        // Check last sample in block for gate state
        float gateVal = gateIn[numSamples - 1];
        bool high = (gateVal > 0.5f);

        if (high && !gateHigh_[pad])
            engine_.trigger(pad);  // rising edge

        gateHigh_[pad] = high;
    }

    // ── Read pitch CVs: 1V/oct, 0V = original pitch ─────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int pitchCh = IN_PITCH_1 + pad;
        if (!inputEnabled_[pitchCh] || pitchCh >= numChannels) continue;

        const float* pitchIn = buffer.getReadPointer(pitchCh);
        float voct = pitchIn[numSamples - 1];  // use last sample
        // Convert V/Oct to semitones (1V = 12 semitones)
        float semitones = voct * 12.0f;
        engine_.getSlot(pad).setPitchSemitones(semitones);
    }

    // ── Clear and write output ───────────────────────────────────────────
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
