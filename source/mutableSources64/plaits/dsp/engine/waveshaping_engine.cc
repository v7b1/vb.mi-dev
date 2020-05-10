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
// Slope -> Waveshaper -> Wavefolder.

#include "plaits/dsp/engine/waveshaping_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/dsp.h"

#include "plaits/resources.h"


namespace plaits {

using namespace std;
using namespace stmlib;

void WaveshapingEngine::Init(BufferAllocator* allocator) {
  slope_.Init();
  triangle_.Init();
  previous_shape_ = 0.0;
  previous_wavefolder_gain_ = 0.0;
  previous_overtone_gain_ = 0.0;
}

void WaveshapingEngine::Reset() {
  
}

double Tame(double f0, double harmonics, double order) {
  f0 *= harmonics;
  double max_f = 0.5 / order;
  double max_amount = 1.0 - (f0 - max_f) / (0.5 - max_f);
  CONSTRAIN(max_amount, 0.0, 1.0);
  return max_amount * max_amount * max_amount;
}

void WaveshapingEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  const double root = parameters.note;
  
  const double f0 = NoteToFrequency(root);
  const double pw = parameters.morph * 0.45 + 0.5;
  
  // Start from bandlimited slope signal.
  slope_.Render<OSCILLATOR_SHAPE_SLOPE>(f0, pw, out, size);
  triangle_.Render<OSCILLATOR_SHAPE_SLOPE>(f0, 0.5, aux, size);

  // Try to estimate how rich the spectrum is, and reduce the range of the
  // waveshaping control accordingly.
  const double slope = 3.0 + fabs(parameters.morph - 0.5) * 5.0;
  const double shape_amount = fabs(parameters.harmonics - 0.5) * 2.0;
  const double shape_amount_attenuation = Tame(f0, slope, 16.0);
  const double wavefolder_gain = parameters.timbre;
  const double wavefolder_gain_attenuation = Tame(
      f0,
      slope * (3.0 + shape_amount * shape_amount_attenuation * 5.0),
      12.0);
  
  // Apply waveshaper / wavefolder.
  ParameterInterpolator shape_modulation(
      &previous_shape_,
      0.5 + (parameters.harmonics - 0.5) * shape_amount_attenuation,
      size);
  ParameterInterpolator wf_gain_modulation(
      &previous_wavefolder_gain_,
      0.03 + 0.46 * wavefolder_gain * wavefolder_gain_attenuation,
      size);
  const double overtone_gain = parameters.timbre * (2.0 - parameters.timbre);
  ParameterInterpolator overtone_gain_modulation(
      &previous_overtone_gain_,
      overtone_gain * (2.0 - overtone_gain),
      size);
  
  for (size_t i = 0; i < size; ++i) {
    double shape = shape_modulation.Next() * 3.9999;
    MAKE_INTEGRAL_FRACTIONAL(shape);
    
    const int16_t* shape_1 = lookup_table_i16_table[shape_integral];
    const int16_t* shape_2 = lookup_table_i16_table[shape_integral + 1];
    
    double ws_index = 127.0 * out[i] + 128.0;
    MAKE_INTEGRAL_FRACTIONAL(ws_index)
    ws_index_integral &= 255;
    
    double x0 = static_cast<double>(shape_1[ws_index_integral]) / 32768.0;
    double x1 = static_cast<double>(shape_1[ws_index_integral + 1]) / 32768.0;
    double x = x0 + (x1 - x0) * ws_index_fractional;

    double y0 = static_cast<double>(shape_2[ws_index_integral]) / 32768.0;
    double y1 = static_cast<double>(shape_2[ws_index_integral + 1]) / 32768.0;
    double y = y0 + (y1 - y0) * ws_index_fractional;
    
    double mix = x + (y - x) * shape_fractional;
    double index = mix * wf_gain_modulation.Next() + 0.5;
    double fold = InterpolateHermite(
        lut_fold + 1, index, 512.0);
    double fold_2 = -InterpolateHermite(
        lut_fold_2 + 1, index, 512.0);
    
    double sine = InterpolateWrap(lut_sine, aux[i] * 0.25 + 0.5, 1024.0);
    out[i] = fold;
    aux[i] = sine + (fold_2 - sine) * overtone_gain_modulation.Next();
  }
}

}  // namespace plaits
