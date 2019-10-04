// Copyright 2015 Olivier Gillet.
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
// Polyblep oscillator used for string synth synthesis.

#ifndef RINGS_DSP_STRING_SYNTH_OSCILLATOR_H_
#define RINGS_DSP_STRING_SYNTH_OSCILLATOR_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

namespace rings {

using namespace stmlib;

enum OscillatorShape {
  OSCILLATOR_SHAPE_BRIGHT_SQUARE,
  OSCILLATOR_SHAPE_SQUARE,
  OSCILLATOR_SHAPE_DARK_SQUARE,
  OSCILLATOR_SHAPE_TRIANGLE,
};

class StringSynthOscillator {
 public:
  StringSynthOscillator() { }
  ~StringSynthOscillator() { }

  inline void Init() {
    phase_ = 0.0;
    phase_increment_ = 0.01;
    filter_state_ = 0.0;
    high_ = false;

    next_sample_ = 0.0;
    next_sample_saw_ = 0.0;

    gain_ = 0.0;
    gain_saw_ = 0.0;
  }
  
  template<OscillatorShape shape, bool interpolate_pitch>
  inline void Render(
      double target_increment,
      double target_gain,
      double target_gain_saw,
      double* out,
      size_t size) {
    // Cut harmonics above 12kHz, and low-pass harmonics above 8kHz to clear
    // highs
    if (target_increment >= 0.17) {
      target_gain *= 1.0 - (target_increment - 0.17) * 12.5;
      if (target_increment >= 0.25) {
        return;
      }
    }
    double phase = phase_;
    ParameterInterpolator phase_increment(
        &phase_increment_,
        target_increment,
        size);
    ParameterInterpolator gain(&gain_, target_gain, size);
    ParameterInterpolator gain_saw(&gain_saw_, target_gain_saw, size);

    double next_sample = next_sample_;
    double next_sample_saw = next_sample_saw_;
    double filter_state = filter_state_;
    bool high = high_;

    while (size--) {
      double this_sample = next_sample;
      double this_sample_saw = next_sample_saw;
      next_sample = 0.0;
      next_sample_saw = 0.0;

      double increment = interpolate_pitch
          ? phase_increment.Next()
          : target_increment;
      phase += increment;
    
      double sample = 0.0;
      const double pw = 0.5;

      if (!high && phase >= pw) {
        double t = (phase - pw) / increment;
        this_sample += ThisBlepSample(t);
        next_sample += NextBlepSample(t);
        high = true;
      }
      if (phase >= 1.0) {
        phase -= 1.0;
        double t = phase / increment;
        double a = ThisBlepSample(t);
        double b = NextBlepSample(t);
        this_sample -= a;
        next_sample -= b;
        this_sample_saw -= a;
        next_sample_saw -= b;
        high = false;
      }
      
      next_sample += phase < pw ? 0.0 : 1.0;
      next_sample_saw += phase;
      
      if (shape == OSCILLATOR_SHAPE_TRIANGLE) {
        const double integrator_coefficient = increment * 0.125;
        this_sample = 64.0 * (this_sample - 0.5f);
        filter_state += integrator_coefficient * (this_sample - filter_state);
        sample = filter_state;
      } else if (shape == OSCILLATOR_SHAPE_DARK_SQUARE) {
        const double integrator_coefficient = increment * 2.0;
        this_sample = 4.0 * (this_sample - 0.5f);
        filter_state += integrator_coefficient * (this_sample - filter_state);
        sample = filter_state;
      } else if (shape == OSCILLATOR_SHAPE_BRIGHT_SQUARE) {
        const double integrator_coefficient = increment * 2.0;
        this_sample = 2.0 * this_sample - 1.0;
        filter_state += integrator_coefficient * (this_sample - filter_state);
        sample = (this_sample - filter_state) * 0.5;
      } else {
        this_sample = 2.0 * this_sample - 1.0;
        sample = this_sample;
      }
      this_sample_saw = 2.0 * this_sample_saw - 1.0;
      
      *out++ += sample * gain.Next() + this_sample_saw * gain_saw.Next();
    }
    high_ = high;
    phase_ = phase;
    next_sample_ = next_sample;
    next_sample_saw_ = next_sample_saw;
    filter_state_ = filter_state;
  }

 private:
  static inline double ThisBlepSample(double t) {
    return 0.5 * t * t;
  }
  static inline double NextBlepSample(double t) {
    t = 1.0 - t;
    return -0.5 * t * t;
  }

  bool high_;
  double phase_;
  double phase_increment_;
  double next_sample_;
  double next_sample_saw_;
  double filter_state_;
  double gain_;
  double gain_saw_;

  DISALLOW_COPY_AND_ASSIGN(StringSynthOscillator);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_SYNTH_OSCILLATOR_H_
