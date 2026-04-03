# GRID

**8-pad performance sampler for the Percussa SSP.**

Load. Trigger. Perform.

---

## What is GRID?

GRID is an 8-channel sample player, recorder, and performance instrument for the Percussa SSP Eurorack module. Load WAV or AIFF files onto 8 pads, trigger them with CV gates, MIDI, or the hardware buttons, and shape them with pitch shifting, time stretching, filters, lo-fi degradation, fade curves, and start/end markers — all controllable via CV and MIDI CC.

## Features

**Sampling**
- 8 independent pads, each with its own sample
- WAV and AIFF support (any sample rate — auto-compensated to 48kHz output)
- One-shot, loop, clocked loop, and clocked one-shot modes
- Per-pad volume, pan, pitch (±48 semitones), time stretch (0.25x–4.0x)
- Per-pad start/end markers with CV control (±5V, sample-and-hold on trigger)
- Per-pad fade in/out with linear or exponential curves

**Filters (7 types per pad)**
- TPT SVF engine (Cytomic/Andy Simper topology)
- LPF, HPF, BPF, NOTCH — the standards, stable at all frequencies
- FORMANT — 3 parallel bandpass filters morphing through vowels A→E→I→O→U
- MS-20 — Korg MS-20 style with tanh saturation in feedback. Warm to screaming.
- Cutoff displayed as strength: 0% = no filtering, 100% = max filtering
- Per-pad cutoff (20Hz–20kHz) and resonance (0–100%)
- CC 74 for MIDI cutoff control
- Filter CV input — master cutoff for all pads (0V = open, 1V = closed)

**Lo-Fi (per-pad, in Config Browser)**
- OFF — clean, no degradation
- 8-Bit — 8-bit quantization, no dither
- 12-Bit — 12-bit quantization, no dither
- SP-1200 — 12-bit linear + 26kHz zero-order hold sample rate reduction. The grit, the aliasing, the crunch.
- MPC-60 — 12-bit µ-law companded + 40kHz ZOH. Warm, punchy, thick.

**Recording**
- Stereo audio input (Rec L / Rec R buses)
- Three record modes: Instant, Threshold, Next Bar
- Configurable max length: 5s, 10s, 30s, 60s, 2min, 5min
- Rec Gate input for hands-free operation
- Auto-saves as WAV to /recordings/ folder

**Clock Sync**
- Clock input with BPM detection
- CLK:LOOP — auto time-stretch to match N beats, loops
- CLK:1SHOT — auto time-stretch to match N beats, plays once
- Per-pad CLK Beats: 1 Beat, 2 Beats, 1 Bar, 2 Bars, 4 Bars
- Clock multipliers/dividers: ×8, ×4, ×2, /1, /2, /4, /8
- MIDI clock support (24 PPQN)

**MIDI**
- Direct USB MIDI device access
- Per-pad MIDI channel (OFF, 1–16, OMNI)
- Note On = trigger, Velocity = volume, Note Number = pitch (C3 = original)
- Per-pad CC map (Start, End, Volume, Pan, Stretch, Filter)
- MIDI clock with BPM detection

**Performance Muting (3 modes)**
- Immediate, On Release, On Bar (1–4 bars)
- Visual feedback: green/red/orange borders

**Config Browser (LS+RS)**
- Per-pad: CLK Beats, Lo-Fi mode, 6 CC assignments
- Global: Mute Mode, Queue Bars, Preset Switch, Encoder Speed, Debug Msgs, Reboot

**Kits & Smart Home**
- Kit files with all per-pad settings (including filter and lo-fi)
- Structured file browser with KITS/STACKS/RECORDINGS/SAMPLES sections
- Multi-select for batch operations

**Options**
- Choke groups (A–H)
- Reverse playback
- Enhance (normalize to 0.95 peak)

## I/O Layout

```
Inputs (24 — SSP maximum):
  P1 Trig, P1 Pitch ... P8 Trig, P8 Pitch
  Clock, Reset, Start CV, End CV
  Rec Gate, Rec L, Rec R, Filter CV

Outputs (2):
  Left, Right
```

## Tabs

| Tab | Enc 1 | Enc 2 | Enc 3 |
|-----|-------|-------|-------|
| PADS | REC arm/stop | Rec mode | Max length |
| SAMPLE | Start % | End % | Pitch (st) |
| PLAY | Mode | Volume (push=MUTE) | Pan |
| WARP | Time stretch | Clock M/D | — |
| FADE | Fade In (push=LIN/EXP) | Fade Out (push=LIN/EXP) | — |
| FILTER | Type (push=OFF) | Cutoff 0–100% (push=0%) | Resonance (push=0%) |
| MIDI | Channel (push=OFF) | Device (push=disconnect) | MIDI clock |
| OPTIONS | Choke (push=NONE) | Reverse (push=toggle) | Enhance (push) |

Encoder 0: pad select (turn) / file browser (push).

## Filter Types

| Type | 0% | 100% | Character |
|------|-----|------|-----------|
| LPF | 20kHz | 20Hz | Warm, dark |
| HPF | 20Hz | 20kHz | Thin, airy |
| BPF | 20kHz | 20Hz | Telephone, nasal |
| NOTCH | 20kHz | 20Hz | Phaser-like |
| FORMANT | Vowel A | Vowel U | Vocal morphing |
| MS-20 | 20kHz | 20Hz | Saturated harmonics |

## Lo-Fi Modes

| Mode | Bit Depth | Sample Rate | Character |
|------|-----------|-------------|-----------|
| 8-Bit | 8 | Native | Harsh, retro game |
| 12-Bit | 12 | Native | Subtle grit |
| SP-1200 | 12 linear | 26.04 kHz | Crunchy, bright, aliased |
| MPC-60 | 12 companded | 40 kHz | Warm, punchy, thick |

## MIDI CC Defaults

| CC | Parameter |
|----|-----------|
| 1 | Start position |
| 2 | End position |
| 7 | Volume |
| 10 | Pan (snaps to center) |
| 11 | Time stretch (snaps to 1.0x) |
| 74 | Filter cutoff (exponential) |

## Installation

1. Copy `GRID.so` to `/media/BOOT/plugins/GRID/`
2. Create `/media/BOOT/samples/`
3. Boot, add GRID to network

## Building

```bash
cd Percussa-SSP--GRID
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR=$HOME/Code/JUCE7/ \
      -DVSTSDK=$HOME/Code/VST_SDK/ \
      -DBUILDROOT=$HOME/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot .
make -j$(sysctl -n hw.ncpu) GRID juce_vst3_helper 2>/dev/null || true
echo '#!/bin/bash
exit 0' > juce_vst3_helper && chmod +x juce_vst3_helper
export PATH="$(pwd):$PATH" && make -j$(sysctl -n hw.ncpu)
```

Requires JUCE 7.0.12, ARM cross-compiler, VST3 SDK, SSP SDK.

## Credits

- **TheTechnobear** — architecture patterns, JUCE version intel
- **wavejockey** — testing, CC bugs, encoder speed, lo-fi request, pitch tab suggestion
- **Handsonicsuki** — CV range, Start CV, MIDI disconnect testing
- **ahabzutun** — community enthusiasm
- **Percussa** — SSP hardware and SDK

## License

AGPL-3.0

---

Built in Los Angeles. Tested on hardware.
