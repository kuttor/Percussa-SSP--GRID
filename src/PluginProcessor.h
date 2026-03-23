#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginParameters.h"
#include "GridEngine.h"

namespace grid {

class PluginProcessor : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // SSP callbacks — track which inputs are patched
    void onInputChanged(int ch, bool enabled) {
        if (ch >= 0 && ch < kNumInputs) inputEnabled_[ch] = enabled;
    }
    void onOutputChanged(int, bool) {}

    // Access
    GridEngine& getEngine() { return engine_; }
    const juce::String& getSampleRootPath() const { return sampleRootPath_; }

private:
    static juce::AudioProcessor::BusesProperties getDefaultBusesProperties();

    GridEngine engine_;
    juce::String sampleRootPath_;

    // Input tracking — only read CVs from enabled (patched) inputs
    bool inputEnabled_[kNumInputs] = {};
    bool gateHigh_[kNumPads] = {};  // edge detection per pad

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace grid
