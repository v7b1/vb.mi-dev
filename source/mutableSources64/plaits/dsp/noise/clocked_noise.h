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
// Noise processed by a sample and hold running at a target frequency.

#ifndef PLAITS_DSP_NOISE_CLOCKED_NOISE_H_
#define PLAITS_DSP_NOISE_CLOCKED_NOISE_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"
#include "stmlib/utils/random.h"


namespace plaits {

class ClockedNoise {
 public:
  ClockedNoise() { }
  ~ClockedNoise() { }
  
  void Init() {
    phase_ = 0.0;
    sample_ = 0.0;
    next_sample_ = 0.0;
    frequency_ = 0.001;
  }

  void Render(bool sync, double frequency, double* out, size_t size) {
    CONSTRAIN(frequency, 0.0, 1.0);
    
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);

    double next_sample = next_sample_;
    double sample = sample_;
    
    if (sync) {
      phase_ = 1.0;
    }

    while (size--) {
      double this_sample = next_sample;
      next_sample = 0.0;

      const double frequency = fm.Next();
      const double raw_sample = stmlib::Random::GetDouble() * 2.0 - 1.0;
      double raw_amount = 4.0 * (frequency - 0.25);
      CONSTRAIN(raw_amount, 0.0, 1.0);
      
      phase_ += frequency;
      
      if (phase_ >= 1.0) {
        phase_ -= 1.0;
        double t = phase_ / frequency;
        double new_sample = raw_sample;
        double discontinuity = new_sample - sample;
        this_sample += discontinuity * stmlib::ThisBlepSample(t);
        next_sample += discontinuity * stmlib::NextBlepSample(t);
        sample = new_sample;
      }
      next_sample += sample;
      *out++ = this_sample + raw_amount * (raw_sample - this_sample);
    }
    next_sample_ = next_sample;
    sample_ = sample;
  }
  
 private:
  // Oscillator state.
  double phase_;
  double sample_;
  double next_sample_;

  // For interpolation of parameters.
  double frequency_;
  
  DISALLOW_COPY_AND_ASSIGN(ClockedNoise);
};

}  // namespace plaits

#endif  // PLAITS_DSP_NOISE_CLOCKED_NOISE_H_
