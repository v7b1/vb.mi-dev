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

#include "elements/dsp/resonator.h"

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
  
    set_frequency(220.0 / Dsp::getSr());
    // set_frequency(220.0 / kSampleRate);
  set_geometry(0.25);
  set_brightness(0.5);
  set_damping(0.3);
  set_position(0.999);
  set_resolution(kMaxModes);
    previous_position_ = 0.0;
  
  bow_signal_ = 0.0;
    
    // vb
    lp_bow.Init();
    lp_bow.set_f<FREQUENCY_EXACT>(0.45);
}

size_t Resonator::ComputeFilters() {
  ++clock_divider_;
  double stiffness = Interpolate(lut_stiffness, geometry_, 256.0);
    //std::cout << "stiffness: " << stiffness << "\n";
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
    bool update = i <= 24 || ((i & 1) == (clock_divider_ & 1));
    double partial_frequency = harmonic * stretch_factor;
    if (partial_frequency >= 0.37) {    // vb, was: 0.49 // 0.37
      partial_frequency = 0.37;
    } else {
      num_modes = i + 1;
    }
      // vb, add filter freq array for information outlet
      filtFreqs_[i] = partial_frequency;
      
    if (update) {
        f_[i].set_f_q<FREQUENCY_FAST>(        // TODO: vb: also try FREQUENCY_EXACT
          partial_frequency,
          1.0 + partial_frequency * q);
      if (i < kMaxBowedModes) {     //min(kMaxBowedModes, num_modes)
        size_t period = 1.0 / partial_frequency;
        while (period >= kMaxDelayLineSize) period >>= 1;
        d_bow_[i].set_delay(period);
        f_bow_[i].set_g_q(f_[i].g(), 1.0 + partial_frequency * 1500.0);
      }
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
  
  return num_modes;
}

void Resonator::Process(
    const double* bow_strength,
    const double* in,
    double* center,
    double* sides,
    size_t size) {
  size_t num_modes = ComputeFilters();
  size_t num_banded_wg = min(kMaxBowedModes, num_modes);

  // Linearly interpolate position. This parameter is extremely sensitive to
  // zipper noise.
  double position_increment = (position_ - previous_position_) / size;
  while (size--) {
      double s = 0.0;         // vb: init with zero, so we can easier mute later

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
    aux_amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(
        modulation_offset_ + lfo);
  
    // Render normal modes.
    double input = *in++ * 0.125;
    double sum_center = 0.0;
    double sum_side = 0.0;

    // Note: For a steady sound, the correct way of simulating the effect of
    // a pickup is to use a comb filter. But it sounds very flange-y when
    // modulated, even mildly, and incur a slight delay/smearing of the
    // attacks.
    // Thus, we directly apply the comb filter in the frequency domain by
    // adjusting the amplitude of each mode in the sum. Because the
    // partials may not be in an integer ratios, what we are doing here is
    // approximative when the stretch factor is non null.
    // It sounds interesting nevertheless.
    amplitudes.Start();
    aux_amplitudes.Start();
    for (size_t i = 0; i < num_modes; i++) {
      s = f_[i].Process<FILTER_MODE_BAND_PASS>(input);
      sum_center += s * amplitudes.Next();
      sum_side += s * aux_amplitudes.Next();
    }
    *sides++ = sum_side - sum_center;
    
    // Render bowed modes.
    double bow_signal = 0.0;
    input += bow_signal_;
    amplitudes.Start();
    for (size_t i = 0; i < num_banded_wg; ++i) {
      s = 0.99 * d_bow_[i].Read();
      bow_signal += s;
      s = f_bow_[i].Process<FILTER_MODE_BAND_PASS_NORMALIZED>(input + s);
      //d_bow_[i].Write(s);
        d_bow_[i].Write(lp_bow.Process<FILTER_MODE_LOW_PASS>(s));   // vb, prevent high freq ringing
      sum_center += s * amplitudes.Next() * 8.0;
    }
    bow_signal_ = BowTable(bow_signal, *bow_strength++);
    *center++ = sum_center;
  }
}

}  // namespace elements
