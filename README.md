# GRID

**8-pad sampler for the Percussa SSP.**

Load. Trigger. Done.

---

## What is GRID?

GRID is an 8-channel sample player and recorder for the Percussa SSP Eurorack module. Load WAV or AIFF files onto 8 pads, trigger them with CV gates or the hardware buttons, shape them with pitch shifting, time stretching, fade curves, and start/end markers — all controllable via CV.

Think MPC meets Eurorack. No menu diving. Everything visible. Everything patchable.

## Features

**Sampling**
- 8 independent pads, each with its own sample
- WAV and AIFF support (any sample rate, mono or stereo)
- One-shot, loop, clocked loop, and clocked bar modes
- Per-pad volume, pan, pitch (+-48 semitones), time stretch (0.25x-4.0x)
- Per-pad start/end markers with CV control
- Per-pad fade in/out with linear or exponential curves
- Anti-click crossfade on trigger and retrigger
- Instant retrigger — no queuing, no delay

**Recording**
- Stereo audio input (Rec L / Rec R buses)
- Three record modes: Instant, Threshold, Next Bar
- Configurable max length: 1s, 2s, 5s, 10s, 30s, 60s
- Rec Gate input for hands-free operation (foot pedal, external trigger)
- Auto-saves recordings as WAV to /recordings/ folder
- Recorded audio loads directly onto the target pad

**Clock Sync**
- Clock input with BPM detection (displayed on screen)
- CLK LOOP mode: auto time-stretch to match one clock pulse
- CLK BAR mode: auto time-stretch to match one bar (4 beats in 4/4)
- Clock division setting (/1, /2, /4, /8)
- MIDI clock support (24 PPQN, overrides CV clock)

**MIDI**
- Direct USB MIDI device access — no factory MIDI module needed
- Per-pad MIDI channel (OFF, 1-16, OMNI)
- Note On = trigger, Velocity = volume, Note = pitch
- Default CC map: Start, End, Volume, Pan, Stretch
- MIDI clock with automatic BPM detection

**Options**
- Choke groups (A-H) — open/closed hi-hat behavior
- Reverse — flip sample playback direction
- Enhance — one-shot normalization to 0.95 peak

**CV Control (per pad)**
- Trigger — gate input, rising edge fires the pad
- Pitch — 1V/oct, 0V = original pitch
- Start — 0-1V sets sample start position
- End — 0-1V sets sample end position

**Browser**
- File browser with duration, sample rate, and stereo/mono info
- Smart Home: quick access to Samples, Kits, Recordings folders
- Clear Pad function to unassign samples
- Audition: load and play highlighted file with one push
- Browser stays open after loading — load 8 samples without closing

**State**
- Full preset save/load — samples, settings, fade curves all persist
- File paths stored relative to samples root for portability

## I/O Layout

```
Inputs (23 total):
  P1 Trig, P1 Pitch
  P2 Trig, P2 Pitch
  P3 Trig, P3 Pitch
  P4 Trig, P4 Pitch
  P5 Trig, P5 Pitch
  P6 Trig, P6 Pitch
  P7 Trig, P7 Pitch
  P8 Trig, P8 Pitch
  Clock
  Reset
  Start CV
  End CV
  Rec Gate
  Rec L, Rec R

Outputs (2):
  Left, Right (stereo mix)
```

## Tabs

| Tab | Enc 0 | Enc 1 | Enc 2 | Enc 3 |
|-----|-------|-------|-------|-------|
| PADS | Pad select / Browser | REC arm/stop | Rec mode | Max length |
| SAMPLE | Pad select / Browser | Start % | End % | --- |
| PLAY | Pad select / Browser | Mode | Volume (push=MUTE) | Pan |
| WARP | Pad select / Browser | Pitch (st) | Time stretch | Clock div |
| FADE | Pad select / Browser | Fade In (push=LIN/EXP) | Fade Out (push=LIN/EXP) | --- |
| MIDI | Pad select / Browser | Channel (push=OFF) | Device (push=disconnect) | MIDI clock |
| OPTIONS | Pad select / Browser | Choke group (push=NONE) | Reverse (push=toggle) | Enhance (push=normalize) |

Push encoder resets that parameter to default unless noted above.

Buttons 1-8 always trigger the corresponding pad. In loop mode, pressing a playing pad stops it. Hold right shift + buttons to toggle mutes.

## MIDI

GRID opens MIDI devices directly — no factory MIDI module needed. Plug a USB MIDI controller into the SSP and select it on the MIDI tab.

Per-pad MIDI channel: set each pad to listen on a different MIDI channel (or OMNI for all). Note On triggers the pad, velocity scales volume, note number shifts pitch.

Default CC map per channel:
- CC 1 = Start position
- CC 2 = End position
- CC 7 = Volume
- CC 10 = Pan
- CC 11 = Time stretch

MIDI clock (24 PPQN): enable on MIDI tab enc 3. Overrides CV clock input when active.

## Options

Choke groups (A-H): assign pads to a group. Triggering one pad in a group stops all others in that group. Classic open/closed hi-hat behavior.

Reverse: flips the sample buffer. Waveform displays reversed. Toggle on/off.

Enhance: one-shot normalization. Scans peak amplitude, applies gain to reach 0.95. Push enc 3 to apply.

## Installation

1. Copy `GRID.so` to `/media/BOOT/plugins/GRID/` on the SSP's SD card
2. Create a `/media/BOOT/samples/` folder for your samples
3. Boot the SSP, add GRID to your network

## Building from Source

Requires:
- ARM cross-compiler (buildroot toolchain for Rockchip RK3288)
- JUCE framework
- VST3 SDK
- Percussa SSP SDK (included as submodule)

```bash
git clone --recursive https://github.com/kuttor/Percussa-SSP--GRID.git
cd Percussa-SSP--GRID

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR=/path/to/JUCE \
      -DVSTSDK=/path/to/VST_SDK \
      -DBUILDROOT=/path/to/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot \
      .

# Build
make -j$(nproc) GRID GRID_vst3_helper 2>/dev/null || true
echo '#!/bin/bash
exit 0' > GRID_vst3_helper
chmod +x GRID_vst3_helper
export PATH="$(pwd):$PATH"
make -j$(nproc)

# Strip and deploy
arm-linux-gnueabihf-strip GRID_artefacts/Release/VST3/GRID.vst3/Contents/armv7l-linux/GRID.so
cp GRID_artefacts/Release/VST3/GRID.vst3/Contents/armv7l-linux/GRID.so /path/to/BOOT/plugins/GRID/
```

## File Structure

```
src/
  PluginParameters.h    I/O enums, bus layout, constants
  SampleSlot.h/cpp      Per-pad sample engine (load, trigger, DSP)
  GridEngine.h/cpp      8 slots, stereo mix output
  PluginProcessor.h/cpp Audio processing, CV reading, clock, recording, state
  PluginEditor.h/cpp    UI (tabs, pads, browser, encoders)
  SSPApi.cpp            Percussa SSP bridge
cmake/
  xcSSP.cmake           ARM cross-compilation toolchain
libs/
  ssp-sdk/              Percussa SSP SDK (submodule)
```

## Roadmap

- Config Browser — per-pad CC remapping (left+right shift flyout)
- Kit files (.kit) — save/load 8-pad configurations
- Stack files (.stack) — round-robin / random sample cycling per pad
- Multi-select in browser to create kits and stacks
- Per-pad direct outputs
- Stereo waveform display
- Background thread for WAV file writes
- Editor refactor (SSPShell framework for reuse across plugins)

## Ecosystem (planned)

GRID is the first in a family of SSP plugins:

- **GRID** — 8-pad sampler (this plugin)
- **gSEQ** — step sequencer (Elektron-style parameter locks)
- **gMOD** — motion sequencer / CV recorder
- **gMIX** — performance mixer

All share the same 8-channel UI framework and auto-connect to each other.

## Credits

- **TheTechnobear** — SSP plugin architecture patterns, mono bus convention, code inspiration. His open-source SSP modules are the reference implementation for this platform.
- **wavejockey** — testing, bug reports, feature requests, patience
- **Percussa** — the SSP hardware and SDK

## License

AGPL-3.0. See [LICENSE](LICENSE).

---

Built in Los Angeles. Tested on hardware. Shipped with impatience.
