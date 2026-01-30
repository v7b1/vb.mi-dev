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
// Harmonic oscillator based on Chebyshev polynomials.
// Works well for a small number of harmonics. For the higher order harmonics,
// we need to reinitialize the recurrence by computing two high harmonics.

#ifndef PLAITS_DSP_OSCILLATOR_HARMONIC_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_HARMONIC_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"


namespace plaits {

template<int num_harmonics>
class HarmonicOscillator {
 public:
  HarmonicOscillator() { }
  ~HarmonicOscillator() { }

  void Init() {
    phase_ = 0.0;
    frequency_ = 0.0;
    for (int i = 0; i < num_harmonics; ++i) {
      amplitude_[i] = 0.0;
    }
  }

  template<int first_harmonic_index>
  void Render(
      double frequency,
      const double* amplitudes,
      double* out,
      size_t size) {
    if (frequency >= 0.5) {
      frequency = 0.5;
    }

    stmlib::ParameterInterpolator am[num_harmonics];
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);

    for (int i = 0; i < num_harmonics; ++i) {
      double f = frequency * static_cast<double>(first_harmonic_index + i);
      if (f >= 0.5) {
        f = 0.5;
      }
      am[i].Init(&amplitude_[i], amplitudes[i] * (1.0 - f * 2.0), size);
    }

    while (size--) {
      phase_ += fm.Next();
      if (phase_ >= 1.0) {
        phase_ -= 1.0;
      }
      const double two_x = 2.0 * SineNoWrap(phase_);
      double previous, current;
      if (first_harmonic_index == 1) {
        previous = 1.0;
        current = two_x * 0.5;
      } else {
        const double k = first_harmonic_index;
        previous = Sine(phase_ * (k - 1.0) + 0.25);
        current = Sine(phase_ * k);
      }

      double sum = 0.0;
      for (int i = 0; i < num_harmonics; ++i) {
        sum += am[i].Next() * current;
        double temp = current;
        current = two_x * current - previous;
        previous = temp;
      }
      if (first_harmonic_index == 1) {
        *out++ = sum;
      } else {
        *out++ += sum;
      }
    }
  }

 private:
  // Oscillator state.
  double phase_;

  // For interpolation of parameters.
  double frequency_;
  double amplitude_[num_harmonics];

  DISALLOW_COPY_AND_ASSIGN(HarmonicOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_HARMONIC_OSCILLATOR_H_
