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
// Integrated wavetable synthesis.

#ifndef PLAITS_DSP_OSCILLATOR_WAVETABLE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_WAVETABLE_OSCILLATOR_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/oscillator.h"



namespace plaits {

class Differentiator {
 public:
  Differentiator() { }
  ~Differentiator() { }

  void Init() {
    previous_ = 0.0;
    lp_ = 0.0;
  }

  double Process(double coefficient, double s) {
    ONE_POLE(lp_, s - previous_, coefficient);
    previous_ = s;
    return lp_;
  }
 private:
  double lp_;
  double previous_;

  DISALLOW_COPY_AND_ASSIGN(Differentiator);
};

template<typename T>
inline double InterpolateWave(
    const T* table,
    int32_t index_integral,
    double index_fractional) {
  double a = static_cast<double>(table[index_integral]);
  double b = static_cast<double>(table[index_integral + 1]);
  double t = index_fractional;
  return a + (b - a) * t;
}

template<typename T>
inline double InterpolateWaveHermite(
    const T* table,
    int32_t index_integral,
    double index_fractional) {
  const double xm1 = static_cast<double>(table[index_integral]);
  const double x0 = static_cast<double>(table[index_integral + 1]);
  const double x1 = static_cast<double>(table[index_integral + 2]);
  const double x2 = static_cast<double>(table[index_integral + 3]);
  const double c = (x1 - xm1) * 0.5;
  const double v = x0 - x1;
  const double w = c + v;
  const double a = w + v + (x2 - x0) * 0.5;
  const double b_neg = w + a;
  const double f = index_fractional;
  return (((a * f) - b_neg) * f + c) * f + x0;
}

template<
    size_t wavetable_size,
    size_t num_waves,
    bool approximate_scale=true,
    bool attenuate_high_frequencies=true>
class WavetableOscillator {
 public:
  WavetableOscillator() { }
  ~WavetableOscillator() { }

  void Init() {
    phase_ = 0.0;
    frequency_ = 0.0;
    amplitude_ = 0.0;
    waveform_ = 0.0;
    lp_ = 0.0;
    differentiator_.Init();
  }

  void Render(
      double frequency,
      double amplitude,
      double waveform,
      const int16_t* const* wavetable,
      double* out,
      size_t size) {
    CONSTRAIN(frequency, 0.0000001, kMaxFrequency)

    if (attenuate_high_frequencies) {
    amplitude *= 1.0 - 2.0 * frequency;
    }
    if (approximate_scale) {
      amplitude *= 1.0 / (frequency * 131072.0);
    }

    stmlib::ParameterInterpolator frequency_modulation(
        &frequency_,
        frequency,
        size);
    stmlib::ParameterInterpolator amplitude_modulation(
        &amplitude_,
        amplitude,
        size);
    stmlib::ParameterInterpolator waveform_modulation(
        &waveform_,
        waveform * double(num_waves - 1.0001),
        size);

    double lp = lp_;
    double phase = phase_;
    while (size--) {
      const double f0 = frequency_modulation.Next();
      const double cutoff = std::min(double(wavetable_size) * f0, 1.0);

      const double scale = approximate_scale ? 1.0 : 1.0 / (f0 * 131072.0);

      phase += f0;
      if (phase >= 1.0) {
        phase -= 1.0;
      }

      const double waveform = waveform_modulation.Next();
      MAKE_INTEGRAL_FRACTIONAL(waveform);

      const double p = phase * double(wavetable_size);
      MAKE_INTEGRAL_FRACTIONAL(p);

      const double x0 = InterpolateWave(
          wavetable[waveform_integral], p_integral, p_fractional);
      const double x1 = InterpolateWave(
          wavetable[waveform_integral + 1], p_integral, p_fractional);

      const double s = differentiator_.Process(
          cutoff,
          (x0 + (x1 - x0) * waveform_fractional) * scale);
      ONE_POLE(lp, s, cutoff);
      *out++ += amplitude_modulation.Next() * lp;
    }
    lp_ = lp;
    phase_ = phase;
  }

 private:
  // Oscillator state.
  double phase_;

  // For interpolation of parameters.
  double frequency_;
  double amplitude_;
  double waveform_;
  double lp_;

  Differentiator differentiator_;

  DISALLOW_COPY_AND_ASSIGN(WavetableOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_WAVETABLE_OSCILLATOR_H_
