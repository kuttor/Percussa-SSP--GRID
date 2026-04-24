#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
                            if (slot.isSliceMode() && slot.getSliceCount() > 0) {
                                // Slice mode: use currently selected slice
                                // (set by CC Slice, CV, or note-based mapping)
                                int sliceIdx = slot.getSelectedSlice();
                                // Note-based slice mapping: if note is in
                                // chromatic range (C2+), override selection
                                // ONLY if no CC slice is assigned for this pad
                                if (padCCMaps_[pad].ccStart == 0) {
                                    sliceIdx = juce::jlimit(0, std::max(0, slot.getSliceCount() - 1),
                                                             note - 36);
                                }
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
                            int numRegions = slot.getSliceCount();
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

    // ── Clear output channels ─────────────────────────────────────────────
    for (int ch = 0; ch < std::min((int)O_MAX, numChannels); ++ch)
        buffer.clear(ch, 0, numSamples);

    float* outL = (O_LEFT < numChannels)  ? buffer.getWritePointer(O_LEFT)  : nullptr;
    float* outR = (O_RIGHT < numChannels) ? buffer.getWritePointer(O_RIGHT) : nullptr;

    // ── Per-pad temp buffers for routing ──────────────────────────────────
    // SSP block sizes are ≤1024, stack allocation is safe
    float padBufL[kNumPads][1024];
    float padBufR[kNumPads][1024];
    int safeNumSamples = std::min(numSamples, 1024);

    bool anyRouted = false;
    for (int i = 0; i < kNumPads; ++i) {
        int outCh = engine_.getSlot(i).getOutputChannel();
        bool isSCSrc = (compEnabled_ && compSCSrc_ == i);
        if (outCh >= 0 || isSCSrc) {
            engine_.setPadOutputBuffers(i, padBufL[i], padBufR[i]);
            anyRouted = true;
        }
    }

    if (outL && outR)
        engine_.process(outL, outR, safeNumSamples);

    // Route per-pad buffers to their assigned output channels
    if (anyRouted) {
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
    }

    // ── Bus compressor (SSL-style feedback, dual-time-constant, soft knee) ──
    if (compEnabled_ && outL && outR) {
        // Check if any pad has send > 0
        bool anySend = false;
        for (int i = 0; i < kNumPads; ++i)
            if (engine_.getSlot(i).getCompSend() > 0.01f) { anySend = true; break; }

        if (anySend) {
            // ── Pre-compute coefficients (once per block) ────────────────
            const float sr = (float)sampleRate_;
            const float attackCoeff = std::exp(-1.0f / (compAttackMs_ * 0.001f * sr));
            const float fastRelCoeff = std::exp(-1.0f / (compReleaseMs_ * 0.001f * sr));
            // Slow release: 8x the fast time constant (SSL-style dual TC)
            const float slowRelCoeff = std::exp(-1.0f / (compReleaseMs_ * 0.008f * sr));
            const float makeupLin = std::pow(10.0f, compMakeupDb_ / 20.0f);
            const float ratio = compRatio_;
            const float thresh = compThreshDb_;
            const float knee = compKneeDb_;
            const float halfK = knee * 0.5f;
            const float drive = compDrive_;

            // ── Update sidechain HPF coefficients if frequency changed ───
            if (compSCHpfHz_ != scHpfLastHz_) updateSCHpfCoeffs();

            // ── Pre-compute wet blend from active pad sends ──────────────
            float totalSend = 0.0f;
            int activePads = 0;
            for (int p = 0; p < kNumPads; ++p) {
                if (engine_.getSlot(p).isPlaying() || engine_.getSlot(p).getCompSend() > 0.01f) {
                    totalSend += engine_.getSlot(p).getCompSend();
                    activePads++;
                }
            }
            const float wet = (activePads > 0) ? juce::jlimit(0.0f, 1.0f, totalSend / (float)activePads) : 0.0f;

            // ── Sidechain source: external pad or bus feedback ───────────
            const float* scSrcL = nullptr;
            const float* scSrcR = nullptr;
            bool scFromPad = (compSCSrc_ >= 0 && compSCSrc_ < kNumPads);
            if (scFromPad) {
                // Per-pad buffers are on the stack (padBufL/padBufR) —
                // allocated for SC source pads above, still valid here.
                scSrcL = padBufL[compSCSrc_];
                scSrcR = padBufR[compSCSrc_];
            }

            // ── Per-sample processing ────────────────────────────────────
            for (int s = 0; s < safeNumSamples; ++s) {
                // ── 1. Sidechain signal (feedback from output, or pad) ───
                float sc;
                if (scSrcL) {
                    sc = std::max(std::abs(scSrcL[s]), std::abs(scSrcR[s]));
                } else {
                    // Feedback topology: detect from previous output
                    sc = std::max(std::abs(compPrevOutL_), std::abs(compPrevOutR_));
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

                // ── 3. Envelope follower (dual-time-constant) ────────────
                // Fast envelope
                if (sc > compEnvFast_)
                    compEnvFast_ = attackCoeff * compEnvFast_ + (1.0f - attackCoeff) * sc;
                else
                    compEnvFast_ = fastRelCoeff * compEnvFast_;

                // Slow envelope
                if (sc > compEnvSlow_)
                    compEnvSlow_ = attackCoeff * compEnvSlow_ + (1.0f - attackCoeff) * sc;
                else
                    compEnvSlow_ = slowRelCoeff * compEnvSlow_;

                // Use whichever is higher (auto-release behavior)
                float env = compAutoRelease_ ? std::max(compEnvFast_, compEnvSlow_) : compEnvFast_;

                // Denormal guard
                if (env < 1e-20f) env = 0.0f;

                // ── 4. Gain computer (log domain, soft knee) ─────────────
                float gainReductionDb = 0.0f;
                if (env > 1e-20f) {
                    float envDb = 20.0f * std::log10(env + 1e-30f);

                    if (envDb < thresh - halfK) {
                        // Below knee: no compression
                        gainReductionDb = 0.0f;
                    } else if (envDb > thresh + halfK) {
                        // Above knee: full ratio
                        gainReductionDb = (1.0f - 1.0f / ratio) * (envDb - thresh);
                    } else {
                        // Inside knee: quadratic interpolation (Giannoulis)
                        float x = envDb - thresh + halfK;
                        gainReductionDb = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee + 1e-10f);
                    }
                }

                float gainLin = std::pow(10.0f, -gainReductionDb / 20.0f);

                // Store peak GR for visualization (smoothed)
                if (gainReductionDb > compGainReductionDb_)
                    compGainReductionDb_ = gainReductionDb;
                else
                    compGainReductionDb_ *= 0.9995f;  // slow decay for meter

                // ── 5. Apply gain with wet/dry blend ─────────────────────
                // Makeup gain only applies to the compressed (wet) portion.
                // Dry portion passes at unity so uncompressed pads aren't boosted.
                float compGainL = (1.0f - wet) + wet * gainLin * makeupLin;
                float compGainR = compGainL;  // stereo linked

                float outSampleL = outL[s] * compGainL;
                float outSampleR = outR[s] * compGainR;

                // ── 6. Subtle output saturation (transformer warmth) ─────
                if (drive > 1.001f) {
                    // tanh soft clip — adds odd harmonics, very gentle
                    outSampleL = std::tanh(drive * outSampleL) / std::tanh(drive);
                    outSampleR = std::tanh(drive * outSampleR) / std::tanh(drive);
                }

                // Store feedback state
                compPrevOutL_ = outSampleL;
                compPrevOutR_ = outSampleR;

                outL[s] = outSampleL;
                outR[s] = outSampleR;
            }
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
        p.outputChannel = slot.getOutputChannel();
        p.sendToMix = slot.getSendToMix();
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
        slot.setMidiChannel(p.midiCh);
        slot.setClockBeats(p.clockBeats);
        slot.setVoiceMode(static_cast<VoiceMode>(p.voiceMode));
        slot.setFilterType(static_cast<FilterType>(p.filterType));
        slot.setFilterCutoff(p.filterCutoff);
        slot.setFilterResonance(p.filterReso);
        slot.setLofiMode(static_cast<LofiMode>(p.lofiMode));
        slot.setCompSend(p.compSend);
        slot.setOutputChannel(p.outputChannel);
        slot.setSendToMix(p.sendToMix);
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
        slot.setCompSend(p.compSend);
        slot.setOutputChannel(p.outputChannel);
        slot.setSendToMix(p.sendToMix);
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
        pad->setAttribute("compSend", slot.getCompSend());
        pad->setAttribute("outputChannel", slot.getOutputChannel());
        pad->setAttribute("sendToMix", slot.getSendToMix() ? 1 : 0);
        pad->setAttribute("sliceMode", slot.isSliceMode() ? 1 : 0);
        const int sc = slot.getSliceCount();  // snapshot once — avoid race with audio thread
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
    xml->setAttribute("progChangeEnabled", progChangeEnabled_ ? 1 : 0);
    xml->setAttribute("progChangeCC", progChangeCC_);
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

    // Save slice cache (per-file slice persistence)
    for (auto& pair : sliceCache_) {
        auto* cacheEl = xml->createNewChildElement("SLICE_CACHE");
        cacheEl->setAttribute("file", pair.first);
        cacheEl->setAttribute("count", pair.second.count);
        juce::String starts, ends, pitches;
        for (int i = 0; i < pair.second.count; ++i) {
            if (i > 0) { starts += ","; ends += ","; pitches += ","; }
            starts += juce::String(pair.second.starts[i], 6);
            ends += juce::String(pair.second.ends[i], 6);
            pitches += juce::String(pair.second.pitches[i], 2);
        }
        cacheEl->setAttribute("starts", starts);
        cacheEl->setAttribute("ends", ends);
        cacheEl->setAttribute("pitches", pitches);
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
        slot.setCompSend((float)pad->getDoubleAttribute("compSend", 0.0));
        slot.setOutputChannel(pad->getIntAttribute("outputChannel", -1));
        slot.setSendToMix(pad->getIntAttribute("sendToMix", 1) != 0);
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
    progChangeEnabled_ = xml->getIntAttribute("progChangeEnabled", 0) != 0;
    progChangeCC_ = juce::jlimit(0, 127, xml->getIntAttribute("progChangeCC", 0));
    browserFontSize_ = juce::jlimit(-3.0f, 3.0f, (float)xml->getDoubleAttribute("browserFontAdj", 0.0));
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

    // Load slice cache
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

// ═══════════════════════════════════════════════════════════════════════════
// Per-sample slice persistence — cache slices keyed by filename
// ═══════════════════════════════════════════════════════════════════════════

void PluginProcessor::cacheSlicesForPad(int pad)
{
    auto& slot = engine_.getSlot(juce::jlimit(0, kNumPads - 1, pad));
    if (!slot.isLoaded() || slot.getSliceCount() == 0) return;

    SliceCache cache;
    cache.count = slot.getSliceCount();
    for (int i = 0; i < cache.count; ++i) {
        cache.starts[i] = slot.getSliceStart(i);
        cache.ends[i] = slot.getSliceEnd(i);
        cache.pitches[i] = slot.getSlicePitch(i);
    }
    sliceCache_[slot.getFilePath()] = cache;
}

bool PluginProcessor::restoreCachedSlices(int pad)
{
    auto& slot = engine_.getSlot(juce::jlimit(0, kNumPads - 1, pad));
    if (!slot.isLoaded()) return false;

    auto it = sliceCache_.find(slot.getFilePath());
    if (it == sliceCache_.end()) return false;

    auto& cache = it->second;
    slot.clearSlices();
    for (int i = 0; i < cache.count; ++i)
        slot.addSlicePair(cache.starts[i], cache.ends[i], cache.pitches[i]);
    return true;
}

} // namespace grid
