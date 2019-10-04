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
// Resonator, taken from Rings' code but with fixed position.

#include "plaits/dsp/physical_modelling/resonator.h"

#include <algorithm>

#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/resources.h"




namespace plaits {

using namespace std;
using namespace stmlib;

void Resonator::Init(double position, int resolution) {
  resolution_ = min(resolution, kMaxNumModes);
  
  CosineOscillator amplitudes;
  amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position);
  
  for (int i = 0; i < resolution; ++i) {
    mode_amplitude_[i] = amplitudes.Next() * 0.25;
  }
  
  for (int i = 0; i < kMaxNumModes / kModeBatchSize; ++i) {
    mode_filters_[i].Init();
  }
}

inline double NthHarmonicCompensation(int n, double stiffness) {
  double stretch_factor = 1.0;
  for (int i = 0; i < n - 1; ++i) {
    stretch_factor += stiffness;
    if (stiffness < 0.0) {
      stiffness *= 0.93;
    } else {
      stiffness *= 0.98;
    }
  }
  return 1.0 / stretch_factor;
}

void Resonator::Process(
    double f0,
    double structure,
    double brightness,
    double damping,
    const double* in,
    double* out,
    size_t size) {
  double stiffness = Interpolate(lut_stiffness, structure, 64.0);
  f0 *= NthHarmonicCompensation(3, stiffness);
  
  double harmonic = f0;
  double stretch_factor = 1.0;
  double q_sqrt = SemitonesToRatio(damping * 79.7);
  double q = 500.0 * q_sqrt * q_sqrt;
  brightness *= 1.0 - structure * 0.3;
  brightness *= 1.0 - damping * 0.3;
  double q_loss = brightness * (2.0 - brightness) * 0.85 + 0.15;
  
  double mode_q[kModeBatchSize];
  double mode_f[kModeBatchSize];
  double mode_a[kModeBatchSize];
  int batch_counter = 0;
  
  ResonatorSvf<kModeBatchSize>* batch_processor = &mode_filters_[0];
  
  
  for (int i = 0; i < resolution_; ++i) {
    double mode_frequency = harmonic * stretch_factor;
    if (mode_frequency >= 0.499) {
      mode_frequency = 0.499;
    }
    const double mode_attenuation = 1.0 - mode_frequency * 2.0;
    
    mode_f[batch_counter] = mode_frequency;
    mode_q[batch_counter] = 1.0 + mode_frequency * q;
    mode_a[batch_counter] = mode_amplitude_[i] * mode_attenuation;
    ++batch_counter;
    
    if (batch_counter == kModeBatchSize) {
      batch_counter = 0;
      batch_processor->Process<FILTER_MODE_BAND_PASS, true>(
          mode_f,
          mode_q,
          mode_a,
          in,
          out,
          size);
      ++batch_processor;
    }
    
    stretch_factor += stiffness;
    if (stiffness < 0.0) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98;
    }
    harmonic += f0;
    q *= q_loss;
  }
}

}  // namespace plaits
