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
// Resonator, taken from Rings' code but with fixed position.

#ifndef PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_
#define PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_

#include "stmlib/dsp/filter.h"

namespace plaits {

const int kMaxNumModes = 24;
const int kModeBatchSize = 4;

// We render 4 modes simultaneously since there are enough registers to hold
// all state variables.
template<int batch_size>
class ResonatorSvf {
 public:
  ResonatorSvf() { }
  ~ResonatorSvf() { }
  
  void Init() {
    for (int i = 0; i < batch_size; ++i) {
      state_1_[i] = state_2_[i] = 0.0;
    }
  }
  
  template<stmlib::FilterMode mode, bool add>
  void Process(
      const double* f,
      const double* q,
      const double* gain,
      const double* in,
      double* out,
      size_t size) {
    double g[batch_size];
    double r[batch_size];
    double r_plus_g[batch_size];
    double h[batch_size];
    double state_1[batch_size];
    double state_2[batch_size];
    double gains[batch_size];
    for (int i = 0; i < batch_size; ++i) {
      g[i] = stmlib::OnePole::tangens<stmlib::FREQUENCY_FAST>(f[i]);
      r[i] = 1.0 / q[i];
      h[i] = 1.0 / (1.0 + r[i] * g[i] + g[i] * g[i]);
      r_plus_g[i] = r[i] + g[i];
      state_1[i] = state_1_[i];
      state_2[i] = state_2_[i];
      gains[i] = gain[i];
    }
    
    while (size--) {
      double s_in = *in++;
      double s_out = 0.0;
      for (int i = 0; i < batch_size; ++i) {
        const double hp = (s_in - r_plus_g[i] * state_1[i] - state_2[i]) * h[i];
        const double bp = g[i] * hp + state_1[i];
        state_1[i] = g[i] * hp + bp;
        const double lp = g[i] * bp + state_2[i];
        state_2[i] = g[i] * bp + lp;
        s_out += gains[i] * ((mode == stmlib::FILTER_MODE_LOW_PASS) ? lp : bp);
      }
      if (add) {
        *out++ += s_out;
      } else {
        *out++ = s_out;
      }
    }
    for (int i = 0; i < batch_size; ++i) {
      state_1_[i] = state_1[i];
      state_2_[i] = state_2[i];
    }
  }
  
 private:
  double state_1_[batch_size];
  double state_2_[batch_size];
  
  DISALLOW_COPY_AND_ASSIGN(ResonatorSvf);
};

class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }
  
  void Init(double position, int resolution);
  void Process(
      double f0,
      double structure,
      double brightness,
      double damping,
      const double* in,
      double* out,
      size_t size);
  
 private:
  int resolution_;
  
  double mode_amplitude_[kMaxNumModes];
  ResonatorSvf<kModeBatchSize> mode_filters_[kMaxNumModes / kModeBatchSize];
  
  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_PHYSICAL_MODELLING_RESONATOR_H_
