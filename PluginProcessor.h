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

    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }

    // SSP callbacks — track which inputs/outputs are patched
    void onInputChanged(int n, bool enabled) {
        if (n >= 0 && n < I_MAX) inputEnabled_[n] = enabled;
    }
    void onOutputChanged(int n, bool enabled) {
        if (n >= 0 && n < O_MAX) outputEnabled_[n] = enabled;
    }

    // Bear's pattern
    bool isInputEnabled(int n) const { return n >= 0 && n < I_MAX && inputEnabled_[n]; }
    bool isOutputEnabled(int n) const { return n >= 0 && n < O_MAX && outputEnabled_[n]; }

    // Access
    GridEngine& getEngine() { return engine_; }
    const juce::String& getSampleRootPath() const { return sampleRootPath_; }
    float getBPM() const { return bpm_; }
    bool hasClockInput() const { return clockActive_; }

    // Mono bus layout (Bear's pattern) — must be inside AudioProcessor subclass
    static BusesProperties getBusesProperties() {
        BusesProperties props;
        for (int i = 0; i < I_MAX; ++i)
            props.addBus(true, inputBusName(i), juce::AudioChannelSet::mono());
        for (int i = 0; i < O_MAX; ++i)
            props.addBus(false, outputBusName(i), juce::AudioChannelSet::mono());
        return props;
    }

    // Expose for SSPApi descriptor
    static BusesProperties getDefaultBusesProperties() {
        return getBusesProperties();
    }

private:
    GridEngine engine_;
    juce::String sampleRootPath_;
    double sampleRate_ = 48000.0;

    bool inputEnabled_[I_MAX] = {};
    bool outputEnabled_[O_MAX] = {};
    bool gateHigh_[kNumPads] = {};

    // Clock / BPM tracking
    bool clockHigh_ = false;
    bool clockActive_ = false;
    int samplesSinceClock_ = 0;
    float bpm_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace grid
