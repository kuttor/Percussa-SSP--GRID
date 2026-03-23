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

    numChannels_ = std::min(static_cast<int>(reader->numChannels), 2);
    numSamples_  = static_cast<int>(reader->lengthInSamples);
    fileSampleRate_ = reader->sampleRate;

    buffer_.setSize(numChannels_, numSamples_);
    reader->read(&buffer_, 0, numSamples_, 0, true, numChannels_ > 1);

    fileName_ = file.getFileName();
    readPosition_ = 0.0;
    playing_ = false;

    return true;
}

void SampleSlot::clear()
{
    buffer_.setSize(0, 0);
    numSamples_ = 0;
    numChannels_ = 0;
    fileName_.clear();
    playing_ = false;
    readPosition_ = 0.0;
}

void SampleSlot::trigger()
{
    if (numSamples_ == 0) return;
    int startSample = static_cast<int>(startPos_ * numSamples_);
    readPosition_ = static_cast<double>(startSample);
    sourcePos_ = static_cast<double>(startSample);
    grainPos_[0] = sourcePos_;
    grainPos_[1] = sourcePos_;
    grainCounter_ = 0;
    playing_ = true;
}

void SampleSlot::stop()
{
    playing_ = false;
}

float SampleSlot::getPlaybackPosition() const
{
    if (numSamples_ == 0) return 0.0f;
    return static_cast<float>(sourcePos_ / static_cast<double>(numSamples_));
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
    // Hanning window
    float phase = static_cast<float>(sampleInGrain) / static_cast<float>(kGrainSize);
    return 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * phase);
}

void SampleSlot::process(float* outL, float* outR, int numSamples)
{
    if (!playing_ || numSamples_ == 0) return;

    const int startSample = static_cast<int>(startPos_ * numSamples_);
    const int endSample = static_cast<int>(endPos_ * numSamples_);
    const int regionLen = endSample - startSample;
    if (regionLen <= 0) { playing_ = false; return; }

    const float* srcL = buffer_.getReadPointer(0);
    const float* srcR = (numChannels_ > 1) ? buffer_.getReadPointer(1) : srcL;

    const float panL = std::cos((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
    const float panR = std::sin((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);

    // Simple path: no time stretch
    const bool useGranular = (timeStretch_ < 0.99f || timeStretch_ > 1.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        float sL = 0.0f, sR = 0.0f;

        if (!useGranular)
        {
            // Direct read — pitch only
            int idx = static_cast<int>(readPosition_);
            if (idx >= endSample || idx >= numSamples_) {
                if (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop) {
                    readPosition_ = static_cast<double>(startSample);
                    idx = startSample;
                } else {
                    playing_ = false; return;
                }
            }
            sL = srcL[idx];
            sR = srcR[idx];
            readPosition_ += static_cast<double>(pitchRate_);
            sourcePos_ = readPosition_;
        }
        else
        {
            // Granular time stretch — two overlapping grains
            int halfGrain = kGrainSize / 2;

            // Grain A: active during first half of cycle
            // Grain B: active during second half, offset by halfGrain
            float winA = getGrainWindow(grainCounter_);
            float winB = getGrainWindow((grainCounter_ + halfGrain) % kGrainSize);

            sL = readInterpolated(srcL, grainPos_[0], numSamples_) * winA
               + readInterpolated(srcL, grainPos_[1], numSamples_) * winB;
            sR = readInterpolated(srcR, grainPos_[0], numSamples_) * winA
               + readInterpolated(srcR, grainPos_[1], numSamples_) * winB;

            // Advance grain read positions at pitch rate
            grainPos_[0] += static_cast<double>(pitchRate_);
            grainPos_[1] += static_cast<double>(pitchRate_);

            // Advance source position (how fast we move through the file)
            sourcePos_ += static_cast<double>(pitchRate_) / static_cast<double>(timeStretch_);

            grainCounter_++;

            // Reset grain A every grainSize samples
            if (grainCounter_ >= kGrainSize) {
                grainCounter_ = 0;
                grainPos_[0] = sourcePos_;
            }
            // Reset grain B at the halfway point
            if (grainCounter_ == halfGrain) {
                grainPos_[1] = sourcePos_;
            }

            // Check bounds
            if (sourcePos_ >= static_cast<double>(endSample)) {
                if (mode_ == PadMode::Loop || mode_ == PadMode::ClockedLoop) {
                    sourcePos_ = static_cast<double>(startSample);
                    grainPos_[0] = sourcePos_;
                    grainPos_[1] = sourcePos_;
                    grainCounter_ = 0;
                } else {
                    playing_ = false; return;
                }
            }

            readPosition_ = sourcePos_;
        }

        outL[i] += sL * volume_ * panL;
        outR[i] += sR * volume_ * panR;
    }
}

} // namespace grid
