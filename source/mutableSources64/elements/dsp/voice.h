// Copyright 2014 Emilie Gillet.
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
// Modal synthesis voice.

#ifndef ELEMENTS_DSP_VOICE_H_
#define ELEMENTS_DSP_VOICE_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/filter.h"

#include "elements/dsp/dsp.h"
#include "elements/dsp/exciter.h"
#include "elements/dsp/multistage_envelope.h"
#include "elements/dsp/patch.h"
#include "elements/dsp/resonator.h"
#include "elements/dsp/string.h"
#include "elements/dsp/tube.h"

#include "elements/dsp/fx/diffuser.h"

namespace elements {

const size_t kNumStrings = 5;

enum ResonatorModel {
  RESONATOR_MODEL_MODAL,
  RESONATOR_MODEL_STRING,
  RESONATOR_MODEL_STRINGS,
};

class Voice {
 public:
  Voice() { }
  ~Voice() { }
  
  void Init();
  void Process(
      const Patch& patch,
      double frequency,
      double strength,
      const bool gate_in,
      const double* blow_in,
      const double* strike_in,
      double* raw,
      double* center,
      double* sides,
      size_t size);
  // For metering.
  inline double exciter_level() const { return exciter_level_; }
  void Panic() {
    ResetResonator();
  }
  void set_resonator_model(ResonatorModel resonator_model) {
    resonator_model_ = resonator_model;
  }
    
    double* getF() { return resonator_.get_f(); }
  
 private:
  void ResetResonator();
  inline uint8_t GetGateFlags(bool gate_in) {
    uint8_t flags = 0;
    if (gate_in) {
      if (!previous_gate_) {
        flags |= EXCITER_FLAG_RISING_EDGE;
      }
      flags |= EXCITER_FLAG_GATE;
    } else if (previous_gate_) {
      flags = EXCITER_FLAG_FALLING_EDGE;
    }
    previous_gate_ = gate_in;
    return flags;
  }
  
  MultistageEnvelope envelope_;
  Tube tube_; 
  Exciter bow_;
  Exciter blow_;
  Exciter strike_;
  Diffuser diffuser_;
  Resonator resonator_;
  String string_[kNumStrings];
  stmlib::DCBlocker dc_blocker_;
  
  double strength_;
  double envelope_value_;
  
  double exciter_level_;
  
  double bow_buffer_[kMaxBlockSize];
  double bow_strength_buffer_[kMaxBlockSize];
  double blow_buffer_[kMaxBlockSize];
  double strike_buffer_[kMaxBlockSize];
  double external_buffer_[kMaxBlockSize];
  
    float diffuser_buffer_[1024];     // TODO: check out float/double duffuser buffer
    //double diffuser_buffer_[1024];
  
  bool previous_gate_;
  
  ResonatorModel resonator_model_;
  double chord_index_;
  
  DISALLOW_COPY_AND_ASSIGN(Voice);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_VOICE_H_
