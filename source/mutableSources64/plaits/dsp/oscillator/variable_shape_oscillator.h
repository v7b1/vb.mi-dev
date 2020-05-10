// Copyright 2016 Emilie Gillet.
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
// Continuously variable waveform: triangle > saw > square. Both square and
// triangle have variable slope / pulse-width. Additionally, the phase resets
// can be locked to a master frequency.

#ifndef PLAITS_DSP_OSCILLATOR_VARIABLE_SHAPE_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_VARIABLE_SHAPE_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include <algorithm>

namespace plaits {

class VariableShapeOscillator {
 public:
  VariableShapeOscillator() { }
  ~VariableShapeOscillator() { }

  void Init() {
    master_phase_ = 0.0;
    slave_phase_ = 0.0;
    next_sample_ = 0.0;
    previous_pw_ = 0.5;
    high_ = false;
  
    master_frequency_ = 0.0;
    slave_frequency_ = 0.01;
    pw_ = 0.5;
    waveshape_ = 0.0;
  }
  
  template<bool enable_sync>
  void Render(
      double master_frequency,
      double frequency,
      double pw,
      double waveshape,
      double* out,
      size_t size) {
    if (master_frequency >= kMaxFrequency) {
      master_frequency = kMaxFrequency;
    }
    if (frequency >= kMaxFrequency) {
      frequency = kMaxFrequency;
    }
    
    if (frequency >= 0.25) {
      pw = 0.5;
    } else {
      CONSTRAIN(pw, frequency * 2.0, 1.0 - 2.0 * frequency);
    }

    stmlib::ParameterInterpolator master_fm(
        &master_frequency_, master_frequency, size);
    stmlib::ParameterInterpolator fm(&slave_frequency_, frequency, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
    stmlib::ParameterInterpolator waveshape_modulation(
        &waveshape_, waveshape, size);

    double next_sample = next_sample_;
    
    while (size--) {
      bool reset = false;
      bool transition_during_reset = false;
      double reset_time = 0.0;

      double this_sample = next_sample;
      next_sample = 0.0f;
    
      const double master_frequency = master_fm.Next();
      const double slave_frequency = fm.Next();
      const double pw = pwm.Next();
      const double waveshape = waveshape_modulation.Next();
      const double square_amount = std::max(waveshape - 0.5, 0.0) * 2.0;
      const double triangle_amount = std::max(1.0 - waveshape * 2.0, 0.0);
      const double slope_up = 1.0 / (pw);
      const double slope_down = 1.0 / (1.0 - pw);

      if (enable_sync) {
        master_phase_ += master_frequency;
        if (master_phase_ >= 1.0f) {
          master_phase_ -= 1.0f;
          reset_time = master_phase_ / master_frequency;
      
          double slave_phase_at_reset = slave_phase_ + \
              (1.0 - reset_time) * slave_frequency;
          reset = true;
          if (slave_phase_at_reset >= 1.0) {
            slave_phase_at_reset -= 1.0;
            transition_during_reset = true;
          }
          if (!high_ && slave_phase_at_reset >= pw) {
            transition_during_reset = true;
          }
          double value = ComputeNaiveSample(
              slave_phase_at_reset,
              pw,
              slope_up,
              slope_down,
              triangle_amount,
              square_amount);
          this_sample -= value * stmlib::ThisBlepSample(reset_time);
          next_sample -= value * stmlib::NextBlepSample(reset_time);
        }
      }
      
      slave_phase_ += slave_frequency;
      while (transition_during_reset || !reset) {
        if (!high_) {
          if (slave_phase_ < pw) {
            break;
          }
          double t = (slave_phase_ - pw) / (previous_pw_ - pw + slave_frequency);
          double triangle_step = (slope_up + slope_down) * slave_frequency;
          triangle_step *= triangle_amount;
          
          this_sample += square_amount * stmlib::ThisBlepSample(t);
          next_sample += square_amount * stmlib::NextBlepSample(t);
          this_sample -= triangle_step * stmlib::ThisIntegratedBlepSample(t);
          next_sample -= triangle_step * stmlib::NextIntegratedBlepSample(t);
          high_ = true;
        }
      
        if (high_) {
          if (slave_phase_ < 1.0) {
            break;
          }
          slave_phase_ -= 1.0;
          double t = slave_phase_ / slave_frequency;
          double triangle_step = (slope_up + slope_down) * slave_frequency;
          triangle_step *= triangle_amount;

          this_sample -= (1.0 - triangle_amount) * stmlib::ThisBlepSample(t);
          next_sample -= (1.0 - triangle_amount) * stmlib::NextBlepSample(t);
          this_sample += triangle_step * stmlib::ThisIntegratedBlepSample(t);
          next_sample += triangle_step * stmlib::NextIntegratedBlepSample(t);
          high_ = false;
        }
      }
    
      if (enable_sync && reset) {
        slave_phase_ = reset_time * slave_frequency;
        high_ = false;
      }
    
      next_sample += ComputeNaiveSample(
          slave_phase_,
          pw,
          slope_up,
          slope_down,
          triangle_amount,
          square_amount);
      previous_pw_ = pw;

      *out++ = (2.0 * this_sample - 1.0);
    }
    
    next_sample_ = next_sample;
  }


 private:
  inline double ComputeNaiveSample(
      double phase,
      double pw,
      double slope_up,
      double slope_down,
      double triangle_amount,
      double square_amount) const {
    double saw = phase;
    double square = phase < pw ? 0.0 : 1.0;
    double triangle = phase < pw
        ? phase * slope_up
        : 1.0 - (phase - pw) * slope_down;
    saw += (square - saw) * square_amount;
    saw += (triangle - saw) * triangle_amount;
    return saw;
  }

  // Oscillator state.
  double master_phase_;
  double slave_phase_;
  double next_sample_;
  double previous_pw_;
  bool high_;

  // For interpolation of parameters.
  double master_frequency_;
  double slave_frequency_;
  double pw_;
  double waveshape_;

  DISALLOW_COPY_AND_ASSIGN(VariableShapeOscillator);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_VARIABLE_SHAPE_OSCILLATOR_H_
