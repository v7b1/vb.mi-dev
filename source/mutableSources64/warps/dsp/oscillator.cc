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

#include "warps/dsp/oscillator.h"

#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/random.h"


namespace warps {

using namespace stmlib;

const double kToFloat = 1.0 / 4294967296.0;
const double kToUint32 = 4294967296.0;


void Oscillator::Init(double sample_rate) {
  one_hertz_ = 1.0 / sample_rate;
    pit_scale_ = kInternalOscillatorSampleRate / sample_rate;
  next_sample_ = 0.0;
  phase_ = 0.0;
  phase_increment_ = 100.0 * one_hertz_;
  hp_state_ = 0.0;
  lp_state_ = 0.0;
  
  high_ = false;
  
  external_input_level_ = 0.0;

  filter_.Init();
}

double Oscillator::Render(
    OscillatorShape shape,
    double note,
    double* modulation,
    double* out,
    size_t size) {
  return (this->*fn_table_[shape])(note, modulation, out, size);
}

double Oscillator::RenderSine(
    double note,
    double* modulation,
    double* out,
    size_t size) {
  double phase = phase_;
  ParameterInterpolator phase_increment(
      &phase_increment_,
      midi_to_increment(note),
      size);
  while (size--) {
    phase += phase_increment.Next();
    if (phase >= 1.0) {
      phase -= 1.0;
    }
    uint32_t modulated_phase = static_cast<uint32_t>(phase * kToUint32);
    modulated_phase += static_cast<int32_t>(*modulation++ * 0.5 * kToUint32);
    uint32_t integral = modulated_phase >> 22;
    double fractional = static_cast<double>(modulated_phase << 10) * kToFloat;
    double a = lut_sin[integral];
    double b = lut_sin[integral + 1];
    *out++ = a + (b - a) * fractional;
  }
  phase_ = phase;
  return 1.0;
}

template<OscillatorShape shape>
double Oscillator::RenderPolyblep(
    double note,
    double* modulation,
    double* out,
    size_t size) {
  double phase = phase_;
  ParameterInterpolator phase_increment(
      &phase_increment_,
      midi_to_increment(note),
      size);
  
  double next_sample = next_sample_;
  bool high = high_;
  double lp_state = lp_state_;
  double hp_state = hp_state_;
  
  while (size--) {
    double this_sample = next_sample;
    next_sample = 0.0;

    double modulated_increment = phase_increment.Next() * (1.0 + *modulation++);
    
    if (modulated_increment <= 0.0) {
      modulated_increment = 1.0e-7;
    }
    phase += modulated_increment;
    
    if (shape == OSCILLATOR_SHAPE_TRIANGLE) {
      if (!high && phase >= 0.5) {
        double t = (phase - 0.5) / modulated_increment;
        this_sample += ThisBlepSample(t);
        next_sample += NextBlepSample(t);
        high = true;
      }
      if (phase >= 1.0) {
        phase -= 1.0;
        double t = phase / modulated_increment;
        this_sample -= ThisBlepSample(t);
        next_sample -= NextBlepSample(t);
        high = false;
      }
      const double integrator_coefficient = modulated_increment * 0.0625;
      next_sample += phase < 0.5 ? 0.0 : 1.0;
      this_sample = 128.0 * (this_sample - 0.5);
      lp_state += integrator_coefficient * (this_sample - lp_state);
      *out++ = lp_state;
    } else {
      if (phase >= 1.0) {
        phase -= 1.0;
        double t = phase / modulated_increment;
        this_sample -= ThisBlepSample(t);
        next_sample -= NextBlepSample(t);
      }
      next_sample += phase;
      
      if (shape == OSCILLATOR_SHAPE_SAW) {
        this_sample = this_sample * 2.0 - 1.0;
        // Slight roll-off of high frequencies - prevent high components near
        // 48kHz that are not eliminated by the upsampling filter.
        lp_state += 0.3 * (this_sample - lp_state);
        *out++ = lp_state;
      } else {
        lp_state += 0.25 * ((hp_state - this_sample) - lp_state);
        *out++ = 4.0 * lp_state;
        hp_state = this_sample;
      }
    }
  }
  
  high_ = high;
  phase_ = phase;
  next_sample_ = next_sample;
  lp_state_ = lp_state;
  hp_state_ = hp_state;
  
  return shape == OSCILLATOR_SHAPE_PULSE
      ?  0.025 / (0.0002 + phase_increment_)
      : 1.0;
}

double Oscillator::Duck(
    const double* internal,
    const double* external,
    double* destination, size_t size) {
  double level = external_input_level_;
  for (size_t i = 0; i < size; ++i) {
    double error = external[i] * external[i] - level;
    level += ((error > 0.0) ? 0.01 : 0.0001) * error;
    double internal_gain = 1.0 - 32.0 * level;
    if (internal_gain <= 0.0) {
      internal_gain = 0.0;
    }
    destination[i] = external[i] + internal_gain * (internal[i] - external[i]);
  }
  external_input_level_ = level;
  return level;
}

double Oscillator::RenderNoise(
    double note,
    double* modulation,
    double* out,
    size_t size) {
  for (size_t i = 0; i < size; ++i) {
    double noise = static_cast<double>(stmlib::Random::GetWord()) * kToFloat;
    out[i] = 2.0 * noise - 1.0;
  }
  Duck(out, modulation, out, size);
  filter_.set_f_q<FREQUENCY_ACCURATE>(midi_to_increment(note) * 4.0, 1.0);
  filter_.Process<FILTER_MODE_LOW_PASS>(out, out, size);
  return 1.0;
}

/* static */
Oscillator::RenderFn Oscillator::fn_table_[] = {
  &Oscillator::RenderSine,
  &Oscillator::RenderPolyblep<OSCILLATOR_SHAPE_TRIANGLE>,
  &Oscillator::RenderPolyblep<OSCILLATOR_SHAPE_SAW>,
  &Oscillator::RenderPolyblep<OSCILLATOR_SHAPE_PULSE>,
  &Oscillator::RenderNoise,
};

}  // namespace warps
