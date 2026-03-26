#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace grid {

PluginProcessor::PluginProcessor()
    : AudioProcessor(getBusesProperties())
{
    sampleRootPath_ = findSSPSamplePath();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Read gate CVs ────────────────────────────────────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = trigChannel(pad);
        if (ch >= numChannels) continue;

        bool triggered = false;
        for (int s = 0; s < numSamples; ++s)
        {
            bool high = (buffer.getSample(ch, s) > kTrigThreshold);
            if (high && !gateHigh_[pad]) {
                triggered = true;
                break;
            }
            gateHigh_[pad] = high;
        }
        gateHigh_[pad] = (buffer.getSample(ch, numSamples - 1) > kTrigThreshold);

        if (triggered)
            engine_.trigger(pad);
    }

    // ── Read pitch CVs ───────────────────────────────────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = pitchChannel(pad);
        if (ch >= numChannels) continue;

        float voct = buffer.getSample(ch, numSamples - 1);
        if (std::abs(voct) > 0.01f)
            engine_.getSlot(pad).setPitchSemitones(voct * 12.0f);
    }

    // ── Read start/end CVs (0-1V = 0-100% position) ─────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int sCh = startChannel(pad);
        int eCh = endChannel(pad);
        auto& slot = engine_.getSlot(pad);

        if (sCh < numChannels) {
            float sv = buffer.getSample(sCh, numSamples - 1);
            if (std::abs(sv) > 0.005f)
                slot.setStartPos(juce::jlimit(0.0f, 1.0f, sv));
        }
        if (eCh < numChannels) {
            float ev = buffer.getSample(eCh, numSamples - 1);
            if (std::abs(ev) > 0.005f)
                slot.setEndPos(juce::jlimit(0.0f, 1.0f, ev));
        }
    }

    // ── Read clock input — track BPM ─────────────────────────────────────
    if (I_CLOCK < numChannels)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            bool high = (buffer.getSample(I_CLOCK, s) > kTrigThreshold);
            if (high && !clockHigh_) {
                // Rising edge — compute BPM from interval
                if (clockActive_ && samplesSinceClock_ > 0) {
                    float intervalSecs = (float)samplesSinceClock_ / (float)sampleRate_;
                    float newBPM = 60.0f / intervalSecs;
                    // Smooth to avoid jitter (simple IIR)
                    if (bpm_ < 1.0f)
                        bpm_ = newBPM;
                    else
                        bpm_ = bpm_ * 0.7f + newBPM * 0.3f;
                }
                clockActive_ = true;
                samplesSinceClock_ = 0;
            }
            clockHigh_ = high;
            samplesSinceClock_++;
        }
        // If no clock for 2 seconds, mark inactive
        if (samplesSinceClock_ > (int)(sampleRate_ * 2.0))
            clockActive_ = false;
    }

    // ── Clock sync: adjust time stretch for CLK LOOP / CLK BAR pads ─────
    if (clockActive_ && bpm_ > 1.0f)
    {
        float clockPeriodSecs = 60.0f / bpm_;

        for (int pad = 0; pad < kNumPads; ++pad)
        {
            auto& slot = engine_.getSlot(pad);
            PadMode mode = slot.getMode();
            if (mode != PadMode::ClockedLoop && mode != PadMode::ClockedBar) continue;
            if (!slot.isLoaded()) continue;

            // Region duration in seconds
            float regionFrac = slot.getEndPos() - slot.getStartPos();
            if (regionFrac <= 0.0f) continue;
            float regionSecs = regionFrac * (float)slot.getNumSamples() / (float)sampleRate_;
            if (regionSecs <= 0.001f) continue;

            // CLK LOOP: one loop = one clock pulse
            // CLK BAR: one loop = four clock pulses (one bar in 4/4)
            float targetSecs = (mode == PadMode::ClockedBar) ? clockPeriodSecs * 4.0f : clockPeriodSecs;

            // stretch = how much to slow/speed the sample to fit the target
            float stretch = targetSecs / regionSecs;
            stretch = juce::jlimit(0.25f, 4.0f, stretch);
            slot.setTimeStretch(stretch);
        }
    }

    // ── Rec Gate: rising edge toggles arm/stop ─────────────────────────
    if (I_REC_GATE < numChannels)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            bool high = (buffer.getSample(I_REC_GATE, s) > kTrigThreshold);
            if (high && !recGateHigh_) {
                // Rising edge — toggle
                if (recState_ == RecState::Idle)
                    armRecord(recTargetPad_);
                else
                    stopRecord();
            }
            recGateHigh_ = high;
        }
    }

    // ── Recording ────────────────────────────────────────────────────────
    if (recState_ == RecState::Armed || recState_ == RecState::Recording)
    {
        bool hasRecL = (I_REC_L < numChannels);
        bool hasRecR = (I_REC_R < numChannels);

        for (int s = 0; s < numSamples; ++s)
        {
            float inL = hasRecL ? buffer.getSample(I_REC_L, s) : 0.0f;
            float inR = hasRecR ? buffer.getSample(I_REC_R, s) : inL;

            if (recState_ == RecState::Armed)
            {
                bool start = false;
                switch (recMode_) {
                    case RecMode::Instant:
                        start = true;
                        break;
                    case RecMode::Threshold:
                        if (std::abs(inL) > kRecThreshold || std::abs(inR) > kRecThreshold)
                            start = true;
                        break;
                    case RecMode::NextBar:
                        if (I_CLOCK < numChannels) {
                            bool clkHi = (buffer.getSample(I_CLOCK, s) > kTrigThreshold);
                            if (clkHi && !clockHigh_) start = true;
                        }
                        break;
                }
                if (start) {
                    recState_ = RecState::Recording;
                    recSilenceCount_ = 0;
                }
            }

            if (recState_ == RecState::Recording)
            {
                if (recPos_ < recMaxSamples_) {
                    recBuffer_.setSample(0, recPos_, inL);
                    recBuffer_.setSample(1, recPos_, inR);
                    recPos_++;

                    // Track silence
                    if (std::abs(inL) > kSilenceThreshold || std::abs(inR) > kSilenceThreshold)
                        recSilenceCount_ = 0;
                    else
                        recSilenceCount_++;

                    // 3s of silence after at least 0.1s of recording = stop and trim
                    if (recPos_ > (int)(sampleRate_ * 0.1) && recSilenceCount_ > kSilenceTimeoutSamples) {
                        recPos_ = std::max(0, recPos_ - kSilenceTimeoutSamples);
                        if (recPos_ > 0)
                            finalizeRecording();
                        else
                            disarmRecord();
                        return;
                    }
                }
                // Safety cap at max length
                if (recPos_ >= recMaxSamples_) {
                    finalizeRecording();
                    return;
                }
            }
        }
    }

    // ── Clear ONLY output channels, then write audio ─────────────────────
    if (O_LEFT < numChannels)  buffer.clear(O_LEFT, 0, numSamples);
    if (O_RIGHT < numChannels) buffer.clear(O_RIGHT, 0, numSamples);

    float* outL = (O_LEFT < numChannels)  ? buffer.getWritePointer(O_LEFT)  : nullptr;
    float* outR = (O_RIGHT < numChannels) ? buffer.getWritePointer(O_RIGHT) : nullptr;

    if (outL && outR)
        engine_.process(outL, outR, numSamples);
}

// ═══════════════════════════════════════════════════════════════════════════
// Recording
// ═══════════════════════════════════════════════════════════════════════════

void PluginProcessor::armRecord(int pad)
{
    if (recState_ != RecState::Idle) return;
    recPad_ = juce::jlimit(0, kNumPads - 1, pad);
    recMaxSamples_ = (int)(kRecLengths[recMaxLenIdx_] * sampleRate_);
    recBuffer_.setSize(2, recMaxSamples_);
    recBuffer_.clear();
    recPos_ = 0;
    recSilenceCount_ = 0;
    if (recMode_ == RecMode::Instant)
        recState_ = RecState::Recording;
    else
        recState_ = RecState::Armed;
}

void PluginProcessor::disarmRecord()
{
    recState_ = RecState::Idle;
    recPos_ = 0;
}

void PluginProcessor::stopRecord()
{
    if (recState_ == RecState::Recording && recPos_ > 0)
        finalizeRecording();
    else
        disarmRecord();
}

float PluginProcessor::getRecProgress() const
{
    if (recMaxSamples_ <= 0 || recState_ != RecState::Recording) return 0.0f;
    return (float)recPos_ / (float)recMaxSamples_;
}

void PluginProcessor::finalizeRecording()
{
    if (recPos_ <= 0) { recState_ = RecState::Idle; return; }

    // Generate filename with timestamp
    auto now = juce::Time::getCurrentTime();
    juce::String name = "rec_" + now.formatted("%Y%m%d_%H%M%S") + ".wav";

    // Ensure recordings directory exists
    juce::File recDir(sampleRootPath_ + "/recordings");
    if (!recDir.isDirectory()) recDir.createDirectory();
    juce::File outFile = recDir.getChildFile(name);

    // Write WAV file
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> fos(outFile.createOutputStream());
    if (fos) {
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(fos.get(), sampleRate_, 2, 16, {}, 0));
        if (writer) {
            fos.release();  // writer takes ownership
            writer->writeFromAudioSampleBuffer(recBuffer_, 0, recPos_);
        }
    }

    // Load into the target pad
    engine_.getSlot(recPad_).loadFromBuffer(recBuffer_, recPos_, sampleRate_,
                                             name, outFile.getFullPathName());

    recState_ = RecState::Idle;
    recPos_ = 0;
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto xml = std::make_unique<juce::XmlElement>("GRID_STATE");
    xml->setAttribute("version", 1);

    for (int i = 0; i < kNumPads; ++i)
    {
        auto& slot = engine_.getSlot(i);
        auto* pad = xml->createNewChildElement("PAD");
        pad->setAttribute("index", i);

        // Store relative path from sample root
        juce::String path = slot.getFilePath();
        if (path.isNotEmpty() && path.startsWith(sampleRootPath_))
            path = path.substring(sampleRootPath_.length() + 1);  // strip root + separator
        pad->setAttribute("file", path);

        pad->setAttribute("mode", static_cast<int>(slot.getMode()));
        pad->setAttribute("volume", (double)slot.getVolume());
        pad->setAttribute("pan", (double)slot.getPan());
        pad->setAttribute("start", (double)slot.getStartPos());
        pad->setAttribute("end", (double)slot.getEndPos());
        pad->setAttribute("pitch", (double)slot.getPitchSemitones());
        pad->setAttribute("stretch", (double)slot.getTimeStretch());
        pad->setAttribute("fadeIn", (double)slot.getFadeInMs());
        pad->setAttribute("fadeOut", (double)slot.getFadeOutMs());
        pad->setAttribute("fadeInCurve", slot.getFadeInCurve());
        pad->setAttribute("fadeOutCurve", slot.getFadeOutCurve());
    }

    copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || xml->getTagName() != "GRID_STATE") return;

    for (int p = 0; p < xml->getNumChildElements(); ++p)
    {
        auto* pad = xml->getChildElement(p);
        if (!pad || pad->getTagName() != "PAD") continue;

        int i = pad->getIntAttribute("index", -1);
        if (i < 0 || i >= kNumPads) continue;

        auto& slot = engine_.getSlot(i);

        // Resolve file path — try relative from sample root, then absolute
        juce::String path = pad->getStringAttribute("file");
        if (path.isNotEmpty())
        {
            juce::File file(path);
            if (!file.existsAsFile())
                file = juce::File(sampleRootPath_ + "/" + path);
            if (file.existsAsFile())
                slot.loadFile(file);
        }

        slot.setMode(static_cast<PadMode>(pad->getIntAttribute("mode", 0)));
        slot.setVolume((float)pad->getDoubleAttribute("volume", 1.0));
        slot.setPan((float)pad->getDoubleAttribute("pan", 0.0));
        slot.setStartPos((float)pad->getDoubleAttribute("start", 0.0));
        slot.setEndPos((float)pad->getDoubleAttribute("end", 1.0));
        slot.setPitchSemitones((float)pad->getDoubleAttribute("pitch", 0.0));
        slot.setTimeStretch((float)pad->getDoubleAttribute("stretch", 1.0));
        slot.setFadeInMs((float)pad->getDoubleAttribute("fadeIn", 0.0));
        slot.setFadeOutMs((float)pad->getDoubleAttribute("fadeOut", 0.0));
        slot.setFadeInCurve(pad->getIntAttribute("fadeInCurve", 0));
        slot.setFadeOutCurve(pad->getIntAttribute("fadeOutCurve", 0));
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace grid
