// Copyright 2014 Olivier Gillet.
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
// Wavetable oscillator with through-zero FM and quadrature outputs.

#ifndef WARPS_DSP_QUADRATURE_OSCILLATOR_H_
#define WARPS_DSP_QUADRATURE_OSCILLATOR_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "warps/dsp/parameters.h"
#include "warps/resources.h"

namespace warps {

class QuadratureOscillator {
 public:
  QuadratureOscillator() { }
  ~QuadratureOscillator() { }
  
  void Init(double sample_rate) {
    one_hertz_ = 1.0 / sample_rate;
    frequency_ = 0.0;
    shape_ = 0.0;
    note_ = 0.0;
    phase_ = 0.0;
  }
  
  void Render(
      double shape,
      double frequency,
      double* i_out,
      double* q_out,
      size_t size) {
    double normalized_frequency = frequency * one_hertz_;
    CONSTRAIN(normalized_frequency, -0.25, 0.25);
    stmlib::ParameterInterpolator frequency_parameter(
        &frequency_,
        normalized_frequency,
        size);
    stmlib::ParameterInterpolator shape_parameter(&shape_, shape, size);

    double phase = phase_;
    while (size--) {
      phase += frequency_parameter.Next();
      
      if (phase <= 0.0) {
        phase += 1.0;
      } else if (phase >= 1.0) {
        phase -= 1.0;
      }
      
      shape = shape_parameter.Next() * 1.9999;
      MAKE_INTEGRAL_FRACTIONAL(shape);
      
      double iq[2];
      for (int32_t component = 0; component < 2; ++component) {
        double a = stmlib::Interpolate(
            wav_table[2 * shape_integral + component],
            phase,
            1024);
        
        double b = stmlib::Interpolate(
            wav_table[2 * shape_integral + 2 + component],
            phase,
            1024);
        
        iq[component] = a + (b - a) * shape_fractional;
      }
      
      *i_out++ = iq[0];
      *q_out++ = iq[1];
    }
    phase_ = phase;
  }
  
 private:
  double one_hertz_;
  
  double phase_;

  double frequency_;
  double shape_;
  double note_;
  
  DISALLOW_COPY_AND_ASSIGN(QuadratureOscillator);
};

}  // namespace warps

#endif  // WARPS_DSP_QUADRATURE_OSCILLATOR_H_
