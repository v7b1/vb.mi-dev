// Copyright 2021 Emilie Gillet.
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
// DX7 voice.

#ifndef PLAITS_DSP_FM_VOICE_H_
#define PLAITS_DSP_FM_VOICE_H_

#include "stmlib/stmlib.h"

#include "plaits/dsp/fm/algorithms.h"
#include "plaits/dsp/fm/dx_units.h"
#include "plaits/dsp/fm/envelope.h"
#include "plaits/dsp/fm/patch.h"

// When enabled, the amplitude modulation LFO linearly modulates the amplitude
// of an operator. Otherwise, a more complex formula involving an exponential
// is used, to match Dexed's output.
// #define FAST_OP_LEVEL_MODULATION

namespace plaits {

namespace fm {

template<int num_operators>
class Voice {
 public:
  Voice() { }
  ~Voice() { }

  struct Parameters {
    bool sustain;
    bool gate;
    double note;
    double velocity;
    double brightness;
    double envelope_control;
    double pitch_mod;
    double amp_mod;
  };

  inline void Init(
      const Algorithms<num_operators>* algorithms,
      double sample_rate) {
    algorithms_ = algorithms;

    sample_rate_ = sample_rate;
    one_hz_ = 1.0 / sample_rate;
    a0_ = 55.0 / sample_rate;

    const double native_sr = 44100.0;  // Legacy sample rate.
    const double envelope_scale = native_sr * one_hz_;

    for (int i = 0; i < num_operators; ++i) {
      operator_[i].Reset();
      operator_envelope_[i].Init(envelope_scale);
    }
    pitch_envelope_.Init(envelope_scale);

    feedback_state_[0] = feedback_state_[1] = 0.0;

    patch_ = NULL;
    gate_ = false;
    note_ = 48.0;
    normalized_velocity_ = 10.0;

    dirty_ = true;
  }

  inline void SetPatch(const Patch* patch) {
    patch_ = patch;
    dirty_ = true;
  }

  // Pre-compute everything that can be pre-computed once a patch is loaded:
  // - envelope constants
  // - frequency ratios
  inline bool Setup() {
    if (!dirty_) {
      return false;
    }

    pitch_envelope_.Set(
        patch_->pitch_envelope.rate,
        patch_->pitch_envelope.level);
    for (int i = 0; i < num_operators; ++i) {
      const Patch::Operator& op = patch_->op[i];

      int level = OperatorLevel(op.level);
      operator_envelope_[i].Set(op.envelope.rate, op.envelope.level, level);

      // The level increase caused by keyboard scaling plus velocity
      // scaling should not exceed this number - otherwise it would be
      // equivalent to have an operator with a level above 99.
      level_headroom_[i] = double(127 - level);

      // Pre-compute frequency ratios. Encode the base frequency
      // (1Hz or the root note) as the sign of the ratio.
      double sign = op.mode == 0 ? 1.0 : -1.0;
      ratios_[i] = sign * FrequencyRatio(op);
    }
    dirty_ = false;
    return true;
  }

  inline double op_level(int i) const {
    return level_[i];
  }

  inline void Render(
      const Parameters& parameters,
      double* temp,
      double* out,
      double* aux,
      size_t size) {
    double* buffers[4] = { out, aux, temp, temp };
    Render(parameters, buffers, size);
  }

  inline void Render(
      const Parameters& parameters,
      double* temp,
      size_t size) {
    double* buffers[4] = { temp, temp + size, temp + 2 * size, temp + 2 * size };
    Render(parameters, buffers, size);
  }

  inline void Render(
      const Parameters& parameters,
      double* buffers[4],
      size_t size) {
    if (Setup()) {
      // This prevents a CPU overrun, since there is not enough CPU to perform
      // both a patch setup and a full render in the time alloted for
      // a render. As a drawback, this causes a 0.5ms blank before a new
      // patch starts playing. But this is a clean blank, as opposed to a
      // glitchy overrun.
      return;
    }

    const double envelope_rate = double(size);
    const double ad_scale = Pow2Fast<1>(
        (0.5 - parameters.envelope_control) * 8.0);
    const double r_scale = Pow2Fast<1>(
        -std::abs(parameters.envelope_control - 0.3) * 8.0);
    const double gate_duration = 1.5 * sample_rate_;
    const double envelope_sample = gate_duration * parameters.envelope_control;

    // Apply LFO and pitch envelope modulations.
    const double pitch_envelope = parameters.sustain
        ? pitch_envelope_.RenderAtSample(envelope_sample, gate_duration)
        : pitch_envelope_.Render(
              parameters.gate,
              envelope_rate,
              ad_scale,
              r_scale);
    const double pitch_mod = pitch_envelope + parameters.pitch_mod;
    const double f0 = a0_ * 0.25 * stmlib::SemitonesToRatioSafe(
        parameters.note - 9.0 + pitch_mod * 12.0);

    // Sample the note and velocity (used for scaling) only when a trigger
    // is received, or constantly when we are in free-running mode.
    const bool note_on = parameters.gate && !gate_;
    gate_ = parameters.gate;
    if (note_on || parameters.sustain) {
      normalized_velocity_ = NormalizeVelocity(parameters.velocity);
      note_ = parameters.note;
    }

    // Reset operator phase if a note on is detected & if the patch requires it.
    if (note_on && patch_->reset_phase) {
      for (int i = 0; i < num_operators; ++i) {
        operator_[i].phase = 0;
      }
    }

    // Compute frequencies and amplitudes.
    double f[num_operators];
    double a[num_operators];
    for (int i = 0; i < num_operators; ++i) {
      const Patch::Operator& op = patch_->op[i];

      f[i] = ratios_[i] * (ratios_[i] < 0.0 ? -one_hz_ : f0);

      const double rate_scaling = RateScaling(note_, op.rate_scaling);
      double level = parameters.sustain
          ? operator_envelope_[i].RenderAtSample(envelope_sample, gate_duration)
          : operator_envelope_[i].Render(
                parameters.gate,
                envelope_rate * rate_scaling,
                ad_scale,
                r_scale);
      const double kb_scaling = KeyboardScaling(note_, op.keyboard_scaling);
      const double velocity_scaling = normalized_velocity_ * \
          double(op.velocity_sensitivity);
      const double brightness = algorithms_->is_modulator(patch_->algorithm, i)
          ? (parameters.brightness - 0.5) * 32.0
          : 0.0;

      level += 0.125 * std::min(
          kb_scaling + velocity_scaling + brightness,
          level_headroom_[i]);

      level_[i] = level;

      const double sensitivity = AmpModSensitivity(op.amp_mod_sensitivity);
#ifdef FAST_OP_LEVEL_MODULATION
      const double level_mod = 1.0 - sensitivity * parameters.amp_mod;
      a[i] = Pow2Fast<2>(-14.0 + level) * level_mod;
#else
      const double log_level_mod = sensitivity * parameters.amp_mod - 1.0;
      const double level_mod = 1.0 - Pow2Fast<2>(6.4 * log_level_mod);
      a[i] = Pow2Fast<2>(-14.0 + level * level_mod);
#endif  // FAST_LINEAR_AMPLITUDE_MODULATION
    }

    for (int i = 0; i < num_operators; ) {
      const typename Algorithms<num_operators>::RenderCall& call = \
          algorithms_->render_call(patch_->algorithm, i);
      (*call.render_fn)(
          &operator_[i],
          &f[i],
          &a[i],
          feedback_state_,
          patch_->feedback,
          buffers[call.input_index],
          buffers[call.output_index],
          size);
      i += call.n;
    }
  }

 private:
  const Algorithms<num_operators>* algorithms_;
  double sample_rate_;
  double one_hz_;
  double a0_;

  bool gate_;

  Operator operator_[num_operators];
  OperatorEnvelope operator_envelope_[num_operators];
  PitchEnvelope pitch_envelope_;

  double normalized_velocity_;
  double note_;

  double ratios_[num_operators];
  double level_headroom_[num_operators];
  double level_[num_operators];

  double feedback_state_[2];

  const Patch* patch_;

  bool dirty_;

  DISALLOW_COPY_AND_ASSIGN(Voice);
};

}  // namespace fm

}  // namespace plaits

#endif  // PLAITS_DSP_FM_VOICE_H_
