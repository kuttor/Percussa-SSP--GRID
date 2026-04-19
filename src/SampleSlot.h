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
    bool isLoading() const { return loading_.load(std::memory_order_acquire); }
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

    // Stretch algorithm
    void setStretchMode(StretchMode m) { stretchMode_ = m; }
    StretchMode getStretchMode() const { return stretchMode_; }

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

    // Pitch quantize
    bool getPitchQuantize() const        { return pitchQuantize_; }
    void setPitchQuantize(bool q)        { pitchQuantize_ = q; }
    float getQuantizedPitch() const      { return pitchQuantize_ ? std::round(pitchSemitones_) : pitchSemitones_; }
    static const char* semitoneName(int st) {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int note = ((st % 12) + 12) % 12;
        return names[note];
    }
    static int semitoneOctave(int st) { return (st + 60) / 12 - 1; }  // C4 = 0 semitones

    // Compressor send (0.0 = dry, 1.0 = full send to bus compressor)
    void setCompSend(float s)            { compSend_ = juce::jlimit(0.0f, 1.0f, s); }
    float getCompSend() const            { return compSend_; }
    void setCompBypass(bool b)           { compBypass_ = b; }
    bool getCompBypass() const           { return compBypass_; }

    // Output routing
    int getOutputChannel() const         { return outputChannel_; }
    void setOutputChannel(int ch)        { outputChannel_ = juce::jlimit(0, 7, ch); }
    bool getSendToMix() const            { return sendToMix_; }
    void setSendToMix(bool b)            { sendToMix_ = b; }

    // ══════════════════════════════════════════════════════════════════════
    // Slice system — paired start/end regions
    // Only completed pairs are playable. First tap = start, second = end.
    // ══════════════════════════════════════════════════════════════════════
    static constexpr int kMaxSlices = 128;
    int getSliceCount() const            { return sliceCount_; }
    float getSliceStart(int i) const     { return (i >= 0 && i < sliceCount_) ? sliceStarts_[i] : 0.0f; }
    float getSliceEnd(int i) const       { return (i >= 0 && i < sliceCount_) ? sliceEnds_[i] : 1.0f; }
    bool isSliceMode() const             { return sliceMode_; }
    void setSliceMode(bool on)           { sliceMode_ = on; }
    int getSelectedSlice() const         { return selectedSlice_; }
    void setSelectedSlice(int s)         { selectedSlice_ = juce::jlimit(0, std::max(0, sliceCount_ - 1), s); }
    bool isSlicePending() const          { return slicePending_; }
    float getSlicePendingStart() const   { return slicePendingStart_; }

    float getSlicePitch(int i) const     { return (i >= 0 && i < kMaxSlices) ? slicePitch_[i] : 0.0f; }
    void setSlicePitch(int i, float st)  { if (i >= 0 && i < kMaxSlices) slicePitch_[i] = juce::jlimit(-48.0f, 48.0f, st); }
    float getSlicePitchOffset() const    { return slicePitchOffset_; }
    void setSlicePitchOffset(float st)   { slicePitchOffset_ = st; }

    // First tap: begin a slice at cursor
    bool beginSlice(float pos) {
        if (slicePending_) return false;
        slicePending_ = true;
        slicePendingStart_ = juce::jlimit(0.0f, 1.0f, pos);
        return true;
    }
    // Second tap: complete the slice
    int completeSlice(float pos) {
        if (!slicePending_ || sliceCount_ >= kMaxSlices) return -1;
        pos = juce::jlimit(0.0f, 1.0f, pos);
        float s = std::min(slicePendingStart_, pos);
        float e = std::max(slicePendingStart_, pos);
        if (e - s < 0.005f) return -1;
        int idx = 0;
        while (idx < sliceCount_ && sliceStarts_[idx] < s) idx++;
        for (int i = sliceCount_; i > idx; --i) {
            sliceStarts_[i] = sliceStarts_[i - 1];
            sliceEnds_[i] = sliceEnds_[i - 1];
            slicePitch_[i] = slicePitch_[i - 1];
        }
        sliceStarts_[idx] = s;
        sliceEnds_[idx] = e;
        slicePitch_[idx] = 0.0f;
        sliceCount_++;
        slicePending_ = false;
        sliceMode_ = true;  // auto-enable when slices exist
        return idx;
    }
    void cancelSlice() { slicePending_ = false; }

    // Remove slice region containing pos, or by index
    bool removeSlice(int idx) {
        if (idx < 0 || idx >= sliceCount_) return false;
        for (int i = idx; i < sliceCount_ - 1; ++i) {
            sliceStarts_[i] = sliceStarts_[i + 1];
            sliceEnds_[i] = sliceEnds_[i + 1];
            slicePitch_[i] = slicePitch_[i + 1];
        }
        sliceCount_--;
        if (sliceCount_ == 0) sliceMode_ = false;
        return true;
    }
    int findSliceAt(float pos) const {
        for (int i = 0; i < sliceCount_; ++i)
            if (pos >= sliceStarts_[i] - 0.002f && pos <= sliceEnds_[i] + 0.002f) return i;
        return -1;
    }

    void clearSlices() {
        sliceCount_ = 0; selectedSlice_ = 0; slicePending_ = false;
        sliceMode_ = false;  // no slices = no slice mode
        for (int i = 0; i < kMaxSlices; ++i) slicePitch_[i] = 0.0f;
    }
    void getSliceRegion(int idx, float& outStart, float& outEnd) const {
        if (idx >= 0 && idx < sliceCount_) { outStart = sliceStarts_[idx]; outEnd = sliceEnds_[idx]; }
        else { outStart = startPos_; outEnd = endPos_; }
    }

    // Direct pair insertion (for auto-slice and state load)
    int addSlicePair(float s, float e, float pitch = 0.0f) {
        if (sliceCount_ >= kMaxSlices) return -1;
        int idx = 0;
        while (idx < sliceCount_ && sliceStarts_[idx] < s) idx++;
        for (int i = sliceCount_; i > idx; --i) {
            sliceStarts_[i] = sliceStarts_[i - 1];
            sliceEnds_[i] = sliceEnds_[i - 1];
            slicePitch_[i] = slicePitch_[i - 1];
        }
        sliceStarts_[idx] = s; sliceEnds_[idx] = e; slicePitch_[idx] = pitch;
        sliceCount_++;
        sliceMode_ = true;  // auto-enable
        return idx;
    }

    // Auto-slice: create N pairs covering start→end
    void autoSlice(int numSlices) {
        clearSlices();
        if (numSlices < 1) return;
        float s = startPos_, e = endPos_, w = (e - s) / (float)numSlices;
        for (int i = 0; i < numSlices && i < kMaxSlices; ++i)
            addSlicePair(s + (float)i * w, s + (float)(i + 1) * w);
    }

    // Transient detection: energy-based onset detector with zero-crossing snap
    // sensitivity: 0.0 = few (only loud hits), 1.0 = many (catches ghost notes)
    void detectTransients(float sensitivity = 0.5f) {
        clearSlices();
        int ns = numSamples_.load();
        if (ns == 0) return;
        auto& buf = buffers_[activeBuffer_.load(std::memory_order_acquire)];
        const float* data = buf.getReadPointer(0);
        int startSamp = (int)(startPos_ * ns);
        int endSamp = (int)(endPos_ * ns);
        int regionLen = endSamp - startSamp;
        if (regionLen < 1024) return;

        // Map sensitivity to detector params — conservative by default
        float inv = 1.0f - sensitivity;
        float threshMult = 2.0f + 12.0f * inv * inv;   // 14→2 (much higher floor)
        int minInterOnsetSamples = (int)((120.0f - 80.0f * sensitivity) * 0.001f * (float)getSampleRate());
        float silenceGateDb = -40.0f - 30.0f * sensitivity;
        float silenceGate = std::pow(10.0f, silenceGateDb / 10.0f);

        // Pass 1: compute windowed energy
        constexpr int kWin = 1024;   // wider window = smoother, less false positives
        constexpr int kHop = 256;
        int numFrames = (regionLen - kWin) / kHop + 1;
        if (numFrames < 3) return;

        constexpr int kMaxFrames = 4096;
        if (numFrames > kMaxFrames) numFrames = kMaxFrames;
        float energy[kMaxFrames];

        for (int f = 0; f < numFrames; ++f) {
            float sum = 0.0f;
            const float* ptr = data + startSamp + f * kHop;
            for (int i = 0; i < kWin; ++i) sum += ptr[i] * ptr[i];
            energy[f] = sum / kWin;
        }

        // Compute peak energy for absolute threshold
        float peakEnergy = 0.0f;
        for (int f = 0; f < numFrames; ++f)
            if (energy[f] > peakEnergy) peakEnergy = energy[f];
        float absThreshold = peakEnergy * 0.02f;  // ignore anything below 2% of peak

        // Pass 2: collect onset positions using LOCAL window threshold
        // (EMA was biased toward beginning — local window adapts everywhere)
        float onsetPositions[kMaxSlices];
        int numOnsets = 0;
        constexpr int kLocalWin = 20;  // look-back window for local threshold
        float odfBuf[kMaxFrames];

        // Compute ODF for all frames first
        odfBuf[0] = 0.0f;
        for (int f = 1; f < numFrames; ++f)
            odfBuf[f] = std::max(0.0f, energy[f] - energy[f - 1]);

        int lastOnsetFrame = -minInterOnsetSamples / kHop;

        for (int f = 1; f < numFrames - 1; ++f) {
            // Local threshold: mean ODF over previous kLocalWin frames * sensitivity
            float localMean = 0.0f;
            int winStart = std::max(0, f - kLocalWin);
            for (int w = winStart; w < f; ++w) localMean += odfBuf[w];
            localMean /= (float)(f - winStart);
            float thresh = localMean * threshMult;

            // Also require minimum absolute threshold
            thresh = std::max(thresh, peakEnergy * 0.005f);

            float odfNext = odfBuf[std::min(f + 1, numFrames - 1)];

            if (odfBuf[f] > thresh && odfBuf[f] > odfNext
                && energy[f] > silenceGate && energy[f] > absThreshold
                && (f - lastOnsetFrame) >= (minInterOnsetSamples / kHop)) {
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
                    onsetPositions[numOnsets++] = pos;
                    if (numOnsets >= kMaxSlices) break;
                }
                lastOnsetFrame = f;
            }
        }

        // Create pairs: each onset starts a region, next onset (or sample end) ends it
        for (int i = 0; i < numOnsets; ++i) {
            float pairEnd = (i + 1 < numOnsets) ? onsetPositions[i + 1] : endPos_;
            addSlicePair(onsetPositions[i], pairEnd);
        }
        // If first onset isn't at the very start, add the initial region too
        if (numOnsets > 0 && onsetPositions[0] > startPos_ + 0.01f) {
            addSlicePair(startPos_, onsetPositions[0]);
        }
    }

    // Find nearest zero crossing to pos (searches ±512 samples)
    float findNearestZeroCrossing(float pos) const {
        int ns = numSamples_.load();
        if (ns == 0) return pos;
        auto& buf = buffers_[activeBuffer_.load(std::memory_order_acquire)];
        const float* data = buf.getReadPointer(0);
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

    // Delete a slice region and splice the buffer (tape-cut)
    bool deleteSliceRegion(int sliceIdx, float& cursorPos) {
        int ns = numSamples_.load();
        if (ns == 0 || sliceIdx < 0 || sliceIdx >= sliceCount_) return false;

        float regStart = sliceStarts_[sliceIdx];
        float regEnd = sliceEnds_[sliceIdx];
        int sampStart = (int)(regStart * ns);
        int sampEnd = (int)(regEnd * ns);
        int deleteLen = sampEnd - sampStart;
        if (deleteLen <= 0 || deleteLen >= ns) return false;

        // Guard: stop audio thread from reading during buffer splice
        loading_.store(true, std::memory_order_release);

        int newLen = ns - deleteLen;
        auto& curBuf = buffers_[activeBuffer_.load(std::memory_order_acquire)];
        int numCh = curBuf.getNumChannels();

        // Write splice result into the inactive buffer
        int inactive = 1 - activeBuffer_.load(std::memory_order_acquire);
        buffers_[inactive].setSize(numCh, newLen);
        for (int ch = 0; ch < numCh; ++ch) {
            const float* src = curBuf.getReadPointer(ch);
            float* dst = buffers_[inactive].getWritePointer(ch);
            if (sampStart > 0)
                std::memcpy(dst, src, sizeof(float) * sampStart);
            if (sampEnd < ns)
                std::memcpy(dst + sampStart, src + sampEnd, sizeof(float) * (ns - sampEnd));
        }
        numSamples_.store(0, std::memory_order_release);
        activeBuffer_.store(inactive, std::memory_order_release);
        numSamples_.store(newLen, std::memory_order_release);

        // Remove the deleted slice
        removeSlice(sliceIdx);

        // Rescale remaining slices to new buffer length
        for (int i = 0; i < sliceCount_; ++i) {
            if (sliceStarts_[i] * ns >= sampEnd)
                sliceStarts_[i] = (sliceStarts_[i] * ns - deleteLen) / (float)newLen;
            else
                sliceStarts_[i] = (sliceStarts_[i] * ns) / (float)newLen;
            if (sliceEnds_[i] * ns >= sampEnd)
                sliceEnds_[i] = (sliceEnds_[i] * ns - deleteLen) / (float)newLen;
            else
                sliceEnds_[i] = (sliceEnds_[i] * ns) / (float)newLen;
            sliceStarts_[i] = juce::jlimit(0.0f, 1.0f, sliceStarts_[i]);
            sliceEnds_[i] = juce::jlimit(sliceStarts_[i], 1.0f, sliceEnds_[i]);
        }

        if (startPos_ * ns >= sampEnd)
            startPos_ = (startPos_ * ns - deleteLen) / (float)newLen;
        else
            startPos_ = (startPos_ * ns) / (float)newLen;
        if (endPos_ * ns >= sampEnd)
            endPos_ = (endPos_ * ns - deleteLen) / (float)newLen;
        else
            endPos_ = (endPos_ * ns) / (float)newLen;
        startPos_ = juce::jlimit(0.0f, 1.0f, startPos_);
        endPos_ = juce::jlimit(startPos_, 1.0f, endPos_);

        cursorPos = (float)sampStart / (float)newLen;
        cursorPos = juce::jlimit(0.0f, 1.0f, cursorPos);

        loading_.store(false, std::memory_order_release);
        return true;
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
    void setNumSamplesFromUndo(int n) { numSamples_.store(n); }
    int getNumChannels() const { return numChannels_; }
    double getSampleRate() const { return fileSampleRate_; }
    float getPlaybackPosition() const;
    const juce::AudioBuffer<float>& getBuffer() const { return buffers_[activeBuffer_.load(std::memory_order_acquire)]; }
    juce::AudioBuffer<float>& getBuffer() { return buffers_[activeBuffer_.load(std::memory_order_acquire)]; }  // mutable for undo

    // Voice access (for UI display)
    const Voice& getVoice(int i) const { return voices_[juce::jlimit(0, kMaxVoices - 1, i)]; }
    Voice& getVoice(int i) { return voices_[juce::jlimit(0, kMaxVoices - 1, i)]; }
    int getActiveVoiceCount() const;

private:
    juce::AudioFormatManager formatManager_;
    juce::AudioBuffer<float> buffers_[2];          // double-buffer: audio reads one, UI writes the other
    std::atomic<int> activeBuffer_ { 0 };           // which buffer the audio thread reads from
    std::atomic<int> numSamples_ { 0 };
    std::atomic<bool> loading_ { false };  // guard: audio thread skips when true
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
    static constexpr int kFadeSamples = 64;   // ~1.3ms at 48kHz crossfade
    static constexpr int kLoopXfadeSamples = 128; // ~2.7ms loop boundary crossfade
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
    StretchMode stretchMode_ = StretchMode::WSOLA;  // default to WSOLA
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

    // Step modulation (MOD tab) — 3 independent mod slots per pad
    static constexpr int kModSlots = 3;
    PadMod mods_[kModSlots];
    juce::Random modRng_;
public:
    PadMod&       getMod(int slot)       { return mods_[juce::jlimit(0, kModSlots - 1, slot)]; }
    const PadMod& getMod(int slot) const { return mods_[juce::jlimit(0, kModSlots - 1, slot)]; }

    // Called on trigger — advances all enabled mods, returns per-slot amounts
    float advanceMod(int slot) { return mods_[juce::jlimit(0, kModSlots - 1, slot)].advance(modRng_); }
private:

    // LPG (Buchla-style Low Pass Gate) — vactrol envelope + one-pole filter + VCA
    float lpgVactrol_    = 0.0f;  // vactrol excitation [0..1]
    float lpgEnv_        = 0.0f;  // interpolated envelope (per-sample)
    float lpgEnvInc_     = 0.0f;  // per-sample increment (block-rate update)
    float lpgMemory_     = 0.0f;  // illumination history (successive strikes lengthen decay)
    float lpgFilterZ_L_  = 0.0f;  // one-pole filter state L
    float lpgFilterZ_R_  = 0.0f;  // one-pole filter state R
    int   lpgBlockCount_ = 0;
    static constexpr int kLPGBlockSize = 16;

    // LPG helpers
    float lpgStepEnvelope();
    static inline float lpgFastPow2(float x) {
        x = juce::jlimit(-126.0f, 126.0f, x);
        union { float f; int32_t i; } u;
        u.i = (int32_t)(x * 8388608.0f) + 1065353216;
        return u.f;
    }

    // Pitch quantize mode
    bool pitchQuantize_ = false;
    float compSend_ = 0.0f;  // compressor send amount
    bool compBypass_ = false;  // true = this pad's audio bypasses the bus compressor
    int outputChannel_ = -1;  // -1 = use default (pad index), 0-7 = specific output
    bool sendToMix_ = true;   // send to stereo mix (L/R)

    // Slice system — paired regions
    float sliceStarts_[kMaxSlices] = {};
    float sliceEnds_[kMaxSlices] = {};
    float slicePitch_[kMaxSlices] = {};
    int sliceCount_ = 0;
    bool sliceMode_ = false;
    int selectedSlice_ = 0;
    float slicePitchOffset_ = 0.0f;
    bool slicePending_ = false;
    float slicePendingStart_ = 0.0f;

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
    static constexpr int kWsolaSearchRange = 64;  // ±64 samples search window
    float grainWindow_[kGrainSize];
    void initGrainWindow() {
        for (int i = 0; i < kGrainSize; ++i)
            grainWindow_[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                              * static_cast<float>(i) / static_cast<float>(kGrainSize));
    }

    float getGrainWindow(int sampleInGrain) const {
        return grainWindow_[sampleInGrain & (kGrainSize - 1)];
    }

    // WSOLA: find best grain placement via cross-correlation
    // Returns offset in [-kWsolaSearchRange, +kWsolaSearchRange]
    int wsolaFindBestOffset(const float* src, int ns, double expectedPos,
                            const float* prevTail, int overlapLen) const;

    float readInterpolated(const float* src, double pos, int limit) const;

    // Pre-computed waveform overview (computed once on load, used by paint)
public:
    static constexpr int kOverviewBuckets = 512;
    void computeOverview();
    bool hasOverview() const { return overviewReady_; }
    float getOverviewMin(int bucket) const { return overviewMin_[bucket]; }
    float getOverviewMax(int bucket) const { return overviewMax_[bucket]; }
private:
    float overviewMin_[kOverviewBuckets] = {};
    float overviewMax_[kOverviewBuckets] = {};
    bool overviewReady_ = false;

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
