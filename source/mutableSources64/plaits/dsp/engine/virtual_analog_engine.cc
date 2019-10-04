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
// 2 variable shape oscillators with sync, FM and crossfading.

#include "plaits/dsp/engine/virtual_analog_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"


namespace plaits {

using namespace std;
using namespace stmlib;

void VirtualAnalogEngine::Init(BufferAllocator* allocator) {
  primary_.Init();
  auxiliary_.Init();
  sync_.Init();
  variable_saw_.Init();
  
  auxiliary_amount_ = 0.0;
  xmod_amount_ = 0.0;
  
  temp_buffer_ = allocator->Allocate<double>(kMaxBlockSize);
}

void VirtualAnalogEngine::Reset() {
  
}

const double intervals[5] = {
  0.0, 7.01, 12.01, 19.01, 24.01
};

inline double Squash(double x) {
  return x * x * (3.0 - 2.0 * x);
}

double VirtualAnalogEngine::ComputeDetuning(double detune) const {
  detune = 2.05 * detune - 1.025;
  CONSTRAIN(detune, -1.0, 1.0);
  
  double sign = detune < 0.0 ? -1.0 : 1.0;
  detune = detune * sign * 3.9999;
  MAKE_INTEGRAL_FRACTIONAL(detune);
  
  double a = intervals[detune_integral];
  double b = intervals[detune_integral + 1];
  return (a + (b - a) * Squash(Squash(detune_fractional))) * sign;
}

void VirtualAnalogEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {

#if VA_VARIANT == 0
  
  // 1 = variable waveshape controlled by TIMBRE.
  // 2 = variable waveshape controlled by MORPH, detuned by HARMONICS.
  // OUT = 1 + 2.
  // AUX = 1 + sync 2.
  const double auxiliary_detune = ComputeDetuning(parameters.harmonics);
  const double primary_f = NoteToFrequency(parameters.note);
  const double auxiliary_f = NoteToFrequency(parameters.note + auxiliary_detune);
  const double sync_f = NoteToFrequency(
      parameters.note + parameters.harmonics * 48.0);

  double shape_1 = parameters.timbre * 1.5;
  CONSTRAIN(shape_1, 0.0, 1.0);

  double pw_1 = 0.5 + (parameters.timbre - 0.66) * 1.4;
  CONSTRAIN(pw_1, 0.5, 0.99);

  double shape_2 = parameters.morph * 1.5;
  CONSTRAIN(shape_2, 0.0, 1.0);

  double pw_2 = 0.5 + (parameters.morph - 0.66) * 1.4;
  CONSTRAIN(pw_2, 0.5, 0.99);
  
  primary_.Render<false>(
      primary_f, primary_f, pw_1, shape_1, temp_buffer_, size);
  auxiliary_.Render<false>(auxiliary_f, auxiliary_f, pw_2, shape_2, aux, size);
  for (size_t i = 0; i < size; ++i) {
    out[i] = (aux[i] + temp_buffer_[i]) * 0.5;
  }
  
  sync_.Render<true>(primary_f, sync_f, pw_2, shape_2, aux, size);
  for (size_t i = 0; i < size; ++i) {
    aux[i] = (aux[i] + temp_buffer_[i]) * 0.5;
  }

#elif VA_VARIANT == 1
  
  // 1 = variable waveshape controlled by MORPH.
  // 2 = variable waveshape controlled by MORPH.
  // OUT = crossfade between 1 + 2, 1, 1 sync 2 controlled by TIMBRE.
  // AUX = 2.
  
  double auxiliary_amount = max(0.5 - parameters.timbre, 0.0) * 2.0;
  auxiliary_amount *= auxiliary_amount * 0.5;

  const double xmod_amount = max(parameters.timbre - 0.5, 0.0) * 2.0;
  const double squashed_xmod_amount = xmod_amount * (2.0 - xmod_amount);

  const double auxiliary_detune = ComputeDetuning(parameters.harmonics);
  const double primary_f = NoteToFrequency(parameters.note);
  const double auxiliary_f = NoteToFrequency(parameters.note + auxiliary_detune);
  const double sync_f = primary_f * SemitonesToRatio(
      xmod_amount * (auxiliary_detune + 36.0));

  double shape = parameters.morph * 1.5;
  CONSTRAIN(shape, 0.0, 1.0);

  double pw = 0.5 + (parameters.morph - 0.66) * 1.4;
  CONSTRAIN(pw, 0.5, 0.99);

  primary_.Render<false>(primary_f, primary_f, pw, shape, out, size);
  sync_.Render<true>(primary_f, sync_f, pw, shape, aux, size);

  ParameterInterpolator xmod_amount_modulation(
      &xmod_amount_,
      squashed_xmod_amount * (2.0 - squashed_xmod_amount),
      size);
  for (size_t i = 0; i < size; ++i) {
    out[i] += (aux[i] - out[i]) * xmod_amount_modulation.Next();
  }

  auxiliary_.Render<false>(auxiliary_f, auxiliary_f, pw, shape, aux, size);

  ParameterInterpolator auxiliary_amount_modulation(
      &auxiliary_amount_,
      auxiliary_amount,
      size);
  for (size_t i = 0; i < size; ++i) {
    out[i] += (aux[i] - out[i]) * auxiliary_amount_modulation.Next();
  }
  
#elif VA_VARIANT == 2

  // 1 = variable square controlled by TIMBRE.
  // 2 = variable saw controlled by MORPH.
  // OUT = 1 + 2.
  // AUX = dual variable waveshape controlled by MORPH, self sync by TIMBRE.
  
  const double sync_amount = parameters.timbre * parameters.timbre;
  const double auxiliary_detune = ComputeDetuning(parameters.harmonics);
  const double primary_f = NoteToFrequency(parameters.note);
  const double auxiliary_f = NoteToFrequency(parameters.note + auxiliary_detune);
  const double primary_sync_f = NoteToFrequency(
      parameters.note + sync_amount * 48.0);
  const double auxiliary_sync_f = NoteToFrequency(
      parameters.note + auxiliary_detune + sync_amount * 48.0);

  double shape = parameters.morph * 1.5;
  CONSTRAIN(shape, 0.0, 1.0);

  double pw = 0.5 + (parameters.morph - 0.66) * 1.46;
  CONSTRAIN(pw, 0.5, 0.995);
  
  // Render monster sync to AUX.
  primary_.Render<true>(primary_f, primary_sync_f, pw, shape, out, size);
  auxiliary_.Render<true>(auxiliary_f, auxiliary_sync_f, pw, shape, aux, size);
  for (size_t i = 0; i < size; ++i) {
    aux[i] = (aux[i] - out[i]) * 0.5;
  }
  
  // Render double varishape to OUT.
  double square_pw = 1.3 * parameters.timbre - 0.15;
  CONSTRAIN(square_pw, 0.005, 0.5);
  
  const double square_sync_ratio = parameters.timbre < 0.5
      ? 0.0
      : (parameters.timbre - 0.5) * (parameters.timbre - 0.5) * 4.0 * 48.0;
  
  const double square_gain = min(parameters.timbre * 8.0, 1.0);
  
  double saw_pw = parameters.morph < 0.5
      ? parameters.morph + 0.5
      : 1.0 - (parameters.morph - 0.5) * 2.0;
  saw_pw *= 1.1;
  CONSTRAIN(saw_pw, 0.005, 1.0);
    
  double saw_shape = 10.0 - 21.0 * parameters.morph;
  CONSTRAIN(saw_shape, 0.0, 1.0);
  
  double saw_gain = 8.0 * (1.0 - parameters.morph);
  CONSTRAIN(saw_gain, 0.02, 1.0);
  
  const double square_sync_f = NoteToFrequency(
      parameters.note + square_sync_ratio);
  
  sync_.Render<true>(
      primary_f, square_sync_f, square_pw, 1.0, temp_buffer_, size);
  variable_saw_.Render(auxiliary_f, saw_pw, saw_shape, out, size);
  
  double norm = 1.0 / (std::max(square_gain, saw_gain));
  
  ParameterInterpolator square_gain_modulation(
      &auxiliary_amount_,
      square_gain * 0.3 * norm,
      size);

  ParameterInterpolator saw_gain_modulation(
      &xmod_amount_,
      saw_gain * 0.5 * norm,
      size);
  
  for (size_t i = 0; i < size; ++i) {
    out[i] = out[i] * saw_gain_modulation.Next() + \
        square_gain_modulation.Next() * temp_buffer_[i];
  }

#endif  // VA_VARIANT values

}

}  // namespace plaits
