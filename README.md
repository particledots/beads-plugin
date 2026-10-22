# Unofficial Beads — AU / VST3 port (work in progress)

A plugin wrapper around the Mutable Instruments **Beads** DSP, taken from the open-source
release (`insolentconstruct/eurorack`, `beads/` + `stmlib/`). Not a Mutable Instruments product.
Don't contact Emilie Gillet or Mutable Instruments about it.

## Easiest: let GitHub build it
Push this folder to a GitHub repo. `.github/workflows/build.yml` builds the AU and VST3 on a macOS runner;
download `Beads-macOS` from the run's Artifacts (Actions tab > latest run). `INSTALL.txt` inside covers the install.

## Build it yourself (macOS, for Logic Pro)

```
brew install cmake        # if you don't have it
cd beads-plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```
First configure downloads JUCE 8.0.6 (about 100 s). With `COPY_PLUGIN_AFTER_BUILD` on, the AU lands in
`~/Library/Audio/Plug-Ins/Components/` and the VST3 in `~/Library/Audio/Plug-Ins/VST3/`.
Then in Logic: Settings > Plug-in Manager > rescan, or run `killall -9 AudioComponentRegistrar`.
It shows up as "particledots: Unofficial Beads". The AU is declared as a *MIDI-controlled effect*
(`aumf`), so Logic offers it in the MIDI-controlled-effects slot of a channel strip (needed for the MIDI gate).

An unsigned local build may need `xattr -dr com.apple.quarantine` on the bundle, or ad-hoc signing:
`codesign --force --deep -s - "<bundle>"`.

Headless DSP test (any OS): `cmake --build build --target engine_test && build/engine_test`

## Parameters (mapped from the panel)

| Plugin | Module |
|---|---|
| Time, Size, Shape | the three main knobs (Size fully up = delay mode, as on the module) |
| Pitch | semitones, ±48 |
| Time/Size/Shape/Pitch Mod | the four attenuverters. With nothing patched, they set the amount of internal randomness; the sign changes its character |
| Density | density knob (at noon grains stop in Free-run mode, as on the module) |
| Feedback, Dry/Wet, Reverb | the three mix knobs |
| Freeze | freeze button |
| Quality | Dirty tape / Clean tape / Vintage digital / Digital (switching clears the buffer, like the module). Held back while Freeze is on so a frozen buffer is never wiped; the change applies when you release Freeze |
| Mono | mono/stereo buffer (also held back while Freeze is on) |
| Source | **Audio In** = the granular effect on the input (default). **Wavetable Synth** = the module's idle mode: a wavetable oscillator plus wavetable grains, with the audio input ignored. Grain Gate, Density, Time/Size/Shape/Pitch, Dry/Wet (oscillator vs grains), Reverb and Freeze all work in both. Held back while Freeze is on, like Quality |
| Wavetable Bank | 1–12, synth mode only. The module picks the bank with the Feedback knob in this mode; the plugin has a dedicated control instead, and Feedback is greyed out in synth mode. Time sweeps through the 16 waves of the bank. In synth mode Quality sets the bit depth (Dirty tape = 4-bit, Clean tape = 16/6-bit, Vintage digital = 7-bit, Digital = 16-bit) |
| MIDI Pitch | **Our addition, on by default, synth mode only.** MIDI notes set the oscillator's pitch: note 48 (C3) plays the oscillator's own pitch (130.81 Hz) and the Pitch knob is added on top, one semitone per key. Last-note priority (the most recent key still held; when all keys are up the pitch stays on the last note). Pitch changes are applied once per audio buffer, so a very large buffer makes note changes slightly less tight. The module does this with its V/oct input |
| Drone | **Our addition, off by default, synth mode only.** Off = the oscillator only sounds while a MIDI note is held (3 ms fade in, 50 ms fade out). On = the oscillator sounds all the time, as the module's idle mode does. Drone does not touch the grains: they follow Grain Gate as before. Note that in synth mode Dry/Wet blends the oscillator (dry) with the grains (wet), so at 100% wet you hear grains only |
| Load wavetable... / Reset bank | Replaces the selected bank with a WAV file, or puts the factory bank back. See "Loading your own wavetables" below |
| Pitch Snap | **Our addition, off by default.** Pitch settles on unison, semitone, minor/major third, fifth and octaves when within a quarter of a semitone; free everywhere else |
| Soft Grain Edges | **Our addition, off by default.** Lengthens the shortest grain fades (0.5 ms in / 10 ms out becomes 4 ms in / 20 ms out) to smooth the ticks from very short, hard-edged grains. Applies to grains started after you switch it |

**Presets:** 8 factory presets (Init, Slow Bloom, Glass Dust, Tape Smear, Stutter Grid, Octave Fog, Long Haze, Broken Cassette) appear in the host's preset menu (Logic's AU preset list). They set the sound knobs and Quality, not Freeze, Mono or Grain Gate. They are starting points written from the knob meanings and **have not been tuned by ear yet**.
| Grain Gate | **Free-run** = seed button locked; **MIDI gate** = grains only while a MIDI note is held (the module's SEED gate input); **Manual** = the "Seed (manual)" switch |

## How it works / honest limits

* The module DSP is called exactly as the firmware does: 4 kHz block rate (12 frames at 48 kHz),
  at the module's internal rate (24 kHz dirty tape, 32 kHz vintage digital, 48 kHz otherwise). A windowed-sinc
  resampler converts to and from the host rate (bypassed at 48 kHz for the 48 kHz modes).
  Reported latency is about 1–2 ms.
* The host's CV inputs are not modelled; the module's CV and attenuverter logic is reached only through the "Mod" parameters
  (no CV, internal randomness). The assignable CV input, gate-output mode, calibration, and the pot/UI code are left out.
* Synth mode (added Oct 2026) runs the module's own `Synth` code at its fixed 48 kHz, whatever the Quality setting, through the same
  resampler. The oscillator's pitch is C3 (130.81 Hz) plus the Pitch knob plus, with MIDI Pitch on, the held MIDI note (see the table). The total is limited to +-100 semitones. Quality maps to the
  synth's bit depth in the module's order, which is my reading of the code and **not checked against hardware**.
* The wavetables start as the 12 factory banks, compiled in (`resources.cc`: 12 banks x 16 waves x 256 samples, stored as running totals).
  Each plugin instance plays from its own copy, so a loaded bank only affects that instance.

## Loading your own wavetables

In synth mode, pick a Bank (1-12), press **Load wavetable...** and choose a `.wav`. That bank is replaced until you press **Reset bank**.
Loaded banks are saved inside the Logic project (about 11 KB each), so the project still works if the WAV moves or is deleted.
The change takes effect within one audio block; a note or grain that is sounding at that moment may click once.

What the loader accepts:
* 16/24/32-bit integer or 32-bit float WAV, mono or stereo (channels are averaged).
* A Serum-style cycle-length marker (the `clm ` chunk, which GrainKontrol writes) sets the cycle length. Without one: exactly 4096 samples
  = 16 cycles of 256; a multiple of 2048 = cycles of 2048; a multiple of 256 = cycles of 256; a short file = one cycle.
* Cycles of any other length are resampled to 256 samples without aliasing (rebuilt from their harmonics). If the file holds more or fewer
  than 16 cycles, the 16 waves are spread evenly through it with cross-fades between neighbours.
* Each wave has its DC removed and its peak set to the level the factory waves use. A silent or flat file is refused with a message.
* Not supported: compressed WAV (ADPCM, MP3-in-WAV), files over 64 MB, cycle lengths under 8 or over 65536 samples.

**Verified (headless test, `engine_test`):** the bank exported from GrainKontrol as factory bank 12 (`tests/data/bank-12-fm-8-1.wav`) loads
into exactly the table values the firmware stores for that bank (all 4160 match), and played from slot 12 it is sample-for-sample identical
to the factory bank. A second export, a custom morph (`tests/data/bank-07-braids-digital-and-metallic.wav`), loads well formed; its last
wave, which is factory bank 7's wave 16, comes out bit-exact, and every wave converts back to the file's samples to within rounding.
**Not verified:** how banks made from other WAVs sound compared with the hardware or with GrainKontrol's own playback, and whether
GrainKontrol would store a custom wave with exactly the same rounding (the loader matches it wherever the source is a factory wave; for
a custom morph the values can differ by one step of 1/512 of the peak). The exporter's 3-sample rotation and the zero-mean centring were
worked out from these files and the factory table, not from GrainKontrol's source.
* Verified here: the VST3 builds on Linux (Oct 2026 changes included); the DSP wrapper runs at 44.1/48/96 kHz in all four modes with finite
  output; dry-path noise floor around -72 dB; the Freeze lock holds a Quality change back and applies it on release; Soft Grain Edges
  and Pitch Snap keep the output finite in all four modes, and Soft Grain Edges lowers a roughness measure on a sine test (a measurement,
  not a listening test). Synth mode (headless test): makes sound from silent input, finite and bounded, at 44.1/48/96 kHz in all four
  Quality settings; all 12 banks are audible and differ from each other; switching Source back and forth keeps working and is held back
  while frozen. MIDI Pitch and Drone (headless test): +12 semitones doubles the oscillator frequency and +7 gives the equal-tempered fifth (measured by zero crossings); extreme offsets stay finite; with Drone off the oscillator is silent with no note, sounds while one is held and fades out after release without a click larger than its normal waveform step; Drone off leaves fully-wet grains audible. **Not verified:** MIDI Pitch and Drone in Logic, the AU build, loading in Logic, MIDI-gate behaviour in a real host, how the presets sound,
  how synth mode sounds or whether it matches the hardware's idle mode, whether Pitch Snap feels right, and how closely anything sounds
  like the hardware. No A/B against a module was possible.
* Observed, not explained: in the original audio mode, Dirty tape and Clean tape give no wet output at all with Dry/Wet fully up and the
  default knob positions (the headless test sees silence; Vintage digital and Digital do not). This predates synth mode. It may be
  how the tape modes behave with those settings, or a bug. It needs a listen in Logic before anyone changes anything.
* The vendored source is the `insolentconstruct/eurorack` master as of Oct 2026. The zip's README says its firmware has "one change to
  grain stealing"; I couldn't find that changelog in the repo, so I haven't confirmed the vendored source includes it.
* The UI is a basic custom panel (knobs, switches, menus). It is functional rather than polished.

## Licensing

`third_party/` is MIT-licensed code by Emilie Gillet (copyright notices kept in the file headers; `third_party/stmlib/LICENSE`).
Keep those notices if you redistribute. `THIRD-PARTY-LICENSES.txt` (also copied into the downloadable zip) carries the MIT text.
Five small files under `third_party/beads/dsp/` were extended with a `SetSoftEdges` option (marked "Plugin addition" in the source). `wavetable_granular_player.h` has its debug `printf` switched off, and the wavetable voice, grain, player and `synth.h` read their wavetables from a settable pointer instead of the fixed array (all marked "Plugin addition" / "Plugin change"). `tests/data/bank-12-fm-8-1.wav` is a test file derived from the MIT-licensed factory wavetables. The hardware design is CC-BY-SA; none of it is used here. The wrapper code in `src/` has
no licence assigned yet. JUCE is used under its own licence terms (AGPLv3 / commercial); check them before distributing builds.
