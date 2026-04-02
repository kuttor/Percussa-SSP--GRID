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
    void setClockDiv(int d) { clockDiv_ = juce::jlimit(-3, 3, d); clockPulseCount_ = 0; }
    // clockDiv: -3=*8, -2=*4, -1=*2, 0=/1, 1=/2, 2=/4, 3=/8
    float getClockMultiplier() const {
        return std::pow(2.0f, -(float)clockDiv_);  // -3→8, -2→4, -1→2, 0→1, 1→0.5, 2→0.25, 3→0.125
    }
    int getClockPulsesPerBeat() const {
        return clockDiv_ >= 0 ? (1 << clockDiv_) : 1;  // positive = more pulses per beat, negative = 1
    }

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
    void refreshMidiDevices();
    juce::String getMidiDeviceName() const { return midiDeviceName_; }
    void showTickerPublic(const juce::String& msg) { showTicker(msg); }
    void setMidiDevice(const juce::String& name);
    void closeMidiDevice();
    bool isMidiClockEnabled() const { return midiClockEnabled_; }
    void setMidiClockEnabled(bool b);

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
    int clockDiv_ = 0;          // -3=*8, -2=*4, -1=*2, 0=/1, 1=/2, 2=/4, 3=/8
    int clockPulseCount_ = 0;   // counts pulses for division
    int samplesSinceDiv_ = 0;   // samples since last divided beat

    // Bar tracking (for On Bar performance features)
    int beatCount_ = 0;         // 0-3 within a bar (4/4 assumed)
    int barCount_ = 0;          // total bars since transport start
    bool pendingBarMutes_[kNumPads] = {};  // queued mute toggles for next bar
    int pendingBarCountdown_ = 0;  // bars remaining until flush (0 = no pending)

public:
    void setPendingBarMute(int pad, bool pending) {
        if (pad >= 0 && pad < kNumPads) pendingBarMutes_[pad] = pending;
    }
    bool getPendingBarMute(int pad) const {
        return (pad >= 0 && pad < kNumPads) ? pendingBarMutes_[pad] : false;
    }
    void commitBarMutes(int barsAhead);
    void flushBarMutes();
    int getPendingBarCountdown() const { return pendingBarCountdown_; }
    int getBeatCount() const { return beatCount_; }
private:

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

    // Config state
    PadCCMap padCCMaps_[kNumPads];
    PerfMode perfMode_ = PerfMode::Immediate;
    PerfMode presetSwitchMode_ = PerfMode::Immediate;
    int queueBars_ = 1;  // 1-4 bars ahead for OnBar mode
    bool debugMsgs_ = false;

public:
    PadCCMap& getPadCCMap(int pad) { return padCCMaps_[juce::jlimit(0, kNumPads - 1, pad)]; }
    const PadCCMap& getPadCCMap(int pad) const { return padCCMaps_[juce::jlimit(0, kNumPads - 1, pad)]; }
    PerfMode getPerfMode() const { return perfMode_; }
    void setPerfMode(PerfMode m) { perfMode_ = m; }
    PerfMode getPresetSwitchMode() const { return presetSwitchMode_; }
    void setPresetSwitchMode(PerfMode m) { presetSwitchMode_ = m; }
    int getQueueBars() const { return queueBars_; }
    void setQueueBars(int b) { queueBars_ = juce::jlimit(1, 4, b); }
    bool getDebugMsgs() const { return debugMsgs_; }
    void setDebugMsgs(bool b) { debugMsgs_ = b; }
    void rebootPlugin();
private:

    // Kit/Stack management
public:
    juce::String getCurrentKitName() const { return currentKitName_; }
    void saveCurrentAsKit(const juce::String& name);
    void loadKit(const juce::File& kitFile);
    KitData captureCurrentState() const;
    void applyKitData(const KitData& kit);
    juce::File getKitsDir() const;
    juce::File getStacksDir() const;
    juce::Array<juce::File> getAvailableKits() const;
    void createStackFile(const juce::String& name, const juce::StringArray& layerPaths);
private:
    juce::String currentKitName_ = "Untitled";

    // MIDI state (direct device access, Bear's pattern)
    std::unique_ptr<juce::MidiInput> midiInDevice_;
    juce::String midiDeviceName_;
    juce::StringArray cachedMidiDeviceNames_ { "None" };
    bool midiClockEnabled_ = false;
    bool midiTransportRunning_ = false;  // MIDI Start/Stop state
    int midiClockCount_ = 0;             // counts 24 PPQN pulses
    double midiClockLastBeatMs_ = 0.0;   // hi-res time of last beat
    bool midiTrigPending_[kNumPads] = {};
    float midiVelocity_[kNumPads] = {};
    int midiNote_[kNumPads] = {};

    // Ticker message system
    juce::String tickerMessage_;
    double tickerStartTime_ = 0.0;
    static constexpr double kTickerDurationMs = 5000.0;

public:
    juce::String getTickerMessage() const {
        if (tickerMessage_.isEmpty()) return {};
        double elapsed = juce::Time::getMillisecondCounterHiRes() - tickerStartTime_;
        if (elapsed > kTickerDurationMs) return {};
        return tickerMessage_;
    }
    float getTickerProgress() const {
        if (tickerMessage_.isEmpty()) return 0.0f;
        double elapsed = juce::Time::getMillisecondCounterHiRes() - tickerStartTime_;
        if (elapsed > kTickerDurationMs) return 0.0f;
        return (float)(elapsed / kTickerDurationMs);
    }
    bool isMidiTransportRunning() const { return midiTransportRunning_; }
private:
    void showTicker(const juce::String& msg) {
        tickerMessage_ = msg;
        tickerStartTime_ = juce::Time::getMillisecondCounterHiRes();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace grid
