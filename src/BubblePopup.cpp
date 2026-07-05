#include "BubblePopup.h"

namespace grid {

// ═══════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

void BubblePopup::show(int anchorEncoder, juce::String title)
{
    anchor_ = juce::jlimit(0, kEncodersPerRow - 1, anchorEncoder);
    title_  = std::move(title);
    open_   = true;
    cursor_ = 0;
    currentRow_ = -1;
    editingChoice_ = false;
    params_.clear();
    onClose_ = nullptr;
    onSelect_ = nullptr;
}

void BubblePopup::close()
{
    if (!open_) return;
    open_ = false;
    auto cb = std::move(onClose_);
    onClose_ = nullptr;
    onSelect_ = nullptr;
    params_.clear();
    if (cb) cb();
}

// ═══════════════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════════════

void BubblePopup::clearParams()
{
    params_.clear();
    cursor_ = 0;
}

void BubblePopup::addParam(juce::String name, juce::String value,
                            std::function<void(float delta)> onAdjust)
{
    Param p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.mode = RowMode::NameValue;
    p.onAdjust = std::move(onAdjust);
    params_.add(std::move(p));
}

void BubblePopup::addSelector(juce::String name, juce::StringArray choices,
                               int currentIdx,
                               std::function<void(float delta)> onAdjust)
{
    Param p;
    p.name = std::move(name);
    p.mode = RowMode::Selector;
    p.choices = std::move(choices);
    p.choiceIdx = juce::jlimit(0, std::max(0, p.choices.size() - 1), currentIdx);
    p.onAdjust = std::move(onAdjust);
    params_.add(std::move(p));
}

void BubblePopup::updateParam(int idx, juce::String newValue)
{
    if (idx < 0 || idx >= params_.size()) return;
    params_.getReference(idx).value = std::move(newValue);
}

void BubblePopup::updateChoiceIdx(int idx, int newChoiceIdx)
{
    if (idx < 0 || idx >= params_.size()) return;
    auto& p = params_.getReference(idx);
    p.choiceIdx = juce::jlimit(0, std::max(0, p.choices.size() - 1), newChoiceIdx);
}

// ═══════════════════════════════════════════════════════════════════════════
// Encoder bar override
// ═══════════════════════════════════════════════════════════════════════════

juce::String BubblePopup::getEncoderLabel(int n) const
{
    if (!open_) return {};
    // Single-encoder navigation: only the anchor encoder is active.
    // Other encoders show blank so the bar reads as "owned by anchor".
    if (n != anchor_) return "";

    const bool selectorMode = (bool)onSelect_;
    if (selectorMode) {
        return editingChoice_ ? "Value" : "Scroll";
    }
    return "Value";
}

juce::String BubblePopup::getEncoderValue(int n) const
{
    if (!open_ || params_.isEmpty()) return {};
    if (n != anchor_) return "";

    int c = juce::jlimit(0, params_.size() - 1, cursor_);
    const bool selectorMode = (bool)onSelect_;

    if (selectorMode) {
        const auto& row = params_.getReference(c);
        if (editingChoice_ && row.choices.size() > 0) {
            // Show the live choice on the cursor row
            return row.choices[juce::jlimit(0, row.choices.size() - 1, row.choiceIdx)];
        }
        return row.name;
    }
    return params_[c].value;
}

// ═══════════════════════════════════════════════════════════════════════════
// Event routing
// ═══════════════════════════════════════════════════════════════════════════

void BubblePopup::onEncoderTurn(int n, float delta)
{
    if (!open_ || params_.isEmpty()) return;
    // Only the anchor encoder controls the bubble. All other encoders are
    // ignored while a bubble is open — Andy wanted single-encoder navigation.
    if (n != anchor_) return;

    int d = (delta > 0) ? 1 : -1;
    const bool selectorMode = (bool)onSelect_;

    if (selectorMode) {
        if (editingChoice_) {
            // Already on a row, cycling its inline choices.
            int c = juce::jlimit(0, params_.size() - 1, cursor_);
            const auto& row = params_.getReference(c);
            if (row.mode == RowMode::Selector && row.choices.size() > 0
                && row.onAdjust) {
                row.onAdjust(delta);
            }
        } else {
            // Scrolling rows.
            cursor_ = juce::jlimit(0, params_.size() - 1, cursor_ + d);
        }
        return;
    }

    // Adjust mode (non-selector) — anchor turn adjusts the highlighted row.
    int c = juce::jlimit(0, params_.size() - 1, cursor_);
    if (params_[c].onAdjust) params_.getReference(c).onAdjust(delta);
}

void BubblePopup::onEncoderPush(int n)
{
    if (!open_) return;
    // Only the anchor encoder is active.
    if (n != anchor_) return;

    const bool selectorMode = (bool)onSelect_;

    if (selectorMode) {
        int c = juce::jlimit(0, params_.size() - 1, cursor_);
        const auto& row = params_.getReference(c);
        bool hasChoices = (row.mode == RowMode::Selector && row.choices.size() > 0);

        if (!editingChoice_ && hasChoices) {
            // First push on a row with choices: enter edit mode. The bubble
            // stays open and subsequent anchor turns cycle choices.
            editingChoice_ = true;
            return;
        }

        // Either no inline choices, or we're already editing and pushing to commit.
        // In both cases: fire onSelect with cursor row and close.
        auto cb = std::move(onSelect_);
        int selected = cursor_;
        onSelect_ = nullptr;
        editingChoice_ = false;
        close();
        if (cb) cb(selected);
        return;
    }

    // Adjust-mode bubble (Stretch / Tape Character) — push just closes.
    editingChoice_ = false;
    close();
}

// ═══════════════════════════════════════════════════════════════════════════
// Geometry
// ═══════════════════════════════════════════════════════════════════════════

int BubblePopup::encoderCenterX(int idx) const
{
    const int encW = kEncZoneW / kEncodersPerRow;
    return idx * encW + kEncZoneNudge[idx] + encW / 2;
}

juce::Rectangle<int> BubblePopup::computeBubbleRect(juce::Rectangle<int> screen) const
{
    // Adaptive height: title + N rows + padding
    int contentH = kTitleHeight + 6
                 + juce::jmax(1, params_.size()) * kRowHeight
                 + kBottomPadding;
    int h = juce::jlimit(kBubbleHeightMin, kBubbleHeightMax, contentH);

    // Adaptive width: estimate widest row using a per-char average (12px @ 22pt
    // bold). Errs slightly wide which is fine — paint clips to the rect.
    auto estW = [](const juce::String& s) {
        return s.length() * 12 + 8;
    };

    int widest = estW(title_) + kBubbleHorizPad * 2;

    for (int i = 0; i < params_.size(); ++i) {
        const auto& p = params_.getReference(i);
        int rowW = 0;
        if (p.mode == RowMode::Selector) {
            rowW = 18 + estW(p.name);  // 18 = active-row dot indicator slot
            if (p.choices.size() > 0) {
                rowW += 28;  // separator bar + gap
                for (int ci = 0; ci < p.choices.size(); ++ci)
                    rowW += 20 + estW(p.choices[ci]);
            }
        } else {
            rowW = estW(p.name) + 24 + estW(p.value);
        }
        rowW += kBubbleHorizPad * 2;
        if (rowW > widest) widest = rowW;
    }

    int w = juce::jlimit(kBubbleMinWidth, kBubbleMaxWidth, widest);

    int anchorX = encoderCenterX(anchor_);
    int x = anchorX - w / 2;
    // Use the actual screen width for clamping — the encoder zone is only
    // the left half of the display, but the bubble can extend across the
    // whole window. Without this, E3 bubbles (anchor near x=760) get
    // pushed left by the kEncZoneW clamp and look mis-aligned.
    const int leftBound  = 8;
    const int rightBound = screen.getWidth() - 8;
    if (x < leftBound)      x = leftBound;
    if (x + w > rightBound) x = rightBound - w;

    int encoderBarTop = screen.getHeight() - kEncoderBarH;
    int y = encoderBarTop - kTailHeight - kBubbleGap - h;
    if (y < 8) y = 8;

    return { x, y, w, h };
}

// ═══════════════════════════════════════════════════════════════════════════
// Paint
// ═══════════════════════════════════════════════════════════════════════════

void BubblePopup::paint(juce::Graphics& g, juce::Rectangle<int> screen)
{
    if (!open_) return;

    auto rect    = computeBubbleRect(screen);
    int  anchorX = encoderCenterX(anchor_);
    int  encBarTop = screen.getHeight() - kEncoderBarH;
    int  tailTipY  = encBarTop + 2;

    // ── Bubble + tail path ────────────────────────────────────────────────
    juce::Path path;
    const float r  = (float)kCornerRadius;
    const float bx = (float)rect.getX();
    const float by = (float)rect.getY();
    const float bw = (float)rect.getWidth();
    const float bh = (float)rect.getHeight();

    float tailLeftX  = juce::jlimit(bx + r + 2.0f, bx + bw - r - 2.0f - 2.0f * kTailHalfWidth,
                                     (float)anchorX - kTailHalfWidth);
    float tailRightX = tailLeftX + 2.0f * kTailHalfWidth;
    float tipX       = (tailLeftX + tailRightX) * 0.5f;

    path.startNewSubPath(bx + r, by);
    path.lineTo(bx + bw - r, by);
    path.quadraticTo(bx + bw, by, bx + bw, by + r);
    path.lineTo(bx + bw, by + bh - r);
    path.quadraticTo(bx + bw, by + bh, bx + bw - r, by + bh);
    path.lineTo(tailRightX, by + bh);
    path.lineTo(tipX, (float)tailTipY);
    path.lineTo(tailLeftX, by + bh);
    path.lineTo(bx + r, by + bh);
    path.quadraticTo(bx, by + bh, bx, by + bh - r);
    path.lineTo(bx, by + r);
    path.quadraticTo(bx, by, bx + r, by);
    path.closeSubPath();

    // Drop shadow (lighter — less visual noise)
    {
        juce::DropShadow sh(juce::Colour(0x55000000), 10, { 0, 3 });
        sh.drawForPath(g, path);
    }

    // Fill + thin border
    g.setColour(juce::Colour(kBubbleFill));
    g.fillPath(path);
    g.setColour(juce::Colour(kBubbleBorder).withAlpha(0.45f));
    g.strokePath(path, juce::PathStrokeType(1.0f));

    // ── Title ─────────────────────────────────────────────────────────────
    auto titleArea = juce::Rectangle<int>(rect.getX() + kBubbleHorizPad,
                                           rect.getY() + 4,
                                           rect.getWidth() - kBubbleHorizPad * 2,
                                           kTitleHeight - 4);
    g.setColour(juce::Colour(kBubbleTitle));
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText(title_, titleArea, juce::Justification::centred);

    // Title separator (subtler)
    g.setColour(juce::Colour(0x22FFFFFF));
    g.fillRect(rect.getX() + kBubbleHorizPad,
               rect.getY() + kTitleHeight,
               rect.getWidth() - kBubbleHorizPad * 2, 1);

    // ── Parameter list ────────────────────────────────────────────────────
    if (params_.isEmpty()) {
        g.setColour(juce::Colour(kBubbleDim));
        g.setFont(20.0f);
        g.drawText("No parameters", rect, juce::Justification::centred);
    } else {
        int listTop = rect.getY() + kTitleHeight + 4;
        int cursorClamped = juce::jlimit(0, params_.size() - 1, cursor_);

        const float paramFontSize = 22.0f;

        for (int i = 0; i < params_.size(); ++i) {
            // Row spans the inner width with kBubbleHorizPad on each side
            auto rowRect = juce::Rectangle<int>(rect.getX() + kBubbleHorizPad,
                                                 listTop + i * kRowHeight,
                                                 rect.getWidth() - kBubbleHorizPad * 2,
                                                 kRowHeight);
            bool sel = (i == cursorClamped);

            if (sel) {
                // Brighter background when actively editing a choice — gives
                // visual feedback that the user is in "value cycle" mode vs
                // just hovering on the row.
                bool editing = editingChoice_;
                g.setColour(juce::Colour(kBubbleSelectBg)
                              .withMultipliedAlpha(editing ? 2.0f : 1.0f));
                g.fillRoundedRectangle(rowRect.toFloat(), 3.0f);
                g.setColour(juce::Colour(kBubbleBorder));
                g.fillRoundedRectangle((float)rowRect.getX() + 2.0f,
                                        (float)rowRect.getY() + 6.0f,
                                        editing ? 4.0f : 2.5f,
                                        (float)rowRect.getHeight() - 12.0f, 1.0f);
            }

            const auto& p = params_.getReference(i);
            int textY = rowRect.getY();
            int textH = rowRect.getHeight();
            const int textPad = 10;  // padding from the row edge to text

            if (p.mode == RowMode::Selector) {
                // Selector row: LEFT-ALIGNED inside the row.
                // "[●] Name  │  choice1   choice2   choice3"
                // The leading dot marks the currently-active choice — distinct
                // from the cursor highlight which marks where you ARE.
                g.setFont(juce::Font(paramFontSize, sel ? juce::Font::bold : 0));
                int x = rowRect.getX() + textPad;

                // "Active row" dot indicator (always reserved space so names
                // align across rows whether marked or not).
                const int dotSlot = 18;
                if (currentRow_ == i) {
                    float cx = (float)x + 4.0f;
                    float cy = (float)textY + (float)textH * 0.5f;
                    g.setColour(juce::Colour(kBubbleBorder));
                    g.fillEllipse(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
                }
                x += dotSlot;

                int nameW = g.getCurrentFont().getStringWidth(p.name);

                g.setColour(sel ? juce::Colour(kBubbleText) : juce::Colour(kBubbleDim));
                g.drawText(p.name, x, textY, nameW, textH,
                           juce::Justification::centredLeft);
                x += nameW;

                if (p.choices.size() > 0) {
                    // Visual separator: small gap + vertical bar + gap
                    x += 14;
                    int barH = textH - 14;
                    int barY = textY + 7;
                    g.setColour(juce::Colour(kBubbleDim).withAlpha(0.5f));
                    g.fillRect((float)x, (float)barY, 1.5f, (float)barH);
                    x += 14;

                    for (int ci = 0; ci < p.choices.size(); ++ci) {
                        int cw = g.getCurrentFont().getStringWidth(p.choices[ci]);
                        bool curChoice = (ci == p.choiceIdx);

                        if (curChoice && sel) {
                            g.setColour(juce::Colour(kBubbleBorder));
                            g.setFont(juce::Font(paramFontSize, juce::Font::bold));
                        } else if (curChoice) {
                            g.setColour(juce::Colour(kBubbleText));
                            g.setFont(juce::Font(paramFontSize, juce::Font::bold));
                        } else {
                            g.setColour(juce::Colour(kBubbleDim).withAlpha(0.5f));
                            g.setFont(juce::Font(paramFontSize, 0));
                        }
                        g.drawText(p.choices[ci], x, textY, cw, textH,
                                   juce::Justification::centredLeft);
                        x += cw + 20;  // wider gap between choices
                    }
                }
            } else {
                // NameValue: name LEFT, value RIGHT (against opposite edges of row)
                g.setFont(juce::Font(paramFontSize, sel ? juce::Font::bold : 0));
                int valueW = g.getCurrentFont().getStringWidth(p.value);

                g.setColour(sel ? juce::Colour(kBubbleText) : juce::Colour(kBubbleDim));
                g.drawText(p.name,
                           rowRect.getX() + textPad, textY,
                           rowRect.getWidth() - textPad * 2 - valueW - 8, textH,
                           juce::Justification::centredLeft);

                g.setColour(sel ? juce::Colour(kBubbleBorder)
                                : juce::Colour(kBubbleText).withAlpha(0.75f));
                g.drawText(p.value,
                           rowRect.getX() + rowRect.getWidth() - textPad - valueW,
                           textY, valueW, textH,
                           juce::Justification::centredLeft);
            }
        }
    }
}

} // namespace grid
