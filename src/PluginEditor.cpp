#include "PluginEditor.h"

namespace grid {

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(kDisplayWidth, kDisplayHeight);
    setOpaque(true);

    for (int i = 0; i < kEncodersPerPage; ++i)
    {
        auto& slot = encoderSlots_[i];
        slot.nameLabel.setColour(juce::Label::textColourId, juce::Colour(kEncLabel));
        slot.nameLabel.setJustificationType(juce::Justification::centred);
        slot.nameLabel.setFont(juce::Font(15.0f));
        addAndMakeVisible(slot.nameLabel);

        slot.valueLabel.setColour(juce::Label::textColourId, juce::Colour(kEncValue));
        slot.valueLabel.setJustificationType(juce::Justification::centred);
        slot.valueLabel.setFont(juce::Font(18.0f));
        addAndMakeVisible(slot.valueLabel);
    }

    // statusLabel_ not used - pad specs painted directly in paint()
    statusLabel_.setVisible(false);

    browseFormatMgr_.registerBasicFormats();

    updateEncoderDisplay();
    startTimerHz(30);
}

PluginEditor::~PluginEditor() { stopTimer(); }

void PluginEditor::resized()
{
    const int encY = getHeight() - kEncoderBarH;
    // SSP encoders are physically on the left ~half of the module
    // 4 encoders across ~800px (200px each), with per-encoder nudge
    static constexpr int kEncZoneW = 800;
    const int encW = kEncZoneW / kEncodersPerPage;
    static constexpr int nudge[] = { -14, 0, 52, 60 };

    for (int i = 0; i < kEncodersPerPage; ++i)
    {
        int x = i * encW + nudge[i];
        encoderSlots_[i].nameLabel.setBounds(x, encY, encW, 20);
        encoderSlots_[i].valueLabel.setBounds(x, encY + 18, encW, 28);
    }

    const int w = getWidth();
    statusLabel_.setBounds(w - 300, 8, 290, kTabHeight - 16);
}

// ═══════════════════════════════════════════════════════════════════════════
// Paint
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paint(juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    g.fillAll(juce::Colour(kBg));

    // ── Tab bar ──────────────────────────────────────────────────────────
    g.setColour(juce::Colour(kTabBg));
    g.fillRect(0, 0, w, kTabHeight);

    const char* pages[] = { "PADS", "SAMPLE", "PLAY", "WARP", "FADE", "MIDI", "OPTIONS" };
    int tabW = 130;
    for (int i = 0; i < kNumPages; ++i)
    {
        int tx = 10 + i * (tabW + 6);
        bool active = (i == currentPage_);
        if (active) {
            // Active tab: red top bar, background flows into content
            g.setColour(juce::Colour(kTabActive));
            g.fillRect((float)tx, 0.0f, (float)tabW, 3.0f);  // thin red top line
            g.setColour(juce::Colour(kBg));
            g.fillRect(tx, 3, tabW, kTabHeight - 3);  // tab bg matches content
            g.setColour(juce::Colour(kTabTextActive));
        } else {
            g.setColour(juce::Colour(kTabText));
        }
        g.setFont(18.0f);
        g.drawText(pages[i], tx, 0, tabW, kTabHeight, juce::Justification::centred);
    }

    // Thin separator line under tab bar (except under active tab)
    g.setColour(juce::Colour(0xFF222222));
    g.fillRect(0, kTabHeight - 1, w, 1);

    // ── ContextBar ────────────────────────────────────────────────────────
    // Only on PADS page — detail pages show their own info
    auto& selSlot = processor_.getEngine().getSlot(selectedPad_);

    if (currentPage_ == PAGE_OVERVIEW && selSlot.isLoaded()) {
        juce::String locks;
        int mode = static_cast<int>(selSlot.getMode());
        if (mode != 0) {
            const char* m[] = { "", "LOOP", "CLK LOOP", "CLK BAR" };
            locks += juce::String(m[mode]);
        }
        if (selSlot.getVolume() < 0.99f) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "VOL " + juce::String((int)(selSlot.getVolume() * 100)) + "%";
        }
        if (selSlot.getPan() < -0.01f || selSlot.getPan() > 0.01f) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "PAN " + juce::String(selSlot.getPan() < 0 ? "L" : "R") +
                     juce::String((int)(std::abs(selSlot.getPan()) * 100));
        }
        if (selSlot.getPitchSemitones() < -0.1f || selSlot.getPitchSemitones() > 0.1f) {
            if (locks.isNotEmpty()) locks += " ";
            locks += (selSlot.getPitchSemitones() > 0 ? "+" : "") +
                     juce::String(selSlot.getPitchSemitones(), 1) + "st";
        }
        if (selSlot.getTimeStretch() < 0.99f || selSlot.getTimeStretch() > 1.01f) {
            if (locks.isNotEmpty()) locks += " ";
            locks += juce::String(selSlot.getTimeStretch(), 2) + "x";
        }
        if (selSlot.getStartPos() > 0.01f || selSlot.getEndPos() < 0.99f) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "TRIM";
        }
        if (selSlot.getFadeMs() > 0.5f) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "FADE";
        }
        if (processor_.getEngine().isMuted(selectedPad_)) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "MUTE";
        }
        if (selSlot.getChokeGroup() != ChokeGroup::None) {
            const char* grpNames[] = { "", "A", "B", "C", "D", "E", "F", "G", "H" };
            if (locks.isNotEmpty()) locks += " ";
            locks += "CHK:";
            locks += grpNames[static_cast<int>(selSlot.getChokeGroup())];
        }
        if (selSlot.isReversed()) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "REV";
        }
        if (selSlot.getMidiChannel() > 0) {
            if (locks.isNotEmpty()) locks += " ";
            locks += "MIDI";
        }

        if (locks.isNotEmpty()) {
            g.setColour(juce::Colour(kTabActive));
            g.setFont(17.0f);
            int locksRight = processor_.hasClockInput() ? (w - 140) : (w - 10);
            g.drawText(locks, 540, 8, locksRight - 540, 22, juce::Justification::centredRight);
        }
    }

    // BPM indicator (far right of top bar, pill style)
    if (processor_.hasClockInput()) {
        juce::String bpmStr = juce::String((int)processor_.getBPM()) + " BPM";
        int bpmW = 110;
        int bpmH = 24;
        int bpmX = w - bpmW - 12;
        int bpmY = (kTabHeight - bpmH) / 2;
        // Pill background
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRoundedRectangle((float)bpmX, (float)bpmY, (float)bpmW, (float)bpmH, 12.0f);
        g.setColour(juce::Colour(kTabActive).withAlpha(0.3f));
        g.drawRoundedRectangle((float)bpmX, (float)bpmY, (float)bpmW, (float)bpmH, 12.0f, 1.0f);
        // Text
        g.setColour(juce::Colour(kTabActive));
        g.setFont(18.0f);
        g.drawText(bpmStr, bpmX, bpmY, bpmW, bpmH, juce::Justification::centred);
    }

    // ── Encoder bar background ───────────────────────────────────────────
    int encY = h - kEncoderBarH;
    g.setColour(juce::Colour(kEncBarBg));
    g.fillRect(0, encY, w, kEncoderBarH);

    // ── Main content: ALWAYS show 8-pad grid ─────────────────────────────
    auto content = juce::Rectangle<int>(0, kTabHeight, w, encY - kTabHeight);

    if (browseMode_) {
        paintFileBrowser(g, content);
    } else {
        paintOverviewPage(g, content);
    }

    // ── Encoder separator lines (faint, half height) ─────────────────────
    static constexpr int kEncZoneW = 800;
    int encSlotW = kEncZoneW / kEncodersPerPage;
    g.setColour(juce::Colour(0xFF1A1A1A));
    for (int i = 1; i < kEncodersPerPage; ++i) {
        int sx = i * encSlotW;
        g.fillRect(sx, encY + kEncoderBarH / 2, 1, kEncoderBarH / 2);
    }

    // Firmware version (bottom bar, far right)
    g.setColour(juce::Colour(0xFF888888));
    g.setFont(17.0f);
    g.drawText("Firmware 0.1.8-beta", w - 280, encY, 270, kEncoderBarH,
               juce::Justification::centredRight);
}

// ═══════════════════════════════════════════════════════════════════════════
// Overview Page — 2×4 Grid of Pads
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paintOverviewPage(juce::Graphics& g, juce::Rectangle<int> area)
{
    const int cols = 4, rows = 2;
    const int padGap = 8;
    auto inner = area.reduced(12, 8);
    int padW = (inner.getWidth() - (cols - 1) * padGap) / cols;
    int padH = (inner.getHeight() - (rows - 1) * padGap) / rows;

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            int idx = r * cols + c;
            int px = inner.getX() + c * (padW + padGap);
            int py = inner.getY() + r * (padH + padGap);
            paintPadBox(g, { px, py, padW, padH }, idx);
        }
    }
}

void PluginEditor::paintPadBox(juce::Graphics& g, juce::Rectangle<int> box, int padIndex)
{
    auto& slot = processor_.getEngine().getSlot(padIndex);
    bool selected = (padIndex == selectedPad_);
    bool playing = slot.isPlaying();
    float pos = slot.getPlaybackPosition();

    // Background
    g.setColour(juce::Colour(selected ? kPadSelected : kPadBg));
    g.fillRoundedRectangle(box.toFloat(), 6.0f);

    // ── Progress fill: shaped by fade curves ────────────────────────────
    if (playing && slot.isLoaded())
    {
        float regionStart = slot.getStartPos();
        float regionEnd = slot.getEndPos();
        float regionFrac = 0.0f;
        if (regionEnd > regionStart)
            regionFrac = juce::jlimit(0.0f, 1.0f, (pos - regionStart) / (regionEnd - regionStart));

        float boxW = (float)box.getWidth();
        float boxH = (float)box.getHeight();
        float startPx = regionStart * boxW;
        float endPx = regionEnd * boxW;
        float regionPxW = endPx - startPx;
        float fillEndPx = startPx + regionFrac * regionPxW;

        // Compute fade extents in normalized region space
        float fiN = 0.0f, foN = 0.0f;
        int regionSamples = (int)((regionEnd - regionStart) * slot.getNumSamples());
        bool fiExp = (slot.getFadeInCurve() == 1);
        bool foExp = (slot.getFadeOutCurve() == 1);
        if (slot.getFadeInMs() > 0.5f && regionSamples > 0) {
            int fs = (int)(slot.getFadeInMs() * 0.001f * (float)slot.getSampleRate());
            fiN = std::min((float)fs / (float)regionSamples, 1.0f);
        }
        if (slot.getFadeOutMs() > 0.5f && regionSamples > 0) {
            int fs = (int)(slot.getFadeOutMs() * 0.001f * (float)slot.getSampleRate());
            foN = std::min((float)fs / (float)regionSamples, 1.0f);
        }

        g.saveState();
        g.reduceClipRegion(box);

        // Draw fill column by column, height follows fade envelope
        for (int px = 0; px < (int)(fillEndPx - startPx); ++px)
        {
            float normInRegion = (float)px / regionPxW;
            float env = 1.0f;
            if (fiN > 0.0f && normInRegion < fiN) {
                float t = normInRegion / fiN;
                env *= fiExp ? t * t : t;
            }
            if (foN > 0.0f && normInRegion > (1.0f - foN)) {
                float t = (1.0f - normInRegion) / foN;
                env *= foExp ? t * t : t;
            }

            float colH = env * boxH;
            float colY = (float)box.getY() + (boxH - colH);  // grow from bottom
            g.setColour(juce::Colour(0x40E53935));
            g.fillRect((float)box.getX() + startPx + px, colY, 1.0f, colH);
        }

        // Leading edge line at playhead (follows envelope height)
        float headNorm = regionFrac;
        float headEnv = 1.0f;
        if (fiN > 0.0f && headNorm < fiN) {
            float t = headNorm / fiN;
            headEnv *= fiExp ? t * t : t;
        }
        if (foN > 0.0f && headNorm > (1.0f - foN)) {
            float t = (1.0f - headNorm) / foN;
            headEnv *= foExp ? t * t : t;
        }
        float headH = headEnv * boxH;
        float headY = (float)box.getY() + (boxH - headH);
        g.setColour(juce::Colour(0xAAE53935));
        g.fillRect((float)box.getX() + fillEndPx - 1.5f, headY, 3.0f, headH);

        g.restoreState();
    }

    // ── Flash on retrigger ───────────────────────────────────────────────
    if (playing && slot.isLoaded())
    {
        float regionStart = slot.getStartPos();
        float regionEnd = slot.getEndPos();
        float regionFrac = 0.0f;
        if (regionEnd > regionStart)
            regionFrac = (pos - regionStart) / (regionEnd - regionStart);
        if (regionFrac < 0.02f) {
            g.saveState();
            g.reduceClipRegion(box);
            g.setColour(juce::Colour(0x55FFFFFF));
            g.fillRect(box);
            g.restoreState();
        }
    }

    // Border
    juce::Colour border = selected ? juce::Colour(kPadSelBorder)
                         : playing ? juce::Colour(kPadPlaying)
                         : juce::Colour(kPadBorder);
    g.setColour(border);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 6.0f, selected ? 2.5f : 1.0f);

    if (!slot.isLoaded())
    {
        // Pad number for empty pads
        g.setColour(juce::Colour(kPadNumText).withAlpha(0.25f));
        g.setFont(32.0f);
        g.drawText(juce::String(padIndex + 1), box.getX() + 6, box.getY() + 2, 36, 36,
                   juce::Justification::topLeft);
        g.setColour(juce::Colour(kPadEmpty));
        g.setFont(18.0f);
        g.drawText("- empty -", box, juce::Justification::centred);
        return;
    }

    // Waveform + fade overlay
    auto wfArea = box.reduced(4, 2);
    paintMiniWaveform(g, wfArea, slot);

    // ── Dark overlay: everything outside the envelope shape ─────────────
    {
        g.saveState();
        g.reduceClipRegion(box);

        float bx = (float)box.getX();
        float by = (float)box.getY();
        float bw = (float)box.getWidth();
        float bh = (float)box.getHeight();
        float startPx = slot.getStartPos() * bw;
        float endPx = slot.getEndPos() * bw;

        // Compute fade extents in pixels
        float regionPxW = endPx - startPx;
        int regionSamples = (int)((slot.getEndPos() - slot.getStartPos()) * slot.getNumSamples());
        bool fiExp = (slot.getFadeInCurve() == 1);
        bool foExp = (slot.getFadeOutCurve() == 1);
        float fiPx = 0.0f, foPx = 0.0f;
        if (slot.getFadeInMs() > 0.5f && regionSamples > 0) {
            int fs = (int)(slot.getFadeInMs() * 0.001f * (float)slot.getSampleRate());
            fiPx = std::min((float)fs / (float)regionSamples * regionPxW, regionPxW);
        }
        if (slot.getFadeOutMs() > 0.5f && regionSamples > 0) {
            int fs = (int)(slot.getFadeOutMs() * 0.001f * (float)slot.getSampleRate());
            foPx = std::min((float)fs / (float)regionSamples * regionPxW, regionPxW);
        }

        g.setColour(juce::Colour(0xDD0D0D0D));

        // Left dead zone: everything left of and above the fade-in curve
        {
            juce::Path p;
            p.startNewSubPath(bx, by);                    // top-left corner
            p.lineTo(bx, by + bh);                        // bottom-left
            p.lineTo(bx + startPx, by + bh);              // bottom at start position
            // Trace fade-in curve upward
            if (fiPx > 2.0f) {
                int steps = std::max(8, (int)fiPx / 2);
                for (int i = 1; i <= steps; ++i) {
                    float t = (float)i / (float)steps;
                    float c = fiExp ? t * t : t;
                    p.lineTo(bx + startPx + t * fiPx, by + bh - c * bh);
                }
            } else {
                p.lineTo(bx + startPx, by);               // straight up if no fade
            }
            p.lineTo(bx, by);                             // back to top-left
            p.closeSubPath();
            g.fillPath(p);
        }

        // Right dead zone: everything right of and above the fade-out curve
        {
            juce::Path p;
            p.startNewSubPath(bx + bw, by);               // top-right corner
            p.lineTo(bx + bw, by + bh);                   // bottom-right
            p.lineTo(bx + endPx, by + bh);                // bottom at end position
            // Trace fade-out curve upward (going right to left)
            if (foPx > 2.0f) {
                int steps = std::max(8, (int)foPx / 2);
                for (int i = 1; i <= steps; ++i) {
                    float t = (float)i / (float)steps;
                    float c = foExp ? t * t : t;
                    p.lineTo(bx + endPx - t * foPx, by + bh - c * bh);
                }
            } else {
                p.lineTo(bx + endPx, by);                 // straight up if no fade
            }
            p.lineTo(bx + bw, by);                        // back to top-right
            p.closeSubPath();
            g.fillPath(p);
        }

        g.restoreState();
    }

    // ── ALL text on top of everything ────────────────────────────────────
    // Pad number
    g.setColour(juce::Colour(playing ? kPadPlaying : kPadNumText).withAlpha(playing ? 0.5f : 0.25f));
    g.setFont(32.0f);
    g.drawText(juce::String(padIndex + 1), box.getX() + 6, box.getY() + 2, 36, 36,
               juce::Justification::topLeft);

    // Filename
    juce::String dispName = slot.getFileName();
    if (dispName.contains(".")) dispName = dispName.upToLastOccurrenceOf(".", false, false);
    g.setColour(juce::Colour(kPadText).withAlpha(0.75f));
    g.setFont(16.0f);
    g.drawText(dispName, box.getX() + 30, box.getY() + 4, box.getWidth() - 40, 20,
               juce::Justification::centredRight);

    // REC indicator (if this pad is armed/recording)
    if (processor_.getRecPad() == padIndex &&
        processor_.getRecState() != PluginProcessor::RecState::Idle) {
        bool recording = (processor_.getRecState() == PluginProcessor::RecState::Recording);
        g.setColour(juce::Colour(kTabActive).withAlpha(recording ? 0.9f : 0.5f));
        g.setFont(14.0f);
        g.drawText(recording ? "REC" : "ARM", box.getX() + 8, box.getBottom() - 20, 40, 16,
                   juce::Justification::bottomLeft);
        if (recording) {
            float prog = processor_.getRecProgress();
            g.setColour(juce::Colour(kTabActive).withAlpha(0.4f));
            g.fillRect((float)box.getX(), (float)box.getBottom() - 3.0f,
                       prog * (float)box.getWidth(), 3.0f);
        }
    }

    // ── Status symbols (bottom right, side by side) ──────────────────────
    int symX = box.getRight() - 6;  // right edge, accumulates leftward
    const int symY = box.getBottom() - 28;
    const auto symCol = juce::Colour(0xFFFFFFFF).withAlpha(0.55f);

    // Reverse indicator
    if (slot.isReversed()) {
        symX -= 28;
        g.setColour(symCol);
        g.setFont(22.0f);
        g.drawText(juce::CharPointer_UTF8("\xe2\x97\x80"), symX, symY, 26, 24,
                   juce::Justification::centred);
    }

    // Choke group indicator (triangle with letter)
    ChokeGroup grp = slot.getChokeGroup();
    if (grp != ChokeGroup::None) {
        symX -= 30;
        const char* grpNames[] = { "", "A", "B", "C", "D", "E", "F", "G", "H" };
        // Draw triangle outline
        float cx = (float)symX + 13.0f;
        float ty = (float)symY;
        float by = (float)symY + 23.0f;
        juce::Path tri;
        tri.addTriangle(cx, ty, cx - 13.0f, by, cx + 13.0f, by);
        g.setColour(symCol);
        g.strokePath(tri, juce::PathStrokeType(1.8f));
        // Letter inside
        g.setFont(14.0f);
        g.drawText(grpNames[static_cast<int>(grp)],
                   symX, symY + 6, 26, 18,
                   juce::Justification::centred);
    }

    // MUTE overlay (on top of everything)
    if (processor_.getEngine().isMuted(padIndex)) {
        g.saveState();
        g.reduceClipRegion(box);
        g.setColour(juce::Colour(0xCC0D0D0D));  // heavy dark overlay
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.restoreState();
        g.setColour(juce::Colour(kTabActive).withAlpha(0.7f));
        g.setFont(20.0f);
        g.drawText("MUTE", box, juce::Justification::centred);
    }

    // Mute mode: colored borders when shift held
    if (muteMode_) {
        bool muted = processor_.getEngine().isMuted(padIndex);
        g.setColour(juce::Colour(muted ? kTabActive : 0xFF4CAF50).withAlpha(0.7f));
        g.drawRoundedRectangle(box.toFloat().reduced(1.0f), 6.0f, 2.5f);
    }
}

void PluginEditor::paintMiniWaveform(juce::Graphics& g, juce::Rectangle<int> area, const SampleSlot& slot)
{
    if (!slot.isLoaded() || area.getWidth() <= 0) return;

    const float* data = slot.getBuffer().getReadPointer(0);
    const int total = slot.getNumSamples();
    const float cy = (float)area.getCentreY();
    const float amp = (float)area.getHeight() * 0.45f;
    const float startN = slot.getStartPos();
    const float endN = slot.getEndPos();
    const float effStretch = slot.getEffectiveStretch();

    // Time stretch tint — stronger when further from 1.0
    if (effStretch < 0.95f || effStretch > 1.05f) {
        int tsx = area.getX() + (int)(startN * area.getWidth());
        int tex = area.getX() + (int)(endN * area.getWidth());
        float tintAlpha = std::min(std::abs(effStretch - 1.0f) * 0.15f, 0.12f);
        g.setColour(juce::Colour(0xFF42A5F5).withAlpha(tintAlpha));
        g.fillRect(tsx, area.getY(), tex - tsx, area.getHeight());
    }

    // Precompute fade regions in normalized 0-1 space
    float fadeInMs = slot.getFadeInMs();
    float fadeOutMs = slot.getFadeOutMs();
    int regionSamples = (int)((endN - startN) * total);
    bool inExp = (slot.getFadeInCurve() == 1);
    bool outExp = (slot.getFadeOutCurve() == 1);

    float fadeInN = 0.0f, fadeOutN = 0.0f;
    if (fadeInMs > 0.5f && regionSamples > 0) {
        int fs = (int)(fadeInMs * 0.001f * (float)slot.getSampleRate());
        fadeInN = std::min((float)fs / (float)regionSamples, 1.0f);
    }
    if (fadeOutMs > 0.5f && regionSamples > 0) {
        int fs = (int)(fadeOutMs * 0.001f * (float)slot.getSampleRate());
        fadeOutN = std::min((float)fs / (float)regionSamples, 1.0f);
    }

    // Draw waveform with visual stretch + fade contrast reduction
    int startSample = (int)(startN * total);
    int endSample = (int)(endN * total);
    int regionLen = endSample - startSample;

    for (int px = 0; px < area.getWidth(); ++px)
    {
        float norm = (float)px / (float)area.getWidth();
        bool inside = (norm >= startN && norm <= endN);

        // Sample lookup: outside = direct, inside = stretched
        int s0, s1;
        if (inside && regionLen > 0) {
            // Map pixel position within region to sample, scaled by stretch
            float posInRegion = (norm - startN) / (endN - startN);
            float stretchedPos = posInRegion / effStretch;  // > 1 stretch = less data shown

            // How many samples per pixel at this stretch
            float samplesPerPx = (float)total / (float)area.getWidth();
            float stretchedSamplesPerPx = samplesPerPx / effStretch;

            float sampleF = (float)startSample + stretchedPos * (float)regionLen;
            s0 = juce::jlimit(0, total - 1, (int)sampleF);
            s1 = juce::jlimit(s0, total, s0 + std::max(1, (int)stretchedSamplesPerPx));

            // If stretched position is past the actual data, show as empty
            if (stretchedPos > 1.0f) inside = false;
        } else {
            s0 = (px * total) / area.getWidth();
            s1 = ((px + 1) * total) / area.getWidth();
            s1 = std::min(s1, total);
        }

        float mn = 0, mx = 0;
        for (int s = s0; s < s1; ++s) { if (data[s] < mn) mn = data[s]; if (data[s] > mx) mx = data[s]; }

        float peak = std::max(std::abs(mx), std::abs(mn));
        juce::Colour col;
        if (!inside) {
            col = juce::Colour(0xFF111111);
        } else {
            col = juce::Colour(kWfGreen);
            if (peak > 0.55f) col = col.interpolatedWith(juce::Colour(kWfYellow), (peak - 0.55f) / 0.3f);
            if (peak > 0.85f) col = col.interpolatedWith(juce::Colour(kWfRed), (peak - 0.85f) / 0.15f);

            // Fade contrast reduction
            float posInRegion = (norm - startN) / (endN - startN);
            float fadeKeep = 1.0f;
            if (fadeInN > 0.0f && posInRegion < fadeInN) {
                float t = posInRegion / fadeInN;
                fadeKeep *= (inExp ? t * t : t);
            }
            if (fadeOutN > 0.0f && posInRegion > (1.0f - fadeOutN)) {
                float t = (1.0f - posInRegion) / fadeOutN;
                fadeKeep *= (outExp ? t * t : t);
            }
            if (fadeKeep < 0.95f) {
                float dim = (1.0f - fadeKeep);
                col = col.interpolatedWith(juce::Colour(0xFF111111), dim * dim * 0.9f);
            }
        }
        g.setColour(col);

        float ty = cy - mx * amp;
        float by = cy - mn * amp;
        if (by - ty < 1) { ty = cy - 0.5f; by = cy + 0.5f; }
        g.fillRect((float)(area.getX() + px), ty, 1.0f, by - ty);
    }

    // Fade curve lines (just the line, no dark fill — waveform already dims itself)
    float regionStartPx = (float)area.getX() + startN * (float)area.getWidth();
    float regionEndPx = (float)area.getX() + endN * (float)area.getWidth();
    float regionW = regionEndPx - regionStartPx;
    float areaTop = (float)area.getY();
    float areaH = (float)area.getHeight();

    // ── Unified start/end markers ──────────────────────────────────────
    // No fade = vertical line. With fade = the line becomes a curve.
    // Same element, dual purpose.
    g.setColour(juce::Colour(kTabActive).withAlpha(0.6f));

    // Start marker / fade-in curve
    if (fadeInN > 0.01f) {
        float fadeInPx = fadeInN * regionW;
        int steps = std::max(8, (int)fadeInPx / 2);
        for (int i = 1; i <= steps; ++i) {
            float t0 = (float)(i - 1) / (float)steps;
            float t1 = (float)i / (float)steps;
            float c0 = inExp ? t0 * t0 : t0;
            float c1 = inExp ? t1 * t1 : t1;
            g.drawLine(regionStartPx + t0 * fadeInPx, areaTop + areaH - c0 * areaH,
                       regionStartPx + t1 * fadeInPx, areaTop + areaH - c1 * areaH, 1.5f);
        }
    } else {
        // No fade = simple vertical line
        g.fillRect(regionStartPx, areaTop, 1.5f, areaH);
    }

    // End marker / fade-out curve
    if (fadeOutN > 0.01f) {
        float fadeOutPx = fadeOutN * regionW;
        int steps = std::max(8, (int)fadeOutPx / 2);
        for (int i = 1; i <= steps; ++i) {
            float t0 = (float)(i - 1) / (float)steps;
            float t1 = (float)i / (float)steps;
            float c0 = outExp ? (1.0f - (1.0f - t0) * (1.0f - t0)) : t0;
            float c1 = outExp ? (1.0f - (1.0f - t1) * (1.0f - t1)) : t1;
            float x0 = regionEndPx - fadeOutPx + t0 * fadeOutPx;
            float x1 = regionEndPx - fadeOutPx + t1 * fadeOutPx;
            g.drawLine(x0, areaTop + areaH * c0,
                       x1, areaTop + areaH * c1, 1.5f);
        }
    } else {
        // No fade = simple vertical line
        g.fillRect(regionEndPx - 1.0f, areaTop, 1.5f, areaH);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Sample Page — Detail waveform for selected pad
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paintSamplePage(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);

    // Pad indicator
    g.setColour(juce::Colour(kTabActive));
    g.setFont(16.0f);
    g.drawText("PAD " + juce::String(selectedPad_ + 1), area.getX() + 12, area.getY() + 4, 100, 22,
               juce::Justification::centredLeft);

    if (!slot.isLoaded())
    {
        g.setColour(juce::Colour(kPadEmpty));
        g.setFont(20.0f);
        g.drawText("NO SAMPLE - Push FILE to browse", area, juce::Justification::centred);
        return;
    }

    g.setColour(juce::Colour(kPadText));
    g.setFont(17.0f);
    juce::String sampleName = slot.getFileName();
    if (sampleName.contains(".")) sampleName = sampleName.upToLastOccurrenceOf(".", false, false);
    g.drawText(sampleName, area.getX() + 80, area.getY() + 4, 400, 20,
               juce::Justification::centredLeft);

    auto wfArea = area.withTrimmedTop(28).reduced(12, 8);
    paintWaveformDetail(g, wfArea, slot);
}

void PluginEditor::paintWaveformDetail(juce::Graphics& g, juce::Rectangle<int> area, const SampleSlot& slot)
{
    if (!slot.isLoaded() || area.getWidth() <= 0) return;

    const float* data = slot.getBuffer().getReadPointer(0);
    const int total = slot.getNumSamples();
    const float cy = area.getCentreY();
    const float amp = area.getHeight() * 0.42f;

    // Center line
    g.setColour(juce::Colour(0xFF222222));
    g.drawHorizontalLine((int)cy, (float)area.getX(), (float)area.getRight());

    for (int px = 0; px < area.getWidth(); ++px)
    {
        int s0 = (px * total) / area.getWidth();
        int s1 = ((px + 1) * total) / area.getWidth();
        s1 = std::min(s1, total);
        float mn = 0, mx = 0;
        for (int s = s0; s < s1; ++s) { if (data[s] < mn) mn = data[s]; if (data[s] > mx) mx = data[s]; }

        float norm = (float)px / (float)area.getWidth();
        bool inside = (norm >= slot.getStartPos() && norm <= slot.getEndPos());

        float peak = std::max(std::abs(mx), std::abs(mn));
        juce::Colour col;
        if (!inside) {
            col = juce::Colour(0xFF1A1A1A);
        } else {
            col = juce::Colour(kWfGreen);
            if (peak > 0.20f) col = col.interpolatedWith(juce::Colour(kWfYellow), std::min((peak - 0.20f) / 0.35f, 1.0f));
            if (peak > 0.55f) col = col.interpolatedWith(juce::Colour(kWfRed), std::min((peak - 0.55f) / 0.30f, 1.0f));
        }
        g.setColour(col);

        float ty = cy - mx * amp;
        float by = cy - mn * amp;
        if (by - ty < 1) { ty = cy - 0.5f; by = cy + 0.5f; }
        g.fillRect((float)(area.getX() + px), ty, 1.0f, by - ty);
    }

    // ── Unified start/end markers (same as mini waveform) ──────────────
    float detailStartPx = area.getX() + slot.getStartPos() * area.getWidth();
    float detailEndPx = area.getX() + slot.getEndPos() * area.getWidth();
    float detailRegionW = detailEndPx - detailStartPx;
    float detailH = (float)area.getHeight();
    float detailTop = (float)area.getY();

    float fiMs = slot.getFadeInMs();
    float foMs = slot.getFadeOutMs();
    bool fiExp = (slot.getFadeInCurve() == 1);
    bool foExp = (slot.getFadeOutCurve() == 1);
    int detailRegionSamples = (int)((slot.getEndPos() - slot.getStartPos()) * slot.getNumSamples());

    g.setColour(juce::Colour(kTabActive).withAlpha(0.6f));

    // Start / fade-in
    float fiPx = 0.0f;
    if (fiMs > 0.5f && detailRegionSamples > 0) {
        int fs = (int)(fiMs * 0.001f * (float)slot.getSampleRate());
        fiPx = std::min((float)fs / (float)detailRegionSamples * detailRegionW, detailRegionW);
    }
    if (fiPx > 2.0f) {
        int steps = std::max(8, (int)fiPx / 2);
        for (int i = 1; i <= steps; ++i) {
            float t0 = (float)(i - 1) / (float)steps;
            float t1 = (float)i / (float)steps;
            float c0 = fiExp ? t0 * t0 : t0;
            float c1 = fiExp ? t1 * t1 : t1;
            g.drawLine(detailStartPx + t0 * fiPx, detailTop + detailH - c0 * detailH,
                       detailStartPx + t1 * fiPx, detailTop + detailH - c1 * detailH, 2.0f);
        }
    } else {
        g.fillRect(detailStartPx, detailTop, 2.0f, detailH);
    }

    // End / fade-out
    float foPx = 0.0f;
    if (foMs > 0.5f && detailRegionSamples > 0) {
        int fs = (int)(foMs * 0.001f * (float)slot.getSampleRate());
        foPx = std::min((float)fs / (float)detailRegionSamples * detailRegionW, detailRegionW);
    }
    if (foPx > 2.0f) {
        int steps = std::max(8, (int)foPx / 2);
        for (int i = 1; i <= steps; ++i) {
            float t0 = (float)(i - 1) / (float)steps;
            float t1 = (float)i / (float)steps;
            float c0 = foExp ? (1.0f - (1.0f - t0) * (1.0f - t0)) : t0;
            float c1 = foExp ? (1.0f - (1.0f - t1) * (1.0f - t1)) : t1;
            float x0 = detailEndPx - foPx + t0 * foPx;
            float x1 = detailEndPx - foPx + t1 * foPx;
            g.drawLine(x0, detailTop + detailH * c0,
                       x1, detailTop + detailH * c1, 2.0f);
        }
    } else {
        g.fillRect(detailEndPx - 2.0f, detailTop, 2.0f, detailH);
    }

    // Playhead
    if (slot.isPlaying())
    {
        float pos = slot.getPlaybackPosition();
        float px = area.getX() + pos * area.getWidth();
        g.setColour(juce::Colour(0xFFFFFFFF));
        g.fillRect(px - 0.5f, (float)area.getY(), 2.0f, (float)area.getHeight());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Play / Pitch Pages
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paintPlayPage(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);
    g.setColour(juce::Colour(kTabActive));
    g.setFont(16.0f);
    g.drawText("PAD " + juce::String(selectedPad_ + 1), area.getX() + 12, area.getY() + 4, 100, 22,
               juce::Justification::centredLeft);

    const char* modeNames[] = { "ONE-SHOT", "LOOP", "CLOCKED LOOP", "CLOCKED BAR" };
    int mode = static_cast<int>(slot.getMode());
    g.setColour(juce::Colour(kPadText));
    g.setFont(28.0f);
    g.drawText(modeNames[juce::jlimit(0, 3, mode)], area.withTrimmedTop(40), juce::Justification::centred);

    g.setFont(18.0f);
    g.drawText("Vol: " + juce::String((int)(slot.getVolume() * 100)) + "%   Pan: " +
               juce::String(slot.getPan(), 2), area.withTrimmedTop(80), juce::Justification::centred);
}

void PluginEditor::paintPitchPage(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);

    g.setColour(juce::Colour(kTabActive));
    g.setFont(16.0f);
    g.drawText("PAD " + juce::String(selectedPad_ + 1) + "  WARP",
               area.getX() + 12, area.getY() + 6, 200, 22,
               juce::Justification::centredLeft);

    auto inner = area.withTrimmedTop(34).reduced(60, 10);

    // -- PITCH display --
    auto pitchArea = inner.withHeight(inner.getHeight() / 2);
    float pitchSt = slot.getPitchSemitones();

    g.setColour(juce::Colour(kEncLabel));
    g.setFont(18.0f);
    g.drawText("PITCH", pitchArea.getX(), pitchArea.getY(), 120, 22,
               juce::Justification::centredLeft);

    auto barRect = pitchArea.withTrimmedTop(28).withHeight(26);
    g.setColour(juce::Colour(0xFF222222));
    g.fillRoundedRectangle(barRect.toFloat(), 5.0f);

    float centerX = (float)barRect.getCentreX();
    g.setColour(juce::Colour(0xFF444444));
    g.fillRect(centerX - 0.5f, (float)barRect.getY(), 1.0f, (float)barRect.getHeight());

    float pitchFrac = pitchSt / 48.0f;
    float pStart = pitchFrac < 0 ? centerX + pitchFrac * (float)barRect.getWidth() * 0.5f : centerX;
    float pEnd = pitchFrac < 0 ? centerX : centerX + pitchFrac * (float)barRect.getWidth() * 0.5f;
    g.setColour(juce::Colour(kTabActive).withAlpha(0.7f));
    g.fillRect(pStart, (float)barRect.getY() + 3, pEnd - pStart, (float)barRect.getHeight() - 6);

    g.setColour(juce::Colour(kPadText));
    g.setFont(32.0f);
    juce::String pitchStr = (pitchSt >= 0 ? "+" : "") + juce::String(pitchSt, 1) + " st";
    g.drawText(pitchStr, pitchArea.withTrimmedTop(58), juce::Justification::centred);

    // -- TIME display --
    auto timeArea = inner.withTrimmedTop(inner.getHeight() / 2);
    float timeVal = slot.getTimeStretch();

    g.setColour(juce::Colour(kEncLabel));
    g.setFont(18.0f);
    g.drawText("TIME", timeArea.getX(), timeArea.getY(), 120, 22,
               juce::Justification::centredLeft);

    auto timeBar = timeArea.withTrimmedTop(28).withHeight(26);
    g.setColour(juce::Colour(0xFF222222));
    g.fillRoundedRectangle(timeBar.toFloat(), 5.0f);

    float timeCX = (float)timeBar.getCentreX();
    g.setColour(juce::Colour(0xFF444444));
    g.fillRect(timeCX - 0.5f, (float)timeBar.getY(), 1.0f, (float)timeBar.getHeight());

    float timeFrac = juce::jlimit(-1.0f, 1.0f, std::log2(timeVal) / 2.0f);
    float tStart = timeFrac < 0 ? timeCX + timeFrac * (float)timeBar.getWidth() * 0.5f : timeCX;
    float tEnd = timeFrac < 0 ? timeCX : timeCX + timeFrac * (float)timeBar.getWidth() * 0.5f;
    g.setColour(juce::Colour(0xFF42A5F5).withAlpha(0.7f));
    g.fillRect(tStart, (float)timeBar.getY() + 3, tEnd - tStart, (float)timeBar.getHeight() - 6);

    g.setColour(juce::Colour(kPadText));
    g.setFont(32.0f);
    g.drawText(juce::String(timeVal, 2) + "x", timeArea.withTrimmedTop(58), juce::Justification::centred);
}

// ═══════════════════════════════════════════════════════════════════════════
// File Browser (from ELAS, adapted)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paintFileBrowser(juce::Graphics& g, juce::Rectangle<int> area)
{
    const int browserW = (int)(area.getWidth() * 0.30f);

    // Left side: browser panel
    auto panel = area.withWidth(browserW);
    g.setColour(juce::Colour(kBrowseBg));
    g.fillRect(panel);

    // Right side: pad grid
    auto rightArea = area.withTrimmedLeft(browserW);
    paintOverviewPage(g, rightArea);

    // Separator line
    g.setColour(juce::Colour(0xFF333333));
    g.fillRect(panel.getRight() - 1, panel.getY(), 1, panel.getHeight());

    const int fileCount = browseItems_.size();
    auto content = panel.reduced(12, 8);

    // Header: folder name + version
    g.setColour(juce::Colour(kTabActive));
    g.setFont(22.0f);
    juce::String header = browseCurrentDir_.getFileName().isEmpty() ? "Smart Home" : browseCurrentDir_.getFileName();
    g.drawText(header, content.removeFromTop(30), juce::Justification::centredLeft);

    // Pad indicator
    auto subLine = content.removeFromTop(20);
    g.setColour(juce::Colour(kEncLabel));
    g.setFont(14.0f);
    g.drawText("-> PAD " + juce::String(selectedPad_ + 1), subLine, juce::Justification::centredLeft);
    content.removeFromTop(4);

    if (fileCount == 0) {
        g.setColour(juce::Colour(kPadEmpty));
        g.setFont(16.0f);
        g.drawText("No items found", content, juce::Justification::centred);
        return;
    }

    const int rowH = 38;
    const int visibleRows = std::max(1, content.getHeight() / rowH);
    if (browseIndex_ < browseScrollOffset_) browseScrollOffset_ = browseIndex_;
    if (browseIndex_ >= browseScrollOffset_ + visibleRows) browseScrollOffset_ = browseIndex_ - visibleRows + 1;
    browseScrollOffset_ = juce::jlimit(0, std::max(0, fileCount - visibleRows), browseScrollOffset_);

    for (int i = 0; i < visibleRows; ++i)
    {
        int idx = browseScrollOffset_ + i;
        if (idx >= fileCount) break;
        auto row = juce::Rectangle<int>(content.getX(), content.getY() + i * rowH, content.getWidth(), rowH);
        bool sel = (idx == browseIndex_);
        bool isDir = browseItems_[idx].isDirectory();
        if (sel) {
            g.setColour(juce::Colour(kBrowseSelBg));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
        }

        // File/folder name
        g.setColour(sel ? juce::Colour(0xFFFFFFFF) : juce::Colour(isDir ? kBrowseFolder : kBrowseText));
        g.setFont(18.0f);
        juce::String displayName = browseItemNames_[idx];
        if (!isDir && displayName.contains("."))
            displayName = displayName.upToLastOccurrenceOf(".", false, false);

        int nameW = row.getWidth() - 120;  // more room for specs
        g.drawText(displayName, row.getX() + 8, row.getY(), nameW, rowH, juce::Justification::centredLeft);

        // File specs (right-aligned): "1.2s 48k St"
        if (!isDir && idx < browseItemDurations_.size() && browseItemDurations_[idx].isNotEmpty()) {
            juce::String info = browseItemDurations_[idx];
            g.setColour(sel ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF999999));
            g.setFont(17.0f);
            g.drawText(info, row.getX(), row.getY(), row.getWidth() - 8, rowH,
                       juce::Justification::centredRight);
        }
    }
}

void PluginEditor::enterBrowseMode()
{
    browseMode_ = true; browseIndex_ = 0; browseScrollOffset_ = 0;
    // Only set initial dir if we haven't browsed before
    if (!browseCurrentDir_.isDirectory())
    {
        browseCurrentDir_ = juce::File(processor_.getSampleRootPath());
        if (!browseCurrentDir_.isDirectory()) browseCurrentDir_ = juce::File("/");
    }
    browseScanCurrentDir();
    encoderSlots_[0].nameLabel.setText("CLOSE", juce::dontSendNotification);
    encoderSlots_[0].valueLabel.setText("[PUSH]", juce::dontSendNotification);
    encoderSlots_[1].nameLabel.setText("FILES", juce::dontSendNotification);
    encoderSlots_[1].valueLabel.setText("Load", juce::dontSendNotification);
    encoderSlots_[2].nameLabel.setText("PAD", juce::dontSendNotification);
    encoderSlots_[2].valueLabel.setText("Back", juce::dontSendNotification);
    encoderSlots_[3].nameLabel.setText("PLAY", juce::dontSendNotification);
    encoderSlots_[3].valueLabel.setText("[PUSH]", juce::dontSendNotification);
    repaint();
}

void PluginEditor::exitBrowseMode() { browseMode_ = false; updateEncoderDisplay(); repaint(); }

void PluginEditor::browseScanCurrentDir()
{
    browseItems_.clear(); browseItemNames_.clear(); browseItemDurations_.clear();
    if (!browseCurrentDir_.isDirectory()) return;
    auto dirs = browseCurrentDir_.findChildFiles(juce::File::findDirectories, false); dirs.sort();
    for (auto& d : dirs) {
        browseItems_.add(d);
        browseItemNames_.add("[" + d.getFileName() + "]");
        browseItemDurations_.add("");
    }
    auto files = browseCurrentDir_.findChildFiles(juce::File::findFiles, false, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF"); files.sort();
    for (auto& f : files) {
        browseItems_.add(f);
        browseItemNames_.add(f.getFileName());
        // Get properties from file header
        juce::String info;
        if (auto* reader = browseFormatMgr_.createReaderFor(f)) {
            // Duration
            double secs = (double)reader->lengthInSamples / reader->sampleRate;
            if (secs < 1.0)
                info = juce::String((int)(secs * 1000)) + "ms";
            else if (secs < 60.0)
                info = juce::String(secs, 1) + "s";
            else
                info = juce::String((int)(secs / 60)) + ":" + juce::String((int)secs % 60).paddedLeft('0', 2);
            // Sample rate (compact)
            int sr = (int)reader->sampleRate;
            if (sr >= 1000) info += "   " + juce::String(sr / 1000) + "k";
            // Channels
            info += "   " + juce::String(reader->numChannels > 1 ? "Stereo" : "Mono");
            delete reader;
        }
        browseItemDurations_.add(info);
    }
    browseIndex_ = 0; browseScrollOffset_ = 0;
}

void PluginEditor::browseSelect()
{
    if (browseIndex_ < 0 || browseIndex_ >= browseItems_.size()) return;
    auto sel = browseItems_[browseIndex_];

    // Special "Clear Pad" action (smart home item with empty path)
    if (browseItemNames_[browseIndex_] == ">> Clear Pad") {
        processor_.getEngine().getSlot(selectedPad_).clear();
        repaint();
        return;
    }

    if (sel.isDirectory()) { browseCurrentDir_ = sel; browseScanCurrentDir(); repaint(); }
    else {
        // Load into currently selected pad — browser stays open
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        slot.loadFile(sel);
        repaint();
    }
}

void PluginEditor::browseGoUp()
{
    auto p = browseCurrentDir_.getParentDirectory();
    if (p.isDirectory() && p != browseCurrentDir_) { browseCurrentDir_ = p; browseScanCurrentDir(); repaint(); }
}

void PluginEditor::browseGoHome()
{
    // Smart Home: virtual root with quick access folders + actions
    browseItems_.clear();
    browseItemNames_.clear();
    browseItemDurations_.clear();

    // Main samples folder
    juce::File samplesDir(processor_.getSampleRootPath());
    if (samplesDir.isDirectory()) {
        browseItems_.add(samplesDir);
        browseItemNames_.add("[Samples]");
        browseItemDurations_.add("");
    }

    // Kits folder (create if missing)
    juce::File kitsDir = samplesDir.getChildFile("kits");
    if (!kitsDir.isDirectory()) kitsDir.createDirectory();
    if (kitsDir.isDirectory()) {
        browseItems_.add(kitsDir);
        browseItemNames_.add("[Kits]");
        browseItemDurations_.add("");
    }

    // Recordings folder (create if missing)
    juce::File recDir = samplesDir.getChildFile("recordings");
    if (!recDir.isDirectory()) recDir.createDirectory();
    if (recDir.isDirectory()) {
        browseItems_.add(recDir);
        browseItemNames_.add("[Recordings]");
        browseItemDurations_.add("");
    }

    // Clear Pad action (special item — empty file)
    browseItems_.add(juce::File());
    browseItemNames_.add(">> Clear Pad");
    browseItemDurations_.add("");

    browseCurrentDir_ = juce::File();  // no real dir — we're in smart home
    browseIndex_ = 0;
    browseScrollOffset_ = 0;
    repaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// Page System
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::switchPage(int page) {
    if (browseMode_) return;
    currentPage_ = juce::jlimit(0, kNumPages - 1, page);
    updateEncoderDisplay(); repaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// Encoder Display
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::updateEncoderDisplay()
{
    // Keep processor's rec target in sync with UI selection
    processor_.setRecTargetPad(selectedPad_);

    for (int i = 0; i < kEncodersPerPage; ++i)
    {
        encoderSlots_[i].nameLabel.setText(getEncoderLabel(currentPage_, i), juce::dontSendNotification);
        encoderSlots_[i].valueLabel.setText(getEncoderValue(currentPage_, i), juce::dontSendNotification);
    }
}

juce::String PluginEditor::getEncoderLabel(int page, int enc) const
{
    if (enc == 0) return "FILE";  // push always opens browser

    switch (page) {
        case PAGE_OVERVIEW: {
            const char* l[] = { "", "REC", "MODE", "LENGTH" };
            return l[enc];
        }
        case PAGE_SAMPLE: {
            const char* l[] = { "", "START", "END", "---" };
            return l[enc];
        }
        case PAGE_PLAY: {
            const char* l[] = { "", "MODE", "VOL", "PAN" };
            return l[enc];
        }
        case PAGE_PITCH: {
            const char* l[] = { "", "PITCH", "TIME", "CLK DIV" };
            return l[enc];
        }
        case PAGE_FADE: {
            const char* l[] = { "", "FADE IN", "FADE OUT", "---" };
            return l[enc];
        }
        case PAGE_MIDI: {
            const char* l[] = { "", "CHANNEL", "DEVICE", "CLK" };
            return l[enc];
        }
        case PAGE_OPTIONS: {
            const char* l[] = { "", "CHOKE", "REVERSE", "ENHANCE" };
            return l[enc];
        }
    }
    return "";
}

juce::String PluginEditor::getEncoderValue(int page, int enc) const
{
    auto& slot = const_cast<GridEngine&>(processor_.getEngine()).getSlot(selectedPad_);

    if (enc == 0) {
        juce::String v = juce::String(selectedPad_ + 1) + ": ";
        if (slot.isLoaded()) {
            juce::String fn = slot.getFileName();
            if (fn.contains(".")) fn = fn.upToLastOccurrenceOf(".", false, false);
            return v + fn;
        }
        return v + "[PUSH]";
    }

    switch (page) {
        case PAGE_OVERVIEW: {
            if (enc == 1) {
                auto rs = processor_.getRecState();
                if (rs == PluginProcessor::RecState::Idle) return "[PUSH]";
                if (rs == PluginProcessor::RecState::Armed) return "ARMED";
                return "REC " + juce::String((int)(processor_.getRecProgress() * 100)) + "%";
            }
            if (enc == 2) {
                const char* rm[] = { "INSTANT", "THRESH", "NEXT BAR" };
                return rm[static_cast<int>(processor_.getRecMode())];
            }
            if (enc == 3) {
                float secs = kRecLengths[processor_.getRecMaxLenIdx()];
                return (secs < 60.0f) ? juce::String((int)secs) + "s" : juce::String((int)(secs / 60)) + "min";
            }
            return "---";
        }
        case PAGE_SAMPLE: {
            if (enc == 1) return juce::String(slot.getStartPos() * 100.0f, 1) + "%";
            if (enc == 2) return juce::String(slot.getEndPos() * 100.0f, 1) + "%";
            return "---";
        }
        case PAGE_PLAY: {
            const char* modes[] = { "ONE-SHOT", "LOOP", "CLK LOOP", "CLK BAR" };
            if (enc == 1) return modes[static_cast<int>(slot.getMode())];
            if (enc == 2) {
                if (processor_.getEngine().isMuted(selectedPad_))
                    return "MUTE";
                return juce::String((int)(slot.getVolume() * 100)) + "%";
            }
            if (enc == 3) return juce::String(slot.getPan(), 2);
            return "---";
        }
        case PAGE_PITCH: {
            float st = slot.getPitchSemitones();
            if (enc == 1) return (st >= 0 ? "+" : "") + juce::String(st, 1) + "st";
            if (enc == 2) {
                bool clocked = (slot.getMode() == PadMode::ClockedLoop || slot.getMode() == PadMode::ClockedBar);
                if (clocked && processor_.hasClockInput()) {
                    float eff = slot.getEffectiveStretch();
                    return "CLK " + juce::String(eff, 2) + "x";
                }
                return juce::String(slot.getTimeStretch(), 2) + "x";
            }
            if (enc == 3) return "/" + juce::String(processor_.getClockDiv());
            return "---";
        }
        case PAGE_FADE: {
            auto fmtFade = [](float ms, int curve) -> juce::String {
                if (ms < 0.5f) return "OFF";
                juce::String v;
                if (ms < 1000.0f)
                    v = juce::String((int)ms) + "ms";
                else
                    v = juce::String(ms / 1000.0f, 1) + "s";
                v += (curve == 0) ? " LIN" : " EXP";
                return v;
            };
            if (enc == 1) return fmtFade(slot.getFadeInMs(), slot.getFadeInCurve());
            if (enc == 2) return fmtFade(slot.getFadeOutMs(), slot.getFadeOutCurve());
            return "---";
        }
        case PAGE_MIDI: {
            if (enc == 1) {
                int ch = slot.getMidiChannel();
                if (ch == 0) return "OFF";
                if (ch == 17) return "OMNI";
                return "CH " + juce::String(ch);
            }
            if (enc == 2) {
                auto name = processor_.getMidiDeviceName();
                return name.isEmpty() ? "None" : name;
            }
            if (enc == 3) {
                if (processor_.getMidiDeviceName().isEmpty())
                    return "---";
                return processor_.isMidiClockEnabled() ? "ON" : "OFF";
            }
            return "---";
        }
        case PAGE_OPTIONS: {
            if (enc == 1) {
                const char* grps[] = { "NONE", "A", "B", "C", "D", "E", "F", "G", "H" };
                return grps[static_cast<int>(slot.getChokeGroup())];
            }
            if (enc == 2) return slot.isReversed() ? "ON" : "OFF";
            if (enc == 3) return "[PUSH]";
            return "---";
        }
    }
    return "";
}

// ═══════════════════════════════════════════════════════════════════════════
// Timer
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::timerCallback()
{
    if (browseMode_) { repaint(); return; }
    updateEncoderDisplay();
    repaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// SSP Button Handlers
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::onButton(int n, bool val)
{
    if (!val || n < 0 || n >= kNumPads) return;
    auto& slot = processor_.getEngine().getSlot(n);

    // Mute mode: right shift held, buttons toggle mutes
    if (muteMode_) {
        processor_.getEngine().toggleMute(n);
        muteToggled_ = true;
        repaint();
        return;
    }

    if (browseMode_) {
        // Browse mode: load highlighted file to this pad + trigger
        if (browseIndex_ >= 0 && browseIndex_ < browseItems_.size()) {
            auto sel = browseItems_[browseIndex_];
            if (!sel.isDirectory() && browseItemNames_[browseIndex_] != ">> Clear Pad") {
                slot.loadFile(sel);
                processor_.getEngine().trigger(n);
            }
        }
        selectedPad_ = n;
        repaint();
        return;
    }

    // Normal mode: loop toggle or retrigger
    bool isLoop = (slot.getMode() == PadMode::Loop || slot.getMode() == PadMode::ClockedLoop);
    if (isLoop && slot.isPlaying() && !slot.isStopping()) {
        slot.stop();
    } else {
        processor_.getEngine().trigger(n);
    }
    selectedPad_ = n;
    repaint();
}

void PluginEditor::onLeftButton(bool val)
{
    if (!val) return;
    if (browseMode_) return;
    switchPage(currentPage_ - 1);
}

void PluginEditor::onRightButton(bool val)
{
    if (!val) return;
    if (browseMode_) return;
    switchPage(currentPage_ + 1);
}

void PluginEditor::onUpButton(bool val)
{
    if (!val) return;
    if (browseMode_) { browseIndex_ = std::max(0, browseIndex_ - 1); repaint(); return; }
    if (selectedPad_ >= 4) { selectedPad_ -= 4; repaint(); }
}

void PluginEditor::onDownButton(bool val)
{
    if (!val) return;
    if (browseMode_) { browseIndex_ = std::min(std::max(0, browseItems_.size() - 1), browseIndex_ + 1); repaint(); return; }
    if (selectedPad_ < 4) { selectedPad_ += 4; repaint(); }
}

void PluginEditor::onLeftShiftButton(bool val)
{
    if (!val) return;
    switchPage(currentPage_ - 1);
}

void PluginEditor::onRightShiftButton(bool val)
{
    // Pure mute mode — hold to enter, release to exit. No page switching.
    if (val) {
        muteMode_ = true;
        repaint();
    } else {
        muteMode_ = false;
        repaint();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Encoder Handlers
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::onEncoder(int n, float delta)
{
    if (browseMode_) {
        // Enc 0 turn: select pads (same as normal)
        if (n == 0) {
            selectedPad_ = juce::jlimit(0, kNumPads - 1, selectedPad_ + (delta > 0 ? 1 : -1));
            repaint();
        }
        // Enc 1 turn: browse files
        if (n == 1) {
            int fc = browseItems_.size();
            if (fc > 0) {
                browseIndex_ = delta > 0 ? std::min(fc - 1, browseIndex_ + 1) : std::max(0, browseIndex_ - 1);
                repaint();
            }
        }
        // Enc 2 turn: also navigate pads (push = back)
        if (n == 2) {
            selectedPad_ = juce::jlimit(0, kNumPads - 1, selectedPad_ + (delta > 0 ? 1 : -1));
            repaint();
        }
        return;
    }

    auto& slot = processor_.getEngine().getSlot(selectedPad_);

    // Encoder 0 always navigates pads
    if (n == 0) {
        selectedPad_ = juce::jlimit(0, kNumPads - 1, selectedPad_ + (delta > 0 ? 1 : -1));
        repaint();
        return;
    }

    switch (currentPage_)
    {
        case PAGE_OVERVIEW:
            if (n == 2) {
                int m = static_cast<int>(processor_.getRecMode()) + (delta > 0 ? 1 : -1);
                processor_.setRecMode(static_cast<RecMode>(juce::jlimit(0, 2, m)));
            }
            if (n == 3) {
                int idx = processor_.getRecMaxLenIdx() + (delta > 0 ? 1 : -1);
                processor_.setRecMaxLenIdx(idx);
            }
            break;

        case PAGE_SAMPLE:
            if (n == 1) slot.setStartPos(slot.getStartPos() + delta * 0.015f);
            if (n == 2) slot.setEndPos(slot.getEndPos() + delta * 0.015f);
            break;

        case PAGE_PLAY:
            if (n == 1) {
                int m = static_cast<int>(slot.getMode()) + (delta > 0 ? 1 : -1);
                slot.setMode(static_cast<PadMode>(juce::jlimit(0, 3, m)));
                slot.setTimeStretch(1.0f);  // reset stretch on mode change
            }
            if (n == 2) slot.setVolume(juce::jlimit(0.0f, 1.0f, slot.getVolume() + delta * 0.02f));
            if (n == 3) slot.setPan(juce::jlimit(-1.0f, 1.0f, slot.getPan() + delta * 0.05f));
            break;

        case PAGE_PITCH:
            if (n == 1) slot.setPitchSemitones(slot.getPitchSemitones() + delta * 1.0f);
            if (n == 2) slot.setTimeStretch(slot.getTimeStretch() + delta * 0.05f);
            if (n == 3) {
                static constexpr int divs[] = { 1, 2, 4, 8 };
                int cur = processor_.getClockDiv();
                int idx = 0;
                for (int i = 0; i < 4; ++i) { if (divs[i] == cur) idx = i; }
                idx = juce::jlimit(0, 3, idx + (delta > 0 ? 1 : -1));
                processor_.setClockDiv(divs[idx]);
            }
            break;

        case PAGE_FADE: {
            auto fadeStep = [](float ms) -> float {
                if (ms > 2000.0f) return 200.0f;
                if (ms > 500.0f) return 100.0f;
                return 40.0f;
            };
            if (n == 1) {
                slot.setFadeInMs(slot.getFadeInMs() + delta * fadeStep(slot.getFadeInMs()));
            }
            if (n == 2) {
                slot.setFadeOutMs(slot.getFadeOutMs() - delta * fadeStep(slot.getFadeOutMs()));
            }
            break;
        }

        case PAGE_MIDI:
            if (n == 1) {
                int ch = slot.getMidiChannel() + (delta > 0 ? 1 : -1);
                slot.setMidiChannel(juce::jlimit(0, 17, ch));
            }
            if (n == 2) {
                auto devs = processor_.getMidiDeviceNames();
                if (devs.size() > 0) {
                    auto curName = processor_.getMidiDeviceName();
                    int cur = curName.isEmpty() ? 0 : devs.indexOf(curName);
                    if (cur < 0) cur = 0;
                    int next = juce::jlimit(0, devs.size() - 1, cur + (delta > 0 ? 1 : -1));
                    processor_.setMidiDevice(devs[next]);
                }
            }
            if (n == 3 && processor_.getMidiDeviceName().isNotEmpty()) {
                processor_.setMidiClockEnabled(delta > 0);  // turn right = ON, left = OFF
            }
            break;

        case PAGE_OPTIONS:
            if (n == 1) {
                int g = static_cast<int>(slot.getChokeGroup()) + (delta > 0 ? 1 : -1);
                slot.setChokeGroup(static_cast<ChokeGroup>(juce::jlimit(0, 8, g)));
            }
            // enc 2 (reverse) is push-only toggle
            break;
    }
}

void PluginEditor::onEncoderSwitch(int n, bool val)
{
    if (!val) return;

    if (browseMode_) {
        if (n == 0) { exitBrowseMode(); return; }  // close
        if (n == 1) browseSelect();                  // load file / enter dir
        if (n == 2) browseGoUp();                    // back up
        if (n == 3) {                                // audition: load + play
            if (browseIndex_ >= 0 && browseIndex_ < browseItems_.size()) {
                auto sel = browseItems_[browseIndex_];
                if (!sel.isDirectory() && browseItemNames_[browseIndex_] != ">> Clear Pad") {
                    processor_.getEngine().getSlot(selectedPad_).loadFile(sel);
                    processor_.getEngine().trigger(selectedPad_);
                    repaint();
                }
            }
        }
        return;
    }

    // Push encoder 0 on ANY page = toggle browser
    if (n == 0) {
        enterBrowseMode();
        return;
    }

    // Push encoder 1-3 = reset that parameter to default
    auto& slot = processor_.getEngine().getSlot(selectedPad_);
    switch (currentPage_)
    {
        case PAGE_OVERVIEW:
            if (n == 1) {
                // Toggle arm/stop
                auto rs = processor_.getRecState();
                if (rs == PluginProcessor::RecState::Idle)
                    processor_.armRecord(selectedPad_);
                else
                    processor_.stopRecord();
            }
            break;
        case PAGE_SAMPLE:
            if (n == 1) slot.setStartPos(0.0f);
            if (n == 2) slot.setEndPos(1.0f);
            break;
        case PAGE_PLAY:
            if (n == 1) { slot.setMode(PadMode::OneShot); slot.setTimeStretch(1.0f); }
            if (n == 2) processor_.getEngine().toggleMute(selectedPad_);
            if (n == 3) slot.setPan(0.0f);
            break;
        case PAGE_PITCH:
            if (n == 1) slot.setPitchSemitones(0.0f);
            if (n == 2) slot.setTimeStretch(1.0f);
            if (n == 3) processor_.setClockDiv(1);
            break;
        case PAGE_FADE:
            if (n == 1) slot.setFadeInCurve(slot.getFadeInCurve() == 0 ? 1 : 0);
            if (n == 2) slot.setFadeOutCurve(slot.getFadeOutCurve() == 0 ? 1 : 0);
            break;
        case PAGE_MIDI:
            if (n == 1) slot.setMidiChannel(0);  // push = OFF
            if (n == 2) processor_.closeMidiDevice();  // push = disconnect
            if (n == 3 && processor_.getMidiDeviceName().isNotEmpty())
                processor_.setMidiClockEnabled(!processor_.isMidiClockEnabled());
            break;
        case PAGE_OPTIONS:
            if (n == 1) slot.setChokeGroup(ChokeGroup::None);
            if (n == 2) slot.setReversed(!slot.isReversed());
            if (n == 3) slot.normalize();  // one-shot enhance
            break;
        default:
            break;
    }
    repaint();
}

} // namespace grid
