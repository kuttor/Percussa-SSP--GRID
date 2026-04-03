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

    juce::AudioBuffer<float> tempBuffer(newChannels, newSamples);
    reader->read(&tempBuffer, 0, newSamples, 0, true, newChannels > 1);

    // Stop all voices before swapping
    for (auto& v : voices_) v.reset();

    numSamples_.store(0);
    buffer_ = std::move(tempBuffer);
    numChannels_ = newChannels;
    fileSampleRate_ = newRate;
    fileName_ = file.getFileName();
    filePath_ = file.getFullPathName();
    numSamples_.store(newSamples);

    return true;
}

bool SampleSlot::loadFromBuffer(const juce::AudioBuffer<float>& src, int numSamps,
                                 double sr, const juce::String& name, const juce::String& path)
{
    if (numSamps <= 0) return false;

    int newChannels = std::min(src.getNumChannels(), 2);
    juce::AudioBuffer<float> tempBuffer(newChannels, numSamps);
    for (int ch = 0; ch < newChannels; ++ch)
        tempBuffer.copyFrom(ch, 0, src, ch, 0, numSamps);

    for (auto& v : voices_) v.reset();
    numSamples_.store(0);
    buffer_ = std::move(tempBuffer);
    numChannels_ = newChannels;
    fileSampleRate_ = sr;
    fileName_ = name;
    filePath_ = path;
    numSamples_.store(numSamps);

    return true;
}

void SampleSlot::clear()
{
    for (auto& v : voices_) v.reset();
    numSamples_.store(0);
    buffer_.setSize(0, 0);
    numChannels_ = 0;
    fileName_.clear();
    filePath_.clear();
    reversed_ = false;
    normalizeGain_ = 1.0f;
    chokeGroup_ = ChokeGroup::None;
    midiChannel_ = 0;
}

void SampleSlot::setReversed(bool r)
{
    if (r == reversed_) return;
    reversed_ = r;

    int ns = numSamples_.load();
    if (ns <= 0) return;

    numSamples_.store(0);
    for (int ch = 0; ch < numChannels_; ++ch) {
        float* data = buffer_.getWritePointer(ch);
        std::reverse(data, data + ns);
    }
    numSamples_.store(ns);
}

void SampleSlot::normalize()
{
    int ns = numSamples_.load();
    if (ns <= 0) return;

    float peak = 0.0f;
    for (int ch = 0; ch < numChannels_; ++ch) {
        const float* data = buffer_.getReadPointer(ch);
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
        float* data = buffer_.getWritePointer(ch);
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
    // Set offset on the newly started voice so processVoice skips those samples
    voices_[0].startOffset = sampleOffset;
}

void SampleSlot::triggerWithVelocity(float vel)
{
    int ns = numSamples_.load();
    if (ns == 0) return;

    // Retrigger guard
    if (samplesSinceLastTrigger_ < kRetriggerGuard) return;
    samplesSinceLastTrigger_ = 0;

    // Kill and restart voice 0
    voices_[0].reset();
    startVoice(voices_[0], vel);
    // Reset filter state for clean start
    svfIc1L_ = svfIc2L_ = svfIc1R_ = svfIc2R_ = 0.0f;
    for (int b = 0; b < 3; ++b) {
        fmtIc1L_[b] = fmtIc2L_[b] = fmtIc1R_[b] = fmtIc2R_[b] = 0.0f;
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
    int ns = numSamples_.load();
    if (ns == 0) return;

    // Check if any voice is active
    bool anyActive = false;
    for (auto& v : voices_)
        if (v.playing) { anyActive = true; break; }
    if (!anyActive) return;

    const int startSample = static_cast<int>(startPos_ * (float)ns);
    const int endSample = static_cast<int>(endPos_ * (float)ns);
    const int regionLen = endSample - startSample;
    if (regionLen <= 0) return;

    const float* srcL = buffer_.getReadPointer(0);
    const float* srcR = (numChannels_ > 1) ? buffer_.getReadPointer(1) : srcL;

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
    // Slot-level pitch (same for all voices)
    const double rateStep = static_cast<double>(pitchRate_) * sampleRateRatio;
    const double rateStepStretched = (effStretch > 0.001f)
        ? rateStep / static_cast<double>(effStretch) : rateStep;
    const float invFadeIn = (fadeInSamples > 0) ? 1.0f / (float)fadeInSamples : 0.0f;
    const float invFadeOut = (fadeOutSamples > 0) ? 1.0f / (float)fadeOutSamples : 0.0f;

    // Determine the fade-out length for this voice
    // Tail voices get a longer fade, normal voices get kFadeSamples
    const int voiceFadeSamples = (v.fadeOut > kFadeSamples) ? v.fadeOut : kFadeSamples;

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
            fK = 2.0f - 2.0f * filterReso_;
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
                v.grainPos[0] = v.sourcePos;
            }
            if (v.grainCounter == halfGrain) {
                v.grainPos[1] = v.sourcePos;
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

        // ── Filter ─────────────────────────────────────────────────────────
        if (filterActive) {
            if (filterType_ == FilterType::Formant) {
                // Sum 3 parallel BPF bands (vowel formants)
                float sumL = 0.0f, sumR = 0.0f;
                for (int b = 0; b < 3; ++b) {
                    sumL += filterBPF(sL, fmtIc1L_[b], fmtIc2L_[b],
                                       fmtG[b], fmtA1[b], fmtA2[b], fmtA3[b]) * fmtAmp[b];
                    sumR += filterBPF(sR, fmtIc1R_[b], fmtIc2R_[b],
                                       fmtG[b], fmtA1[b], fmtA2[b], fmtA3[b]) * fmtAmp[b];
                }
                sL = sumL;
                sR = sumR;
            } else if (filterType_ == FilterType::MS20) {
                sL = filterMS20(sL, svfIc1L_, svfIc2L_, fG, fK, fA1, fA2, fA3);
                sR = filterMS20(sR, svfIc1R_, svfIc2R_, fG, fK, fA1, fA2, fA3);
            } else {
                sL = filterSVF(sL, svfIc1L_, svfIc2L_, fG, fK, fA1, fA2, fA3);
                sR = filterSVF(sR, svfIc1R_, svfIc2R_, fG, fK, fA1, fA2, fA3);
            }
        }

        outL[i] += sL * v.velocity * panL * env * fadeEnv;
        outR[i] += sR * v.velocity * panR * env * fadeEnv;
    }
}

} // namespace grid
