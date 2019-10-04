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
// Modal synthesis voice.

#include "elements/dsp/voice.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include <algorithm>

namespace elements {

using namespace std;
using namespace stmlib;

void Voice::Init() {
  envelope_.Init();
  bow_.Init();
  blow_.Init();
  strike_.Init();
  diffuser_.Init(diffuser_buffer_);
  
  ResetResonator();

  bow_.set_model(EXCITER_MODEL_FLOW);
  bow_.set_parameter(0.7);
  bow_.set_timbre(0.5);
  
  blow_.set_model(EXCITER_MODEL_GRANULAR_SAMPLE_PLAYER);
  
  envelope_.set_adsr(0.5, 0.5, 0.5, 0.5);

  previous_gate_ = false;
  strength_ = 0.0;
  exciter_level_ = 0.0;
  envelope_value_ = 0.0;
  chord_index_ = 0.0;
  
  resonator_model_ = RESONATOR_MODEL_MODAL;
    
    // vb -- why no call to tube.init ?? TODO: check tube.init
    //tube_.Init();
}

void Voice::ResetResonator() {
  resonator_.Init();
  for (size_t i = 0; i < kNumStrings; ++i) {
    string_[i].Init(true);
  }
  dc_blocker_.Init(1.0 - 10.0 / Dsp::getSr());
  resonator_.set_resolution(52);  // Runs with 56 extremely tightly.
}

double chords[11][5] = {
    { 0.0, -12.0, 0.0, 0.01, 12.0 },
    { 0.0, -12.0, 3.0, 7.0,  10.0 },
    { 0.0, -12.0, 3.0, 7.0,  12.0 },
    { 0.0, -12.0, 3.0, 7.0,  14.0 },
    { 0.0, -12.0, 3.0, 7.0,  17.0 },
    { 0.0, -12.0, 7.0, 12.0, 19.0 },
    { 0.0, -12.0, 4.0, 7.0,  17.0 },
    { 0.0, -12.0, 4.0, 7.0,  14.0 },
    { 0.0, -12.0, 4.0, 7.0,  12.0 },
    { 0.0, -12.0, 4.0, 7.0,  11.0 },
    { 0.0, -12.0, 5.0, 7.0,  12.0 },
};


void Voice::Process(
    const Patch& patch,
    double frequency,
    double strength,
    const bool gate_in,
    const double* blow_in,
    const double* strike_in,
    double* raw,
    double* center,
    double* sides,
    size_t size) {
  uint8_t flags = GetGateFlags(gate_in);

  // Compute the envelope.
  double envelope_gain = 1.0;
  if (patch.exciter_envelope_shape < 0.4) {
    double a = patch.exciter_envelope_shape * 0.75 + 0.15;
    double dr = a * 1.8;
    envelope_.set_adsr(a, dr, 0.0, dr);
    envelope_gain = 5.0 - patch.exciter_envelope_shape * 10.0;
  } else if (patch.exciter_envelope_shape < 0.6) {
    double s = (patch.exciter_envelope_shape - 0.4) * 5.0;
    envelope_.set_adsr(0.45, 0.81, s, 0.81);
  } else {
    double a = (1.0 - patch.exciter_envelope_shape) * 0.75 + 0.15;
    double dr = a * 1.8;
    envelope_.set_adsr(a, dr, 1.0, dr);
  }
    
  double envelope_value = envelope_.Process(flags) * envelope_gain;
  double envelope_increment = (envelope_value - envelope_value_) / size;
  
  // Configure and evaluate exciters.
  double brightness_factor = 0.4 + 0.6 * patch.resonator_brightness;
  bow_.set_timbre(patch.exciter_bow_timbre * brightness_factor);

  blow_.set_parameter(patch.exciter_blow_meta);
  blow_.set_timbre(patch.exciter_blow_timbre);
  blow_.set_signature(patch.exciter_signature);
  
  double strike_meta = patch.exciter_strike_meta;
  strike_.set_meta(
      strike_meta <= 0.4 ? strike_meta * 0.625 : strike_meta * 1.25 - 0.25,
      EXCITER_MODEL_SAMPLE_PLAYER,
      EXCITER_MODEL_PARTICLES);
  strike_.set_timbre(patch.exciter_strike_timbre);
  strike_.set_signature(patch.exciter_signature);

  bow_.Process(flags, bow_buffer_, size);
  
  double blow_level, tube_level;
  blow_level = patch.exciter_blow_level * 1.5;
  tube_level = blow_level > 1.0 ? (blow_level - 1.0) * 2.0 : 0.0;
  blow_level = blow_level < 1.0 ? blow_level * 0.4 : 0.4;
  blow_.Process(flags, blow_buffer_, size);
  tube_.Process(
      frequency,
      envelope_value,
      patch.resonator_damping,
      tube_level,
      blow_buffer_,
      tube_level * 0.5,
      size);
  
  for (size_t i = 0; i < size; ++i) {
    blow_buffer_[i] = blow_buffer_[i] * blow_level + blow_in[i];
  }
  diffuser_.Process(blow_buffer_, size);
  strike_.Process(flags, strike_buffer_, size);
  
  // The Strike exciter is implemented in such a way that raising the level
  // beyond a certain point doesn't change the exciter amplitude, but instead,
  // increasingly mixes the raw exciter signal into the resonator output.
  double strike_level, strike_bleed;
  strike_level = patch.exciter_strike_level * 1.25;
  strike_bleed = strike_level > 1.0 ? (strike_level - 1.0) * 2.0 : 0.0;
  strike_level = strike_level < 1.0 ? strike_level : 1.0;
  strike_level *= 1.5;
  
  // The strength parameter is very sensitive to zipper noise.
  strength *= 256.0;
  double strength_increment = (strength - strength_) / size;
  
  // Sum all sources of excitation.
  for (size_t i = 0; i < size; ++i) {
    strength_ += strength_increment;
    envelope_value_ += envelope_increment;
    double input_sample = 0.0;
    double e = envelope_value_;
    double strength_lut = strength_;
    MAKE_INTEGRAL_FRACTIONAL(strength_lut);
    double accent = lut_accent_gain_coarse[strength_lut_integral] *
       lut_accent_gain_fine[
           static_cast<int32_t>(256.0 * strength_lut_fractional)];
    bow_strength_buffer_[i] = e * patch.exciter_bow_level;

    strike_buffer_[i] *= accent;
    e *= accent;

    input_sample += bow_buffer_[i] * bow_strength_buffer_[i] * 0.125 * accent;
    input_sample += blow_buffer_[i] * e;
    input_sample += strike_buffer_[i] * strike_level;
    input_sample += strike_in[i];
    raw[i] = input_sample * 0.5;
  }
  /*
  // Update meter for exciter.
  for (size_t i = 0; i < size; ++i) {
    double error = raw[i] * raw[i] - exciter_level_;
    exciter_level_ += error * (error > 0.0 ? 0.5 : 0.001);
  }*/
  
  // Some exciters can cause palm mutes on release.
  double damping = patch.resonator_damping;
  damping -= strike_.damping() * strike_level * 0.125;
  damping -= (1.0 - bow_strength_buffer_[0]) * \
      patch.exciter_bow_level * 0.0625;
  
  if (damping <= 0.0) {
    damping = 0.0;
  }

  // Configure resonator.
  if (resonator_model_ == RESONATOR_MODEL_MODAL) {
    resonator_.set_frequency(frequency);
    resonator_.set_geometry(patch.resonator_geometry);
    resonator_.set_brightness(patch.resonator_brightness);
    resonator_.set_position(patch.resonator_position);
    resonator_.set_damping(damping);
    resonator_.set_modulation_frequency(patch.resonator_modulation_frequency);
    resonator_.set_modulation_offset(patch.resonator_modulation_offset);

    // Process through resonator.
    resonator_.Process(bow_strength_buffer_, raw, center, sides, size);
  } else {
    size_t num_notes = resonator_model_ == RESONATOR_MODEL_STRING
        ? 1
        : kNumStrings;
    
    double normalization = 1.0 / static_cast<double>(num_notes);
    dc_blocker_.Process(raw, size);
    for (size_t i = 0; i < size; ++i) {
      raw[i] *= normalization;
    }
    
    double chord = patch.resonator_geometry * 10.0;
    double hysteresis = chord > chord_index_ ? -0.1 : 0.1;
    int chord_index = static_cast<int>(chord + hysteresis + 0.5);
    CONSTRAIN(chord_index, 0, 10);
    chord_index_ = static_cast<double>(chord_index);

    fill(&center[0], &center[size], 0.0);
    fill(&sides[0], &sides[size], 0.0);
    for (size_t i = 0; i < num_notes; ++i) {
      double transpose = chords[chord_index][i];
      string_[i].set_frequency(frequency * SemitonesToRatio(transpose));
      string_[i].set_brightness(patch.resonator_brightness);
      string_[i].set_position(patch.resonator_position);
      string_[i].set_damping(damping);
      if (num_notes == 1) {
        string_[i].set_dispersion(patch.resonator_geometry);
      } else {
        double b = patch.resonator_brightness;
        string_[i].set_dispersion(b < 0.5 ? 0.0 : (b - 0.5) * -0.4);
      }
      string_[i].Process(raw, center, sides, size);
    }
    for (size_t i = 0; i < size; ++i) {
      double left = center[i];
      double right = sides[i];
      center[i] = left - right;
      sides[i] = left + right;
    }
  }

  // This is where the raw mallet signal bleeds through the exciter output.
  for (size_t i = 0; i < size; ++i) {
    center[i] += strike_bleed * strike_buffer_[i];
  }
}

}  // namespace elements
