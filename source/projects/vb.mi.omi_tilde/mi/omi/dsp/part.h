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
// Group of voices.

#ifndef ELEMENTS_DSP_PART_H_
#define ELEMENTS_DSP_PART_H_

#include "stmlib/stmlib.h"


#include "omi/dsp/ominous_voice.h"
#include "omi/dsp/patch.h"

namespace elements {

struct PerformanceState {
  bool gate;
  double note;
  double modulation;
  double strength;
};



class Part {
 public:
  Part() { }
  ~Part() { }
  
  void Init(double sr);
  
  void Process(
      const PerformanceState& performance_state,
      const double* blow_in,
      double* left,
      double* right,
      double* raw,
      size_t n);

  inline Patch* mutable_patch() { return &patch_; }
  
  void Seed(uint32_t* seed, size_t size);

 private:
    Patch patch_;
    OminousVoice ominous_voice_;


    double center_buffer_[kMaxBlockSize];
    double sides_buffer_[kMaxBlockSize];
    
    double sr_;
  
    DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_PART_H_
