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
// Wave terrain synthesis.

#include "plaits/dsp/engine2/wave_terrain_engine.h"

#include <cmath>
#include <algorithm>

#include "plaits/dsp/oscillator/wavetable_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void WaveTerrainEngine::Init(BufferAllocator* allocator) {
  path_.Init();
  offset_ = 0.0;
  terrain_ = 0.0;
  temp_buffer_ = allocator->Allocate<double>(kMaxBlockSize * 4);
  user_terrain_ = NULL;
}

void WaveTerrainEngine::Reset() {

}

inline double TerrainLookup(double x, double y, const int8_t* terrain) {
  const int terrain_size = 64;
  const double value_scale = 1.0 / 128.0;
  const double coord_scale = double(terrain_size - 2) * 0.5;

  x = (x + 1.0) * coord_scale;
  y = (y + 1.0) * coord_scale;

  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);

  double xy[2];

  terrain += y_integral * terrain_size;
  xy[0] = InterpolateWave(terrain, x_integral, x_fractional);
  terrain += terrain_size;
  xy[1] = InterpolateWave(terrain, x_integral, x_fractional);
  return (xy[0] + (xy[1] - xy[0]) * y_fractional) * value_scale;
}

template<typename T>
inline double InterpolateIntegratedWave(
    const T* table,
    int32_t index_integral,
    double index_fractional) {
  double a = static_cast<double>(table[index_integral]);
  double b = static_cast<double>(table[index_integral + 1]);
  double c = static_cast<double>(table[index_integral + 2]);
  double t = index_fractional;
  return (b - a) + (c - b - b + a) * t;
}

// The wavetables are stored in integrated form. Either we directly use the
// integrated data (which can have large variations in amplitude), or we
// differentiate it on the fly to recover the original waveform. This second
// option can be noisier, and it would ideally need a low pass-filter.

#define DIFFERENTIATE_WAVE_DATA

// Lookup from the wavetable data re-interpreted as a terrain. :facepalm:
inline double TerrainLookupWT(double x, double y, int bank) {
  const int table_size = 128;
  const int table_size_full = table_size + 4;  // Includes 4 wrapped samples
  const int num_waves = 64;
  const double sample = (y + 1.0) * 0.5 * double(table_size);
  const double wt = (x + 1.0) * 0.5 * double(num_waves - 1);

  const int16_t* waves = wav_integrated_waves + \
      bank * num_waves * table_size_full;

  MAKE_INTEGRAL_FRACTIONAL(sample);
  MAKE_INTEGRAL_FRACTIONAL(wt);

  double xy[2];
#ifdef DIFFERENTIATE_WAVE_DATA
  const double value_scale = 1.0 / 1024.0;
  waves += wt_integral * table_size_full;
  xy[0] = InterpolateIntegratedWave(waves, sample_integral, sample_fractional);
  waves += table_size_full;
  xy[1] = InterpolateIntegratedWave(waves, sample_integral, sample_fractional);
#else
  const double value_scale = 1.0 / 32768.0;
  waves += wt_integral * table_size_full;
  xy[0] = InterpolateWave(waves, sample_integral, sample_fractional);
  waves += table_size_full;
  xy[1] = InterpolateWave(waves, sample_integral, sample_fractional);
#endif  // DIFFERENTIATE_WAVE_DATA
  return (xy[0] + (xy[1] - xy[0]) * wt_fractional) * value_scale;
}

inline double Squash(double x, double a) {
  x *= a;
  return x / (1.0 + abs(x));
}

inline double WaveTerrainEngine::Terrain(double x, double y, int terrain_index) {
  // The Sine function only works for a positive argument.
  // Thus, all calls to Sine include a positive offset of the argument!
  const double k = 4.0;
  switch (terrain_index) {
    case 0:
      {
        return (Squash(Sine(k + x * 1.273), 2.0) - \
            Sine(k + y * (x + 1.571) * 0.637)) * 0.57;
      }
      break;
    case 1:
      {
        const double xy = x * y;
        return Sine(k + Sine(k + (x + y) * 0.637) / (0.2 + xy * xy) * 0.159);
      }
      break;
    case 2:
      {
        const double xy = x * y;
        return Sine(k + Sine(k + 2.387 * xy) / (0.350 + xy * xy) * 0.159);
      }
      break;
    case 3:
      {
        const double xy = x * y;
        const double xys = (x - 0.25) * (y + 0.25);
        return Sine(k + xy / (2.0 + abs(5.0 * xys)) * 6.366);
      }
      break;
    case 4:
      {
        return Sine(
          0.159 / (0.170 + abs(y - 0.25)) + \
          0.477 / (0.350 + abs((x + 0.5) * (y + 1.5))) + k);
      }
      break;
    case 5:
    case 6:
    case 7:
      {
        return TerrainLookupWT(x, y, 2 - (terrain_index - 5));
      }
      break;
    case 8:
      {
        return TerrainLookup(x, y, user_terrain_);
      }
  }
  return 0.0f;
}

void WaveTerrainEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  const size_t kOversampling = 2;
  const double kScale = 1.0 / double(kOversampling);

  double* path_x = &temp_buffer_[0];
  double* path_y = &temp_buffer_[kOversampling * size];

  const double f0 = NoteToFrequency(parameters.note);
  const double attenuation = max(1.0 - 8.0 * f0, 0.0);
  const double radius = 0.1 + 0.9 * parameters.timbre * attenuation * \
      (2.0 - attenuation);

  // Use the "magic sine" algorithm to generate sin and cos functions for the
  // trajectory coordinates.
  path_.RenderQuadrature(
      f0 * kScale, radius, path_x, path_y, size * kOversampling);

  ParameterInterpolator offset(&offset_, 1.9 * parameters.morph - 1.0, size);
  int num_terrains = user_terrain_ ? 9 : 8;
  ParameterInterpolator terrain(
      &terrain_,
      min(parameters.harmonics * 1.05, 1.0) * double(num_terrains - 1.0001),
      size);

  size_t ij = 0;
  for (size_t i = 0; i < size; ++i) {
    const double x_offset = offset.Next();

    const double z = terrain.Next();
    MAKE_INTEGRAL_FRACTIONAL(z);

    double out_s = 0.0;
    double aux_s = 0.0;

    for (size_t j = 0; j < kOversampling; ++j) {
      const double x = path_x[ij] * (1.0 - abs(x_offset)) + x_offset;
      const double y = path_y[ij];
      ++ij;

      const double z0 = Terrain(x, y, z_integral);
      const double z1 = Terrain(x, y, z_integral + 1);
      const double z = (z0 + (z1 - z0) * z_fractional);
      out_s += z;
      aux_s += y + z;
    }
    out[i] = kScale * out_s;
    aux[i] = Sine(1.0 + 0.5 * kScale * aux_s);
  }
}

}  // namespace plaits
