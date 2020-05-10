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
// Envelope for the internal LPG.

#ifndef PLAITS_DSP_ENVELOPE_H_
#define PLAITS_DSP_ENVELOPE_H_

#include "stmlib/stmlib.h"


namespace plaits {

class LPGEnvelope {
 public:
  LPGEnvelope() { }
  ~LPGEnvelope() { }
  
  inline void Init() {
    vactrol_state_ = 0.0;
    gain_ = 1.0;
    frequency_ = 0.5;
    hf_bleed_ = 0.0;
  }
  
  inline void Trigger() {
    ramp_up_ = true;
  }
  
  inline void ProcessPing(
      double attack,
      double short_decay,
      double decay_tail,
      double hf) {
    if (ramp_up_) {
      vactrol_state_ += attack;
      if (vactrol_state_ >= 1.0) {
        vactrol_state_ = 1.0;
        ramp_up_ = false;
      }
    }
    ProcessLP(ramp_up_ ? vactrol_state_ : 0.0, short_decay, decay_tail, hf);
  }
  
  inline void ProcessLP(
      double level,
      double short_decay,
      double decay_tail,
      double hf) {
    double vactrol_input = level;
    double vactrol_error = (vactrol_input - vactrol_state_);
    double vactrol_state_2 = vactrol_state_ * vactrol_state_;
    double vactrol_state_4 = vactrol_state_2 * vactrol_state_2;
    double tail = 1.0 - vactrol_state_;
    double tail_2 = tail * tail;
    double vactrol_coefficient = (vactrol_error > 0.0)
        ? 0.6
        : short_decay + (1.0 - vactrol_state_4) * decay_tail;
    vactrol_state_ += vactrol_coefficient * vactrol_error;
    
    gain_ = vactrol_state_;
    frequency_ = 0.003 + 0.3 * vactrol_state_4 + hf * 0.04;
    hf_bleed_ = (tail_2 + (1.0 - tail_2) * hf) * hf * hf;
  }
  
  inline double gain() const { return gain_; }
  inline double frequency() const { return frequency_; }
  inline double hf_bleed() const { return hf_bleed_; }
  
 private:
  double vactrol_state_;
  double gain_;
  double frequency_;
  double hf_bleed_;
  bool ramp_up_;
  
  DISALLOW_COPY_AND_ASSIGN(LPGEnvelope);
};

class DecayEnvelope {
 public:
  DecayEnvelope() { }
  ~DecayEnvelope() { }
  
  inline void Init() {
    value_ = 0.0;
  }
  
  inline void Trigger() {
    value_ = 1.0;
  }
   
    // TODO: could be useful, vb
    inline void Trigger(double val) {
        value_ = val;
    }
  
  inline void Process(double decay) {
    value_ *= (1.0 - decay);
  }
  
  inline double value() const { return value_; }
  
 private:
  double value_;
  
  DISALLOW_COPY_AND_ASSIGN(DecayEnvelope);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENVELOPE_H_
