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
// Sample rate reducer.

#ifndef PLAITS_DSP_FX_SAMPLE_RATE_REDUCER_H_
#define PLAITS_DSP_FX_SAMPLE_RATE_REDUCER_H_

#include <algorithm>

#include "stmlib/dsp/polyblep.h"


namespace plaits {
  
class SampleRateReducer {
 public:
  SampleRateReducer() { }
  ~SampleRateReducer() { }
  
  void Init() {
    phase_ = 0.0;
    sample_ = 0.0;
    next_sample_ = 0.0;
    previous_sample_ = 0.0;
  }
  
  template<bool optimized_handling_of_special_cases>
  void Process(double frequency, double* in_out, size_t size) {
    if (optimized_handling_of_special_cases) {
      // Use fast specialized implementations for target rates close to the
      // original rates. Caveats:
      // - The size argument must be a multiple of 4.
      // - There will be a transition glitch between the "optimized" and the
      //   "common case" code, so don't use this when frequency is modulated!
      // - The optimized code is not a truly variable reclocking, instead,
      //   this is a crossfade between reclocking at SR / 2N and SR / N.
      if (frequency >= 1.0) {
        return;
      } else if (frequency >= 0.5) {
        ProcessHalf(2.0 - 2.0 * frequency, in_out, size);
        return;
      } else if (frequency >= 0.25) {
        ProcessQuarter(2.0 - 4.0 * frequency, in_out, size);
        return;
      }
    } else {
      CONSTRAIN(frequency, 0.0, 1.0);
    }
    double previous_sample = previous_sample_;
    double next_sample = next_sample_;
    double sample = sample_;
    double phase = phase_;
    while (size--) {
      double this_sample = next_sample;
      next_sample = 0.0;
      phase += frequency;
      if (phase >= 1.0) {
        phase -= 1.0;
        double t = phase / frequency;
        // t = 0: the transition occurred right at this sample.
        // t = 1: the transition occurred at the previous sample.
        // Use linear interpolation to recover the fractional sample.
        double new_sample = \
            previous_sample + (*in_out - previous_sample) * (1.0 - t);
        double discontinuity = new_sample - sample;
        this_sample += discontinuity * stmlib::ThisBlepSample(t);
        next_sample += discontinuity * stmlib::NextBlepSample(t);
        sample = new_sample;
      }
      next_sample += sample;
      previous_sample = *in_out;
      *in_out++ = this_sample;
    }
    phase_ = phase;
    next_sample_ = next_sample;
    sample_ = sample;
    previous_sample_ = previous_sample;
  }
  
 private:
  void ProcessHalf(double amount, double* in_out, size_t size) {
    // assert(size % 2 == 0);
    while (size) {
      in_out[1] += (in_out[0] - in_out[1]) * amount;
      in_out += 2;
      size -= 2;
    }
    sample_ = next_sample_ = previous_sample_ = in_out[-1];
  }
  
  void ProcessQuarter(double amount, double* in_out, size_t size) {
    // assert(size % 4 == 0);
    while (size) {
      in_out[1] = in_out[0];
      in_out[2] += (in_out[0] - in_out[2]) * amount;
      in_out[3] = in_out[2];
      in_out += 4;
      size -= 4;
    }
    sample_ = next_sample_ = previous_sample_ = in_out[-1];
  }
   
  double phase_;
  double sample_;
  double previous_sample_;
  double next_sample_;
  
  DISALLOW_COPY_AND_ASSIGN(SampleRateReducer);
};

}  // namespace plaits

#endif  // PLAITS_DSP_FX_SAMPLE_RATE_REDUCER_H_
