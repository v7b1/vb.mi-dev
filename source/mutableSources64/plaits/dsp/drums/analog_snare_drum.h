// Copyright 2016 Olivier Gillet.
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
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
// 808 snare drum model, revisited.

#ifndef PLAITS_DSP_DRUMS_ANALOG_SNARE_DRUM_H_
#define PLAITS_DSP_DRUMS_ANALOG_SNARE_DRUM_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"


namespace plaits {

class AnalogSnareDrum {
 public:
  AnalogSnareDrum() { }
  ~AnalogSnareDrum() { }

  static const int kNumModes = 5;

  void Init() {
    pulse_remaining_samples_ = 0;
    pulse_ = 0.0;
    pulse_height_ = 0.0;
    pulse_lp_ = 0.0;
    noise_envelope_ = 0.0;
    sustain_gain_ = 0.0;

    for (int i = 0; i < kNumModes; ++i) {
      resonator_[i].Init();
      oscillator_[i].Init();
    }
    noise_filter_.Init();
  }
  
  void Render(
      bool sustain,
      bool trigger,
      double accent,
      double f0,
      double tone,
      double decay,
      double snappy,
      double* out,
      size_t size) {
      const double sr = Dsp::getSr();
    const double decay_xt = decay * (1.0 + decay * (decay - 1.0));
    const int kTriggerPulseDuration = 1.0e-3 * sr;
    const double kPulseDecayTime = 0.1e-3 * sr;
    const double q = 2000.0 * stmlib::SemitonesToRatio(decay_xt * 84.0);
    const double noise_envelope_decay = 1.0 - 0.0017 * \
        stmlib::SemitonesToRatio(-decay * (50.0 + snappy * 10.0));
    const double exciter_leak = snappy * (2.0 - snappy) * 0.1;
    
    snappy = snappy * 1.1 - 0.05;
    CONSTRAIN(snappy, 0.0, 1.0);
    
    if (trigger) {
      pulse_remaining_samples_ = kTriggerPulseDuration;
      pulse_height_ = 3.0 + 7.0 * accent;
      noise_envelope_ = 2.0;
    }
    
    static const double kModeFrequencies[kNumModes] = {
        1.00,
        2.00,
        3.18,
        4.16,
        5.62};
    
    double f[kNumModes];
    double gain[kNumModes];
    
    for (int i = 0; i < kNumModes; ++i) {
      f[i] = std::min(f0 * kModeFrequencies[i], 0.499);
      resonator_[i].set_f_q<stmlib::FREQUENCY_FAST>(
          f[i],
          1.0 + f[i] * (i == 0 ? q : q * 0.25));
    }
    
    if (tone < 0.666667) {
      // 808-style (2 modes)
      tone *= 1.5f;
      gain[0] = 1.5 + (1.0 - tone) * (1.0 - tone) * 4.5;
      gain[1] = 2.0 * tone + 0.15;
      std::fill(&gain[2], &gain[kNumModes], 0.0);
    } else {
      // What the 808 could have been if there were extra modes!
      tone = (tone - 0.666667) * 3.0;
      gain[0] = 1.5 - tone * 0.5;
      gain[1] = 2.15 - tone * 0.7;
      for (int i = 2; i < kNumModes; ++i) {
        gain[i] = tone;
        tone *= tone;
      }
    }

    double f_noise = f0 * 16.0;
    CONSTRAIN(f_noise, 0.0, 0.499);
    noise_filter_.set_f_q<stmlib::FREQUENCY_FAST>(
        f_noise, 1.0 + f_noise * 1.5);
        
    
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_,
        accent * decay,
        size);
    
    while (size--) {
      // Q45 / Q46
      double pulse = 0.0;
      if (pulse_remaining_samples_) {
        --pulse_remaining_samples_;
        pulse = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - 1.0;
        pulse_ = pulse;
      } else {
        pulse_ *= 1.0 - 1. / kPulseDecayTime;
        pulse = pulse_;
      }
      
      double sustain_gain_value = sustain_gain.Next();
      
      // R189 / C57 / R190 + C58 / C59 / R197 / R196 / IC14
      ONE_POLE(pulse_lp_, pulse, 0.75);
      
      double shell = 0.0;
      for (int i = 0; i < kNumModes; ++i) {
        double excitation = i == 0
            ? (pulse - pulse_lp_) + 0.006 * pulse
            : 0.026 * pulse;
        shell += gain[i] * (sustain
            ? oscillator_[i].Next(f[i]) * sustain_gain_value * 0.25
            : resonator_[i].Process<stmlib::FILTER_MODE_BAND_PASS>(
                  excitation) + excitation * exciter_leak);
      }
      shell = stmlib::SoftClip(shell);
      
      // C56 / R194 / Q48 / C54 / R188 / D54
      double noise = 2.0 * stmlib::Random::GetDouble() - 1.0;
      if (noise < 0.0f) noise = 0.0;
      noise_envelope_ *= noise_envelope_decay;
      noise *= (sustain ? sustain_gain_value : noise_envelope_) * snappy * 2.0;

      // C66 / R201 / C67 / R202 / R203 / Q49
      noise = noise_filter_.Process<stmlib::FILTER_MODE_BAND_PASS>(noise);
      
      // IC13
      *out++ = noise + shell * (1.0 - snappy);
    }
  }

 private:
  int pulse_remaining_samples_;
  double pulse_;
  double pulse_height_;
  double pulse_lp_;
  double noise_envelope_;
  double sustain_gain_;
  
  stmlib::Svf resonator_[kNumModes];
  stmlib::Svf noise_filter_;

  // Replace the resonators in "free running" (sustain) mode.
  SineOscillator oscillator_[kNumModes];
  
  DISALLOW_COPY_AND_ASSIGN(AnalogSnareDrum);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_ANALOG_SNARE_DRUM_H_
