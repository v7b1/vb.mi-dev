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
// Naive bass drum model (modulated oscillator with FM + envelope).
// Inadvertently 909-ish.

#ifndef PLAITS_DSP_DRUMS_SYNTHETIC_BASS_DRUM_H_
#define PLAITS_DSP_DRUMS_SYNTHETIC_BASS_DRUM_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"


namespace plaits {

class SyntheticBassDrumClick {
 public:
  SyntheticBassDrumClick() { }
  ~SyntheticBassDrumClick() { }
  
  void Init() {
    lp_ = 0.0;
    hp_ = 0.0;
    filter_.Init();
      filter_.set_f_q<stmlib::FREQUENCY_FAST>(5000.0 / kSampleRate, 2.0);
  }
  
  double Process(double in) {
    SLOPE(lp_, in, 0.5, 0.1);
    ONE_POLE(hp_, lp_, 0.04);
    return filter_.Process<stmlib::FILTER_MODE_LOW_PASS>(lp_ - hp_);
  }
  
 private:
  double lp_;
  double hp_;
  stmlib::Svf filter_;
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticBassDrumClick);
};

class SyntheticBassDrumAttackNoise {
 public:
  SyntheticBassDrumAttackNoise() { }
  ~SyntheticBassDrumAttackNoise() { }
  
  void Init() {
    lp_ = 0.0;
    hp_ = 0.0;
  }
  
  double Render() {
    double sample = stmlib::Random::GetDouble();
    ONE_POLE(lp_, sample, 0.05);
    ONE_POLE(hp_, lp_, 0.005);
    return lp_ - hp_;
  }
  
 private:
  double lp_;
  double hp_;
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticBassDrumAttackNoise);
};

class SyntheticBassDrum {
 public:
  SyntheticBassDrum() { }
  ~SyntheticBassDrum() { }

  void Init() {
    phase_ = 0.0;
    phase_noise_ = 0.0;
    f0_ = 0.0;
    fm_ = 0.0;
    fm_lp_ = 0.0;
    body_env_lp_ = 0.0;
    body_env_ = 0.0;
    body_env_pulse_width_ = 0;
    fm_pulse_width_ = 0;
    tone_lp_ = 0.0;
    sustain_gain_ = 0.0;
    
    click_.Init();
    noise_.Init();
  }
  
  inline double DistortedSine(double phase, double phase_noise, double dirtiness) {
    phase += phase_noise * dirtiness;
    MAKE_INTEGRAL_FRACTIONAL(phase);
    phase = phase_fractional;
    double triangle = (phase < 0.5 ? phase : 1.0 - phase) * 4.0 - 1.0;
    double sine = 2.0 * triangle / (1.0 + fabs(triangle));
    double clean_sine = stmlib::InterpolateWrap(
        lut_sine, phase + 0.75, 1024.0);
    return sine + (1.0 - dirtiness) * (clean_sine - sine);
  }
  
  inline double TransistorVCA(double s, double gain) {
    s = (s - 0.6) * gain;
    return 3.0 * s / (2.0 + fabs(s)) + gain * 0.3;
  }
  
  void Render(
      bool sustain,
      bool trigger,
      double accent,
      double f0,
      double tone,
      double decay,
      double dirtiness,
      double fm_envelope_amount,
      double fm_envelope_decay,
      double* out,
      size_t size) {
    decay *= decay;
    fm_envelope_decay *= fm_envelope_decay;
    
    stmlib::ParameterInterpolator f0_mod(&f0_, f0, size);
    
    dirtiness *= std::max(1.0 - 8.0 * f0, 0.0);
      //const double sr = Dsp::getSr();
    const double fm_decay = 1.0 - \
        1.0 / (0.008 * (1.0 + fm_envelope_decay * 4.0) * kSampleRate);

    const double body_env_decay = 1.0 - 1.0 / (0.02 * kSampleRate) * \
        stmlib::SemitonesToRatio(-decay * 60.0);
    const double transient_env_decay = 1.0 - 1.0 / (0.005 * kSampleRate);
    const double tone_f = std::min(
        4.0 * f0 * stmlib::SemitonesToRatio(tone * 108.0),
        1.0);
    const double transient_level = tone;
    
    if (trigger) {
      fm_ = 1.0;
      body_env_ = transient_env_ = 0.3 + 0.7 * accent;
      body_env_pulse_width_ = kSampleRate * 0.001;
      fm_pulse_width_ = kSampleRate * 0.0013;
    }
    
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_,
        accent * decay,
        size);
    
    while (size--) {
      ONE_POLE(phase_noise_, stmlib::Random::GetDouble() - 0.5, 0.002);
      
      double mix = 0.0;

      if (sustain) {
        phase_ += f0_mod.Next();
        if (phase_ >= 1.0) {
          phase_ -= 1.0;
        }
        double body = DistortedSine(phase_, phase_noise_, dirtiness);
        mix -= TransistorVCA(body, sustain_gain.Next());
      } else {
        if (fm_pulse_width_) {
          --fm_pulse_width_;
          phase_ = 0.25f;
        } else {
          fm_ *= fm_decay;
          double fm = 1.0 + fm_envelope_amount * 3.5 * fm_lp_;
          phase_ += std::min(f0_mod.Next() * fm, 0.5);
          if (phase_ >= 1.0) {
            phase_ -= 1.0;
          }
        }
      
        if (body_env_pulse_width_) {
          --body_env_pulse_width_;
        } else {
          body_env_ *= body_env_decay;
          transient_env_ *= transient_env_decay;
        }
      
        const double envelope_lp_f = 0.1;
        ONE_POLE(body_env_lp_, body_env_, envelope_lp_f);
        ONE_POLE(transient_env_lp_, transient_env_, envelope_lp_f);
        ONE_POLE(fm_lp_, fm_, envelope_lp_f);
      
        double body = DistortedSine(phase_, phase_noise_, dirtiness);
        double transient = click_.Process(
            body_env_pulse_width_ ? 0.0 : 1.0) + noise_.Render();
      
        mix -= TransistorVCA(body, body_env_lp_);
        mix -= transient * transient_env_lp_ * transient_level;
      }

      ONE_POLE(tone_lp_, mix, tone_f);
      *out++ = tone_lp_;
    }
  }

 private:
  double f0_;
  double phase_;
  double phase_noise_;

  double fm_;
  double fm_lp_;
  double body_env_;
  double body_env_lp_;
  double transient_env_;
  double transient_env_lp_;
  
  double sustain_gain_;
  
  double tone_lp_;
  
  SyntheticBassDrumClick click_;
  SyntheticBassDrumAttackNoise noise_;
  
  int body_env_pulse_width_;
  int fm_pulse_width_;
  
  DISALLOW_COPY_AND_ASSIGN(SyntheticBassDrum);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_SYNTHETIC_BASS_DRUM_H_
