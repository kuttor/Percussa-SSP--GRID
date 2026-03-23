<p align="center">
  <h1 align="center">GRID</h1>
  <p align="center"><strong>8-Pad Sample Trigger for Percussa SSP</strong></p>
  <p align="center">v0.1.2-beta · AGPL-3.0 · ARM Cross-Compiled</p>
</p>

An 8-channel sampler that's really fast to set up. Takes influences from BitBox Micro and Elektron devices. Built for the Percussa SSP Eurorack module.
Load WAVs. Hit buttons. Hear sounds. Patch gates from a sequencer. Have a drum kit running in 30 seconds.

"The factory sampler makes me want to throw my rack out the window."
— me, before building this

Install
Download GRID.so from releases. Drop it on your SSP's SD card:
/media/BOOT/plugins/GRID/GRID.so
Eject. Insert. Power on. Done.
What It Does
8 pads. Each one independent.
FeatureStatusOne-shot / Loop / Clocked modesDonePer-pad volume + panDonePer-pad pitch shift (+-24 semitones via resampling)DonePer-pad start/end markersDoneGranular time stretch (0.5x - 2.0x)Done8 gate CV inputs (trigger pads from sequencers)Done8 pitch CV inputs (1V/oct per pad)DoneStereo output (L/R)DoneFile browser with durationsDoneState save/recallComingMIDI note-to-pad mappingComingRecord to pad (live sampling)Coming
The UI
GRID always shows your 8 pads. Tabs change what the encoders do — you never lose sight of the grid.

VU-colored waveforms per pad (green / yellow / red by amplitude)
Red progress fill sweeps across each pad during playback
Start/end markers visible on waveforms — audio outside region is dimmed
ContextBar at top shows selected pad specs in red (mode, volume, pitch)
Split-panel file browser — stays open while you load samples across pads
Pitch/stretch indicators show on pads when non-default

Tabs
TabEncodersWhat You SeePADSPad selectThe grid, alwaysSAMPLEStart / EndDetail waveform with markersPLAYMode / Vol / PanMode name, volume/pan valuesWARPPitch / TimeBidirectional bars (center = default)
Controls
ControlActionButtons 1-8Trigger + select pad (select-only in browser)Shift L/RSwitch tabsEnc 0 turnNavigate pads (all tabs)Enc 0 pushToggle file browser
File Browser:
ControlActionEnc 1 turnBrowse filesEnc 1 pushLoad / enter folderEnc 2 turnSwitch target padEnc 2 pushGo back
CV I/O
17 inputs, 2 outputs. Patch a drum sequencer and go.
INPUTS                          OUTPUTS
Gate1  Gate2  Gate3  Gate4      Left
Gate5  Gate6  Gate7  Gate8      Right
Pitch1 Pitch2 Pitch3 Pitch4
Pitch5 Pitch6 Pitch7 Pitch8
Clock
Gates use per-sample rising edge detection at 0.2V threshold. Only reads patched inputs — no garbage from unconnected channels.
Building From Source
Prerequisites

macOS (Apple Silicon or Intel)
JUCE
SSP Buildroot SDK
Steinberg VST3 SDK
CMake 3.15+

Setup + Build
bashgit clone --recursive https://github.com/kuttor/Percussa-SSP--GRID.git
cd Percussa-SSP--GRID
./configure.sh    # one time
./build.sh        # builds + deploys to SD card if mounted
Architecture
PluginParameters    I/O enums, mono bus layout, constants
SampleSlot (x8)    Buffer, playhead, mode, vol/pan/pitch/stretch
GridEngine          Holds 8 slots, sums to stereo L/R
PluginProcessor     Gate/pitch CV reads, audio output
PluginEditor        Tab UI, pad grid, file browser, encoder dispatch
SSPApi              Bridge between SYNTHOR host and JUCE
Individual mono buses per I/O channel. Direct buffer indices, shared channel space. Follows the established SSP plugin architecture.
Roadmap

 Basic sample playback
 8-pad UI with VU waveforms
 File browser with durations
 Per-pad pitch shifting (resampling)
 Granular time stretch
 Gate/pitch CV inputs per pad
 Mono bus architecture
 State save/recall (APVTS)
 MIDI tab (note-to-pad, CC learn)
 REC tab (live sampling to pads)
 Waveform zoom for long samples
 Star/favorite samples
 Reusable UI framework (SSPShell)

Shoutouts
TheTechnobear (Mark Harris) — The godfather of SSP third-party development. 30+ open source plugins, years of framework iteration, and generous enough to share all of it. GRID's I/O architecture, SSP API bridge, mono bus layout, and build patterns are directly informed by studying his code. His BaseProcessor and plugin framework set the standard. If you use the SSP, you owe this man a ko-fi.
Bert Schiettecatte / Percussa — For building the SSP in the first place, and for publishing the SDK that makes all of this possible. The SSP screen is one of the best displays in Eurorack and deserves better software taking advantage of it.
The Percussa Forum Community — wavejockey, and everyone testing early builds and giving feedback. This module exists because of that community.
JUCE — The audio framework underneath. Cross-compiling from Mac to ARM with full GUI rendering. Magic.
Anthropic Claude — AI-assisted development. Architecture, C++ implementation, build system debugging, UI iteration. Every line reviewed and tested on hardware by a human, but the velocity wouldn't be possible without it.
License
AGPL-3.0 — Required due to JUCE dependency under open source usage. Same license as TheTechnobear's SSP plugins.
See LICENSE for full text.

<p align="center">
  <em>Built in Reno. Tested on hardware. Shipped with impatience.</em>
</p>
