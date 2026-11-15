#include "SampleSlot.h"

namespace grid {

SampleSlot::SampleSlot()
{
    formatManager_.registerBasicFormats();
    initGrainWindow();
    for (int i = 0; i < kMaxSlices; ++i) sliceCC_[i] = -1;
}

bool SampleSlot::loadFile(const juce::File& file)
{
    if (!file.existsAsFile()) return false;

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file));
    if (!reader) return false;

    int newChannels = std::min(static_cast<int>(reader->numChannels), 2);
    int newSamples  = static_cast<int>(reader->lengthInSamples);
    double newRate  = reader->sampleRate;

    // Atomic guard: audio thread will skip process() while loading
    loading_.store(true, std::memory_order_release);

    // Write to the INACTIVE buffer (audio thread is reading the active one)
    int inactive = 1 - activeBuffer_.load(std::memory_order_acquire);
    buffers_[inactive].setSize(newChannels, newSamples);
    reader->read(&buffers_[inactive], 0, newSamples, 0, true, newChannels > 1);

    // Stop all voices, zero out sample count
    for (auto& v : voices_) v.reset();
    numSamples_.store(0, std::memory_order_release);

    // Swap: audio thread now sees the new buffer
    activeBuffer_.store(inactive, std::memory_order_release);

    numChannels_ = newChannels;
    fileSampleRate_ = newRate;
    fileName_ = file.getFileName();
    filePath_ = file.getFullPathName();
    clearSlices();
    startPos_ = 0.0f;
    endPos_ = 1.0f;
    numSamples_.store(newSamples, std::memory_order_release);

    computeOverview();
    loading_.store(false, std::memory_order_release);
    return true;
}

bool SampleSlot::loadFromBuffer(const juce::AudioBuffer<float>& src, int numSamps,
                                 double sr, const juce::String& name, const juce::String& path)
{
    if (numSamps <= 0) return false;

    int newChannels = std::min(src.getNumChannels(), 2);

    loading_.store(true, std::memory_order_release);

    int inactive = 1 - activeBuffer_.load(std::memory_order_acquire);
    buffers_[inactive].setSize(newChannels, numSamps);
    for (int ch = 0; ch < newChannels; ++ch)
        buffers_[inactive].copyFrom(ch, 0, src, ch, 0, numSamps);

    for (auto& v : voices_) v.reset();
    numSamples_.store(0, std::memory_order_release);

    activeBuffer_.store(inactive, std::memory_order_release);

    numChannels_ = newChannels;
    fileSampleRate_ = sr;
    fileName_ = name;
    filePath_ = path;
    clearSlices();
    startPos_ = 0.0f;
    endPos_ = 1.0f;
    numSamples_.store(numSamps, std::memory_order_release);

    computeOverview();
    loading_.store(false, std::memory_order_release);
    return true;
}

void SampleSlot::clear()
{
    loading_.store(true, std::memory_order_release);

    for (auto& v : voices_) v.reset();
    numSamples_.store(0, std::memory_order_release);
    // DO NOT free buffers here — audio thread may still be reading from them.
    // buffers_[0].setSize(0, 0);  // ← was causing heap corruption
    // buffers_[1].setSize(0, 0);  // ← use-after-free if audio thread is mid-read
    // The buffer memory stays allocated but unused. Next loadFile() will overwrite.
    activeBuffer_.store(0, std::memory_order_release);
    numChannels_ = 0;
    fileName_.clear();
    filePath_.clear();
    reversed_ = false;
    normalizeGain_ = 1.0f;
    chokeGroup_ = ChokeGroup::None;
    midiChannel_ = 0;
    clearSlices();
    startPos_ = 0.0f;
    endPos_ = 1.0f;
    overviewReady_ = false;

    loading_.store(false, std::memory_order_release);
}

void SampleSlot::setReversed(bool r)
{
    if (r == reversed_) return;
    reversed_ = r;

    int ns = numSamples_.load();
    if (ns <= 0) return;

    numSamples_.store(0);
    auto& buf = buffers_[activeBuffer_.load(std::memory_order_acquire)];
    for (int ch = 0; ch < numChannels_; ++ch) {
        float* data = buf.getWritePointer(ch);
        std::reverse(data, data + ns);
    }
    overviewReady_ = false;  // force waveform visual refresh
    numSamples_.store(ns);
}

void SampleSlot::normalize()
{
    int ns = numSamples_.load();
    if (ns <= 0) return;

    float peak = 0.0f;
    auto& buf = buffers_[activeBuffer_.load(std::memory_order_acquire)];
    for (int ch = 0; ch < numChannels_; ++ch) {
        const float* data = buf.getReadPointer(ch);
        for (int s = 0; s < ns; ++s) {
            float a = std::abs(data[s]);
            if (a > peak) peak = a;
        }
    }
    if (peak < 0.001f) return;

    float gain = 0.95f / peak;
    normalizeGain_ = gain;

    numSamples_.store(0);
    for (int ch = 0; ch < numChannels_; ++ch) {
        float* data = buf.getWritePointer(ch);
        for (int s = 0; s < ns; ++s)
            data[s] *= gain;
    }
    numSamples_.store(ns);
}

// ═══════════════════════════════════════════════════════════════════════════
// Voice management
// ═══════════════════════════════════════════════════════════════════════════

void SampleSlot::startVoice(Voice& v, float vel)
{
    int ns = numSamples_.load();
    if (ns == 0) return;

    int startSample = static_cast<int>(startPos_ * (float)ns);
    v.readPosition = static_cast<double>(startSample);
    v.sourcePos    = static_cast<double>(startSample);
    v.grainPos[0]  = v.sourcePos;
    v.grainPos[1]  = v.sourcePos;
    v.grainCounter = 0;
    v.fadeIn        = 0;
    v.fadeOut        = 0;
    v.startOffset   = 0;
    v.stopping      = false;
    v.playing       = true;
    v.velocity      = vel;
}

void SampleSlot::trigger()
{
    triggerInternal(1.0f, 0);  // no MIDI velocity scaling — volume_ applied at output
}

void SampleSlot::triggerWithOffset(int sampleOffset)
{
    triggerInternal(1.0f, sampleOffset);
}

void SampleSlot::triggerWithVelocityAndOffset(float vel, int sampleOffset)
{
    triggerInternal(vel, sampleOffset);
}

void SampleSlot::triggerWithVelocity(float vel)
{
    triggerInternal(vel, 0);
}

void SampleSlot::beginFadeOut(Voice& v, int len)
{
    if (!v.playing) return;
    len = std::max(1, len);
    // Continue from the voice's current fade level instead of restarting at
    // full gain (restarting caused an upward jump = click when a voice that
    // was already fading got stolen or stopped again).
    const float level = v.stopLevel();
    const int newRemaining = std::max(1, (int)std::lround(level * (float)len));
    if (v.stopping && v.fadeOut <= newRemaining && v.fadeOutLen > 0)
        return;  // an equal-or-faster fade is already running — keep it
    v.stopping   = true;
    v.fadeOutLen = len;
    v.fadeOut    = newRemaining;
}

void SampleSlot::stealVoice0(int fadeLen, int fadeDelay)
{
    // Find a free tail slot (voices 1-3); if all are busy, reuse the one
    // that is closest to silent rather than always the last one.
    int tail = -1;
    for (int i = kMaxVoices - 1; i >= 1; --i)
        if (!voices_[i].playing) { tail = i; break; }
    if (tail < 0) {
        float best = 2.0f;
        for (int i = 1; i < kMaxVoices; ++i) {
            float lvl = voices_[i].stopLevel();
            if (lvl < best) { best = lvl; tail = i; }
        }
    }

    Voice& t = voices_[tail];
    t = voices_[0];

    // Freeze what the old voice was playing so that changes made right
    // after this trigger (slice region, step-mod Start/End, note pitch,
    // per-slice pitch) can't make the fading voice jump.
    const int ns = numSamples_.load();
    if (t.regionStart < 0) {
        t.regionStart = juce::jlimit(0, std::max(0, ns - 1), (int)(startPos_ * (float)ns));
        t.regionEnd   = juce::jlimit(t.regionStart + 1, std::max(1, ns), (int)(endPos_ * (float)ns));
    }
    if (t.frozenRate <= 0.0f)
        t.frozenRate = (t.lastRate > 0.0f) ? t.lastRate
                                           : pitchRate_ * std::pow(2.0f, slicePitchOffset_ / 12.0f);

    beginFadeOut(t, fadeLen);
    t.fadeDelay = std::max(0, fadeDelay);
}

bool SampleSlot::triggerInternal(float vel, int sampleOffset)
{
    int ns = numSamples_.load();
    if (ns == 0) return false;

    // Retrigger guard. (Previously the offset was applied to voice 0 even
    // when the trigger was dropped here, silencing the *playing* voice for
    // a few samples — a dropout click.)
    if (samplesSinceLastTrigger_ < kRetriggerGuard) return false;
    samplesSinceLastTrigger_ = 0;

    // Voice stealing: crossfade the old voice out. With a short attack the
    // new onset masks a ~3 ms fade; with a long attack the old voice would
    // otherwise drop out audibly while the new one is still near silence,
    // so the crossfade follows the attack time (capped).
    if (voices_[0].playing) {
        const int len = juce::jlimit(msToOutSamples(kStopFadeMs),
                                     msToOutSamples(kStealFadeMaxMs),
                                     msToOutSamples(fadeInMs_));
        stealVoice0(len, sampleOffset);
    }

    // Start fresh voice on slot 0 (reset() also clears its filter/FX state;
    // the tail keeps its own, so the fading voice is not disturbed).
    voices_[0].reset();
    startVoice(voices_[0], vel);
    voices_[0].startOffset = std::max(0, sampleOffset);

    // LPG: strike the vactrol on trigger
    if (filterType_ == FilterType::LPG) {
        float strike = filterCutoff_ / 20000.0f;  // cutoff knob → strike amount [0..1]
        voices_[0].lpgVactrol = std::min(1.0f, vel * strike);
        voices_[0].lpgBlockCount = kLPGBlockSize - 1;  // update envelope on first sample
        lpgMemory_ = std::min(1.0f, lpgMemory_ + 0.15f * vel);  // accumulate heat
    }
    return true;
}

void SampleSlot::triggerSlice(int sliceIdx)
{
    triggerSliceWithOffset(sliceIdx, 1.0f, 0);
}

void SampleSlot::triggerSlice(int sliceIdx, float velocity)
{
    triggerSliceWithOffset(sliceIdx, velocity, 0);
}

void SampleSlot::triggerSliceWithOffset(int sliceIdx, float velocity, int sampleOffset)
{
    int ns = numSamples_.load();
    if (ns == 0) return;
    if (sliceIdx < 0 || sliceIdx >= sliceCount_) return;

    // Use the normal trigger path so voice stealing happens. It allocates
    // voice 0 at startPos_ * ns; we then point voice 0 at the slice region.
    // The slot's startPos_/endPos_ stay untouched, so the visual trim and
    // grid mini-waveform don't darken out.
    // If the retrigger guard dropped the trigger, voice 0 is still the
    // previous voice — don't touch it (moving it would jump = click).
    if (!triggerInternal(juce::jlimit(0.0f, 1.0f, velocity), sampleOffset)) return;

    float s = sliceStarts_[sliceIdx];
    float e = sliceEnds_[sliceIdx];
    int vStart = juce::jlimit(0, ns - 1, (int)(s * (float)ns));
    int vEnd   = juce::jlimit(vStart + 1, ns, (int)(e * (float)ns));

    voices_[0].regionStart = vStart;
    voices_[0].regionEnd   = vEnd;
    voices_[0].readPosition = (double)vStart;
    voices_[0].sourcePos    = (double)vStart;
    voices_[0].grainPos[0]  = (double)vStart;
    voices_[0].grainPos[1]  = (double)vStart;

    // Apply the slice's per-slice pitch offset (same as the old audition path
    // — kept so pitch edits set per-slice continue to behave the same)
    slicePitchOffset_ = slicePitch_[sliceIdx];
    selectedSlice_ = sliceIdx;
}

void SampleSlot::stop()
{
    beginFadeOut(voices_[0], msToOutSamples(kStopFadeMs));
}

void SampleSlot::stopAll()
{
    const int len = msToOutSamples(kStopFadeMs);
    for (auto& v : voices_) beginFadeOut(v, len);
}

void SampleSlot::release()
{
    const int len = std::max(msToOutSamples(kStopFadeMs), msToOutSamples(fadeOutMs_));
    beginFadeOut(voices_[0], len);
}

bool SampleSlot::isPlaying() const
{
    for (auto& v : voices_)
        if (v.playing) return true;
    return false;
}

bool SampleSlot::isStopping() const
{
    return voices_[0].stopping;
}

int SampleSlot::getActiveVoiceCount() const
{
    int count = 0;
    for (auto& v : voices_)
        if (v.playing) count++;
    return count;
}

float SampleSlot::getPlaybackPosition() const
{
    int ns = numSamples_.load();
    if (ns == 0) return 0.0f;
    // Return primary voice position
    return static_cast<float>(voices_[0].sourcePos / static_cast<double>(ns));
}

float SampleSlot::readInterpolated(const float* src, double pos, int limit) const
{
    int i1 = static_cast<int>(pos);
    if (i1 < 0) i1 = 0;
    if (i1 >= limit) i1 = limit - 1;
    float frac = static_cast<float>(pos - i1);

    // 4-point Hermite interpolation (dramatically cleaner than linear for pitched playback)
    int i0 = (i1 > 0) ? i1 - 1 : i1;
    int i2 = (i1 + 1 < limit) ? i1 + 1 : i1;
    int i3 = (i1 + 2 < limit) ? i1 + 2 : i2;

    float y0 = src[i0], y1 = src[i1], y2 = src[i2], y3 = src[i3];
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Process — iterate all active voices
// ═══════════════════════════════════════════════════════════════════════════

void SampleSlot::process(float* outL, float* outR, int numSamples)
{
    // Skip processing while buffer is being swapped (race condition guard)
    if (loading_.load(std::memory_order_acquire)) return;

    int ns = numSamples_.load();
    if (ns == 0) return;

    // Check if any voice is active
    bool anyActive = false;
    for (auto& v : voices_)
        if (v.playing) { anyActive = true; break; }
    if (!anyActive) return;

    // Re-check loading after voice check (narrow the race window)
    if (loading_.load(std::memory_order_acquire)) return;

    // Snapshot buffer pointers — if loading starts after this point,
    // we're committed to this block but the pointers are still valid
    // because loadFile writes to the INACTIVE buffer then swaps
    auto& buf = buffers_[activeBuffer_.load(std::memory_order_acquire)];
    if (buf.getNumSamples() < ns || buf.getNumChannels() == 0) return;

    const int startSample = static_cast<int>(startPos_ * (float)ns);
    const int endSample = static_cast<int>(endPos_ * (float)ns);

    const float* srcL = buf.getReadPointer(0);
    const float* srcR = (numChannels_ > 1) ? buf.getReadPointer(1) : srcL;

    const float panL = cachedPanL_;
    const float panR = cachedPanR_;

    const float effStretch = getEffectiveStretch();
    const double sampleRateRatio = fileSampleRate_ / outputSampleRate_;

    // ── Tape character: wow + flutter modulation (slot-level) ────────────
    // Updates tapeRateMod_, which processVoice() reads when computing the
    // per-voice read step. Wow = slow drift (0.7 Hz), Flutter = faster
    // wobble (8 Hz). Depths are tiny (±0.5% wow, ±0.2% flutter) — tape is
    // subtle; more than this sounds broken, not tapey.
    {
        const double blockSeconds = (double)numSamples / outputSampleRate_;
        float wowMul = 1.0f, flutterMul = 1.0f;
        if (tapeWow_ > 0.005f) {
            wowPhase_ += (float)(2.0 * 3.14159265 * 0.7 * blockSeconds);
            if (wowPhase_ > 6.28318f) wowPhase_ -= 6.28318f;
            wowMul = 1.0f + std::sin(wowPhase_) * tapeWow_ * 0.005f;
        }
        if (tapeFlutter_ > 0.005f) {
            flutterPhase_ += (float)(2.0 * 3.14159265 * 8.0 * blockSeconds);
            if (flutterPhase_ > 6.28318f) flutterPhase_ -= 6.28318f;
            flutterMul = 1.0f + std::sin(flutterPhase_) * tapeFlutter_ * 0.002f;
        }
        tapeRateMod_ = tapeRate_ * wowMul * flutterMul;
    }

    // Fade lengths in source samples. Clamped per voice in processVoice()
    // against the region that voice actually plays (a slice can be much
    // shorter than the pad's trim).
    const int fadeInSamples = static_cast<int>(fadeInMs_ * 0.001f * (float)fileSampleRate_);
    const int fadeOutSamples = static_cast<int>(fadeOutMs_ * 0.001f * (float)fileSampleRate_);

    for (int vi = 0; vi < kMaxVoices; ++vi) {
        Voice& v = voices_[vi];
        if (!v.playing) continue;

        // If this voice was triggered as a slice audition, it carries its
        // own region bounds — use those instead of the slot's live trim.
        // This lets slice audition coexist with the user's trim setting
        // and keeps the visual waveform from blacking out.
        int vStart  = (v.regionStart >= 0) ? v.regionStart : startSample;
        int vEnd    = (v.regionEnd   >= 0) ? v.regionEnd   : endSample;
        int vRegion = vEnd - vStart;
        if (vRegion <= 0) continue;

        processVoice(v, outL, outR, numSamples, srcL, srcR, ns,
                     vStart, vEnd, vRegion,
                     panL, panR, effStretch, sampleRateRatio,
                     fadeInSamples, fadeOutSamples);
    }
}

void SampleSlot::processVoice(Voice& v, float* outL, float* outR, int numSamples,
                               const float* srcL, const float* srcR, int ns,
                               int startSample, int endSample, int regionLen,
                               float panL, float panR, float effStretch, double sampleRateRatio,
                               int fadeInSamples, int fadeOutSamples)
{
    const bool useGranular = (effStretch < 0.99f || effStretch > 1.01f) || (pitchMode_ == 1);
    const bool isLoop = (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop);
    // Slot-level pitch + per-slice pitch offset (or the frozen rate of a
    // fading tail voice) × modulated tape rate (varispeed + wow/flutter)
    const float baseRate = (v.frozenRate > 0.0f)
        ? v.frozenRate
        : pitchRate_ * std::pow(2.0f, slicePitchOffset_ / 12.0f);
    v.lastRate = baseRate;
    const float combinedPitchRate = baseRate * tapeRateMod_;
    const double rateStep = static_cast<double>(combinedPitchRate) * sampleRateRatio;
    const double rateStepStretched = (effStretch > 0.001f)
        ? rateStep / static_cast<double>(effStretch) : rateStep;

    fadeInSamples  = std::min(fadeInSamples,  regionLen);
    fadeOutSamples = std::min(fadeOutSamples, regionLen);
    const float invFadeIn = (fadeInSamples > 0) ? 1.0f / (float)fadeInSamples : 0.0f;
    const float invFadeOut = (fadeOutSamples > 0) ? 1.0f / (float)fadeOutSamples : 0.0f;
    // Loop seam crossfade: ~10 ms when the loop is long enough (at most a
    // quarter of the loop), never shorter than the old 128 samples.
    const int loopXfadeLen = std::max(kLoopXfadeSamples,
                                      std::min(kLoopXfadeMaxSamples, regionLen / 4));
    const bool loopXfade = isLoop && regionLen > loopXfadeLen * 2;

    // ── Lo-fi pre-computation (once per block) ─────────────────────────
    const bool lofiActive = (lofiMode_ != LofiMode::Off);
    float lofiBits = 0.0f, lofiLevels = 0.0f, lofiInvLevels = 0.0f;
    float lofiRateRatio = 1.0f;
    bool lofiCompand = false;
    if (lofiActive) {
        switch (lofiMode_) {
            case LofiMode::Bit8:  lofiBits = 8.0f;  lofiRateRatio = 1.0f; break;
            case LofiMode::Bit12: lofiBits = 12.0f; lofiRateRatio = 1.0f; break;
            case LofiMode::SP1200: lofiBits = 12.0f; lofiRateRatio = 26040.0f / (float)outputSampleRate_; break;
            case LofiMode::MPC60: lofiBits = 12.0f; lofiRateRatio = 40000.0f / (float)outputSampleRate_; lofiCompand = true; break;
            default: break;
        }
        lofiLevels = std::pow(2.0f, lofiBits - 1.0f);
        lofiInvLevels = 1.0f / lofiLevels;
    }

    // ── Filter coefficients (computed once per block) ───────────────────
    const bool filterActive = (filterType_ != FilterType::Off);
    float fG = 0.0f, fK = 0.0f, fA1 = 0.0f, fA2 = 0.0f, fA3 = 0.0f;

    // Formant: 3 BPF bands with vowel-morphing frequencies
    float fmtG[3] = {}, fmtK[3] = {}, fmtA1[3] = {}, fmtA2[3] = {}, fmtA3[3] = {};
    float fmtAmp[3] = { 1.0f, 0.8f, 0.6f };  // band amplitudes (F1 loudest)

    if (filterActive) {
        float fc = juce::jlimit(20.0f, 20000.0f, filterCutoff_);
        const float sr = (float)outputSampleRate_;

        if (filterType_ == FilterType::Formant) {
            // Vowel formant frequencies (F1, F2, F3) for A, E, I, O, U
            static const float vowels[5][3] = {
                { 730.0f, 1090.0f, 2440.0f },  // A
                { 660.0f, 1720.0f, 2410.0f },  // E
                { 270.0f, 2290.0f, 3010.0f },  // I
                { 570.0f,  840.0f, 2410.0f },  // O
                { 300.0f,  870.0f, 2240.0f },  // U
            };
            // Map cutoff to vowel morph position (log scale: 20Hz=0, 20kHz=1)
            float norm = std::log(fc / 20.0f) / std::log(1000.0f);
            norm = juce::jlimit(0.0f, 1.0f, norm);
            float pos = norm * 4.0f;  // 0-4 across 5 vowels
            int v0 = juce::jlimit(0, 3, (int)pos);
            int v1 = v0 + 1;
            float frac = pos - (float)v0;
            // Bandwidth from resonance: higher reso = narrower Q
            float bw = 2.0f - filterReso_ * 1.8f;  // k: 2.0 (wide) to 0.2 (narrow)
            for (int b = 0; b < 3; ++b) {
                float freq = vowels[v0][b] * (1.0f - frac) + vowels[v1][b] * frac;
                freq = juce::jlimit(20.0f, sr * 0.49f, freq);
                fmtG[b] = std::tan(juce::MathConstants<float>::pi * freq / sr);
                fmtK[b] = bw;
                fmtA1[b] = 1.0f / (1.0f + fmtG[b] * (fmtG[b] + fmtK[b]));
                fmtA2[b] = fmtG[b] * fmtA1[b];
                fmtA3[b] = fmtG[b] * fmtA2[b];
            }
        } else {
            // Standard SVF coefficients (LPF, HPF, BPF, Notch, MS20)
            fG = std::tan(juce::MathConstants<float>::pi * fc / sr);
            // Resonance: k=2 (no reso) → k=0.15 (screaming). Floor prevents self-oscillation.
            // Soft clip on output keeps it safe at high Q.
            fK = std::max(0.15f, 2.0f - 1.85f * filterReso_);
            fA1 = 1.0f / (1.0f + fG * (fG + fK));
            fA2 = fG * fA1;
            fA3 = fG * fA2;
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (!v.playing) return;

        // Sample-accurate trigger: skip output samples before trigger point
        if (v.startOffset > 0) {
            v.startOffset--;
            continue;
        }

        // ── Anti-click envelope ──────────────────────────────────────────
        float env = 1.0f;
        if (v.fadeIn < kFadeSamples) {
            env = smooth01(static_cast<float>(v.fadeIn) / static_cast<float>(kFadeSamples));
            v.fadeIn++;
        }
        if (v.stopping) {
            if (v.fadeDelay > 0) {
                // Steal fade waits for the new voice's sample-accurate start
                env *= smooth01(v.stopLevel());
                v.fadeDelay--;
            } else if (v.fadeOut > 0) {
                env *= smooth01(v.stopLevel());
                v.fadeOut--;
            } else {
                v.playing = false;
                v.stopping = false;
                return;
            }
        }

        float sL = 0.0f, sR = 0.0f;

        if (!useGranular)
        {
            if (v.readPosition >= (double)endSample || v.readPosition >= (double)ns) {
                if (isLoop) {
                    // The crossfade below has already blended into
                    // [start, start + X) by the time we reach the end, so
                    // continue from start + X. (Jumping back to `start` —
                    // as before — replayed those X samples and produced a
                    // step at every loop pass.)
                    double over = std::max(0.0, v.readPosition - (double)endSample);
                    v.readPosition = (double)startSample
                                   + (loopXfade ? (double)loopXfadeLen : 0.0) + over;
                    if (v.readPosition >= (double)endSample) v.readPosition = (double)startSample;
                    v.wrapped = true;
                } else {
                    v.playing = false;
                    return;
                }
            }
            sL = readInterpolated(srcL, v.readPosition, ns);
            sR = readInterpolated(srcR, v.readPosition, ns);

            // End-of-sample fade for one-shots, measured in *output* samples
            // so it stays ~1.3 ms even when the sample is pitched way up.
            if (!isLoop) {
                double distOut = ((double)endSample - v.readPosition) / std::max(rateStep, 1e-6);
                if (distOut < (double)kFadeSamples) {
                    float endFade = smooth01((float)(distOut / (double)kFadeSamples));
                    sL *= endFade;
                    sR *= endFade;
                }
            }

            // Loop crossfade: blend towards the loop start while approaching
            // the end; at the end we are fully on [start + X] (see wrap above).
            if (loopXfade) {
                double distToEnd = (double)endSample - v.readPosition;
                if (distToEnd > 0.0 && distToEnd < (double)loopXfadeLen) {
                    float xfade = smooth01((float)(distToEnd / (double)loopXfadeLen));
                    double loopPos = (double)startSample + ((double)loopXfadeLen - distToEnd);
                    float xL = readInterpolated(srcL, loopPos, ns);
                    float xR = readInterpolated(srcR, loopPos, ns);
                    sL = sL * xfade + xL * (1.0f - xfade);
                    sR = sR * xfade + xR * (1.0f - xfade);
                }
            }

            v.readPosition += rateStep;
            v.sourcePos = v.readPosition;
        }
        else
        {
            int halfGrain = kGrainSize / 2;

            float winA = getGrainWindow(v.grainCounter);
            float winB = getGrainWindow((v.grainCounter + halfGrain) % kGrainSize);

            sL = readInterpolated(srcL, v.grainPos[0], ns) * winA
               + readInterpolated(srcL, v.grainPos[1], ns) * winB;
            sR = readInterpolated(srcR, v.grainPos[0], ns) * winA
               + readInterpolated(srcR, v.grainPos[1], ns) * winB;

            v.grainPos[0] += rateStep;
            v.grainPos[1] += rateStep;

            v.sourcePos += rateStepStretched;

            v.grainCounter++;

            if (v.grainCounter >= kGrainSize) {
                v.grainCounter = 0;
                double newPos = v.sourcePos;
                // WSOLA: search for best grain alignment
                if (stretchMode_ == StretchMode::WSOLA) {
                    int prevIdx = static_cast<int>(v.grainPos[0]) - halfGrain;
                    if (prevIdx >= 0 && prevIdx + halfGrain < ns) {
                        int offset = wsolaFindBestOffset(srcL, ns, newPos,
                                                          srcL + prevIdx, std::min(halfGrain, 128));
                        newPos += offset;
                    }
                }
                newPos = juce::jlimit((double)startSample, std::max((double)startSample, (double)endSample - 1.0), newPos);
                v.grainPos[0] = newPos;
            }
            if (v.grainCounter == halfGrain) {
                double newPos = v.sourcePos;
                if (stretchMode_ == StretchMode::WSOLA) {
                    int prevIdx = static_cast<int>(v.grainPos[1]) - halfGrain;
                    if (prevIdx >= 0 && prevIdx + halfGrain < ns) {
                        int offset = wsolaFindBestOffset(srcL, ns, newPos,
                                                          srcL + prevIdx, std::min(halfGrain, 128));
                        newPos += offset;
                    }
                }
                newPos = juce::jlimit((double)startSample, std::max((double)startSample, (double)endSample - 1.0), newPos);
                v.grainPos[1] = newPos;
            }

            if (v.sourcePos >= static_cast<double>(endSample)) {
                if (isLoop) {
                    // Wrap only the source position. The two running grains
                    // finish their windows and the next grains spawn at the
                    // loop start, so the seam is crossfaded by the grain
                    // windows. (Resetting both grains + the window phase
                    // here made the output jump at every loop pass.)
                    v.sourcePos -= static_cast<double>(regionLen);
                    if (v.sourcePos < static_cast<double>(startSample))
                        v.sourcePos = static_cast<double>(startSample);
                    v.wrapped = true;
                } else {
                    v.playing = false;
                    return;
                }
            }

            // End-of-sample fade for granular one-shots (matches non-granular path).
            // Without this, granular playback hitting endSample causes the same
            // click that the non-granular path's distToEnd<64 check avoids.
            if (!isLoop) {
                double distOut = (static_cast<double>(endSample) - v.sourcePos)
                               / std::max(rateStepStretched, 1e-6);
                if (distOut < (double)kFadeSamples) {
                    float endFade = smooth01((float)(distOut / (double)kFadeSamples));
                    sL *= endFade;
                    sR *= endFade;
                }
            }

            v.readPosition = v.sourcePos;
        }

        // ── Position-based fade in/out ───────────────────────────────────
        // t is clamped to [0,1] defensively — when slice playback mutates
        // startPos_/endPos_ mid-flight (PluginProcessor::processSliceCV),
        // other voices on the same slot see stale region refs and posInRegion
        // or distFromEnd can go negative, producing negative gain and an
        // audible click. Clamp keeps the envelope monotonic.
        float fadeEnv = 1.0f;
        double posInRegion = v.sourcePos - static_cast<double>(startSample);
        double distFromEnd = static_cast<double>(endSample) - v.sourcePos;

        // Loop modes: the attack only shapes the first pass, and the
        // release is applied when the loop is stopped (release()). Applying
        // them on every loop pass faded the loop out to silence at its end
        // and jumped back to full level at its start (or vice versa) — a pop
        // on every cycle unless attack and release were both set.
        if (fadeInSamples > 0 && !v.wrapped && posInRegion < fadeInSamples) {
            float t = juce::jlimit(0.0f, 1.0f, (float)posInRegion * invFadeIn);
            fadeEnv *= (fadeInCurve_ == 0) ? t : t * t;
        }
        if (fadeOutSamples > 0 && !isLoop && distFromEnd < fadeOutSamples) {
            float t = juce::jlimit(0.0f, 1.0f, (float)distFromEnd * invFadeOut);
            fadeEnv *= (fadeOutCurve_ == 0) ? t : t * t;
        }

        // ── Lo-fi sampler emulation ──────────────────────────────────────
        if (lofiActive) {
            // Sample rate reduction via ZOH (zero-order hold)
            if (lofiRateRatio < 0.999f) {
                v.lofiPhase += lofiRateRatio;
                if (v.lofiPhase >= 1.0f) {
                    v.lofiPhase -= 1.0f;
                    v.lofiHeldL = sL;
                    v.lofiHeldR = sR;
                }
                sL = v.lofiHeldL;
                sR = v.lofiHeldR;
            }
            // Bit crush (with optional µ-law companding for MPC-60)
            if (lofiCompand) {
                sL = muExpand(bitCrush(muCompress(sL), lofiLevels, lofiInvLevels));
                sR = muExpand(bitCrush(muCompress(sR), lofiLevels, lofiInvLevels));
            } else {
                sL = bitCrush(sL, lofiLevels, lofiInvLevels);
                sR = bitCrush(sR, lofiLevels, lofiInvLevels);
            }
        }

        // ── Filter ─────────────────────────────────────────────────────────
        if (filterActive) {
            if (filterType_ == FilterType::LPG) {
                // Block-rate vactrol envelope update
                if (++v.lpgBlockCount >= kLPGBlockSize) {
                    v.lpgBlockCount = 0;
                    float newEnv = lpgStepEnvelope(v, &v == &voices_[0]);
                    v.lpgEnvInc = (newEnv - v.lpgEnv) * (1.0f / (float)kLPGBlockSize);
                }
                v.lpgEnv += v.lpgEnvInc;
                float e = juce::jlimit(0.0f, 1.0f, v.lpgEnv);

                // Map envelope to cutoff: 20Hz (closed) → 20kHz (open)
                float fc = 20.0f * lpgFastPow2(9.9658f * e);  // 20 * 1000^e
                float twoPiOverSr = 6.283185307f / (float)outputSampleRate_;
                float g = 1.0f - std::exp(-twoPiOverSr * std::min(fc, 20000.0f));

                // One-pole lowpass (6dB/oct — authentic Buchla 292 slope)
                v.lpgZL += g * (sL - v.lpgZL);
                v.lpgZR += g * (sR - v.lpgZR);

                // VCA: squared envelope for natural amplitude curve
                float vca = e * e;
                sL = v.lpgZL * vca;
                sR = v.lpgZR * vca;
            } else if (filterType_ == FilterType::Formant) {
                // Input drive for vocal presence
                float fmtDrive = 1.0f + filterReso_ * 0.4f;
                float dL = sL * fmtDrive, dR = sR * fmtDrive;
                float sumL = 0.0f, sumR = 0.0f;
                for (int b = 0; b < 3; ++b) {
                    sumL += filterBPF(dL, v.fmtIc1L[b], v.fmtIc2L[b],
                                       fmtG[b], fmtA1[b], fmtA2[b], fmtA3[b]) * fmtAmp[b];
                    sumR += filterBPF(dR, v.fmtIc1R[b], v.fmtIc2R[b],
                                       fmtG[b], fmtA1[b], fmtA2[b], fmtA3[b]) * fmtAmp[b];
                }
                // Formant gain: 3 BPFs sum to ~0.3-0.5 of input level. Boost to match.
                sL = sumL * 2.2f;
                sR = sumR * 2.2f;
            } else if (filterType_ == FilterType::MS20) {
                sL = filterMS20(sL, v.svfIc1L, v.svfIc2L, fG, fK, fA1, fA2, fA3);
                sR = filterMS20(sR, v.svfIc1R, v.svfIc2R, fG, fK, fA1, fA2, fA3);
            } else if (filterType_ == FilterType::RingMod) {
                // Ring modulator: multiply signal by sine oscillator
                // Cutoff repurposed as frequency (20-2000Hz), reso as dry/wet mix
                float freq = 20.0f + (filterCutoff_ / 20000.0f) * 1980.0f;
                float phaseInc = freq / (float)outputSampleRate_;
                v.ringModPhase += phaseInc;
                if (v.ringModPhase >= 1.0f) v.ringModPhase -= 1.0f;
                float mod = std::sin(v.ringModPhase * 6.283185307f);
                float mix = filterReso_;
                sL = sL * (1.0f - mix) + sL * mod * mix;
                sR = sR * (1.0f - mix) + sR * mod * mix;
            } else if (filterType_ == FilterType::WaveFolder) {
                // Two-stage wave folder with symmetry control
                // Cutoff repurposed as drive (1x-10x), reso as symmetry (-1..+1)
                float drive = 1.0f + (filterCutoff_ / 20000.0f) * 9.0f;
                float sym = filterReso_ * 2.0f - 1.0f;  // 0..1 → -1..+1
                // Stage 1: drive + asymmetric offset
                float fL = sL * drive + sym * 0.3f;
                float fR = sR * drive + sym * 0.3f;
                // Fold: sin() gives natural smooth folding
                fL = std::sin(fL * 1.5707963f);  // pi/2
                fR = std::sin(fR * 1.5707963f);
                // Stage 2: second fold for more harmonic complexity
                fL = std::sin(fL * drive * 0.5f * 1.5707963f);
                fR = std::sin(fR * drive * 0.5f * 1.5707963f);
                sL = fL;
                sR = fR;
            } else if (filterType_ == FilterType::CombFilter) {
                // Comb filter: short delay with feedback
                // Cutoff repurposed as comb frequency (40-1000Hz), reso as feedback
                float freq = 40.0f + (filterCutoff_ / 20000.0f) * 960.0f;
                int delaySamples = juce::jlimit(1, kCombMaxDelay - 1,
                    (int)((float)outputSampleRate_ / freq));
                float fb = filterReso_ * 0.95f;  // cap at 0.95 to prevent runaway
                // Read from delay
                int readIdx = (v.combWriteIdx - delaySamples + kCombMaxDelay) % kCombMaxDelay;
                float delL = v.combBufL[readIdx];
                float delR = v.combBufR[readIdx];
                // Write: input + feedback * delayed
                float writeL = sL + fb * delL;
                float writeR = sR + fb * delR;
                v.combBufL[v.combWriteIdx] = writeL;
                v.combBufR[v.combWriteIdx] = writeR;
                v.combWriteIdx = (v.combWriteIdx + 1) % kCombMaxDelay;
                // Output: mix of direct + delayed (classic comb sound)
                sL = sL + delL;
                sR = sR + delR;
            } else {
                sL = filterSVF(sL, v.svfIc1L, v.svfIc2L, fG, fK, fA1, fA2, fA3);
                sR = filterSVF(sR, v.svfIc1R, v.svfIc2R, fG, fK, fA1, fA2, fA3);
            }
            // Soft clip: tame resonance peaks without hard clipping
            sL = fastTanh(sL);
            sR = fastTanh(sR);
        }

        // ── Mute fade ramp ────────────────────────────────────────────────
        if (muteGain_ != muteTarget_) {
            if (muteFadeMs_ < 0.5f) {
                muteGain_ = muteTarget_;  // instant
            } else {
                float rate = 1.0f / (muteFadeMs_ * 0.001f * (float)outputSampleRate_);
                if (muteTarget_ > muteGain_)
                    muteGain_ = std::min(muteTarget_, muteGain_ + rate);
                else
                    muteGain_ = std::max(muteTarget_, muteGain_ - rate);
            }
        }

        // ── Tape character per-sample DSP ──────────────────────────────
        // HF rolloff: one-pole LP, cutoff = f(tape rate, rolloff amount).
        // Slower tape → darker (mimics head response). Rolloff=0 → bypass.
        if (tapeHFRolloff_ > 0.005f) {
            // Cutoff: 18 kHz at rolloff=0 fades to ~2 kHz at rolloff=1,
            // further halved when tape rate drops to 0.5x.
            float baseCutoff = 18000.0f - tapeHFRolloff_ * 16000.0f;
            float speedScale = std::min(tapeRateMod_, 1.0f);  // only darken on slowdown
            float cutoff = baseCutoff * (0.5f + 0.5f * speedScale);
            float alpha = 1.0f - std::exp(-2.0f * 3.14159265f * cutoff / (float)outputSampleRate_);
            v.tapeLpL += alpha * (sL - v.tapeLpL);
            v.tapeLpR += alpha * (sR - v.tapeLpR);
            sL = v.tapeLpL;
            sR = v.tapeLpR;
        }

        // Head bump: low-mid emphasis around ~120 Hz via LP-then-add.
        // Mix LP-state back into dry signal at scaled amount.
        if (tapeHeadBump_ > 0.005f) {
            const float bumpAlpha = 0.012f;  // ~90 Hz at 48k
            v.tapeBumpLpL += bumpAlpha * (sL - v.tapeBumpLpL);
            v.tapeBumpLpR += bumpAlpha * (sR - v.tapeBumpLpR);
            sL += v.tapeBumpLpL * tapeHeadBump_ * 0.5f;
            sR += v.tapeBumpLpR * tapeHeadBump_ * 0.5f;
        }

        // Saturation: tanh with drive scaled by amount.
        if (tapeSaturation_ > 0.005f) {
            float drive = 1.0f + tapeSaturation_ * 2.5f;
            float invTanh = 1.0f / std::tanh(drive);
            sL = std::tanh(sL * drive) * invTanh;
            sR = std::tanh(sR * drive) * invTanh;
        }

        // ── Overdrive saturation (volume > 100%) ───────────────────────────
        if (volume_ > 1.01f) {
            float gain = volume_;
            // Warm tanh saturation — gets progressively dirtier above 100%
            float invTanh = 1.0f / std::tanh(gain);
            sL = std::tanh(sL * gain) * invTanh;
            sR = std::tanh(sR * gain) * invTanh;
            // Already applied volume via saturation, only multiply velocity
            outL[i] += sL * v.velocity * panL * env * fadeEnv * muteGain_;
            outR[i] += sR * v.velocity * panR * env * fadeEnv * muteGain_;
            continue;  // skip normal output below
        }

        outL[i] += sL * v.velocity * volume_ * panL * env * fadeEnv * muteGain_;
        outR[i] += sR * v.velocity * volume_ * panR * env * fadeEnv * muteGain_;
    }
}

int SampleSlot::wsolaFindBestOffset(const float* src, int ns, double expectedPos,
                                     const float* prevTail, int overlapLen) const
{
    if (overlapLen <= 0 || !prevTail) return 0;

    int bestOffset = 0;
    float bestCorr = -1e30f;
    int ePos = static_cast<int>(expectedPos);

    for (int d = -kWsolaSearchRange; d <= kWsolaSearchRange; ++d) {
        int pos = ePos + d;
        if (pos < 0 || pos + overlapLen >= ns) continue;

        float corr = 0.0f;
        // Simplified cross-correlation (every 4th sample for speed)
        for (int j = 0; j < overlapLen; j += 4)
            corr += src[pos + j] * prevTail[j];

        if (corr > bestCorr) {
            bestCorr = corr;
            bestOffset = d;
        }
    }
    return bestOffset;
}

float SampleSlot::lpgStepEnvelope(Voice& v, bool decayMemory)
{
    float decay = filterReso_;  // resonance knob → decay [0..1]

    // Attack: 0.5ms (hard) to 5ms (soft)
    float strike = filterCutoff_ / 20000.0f;
    float attSec = 0.0005f + (1.0f - strike) * 0.0045f;
    float attackCoeff = std::exp(-1.0f / (attSec * (float)outputSampleRate_));

    // Fast decay: 20-100ms
    float fdSec = 0.020f + decay * 0.080f;
    float decayFast = std::exp(-1.0f / (fdSec * (float)outputSampleRate_));

    // Slow tail: 200ms-3s
    float sdSec = 0.200f + decay * 2.8f;
    float decaySlow = std::exp(-1.0f / (sdSec * (float)outputSampleRate_));

    if (v.lpgVactrol > 0.001f) {
        // State-dependent dual-rate decay
        float mix = v.lpgVactrol;
        float coeff = decaySlow + mix * (decayFast - decaySlow);
        // Memory effect: repeated strikes slow decay
        float memFactor = 1.0f + lpgMemory_ * 0.5f;
        coeff = 1.0f - (1.0f - coeff) / memFactor;
        v.lpgVactrol = coeff * v.lpgVactrol;
    } else {
        v.lpgVactrol = 0.0f;
    }

    // Decay illumination memory (once per step, not once per active voice)
    if (decayMemory) lpgMemory_ *= 0.99995f;

    return v.lpgVactrol;
}

void SampleSlot::computeOverview()
{
    overviewReady_ = false;
    int ns = numSamples_.load(std::memory_order_acquire);
    if (ns <= 0) return;

    int active = activeBuffer_.load(std::memory_order_acquire);
    const float* data = buffers_[active].getReadPointer(0);

    for (int b = 0; b < kOverviewBuckets; ++b) {
        int s0 = (b * ns) / kOverviewBuckets;
        int s1 = ((b + 1) * ns) / kOverviewBuckets;
        s1 = std::min(s1, ns);
        float mn = 0.0f, mx = 0.0f;
        for (int s = s0; s < s1; ++s) {
            if (data[s] < mn) mn = data[s];
            if (data[s] > mx) mx = data[s];
        }
        overviewMin_[b] = mn;
        overviewMax_[b] = mx;
    }
    overviewReady_ = true;
}

} // namespace grid
