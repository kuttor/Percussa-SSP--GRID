#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <functional>

namespace grid {

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::Timer {
public:
    explicit PluginEditor(PluginProcessor& p);
    ~PluginEditor() override;

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
    static constexpr int kSliceAutoCount = 6;

    // 0=OFF, 1=8, 2=16, 3=24, 4=32, 5=Zero-X (-1)
    static int sliceAutoValue(int idx) {
        static const int vals[] = { 0, 8, 16, 24, 32, -1 };
        return vals[juce::jlimit(0, kSliceAutoCount - 1, idx)];
    }
    static juce::String sliceAutoName(int idx) {
        static const char* names[] = { "OFF", "8", "16", "24", "32", "Transient" };
        return names[juce::jlimit(0, kSliceAutoCount - 1, idx)];
    }

    void enterSliceEditor();
    void exitSliceEditor();
    void paintSliceEditor(juce::Graphics& g, juce::Rectangle<int> area);

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
