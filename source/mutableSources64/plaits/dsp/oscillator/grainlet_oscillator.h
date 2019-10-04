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
// A phase-distortd single cycle sine * another continuously running sine,
// the whole thing synced to a main oscillator.

#ifndef PLAITS_DSP_OSCILLATOR_GRAINLET_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_GRAINLET_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/resources.h"


namespace plaits {

class GrainletOscillator {
 public:
  GrainletOscillator() { }
  ~GrainletOscillator() { }

  void Init() {
    carrier_phase_ = 0.0;
    formant_phase_ = 0.0;
    next_sample_ = 0.0;
  
    carrier_frequency_ = 0.0;
    formant_frequency_ = 0.0;
    carrier_shape_ = 0.0;
    carrier_bleed_ = 0.0;
  }
  
  void Render(
      double carrier_frequency,
      double formant_frequency,
      double carrier_shape,
      double carrier_bleed,
      double* out,
      size_t size) {
    if (carrier_frequency >= kMaxFrequency * 0.5) {
      carrier_frequency = kMaxFrequency * 0.5;
    }
    if (formant_frequency >= kMaxFrequency) {
      formant_frequency = kMaxFrequency;
    }
    
    stmlib::ParameterInterpolator carrier_frequency_modulation(
        &carrier_frequency_,
        carrier_frequency,
        size);
    stmlib::ParameterInterpolator formant_frequency_modulation(
        &formant_frequency_,
        formant_frequency,
        size);
    stmlib::ParameterInterpolator carrier_shape_modulation(
        &carrier_shape_,
        carrier_shape,
        size);
    stmlib::ParameterInterpolator carrier_bleed_modulation(
        &carrier_bleed_,
        carrier_bleed,
        size);

    double next_sample = next_sample_;
    
    while (size--) {
      bool reset = false;
      double reset_time = 0.0;

      double this_sample = next_sample;
      next_sample = 0.0;
    
      const double f0 = carrier_frequency_modulation.Next();
      const double f1 = formant_frequency_modulation.Next();
    
      carrier_phase_ += f0;
      reset = carrier_phase_ >= 1.0;
      
      if (reset) {
        carrier_phase_ -= 1.0;
        reset_time = carrier_phase_ / f0;
        double before = Grainlet(
            1.0,
            formant_phase_ + (1.0 - reset_time) * f1,
            carrier_shape_modulation.subsample(1.0 - reset_time),
            carrier_bleed_modulation.subsample(1.0 - reset_time));

        double after = Grainlet(
            0.0,
            0.0,
            carrier_shape_modulation.subsample(1.0),
            carrier_bleed_modulation.subsample(1.0));

        double discontinuity = after - before;
        this_sample += discontinuity * stmlib::ThisBlepSample(reset_time);
        next_sample += discontinuity * stmlib::NextBlepSample(reset_time);
        formant_phase_ = reset_time * f1;
      } else {
        formant_phase_ += f1;
        if (formant_phase_ >= 1.0) {
          formant_phase_ -= 1.0;
        }
      }
      
      next_sample += Grainlet(
          carrier_phase_,
          formant_phase_,
          carrier_shape_modulation.Next(),
          carrier_bleed_modulation.Next());
      *out++ = this_sample;
    }
    
    next_sample_ = next_sample;
  }

 private:
  inline double Sine(double phase) {
    return stmlib::InterpolateWrap(lut_sine, phase, 1024.0);
      //return std::sin(phase*2*M_PI);
  }
  
  inline double Carrier(double phase, double shape) {
    shape *= 3.0;
    MAKE_INTEGRAL_FRACTIONAL(shape);
    double t = 1.0 - shape_fractional;
    
    if (shape_integral == 0) {
      phase = phase * (1.0 + t * t * t * 15.0);
      if (phase >= 1.0) {
        phase = 1.0;
      }
      phase += 0.75;
    } else if (shape_integral == 1) {
      double breakpoint = 0.001 + 0.499 * t * t * t;
      if (phase < breakpoint) {
        phase *= (0.5 / breakpoint);
      } else {
        phase = 0.5 + (phase - breakpoint) * 0.5f / (1.0 - breakpoint);
      }
      phase += 0.75;
    } else {
      t = 1.0 - t;
      phase = 0.25 + phase * (0.5 + t * t * t * 14.5);
      if (phase >= 0.75) phase = 0.75;
    }
    return (Sine(phase) + 1.0) * 0.25;
  }

  inline double Grainlet(
      double carrier_phase,
      double formant_phase,
      double shape,
      double bleed) {
    double carrier = Carrier(carrier_phase, shape);
    double formant = Sine(formant_phase);
    return carrier * (formant + bleed) / (1.0 + bleed);
  }

  // Oscillator state.
  double carrier_phase_;
  double formant_phase_;
  double next_sample_;

  // For interpolation of parameters.
  double carrier_frequency_;
  double formant_frequency_;
  double carrier_shape_;
  double carrier_bleed_;
  
  DISALLOW_COPY_AND_ASSIGN(GrainletOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_GRAINLET_OSCILLATOR_H_
