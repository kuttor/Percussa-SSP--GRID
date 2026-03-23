#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginParameters.h"

namespace grid {

class SampleSlot {
public:
    SampleSlot();

    // Loading
    bool loadFile(const juce::File& file);
    bool isLoaded() const { return numSamples_ > 0; }
    void clear();

    // Trigger
    void trigger();
    void stop();
    bool isPlaying() const { return playing_; }

    // Process: writes into outL/outR (ADDS to existing data)
    void process(float* outL, float* outR, int numSamples);

    // Parameters
    void setMode(PadMode m)     { mode_ = m; }
    void setVolume(float v)     { volume_ = v; }
    void setPan(float p)        { pan_ = p; }
    void setStartPos(float n)   { startPos_ = juce::jlimit(0.0f, 1.0f, n); }
    void setEndPos(float n)     { endPos_ = juce::jlimit(0.0f, 1.0f, n); }

    // Pitch: semitones, +-24 range (+-2 octaves). Resampling-based.
    void setPitchSemitones(float st) { pitchSemitones_ = juce::jlimit(-24.0f, 24.0f, st);
                                        pitchRate_ = std::pow(2.0f, pitchSemitones_ / 12.0f); }
    float getPitchSemitones() const  { return pitchSemitones_; }
    float getPitchRate() const       { return pitchRate_; }

    // Time stretch: 0.5x to 2.0x, default 1.0. Placeholder until DSP added.
    void setTimeStretch(float t)     { timeStretch_ = juce::jlimit(0.5f, 2.0f, t); }
    float getTimeStretch() const     { return timeStretch_; }

    PadMode getMode() const     { return mode_; }
    float getVolume() const     { return volume_; }
    float getPan() const        { return pan_; }
    float getStartPos() const   { return startPos_; }
    float getEndPos() const     { return endPos_; }

    // Info
    const juce::String& getFileName() const { return fileName_; }
    int getNumSamples() const { return numSamples_; }
    int getNumChannels() const { return numChannels_; }
    double getSampleRate() const { return fileSampleRate_; }
    float getPlaybackPosition() const; // 0-1 normalised
    const juce::AudioBuffer<float>& getBuffer() const { return buffer_; }

private:
    juce::AudioFormatManager formatManager_;
    juce::AudioBuffer<float> buffer_;
    int numSamples_ = 0;
    int numChannels_ = 0;
    double fileSampleRate_ = 48000.0;
    juce::String fileName_;

    // Playback state
    double readPosition_ = 0.0;
    bool playing_ = false;
    PadMode mode_ = PadMode::OneShot;

    // Parameters
    float volume_         = 1.0f;
    float pan_            = 0.0f;
    float startPos_       = 0.0f;
    float endPos_         = 1.0f;
    float pitchSemitones_ = 0.0f;
    float pitchRate_      = 1.0f;
    float timeStretch_    = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleSlot)
};

} // namespace grid
