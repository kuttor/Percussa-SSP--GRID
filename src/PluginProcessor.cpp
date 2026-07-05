#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace grid {

PluginProcessor::PluginProcessor()
    : AudioProcessor(getBusesProperties())
{
    sampleRootPath_ = findSSPSamplePath();
    instanceId_ = juce::Uuid().toString().substring(0, 8);  // short unique ID
    loadGlobalPrefs();  // shared settings across all instances
}

PluginProcessor::~PluginProcessor()
{
    // Stop MIDI devices first — prevents callbacks during teardown
    if (midiInDevice_) {
        midiInDevice_->stop();
        midiInDevice_ = nullptr;
    }
    if (globalsMidiInDevice_) {
        globalsMidiInDevice_->stop();
        globalsMidiInDevice_ = nullptr;
    }
    // Stop all voices to prevent audio callback accessing destroyed state
    for (int i = 0; i < kNumPads; ++i)
        engine_.getSlot(i).stopAll();
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
        midiDeviceName_.clear();
        setMidiDevice(saved);
    }

    // Only load autosave if: no state was provided by host AND autosave is enabled
    if (!stateLoadedFromHost_ && autosaveEnabled_)
        loadAutosave();
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
                int numRegions = slot.getSliceCount();
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

            // Apply step modulation (all 3 mod slots)
            for (int ms = 0; ms < 3; ++ms) {
                auto& mod = slot.getMod(ms);
                if (!mod.enabled || mod.activeStepCount() == 0) continue;
                float amt = slot.advanceMod(ms);
                switch (mod.target) {
                    case ModTarget::Velocity:
                        for (int vi = 0; vi < 4; ++vi) {
                            auto& v = slot.getVoice(vi);
                            if (v.playing) v.velocity = juce::jlimit(0.0f, 1.0f, v.velocity + amt * 0.5f);
                        }
                        break;
                    case ModTarget::Pitch:
                        slot.setPitchSemitones(slot.getPitchSemitones() + amt * 12.0f);
                        break;
                    case ModTarget::Start:
                        slot.setStartPos(juce::jlimit(0.0f, slot.getEndPos(), slot.getStartPos() + amt * 0.1f));
                        break;
                    case ModTarget::End:
                        slot.setEndPos(juce::jlimit(slot.getStartPos(), 1.0f, slot.getEndPos() + amt * 0.1f));
                        break;
                    case ModTarget::Filter:
                        slot.setFilterCutoff(juce::jlimit(20.0f, 20000.0f, slot.getFilterCutoff() + amt * 5000.0f));
                        break;
                    case ModTarget::Pan:
                        slot.setPan(juce::jlimit(-1.0f, 1.0f, slot.getPan() + amt * 0.5f));
                        break;
                    case ModTarget::Stretch:
                        slot.setTimeStretch(juce::jlimit(0.25f, 4.0f, slot.getTimeStretch() + amt * 0.5f));
                        break;
                    default: break;
                }
            }
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
                            // ── MIDI-CV mode (2.4.9): note drives gate on the
                            // pad's own output channel (O_PAD1 + pad). No
                            // audio playback. Note is remembered so note-off
                            // can drop the gate.
                            if (slot.getMode() == PadMode::MidiCV) {
                                midiCVGate_[pad] = true;
                                midiCVLastNote_[pad] = note;
                                midiCVSharedPitch_ = ((float)note - 60.0f) / 12.0f * 0.1f;
                                midiCVSharedVel_ = msg.getFloatVelocity() * 0.5f;
                                break;  // don't fall through to audio trigger
                            }
                            if (slot.isSliceMode() && slot.getSliceCount() > 0) {
                                // Slice mode: pick which slice to play, then
                                // route through triggerSlice — per-voice
                                // region, slot trim stays put. Avoids the
                                // visual blackout + "slicing inside the
                                // slice" bug the old setStartPos/setEndPos
                                // pattern caused.
                                int sliceIdx = slot.getSelectedSlice();
                                if (padCCMaps_[pad].ccStart == 0) {
                                    sliceIdx = juce::jlimit(0, std::max(0, slot.getSliceCount() - 1),
                                                             note - 36);
                                }
                                slot.triggerSlice(sliceIdx, msg.getFloatVelocity());
                                // triggerSlice does its own voice setup,
                                // so skip the generic triggerWithChoke call
                                // below for sliced pads.
                                // Step modulation still applies after.
                            } else {
                                // Normal mode: note controls pitch
                                float pitchSt = (float)(note - 60);
                                slot.setPitchSemitones(pitchSt);
                                engine_.triggerWithChokeAndVelocityAndOffset(
                                    pad, msg.getFloatVelocity(), samplePos);
                            }

                            // Apply step modulation (all 3 mod slots)
                            for (int ms = 0; ms < 3; ++ms) {
                                auto& mod = slot.getMod(ms);
                                if (!mod.enabled || mod.activeStepCount() == 0) continue;
                                float amt = slot.advanceMod(ms);
                                switch (mod.target) {
                                    case ModTarget::Velocity:
                                        for (int vi = 0; vi < 4; ++vi) {
                                            auto& v = slot.getVoice(vi);
                                            if (v.playing) v.velocity = juce::jlimit(0.0f, 1.0f, v.velocity + amt * 0.5f);
                                        }
                                        break;
                                    case ModTarget::Pitch:
                                        slot.setPitchSemitones(slot.getPitchSemitones() + amt * 12.0f);
                                        break;
                                    case ModTarget::Start:
                                        slot.setStartPos(juce::jlimit(0.0f, slot.getEndPos(), slot.getStartPos() + amt * 0.1f));
                                        break;
                                    case ModTarget::End:
                                        slot.setEndPos(juce::jlimit(slot.getStartPos(), 1.0f, slot.getEndPos() + amt * 0.1f));
                                        break;
                                    case ModTarget::Filter:
                                        slot.setFilterCutoff(juce::jlimit(20.0f, 20000.0f, slot.getFilterCutoff() + amt * 5000.0f));
                                        break;
                                    case ModTarget::Pan:
                                        slot.setPan(juce::jlimit(-1.0f, 1.0f, slot.getPan() + amt * 0.5f));
                                        break;
                                    case ModTarget::Stretch:
                                        slot.setTimeStretch(juce::jlimit(0.25f, 4.0f, slot.getTimeStretch() + amt * 0.5f));
                                        break;
                                    default: break;
                                }
                            }
                        }
                        break;
                    }
                }
            }
            else if (msg.isNoteOff()) {
                // Gate mode: stop pad on note off
                int ch = msg.getChannel();
                for (int pad = 0; pad < kNumPads; ++pad) {
                    int padCh = engine_.getSlot(pad).getMidiChannel();
                    if (padCh == 0) continue;
                    if (padCh == ch || padCh == 17) {
                        auto& slot = engine_.getSlot(pad);
                        // MIDI-CV (2.4.9): drop gate on matching note-off
                        if (slot.getMode() == PadMode::MidiCV) {
                            if (midiCVLastNote_[pad] == msg.getNoteNumber())
                                midiCVGate_[pad] = false;
                            continue;
                        }
                        if (slot.getMode() == PadMode::Gate && slot.isPlaying()) {
                            engine_.stop(pad);
                        }
                    }
                }
            }
            else if (msg.isController()) {
                int ch = msg.getChannel();
                int cc = msg.getControllerNumber();
                float val = (float)msg.getControllerValue() / 127.0f;

                if (debugMsgs_)
                    showTicker("CC" + juce::String(cc) + " v" + juce::String(msg.getControllerValue()) + " ch" + juce::String(ch));

                // ── Global Pad Mute CCs ──
                // CCs (base..base+7) → mute for pad 1..8. Value >= 64 mutes,
                // < 64 unmutes. Accepts on any connected input — distinguishing
                // by source (Pads vs Global device) would require two MIDI
                // collectors and message tagging; deferred to 2.4.9 when the
                // Global routing gets a proper surface.
                if (padMuteCCBase_ >= 0
                    && cc >= padMuteCCBase_ && cc < padMuteCCBase_ + kNumPads)
                {
                    int targetPad = cc - padMuteCCBase_;
                    bool muteNow = (msg.getControllerValue() >= 64);
                    engine_.setMuted(targetPad, muteNow);
                    markStateDirty();
                    // Don't return — let the per-pad loop still process this
                    // CC in case a pad has it mapped to something else too.
                }

                for (int pad = 0; pad < kNumPads; ++pad) {
                    int padCh = engine_.getSlot(pad).getMidiChannel();
                    if (padCh == 0) continue;
                    if (padCh != ch && padCh != 17) continue;

                    auto& slot = engine_.getSlot(pad);
                    auto& ccMap = padCCMaps_[pad];
                    float val100 = juce::jlimit(0.0f, 1.0f, (float)msg.getControllerValue() / 100.0f);

                    // Per-slice direct CC trigger: if a slice has this CC
                    // assigned and the value is non-zero, fire that slice at
                    // velocity = value/127. This is additive with ccStart —
                    // both routing paths can be used at once. If the same CC
                    // happens to match ccStart AND a per-slice CC, per-slice
                    // wins (more specific) and we skip the ccStart branch.
                    bool perSliceFired = false;
                    if (slot.isSliceMode() && slot.getSliceCount() > 0
                        && msg.getControllerValue() > 0)
                    {
                        for (int sIdx = 0; sIdx < slot.getSliceCount(); ++sIdx) {
                            if (slot.getSliceCC(sIdx) == cc) {
                                slot.triggerSlice(sIdx, val);
                                perSliceFired = true;
                            }
                        }
                    }

                    if (!perSliceFired && cc == ccMap.ccStart) {
                        if (slot.isSliceMode() && slot.getSliceCount() > 0) {
                            int numRegions = slot.getSliceCount();
                            int sliceIdx = juce::jlimit(0, numRegions - 1,
                                                         (int)(val100 * numRegions));
                            // Set selection only — actual playback fires when
                            // a note-on or per-slice CC arrives. Don't mutate
                            // slot trim (was causing the visual blackout +
                            // "slicing inside the slice" bug).
                            slot.setSelectedSlice(sliceIdx);
                            slot.setSlicePitchOffset(slot.getSlicePitch(sliceIdx));
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

                // CC-based kit switch (if enabled and CC matches)
                if (progChangeEnabled_ && progChangeCC_ > 0 && cc == progChangeCC_) {
                    int kitIdx = msg.getControllerValue();
                    loadKitByIndex(kitIdx);
                    showTicker("Kit " + juce::String(kitIdx + 1));
                }
            }
            // Program change → kit switch
            else if (msg.isProgramChange() && progChangeEnabled_ && progChangeCC_ == 0) {
                int kitIdx = msg.getProgramChangeNumber();
                loadKitByIndex(kitIdx);
                showTicker("Kit " + juce::String(kitIdx + 1));
            }
        }
    }

    // ── MIDI clock BPM (overrides CV clock when enabled) ────────────────
    // Handled in handleIncomingMidiMessage — midiClockCount_ counts 24 PPQN

    // ── Read pitch CVs ───────────────────────────────────────────────────
    // SSP inputs are normalized ±1.0 = ±5V. V/Oct: 1V = 12 semitones.
    // So buffer value * 5.0 * 12.0 = buffer value * 60.0 semitones.
    // CV ADDS to the encoder pitch (doesn't replace it).
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        int ch = pitchChannel(pad);
        if (ch >= numChannels || !isInputEnabled(ch)) {
            engine_.getSlot(pad).setPitchCVOffset(0.0f);
            continue;
        }

        float voct = buffer.getSample(ch, numSamples - 1);
        if (std::isfinite(voct) && std::abs(voct) > 0.005f)
            engine_.getSlot(pad).setPitchCVOffset(voct * 60.0f);
        else
            engine_.getSlot(pad).setPitchCVOffset(0.0f);
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

    // ── Clear output channels ─────────────────────────────────────────────
    for (int ch = 0; ch < std::min((int)O_MAX, numChannels); ++ch)
        buffer.clear(ch, 0, numSamples);

    float* outL = (O_LEFT < numChannels)  ? buffer.getWritePointer(O_LEFT)  : nullptr;
    float* outR = (O_RIGHT < numChannels) ? buffer.getWritePointer(O_RIGHT) : nullptr;

    // ── Per-pad temp buffers for routing ──────────────────────────────────
    // SSP block sizes are ≤1024, stack allocation is safe
    // ALL pads get per-pad buffers (needed for comp bypass + individual routing)
    float padBufL[kNumPads][1024];
    float padBufR[kNumPads][1024];
    int safeNumSamples = std::min(numSamples, 1024);

    for (int i = 0; i < kNumPads; ++i)
        engine_.setPadOutputBuffers(i, padBufL[i], padBufR[i]);

    if (outL && outR)
        engine_.process(outL, outR, safeNumSamples);

    // Route per-pad buffers to their assigned output channels
    for (int i = 0; i < kNumPads; ++i) {
        int outCh = engine_.getSlot(i).getOutputChannel();
        if (outCh < 0) continue;
        int busIdx = O_PAD1 + outCh;
        if (busIdx < numChannels) {
            float* dst = buffer.getWritePointer(busIdx);
            for (int s = 0; s < safeNumSamples; ++s)
                dst[s] += (padBufL[i][s] + padBufR[i][s]) * 0.5f;
        }
    }
    engine_.clearPadOutputBuffers();

    // ── Comp bypass: subtract bypassed pads from bus before compressor ──
    if (compEnabled_ && outL && outR) {
        for (int i = 0; i < kNumPads; ++i) {
            if (!engine_.getSlot(i).getCompBypass()) continue;
            if (!engine_.getSlot(i).getSendToMix()) continue;
            for (int s = 0; s < safeNumSamples; ++s) {
                outL[s] -= padBufL[i][s];
                outR[s] -= padBufR[i][s];
            }
        }
    }

    // ── Track peak input level for compressor scope display ──────────────
    if (outL && outR) {
        float blockPeak = 0.0f;
        for (int s = 0; s < safeNumSamples; ++s) {
            float p = std::max(std::abs(outL[s]), std::abs(outR[s]));
            if (p > blockPeak) blockPeak = p;
        }
        compDisplayInputPeak_ = blockPeak;
    }

    // ── Bus compressor (feedforward, dual-TC auto-release, soft knee, parallel mix) ──
    // Architecture: processes the full stereo bus. Per-pad mono outputs are NOT
    // affected (they were written before this point). Mix knob controls parallel
    // compression blend: 0% = bypass, 100% = fully compressed.
    if (compEnabled_ && outL && outR && compMix_ > 0.001f) {
        // ── Pre-compute coefficients (once per block) ────────────────
        const float sr = (float)sampleRate_;
        const float attackCoeff = std::exp(-1.0f / (compAttackMs_ * 0.001f * sr));
        const float fastRelCoeff = std::exp(-1.0f / (compReleaseMs_ * 0.001f * sr));
        const float slowRelCoeff = std::exp(-1.0f / (compReleaseMs_ * 0.008f * sr));  // 8x slower
        const float makeupLin = std::pow(10.0f, compMakeupDb_ / 20.0f);
        const float ratio = compRatio_;
        const float thresh = compThreshDb_;
        const float knee = compKneeDb_;
        const float halfK = knee * 0.5f;
        const float drive = compDrive_;
        const float mix = compMix_;

        // ── Update sidechain HPF coefficients if frequency changed ───
        if (compSCHpfHz_ != scHpfLastHz_) updateSCHpfCoeffs();

        // ── Sidechain source: external pad buffer or feedforward ─────
        const float* scSrcL = nullptr;
        const float* scSrcR = nullptr;
        bool scFromPad = (compSCSrc_ >= 0 && compSCSrc_ < kNumPads);
        if (scFromPad) {
            scSrcL = padBufL[compSCSrc_];
            scSrcR = padBufR[compSCSrc_];
        }

        // ── Per-sample processing ────────────────────────────────────
        for (int s = 0; s < safeNumSamples; ++s) {
            // ── 1. Sidechain signal (feedforward from input, or pad) ──
            float sc;
            if (scSrcL) {
                sc = std::max(std::abs(scSrcL[s]), std::abs(scSrcR[s]));
            } else {
                // Feedforward: detect from current uncompressed input
                sc = std::max(std::abs(outL[s]), std::abs(outR[s]));
            }

            // ── 2. Sidechain HPF (remove sub-bass from detector) ─────
            if (compSCHpfHz_ > 0) {
                float x = sc;
                float y = scHpfB0_ * x + scHpfB1_ * scHpfX1_ + scHpfB2_ * scHpfX2_
                        - scHpfA1_ * scHpfY1_ - scHpfA2_ * scHpfY2_;
                scHpfX2_ = scHpfX1_; scHpfX1_ = x;
                scHpfY2_ = scHpfY1_; scHpfY1_ = y;
                sc = std::abs(y);
            }

            // ── 3. Envelope follower (dual-TC auto-release, always on) ─
            if (sc > compEnvFast_)
                compEnvFast_ = attackCoeff * compEnvFast_ + (1.0f - attackCoeff) * sc;
            else
                compEnvFast_ = fastRelCoeff * compEnvFast_;

            if (sc > compEnvSlow_)
                compEnvSlow_ = attackCoeff * compEnvSlow_ + (1.0f - attackCoeff) * sc;
            else
                compEnvSlow_ = slowRelCoeff * compEnvSlow_;

            float env = std::max(compEnvFast_, compEnvSlow_);
            if (env < 1e-20f) env = 0.0f;

            // ── 4. Gain computer (log domain, soft knee, Giannoulis) ──
            float gainReductionDb = 0.0f;
            if (env > 1e-20f) {
                float envDb = 20.0f * std::log10(env + 1e-30f);

                if (envDb < thresh - halfK) {
                    gainReductionDb = 0.0f;
                } else if (envDb > thresh + halfK) {
                    gainReductionDb = (1.0f - 1.0f / ratio) * (envDb - thresh);
                } else {
                    float x = envDb - thresh + halfK;
                    gainReductionDb = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee + 1e-10f);
                }
            }

            float gainLin = std::pow(10.0f, -gainReductionDb / 20.0f);

            // GR meter (smoothed peak hold)
            if (gainReductionDb > compGainReductionDb_)
                compGainReductionDb_ = gainReductionDb;
            else
                compGainReductionDb_ *= 0.9995f;

            // ── 5. Parallel mix: dry * (1-mix) + compressed * mix ─────
            // Makeup gain only applies to the compressed portion.
            float compL = outL[s] * gainLin * makeupLin;
            float compR = outR[s] * gainLin * makeupLin;

            // Subtle output saturation (transformer warmth)
            if (drive > 1.001f) {
                float tanhD = std::tanh(drive);
                compL = std::tanh(drive * compL) / tanhD;
                compR = std::tanh(drive * compR) / tanhD;
            }

            float outSampleL = outL[s] * (1.0f - mix) + compL * mix;
            float outSampleR = outR[s] * (1.0f - mix) + compR * mix;

            outL[s] = outSampleL;
            outR[s] = outSampleR;
        }
    }

    // ── Comp bypass: add bypassed pads back to bus (post-compression) ────
    if (compEnabled_ && outL && outR) {
        for (int i = 0; i < kNumPads; ++i) {
            if (!engine_.getSlot(i).getCompBypass()) continue;
            if (!engine_.getSlot(i).getSendToMix()) continue;
            for (int s = 0; s < safeNumSamples; ++s) {
                outL[s] += padBufL[i][s];
                outR[s] += padBufR[i][s];
            }
        }
    }

    // ── Output low-cut HPF (end-of-chain, 4th order Butterworth -24dB/oct) ──
    if (outputHpfHz_ > 0 && outL && outR) {
        if (outputHpfHz_ != hpfLastHz_) updateOutputHpfCoeffs();
        for (int s = 0; s < safeNumSamples; ++s) {
            for (int sec = 0; sec < 2; ++sec) {
                auto& c = hpfSections_[sec];
                // Left channel
                {
                    auto& st = hpfL_[sec];
                    float x = outL[s];
                    float y = c.b0*x + c.b1*st.x1 + c.b2*st.x2 - c.a1*st.y1 - c.a2*st.y2;
                    st.x2 = st.x1; st.x1 = x;
                    st.y2 = st.y1; st.y1 = y;
                    outL[s] = y;
                }
                // Right channel
                {
                    auto& st = hpfR_[sec];
                    float x = outR[s];
                    float y = c.b0*x + c.b1*st.x1 + c.b2*st.x2 - c.a1*st.y1 - c.a2*st.y2;
                    st.x2 = st.x1; st.x1 = x;
                    st.y2 = st.y1; st.y1 = y;
                    outR[s] = y;
                }
            }
        }
    }

    // ── MIDI-CV gate writer (2.4.9) ──────────────────────────────────
    // For each pad in PadMode::MidiCV, the pad's own output channel
    // (O_PAD1 + padIdx) becomes a gate line. 5V while note held (0.5f
    // in buffer). Silent otherwise. Overwrites (not adds to) any audio
    // that may have leaked from the mix routing above — MidiCV pads
    // must never produce audio.
    {
        for (int p = 0; p < kNumPads; ++p) {
            auto& slot = engine_.getSlot(p);
            if (slot.getMode() != PadMode::MidiCV) continue;

            const int busIdx = O_PAD1 + p;
            if (busIdx >= numChannels) continue;

            // Zero the channel first (kill any stale audio content)
            buffer.clear(busIdx, 0, numSamples);

            const bool muted = engine_.isMuted(p);
            const bool gateActive = midiCVGate_[p] && !muted;
            if (!gateActive) continue;

            float* out = buffer.getWritePointer(busIdx);
            for (int s = 0; s < numSamples; ++s) out[s] = 0.5f;  // 5V
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Bus Compressor — Sidechain HPF coefficient update
// ═══════════════════════════════════════════════════════════════════════════

void PluginProcessor::updateSCHpfCoeffs()
{
    scHpfLastHz_ = compSCHpfHz_;
    if (compSCHpfHz_ <= 0 || sampleRate_ <= 0.0) {
        scHpfB0_ = 1.0f; scHpfB1_ = 0.0f; scHpfB2_ = 0.0f;
        scHpfA1_ = 0.0f; scHpfA2_ = 0.0f;
        return;
    }
    // 2nd order Butterworth HPF
    const double pi = 3.14159265358979323846;
    double w0 = 2.0 * pi * (double)compSCHpfHz_ / sampleRate_;
    double cosW = std::cos(w0);
    double sinW = std::sin(w0);
    double alpha = sinW / (2.0 * 0.7071); // Q = sqrt(2)/2 for Butterworth
    double a0 = 1.0 + alpha;
    scHpfB0_ = (float)((1.0 + cosW) * 0.5 / a0);
    scHpfB1_ = (float)(-(1.0 + cosW) / a0);
    scHpfB2_ = scHpfB0_;
    scHpfA1_ = (float)(-2.0 * cosW / a0);
    scHpfA2_ = (float)((1.0 - alpha) / a0);
    // Reset filter state
    scHpfX1_ = scHpfX2_ = scHpfY1_ = scHpfY2_ = 0.0f;
}

void PluginProcessor::updateOutputHpfCoeffs()
{
    hpfLastHz_ = outputHpfHz_;
    if (outputHpfHz_ <= 0 || sampleRate_ <= 0.0) {
        for (int s = 0; s < 2; ++s)
            hpfSections_[s] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        return;
    }
    // 4th order Butterworth HPF = 2 cascaded 2nd-order sections
    // Q values: Q1 = 0.5412, Q2 = 1.3066 (from Butterworth polynomial)
    const double qVals[2] = { 0.54119610, 1.30656297 };
    const double pi = 3.14159265358979323846;
    for (int s = 0; s < 2; ++s) {
        double w0 = 2.0 * pi * (double)outputHpfHz_ / sampleRate_;
        double cosW = std::cos(w0);
        double sinW = std::sin(w0);
        double alpha = sinW / (2.0 * qVals[s]);
        double a0 = 1.0 + alpha;
        hpfSections_[s].b0 = (float)((1.0 + cosW) * 0.5 / a0);
        hpfSections_[s].b1 = (float)(-(1.0 + cosW) / a0);
        hpfSections_[s].b2 = hpfSections_[s].b0;
        hpfSections_[s].a1 = (float)(-2.0 * cosW / a0);
        hpfSections_[s].a2 = (float)((1.0 - alpha) / a0);
    }
    // Reset state
    for (int s = 0; s < 2; ++s) {
        hpfL_[s] = hpfR_[s] = { 0, 0, 0, 0 };
    }
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

// Bear's SSP host uses ALSA. Real hardware MIDI devices show up with
// their vendor-product name (e.g., "Arturia MiniLab MkII MIDI 1").
// Virtual/loopback devices we don't want in the picker: JUCE's own
// virtual MIDI, the ALSA "Midi Through" port, and any raw ALSA
// clientless entries with an empty name.
//
// The old filter also matched "internal" (too generic — some real USB
// device names contain that substring, e.g. "Internal Audio MIDI").
// Removed 2.4.10 — was causing devices to disappear from the picker.
// Same for "rtmidi" which was speculative.
static bool isInternalMidi(const juce::String& name) {
    auto lower = name.toLowerCase();
    return lower.contains("juce") ||
           lower.contains("midi through") ||
           name.isEmpty();
}

juce::StringArray PluginProcessor::getMidiDeviceNames() const
{
    // Live rescan on every query — without this, a device plugged in after
    // prepareToPlay() won't show up in the picker. The query is cheap
    // (filesystem/ALSA poll, no I/O), so do it every time. Also refreshes
    // the cache as a side effect so other callers see fresh data.
    auto devs = juce::MidiInput::getAvailableDevices();
    cachedMidiDeviceNames_.clear();
    cachedMidiDeviceNames_.add("None");
    int filtered = 0;
    for (auto& d : devs) {
        if (!isInternalMidi(d.name) && d.name.isNotEmpty())
            cachedMidiDeviceNames_.add(d.name);
        else
            filtered++;
    }
    // 2.4.10: diagnostic when Debug Msgs is on. Shows raw device count +
    // how many got filtered. Tester screenshots this if a device won't
    // appear so we can see exactly what JUCE returned vs what we accepted.
    if (debugMsgs_) {
        const_cast<PluginProcessor*>(this)->showTicker(
            "MIDI scan: " + juce::String(devs.size()) + " raw, "
            + juce::String(cachedMidiDeviceNames_.size() - 1) + " visible, "
            + juce::String(filtered) + " filtered");
    }
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

void PluginProcessor::setMidiDevice(const juce::String& nameOrId)
{
    // Per TheTechnobear v260425: prefer identifier over name when matching.
    // Names aren't unique across ports — some devices report the same name
    // on every port. Identifiers are. The argument here can be either:
    // - An identifier (from XML load or programmatic call)
    // - A name (from the UI picker, which displays names)
    // We try identifier match first, then name. Whatever we open, we record
    // BOTH the identifier and the name for later.

    // Early-out: same device already open
    if ((nameOrId == midiDeviceName_ || nameOrId == midiDeviceId_)
        && midiInDevice_ != nullptr) return;

    if (nameOrId.isEmpty() || nameOrId == "None") {
        if (midiInDevice_) {
            midiInDevice_->stop();
            midiInDevice_ = nullptr;
        }
        if (midiDeviceName_.isNotEmpty())
            showTicker("MIDI device disconnected");
        midiDeviceName_.clear();
        midiDeviceId_.clear();
        midiClockEnabled_ = false;
        midiTransportRunning_ = false;
        return;
    }

    if (isInternalMidi(nameOrId)) return;

    if (midiInDevice_) {
        midiInDevice_->stop();
        midiInDevice_ = nullptr;
    }

    auto devs = juce::MidiInput::getAvailableDevices();
    juce::MidiDeviceInfo matched;
    // Pass 1: identifier match (preferred — robust against duplicate names)
    for (auto& d : devs) {
        if (d.identifier == nameOrId && !isInternalMidi(d.name)) {
            matched = d;
            break;
        }
    }
    // Pass 2: name match (fallback for UI picker + legacy patches)
    if (matched.identifier.isEmpty()) {
        for (auto& d : devs) {
            if (d.name == nameOrId && !isInternalMidi(d.name)) {
                matched = d;
                break;
            }
        }
    }
    if (matched.identifier.isEmpty()) return;  // not found, no-op

    midiInDevice_ = juce::MidiInput::openDevice(matched.identifier, this);
    if (midiInDevice_) {
        midiInDevice_->start();
        midiDeviceName_ = matched.name;
        midiDeviceId_   = matched.identifier;
        showTicker("MIDI: " + matched.name);
    }
}

void PluginProcessor::closeMidiDevice()
{
    if (midiInDevice_) {
        midiInDevice_->stop();
        midiInDevice_ = nullptr;  // Bear's pattern
    }
    midiDeviceName_.clear();
    midiDeviceId_.clear();
    midiClockEnabled_ = false;
    midiTransportRunning_ = false;
    midiClockCount_ = 0;
    midiClockLastBeatMs_ = 0.0;
}

void PluginProcessor::setGlobalsMidiDevice(const juce::String& nameOrId)
{
    // Mirrors setMidiDevice — identifier-first matching, name fallback.
    // Andy: the Pad and Global device slots are intentionally independent.
    // If a user wants to point both at the same physical device they can
    // (JUCE handles the duplicate open fine on the SSP host).
    if ((nameOrId == globalsMidiDeviceName_ || nameOrId == globalsMidiDeviceId_)
        && globalsMidiInDevice_ != nullptr) return;

    if (nameOrId.isEmpty() || nameOrId == "None") {
        if (globalsMidiInDevice_) {
            globalsMidiInDevice_->stop();
            globalsMidiInDevice_ = nullptr;
        }
        if (globalsMidiDeviceName_.isNotEmpty())
            showTicker("Global MIDI disconnected");
        globalsMidiDeviceName_.clear();
        globalsMidiDeviceId_.clear();
        return;
    }

    if (isInternalMidi(nameOrId)) return;

    if (globalsMidiInDevice_) {
        globalsMidiInDevice_->stop();
        globalsMidiInDevice_ = nullptr;
    }

    auto devs = juce::MidiInput::getAvailableDevices();
    juce::MidiDeviceInfo matched;
    for (auto& d : devs) {
        if (d.identifier == nameOrId && !isInternalMidi(d.name)) {
            matched = d;
            break;
        }
    }
    if (matched.identifier.isEmpty()) {
        for (auto& d : devs) {
            if (d.name == nameOrId && !isInternalMidi(d.name)) {
                matched = d;
                break;
            }
        }
    }
    if (matched.identifier.isEmpty()) return;

    globalsMidiInDevice_ = juce::MidiInput::openDevice(matched.identifier, this);
    if (globalsMidiInDevice_) {
        globalsMidiInDevice_->start();
        globalsMidiDeviceName_ = matched.name;
        globalsMidiDeviceId_   = matched.identifier;
        showTicker("Global MIDI: " + matched.name);
    }
}

void PluginProcessor::closeGlobalsMidiDevice()
{
    if (globalsMidiInDevice_) {
        globalsMidiInDevice_->stop();
        globalsMidiInDevice_ = nullptr;
    }
    globalsMidiDeviceName_.clear();
    globalsMidiDeviceId_.clear();
}

void PluginProcessor::copyPadSettings(int srcPad)
{
    if (srcPad < 0 || srcPad >= kNumPads) return;
    auto& slot = engine_.getSlot(srcPad);
    padClip_.mode          = slot.getMode();
    padClip_.volume        = slot.getVolume();
    padClip_.pan           = slot.getPan();
    padClip_.pitch         = slot.getPitchSemitones();
    padClip_.stretch       = slot.getTimeStretch();
    padClip_.fadeInMs      = slot.getFadeInMs();
    padClip_.fadeOutMs     = slot.getFadeOutMs();
    padClip_.fadeInCurve   = slot.getFadeInCurve();
    padClip_.fadeOutCurve  = slot.getFadeOutCurve();
    padClip_.choke         = slot.getChokeGroup();
    padClip_.midiChannel   = slot.getMidiChannel();
    padClip_.clockBeats    = slot.getClockBeats();
    padClip_.voiceMode     = slot.getVoiceMode();
    padClip_.filterType    = slot.getFilterType();
    padClip_.filterCutoff  = slot.getFilterCutoff();
    padClip_.filterReso    = slot.getFilterResonance();
    padClip_.lofiMode      = slot.getLofiMode();
    padClip_.compSend      = slot.getCompSend();
    padClip_.compBypass    = slot.getCompBypass();
    padClip_.pitchMode     = slot.getPitchMode();
    padClip_.outputChannel = slot.getOutputChannel();
    padClip_.sendToMix     = slot.getSendToMix();
    padClip_.reversed      = slot.isReversed();
    padClip_.ccMap         = padCCMaps_[srcPad];
    padClipValid_          = true;
    padClipSourcePad_      = srcPad;
}

void PluginProcessor::pasteSettingsToAllPads()
{
    if (!padClipValid_) return;
    for (int p = 0; p < kNumPads; ++p) {
        if (p == padClipSourcePad_) continue;  // skip the source itself
        auto& slot = engine_.getSlot(p);
        slot.setMode(padClip_.mode);
        slot.setVolume(padClip_.volume);
        slot.setPan(padClip_.pan);
        slot.setPitchSemitones(padClip_.pitch);
        slot.setTimeStretch(padClip_.stretch);
        slot.setFadeInMs(padClip_.fadeInMs);
        slot.setFadeOutMs(padClip_.fadeOutMs);
        slot.setFadeInCurve(padClip_.fadeInCurve);
        slot.setFadeOutCurve(padClip_.fadeOutCurve);
        slot.setChokeGroup(padClip_.choke);
        // MIDI channel is INTENTIONALLY NOT pasted — pasting same channel to
        // all 8 pads would create a chaotic OMNI-like situation. Each pad
        // keeps its natural channel.
        slot.setClockBeats(padClip_.clockBeats);
        slot.setVoiceMode(padClip_.voiceMode);
        slot.setFilterType(padClip_.filterType);
        slot.setFilterCutoff(padClip_.filterCutoff);
        slot.setFilterResonance(padClip_.filterReso);
        slot.setLofiMode(padClip_.lofiMode);
        slot.setCompSend(padClip_.compSend);
        slot.setCompBypass(padClip_.compBypass);
        slot.setPitchMode(padClip_.pitchMode);
        slot.setOutputChannel(padClip_.outputChannel);
        slot.setSendToMix(padClip_.sendToMix);
        slot.setReversed(padClip_.reversed);
        // CC map: copy CC numbers but keep per-pad MIDI channel separation
        padCCMaps_[p] = padClip_.ccMap;
    }
    markStateDirty();
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

void PluginProcessor::initPad(int pad)
{
    if (pad < 0 || pad >= kNumPads) return;
    auto& slot = engine_.getSlot(pad);
    slot.clear();  // removes sample
    // Reset all parameters to defaults
    slot.setMode(PadMode::OneShot);
    slot.setVolume(1.0f);
    slot.setPan(0.0f);
    slot.setPitchSemitones(0.0f);
    slot.setTimeStretch(1.0f);
    slot.setStartPos(0.0f);
    slot.setEndPos(1.0f);
    slot.setFadeInMs(0.0f);
    slot.setFadeOutMs(0.0f);
    slot.setFadeInCurve(0);
    slot.setFadeOutCurve(0);
    slot.setChokeGroup(ChokeGroup::None);
    // Default MIDI channel: pad N maps to channel N (1-based). MPC style.
    // Multi-channel sequencer just works; single-channel source only fires
    // pad 1. Users can override to OFF, OMNI, or any channel via ContextBrowser.
    slot.setMidiChannel(pad + 1);
    slot.setClockBeats(4);
    slot.setVoiceMode(VoiceMode::Mono);
    slot.setFilterType(FilterType::Off);
    slot.setFilterCutoff(20000.0f);
    slot.setFilterResonance(0.0f);
    slot.setLofiMode(LofiMode::Off);
    slot.setCompSend(0.0f);
    slot.setCompBypass(false);
    slot.setPitchMode(0);
    slot.setOutputChannel(-1);
    slot.setSendToMix(true);
    slot.setReversed(false);
    engine_.setMuted(pad, false);
    markStateDirty();
}

void PluginProcessor::clearAllPads()
{
    for (int i = 0; i < kNumPads; ++i)
        initPad(i);
    // Reset global settings too
    clockDiv_ = 0;
    compEnabled_ = false;
    midiClockEnabled_ = false;
    showTicker("All pads cleared");
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

void PluginProcessor::loadKitByIndex(int index)
{
    auto kits = getAvailableKits();
    if (index >= 0 && index < kits.size())
        loadKit(kits[index]);
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
        p.compSend = slot.getCompSend();
        p.compBypass = slot.getCompBypass();
        p.pitchMode = slot.getPitchMode();
        p.outputChannel = slot.getOutputChannel();
        p.sendToMix = slot.getSendToMix();
        // Tape (2.4.9)
        p.tapeRate       = slot.getTapeRate();
        p.tapeWow        = slot.getTapeWow();
        p.tapeFlutter    = slot.getTapeFlutter();
        p.tapeHFRolloff  = slot.getTapeHFRolloff();
        p.tapeHeadBump   = slot.getTapeHeadBump();
        p.tapeSaturation = slot.getTapeSaturation();
        p.sliceMode = slot.isSliceMode();
        p.sliceCount = slot.getSliceCount();
        for (int s = 0; s < p.sliceCount; ++s) {
            p.sliceStarts[s] = slot.getSliceStart(s);
            p.sliceEnds[s] = slot.getSliceEnd(s);
            p.slicePitches[s] = slot.getSlicePitch(s);
        }
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
        // MIDI channel preserved across kit switches
        slot.setClockBeats(p.clockBeats);
        slot.setVoiceMode(static_cast<VoiceMode>(p.voiceMode));
        slot.setFilterType(static_cast<FilterType>(p.filterType));
        slot.setFilterCutoff(p.filterCutoff);
        slot.setFilterResonance(p.filterReso);
        slot.setLofiMode(static_cast<LofiMode>(p.lofiMode));
        slot.setCompSend(p.compSend);
        slot.setCompBypass(p.compBypass);
        slot.setPitchMode(p.pitchMode);
        slot.setOutputChannel(p.outputChannel);
        slot.setSendToMix(p.sendToMix);
        // Tape (2.4.9)
        slot.setTapeRate(p.tapeRate);
        slot.setTapeWow(p.tapeWow);
        slot.setTapeFlutter(p.tapeFlutter);
        slot.setTapeHFRolloff(p.tapeHFRolloff);
        slot.setTapeHeadBump(p.tapeHeadBump);
        slot.setTapeSaturation(p.tapeSaturation);
        slot.setSliceMode(p.sliceMode);
        slot.clearSlices();
        for (int s = 0; s < p.sliceCount; ++s) slot.addSlicePair(p.sliceStarts[s], p.sliceEnds[s], p.slicePitches[s]);
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

    bool ok = kit.saveToFile(kitFile);
    if (ok)
        showTicker("Saved: " + name + ".kit");
    else
        showTicker("SAVE FAILED: " + kitFile.getFullPathName());
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
        // MIDI channel preserved across kit switches
        slot.setClockBeats(p.clockBeats);
        slot.setVoiceMode(static_cast<VoiceMode>(p.voiceMode));
        slot.setFilterType(static_cast<FilterType>(p.filterType));
        slot.setFilterCutoff(p.filterCutoff);
        slot.setFilterResonance(p.filterReso);
        slot.setLofiMode(static_cast<LofiMode>(p.lofiMode));
        slot.setCompSend(p.compSend);
        slot.setCompBypass(p.compBypass);
        slot.setPitchMode(p.pitchMode);
        slot.setOutputChannel(p.outputChannel);
        slot.setSendToMix(p.sendToMix);
        // Tape (2.4.9)
        slot.setTapeRate(p.tapeRate);
        slot.setTapeWow(p.tapeWow);
        slot.setTapeFlutter(p.tapeFlutter);
        slot.setTapeHFRolloff(p.tapeHFRolloff);
        slot.setTapeHeadBump(p.tapeHeadBump);
        slot.setTapeSaturation(p.tapeSaturation);
        slot.setSliceMode(p.sliceMode);
        slot.clearSlices();
        for (int s = 0; s < p.sliceCount; ++s) slot.addSlicePair(p.sliceStarts[s], p.sliceEnds[s], p.slicePitches[s]);
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

// ═══════════════════════════════════════════════════════════════════════════
// Autosave — lightweight periodic state persistence (XML only, no WAV)
// ═══════════════════════════════════════════════════════════════════════════

juce::File PluginProcessor::getAutosaveFile() const
{
    auto gridDir = juce::File(sampleRootPath_).getChildFile(".grid");
    if (!gridDir.isDirectory()) gridDir.createDirectory();
    return gridDir.getChildFile("autosave_" + instanceId_ + ".gridstate");
}

void PluginProcessor::cleanupOldAutosaves()
{
    auto gridDir = juce::File(sampleRootPath_).getChildFile(".grid");
    if (!gridDir.isDirectory()) return;
    auto files = gridDir.findChildFiles(juce::File::findFiles, false, "autosave_*.gridstate");
    auto now = juce::Time::getCurrentTime();
    for (auto& f : files) {
        // Delete autosave files older than 24 hours (except current instance)
        if (!f.getFileName().contains(instanceId_)) {
            auto age = now - f.getLastModificationTime();
            if (age.inHours() > 24)
                f.deleteFile();
        }
    }
    // Also delete legacy autosave.gridstate if it exists
    auto legacy = gridDir.getChildFile("autosave.gridstate");
    if (legacy.existsAsFile()) legacy.deleteFile();
    // And old root-level autosave
    auto rootLegacy = juce::File(sampleRootPath_).getChildFile("autosave.gridstate");
    if (rootLegacy.existsAsFile()) rootLegacy.deleteFile();
}

void PluginProcessor::performAutosave()
{
    juce::MemoryBlock state;
    getStateInformation(state);
    auto file = getAutosaveFile();
    if (auto stream = file.createOutputStream()) {
        stream->write(state.getData(), state.getSize());
        lastAutosaveTimeMs_ = juce::Time::getMillisecondCounterHiRes();
        stream->flush();
    }
}

void PluginProcessor::loadAutosave()
{
    auto file = getAutosaveFile();
    if (file.existsAsFile()) {
        juce::MemoryBlock state;
        if (file.loadFileAsData(state) && state.getSize() > 0) {
            setStateInformation(state.getData(), (int)state.getSize());
            return;
        }
    }

    // No file for our UUID — scan for unclaimed autosave files
    auto gridDir = juce::File(sampleRootPath_).getChildFile(".grid");

    // Try legacy root-level autosave first
    auto rootLegacy = juce::File(sampleRootPath_).getChildFile("autosave.gridstate");
    if (rootLegacy.existsAsFile()) {
        juce::MemoryBlock state;
        if (rootLegacy.loadFileAsData(state) && state.getSize() > 0) {
            setStateInformation(state.getData(), (int)state.getSize());
            performAutosave();  // re-save under new instance ID
            rootLegacy.deleteFile();
            return;
        }
    }

    if (!gridDir.isDirectory()) return;

    // Find newest autosave file
    auto files = gridDir.findChildFiles(juce::File::findFiles, false, "autosave_*.gridstate");
    if (files.isEmpty()) return;

    juce::File newest;
    juce::Time newestTime;
    for (auto& f : files) {
        auto mod = f.getLastModificationTime();
        if (mod > newestTime) { newestTime = mod; newest = f; }
    }
    if (newest.existsAsFile()) {
        juce::MemoryBlock state;
        if (newest.loadFileAsData(state) && state.getSize() > 0) {
            setStateInformation(state.getData(), (int)state.getSize());
            // instanceId_ restored from XML by setStateInformation
            // Delete old file and re-save under (possibly new) instance ID
            if (newest != getAutosaveFile())
                newest.deleteFile();
            performAutosave();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Global Preferences — shared across all GRID instances
// ═══════════════════════════════════════════════════════════════════════════

void PluginProcessor::saveGlobalPrefs()
{
    auto gridDir = juce::File(sampleRootPath_).getChildFile(".grid");
    if (!gridDir.isDirectory()) gridDir.createDirectory();
    auto file = gridDir.getChildFile("prefs.xml");

    auto xml = std::make_unique<juce::XmlElement>("GridPrefs");
    xml->setAttribute("encoderSpeed", (double)encoderSpeed_);
    xml->setAttribute("browserFont", (double)browserFontSize_);
    xml->setAttribute("debugMsgs", debugMsgs_ ? 1 : 0);
    xml->setAttribute("autosave", autosaveEnabled_ ? 1 : 0);
    xml->setAttribute("rollDiv", rollStartDiv_);
    xml->setAttribute("crashLog", crashLogEnabled_ ? 1 : 0);
    xml->writeTo(file);
}

void PluginProcessor::loadGlobalPrefs()
{
    auto file = juce::File(sampleRootPath_).getChildFile(".grid").getChildFile("prefs.xml");
    if (!file.existsAsFile()) return;

    auto xml = juce::XmlDocument::parse(file);
    if (!xml || !xml->hasTagName("GridPrefs")) return;

    encoderSpeed_ = juce::jlimit(1.0f, 3.0f, (float)xml->getDoubleAttribute("encoderSpeed", 1.0));
    browserFontSize_ = juce::jlimit(-3.0f, 5.0f, (float)xml->getDoubleAttribute("browserFont", 0.0));
    debugMsgs_ = xml->getIntAttribute("debugMsgs", 0) != 0;
    autosaveEnabled_ = xml->getIntAttribute("autosave", 1) != 0;
    rollStartDiv_ = juce::jlimit(0, 4, xml->getIntAttribute("rollDiv", 3));
    crashLogEnabled_ = xml->getIntAttribute("crashLog", 0) != 0;
}

void PluginProcessor::setRecallPoint()
{
    recallPointData_.reset();
    getStateInformation(recallPointData_);
    recallPointSet_ = true;
}

bool PluginProcessor::restoreRecallPoint()
{
    if (!recallPointSet_ || recallPointData_.getSize() == 0) return false;
    setStateInformation(recallPointData_.getData(), (int)recallPointData_.getSize());
    return true;
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
    if (msg.isMidiStart() && midiTransportEnabled_) {
        midiTransportRunning_ = true;
        midiClockCount_ = 0;
        midiClockLastBeatMs_ = 0.0;
        beatCount_ = 0;
        barCount_ = 0;
        if (midiClockEnabled_) clockActive_ = true;
        return;
    }
    if (msg.isMidiContinue() && midiTransportEnabled_) {
        midiTransportRunning_ = true;
        if (midiClockEnabled_) clockActive_ = true;
        return;
    }
    if (msg.isMidiStop() && midiTransportEnabled_) {
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
    const juce::ScopedLock sl(stateLock_);  // prevent host + autosave racing

    auto logDir = juce::File(sampleRootPath_).getParentDirectory().getChildFile("Grid Logs");
    if (!logDir.isDirectory()) logDir.createDirectory();
    auto logFile = logDir.getChildFile("crash.log");
    bool doLog = crashLogEnabled_;
    if (doLog) logFile.appendText("gsi: enter\n");

    auto xml = std::make_unique<juce::XmlElement>("GRID_STATE");
    xml->setAttribute("version", 1);
    xml->setAttribute("instanceId", instanceId_);

    for (int i = 0; i < kNumPads; ++i)
    {
        if (doLog) logFile.appendText("gsi: pad " + juce::String(i) + " start\n");
        auto& slot = engine_.getSlot(i);
        auto* pad = xml->createNewChildElement("PAD");
        pad->setAttribute("index", i);

        // CRITICAL: if the slot is mid-load, its non-atomic members (filePath_,
        // fileName_, etc.) are being written on another thread. Reading them here
        // is undefined behavior (use-after-free on juce::String internals).
        // Write an empty pad entry and move on — next save will capture it.
        if (slot.isLoading()) {
            pad->setAttribute("file", "");
            if (doLog) logFile.appendText("gsi: pad " + juce::String(i) + " loading, skip\n");
            continue;
        }

        // Store relative path from sample root — safe copy by value
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
        // Per-slice CC (only write non-OFF entries to keep XML compact)
        for (int s = 0; s < SampleSlot::kMaxSlices; ++s) {
            int cc = slot.getSliceCC(s);
            if (cc >= 0) pad->setAttribute("sliceCC" + juce::String(s), cc);
        }
        pad->setAttribute("clockBeats", slot.getClockBeats());
        pad->setAttribute("voiceMode", static_cast<int>(slot.getVoiceMode()));
        pad->setAttribute("filterType", static_cast<int>(slot.getFilterType()));
        pad->setAttribute("filterCutoff", slot.getFilterCutoff());
        pad->setAttribute("filterReso", slot.getFilterResonance());
        pad->setAttribute("lofiMode", static_cast<int>(slot.getLofiMode()));
        pad->setAttribute("compSend", slot.getCompSend());
        pad->setAttribute("compBypass", slot.getCompBypass() ? 1 : 0);
        pad->setAttribute("pitchMode", slot.getPitchMode());
        pad->setAttribute("outputChannel", slot.getOutputChannel());
        pad->setAttribute("sendToMix", slot.getSendToMix() ? 1 : 0);
        // Tape settings (2.4.9 — were being lost on preset reload)
        pad->setAttribute("tapeRate",       (double)slot.getTapeRate());
        pad->setAttribute("tapeWow",        (double)slot.getTapeWow());
        pad->setAttribute("tapeFlutter",    (double)slot.getTapeFlutter());
        pad->setAttribute("tapeHFRolloff",  (double)slot.getTapeHFRolloff());
        pad->setAttribute("tapeHeadBump",   (double)slot.getTapeHeadBump());
        pad->setAttribute("tapeSaturation", (double)slot.getTapeSaturation());
        pad->setAttribute("sliceMode", slot.isSliceMode() ? 1 : 0);
        const int sc = juce::jlimit(0, 128, slot.getSliceCount());  // snapshot + bounds-check
        if (sc > 0) {
            juce::String starts, ends, pitches;
            for (int s = 0; s < sc; ++s) {
                if (s > 0) { starts += ","; ends += ","; pitches += ","; }
                starts += juce::String(slot.getSliceStart(s), 6);
                ends += juce::String(slot.getSliceEnd(s), 6);
                pitches += juce::String(slot.getSlicePitch(s), 2);
            }
            pad->setAttribute("sliceStarts", starts);
            pad->setAttribute("sliceEnds", ends);
            pad->setAttribute("slicePitches", pitches);
        }

        // Step modulation (3 slots per pad)
        for (int ms = 0; ms < 3; ++ms) {
            const auto& mod = slot.getMod(ms);
            juce::String suffix = juce::String(ms);
            pad->setAttribute("modEnabled" + suffix, mod.enabled ? 1 : 0);
            pad->setAttribute("modTarget" + suffix, static_cast<int>(mod.target));
            pad->setAttribute("modAction" + suffix, static_cast<int>(mod.action));
            pad->setAttribute("modStrength" + suffix, (double)mod.strength);
            juce::String stepStr;
            for (int s = 0; s < kModSteps; ++s) {
                if (s > 0) stepStr += ",";
                stepStr += mod.steps[s] ? "1" : "0";
            }
            pad->setAttribute("modSteps" + suffix, stepStr);
            pad->setAttribute("modPreset" + suffix, mod.activePreset);
            // Save 10 presets per mod slot
            for (int pr = 0; pr < kModPresets; ++pr) {
                const auto& preset = mod.presets[pr];
                juce::String pkey = "modP" + suffix + "_" + juce::String(pr);
                juce::String pval = juce::String(static_cast<int>(preset.target)) + ","
                    + juce::String(static_cast<int>(preset.action)) + ","
                    + juce::String(preset.strength, 3);
                for (int s = 0; s < kModSteps; ++s)
                    pval += juce::String(preset.steps[s] ? ",1" : ",0");
                pad->setAttribute(pkey, pval);
            }
        }
        if (doLog) logFile.appendText("gsi: pad " + juce::String(i) + " done\n");
    }

    // Global MIDI settings
    if (doLog) logFile.appendText("gsi: globals\n");
    // Identifier is canonical (matches by ID on reload — robust against
    // name duplication). Name kept around as a fallback for legacy patches
    // and for display when the device is disconnected.
    xml->setAttribute("midiDeviceId",   midiDeviceId_);
    xml->setAttribute("midiDevice",     midiDeviceName_);
    xml->setAttribute("globalsMidiDeviceId", globalsMidiDeviceId_);
    xml->setAttribute("globalsMidiDevice",   globalsMidiDeviceName_);
    xml->setAttribute("padMuteCCBase", padMuteCCBase_);
    xml->setAttribute("midiClock", midiClockEnabled_ ? 1 : 0);
    xml->setAttribute("clockDiv", clockDiv_);

    // Global config
    xml->setAttribute("perfMode", static_cast<int>(perfMode_));
    xml->setAttribute("presetSwitchMode", static_cast<int>(presetSwitchMode_));
    xml->setAttribute("queueBars", queueBars_);
    xml->setAttribute("debugMidi", debugMsgs_ ? 1 : 0);
    xml->setAttribute("encoderSpeed", encoderSpeed_);
    xml->setAttribute("muteFadeMs", muteFadeMs_);
    xml->setAttribute("progChangeEnabled", progChangeEnabled_ ? 1 : 0);
    xml->setAttribute("progChangeCC", progChangeCC_);
    xml->setAttribute("midiTransportEnabled", midiTransportEnabled_ ? 1 : 0);
    xml->setAttribute("browserFontAdj", (double)browserFontSize_);
    xml->setAttribute("sliceCVPad1", sliceCVPad_[0]);
    xml->setAttribute("sliceCVPad2", sliceCVPad_[1]);
    xml->setAttribute("compEnabled", compEnabled_ ? 1 : 0);
    xml->setAttribute("compThreshDb", compThreshDb_);
    xml->setAttribute("compRatio", compRatio_);
    xml->setAttribute("compAttackMs", compAttackMs_);
    xml->setAttribute("compReleaseMs", compReleaseMs_);
    xml->setAttribute("compMakeupDb", compMakeupDb_);
    xml->setAttribute("compKneeDb", compKneeDb_);
    xml->setAttribute("compAutoRelease", compAutoRelease_ ? 1 : 0);
    xml->setAttribute("compSCHpfHz", compSCHpfHz_);
    xml->setAttribute("compSCSrc", compSCSrc_);
    xml->setAttribute("compDrive", compDrive_);
    xml->setAttribute("transSensitivity", transSensitivity_);
    xml->setAttribute("compShowGR", compShowGR_ ? 1 : 0);
    xml->setAttribute("compMix", compMix_);
    xml->setAttribute("outputHpfHz", outputHpfHz_);
    xml->setAttribute("autosaveEnabled", autosaveEnabled_ ? 1 : 0);

    // Save slice cache (per-file slice persistence)
    {
        const juce::ScopedLock sl(sliceCacheLock_);
        for (auto& pair : sliceCache_) {
            auto* cacheEl = xml->createNewChildElement("SLICE_CACHE");
            cacheEl->setAttribute("file", pair.first);
            int count = juce::jlimit(0, 128, pair.second.count);  // bounds-check
            cacheEl->setAttribute("count", count);
            juce::String starts, ends, pitches;
            for (int i = 0; i < count; ++i) {
                if (i > 0) { starts += ","; ends += ","; pitches += ","; }
                starts += juce::String(pair.second.starts[i], 6);
                ends += juce::String(pair.second.ends[i], 6);
                pitches += juce::String(pair.second.pitches[i], 2);
            }
            cacheEl->setAttribute("starts", starts);
            cacheEl->setAttribute("ends", ends);
            cacheEl->setAttribute("pitches", pitches);
        }
    }  // end sliceCacheLock_

    if (doLog) logFile.appendText("gsi: slice cache done, calling copyXmlToBinary\n");
    copyXmlToBinary(*xml, destData);
    if (doLog) logFile.appendText("gsi: done, " + juce::String((int)destData.getSize()) + " bytes\n");
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::ScopedLock sl(stateLock_);  // prevent host + autosave racing

    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || xml->getTagName() != "GRID_STATE") return;

    stateLoadedFromHost_ = true;

    // ── CRITICAL: Reset ALL pads to defaults BEFORE loading ────────────
    // Without this, old autosave data persists in pads not covered by the preset
    for (int i = 0; i < kNumPads; ++i) {
        auto& slot = engine_.getSlot(i);
        slot.clear();
        slot.setMode(PadMode::OneShot);
        slot.setVolume(1.0f);
        slot.setPan(0.0f);
        slot.setPitchSemitones(0.0f);
        slot.setTimeStretch(1.0f);
        slot.setStartPos(0.0f);
        slot.setEndPos(1.0f);
        slot.setFadeInMs(0.0f);
        slot.setFadeOutMs(0.0f);
        slot.setFadeInCurve(0);
        slot.setFadeOutCurve(0);
        slot.setChokeGroup(ChokeGroup::None);
        slot.setMidiChannel(i + 1);  // 1:1 pad-to-channel default (MPC style)
        slot.setClockBeats(4);
        slot.setVoiceMode(VoiceMode::Mono);
        slot.setFilterType(FilterType::Off);
        slot.setFilterCutoff(20000.0f);
        slot.setFilterResonance(0.0f);
        slot.setLofiMode(LofiMode::Off);
        slot.setCompSend(0.0f);
        slot.setCompBypass(false);
        slot.setPitchMode(0);
        slot.setOutputChannel(-1);
        slot.setSendToMix(true);
        slot.setReversed(false);
        engine_.setMuted(i, false);
    }

    // Adopt saved instance ID (for per-instance autosave)
    auto savedId = xml->getStringAttribute("instanceId");
    if (savedId.isNotEmpty())
        instanceId_ = savedId;
    cleanupOldAutosaves();

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
        slot.setReversed(pad->getIntAttribute("reversed", 0) != 0);
        // Default missing midiChannel attribute to pad's natural channel
        // (Pad N → Ch N) so patches saved before per-pad channel defaults
        // existed come back online instead of being stuck at OFF.
        slot.setMidiChannel(pad->getIntAttribute("midiChannel", i + 1));

        // Per-pad CC map. Missing-attribute defaults updated to the new
        // safe values (off CC1/mod wheel). Old patches with explicit ccStart=1
        // keep their value — only patches missing the attribute get the new
        // default.
        auto& ccMap = padCCMaps_[i];
        ccMap.ccStart   = pad->getIntAttribute("ccStart",   16);
        ccMap.ccEnd     = pad->getIntAttribute("ccEnd",     17);
        ccMap.ccVolume  = pad->getIntAttribute("ccVolume",  7);
        ccMap.ccPan     = pad->getIntAttribute("ccPan",     10);
        ccMap.ccStretch = pad->getIntAttribute("ccStretch", 18);
        ccMap.ccFilter  = pad->getIntAttribute("ccFilter", 74);
        // Per-slice CC array (sliceCC0..sliceCCN). Missing = OFF (-1).
        for (int s = 0; s < SampleSlot::kMaxSlices; ++s) {
            int cc = pad->getIntAttribute("sliceCC" + juce::String(s), -1);
            slot.setSliceCC(s, cc);
        }
        slot.setClockBeats(pad->getIntAttribute("clockBeats", 4));
        slot.setVoiceMode(static_cast<VoiceMode>(pad->getIntAttribute("voiceMode", 0)));
        slot.setFilterType(static_cast<FilterType>(pad->getIntAttribute("filterType", 0)));
        slot.setFilterCutoff((float)pad->getDoubleAttribute("filterCutoff", 20000.0));
        slot.setFilterResonance((float)pad->getDoubleAttribute("filterReso", 0.0));
        slot.setLofiMode(static_cast<LofiMode>(pad->getIntAttribute("lofiMode", 0)));
        slot.setCompSend((float)pad->getDoubleAttribute("compSend", 0.0));
        slot.setCompBypass(pad->getIntAttribute("compBypass", 0) != 0);
        slot.setPitchMode(pad->getIntAttribute("pitchMode", 0));
        slot.setOutputChannel(pad->getIntAttribute("outputChannel", -1));
        slot.setSendToMix(pad->getIntAttribute("sendToMix", 1) != 0);
        // Tape settings (2.4.9) — default rate = 1.0, rest = 0.0 so old
        // patches without these attributes come back neutral.
        slot.setTapeRate      ((float)pad->getDoubleAttribute("tapeRate",       1.0));
        slot.setTapeWow       ((float)pad->getDoubleAttribute("tapeWow",        0.0));
        slot.setTapeFlutter   ((float)pad->getDoubleAttribute("tapeFlutter",    0.0));
        slot.setTapeHFRolloff ((float)pad->getDoubleAttribute("tapeHFRolloff",  0.0));
        slot.setTapeHeadBump  ((float)pad->getDoubleAttribute("tapeHeadBump",   0.0));
        slot.setTapeSaturation((float)pad->getDoubleAttribute("tapeSaturation", 0.0));
        slot.setSliceMode(pad->getIntAttribute("sliceMode", 0) != 0);
        slot.clearSlices();
        auto startsStr = pad->getStringAttribute("sliceStarts", "");
        auto endsStr = pad->getStringAttribute("sliceEnds", "");
        if (startsStr.isNotEmpty() && endsStr.isNotEmpty()) {
            juce::StringArray sTok, eTok;
            sTok.addTokens(startsStr, ",", "");
            eTok.addTokens(endsStr, ",", "");
            int count = std::min(sTok.size(), eTok.size());
            for (int s = 0; s < count && s < 64; ++s)
                slot.addSlicePair(sTok[s].getFloatValue(), eTok[s].getFloatValue());
        }
        auto pitchStr = pad->getStringAttribute("slicePitches", "");
        if (pitchStr.isNotEmpty()) {
            juce::StringArray ptokens;
            ptokens.addTokens(pitchStr, ",", "");
            for (int s = 0; s < ptokens.size() && s < 64; ++s)
                slot.setSlicePitch(s, ptokens[s].getFloatValue());
        }

        // Restore step modulation (3 slots per pad)
        for (int ms = 0; ms < 3; ++ms) {
            auto& mod = slot.getMod(ms);
            juce::String suffix = juce::String(ms);
            mod.enabled  = pad->getIntAttribute("modEnabled" + suffix, 0) != 0;
            mod.target   = static_cast<ModTarget>(juce::jlimit(0, (int)ModTarget::kCount - 1, pad->getIntAttribute("modTarget" + suffix, 0)));
            mod.action   = static_cast<ModAction>(juce::jlimit(0, (int)ModAction::kCount - 1, pad->getIntAttribute("modAction" + suffix, 0)));
            mod.strength = juce::jlimit(0.0f, 1.0f, (float)pad->getDoubleAttribute("modStrength" + suffix, 0.5));
            auto stepStr = pad->getStringAttribute("modSteps" + suffix, "");
            if (stepStr.isNotEmpty()) {
                juce::StringArray stokens;
                stokens.addTokens(stepStr, ",", "");
                for (int s = 0; s < stokens.size() && s < kModSteps; ++s)
                    mod.steps[s] = (stokens[s].getIntValue() != 0);
            }
            mod.currentStep = 0;
            mod.activePreset = juce::jlimit(0, kModPresets, pad->getIntAttribute("modPreset" + suffix, 0));
            // Restore 10 presets per mod slot
            for (int pr = 0; pr < kModPresets; ++pr) {
                juce::String pkey = "modP" + suffix + "_" + juce::String(pr);
                auto pval = pad->getStringAttribute(pkey, "");
                if (pval.isNotEmpty()) {
                    juce::StringArray ptok;
                    ptok.addTokens(pval, ",", "");
                    if (ptok.size() >= 3) {
                        mod.presets[pr].target = static_cast<ModTarget>(juce::jlimit(0, (int)ModTarget::kCount - 1, ptok[0].getIntValue()));
                        mod.presets[pr].action = static_cast<ModAction>(juce::jlimit(0, (int)ModAction::kCount - 1, ptok[1].getIntValue()));
                        mod.presets[pr].strength = juce::jlimit(0.0f, 1.0f, ptok[2].getFloatValue());
                        for (int s = 0; s < kModSteps && (s + 3) < ptok.size(); ++s)
                            mod.presets[pr].steps[s] = (ptok[s + 3].getIntValue() != 0);
                    }
                }
            }
        }
    }

    // Global MIDI settings
    // Load MIDI devices: try identifier first (canonical), fall back to
    // legacy name-only save format for patches saved before v2.4.8.
    juce::String midiDevId   = xml->getStringAttribute("midiDeviceId");
    juce::String midiDevName = xml->getStringAttribute("midiDevice");
    if (midiDevId.isNotEmpty())        setMidiDevice(midiDevId);
    else if (midiDevName.isNotEmpty()) setMidiDevice(midiDevName);

    juce::String globalsDevId   = xml->getStringAttribute("globalsMidiDeviceId");
    juce::String globalsDevName = xml->getStringAttribute("globalsMidiDevice");
    if (globalsDevId.isNotEmpty())        setGlobalsMidiDevice(globalsDevId);
    else if (globalsDevName.isNotEmpty()) setGlobalsMidiDevice(globalsDevName);

    padMuteCCBase_ = juce::jlimit(-1, 120, xml->getIntAttribute("padMuteCCBase", 24));
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
    progChangeEnabled_ = xml->getIntAttribute("progChangeEnabled", 0) != 0;
    progChangeCC_ = juce::jlimit(0, 127, xml->getIntAttribute("progChangeCC", 0));
    midiTransportEnabled_ = xml->getIntAttribute("midiTransportEnabled", 1) != 0;
    // browserFontSize_ intentionally NOT loaded from per-preset state — global pref only
    // (kept in save for backward compatibility)
    sliceCVPad_[0] = juce::jlimit(-1, 7, xml->getIntAttribute("sliceCVPad1", 0));
    sliceCVPad_[1] = juce::jlimit(-1, 7, xml->getIntAttribute("sliceCVPad2", 1));
    compEnabled_ = xml->getIntAttribute("compEnabled", 0) != 0;
    compThreshDb_ = (float)xml->getDoubleAttribute("compThreshDb", -12.0);
    compRatio_ = (float)xml->getDoubleAttribute("compRatio", 4.0);
    compAttackMs_ = (float)xml->getDoubleAttribute("compAttackMs", 5.0);
    compReleaseMs_ = (float)xml->getDoubleAttribute("compReleaseMs", 80.0);
    compMakeupDb_ = (float)xml->getDoubleAttribute("compMakeupDb", 0.0);
    compKneeDb_ = (float)xml->getDoubleAttribute("compKneeDb", 6.0);
    compAutoRelease_ = xml->getIntAttribute("compAutoRelease", 1) != 0;
    compSCHpfHz_ = xml->getIntAttribute("compSCHpfHz", 80);
    compSCSrc_ = juce::jlimit(-1, 7, xml->getIntAttribute("compSCSrc", -1));
    compDrive_ = (float)xml->getDoubleAttribute("compDrive", 1.03);
    transSensitivity_ = (float)xml->getDoubleAttribute("transSensitivity", 0.3);
    compShowGR_ = xml->getIntAttribute("compShowGR", 0) != 0;
    compMix_ = (float)xml->getDoubleAttribute("compMix", 1.0);
    outputHpfHz_ = xml->getIntAttribute("outputHpfHz", 0);
    // autosaveEnabled_ intentionally NOT loaded from per-preset state — global pref only

    // Load slice cache
    {
        const juce::ScopedLock sl(sliceCacheLock_);
        sliceCache_.clear();
        for (int i = 0; i < xml->getNumChildElements(); ++i) {
            auto* el = xml->getChildElement(i);
            if (!el || el->getTagName() != "SLICE_CACHE") continue;
            juce::String filePath = el->getStringAttribute("file");
            if (filePath.isEmpty()) continue;
            SliceCache cache;
            cache.count = el->getIntAttribute("count", 0);
            auto starts = juce::StringArray::fromTokens(el->getStringAttribute("starts"), ",", "");
            auto ends = juce::StringArray::fromTokens(el->getStringAttribute("ends"), ",", "");
            auto pitches = juce::StringArray::fromTokens(el->getStringAttribute("pitches"), ",", "");
            for (int s = 0; s < cache.count && s < 128; ++s) {
                cache.starts[s] = (s < starts.size()) ? starts[s].getFloatValue() : 0.0f;
                cache.ends[s] = (s < ends.size()) ? ends[s].getFloatValue() : 1.0f;
                cache.pitches[s] = (s < pitches.size()) ? pitches[s].getFloatValue() : 0.0f;
            }
            sliceCache_[filePath] = cache;
        }
    }  // end sliceCacheLock_
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// Slice Export — write each slice region as individual WAV file
// ═══════════════════════════════════════════════════════════════════════════

int PluginProcessor::exportSlicesToFiles(int pad)
{
    auto& slot = engine_.getSlot(juce::jlimit(0, kNumPads - 1, pad));
    if (!slot.isLoaded() || slot.getSliceCount() == 0) return 0;

    const auto& buf = slot.getBuffer();
    int ns = slot.getNumSamples();
    int nch = buf.getNumChannels();
    double sr = slot.getSampleRate();
    juce::String baseName = slot.getFileName().upToLastOccurrenceOf(".", false, false);

    // Create export directory: same folder as sample, subfolder = samplename_slices
    juce::File srcFile(slot.getFilePath());
    juce::File exportDir = srcFile.getParentDirectory().getChildFile(baseName + "_slices");
    exportDir.createDirectory();

    juce::WavAudioFormat wav;
    int exported = 0;

    for (int i = 0; i < slot.getSliceCount(); ++i) {
        float slStart = slot.getSliceStart(i);
        float slEnd = slot.getSliceEnd(i);
        int startSamp = (int)(slStart * ns);
        int endSamp = (int)(slEnd * ns);
        int len = endSamp - startSamp;
        if (len <= 0) continue;

        juce::AudioBuffer<float> sliceBuf(nch, len);
        for (int ch = 0; ch < nch; ++ch)
            sliceBuf.copyFrom(ch, 0, buf, ch, startSamp, len);

        juce::String sliceName = baseName + "_slice_" + juce::String(i + 1).paddedLeft('0', 3) + ".wav";
        juce::File outFile = exportDir.getChildFile(sliceName);

        std::unique_ptr<juce::FileOutputStream> fos(outFile.createOutputStream());
        if (fos) {
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(fos.get(), sr, (unsigned int)nch, 24, {}, 0));
            if (writer) {
                fos.release();  // writer takes ownership
                writer->writeFromAudioSampleBuffer(sliceBuf, 0, len);
                exported++;
            }
        }
    }
    return exported;
}

int PluginProcessor::importPadsAsSliceChain(int targetPad)
{
    // Concatenate audio from all currently-loaded pads into one buffer,
    // load it into targetPad, and create slice points at each pad boundary.
    // Inverse of exportSlicesToFiles — Octatrack-style sample chain.
    targetPad = juce::jlimit(0, kNumPads - 1, targetPad);

    // Gather source pads (in pad-order), excluding the target.
    struct Src { int pad; int frames; int nch; double sr; };
    std::vector<Src> srcs;
    int totalFrames = 0;
    int maxChannels = 1;
    double useSampleRate = 0.0;

    for (int p = 0; p < kNumPads; ++p) {
        if (p == targetPad) continue;
        auto& s = engine_.getSlot(p);
        if (!s.isLoaded() || s.getNumSamples() <= 0) continue;
        int frames = s.getNumSamples();
        int nch = s.getBuffer().getNumChannels();
        double sr = s.getSampleRate();
        if (useSampleRate == 0.0) useSampleRate = sr;
        srcs.push_back({ p, frames, nch, sr });
        totalFrames += frames;
        if (nch > maxChannels) maxChannels = nch;
    }

    if (srcs.empty() || totalFrames <= 0) return 0;

    // Allocate the combined buffer (always stereo for output simplicity).
    juce::AudioBuffer<float> combined(maxChannels, totalFrames);
    combined.clear();

    // Copy each pad's audio in sequence. If a source pad's sample rate
    // differs from useSampleRate, we copy 1:1 (no resampling) — the rate
    // mismatch is documented; consistent SR among pads is the common case.
    int writeOffset = 0;
    juce::Array<float> sliceBoundaries;   // normalized 0..1 boundaries
    sliceBoundaries.add(0.0f);

    for (auto& src : srcs) {
        auto& s = engine_.getSlot(src.pad);
        const auto& srcBuf = s.getBuffer();
        for (int ch = 0; ch < maxChannels; ++ch) {
            int srcCh = std::min(ch, src.nch - 1);
            combined.copyFrom(ch, writeOffset, srcBuf, srcCh, 0, src.frames);
        }
        writeOffset += src.frames;
        sliceBoundaries.add((float)writeOffset / (float)totalFrames);
    }

    // Load combined buffer into target pad
    auto& dst = engine_.getSlot(targetPad);
    juce::String chainName = "chain_" + juce::String((int)juce::Time::currentTimeMillis() % 100000);
    dst.loadFromBuffer(combined, totalFrames, useSampleRate, chainName, juce::String());

    // Create slice pairs: one slice per source pad
    dst.clearSlices();
    for (int i = 0; i < (int)srcs.size(); ++i) {
        float s = sliceBoundaries[i];
        float e = sliceBoundaries[i + 1];
        dst.addSlicePair(s, e);
    }

    return (int)srcs.size();
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-sample slice persistence — cache slices keyed by filename
// ═══════════════════════════════════════════════════════════════════════════

void PluginProcessor::cacheSlicesForPad(int pad)
{
    auto& slot = engine_.getSlot(juce::jlimit(0, kNumPads - 1, pad));
    if (!slot.isLoaded()) return;

    // Always update the cache for this file's path, even when slot has 0
    // slices. Previously this early-returned on 0 slices, leaving stale
    // entries from prior sessions or earlier in this session — so audition
    // or restoreCachedSlices could resurrect cleared slices later.
    SliceCache cache;
    cache.count = slot.getSliceCount();
    for (int i = 0; i < cache.count; ++i) {
        cache.starts[i] = slot.getSliceStart(i);
        cache.ends[i] = slot.getSliceEnd(i);
        cache.pitches[i] = slot.getSlicePitch(i);
    }
    const juce::ScopedLock sl(sliceCacheLock_);
    if (cache.count == 0) {
        // Explicitly remove the entry so restoreCachedSlices won't resurrect
        // anything. (Storing an empty entry would also work, but removing is
        // cleaner — the map stays tidy.)
        sliceCache_.erase(slot.getFilePath());
    } else {
        sliceCache_[slot.getFilePath()] = cache;
    }
}

bool PluginProcessor::restoreCachedSlices(int pad)
{
    auto& slot = engine_.getSlot(juce::jlimit(0, kNumPads - 1, pad));
    if (!slot.isLoaded()) return false;

    const juce::ScopedLock sl(sliceCacheLock_);
    auto it = sliceCache_.find(slot.getFilePath());
    if (it == sliceCache_.end()) return false;

    auto& cache = it->second;
    slot.clearSlices();
    for (int i = 0; i < cache.count; ++i)
        slot.addSlicePair(cache.starts[i], cache.ends[i], cache.pitches[i]);
    return true;
}

} // namespace grid
