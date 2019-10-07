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
// Resonator.

#include "resonator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"

#include "elements/dsp/dsp.h"
#include "elements/resources.h"

#include <iostream>

namespace elements {

using namespace std;
using namespace stmlib;

void Resonator::Init() {
  for (size_t i = 0; i < kMaxModes; ++i) {
    f_[i].Init();
  }

  for (size_t i = 0; i < kMaxBowedModes; ++i) {
    f_bow_[i].Init();
    d_bow_[i].Init();
  }
  
    set_frequency(220.0 / 44100.0);
    //set_frequency(220.0 / Dsp::getSr());
    // set_frequency(220.0 / kSampleRate);
  set_geometry(0.25);
  set_brightness(0.5);
  set_damping(0.3);
  set_position(0.999);
  set_resolution(kMaxModes);
  
    ComputeFilters();
  bow_signal_ = 0.0;
    modulation_frequency_ = 0.5 * 64.0 / 44100.0;
    
    calc_wg = calc_res = true;
}

size_t Resonator::ComputeFilters() {
  //++clock_divider_;
  double stiffness = Interpolate(lut_stiffness, geometry_, 256.0);
  double harmonic = frequency_;
  double stretch_factor = 1.0; 
  double q = 500.0 * Interpolate(
      lut_4_decades,
      damping_ * 0.8,
      256.0);
  double brightness_attenuation = 1.0 - geometry_;
  // Reduces the range of brightness when geometry is very low, to prevent
  // clipping.
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  double brightness = brightness_ * (1.0 - 0.2 * brightness_attenuation);
  double q_loss = brightness * (2.0 - brightness) * 0.85 + 0.15;
  double q_loss_damping_rate = geometry_ * (2.0 - geometry_) * 0.1;
  size_t num_modes = 0;
  for (size_t i = 0; i < min(kMaxModes, resolution_); ++i) {
    // Update the first 24 modes every time (2kHz). The higher modes are
    // refreshed as a slowest rate.
    //bool update = i <= 24 || ((i & 1) == (clock_divider_ & 1));
    double partial_frequency = harmonic * stretch_factor;
    if (partial_frequency >= 0.49) {
      partial_frequency = 0.49;
    } else {
      num_modes = i + 1;
    }
      
    //if (update) {
      f_[i].set_f_q<FREQUENCY_FAST>(
          partial_frequency,
          1.0 + partial_frequency * q);
      if (i < kMaxBowedModes) {
        size_t period = 1.0 / partial_frequency;
        while (period >= kMaxDelayLineSize) period >>= 1;
        d_bow_[i].set_delay(period);
        f_bow_[i].set_g_q(f_[i].g(), 1.0 + partial_frequency * 1500.0);
      }
    //}
    stretch_factor += stiffness;
    if (stiffness < 0.0) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98;
    }
    // This prevents the highest partials from decaying too fast.
    q_loss += q_loss_damping_rate * (1.0 - q_loss);
    harmonic += frequency_;
    q *= q_loss;
  }
    num_modes_ = num_modes;
  return num_modes;
}

    
    void Resonator::setFilters(double *freqs, double* qs, double* gains) {
        double r_sr = 1/44100.0;
        size_t num_modes = 0;
        for (size_t i = 0; i < min(kMaxModes, resolution_); ++i) {
            double partial_frequency = freqs[i] * r_sr;
            if (partial_frequency >= 0.49) {
                partial_frequency = 0.49;
            } else {
                num_modes = i + 1;
            }
            
            f_[i].set_f_q<FREQUENCY_FAST>(
                                          partial_frequency,
                                          qs[i]);
            if (i < kMaxBowedModes) {
                size_t period = 1.0 / partial_frequency;
                while (period >= kMaxDelayLineSize) period >>= 1;
                d_bow_[i].set_delay(period);
                f_bow_[i].set_g_q(f_[i].g(), 1.0 + partial_frequency * 1500.0);
            }
            
        }
        num_modes_ = num_modes;
    }
    
    
void Resonator::Process(
    const double* bow_strength,
    const double* in,
    double* center,
    double* sides,
    size_t size)
    {
    size_t num_modes = num_modes_;  //ComputeFilters();
    size_t num_banded_wg = min(kMaxBowedModes, num_modes);
  // Linearly interpolate position. This parameter is extremely sensitive to
  // zipper noise.
    double position_increment = (position_ - previous_position_) / size;
    
    // 0.5 Hz LFO used to modulate the position of the stereo side channel.
    lfo_phase_ += modulation_frequency_;
    if (lfo_phase_ >= 1.0) {
        lfo_phase_ -= 1.0;
    }
    previous_position_ += position_increment;
    double lfo = lfo_phase_ > 0.5 ? 1.0 - lfo_phase_ : lfo_phase_;
    CosineOscillator amplitudes;
    CosineOscillator aux_amplitudes;
    amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(previous_position_);
    aux_amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(modulation_offset_ + lfo);
    amplitudes.Start();
    aux_amplitudes.Start();
    
    if(calc_res) {
        for (size_t i = 0; i < num_modes; i++) {
            f_[i].Process_bp_block(in, center, sides, size, amplitudes.Next(), aux_amplitudes.Next());
        }

        for (size_t i=0; i<size; ++i)
            sides[i] -= center[i];
    }
        
    if(calc_wg) {
        amplitudes.Start();
        for (size_t i = 0; i < num_banded_wg; ++i) {
            f_bow_[i].Process_bp_norm_block(in, center, size, &d_bow_[i], amplitudes.Next());
           // sum_center += s * amplitudes.Next() * 8.0;
        }
    }
}

}  // namespace elements
