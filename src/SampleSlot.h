#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginParameters.h"
#include <atomic>

namespace grid {

// Per-voice playback state
struct Voice {
    double readPosition = 0.0;
    double sourcePos    = 0.0;
    double grainPos[2]  = {0.0, 0.0};
    int    grainCounter  = 0;
    int    fadeIn         = 0;
    int    fadeOut        = 0;
    bool   playing       = false;
    bool   stopping      = false;
    float  velocity      = 1.0f;
    int    startOffset   = 0;

    void reset() {
        readPosition = 0.0; sourcePos = 0.0;
        grainPos[0] = grainPos[1] = 0.0; grainCounter = 0;
        fadeIn = 0; fadeOut = 0;
        playing = false; stopping = false; velocity = 1.0f;
        startOffset = 0;
    }
};

class SampleSlot {
public:
    static constexpr int kMaxVoices = 4;

    SampleSlot();

    // Loading — thread-safe (loads to temp, swaps atomically)
    bool loadFile(const juce::File& file);
    bool loadFromBuffer(const juce::AudioBuffer<float>& src, int numSamps,
                        double sr, const juce::String& name, const juce::String& path);
    bool isLoaded() const { return numSamples_.load() > 0; }
    void clear();

    // Trigger / Stop
    void trigger();
    void triggerWithVelocity(float vel);
    void triggerWithOffset(int sampleOffset);  // sample-accurate CV trigger
    void stop();
    void stopAll();

    bool isPlaying() const;
    bool isStopping() const;
    void advanceRetriggerGuard(int samples) { samplesSinceLastTrigger_ += samples; }

    // Process: writes into outL/outR (ADDS to existing data)
    void process(float* outL, float* outR, int numSamples);

    // Voice mode
    void setVoiceMode(VoiceMode m) { voiceMode_ = m; }
    VoiceMode getVoiceMode() const { return voiceMode_; }

    // Parameters
    void setMode(PadMode m)     { mode_ = m; }
    void setVolume(float v)     { volume_ = v; }
    void setPan(float p)        { pan_ = p;
                                    cachedPanL_ = std::cos((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                                    cachedPanR_ = std::sin((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi); }
    void setStartPos(float n)   { startPos_ = juce::jlimit(0.0f, 1.0f, n); }
    void setEndPos(float n)     { endPos_ = juce::jlimit(0.0f, 1.0f, n); }

    // Pitch: +-48 semitones (+-4 octaves). Resampling-based.
    void setPitchSemitones(float st) { pitchSemitones_ = juce::jlimit(-48.0f, 48.0f, st);
                                        pitchRate_ = std::pow(2.0f, pitchSemitones_ / 12.0f); }
    float getPitchSemitones() const  { return pitchSemitones_; }
    float getPitchRate() const       { return pitchRate_; }

    // Time stretch: 0.25x to 4.0x (user control)
    void setTimeStretch(float t)     { timeStretch_ = juce::jlimit(0.25f, 4.0f, t); }
    float getTimeStretch() const     { return timeStretch_; }

    // Clock beats: how many beats the sample stretches to fit (1,2,4,8,16)
    void setClockBeats(int b)        { clockBeats_ = juce::jlimit(1, 16, b); }
    int getClockBeats() const        { return clockBeats_; }

    // Clock stretch: auto-computed from BPM, 1.0 = no clock adjustment
    void setClockStretch(float t)    { clockStretch_ = juce::jlimit(0.1f, 10.0f, t); }
    float getClockStretch() const    { return clockStretch_; }
    void clearClockStretch()         { clockStretch_ = 1.0f; }

    // Effective stretch = clock base × user multiplier
    float getEffectiveStretch() const { return clockStretch_ * timeStretch_; }

    // Fade: separate in/out
    void setFadeInMs(float ms)       { fadeInMs_ = std::max(0.0f, ms); }
    float getFadeInMs() const        { return fadeInMs_; }
    void setFadeOutMs(float ms)      { fadeOutMs_ = std::max(0.0f, ms); }
    float getFadeOutMs() const       { return fadeOutMs_; }
    void setFadeInCurve(int c)       { fadeInCurve_ = juce::jlimit(0, 1, c); }
    int getFadeInCurve() const       { return fadeInCurve_; }
    void setFadeOutCurve(int c)      { fadeOutCurve_ = juce::jlimit(0, 1, c); }
    int getFadeOutCurve() const      { return fadeOutCurve_; }
    float getFadeMs() const          { return std::max(fadeInMs_, fadeOutMs_); }

    // Options
    void setChokeGroup(ChokeGroup g) { chokeGroup_ = g; }
    ChokeGroup getChokeGroup() const { return chokeGroup_; }
    void setReversed(bool r);
    bool isReversed() const          { return reversed_; }
    void normalize();
    float getNormalizeGain() const   { return normalizeGain_; }

    // MIDI (per-pad)
    void setMidiChannel(int ch)      { midiChannel_ = juce::jlimit(0, 17, ch); }
    int getMidiChannel() const       { return midiChannel_; }

    // Output sample rate
    void setOutputSampleRate(double sr) { outputSampleRate_ = sr; }
    double getOutputSampleRate() const  { return outputSampleRate_; }
    double getSampleRateRatio() const   { return fileSampleRate_ / outputSampleRate_; }

    // Filter (TPT SVF)
    void setFilterType(FilterType t)     { filterType_ = t; }
    FilterType getFilterType() const     { return filterType_; }
    void setFilterCutoff(float hz)       { filterCutoff_ = juce::jlimit(20.0f, 20000.0f, hz); }
    float getFilterCutoff() const        { return filterCutoff_; }
    void setFilterResonance(float r)     { filterReso_ = juce::jlimit(0.0f, 1.0f, r); }
    float getFilterResonance() const     { return filterReso_; }

    PadMode getMode() const     { return mode_; }
    float getVolume() const     { return volume_; }
    float getPan() const        { return pan_; }
    float getStartPos() const   { return startPos_; }
    float getEndPos() const     { return endPos_; }

    // Info
    const juce::String& getFileName() const { return fileName_; }
    const juce::String& getFilePath() const { return filePath_; }
    int getNumSamples() const { return numSamples_.load(); }
    int getNumChannels() const { return numChannels_; }
    double getSampleRate() const { return fileSampleRate_; }
    float getPlaybackPosition() const;
    const juce::AudioBuffer<float>& getBuffer() const { return buffer_; }

    // Voice access (for UI display)
    const Voice& getVoice(int i) const { return voices_[juce::jlimit(0, kMaxVoices - 1, i)]; }
    int getActiveVoiceCount() const;

private:
    juce::AudioFormatManager formatManager_;
    juce::AudioBuffer<float> buffer_;
    std::atomic<int> numSamples_ { 0 };
    int numChannels_ = 0;
    double fileSampleRate_ = 48000.0;
    juce::String fileName_;
    juce::String filePath_;

    // Voice pool
    Voice voices_[kMaxVoices];
    VoiceMode voiceMode_ = VoiceMode::Mono;

    // Playback mode
    PadMode mode_ = PadMode::OneShot;

    // Anti-click envelope
    static constexpr int kFadeSamples = 32;
    static constexpr int kRetriggerGuard = 64;  // ~1.3ms at 48kHz
    int samplesSinceLastTrigger_ = 99999;

    // Parameters (shared across all voices)
    float volume_         = 1.0f;
    float pan_            = 0.0f;
    float startPos_       = 0.0f;
    float endPos_         = 1.0f;
    float pitchSemitones_ = 0.0f;
    float pitchRate_      = 1.0f;
    float timeStretch_    = 1.0f;
    float clockStretch_   = 1.0f;
    int   clockBeats_     = 4;
    float fadeInMs_       = 0.0f;
    float fadeOutMs_      = 0.0f;
    int   fadeInCurve_    = 0;
    int   fadeOutCurve_   = 0;
    ChokeGroup chokeGroup_ = ChokeGroup::None;
    bool  reversed_       = false;
    float normalizeGain_  = 1.0f;
    int   midiChannel_    = 0;
    double outputSampleRate_ = 48000.0;

    // Cached pan (avoid cos/sin per process call)
    float cachedPanL_ = 0.707f;
    float cachedPanR_ = 0.707f;

    // Filter (TPT SVF — Topology-Preserving Transform State Variable Filter)
    FilterType filterType_ = FilterType::Off;
    float filterCutoff_ = 20000.0f;   // Hz
    float filterReso_   = 0.0f;       // 0.0 - 1.0

    // SVF state (stereo) — used by LPF, HPF, BPF, Notch, MS20
    float svfIc1L_ = 0.0f, svfIc2L_ = 0.0f;
    float svfIc1R_ = 0.0f, svfIc2R_ = 0.0f;

    // Formant state: 3 parallel BPF bands × stereo
    float fmtIc1L_[3] = {}, fmtIc2L_[3] = {};
    float fmtIc1R_[3] = {}, fmtIc2R_[3] = {};

    // Fast tanh approximation (for MS20 feedback saturation)
    static inline float fastTanh(float x) {
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Standard SVF filter — one sample (LPF/HPF/BPF/Notch)
    inline float filterSVF(float input, float& ic1, float& ic2,
                            float g, float k, float a1, float a2, float a3) const {
        float v3 = input - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        switch (filterType_) {
            case FilterType::LPF:   return v2;
            case FilterType::HPF:   return input - k * v1 - v2;
            case FilterType::BPF:   return v1;
            case FilterType::Notch: return input - k * v1;
            default:                return input;
        }
    }

    // MS20 filter — SVF with tanh saturation in feedback path
    inline float filterMS20(float input, float& ic1, float& ic2,
                             float g, float k, float a1, float a2, float a3) const {
        float drive = 1.0f + filterReso_ * 4.0f;
        float v3 = input - fastTanh(ic2 * drive);
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;  // LP output with harmonic saturation
    }

    // Formant BPF — one band, one sample
    inline float filterBPF(float input, float& ic1, float& ic2,
                            float g, float a1, float a2, float a3) const {
        float v3 = input - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v1;  // bandpass output
    }

    // Granular — shared window table
    static constexpr int kGrainSize = 4096;
    float grainWindow_[kGrainSize];
    void initGrainWindow() {
        for (int i = 0; i < kGrainSize; ++i)
            grainWindow_[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                              * static_cast<float>(i) / static_cast<float>(kGrainSize));
    }

    float getGrainWindow(int sampleInGrain) const {
        return grainWindow_[sampleInGrain & (kGrainSize - 1)];
    }
    float readInterpolated(const float* src, double pos, int limit) const;

    // Internal: start a specific voice at the sample start position
    void startVoice(Voice& v, float vel);
    // Internal: process one voice for N samples
    void processVoice(Voice& v, float* outL, float* outR, int numSamples,
                      const float* srcL, const float* srcR, int ns,
                      int startSample, int endSample, int regionLen,
                      float panL, float panR, float effStretch, double sampleRateRatio,
                      int fadeInSamples, int fadeOutSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleSlot)
};

} // namespace grid
