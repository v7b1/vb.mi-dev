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
// Oscillator.

#ifndef WARPS_DSP_OSCILLATOR_H_
#define WARPS_DSP_OSCILLATOR_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

#include "warps/dsp/parameters.h"
#include "warps/resources.h"


namespace warps {

const double kInternalOscillatorSampleRate = 96000.0;

class Oscillator {
 public:
  Oscillator() { }
  ~Oscillator() { }
  
  void Init(double sample_rate);
  
  double Render(
      OscillatorShape shape,
      double note,
      double* modulation,
      double* out,
      size_t size);

  inline double midi_to_increment(double midi_pitch) const {
    int32_t pitch = static_cast<int32_t>(midi_pitch * 256.0);
    pitch = 32768 + stmlib::Clip16(pitch - 20480);
      /*
    double increment = lut_midi_to_f_high[pitch >> 8] * \
        lut_midi_to_f_low[pitch & 0xff];*/
      double increment = lut_midi_to_f_high[pitch >> 8] * pit_scale_ * \
      lut_midi_to_f_low[pitch & 0xff];
    return increment;
  }
  
  typedef double (Oscillator::*RenderFn)(
      double note, double* mod, double* out, size_t size);

 private:
  double Duck(
      const double* internal,
      const double* external,
      double* destination,
      size_t size);

  template<OscillatorShape shape>
  double RenderPolyblep(double, double*, double*, size_t);
  double RenderSine(double, double*, double*, size_t);
  double RenderNoise(double, double*, double*, size_t);
  
  static inline double ThisBlepSample(double t) {
    return 0.5 * t * t;
  }
  static inline double NextBlepSample(double t) {
    t = 1.0 - t;
    return -0.5 * t * t;
  }

  bool high_;
  double phase_;
  double phase_increment_;
  double next_sample_;
  double lp_state_;
  double hp_state_;
  
  double external_input_level_;
  double one_hertz_;
    double pit_scale_;      //vb: handle sample rate changes
  
  static RenderFn fn_table_[];
  stmlib::Svf filter_;

  DISALLOW_COPY_AND_ASSIGN(Oscillator);
};

}  // namespace warps

#endif  // WARPS_DSP_OSCILLATOR_H_
