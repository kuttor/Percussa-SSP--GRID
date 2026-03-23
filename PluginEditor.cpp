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
        slot.nameLabel.setFont(juce::Font(13.0f));
        addAndMakeVisible(slot.nameLabel);

        slot.valueLabel.setColour(juce::Label::textColourId, juce::Colour(kEncValue));
        slot.valueLabel.setJustificationType(juce::Justification::centred);
        slot.valueLabel.setFont(juce::Font(16.0f));
        addAndMakeVisible(slot.valueLabel);
    }

    // statusLabel_ not used - pad specs painted directly in paint()
    statusLabel_.setVisible(false);

    updateEncoderDisplay();
    startTimerHz(30);
}

PluginEditor::~PluginEditor() { stopTimer(); }

void PluginEditor::resized()
{
    const int w = getWidth();
    const int encY = getHeight() - kEncoderBarH;
    const int encW = w / kEncodersPerPage;

    for (int i = 0; i < kEncodersPerPage; ++i)
    {
        int x = i * encW;
        encoderSlots_[i].nameLabel.setBounds(x, encY, encW, 20);
        encoderSlots_[i].valueLabel.setBounds(x, encY + 18, encW, 28);
    }

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

    const char* pages[] = { "PADS", "SAMPLE", "PLAY", "WARP" };
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
        g.setFont(16.0f);
        g.drawText(pages[i], tx, 0, tabW, kTabHeight, juce::Justification::centred);
    }

    // Thin separator line under tab bar (except under active tab)
    g.setColour(juce::Colour(0xFF222222));
    g.fillRect(0, kTabHeight - 1, w, 1);

    // ── Status ───────────────────────────────────────────────────────────
    // Show selected pad specs in red at top right
    auto& selSlot = processor_.getEngine().getSlot(selectedPad_);
    g.setColour(juce::Colour(kTabActive));  // red
    g.setFont(14.0f);
    const char* modeStr[] = { "ONE-SHOT", "LOOP", "CLK LOOP", "CLK BAR" };
    juce::String specs = "PAD " + juce::String(selectedPad_ + 1);
    if (selSlot.isLoaded()) {
        specs += "  |  " + juce::String(modeStr[static_cast<int>(selSlot.getMode())]);
        specs += "  |  VOL " + juce::String((int)(selSlot.getVolume() * 100)) + "%";
        if (selSlot.getPan() != 0.0f)
            specs += "  |  PAN " + juce::String(selSlot.getPan(), 1);
        if (selSlot.getPitchSemitones() != 0.0f)
            specs += "  |  " + juce::String(selSlot.getPitchSemitones(), 1) + "st";
    }
    g.drawText(specs, w - 500, 10, 490, 18, juce::Justification::centredRight);

    // ── Encoder bar background ───────────────────────────────────────────
    int encY = h - kEncoderBarH;
    g.setColour(juce::Colour(kEncBarBg));
    g.fillRect(0, encY, w, kEncoderBarH);

    // ── Main content: ALWAYS show 8-pad grid ─────────────────────────────
    auto content = juce::Rectangle<int>(0, kTabHeight, w, encY - kTabHeight);

    if (browseMode_) {
        paintFileBrowser(g, content);
        return;
    }

    // Always paint the grid
    paintOverviewPage(g, content);
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

    // ── Progress fill: translucent red sweep within start/end region ─────
    if (playing && slot.isLoaded())
    {
        float regionStart = slot.getStartPos();
        float regionEnd = slot.getEndPos();
        float regionFrac = 0.0f;
        if (regionEnd > regionStart)
            regionFrac = juce::jlimit(0.0f, 1.0f, (pos - regionStart) / (regionEnd - regionStart));

        // Map region to box pixels
        float boxW = (float)box.getWidth();
        float startPx = regionStart * boxW;
        float endPx = regionEnd * boxW;
        float regionPxW = endPx - startPx;
        float fillW = regionFrac * regionPxW;

        g.saveState();
        g.reduceClipRegion(box);
        g.setColour(juce::Colour(0x40E53935));
        g.fillRect((float)box.getX() + startPx, (float)box.getY(), fillW, (float)box.getHeight());

        // Leading edge
        g.setColour(juce::Colour(0xAAE53935));
        g.fillRect((float)box.getX() + startPx + fillW - 2.0f, (float)box.getY(), 3.0f, (float)box.getHeight());
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

    // Pad number
    g.setColour(juce::Colour(playing ? kPadPlaying : kPadNumText));
    g.setFont(14.0f);
    g.drawText(juce::String(padIndex + 1), box.getX() + 8, box.getY() + 4, 20, 18,
               juce::Justification::topLeft);

    if (!slot.isLoaded())
    {
        g.setColour(juce::Colour(kPadEmpty));
        g.setFont(16.0f);
        g.drawText("- empty -", box, juce::Justification::centred);
        return;
    }

    // Filename (strip extension)
    g.setColour(juce::Colour(kPadText));
    g.setFont(14.0f);
    juce::String dispName = slot.getFileName();
    if (dispName.contains(".")) dispName = dispName.upToLastOccurrenceOf(".", false, false);
    g.drawText(dispName, box.getX() + 8, box.getY() + 20, box.getWidth() - 16, 18,
               juce::Justification::centredLeft);

    // Mini waveform
    auto wfArea = box.withTrimmedTop(42).withTrimmedBottom(8).reduced(8, 0);
    paintMiniWaveform(g, wfArea, slot);
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

    for (int px = 0; px < area.getWidth(); ++px)
    {
        int s0 = (px * total) / area.getWidth();
        int s1 = ((px + 1) * total) / area.getWidth();
        s1 = std::min(s1, total);
        float mn = 0, mx = 0;
        for (int s = s0; s < s1; ++s) { if (data[s] < mn) mn = data[s]; if (data[s] > mx) mx = data[s]; }

        float norm = (float)px / (float)area.getWidth();
        bool inside = (norm >= startN && norm <= endN);

        float peak = std::max(std::abs(mx), std::abs(mn));
        juce::Colour col;
        if (!inside) {
            col = juce::Colour(0xFF1A1A1A);  // dimmed outside region
        } else {
            col = juce::Colour(kWfGreen);
            if (peak > 0.55f) col = col.interpolatedWith(juce::Colour(kWfYellow), (peak - 0.55f) / 0.3f);
            if (peak > 0.85f) col = col.interpolatedWith(juce::Colour(kWfRed), (peak - 0.85f) / 0.15f);
        }
        g.setColour(col);

        float ty = cy - mx * amp;
        float by = cy - mn * amp;
        if (by - ty < 1) { ty = cy - 0.5f; by = cy + 0.5f; }
        g.fillRect((float)(area.getX() + px), ty, 1.0f, by - ty);
    }

    // Start/end marker lines
    float sx = (float)area.getX() + startN * (float)area.getWidth();
    float ex = (float)area.getX() + endN * (float)area.getWidth();
    g.setColour(juce::Colour(kWfRed));
    g.fillRect(sx, (float)area.getY(), 1.5f, (float)area.getHeight());
    g.fillRect(ex - 1.0f, (float)area.getY(), 1.5f, (float)area.getHeight());
}

// ═══════════════════════════════════════════════════════════════════════════
// Sample Page — Detail waveform for selected pad
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paintSamplePage(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);

    // Pad indicator
    g.setColour(juce::Colour(kTabActive));
    g.setFont(14.0f);
    g.drawText("PAD " + juce::String(selectedPad_ + 1), area.getX() + 12, area.getY() + 4, 100, 20,
               juce::Justification::centredLeft);

    if (!slot.isLoaded())
    {
        g.setColour(juce::Colour(kPadEmpty));
        g.setFont(28.0f);
        g.drawText("NO SAMPLE - Push FILE to browse", area, juce::Justification::centred);
        return;
    }

    g.setColour(juce::Colour(kPadText));
    g.setFont(15.0f);
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

    // Start/end markers
    float sx = area.getX() + slot.getStartPos() * area.getWidth();
    float ex = area.getX() + slot.getEndPos() * area.getWidth();
    g.setColour(juce::Colour(kWfRed));
    g.fillRect(sx, (float)area.getY(), 2.0f, (float)area.getHeight());
    g.fillRect(ex - 2, (float)area.getY(), 2.0f, (float)area.getHeight());

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
    g.setFont(14.0f);
    g.drawText("PAD " + juce::String(selectedPad_ + 1), area.getX() + 12, area.getY() + 4, 100, 20,
               juce::Justification::centredLeft);

    const char* modeNames[] = { "ONE-SHOT", "LOOP", "CLOCKED LOOP", "CLOCKED BAR" };
    int mode = static_cast<int>(slot.getMode());
    g.setColour(juce::Colour(kPadText));
    g.setFont(24.0f);
    g.drawText(modeNames[juce::jlimit(0, 3, mode)], area.withTrimmedTop(40), juce::Justification::centred);

    g.setFont(16.0f);
    g.drawText("Vol: " + juce::String((int)(slot.getVolume() * 100)) + "%   Pan: " +
               juce::String(slot.getPan(), 2), area.withTrimmedTop(80), juce::Justification::centred);
}

void PluginEditor::paintPitchPage(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& slot = processor_.getEngine().getSlot(selectedPad_);

    g.setColour(juce::Colour(kTabActive));
    g.setFont(15.0f);
    g.drawText("PAD " + juce::String(selectedPad_ + 1) + " WARP",
               area.getX() + 12, area.getY() + 4, 200, 20,
               juce::Justification::centredLeft);

    auto inner = area.withTrimmedTop(30).reduced(40, 10);

    // -- PITCH display --
    auto pitchArea = inner.withHeight(inner.getHeight() / 2);
    float pitchSt = slot.getPitchSemitones();

    g.setColour(juce::Colour(kEncLabel));
    g.setFont(14.0f);
    g.drawText("PITCH", pitchArea.getX(), pitchArea.getY(), 100, 20,
               juce::Justification::centredLeft);

    auto barRect = pitchArea.withTrimmedTop(24).withHeight(20);
    g.setColour(juce::Colour(0xFF222222));
    g.fillRoundedRectangle(barRect.toFloat(), 4.0f);

    float centerX = (float)barRect.getCentreX();
    g.setColour(juce::Colour(0xFF444444));
    g.fillRect(centerX - 0.5f, (float)barRect.getY(), 1.0f, (float)barRect.getHeight());

    float pitchFrac = pitchSt / 24.0f;
    float pStart = pitchFrac < 0 ? centerX + pitchFrac * (float)barRect.getWidth() * 0.5f : centerX;
    float pEnd = pitchFrac < 0 ? centerX : centerX + pitchFrac * (float)barRect.getWidth() * 0.5f;
    g.setColour(juce::Colour(kTabActive).withAlpha(0.7f));
    g.fillRect(pStart, (float)barRect.getY() + 2, pEnd - pStart, (float)barRect.getHeight() - 4);

    g.setColour(juce::Colour(kPadText));
    g.setFont(22.0f);
    juce::String pitchStr = (pitchSt >= 0 ? "+" : "") + juce::String(pitchSt, 1) + " st";
    g.drawText(pitchStr, pitchArea.withTrimmedTop(48), juce::Justification::centred);

    // -- TIME display --
    auto timeArea = inner.withTrimmedTop(inner.getHeight() / 2);
    float timeVal = slot.getTimeStretch();

    g.setColour(juce::Colour(kEncLabel));
    g.setFont(14.0f);
    g.drawText("TIME", timeArea.getX(), timeArea.getY(), 100, 20,
               juce::Justification::centredLeft);

    auto timeBar = timeArea.withTrimmedTop(24).withHeight(20);
    g.setColour(juce::Colour(0xFF222222));
    g.fillRoundedRectangle(timeBar.toFloat(), 4.0f);

    float timeCX = (float)timeBar.getCentreX();
    g.setColour(juce::Colour(0xFF444444));
    g.fillRect(timeCX - 0.5f, (float)timeBar.getY(), 1.0f, (float)timeBar.getHeight());

    float timeFrac = juce::jlimit(-1.0f, 1.0f, (timeVal - 1.0f));
    float tStart = timeFrac < 0 ? timeCX + timeFrac * (float)timeBar.getWidth() * 0.5f : timeCX;
    float tEnd = timeFrac < 0 ? timeCX : timeCX + timeFrac * (float)timeBar.getWidth() * 0.5f;
    g.setColour(juce::Colour(0xFF42A5F5).withAlpha(0.7f));
    g.fillRect(tStart, (float)timeBar.getY() + 2, tEnd - tStart, (float)timeBar.getHeight() - 4);

    g.setColour(juce::Colour(kPadText));
    g.setFont(22.0f);
    g.drawText(juce::String(timeVal, 2) + "x", timeArea.withTrimmedTop(48), juce::Justification::centred);

    g.setColour(juce::Colour(kPadEmpty));
    g.setFont(11.0f);
    g.drawText("PITCH = resampling (active)   TIME = stretch (coming soon)",
               area.getX(), area.getBottom() - 20, area.getWidth(), 16,
               juce::Justification::centred);
}

// ═══════════════════════════════════════════════════════════════════════════
// File Browser (from ELAS, adapted)
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::paintFileBrowser(juce::Graphics& g, juce::Rectangle<int> area)
{
    const int browserW = (int)(area.getWidth() * 0.35f);

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
    auto content = panel.reduced(14, 10);

    g.setColour(juce::Colour(kTabActive));
    g.setFont(15.0f);
    g.drawText(browseCurrentDir_.getFileName().isEmpty() ? "/" : browseCurrentDir_.getFileName(),
               content.removeFromTop(26), juce::Justification::centredLeft);

    g.setColour(juce::Colour(kEncLabel));
    g.setFont(11.0f);
    g.drawText("Turn=browse  Push=open  Enc2=back", content.removeFromTop(16), juce::Justification::centredLeft);
    content.removeFromTop(4);

    if (fileCount == 0) {
        g.setColour(juce::Colour(kPadEmpty));
        g.setFont(15.0f);
        g.drawText("No items found", content, juce::Justification::centred);
        return;
    }

    const int visibleRows = std::max(1, content.getHeight() / kBrowseRowHeight);
    if (browseIndex_ < browseScrollOffset_) browseScrollOffset_ = browseIndex_;
    if (browseIndex_ >= browseScrollOffset_ + visibleRows) browseScrollOffset_ = browseIndex_ - visibleRows + 1;
    browseScrollOffset_ = juce::jlimit(0, std::max(0, fileCount - visibleRows), browseScrollOffset_);

    for (int i = 0; i < visibleRows; ++i)
    {
        int idx = browseScrollOffset_ + i;
        if (idx >= fileCount) break;
        auto row = juce::Rectangle<int>(content.getX(), content.getY() + i * kBrowseRowHeight, content.getWidth(), kBrowseRowHeight);
        bool sel = (idx == browseIndex_);
        bool isDir = browseItems_[idx].isDirectory();
        if (sel) {
            g.setColour(juce::Colour(kBrowseSelBg));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
        }
        g.setColour(sel ? juce::Colour(0xFFFFFFFF) : juce::Colour(isDir ? kBrowseFolder : kBrowseText));
        g.setFont(16.0f);
        juce::String displayName = browseItemNames_[idx];
        if (!isDir && displayName.contains("."))
            displayName = displayName.upToLastOccurrenceOf(".", false, false);
        g.drawText(displayName, row.reduced(10, 0), juce::Justification::centredLeft);
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
    encoderSlots_[0].nameLabel.setText("BROWSE", juce::dontSendNotification);
    encoderSlots_[0].valueLabel.setText("Turn", juce::dontSendNotification);
    encoderSlots_[1].nameLabel.setText("BACK", juce::dontSendNotification);
    encoderSlots_[1].valueLabel.setText("[PUSH]", juce::dontSendNotification);
    encoderSlots_[2].nameLabel.setText("", juce::dontSendNotification);
    encoderSlots_[2].valueLabel.setText("", juce::dontSendNotification);
    encoderSlots_[3].nameLabel.setText("SELECT", juce::dontSendNotification);
    encoderSlots_[3].valueLabel.setText("[PUSH]", juce::dontSendNotification);
    repaint();
}

void PluginEditor::exitBrowseMode() { browseMode_ = false; updateEncoderDisplay(); repaint(); }

void PluginEditor::browseScanCurrentDir()
{
    browseItems_.clear(); browseItemNames_.clear();
    if (!browseCurrentDir_.isDirectory()) return;
    auto dirs = browseCurrentDir_.findChildFiles(juce::File::findDirectories, false); dirs.sort();
    for (auto& d : dirs) { browseItems_.add(d); browseItemNames_.add("[" + d.getFileName() + "]"); }
    auto files = browseCurrentDir_.findChildFiles(juce::File::findFiles, false, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF"); files.sort();
    for (auto& f : files) { browseItems_.add(f); browseItemNames_.add(f.getFileName()); }
    browseIndex_ = 0; browseScrollOffset_ = 0;
}

void PluginEditor::browseSelect()
{
    if (browseIndex_ < 0 || browseIndex_ >= browseItems_.size()) return;
    auto sel = browseItems_[browseIndex_];
    if (sel.isDirectory()) { browseCurrentDir_ = sel; browseScanCurrentDir(); repaint(); }
    else {
        auto& slot = processor_.getEngine().getSlot(selectedPad_);
        slot.loadFile(sel);
        browseMode_ = false; updateEncoderDisplay(); repaint();
    }
}

void PluginEditor::browseGoUp()
{
    auto p = browseCurrentDir_.getParentDirectory();
    if (p.isDirectory() && p != browseCurrentDir_) { browseCurrentDir_ = p; browseScanCurrentDir(); repaint(); }
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
            const char* l[] = { "", "---", "---", "---" };
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
            const char* l[] = { "", "PITCH", "TIME", "---" };
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
        case PAGE_OVERVIEW:
            return "---";
        case PAGE_SAMPLE: {
            if (enc == 1) return juce::String(slot.getStartPos() * 100.0f, 1) + "%";
            if (enc == 2) return juce::String(slot.getEndPos() * 100.0f, 1) + "%";
            return "---";
        }
        case PAGE_PLAY: {
            const char* modes[] = { "ONE-SHOT", "LOOP", "CLK LOOP", "CLK BAR" };
            if (enc == 1) return modes[static_cast<int>(slot.getMode())];
            if (enc == 2) return juce::String((int)(slot.getVolume() * 100)) + "%";
            if (enc == 3) return juce::String(slot.getPan(), 2);
            return "---";
        }
        case PAGE_PITCH: {
            float st = slot.getPitchSemitones();
            if (enc == 1) return (st >= 0 ? "+" : "") + juce::String(st, 1) + "st";
            if (enc == 2) return juce::String(slot.getTimeStretch(), 2) + "x";
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
    // Trigger pad AND select it
    processor_.getEngine().trigger(n);
    selectedPad_ = n;
    repaint();
}

void PluginEditor::onLeftButton(bool val)
{
    if (!val) return;
    if (browseMode_) { exitBrowseMode(); return; }
    // Navigate pads: left in grid
    int col = selectedPad_ % 4;
    if (col > 0) { selectedPad_--; repaint(); }
}

void PluginEditor::onRightButton(bool val)
{
    if (!val) return;
    if (browseMode_) return;
    int col = selectedPad_ % 4;
    if (col < 3) { selectedPad_++; repaint(); }
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
    if (!val) return;
    switchPage(currentPage_ + 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Encoder Handlers
// ═══════════════════════════════════════════════════════════════════════════

void PluginEditor::onEncoder(int n, float delta)
{
    if (browseMode_) {
        if (n == 0) { int fc = browseItems_.size(); if (fc > 0) { browseIndex_ = delta > 0 ? std::min(fc - 1, browseIndex_ + 1) : std::max(0, browseIndex_ - 1); repaint(); } }
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
            break;

        case PAGE_SAMPLE:
            if (n == 1) slot.setStartPos(slot.getStartPos() + delta * 0.005f);
            if (n == 2) slot.setEndPos(slot.getEndPos() + delta * 0.005f);
            break;

        case PAGE_PLAY:
            if (n == 1) {
                int m = static_cast<int>(slot.getMode()) + (delta > 0 ? 1 : -1);
                slot.setMode(static_cast<PadMode>(juce::jlimit(0, 3, m)));
            }
            if (n == 2) slot.setVolume(juce::jlimit(0.0f, 1.0f, slot.getVolume() + delta * 0.02f));
            if (n == 3) slot.setPan(juce::jlimit(-1.0f, 1.0f, slot.getPan() + delta * 0.05f));
            break;

        case PAGE_PITCH:
            if (n == 1) slot.setPitchSemitones(slot.getPitchSemitones() + delta * 0.5f);
            if (n == 2) slot.setTimeStretch(slot.getTimeStretch() + delta * 0.02f);
            break;
    }
}

void PluginEditor::onEncoderSwitch(int n, bool val)
{
    if (!val) return;

    if (browseMode_) {
        if (n == 0 || n == 3) browseSelect();
        else if (n == 1) browseGoUp();
        return;
    }

    // Push encoder 0 on ANY page = open browser for selected pad
    if (n == 0)
        enterBrowseMode();
}

} // namespace grid
