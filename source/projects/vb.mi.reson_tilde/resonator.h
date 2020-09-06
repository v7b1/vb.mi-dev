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
// Resonator.

#ifndef ELEMENTS_DSP_RESONATOR_H_
#define ELEMENTS_DSP_RESONATOR_H_

#include "stmlib/stmlib.h"

#include <cmath>
#include <algorithm>

#include "elements/dsp/dsp.h"
#include "filter.h"
#include "stmlib/dsp/delay_line.h"

//namespace elements {

const size_t kMaxModes = 64;
const size_t kMaxBowedModes = 8;
const size_t kMaxDelayLineSize = 1024;

class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }
  
  void Init(double sr);
    
  void Process(
      const double* in,
      double* center,
      double* sides,
      size_t size);
  
  inline void set_frequency(double frequency) {
    frequency_ = frequency * r_sr_;
  }
  
  inline void set_geometry(double geometry) {
    geometry_ = geometry;
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
  
  inline void set_resolution(size_t resolution) {
    resolution_ = std::min(resolution, kMaxModes);
  }
  
    inline void xf_resonators(double xf) {
//        xf *= M_PI * 0.5;
//        res_gain_ = cos(xf);
//        wg_gain_ = sin(xf);
        
//        res_gain_ = 1.0 - xf;
//        wg_gain_ = xf;
        
        double c = 0.5 * cos(xf * M_PI);
        res_gain_ = 0.5 + c;
        wg_gain_ = 0.5 - c;
    }
  

    void setFilters(double *freqs, double* qs, double* gains);
    size_t ComputeFilters(double *freqs, double *qs, double *gains);
    
    bool calc_res;
    bool calc_wg;
  
 private:
  
    double num_modes_;
    double frequency_;
    double geometry_;
    double brightness_;
    double position_;
    double previous_position_;
    double damping_;
  
    double modulation_frequency_;
    double modulation_offset_;
    double lfo_phase_;

    
    double sample_rate_;
    double r_sr_;
  
    size_t resolution_;
  
    Svf f_[kMaxModes];
    Svf f_bow_[kMaxBowedModes];
    uint8_t active_[kMaxModes];
    
    //stmlib::DelayLine<double, kMaxDelayLineSize> d_bow_[kMaxBowedModes];
    stmlib::DelayLine<double, 1024> d_bow_[kMaxBowedModes];
  
    double res_gain_;
    double wg_gain_;
  
  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

//}  // namespace elements

#endif  // ELEMENTS_DSP_RESONATOR_H_
