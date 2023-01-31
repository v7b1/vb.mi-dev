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

#include "resonator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"

#include "elements/dsp/dsp.h"
#include "elements/resources.h"

#include <cstdio>


//namespace elements {

using namespace std;
using namespace stmlib;

void Resonator::Init(double sr) {
    
    sample_rate_ = sr;
    r_sr_ = 1.0 / sr;
    
    for (size_t i = 0; i < kMaxModes; ++i) {
        f_[i].Init();
        active_[i] = i;      // map of active filters
        
    }

    for (size_t i = 0; i < kMaxBowedModes; ++i) {
        f_bow_[i].Init();
        d_bow_[i].Init();
    }
  
    set_frequency(220.0 / sample_rate_);
    
    set_geometry(0.25);
    set_brightness(0.5);
    set_damping(0.3);
    set_position(0.999);
    set_resolution(kMaxModes);            // TODO: do we need this?
  
    //ComputeFilters();
    modulation_frequency_ = 0.5 * 64.0 / sample_rate_;
    
    calc_wg = calc_res = true;
    
    res_gain_ = wg_gain_ = 0.707;
    previous_position_ = 0.0;
}

size_t Resonator::ComputeFilters(double *freqs, double *qs, double *gains) {
  
    double stiffness = Interpolate(elements::lut_stiffness, geometry_, 256.0);
    double harmonic = frequency_;
    double stretch_factor = 1.0;
    double q = 500.0 * Interpolate(elements::lut_4_decades, damping_ * 0.8, 256.0);
    double brightness_attenuation = 1.0 - geometry_;
    // Reduces the range of brightness when geometry is very low, to prevent clipping.
    brightness_attenuation *= brightness_attenuation;
    brightness_attenuation *= brightness_attenuation;
    brightness_attenuation *= brightness_attenuation;
    double brightness = brightness_ * (1.0 - 0.2 * brightness_attenuation);
    double q_loss = brightness * (2.0 - brightness) * 0.85 + 0.15;
    double q_loss_damping_rate = geometry_ * (2.0 - geometry_) * 0.1;
    size_t num_modes = 0;
    for (size_t i = 0; i < min(kMaxModes, resolution_); ++i) {
        active_[i] = i;
        double partial_frequency = harmonic * stretch_factor;
        if (partial_frequency >= 0.49) {
            partial_frequency = 0.49;
        } else {
            num_modes = i + 1;
        }
      
        freqs[i] = partial_frequency * sample_rate_;
        qs[i] = 1.0 + partial_frequency * q;
        

        f_[i].set_f_q_g<FREQUENCY_FAST>(partial_frequency, qs[i], gains[i]);
        
        if (i < kMaxBowedModes) {
            size_t period = 1.0 / partial_frequency;
            while (period >= kMaxDelayLineSize) period >>= 1;
            d_bow_[i].set_delay(period);
            f_bow_[i].set_g_q(f_[i].g(), 1.0 + partial_frequency * 1500.0);
          }

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
        size_t num_modes = 0;
        for (size_t i = 0; i < min(kMaxModes, resolution_); ++i) {
            double partial_frequency = freqs[i] * r_sr_;
            
            if (partial_frequency >= 0.49) {
                partial_frequency = 0.49;
            }
            else if(gains[i] != 0.0) {
                active_[num_modes] = i;
                num_modes ++;
            }

            f_[i].set_f_q_g<FREQUENCY_FAST>(partial_frequency, qs[i], gains[i]);
            if (i < kMaxBowedModes) {
                size_t period = 1.0 / partial_frequency;
                while (period >= kMaxDelayLineSize) period >>= 1;
                d_bow_[i].set_delay(period);
//                f_bow_[i].set_g_q(f_[i].g(), 1.0 + partial_frequency * 1500.0);
                f_bow_[i].set_g_q_g(f_[i].g(), 1.0 + partial_frequency * 1500.0, gains[i] * 8.0);
                
//                printf("[%zu]: period: %zu -- freq: %f\n", i, period, partial_frequency);
            }
            
        }

        num_modes_ = num_modes;
//        printf("num_modes: %zu\n", num_modes);
//        for (size_t i = 0; i < num_modes; ++i)
//            printf("active[%zu]: %hhu\n", i, active_[i]);
    }
    
    
void Resonator::Process(
    const double* in,
    double* center,
    double* sides,
    size_t size)
    {
    size_t num_modes = num_modes_;
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
            uint8_t a = active_[i];
            f_[a].Process_bp_block(in, center, sides, size, amplitudes.Next()*res_gain_, aux_amplitudes.Next()*res_gain_);
        }

        for (size_t i=0; i<size; ++i)
            sides[i] -= center[i];
    }
        
    if(calc_wg) {
        amplitudes.Start();
        for (size_t i = 0; i < num_banded_wg; ++i) {
            uint8_t a = active_[i];
            if (a < kMaxBowedModes)
                f_bow_[a].Process_bp_norm_block(in, center, size, &d_bow_[a], amplitudes.Next()*wg_gain_);
           // sum_center += s * amplitudes.Next() * 8.0;
        }
    }
}

//}  // namespace elements
