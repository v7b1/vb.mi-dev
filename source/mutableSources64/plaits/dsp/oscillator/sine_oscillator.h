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
// Simple sine oscillator (wavetable) + fast sine oscillator (magic circle).
//
// The fast implementation might glitch a bit under heavy modulations of the
// frequency.

#ifndef PLAITS_DSP_OSCILLATOR_SINE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_SINE_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/rsqrt.h"

#include "plaits/resources.h"


namespace plaits {

class SineOscillator {
 public:
  SineOscillator() { }
  ~SineOscillator() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
    amplitude_ = 0.0f;
  }
  
  inline double Next(double frequency) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    
    phase_ += frequency;
    if (phase_ >= 1.0) {
      phase_ -= 1.0;
    }
    
    return stmlib::Interpolate(lut_sine, phase_, 1024.0);
  }
  
  inline void Next(double frequency, double amplitude, double* sin, double* cos) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }
    
    phase_ += frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    
    *sin = amplitude * stmlib::Interpolate(lut_sine, phase_, 1024.0);
    *cos = amplitude * stmlib::Interpolate(lut_sine + 256, phase_, 1024.0);
  }
  
  void Render(double frequency, double amplitude, double* out, size_t size) {
    RenderInternal<true>(frequency, amplitude, out, size);
  }
  
  void Render(double frequency, double* out, size_t size) {
    RenderInternal<false>(frequency, 1.0, out, size);
  }

 private:
  template<bool additive>
  void RenderInternal(
      double frequency, double amplitude, double* out, size_t size) {
    if (frequency >= 0.5) {
      frequency = 0.5;
    }
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator am(&amplitude_, amplitude, size);

    while (size--) {
      phase_ += fm.Next();
      if (phase_ >= 1.0) {
        phase_ -= 1.0;
      }
      double s = stmlib::Interpolate(lut_sine, phase_, 1024.0);
      if (additive) {
        *out++ += am.Next() * s;
      } else {
        *out++ = s;
      }
    }
  }
  
  // Oscillator state.
  double phase_;

  // For interpolation of parameters.
  double frequency_;
  double amplitude_;
  
  DISALLOW_COPY_AND_ASSIGN(SineOscillator);
};

class FastSineOscillator {
 public:
  FastSineOscillator() { }
  ~FastSineOscillator() { }

  void Init() {
    x_ = 1.0;
    y_ = 0.0;
    epsilon_ = 0.0;
    amplitude_ = 0.0;
  }
  
  static inline double Fast2Sin(double f) {
    // In theory, epsilon = 2 sin(pi f)
    // Here, to avoid the call to sinf, we use a 3rd order polynomial
    // approximation, which looks like a Taylor expansion, but with a
    // correction term to give a good trade-off between average error
    // (1.13 cents) and maximum error (7.33 cents) when generating sinewaves
    // in the 16 Hz to 16kHz range (with sr = 48kHz).
    const double f_pi = f * M_PI;
    return f_pi * (2.0 - (2.0 * 0.96 / 6.0) * f_pi * f_pi);
  }
  
  void Render(double frequency, double* out, size_t size) {
    RenderInternal<false>(frequency, 1.0, out, size);
  }
  
  void Render(double frequency, double amplitude, double* out, size_t size) {
    RenderInternal<true>(frequency, amplitude, out, size);
  }
  
 private:
  template<bool additive>
  void RenderInternal(
      double frequency, double amplitude, double* out, size_t size) {
    if (frequency >= 0.25) {
      frequency = 0.25;
      amplitude = 0.0;
    } else {
      amplitude *= 1.0 - frequency * 4.0;
    }
    
    stmlib::ParameterInterpolator epsilon(&epsilon_, Fast2Sin(frequency), size);
    stmlib::ParameterInterpolator am(&amplitude_, amplitude, size);
    double x = x_;
    double y = y_;
    
    const double norm = x * x + y * y;
    if (norm <= 0.5 || norm >= 2.0) {
      const double scale = stmlib::fast_rsqrt_carmack(norm);
      x *= scale;
      y *= scale;
    }
    
    while (size--) {
      const double e = epsilon.Next();
      x += e * y;
      y -= e * x;
      if (additive) {
        *out++ += am.Next() * x;
      } else {
        *out++ = x;
      }
    }
    x_ = x;
    y_ = y;
  }
     
  // Oscillator state.
  double x_;
  double y_;

  // For interpolation of parameters.
  double epsilon_;
  double amplitude_;
  
  DISALLOW_COPY_AND_ASSIGN(FastSineOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_SINE_OSCILLATOR_H_
