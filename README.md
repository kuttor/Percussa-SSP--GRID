# GRID — 8-Pad Sample Trigger for Percussa SSP

An 8-pad sample trigger module for the [Percussa SSP](http://www.percussa.com/) Eurorack platform. Load WAVs, hit buttons, hear sounds. Built with JUCE and the SSP SDK.

## What It Does

8 sample slots, each independently triggerable via SSP's soft buttons or CV gate inputs. Per-pad volume, pan, pitch (resampling), start/end markers, and play mode. Stereo output.

## Features

**Playback**
- 8 independent sample pads
- One-shot, Loop, Clocked Loop, Clocked Bar modes
- Per-pad volume and pan (equal-power pan law)
- Per-pad pitch shifting via resampling (±24 semitones / 2 octaves)
- Per-pad start/end region markers
- Time stretch parameter stored (DSP coming)

**UI**
- Always-visible 2×4 pad grid with VU-colored mini waveforms
- Translucent red progress fill sweeps across each pad during playback
- Start/end markers visible on waveforms (dimmed outside region)
- ELAS-style tab bar with red accent, flowing into content
- Split-panel file browser (remembers last folder)
- Top bar shows selected pad specs in red
- 4 tabs: PADS, SAMPLE, PLAY, WARP

**CV**
- 8 gate inputs (Gate1-Gate8) — rising edge triggers corresponding pad
- 8 pitch inputs (Pitch1-Pitch8) — 1V/oct, 0V = original pitch
- 1 clock input
- 2 outputs (Left, Right)
- Only reads from patched inputs (inputEnabled tracking)

## Controls

| Control | Action |
|---------|--------|
| Soft buttons 1-8 | Trigger + select pad |
| Shift L/R | Change tab |
| Arrows | Navigate pad grid |
| Encoder 0 turn | Select pad (all tabs) |
| Encoder 0 push | Open file browser (all tabs) |
| Enc 1/2 on SAMPLE | Start / End position |
| Enc 1/2/3 on PLAY | Mode / Volume / Pan |
| Enc 1/2 on WARP | Pitch (semitones) / Time stretch |

## Building

### Prerequisites

- macOS (Apple Silicon tested)
- ARM cross-compiler: `arm-linux-gnueabihf-gcc` (via Homebrew)
- CMake 3.15+
- [JUCE](https://github.com/juce-framework/JUCE)
- [SSP Buildroot SDK](https://sw13072022.s3.us-west-1.amazonaws.com/arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz)
- [Steinberg VST3 SDK](https://www.steinberg.net/developers/)
- [Percussa SSP SDK](https://github.com/percussa/ssp-sdk) (included as submodule)

### Setup

```bash
git clone --recursive https://github.com/YOUR_USER/ssp-grid.git
cd ssp-grid
```

### Configure (one time)

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR=$HOME/Code/JUCE \
      -DVSTSDK=$HOME/Code/VST_SDK \
      -DBUILDROOT=$HOME/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot \
      .
echo '#!/bin/bash
exit 0' > GRID_vst3_helper
chmod +x GRID_vst3_helper
```

### Build

```bash
make -j$(sysctl -n hw.ncpu)
```

### Deploy

```bash
mkdir -p /Volumes/BOOT/plugins/GRID
cp GRID_artefacts/Release/VST3/GRID.vst3/Contents/armv7l-linux/GRID.so /Volumes/BOOT/plugins/GRID/
```

Eject the SD card, insert into SSP, power on. GRID appears in the module picker.

## Architecture

```
SampleSlot (×8)     Per-pad: buffer, playhead, mode, vol/pan/pitch, start/end
GridEngine           Holds 8 slots, sums all to stereo L/R
PluginProcessor      processBlock reads gate/pitch CVs, writes ch 0,1
PluginEditor         UI: 4-tab layout, file browser, encoder/button dispatch
SSPApi               Bridge between SYNTHOR host and JUCE plugin
PluginParameters     Constants, I/O enums, path finder
```

## SSP Dev Notes

Things we learned building this:

- **Outputs are channels 0,1** — same indices as inputs (shared flat buffer)
- **Unconnected inputs contain garbage** — use `inputEnabled()` callbacks to know what's patched
- **processBlock only runs when outputs are patched** in the SSP grid
- **Read inputs BEFORE `buffer.clear()`** — the SSP shares the buffer
- **JUCE VST3 helper deletes .so on cross-compile** — fake it with `exit 0` wrapper
- **`createPluginFilter()` must be in a PUBLIC source** — we put it in SSPApi.cpp
- **Font rendering** — SSP can't render unicode (em dashes etc) or complex Font styles. Use `g.setFont(size)` and ASCII only
- **Don't delete CMakeCache.txt** — it stores your toolchain config. If you do, you need the full cmake command again
- **Never use `cmake --build .`** — it reconfigures and overwrites the vst3_helper. Use `make` directly
- **Sample path**: `/media/BOOT/samples` (TheTechnobear firmware)

## Roadmap

- [x] Basic sample playback
- [x] 8-pad UI with waveforms
- [x] File browser
- [x] Per-pad pitch shifting (resampling)
- [x] Gate/pitch CV inputs per pad
- [ ] Time stretch DSP (granular or phase vocoder)
- [ ] Waveform zoom for long samples
- [ ] State save/recall
- [ ] Stereo file playback improvements

## Credits

Built on the shoulders of:
- [Percussa SSP SDK](https://github.com/percussa/ssp-sdk)
- [TheTechnobear's SSP plugins](https://github.com/TheTechnobear/SSP) — the reference implementation
- [JUCE framework](https://juce.com/)

## License

GPL-3.0 — same as SSP SDK.
