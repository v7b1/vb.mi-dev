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
// Sinewave multiplied by and sync'ed to a carrier.

#ifndef PLAITS_DSP_OSCILLATOR_Z_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_Z_OSCILLATOR_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"


namespace plaits {

class ZOscillator {
 public:
  ZOscillator() { }
  ~ZOscillator() { }

  void Init() {
    carrier_phase_ = 0.0;
    discontinuity_phase_ = 0.0;
    formant_phase_ = 0.0;
    next_sample_ = 0.0;

    carrier_frequency_ = 0.0;
    formant_frequency_ = 0.0;
    carrier_shape_ = 0.0;
    mode_ = 0.0;
  }

  void Render(
      double carrier_frequency,
      double formant_frequency,
      double carrier_shape,
      double mode,
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
    stmlib::ParameterInterpolator mode_modulation(
        &mode_,
        mode,
        size);

    double next_sample = next_sample_;

    while (size--) {
      bool reset = false;
      double reset_time = 0.0;

      double this_sample = next_sample;
      next_sample = 0.0;

      const double f0 = carrier_frequency_modulation.Next();
      const double f1 = formant_frequency_modulation.Next();

      discontinuity_phase_ += 2.0 * f0;
      carrier_phase_ += f0;
      reset = discontinuity_phase_ >= 1.0;

      if (reset) {
        discontinuity_phase_ -= 1.0;
        reset_time = discontinuity_phase_ / (2.0 * f0);

        double carrier_phase_before = carrier_phase_ >= 1.0 ? 1.0 : 0.5;
        double carrier_phase_after = carrier_phase_ >= 1.0 ? 0.0 : 0.5;
        double before = Z(
            carrier_phase_before,
            1.0,
            formant_phase_ + (1.0 - reset_time) * f1,
            carrier_shape_modulation.subsample(1.0 - reset_time),
            mode_modulation.subsample(1.0 - reset_time));

        double after = Z(
            carrier_phase_after,
            0.0,
            0.0,
            carrier_shape_modulation.subsample(1.0),
            mode_modulation.subsample(1.0));

        double discontinuity = after - before;
        this_sample += discontinuity * stmlib::ThisBlepSample(reset_time);
        next_sample += discontinuity * stmlib::NextBlepSample(reset_time);
        formant_phase_ = reset_time * f1;

        if (carrier_phase_ > 1.0) {
          carrier_phase_ = discontinuity_phase_ * 0.5;
        }
      } else {
        formant_phase_ += f1;
        if (formant_phase_ >= 1.0) {
          formant_phase_ -= 1.0;
        }
      }

      if (carrier_phase_ >= 1.0) {
        carrier_phase_ -= 1.0;
      }

      next_sample += Z(
          carrier_phase_,
          discontinuity_phase_,
          formant_phase_,
          carrier_shape_modulation.Next(),
          mode_modulation.Next());
      *out++ = this_sample;
    }

    next_sample_ = next_sample;
  }

 private:

  inline double Z(double c, double d, double f, double shape, double mode) {
    double ramp_down = 0.5 * (1.0 + Sine(0.5 * d + 0.25));

    double offset;
    double phase_shift;
    if (mode < 0.333) {
      offset = 1.0;
      phase_shift = 0.25 + mode * 1.50;
    } else if (mode < 0.666) {
      phase_shift = 0.7495 - (mode - 0.33) * 0.75;
      offset = -Sine(phase_shift);
    } else {
      phase_shift = 0.7495 - (mode - 0.33) * 0.75;
      offset = 0.001;
    }

    double discontinuity = Sine(f + phase_shift);
    double contour;
    if (shape < 0.5) {
      shape *= 2.0;
      if (c >= 0.5) {
        ramp_down *= shape;
      }
      contour = 1.0 + (Sine(c + 0.25) - 1.0) * shape;
    } else {
      contour = Sine(c + shape * 0.5);
    }
    return (ramp_down * (offset + discontinuity) - offset) * contour;
  }

  // Oscillator state.
  double carrier_phase_;
  double discontinuity_phase_;
  double formant_phase_;
  double next_sample_;

  // For interpolation of parameters.
  double carrier_frequency_;
  double formant_frequency_;
  double carrier_shape_;
  double mode_;

  DISALLOW_COPY_AND_ASSIGN(ZOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_Z_OSCILLATOR_H_
