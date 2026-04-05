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
    void triggerWithOffset(int sampleOffset);
    void triggerWithVelocityAndOffset(float vel, int sampleOffset);
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

    // Mute fade (smooth gain ramp for mute/unmute/solo transitions)
    void setMuteTarget(float t)          { muteTarget_ = t; }
    float getMuteTarget() const          { return muteTarget_; }
    float getMuteGain() const            { return muteGain_; }
    void setMuteFadeMs(float ms)         { muteFadeMs_ = std::max(0.0f, ms); }
    void snapMuteGain(float g)           { muteGain_ = g; muteTarget_ = g; }  // instant

    // Filter (TPT SVF)
    void setFilterType(FilterType t)     { filterType_ = t; }
    FilterType getFilterType() const     { return filterType_; }
    void setFilterCutoff(float hz)       { filterCutoff_ = juce::jlimit(20.0f, 20000.0f, hz); }
    float getFilterCutoff() const        { return filterCutoff_; }
    void setFilterResonance(float r)     { filterReso_ = juce::jlimit(0.0f, 1.0f, r); }
    float getFilterResonance() const     { return filterReso_; }

    // Lo-fi sampler emulation
    void setLofiMode(LofiMode m)         { lofiMode_ = m; }
    LofiMode getLofiMode() const         { return lofiMode_; }

    // Slice system
    static constexpr int kMaxSlicePoints = 64;
    int getSliceCount() const            { return sliceCount_; }
    float getSlicePoint(int i) const     { return (i >= 0 && i < sliceCount_) ? slicePoints_[i] : -1.0f; }
    const float* getSlicePoints() const  { return slicePoints_; }
    bool isSliceMode() const             { return sliceMode_; }
    void setSliceMode(bool on)           { sliceMode_ = on; }
    int getSelectedSlice() const         { return selectedSlice_; }
    void setSelectedSlice(int s)         { selectedSlice_ = juce::jlimit(0, std::max(0, sliceCount_), s); }

    // Per-slice pitch offset in semitones
    float getSlicePitch(int regionIdx) const {
        return (regionIdx >= 0 && regionIdx < kMaxSlicePoints) ? slicePitch_[regionIdx] : 0.0f;
    }
    void setSlicePitch(int regionIdx, float st) {
        if (regionIdx >= 0 && regionIdx < kMaxSlicePoints)
            slicePitch_[regionIdx] = juce::jlimit(-48.0f, 48.0f, st);
    }
    // Active slice pitch offset (applied during playback, set on trigger)
    float getSlicePitchOffset() const     { return slicePitchOffset_; }
    void setSlicePitchOffset(float st)    { slicePitchOffset_ = st; }

    // Insert a slice point at normalized position. Returns index, or -1 if full.
    int insertSlicePoint(float pos) {
        if (sliceCount_ >= kMaxSlicePoints) return -1;
        pos = juce::jlimit(0.0f, 1.0f, pos);
        int idx = 0;
        while (idx < sliceCount_ && slicePoints_[idx] < pos) idx++;
        if (idx > 0 && std::abs(slicePoints_[idx - 1] - pos) < 0.005f) return -1;
        if (idx < sliceCount_ && std::abs(slicePoints_[idx] - pos) < 0.005f) return -1;
        // Shift right (points AND pitch — pitch is per-region, region idx+1 gets split)
        for (int i = sliceCount_; i > idx; --i) {
            slicePoints_[i] = slicePoints_[i - 1];
            slicePitch_[i + 1] = slicePitch_[i];  // shift region pitches right
        }
        slicePoints_[idx] = pos;
        slicePitch_[idx + 1] = slicePitch_[idx];  // new region inherits pitch from parent
        sliceCount_++;
        return idx;
    }

    // Remove slice point nearest to pos (within tolerance). Returns true if removed.
    bool removeSlicePoint(float pos, float tolerance = 0.01f) {
        int best = -1;
        float bestDist = tolerance;
        for (int i = 0; i < sliceCount_; ++i) {
            float d = std::abs(slicePoints_[i] - pos);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        if (best < 0) return false;
        // Shift left — merge region pitch (keep the earlier region's pitch)
        for (int i = best; i < sliceCount_ - 1; ++i) {
            slicePoints_[i] = slicePoints_[i + 1];
            slicePitch_[i + 1] = slicePitch_[i + 2];
        }
        sliceCount_--;
        return true;
    }

    // Remove all slice points
    void clearSlices() {
        sliceCount_ = 0; selectedSlice_ = 0;
        for (int i = 0; i < kMaxSlicePoints; ++i) slicePitch_[i] = 0.0f;
    }

    // Auto-slice: evenly divide the start→end region
    void autoSlice(int numSlices) {
        clearSlices();
        if (numSlices < 2) return;
        float s = startPos_, e = endPos_;
        for (int i = 1; i < numSlices; ++i) {
            float pos = s + (e - s) * ((float)i / (float)numSlices);
            insertSlicePoint(pos);
        }
    }

    // Transient detection: energy-based onset detector with zero-crossing snap
    // sensitivity: 0.0 = few (only loud hits), 1.0 = many (catches ghost notes)
    void detectTransients(float sensitivity = 0.5f) {
        clearSlices();
        int ns = numSamples_.load();
        if (ns == 0) return;
        const float* data = buffer_.getReadPointer(0);
        int startSamp = (int)(startPos_ * ns);
        int endSamp = (int)(endPos_ * ns);
        int regionLen = endSamp - startSamp;
        if (regionLen < 1024) return;

        // Map sensitivity to detector params
        float inv = 1.0f - sensitivity;
        float threshMult = 1.0f + 7.0f * inv * inv;   // 8→1
        int minInterOnset = (int)((80.0f - 60.0f * sensitivity) * 0.001f * (float)getSampleRate());
        float silenceGateDb = -30.0f - 60.0f * sensitivity;
        float silenceGate = std::pow(10.0f, silenceGateDb / 10.0f);  // power threshold

        // Pass 1: compute windowed energy
        constexpr int kWin = 512;
        constexpr int kHop = 128;
        int numFrames = (regionLen - kWin) / kHop + 1;
        if (numFrames < 3) return;

        // Use stack-friendly fixed buffer (max ~3000 frames for 4 bars at 48k)
        constexpr int kMaxFrames = 4096;
        if (numFrames > kMaxFrames) numFrames = kMaxFrames;
        float energy[kMaxFrames];

        for (int f = 0; f < numFrames; ++f) {
            float sum = 0.0f;
            const float* ptr = data + startSamp + f * kHop;
            for (int i = 0; i < kWin; ++i) sum += ptr[i] * ptr[i];
            energy[f] = sum / kWin;
        }

        // Pass 2: half-wave rectified first-difference + adaptive threshold + peak pick
        float prevE = energy[0];
        float emaThresh = energy[0];
        int lastOnsetFrame = -minInterOnset / kHop;

        for (int f = 1; f < numFrames - 1; ++f) {
            float odf = std::max(0.0f, energy[f] - prevE);
            emaThresh = 0.1f * odf + 0.9f * emaThresh;
            float thresh = emaThresh * threshMult;

            float odfNext = std::max(0.0f, energy[f + 1] - energy[f]);

            if (odf > thresh && odf > odfNext && energy[f] > silenceGate
                && (f - lastOnsetFrame) >= (minInterOnset / kHop)) {
                // Backtrack to energy minimum in previous 10 frames
                int bestFrame = f;
                float bestEnergy = energy[f];
                for (int b = f - 1; b >= std::max(0, f - 10); --b) {
                    if (energy[b] < bestEnergy) { bestEnergy = energy[b]; bestFrame = b; }
                }
                int onsetSamp = startSamp + bestFrame * kHop;

                // Snap backward to nearest zero crossing (max 256 samples)
                for (int s = onsetSamp; s > std::max(startSamp + 1, onsetSamp - 256); --s) {
                    if ((data[s] >= 0.0f) != (data[s - 1] >= 0.0f)) {
                        onsetSamp = s;
                        break;
                    }
                }

                float pos = (float)onsetSamp / (float)ns;
                if (pos > startPos_ + 0.005f && pos < endPos_ - 0.005f) {
                    insertSlicePoint(pos);
                    if (sliceCount_ >= kMaxSlicePoints) break;
                }
                lastOnsetFrame = f;
            }
            prevE = energy[f];
        }
    }

    // Find nearest zero crossing to pos (searches ±512 samples)
    float findNearestZeroCrossing(float pos) const {
        int ns = numSamples_.load();
        if (ns == 0) return pos;
        const float* data = buffer_.getReadPointer(0);
        int center = (int)(pos * ns);
        int best = center;
        float bestDist = 999999.0f;
        for (int offset = 0; offset < 512; ++offset) {
            for (int dir = -1; dir <= 1; dir += 2) {
                int idx = center + dir * offset;
                if (idx < 1 || idx >= ns) continue;
                // Zero crossing = sign change between adjacent samples
                if ((data[idx - 1] >= 0.0f) != (data[idx] >= 0.0f)) {
                    float dist = (float)std::abs(idx - center);
                    if (dist < bestDist) { bestDist = dist; best = idx; }
                }
            }
            if (bestDist < 999998.0f) break;  // found one, stop
        }
        return (float)best / (float)ns;
    }

    // Get the start/end positions for a given slice index
    // Slice 0 = startPos→first point, slice N = last point→endPos
    void getSliceRegion(int sliceIdx, float& outStart, float& outEnd) const {
        float s = startPos_, e = endPos_;
        if (sliceCount_ == 0 || sliceIdx < 0) { outStart = s; outEnd = e; return; }
        outStart = (sliceIdx == 0) ? s : slicePoints_[std::min(sliceIdx - 1, sliceCount_ - 1)];
        outEnd = (sliceIdx >= sliceCount_) ? e : slicePoints_[std::min(sliceIdx, sliceCount_ - 1)];
    }

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

    // Mute fade
    float muteGain_    = 1.0f;   // current gain (0=silent, 1=full)
    float muteTarget_  = 1.0f;   // where to ramp to
    float muteFadeMs_  = 0.0f;   // 0 = instant

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

    // Lo-fi sampler emulation
    LofiMode lofiMode_ = LofiMode::Off;
    float lofiPhaseL_ = 0.0f, lofiPhaseR_ = 0.0f;
    float lofiHeldL_ = 0.0f, lofiHeldR_ = 0.0f;

    // Slice system
    float slicePoints_[kMaxSlicePoints] = {};  // sorted normalized positions
    float slicePitch_[kMaxSlicePoints] = {};   // per-region pitch offset (semitones)
    int sliceCount_ = 0;
    bool sliceMode_ = false;
    int selectedSlice_ = 0;  // which slice plays on next trigger
    float slicePitchOffset_ = 0.0f;  // active pitch offset from current slice

    // Bit crush: quantize to N bits, no dither (authentic vintage)
    static inline float bitCrush(float x, float levels, float invLevels) {
        return invLevels * (float)((int)(x * levels));
    }
    // µ-law compress (for MPC-60 companding)
    static inline float muCompress(float x) {
        constexpr float mu = 255.0f;
        constexpr float invLog = 1.0f / 5.5452f;  // 1/ln(256)
        float s = (x >= 0.0f) ? 1.0f : -1.0f;
        return s * std::log(1.0f + mu * std::abs(x)) * invLog;
    }
    // µ-law expand
    static inline float muExpand(float x) {
        constexpr float mu = 255.0f;
        constexpr float logMu1 = 5.5452f;  // ln(256)
        float s = (x >= 0.0f) ? 1.0f : -1.0f;
        return s * (std::exp(std::abs(x) * logMu1) - 1.0f) / mu;
    }

    // Fast tanh approximation (for MS20 feedback saturation)
    static inline float fastTanh(float x) {
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Standard SVF filter — one sample (LPF/HPF/BPF/Notch)
    // With resonance-dependent input drive (analog warmth) and per-type output compensation
    inline float filterSVF(float input, float& ic1, float& ic2,
                            float g, float k, float a1, float a2, float a3) const {
        // Subtle analog drive: signal gets slightly fatter with resonance
        // Mimics the gentle compression of analog integrators under feedback
        float driven = input * (1.0f + filterReso_ * 0.2f);

        float v3 = driven - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        switch (filterType_) {
            case FilterType::LPF: {
                // Bass compensation: resonance steals energy from the passband.
                // Scale output to restore body. Quadratic curve: gentle at low reso, strong at high.
                float comp = 1.0f + filterReso_ * filterReso_ * 0.6f;
                return v2 * comp;
            }
            case FilterType::HPF:   return input - k * v1 - v2;
            case FilterType::BPF: {
                // BPF is naturally quieter — boost proportional to Q
                float bpGain = 1.0f + filterReso_ * 0.4f + 0.3f;
                return v1 * bpGain;
            }
            case FilterType::Notch: return input - k * v1;
            default:                return input;
        }
    }

    // MS20 filter — SVF with tanh saturation in feedback path
    // Drive into saturation for harmonics, soft clip output for safety
    inline float filterMS20(float input, float& ic1, float& ic2,
                             float g, float k, float a1, float a2, float a3) const {
        // Drive: 1.0 at zero reso → 3.0 at full reso. Warm growl, not harsh clip.
        float drive = 1.0f + filterReso_ * 2.0f;
        // Input drive for fatness
        float driven = input * (1.0f + filterReso_ * 0.3f);
        float v3 = driven - fastTanh(ic2 * drive);
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        // Bass compensation + soft clip
        float comp = 1.0f + filterReso_ * filterReso_ * 0.5f;
        return fastTanh(v2 * comp);
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
