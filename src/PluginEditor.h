#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <functional>
#include <vector>

namespace grid {

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::Timer {
public:
    explicit PluginEditor(PluginProcessor& p);
    ~PluginEditor() override;

    static constexpr const char* kFirmwareVersion = "2.4.2-beta";

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // SSP hardware callbacks
    void onButton(int n, bool val);
    void onLeftButton(bool val);
    void onRightButton(bool val);
    void onUpButton(bool val);
    void onDownButton(bool val);
    void onLeftShiftButton(bool val);
    void onRightShiftButton(bool val);
    void onEncoder(int n, float delta);
    void onEncoderSwitch(int n, bool val);

private:
    PluginProcessor& processor_;

    // ── Page system ──────────────────────────────────────────────────────
    int currentPage_ = PAGE_OVERVIEW;
    int selectedPad_ = 0;

    void switchPage(int page);
    const char* getPageName(int page) const;

    // ── Encoder bar ──────────────────────────────────────────────────────
    struct EncoderSlot {
        juce::Label nameLabel, valueLabel;
    };
    EncoderSlot encoderSlots_[kEncodersPerPage];
    void updateEncoderDisplay();
    juce::String getEncoderLabel(int page, int enc) const;
    juce::String getEncoderValue(int page, int enc) const;

    // ── Status bar ───────────────────────────────────────────────────────
    juce::Label statusLabel_;

    // ── Paint helpers ────────────────────────────────────────────────────
    void paintOverviewPage(juce::Graphics& g, juce::Rectangle<int> area);
    void paintSamplePage(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPlayPage(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPitchPage(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPadBox(juce::Graphics& g, juce::Rectangle<int> box, int padIndex);
    void paintMiniWaveform(juce::Graphics& g, juce::Rectangle<int> area, const SampleSlot& slot);
    void paintWaveformDetail(juce::Graphics& g, juce::Rectangle<int> area, const SampleSlot& slot);

    // ── File Browser ─────────────────────────────────────────────────────
    bool browseMode_ = false;
    bool muteMode_ = false;
    bool soloMode_ = false;          // double-tap RS
    bool muteToggled_ = false;
    bool pendingMute_[kNumPads] = {};
    bool soloActive_[kNumPads] = {}; // which pads are soloed
    bool preSoloMute_[kNumPads] = {}; // mute state before entering solo (to restore)
    double lastRSTapTime_ = 0.0;     // for double-tap detection
    static constexpr double kDoubleTapMs = 350.0;  // queued mute toggles (On Release / On Bar)
    int browseIndex_ = 0;
    int browseScrollOffset_ = 0;
    juce::File browseCurrentDir_;
    juce::Array<juce::File> browseItems_;
    juce::StringArray browseItemNames_;
    juce::StringArray browseItemDurations_;
    juce::AudioFormatManager browseFormatMgr_;

    void enterBrowseMode();
    void exitBrowseMode();
    void browseScanCurrentDir();
    void browseSelect();
    void browseGoUp();
    void browseGoHome();
    void paintFileBrowser(juce::Graphics& g, juce::Rectangle<int> area);

    // Browser UX v2: hold detection + file ops + auto-preview
    double encPushTime_[4] = {};       // timestamp of encoder push-down
    bool   encPushHandled_[4] = {};    // true if long-press already consumed
    bool   browseAutoPreview_ = false; // play sample on scroll
    int    browseFileOp_ = 0;          // 0=none, 1=Move, 2=Copy, 3=Delete, 4=Multi
    static constexpr double kHoldMs = 800.0;
    static const char* browseFileOpName(int op) {
        static const char* names[] = { "---", "MOVE", "COPY", "DELETE", "MULTI" };
        return names[juce::jlimit(0, 4, op)];
    }

    // Slice Combine: concatenate multiple samples into one with auto-slicing
    void sliceCombine(const juce::StringArray& paths);

    // File clipboard for Move/Copy
    juce::String browseClipboardPath_;
    int browseClipboardOp_ = 0;  // 0=none, 1=move, 2=copy
    void browseExecuteFileOp();

    // ── Config Browser (right-side flyout, LS+RS) ─────────────────────
    bool configMode_ = false;
    int configIndex_ = 0;       // index into configRows_ (selectable only)
    int configScrollOffset_ = 0;
    bool configEditMode_ = false;  // true = turning encoder changes value
    bool leftShiftHeld_ = false;
    bool rightShiftHeld_ = false;
    double leftShiftPressTime_ = 0.0;
    juce::Array<ConfigRow> configRows_;

    void enterConfigMode();
    void exitConfigMode();
    void buildConfigRows();
    void paintConfigBrowser(juce::Graphics& g, juce::Rectangle<int> area);
    int configSelectableCount() const;
    int configVisualToSelectable(int visualIdx) const;
    int configSelectableToVisual(int selIdx) const;
    void configAdjustValue(int selIdx, int delta);
    void configPushValue(int selIdx);

    // ── Popup Modal (Octatrack-style centered dialog) ─────────────────
    bool popupMode_ = false;
    int popupIndex_ = 0;
    juce::String popupTitle_;
    juce::StringArray popupOptions_;
    std::function<void(int)> popupCallback_;  // called with selected index, -1 = cancel

    void showPopup(const juce::String& title, const juce::StringArray& options,
                   std::function<void(int)> callback);
    void closePopup(int result = -1);
    void paintPopup(juce::Graphics& g, juce::Rectangle<int> area);

    // ── Multi-select in file browser ──────────────────────────────────
    bool multiSelectMode_ = false;
    bool multiSelected_[kNumPads] = {};     // which slots are filled
    int multiSelectedIndices_[kNumPads] = {};  // browseItems_ index for each slot
    int multiSelectCount_ = 0;

    // ── Preset (Kit) Browsing ─────────────────────────────────────────
    bool kitBrowseActive_ = false;
    int kitBrowseIndex_ = -1;
    int kitCurrentIndex_ = -1;
    double kitBrowseStartTime_ = 0.0;
    juce::Array<juce::File> availableKits_;
    static constexpr double kKitBrowseTimeoutMs = 3000.0;

    void refreshAvailableKits();
    void kitBrowseCommit();
    void kitBrowseRevert();

    // ── Name Entry ────────────────────────────────────────────────────
    juce::Random nameRng_;
    void showNameEntryPopup(const juce::String& title,
                            std::function<void(const juce::String&)> callback);

    // ── On-screen Keyboard ───────────────────────────────────────────
    bool keyboardMode_ = false;
    juce::String keyboardTitle_;
    juce::String keyboardText_;
    int keyboardRow_ = 0;     // 0-3 (3 char rows + 1 button row)
    int keyboardCol_ = 0;
    std::function<void(const juce::String&)> keyboardCallback_;
    static constexpr int kKeyboardMinLen = 4;
    static constexpr int kKeyboardMaxLen = 12;

    void showKeyboard(const juce::String& title,
                      std::function<void(const juce::String&)> callback);
    void closeKeyboard();
    void keyboardAction();  // left shift press
    void paintKeyboard(juce::Graphics& g, juce::Rectangle<int> area);
    char keyboardCharAt(int row, int col) const;
    int keyboardRowLen(int row) const;

    // ── Slice Editor (full-screen overlay) ──────────────────────────────
    bool sliceEditorMode_ = false;
    float sliceCursorPos_ = 0.0f;    // normalized 0-1 within sample
    float sliceZoom_ = 1.0f;         // 1x = full sample, 32x = zoomed
    float sliceViewCenter_ = 0.5f;   // center of zoomed view (normalized)
    int sliceAutoPreset_ = 0;        // index into auto values

    // Save user's trim positions — restored on exit so audition doesn't wreck them
    float preSliceStartPos_ = 0.0f;
    float preSliceEndPos_ = 1.0f;
    static constexpr int kSliceAutoCount = 9;

    // 0=OFF, 1=8, 2=16, 3=24, 4=32, 5=48, 6=64, 7=128, 8=Transient
    static int sliceAutoValue(int idx) {
        static const int vals[] = { 0, 8, 16, 24, 32, 48, 64, 128, -1 };
        return vals[juce::jlimit(0, kSliceAutoCount - 1, idx)];
    }
    static juce::String sliceAutoName(int idx) {
        static const char* names[] = { "OFF", "8", "16", "24", "32", "48", "64", "128", "Transient" };
        return names[juce::jlimit(0, kSliceAutoCount - 1, idx)];
    }

    void enterSliceEditor();
    void exitSliceEditor();
    void paintSliceEditor(juce::Graphics& g, juce::Rectangle<int> area);

    // ── Mod Editor (popup overlay on MOD page) ──────────────────────────
    bool modEditorOpen_ = false;
    int  modEditorSlot_ = 0;   // which mod (0/1/2) opened the editor
    int  modEditorRow_ = 0;    // 0 = steps 1-8, 1 = steps 9-16
    void paintModEditor(juce::Graphics& g, juce::Rectangle<int> area);

    // ── Compressor Editor (popup overlay) ───────────────────────────────
    bool compEditorOpen_ = false;
    int  compEditorRow_ = 0;   // 0 = THR/RAT/ATK/REL, 1 = MUP/MIX/SCF/PWR
    static constexpr int kCompScopeLen = 200;
    float compScopePeak_[kCompScopeLen] = {};  // input peak history (0-1)
    float compScopeGR_[kCompScopeLen] = {};    // GR history (dB, 0-24)
    // Per-param animation phase (driven from time + real envelope data)
    float compIconEnv_[8] = {};   // general env/pulse per cell
    void enterCompEditor();
    void exitCompEditor();
    void paintCompEditor(juce::Graphics& g, juce::Rectangle<int> area);
    void compEditorEncoderTurn(int enc, float delta);
    void compScopePush(float peakLin, float grDb);  // shifts history left
    // Param-specific icon painters (each cell)
    void paintCompIconThreshold(juce::Graphics& g, int cx, int cy, int cellH, float animPhase);
    void paintCompIconRatio    (juce::Graphics& g, int cx, int cy, int cellH);
    void paintCompIconAttack   (juce::Graphics& g, int cx, int cy, int cellH, float animPhase);
    void paintCompIconRelease  (juce::Graphics& g, int cx, int cy, int cellH, float animPhase);
    void paintCompIconMakeup   (juce::Graphics& g, int cx, int cy, int cellH);
    void paintCompIconMix      (juce::Graphics& g, int cx, int cy, int cellH);
    void paintCompIconSCFilter (juce::Graphics& g, int cx, int cy, int cellH);
    void paintCompIconLowCut   (juce::Graphics& g, int cx, int cy, int cellH);

    // ── Mixer Popup (all 8 pad volumes in one view) ─────────────────────
    bool mixerOpen_ = false;
    int  mixerPad_ = 0;  // selected pad
    void enterMixer();
    void exitMixer();
    void paintMixer(juce::Graphics& g, juce::Rectangle<int> area);

    // Browser font size helper — applies global adjustment
    float bFont(float base) const {
        return juce::jlimit(10.0f, 32.0f, base + processor_.getBrowserFontAdj());
    }

    // ── Slice visual effects (arcade-style) ─────────────────────────────
    // Splice impact animation
    double spliceAnimStartMs_ = 0.0;
    float spliceAnimPos_ = 0.5f;        // normalized position of the splice
    bool spliceAnimActive_ = false;

    // Stitch marks: where tape was cut and joined (persistent)
    static constexpr int kMaxStitches = 32;
    float stitchPositions_[kMaxStitches] = {};
    int stitchCount_ = 0;
    void addStitch(float normPos) {
        if (stitchCount_ < kMaxStitches)
            stitchPositions_[stitchCount_++] = normPos;
    }
    void clearStitches() { stitchCount_ = 0; }

    // Action flash (brief full-screen tint on big actions)
    double actionFlashStartMs_ = 0.0;
    juce::Colour actionFlashColour_ { 0x00000000 };

    void triggerSpliceAnim(float pos) {
        spliceAnimActive_ = true;
        spliceAnimPos_ = pos;
        spliceAnimStartMs_ = juce::Time::getMillisecondCounterHiRes();
    }
    void triggerActionFlash(juce::Colour c) {
        actionFlashStartMs_ = juce::Time::getMillisecondCounterHiRes();
        actionFlashColour_ = c;
    }

    // Slice editor undo (multi-level stack, up to 16 levels)
    struct SliceUndoState {
        float sliceStarts[128] = {};
        float sliceEnds[128] = {};
        float slicePitch[128] = {};
        int sliceCount = 0;
        float startPos = 0.0f, endPos = 1.0f;
        juce::AudioBuffer<float> audioBackup;
        int numSamples = 0;
    };
    static constexpr int kMaxUndoLevels = 16;
    std::vector<SliceUndoState> undoStack_;
    void sliceSaveUndo();
    void slicePerformUndo();
    void sliceDeleteRegion();  // destructive: remove audio in cursor's region

    // ── Layout constants ─────────────────────────────────────────────────
    static constexpr int kTabHeight       = 36;
    static constexpr int kEncoderBarH     = 48;
    static constexpr int kBrowseRowHeight = 44;

    // ── Button debounce (wall clock, prevents SSP callback queuing) ──
    double lastButtonTriggerMs_[kNumPads] = {};
    static constexpr double kButtonDebounceMs = 35.0;

    // ── Colors ───────────────────────────────────────────────────────────
    static constexpr uint32_t kBg             = 0xFF0D0D0D;
    static constexpr uint32_t kTabBg          = 0xFF151515;
    static constexpr uint32_t kTabActive      = 0xFFE53935;  // ELAS red
    static constexpr uint32_t kTabText        = 0xFF666666;
    static constexpr uint32_t kTabTextActive  = 0xFFFFFFFF;
    static constexpr uint32_t kPadBg          = 0xFF1C1C1C;
    static constexpr uint32_t kPadSelected    = 0xFF2A2A2A;
    static constexpr uint32_t kPadBorder      = 0xFF333333;
    static constexpr uint32_t kPadSelBorder   = 0xFFE53935;
    static constexpr uint32_t kPadPlaying     = 0xFF4CAF50;
    static constexpr uint32_t kPadEmpty       = 0xFF555555;
    static constexpr uint32_t kPadText        = 0xFFDDDDDD;
    static constexpr uint32_t kPadNumText     = 0xFF666666;
    static constexpr uint32_t kWfGreen        = 0xFF1B5E20;
    static constexpr uint32_t kWfYellow       = 0xFFFBC02D;
    static constexpr uint32_t kWfRed          = 0xFFE53935;
    static constexpr uint32_t kEncBarBg       = 0xFF111111;
    static constexpr uint32_t kEncLabel       = 0xFF888888;
    static constexpr uint32_t kEncValue       = 0xFFFFFFFF;
    static constexpr uint32_t kBrowseBg       = 0xFF141414;
    static constexpr uint32_t kBrowseSelBg    = 0xFFE53935;
    static constexpr uint32_t kBrowseText     = 0xFFFFFFFF;
    static constexpr uint32_t kBrowseFolder   = 0xFFFFAB40;
    static constexpr uint32_t kConfigBg       = 0xE6121212;  // slight transparency
    static constexpr uint32_t kConfigSelBg    = 0xFFE53935;
    static constexpr uint32_t kConfigDivider  = 0x4DFFFFFF;  // 30% white
    static constexpr uint32_t kConfigHeader   = 0xFFE53935;
    static constexpr uint32_t kConfigLabel    = 0xFFAAAAAA;
    static constexpr uint32_t kConfigValue    = 0xFFFFFFFF;
    static constexpr uint32_t kConfigEditVal  = 0xFF42A5F5;  // blue when editing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace grid
