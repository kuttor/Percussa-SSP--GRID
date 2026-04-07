# GRID

8-pad performance sampler for the Percussa SSP. Load samples, slice them, pitch them, sequence them from CV or MIDI. Built with JUCE 7 and the SSP SDK.

v2.1.0-beta | AGPL-3.0 | by kuttor

## What it does

GRID gives you 8 sample pads with per-pad pitch, time stretch, filters, lo-fi emulation, and a full slice editor with transient detection. Two CV inputs let you sequence slices from external sources. Direct MIDI device access for note triggering, CC control, and clock sync.

## Install

Drop `GRID.so` onto your SSP's SD card at `/plugins/`. Reboot.

## Pads

8 pads in a 2x4 grid. Buttons trigger samples. Waveform shows playback position with fade envelope shaping. VU-style coloring (green to yellow to red by amplitude).

Playback modes per pad: One-Shot, Loop, Clocked Loop, Clocked One-Shot.

## Pages

8 tabs across the top. Left/right arrows to navigate.

| Tab | Enc 1 | Enc 2 | Enc 3 |
|-----|-------|-------|-------|
| PADS | REC arm/stop | REC mode | REC length |
| SAMPLE | Start trim | End trim | Pitch (semitones) |
| PLAY | Mode | Volume (push=mute) | Pan |
| WARP | Time stretch | CLK multiply/divide | Slice editor (push) |
| FADE | Fade in (push=curve) | Fade out (push=curve) | --- |
| FILTER | Type | Cutoff | Resonance |
| MIDI | Channel | Device | Clock on/off |
| OPTIONS | Choke group | Reverse (push) | Normalize (push) |

Enc 0 is always pad select. Push enc 0 opens the file browser on any page.

## Slice Editor

Push enc 3 on the WARP page to enter.

### Controls

| Control | Action |
|---------|--------|
| Enc 0 turn | Scrub cursor (momentum: slow=precise, fast=fast) |
| Enc 0 push | First push = set slice start. Second push = set slice end. If cursor is inside a slice, removes it |
| Enc 1 turn | Zoom (1x to 32x) |
| Enc 1 push | Snap cursor to nearest zero crossing |
| Enc 2 turn | Select auto-slice strategy: OFF, 8, 16, 24, 32, Transient |
| Enc 2 push | Apply selected strategy. OFF clears all slices |
| Enc 3 turn | Per-slice pitch offset (semitones) |
| Enc 3 push | Reset current slice pitch to 0 |
| Left/Right arrows | Jump between slice boundaries (wraps) |
| Left Shift | Audition current slice |
| Right Shift | Exit slice editor |

### How slicing works

Slices are paired start/end regions. Only defined regions are playable. Everything outside is raw sample, not selectable by CV or MIDI.

Place a start point, move the cursor, place an end point. That's one slice. The number appears in the region. Repeat for up to 64 slices.

Auto-slice (8/16/24/32) creates equal divisions covering the full sample. Transient detection finds drum hits using energy-based onset detection with zero-crossing snap for click-free boundaries.

Each slice can have its own pitch offset. A triangle indicator appears on the slice number when pitched.

Loading a new sample or clearing a pad automatically clears all slices.

### Tape splicing

Destructive delete removes a slice region and joins the remaining audio together like cutting tape and gluing the ends. Stitch marks show where splices happened. 16-level undo stack lets you back out.

### Mini-map

When zoomed, a mini-map at the top shows the full sample with red lines at slice boundaries and a viewport indicator.

## CV Inputs

| Input | Function |
|-------|----------|
| P1-P8 Trig | Trigger gate per pad |
| P1-P8 Pitch | Pitch CV per pad (1V/oct) |
| Clock | External clock for clocked modes |
| Reset | Reset clock phase |
| Slice CV 1 | Selects slice on assigned pad (-5V to +5V) |
| Slice CV 2 | Selects slice on assigned pad (-5V to +5V) |
| Rec Gate | Gate for audio recording |
| Rec L / Rec R | Audio input for recording |
| Filter CV | Filter cutoff modulation |

### Slice CV

Slice CV 1 and Slice CV 2 each control one pad's slice selection. When that pad receives a trigger, the CV value at that instant picks which slice plays.

Assign which pad each CV controls in the Config Browser (LS+RS) under the Slice CV section. Default: CV 1 = Pad 1, CV 2 = Pad 2. Set to OFF to disable.

## MIDI

Direct ALSA MIDI device access. Select device and channel on the MIDI tab.

| Function | How |
|----------|-----|
| Trigger pad | Note On velocity > 0 |
| Slice select (note) | C2 (36) = slice 1, C#2 = slice 2, etc |
| Slice select (CC) | CC Start value 0-127 mapped across slices |
| Start/End trim | CC Start / CC End (configurable per pad) |
| Volume | CC Volume (configurable) |
| Pan | CC Pan (configurable) |
| Stretch | CC Stretch (configurable) |
| Filter cutoff | CC Filter (configurable) |
| Clock | MIDI clock at 24 PPQN (enable on MIDI tab) |

CC assignments are per-pad, configurable in the Config Browser.

## Filters

Six types per pad on the FILTER tab:

- LPF with bass compensation at high resonance
- HPF standard highpass
- BPF gain-boosted bandpass
- Notch band reject
- MS-20 Korg-style with drive and saturation
- Formant vowel filter with 3 parallel bands

All filters have subtle input drive and post-filter soft saturation.

## Lo-Fi Modes

Per-pad in Config Browser:

- 8-Bit harsh bit crush
- 12-Bit cleaner quantization
- SP-1200 12-bit with 26kHz zero-order hold
- MPC-60 mu-law companding with 40kHz zero-order hold

## Performance

Hold Right Shift for mute mode. Buttons toggle mutes. Release commits based on mode.

Double-tap Right Shift for solo mode. Buttons toggle solo per pad.

Mute fade: 0-500ms crossfade on mute/unmute. Mute modes: Immediate, On Release, On Bar. All configurable in Config Browser.

## Config Browser

Hold Left Shift + Right Shift simultaneously.

Per-pad: CLK Beats, Lo-Fi mode, Voice Mode, 6 CC assignments (Start, End, Volume, Pan, Stretch, Filter).

Global: Mute Mode, Queue Bars, Mute Fade, Slice CV 1/2 pad assignments, Preset Switch, Debug Msgs, Reboot Plugin.

## File Browser

Push enc 0 on any page. Smart Home landing with Kits, Stacks, Recordings, Samples sections.

Multi-select with Left Shift for batch loading or kit/stack creation.

Up/Down arrows on the main page browse kits.

## Recording

Enc 1 push on PADS page arms recording. Modes: Instant, Threshold, Next Bar. Records from Rec L/R inputs to selected pad.

## Build

Requires JUCE 7.0.12, VST SDK, SSP cross-compilation toolchain (arm-rockchip-linux-gnueabihf).

```
cd ~/Code/ssp_projects/ssp-elastic-audio
rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake Makefile juce GRID_artefacts
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

Output: `GRID_artefacts/GRID.so` copy to SSP SD card `/plugins/`.

## Credits

TheTechnobear — SSP SDK architecture and open source plugin reference.
wavejockey — primary hardware tester.
Handsonicsuki — CV and CC testing.
Signalsmith Stretch — MIT licensed stretch library.
Percussa — SSP hardware and SDK.

## License

AGPL-3.0. See LICENSE.
