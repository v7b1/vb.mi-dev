// Copyright 2016 Olivier Gillet.
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
// Comb filter / KS string. "Lite" version of the implementation used in Rings.

//#include "plaits/dsp/physical_modelling/string.h"
#include "string.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"


namespace plaits {
  
using namespace std;
using namespace stmlib;

void String::Init(BufferAllocator* allocator) {
  string_.Init(allocator->Allocate<double>(kDelayLineSize));
  stretch_.Init(allocator->Allocate<double>(kDelayLineSize / 4));
  delay_ = 100.0;
  Reset();
}

void String::Reset() {
  string_.Reset();
  stretch_.Reset();
  iir_damping_filter_.Init();
    dc_blocker_.Init(1.0 - 20.0 / Dsp::getSr());
  dispersion_noise_ = 0.0;
  curved_bridge_ = 0.0;
  out_sample_[0] = out_sample_[1] = 0.0;
  src_phase_ = 0.0;
}

void String::Process(
    double f0,
    double non_linearity_amount,
    double brightness,
    double damping,
    const double* in,
    double* out,
    size_t size) {
  if (non_linearity_amount <= 0.0) {
    ProcessInternal<STRING_NON_LINEARITY_CURVED_BRIDGE>(
        f0, -non_linearity_amount, brightness, damping, in, out, size);
  } else {
    ProcessInternal<STRING_NON_LINEARITY_DISPERSION>(
        f0, non_linearity_amount, brightness, damping, in, out, size);
  }
}

template<StringNonLinearity non_linearity>
void String::ProcessInternal(
    double f0,
    double non_linearity_amount,
    double brightness,
    double damping,
    const double* in,
    double* out,
    size_t size) {
  double delay = 1.0 / f0;
  CONSTRAIN(delay, 4.0, kDelayLineSize - 4.0);
  
  // If there is not enough delay time in the delay line, we play at the
  // lowest possible note and we upsample on the fly with a shitty linear
  // interpolator. We don't care because it's a corner case (f0 < 11.7Hz)
  double src_ratio = delay * f0;
  if (src_ratio >= 0.9999) {
    // When we are above 11.7 Hz, we make sure that the linear interpolator
    // does not get in the way.
    src_phase_ = 1.0;
    src_ratio = 1.0;
  }

  double damping_cutoff = min(
      12.0 + damping * damping * 60.0 + brightness * 24.0,
      84.0);
  double damping_f = min(f0 * SemitonesToRatio(damping_cutoff), 0.499);
  
  // Crossfade to infinite decay.
  if (damping >= 0.95) {
    double to_infinite = 20.0 * (damping - 0.95);
    brightness += to_infinite * (1.0 - brightness);
    damping_f += to_infinite * (0.4999 - damping_f);
    damping_cutoff += to_infinite * (128.0 - damping_cutoff);
  }
  
  iir_damping_filter_.set_f_q<FREQUENCY_FAST>(damping_f, 0.5);
  
  double damping_compensation = Interpolate(lut_svf_shift, damping_cutoff, 1.0);
  
  // Linearly interpolate delay time.
  ParameterInterpolator delay_modulation(
      &delay_, delay * damping_compensation, size);
  
  double stretch_point = non_linearity_amount * (2.0 - non_linearity_amount) * 0.225;
    double stretch_correction = (160.0 / Dsp::getSr()) * delay;
  CONSTRAIN(stretch_correction, 1.0, 2.1);
  
  double noise_amount_sqrt = non_linearity_amount > 0.75
      ? 4.0 * (non_linearity_amount - 0.75)
      : 0.0;
  double noise_amount = noise_amount_sqrt * noise_amount_sqrt * 0.1;
  double noise_filter = 0.06 + 0.94 * brightness * brightness;
  
  double bridge_curving_sqrt = non_linearity_amount;
  double bridge_curving = bridge_curving_sqrt * bridge_curving_sqrt * 0.01;
  
  double ap_gain = -0.618 * non_linearity_amount / (0.15 + fabs(non_linearity_amount));
  
  while (size--) {
    src_phase_ += src_ratio;
    if (src_phase_ > 1.0) {
      src_phase_ -= 1.0;
      
      double delay = delay_modulation.Next();
      double s = 0.0;
      
      if (non_linearity == STRING_NON_LINEARITY_DISPERSION) {
        double noise = Random::GetFloat() - 0.5;
        ONE_POLE(dispersion_noise_, noise, noise_filter)
        delay *= 1.0 + dispersion_noise_ * noise_amount;
      } else {
        delay *= 1.0 - curved_bridge_ * bridge_curving;
      }
      
      if (non_linearity == STRING_NON_LINEARITY_DISPERSION) {
        double ap_delay = delay * stretch_point;
        double main_delay = delay - ap_delay * (0.408 - stretch_point * 0.308) * stretch_correction;
        if (ap_delay >= 4.0 && main_delay >= 4.0) {
          s = string_.Read(main_delay);
          s = stretch_.Allpass(s, ap_delay, ap_gain);
        } else {
          s = string_.ReadHermite(delay);
        }
      } else {
        s = string_.ReadHermite(delay);
      }
      
      if (non_linearity == STRING_NON_LINEARITY_CURVED_BRIDGE) {
        double value = fabs(s) - 0.025;
        double sign = s > 0.0 ? 1.0 : -1.5;
        curved_bridge_ = (fabs(value) + value) * sign;
      }
    
      s += *in;
      CONSTRAIN(s, -20.0f, +20.0f);
      
      dc_blocker_.Process(&s, 1);
      s = iir_damping_filter_.Process<FILTER_MODE_LOW_PASS>(s);
      string_.Write(s);

      out_sample_[1] = out_sample_[0];
      out_sample_[0] = s;
    }
    *out++ += Crossfade(out_sample_[1], out_sample_[0], src_phase_);
    in++;
  }
}

}  // namespace plaits
