#include "SampleSlot.h"

namespace grid {

SampleSlot::SampleSlot()
{
    formatManager_.registerBasicFormats();
}

bool SampleSlot::loadFile(const juce::File& file)
{
    if (!file.existsAsFile()) return false;

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file));
    if (!reader) return false;

    // Load into temp buffer first (audio thread might be reading buffer_)
    int newChannels = std::min(static_cast<int>(reader->numChannels), 2);
    int newSamples  = static_cast<int>(reader->lengthInSamples);
    double newRate  = reader->sampleRate;

    juce::AudioBuffer<float> tempBuffer(newChannels, newSamples);
    reader->read(&tempBuffer, 0, newSamples, 0, true, newChannels > 1);

    // Stop playback before swapping (prevents audio thread from reading stale pointers)
    playing_ = false;
    stopping_ = false;

    // Swap — audio thread checks numSamples_ atomically
    numSamples_.store(0);  // signal "not loaded" to audio thread
    buffer_ = std::move(tempBuffer);
    numChannels_ = newChannels;
    fileSampleRate_ = newRate;
    fileName_ = file.getFileName();
    filePath_ = file.getFullPathName();
    readPosition_ = 0.0;
    sourcePos_ = 0.0;
    numSamples_.store(newSamples);  // signal "loaded" — audio thread can read again

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

    playing_ = false;
    stopping_ = false;
    numSamples_.store(0);
    buffer_ = std::move(tempBuffer);
    numChannels_ = newChannels;
    fileSampleRate_ = sr;
    fileName_ = name;
    filePath_ = path;
    readPosition_ = 0.0;
    sourcePos_ = 0.0;
    numSamples_.store(numSamps);

    return true;
}

void SampleSlot::clear()
{
    playing_ = false;
    stopping_ = false;
    numSamples_.store(0);
    buffer_.setSize(0, 0);
    numChannels_ = 0;
    fileName_.clear();
    filePath_.clear();
    readPosition_ = 0.0;
    sourcePos_ = 0.0;
}

void SampleSlot::trigger()
{
    int ns = numSamples_.load();
    if (ns == 0) return;

    // Retrigger guard: ignore triggers within 512 samples (~10ms at 48k)
    // Prevents SSP button system from queuing phantom hits
    if (samplesSinceLastTrigger_ < 512) return;
    samplesSinceLastTrigger_ = 0;

    int startSample = static_cast<int>(startPos_ * (float)ns);
    readPosition_ = static_cast<double>(startSample);
    sourcePos_ = static_cast<double>(startSample);
    grainPos_[0] = sourcePos_;
    grainPos_[1] = sourcePos_;
    grainCounter_ = 0;

    // If already playing, skip fade-in (instant retrigger, no amplitude dip)
    // Only fade from silence when starting fresh
    if (playing_ && !stopping_)
        fadeIn_ = kFadeSamples;  // already at full volume
    else
        fadeIn_ = 0;             // fade in from silence

    stopping_ = false;
    fadeOut_ = 0;
    playing_ = true;
}

void SampleSlot::stop()
{
    if (playing_ && !stopping_) {
        stopping_ = true;
        fadeOut_ = kFadeSamples;
    }
}

float SampleSlot::getPlaybackPosition() const
{
    int ns = numSamples_.load();
    if (ns == 0) return 0.0f;
    return static_cast<float>(sourcePos_ / static_cast<double>(ns));
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

float SampleSlot::getGrainWindow(int sampleInGrain) const
{
    float phase = static_cast<float>(sampleInGrain) / static_cast<float>(kGrainSize);
    return 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * phase);
}

void SampleSlot::process(float* outL, float* outR, int numSamples)
{
    int ns = numSamples_.load();
    if ((!playing_ && !stopping_) || ns == 0) return;

    const int startSample = static_cast<int>(startPos_ * (float)ns);
    const int endSample = static_cast<int>(endPos_ * (float)ns);
    const int regionLen = endSample - startSample;
    if (regionLen <= 0) { playing_ = false; stopping_ = false; return; }

    const float* srcL = buffer_.getReadPointer(0);
    const float* srcR = (numChannels_ > 1) ? buffer_.getReadPointer(1) : srcL;

    const float panL = std::cos((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
    const float panR = std::sin((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);

    // Fade: convert ms to samples, cap at region length
    int fadeInSamples = static_cast<int>(fadeInMs_ * 0.001f * (float)fileSampleRate_);
    int fadeOutSamples = static_cast<int>(fadeOutMs_ * 0.001f * (float)fileSampleRate_);
    // Cap each at region length
    fadeInSamples = std::min(fadeInSamples, regionLen);
    fadeOutSamples = std::min(fadeOutSamples, regionLen);

    const float effStretch = getEffectiveStretch();
    const bool useGranular = (effStretch < 0.99f || effStretch > 1.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        // ── Anti-click envelope ──────────────────────────────────────────
        float env = 1.0f;
        if (fadeIn_ < kFadeSamples) {
            env = static_cast<float>(fadeIn_) / static_cast<float>(kFadeSamples);
            fadeIn_++;
        }
        if (stopping_) {
            if (fadeOut_ > 0) {
                env *= static_cast<float>(fadeOut_) / static_cast<float>(kFadeSamples);
                fadeOut_--;
            } else {
                playing_ = false;
                stopping_ = false;
                return;
            }
        }

        float sL = 0.0f, sR = 0.0f;

        if (!useGranular)
        {
            int idx = static_cast<int>(readPosition_);
            if (idx >= endSample || idx >= ns) {
                if (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop) {
                    readPosition_ = static_cast<double>(startSample);
                    idx = startSample;
                } else {
                    // Fade out at end instead of hard stop
                    playing_ = false;
                    return;
                }
            }
            sL = readInterpolated(srcL, readPosition_, ns);
            sR = readInterpolated(srcR, readPosition_, ns);
            readPosition_ += static_cast<double>(pitchRate_);
            sourcePos_ = readPosition_;
        }
        else
        {
            int halfGrain = kGrainSize / 2;

            float winA = getGrainWindow(grainCounter_);
            float winB = getGrainWindow((grainCounter_ + halfGrain) % kGrainSize);

            sL = readInterpolated(srcL, grainPos_[0], ns) * winA
               + readInterpolated(srcL, grainPos_[1], ns) * winB;
            sR = readInterpolated(srcR, grainPos_[0], ns) * winA
               + readInterpolated(srcR, grainPos_[1], ns) * winB;

            grainPos_[0] += static_cast<double>(pitchRate_);
            grainPos_[1] += static_cast<double>(pitchRate_);

            sourcePos_ += static_cast<double>(pitchRate_) / static_cast<double>(effStretch);

            grainCounter_++;

            if (grainCounter_ >= kGrainSize) {
                grainCounter_ = 0;
                grainPos_[0] = sourcePos_;
            }
            if (grainCounter_ == halfGrain) {
                grainPos_[1] = sourcePos_;
            }

            if (sourcePos_ >= static_cast<double>(endSample)) {
                if (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop) {
                    sourcePos_ = static_cast<double>(startSample);
                    grainPos_[0] = sourcePos_;
                    grainPos_[1] = sourcePos_;
                    grainCounter_ = 0;
                } else {
                    playing_ = false;
                    return;
                }
            }

            readPosition_ = sourcePos_;
        }

        // ── Fade in/out envelope ─────────────────────────────────────────
        float fadeEnv = 1.0f;
        double posInRegion = sourcePos_ - static_cast<double>(startSample);
        double distFromEnd = static_cast<double>(endSample) - sourcePos_;

        // Fade in
        if (fadeInSamples > 0 && posInRegion < fadeInSamples) {
            float t = (float)(posInRegion / (double)fadeInSamples);
            fadeEnv *= (fadeInCurve_ == 0) ? t : t * t;
        }
        // Fade out — multiplied so overlapping regions blend naturally
        if (fadeOutSamples > 0 && distFromEnd < fadeOutSamples) {
            float t = (float)(distFromEnd / (double)fadeOutSamples);
            fadeEnv *= (fadeOutCurve_ == 0) ? t : t * t;
        }

        outL[i] += sL * volume_ * panL * env * fadeEnv;
        outR[i] += sR * volume_ * panR * env * fadeEnv;
    }
}

} // namespace grid
