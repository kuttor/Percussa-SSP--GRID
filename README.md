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
Inputs (36 total):
  P1 Trig, P1 Pitch, P1 Start, P1 End
  P2 Trig, P2 Pitch, P2 Start, P2 End
  P3 Trig, P3 Pitch, P3 Start, P3 End
  P4 Trig, P4 Pitch, P4 Start, P4 End
  P5 Trig, P5 Pitch, P5 Start, P5 End
  P6 Trig, P6 Pitch, P6 Start, P6 End
  P7 Trig, P7 Pitch, P7 Start, P7 End
  P8 Trig, P8 Pitch, P8 Start, P8 End
  Clock
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
| PLAY | Pad select / Browser | Mode | Volume | Pan |
| WARP | Pad select / Browser | Pitch (st) | Time stretch | --- |
| FADE | Pad select / Browser | Fade In (ms) | Fade Out (ms) | --- |

Push encoder resets that parameter to default. On FADE tab, push enc 1 or 2 toggles linear/exponential curve for that fade.

Buttons 1-8 always trigger the corresponding pad. In loop mode, pressing a playing pad stops it.

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

- Clock division setting (1/beat, 24 PPQN, etc)
- Reset input (phase-lock clocked pads)
- Kit files (.kit) — save/load 8-pad configurations
- Stack files (.stack) — round-robin / random sample cycling per pad
- Multi-select in browser to create kits and stacks
- Per-pad direct outputs
- MIDI note-to-pad mapping
- Options flyout menu
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
