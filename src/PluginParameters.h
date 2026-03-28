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
// Grouped by pad, 4 per pad:
//   P1 Trig, P1 Pitch, P1 Start, P1 End, P2 Trig, ...
//
// For pad N (0-7): trig = N*4, pitch = N*4+1, start = N*4+2, end = N*4+3
//
enum {
    I_P1_TRIG = 0, I_P1_PITCH, I_P1_START, I_P1_END,
    I_P2_TRIG,     I_P2_PITCH, I_P2_START, I_P2_END,
    I_P3_TRIG,     I_P3_PITCH, I_P3_START, I_P3_END,
    I_P4_TRIG,     I_P4_PITCH, I_P4_START, I_P4_END,
    I_P5_TRIG,     I_P5_PITCH, I_P5_START, I_P5_END,
    I_P6_TRIG,     I_P6_PITCH, I_P6_START, I_P6_END,
    I_P7_TRIG,     I_P7_PITCH, I_P7_START, I_P7_END,
    I_P8_TRIG,     I_P8_PITCH, I_P8_START, I_P8_END,
    I_CLOCK,
    I_RESET,
    I_REC_GATE,
    I_REC_L,
    I_REC_R,
    I_MAX  // = 37
};

// Helper: get channel for pad index 0-7
inline int trigChannel(int pad)  { return pad * 4; }
inline int pitchChannel(int pad) { return pad * 4 + 1; }
inline int startChannel(int pad) { return pad * 4 + 2; }
inline int endChannel(int pad)   { return pad * 4 + 3; }

enum {
    O_LEFT = 0, O_RIGHT,
    O_MAX
};

// -- Recording modes --
enum class RecMode { Instant = 0, Threshold, NextBar };

// -- Max record lengths (in seconds) --
static constexpr float kRecLengths[] = { 5.0f, 10.0f, 30.0f, 60.0f, 120.0f, 300.0f };
static constexpr int kNumRecLengths = 6;

// -- Bus names (grouped by pad) --
inline const char* inputBusName(int i) {
    static const char* n[] = {
        "P1 Trig", "P1 Pitch", "P1 Start", "P1 End",
        "P2 Trig", "P2 Pitch", "P2 Start", "P2 End",
        "P3 Trig", "P3 Pitch", "P3 Start", "P3 End",
        "P4 Trig", "P4 Pitch", "P4 Start", "P4 End",
        "P5 Trig", "P5 Pitch", "P5 Start", "P5 End",
        "P6 Trig", "P6 Pitch", "P6 Start", "P6 End",
        "P7 Trig", "P7 Pitch", "P7 Start", "P7 End",
        "P8 Trig", "P8 Pitch", "P8 Start", "P8 End",
        "Clock",
        "Reset", "Rec Gate", "Rec L", "Rec R"
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
