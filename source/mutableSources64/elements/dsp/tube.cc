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
// Simple waveguide tube.

#include "elements/dsp/tube.h"

#include <cstdio>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"


namespace elements {

void Tube::Init() {
  zero_state_ = 0.0;
  pole_state_ = 0.0;
  delay_ptr_ = 0;
    
    // vb init additions
    std::fill(&delay_line_[0], &delay_line_[kTubeDelaySize], 0.0);
}

void Tube::Process(
    double frequency,
    double envelope,
    double damping,
    double timbre,
    double* input_output,
    double gain,
    size_t size) {
  double delay = 1.0 / frequency;
  while (delay >= double(kTubeDelaySize)) {
    delay *= 0.5;
  }
  MAKE_INTEGRAL_FRACTIONAL(delay);
  
  if (envelope >= 1.0) envelope = 1.0;
  
  damping = 3.6 - damping * 1.8;
  double lpf_coefficient = frequency * (1.0 + timbre * timbre * 256.0);
  if (lpf_coefficient >= 0.995) lpf_coefficient = 0.995;
  
  int32_t d = delay_ptr_;
  while (size--) {
    double breath = *input_output * damping + 0.8;
    double a = delay_line_[(d + delay_integral) % kTubeDelaySize];
    double b = delay_line_[(d + delay_integral + 1) % kTubeDelaySize];
    double in = a + (b - a) * delay_fractional;
    double pressure_delta = -0.95 * (in * envelope + zero_state_) - breath;
    zero_state_ = in;
    
    double reed = pressure_delta * -0.2 + 0.8;
    double out = pressure_delta * reed + breath;
    
    CONSTRAIN(out, -5.0, 5.0);
    delay_line_[d] = out * 0.5;         // TODO: Crahses here, because d goes out of bound! why?
    
    --d;
    if (d < 0) {
      d = kTubeDelaySize - 1;
    }
    pole_state_ += lpf_coefficient * (out - pole_state_);
    *input_output++ += gain * envelope * pole_state_;
  }
  delay_ptr_ = d;
}

}  // namespace elements
