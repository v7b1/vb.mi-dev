// Copyright 2015 Olivier Gillet.
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
// Noise burst generator for Karplus-Strong synthesis.

#ifndef RINGS_DSP_PLUCKER_H_
#define RINGS_DSP_PLUCKER_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/delay_line.h"
#include "stmlib/utils/random.h"

namespace rings {

class Plucker {
 public:
  Plucker() { }
  ~Plucker() { }
  
  void Init() {
    svf_.Init();
    comb_filter_.Init();
    remaining_samples_ = 0;
    comb_filter_period_ = 0.0;
  }
  
  void Trigger(double frequency, double cutoff, double position) {
    double ratio = position * 0.9 + 0.05;
    double comb_period = 1.0 / frequency * ratio;
    remaining_samples_ = static_cast<size_t>(comb_period);
    while (comb_period >= 255.0) {
      comb_period *= 0.5;
    }
    comb_filter_period_ = comb_period;
    comb_filter_gain_ = (1.0 - position) * 0.8;
    svf_.set_f_q<FREQUENCY_DIRTY>(std::min(cutoff, 0.499), 1.0);
  }
  
  void Process(double* out, size_t size) {
    const double comb_gain = comb_filter_gain_;
    const double comb_delay = comb_filter_period_;
    for (size_t i = 0; i < size; ++i) {
      double in = 0.0;
      if (remaining_samples_) {
        in = 2.0 * Random::GetDouble() - 1.0;
        --remaining_samples_;
      }
      out[i] = in + comb_gain * comb_filter_.Read(comb_delay);
      comb_filter_.Write(out[i]);
    }
    svf_.Process<FILTER_MODE_LOW_PASS>(out, out, size);
  }

 private:
  stmlib::Svf svf_;
  stmlib::DelayLine<double, 256> comb_filter_;
  size_t remaining_samples_;
  double comb_filter_period_;
  double comb_filter_gain_;
  
  DISALLOW_COPY_AND_ASSIGN(Plucker);
};

}  // namespace rings

#endif  // RINGS_DSP_PLUCKER_H_
