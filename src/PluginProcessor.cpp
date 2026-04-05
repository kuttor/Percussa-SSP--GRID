#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace grid {

PluginProcessor::PluginProcessor()
    : AudioProcessor(getBusesProperties())
{
    sampleRootPath_ = findSSPSamplePath();
}

PluginProcessor::~PluginProcessor()
{
    if (midiInDevice_) {
        midiInDevice_->stop();
        midiInDevice_ = nullptr;
    }
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock);
    midiCollector_.reset(sampleRate);
    refreshMidiDevices();

    // Reopen MIDI device if name is set but device was killed
    if (midiDeviceName_.isNotEmpty() && midiInDevice_ == nullptr) {
        auto saved = midiDeviceName_;
        midiDeviceName_.clear();  // force setMidiDevice to actually open
        setMidiDevice(saved);
    }
}

void PluginProcessor::releaseResources()
{
    // Don't close MIDI here — device persists across audio restarts
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Read gate CVs + Slice CV selection on trigger ─────────────────
    bool hasSliceCV1 = (I_SLICE_CV1 < numChannels && isInputEnabled(I_SLICE_CV1));
    bool hasSliceCV2 = (I_SLICE_CV2 < numChannels && isInputEnabled(I_SLICE_CV2));

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = trigChannel(pad);
        if (ch >= numChannels || !isInputEnabled(ch)) continue;
        if (engine_.isMuted(pad)) continue;

        bool triggered = false;
        int triggerOffset = 0;
        for (int s = 0; s < numSamples; ++s)
        {
            float gSample = buffer.getSample(ch, s);
            bool high = (gSample > kTrigThreshold);
            if (high && !gateHigh_[pad]) {
                triggered = true;
                triggerOffset = s;
                break;
            }
            gateHigh_[pad] = high;
        }
        gateHigh_[pad] = (buffer.getSample(ch, numSamples - 1) > kTrigThreshold);

        if (triggered) {
            auto& slot = engine_.getSlot(pad);

            // Check if this pad is assigned to a Slice CV input
            if (slot.isSliceMode() && slot.getSliceCount() > 0) {
                int numRegions = slot.getSliceCount() + 1;
                int sliceIdx = slot.getSelectedSlice();

                // Check both Slice CVs — use whichever is assigned to this pad
                for (int cv = 0; cv < 2; ++cv) {
                    if (sliceCVPad_[cv] != pad) continue;
                    bool hasCV = (cv == 0) ? hasSliceCV1 : hasSliceCV2;
                    int cvChan = (cv == 0) ? I_SLICE_CV1 : I_SLICE_CV2;
                    if (hasCV) {
                        float sv = buffer.getSample(cvChan, triggerOffset);
                        if (std::isfinite(sv)) {
                            float norm = juce::jlimit(0.0f, 1.0f, (sv + 1.0f) * 0.5f);
                            sliceIdx = juce::jlimit(0, numRegions - 1, (int)(norm * (float)numRegions));
                        }
                    }
                }

                float slStart, slEnd;
                slot.getSliceRegion(sliceIdx, slStart, slEnd);
                slot.setStartPos(slStart);
                slot.setEndPos(slEnd);
                slot.setSelectedSlice(sliceIdx);
                // Apply per-slice pitch offset (adds to base pad pitch)
                slot.setSlicePitchOffset(slot.getSlicePitch(sliceIdx));
            }
            engine_.triggerWithChokeAndOffset(pad, triggerOffset);
        }
    }

    // ── Process MIDI from collector (sample-accurate, thread-safe) ─────
    {
        juce::MidiBuffer midiMessages;
        midiCollector_.removeNextBlockOfMessages(midiMessages, numSamples);

        for (const auto metadata : midiMessages) {
            auto msg = metadata.getMessage();
            int samplePos = metadata.samplePosition;

            if (msg.isNoteOn()) {
                int ch = msg.getChannel();
                int note = msg.getNoteNumber();
                for (int pad = 0; pad < kNumPads; ++pad) {
                    int padCh = engine_.getSlot(pad).getMidiChannel();
                    if (padCh == 0) continue;
                    if (padCh == ch || padCh == 17) {
                        if (!engine_.isMuted(pad)) {
                            auto& slot = engine_.getSlot(pad);
                            if (slot.isSliceMode() && slot.getSliceCount() > 0) {
                                // Slice mode: note selects slice (C2=36 = slice 0)
                                int sliceIdx = juce::jlimit(0, slot.getSliceCount(),
                                                             note - 36);
                                float slStart, slEnd;
                                slot.getSliceRegion(sliceIdx, slStart, slEnd);
                                slot.setStartPos(slStart);
                                slot.setEndPos(slEnd);
                                slot.setSelectedSlice(sliceIdx);
                                slot.setSlicePitchOffset(slot.getSlicePitch(sliceIdx));
                            } else {
                                // Normal mode: note controls pitch
                                float pitchSt = (float)(note - 60);
                                slot.setPitchSemitones(pitchSt);
                            }
                            engine_.triggerWithChokeAndVelocityAndOffset(
                                pad, msg.getFloatVelocity(), samplePos);
                        }
                        break;
                    }
                }
            }
            else if (msg.isController()) {
                int ch = msg.getChannel();
                int cc = msg.getControllerNumber();
                float val = (float)msg.getControllerValue() / 127.0f;

                if (debugMsgs_)
                    showTicker("CC" + juce::String(cc) + " v" + juce::String(msg.getControllerValue()) + " ch" + juce::String(ch));

                for (int pad = 0; pad < kNumPads; ++pad) {
                    int padCh = engine_.getSlot(pad).getMidiChannel();
                    if (padCh == 0) continue;
                    if (padCh != ch && padCh != 17) continue;

                    auto& slot = engine_.getSlot(pad);
                    auto& ccMap = padCCMaps_[pad];
                    float val100 = juce::jlimit(0.0f, 1.0f, (float)msg.getControllerValue() / 100.0f);
                    if (cc == ccMap.ccStart) {
                        if (slot.isSliceMode() && slot.getSliceCount() > 0) {
                            // Slice mode: CC 0-100 selects slice
                            int numRegions = slot.getSliceCount() + 1;
                            int sliceIdx = juce::jlimit(0, numRegions - 1,
                                                         (int)(val100 * numRegions));
                            slot.setSelectedSlice(sliceIdx);
                            slot.setSlicePitchOffset(slot.getSlicePitch(sliceIdx));
                            float slStart, slEnd;
                            slot.getSliceRegion(sliceIdx, slStart, slEnd);
                            slot.setStartPos(slStart);
                            slot.setEndPos(slEnd);
                        } else {
                            slot.setStartPos(val100);
                        }
                    }
                    else if (cc == ccMap.ccEnd)     slot.setEndPos(val100);
                    else if (cc == ccMap.ccVolume)  slot.setVolume(val);
                    else if (cc == ccMap.ccPan) {
                        float p = val * 2.0f - 1.0f;
                        if (std::abs(p) < 0.02f) p = 0.0f;
                        slot.setPan(p);
                    }
                    else if (cc == ccMap.ccStretch) {
                        float s = 0.25f + val * 3.75f;
                        if (std::abs(s - 1.0f) < 0.04f) s = 1.0f;
                        slot.setTimeStretch(s);
                    }
                    else if (cc == ccMap.ccFilter) {
                        float hz = 20.0f * std::pow(1000.0f, val);
                        slot.setFilterCutoff(hz);
                    }
                }
            }
        }
    }

    // ── MIDI clock BPM (overrides CV clock when enabled) ────────────────
    // Handled in handleIncomingMidiMessage — midiClockCount_ counts 24 PPQN

    // ── Read pitch CVs ───────────────────────────────────────────────────
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = pitchChannel(pad);
        if (ch >= numChannels || !isInputEnabled(ch)) continue;

        float voct = buffer.getSample(ch, numSamples - 1);
        if (std::isfinite(voct) && std::abs(voct) > 0.01f)
            engine_.getSlot(pad).setPitchSemitones(voct * 12.0f);
    }

    // ── Filter CV — master strength for all pads ──────────────────────────
    // 0V = 0% (no filtering), 1V = 100% (max filtering)
    // Hz direction depends on per-pad filter type
    if (I_FILTER_CV < numChannels && isInputEnabled(I_FILTER_CV)) {
        float cv = buffer.getSample(I_FILTER_CV, numSamples - 1);
        if (std::isfinite(cv)) {
            float strength = juce::jlimit(0.0f, 1.0f, cv);
            for (int pad = 0; pad < kNumPads; ++pad) {
                auto& slot = engine_.getSlot(pad);
                float hz;
                if (slot.getFilterType() == FilterType::HPF)
                    hz = 20.0f * std::pow(1000.0f, strength);       // 0%=20Hz, 100%=20kHz
                else
                    hz = 20.0f * std::pow(1000.0f, 1.0f - strength); // 0%=20kHz, 100%=20Hz
                slot.setFilterCutoff(hz);
            }
        }
    }

    // ── Read clock input — track BPM (skipped when MIDI clock active) ────
    if (!midiClockEnabled_ && I_CLOCK < numChannels && isInputEnabled(I_CLOCK))
    {
        for (int s = 0; s < numSamples; ++s)
        {
            float clkSample = buffer.getSample(I_CLOCK, s);
            if (!std::isfinite(clkSample)) continue;

            bool high = (clkSample > kTrigThreshold);
            if (high && !clockHigh_) {
                // Rising edge — count pulses for division
                clockPulseCount_++;
                if (clockPulseCount_ >= getClockPulsesPerBeat()) {
                    // N pulses received = 1 beat
                    if (clockActive_ && samplesSinceDiv_ > 64) {
                        float intervalSecs = (float)samplesSinceDiv_ / (float)sampleRate_;
                        if (intervalSecs > 0.001f) {
                            float newBPM = 60.0f / intervalSecs;
                            newBPM = juce::jlimit(20.0f, 300.0f, newBPM);
                            if (bpm_ < 1.0f || std::abs(newBPM - bpm_) > bpm_ * 0.1f)
                                bpm_ = newBPM;
                            else
                                bpm_ = bpm_ * 0.7f + newBPM * 0.3f;
                        }
                    }
                    clockActive_ = true;
                    clockPulseCount_ = 0;
                    samplesSinceDiv_ = 0;

                    // Beat/bar tracking
                    beatCount_++;
                    if (beatCount_ >= 4) {
                        beatCount_ = 0;
                        barCount_++;
                        if (pendingBarCountdown_ > 0) {
                            pendingBarCountdown_--;
                            if (pendingBarCountdown_ == 0) flushBarMutes();
                        }
                    }
                }
            }
            clockHigh_ = high;
            samplesSinceDiv_++;
        }
        if (samplesSinceDiv_ > (int)(sampleRate_ * 2.0))
            clockActive_ = false;
    }

    // ── Reset input: rising edge resets all clocked pads to start ────────
    if (I_RESET < numChannels && isInputEnabled(I_RESET))
    {
        for (int s = 0; s < numSamples; ++s)
        {
            float rst = buffer.getSample(I_RESET, s);
            if (!std::isfinite(rst)) continue;
            bool high = (rst > kTrigThreshold);
            if (high && !resetHigh_) {
                // Rising edge — reset all playing clocked pads
                for (int pad = 0; pad < kNumPads; ++pad) {
                    auto& slot = engine_.getSlot(pad);
                    if (slot.isPlaying() &&
                        (slot.getMode() == PadMode::ClockedLoop || slot.getMode() == PadMode::ClockedOneShot)) {
                        slot.trigger();  // retrigger = snap to start
                    }
                }
                break;
            }
            resetHigh_ = high;
        }
        resetHigh_ = (buffer.getSample(I_RESET, numSamples - 1) > kTrigThreshold);
    }

    // ── Clock sync: set clock base stretch for CLK LOOP / CLK BAR pads ──
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        auto& slot = engine_.getSlot(pad);
        PadMode mode = slot.getMode();

        if ((mode != PadMode::ClockedLoop && mode != PadMode::ClockedOneShot) || !clockActive_ || bpm_ < 20.0f) {
            slot.clearClockStretch();  // non-clocked pads = 1.0
            continue;
        }
        if (!slot.isLoaded()) continue;

        float clockPeriodSecs = 60.0f / bpm_;
        float mult = getClockMultiplier();
        float regionFrac = slot.getEndPos() - slot.getStartPos();
        if (regionFrac <= 0.01f) continue;
        float regionSamples = regionFrac * (float)slot.getNumSamples();
        if (regionSamples < 1.0f || sampleRate_ < 1.0) continue;
        float regionSecs = regionSamples / (float)slot.getSampleRate();  // use FILE sample rate

        // Both CLK modes stretch to fit clockBeats_ × beat period
        // mult scales: *8 means 8× faster playback, /2 means half speed
        float targetSecs = clockPeriodSecs * (float)slot.getClockBeats() * mult;

        float stretch = targetSecs / regionSecs;
        if (std::isfinite(stretch))
            slot.setClockStretch(stretch);
    }

    // ── Rec Gate: rising edge toggles arm/stop ─────────────────────────
    if (I_REC_GATE < numChannels && isInputEnabled(I_REC_GATE))
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
        bool hasRecL = (I_REC_L < numChannels && isInputEnabled(I_REC_L));
        bool hasRecR = (I_REC_R < numChannels && isInputEnabled(I_REC_R));

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
                        if (I_CLOCK < numChannels && isInputEnabled(I_CLOCK)) {
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

// ═══════════════════════════════════════════════════════════════════════════
// MIDI (direct device access — Bear's pattern, improved)
// ═══════════════════════════════════════════════════════════════════════════

static bool isInternalMidi(const juce::String& name) {
    auto lower = name.toLowerCase();
    return lower.contains("juce") || lower.contains("midi through") || 
           lower.contains("rtmidi") || lower.contains("internal") ||
           name.isEmpty();
}

juce::StringArray PluginProcessor::getMidiDeviceNames() const
{
    return cachedMidiDeviceNames_;
}

void PluginProcessor::refreshMidiDevices()
{
    cachedMidiDeviceNames_.clear();
    cachedMidiDeviceNames_.add("None");
    auto devs = juce::MidiInput::getAvailableDevices();
    for (auto& d : devs) {
        if (!isInternalMidi(d.name) && d.name.isNotEmpty())
            cachedMidiDeviceNames_.add(d.name);
    }
}

void PluginProcessor::setMidiDevice(const juce::String& name)
{
    // Skip only if name matches AND device is actually open
    if (name == midiDeviceName_ && midiInDevice_ != nullptr) return;

    if (name.isEmpty() || name == "None") {
        // Only close if something is actually open
        if (midiInDevice_) {
            midiInDevice_->stop();
            midiInDevice_ = nullptr;  // Bear's pattern
        }
        if (midiDeviceName_.isNotEmpty())
            showTicker("MIDI device disconnected");
        midiDeviceName_.clear();
        midiClockEnabled_ = false;
        midiTransportRunning_ = false;
        return;
    }

    if (isInternalMidi(name)) return;

    // Close existing device before opening new one
    if (midiInDevice_) {
        midiInDevice_->stop();
        midiInDevice_ = nullptr;
    }

    auto devs = juce::MidiInput::getAvailableDevices();
    for (auto& d : devs) {
        if (d.name == name && !isInternalMidi(d.name)) {
            midiInDevice_ = juce::MidiInput::openDevice(d.identifier, this);
            if (midiInDevice_) {
                midiInDevice_->start();
                midiDeviceName_ = name;
                showTicker("MIDI: " + name);
            }
            return;
        }
    }
}

void PluginProcessor::closeMidiDevice()
{
    if (midiInDevice_) {
        midiInDevice_->stop();
        midiInDevice_ = nullptr;  // Bear's pattern
    }
    midiDeviceName_.clear();
    midiClockEnabled_ = false;
    midiTransportRunning_ = false;
    midiClockCount_ = 0;
    midiClockLastBeatMs_ = 0.0;
}

void PluginProcessor::rebootPlugin()
{
    // Close and reopen MIDI
    auto savedDevice = midiDeviceName_;
    auto savedClock = midiClockEnabled_;
    closeMidiDevice();

    // Stop all pads
    engine_.stopAll();

    // Reset clock state
    bpm_ = 0.0f;
    clockActive_ = false;
    clockPulseCount_ = 0;
    samplesSinceDiv_ = 0;

    // Reopen MIDI if it was connected
    if (savedDevice.isNotEmpty()) {
        setMidiDevice(savedDevice);
        if (savedClock) setMidiClockEnabled(true);
    }

    showTicker("Plugin rebooted");
}

void PluginProcessor::commitBarMutes(int barsAhead)
{
    // Check if we have a clock running (either MIDI or CV)
    if (!clockActive_ && !midiClockEnabled_) {
        // No clock — can't count bars, flush immediately
        flushBarMutes();
        showTicker("No clock — mutes applied now");
        return;
    }
    pendingBarCountdown_ = juce::jlimit(1, 4, barsAhead);
    juce::String msg = "Mutes queued: " + juce::String(pendingBarCountdown_) + " bar";
    if (pendingBarCountdown_ > 1) msg += "s";
    showTicker(msg);
}

void PluginProcessor::flushBarMutes()
{
    for (int i = 0; i < kNumPads; ++i) {
        if (pendingBarMutes_[i]) {
            engine_.toggleMute(i);
            pendingBarMutes_[i] = false;
        }
    }
    pendingBarCountdown_ = 0;
    showTicker("Mutes applied");
}

// ═══════════════════════════════════════════════════════════════════════════
// Kit / Stack Management
// ═══════════════════════════════════════════════════════════════════════════

juce::File PluginProcessor::getKitsDir() const
{
    auto dir = juce::File(sampleRootPath_).getChildFile("kits");
    if (!dir.isDirectory()) dir.createDirectory();
    return dir;
}

juce::File PluginProcessor::getStacksDir() const
{
    auto dir = juce::File(sampleRootPath_).getChildFile("stacks");
    if (!dir.isDirectory()) dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PluginProcessor::getAvailableKits() const
{
    auto dir = juce::File(sampleRootPath_).getChildFile("kits");
    if (!dir.isDirectory()) return {};
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.kit");
    files.sort();
    return files;
}

KitData PluginProcessor::captureCurrentState() const
{
    KitData kit;
    kit.name = currentKitName_;
    for (int i = 0; i < kNumPads; ++i) {
        auto& slot = engine_.getSlot(i);
        auto& p = kit.pads[i];
        // Store relative path
        juce::String path = slot.getFilePath();
        if (path.isNotEmpty() && path.startsWith(sampleRootPath_))
            path = path.substring(sampleRootPath_.length() + 1);
        p.filePath  = path;
        p.volume    = slot.getVolume();
        p.pan       = slot.getPan();
        p.startPos  = slot.getStartPos();
        p.endPos    = slot.getEndPos();
        p.pitch     = slot.getPitchSemitones();
        p.stretch   = slot.getTimeStretch();
        p.mode      = static_cast<int>(slot.getMode());
        p.choke     = static_cast<int>(slot.getChokeGroup());
        p.reversed  = slot.isReversed();
        p.midiCh    = slot.getMidiChannel();
        p.clockBeats = slot.getClockBeats();
        p.voiceMode  = static_cast<int>(slot.getVoiceMode());
        p.filterType = static_cast<int>(slot.getFilterType());
        p.filterCutoff = slot.getFilterCutoff();
        p.filterReso = slot.getFilterResonance();
        p.lofiMode = static_cast<int>(slot.getLofiMode());
        p.sliceMode = slot.isSliceMode();
        p.sliceCount = slot.getSliceCount();
        for (int s = 0; s < p.sliceCount; ++s)
            p.slicePoints[s] = slot.getSlicePoint(s);
    }
    return kit;
}

void PluginProcessor::applyKitData(const KitData& kit)
{
    currentKitName_ = kit.name;
    for (int i = 0; i < kNumPads; ++i) {
        auto& slot = engine_.getSlot(i);
        auto& p = kit.pads[i];

        // Load sample
        if (p.filePath.isNotEmpty()) {
            juce::File file(p.filePath);
            if (!file.existsAsFile())
                file = juce::File(sampleRootPath_ + "/" + p.filePath);
            if (file.existsAsFile())
                slot.loadFile(file);
            else
                slot.clear();
        } else {
            slot.clear();
        }

        slot.setMode(static_cast<PadMode>(p.mode));
        slot.setVolume(p.volume);
        slot.setPan(p.pan);
        slot.setStartPos(p.startPos);
        slot.setEndPos(p.endPos);
        slot.setPitchSemitones(p.pitch);
        slot.setTimeStretch(p.stretch);
        slot.setChokeGroup(static_cast<ChokeGroup>(p.choke));
        if (p.reversed != slot.isReversed()) slot.setReversed(p.reversed);
        slot.setMidiChannel(p.midiCh);
        slot.setClockBeats(p.clockBeats);
        slot.setVoiceMode(static_cast<VoiceMode>(p.voiceMode));
        slot.setFilterType(static_cast<FilterType>(p.filterType));
        slot.setFilterCutoff(p.filterCutoff);
        slot.setFilterResonance(p.filterReso);
        slot.setLofiMode(static_cast<LofiMode>(p.lofiMode));
        slot.setSliceMode(p.sliceMode);
        slot.clearSlices();
        for (int s = 0; s < p.sliceCount; ++s) slot.insertSlicePoint(p.slicePoints[s]);
        engine_.setMuted(i, false);
    }
    showTicker("Kit: " + kit.name);
}

void PluginProcessor::saveCurrentAsKit(const juce::String& name)
{
    auto kit = captureCurrentState();
    kit.name = name;
    currentKitName_ = name;

    auto kitFile = getKitsDir().getChildFile(name + ".kit");
    auto wavFile = getKitsDir().getChildFile(name + ".kit.wav");

    // Build companion WAV: concatenate all pad samples as stereo 32-bit float
    int totalSamples = 0;
    for (int i = 0; i < kNumPads; ++i) {
        auto& slot = engine_.getSlot(i);
        int ns = slot.getNumSamples();
        kit.pads[i].bundleOffset = (ns > 0) ? totalSamples : -1;
        kit.pads[i].bundleLength = ns;
        kit.pads[i].bundleChannels = slot.getNumChannels();
        totalSamples += ns;
    }

    if (totalSamples > 0) {
        juce::AudioBuffer<float> bundle(2, totalSamples);
        bundle.clear();
        for (int i = 0; i < kNumPads; ++i) {
            auto& slot = engine_.getSlot(i);
            int ns = slot.getNumSamples();
            if (ns == 0) continue;
            int off = kit.pads[i].bundleOffset;
            auto& buf = slot.getBuffer();
            bundle.copyFrom(0, off, buf, 0, 0, ns);
            bundle.copyFrom(1, off, buf, std::min(1, buf.getNumChannels() - 1), 0, ns);
        }

        // Write companion WAV
        wavFile.deleteFile();
        if (auto stream = wavFile.createOutputStream()) {
            juce::WavAudioFormat wav;
            if (auto* writer = wav.createWriterFor(stream.release(), 48000.0, 2, 32, {}, 0)) {
                writer->writeFromAudioSampleBuffer(bundle, 0, totalSamples);
                delete writer;
            }
        }
    }

    kit.saveToFile(kitFile);
    showTicker("Saved: " + name);
}

void PluginProcessor::loadKit(const juce::File& kitFile)
{
    auto kit = KitData::loadFromFile(kitFile);
    if (kit.name.isEmpty()) kit.name = kitFile.getFileNameWithoutExtension();

    // Check for companion .kit.wav bundle
    auto wavFile = juce::File(kitFile.getFullPathName() + ".wav");
    juce::AudioBuffer<float> bundleBuffer;
    bool hasBundle = false;

    if (wavFile.existsAsFile()) {
        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        if (auto* reader = fmt.createReaderFor(wavFile)) {
            int ns = (int)reader->lengthInSamples;
            bundleBuffer.setSize((int)reader->numChannels, ns);
            reader->read(&bundleBuffer, 0, ns, 0, true, true);
            delete reader;
            hasBundle = true;
        }
    }

    // Apply kit data — use bundle if available, else fall back to file paths
    currentKitName_ = kit.name;
    for (int i = 0; i < kNumPads; ++i) {
        auto& slot = engine_.getSlot(i);
        auto& p = kit.pads[i];

        // Load sample: prefer bundle, fall back to file path
        bool loaded = false;
        if (hasBundle && p.bundleOffset >= 0 && p.bundleLength > 0
            && p.bundleOffset + p.bundleLength <= bundleBuffer.getNumSamples()) {
            // Slice from companion WAV
            int nch = std::min(p.bundleChannels, bundleBuffer.getNumChannels());
            juce::AudioBuffer<float> slice(nch, p.bundleLength);
            for (int ch = 0; ch < nch; ++ch)
                slice.copyFrom(ch, 0, bundleBuffer, ch, p.bundleOffset, p.bundleLength);
            juce::String padName = p.filePath.isNotEmpty()
                ? juce::File(p.filePath).getFileNameWithoutExtension()
                : "Pad " + juce::String(i + 1);
            loaded = slot.loadFromBuffer(slice, p.bundleLength, 48000.0, padName, p.filePath);
        }
        if (!loaded) {
            if (p.filePath.isNotEmpty()) {
                juce::File file(p.filePath);
                if (!file.existsAsFile())
                    file = juce::File(sampleRootPath_ + "/" + p.filePath);
                if (file.existsAsFile())
                    slot.loadFile(file);
                else
                    slot.clear();
            } else {
                slot.clear();
            }
        }

        slot.setMode(static_cast<PadMode>(p.mode));
        slot.setVolume(p.volume);
        slot.setPan(p.pan);
        slot.setStartPos(p.startPos);
        slot.setEndPos(p.endPos);
        slot.setPitchSemitones(p.pitch);
        slot.setTimeStretch(p.stretch);
        slot.setChokeGroup(static_cast<ChokeGroup>(p.choke));
        if (p.reversed != slot.isReversed()) slot.setReversed(p.reversed);
        slot.setMidiChannel(p.midiCh);
        slot.setClockBeats(p.clockBeats);
        slot.setVoiceMode(static_cast<VoiceMode>(p.voiceMode));
        slot.setFilterType(static_cast<FilterType>(p.filterType));
        slot.setFilterCutoff(p.filterCutoff);
        slot.setFilterResonance(p.filterReso);
        slot.setLofiMode(static_cast<LofiMode>(p.lofiMode));
        slot.setSliceMode(p.sliceMode);
        slot.clearSlices();
        for (int s = 0; s < p.sliceCount; ++s) slot.insertSlicePoint(p.slicePoints[s]);
        engine_.setMuted(i, false);
    }
    showTicker("Kit: " + kit.name + (hasBundle ? " [bundled]" : ""));
}

void PluginProcessor::createStackFile(const juce::String& name, const juce::StringArray& layerPaths)
{
    StackData stack;
    stack.name = name;
    stack.mode = StackLayerMode::RoundRobin;
    stack.layerPaths = layerPaths;
    auto file = getStacksDir().getChildFile(name + ".stack");
    stack.saveToFile(file);
    showTicker("Stack: " + name + " (" + juce::String(layerPaths.size()) + " layers)");
}

void PluginProcessor::setMidiClockEnabled(bool b)
{
    if (b == midiClockEnabled_) return;
    midiClockEnabled_ = b;
    midiClockCount_ = 0;
    midiClockLastBeatMs_ = 0.0;

    if (b) {
        // Reset CV clock state — MIDI clock takes priority
        clockActive_ = false;
        bpm_ = 0.0f;
        showTicker("MIDI Clock enabled - CV clock disabled");
    } else {
        clockActive_ = false;
        bpm_ = 0.0f;
        showTicker("MIDI Clock disabled");
    }
}

void PluginProcessor::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg)
{
    // ── MIDI Transport ──────────────────────────────────────────────────
    if (msg.isMidiStart()) {
        midiTransportRunning_ = true;
        midiClockCount_ = 0;
        midiClockLastBeatMs_ = 0.0;
        beatCount_ = 0;
        barCount_ = 0;
        if (midiClockEnabled_) clockActive_ = true;
        return;
    }
    if (msg.isMidiContinue()) {
        midiTransportRunning_ = true;
        if (midiClockEnabled_) clockActive_ = true;
        return;
    }
    if (msg.isMidiStop()) {
        midiTransportRunning_ = false;
        return;
    }

    // ── MIDI Clock: 24 PPQN — wall-clock BPM detection stays on MIDI thread ─
    if (midiClockEnabled_ && msg.isMidiClock()) {
        midiClockCount_++;
        if (midiClockCount_ >= 24) {
            double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (midiClockLastBeatMs_ > 0.0) {
                double intervalMs = nowMs - midiClockLastBeatMs_;
                if (intervalMs > 10.0 && intervalMs < 3000.0) {
                    float newBPM = (float)(60000.0 / intervalMs);
                    newBPM = juce::jlimit(20.0f, 300.0f, newBPM);
                    if (bpm_ < 1.0f)
                        bpm_ = newBPM;
                    else
                        bpm_ = bpm_ * 0.5f + newBPM * 0.5f;
                }
            }
            midiClockLastBeatMs_ = nowMs;
            midiClockCount_ = 0;
            clockActive_ = true;

            beatCount_++;
            if (beatCount_ >= 4) {
                beatCount_ = 0;
                barCount_++;
                if (pendingBarCountdown_ > 0) {
                    pendingBarCountdown_--;
                    if (pendingBarCountdown_ == 0) flushBarMutes();
                }
            }
        }
        return;
    }

    // ── Everything else (Note On/Off, CC) → collector for sample-accurate
    //    processing in processBlock. Thread-safe, timestamped. ────────────
    midiCollector_.handleIncomingMidiMessage(source, msg);
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
        pad->setAttribute("muted", engine_.isMuted(i) ? 1 : 0);
        pad->setAttribute("choke", static_cast<int>(slot.getChokeGroup()));
        pad->setAttribute("reversed", slot.isReversed() ? 1 : 0);
        pad->setAttribute("midiChannel", slot.getMidiChannel());

        // Per-pad CC map
        auto& ccMap = padCCMaps_[i];
        pad->setAttribute("ccStart",   ccMap.ccStart);
        pad->setAttribute("ccEnd",     ccMap.ccEnd);
        pad->setAttribute("ccVolume",  ccMap.ccVolume);
        pad->setAttribute("ccPan",     ccMap.ccPan);
        pad->setAttribute("ccStretch", ccMap.ccStretch);
        pad->setAttribute("ccFilter", ccMap.ccFilter);
        pad->setAttribute("clockBeats", slot.getClockBeats());
        pad->setAttribute("voiceMode", static_cast<int>(slot.getVoiceMode()));
        pad->setAttribute("filterType", static_cast<int>(slot.getFilterType()));
        pad->setAttribute("filterCutoff", slot.getFilterCutoff());
        pad->setAttribute("filterReso", slot.getFilterResonance());
        pad->setAttribute("lofiMode", static_cast<int>(slot.getLofiMode()));
        pad->setAttribute("sliceMode", slot.isSliceMode() ? 1 : 0);
        if (slot.getSliceCount() > 0) {
            juce::String pts, pitches;
            for (int s = 0; s < slot.getSliceCount(); ++s) {
                if (s > 0) { pts += ","; pitches += ","; }
                pts += juce::String(slot.getSlicePoint(s), 6);
            }
            pad->setAttribute("slicePoints", pts);
            // Per-region pitch: sliceCount+1 regions
            for (int s = 0; s <= slot.getSliceCount(); ++s) {
                if (s > 0) pitches += ",";
                pitches += juce::String(slot.getSlicePitch(s), 2);
            }
            pad->setAttribute("slicePitches", pitches);
        }
    }

    // Global MIDI settings
    xml->setAttribute("midiDevice", midiDeviceName_);
    xml->setAttribute("midiClock", midiClockEnabled_ ? 1 : 0);
    xml->setAttribute("clockDiv", clockDiv_);

    // Global config
    xml->setAttribute("perfMode", static_cast<int>(perfMode_));
    xml->setAttribute("presetSwitchMode", static_cast<int>(presetSwitchMode_));
    xml->setAttribute("queueBars", queueBars_);
    xml->setAttribute("debugMidi", debugMsgs_ ? 1 : 0);
    xml->setAttribute("encoderSpeed", encoderSpeed_);
    xml->setAttribute("muteFadeMs", muteFadeMs_);
    xml->setAttribute("sliceCVPad1", sliceCVPad_[0]);
    xml->setAttribute("sliceCVPad2", sliceCVPad_[1]);

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
        engine_.setMuted(i, pad->getIntAttribute("muted", 0) != 0);
        slot.setChokeGroup(static_cast<ChokeGroup>(pad->getIntAttribute("choke", 0)));
        if (pad->getIntAttribute("reversed", 0)) slot.setReversed(true);
        slot.setMidiChannel(pad->getIntAttribute("midiChannel", 0));

        // Per-pad CC map (defaults if not present = backwards compatible)
        auto& ccMap = padCCMaps_[i];
        ccMap.ccStart   = pad->getIntAttribute("ccStart",   1);
        ccMap.ccEnd     = pad->getIntAttribute("ccEnd",     2);
        ccMap.ccVolume  = pad->getIntAttribute("ccVolume",  7);
        ccMap.ccPan     = pad->getIntAttribute("ccPan",     10);
        ccMap.ccStretch = pad->getIntAttribute("ccStretch", 11);
        ccMap.ccFilter  = pad->getIntAttribute("ccFilter", 74);
        slot.setClockBeats(pad->getIntAttribute("clockBeats", 4));
        slot.setVoiceMode(static_cast<VoiceMode>(pad->getIntAttribute("voiceMode", 0)));
        slot.setFilterType(static_cast<FilterType>(pad->getIntAttribute("filterType", 0)));
        slot.setFilterCutoff((float)pad->getDoubleAttribute("filterCutoff", 20000.0));
        slot.setFilterResonance((float)pad->getDoubleAttribute("filterReso", 0.0));
        slot.setLofiMode(static_cast<LofiMode>(pad->getIntAttribute("lofiMode", 0)));
        slot.setSliceMode(pad->getIntAttribute("sliceMode", 0) != 0);
        slot.clearSlices();
        auto ptsStr = pad->getStringAttribute("slicePoints", "");
        if (ptsStr.isNotEmpty()) {
            juce::StringArray tokens;
            tokens.addTokens(ptsStr, ",", "");
            for (int s = 0; s < tokens.size() && s < 64; ++s)
                slot.insertSlicePoint(tokens[s].getFloatValue());
        }
        // Load per-region pitch offsets
        auto pitchStr = pad->getStringAttribute("slicePitches", "");
        if (pitchStr.isNotEmpty()) {
            juce::StringArray ptokens;
            ptokens.addTokens(pitchStr, ",", "");
            for (int s = 0; s < ptokens.size() && s < 64; ++s)
                slot.setSlicePitch(s, ptokens[s].getFloatValue());
        }
    }

    // Global MIDI settings
    juce::String midiDev = xml->getStringAttribute("midiDevice");
    if (midiDev.isNotEmpty()) setMidiDevice(midiDev);
    midiClockEnabled_ = xml->getIntAttribute("midiClock", 0) != 0;

    // Migrate old clockDiv values (1,2,4,8) → new format (-3 to 3)
    int savedDiv = xml->getIntAttribute("clockDiv", 0);
    if (savedDiv >= 2) {
        // Old format: 2→1, 4→2, 8→3
        int newVal = 0;
        if (savedDiv >= 8) newVal = 3;
        else if (savedDiv >= 4) newVal = 2;
        else if (savedDiv >= 2) newVal = 1;
        clockDiv_ = newVal;
    } else {
        clockDiv_ = juce::jlimit(-3, 3, savedDiv);
    }

    // Reset BPM — don't inherit stale values from presets
    bpm_ = 0.0f;
    clockActive_ = false;

    // Global config
    perfMode_ = static_cast<PerfMode>(xml->getIntAttribute("perfMode", 0));
    presetSwitchMode_ = static_cast<PerfMode>(xml->getIntAttribute("presetSwitchMode", 0));
    queueBars_ = juce::jlimit(1, 4, xml->getIntAttribute("queueBars", 1));
    debugMsgs_ = xml->getIntAttribute("debugMidi", 0) != 0;
    encoderSpeed_ = (float)xml->getDoubleAttribute("encoderSpeed", 1.0);
    setMuteFadeMs((float)xml->getDoubleAttribute("muteFadeMs", 0.0));
    sliceCVPad_[0] = juce::jlimit(-1, 7, xml->getIntAttribute("sliceCVPad1", 0));
    sliceCVPad_[1] = juce::jlimit(-1, 7, xml->getIntAttribute("sliceCVPad2", 1));
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace grid
