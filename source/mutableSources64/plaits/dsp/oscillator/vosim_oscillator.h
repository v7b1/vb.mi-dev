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
// Two sinewaves multiplied by and sync'ed to a carrier.

#ifndef PLAITS_DSP_OSCILLATOR_VOSIM_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_VOSIM_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/resources.h"



namespace plaits {

class VOSIMOscillator {
 public:
  VOSIMOscillator() { }
  ~VOSIMOscillator() { }

  void Init() {
    carrier_phase_ = 0.0;
    formant_1_phase_ = 0.0;
    formant_2_phase_ = 0.0;
  
    carrier_frequency_ = 0.0;
    formant_1_frequency_ = 0.0;
    formant_2_frequency_ = 0.0;
    carrier_shape_ = 0.0;
  }
  
  void Render(
      double carrier_frequency,
      double formant_frequency_1,
      double formant_frequency_2,
      double carrier_shape,
      double* out,
      size_t size) {
    if (carrier_frequency >= kMaxFrequency) {
      carrier_frequency = kMaxFrequency;
    }
    if (formant_frequency_1 >= kMaxFrequency) {
      formant_frequency_1 = kMaxFrequency;
    }
    if (formant_frequency_2 >= kMaxFrequency) {
      formant_frequency_2 = kMaxFrequency;
    }

    stmlib::ParameterInterpolator f0_modulation(
        &carrier_frequency_,
        carrier_frequency,
        size);
    stmlib::ParameterInterpolator f1_modulation(
        &formant_1_frequency_,
        formant_frequency_1,
        size);
    stmlib::ParameterInterpolator f2_modulation(
        &formant_2_frequency_,
        formant_frequency_2,
        size);
    stmlib::ParameterInterpolator carrier_shape_modulation(
        &carrier_shape_,
        carrier_shape,
        size);

    while (size--) {
      const double f0 = f0_modulation.Next();
      const double f1 = f1_modulation.Next();
      const double f2 = f2_modulation.Next();
    
      carrier_phase_ += carrier_frequency;
      if (carrier_phase_ >= 1.0) {
        carrier_phase_ -= 1.0;
        double reset_time = carrier_phase_ / f0;
        formant_1_phase_ = reset_time * f1;
        formant_2_phase_ = reset_time * f2;
      } else {
        formant_1_phase_ += f1;
        if (formant_1_phase_ >= 1.0) {
          formant_1_phase_ -= 1.0;
        }
        formant_2_phase_ += f2;
        if (formant_2_phase_ >= 1.0) {
          formant_2_phase_ -= 1.0;
        }
      }
      
      double carrier = Sine(carrier_phase_ * 0.5 + 0.25) + 1.0;
      double reset_phase = 0.75 - 0.25 * carrier_shape_modulation.Next();
      double reset_amplitude = Sine(reset_phase);
      double formant_0 = Sine(formant_1_phase_ + reset_phase) - reset_amplitude;
      double formant_1 = Sine(formant_2_phase_ + reset_phase) - reset_amplitude;
      *out++ = carrier * (formant_0 + formant_1) * 0.25 + reset_amplitude;
    }
  }

 private:
  inline double Sine(double phase) {
    return stmlib::InterpolateWrap(lut_sine, phase, 1024.0);
  }

  // Oscillator state.
  double carrier_phase_;
  double formant_1_phase_;
  double formant_2_phase_;

  // For interpolation of parameters.
  double carrier_frequency_;
  double formant_1_frequency_;
  double formant_2_frequency_;
  double carrier_shape_;
  
  DISALLOW_COPY_AND_ASSIGN(VOSIMOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_VOSIM_OSCILLATOR_H_
