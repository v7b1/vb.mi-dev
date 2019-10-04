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
// Comb filter / KS string.

#ifndef RINGS_DSP_STRING_H_
#define RINGS_DSP_STRING_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/delay_line.h"
#include "stmlib/dsp/filter.h"

#include "rings/dsp/dsp.h"

namespace rings {

const size_t kDelayLineSize = 2048;

class DampingFilter {
 public:
  DampingFilter() { }
  ~DampingFilter() { }
  
  void Init() {
    x_ = 0.0;
    x__ = 0.0;
    brightness_ = 0.0;
    brightness_increment_ = 0.0;
    damping_ = 0.0;
    damping_increment_ = 0.0;
  }
   
  inline void Configure(double damping, double brightness, size_t size) {
    if (!size) {
      damping_ = damping;
      brightness_ = brightness;
      damping_increment_ = 0.0;
      brightness_increment_ = 0.0;
    } else {
      double step = 1.0 / static_cast<double>(size);
      damping_increment_ = (damping - damping_) * step;
      brightness_increment_ = (brightness - brightness_) * step;
    }
  }
   
  inline double Process(double x) {
    double h0 = (1.0 + brightness_) * 0.5f;
    double h1 = (1.0 - brightness_) * 0.25f;
    double y = damping_ * (h0 * x_ + h1 * (x + x__));
    x__ = x_;
    x_ = x;
    brightness_ += brightness_increment_;
    damping_ += damping_increment_;
    return y;
  }
 private:
  double x_;
  double x__;
  double brightness_;
  double brightness_increment_;
  double damping_;
  double damping_increment_;
  
  DISALLOW_COPY_AND_ASSIGN(DampingFilter);
};

typedef stmlib::DelayLine<double, kDelayLineSize> StringDelayLine;
typedef stmlib::DelayLine<double, kDelayLineSize / 2> StiffnessDelayLine;

class String {
 public:
  String() { }
  ~String() { }
  
  void Init(bool enable_dispersion);
  void Process(const double* in, double* out, double* aux, size_t size);
  
  inline void set_frequency(double frequency) {
    frequency_ = frequency;
  }

  inline void set_frequency(double frequency, double coefficient) {
    frequency_ += coefficient * (frequency - frequency_);
  }

  inline void set_dispersion(double dispersion) {
    dispersion_ = dispersion;
  }
  
  inline void set_brightness(double brightness) {
    brightness_ = brightness;
  }
  
  inline void set_damping(double damping) {
    damping_ = damping;
  }
  
  inline void set_position(double position) {
    position_ = position;
  }
  
  inline StringDelayLine* mutable_string() { return &string_; }
  
 private:
  template<bool enable_dispersion>
  void ProcessInternal(const double* in, double* out, double* aux, size_t size);
   
  double frequency_;
  double dispersion_;
  double brightness_;
  double damping_;
  double position_;
  
  double delay_;
  double clamped_position_;
  double previous_dispersion_;
  double previous_damping_compensation_;
  
  bool enable_dispersion_;
  bool enable_iir_damping_;
  double dispersion_noise_;
  
  // Very crappy linear interpolation upsampler used for low pitches that
  // do not fit the delay line. Rarely used.
  double src_phase_;
  double out_sample_[2];
  double aux_sample_[2];
  
  double curved_bridge_;
  
  StringDelayLine string_;
  StiffnessDelayLine stretch_;
  
  DampingFilter fir_damping_filter_;
  stmlib::Svf iir_damping_filter_;
  stmlib::DCBlocker dc_blocker_;
  
  DISALLOW_COPY_AND_ASSIGN(String);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_H_
