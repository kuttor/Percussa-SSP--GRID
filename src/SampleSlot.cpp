#include "SampleSlot.h"

namespace grid {

SampleSlot::SampleSlot()
{
    formatManager_.registerBasicFormats();
    initGrainWindow();
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

    loading_.store(false, std::memory_order_release);
    return true;
}

void SampleSlot::clear()
{
    loading_.store(true, std::memory_order_release);

    for (auto& v : voices_) v.reset();
    numSamples_.store(0, std::memory_order_release);
    buffers_[0].setSize(0, 0);
    buffers_[1].setSize(0, 0);
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
    triggerWithVelocity(volume_);
}

void SampleSlot::triggerWithOffset(int sampleOffset)
{
    triggerWithVelocity(volume_);
    voices_[0].startOffset = sampleOffset;
}

void SampleSlot::triggerWithVelocityAndOffset(float vel, int sampleOffset)
{
    triggerWithVelocity(vel);
    voices_[0].startOffset = sampleOffset;
}

void SampleSlot::triggerWithVelocity(float vel)
{
    int ns = numSamples_.load();
    if (ns == 0) return;

    // Retrigger guard
    if (samplesSinceLastTrigger_ < kRetriggerGuard) return;
    samplesSinceLastTrigger_ = 0;

    // Voice stealing: if voice 0 is playing, move it to a tail voice
    // for a short crossfade (eliminates clicks at slice boundaries)
    if (voices_[0].playing) {
        // Find a free tail slot (voices 1-3)
        int tail = -1;
        for (int i = kMaxVoices - 1; i >= 1; --i) {
            if (!voices_[i].playing) { tail = i; break; }
        }
        if (tail < 0) tail = kMaxVoices - 1;  // steal oldest tail
        voices_[tail] = voices_[0];
        voices_[tail].stopping = true;
        voices_[tail].fadeOut = kFadeSamples;
    }

    // Start fresh voice on slot 0
    voices_[0].reset();
    startVoice(voices_[0], vel);
    // Reset filter state for clean start
    svfIc1L_ = svfIc2L_ = svfIc1R_ = svfIc2R_ = 0.0f;
    for (int b = 0; b < 3; ++b) {
        fmtIc1L_[b] = fmtIc2L_[b] = fmtIc1R_[b] = fmtIc2R_[b] = 0.0f;
    }
    lofiPhaseL_ = lofiPhaseR_ = lofiHeldL_ = lofiHeldR_ = 0.0f;

    // LPG: strike the vactrol on trigger
    if (filterType_ == FilterType::LPG) {
        float strike = filterCutoff_ / 20000.0f;  // cutoff knob → strike amount [0..1]
        lpgVactrol_ = std::min(1.0f, vel * strike);
        lpgMemory_ = std::min(1.0f, lpgMemory_ + 0.15f * vel);  // accumulate heat
        lpgBlockCount_ = 0;
    }
}

void SampleSlot::stop()
{
    if (voices_[0].playing && !voices_[0].stopping) {
        voices_[0].stopping = true;
        voices_[0].fadeOut = kFadeSamples;
    }
}

void SampleSlot::stopAll()
{
    for (auto& v : voices_) {
        if (v.playing && !v.stopping) {
            v.stopping = true;
            v.fadeOut = kFadeSamples;
        }
    }
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
    int i0 = static_cast<int>(pos);
    if (i0 < 0) i0 = 0;
    if (i0 >= limit) i0 = limit - 1;
    int i1 = i0 + 1;
    if (i1 >= limit) i1 = i0;
    float frac = static_cast<float>(pos - i0);
    return src[i0] + frac * (src[i1] - src[i0]);
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
    const int regionLen = endSample - startSample;
    if (regionLen <= 0) return;

    const float* srcL = buf.getReadPointer(0);
    const float* srcR = (numChannels_ > 1) ? buf.getReadPointer(1) : srcL;

    const float panL = cachedPanL_;
    const float panR = cachedPanR_;

    const float effStretch = getEffectiveStretch();
    const double sampleRateRatio = fileSampleRate_ / outputSampleRate_;

    int fadeInSamples = static_cast<int>(fadeInMs_ * 0.001f * (float)fileSampleRate_);
    int fadeOutSamples = static_cast<int>(fadeOutMs_ * 0.001f * (float)fileSampleRate_);
    fadeInSamples = std::min(fadeInSamples, regionLen);
    fadeOutSamples = std::min(fadeOutSamples, regionLen);

    for (int vi = 0; vi < kMaxVoices; ++vi) {
        Voice& v = voices_[vi];
        if (!v.playing) continue;

        processVoice(v, outL, outR, numSamples, srcL, srcR, ns,
                     startSample, endSample, regionLen,
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
    const bool useGranular = (effStretch < 0.99f || effStretch > 1.01f);
    // Slot-level pitch + per-slice pitch offset
    const float combinedPitchRate = pitchRate_ * std::pow(2.0f, slicePitchOffset_ / 12.0f);
    const double rateStep = static_cast<double>(combinedPitchRate) * sampleRateRatio;
    const double rateStepStretched = (effStretch > 0.001f)
        ? rateStep / static_cast<double>(effStretch) : rateStep;
    const float invFadeIn = (fadeInSamples > 0) ? 1.0f / (float)fadeInSamples : 0.0f;
    const float invFadeOut = (fadeOutSamples > 0) ? 1.0f / (float)fadeOutSamples : 0.0f;

    // Determine the fade-out length for this voice
    // Tail voices get a longer fade, normal voices get kFadeSamples
    const int voiceFadeSamples = (v.fadeOut > kFadeSamples) ? v.fadeOut : kFadeSamples;

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
            env = static_cast<float>(v.fadeIn) / static_cast<float>(kFadeSamples);
            v.fadeIn++;
        }
        if (v.stopping) {
            if (v.fadeOut > 0) {
                env *= static_cast<float>(v.fadeOut) / static_cast<float>(voiceFadeSamples);
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
            int idx = static_cast<int>(v.readPosition);
            if (idx >= endSample || idx >= ns) {
                if (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop) {
                    v.readPosition = static_cast<double>(startSample);
                    idx = startSample;
                } else {
                    v.playing = false;
                    return;
                }
            }
            sL = readInterpolated(srcL, v.readPosition, ns);
            sR = readInterpolated(srcR, v.readPosition, ns);
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
                v.grainPos[1] = newPos;
            }

            if (v.sourcePos >= static_cast<double>(endSample)) {
                if (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop) {
                    v.sourcePos = static_cast<double>(startSample);
                    v.grainPos[0] = v.sourcePos;
                    v.grainPos[1] = v.sourcePos;
                    v.grainCounter = 0;
                } else {
                    v.playing = false;
                    return;
                }
            }

            v.readPosition = v.sourcePos;
        }

        // ── Position-based fade in/out ───────────────────────────────────
        float fadeEnv = 1.0f;
        double posInRegion = v.sourcePos - static_cast<double>(startSample);
        double distFromEnd = static_cast<double>(endSample) - v.sourcePos;

        if (fadeInSamples > 0 && posInRegion < fadeInSamples) {
            float t = (float)posInRegion * invFadeIn;
            fadeEnv *= (fadeInCurve_ == 0) ? t : t * t;
        }
        if (fadeOutSamples > 0 && distFromEnd < fadeOutSamples) {
            float t = (float)distFromEnd * invFadeOut;
            fadeEnv *= (fadeOutCurve_ == 0) ? t : t * t;
        }

        // ── Lo-fi sampler emulation ──────────────────────────────────────
        if (lofiActive) {
            // Sample rate reduction via ZOH (zero-order hold)
            if (lofiRateRatio < 0.999f) {
                lofiPhaseL_ += lofiRateRatio;
                if (lofiPhaseL_ >= 1.0f) {
                    lofiPhaseL_ -= 1.0f;
                    lofiHeldL_ = sL;
                    lofiHeldR_ = sR;
                }
                sL = lofiHeldL_;
                sR = lofiHeldR_;
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
                if (++lpgBlockCount_ >= kLPGBlockSize) {
                    lpgBlockCount_ = 0;
                    float newEnv = lpgStepEnvelope();
                    lpgEnvInc_ = (newEnv - lpgEnv_) * (1.0f / (float)kLPGBlockSize);
                }
                lpgEnv_ += lpgEnvInc_;
                float e = juce::jlimit(0.0f, 1.0f, lpgEnv_);

                // Map envelope to cutoff: 20Hz (closed) → 20kHz (open)
                float fc = 20.0f * lpgFastPow2(9.9658f * e);  // 20 * 1000^e
                float twoPiOverSr = 6.283185307f / (float)outputSampleRate_;
                float g = 1.0f - std::exp(-twoPiOverSr * std::min(fc, 20000.0f));

                // One-pole lowpass (6dB/oct — authentic Buchla 292 slope)
                lpgFilterZ_L_ += g * (sL - lpgFilterZ_L_);
                lpgFilterZ_R_ += g * (sR - lpgFilterZ_R_);

                // VCA: squared envelope for natural amplitude curve
                float vca = e * e;
                sL = lpgFilterZ_L_ * vca;
                sR = lpgFilterZ_R_ * vca;
            } else if (filterType_ == FilterType::Formant) {
                // Input drive for vocal presence
                float fmtDrive = 1.0f + filterReso_ * 0.4f;
                float dL = sL * fmtDrive, dR = sR * fmtDrive;
                float sumL = 0.0f, sumR = 0.0f;
                for (int b = 0; b < 3; ++b) {
                    sumL += filterBPF(dL, fmtIc1L_[b], fmtIc2L_[b],
                                       fmtG[b], fmtA1[b], fmtA2[b], fmtA3[b]) * fmtAmp[b];
                    sumR += filterBPF(dR, fmtIc1R_[b], fmtIc2R_[b],
                                       fmtG[b], fmtA1[b], fmtA2[b], fmtA3[b]) * fmtAmp[b];
                }
                // Formant gain: 3 BPFs sum to ~0.3-0.5 of input level. Boost to match.
                sL = sumL * 2.2f;
                sR = sumR * 2.2f;
            } else if (filterType_ == FilterType::MS20) {
                sL = filterMS20(sL, svfIc1L_, svfIc2L_, fG, fK, fA1, fA2, fA3);
                sR = filterMS20(sR, svfIc1R_, svfIc2R_, fG, fK, fA1, fA2, fA3);
            } else {
                sL = filterSVF(sL, svfIc1L_, svfIc2L_, fG, fK, fA1, fA2, fA3);
                sR = filterSVF(sR, svfIc1R_, svfIc2R_, fG, fK, fA1, fA2, fA3);
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

        outL[i] += sL * v.velocity * panL * env * fadeEnv * muteGain_;
        outR[i] += sR * v.velocity * panR * env * fadeEnv * muteGain_;
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

float SampleSlot::lpgStepEnvelope()
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

    if (lpgVactrol_ > 0.001f) {
        // State-dependent dual-rate decay
        float mix = lpgVactrol_;
        float coeff = decaySlow + mix * (decayFast - decaySlow);
        // Memory effect: repeated strikes slow decay
        float memFactor = 1.0f + lpgMemory_ * 0.5f;
        coeff = 1.0f - (1.0f - coeff) / memFactor;
        lpgVactrol_ = coeff * lpgVactrol_;
    } else {
        lpgVactrol_ = 0.0f;
    }

    // Decay illumination memory
    lpgMemory_ *= 0.99995f;

    return lpgVactrol_;
}

} // namespace grid
