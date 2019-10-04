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
// Saw with variable slope or notch

#ifndef PLAITS_DSP_OSCILLATOR_VARIABLE_SAW_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_VARIABLE_SAW_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include <algorithm>

#include "plaits/dsp/oscillator/oscillator.h"



namespace plaits {

const double kVariableSawNotchDepth = 0.2;

class VariableSawOscillator {
 public:
  VariableSawOscillator() { }
  ~VariableSawOscillator() { }

  void Init() {
    phase_ = 0.0;
    next_sample_ = 0.0;
    previous_pw_ = 0.5;
    high_ = false;
  
    frequency_ = 0.01;
    pw_ = 0.5;
    waveshape_ = 0.0;
  }
  
  void Render(
      double frequency,
      double pw,
      double waveshape,
      double* out,
      size_t size) {
    if (frequency >= kMaxFrequency) {
      frequency = kMaxFrequency;
    }
    
    if (frequency >= 0.25) {
      pw = 0.5;
    } else {
      CONSTRAIN(pw, frequency * 2.0, 1.0 - 2.0 * frequency);
    }

    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
    stmlib::ParameterInterpolator waveshape_modulation(
        &waveshape_, waveshape, size);

    double next_sample = next_sample_;
    
    while (size--) {
      double this_sample = next_sample;
      next_sample = 0.0f;
    
      const double frequency = fm.Next();
      const double pw = pwm.Next();
      const double waveshape = waveshape_modulation.Next();
      const double triangle_amount = waveshape;
      const double notch_amount = 1.0 - waveshape;
      const double slope_up = 1.0 / (pw);
      const double slope_down = 1.0 / (1.0 - pw);

      phase_ += frequency;
      
      if (!high_ && phase_ >= pw) {
        const double triangle_step = (slope_up + slope_down) * frequency * triangle_amount;
        const double notch = (kVariableSawNotchDepth + 1.0 - pw) * notch_amount;
        const double t = (phase_ - pw) / (previous_pw_ - pw + frequency);
        this_sample += notch * stmlib::ThisBlepSample(t);
        next_sample += notch * stmlib::NextBlepSample(t);
        this_sample -= triangle_step * stmlib::ThisIntegratedBlepSample(t);
        next_sample -= triangle_step * stmlib::NextIntegratedBlepSample(t);
        high_ = true;
      } else if (phase_ >= 1.0) {
        phase_ -= 1.0;
        const double triangle_step = (slope_up + slope_down) * frequency * triangle_amount;
        const double notch = (kVariableSawNotchDepth + 1.0) * notch_amount;
        const double t = phase_ / frequency;
        this_sample -= notch * stmlib::ThisBlepSample(t);
        next_sample -= notch * stmlib::NextBlepSample(t);
        this_sample += triangle_step * stmlib::ThisIntegratedBlepSample(t);
        next_sample += triangle_step * stmlib::NextIntegratedBlepSample(t);
        high_ = false;
      }
    
      next_sample += ComputeNaiveSample(
          phase_,
          pw,
          slope_up,
          slope_down,
          triangle_amount,
          notch_amount);
      previous_pw_ = pw;

      *out++ = (2.0 * this_sample - 1.0) / (1.0 + kVariableSawNotchDepth);
    }
    
    next_sample_ = next_sample;
  }


 private:
  inline double ComputeNaiveSample(
      double phase,
      double pw,
      double slope_up,
      double slope_down,
      double triangle_amount,
      double notch_amount) const {
    double notch_saw = phase < pw ? phase : 1.0 + kVariableSawNotchDepth;
    double triangle = phase < pw
        ? phase * slope_up
        : 1.0f - (phase - pw) * slope_down;
    return notch_saw * notch_amount + triangle * triangle_amount;
  }

  // Oscillator state.
  double phase_;
  double next_sample_;
  double previous_pw_;
  bool high_;

  // For interpolation of parameters.
  double frequency_;
  double pw_;
  double waveshape_;

  DISALLOW_COPY_AND_ASSIGN(VariableSawOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_VARIABLE_SAW_OSCILLATOR_H_
