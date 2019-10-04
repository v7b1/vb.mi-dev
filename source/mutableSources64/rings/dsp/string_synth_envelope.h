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
// AD envelope for the string synth.

#ifndef ELEMENTS_DSP_STRING_SYNTH_ENVELOPE_H_
#define ELEMENTS_DSP_STRING_SYNTH_ENVELOPE_H_

#include "stmlib/stmlib.h"

namespace rings {

enum EnvelopeShape {
  ENVELOPE_SHAPE_LINEAR,
  ENVELOPE_SHAPE_QUARTIC
};

enum EnvelopeFlags {
  ENVELOPE_FLAG_RISING_EDGE = 1,
  ENVELOPE_FLAG_FALLING_EDGE = 2,
  ENVELOPE_FLAG_GATE = 4
};

class StringSynthEnvelope {
 public:
  StringSynthEnvelope() { }
  ~StringSynthEnvelope() { }
  
  void Init() {
    set_ad(0.1, 0.001);
    segment_ = num_segments_;
    phase_ = 0.0;
    start_value_ = 0.0;
    value_ = 0.0;
  }

  inline double Process(uint8_t flags) {
    if (flags & ENVELOPE_FLAG_RISING_EDGE) {
      start_value_ = segment_ == num_segments_ ? level_[0] : value_;
      segment_ = 0;
      phase_ = 0.0;
    } else if (flags & ENVELOPE_FLAG_FALLING_EDGE && sustain_point_) {
      start_value_ = value_;
      segment_ = sustain_point_;
      phase_ = 0.0;
    } else if (phase_ >= 1.0) {
      start_value_ = level_[segment_ + 1];
      ++segment_;
      phase_ = 0.0;
    }
  
    bool done = segment_ == num_segments_;
    bool sustained = sustain_point_ && segment_ == sustain_point_ &&
        flags & ENVELOPE_FLAG_GATE;
  
    double phase_increment = 0.0;
    if (!sustained && !done) {
      phase_increment = rate_[segment_];
    }
    double t = phase_;
    if (shape_[segment_] == ENVELOPE_SHAPE_QUARTIC) {
      t = 1.0 - t;
      t *= t;
      t *= t;
      t = 1.0 - t;
    }
    
    phase_ += phase_increment;
    value_ = start_value_ + (level_[segment_ + 1] - start_value_) * t;
    return value_;
  }

  inline void set_ad(double attack, double decay) {
    num_segments_ = 2;
    sustain_point_ = 0;

    level_[0] = 0.0;
    level_[1] = 1.0;
    level_[2] = 0.0;

    rate_[0] = attack;
    rate_[1] = decay;
    
    shape_[0] = ENVELOPE_SHAPE_LINEAR;
    shape_[1] = ENVELOPE_SHAPE_QUARTIC;
  }

  inline void set_ar(double attack, double decay) {
    num_segments_ = 2;
    sustain_point_ = 1;

    level_[0] = 0.0;
    level_[1] = 1.0;
    level_[2] = 0.0;

    rate_[0] = attack;
    rate_[1] = decay;
    
    shape_[0] = ENVELOPE_SHAPE_LINEAR;
    shape_[1] = ENVELOPE_SHAPE_LINEAR;
  }
  
 private:
  double level_[4];
  double rate_[4];
  EnvelopeShape shape_[4];
  
  int16_t segment_;
  double start_value_;
  double value_;
  double phase_;
  
  uint16_t num_segments_;
  uint16_t sustain_point_;

  DISALLOW_COPY_AND_ASSIGN(StringSynthEnvelope);
};

}  // namespace rings

#endif  // ELEMENTS_DSP_STRING_SYNTH_ENVELOPE_H_
