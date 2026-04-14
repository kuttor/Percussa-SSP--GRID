# GRID — 8-Pad Sampler for Percussa SSP

**GRID** is a performance sampler plugin for the [Percussa SSP](https://www.percussa.com/) Eurorack module. Eight pads, deep editing, modular integration.

**License:** AGPL-3.0  
**Author:** Andy Kuttor ([@kuttor](https://github.com/kuttor))  
**Platform:** Percussa SSP (ARM Cortex-A17, 1600×480 display)  
**Version:** 2.4.0-beta

---

## Features

### Sampling & Playback
- 8 independent sample pads with per-pad file loading
- Multi-voice polyphony (4 voices per pad) with voice stealing and crossfade
- Play modes: One-Shot, Loop, Clocked Loop, Clocked One-Shot
- Sample start/end with visual trim
- Reverse playback
- File browser with Smart Home, auto-preview, multi-select, and file operations

### Slice System
- Transient detection with LOW/MED/HIGH sensitivity
- Manual slice placement with undo
- Auto-slice presets (8, 16, 24, 32, 48, 64, 128, Transient)
- Per-slice pitch offset
- Destructive delete (tape-cut splice)
- Slice export to individual files
- CV-driven slice selection (2 assignable CV inputs)
- MIDI note-to-slice mapping

### Time Stretch & Pitch
- WSOLA time stretch with cross-correlation grain alignment (default)
- Legacy OLA mode selectable per pad
- Pitch control with quantize mode (push encoder to toggle, displays note names)

### Filters
- TPT SVF: LPF, HPF, BPF, Notch
- Formant filter
- MS-20 style filter
- Buchla LPG: vactrol envelope with memory effect, coupled 6dB/oct filter + VCA

### Lo-Fi Modes
- 8-bit, 12-bit, SP-1200 emulation, MPC-60 emulation

### MOD Tab — Per-Pad Step Modulation
- 3 independent mod slots per pad (Mod A / B / C)
- 7 targets: Velocity, Pitch, Start, End, Filter, Pan, Stretch
- 5 actions: Increase, Decrease, Ping/Pong, Random, Sample/Hold
- 16-step pattern that advances on every trigger
- Mod editor popup with pixel art icons and 2x8 step grid
- 2x4 block selection matches SSP physical button layout
- 10 presets per mod slot with auto-save on editor close

### Bus Compressor
- SSL-style feedback topology with soft knee
- Dual-time-constant auto-release
- Sidechain HPF, tanh saturation drive
- Gain reduction visualization overlay on pads

### Performance
- Mute/Solo with configurable fade time
- Choke groups (A-H)
- Performance modes: Immediate, On Release, On Bar
- Kit save/load system with random name generator
- Program Change kit switching (MIDI PC or configurable CC)
- Stack files (Round Robin, Random, Velocity Split)

### MIDI
- Per-pad MIDI channel assignment (1-16 or Omni)
- Per-pad CC mapping (Slice, End, Volume, Pan, Stretch, Filter)
- MIDI clock sync with division/multiplication

### Modular I/O
- 24 inputs: per-pad trigger + pitch CV, clock, reset, slice CV, record, filter CV
- 10 outputs: stereo mix + 8 individual pad outputs
- Per-pad output routing with mix send toggle

---

## Building

### Requirements
- ARM cross-compiler: `arm-linux-gnueabihf-g++`
- JUCE framework (included via CMake)
- SSP SDK (in `libs/ssp-sdk/`)

### Build (macOS to SSP)

```bash
cd ~/Code/Percussa-SSP--GRID

# First build compiles objects + creates helper
make -j$(sysctl -n hw.ncpu) 2>/dev/null || true

# Fake the VST3 helper (can't run ARM binary on Mac)
echo '#!/bin/bash
exit 0' > juce_vst3_helper
chmod 755 juce_vst3_helper

# If helper not in PATH:
sudo cp juce_vst3_helper /usr/local/bin/juce_vst3_helper
sudo chmod 755 /usr/local/bin/juce_vst3_helper

# Build VST3
make GRID_VST3

# Strip for deployment
arm-linux-gnueabihf-strip GRID_artefacts/Release/VST3/GRID.vst3/Contents/armv7l-linux/GRID.so
```

### Deploy

Copy `GRID.so` to your SSP's SD card:

```
/media/BOOT/plugins/GRID/GRID.so
```

---

## I/O Layout

**Inputs (24):**

| Channel | Name | Description |
|---------|------|-------------|
| 0,2,4...14 | P1-P8 Trig | Gate trigger per pad |
| 1,3,5...15 | P1-P8 Pitch | V/Oct pitch CV per pad |
| 16 | Clock | External clock |
| 17 | Reset | Clock reset |
| 18-19 | Slice CV 1-2 | Slice selection (assignable to any pad) |
| 20 | Rec Gate | Recording gate |
| 21-22 | Rec L/R | Recording input |
| 23 | Filter CV | Filter modulation |

**Outputs (10):**

| Channel | Name | Description |
|---------|------|-------------|
| 0-1 | Left/Right | Stereo mix bus |
| 2-9 | Pad 1-8 | Individual pad outputs |

---

## Pages

| Page | Encoders | Description |
|------|----------|-------------|
| PADS | Pad / Rec / Mode / Length | Overview, recording |
| SAMPLE | Pad / File / - / - | File loading, browser |
| PLAY | Pad / Volume / Mode / - | Playback mode, volume |
| WARP | Pad / Stretch / Time / - | Time stretch |
| FADE | Pad / Fade In / Fade Out / Curve | Fade envelopes |
| FILTER | Pad / Cutoff / Reso / Type | Filter control |
| MIDI | Pad / Channel / Device / Clock | MIDI setup |
| MOD | Pad / Mod A / Mod B / Mod C | Step modulation |

---

## File Structure

```
/media/BOOT/
├── plugins/
│   └── GRID/
│       └── GRID.so
└── samples/
    ├── kits/           <- .kit preset files
    ├── stacks/         <- .stack multi-sample files
    ├── recordings/     <- recorded samples
    └── [your samples]
```

---

## Credits

- **TheTechnobear (Bear):** SSP SDK guidance, Midi Research and a bunch of other amazing SSP contributions.
- **wavejockey:** Lead Tester & Documentation
- **Handsonicsuki:** Tester
- **ahabzutun:** Tester

GRID uses the [JUCE](https://juce.com/) framework and the Percussa SSP SDK.

---

## License

GNU Affero General Public License v3.0 (AGPL-3.0). See [LICENSE](LICENSE).
