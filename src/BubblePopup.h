#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace grid {

// ═══════════════════════════════════════════════════════════════════════════
// BubblePopup — anchored speech-bubble overlay listing related parameters.
//
// Interaction model:
//   • enc0 moves a cursor up/down the param list
//   • The ANCHOR encoder (the one that opened the bubble) adjusts the value
//     of the highlighted param
//   • Pushing ANY encoder closes the bubble (Down arrow also closes)
//
// Caller usage:
//   bubble_.show(anchorEnc, "Tape Character");
//   bubble_.addParam("Wow",    fmt(wow_),    [&](float d){ wow_ += d * 0.01f; });
//   bubble_.addParam("Flutter", fmt(flutter_), [&](float d){ flutter_ += d * 0.01f; });
//   bubble_.setOnClose([&]{ /* cleanup */ });
//
// After a value changes, call bubble_.updateParam(idx, newValueString) to
// refresh the rendered text.
// ═══════════════════════════════════════════════════════════════════════════

class BubblePopup
{
public:
    // ── Row rendering modes ──────────────────────────────────────────────
    // NameValue (default): "Wow ..... 50%" — name left, value right, grouped center.
    // Selector: "Crossfade: low MED high" — single centered string, optional
    //   inline choices where the current selection is bold/bright. Use for
    //   "pick one option from a list" bubbles. Anchor encoder TURN cycles
    //   the inline choices (if any), PUSH selects this row and fires onSelect.
    enum class RowMode { NameValue, Selector };

    struct Param {
        juce::String name;
        juce::String value;
        RowMode mode = RowMode::NameValue;
        // For Selector rows with inline choices (e.g. Low/Medium/High):
        // choices[] holds the labels, choiceIdx selects the current.
        // If choices is empty, Selector renders just `name` centered.
        juce::StringArray choices;
        int   choiceIdx = 0;
        std::function<void(float delta)> onAdjust;
    };

    BubblePopup() = default;

    void show(int anchorEncoder, juce::String title);
    void close();
    bool isOpen() const                  { return open_; }
    int  getAnchorEncoder() const        { return anchor_; }
    const juce::String& getTitle() const { return title_; }

    void clearParams();
    void addParam(juce::String name, juce::String value, std::function<void(float delta)> onAdjust);
    // Selector variant: row renders centered. choices=[]: just the name.
    // choices=["Low","Med","High"]: name + inline choices with current highlighted.
    void addSelector(juce::String name, juce::StringArray choices, int currentIdx,
                     std::function<void(float delta)> onAdjust);
    void updateParam(int idx, juce::String newValue);
    void updateChoiceIdx(int idx, int newChoiceIdx);
    int  numParams() const { return params_.size(); }
    int  getCursor() const { return cursor_; }
    const Param& getParam(int i) const { return params_.getReference(i); }

    void setOnClose(std::function<void()> cb) { onClose_ = std::move(cb); }
    // If set: anchor-encoder PUSH fires onSelect(cursor) and closes the bubble
    // instead of just closing. Other encoder pushes still just close.
    void setOnSelect(std::function<void(int)> cb) { onSelect_ = std::move(cb); }
    bool hasOnSelect() const { return (bool)onSelect_; }
    void fireOnSelect() { if (onSelect_) onSelect_(cursor_); }

    // Position the cursor (after addSelector / addParam calls). Caller uses
    // this to land the cursor on the currently-active row when the bubble opens.
    void setCursor(int idx) { cursor_ = juce::jlimit(0, std::max(0, params_.size() - 1), idx); }

    // Mark a row as the "currently active" choice — paint draws a small dot
    // marker before its name to distinguish it from the cursor highlight.
    // Use this in picker bubbles so the user can see "this is the current
    // pick; the cursor is just hovering elsewhere." -1 = no current row.
    void setCurrentRow(int idx) { currentRow_ = idx; }
    int  getCurrentRow() const  { return currentRow_; }

    // Encoder bar override (caller queries while bubble is open)
    juce::String getEncoderLabel(int n) const;
    juce::String getEncoderValue(int n) const;

    // Event routing
    void onEncoderTurn(int n, float delta);
    void onEncoderPush(int n);  // any push closes

    void paint(juce::Graphics& g, juce::Rectangle<int> screen);

    // Layout constants (mirror PluginEditor's encoder zone)
    static constexpr int kEncoderBarH    = 48;
    static constexpr int kEncodersPerRow = 4;
    static constexpr int kEncZoneW       = 800;
    static constexpr int kEncZoneNudge[4] = { -14, 0, 52, 86 };

    // Bubble visuals
    // Height AND width are now adaptive — computed from content when
    // show() is called and rows are added. These are caps/floors only.
    static constexpr int kBubbleHeightMin = 110;
    static constexpr int kBubbleHeightMax = 380;
    static constexpr int kBubbleMinWidth  = 260;
    static constexpr int kBubbleMaxWidth  = 700;
    static constexpr int kBubbleHorizPad  = 14;   // L/R padding inside bubble
    static constexpr int kTailHeight      = 16;
    static constexpr int kTailHalfWidth   = 12;
    static constexpr int kBubbleGap       = 4;
    static constexpr int kCornerRadius    = 10;
    static constexpr int kRowHeight       = 38;
    static constexpr int kTitleHeight     = 34;
    static constexpr int kBottomPadding   = 8;

    static constexpr uint32_t kBubbleFill     = 0xFF161616;
    static constexpr uint32_t kBubbleBorder   = 0xFFE53935;
    static constexpr uint32_t kBubbleTitle    = 0xFFE53935;
    static constexpr uint32_t kBubbleText     = 0xFFFFFFFF;
    static constexpr uint32_t kBubbleDim      = 0xFFAAAAAA;
    static constexpr uint32_t kBubbleHint     = 0x77FFFFFF;
    static constexpr uint32_t kBubbleSelectBg = 0x33E53935;

private:
    bool open_ = false;
    int  anchor_ = -1;
    int  cursor_ = 0;
    int  currentRow_ = -1;  // "active selection" indicator (separate from cursor)
    bool editingChoice_ = false;   // true while user is cycling inline choices on the cursor row
    juce::String title_;
    juce::Array<Param> params_;
    std::function<void()> onClose_;
    std::function<void(int)> onSelect_;

    int  encoderCenterX(int idx) const;
    juce::Rectangle<int> computeBubbleRect(juce::Rectangle<int> screen) const;
};

} // namespace grid
