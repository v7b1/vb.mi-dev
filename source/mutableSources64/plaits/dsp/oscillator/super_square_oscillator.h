// Copyright 2021 Emilie Gillet.
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
// Two hard-sync'ed square waves with a meta-parameter, also faking PWM.
// Based on VariableShapeOscillator, with hard-coded pulse width (0.5),
// waveshape (only square), and sync enabled by default.

#ifndef PLAITS_DSP_OSCILLATOR_SUPERSQUARE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_SUPERSQUARE_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include <algorithm>

namespace plaits {

class SuperSquareOscillator {
 public:
  SuperSquareOscillator() { }
  ~SuperSquareOscillator() { }

  void Init() {
    master_phase_ = 0.0;
    slave_phase_ = 0.0;
    next_sample_ = 0.0;
    high_ = false;

    master_frequency_ = 0.0;
    slave_frequency_ = 0.01;
  }

  void Render(
      double frequency,
      double shape,
      double* out,
      size_t size) {
    double master_frequency = frequency;
    frequency *= shape < 0.5
        ? (0.51 + 0.98 * shape)
        : 1.0 + 16.0 * (shape - 0.5) * (shape - 0.5);

    if (master_frequency >= kMaxFrequency) {
      master_frequency = kMaxFrequency;
    }

    if (frequency >= kMaxFrequency) {
      frequency = kMaxFrequency;
    }

    stmlib::ParameterInterpolator master_fm(
        &master_frequency_, master_frequency, size);
    stmlib::ParameterInterpolator fm(&slave_frequency_, frequency, size);

    double next_sample = next_sample_;

    while (size--) {
      bool reset = false;
      bool transition_during_reset = false;
      double reset_time = 0.0;

      double this_sample = next_sample;
      next_sample = 0.0;

      const double master_frequency = master_fm.Next();
      const double slave_frequency = fm.Next();

      master_phase_ += master_frequency;
      if (master_phase_ >= 1.0) {
        master_phase_ -= 1.0;
        reset_time = master_phase_ / master_frequency;

        double slave_phase_at_reset = slave_phase_ + \
            (1.0 - reset_time) * slave_frequency;
        reset = true;
        if (slave_phase_at_reset >= 1.0) {
          slave_phase_at_reset -= 1.0;
          transition_during_reset = true;
        }
        if (!high_ && slave_phase_at_reset >= 0.5) {
          transition_during_reset = true;
        }
        double value = slave_phase_at_reset < 0.5 ? 0.0 : 1.0;
        this_sample -= value * stmlib::ThisBlepSample(reset_time);
        next_sample -= value * stmlib::NextBlepSample(reset_time);
      }

      slave_phase_ += slave_frequency;
      while (transition_during_reset || !reset) {
        if (!high_) {
          if (slave_phase_ < 0.5) {
            break;
          }
          double t = (slave_phase_ - 0.5) / slave_frequency;
          this_sample += stmlib::ThisBlepSample(t);
          next_sample += stmlib::NextBlepSample(t);
          high_ = true;
        }

        if (high_) {
          if (slave_phase_ < 1.0) {
            break;
          }
          slave_phase_ -= 1.0;
          double t = slave_phase_ / slave_frequency;
          this_sample -= stmlib::ThisBlepSample(t);
          next_sample -= stmlib::NextBlepSample(t);
          high_ = false;
        }
      }

      if (reset) {
        slave_phase_ = reset_time * slave_frequency;
        high_ = false;
      }

      next_sample += slave_phase_ < 0.5 ? 0.0 : 1.0;
      *out++ = 2.0 * this_sample - 1.0;
    }

    next_sample_ = next_sample;
  }

 private:
  // Oscillator state.
  double master_phase_;
  double slave_phase_;
  double next_sample_;
  bool high_;

  // For interpolation of parameters.
  double master_frequency_;
  double slave_frequency_;

  DISALLOW_COPY_AND_ASSIGN(SuperSquareOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_SUPERSQUARE_OSCILLATOR_H_
