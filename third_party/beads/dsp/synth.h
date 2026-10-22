// Copyright 2017 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// The synth that is used when no external signal is available.

#ifndef BEADS_DSP_SYNTH_H_
#define BEADS_DSP_SYNTH_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "beads/dsp/processor.h"
#include "beads/dsp/wavetable_granular_player.h"
#include "beads/dsp/wavetable_voice.h"
#include "beads/dsp/fx/reverb.h"

namespace beads {

const size_t kNumBanks = 12;

class Synth {
 public:
  Synth() { }
  ~Synth() { }
  
  enum Resolution {
    RESOLUTION_4_BIT,
    RESOLUTION_16_BIT_6_BIT,
    RESOLUTION_7_BIT,
    RESOLUTION_16_BIT
  };
  
  void Init(uint16_t* reverb_buffer);
  void Reset();

  // Plugin addition: use this table instead of the built-in factory wavetables
  // (12 banks x 16 waves x 260 int16 values). The pointer must stay valid for
  // as long as the synth can play; the contents may change between blocks.
  void SetWavetables(const int16_t* wavetables) {
    wavetable_granular_player_.set_wavetables(wavetables);
    wavetable_voice_.set_wavetables(wavetables);
  }
  
  // Plugin addition: scales the continuous oscillator (the "dry" side of Dry/Wet)
  // without touching the grains. 1 = as on the module. Smoothed within each block;
  // the caller is expected to move it gradually.
  void SetOscillatorLevel(float level) { oscillator_level_ = level; }
  
  void Render(
      SeedButtonState seed_button_state,
      const stmlib::GateFlags* seed_input,
      const bool* freeze,
      Resolution quality,
      const ModulatedParameter& density,
      const Parameters& parameters,
      const Processor::MixParameters& mix_parameters,
      FloatFrame* out,
      size_t size);
  
 private:
  TriggerGenerator trigger_generator_;
  
  float dry_;
  float wet_;
  float oscillator_level_ = 1.0f;
  
  Reverb reverb_;
  Limiter pre_reverb_limiter_;
  Limiter output_limiter_;
  
  WavetableGranularPlayer wavetable_granular_player_;
  WavetableVoice wavetable_voice_;
  
  stmlib::HysteresisQuantizer2 bank_quantizer_;
  
  DISALLOW_COPY_AND_ASSIGN(Synth);
};

}  // namespace beads

#endif  // BEADS_DSP_SYNTH_H_
