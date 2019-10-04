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
// Single waveform oscillator. Can optionally do audio-rate linear FM, with
// through-zero capabilities (negative frequencies).

#ifndef PLAITS_DSP_OSCILLATOR_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"


namespace plaits {

enum OscillatorShape {
  OSCILLATOR_SHAPE_IMPULSE_TRAIN,
  OSCILLATOR_SHAPE_SAW,
  OSCILLATOR_SHAPE_TRIANGLE,
  OSCILLATOR_SHAPE_SLOPE,
  OSCILLATOR_SHAPE_SQUARE,
  OSCILLATOR_SHAPE_SQUARE_BRIGHT,
  OSCILLATOR_SHAPE_SQUARE_DARK,
  OSCILLATOR_SHAPE_SQUARE_TRIANGLE
};

const double kMaxFrequency = 0.25;
const double kMinFrequency = 0.000001;

class Oscillator {
 public:
  Oscillator() { }
  ~Oscillator() { }
  
  void Init() {
    phase_ = 0.5;
    next_sample_ = 0.0;
    lp_state_ = 1.0;
    hp_state_ = 0.0;
    high_ = true;

    frequency_ = 0.001;
    pw_ = 0.5;
  }

  template<OscillatorShape shape>
  void Render(double frequency, double pw, double* out, size_t size) {
    Render<shape, false, false>(frequency, pw, NULL, out, size);
  }
  
  template<OscillatorShape shape>
  void Render(
      double frequency,
      double pw,
      const double* fm,
      double* out,
      size_t size) {
    if (!fm) {
      Render<shape, false, false>(frequency, pw, NULL, out, size);
    } else {
      Render<shape, true, true>(frequency, pw, fm, out, size);
    }
  }

  template<OscillatorShape shape, bool has_external_fm, bool through_zero_fm>
  void Render(
      double frequency,
      double pw,
      const double* external_fm,
      double* out,
      size_t size) {
    
    if (!has_external_fm) {
      if (!through_zero_fm) {
        CONSTRAIN(frequency, kMinFrequency, kMaxFrequency);
      } else {
        CONSTRAIN(frequency, -kMaxFrequency, kMaxFrequency);
      }
      CONSTRAIN(pw, std::abs(frequency) * 2.0, 1.0 - 2.0 * std::abs(frequency))
    }
    
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
  
    double next_sample = next_sample_;
  
    while (size--) {
      double this_sample = next_sample;
      next_sample = 0.0;

      double frequency = fm.Next();
      if (has_external_fm) {
        frequency *= (1.0 + *external_fm++);
        if (!through_zero_fm) {
          CONSTRAIN(frequency, kMinFrequency, kMaxFrequency);
        } else {
          CONSTRAIN(frequency, -kMaxFrequency, kMaxFrequency);
        }
      }
      double pw = (shape == OSCILLATOR_SHAPE_SQUARE_TRIANGLE ||
                  shape == OSCILLATOR_SHAPE_TRIANGLE) ? 0.5 : pwm.Next();
      if (has_external_fm) {
        CONSTRAIN(pw, std::abs(frequency) * 2.0, 1.0 - 2.0 * std::abs(frequency))
      }
      phase_ += frequency;
      
      if (shape <= OSCILLATOR_SHAPE_SAW) {
        if (phase_ >= 1.0) {
          phase_ -= 1.0;
          double t = phase_ / frequency;
          this_sample -= stmlib::ThisBlepSample(t);
          next_sample -= stmlib::NextBlepSample(t);
        } else if (through_zero_fm && phase_ < 0.0) {
          double t = phase_ / frequency;
          phase_ += 1.0;
          this_sample += stmlib::ThisBlepSample(t);
          next_sample += stmlib::NextBlepSample(t);
        }
        next_sample += phase_;

        if (shape == OSCILLATOR_SHAPE_SAW) {
          *out++ = 2.0 * this_sample - 1.0;
        } else {
          lp_state_ += 0.25 * ((hp_state_ - this_sample) - lp_state_);
          *out++ = 4.0 * lp_state_;
          hp_state_ = this_sample;
        }
      } else if (shape <= OSCILLATOR_SHAPE_SLOPE) {
        double slope_up = 2.0;
        double slope_down = 2.0;
        if (shape == OSCILLATOR_SHAPE_SLOPE) {
          slope_up = 1.0 / (pw);
          slope_down = 1.0 / (1.0 - pw);
        }
        if (high_ ^ (phase_ < pw)) {
          double t = (phase_ - pw) / frequency;
          double discontinuity = (slope_up + slope_down) * frequency;
          if (through_zero_fm && frequency < 0.0) {
            discontinuity = -discontinuity;
          }
          this_sample -= stmlib::ThisIntegratedBlepSample(t) * discontinuity;
          next_sample -= stmlib::NextIntegratedBlepSample(t) * discontinuity;
          high_ = phase_ < pw;
        }
        if (phase_ >= 1.0) {
          phase_ -= 1.0;
          double t = phase_ / frequency;
          double discontinuity = (slope_up + slope_down) * frequency;
          this_sample += stmlib::ThisIntegratedBlepSample(t) * discontinuity;
          next_sample += stmlib::NextIntegratedBlepSample(t) * discontinuity;
          high_ = true;
        } else if (through_zero_fm && phase_ < 0.0) {
          double t = phase_ / frequency;
          phase_ += 1.0;
          double discontinuity = (slope_up + slope_down) * frequency;
          this_sample -= stmlib::ThisIntegratedBlepSample(t) * discontinuity;
          next_sample -= stmlib::NextIntegratedBlepSample(t) * discontinuity;
          high_ = false;
        }
        next_sample += high_
          ? phase_ * slope_up
          : 1.0 - (phase_ - pw) * slope_down;
        *out++ = 2.0 * this_sample - 1.0;
      } else {
        if (high_ ^ (phase_ >= pw)) {
          double t = (phase_ - pw) / frequency;
          double discontinuity = 1.0;
          if (through_zero_fm && frequency < 0.0) {
            discontinuity = -discontinuity;
          }
          this_sample += stmlib::ThisBlepSample(t) * discontinuity;
          next_sample += stmlib::NextBlepSample(t) * discontinuity;
          high_ = phase_ >= pw;
        }
        if (phase_ >= 1.0) {
          phase_ -= 1.0;
          double t = phase_ / frequency;
          this_sample -= stmlib::ThisBlepSample(t);
          next_sample -= stmlib::NextBlepSample(t);
          high_ = false;
        } else if (through_zero_fm && phase_ < 0.0) {
          double t = phase_ / frequency;
          phase_ += 1.0;
          this_sample += stmlib::ThisBlepSample(t);
          next_sample += stmlib::NextBlepSample(t);
          high_ = true;
        }
        next_sample += phase_ < pw ? 0.0 : 1.0;
        
        if (shape == OSCILLATOR_SHAPE_SQUARE_TRIANGLE) {
          const double integrator_coefficient = frequency * 0.0625;
          this_sample = 128.0 * (this_sample - 0.5);
          lp_state_ += integrator_coefficient * (this_sample - lp_state_);
          *out++ = lp_state_;
        } else if (shape == OSCILLATOR_SHAPE_SQUARE_DARK) {
          const double integrator_coefficient = frequency * 2.0;
          this_sample = 4.0 * (this_sample - 0.5);
          lp_state_ += integrator_coefficient * (this_sample - lp_state_);
          *out++ = lp_state_;
        } else if (shape == OSCILLATOR_SHAPE_SQUARE_BRIGHT) {
          const double integrator_coefficient = frequency * 2.0;
          this_sample = 2.0 * this_sample - 1.0;
          lp_state_ += integrator_coefficient * (this_sample - lp_state_);
          *out++ = (this_sample - lp_state_) * 0.5;
        } else {
          this_sample = 2.0 * this_sample - 1.0;
          *out++ = this_sample;
        }
      }
    }
    next_sample_ = next_sample;
  }
  
 private:
  // Oscillator state.
  double phase_;
  double next_sample_;
  double lp_state_;
  double hp_state_;
  bool high_;

  // For interpolation of parameters.
  double frequency_;
  double pw_;
  
  DISALLOW_COPY_AND_ASSIGN(Oscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_OSCILLATOR_H_
