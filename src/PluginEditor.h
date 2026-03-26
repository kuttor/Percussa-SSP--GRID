#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

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

    // ── Layout constants ─────────────────────────────────────────────────
    static constexpr int kTabHeight       = 36;
    static constexpr int kEncoderBarH     = 48;
    static constexpr int kBrowseRowHeight = 44;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace grid
