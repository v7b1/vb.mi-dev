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

#include "rings/dsp/string.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "rings/resources.h"

//extern double gSampleRate;

namespace rings {
  
using namespace std;
using namespace stmlib;

void String::Init(bool enable_dispersion) {
  enable_dispersion_ = enable_dispersion;
  
  string_.Init();
  stretch_.Init();
  fir_damping_filter_.Init();
  iir_damping_filter_.Init();
  
  set_frequency(220.0 / Dsp::getSr());
  set_dispersion(0.25);
  set_brightness(0.5);
  set_damping(0.3);
  set_position(0.8);
  
  delay_ = 1.0 / frequency_;
  clamped_position_ = 0.0;
  previous_dispersion_ = 0.0;
  dispersion_noise_ = 0.0;
  curved_bridge_ = 0.0;
  previous_damping_compensation_ = 0.0;
  
  out_sample_[0] = out_sample_[1] = 0.0;
  aux_sample_[0] = aux_sample_[1] = 0.0;
  
  dc_blocker_.Init(1.0 - 20.0 / Dsp::getSr());
}

template<bool enable_dispersion>
void String::ProcessInternal(
    const double* in,
    double* out,
    double* aux,
    size_t size) {
  double delay = 1.0 / frequency_;
  CONSTRAIN(delay, 4.0, kDelayLineSize - 4.0);
  
  // If there is not enough delay time in the delay line, we play at the
  // lowest possible note and we upsample on the fly with a shitty linear
  // interpolator. We don't care because it's a corner case (f0 < 11.7Hz)
  double src_ratio = delay * frequency_;
  if (src_ratio >= 0.9999) {
    // When we are above 11.7 Hz, we make sure that the linear interpolator
    // does not get in the way.
    src_phase_ = 1.0;
    src_ratio = 1.0;
  }

  double clamped_position = 0.5 - 0.98 * abs(position_ - 0.5);
  
  // Linearly interpolate all comb-related CV parameters for each sample.
  ParameterInterpolator delay_modulation(
      &delay_, delay, size);
  ParameterInterpolator position_modulation(
      &clamped_position_, clamped_position, size);
  ParameterInterpolator dispersion_modulation(
      &previous_dispersion_, dispersion_, size);
  
  // For damping/absorption, the interpolation is done in the filter code.
  double lf_damping = damping_ * (2.0 - damping_);
  double rt60 = 0.07 * SemitonesToRatio(lf_damping * 96.0) * Dsp::getSr();
  double rt60_base_2_12 = max(-120.0 * delay / src_ratio / rt60, -127.0);
  double damping_coefficient = SemitonesToRatio(rt60_base_2_12);
  double brightness = brightness_ * brightness_;
  double noise_filter = SemitonesToRatio((brightness_ - 1.0) * 48.0);
  double damping_cutoff = min(
      24.0 + damping_ * damping_ * 48.0 + brightness_ * brightness_ * 24.0,
      84.0);
  double damping_f = min(frequency_ * SemitonesToRatio(damping_cutoff), 0.499);
  
  // Crossfade to infinite decay.
  if (damping_ >= 0.95) {
    double to_infinite = 20.0 * (damping_ - 0.95);
    damping_coefficient += to_infinite * (1.0 - damping_coefficient);
    brightness += to_infinite * (1.0 - brightness);
    damping_f += to_infinite * (0.4999 - damping_f);
    damping_cutoff += to_infinite * (128.0 - damping_cutoff);
  }
  
  fir_damping_filter_.Configure(damping_coefficient, brightness, size);
  iir_damping_filter_.set_f_q<FREQUENCY_ACCURATE>(damping_f, 0.5);
  ParameterInterpolator damping_compensation_modulation(
      &previous_damping_compensation_,
      1.0 - Interpolate(lut_svf_shift, damping_cutoff, 1.0),
      size);
  
  while (size--) {
    src_phase_ += src_ratio;
    if (src_phase_ > 1.0) {
      src_phase_ -= 1.0;
      
      double delay = delay_modulation.Next();
      double comb_delay = delay * position_modulation.Next();
    
#ifndef MIC_W
      delay *= damping_compensation_modulation.Next();  // IIR delay.
#endif  // MIC_W
      delay -= 1.0; // FIR delay.
    
      double s = 0.0;

      if (enable_dispersion) {
        double noise = 2.0 * Random::GetDouble() - 1.0;
        noise *= 1.0 / (0.2 + noise_filter);
        dispersion_noise_ += noise_filter * (noise - dispersion_noise_);

        double dispersion = dispersion_modulation.Next();
        double stretch_point = dispersion <= 0.0
            ? 0.0
            : dispersion * (2.0 - dispersion) * 0.475;
        double noise_amount = dispersion > 0.75
            ? 4.0 * (dispersion - 0.75)
            : 0.0;
        double bridge_curving = dispersion < 0.0
            ? -dispersion
            : 0.0;
        
        noise_amount = noise_amount * noise_amount * 0.025;
        double ac_blocking_amount = bridge_curving;

        bridge_curving = bridge_curving * bridge_curving * 0.01;
        double ap_gain = -0.618 * dispersion / (0.15 + abs(dispersion));
        
        double delay_fm = 1.0;
        delay_fm += dispersion_noise_ * noise_amount;
        delay_fm -= curved_bridge_ * bridge_curving;
        delay *= delay_fm;
        
        double ap_delay = delay * stretch_point;
        double main_delay = delay - ap_delay;
        if (ap_delay >= 4.0 && main_delay >= 4.0) {
          s = string_.ReadHermite(main_delay);
          s = stretch_.Allpass(s, ap_delay, ap_gain);
        } else {
          s = string_.ReadHermite(delay);
        }
        double s_ac = s;
        dc_blocker_.Process(&s_ac, 1);
        s += ac_blocking_amount * (s_ac - s);
        
        double value = abs(s) - 0.025;
        double sign = s > 0.0 ? 1.0 : -1.5;
        curved_bridge_ = (abs(value) + value) * sign;
      } else {
        s = string_.ReadHermite(delay);
      }
    
      s += *in;  // When f0 < 11.7 Hz, causes ugly bitcrushing on the input!
      s = fir_damping_filter_.Process(s);
#ifndef MIC_W
      s = iir_damping_filter_.Process<FILTER_MODE_LOW_PASS>(s);
#endif  // MIC_W
        string_.Write(s);     // TODO: crashed here

      out_sample_[1] = out_sample_[0];
      aux_sample_[1] = aux_sample_[0];

      out_sample_[0] = s;
      aux_sample_[0] = string_.Read(comb_delay);
    }
    *out++ += Crossfade(out_sample_[1], out_sample_[0], src_phase_);
    *aux++ += Crossfade(aux_sample_[1], aux_sample_[0], src_phase_);
    in++;
  }
}

void String::Process(const double* in, double* out, double* aux, size_t size) {
  if (enable_dispersion_) {
    ProcessInternal<true>(in, out, aux, size);
  } else {
    ProcessInternal<false>(in, out, aux, size);
  }
}

}  // namespace rings
