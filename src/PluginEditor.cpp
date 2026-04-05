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
    static constexpr int nudge[] = { -14, 0, 52, 86 };

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

    const char* pages[] = { "PADS", "SAMPLE", "PLAY", "WARP", "FADE", "FILTER", "MIDI", "OPTIONS" };
    int tabW = 110;
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
            const char* m[] = { "", "LOOP", "CLK:LOOP", "CLK:1SHOT" };
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
            bool hasBPMPill = processor_.hasClockInput() || processor_.isMidiClockEnabled();
            int locksRight = hasBPMPill ? (w - 150) : (w - 10);
            g.drawText(locks, 540, 8, locksRight - 540, 22, juce::Justification::centredRight);
        }
    }

    // BPM indicator (far right of top bar, pill style)
    bool showBPM = processor_.hasClockInput() || processor_.isMidiClockEnabled();
    if (showBPM) {
        juce::String bpmStr;
        if (processor_.isMidiClockEnabled()) {
            if (processor_.getBPM() > 1.0f)
                bpmStr = "MIDI " + juce::String((int)processor_.getBPM());
            else
                bpmStr = "MIDI --";
        } else {
            bpmStr = juce::String((int)processor_.getBPM()) + " BPM";
        }
        int bpmW = 120;
        int bpmH = 24;
        int bpmX = w - bpmW - 12;
        int bpmY = (kTabHeight - bpmH) / 2;
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRoundedRectangle((float)bpmX, (float)bpmY, (float)bpmW, (float)bpmH, 12.0f);
        g.setColour(juce::Colour(kTabActive).withAlpha(0.3f));
        g.drawRoundedRectangle((float)bpmX, (float)bpmY, (float)bpmW, (float)bpmH, 12.0f, 1.0f);
        g.setColour(juce::Colour(kTabActive));
        g.setFont(18.0f);
        g.drawText(bpmStr, bpmX, bpmY, bpmW, bpmH, juce::Justification::centred);
    }

    // ── Ticker message (types in from right, stops before tabs) ────────
    auto ticker = processor_.getTickerMessage();
    if (ticker.isNotEmpty()) {
        float progress = processor_.getTickerProgress();

        // Safe area: after last tab, before BPM pill
        int areaLeft = 560;   // well clear of OPTIONS tab
        int areaRight = w - 145;  // before BPM pill
        int areaW = areaRight - areaLeft;
        int textW = std::min(areaW, 320);

        // Phase 1 (0-40%): slide in from right to resting position
        // Phase 2 (40-75%): hold at resting position
        // Phase 3 (75-100%): fade out
        int restX = areaRight - textW;  // resting position (right-aligned in area)
        int startX = areaRight + 20;     // just off-screen right
        int tickerX;

        if (progress < 0.4f) {
            float slide = progress / 0.4f;
            // Ease-out curve for smooth deceleration
            slide = 1.0f - (1.0f - slide) * (1.0f - slide);
            tickerX = startX + (int)(slide * (float)(restX - startX));
        } else {
            tickerX = restX;
        }

        float alpha;
        if (progress < 0.05f)
            alpha = progress / 0.05f;  // quick fade in
        else if (progress < 0.75f)
            alpha = 0.9f;
        else
            alpha = 0.9f * (1.0f - (progress - 0.75f) / 0.25f);

        g.saveState();
        g.reduceClipRegion(areaLeft, 0, areaW, kTabHeight);
        g.setColour(juce::Colour(0xFFCCCCCC).withAlpha(alpha));
        g.setFont(17.0f);
        g.drawText(ticker, tickerX, 4, textW, kTabHeight - 8,
                   juce::Justification::centredRight);
        g.restoreState();
    }

    // ── Encoder bar background ───────────────────────────────────────────
    int encY = h - kEncoderBarH;
    g.setColour(juce::Colour(kEncBarBg));
    g.fillRect(0, encY, w, kEncoderBarH);

    // ── Main content: ALWAYS show 8-pad grid ─────────────────────────────
    auto content = juce::Rectangle<int>(0, kTabHeight, w, encY - kTabHeight);

    if (browseMode_) {
        paintFileBrowser(g, content);
    } else if (configMode_) {
        // Pads on left, config panel on right
        int configW = (int)(content.getWidth() * 0.32f);
        auto leftArea = content.withTrimmedRight(configW);
        auto rightArea = content.withTrimmedLeft(content.getWidth() - configW);
        paintOverviewPage(g, leftArea);
        paintConfigBrowser(g, rightArea);
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

    // ── Preset name (center of encoder bar, right half) ────────────────
    {
        juce::String kitName = processor_.getCurrentKitName();
        g.setColour(juce::Colour(0xFFAAAAAA));
        g.setFont(18.0f);
        g.drawText(kitName, 850, encY, 500, kEncoderBarH, juce::Justification::centred);
    }

    // Version (bottom-right corner, small)
    g.setColour(juce::Colour(0xFF555555));
    g.setFont(14.0f);
    g.drawText("0.1.9.1", w - 100, encY, 90, kEncoderBarH,
               juce::Justification::centredRight);

    // ── Popup overlay (renders on top of everything) ─────────────────
    if (popupMode_) {
        auto fullArea = juce::Rectangle<int>(0, 0, w, h);
        paintPopup(g, fullArea);
    }

    // ── Keyboard overlay ─────────────────────────────────────────────
    if (keyboardMode_) {
        auto fullArea = juce::Rectangle<int>(0, 0, w, h);
        paintKeyboard(g, fullArea);
    }

    // ── Slice editor overlay ─────────────────────────────────────────
    if (sliceEditorMode_) {
        auto fullArea = juce::Rectangle<int>(0, 0, w, h);
        paintSliceEditor(g, fullArea);
    }
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

    // MUTE overlay and queued mute visuals
    bool isMuted = processor_.getEngine().isMuted(padIndex);
    bool isPending = (muteMode_ && pendingMute_[padIndex])
                   || processor_.getPendingBarMute(padIndex);  // bar-queued persists after shift release

    if (isMuted && !isPending) {
        // Currently muted, staying muted
        g.saveState();
        g.reduceClipRegion(box);
        g.setColour(juce::Colour(0xCC0D0D0D));
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.restoreState();
        g.setColour(juce::Colour(kTabActive).withAlpha(0.7f));
        g.setFont(20.0f);
        g.drawText("MUTE", box, juce::Justification::centred);
    }
    else if (isMuted && isPending) {
        // Currently muted, queued to UNMUTE
        g.saveState();
        g.reduceClipRegion(box);
        g.setColour(juce::Colour(0x660D0D0D));  // lighter overlay
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.restoreState();
        g.setColour(juce::Colour(0xFF4CAF50).withAlpha(0.8f));  // green
        g.setFont(16.0f);
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb6 UNMUTE"), box, juce::Justification::centred);
    }
    else if (!isMuted && isPending) {
        // Currently playing, queued to MUTE
        g.saveState();
        g.reduceClipRegion(box);
        g.setColour(juce::Colour(0x880D0D0D));  // medium overlay
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.restoreState();
        g.setColour(juce::Colour(kTabActive).withAlpha(0.8f));  // red
        g.setFont(16.0f);
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xba MUTE"), box, juce::Justification::centred);
    }

    // Mute mode borders OR bar-pending indicators
    bool showBorder = muteMode_ || processor_.getPendingBarMute(padIndex);
    if (showBorder) {
        juce::Colour borderCol;
        if (isPending)
            borderCol = juce::Colour(0xFFFFAB40);  // orange = pending change
        else if (isMuted)
            borderCol = juce::Colour(kTabActive);   // red = muted
        else
            borderCol = juce::Colour(0xFF4CAF50);   // green = playing

        g.setColour(borderCol.withAlpha(0.8f));
        g.drawRoundedRectangle(box.toFloat().reduced(1.0f), 6.0f, isPending ? 3.0f : 2.5f);
    }

    // Solo mode visuals
    if (soloMode_) {
        if (soloActive_[padIndex]) {
            // Soloed pad: bright yellow border + SOLO text
            g.setColour(juce::Colour(0xFFFFD600).withAlpha(0.9f));
            g.drawRoundedRectangle(box.toFloat().reduced(1.0f), 6.0f, 3.0f);
            g.setFont(14.0f);
            g.drawText("SOLO", box.getX() + 4, box.getBottom() - 20, 50, 16,
                       juce::Justification::bottomLeft);
        } else {
            // Not soloed: dim overlay
            bool anySoloed = false;
            for (int i = 0; i < kNumPads; ++i) if (soloActive_[i]) { anySoloed = true; break; }
            if (anySoloed) {
                g.saveState();
                g.reduceClipRegion(box);
                g.setColour(juce::Colour(0xBB0D0D0D));
                g.fillRoundedRectangle(box.toFloat(), 6.0f);
                g.restoreState();
                g.setColour(juce::Colour(0xFF888888).withAlpha(0.6f));
                g.setFont(16.0f);
                g.drawText("SOLO", box, juce::Justification::centred);
            } else {
                // No solos — show border indicating solo mode active
                g.setColour(juce::Colour(0xFFFFD600).withAlpha(0.3f));
                g.drawRoundedRectangle(box.toFloat().reduced(1.0f), 6.0f, 2.0f);
            }
        }
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

    // Pad indicator or multi-select status
    auto subLine = content.removeFromTop(22);
    if (multiSelectMode_) {
        g.setColour(juce::Colour(kTabActive));
        g.setFont(17.0f);
        g.drawText("SELECT: " + juce::String(multiSelectCount_) + "/8  (Enc0 to finish)",
                   subLine, juce::Justification::centredLeft);
    } else {
        g.setFont(14.0f);
        g.setColour(juce::Colour(kEncLabel));
        g.drawText("-> PAD " + juce::String(selectedPad_ + 1), subLine, juce::Justification::centredLeft);
    }
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

        // ── Section header (divider style) ──
        bool isHeader = (browseItemDurations_[idx] == "__HDR__");
        if (isHeader) {
            juce::String label = browseItemNames_[idx].substring(7);  // strip "__HDR__"
            int divY = row.getCentreY();
            g.setFont(13.0f);
            int textW = g.getCurrentFont().getStringWidth(label) + 16;
            int textX = row.getCentreX() - textW / 2;
            // Dashes left
            g.setColour(juce::Colour(0x55FFFFFF));
            for (int dx = row.getX(); dx < textX - 4; dx += 10)
                g.fillRect(dx, divY, 5, 1);
            // Dashes right
            for (int dx = textX + textW + 4; dx < row.getRight(); dx += 10)
                g.fillRect(dx, divY, 5, 1);
            // Label centered
            g.setColour(juce::Colour(0x88FFFFFF));
            g.drawText(label, textX, row.getY(), textW, row.getHeight(),
                       juce::Justification::centred);
            continue;
        }

        bool sel = (idx == browseIndex_);
        bool isDir = browseItems_[idx].isDirectory();

        // Check if this file is multi-selected
        int multiNum = -1;  // -1 = not selected
        if (multiSelectMode_) {
            for (int s = 0; s < kNumPads; ++s) {
                if (multiSelected_[s] && multiSelectedIndices_[s] == idx) {
                    multiNum = s + 1;  // 1-based display number
                    break;
                }
            }
        }

        if (sel) {
            g.setColour(juce::Colour(kBrowseSelBg));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
        }

        // Multi-select: numbered circle on the left
        int nameOffset = 8;
        if (multiSelectMode_ && !isDir) {
            int circleSize = 22;
            int circleX = row.getX() + 4;
            int circleY = row.getCentreY() - circleSize / 2;
            if (multiNum > 0) {
                // Selected — filled red circle with number
                g.setColour(juce::Colour(kTabActive));
                g.fillEllipse((float)circleX, (float)circleY, (float)circleSize, (float)circleSize);
                g.setColour(juce::Colour(0xFFFFFFFF));
                g.setFont(14.0f);
                g.drawText(juce::String(multiNum), circleX, circleY, circleSize, circleSize,
                           juce::Justification::centred);
            } else {
                // Not selected — empty circle
                g.setColour(juce::Colour(0x66FFFFFF));
                g.drawEllipse((float)circleX, (float)circleY, (float)circleSize, (float)circleSize, 1.5f);
            }
            nameOffset = circleSize + 10;
        }

        // File/folder name
        g.setColour(sel ? juce::Colour(0xFFFFFFFF) : juce::Colour(isDir ? kBrowseFolder : kBrowseText));
        g.setFont(18.0f);
        juce::String displayName = browseItemNames_[idx];
        if (!isDir && displayName.contains("."))
            displayName = displayName.upToLastOccurrenceOf(".", false, false);

        int nameW = row.getWidth() - 120;
        g.drawText(displayName, row.getX() + nameOffset, row.getY(), nameW - nameOffset + 8, rowH,
                   juce::Justification::centredLeft);

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
    // First open: go to Smart Home. Subsequent opens: stay where we were.
    if (!browseCurrentDir_.isDirectory())
    {
        browseGoHome();
    } else {
        browseScanCurrentDir();
    }
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

void PluginEditor::exitBrowseMode() {
    browseMode_ = false;
    multiSelectMode_ = false;
    multiSelectCount_ = 0;
    leftShiftHeld_ = false;  // prevent stuck shift from clearing pads
    for (int i = 0; i < kNumPads; ++i) multiSelected_[i] = false;
    updateEncoderDisplay();
    repaint();
}

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

    // Skip headers
    if (browseItemDurations_[browseIndex_] == "__HDR__") return;

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
    // If we're at Smart Home (no real dir), do nothing
    if (!browseCurrentDir_.isDirectory()) return;

    juce::File root(processor_.getSampleRootPath());
    auto parent = browseCurrentDir_.getParentDirectory();

    // Go to Smart Home if: at root, parent IS root, or outside root
    if (browseCurrentDir_ == root || parent == root || !browseCurrentDir_.isAChildOf(root)) {
        browseGoHome();
        return;
    }

    if (parent.isDirectory() && parent != browseCurrentDir_) {
        browseCurrentDir_ = parent;
        browseScanCurrentDir();
        repaint();
    }
}

void PluginEditor::browseGoHome()
{
    // Smart Home: structured landing page with sections
    browseItems_.clear();
    browseItemNames_.clear();
    browseItemDurations_.clear();

    juce::File samplesDir(processor_.getSampleRootPath());

    // ── KITS section ──
    browseItems_.add(juce::File());
    browseItemNames_.add("__HDR__KITS");
    browseItemDurations_.add("__HDR__");

    juce::File kitsDir = samplesDir.getChildFile("kits");
    if (!kitsDir.isDirectory()) kitsDir.createDirectory();
    if (kitsDir.isDirectory()) {
        browseItems_.add(kitsDir);
        browseItemNames_.add("[kits]");
        browseItemDurations_.add("");
    }

    // ── STACKS section ──
    browseItems_.add(juce::File());
    browseItemNames_.add("__HDR__STACKS");
    browseItemDurations_.add("__HDR__");

    juce::File stacksDir = samplesDir.getChildFile("stacks");
    if (!stacksDir.isDirectory()) stacksDir.createDirectory();
    if (stacksDir.isDirectory()) {
        browseItems_.add(stacksDir);
        browseItemNames_.add("[stacks]");
        browseItemDurations_.add("");
    }

    // ── RECORDINGS section ──
    browseItems_.add(juce::File());
    browseItemNames_.add("__HDR__RECORDINGS");
    browseItemDurations_.add("__HDR__");

    juce::File recDir = samplesDir.getChildFile("recordings");
    if (!recDir.isDirectory()) recDir.createDirectory();
    if (recDir.isDirectory()) {
        browseItems_.add(recDir);
        browseItemNames_.add("[recordings]");
        browseItemDurations_.add("");
    }

    // ── SAMPLES section ──
    browseItems_.add(juce::File());
    browseItemNames_.add("__HDR__SAMPLES");
    browseItemDurations_.add("__HDR__");

    // Show root sample folders and files
    if (samplesDir.isDirectory()) {
        auto dirs = samplesDir.findChildFiles(juce::File::findDirectories, false);
        dirs.sort();
        for (auto& d : dirs) {
            // Skip kits/stacks/recordings since they're in their own sections
            auto name = d.getFileName().toLowerCase();
            if (name == "kits" || name == "stacks" || name == "recordings") continue;
            browseItems_.add(d);
            browseItemNames_.add("[" + d.getFileName() + "]");
            browseItemDurations_.add("");
        }
        auto files = samplesDir.findChildFiles(juce::File::findFiles, false, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF");
        files.sort();
        for (auto& f : files) {
            browseItems_.add(f);
            browseItemNames_.add(f.getFileName());
            juce::String info;
            if (auto* reader = browseFormatMgr_.createReaderFor(f)) {
                double secs = (double)reader->lengthInSamples / reader->sampleRate;
                if (secs < 1.0)
                    info = juce::String((int)(secs * 1000)) + "ms";
                else if (secs < 60.0)
                    info = juce::String(secs, 1) + "s";
                else
                    info = juce::String((int)(secs / 60)) + ":" + juce::String((int)secs % 60).paddedLeft('0', 2);
                int sr = (int)reader->sampleRate;
                if (sr >= 1000) info += "   " + juce::String(sr / 1000) + "k";
                info += "   " + juce::String(reader->numChannels > 1 ? "Stereo" : "Mono");
                delete reader;
            }
            browseItemDurations_.add(info);
        }
    }

    // Clear Pad action
    browseItems_.add(juce::File());
    browseItemNames_.add(">> Clear Pad");
    browseItemDurations_.add("");

    browseCurrentDir_ = juce::File();  // no real dir — we're in smart home
    browseIndex_ = 0;
    browseScrollOffset_ = 0;
    // Skip first header so cursor starts on first selectable item
    while (browseIndex_ < browseItemDurations_.size()
           && browseItemDurations_[browseIndex_] == "__HDR__")
        browseIndex_++;
    if (browseIndex_ >= browseItems_.size()) browseIndex_ = 0;
    repaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// Page System
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::switchPage(int page) {
    if (browseMode_) return;
    currentPage_ = juce::jlimit(0, kNumPages - 1, page);
    if (currentPage_ == PAGE_MIDI)
        processor_.refreshMidiDevices();
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
            const char* l[] = { "", "START", "END", "PITCH" };
            return l[enc];
        }
        case PAGE_PLAY: {
            const char* l[] = { "", "MODE", "VOL", "PAN" };
            return l[enc];
        }
        case PAGE_PITCH: {
            const char* l[] = { "", "TIME", "CLK M/D", "SLICE" };
            return l[enc];
        }
        case PAGE_FADE: {
            const char* l[] = { "", "FADE IN", "FADE OUT", "---" };
            return l[enc];
        }
        case PAGE_FILTER: {
            const char* l[] = { "", "TYPE", "CUTOFF", "RESO" };
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
            if (enc == 3) {
                float st = slot.getPitchSemitones();
                return (st >= 0 ? "+" : "") + juce::String(st, 1) + "st";
            }
            return "---";
        }
        case PAGE_PLAY: {
            const char* modes[] = { "ONE-SHOT", "LOOP", "CLK:LOOP", "CLK:1SHOT" };
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
            if (enc == 1) {
                bool clocked = (slot.getMode() == PadMode::ClockedLoop || slot.getMode() == PadMode::ClockedOneShot);
                if (clocked && (processor_.hasClockInput() || processor_.isMidiClockEnabled())) {
                    return juce::String(slot.getTimeStretch(), 2) + "x CLK";
                }
                return juce::String(slot.getTimeStretch(), 2) + "x";
            }
            if (enc == 2) {
                int d = processor_.getClockDiv();
                if (d < 0) return "*" + juce::String(1 << (-d));
                if (d == 0) return "/1";
                return "/" + juce::String(1 << d);
            }
            if (enc == 3) {
                int sc = slot.getSliceCount();
                if (sc == 0) return "[PUSH]";
                return juce::String(sc) + " pts";
            }
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
        case PAGE_FILTER: {
            if (enc == 1) {
                const char* types[] = { "OFF", "LPF", "HPF", "BPF", "NOTCH", "FORMANT", "MS-20" };
                return types[static_cast<int>(slot.getFilterType())];
            }
            if (enc == 2) {
                float hz = slot.getFilterCutoff();
                FilterType ft = slot.getFilterType();
                // Strength: 0% = no filtering, 100% = max filtering
                float pct;
                if (ft == FilterType::HPF)
                    pct = std::log(hz / 20.0f) / std::log(1000.0f);
                else
                    pct = 1.0f - std::log(hz / 20.0f) / std::log(1000.0f);
                pct = juce::jlimit(0.0f, 1.0f, pct) * 100.0f;
                juce::String hzStr;
                if (hz >= 10000.0f) hzStr = juce::String(hz / 1000.0f, 1) + "k";
                else if (hz >= 1000.0f) hzStr = juce::String(hz / 1000.0f, 2) + "k";
                else hzStr = juce::String((int)hz);
                return juce::String((int)pct) + "% / " + hzStr + "Hz";
            }
            if (enc == 3) return juce::String((int)(slot.getFilterResonance() * 100)) + "%";
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
    // Kit browse timeout: revert after 3 seconds of no input
    if (kitBrowseActive_) {
        double elapsed = juce::Time::getMillisecondCounterHiRes() - kitBrowseStartTime_;
        if (elapsed > kKitBrowseTimeoutMs) {
            kitBrowseRevert();
        }
    }

    // Left shift long-press: go to smart home root in browse mode
    if (browseMode_ && leftShiftHeld_) {
        double held = juce::Time::getMillisecondCounterHiRes() - leftShiftPressTime_;
        if (held > 2000.0) {
            // Clear multi-select if active
            multiSelectMode_ = false;
            multiSelectCount_ = 0;
            for (int i = 0; i < kNumPads; ++i) multiSelected_[i] = false;
            browseGoHome();
            leftShiftPressTime_ = juce::Time::getMillisecondCounterHiRes() + 99999.0;  // prevent re-trigger
            processor_.showTickerPublic("Smart Home");
            repaint();
        }
    }

    if (browseMode_) { repaint(); return; }
    if (sliceEditorMode_) { repaint(); return; }  // encoder labels managed by paintSliceEditor
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

    // Popup blocks everything
    if (popupMode_) return;

    // Keyboard blocks everything
    if (keyboardMode_) return;

    // Config mode: switch pad context
    if (configMode_) {
        selectedPad_ = n;
        buildConfigRows();
        repaint();
        return;
    }

    // Left shift + pad = clear pad (only on OVERVIEW/OPTIONS page, not in any overlay)
    if (leftShiftHeld_ && !browseMode_ && !muteMode_ && !configMode_
        && (currentPage_ == PAGE_OVERVIEW || currentPage_ == PAGE_OPTIONS)) {
        processor_.getEngine().getSlot(n).clear();
        processor_.getEngine().setMuted(n, false);
        selectedPad_ = n;
        processor_.showTickerPublic("Pad " + juce::String(n + 1) + " cleared");
        repaint();
        return;
    }

    // Solo mode: double-tap RS held, buttons toggle solo
    if (soloMode_) {
        soloActive_[n] = !soloActive_[n];
        // Apply: mute everything except soloed pads
        bool anySoloed = false;
        for (int i = 0; i < kNumPads; ++i) if (soloActive_[i]) anySoloed = true;
        if (anySoloed) {
            processor_.getEngine().applySolo(soloActive_);
        } else {
            // No solos active — restore pre-solo state
            for (int i = 0; i < kNumPads; ++i)
                processor_.getEngine().setMuted(i, preSoloMute_[i]);
        }
        repaint();
        return;
    }

    // Mute mode: right shift held, buttons toggle mutes
    if (muteMode_) {
        PerfMode mode = processor_.getPerfMode();
        if (mode == PerfMode::Immediate) {
            processor_.getEngine().toggleMute(n);
        } else {
            pendingMute_[n] = !pendingMute_[n];
        }
        muteToggled_ = true;
        repaint();
        return;
    }

    if (browseMode_) {
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
        // Wall-clock debounce: drop stale queued button events
        double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (nowMs - lastButtonTriggerMs_[n] < kButtonDebounceMs) {
            selectedPad_ = n;
            repaint();
            return;
        }
        lastButtonTriggerMs_[n] = nowMs;
        processor_.getEngine().triggerWithChoke(n);
    }
    selectedPad_ = n;
    repaint();
}

void PluginEditor::onLeftButton(bool val)
{
    if (!val) return;
    if (keyboardMode_) {
        keyboardCol_ = std::max(0, keyboardCol_ - 1);
        repaint(); return;
    }
    if (sliceEditorMode_) {
        // Left arrow = previous slice boundary (includes start, wraps)
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        // Build full nav list: start + all slice points + end
        float navPts[66];
        int navCount = 0;
        navPts[navCount++] = slot.getStartPos();
        for (int i = 0; i < slot.getSliceCount(); ++i) navPts[navCount++] = slot.getSlicePoint(i);
        navPts[navCount++] = slot.getEndPos();

        // Find previous position
        int found = -1;
        for (int i = navCount - 1; i >= 0; --i) {
            if (navPts[i] < sliceCursorPos_ - 0.001f) { found = i; break; }
        }
        if (found < 0) found = navCount - 1;  // wrap to end
        sliceCursorPos_ = navPts[found];
        sliceViewCenter_ = sliceCursorPos_;
        float halfSpan = 0.5f / sliceZoom_;
        sliceViewCenter_ = juce::jlimit(halfSpan, 1.0f - halfSpan, sliceViewCenter_);
        repaint(); return;
    }
    if (popupMode_) { closePopup(-1); return; }
    if (configMode_) {
        configAdjustValue(configIndex_, -1);
        configEditMode_ = true;
        repaint();
        return;
    }
    if (browseMode_) return;
    switchPage(currentPage_ - 1);
}

void PluginEditor::onRightButton(bool val)
{
    if (!val) return;
    if (keyboardMode_) {
        keyboardCol_ = std::min(keyboardRowLen(keyboardRow_) - 1, keyboardCol_ + 1);
        repaint(); return;
    }
    if (sliceEditorMode_) {
        // Right arrow = next slice boundary (includes end, wraps)
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        float navPts[66];
        int navCount = 0;
        navPts[navCount++] = slot.getStartPos();
        for (int i = 0; i < slot.getSliceCount(); ++i) navPts[navCount++] = slot.getSlicePoint(i);
        navPts[navCount++] = slot.getEndPos();

        int found = -1;
        for (int i = 0; i < navCount; ++i) {
            if (navPts[i] > sliceCursorPos_ + 0.001f) { found = i; break; }
        }
        if (found < 0) found = 0;  // wrap to start
        sliceCursorPos_ = navPts[found];
        sliceViewCenter_ = sliceCursorPos_;
        float halfSpan = 0.5f / sliceZoom_;
        sliceViewCenter_ = juce::jlimit(halfSpan, 1.0f - halfSpan, sliceViewCenter_);
        repaint(); return;
    }
    if (configMode_) {
        configAdjustValue(configIndex_, 1);
        configEditMode_ = true;
        repaint();
        return;
    }
    if (browseMode_) return;
    switchPage(currentPage_ + 1);
}

void PluginEditor::onUpButton(bool val)
{
    if (!val) return;
    if (keyboardMode_) {
        keyboardRow_ = std::max(0, keyboardRow_ - 1);
        keyboardCol_ = std::min(keyboardCol_, keyboardRowLen(keyboardRow_) - 1);
        repaint(); return;
    }
    if (sliceEditorMode_) {
        repaint(); return;  // up/down unused in slice editor
    }
    if (popupMode_) {
        popupIndex_ = std::max(0, popupIndex_ - 1);
        repaint(); return;
    }
    if (configMode_) {
        if (configIndex_ > 0) { configIndex_--; configEditMode_ = false; }
        repaint(); return;
    }
    if (browseMode_) {
        browseIndex_ = std::max(0, browseIndex_ - 1);
        // Skip headers
        while (browseIndex_ > 0 && browseItemDurations_[browseIndex_] == "__HDR__")
            browseIndex_--;
        repaint(); return;
    }

    // Kit browsing: up = previous kit
    if (!browseMode_ && !configMode_) {
        if (!kitBrowseActive_) {
            refreshAvailableKits();
            if (availableKits_.isEmpty()) {
                processor_.showTickerPublic("No kits found");
                return;
            }
            kitBrowseActive_ = true;
            kitBrowseIndex_ = std::max(0, kitCurrentIndex_ - 1);
        } else {
            kitBrowseIndex_ = std::max(0, kitBrowseIndex_ - 1);
        }
        kitBrowseStartTime_ = juce::Time::getMillisecondCounterHiRes();
        auto kitName = availableKits_[kitBrowseIndex_].getFileNameWithoutExtension();
        processor_.showTickerPublic("Kit: " + kitName + " (" + juce::String(kitBrowseIndex_ + 1)
                                    + "/" + juce::String(availableKits_.size()) + ")");
        repaint();
        return;
    }

    if (selectedPad_ >= 4) { selectedPad_ -= 4; repaint(); }
}

void PluginEditor::onDownButton(bool val)
{
    if (!val) return;
    if (keyboardMode_) {
        keyboardRow_ = std::min(3, keyboardRow_ + 1);
        keyboardCol_ = std::min(keyboardCol_, keyboardRowLen(keyboardRow_) - 1);
        repaint(); return;
    }
    if (sliceEditorMode_) {
        repaint(); return;  // up/down unused in slice editor
    }
    if (popupMode_) {
        popupIndex_ = std::min(popupOptions_.size() - 1, popupIndex_ + 1);
        repaint(); return;
    }
    if (configMode_) {
        if (configIndex_ < configSelectableCount() - 1) { configIndex_++; configEditMode_ = false; }
        repaint(); return;
    }
    if (browseMode_) {
        browseIndex_ = std::min(std::max(0, browseItems_.size() - 1), browseIndex_ + 1);
        // Skip headers
        while (browseIndex_ < browseItems_.size() - 1 && browseItemDurations_[browseIndex_] == "__HDR__")
            browseIndex_++;
        repaint(); return;
    }

    // Kit browsing: down = next kit
    if (!browseMode_ && !configMode_) {
        if (!kitBrowseActive_) {
            refreshAvailableKits();
            if (availableKits_.isEmpty()) {
                processor_.showTickerPublic("No kits found");
                return;
            }
            kitBrowseActive_ = true;
            kitBrowseIndex_ = std::min(availableKits_.size() - 1,
                                       kitCurrentIndex_ >= 0 ? kitCurrentIndex_ + 1 : 0);
        } else {
            kitBrowseIndex_ = std::min(availableKits_.size() - 1, kitBrowseIndex_ + 1);
        }
        kitBrowseStartTime_ = juce::Time::getMillisecondCounterHiRes();
        auto kitName = availableKits_[kitBrowseIndex_].getFileNameWithoutExtension();
        processor_.showTickerPublic("Kit: " + kitName + " (" + juce::String(kitBrowseIndex_ + 1)
                                    + "/" + juce::String(availableKits_.size()) + ")");
        repaint();
        return;
    }

    if (selectedPad_ < 4) { selectedPad_ += 4; repaint(); }
}

void PluginEditor::onLeftShiftButton(bool val)
{
    leftShiftHeld_ = val;
    if (val) leftShiftPressTime_ = juce::Time::getMillisecondCounterHiRes();

    if (!val) return;

    // Popup: left shift selects current option
    if (popupMode_) {
        closePopup(popupIndex_);
        return;
    }

    // Keyboard: left shift types character or triggers button
    if (keyboardMode_) {
        keyboardAction();
        return;
    }

    // Slice editor: LS hold = audition current slice
    if (sliceEditorMode_) {
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        if (slot.isLoaded() && slot.getSliceCount() > 0) {
            int curSlice = 0;
            for (int i = 0; i < slot.getSliceCount(); ++i)
                if (sliceCursorPos_ >= slot.getSlicePoint(i)) curSlice = i + 1;
            float slStart, slEnd;
            slot.getSliceRegion(curSlice, slStart, slEnd);
            slot.setStartPos(slStart);
            slot.setEndPos(slEnd);
            processor_.getEngine().trigger(selectedPad_);
        }
        repaint();
        return;
    }

    // LS+RS = config toggle
    if (rightShiftHeld_) {
        if (configMode_) exitConfigMode();
        else enterConfigMode();
        return;
    }

    // Kit browse: left shift commits
    if (kitBrowseActive_) {
        kitBrowseCommit();
        return;
    }

    // In browse mode: multi-select interactions
    if (browseMode_) {
        if (!multiSelectMode_) {
            // Enter multi-select
            multiSelectMode_ = true;
            multiSelectCount_ = 0;
            for (int i = 0; i < kNumPads; ++i) multiSelected_[i] = false;
            processor_.showTickerPublic("Multi-select: LS=select, Enc0=finish");
        } else {
            // In multi-select: left shift on file = toggle, on folder = enter
            if (browseIndex_ >= 0 && browseIndex_ < browseItems_.size()
                && browseItemDurations_[browseIndex_] != "__HDR__") {
                auto& item = browseItems_.getReference(browseIndex_);
                if (item.isDirectory()) {
                    // Navigate into folder
                    browseCurrentDir_ = item;
                    browseScanCurrentDir();
                } else {
                    // Toggle file selection
                    bool wasSelected = false;
                    for (int i = 0; i < kNumPads; ++i) {
                        if (multiSelected_[i] && multiSelectedIndices_[i] == browseIndex_) {
                            multiSelected_[i] = false;
                            multiSelectCount_--;
                            wasSelected = true;
                            break;
                        }
                    }
                    if (!wasSelected && multiSelectCount_ < kNumPads) {
                        for (int i = 0; i < kNumPads; ++i) {
                            if (!multiSelected_[i]) {
                                multiSelected_[i] = true;
                                multiSelectedIndices_[i] = browseIndex_;
                                multiSelectCount_++;
                                break;
                            }
                        }
                    }
                }
            }
        }
        repaint();
        return;
    }
}

void PluginEditor::onRightShiftButton(bool val)
{
    rightShiftHeld_ = val;

    // Right shift cancels keyboard
    if (val && keyboardMode_) {
        closeKeyboard();
        return;
    }

    // Right shift = exit slice editor
    if (val && sliceEditorMode_) {
        exitSliceEditor();
        return;
    }

    if (val && leftShiftHeld_) {
        if (configMode_) exitConfigMode();
        else enterConfigMode();
        return;
    }

    // Config mode: don't enter mute mode
    if (configMode_) return;

    // ── RS PRESS ─────────────────────────────────────────────────────────
    if (val) {
        double nowMs = juce::Time::getMillisecondCounterHiRes();
        bool doubleTap = (nowMs - lastRSTapTime_ < kDoubleTapMs);
        lastRSTapTime_ = nowMs;

        if (doubleTap && !soloMode_) {
            // Double-tap: enter SOLO mode
            muteMode_ = false;
            soloMode_ = true;
            // Save current mute state so we can restore on exit
            for (int i = 0; i < kNumPads; ++i) {
                preSoloMute_[i] = processor_.getEngine().isMuted(i);
                soloActive_[i] = false;
            }
            processor_.showTickerPublic("SOLO MODE");
        } else if (!soloMode_) {
            // Single tap: enter MUTE mode
            muteMode_ = true;
            for (int i = 0; i < kNumPads; ++i) pendingMute_[i] = false;
        }
        repaint();
    }
    // ── RS RELEASE ───────────────────────────────────────────────────────
    else {
        if (soloMode_) {
            // Exit solo: restore pre-solo mute state
            soloMode_ = false;
            for (int i = 0; i < kNumPads; ++i)
                processor_.getEngine().setMuted(i, preSoloMute_[i]);
            processor_.showTickerPublic("Solo off");
        }
        else if (muteMode_) {
            muteMode_ = false;
            PerfMode mode = processor_.getPerfMode();

            if (mode == PerfMode::OnRelease) {
                for (int i = 0; i < kNumPads; ++i) {
                    if (pendingMute_[i]) {
                        processor_.getEngine().toggleMute(i);
                        pendingMute_[i] = false;
                    }
                }
            }
            else if (mode == PerfMode::OnBar) {
                for (int i = 0; i < kNumPads; ++i)
                    processor_.setPendingBarMute(i, false);
                bool anyPending = false;
                for (int i = 0; i < kNumPads; ++i) {
                    if (pendingMute_[i]) {
                        processor_.setPendingBarMute(i, true);
                        anyPending = true;
                    }
                    pendingMute_[i] = false;
                }
                if (anyPending)
                    processor_.commitBarMutes(processor_.getQueueBars());
            }
        }
        repaint();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Encoder Handlers
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::onEncoder(int n, float delta)
{
    // Popup: encoder scrolls options
    if (popupMode_) {
        int d = (delta > 0) ? 1 : -1;
        popupIndex_ = juce::jlimit(0, popupOptions_.size() - 1, popupIndex_ + d);
        repaint();
        return;
    }

    // Keyboard: encoder scrolls characters
    if (keyboardMode_) {
        int d = (delta > 0) ? 1 : -1;
        keyboardCol_ = juce::jlimit(0, keyboardRowLen(keyboardRow_) - 1, keyboardCol_ + d);
        repaint();
        return;
    }

    // Slice editor: cursor, zoom, auto-slice
    if (sliceEditorMode_) {
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        if (n == 0) {
            // Cursor with momentum: slow turn = micro precision, fast turn = big jumps
            float absDelta = std::abs(delta);
            float speed = absDelta < 1.5f ? absDelta : absDelta * absDelta * 0.5f;
            float step = (delta > 0 ? 1.0f : -1.0f) * speed * 0.003f / sliceZoom_;
            sliceCursorPos_ = juce::jlimit(0.0f, 1.0f, sliceCursorPos_ + step);
            // Keep cursor visible: pan view to follow
            float halfSpan = 0.5f / sliceZoom_;
            if (sliceCursorPos_ < sliceViewCenter_ - halfSpan * 0.85f)
                sliceViewCenter_ = sliceCursorPos_ + halfSpan * 0.85f;
            if (sliceCursorPos_ > sliceViewCenter_ + halfSpan * 0.85f)
                sliceViewCenter_ = sliceCursorPos_ - halfSpan * 0.85f;
            sliceViewCenter_ = juce::jlimit(halfSpan, 1.0f - halfSpan, sliceViewCenter_);
        }
        else if (n == 1) {
            // Zoom: 1x to 32x, centered on cursor
            float zoomDelta = delta * 0.3f;
            sliceZoom_ = juce::jlimit(1.0f, 32.0f, sliceZoom_ + zoomDelta * sliceZoom_ * 0.1f);
            sliceViewCenter_ = sliceCursorPos_;  // re-center on cursor
            float halfSpan = 0.5f / sliceZoom_;
            sliceViewCenter_ = juce::jlimit(halfSpan, 1.0f - halfSpan, sliceViewCenter_);
        }
        else if (n == 2) {
            // Auto-slice preset selector
            int d = (delta > 0) ? 1 : -1;
            sliceAutoPreset_ = juce::jlimit(0, kSliceAutoCount - 1, sliceAutoPreset_ + d);
        }
        else if (n == 3) {
            // Per-slice pitch: adjust pitch for the slice the cursor is in
            auto& slot = processor_.getEngine().getSlot(selectedPad_);
            int curSlice = 0;
            for (int i = 0; i < slot.getSliceCount(); ++i)
                if (sliceCursorPos_ >= slot.getSlicePoint(i)) curSlice = i + 1;
            float cur = slot.getSlicePitch(curSlice);
            slot.setSlicePitch(curSlice, cur + delta * 1.0f);
        }
        repaint();
        return;
    }

    // Config mode: enc 0 = scroll rows, enc 1-3 = adjust value if editing
    if (configMode_) {
        if (n == 0) {
            int d = (delta > 0) ? 1 : -1;
            int count = configSelectableCount();
            configIndex_ = juce::jlimit(0, std::max(0, count - 1), configIndex_ + d);
            configEditMode_ = false;  // reset edit on scroll
        } else if (configEditMode_) {
            configAdjustValue(configIndex_, delta > 0 ? 1 : -1);
        }
        repaint();
        return;
    }

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
                // Skip headers
                if (delta > 0) {
                    while (browseIndex_ < fc - 1 && browseItemDurations_[browseIndex_] == "__HDR__")
                        browseIndex_++;
                } else {
                    while (browseIndex_ > 0 && browseItemDurations_[browseIndex_] == "__HDR__")
                        browseIndex_--;
                }
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

    // Apply global encoder speed (doesn't affect enum steps which use delta > 0 ? 1 : -1)
    delta *= processor_.getEncoderSpeed();

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
            if (n == 3) slot.setPitchSemitones(slot.getPitchSemitones() + delta * 1.0f);
            break;

        case PAGE_PLAY:
            if (n == 1) {
                int m = static_cast<int>(slot.getMode()) + (delta > 0 ? 1 : -1);
                slot.setMode(static_cast<PadMode>(juce::jlimit(0, 3, m)));
                slot.setTimeStretch(1.0f);
            }
            if (n == 2) slot.setVolume(juce::jlimit(0.0f, 1.0f, slot.getVolume() + delta * 0.02f));
            if (n == 3) slot.setPan(juce::jlimit(-1.0f, 1.0f, slot.getPan() + delta * 0.05f));
            break;

        case PAGE_PITCH:
            if (n == 1) slot.setTimeStretch(slot.getTimeStretch() + delta * 0.01f);
            if (n == 2) {
                int cur = processor_.getClockDiv();
                processor_.setClockDiv(cur + (delta > 0 ? 1 : -1));
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

        case PAGE_FILTER: {
            if (n == 1) {
                int t = static_cast<int>(slot.getFilterType()) + (delta > 0 ? 1 : -1);
                auto newType = static_cast<FilterType>(juce::jlimit(0, 6, t));
                slot.setFilterType(newType);
                // Reset cutoff to 0% (no filtering) for the new type
                if (newType == FilterType::HPF)
                    slot.setFilterCutoff(20.0f);      // HPF 0% = 20Hz
                else if (newType != FilterType::Off)
                    slot.setFilterCutoff(20000.0f);    // others 0% = 20kHz
            }
            if (n == 2) {
                // Work in strength % space: right = more filtering
                float hz = slot.getFilterCutoff();
                FilterType ft = slot.getFilterType();
                float pct;
                if (ft == FilterType::HPF)
                    pct = std::log(hz / 20.0f) / std::log(1000.0f);
                else
                    pct = 1.0f - std::log(hz / 20.0f) / std::log(1000.0f);
                pct += delta * 0.02f;  // 2% per click (scaled by encoder speed)
                pct = juce::jlimit(0.0f, 1.0f, pct);
                float newHz;
                if (ft == FilterType::HPF)
                    newHz = 20.0f * std::pow(1000.0f, pct);
                else
                    newHz = 20.0f * std::pow(1000.0f, 1.0f - pct);
                slot.setFilterCutoff(newHz);
            }
            if (n == 3) {
                slot.setFilterResonance(slot.getFilterResonance() + delta * 0.02f);
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

    // Popup: push selects current option
    if (popupMode_) {
        return;
    }

    // Slice editor: push actions
    if (sliceEditorMode_) {
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        if (n == 0) {
            // Push cursor = insert or remove slice at cursor position
            if (!slot.removeSlicePoint(sliceCursorPos_, 0.01f / sliceZoom_)) {
                int idx = slot.insertSlicePoint(sliceCursorPos_);
                if (idx >= 0) {
                    int regions = slot.getSliceCount() + 1;
                    processor_.showTickerPublic("Cut — " + juce::String(regions) + " slices");
                } else {
                    processor_.showTickerPublic("64 slice limit reached");
                }
            } else {
                int regions = slot.getSliceCount() + 1;
                if (slot.getSliceCount() == 0)
                    processor_.showTickerPublic("All cuts removed");
                else
                    processor_.showTickerPublic("Cut removed — " + juce::String(regions) + " slices");
            }
        }
        else if (n == 1) {
            // Push zoom = snap cursor to nearest zero crossing
            float before = sliceCursorPos_;
            sliceCursorPos_ = slot.findNearestZeroCrossing(sliceCursorPos_);
            float movedMs = std::abs(sliceCursorPos_ - before) * (float)slot.getNumSamples() / (float)slot.getSampleRate() * 1000.0f;
            processor_.showTickerPublic("Zero crossing — moved " + juce::String(movedMs, 1) + "ms");
        }
        else if (n == 2) {
            // Push auto = apply instantly (no popup)
            int val = sliceAutoValue(sliceAutoPreset_);
            if (val == 0) {
                slot.clearSlices();
                processor_.showTickerPublic("All slices cleared");
            } else if (val == -1) {
                // Transient detection
                slot.detectTransients(0.5f);
                processor_.showTickerPublic("Transients: " + juce::String(slot.getSliceCount()) + " cuts");
            } else {
                slot.autoSlice(val);
                processor_.showTickerPublic("Auto-sliced into " + juce::String(val));
            }
        }
        else if (n == 3) {
            // Push pitch = reset pitch for current slice to 0
            int curSlice = 0;
            for (int i = 0; i < slot.getSliceCount(); ++i)
                if (sliceCursorPos_ >= slot.getSlicePoint(i)) curSlice = i + 1;
            slot.setSlicePitch(curSlice, 0.0f);
            processor_.showTickerPublic("Slice " + juce::String(curSlice + 1) + " pitch reset");
        }
        repaint();
        return;
    }

    // Config mode: push toggles edit mode or executes action
    if (configMode_) {
        if (n == 0) { exitConfigMode(); return; }
        configPushValue(configIndex_);
        repaint();
        return;
    }

    if (browseMode_) {
        if (n == 0) {
            if (multiSelectMode_) {
                // Exit multi-select — show popup if any selected
                multiSelectMode_ = false;
                if (multiSelectCount_ > 0) {
                    juce::StringArray selectedPaths;
                    for (int i = 0; i < kNumPads; ++i) {
                        if (multiSelected_[i] && multiSelectedIndices_[i] < browseItems_.size())
                            selectedPaths.add(browseItems_[multiSelectedIndices_[i]].getFullPathName());
                    }
                    int count = multiSelectCount_;
                    multiSelectCount_ = 0;
                    for (int i = 0; i < kNumPads; ++i) multiSelected_[i] = false;

                    showPopup("MULTI-SELECT", { "Create Kit", "Create Stack", "Delete Selected", "Cancel" },
                        [this, selectedPaths, count](int result) {
                            if (result == 0) {
                                for (int i = 0; i < std::min(count, kNumPads); ++i) {
                                    juce::File f(selectedPaths[i]);
                                    if (f.existsAsFile())
                                        processor_.getEngine().getSlot(i).loadFile(f);
                                }
                                showNameEntryPopup("NAME YOUR KIT", [this](const juce::String& name) {
                                    processor_.saveCurrentAsKit(name);
                                });
                            } else if (result == 1) {
                                juce::StringArray relPaths;
                                auto root = processor_.getSampleRootPath();
                                for (auto& p : selectedPaths) {
                                    if (p.startsWith(root))
                                        relPaths.add(p.substring(root.length() + 1));
                                    else
                                        relPaths.add(p);
                                }
                                showNameEntryPopup("NAME YOUR STACK", [this, relPaths](const juce::String& name) {
                                    processor_.createStackFile(name, relPaths);
                                });
                            } else if (result == 2) {
                                // Build confirmation with file list + Yes/Cancel
                                juce::StringArray confirmOpts;
                                for (auto& p : selectedPaths)
                                    confirmOpts.add(juce::File(p).getFileName());
                                confirmOpts.add("Yes, delete all");
                                confirmOpts.add("Cancel");
                                int yesIdx = selectedPaths.size();

                                showPopup("DELETE " + juce::String(selectedPaths.size()) + " FILES?", confirmOpts,
                                    [this, selectedPaths, yesIdx](int confirmResult) {
                                        if (confirmResult == yesIdx) {
                                            int deleted = 0;
                                            for (auto& p : selectedPaths) {
                                                juce::File f(p);
                                                if (f.existsAsFile() && f.deleteFile()) deleted++;
                                            }
                                            processor_.showTickerPublic("Deleted " + juce::String(deleted) + " files");
                                            if (browseMode_) browseScanCurrentDir();
                                        }
                                    });
                            }
                        });
                } else {
                    processor_.showTickerPublic("No files selected");
                }
                repaint();
                return;
            }
            exitBrowseMode(); return;
        }

        // Multi-select mode: enc 1 push toggles file selection
        if (multiSelectMode_ && n == 1) {
            if (browseIndex_ >= 0 && browseIndex_ < browseItems_.size()) {
                auto& sel = browseItems_.getReference(browseIndex_);
                if (!sel.isDirectory()) {
                    // Find if already selected, toggle it
                    // Use browseIndex as key — track which browse indices are selected
                    bool wasSelected = false;
                    for (int i = 0; i < kNumPads; ++i) {
                        if (multiSelected_[i] && multiSelectedIndices_[i] == browseIndex_) {
                            multiSelected_[i] = false;
                            multiSelectCount_--;
                            wasSelected = true;
                            break;
                        }
                    }
                    if (!wasSelected && multiSelectCount_ < kNumPads) {
                        // Find next empty slot
                        for (int i = 0; i < kNumPads; ++i) {
                            if (!multiSelected_[i]) {
                                multiSelected_[i] = true;
                                multiSelectedIndices_[i] = browseIndex_;
                                multiSelectCount_++;
                                break;
                            }
                        }
                    }
                    repaint();
                }
            }
            return;
        }

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
            if (n == 3) slot.setPitchSemitones(0.0f);
            break;
        case PAGE_PLAY:
            if (n == 1) { slot.setMode(PadMode::OneShot); slot.setTimeStretch(1.0f); }
            if (n == 2) processor_.getEngine().toggleMute(selectedPad_);
            if (n == 3) slot.setPan(0.0f);
            break;
        case PAGE_PITCH:
            if (n == 1) slot.setTimeStretch(1.0f);
            if (n == 2) processor_.setClockDiv(0);
            if (n == 3) enterSliceEditor();
            break;
        case PAGE_FADE:
            if (n == 1) slot.setFadeInCurve(slot.getFadeInCurve() == 0 ? 1 : 0);
            if (n == 2) slot.setFadeOutCurve(slot.getFadeOutCurve() == 0 ? 1 : 0);
            break;
        case PAGE_FILTER:
            if (n == 1) slot.setFilterType(FilterType::Off);
            if (n == 2) {
                // Reset to 0% (no filtering) for current type
                if (slot.getFilterType() == FilterType::HPF)
                    slot.setFilterCutoff(20.0f);
                else
                    slot.setFilterCutoff(20000.0f);
            }
            if (n == 3) slot.setFilterResonance(0.0f);
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
            if (n == 3) {
                float oldGain = slot.getNormalizeGain();
                slot.normalize();
                float newGain = slot.getNormalizeGain();
                if (newGain <= 1.0f && oldGain <= 1.0f && std::abs(newGain - 1.0f) < 0.01f) {
                    // Already at or near 0dB
                } else {
                    float dB = 20.0f * std::log10(newGain);
                    processor_.showTickerPublic("Normalized: " + juce::String(dB > 0 ? "+" : "") + juce::String(dB, 1) + " dB");
                }
            }
            break;
        default:
            break;
    }
    repaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// Config Browser (right-side flyout, LS+RS)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::enterConfigMode()
{
    if (browseMode_) return;
    configMode_ = true;
    configIndex_ = 0;
    configScrollOffset_ = 0;
    configEditMode_ = false;
    buildConfigRows();
    // Update encoder bar
    encoderSlots_[0].nameLabel.setText("CONFIG", juce::dontSendNotification);
    encoderSlots_[0].valueLabel.setText("[CLOSE]", juce::dontSendNotification);
    encoderSlots_[1].nameLabel.setText("SCROLL", juce::dontSendNotification);
    encoderSlots_[1].valueLabel.setText("Up/Down", juce::dontSendNotification);
    encoderSlots_[2].nameLabel.setText("EDIT", juce::dontSendNotification);
    encoderSlots_[2].valueLabel.setText("[PUSH]", juce::dontSendNotification);
    encoderSlots_[3].nameLabel.setText("", juce::dontSendNotification);
    encoderSlots_[3].valueLabel.setText("", juce::dontSendNotification);
    repaint();
}

void PluginEditor::exitConfigMode()
{
    configMode_ = false;
    configEditMode_ = false;
    leftShiftHeld_ = false;
    rightShiftHeld_ = false;
    updateEncoderDisplay();
    repaint();
}

void PluginEditor::buildConfigRows()
{
    configRows_.clear();
    auto& slot = processor_.getEngine().getSlot(selectedPad_);
    juce::String padName = slot.isLoaded() ? slot.getFileName() : "empty";
    if (padName.contains(".")) padName = padName.upToLastOccurrenceOf(".", false, false);

    // ── Pad-specific section ──
    ConfigRow hdr;
    hdr.type = ConfigRowType::Header;
    hdr.label = "PAD " + juce::String(selectedPad_ + 1) + ": " + padName;
    hdr.padIndex = selectedPad_;
    hdr.paramIndex = -1;
    configRows_.add(hdr);

    // CLK Beats: only visible when pad is in a clocked mode
    PadMode padMode = slot.getMode();
    if (padMode == PadMode::ClockedLoop || padMode == PadMode::ClockedOneShot) {
        { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "CLK Beats";
          r.padIndex = selectedPad_; r.paramIndex = 5; configRows_.add(r); }
    }

    // Lo-Fi mode (paramIndex 8)
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Lo-Fi";
      r.padIndex = selectedPad_; r.paramIndex = 8; configRows_.add(r); }

    // Spacer before CC section
    { ConfigRow s; s.type = ConfigRowType::Spacer; s.padIndex = -1; s.paramIndex = -1;
      configRows_.add(s); }

    for (int i = 0; i < PadCCMap::kNumCCs; ++i) {
        ConfigRow row;
        row.type = ConfigRowType::CCValue;
        row.label = PadCCMap::ccName(i);
        row.padIndex = selectedPad_;
        row.paramIndex = i;
        configRows_.add(row);
    }

    // ── Divider ──
    ConfigRow div;
    div.type = ConfigRowType::Divider;
    div.label = "GLOBAL";
    div.padIndex = -1;
    div.paramIndex = -1;
    configRows_.add(div);

    // ── Performance group ──
    // 0 = Mute Mode
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Mute Mode";
      r.padIndex = -1; r.paramIndex = 0; configRows_.add(r); }
    // 2 = Queue Bars (grouped with Mute Mode)
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Queue Bars";
      r.padIndex = -1; r.paramIndex = 2; configRows_.add(r); }
    // 6 = Mute Fade
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Mute Fade";
      r.padIndex = -1; r.paramIndex = 6; configRows_.add(r); }

    // ── Spacer between groups ──
    { ConfigRow s; s.type = ConfigRowType::Spacer; s.padIndex = -1; s.paramIndex = -1;
      configRows_.add(s); }

    // ── Slice CV group ──
    // 7 = Slice CV 1
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Slice CV 1";
      r.padIndex = -1; r.paramIndex = 7; configRows_.add(r); }
    // 8 = Slice CV 2
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Slice CV 2";
      r.padIndex = -1; r.paramIndex = 8; configRows_.add(r); }

    // ── Spacer between groups ──
    { ConfigRow s; s.type = ConfigRowType::Spacer; s.padIndex = -1; s.paramIndex = -1;
      configRows_.add(s); }

    // ── Preset group ──
    // 1 = Preset Switch
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Preset Switch";
      r.padIndex = -1; r.paramIndex = 1; configRows_.add(r); }

    // ── Spacer ──
    { ConfigRow s; s.type = ConfigRowType::Spacer; s.padIndex = -1; s.paramIndex = -1;
      configRows_.add(s); }

    // ── System group ──
    // 5 = Encoder Speed
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Enc Speed";
      r.padIndex = -1; r.paramIndex = 5; configRows_.add(r); }
    // 3 = Debug Msgs
    { ConfigRow r; r.type = ConfigRowType::Enum; r.label = "Debug Msgs";
      r.padIndex = -1; r.paramIndex = 3; configRows_.add(r); }
    // 4 = Reboot Plugin
    { ConfigRow r; r.type = ConfigRowType::PushAction; r.label = "Reboot Plugin";
      r.padIndex = -1; r.paramIndex = 4; configRows_.add(r); }
}

int PluginEditor::configSelectableCount() const
{
    int count = 0;
    for (auto& row : configRows_)
        if (row.type != ConfigRowType::Header && row.type != ConfigRowType::Divider
            && row.type != ConfigRowType::Spacer)
            count++;
    return count;
}

int PluginEditor::configSelectableToVisual(int selIdx) const
{
    int sel = 0;
    for (int i = 0; i < configRows_.size(); ++i) {
        auto t = configRows_[i].type;
        if (t == ConfigRowType::Header || t == ConfigRowType::Divider || t == ConfigRowType::Spacer)
            continue;
        if (sel == selIdx) return i;
        sel++;
    }
    return 0;
}

int PluginEditor::configVisualToSelectable(int visualIdx) const
{
    int sel = 0;
    for (int i = 0; i < visualIdx && i < configRows_.size(); ++i) {
        auto t = configRows_[i].type;
        if (t != ConfigRowType::Header && t != ConfigRowType::Divider && t != ConfigRowType::Spacer)
            sel++;
    }
    return sel;
}

juce::String configGetValueText(const PluginProcessor& proc, const ConfigRow& row)
{
    if (row.type == ConfigRowType::CCValue && row.padIndex >= 0) {
        int cc = proc.getPadCCMap(row.padIndex).byIndex(row.paramIndex);
        return "CC " + juce::String(cc);
    }
    // Per-pad enum (CLK Beats, Lo-Fi)
    if (row.type == ConfigRowType::Enum && row.padIndex >= 0) {
        if (row.paramIndex == 5) {
            int beats = proc.getEngine().getSlot(row.padIndex).getClockBeats();
            if (beats < 4) return juce::String(beats) + (beats == 1 ? " Beat" : " Beats");
            return juce::String(beats / 4) + (beats == 4 ? " Bar" : " Bars") + " (" + juce::String(beats) + ")";
        }
        if (row.paramIndex == 8) {
            const char* names[] = { "OFF", "8-Bit", "12-Bit", "SP-1200", "MPC-60" };
            return names[static_cast<int>(proc.getEngine().getSlot(row.padIndex).getLofiMode())];
        }
    }
    if (row.type == ConfigRowType::Enum && row.padIndex < 0) {
        switch (row.paramIndex) {
            case 0: {  // Mute Mode
                const char* names[] = { "Immediate", "On Release", "On Bar" };
                return names[static_cast<int>(proc.getPerfMode())];
            }
            case 1: {  // Preset Switch
                const char* names[] = { "Immediate", "On Release", "On Bar" };
                return names[static_cast<int>(proc.getPresetSwitchMode())];
            }
            case 2:  // Queue Bars
                return juce::String(proc.getQueueBars()) + " bar" + (proc.getQueueBars() > 1 ? "s" : "");
            case 3:  // Debug Msgs
                return proc.getDebugMsgs() ? "ON" : "OFF";
            case 5:  // Encoder Speed
                return juce::String(proc.getEncoderSpeed(), 2) + "x";
            case 6: {  // Mute Fade
                float ms = proc.getMuteFadeMs();
                if (ms < 0.5f) return "OFF";
                return juce::String((int)ms) + "ms";
            }
            case 7: {  // Slice CV 1
                int p = proc.getSliceCVPad(0);
                return (p < 0) ? "OFF" : "Pad " + juce::String(p + 1);
            }
            case 8: {  // Slice CV 2
                int p = proc.getSliceCVPad(1);
                return (p < 0) ? "OFF" : "Pad " + juce::String(p + 1);
            }
            default: return "?";
        }
    }
    if (row.type == ConfigRowType::PushAction)
        return "[PUSH]";
    return "";
}

void PluginEditor::paintConfigBrowser(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Panel background
    g.setColour(juce::Colour(kConfigBg));
    g.fillRect(area);

    // Left border separator
    g.setColour(juce::Colour(0xFF333333));
    g.fillRect(area.getX(), area.getY(), 1, area.getHeight());

    auto content = area.reduced(14, 8);
    const int rowH = 32;
    const int visibleRows = std::max(1, content.getHeight() / rowH);

    // Scroll to keep selected row visible
    int selVisual = configSelectableToVisual(configIndex_);
    if (selVisual < configScrollOffset_) configScrollOffset_ = selVisual;
    if (selVisual >= configScrollOffset_ + visibleRows) configScrollOffset_ = selVisual - visibleRows + 1;
    configScrollOffset_ = juce::jlimit(0, std::max(0, configRows_.size() - visibleRows), configScrollOffset_);

    int selCount = 0;  // tracks which selectable row we're on
    for (int i = 0; i < visibleRows; ++i)
    {
        int idx = configScrollOffset_ + i;
        if (idx >= configRows_.size()) break;

        auto& row = configRows_.getReference(idx);
        auto rowRect = juce::Rectangle<int>(content.getX(), content.getY() + i * rowH,
                                             content.getWidth(), rowH);

        bool isSelectable = (row.type != ConfigRowType::Header && row.type != ConfigRowType::Divider
                              && row.type != ConfigRowType::Spacer);
        bool isSelected = isSelectable && (configVisualToSelectable(idx) == configIndex_);

        // ── Header row ──
        if (row.type == ConfigRowType::Header) {
            g.setColour(juce::Colour(kConfigHeader));
            g.setFont(18.0f);
            g.drawText(row.label, rowRect, juce::Justification::centred);
            g.setColour(juce::Colour(kConfigDivider));
            g.fillRect(rowRect.getX(), rowRect.getBottom() - 1, rowRect.getWidth(), 1);
            continue;
        }

        // ── Spacer row (visual gap between groups) ──
        if (row.type == ConfigRowType::Spacer) {
            continue;  // just empty space
        }

        // ── Divider row (centered GLOBAL with dashes on each side) ──
        if (row.type == ConfigRowType::Divider) {
            int divY = rowRect.getCentreY();
            // Measure text width
            g.setFont(13.0f);
            int textW = g.getCurrentFont().getStringWidth(row.label) + 16;
            int textX = rowRect.getCentreX() - textW / 2;
            // Dashes left of text
            g.setColour(juce::Colour(0x55FFFFFF));
            for (int dx = rowRect.getX(); dx < textX - 4; dx += 10)
                g.fillRect(dx, divY, 5, 1);
            // Dashes right of text
            for (int dx = textX + textW + 4; dx < rowRect.getRight(); dx += 10)
                g.fillRect(dx, divY, 5, 1);
            // GLOBAL text centered
            g.setColour(juce::Colour(0x88FFFFFF));
            g.drawText(row.label, textX, rowRect.getY(), textW, rowRect.getHeight(),
                       juce::Justification::centred);
            continue;
        }

        // ── Selectable row ──
        // Check if row should be greyed out (Queue Bars when both modes are Immediate)
        bool greyed = false;
        if (row.type == ConfigRowType::Enum && row.padIndex < 0 && row.paramIndex == 2) {
            greyed = (processor_.getPerfMode() == PerfMode::Immediate
                   && processor_.getPresetSwitchMode() == PerfMode::Immediate);
        }

        if (isSelected) {
            g.setColour(juce::Colour(kConfigSelBg).withAlpha(greyed ? 0.12f : 0.25f));
            g.fillRoundedRectangle(rowRect.toFloat(), 3.0f);
            g.setColour(juce::Colour(kConfigSelBg).withAlpha(greyed ? 0.3f : 1.0f));
            g.fillRoundedRectangle((float)rowRect.getX(), (float)rowRect.getY() + 4,
                                    3.0f, (float)rowRect.getHeight() - 8, 1.5f);
        }

        // Label (left)
        float labelAlpha = greyed ? 0.35f : 1.0f;
        g.setColour((isSelected ? juce::Colour(0xFFFFFFFF) : juce::Colour(kConfigLabel)).withAlpha(labelAlpha));
        g.setFont(16.0f);
        g.drawText(row.label, rowRect.withTrimmedLeft(10).withTrimmedRight(90),
                   juce::Justification::centredLeft);

        // Value (right)
        juce::String valText = configGetValueText(processor_, row);
        bool editing = isSelected && configEditMode_;
        float valAlpha = greyed ? 0.35f : 1.0f;
        g.setColour((editing ? juce::Colour(kConfigEditVal) : juce::Colour(kConfigValue)).withAlpha(valAlpha));
        g.setFont(editing ? 17.0f : 15.0f);
        g.drawText(valText, rowRect.withTrimmedRight(6), juce::Justification::centredRight);
    }

    // Scroll indicators
    if (configScrollOffset_ > 0) {
        g.setColour(juce::Colour(0x66FFFFFF));
        g.setFont(14.0f);
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb2"), content.getX(), content.getY() - 16,
                   content.getWidth(), 14, juce::Justification::centredRight);
    }
    if (configScrollOffset_ + visibleRows < configRows_.size()) {
        g.setColour(juce::Colour(0x66FFFFFF));
        g.setFont(14.0f);
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xbc"),
                   content.getX(), content.getBottom() + 2,
                   content.getWidth(), 14, juce::Justification::centredRight);
    }
}

void PluginEditor::configAdjustValue(int selIdx, int delta)
{
    int visIdx = configSelectableToVisual(selIdx);
    if (visIdx < 0 || visIdx >= configRows_.size()) return;
    auto& row = configRows_.getReference(visIdx);

    if (row.type == ConfigRowType::CCValue && row.padIndex >= 0) {
        auto& ccMap = processor_.getPadCCMap(row.padIndex);
        int& cc = ccMap.byIndex(row.paramIndex);
        cc = juce::jlimit(0, 127, cc + delta);
        return;
    }

    // Per-pad CLK Beats (paramIndex 5)
    if (row.type == ConfigRowType::Enum && row.padIndex >= 0 && row.paramIndex == 5) {
        auto& slot = processor_.getEngine().getSlot(row.padIndex);
        int cur = slot.getClockBeats();
        const int steps[] = { 1, 2, 4, 8, 16 };
        int idx = 0;
        for (int s = 0; s < 5; ++s) if (steps[s] == cur) { idx = s; break; }
        idx = juce::jlimit(0, 4, idx + delta);
        slot.setClockBeats(steps[idx]);
        return;
    }

    // Per-pad Lo-Fi mode (paramIndex 8)
    if (row.type == ConfigRowType::Enum && row.padIndex >= 0 && row.paramIndex == 8) {
        auto& slot = processor_.getEngine().getSlot(row.padIndex);
        int v = static_cast<int>(slot.getLofiMode()) + delta;
        slot.setLofiMode(static_cast<LofiMode>(juce::jlimit(0, 4, v)));
        return;
    }

    if (row.type == ConfigRowType::Enum && row.padIndex < 0) {
        switch (row.paramIndex) {
            case 0: {  // Mute Mode
                int v = static_cast<int>(processor_.getPerfMode()) + delta;
                processor_.setPerfMode(static_cast<PerfMode>(juce::jlimit(0, 2, v)));
                break;
            }
            case 1: {  // Preset Switch
                int v = static_cast<int>(processor_.getPresetSwitchMode()) + delta;
                processor_.setPresetSwitchMode(static_cast<PerfMode>(juce::jlimit(0, 2, v)));
                break;
            }
            case 2: {  // Queue Bars
                processor_.setQueueBars(processor_.getQueueBars() + delta);
                break;
            }
            case 3: {  // Debug Msgs
                processor_.setDebugMsgs(delta > 0);
                break;
            }
            case 5: {  // Encoder Speed
                float s = processor_.getEncoderSpeed() + (float)delta * 0.25f;
                processor_.setEncoderSpeed(s);
                break;
            }
            case 6: {  // Mute Fade
                float ms = processor_.getMuteFadeMs() + (float)delta * 25.0f;
                processor_.setMuteFadeMs(ms);
                break;
            }
            case 7: {  // Slice CV 1
                int p = processor_.getSliceCVPad(0) + delta;
                processor_.setSliceCVPad(0, juce::jlimit(-1, 7, p));
                break;
            }
            case 8: {  // Slice CV 2
                int p = processor_.getSliceCVPad(1) + delta;
                processor_.setSliceCVPad(1, juce::jlimit(-1, 7, p));
                break;
            }
            default: break;
        }
    }
}

void PluginEditor::configPushValue(int selIdx)
{
    int visIdx = configSelectableToVisual(selIdx);
    if (visIdx < 0 || visIdx >= configRows_.size()) return;
    auto& row = configRows_.getReference(visIdx);

    if (row.type == ConfigRowType::PushAction) {
        if (row.paramIndex == 4) {  // Reboot
            processor_.rebootPlugin();
            exitConfigMode();
        }
        return;
    }

    // Toggle edit mode for CC values and enums
    if (row.type == ConfigRowType::CCValue || row.type == ConfigRowType::Enum) {
        configEditMode_ = !configEditMode_;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Popup Modal (Octatrack-style centered dialog)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::showPopup(const juce::String& title, const juce::StringArray& options,
                              std::function<void(int)> callback)
{
    popupTitle_ = title;
    popupOptions_ = options;
    popupCallback_ = std::move(callback);
    popupIndex_ = 0;
    popupMode_ = true;
    repaint();
}

void PluginEditor::closePopup(int result)
{
    popupMode_ = false;
    leftShiftHeld_ = false;
    auto cb = std::move(popupCallback_);
    popupCallback_ = nullptr;
    repaint();
    if (cb) cb(result);
}

void PluginEditor::paintPopup(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Dark overlay
    g.setColour(juce::Colour(0xBB000000));
    g.fillRect(area);

    // Centered box
    int boxW = 380;
    int rowH = 40;
    int boxH = 52 + (int)popupOptions_.size() * rowH + 32;  // extra room for hint
    int boxX = (area.getWidth() - boxW) / 2;
    int boxY = (area.getHeight() - boxH) / 2;
    auto box = juce::Rectangle<int>(boxX, boxY, boxW, boxH);

    // Box background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(box.toFloat(), 8.0f);
    // Box border
    g.setColour(juce::Colour(kTabActive).withAlpha(0.5f));
    g.drawRoundedRectangle(box.toFloat(), 8.0f, 2.0f);

    // Title
    g.setColour(juce::Colour(kTabActive));
    g.setFont(22.0f);
    g.drawText(popupTitle_, box.getX(), box.getY() + 10, box.getWidth(), 32,
               juce::Justification::centred);

    // Separator under title
    g.setColour(juce::Colour(0x44FFFFFF));
    g.fillRect(box.getX() + 20, box.getY() + 46, box.getWidth() - 40, 1);

    // Options
    int optY = box.getY() + 52;
    for (int i = 0; i < popupOptions_.size(); ++i) {
        auto optRect = juce::Rectangle<int>(box.getX() + 16, optY + i * rowH,
                                             box.getWidth() - 32, rowH);
        bool sel = (i == popupIndex_);
        if (sel) {
            g.setColour(juce::Colour(kTabActive).withAlpha(0.2f));
            g.fillRoundedRectangle(optRect.toFloat(), 4.0f);
            // Left indicator bar
            g.setColour(juce::Colour(kTabActive));
            g.fillRoundedRectangle((float)optRect.getX(), (float)optRect.getY() + 6,
                                    3.0f, (float)optRect.getHeight() - 12, 1.5f);
        }
        g.setColour(sel ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFFAAAAAA));
        g.setFont(20.0f);
        g.drawText(popupOptions_[i], optRect.withTrimmedLeft(14),
                   juce::Justification::centredLeft);
    }

    // Hint at bottom
    g.setColour(juce::Colour(0x44FFFFFF));
    g.setFont(12.0f);
    g.drawText("LS = select    LEFT = cancel", box.getX(), box.getBottom() - 20,
               box.getWidth(), 16, juce::Justification::centred);
}

// ═══════════════════════════════════════════════════════════════════════════
// Kit Browsing (up/down arrows, 3s timeout, left shift commits)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::refreshAvailableKits()
{
    availableKits_ = processor_.getAvailableKits();
}

void PluginEditor::kitBrowseCommit()
{
    if (!kitBrowseActive_ || kitBrowseIndex_ < 0 || kitBrowseIndex_ >= availableKits_.size()) {
        kitBrowseActive_ = false;
        return;
    }
    processor_.loadKit(availableKits_[kitBrowseIndex_]);
    kitCurrentIndex_ = kitBrowseIndex_;
    kitBrowseActive_ = false;
    repaint();
}

void PluginEditor::kitBrowseRevert()
{
    kitBrowseActive_ = false;
    processor_.showTickerPublic("Browse cancelled");
    repaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// On-screen Keyboard (name entry for kits/stacks)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::showNameEntryPopup(const juce::String& title,
                                       std::function<void(const juce::String&)> callback)
{
    showKeyboard(title, std::move(callback));
}

void PluginEditor::showKeyboard(const juce::String& title,
                                 std::function<void(const juce::String&)> callback)
{
    keyboardMode_ = true;
    keyboardTitle_ = title;
    keyboardText_.clear();
    keyboardRow_ = 0;
    keyboardCol_ = 0;
    keyboardCallback_ = std::move(callback);
    repaint();
}

void PluginEditor::closeKeyboard()
{
    keyboardMode_ = false;
    leftShiftHeld_ = false;
    keyboardCallback_ = nullptr;
    repaint();
}

int PluginEditor::keyboardRowLen(int row) const
{
    switch (row) {
        case 0: return 13;  // A-M
        case 1: return 13;  // N-Z
        case 2: return 12;  // 0-9 _ ←
        case 3: return 2;   // GENERATE, SAVE
        default: return 1;
    }
}

char PluginEditor::keyboardCharAt(int row, int col) const
{
    if (row == 0 && col >= 0 && col < 13) return 'A' + col;
    if (row == 1 && col >= 0 && col < 13) return 'N' + col;
    if (row == 2) {
        if (col >= 0 && col < 10) return '0' + col;
        if (col == 10) return '_';
        if (col == 11) return '\b';  // backspace
    }
    return 0;  // buttons
}

void PluginEditor::keyboardAction()
{
    if (keyboardRow_ == 3) {
        // Button row
        if (keyboardCol_ == 0) {
            // GENERATE — fill with random name
            keyboardText_ = generateRandomName(nameRng_);
            // Replace spaces with underscores, enforce limits
            keyboardText_ = keyboardText_.replaceCharacter(' ', '_').toLowerCase();
            if (keyboardText_.length() > kKeyboardMaxLen)
                keyboardText_ = keyboardText_.substring(0, kKeyboardMaxLen);
        } else {
            // SAVE
            if (keyboardText_.length() >= kKeyboardMinLen) {
                auto cb = std::move(keyboardCallback_);
                auto text = keyboardText_;
                closeKeyboard();
                if (cb) cb(text);
            } else {
                processor_.showTickerPublic("Name too short (min " + juce::String(kKeyboardMinLen) + " chars)");
            }
        }
    } else {
        // Character key
        char c = keyboardCharAt(keyboardRow_, keyboardCol_);
        if (c == '\b') {
            // Backspace
            if (keyboardText_.isNotEmpty())
                keyboardText_ = keyboardText_.dropLastCharacters(1);
        } else if (c != 0 && keyboardText_.length() < kKeyboardMaxLen) {
            keyboardText_ += juce::String::charToString((juce::juce_wchar)c).toLowerCase();
        }
    }
    repaint();
}

void PluginEditor::paintKeyboard(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Dark overlay
    g.setColour(juce::Colour(0xCC000000));
    g.fillRect(area);

    // Centered box
    int boxW = 520;
    int boxH = 290;
    int boxX = (area.getWidth() - boxW) / 2;
    int boxY = (area.getHeight() - boxH) / 2;
    auto box = juce::Rectangle<int>(boxX, boxY, boxW, boxH);

    // Box background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(box.toFloat(), 8.0f);
    g.setColour(juce::Colour(kTabActive).withAlpha(0.5f));
    g.drawRoundedRectangle(box.toFloat(), 8.0f, 2.0f);

    // Title
    g.setColour(juce::Colour(kTabActive));
    g.setFont(20.0f);
    g.drawText(keyboardTitle_, box.getX(), box.getY() + 8, box.getWidth(), 24,
               juce::Justification::centred);

    // Text field
    auto fieldRect = juce::Rectangle<int>(box.getX() + 20, box.getY() + 38, box.getWidth() - 40, 30);
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRoundedRectangle(fieldRect.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xFF444444));
    g.drawRoundedRectangle(fieldRect.toFloat(), 4.0f, 1.0f);
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.setFont(20.0f);
    juce::String displayText = keyboardText_ + "|";  // cursor
    g.drawText(displayText, fieldRect.reduced(8, 0), juce::Justification::centredLeft);

    // Character count
    g.setColour(juce::Colour(keyboardText_.length() >= kKeyboardMinLen ? 0xFF4CAF50 : 0xFF888888));
    g.setFont(13.0f);
    g.drawText(juce::String(keyboardText_.length()) + "/" + juce::String(kKeyboardMaxLen),
               fieldRect, juce::Justification::centredRight);

    // Key grid
    int keyW = 34;
    int keyH = 30;
    int keyGap = 3;
    int gridStartY = box.getY() + 78;

    for (int row = 0; row < 4; ++row) {
        int rowLen = keyboardRowLen(row);
        int rowW = (row < 3) ? rowLen * (keyW + keyGap) - keyGap : boxW - 40;
        int rowX = box.getX() + (box.getWidth() - rowW) / 2;
        int rowY = gridStartY + row * (keyH + keyGap + 2);

        if (row < 3) {
            // Character keys
            for (int col = 0; col < rowLen; ++col) {
                auto keyRect = juce::Rectangle<int>(rowX + col * (keyW + keyGap), rowY, keyW, keyH);
                bool sel = (keyboardRow_ == row && keyboardCol_ == col);

                if (sel) {
                    g.setColour(juce::Colour(kTabActive));
                    g.fillRoundedRectangle(keyRect.toFloat(), 4.0f);
                    g.setColour(juce::Colour(0xFFFFFFFF));
                } else {
                    g.setColour(juce::Colour(0xFF2A2A2A));
                    g.fillRoundedRectangle(keyRect.toFloat(), 4.0f);
                    g.setColour(juce::Colour(0xFFCCCCCC));
                }

                char c = keyboardCharAt(row, col);
                juce::String label;
                if (c == '\b') label = juce::CharPointer_UTF8("\xe2\x86\x90");  // ← arrow
                else label = juce::String::charToString((juce::juce_wchar)c);

                g.setFont(16.0f);
                g.drawText(label, keyRect, juce::Justification::centred);
            }
        } else {
            // Button row: GENERATE and SAVE
            int btnW = (boxW - 60) / 2;
            int btnGap = 20;
            int btnX = box.getX() + 20;

            for (int col = 0; col < 2; ++col) {
                auto btnRect = juce::Rectangle<int>(btnX + col * (btnW + btnGap), rowY, btnW, keyH + 4);
                bool sel = (keyboardRow_ == 3 && keyboardCol_ == col);

                if (sel) {
                    g.setColour(juce::Colour(kTabActive));
                    g.fillRoundedRectangle(btnRect.toFloat(), 5.0f);
                    g.setColour(juce::Colour(0xFFFFFFFF));
                } else {
                    g.setColour(juce::Colour(0xFF333333));
                    g.fillRoundedRectangle(btnRect.toFloat(), 5.0f);
                    g.setColour(juce::Colour(0xFFAAAAAA));
                }

                g.setFont(18.0f);
                g.drawText(col == 0 ? "GENERATE" : "SAVE", btnRect, juce::Justification::centred);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Slice Editor (full-screen overlay, Octatrack-style)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::enterSliceEditor()
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);
    if (!slot.isLoaded()) {
        processor_.showTickerPublic("No sample loaded");
        return;
    }
    sliceEditorMode_ = true;
    sliceCursorPos_ = slot.getStartPos();
    sliceZoom_ = 1.0f;
    sliceViewCenter_ = 0.5f;
    sliceAutoPreset_ = 0;
    // Encoder bar shows slice editor controls
    encoderSlots_[0].nameLabel.setText("CURSOR", juce::dontSendNotification);
    encoderSlots_[0].valueLabel.setText("[CUT]", juce::dontSendNotification);
    encoderSlots_[1].nameLabel.setText("ZOOM", juce::dontSendNotification);
    encoderSlots_[1].valueLabel.setText("1x", juce::dontSendNotification);
    encoderSlots_[2].nameLabel.setText("AUTO", juce::dontSendNotification);
    encoderSlots_[2].valueLabel.setText("OFF", juce::dontSendNotification);
    encoderSlots_[3].nameLabel.setText("PITCH", juce::dontSendNotification);
    encoderSlots_[3].valueLabel.setText("0st", juce::dontSendNotification);
    processor_.showTickerPublic("Slice Editor - Pad " + juce::String(selectedPad_ + 1));
    repaint();
}

void PluginEditor::exitSliceEditor()
{
    sliceEditorMode_ = false;
    leftShiftHeld_ = false;
    updateEncoderDisplay();
    repaint();
}

void PluginEditor::paintSliceEditor(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);
    if (!slot.isLoaded()) { exitSliceEditor(); return; }

    const int w = area.getWidth(), h = area.getHeight();

    // ── Semi-transparent backdrop (tabs still visible) ──────────────────
    g.setColour(juce::Colour(0xCC000000));
    g.fillRect(area);

    // ── Popup box: compact, well-proportioned ─────────────────────────
    const int boxL = 60, boxR = 60;
    const int boxT = kTabHeight + 6;
    const int boxB = kEncoderBarH + 6;
    auto box = juce::Rectangle<int>(boxL, boxT, w - boxL - boxR, h - boxT - boxB);

    // Box background + border
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(box.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xFF333333));
    g.drawRoundedRectangle(box.toFloat(), 8.0f, 1.5f);

    auto inner = box.reduced(12, 4);

    // ── Single-line header: all info on one row at 18pt ─────────────────
    juce::String fname = slot.getFileName();
    if (fname.contains(".")) fname = fname.upToLastOccurrenceOf(".", false, false);

    int sc = slot.getSliceCount();
    int totalReg = sc + 1;
    int hdrCursorSlice = 0;
    for (int i = 0; i < sc; ++i)
        if (sliceCursorPos_ >= slot.getSlicePoint(i)) hdrCursorSlice = i + 1;
    float cvNorm = (totalReg > 1) ? (float)hdrCursorSlice / (float)(totalReg - 1) : 0.0f;
    float cvVolts = cvNorm * 10.0f - 5.0f;
    int ccNum = processor_.getPadCCMap(selectedPad_).ccStart;

    // Left: pad + slice info
    g.setColour(juce::Colour(0xFFDDDDDD));
    g.setFont(18.0f);
    juce::String leftInfo = "P" + juce::String(selectedPad_ + 1) + "  "
        + juce::String(hdrCursorSlice + 1) + "/" + juce::String(totalReg)
        + "  CV" + (cvVolts >= 0 ? "+" : "") + juce::String(cvVolts, 1)
        + "  CC" + juce::String(ccNum);
    g.drawText(leftInfo, inner.getX() + 4, inner.getY(), inner.getWidth() / 2, 24,
               juce::Justification::centredLeft);

    // Right: filename + count
    g.setColour(juce::Colour(0xFF999999));
    g.setFont(16.0f);
    g.drawText(fname + "  " + juce::String(totalReg) + " slices",
               inner.getX() + inner.getWidth() / 2, inner.getY(),
               inner.getWidth() / 2 - 4, 24, juce::Justification::centredRight);

    // Separator
    g.setColour(juce::Colour(0xFF2A2A2A));
    g.fillRect(inner.getX(), inner.getY() + 26, inner.getWidth(), 1);

    // ── Waveform area ───────────────────────────────────────────────────
    auto wfArea = inner.withTrimmedTop(30).withTrimmedBottom(20);

    // Waveform background
    g.setColour(juce::Colour(0xFF0E0E0E));
    g.fillRoundedRectangle(wfArea.toFloat(), 4.0f);

    // Compute visible range
    float halfSpan = 0.5f / sliceZoom_;
    float viewStart = juce::jlimit(0.0f, std::max(0.0f, 1.0f - 2.0f * halfSpan), sliceViewCenter_ - halfSpan);
    float viewEnd = viewStart + 2.0f * halfSpan;
    float viewSpan = viewEnd - viewStart;

    // ── Overview strip (when zoomed) ────────────────────────────────────
    if (sliceZoom_ > 1.5f) {
        int overH = 14;
        auto overArea = juce::Rectangle<int>(wfArea.getX(), wfArea.getY(), wfArea.getWidth(), overH);
        const float* data = slot.getBuffer().getReadPointer(0);
        int total = slot.getNumSamples();
        float cy = (float)overArea.getCentreY();
        float amp = (float)overArea.getHeight() * 0.4f;
        for (int px = 0; px < overArea.getWidth(); ++px) {
            float norm = (float)px / (float)overArea.getWidth();
            int s0 = (int)(norm * total);
            int s1 = std::min(s0 + std::max(1, total / overArea.getWidth()), total);
            float mn = 0, mx = 0;
            for (int s = s0; s < s1; ++s) { if (data[s] < mn) mn = data[s]; if (data[s] > mx) mx = data[s]; }
            g.setColour(juce::Colour(0xFF1A2A1A));
            float ty = cy - mx * amp, by = cy - mn * amp;
            if (by - ty < 1) { ty = cy - 0.5f; by = cy + 0.5f; }
            g.fillRect((float)(overArea.getX() + px), ty, 1.0f, by - ty);
        }
        // Slice lines on overview (thin white)
        for (int i = 0; i < slot.getSliceCount(); ++i) {
            float pt = slot.getSlicePoint(i);
            float px = (float)overArea.getX() + pt * (float)overArea.getWidth();
            g.setColour(juce::Colour(0x55FFFFFF));
            g.fillRect(px, (float)overArea.getY(), 1.0f, (float)overH);
        }
        // Viewport indicator
        int vpX = overArea.getX() + (int)(viewStart * overArea.getWidth());
        int vpW = std::max(4, (int)(viewSpan * overArea.getWidth()));
        g.setColour(juce::Colour(0x22FFFFFF));
        g.fillRect(vpX, overArea.getY(), vpW, overH);
        g.setColour(juce::Colour(0x44FFFFFF));
        g.drawRect(vpX, overArea.getY(), vpW, overH, 1);

        wfArea = wfArea.withTrimmedTop(overH + 3);
    }

    // ── Main waveform ───────────────────────────────────────────────────
    {
        const float* data = slot.getBuffer().getReadPointer(0);
        int total = slot.getNumSamples();
        float cy = (float)wfArea.getCentreY();
        float amp = (float)wfArea.getHeight() * 0.43f;
        float wfW = (float)wfArea.getWidth();

        // Center line
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.drawHorizontalLine((int)cy, (float)wfArea.getX(), (float)wfArea.getRight());

        for (int px = 0; px < (int)wfW; ++px) {
            float norm = viewStart + ((float)px / wfW) * viewSpan;
            int s0 = juce::jlimit(0, total - 1, (int)(norm * total));
            int s1 = juce::jlimit(s0 + 1, total, (int)((norm + viewSpan / wfW) * total));
            float mn = 0, mx = 0;
            for (int s = s0; s < s1; ++s) { if (data[s] < mn) mn = data[s]; if (data[s] > mx) mx = data[s]; }

            bool inside = (norm >= slot.getStartPos() && norm <= slot.getEndPos());
            float peak = std::max(std::abs(mx), std::abs(mn));
            juce::Colour col;
            if (!inside) {
                col = juce::Colour(0xFF0A0A0A);
            } else {
                col = juce::Colour(kWfGreen);
                if (peak > 0.20f) col = col.interpolatedWith(juce::Colour(kWfYellow), std::min((peak - 0.20f) / 0.35f, 1.0f));
                if (peak > 0.55f) col = col.interpolatedWith(juce::Colour(kWfRed), std::min((peak - 0.55f) / 0.30f, 1.0f));
            }
            g.setColour(col);
            float ty = cy - mx * amp, by = cy - mn * amp;
            if (by - ty < 1) { ty = cy - 0.5f; by = cy + 0.5f; }
            g.fillRect((float)(wfArea.getX() + px), ty, 1.0f, by - ty);
        }
    }

    // Helper: normalize-to-pixel
    auto normToPx = [&](float n) -> float {
        return (float)wfArea.getX() + ((n - viewStart) / viewSpan) * (float)wfArea.getWidth();
    };

    // ── Determine which slice region the cursor is in ─────────────────
    int cursorSlice = 0;
    for (int i = 0; i < slot.getSliceCount(); ++i)
        if (sliceCursorPos_ >= slot.getSlicePoint(i)) cursorSlice = i + 1;
    int totalRegions = slot.getSliceCount() + 1;

    // ── Slice region rendering: active = white wash + dark numbers ───
    //    inactive = no wash, white numbers at top ─────────────────────
    {
        g.saveState();
        g.reduceClipRegion(wfArea);

        float prevPt = slot.getStartPos();
        for (int i = 0; i <= slot.getSliceCount(); ++i) {
            float nextPt = (i < slot.getSliceCount()) ? slot.getSlicePoint(i) : slot.getEndPos();
            if (nextPt < viewStart || prevPt > viewEnd) { prevPt = nextPt; continue; }

            float x1 = normToPx(std::max(prevPt, viewStart));
            float x2 = normToPx(std::min(nextPt, viewEnd));
            float regionW = x2 - x1;
            bool active = (i == cursorSlice);

            if (active) {
                // Active region: softer white wash — visible but not blinding
                g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(0.15f));
                g.fillRect(x1, (float)wfArea.getY(), regionW, (float)wfArea.getHeight());

                // Dark cutout number at top — BIG and readable, with pitch arrow
                if (regionW > 12.0f) {
                    float fontSize = juce::jlimit(16.0f, 32.0f, regionW * 0.5f);
                    g.setColour(juce::Colour(0xFF0D0D0D).withAlpha(0.8f));
                    g.setFont(fontSize);
                    juce::String numStr = juce::String(i + 1);
                    float p = slot.getSlicePitch(i);
                    if (p > 0.1f) numStr += juce::String(juce::CharPointer_UTF8("\xe2\x86\x91"));  // ↑
                    else if (p < -0.1f) numStr += juce::String(juce::CharPointer_UTF8("\xe2\x86\x93"));  // ↓
                    g.drawText(numStr,
                               (int)x1, wfArea.getY() + 3, (int)regionW, (int)fontSize + 4,
                               juce::Justification::centred);
                }
            } else {
                // Inactive: no fill, white number at top — with pitch arrow
                if (regionW > 12.0f) {
                    float fontSize = juce::jlimit(14.0f, 26.0f, regionW * 0.45f);
                    g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(0.3f));
                    g.setFont(fontSize);
                    juce::String numStr = juce::String(i + 1);
                    float p = slot.getSlicePitch(i);
                    if (p > 0.1f) numStr += juce::String(juce::CharPointer_UTF8("\xe2\x86\x91"));
                    else if (p < -0.1f) numStr += juce::String(juce::CharPointer_UTF8("\xe2\x86\x93"));
                    g.drawText(numStr,
                               (int)x1, wfArea.getY() + 3, (int)regionW, (int)fontSize + 4,
                               juce::Justification::centred);
                }
            }

            prevPt = nextPt;
        }
        g.restoreState();
    }

    // ── Slice lines (white, clean) ──────────────────────────────────────
    for (int i = 0; i < slot.getSliceCount(); ++i) {
        float pt = slot.getSlicePoint(i);
        if (pt < viewStart || pt > viewEnd) continue;
        float px = normToPx(pt);

        // Vertical line
        g.setColour(juce::Colour(0xBBFFFFFF));
        g.fillRect(px - 0.5f, (float)wfArea.getY(), 1.5f, (float)wfArea.getHeight());

        // Small notch at top
        g.setColour(juce::Colour(0xDDFFFFFF));
        g.fillRect(px - 3.0f, (float)wfArea.getY(), 7.0f, 2.0f);
    }

    // ── Start/End markers (subtle blue) ─────────────────────────────────
    auto drawMarker = [&](float pos) {
        if (pos < viewStart || pos > viewEnd) return;
        float px = normToPx(pos);
        g.setColour(juce::Colour(0xFF42A5F5).withAlpha(0.5f));
        g.fillRect(px - 0.5f, (float)wfArea.getY(), 1.5f, (float)wfArea.getHeight());
    };
    drawMarker(slot.getStartPos());
    drawMarker(slot.getEndPos());

    // ── Playhead (green) ────────────────────────────────────────────────
    if (slot.isPlaying()) {
        float pos = slot.getPlaybackPosition();
        if (pos >= viewStart && pos <= viewEnd) {
            float px = normToPx(pos);
            g.setColour(juce::Colour(kPadPlaying).withAlpha(0.8f));
            g.fillRect(px - 0.5f, (float)wfArea.getY(), 2.0f, (float)wfArea.getHeight());
        }
    }

    // ── Cursor (bright white, thin, with diamond head) ──────────────────
    if (sliceCursorPos_ >= viewStart && sliceCursorPos_ <= viewEnd) {
        float px = normToPx(sliceCursorPos_);
        float top = (float)wfArea.getY();
        float bot = (float)wfArea.getBottom();

        // Thin cursor line
        g.setColour(juce::Colour(0xFFFFFFFF));
        g.fillRect(px - 0.5f, top + 8.0f, 1.0f, bot - top - 8.0f);

        // Diamond head at top
        juce::Path diamond;
        diamond.addTriangle(px - 5.0f, top + 8.0f, px + 5.0f, top + 8.0f, px, top);
        diamond.addTriangle(px - 5.0f, top + 8.0f, px + 5.0f, top + 8.0f, px, top + 16.0f);
        g.fillPath(diamond);
    }

    // ── Footer hints (clean, no symbols) ────────────────────────────────
    auto footerArea = inner.withTop(inner.getBottom() - 18);
    g.setColour(juce::Colour(0xFF444444));
    g.setFont(12.0f);
    g.drawText("PUSH cut    L/R slices    LS audition    RS exit",
               footerArea, juce::Justification::centred);

    // ── Update encoder bar values ───────────────────────────────────────
    encoderSlots_[0].valueLabel.setText(juce::String(sliceCursorPos_ * 100.0f, 1) + "%", juce::dontSendNotification);
    encoderSlots_[1].valueLabel.setText(juce::String(sliceZoom_, 1) + "x", juce::dontSendNotification);
    if (sliceAutoPreset_ == 0)
        encoderSlots_[2].valueLabel.setText("OFF", juce::dontSendNotification);
    else
        encoderSlots_[2].valueLabel.setText(sliceAutoName(sliceAutoPreset_), juce::dontSendNotification);
    // Per-slice pitch for current region
    float curPitch = slot.getSlicePitch(cursorSlice);
    if (std::abs(curPitch) < 0.05f)
        encoderSlots_[3].valueLabel.setText("0st", juce::dontSendNotification);
    else
        encoderSlots_[3].valueLabel.setText((curPitch > 0 ? "+" : "") + juce::String(curPitch, 1) + "st", juce::dontSendNotification);
}

} // namespace grid
