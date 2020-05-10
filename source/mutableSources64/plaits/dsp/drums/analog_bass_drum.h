// Copyright 2016 Emilie Gillet.
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
// 808 bass drum model, revisited.

#ifndef PLAITS_DSP_DRUMS_ANALOG_BASS_DRUM_H_
#define PLAITS_DSP_DRUMS_ANALOG_BASS_DRUM_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

class AnalogBassDrum {
 public:
  AnalogBassDrum() { }
  ~AnalogBassDrum() { }

  void Init() {
    pulse_remaining_samples_ = 0;
    fm_pulse_remaining_samples_ = 0;
    pulse_ = 0.0;
    pulse_height_ = 0.0;
    pulse_lp_ = 0.0;
    fm_pulse_lp_ = 0.0;
    retrig_pulse_ = 0.0;
    lp_out_ = 0.0;
    tone_lp_ = 0.0;
    sustain_gain_ = 0.0;

    resonator_.Init();
    oscillator_.Init();
  }
  
  inline double Diode(double x) {
    if (x >= 0.0) {
      return x;
    } else {
      x *= 2.0;
      return 0.7 * x / (1.0 + fabs(x));
    }
  }
  
  void Render(
      bool sustain,
      bool trigger,
      double accent,
      double f0,
      double tone,
      double decay,
      double attack_fm_amount,
      double self_fm_amount,
      double* out,
      size_t size) {
      //const double sr = Dsp::getSr();
    const int kTriggerPulseDuration = 1.0e-3 * kSampleRate;
    const int kFMPulseDuration = 6.0e-3 * kSampleRate;
    const double kPulseDecayTime = 0.2e-3 * kSampleRate;
    const double kPulseFilterTime = 0.1e-3 * kSampleRate;
    const double kRetrigPulseDuration = 0.05 * kSampleRate;
    
    const double scale = 0.001 / f0;
    const double q = 1500.0 * stmlib::SemitonesToRatio(decay * 80.0);
    const double tone_f = std::min(
        4.0 * f0 * stmlib::SemitonesToRatio(tone * 108.0),
        1.0);
    const double exciter_leak = 0.08 * (tone + 0.25);
      

    if (trigger) {
      pulse_remaining_samples_ = kTriggerPulseDuration;
      fm_pulse_remaining_samples_ = kFMPulseDuration;
      pulse_height_ = 3.0 + 7.0 * accent;
      lp_out_ = 0.0;
    }
    
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_,
        accent * decay,
        size);
    
    while (size--) {
      // Q39 / Q40
      double pulse = 0.0;
      if (pulse_remaining_samples_) {
        --pulse_remaining_samples_;
        pulse = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - 1.0;
        pulse_ = pulse;
      } else {
        pulse_ *= 1.0 - 1.0 / kPulseDecayTime;
        pulse = pulse_;
      }
      if (sustain) {
        pulse = 0.0;
      }
      
      // C40 / R163 / R162 / D83
      ONE_POLE(pulse_lp_, pulse, 1.0 / kPulseFilterTime);
      pulse = Diode((pulse - pulse_lp_) + pulse * 0.044);

      // Q41 / Q42
      double fm_pulse = 0.0;
      if (fm_pulse_remaining_samples_) {
        --fm_pulse_remaining_samples_;
        fm_pulse = 1.0;
        // C39 / C52
        retrig_pulse_ = fm_pulse_remaining_samples_ ? 0.0 : -0.8;
      } else {
        // C39 / R161
        retrig_pulse_ *= 1.0 - 1.0 / kRetrigPulseDuration;
      }
      if (sustain) {
        fm_pulse = 0.0;
      }
      ONE_POLE(fm_pulse_lp_, fm_pulse, 1.0 / kPulseFilterTime);

      // Q43 and R170 leakage
      double punch = 0.7 + Diode(10.0 * lp_out_ - 1.0);

      // Q43 / R165
      double attack_fm = fm_pulse_lp_ * 1.7 * attack_fm_amount;
      double self_fm = punch * 0.08 * self_fm_amount;
      double f = f0 * (1.0 + attack_fm + self_fm);
      CONSTRAIN(f, 0.0, 0.4);

      double resonator_out;
      if (sustain) {
        oscillator_.Next(f, sustain_gain.Next(), &resonator_out, &lp_out_);
      } else {
        resonator_.set_f_q<stmlib::FREQUENCY_DIRTY>(f, 1.0 + q * f);
        resonator_.Process<stmlib::FILTER_MODE_BAND_PASS,
                           stmlib::FILTER_MODE_LOW_PASS>(
            (pulse - retrig_pulse_ * 0.2) * scale,
            &resonator_out,
            &lp_out_);
      }
      
      ONE_POLE(tone_lp_, pulse * exciter_leak + resonator_out, tone_f);
      
      *out++ = tone_lp_;
    }
  }

 private:
  int pulse_remaining_samples_;
  int fm_pulse_remaining_samples_;
  double pulse_;
  double pulse_height_;
  double pulse_lp_;
  double fm_pulse_lp_;
  double retrig_pulse_;
  double lp_out_;
  double tone_lp_;
  double sustain_gain_;
  
  stmlib::Svf resonator_;
  
  // Replace the resonator in "free running" (sustain) mode.
  SineOscillator oscillator_;
  
  DISALLOW_COPY_AND_ASSIGN(AnalogBassDrum);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_ANALOG_BASS_DRUM_H_
