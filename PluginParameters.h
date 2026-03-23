#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace grid {

// -- SSP Display --
static constexpr int kDisplayWidth  = 1600;
static constexpr int kDisplayHeight = 480;
static constexpr int kNumPads       = 8;
static constexpr int kNumPages      = 5;
static constexpr int kEncodersPerPage = 4;

// -- I/O Channel Indices --
// Grouped by pad so the SSP network view shows:
//   P1 Trig, P1 Pitch, P2 Trig, P2 Pitch, ... P8 Pitch, Clock
//
// For pad N (0-7): trig = N*2, pitch = N*2+1
//
enum {
    I_P1_TRIG = 0, I_P1_PITCH,
    I_P2_TRIG,     I_P2_PITCH,
    I_P3_TRIG,     I_P3_PITCH,
    I_P4_TRIG,     I_P4_PITCH,
    I_P5_TRIG,     I_P5_PITCH,
    I_P6_TRIG,     I_P6_PITCH,
    I_P7_TRIG,     I_P7_PITCH,
    I_P8_TRIG,     I_P8_PITCH,
    I_CLOCK,
    I_MAX
};

// Helper: get trig/pitch channel for pad index 0-7
inline int trigChannel(int pad)  { return pad * 2; }
inline int pitchChannel(int pad) { return pad * 2 + 1; }

enum {
    O_LEFT = 0, O_RIGHT,
    O_MAX
};

// -- Bus names (grouped by pad) --
inline const char* inputBusName(int i) {
    static const char* n[] = {
        "P1 Trig", "P1 Pitch",
        "P2 Trig", "P2 Pitch",
        "P3 Trig", "P3 Pitch",
        "P4 Trig", "P4 Pitch",
        "P5 Trig", "P5 Pitch",
        "P6 Trig", "P6 Pitch",
        "P7 Trig", "P7 Pitch",
        "P8 Trig", "P8 Pitch",
        "Clock"
    };
    return (i >= 0 && i < I_MAX) ? n[i] : "?";
}

inline const char* outputBusName(int i) {
    static const char* n[] = { "Left", "Right" };
    return (i >= 0 && i < O_MAX) ? n[i] : "?";
}

// -- Trigger threshold (Bear uses 0.2f) --
static constexpr float kTrigThreshold = 0.2f;

// -- Pages --
enum Page { PAGE_OVERVIEW = 0, PAGE_SAMPLE, PAGE_PLAY, PAGE_PITCH, PAGE_FADE };

// -- Per-pad modes --
enum class PadMode { OneShot = 0, Loop, ClockedLoop, ClockedBar };

// -- Path finder --
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
