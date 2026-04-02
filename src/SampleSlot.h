#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginParameters.h"
#include <atomic>

namespace grid {

class SampleSlot {
public:
    SampleSlot();

    // Loading — thread-safe (loads to temp, swaps atomically)
    bool loadFile(const juce::File& file);
    bool loadFromBuffer(const juce::AudioBuffer<float>& src, int numSamps,
                        double sr, const juce::String& name, const juce::String& path);
    bool isLoaded() const { return numSamples_.load() > 0; }
    void clear();

    // Trigger
    void trigger();
    void stop();
    bool isPlaying() const { return playing_; }
    bool isStopping() const { return stopping_; }
    void advanceRetriggerGuard(int samples) { samplesSinceLastTrigger_ += samples; }

    // Process: writes into outL/outR (ADDS to existing data)
    void process(float* outL, float* outR, int numSamples);

    // Parameters
    void setMode(PadMode m)     { mode_ = m; }
    void setVolume(float v)     { volume_ = v; }
    void setPan(float p)        { pan_ = p; }
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

    // Clock stretch: auto-computed from BPM, 1.0 = no clock adjustment
    void setClockStretch(float t)    { clockStretch_ = juce::jlimit(0.1f, 10.0f, t); }
    float getClockStretch() const    { return clockStretch_; }
    void clearClockStretch()         { clockStretch_ = 1.0f; }

    // Effective stretch = clock base × user multiplier
    float getEffectiveStretch() const { return clockStretch_ * timeStretch_; }

    // Fade: separate in/out, 0-100% of region. Separate curves per fade.
    void setFadeInMs(float ms)       { fadeInMs_ = std::max(0.0f, ms); }
    float getFadeInMs() const        { return fadeInMs_; }
    void setFadeOutMs(float ms)      { fadeOutMs_ = std::max(0.0f, ms); }
    float getFadeOutMs() const       { return fadeOutMs_; }
    void setFadeInCurve(int c)       { fadeInCurve_ = juce::jlimit(0, 1, c); }
    int getFadeInCurve() const       { return fadeInCurve_; }
    void setFadeOutCurve(int c)      { fadeOutCurve_ = juce::jlimit(0, 1, c); }
    int getFadeOutCurve() const      { return fadeOutCurve_; }
    // Compat helper for context bar
    float getFadeMs() const          { return std::max(fadeInMs_, fadeOutMs_); }

    // Options
    void setChokeGroup(ChokeGroup g) { chokeGroup_ = g; }
    ChokeGroup getChokeGroup() const { return chokeGroup_; }
    void setReversed(bool r);
    bool isReversed() const          { return reversed_; }
    void normalize();                // one-shot: scan peak, apply gain to ~0.95
    float getNormalizeGain() const   { return normalizeGain_; }

    // MIDI (per-pad)
    void setMidiChannel(int ch)      { midiChannel_ = juce::jlimit(0, 17, ch); }  // 0=off, 1-16=ch, 17=omni
    int getMidiChannel() const       { return midiChannel_; }

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

private:
    juce::AudioFormatManager formatManager_;
    juce::AudioBuffer<float> buffer_;
    std::atomic<int> numSamples_ { 0 };  // atomic for thread safety
    int numChannels_ = 0;
    double fileSampleRate_ = 48000.0;
    juce::String fileName_;
    juce::String filePath_;

    // Playback state
    double readPosition_ = 0.0;
    bool playing_ = false;
    PadMode mode_ = PadMode::OneShot;

    // Anti-click envelope
    static constexpr int kFadeSamples = 32;  // ~0.7ms at 48kHz — snappy but click-free
    int fadeIn_ = 0;    // counts up from 0 to kFadeSamples on trigger
    int fadeOut_ = 0;   // counts down from kFadeSamples to 0 on stop
    bool stopping_ = false;  // fade-out in progress
    int samplesSinceLastTrigger_ = 99999;  // retrigger guard

    // Parameters
    float volume_         = 1.0f;
    float pan_            = 0.0f;
    float startPos_       = 0.0f;
    float endPos_         = 1.0f;
    float pitchSemitones_ = 0.0f;
    float pitchRate_      = 1.0f;
    float timeStretch_    = 1.0f;
    float clockStretch_   = 1.0f;   // auto from BPM, 1.0 = no clock
    float fadeInMs_       = 0.0f;
    float fadeOutMs_      = 0.0f;
    int   fadeInCurve_    = 0;    // 0=linear, 1=exponential
    int   fadeOutCurve_   = 0;
    ChokeGroup chokeGroup_ = ChokeGroup::None;
    bool  reversed_       = false;
    float normalizeGain_  = 1.0f;
    int   midiChannel_    = 0;    // 0 = off, 1-16 = active

    // Granular time stretch state
    static constexpr int kGrainSize = 4096;  // ~85ms at 48kHz, less vibrato
    double grainPos_[2]     = {0.0, 0.0};
    int    grainCounter_    = 0;
    double sourcePos_       = 0.0;

    float getGrainWindow(int sampleInGrain) const;
    float readInterpolated(const float* src, double pos, int limit) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleSlot)
};

} // namespace grid
