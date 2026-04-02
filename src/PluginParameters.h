#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace grid {

// -- SSP Display --
static constexpr int kDisplayWidth  = 1600;
static constexpr int kDisplayHeight = 480;
static constexpr int kNumPads       = 8;
static constexpr int kNumPages      = 7;
static constexpr int kEncodersPerPage = 4;

// -- I/O Channel Indices --
// Grouped by pad: Trig then Pitch.
//   P1 Trig, P1 Pitch, P2 Trig, P2 Pitch, ... P8 Pitch, Clock, Reset, Rec Gate, Rec L, Rec R
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
    I_RESET,
    I_START_CV,
    I_END_CV,
    I_REC_GATE,
    I_REC_L,
    I_REC_R,
    I_MAX  // = 23 (+ 2 outputs = 25 total, under JUCE's 32-channel limit)
};

// Helper: get channel for pad index 0-7
inline int trigChannel(int pad)  { return pad * 2; }
inline int pitchChannel(int pad) { return pad * 2 + 1; }

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
        "P1 Trig", "P1 Pitch",
        "P2 Trig", "P2 Pitch",
        "P3 Trig", "P3 Pitch",
        "P4 Trig", "P4 Pitch",
        "P5 Trig", "P5 Pitch",
        "P6 Trig", "P6 Pitch",
        "P7 Trig", "P7 Pitch",
        "P8 Trig", "P8 Pitch",
        "Clock",
        "Reset", "Start CV", "End CV",
        "Rec Gate", "Rec L", "Rec R"
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
enum Page { PAGE_OVERVIEW = 0, PAGE_SAMPLE, PAGE_PLAY, PAGE_PITCH, PAGE_FADE, PAGE_MIDI, PAGE_OPTIONS };

// -- Per-pad modes --
enum class PadMode { OneShot = 0, Loop, ClockedLoop, ClockedBar };

// -- Choke groups --
enum class ChokeGroup { None = 0, A, B, C, D, E, F, G, H };

// -- Performance mode (muting + preset switching) --
enum class PerfMode { Immediate = 0, OnRelease, OnBar };

// -- Per-pad CC map (default CC assignments) --
struct PadCCMap {
    int ccStart   = 1;
    int ccEnd     = 2;
    int ccVolume  = 7;
    int ccPan     = 10;
    int ccStretch = 11;

    static constexpr int kNumCCs = 5;

    int& byIndex(int i) {
        switch (i) {
            case 0: return ccStart;
            case 1: return ccEnd;
            case 2: return ccVolume;
            case 3: return ccPan;
            default: return ccStretch;
        }
    }
    int byIndex(int i) const {
        switch (i) {
            case 0: return ccStart;
            case 1: return ccEnd;
            case 2: return ccVolume;
            case 3: return ccPan;
            default: return ccStretch;
        }
    }
    static const char* ccName(int i) {
        static const char* names[] = { "CC Start", "CC End", "CC Volume", "CC Pan", "CC Stretch" };
        return (i >= 0 && i < kNumCCs) ? names[i] : "?";
    }
};

// -- Config browser row types --
enum class ConfigRowType { Header, Divider, Spacer, CCValue, Enum, PushAction };

struct ConfigRow {
    ConfigRowType type;
    juce::String label;
    int padIndex;     // -1 = global
    int paramIndex;   // index into CC map or global setting enum
};

// -- Stack layer mode --
enum class StackLayerMode { RoundRobin = 0, Random, VelocitySplit };

// -- Stack file: multiple samples on one pad --
struct StackData {
    juce::String name;
    StackLayerMode mode = StackLayerMode::RoundRobin;
    juce::StringArray layerPaths;  // relative to sample root

    bool saveToFile(const juce::File& file) const {
        auto xml = std::make_unique<juce::XmlElement>("GRID_STACK");
        xml->setAttribute("name", name);
        xml->setAttribute("mode", static_cast<int>(mode));
        for (auto& path : layerPaths) {
            auto* layer = xml->createNewChildElement("LAYER");
            layer->setAttribute("file", path);
        }
        return xml->writeTo(file);
    }

    static StackData loadFromFile(const juce::File& file) {
        StackData s;
        auto xml = juce::XmlDocument::parse(file);
        if (!xml || xml->getTagName() != "GRID_STACK") return s;
        s.name = xml->getStringAttribute("name", file.getFileNameWithoutExtension());
        s.mode = static_cast<StackLayerMode>(xml->getIntAttribute("mode", 0));
        for (auto* layer : xml->getChildWithTagNameIterator("LAYER"))
            s.layerPaths.add(layer->getStringAttribute("file"));
        return s;
    }
};

// -- Kit file: 8 pad slots with settings --
struct KitPadSlot {
    juce::String filePath;      // sample path (relative) or empty
    juce::String stackPath;     // .stack file path (relative) or empty — overrides filePath
    float volume    = 1.0f;
    float pan       = 0.0f;
    float startPos  = 0.0f;
    float endPos    = 1.0f;
    float pitch     = 0.0f;
    float stretch   = 1.0f;
    int   mode      = 0;        // PadMode
    int   choke     = 0;        // ChokeGroup
    bool  reversed  = false;
    int   midiCh    = 0;
};

struct KitData {
    juce::String name;
    KitPadSlot pads[kNumPads];

    bool saveToFile(const juce::File& file) const {
        auto xml = std::make_unique<juce::XmlElement>("GRID_KIT");
        xml->setAttribute("name", name);
        for (int i = 0; i < kNumPads; ++i) {
            auto* pad = xml->createNewChildElement("PAD");
            pad->setAttribute("index", i);
            pad->setAttribute("file", pads[i].filePath);
            pad->setAttribute("stack", pads[i].stackPath);
            pad->setAttribute("volume", (double)pads[i].volume);
            pad->setAttribute("pan", (double)pads[i].pan);
            pad->setAttribute("start", (double)pads[i].startPos);
            pad->setAttribute("end", (double)pads[i].endPos);
            pad->setAttribute("pitch", (double)pads[i].pitch);
            pad->setAttribute("stretch", (double)pads[i].stretch);
            pad->setAttribute("mode", pads[i].mode);
            pad->setAttribute("choke", pads[i].choke);
            pad->setAttribute("reversed", pads[i].reversed ? 1 : 0);
            pad->setAttribute("midiCh", pads[i].midiCh);
        }
        return xml->writeTo(file);
    }

    static KitData loadFromFile(const juce::File& file) {
        KitData k;
        auto xml = juce::XmlDocument::parse(file);
        if (!xml || xml->getTagName() != "GRID_KIT") return k;
        k.name = xml->getStringAttribute("name", file.getFileNameWithoutExtension());
        for (auto* pad : xml->getChildWithTagNameIterator("PAD")) {
            int i = pad->getIntAttribute("index", -1);
            if (i < 0 || i >= kNumPads) continue;
            k.pads[i].filePath  = pad->getStringAttribute("file");
            k.pads[i].stackPath = pad->getStringAttribute("stack");
            k.pads[i].volume    = (float)pad->getDoubleAttribute("volume", 1.0);
            k.pads[i].pan       = (float)pad->getDoubleAttribute("pan", 0.0);
            k.pads[i].startPos  = (float)pad->getDoubleAttribute("start", 0.0);
            k.pads[i].endPos    = (float)pad->getDoubleAttribute("end", 1.0);
            k.pads[i].pitch     = (float)pad->getDoubleAttribute("pitch", 0.0);
            k.pads[i].stretch   = (float)pad->getDoubleAttribute("stretch", 1.0);
            k.pads[i].mode      = pad->getIntAttribute("mode", 0);
            k.pads[i].choke     = pad->getIntAttribute("choke", 0);
            k.pads[i].reversed  = pad->getIntAttribute("reversed", 0) != 0;
            k.pads[i].midiCh    = pad->getIntAttribute("midiCh", 0);
        }
        return k;
    }
};

// -- Random name generator --
inline juce::String generateRandomName(juce::Random& rng)
{
    static const char* adjectives[] = {
        "Analog", "Binary", "Cosmic", "Dark", "Electric", "Fractal",
        "Glitch", "Hyper", "Iron", "Jade", "Kinetic", "Lunar",
        "Micro", "Neon", "Orbit", "Plasma", "Quantum", "Rust",
        "Solar", "Turbo", "Ultra", "Volt", "Warp", "Zero"
    };
    static const char* nouns[] = {
        "Prophet", "Moog", "Buchla", "Juno", "Jupiter", "Tesla",
        "Curie", "Planck", "Fermi", "Sagan", "Turing", "Volta",
        "Pulse", "Drift", "Smash", "Fracture", "Echo", "Blaze",
        "Storm", "Cipher", "Phantom", "Prism", "Vertex", "Flux"
    };
    int a = rng.nextInt(24);
    int n = rng.nextInt(24);
    return juce::String(adjectives[a]) + " " + juce::String(nouns[n]);
}

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
