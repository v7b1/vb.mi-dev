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
// Vocoder.

#ifndef WARPS_DSP_VOCODER_H_
#define WARPS_DSP_VOCODER_H_

#include "stmlib/stmlib.h"

#include "warps/dsp/filter_bank.h"
#include "warps/dsp/limiter.h"

namespace warps {

const double kFollowerGain = sqrtf(kNumBands);

class EnvelopeFollower {
 public:
  EnvelopeFollower() { }
  ~EnvelopeFollower() { }
  
  void Init() {
    envelope_ = 0.0;
    freeze_ = false;
    attack_ = decay_ = 0.1;
    peak_ = 0.0;
  };
  
  void set_attack(double attack) {
    attack_ = attack;
  }
  
  void set_decay(double decay) {
    decay_ = decay;
  }
  
  void set_freeze(bool freeze) {
    freeze_ = freeze;
  }
  
  void Process(const double* in, double* out, size_t size) {
    double envelope = envelope_;
    double attack = freeze_ ? 0.0 : attack_;
    double decay = freeze_ ? 0.0 : decay_;
    double peak = 0.0;
    while (size--) {
      double error = fabs(*in++ * kFollowerGain) - envelope;
      envelope += (error > 0.0 ? attack : decay) * error;
      if (envelope > peak) {
        peak = envelope;
      }
      *out++ = envelope;
    }
    envelope_ = envelope;
    double error = peak - peak_;
    peak_ += (error > 0.0 ? 0.5 : 0.1) * error;
  }
  
  inline double peak() const { return peak_; }
  
 private:
  double attack_;
  double decay_;
  double envelope_;
  double peak_;
  double freeze_;
  
  DISALLOW_COPY_AND_ASSIGN(EnvelopeFollower);
};

struct BandGain {
  double carrier;
  double vocoder;
};

class Vocoder {
 public:
  Vocoder() { }
  ~Vocoder() { }
  
  void Init(double sample_rate);
  void Process(
      const double* modulator,
      const double* carrier,
      double* out,
      size_t size);
  
  void set_release_time(double release_time) {
    release_time_ = release_time;
  }

  void set_formant_shift(double formant_shift) {
    formant_shift_ = formant_shift;
  }

 private:
  double release_time_;
  double formant_shift_;
  
  BandGain previous_gain_[kNumBands];
  BandGain gain_[kNumBands];

  double tmp_[kMaxFilterBankBlockSize];
   
  FilterBank modulator_filter_bank_;
  FilterBank carrier_filter_bank_;
  Limiter limiter_;
  EnvelopeFollower follower_[kNumBands];
  
  DISALLOW_COPY_AND_ASSIGN(Vocoder);
};

}  // namespace warps

#endif  // WARPS_DSP_VOCODER_H_
