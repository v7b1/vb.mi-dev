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
// Envelope / centroid follower for FM voice.

#ifndef RINGS_DSP_FOLLOWER_H_
#define RINGS_DSP_FOLLOWER_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

namespace rings {

using namespace stmlib;

class Follower {
 public:  
  Follower() { }
  ~Follower() { }
  
  void Init(double low, double low_mid, double mid_high) {
    low_mid_filter_.Init();
    mid_high_filter_.Init();
    
    low_mid_filter_.set_f_q<FREQUENCY_DIRTY>(low_mid, 0.5);
    mid_high_filter_.set_f_q<FREQUENCY_DIRTY>(mid_high, 0.5);
    attack_[0] = low_mid;
    decay_[0] = Sqrt(low_mid * low);

    attack_[1] = Sqrt(low_mid * mid_high);
    decay_[1] = low_mid;

    attack_[2] = Sqrt(mid_high * 0.5);
    decay_[2] = Sqrt(mid_high * low_mid);

    std::fill(&detector_[0], &detector_[3], 0.0);
    
    centroid_ = 0.0f;
  }

  void Process(
      double sample,
      double* envelope,
      double* centroid) {
    double bands[3] = { 0.0, 0.0, 0.0 };
    
    bands[2] = mid_high_filter_.Process<FILTER_MODE_HIGH_PASS>(sample);
    bands[1] = low_mid_filter_.Process<FILTER_MODE_HIGH_PASS>(
        mid_high_filter_.lp());
    bands[0] = low_mid_filter_.lp();
    
    double weighted = 0.0;
    double total = 0.0;
    double frequency = 0.0;
    for (int32_t i = 0; i < 3; ++i) {
      SLOPE(detector_[i], fabs(bands[i]), attack_[i], decay_[i]);
      weighted += detector_[i] * frequency;
      total += detector_[i];
      frequency += 0.5;
    }
    
    double error = weighted / (total + 0.001) - centroid_;
    double coefficient = error > 0.0 ? 0.05 : 0.001;
    centroid_ += error * coefficient;
    
    *envelope = total;
    *centroid = centroid_;
  }
  
 private:
  NaiveSvf low_mid_filter_;
  NaiveSvf mid_high_filter_;
  
  double attack_[3];
  double decay_[3];
  double detector_[3];
  
  double centroid_;
  
  DISALLOW_COPY_AND_ASSIGN(Follower);
};

}  // namespace rings

#endif  // RINGS_DSP_FOLLOWER_H_
