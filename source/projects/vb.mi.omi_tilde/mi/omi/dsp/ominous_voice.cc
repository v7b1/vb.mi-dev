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
// "Ominous" is a dark 2x2-op FM synth (based on Braids' FM and FBFM modes).

#include "omi/dsp/ominous_voice.h"

#include <algorithm>
#include <cstdio>

namespace omi {

using namespace std;
using namespace stmlib;


void Spatializer::Init(double fixed_position) {
  angle_ = 0.0;
  fixed_position_ = fixed_position;
  left_ = 0.0;
  right_ = 0.0;
  distance_ = 0.0;
  behind_filter_.Init();
  behind_filter_.set_f_q<FREQUENCY_EXACT>(0.05, 1.0);
}
  
void Spatializer::Process(
    double* source,
    double* center,
    double* sides,
    size_t size) {
  behind_filter_.Process<FILTER_MODE_LOW_PASS>(source, behind_, size, 1);
 
  double angle = angle_;
  double x = distance_ * stmlib::InterpolateWrap(
      lut_sine, angle, 4096.0);
  double y = distance_ * stmlib::InterpolateWrap(
      lut_sine, angle + 0.25, 4096.0);
  double backfront = (1.0 + y) * 0.5 * distance_;
  x += fixed_position_ * (1.0 - distance_);

  double target_left = stmlib::InterpolateWrap(
      lut_sine, (1.0 + x) * 0.125, 4096.0);
  double target_right = stmlib::InterpolateWrap(
      lut_sine, (3.0 + x) * 0.125, 4096.0);

  // Prevent zipper noise during rendering.
  double step = 1.0 / static_cast<double>(size);
  double left_increment = (target_left - left_) * step;
  double right_increment = (target_right - right_) * step;

  for (size_t i = 0; i < size; ++i) {
    left_ += left_increment;
    right_ += right_increment;
    double y = source[i] + backfront * (behind_[i] - source[i]);
    double l = left_ * y;
    double r = right_ * y;
    center[i] += (l + r) * 0.5;
      sides[i] += (l - r) * 0.5 / 0.7;
  }
}


void FmOscillator::Process(
    double frequency,
    double ratio,
    double feedback_amount,
    double target_fm_amount,
    const double* external_fm,
    double* cross_fm,       // vb
    double cross_fm_amount, // vb
    double* destination,
    size_t size) {
    
    frequency += intervalCorrection_;   // vb, pitch correction
    
  ratio = Interpolate(lut_fm_frequency_quantizer, ratio, 128.0);

    
  uint32_t inc_carrier = midi_to_increment(frequency);
  uint32_t inc_mod = midi_to_increment(frequency + ratio);
  
  uint32_t phase_carrier = phase_carrier_;
  uint32_t phase_mod = phase_mod_;

  // Linear interpolation on FM amount parameter.
  double step = 1.0 / static_cast<double>(size);
  double fm_amount = fm_amount_;
  double fm_amount_increment = (target_fm_amount - fm_amount) * step;
  double previous_sample = previous_sample_;
  
  // To prevent aliasing, reduce FM amount when frequency or feedback are
  // too high.
    // TODO: prevent aliasing - check this
  double brightness = frequency + ratio * 0.75 - 60.0 + \
      feedback_amount * 24.0;
  double amount_attenuation = brightness <= 0.0
      ? 1.0
      : 1.0 - brightness * brightness * 0.0015;
  if (amount_attenuation < 0.0) {
    amount_attenuation = 0.0; 
  }
  
  for (size_t i = 0; i < size; ++i) {
    fm_amount += fm_amount_increment;
    phase_carrier += inc_carrier;
    phase_mod += inc_mod;
    double mod = SineFm(phase_mod, feedback_amount * previous_sample);
      // vb: add lowpass to prevent ringing at sf/2
      mod = lp_filter[0].Process<FILTER_MODE_LOW_PASS>(mod);
      // vb: add cross fb
    destination[i] = previous_sample = SineFm(
        phase_carrier,
        amount_attenuation * (mod * fm_amount + external_fm[i] + cross_fm_amount * cross_fm[i]));
      cross_fm[i] = previous_sample = lp_filter[1].Process<FILTER_MODE_LOW_PASS>(previous_sample);
  }
  
  phase_carrier_ = phase_carrier;
  phase_mod_ = phase_mod;
  fm_amount_ = fm_amount;
  previous_sample_ = previous_sample;
}


void OminousVoice::Init(double srFactor) {
    srFactor_ = srFactor;  // vb
  envelope_.Init();
  envelope_.set_adsr(0.5, 0.5, 0.5, 0.5);
  previous_gate_ = false;
  level_state_ = 0.0;
  
  for (size_t i = 0; i < kNumOscillators; ++i) {
    external_fm_state_[i] = 0.0;
    oscillator_[i].Init(srFactor);
    
    osc_level_[i] = 0.0;
    filter_[i].Init();
    
    spatializer_[i].Init(i == 0 ? - 0.7 : 0.7);
  }
    
    // vb init additions
    fill(&cross_fm_[0], &cross_fm_[kMaxBlockSize], 0.0);
    damping_ = 0.0;
    feedback_ = 0.0;
}

void OminousVoice::ConfigureEnvelope(const Patch& patch) {
  if (patch.exciter_envelope_shape < 0.4) {
    double a = 0.0;
    double dr = (patch.exciter_envelope_shape * 0.625 + 0.2) * 1.8;
      envelope_.set_adsr(a, dr, 0.0, dr);     // vb: why adsr?
  } else if (patch.exciter_envelope_shape < 0.6) {
    double s = (patch.exciter_envelope_shape - 0.4) * 5.0;
    envelope_.set_adsr(0.0, 0.80, s, 0.80);
  } else {
    double a = 0.0;
    double dr = ((1.0 - patch.exciter_envelope_shape) * 0.75 + 0.15) * 1.8;
    envelope_.set_adsr(a, dr, 1.0, dr);
  }
}

void OminousVoice::Process(
    const Patch& patch,
    double frequency,
    double strength,
    const bool gate_in,
    const double* audio_in,
    double* raw,
    double* center,
    double* sides,
    size_t size) {
    
    uint8_t flags = GetGateFlags(gate_in);
    
    // Compute the envelope.
    ConfigureEnvelope(patch);
    double level = envelope_.Process(flags);
    level += strength;
    double level_increment = (level - level_state_) / size;

    damping_ += 0.1 * (patch.resonator_damping - damping_);
    double filter_env_amount = damping_ <= 0.9 ? 1.1 * damping_ : 0.99;
    double vca_env_amount = 1.0 + \
      damping_ * damping_ * damping_ * damping_ * 0.5;
  
    // Comfigure the filter.
    double cutoff_midi = 12.0;
    cutoff_midi += patch.resonator_brightness * 140.0;
    cutoff_midi += filter_env_amount * level * 120.0;
    cutoff_midi += 0.5 * (frequency - 64.0);

    double cutoff = midi_to_frequency(cutoff_midi);
    double q_bump = patch.resonator_geometry - 0.6;
    double q = 1.72 - q_bump * q_bump * 2.0;
    q += patch.resonance;   // vb
    double cutoff_2 = cutoff * (1.0 + patch.resonator_modulation_offset);

    //printf("cutoff1: %f -- cutoff2: %f\n", cutoff, cutoff_2);
    
    filter_[0].set_f_q<FREQUENCY_FAST>(cutoff, q);
    filter_[1].set_f_q<FREQUENCY_FAST>(cutoff_2, q * 1.25);

    // Process each oscillator.
    fill(&center[0], &center[size], 0.0);
    fill(&sides[0], &sides[size], 0.0);
    fill(&raw[0], &raw[size], 0.0);
  
    const double rotation_speed[2] = { 1.0, 1.123456 };
    feedback_ += 0.01 * (patch.exciter_bow_timbre - feedback_);

    for (size_t i = 0; i < 2; ++i) {

        double detune, ratio, amount, level;
        if (i == 0) {
          detune = 0.0;
          ratio = patch.exciter_blow_meta;
          amount = patch.exciter_blow_timbre;
          level = patch.exciter_blow_level;
        } else {
          detune = Interpolate(
              lut_detune_quantizer, patch.exciter_bow_level, 64.0);
          ratio = patch.exciter_strike_meta;
          amount = patch.exciter_strike_timbre;
          level = patch.exciter_strike_level;
        }

          // vb: skip oversampling to make it cheaper...
          // (filter feedback path instead)
          oscillator_[i].Process(
                                 frequency + detune,
                                 ratio,
                                 feedback_ * (0.25 + 0.15 * patch.exciter_signature),
                                 (2.0 - patch.exciter_signature * feedback_) * amount,
                                 audio_in,
                                 cross_fm_,
                                 patch.cross_fb,
                                 osc_,
                                 size);
        
        // Copy to raw buffer.
        double level_state = osc_level_[i];
        for (size_t j = 0; j < size; ++j) {
          level_state += 0.01 * (level - level_state);
          osc_[j] *= level_state;
          raw[j] += osc_[j];
        }
        osc_level_[i] = level_state;

        // Apply filter.
        filter_[i].ProcessMultimode(osc_, osc_, size, patch.resonator_geometry);

        // Apply VCA.
        double l = level_state_;
        for (size_t j = 0; j < size; ++j) {
          double gain = l * vca_env_amount;
          if (gain >= 1.0) gain = 1.0;
          osc_[j] *= gain;
          l += level_increment;
        }

        // Spatialize.
        double f = patch.resonator_position * patch.resonator_position * 0.001;
        double distance = patch.resonator_position;

        spatializer_[i].Rotate(f * rotation_speed[i]);
        spatializer_[i].set_distance(distance * (2.0 - distance));
        spatializer_[i].Process(osc_, center, sides, size);
    }
  
  level_state_ = level;
}

}  // namespace omi
