# GRID

**8-pad performance sampler for the Percussa SSP.**

Load. Trigger. Perform.

---

## What is GRID?

GRID is an 8-channel sample player, recorder, and performance instrument for the Percussa SSP Eurorack module. Load WAV or AIFF files onto 8 pads, trigger them with CV gates, MIDI, or the hardware buttons, and shape them with pitch shifting, time stretching, fade curves, and start/end markers — all controllable via CV and MIDI CC.

Think MPC meets Octatrack meets Eurorack. No menu diving. Everything visible. Everything patchable. Everything performable.

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
- Clock multipliers and dividers: *8, *4, *2, /1, /2, /4, /8
- MIDI clock support (24 PPQN, hi-res wall-clock timing)
- Beat and bar tracking for performance features

**MIDI**
- Direct USB MIDI device access — no factory MIDI module needed
- Per-pad MIDI channel (OFF, 1-16, OMNI)
- Note On = trigger, Velocity = volume, Note = pitch
- Per-pad CC map (configurable via Config Browser)
- MIDI clock with automatic BPM detection
- MIDI Start/Stop/Continue transport
- Device survives audio engine restarts

**Performance Muting (3 modes)**
- Immediate: mutes fire on button press
- On Release: hold right shift, stage mutes across multiple pads, all commit on shift release
- On Bar: stage mutes, commit on next bar boundary (configurable 1-4 bars ahead)
- Visual feedback: green borders (playing), red (muted), orange (pending change)
- Queued pads show ▶ UNMUTE or ► MUTE text until changes land
- Muting works on empty pads (for future MIDI out support)

**Config Browser (LS+RS)**
- Right-side flyout panel, contextual per-pad
- Per-pad CC remapping (5 CCs: Start, End, Volume, Pan, Stretch)
- Switch pads with buttons 1-8 while config is open
- Global settings: Mute Mode, Preset Switch, Queue Bars (1-4), Debug Msgs, Reboot Plugin
- Queue Bars greys out when not applicable
- Arrow keys change values, up/down navigates rows

**Kits**
- .kit XML files saved to /samples/kits/
- 8 pad slots per kit — sample path, volume, pan, start/end, pitch, stretch, mode, choke, reverse, MIDI channel
- Create from multi-select in file browser
- Relative paths for portability across SD cards
- Browse kits with up/down arrows on main page
- 3-second timeout reverts, left shift commits

**Stacks**
- .stack XML files saved to /samples/stacks/
- Multiple samples per pad: round-robin, random, or velocity split
- Create from multi-select in file browser

**Smart Home (File Browser)**
- Structured landing page with sectioned layout
- KITS / STACKS / RECORDINGS / SAMPLES sections with centered dividers
- Left shift long-press (2 seconds) returns to Smart Home from anywhere
- Go-back stops at sample root — can't escape into the Linux filesystem
- Browser opens to Smart Home on first launch

**Multi-Select**
- Left shift enters multi-select mode in file browser
- Left shift on file: toggles selection (numbered red circles 1-8)
- Left shift on folder: navigates into folder while keeping selections
- Encoder 0 push exits multi-select and shows action popup
- Actions: Create Kit, Create Stack, Delete Selected, Cancel
- Delete confirmation shows file list with Yes/Cancel

**On-Screen Keyboard**
- Full character grid: A-M, N-Z, 0-9, underscore, backspace
- GENERATE button fills random name from 576 combinations
- SAVE button confirms (4-12 character limit, no spaces)
- Arrow keys navigate, left shift types, right shift cancels

**Popup Modal**
- Octatrack-style centered dialog with dark overlay
- Used for multi-select actions, delete confirmation, kit/stack creation
- Left shift selects, left arrow cancels, up/down scrolls

**Options**
- Choke groups (A-H) — open/closed hi-hat behavior
- Reverse — flip sample playback direction
- Enhance — one-shot normalization to 0.95 peak (shows dB gain in ticker)

**CV Control (per pad)**
- Trigger — gate input, rising edge fires the pad
- Pitch — 1V/oct, 0V = original pitch
- Start — 0-1V sets sample start position (shared bus, sample-and-hold on trigger)
- End — 0-1V sets sample end position (shared bus, sample-and-hold on trigger)

**UI**
- Left/right arrows switch tabs
- Right shift is pure mute mode (hold to enter, release to exit)
- Scrolling news ticker for feedback messages
- Choke and reverse symbols visible on pad display

**State**
- Full preset save/load — samples, settings, fade curves, CC maps all persist
- File paths stored relative to samples root for portability
- Global config (performance mode, debug, queue bars) saved with preset

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
| WARP | Pad select / Browser | Pitch (st) | Time stretch | Clock M/D |
| FADE | Pad select / Browser | Fade In (push=LIN/EXP) | Fade Out (push=LIN/EXP) | --- |
| MIDI | Pad select / Browser | Channel (push=OFF) | Device (push=disconnect) | MIDI clock |
| OPTIONS | Pad select / Browser | Choke group (push=NONE) | Reverse (push=toggle) | Enhance (push=normalize) |

Push encoder resets that parameter to default unless noted above.

## Controls

| Input | Normal | Browser | Multi-Select | Config | Popup | Keyboard |
|-------|--------|---------|-------------|--------|-------|----------|
| Buttons 1-8 | Trigger pad | Load to pad | — | Switch pad context | — | — |
| Left/Right | Switch tab | — | — | Change value | — | Navigate grid |
| Up/Down | Browse kits | Scroll files | Scroll files | Scroll rows | Scroll options | Navigate grid |
| Left Shift | Enter multi-select | Select file / Enter folder | Select file / Enter folder | — | Select option | Type character |
| Left Shift (2s) | — | Smart Home | Smart Home | — | — | — |
| Right Shift | Mute mode (hold) | — | — | — | — | Cancel |
| LS+RS | Config Browser | — | — | Close Config | — | — |
| Enc 0 push | Open browser | Close browser | Finish & show popup | Close config | — | — |

## MIDI

GRID opens MIDI devices directly — no factory MIDI module needed. Plug a USB MIDI controller into the SSP and select it on the MIDI tab.

Per-pad MIDI channel: set each pad to listen on a different MIDI channel (or OMNI for all). Note On triggers the pad, velocity scales volume, note number shifts pitch.

Default CC map per channel (configurable in Config Browser):
- CC 1 = Start position
- CC 2 = End position
- CC 7 = Volume
- CC 10 = Pan
- CC 11 = Time stretch

MIDI clock (24 PPQN): enable on MIDI tab enc 3. Overrides CV clock input when active. BPM display settles in about 1 second.

## Installation

1. Copy `GRID.so` to `/media/BOOT/plugins/GRID/` on the SSP's SD card
2. Create a `/media/BOOT/samples/` folder for your samples
3. Boot the SSP, add GRID to your network

## Building from Source

Requires:
- ARM cross-compiler (buildroot toolchain for Rockchip RK3288/RK3399)
- JUCE 7.0.12 (JUCE 8 breaks MIDI on the SSP's Linux kernel)
- VST3 SDK
- Percussa SSP SDK (included as submodule)

```bash
git clone --recursive https://github.com/kuttor/Percussa-SSP--GRID.git
cd Percussa-SSP--GRID

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR=$HOME/Code/JUCE7/ \
      -DVSTSDK=$HOME/Code/VST_SDK/ \
      -DBUILDROOT=$HOME/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot \
      .

make -j$(sysctl -n hw.ncpu) GRID juce_vst3_helper 2>/dev/null || true
echo '#!/bin/bash
exit 0' > juce_vst3_helper
chmod +x juce_vst3_helper
export PATH="$(pwd):$PATH"
make -j$(sysctl -n hw.ncpu)
```

## File Structure

```
src/
  PluginParameters.h    Enums, bus layout, PadCCMap, PerfMode, KitData, StackData
  SampleSlot.h/cpp      Per-pad sample engine (load, trigger, DSP, granular stretch)
  GridEngine.h/cpp      8 slots, mute state, choke groups, stereo mix output
  PluginProcessor.h/cpp Audio processing, CV, clock, MIDI, recording, kit management, state
  PluginEditor.h/cpp    UI (tabs, pads, browser, config browser, popup, keyboard, ticker)
  SSPApi.cpp            Percussa SSP bridge
cmake/
  xcSSP.cmake           ARM cross-compilation toolchain
libs/
  ssp-sdk/              Percussa SSP SDK (submodule)
```

## Roadmap (1.0.0)

- Background thread for WAV/stack loading (atomic swap)
- SSPShell editor refactor (page classes for reuse across plugins)
- Phase vocoder for pitch-independent time stretch
- Per-pad direct outputs
- Stereo waveform display
- Trigger-to-playback latency optimization

## Ecosystem (planned)

GRID is the first in a family of SSP plugins:

- **GRID** — 8-pad performance sampler (this plugin)
- **gSEQ** — step sequencer (Elektron-style parameter locks)
- **gMOD** — motion sequencer / CV recorder
- **gMIX** — performance mixer

All share the same 8-channel UI framework and auto-connect to each other.

## Credits

- **TheTechnobear** — SSP plugin architecture patterns, mono bus convention, JUCE version intel. His open-source SSP modules are the reference implementation for this platform.
- **wavejockey** — relentless testing, bug reports, feature requests, and the arrow-tab navigation idea
- **Handsonicsuki** — CV range mismatch catch, Start CV at 0V bug, MIDI disconnect testing
- **Percussa** — the SSP hardware and SDK

## License

AGPL-3.0. See [LICENSE](LICENSE).

---

Built in Los Angeles. Tested on hardware. Shipped with impatience.
