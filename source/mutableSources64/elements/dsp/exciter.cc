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
// Exciter.

#include "elements/dsp/exciter.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "elements/dsp/dsp.h"
#include "elements/resources.h"

namespace elements {

using namespace std;
using namespace stmlib;

void Exciter::Init() {
  set_model(EXCITER_MODEL_MALLET);
  set_parameter(0.0);
  set_timbre(0.99);

  lp_.Init();
  damp_state_ = 0.0;
  delay_ = 0;
  plectrum_delay_ = 0;
  particle_state_ = 0.5;
  damping_ = 0.0;
  signature_ = 0.0;
}

double Exciter::GetPulseAmplitude(double cutoff) {
  uint32_t cutoff_index = static_cast<uint32_t>(cutoff * 256.0);
  return lut_approx_svf_gain[cutoff_index];
}

void Exciter::Process(const uint8_t flags, double* out, size_t size) {
  damping_ = 0.0;
  (this->*fn_table_[model_])(flags, out, size);
  // Apply filters.
  if (model_ != EXCITER_MODEL_GRANULAR_SAMPLE_PLAYER &&
      model_ != EXCITER_MODEL_SAMPLE_PLAYER) {
    uint32_t cutoff_index = static_cast<uint32_t>(timbre_ * 256.0);
    if (model_ == EXCITER_MODEL_NOISE) {
      uint32_t resonance_index = static_cast<uint32_t>(parameter_ * 256.0);
      lp_.set_g_r(
          lut_approx_svf_g[cutoff_index],
          lut_approx_svf_r[resonance_index]);
    } else {
      lp_.set_g_r_h(
          lut_approx_svf_g[cutoff_index],
          2.0,
          lut_approx_svf_h[cutoff_index]);
    }
    lp_.Process<FILTER_MODE_LOW_PASS>(out, out, size);
  }
}

void Exciter::ProcessGranularSamplePlayer(
    const uint8_t flags, double* out, size_t size) {
  const uint32_t restart_prob = uint32_t(0.01 * 4294967296.0);
  const uint32_t restart_point = uint32_t(parameter_ * 32767.0) << 17;
  const uint32_t phase_increment = static_cast<uint32_t>(
      131072.0 * SemitonesToRatio(72.0 * timbre_ - 60.0));
  const int16_t* base = &smp_noise_sample[static_cast<size_t>(
      signature_ * 8192.0)];
  
  uint32_t phase = phase_;
  while (size--) {
    uint32_t phase_integral = phase >> 17;
    double phase_fractional = static_cast<double>(phase & 0x1fff) / 131072.0;
    double a = static_cast<double>(base[phase_integral]);
    double b = static_cast<double>(base[phase_integral + 1]);
    *out++ = (a + (b - a) * phase_fractional) / 32768.0;
    phase += phase_increment;
    if (Random::GetWord() < restart_prob) {
      phase = restart_point;
    }
  }
  phase_ = phase;
  damping_ = 0.0;
}

void Exciter::ProcessSamplePlayer(
    const uint8_t flags, double* out, size_t size) {
  double index = (1.0 - parameter_) * 8.0;
  MAKE_INTEGRAL_FRACTIONAL(index);
  if (index_integral == 8) {
    index_integral = 7;
    index_fractional = 1.0;
  }
  
  const uint32_t offset_1 = smp_boundaries[index_integral];
  const uint32_t offset_2 = smp_boundaries[index_integral + 1];
  const uint32_t length_1 = offset_2 - offset_1 - 1;
  const uint32_t length_2 = smp_boundaries[index_integral + 2] - offset_2 - 1;
  const uint32_t phase_increment = static_cast<uint32_t>(
      65536.0 * SemitonesToRatio(72.0 * timbre_ - 36.0 + 7.0));
  
  double damp = damp_state_;
  uint32_t phase = phase_;

  if (flags & EXCITER_FLAG_RISING_EDGE) {
    damp = 0.0;
    phase = 0;
  }
  if (!(flags & EXCITER_FLAG_GATE)) {
    damp = 1.0 - 0.95 * (1.0 - damp);
  }
  
  while (size--) {
    uint32_t phase_integral = phase >> 16;
    double phase_fractional = static_cast<double>(phase & 0xffff) / 65536.0;
    double sample_1 = 0.0;
    double sample_2 = 0.0;
    bool step = false;
    if (phase_integral < length_1) {
      const int16_t* base = &smp_sample_data[offset_1 + phase_integral];
      double a = static_cast<double>(base[0]);
      double b = static_cast<double>(base[1]);
      sample_1 = a + (b - a) * phase_fractional;
      step = true;
    }
    if (phase_integral < length_2) {
      const int16_t* base = &smp_sample_data[offset_2 + phase_integral];
      double a = static_cast<double>(base[0]);
      double b = static_cast<double>(base[1]);
      sample_2 = a + (b - a) * phase_fractional;
      step = true;
    }
    if (step) {
      phase += phase_increment;
    }
    
    *out++ = (sample_1 + (sample_2 - sample_1) * index_fractional) / 65536.0;
  }
  phase_ = phase;
  damping_ = damp * (parameter_ >= 0.8 ? parameter_ * 5.0 - 4.0 : 0.0);
  damp_state_ = damp;
}

void Exciter::ProcessMallet(const uint8_t flags, double* out, size_t size) {
  fill(&out[0], &out[size], 0.0);
  if (flags & EXCITER_FLAG_RISING_EDGE) {
    damp_state_ = 0.0;
    out[0] = GetPulseAmplitude(timbre_);
  }
  if (!(flags & EXCITER_FLAG_GATE)) {
    damp_state_ = 1.0 - 0.95 * (1.0 - damp_state_);
  }
  damping_ = damp_state_ * (1.0 - parameter_);
}

void Exciter::ProcessPlectrum(
    const uint8_t flags,
    double* out,
    size_t size) {
  double amplitude = GetPulseAmplitude(timbre_);
  double damp = damp_state_;
  double impulse = 0.0;
  if (flags & EXCITER_FLAG_RISING_EDGE) {
    impulse = -amplitude * (0.05 + signature_ * 0.2);
    plectrum_delay_ = static_cast<uint32_t>(
        4096.0 * parameter_ * parameter_) + 64;
  }
  while (size--) {
    if (plectrum_delay_) {
      --plectrum_delay_;
      if (plectrum_delay_ == 0) {
        impulse = amplitude;
      }
      damp = 1.0 - 0.997 * (1.0 - damp);
    } else {
      damp = 0.9 * damp;
    }
    *out++ = impulse;
    impulse = 0.0;
  }        
  damping_ = damp * 0.5;
  damp_state_ = damp;
}

void Exciter::ProcessParticles(
    const uint8_t flags,
    double* out,
    size_t size) {
  if (flags & EXCITER_FLAG_RISING_EDGE) {
    particle_state_ = RandomSample();
    particle_state_ = 1.0 - 0.6 * particle_state_ * particle_state_;
    delay_ = 0;
    particle_range_ = 1.0;
  }
  fill(&out[0], &out[size], 0.0);
  if (flags & EXCITER_FLAG_GATE) {
    const uint32_t up_probability = uint32_t(0.7 * 4294967296.0);
    const uint32_t down_probability = uint32_t(0.3 * 4294967296.0);
    const double amplitude = GetPulseAmplitude(timbre_);
    while (size--) {
      if (delay_ == 0) {
        double amount = RandomSample();
        amount = 1.05 + 0.5 * amount * amount;
        if (Random::GetWord() > up_probability) {
          particle_state_ *= amount;
          if (particle_state_ >= (particle_range_ + 0.25)) {
            particle_state_ = particle_range_ + 0.25;
          }
        } else if (Random::GetWord() < down_probability) {
          particle_state_ /= amount;
          if (particle_state_ <= 0.02) {
            particle_state_ = 0.02;
          }
        }
          delay_ = static_cast<uint32_t>(particle_state_ * 0.15 * Dsp::getSr());
        double gain = 1.0 - particle_range_;
        gain *= gain;
        *out = particle_state_ * amplitude * (1.0 - gain);
        
        double decay_factor = 1.0 - parameter_;
        particle_range_ *= 1.0 - decay_factor * decay_factor * 0.5;
      } else {
        --delay_;
      }
      ++out;
    }
  }
}

void Exciter::ProcessFlow(
    const uint8_t flags,
    double* out,
    size_t size) {
  double scale = parameter_ * parameter_ * parameter_ * parameter_;
  double threshold = 0.0001 + scale * 0.125;
  if (flags & EXCITER_FLAG_RISING_EDGE) {
    particle_state_ = 0.5;
  }
  while (size--) {
    double sample = RandomSample();
    if (sample < threshold) {
      particle_state_ = -particle_state_;
    }
    *out++ = particle_state_ + (sample - 0.5 - particle_state_) * scale;
  }
}

void Exciter::ProcessNoise(const uint8_t flags, double* out, size_t size) {
  while (size--) {
    *out++ = RandomSample() - 0.5;
  }
}

/* static */
Exciter::ProcessFn Exciter::fn_table_[] = {
  &Exciter::ProcessGranularSamplePlayer,
  &Exciter::ProcessSamplePlayer,
  &Exciter::ProcessMallet,
  &Exciter::ProcessPlectrum,
  &Exciter::ProcessParticles,
  &Exciter::ProcessFlow,
  &Exciter::ProcessNoise
};

}  // namespace elements
