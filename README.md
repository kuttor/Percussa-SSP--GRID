# GRID v0.1.2

### An 8-Channel Sample Trigger for Percussa SSP

---

```
     ██████╗ ██████╗ ██╗██████╗ 
    ██╔════╝ ██╔══██╗██║██╔══██╗
    ██║  ███╗██████╔╝██║██║  ██║
    ██║   ██║██╔══██╗██║██║  ██║
    ╚██████╔╝██║  ██║██║██████╔╝
     ╚═════╝ ╚═╝  ╚═╝╚═╝╚═════╝ 

        Load. Trigger. Done
```

---

## Overview

**GRID** is an 8-channel sampler that's really fast to set up. Takes influences from BitBox Micro and Elektron devices. Built for the [Percussa SSP](http://www.percussa.com/) Eurorack module.

Load WAVs. Hit buttons. Hear sounds. Patch gates from a sequencer. Have a drum kit running in 30 seconds.

The factory sampler has 8 channels too. GRID has waveforms, visual feedback, a file browser that doesn't make you question your life choices, and a UI you can actually read from across the room.

---

## Features

### 🥁 Four Play Modes

| Mode | Behavior |
|------|----------|
| **One-Shot** | Plays once, stops at end marker |
| **Loop** | Loops between start/end markers |
| **Clocked Loop** | Loop synced to clock input |
| **Clocked Bar** | One-shot synced to bar length |

### 🎚️ Per-Pad Control

Every pad is independent. Every pad has its own:

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Volume** | 0-100% | Output level |
| **Pan** | L-R | Equal-power pan law |
| **Pitch** | +-24 semitones | Resampling-based, 4 octave range |
| **Time Stretch** | 0.5x - 2.0x | Granular overlap-add with Hanning window |
| **Start / End** | 0-100% | Region markers, visible on waveform |

### 📺 The UI

GRID always shows your 8 pads. Tabs change what the encoders do — you never lose sight of the grid.

- **VU-colored waveforms** per pad (green / yellow / red by amplitude)
- **Red progress fill** sweeps across each pad during playback, respects start/end region
- **Start/end markers** visible on waveforms — audio outside region is dimmed
- **ContextBar** shows selected pad specs in red (mode, volume, pitch)
- **Pitch/stretch indicators** on pads when non-default (blue text, top-right)
- **White flash** on retrigger

### 📂 File Browser

The browser that stays open. Because loading 8 samples shouldn't take 8 trips.

- Split-panel: browser on left, pads still visible on right
- **Push enc 0** to toggle open/close
- **Soft buttons 1-8** switch target pad without closing browser
- Shows file **durations** next to each file (ms / s / m:ss)
- **Remembers last folder** between opens
- Strips file extensions — you see names, not `.wav`

### 🔌 CV I/O (17 in, 2 out)

Patch a drum sequencer and go.

```
INPUTS                          OUTPUTS
Gate1  Gate2  Gate3  Gate4      Left
Gate5  Gate6  Gate7  Gate8      Right
Pitch1 Pitch2 Pitch3 Pitch4
Pitch5 Pitch6 Pitch7 Pitch8
Clock
```

- **Gates**: Per-sample rising edge detection at 0.2V threshold
- **Pitch**: 1V/oct per pad, 0V = original pitch
- **Clock**: Reserved for clocked modes
- Only reads patched inputs — no garbage from unconnected channels

---

## Tabs

| Tab | Encoders | What You See |
|-----|----------|-------------|
| **PADS** | Pad select | The grid, always |
| **SAMPLE** | Start / End | Detail waveform with markers and playhead |
| **PLAY** | Mode / Vol / Pan | Mode name, volume/pan values |
| **WARP** | Pitch / Time | Bidirectional bars centered on default |

### Coming Soon

| Tab | Purpose |
|-----|---------|
| **REC** | Live sampling — arm a pad, select SSP input, record |
| **MIDI** | Note-to-pad mapping, CC learn, device select |

---

## Controls

### All Tabs

| Control | Action |
|---------|--------|
| **Buttons 1-8** | Trigger + select pad |
| **Shift L / R** | Switch tabs |
| **Enc 0 turn** | Navigate pads |
| **Enc 0 push** | Toggle file browser |

### File Browser

| Control | Action |
|---------|--------|
| **Enc 1 turn** | Browse files |
| **Enc 1 push** | Load file / enter folder |
| **Enc 2 turn** | Switch target pad |
| **Enc 2 push** | Go back (parent folder) |
| **Buttons 1-8** | Select target pad (no trigger) |

### SAMPLE Tab

| Control | Action |
|---------|--------|
| **Enc 1** | Start position |
| **Enc 2** | End position |

### PLAY Tab

| Control | Action |
|---------|--------|
| **Enc 1** | Mode (One-Shot / Loop / Clocked) |
| **Enc 2** | Volume |
| **Enc 3** | Pan |

### WARP Tab

| Control | Action |
|---------|--------|
| **Enc 1** | Pitch (+-24 semitones) |
| **Enc 2** | Time stretch (0.5x - 2.0x) |

---

## Installation

1. Copy `GRID.so` to your SSP's SD card:
   ```
   /media/BOOT/plugins/GRID/GRID.so
   ```

2. Eject. Insert. Power on.

3. GRID appears in the module list.

---

## Architecture

```
PluginParameters     I/O enums, mono bus layout, constants
SampleSlot (x8)      Buffer, playhead, mode, vol/pan/pitch/stretch
GridEngine           Holds 8 slots, sums to stereo L/R
PluginProcessor      Gate/pitch CV reads, audio output
PluginEditor         Tab UI, pad grid, file browser, encoder dispatch
SSPApi               Bridge between SYNTHOR host and JUCE
```

Individual mono buses per I/O channel. Direct buffer indices, shared channel space. Follows TheTechnobear's established SSP plugin architecture.

---

## Signal Flow

```
                 ┌──────────────────────────────────┐
  GATE 1-8 ──────┤  Rising edge detect (0.2V)       │
                 │  Per-sample scan                 │
                 └──────────────┬───────────────────┘
                                │ trigger
                 ┌──────────────┴───────────────────--┐
  PITCH 1-8 ─────┤      SAMPLE SLOT (x8)              │
                 │  ┌────────────────────────────-┐   │
                 │  │ Read position               │   │
                 │  │   += pitchRate              │   │
                 │  │                             │   │
                 │  │ if timeStretch != 1.0:      │   │
                 │  │   Granular OLA (2 grains)   │   │
                 │  │   2048 sample Hanning window│   │
                 │  │   sourcePos += rate/stretch │   │
                 │  │ else:                       │   │
                 │  │   Direct buffer read        │   │
                 │  │                             │   │
                 │  │ Volume + Pan (equal power)  │   │
                 │  └─────────────┬──────────────┘    │
                 └────────────────┼──────────────────-┘
                                  │ (x8 slots summed)
                            ┌─────┴─────-[┐
                            │  GridEngine │
                            │   Stereo    │
                            │    Mix      │
                            └─────┬─────--┘
                                  │
                             OUTPUT L/R
```

---

## Building From Source

### Prerequisites

- macOS (Apple Silicon or Intel)
- [JUCE](https://github.com/juce-framework/JUCE)
- [SSP Buildroot SDK](https://sw13072022.s3.us-west-1.amazonaws.com/arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz)
- [Steinberg VST3 SDK](https://www.steinberg.net/developers/)
- CMake 3.15+

### First Time

```bash
git clone --recursive https://github.com/kuttor/Percussa-SSP--GRID.git
cd Percussa-SSP--GRID
./configure.sh
./build.sh
```

### Subsequent Builds

```bash
./build.sh
```

Auto-deploys to `/Volumes/BOOT/plugins/GRID/` if SD card is mounted.

---

## Technical Specifications

| Specification | Value |
|---------------|-------|
| Max Pads | 8 simultaneous |
| Sample Rate | Follows system (48kHz typical) |
| Bit Depth | 32-bit float internal |
| Pitch Range | +-24 semitones (4 octaves total) |
| Time Stretch | 0.5x - 2.0x (granular OLA) |
| Grain Size | 2048 samples (~42ms at 48kHz) |
| Gate Threshold | 0.2V, per-sample edge detection |
| CV Inputs | 17 (8 gate, 8 pitch, 1 clock) |
| Audio Outputs | 2 (stereo L/R) |
| Latency | Zero (direct buffer read) |
| Pan Law | Equal-power (cos/sin) |

---

## Roadmap

- [x] Basic sample playback
- [x] 8-pad UI with VU-colored waveforms
- [x] File browser with durations
- [x] Per-pad pitch shifting (resampling)
- [x] Granular time stretch
- [x] Gate/pitch CV inputs per pad
- [x] Mono bus architecture
- [ ] State save/recall (APVTS)
- [ ] MIDI tab (note-to-pad, CC learn, visual mapping on pads)
- [ ] REC tab (live sampling from SSP inputs)
- [ ] Waveform zoom for long samples
- [ ] Star/favorite samples
- [ ] Reusable UI framework (`SSPShell`)

---

## Changelog

### v0.1.2-beta (March 2026)
- 🔧 **Mono Bus Architecture** — Individual mono buses per I/O, matching TheTechnobear's proven pattern
- 🔧 **Gate Fix** — Direct buffer indices, per-sample edge detection, 0.2V threshold
- 🔧 **setRateAndBufferSizeDetails** — Correct SSP prepare() call (not setPlayConfigDetails)
- 📝 **AGPL-3.0 License** — Required for JUCE open source usage
- 🔴 **Module Color** — GRID shows red in SSP patching grid

### v0.1.1-beta (March 2026)
- 🎵 **Granular Time Stretch** — 2-grain overlap-add with Hanning window
- 🎹 **Pitch Shifting** — Resampling via pitchRate, +-24 semitones
- 📂 **Browser Improvements** — Stays open, shows durations, remembers folder
- 🎛️ **WARP Tab** — Bidirectional pitch/time bars
- 🔴 **ELAS Red Theme** — All accents `0xFFE53935`, no more orange

### v0.1.0-beta (March 2026)
- 🎉 Initial build
- 8-pad playback with VU waveforms
- File browser on SSP
- Start/end markers
- Per-pad volume, pan, mode
- Progress fill animation
- Button triggers (1-8)

---

## Credits

**GRID** was developed for the [Percussa SSP](http://www.percussa.com/) Eurorack platform.

### Standing on Shoulders

**[TheTechnobear (Mark Harris)](https://github.com/TheTechnobear/SSP)** — The godfather of SSP third-party development. 30+ open source plugins and years of framework iteration. GRID's I/O architecture, SSP API bridge, mono bus layout, and build patterns are directly informed by studying his code. His work made third-party SSP development possible for everyone. If you use the SSP, consider supporting him on [ko-fi](https://ko-fi.com/thetechnobear).

**[Bert Schiettecatte / Percussa](https://github.com/percussa/ssp-sdk)** — For building the SSP and publishing the SDK. The SSP has one of the best screens in Eurorack — it deserves software that takes advantage of it.

**[Percussa Forum Community](https://forum.percussa.com/)** — wavejockey and everyone testing early builds. This module exists because of that community.

**[JUCE](https://juce.com/)** — Cross-compiling from Mac to ARM with full GUI rendering. The framework underneath everything.

**[Anthropic Claude](https://claude.ai/)** — AI-assisted development. Architecture, C++ implementation, build system debugging, UI iteration. Every line reviewed and tested on hardware by a human.

---

## License

**AGPL-3.0** — Required due to JUCE dependency under open source usage. Same license as TheTechnobear's SSP plugins.

See [LICENSE](LICENSE) for full text.

---

## Support

Bug reports, feature requests, or just want to say hi:

**GitHub**: [github.com/kuttor/Percussa-SSP--GRID](https://github.com/kuttor/Percussa-SSP--GRID)
**Forum**: [Percussa Forum — Project GRID](https://forum.percussa.com/t/project-grid-a-better-8-channel-sampler/1977)

---

<div align="center">

```
    ┌───┬───┬───┬───┐
    │ 1 │ 2 │ 3 │ 4 │
    ├───┼───┼───┼───┤
    │ 5 │ 6 │ 7 │ 8 │
    └───┴───┴───┴───┘
```

**GRID** — *Built in Los Angeles. Tested on hardware. Shipped with impatience.*

</div>
