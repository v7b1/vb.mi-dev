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
// 8x8x3 wave terrain.


#include "plaits/dsp/engine/wavetable_engine.h"

#include <algorithm>

#include "plaits/resources.h"


namespace plaits {

using namespace std;
using namespace stmlib;

void WavetableEngine::Init(BufferAllocator* allocator) {
  phase_ = 0.0;

  x_lp_ = 0.0;
  y_lp_ = 0.0;
  z_lp_ = 0.0;
  
  x_pre_lp_ = 0.0;
  y_pre_lp_ = 0.0;
  z_pre_lp_ = 0.0;

  previous_x_ = 0.0;
  previous_y_ = 0.0;
  previous_z_ = 0.0;
    previous_f0_ = plaits::Dsp::getA0();    //a0;

  diff_out_.Init();
}

void WavetableEngine::Reset() {
  
}

inline double Clamp(double x, double amount) {
  x = x - 0.5;
  x *= amount;
  CONSTRAIN(x, -0.5, 0.5);
  x += 0.5;
  return x;
}

const size_t table_size = 256;
const double table_size_f = double(table_size);

inline double ReadWave(
    int x,
    int y,
    int z,
    int randomize,
    int phase_integral,
    double phase_fractional) {
  int wave = ((x + y * 8 + z * 64) * randomize) % 192;
  return InterpolateWaveHermite(
      wav_integrated_waves + wave * (table_size + 4),
      phase_integral,
      phase_fractional);
}

void WavetableEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  const double f0 = NoteToFrequency(parameters.note);
  
  ONE_POLE(x_pre_lp_, parameters.timbre * 6.9999, 0.2);
  ONE_POLE(y_pre_lp_, parameters.morph * 6.9999, 0.2);
  ONE_POLE(z_pre_lp_, parameters.harmonics * 6.9999, 0.05);
  
  const double x = x_pre_lp_;
  const double y = y_pre_lp_;
  const double z = z_pre_lp_;
  
  const double quantization = min(max(z - 3.0, 0.0), 1.0);
  const double lp_coefficient = min(
      max(2.0 * f0 * (4.0 - 3.0 * quantization), 0.01), 0.1);
  
  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);
  MAKE_INTEGRAL_FRACTIONAL(z);
  
  x_fractional += quantization * (Clamp(x_fractional, 16.0) - x_fractional);
  y_fractional += quantization * (Clamp(y_fractional, 16.0) - y_fractional);
  z_fractional += quantization * (Clamp(z_fractional, 16.0) - z_fractional);
  
  ParameterInterpolator x_modulation(
      &previous_x_, static_cast<double>(x_integral) + x_fractional, size);
  ParameterInterpolator y_modulation(
      &previous_y_, static_cast<double>(y_integral) + y_fractional, size);
  ParameterInterpolator z_modulation(
      &previous_z_, static_cast<double>(z_integral) + z_fractional, size);

  ParameterInterpolator f0_modulation(&previous_f0_, f0, size);
  
  while (size--) {
    const double f0 = f0_modulation.Next();
    
    const double gain = (1.0 / (f0 * 131072.0)) * (0.95 - f0);
    const double cutoff = min(table_size_f * f0, 1.0);
    
    ONE_POLE(x_lp_, x_modulation.Next(), lp_coefficient);
    ONE_POLE(y_lp_, y_modulation.Next(), lp_coefficient);
    ONE_POLE(z_lp_, z_modulation.Next(), lp_coefficient);
    
    const double x = x_lp_;
    const double y = y_lp_;
    const double z = z_lp_;

    MAKE_INTEGRAL_FRACTIONAL(x);
    MAKE_INTEGRAL_FRACTIONAL(y);
    MAKE_INTEGRAL_FRACTIONAL(z);

    phase_ += f0;
    if (phase_ >= 1.0) {
      phase_ -= 1.0;
    }
    
    const double p = phase_ * table_size_f;
    MAKE_INTEGRAL_FRACTIONAL(p);
    
    {
      int x0 = x_integral;
      int x1 = x_integral + 1;
      int y0 = y_integral;
      int y1 = y_integral + 1;
      int z0 = z_integral;
      int z1 = z_integral + 1;
      
      if (z0 >= 4) {
        z0 = 7 - z0;
      }
      if (z1 >= 4) {
        z1 = 7 - z1;
      }
      
      int r0 = z0 == 3 ? 101 : 1;
      int r1 = z1 == 3 ? 101 : 1;

      double x0y0z0 = ReadWave(x0, y0, z0, r0, p_integral, p_fractional);
      double x1y0z0 = ReadWave(x1, y0, z0, r0, p_integral, p_fractional);
      double xy0z0 = x0y0z0 + (x1y0z0 - x0y0z0) * x_fractional;

      double x0y1z0 = ReadWave(x0, y1, z0, r0, p_integral, p_fractional); 
      double x1y1z0 = ReadWave(x1, y1, z0, r0, p_integral, p_fractional);
      double xy1z0 = x0y1z0 + (x1y1z0 - x0y1z0) * x_fractional;

      double xyz0 = xy0z0 + (xy1z0 - xy0z0) * y_fractional;

      double x0y0z1 = ReadWave(x0, y0, z1, r1, p_integral, p_fractional);
      double x1y0z1 = ReadWave(x1, y0, z1, r1, p_integral, p_fractional);
      double xy0z1 = x0y0z1 + (x1y0z1 - x0y0z1) * x_fractional;

      double x0y1z1 = ReadWave(x0, y1, z1, r1, p_integral, p_fractional);
      double x1y1z1 = ReadWave(x1, y1, z1, r1, p_integral, p_fractional);
      double xy1z1 = x0y1z1 + (x1y1z1 - x0y1z1) * x_fractional;
      
      double xyz1 = xy0z1 + (xy1z1 - xy0z1) * y_fractional;

      double mix = xyz0 + (xyz1 - xyz0) * z_fractional;
      mix = diff_out_.Process(cutoff, mix) * gain;
      *out++ = mix;
      *aux++ = static_cast<double>(static_cast<int>(mix * 32.0)) / 32.0;
    }
  }
}

}  // namespace plaits
