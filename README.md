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
| Quality | Dirty tape / Clean tape / Vintage digital / Digital (switching clears the buffer, like the module) |
| Mono | mono/stereo buffer |
| Grain Gate | **Free-run** = seed button locked; **MIDI gate** = grains only while a MIDI note is held (the module's SEED gate input); **Manual** = the "Seed (manual)" switch |

## How it works / honest limits

* The module DSP is called exactly as the firmware does: 4 kHz block rate (12 frames at 48 kHz),
  at the module's internal rate (24 kHz dirty tape, 32 kHz vintage digital, 48 kHz otherwise). A windowed-sinc
  resampler converts to and from the host rate (bypassed at 48 kHz for the 48 kHz modes).
  Reported latency is about 1–2 ms.
* The host's CV inputs are not modelled; the module's CV and attenuverter logic is reached only through the "Mod" parameters
  (no CV, internal randomness). The assignable CV input, gate-output mode, synth mode (the idle oscillator),
  calibration, and the pot/UI code are left out.
* Verified here: the VST3 and Standalone build on Linux; the DSP wrapper runs at 44.1/48/96 kHz in all four modes with finite
  output; dry-path noise floor around -72 dB. **Not verified:** the AU build, loading in Logic, MIDI-gate behaviour in a real host,
  and how closely it sounds like the hardware. No A/B against a module was possible.
* The vendored source is the `insolentconstruct/eurorack` master as of Oct 2026. The zip's README says its firmware has "one change to
  grain stealing"; I couldn't find that changelog in the repo, so I haven't confirmed the vendored source includes it.
* The UI is a basic custom panel (knobs, switches, menus). It is functional rather than polished.

## Licensing

`third_party/` is MIT-licensed code by Emilie Gillet (copyright notices kept in the file headers; `third_party/stmlib/LICENSE`).
Keep those notices if you redistribute. The hardware design is CC-BY-SA; none of it is used here. The wrapper code in `src/` has
no licence assigned yet. JUCE is used under its own licence terms (AGPLv3 / commercial); check them before distributing builds.
