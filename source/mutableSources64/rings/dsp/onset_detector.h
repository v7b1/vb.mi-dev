// Copyright 2015 Emilie Gillet.
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
// Onset detector.

#ifndef RINGS_DSP_ONSET_DETECTOR_H_
#define RINGS_DSP_ONSET_DETECTOR_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"


namespace rings {

using namespace std;
using namespace stmlib;

class ZScorer {
 public:
  ZScorer() { }
  ~ZScorer() { }
  
  void Init(double cutoff) {
    coefficient_ = cutoff;
    mean_ = 0.0;
    variance_ = 0.00;
  }
  
  inline double Normalize(double sample) {
    return Update(sample) / Sqrt(variance_);
  }
  
  inline bool Test(double sample, double threshold) {
    double value = Update(sample);
    return value > Sqrt(variance_) * threshold;
  }

  inline bool Test(double sample, double threshold, double absolute_threshold) {
    double value = Update(sample);
    return value > Sqrt(variance_) * threshold && value > absolute_threshold;
  }

 private:
  inline double Update(float sample) {
    double centered = sample - mean_;
    mean_ += coefficient_ * centered;
    variance_ += coefficient_ * (centered * centered - variance_);
    return centered;
  }
  
  double coefficient_;
  double mean_;
  double variance_;
  
  DISALLOW_COPY_AND_ASSIGN(ZScorer);
};

class Compressor {
 public:  
  Compressor() { }
  ~Compressor() { }
  
  void Init(double attack, double decay, double max_gain) {
    attack_ = attack;
    decay_ = decay;
    level_ = 0.0;
    skew_ = 1.0 / max_gain;
  }
  
  void Process(const double* in, double* out, size_t size) {
    double level = level_;
    while (size--) {
        SLOPE(level, abs(*in), attack_, decay_);
        *out++ = *in++ / (skew_ + level);
    }
    level_ = level;
  }
 
 private:
  double attack_;
  double decay_;
  double level_;
  double skew_;
  
  DISALLOW_COPY_AND_ASSIGN(Compressor);
};

class OnsetDetector {
 public:  
  OnsetDetector() { }
  ~OnsetDetector() { }
  
  void Init(
      double low,
      double low_mid,
      double mid_high,
      double decimated_sr,
      double ioi_time) {
    double ioi_f = 1.0 / (ioi_time * decimated_sr);
    compressor_.Init(ioi_f * 10.0, ioi_f * 0.05, 40.0);
    
    low_mid_filter_.Init();
    mid_high_filter_.Init();
    low_mid_filter_.set_f_q<FREQUENCY_DIRTY>(low_mid, 0.5);
    mid_high_filter_.set_f_q<FREQUENCY_DIRTY>(mid_high, 0.5);

    attack_[0] = low_mid;
    decay_[0] = low * 0.25;

    attack_[1] = low_mid;
    decay_[1] = low * 0.25;

    attack_[2] = low_mid;
    decay_[2] = low * 0.25;

    fill(&envelope_[0], &envelope_[3], 0.0);
    fill(&energy_[0], &energy_[3], 0.0);
    
    z_df_.Init(ioi_f * 0.05);
    
    inhibit_time_ = static_cast<int32_t>(ioi_time * decimated_sr);
    inhibit_decay_ = 1.0 / (ioi_time * decimated_sr);
    
    inhibit_threshold_ = 0.0;
    inhibit_counter_ = 0;
    onset_df_ = 0.0;
  }
  
  bool Process(const double* samples, size_t size) {
    // Automatic gain control.
    compressor_.Process(samples, bands_[0], size);
    
    // Quick and dirty filter bank - split the signal in three bands.
    mid_high_filter_.Split(bands_[0], bands_[1], bands_[2], size);
    low_mid_filter_.Split(bands_[1], bands_[0], bands_[1], size);

    // Compute low-pass energy and onset detection function
    // (derivative of energy) in each band.
    double onset_df = 0.0;
    double total_energy = 0.0;
    for (int32_t i = 0; i < 3; ++i) {
      double* s = bands_[i];
      double energy = 0.0;
      double envelope = envelope_[i];
      size_t increment = 4 >> i;
      for (size_t j = 0; j < size; j += increment) {
        SLOPE(envelope, s[j] * s[j], attack_[i], decay_[i]);
        energy += envelope;
      }
      energy = Sqrt(energy) * double(increment);
      envelope_[i] = envelope;

      double derivative = energy - energy_[i];
      onset_df += derivative + fabs(derivative);
      energy_[i] = energy;
      total_energy += energy;
    }
    
    onset_df_ += 0.05 * (onset_df - onset_df_);
    bool outlier_in_df = z_df_.Test(onset_df_, 1.0, 0.01);
    bool exceeds_energy_threshold = total_energy >= inhibit_threshold_;
    bool not_inhibited = !inhibit_counter_;
    bool has_onset = outlier_in_df && exceeds_energy_threshold && not_inhibited;
    
    if (has_onset) {
      inhibit_threshold_ = total_energy * 1.5;
      inhibit_counter_ = inhibit_time_;
    } else {
      inhibit_threshold_ -= inhibit_decay_ * inhibit_threshold_;
      if (inhibit_counter_) {
        --inhibit_counter_;
      }
    }
    return has_onset;
  }
  
 private:
  Compressor compressor_;
  NaiveSvf low_mid_filter_;
  NaiveSvf mid_high_filter_;
  
  double attack_[3];
  double decay_[3];
  double energy_[3];
  double envelope_[3];
  double onset_df_;
  
  //double bands_[3][32];     // TODO: why 32 ??? vb
  double bands_[3][64];
    
  ZScorer z_df_;
  
  double inhibit_threshold_;
  double inhibit_decay_;
  int32_t inhibit_time_;
  int32_t inhibit_counter_;
  
  DISALLOW_COPY_AND_ASSIGN(OnsetDetector);
};

}  // namespace rings

#endif  // RINGS_DSP_ONSET_DETECTOR_H_
