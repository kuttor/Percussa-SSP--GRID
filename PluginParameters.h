#pragma once
#include <juce_core/juce_core.h>
#include <string>

namespace grid {

// ── SSP Display ──────────────────────────────────────────────────────────
static constexpr int kDisplayWidth  = 1600;
static constexpr int kDisplayHeight = 480;
static constexpr int kNumPads       = 8;
static constexpr int kNumPages      = 4;
static constexpr int kEncodersPerPage = 4;

// ── I/O ──────────────────────────────────────────────────────────────────
// Inputs: 8 gates, 8 pitch CVs, 1 clock = 17
// Outputs: stereo = 2
enum InputChannel {
    IN_GATE_1 = 0, IN_GATE_2, IN_GATE_3, IN_GATE_4,
    IN_GATE_5, IN_GATE_6, IN_GATE_7, IN_GATE_8,
    IN_PITCH_1, IN_PITCH_2, IN_PITCH_3, IN_PITCH_4,
    IN_PITCH_5, IN_PITCH_6, IN_PITCH_7, IN_PITCH_8,
    IN_CLOCK,
    kNumInputs
};
enum OutputChannel { OUT_LEFT = 0, OUT_RIGHT, kNumOutputs };

namespace ChannelName {
    static const char* inputs[] = {
        "Gate1", "Gate2", "Gate3", "Gate4",
        "Gate5", "Gate6", "Gate7", "Gate8",
        "Pitch1", "Pitch2", "Pitch3", "Pitch4",
        "Pitch5", "Pitch6", "Pitch7", "Pitch8",
        "Clock"
    };
    static const char* outputs[] = { "Left", "Right" };
}

// ── Pages ────────────────────────────────────────────────────────────────
enum Page { PAGE_OVERVIEW = 0, PAGE_SAMPLE, PAGE_PLAY, PAGE_PITCH };

// ── Per-pad modes ────────────────────────────────────────────────────────
enum class PadMode { OneShot = 0, Loop, ClockedLoop, ClockedBar };

// ── Path finder ──────────────────────────────────────────────────────────
inline juce::String findSSPSamplePath()
{
    const char* candidates[] = {
        "/media/BOOT/samples",
        "/media/BOOT",
        "/media/linaro/BOOT/samples",
        "/media/linaro/SYNTHOR/samples",
        "/media/linaro/boot/samples",
        "/media/sd/samples",
        "/sdcard/samples",
        "/udata/samples",
        "/media/linaro/BOOT",
        "/media/linaro/SYNTHOR",
        "/home/linaro/samples",
        "/data/samples",
        "/root/samples",
    };

    for (const auto* path : candidates)
    {
        juce::File dir(path);
        if (dir.isDirectory())
            return juce::String(path);
    }
    return "/media/BOOT/samples";
}

} // namespace grid
