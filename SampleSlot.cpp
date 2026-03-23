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
    playing_ = true;
}

void SampleSlot::stop()
{
    playing_ = false;
}

float SampleSlot::getPlaybackPosition() const
{
    if (numSamples_ == 0) return 0.0f;
    return static_cast<float>(readPosition_ / static_cast<double>(numSamples_));
}

void SampleSlot::process(float* outL, float* outR, int numSamples)
{
    if (!playing_ || numSamples_ == 0) return;

    const int endSample = static_cast<int>(endPos_ * numSamples_);
    const float* srcL = buffer_.getReadPointer(0);
    const float* srcR = (numChannels_ > 1) ? buffer_.getReadPointer(1) : srcL;

    // Pan law: equal-power
    const float panL = std::cos((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
    const float panR = std::sin((pan_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi);

    for (int i = 0; i < numSamples; ++i)
    {
        int idx = static_cast<int>(readPosition_);

        if (idx >= endSample || idx >= numSamples_)
        {
            // End of region
            switch (mode_)
            {
                case PadMode::Loop:
                case PadMode::ClockedLoop:
                    readPosition_ = static_cast<double>(static_cast<int>(startPos_ * numSamples_));
                    idx = static_cast<int>(readPosition_);
                    break;

                case PadMode::OneShot:
                case PadMode::ClockedBar:
                default:
                    playing_ = false;
                    return;
            }
        }

        float sL = srcL[idx];
        float sR = srcR[idx];

        outL[i] += sL * volume_ * panL;
        outR[i] += sR * volume_ * panR;

        readPosition_ += static_cast<double>(pitchRate_);
    }
}

} // namespace grid
