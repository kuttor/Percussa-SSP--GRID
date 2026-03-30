#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PluginParameters.h"
#include "GridEngine.h"

namespace grid {

class PluginProcessor : public juce::AudioProcessor,
                        public juce::MidiInputCallback {
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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
    int getClockDiv() const { return clockDiv_; }
    void setClockDiv(int d) { clockDiv_ = juce::jlimit(1, 8, d); clockPulseCount_ = 0; }

    // Recording
    enum class RecState { Idle, Armed, Recording };
    void armRecord(int pad);
    void disarmRecord();
    void stopRecord();
    RecState getRecState() const { return recState_; }
    int getRecPad() const { return recPad_; }
    RecMode getRecMode() const { return recMode_; }
    void setRecMode(RecMode m) { recMode_ = m; }
    int getRecMaxLenIdx() const { return recMaxLenIdx_; }
    void setRecMaxLenIdx(int i) { recMaxLenIdx_ = juce::jlimit(0, kNumRecLengths - 1, i); }
    void setRecTargetPad(int p) { recTargetPad_ = juce::jlimit(0, kNumPads - 1, p); }
    float getRecProgress() const;

    // MIDI
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg) override;
    juce::StringArray getMidiDeviceNames() const;
    juce::String getMidiDeviceName() const { return midiDeviceName_; }
    void setMidiDevice(const juce::String& name);
    void closeMidiDevice();
    bool isMidiClockEnabled() const { return midiClockEnabled_; }
    void setMidiClockEnabled(bool b) { midiClockEnabled_ = b; }

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
    bool resetHigh_ = false;
    float bpm_ = 0.0f;
    int clockDiv_ = 1;          // pulses per beat (1,2,4,8)
    int clockPulseCount_ = 0;   // counts pulses for division
    int samplesSinceDiv_ = 0;   // samples since last divided beat

    // Recording
    RecState recState_ = RecState::Idle;
    RecMode recMode_ = RecMode::Instant;
    int recPad_ = 0;
    int recTargetPad_ = 0;
    int recMaxLenIdx_ = 5;  // default 60s
    juce::AudioBuffer<float> recBuffer_;
    int recPos_ = 0;
    int recMaxSamples_ = 0;
    bool recGateHigh_ = false;
    int recSilenceCount_ = 0;
    static constexpr float kRecThreshold = 0.02f;
    static constexpr float kSilenceThreshold = 0.005f;
    static constexpr int kSilenceTimeoutSamples = 48000 * 3;  // 3s at 48k

    void finalizeRecording();

    // MIDI state (direct device access, Bear's pattern)
    std::unique_ptr<juce::MidiInput> midiInDevice_;
    juce::String midiDeviceName_;
    bool midiClockEnabled_ = false;
    int midiClockCount_ = 0;         // counts 24 PPQN
    int midiClockSamples_ = 0;       // samples since last beat (24 clocks)
    bool midiTrigPending_[kNumPads] = {};
    float midiVelocity_[kNumPads] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace grid
