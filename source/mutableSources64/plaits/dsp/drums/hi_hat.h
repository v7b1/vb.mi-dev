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
// 808 HH, with a few extra parameters to push things to the CY territory...
// The template parameter MetallicNoiseSource allows another kind of "metallic
// noise" to be used, for results which are more similar to KR-55 or FM hi-hats.

#ifndef PLAITS_DSP_DRUMS_HI_HAT_H_
#define PLAITS_DSP_DRUMS_HI_HAT_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/oscillator.h"


namespace plaits {

// 808 style "metallic noise" with 6 square oscillators.
class SquareNoise {
 public:
  SquareNoise() { }
  ~SquareNoise() { }

  void Init() {
    std::fill(&phase_[0], &phase_[6], 0);
  }
    
  void Render(double f0, double* temp_1, double* temp_2, double* out, size_t size) {
    const double ratios[6] = {
        // Nominal f0: 414 Hz
        1.0, 1.304, 1.466, 1.787, 1.932, 2.536
    };
  
    uint32_t increment[6];
    uint32_t phase[6];
    for (int i = 0; i < 6; ++i) {
      double f = f0 * ratios[i];
      if (f >= 0.499) f = 0.499;
      increment[i] = static_cast<uint32_t>(f * 4294967296.0);
      phase[i] = phase_[i];
    }

    while (size--) {
      phase[0] += increment[0];
      phase[1] += increment[1];
      phase[2] += increment[2];
      phase[3] += increment[3];
      phase[4] += increment[4];
      phase[5] += increment[5];
      uint32_t noise = 0;
      noise += (phase[0] >> 31);
      noise += (phase[1] >> 31);
      noise += (phase[2] >> 31);
      noise += (phase[3] >> 31);
      noise += (phase[4] >> 31);
      noise += (phase[5] >> 31);
      *out++ = 0.33 * static_cast<double>(noise) - 1.0;
    }
  
    for (int i = 0; i < 6; ++i) {
      phase_[i] = phase[i];
    }
  }

 private:
  uint32_t phase_[6];

  DISALLOW_COPY_AND_ASSIGN(SquareNoise);
};

class RingModNoise {
 public:
  RingModNoise() { }
  ~RingModNoise() { }

  void Init() {
    for (int i = 0; i < 6; ++i) {
      oscillator_[i].Init();
    }
  }
  
  void Render(double f0, double* temp_1, double* temp_2, double* out, size_t size) {
      const double sr = Dsp::getSr();
    const double ratio = f0 / (0.01 + f0);
    const double f1a = 200.0 / sr * ratio;
    const double f1b = 7530.0 / sr * ratio;
    const double f2a = 510.0 / sr * ratio;
    const double f2b = 8075.0 / sr * ratio;
    const double f3a = 730.0 / sr * ratio;
    const double f3b = 10500.0 / sr * ratio;
    
    std::fill(&out[0], &out[size], 0.0);
    
    RenderPair(&oscillator_[0], f1a, f1b, temp_1, temp_2, out, size);
    RenderPair(&oscillator_[2], f2a, f2b, temp_1, temp_2, out, size);
    RenderPair(&oscillator_[4], f3a, f3b, temp_1, temp_2, out, size);
  }

 private:
  void RenderPair(
      Oscillator* osc,
      double f1,
      double f2,
      double* temp_1,
      double* temp_2,
      double* out,
      size_t size) {
    osc[0].Render<OSCILLATOR_SHAPE_SQUARE>(f1, 0.5, temp_1, size);
    osc[1].Render<OSCILLATOR_SHAPE_SAW>(f2, 0.5, temp_2, size);
    while (size--) {
      *out++ += *temp_1++ * *temp_2++;
    }
  }
  Oscillator oscillator_[6];
  
  DISALLOW_COPY_AND_ASSIGN(RingModNoise);
};

class SwingVCA {
 public:
  double operator()(double s, double gain) {
   s *= s > 0.0 ? 10.0 : 0.1;
   s = s / (1.0 + fabs(s));
   return (s + 1.0) * gain;
  }
};

class LinearVCA {
 public:
  double operator()(double s, double gain) {
   return s * gain;
  }
};

template<typename MetallicNoiseSource, typename VCA, bool resonance>
class HiHat {
 public:
  HiHat() { }
  ~HiHat() { }

  void Init() {
    envelope_ = 0.0;
    noise_clock_ = 0.0;
    noise_sample_ = 0.0;
    sustain_gain_ = 0.0;

    metallic_noise_.Init();
    noise_coloration_svf_.Init();
    hpf_.Init();
  }
  
  void Render(
      bool sustain,
      bool trigger,
      double accent,
      double f0,
      double tone,
      double decay,
      double noisiness,
      double* temp_1,
      double* temp_2,
      double* out,
      size_t size) {
    const double envelope_decay = 1.0 - 0.003 * stmlib::SemitonesToRatio(
        -decay * 84.0);
    const double cut_decay = 1.0 - 0.0025 * stmlib::SemitonesToRatio(
        -decay * 36.0);
    
    if (trigger) {
      envelope_ = (1.5 + 0.5 * (1.0 - decay)) * (0.3 + 0.7 * accent);
    }

    // Render the metallic noise.
    metallic_noise_.Render(2.0 * f0, temp_1, temp_2, out, size);

    // Apply BPF on the metallic noise.
      const double sr = Dsp::getSr();
    double cutoff = 150.0 / sr * stmlib::SemitonesToRatio(
        tone * 72.0);
    CONSTRAIN(cutoff, 0.0, 16000.0 / sr);
    noise_coloration_svf_.set_f_q<stmlib::FREQUENCY_ACCURATE>(
        cutoff, resonance ? 3.0 + 6.0 * tone : 1.0);
    noise_coloration_svf_.Process<stmlib::FILTER_MODE_BAND_PASS>(
        out, out, size);
    
    // This is not at all part of the 808 circuit! But to add more variety, we
    // add a variable amount of clocked noise to the output of the 6 schmitt
    // trigger oscillators.
    noisiness *= noisiness;
    double noise_f = f0 * (16.0 + 16.0 * (1.0 - noisiness));
    CONSTRAIN(noise_f, 0.0, 0.5);
    
    for (size_t i = 0; i < size; ++i) {
      noise_clock_ += noise_f;
      if (noise_clock_ >= 1.0) {
        noise_clock_ -= 1.0;
        noise_sample_ = stmlib::Random::GetDouble() - 0.5;
      }
      out[i] += noisiness * (noise_sample_ - out[i]);
    }

    // Apply VCA.
    stmlib::ParameterInterpolator sustain_gain(
        &sustain_gain_,
        accent * decay,
        size);
    for (size_t i = 0; i < size; ++i) {
      VCA vca;
      envelope_ *= envelope_ > 0.5 ? envelope_decay : cut_decay;
      out[i] = vca(out[i], sustain ? sustain_gain.Next() : envelope_);
    }
    
    hpf_.set_f_q<stmlib::FREQUENCY_ACCURATE>(cutoff, 0.5);
    hpf_.Process<stmlib::FILTER_MODE_HIGH_PASS>(out, out, size);
  }

 private:
  double envelope_;
  double noise_clock_;
  double noise_sample_;
  double sustain_gain_;

  MetallicNoiseSource metallic_noise_;
  stmlib::Svf noise_coloration_svf_;
  stmlib::Svf hpf_;
  
  DISALLOW_COPY_AND_ASSIGN(HiHat);
};
  
}  // namespace plaits

#endif  // PLAITS_DSP_DRUMS_HI_HAT_H_
