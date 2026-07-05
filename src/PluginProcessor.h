#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PluginParameters.h"
#include "GridEngine.h"
#include <map>

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
    const GridEngine& getEngine() const { return engine_; }
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

    // Globals MIDI device — second MIDI input dedicated to global-scope
    // controls (mixer faders, master comp, etc.). Sits unused until the
    // Globals overlay UI ships in 2.4.9, but routing infra is wired now
    // so users can start binding controllers ahead of that release.
    juce::String getGlobalsMidiDeviceName() const { return globalsMidiDeviceName_; }
    void setGlobalsMidiDevice(const juce::String& name);
    void closeGlobalsMidiDevice();

    // Pad mute CC base. CC (base+0)..(base+7) → mute for pad 1..8.
    // Value >= 64 mutes the pad, < 64 unmutes. -1 = disabled (no mute CCs).
    // Listens on the Globals Device if connected, else on the Pads Device.
    int  getPadMuteCCBase() const   { return padMuteCCBase_; }
    void setPadMuteCCBase(int cc)   { padMuteCCBase_ = juce::jlimit(-1, 120, cc); }

    // Pad settings clipboard. Sample, slices, and per-pad slice CC are
    // NOT copied (those belong with the sample). Only the "shape" of the
    // pad's playback + routing + CC mapping is transferred.
    void copyPadSettings(int srcPad);
    void pasteSettingsToAllPads();   // copies from clipboard to all 8 pads
    bool hasPadSettingsClipboard() const { return padClipValid_; }
    int  getPadClipSource() const        { return padClipSourcePad_; }

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

    // ── MIDI-CV output state (2.4.9) ──────────────────────────────────
    // When a pad is in PadMode::MidiCV, incoming MIDI notes for its
    // channel drive a gate on plugin output ch (padIdx + 2) instead of
    // triggering audio playback. Shared pitch CV on ch 10 (V/oct),
    // shared velocity CV on ch 11. Both share buses reflect the last
    // MIDI-CV pad triggered.
    bool  midiCVGate_[kNumPads] = {};
    int   midiCVLastNote_[kNumPads] = {};
    float midiCVSharedPitch_ = 0.0f;   // 1V/oct, note 60 = 0V baseline (5V mid)
    float midiCVSharedVel_   = 0.0f;   // 0.0-0.5 buffer range = 0-5V

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
    int getBarCount() const { return barCount_; }
    bool isTransportRunning() const { return midiTransportRunning_; }
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
    bool crashLogEnabled_ = false;
    float encoderSpeed_ = 1.0f;
    float muteFadeMs_ = 0.0f;   // 0 = instant mute/unmute
    bool  progChangeEnabled_ = false;  // respond to MIDI program change for kit switching
    int   progChangeCC_ = 0;           // 0 = use program change msg, 1-127 = use this CC instead
    bool  midiTransportEnabled_ = true; // respond to MIDI start/stop/continue
    float browserFontSize_ = 0.0f;     // 0 = default, -3..+3 adjustment in 0.5 steps
    int   rollStartDiv_ = 3;            // 0=1/2, 1=1/4, 2=1/8, 3=1/16, 4=1/32
    int sliceCVPad_[2] = { 0, 1 };  // which pad each Slice CV controls (-1=OFF, 0-7=pad)

    // Bus compressor (feedback topology, dual-time-constant release, soft knee)
    float compThreshDb_  = -12.0f;  // dB
    float compRatio_     = 4.0f;    // :1
    float compAttackMs_  = 10.0f;   // ms (SSL default: 10ms lets transients through)
    float compReleaseMs_ = 100.0f;  // ms (fast time constant for auto-release)
    float compMakeupDb_  = 0.0f;    // dB
    float compKneeDb_    = 6.0f;    // dB (soft knee width, 0=hard)
    bool  compEnabled_   = false;
    bool  compAutoRelease_ = true;  // dual-time-constant auto-release
    int   compSCHpfHz_   = 80;     // sidechain HPF frequency (0=off, 60/80/120/150)
    int   compSCSrc_     = -1;     // sidechain source pad (-1=bus, 0-7=pad)
    float compDrive_     = 1.03f;  // subtle output saturation (1.0=off, 1.05=warm)
    float compGainReductionDb_ = 0.0f;  // current GR for visualization
    bool  compShowGR_    = false;  // show GR overlay on pads
    float compMix_       = 1.0f;   // 0.0=dry, 1.0=fully compressed (parallel comp)

    // Display: peak input level for compressor scope (updated each processBlock)
    float compDisplayInputPeak_ = 0.0f;

    // Output low-cut HPF (end-of-chain utility, 4th order Butterworth -24dB/oct)
    int outputHpfHz_ = 0;  // 0=OFF, 20/25/30/40/50
    int hpfLastHz_ = -1;
    struct BiquadCoeffs { float b0=1,b1=0,b2=0,a1=0,a2=0; };
    struct BiquadState { float x1=0,x2=0,y1=0,y2=0; };
    BiquadCoeffs hpfSections_[2];
    BiquadState hpfL_[2], hpfR_[2];
    void updateOutputHpfCoeffs();

public:
    float getCompDisplayInputPeak() const { return compDisplayInputPeak_; }
    int   getOutputHpfHz() const     { return outputHpfHz_; }
    void  setOutputHpfHz(int hz)     { outputHpfHz_ = juce::jlimit(0, 60, hz); }
private:
    float transSensitivity_ = 0.3f;  // transient detection sensitivity (0.1=more, 1.0=fewer)

    // Envelope state (dual-time-constant)
    float compEnvFast_ = 0.0f;     // fast release envelope
    float compEnvSlow_ = 0.0f;     // slow release envelope

    // Sidechain HPF state (2nd order Butterworth)
    float scHpfX1_ = 0.0f, scHpfX2_ = 0.0f;
    float scHpfY1_ = 0.0f, scHpfY2_ = 0.0f;
    float scHpfB0_ = 1.0f, scHpfB1_ = 0.0f, scHpfB2_ = 0.0f;
    float scHpfA1_ = 0.0f, scHpfA2_ = 0.0f;
    int   scHpfLastHz_ = 0;  // recalc coeffs when changed

    void updateSCHpfCoeffs();

public:
    PadCCMap& getPadCCMap(int pad) { return padCCMaps_[juce::jlimit(0, kNumPads - 1, pad)]; }
    const PadCCMap& getPadCCMap(int pad) const { return padCCMaps_[juce::jlimit(0, kNumPads - 1, pad)]; }

    // MIDI-CV gate state (for editor pad paint)
    bool getMidiCVGate(int pad) const {
        return (pad >= 0 && pad < kNumPads) ? midiCVGate_[pad] : false;
    }
    PerfMode getPerfMode() const { return perfMode_; }
    void setPerfMode(PerfMode m) {
        perfMode_ = m;
        // Clear all pending bar mutes when switching modes
        for (int i = 0; i < kNumPads; ++i) pendingBarMutes_[i] = false;
        pendingBarCountdown_ = 0;
    }
    PerfMode getPresetSwitchMode() const { return presetSwitchMode_; }
    void setPresetSwitchMode(PerfMode m) { presetSwitchMode_ = m; }
    int getQueueBars() const { return queueBars_; }
    void setQueueBars(int b) { queueBars_ = juce::jlimit(1, 4, b); }
    bool getDebugMsgs() const { return debugMsgs_; }
    void setDebugMsgs(bool b) { debugMsgs_ = b; }
    bool getCrashLogEnabled() const { return crashLogEnabled_; }
    void setCrashLogEnabled(bool b) { crashLogEnabled_ = b; }
    float getEncoderSpeed() const { return encoderSpeed_; }
    void setEncoderSpeed(float s) { encoderSpeed_ = juce::jlimit(1.0f, 3.0f, s); }
    float getMuteFadeMs() const { return muteFadeMs_; }
    void setMuteFadeMs(float ms) { muteFadeMs_ = juce::jlimit(0.0f, 500.0f, ms); engine_.setMuteFadeMs(ms); }
    bool getProgChangeEnabled() const { return progChangeEnabled_; }
    void setProgChangeEnabled(bool b) { progChangeEnabled_ = b; }
    int  getProgChangeCC() const { return progChangeCC_; }
    void setProgChangeCC(int cc) { progChangeCC_ = juce::jlimit(0, 127, cc); }
    bool getMidiTransportEnabled() const { return midiTransportEnabled_; }
    void setMidiTransportEnabled(bool b) { midiTransportEnabled_ = b; }
    float getBrowserFontAdj() const { return browserFontSize_; }
    void  setBrowserFontAdj(float adj) { browserFontSize_ = juce::jlimit(-3.0f, 5.0f, adj); }
    int   getRollStartDiv() const { return rollStartDiv_; }
    void  setRollStartDiv(int d) { rollStartDiv_ = juce::jlimit(0, 4, d); }
    int getSliceCVPad(int cv) const { return sliceCVPad_[juce::jlimit(0, 1, cv)]; }
    void setSliceCVPad(int cv, int pad) { sliceCVPad_[juce::jlimit(0, 1, cv)] = juce::jlimit(-1, 7, pad); }

    // Bus compressor
    bool  getCompEnabled() const     { return compEnabled_; }
    void  setCompEnabled(bool b)     { compEnabled_ = b; }
    float getCompThreshDb() const    { return compThreshDb_; }
    void  setCompThreshDb(float db)  { compThreshDb_ = juce::jlimit(-60.0f, 0.0f, db); }
    float getCompRatio() const       { return compRatio_; }
    void  setCompRatio(float r)      { compRatio_ = juce::jlimit(1.5f, 20.0f, r); }
    float getCompAttackMs() const    { return compAttackMs_; }
    void  setCompAttackMs(float ms)  { compAttackMs_ = juce::jlimit(0.1f, 100.0f, ms); }
    float getCompReleaseMs() const   { return compReleaseMs_; }
    void  setCompReleaseMs(float ms) { compReleaseMs_ = juce::jlimit(10.0f, 500.0f, ms); }
    float getCompMakeupDb() const    { return compMakeupDb_; }
    void  setCompMakeupDb(float db)  { compMakeupDb_ = juce::jlimit(0.0f, 24.0f, db); }
    float getCompKneeDb() const      { return compKneeDb_; }
    void  setCompKneeDb(float db)    { compKneeDb_ = juce::jlimit(0.0f, 12.0f, db); }
    bool  getCompAutoRelease() const { return compAutoRelease_; }
    void  setCompAutoRelease(bool b) { compAutoRelease_ = b; }
    int   getCompSCHpfHz() const     { return compSCHpfHz_; }
    void  setCompSCHpfHz(int hz)     { compSCHpfHz_ = juce::jlimit(0, 200, hz); }
    int   getCompSCSrc() const       { return compSCSrc_; }
    void  setCompSCSrc(int p)        { compSCSrc_ = juce::jlimit(-1, 7, p); }
    float getCompDrive() const       { return compDrive_; }
    void  setCompDrive(float d)      { compDrive_ = juce::jlimit(1.0f, 1.15f, d); }
    float getCompGainReductionDb() const { return compGainReductionDb_; }
    bool  getCompShowGR() const      { return compShowGR_; }
    void  setCompShowGR(bool b)      { compShowGR_ = b; }
    float getCompMix() const         { return compMix_; }
    void  setCompMix(float m)        { compMix_ = juce::jlimit(0.0f, 1.0f, m); }
    float getTransSensitivity() const { return transSensitivity_; }
    void  setTransSensitivity(float s) { transSensitivity_ = juce::jlimit(0.05f, 1.0f, s); }
    void rebootPlugin();
    void clearAllPads();   // removes all samples, resets all settings
    void initPad(int pad); // resets one pad to factory defaults

    // Slice export: write each slice region as individual WAV
    int exportSlicesToFiles(int pad);
    int importPadsAsSliceChain(int targetPad);

    // Per-sample slice persistence: remember slices keyed by filename
    struct SliceCache {
        float starts[128] = {};
        float ends[128] = {};
        float pitches[128] = {};
        int count = 0;
    };
    std::map<juce::String, SliceCache> sliceCache_;
    juce::CriticalSection sliceCacheLock_;  // protects sliceCache_ from host/GUI thread races
    juce::CriticalSection stateLock_;      // protects getState/setState from host + autosave racing
    void cacheSlicesForPad(int pad);
    bool restoreCachedSlices(int pad);
private:

    // Kit/Stack management
public:
    juce::String getCurrentKitName() const { return currentKitName_; }
    void setCurrentKitName(const juce::String& name) { currentKitName_ = name; }
    void saveCurrentAsKit(const juce::String& name);
    void loadKit(const juce::File& kitFile);
    KitData captureCurrentState() const;
    void applyKitData(const KitData& kit);
    juce::File getKitsDir() const;
    juce::File getStacksDir() const;
    juce::Array<juce::File> getAvailableKits() const;
    void loadKitByIndex(int index);
    void createStackFile(const juce::String& name, const juce::StringArray& layerPaths);

    // Autosave: periodically writes state XML to disk (no WAV, lightweight)
    void performAutosave();
    void loadAutosave();
    void saveGlobalPrefs();
    void loadGlobalPrefs();
    juce::File getAutosaveFile() const;
    void tickAutosave() {
        if (!autosaveEnabled_) return;
        if (!stateDirty_) return;
        if (++autosaveFrameCount_ >= kAutosaveIntervalFrames) {
            autosaveFrameCount_ = 0;
            performAutosave();
            stateDirty_ = false;
        }
    }
    bool getAutosaveEnabled() const { return autosaveEnabled_; }
    void setAutosaveEnabled(bool b) { autosaveEnabled_ = b; }
    void markStateDirty() { stateDirty_ = true; }
    bool isStateDirty() const { return stateDirty_; }
    double getLastAutosaveTimeMs() const { return lastAutosaveTimeMs_; }

    // Recall point: single in-memory snapshot for instant restore
    void setRecallPoint();
    bool restoreRecallPoint();
    bool hasRecallPoint() const { return recallPointSet_; }

private:
    juce::String currentKitName_ = "Autosave";
    juce::String instanceId_;  // unique ID per plugin instance for autosave
    int autosaveFrameCount_ = 0;
    static constexpr int kAutosaveIntervalFrames = 150;  // ~5 sec at 30fps
    bool autosaveEnabled_ = true;
    bool stateDirty_ = true;  // starts true to save initial state
    bool stateLoadedFromHost_ = false;  // true after setState called
    double lastAutosaveTimeMs_ = 0.0;  // when last autosave completed
    void cleanupOldAutosaves();

    // Recall point (RAM snapshot)
    bool recallPointSet_ = false;
    juce::MemoryBlock recallPointData_;

    // MIDI state (direct device access, Bear's pattern)
    // Per TheTechnobear v260425: lookup by identifier, not name. Names are
    // not unique across ports (some devices report the same name on every
    // port). Identifier is stable and unique. Name kept around for display.
    std::unique_ptr<juce::MidiInput> midiInDevice_;
    juce::String midiDeviceName_;
    juce::String midiDeviceId_;
    // Second MIDI input for global-scope controls (mixer/comp/master).
    // Same callback (handleIncomingMidiMessage) handles both; routing logic
    // distinguishes by source pointer when we need scope-specific handling.
    std::unique_ptr<juce::MidiInput> globalsMidiInDevice_;
    juce::String globalsMidiDeviceName_;
    juce::String globalsMidiDeviceId_;
    // Pad mute CC base. -1 = disabled. Default 24 (CCs 24-31 = mute pad 1-8).
    int padMuteCCBase_ = 24;
    // Cached MIDI device names. Mutable because getMidiDeviceNames()
    // refreshes it on each call (live rescan, since SSP devices can be
    // hot-plugged after prepareToPlay).
    mutable juce::StringArray cachedMidiDeviceNames_ { "None" };
    bool midiClockEnabled_ = false;
    bool midiTransportRunning_ = false;
    int midiClockCount_ = 0;
    double midiClockLastBeatMs_ = 0.0;
    // Thread-safe MIDI collector: messages are timestamped on MIDI thread,
    // delivered sample-accurately in processBlock. Replaces atomic flag pattern.
    juce::MidiMessageCollector midiCollector_;

    // Pad settings clipboard (sample/slices excluded — only params)
    struct PadClip {
        PadMode    mode = PadMode::OneShot;
        float      volume = 1.0f;
        float      pan = 0.0f;
        float      pitch = 0.0f;
        float      stretch = 1.0f;
        float      fadeInMs = 0.0f;
        float      fadeOutMs = 0.0f;
        int        fadeInCurve = 0;
        int        fadeOutCurve = 0;
        ChokeGroup choke = ChokeGroup::None;
        int        midiChannel = 0;
        int        clockBeats = 4;
        VoiceMode  voiceMode = VoiceMode::Mono;
        FilterType filterType = FilterType::Off;
        float      filterCutoff = 20000.0f;
        float      filterReso = 0.0f;
        LofiMode   lofiMode = LofiMode::Off;
        float      compSend = 0.0f;
        bool       compBypass = false;
        int        pitchMode = 0;
        int        outputChannel = -1;
        bool       sendToMix = true;
        bool       reversed = false;
        PadCCMap   ccMap;
    };
    PadClip padClip_;
    bool padClipValid_ = false;
    int  padClipSourcePad_ = -1;

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
