// Copyright 2015 Olivier Gillet.
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
// String synth part.

#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/dsp.h"


namespace rings {

using namespace std;
using namespace stmlib;
    
void StringSynthPart::Init(uint16_t* reverb_buffer) {
  active_group_ = 0;
  acquisition_delay_ = 0;

  polyphony_ = 1;
  fx_type_ = FX_ENSEMBLE;

  for (int32_t i = 0; i < kStringSynthVoices; ++i) {
    voice_[i].Init();
  }
  
  for (int32_t i = 0; i < kMaxStringSynthPolyphony; ++i) {
    group_[i].tonic = 0.0;
    group_[i].envelope.Init();
  }
  
  for (int32_t i = 0; i < kNumFormants; ++i) {
    formant_filter_[i].Init();
  }
  
  limiter_.Init();
  
  reverb_.Init(reverb_buffer);
  chorus_.Init(reverb_buffer);
  ensemble_.Init(reverb_buffer);
    
  note_filter_.Init(
      Dsp::getSr() / Dsp::getBlockSize(),
      0.001,  // Lag time with a sharp edge on the V/Oct input or trigger.
      0.005,  // Lag time after the trigger has been received.
      0.050,  // Time to transition from reactive to filtered.
      0.004); // Prevent a sharp edge to partly leak on the previous voice.
}

const int32_t kRegistrationTableSize = 11;
const double registrations[kRegistrationTableSize][kNumHarmonics * 2] = {
  { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
  { 1.0, 1.0, 0.0, 0.0, 0.0, 0.0 },
  { 1.0, 0.0, 1.0, 0.0, 0.0, 0.0 },
  { 1.0, 0.1, 0.0, 0.0, 1.0, 0.0 },
  { 1.0, 0.5, 1.0, 0.0, 1.0, 0.0 },
  { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 },
  { 0.0, 1.0, 1.0, 1.0, 1.0, 0.0 },
  { 0.0, 0.5, 1.0, 0.0, 1.0, 0.0 },
  { 0.0, 0.0, 1.0, 0.0, 1.0, 0.0 },
  { 0.0, 0.0, 0.0, 0.0, 1.0, 0.0 },
  { 0.0, 0.0, 0.0, 0.0, 0.0, 1.0 },
};

void StringSynthPart::ComputeRegistration(
    double gain,
    double registration,
    double* amplitudes) {
  registration *= (kRegistrationTableSize - 1.001);
  MAKE_INTEGRAL_FRACTIONAL(registration);
  double total = 0.0;
  for (int32_t i = 0; i < kNumHarmonics * 2; ++i) {
    double a = registrations[registration_integral][i];
    double b = registrations[registration_integral + 1][i];
    amplitudes[i] = a + (b - a) * registration_fractional;
    total += amplitudes[i];
  }
  for (int32_t i = 0; i < kNumHarmonics * 2; ++i) {
    amplitudes[i] = gain * amplitudes[i] / total;
  }
}

#ifdef BRYAN_CHORDS

// Chord table by Bryan Noll:
// - more compact, leaving room for a bass
// - more frequent note changes between adjacent chords.
// - dropped fifth.
const double chords[kMaxStringSynthPolyphony][kNumChords][kMaxChordSize] = {
  {
    { -12.0, -0.01,  0.0,  0.01,  0.02, 11.99, 12.0, 24.0 }, // OCT
    { -12.0, -5.01, -5.0,  0.0,   7.0,  12.0,  19.0, 24.0 }, // 5
    { -12.0, -5.0,   0.0,  5.0,   7.0,  12.0,  17.0, 24.0 }, // sus4
    { -12.0, -5.0,   0.0,  0.01,  3.0,  12.0,  19.0, 24.0 }, // m
    { -12.0, -5.01, -5.0,  0.0,   3.0,  10.0,  19.0, 24.0 }, // m7
    { -12.0, -5.0,   0.0,  3.0,  10.0,  14.0,  19.0, 24.0 }, // m9
    { -12.0, -5.01, -5.0,  0.0,   3.0,  10.0,  17.0, 24.0 }, // m11
    { -12.0, -5.0,   0.0,  2.0,   9.0,  16.0,  19.0, 24.0 }, // 69
    { -12.0, -5.0,   0.0,  4.0,  11.0,  14.0,  19.0, 24.0 }, // M9
    { -12.0, -5.0,   0.0,  4.0,   7.0,  11.0,  19.0, 24.0 }, // M7
    { -12.0, -5.0,   0.0,  4.0,   7.0,  12.0,  19.0, 24.0 }, // M
  },
  {
    { -12.0, -0.01,  0.0,  0.01, 12.0,  12.01 }, // OCT
    { -12.0, -5.01, -5.0,  0.0,   7.0,  12.0  }, // 5
    { -12.0, -5.0,   0.0,  5.0,   7.0,  12.0  }, // sus4
    { -12.0, -5.0,   0.0,  0.01,  3.0,  12.0  }, // m
    { -12.0, -5.01, -5.0,  0.0,   3.0,  10.0  }, // m7
    { -12.0, -5.0,   0.0,  3.0,  10.0,  14.0  }, // m9
    { -12.0, -5.0,   0.0,  3.0,  10.0,  17.0  }, // m11
    { -12.0, -5.0,   0.0,  2.0,   9.0,  16.0  }, // 69
    { -12.0, -5.0,   0.0,  4.0,  11.0,  14.0  }, // M9
    { -12.0, -5.0,   0.0,  4.0,   7.0,  11.0  }, // M7
    { -12.0, -5.0,   0.0,  4.0,   7.0,  12.0  }, // M
  },
  {
    { -12.0, 0.0,  0.01, 12.0 }, // OCT
    { -12.0, 6.99, 7.0,  12.0 }, // 5
    { -12.0, 5.0,  7.0,  12.0 }, // sus4
    { -12.0, 3.0, 11.99, 12.0 }, // m
    { -12.0, 3.0,  9.99, 10.0 }, // m7
    { -12.0, 3.0, 10.0,  14.0 }, // m9
    { -12.0, 3.0, 10.0,  17.0 }, // m11
    { -12.0, 2.0,  9.0,  16.0 }, // 69
    { -12.0, 4.0, 11.0,  14.0 }, // M9
    { -12.0, 4.0,  7.0,  11.0 }, // M7
    { -12.0, 4.0,  7.0,  12.0 }, // M
  },
  {
    { 0.0,  0.01, 12.0 }, // OCT
    { 0.0,  7.0,  12.0 }, // 5
    { 5.0,  7.0,  12.0 }, // sus4
    { 0.0,  3.0,  12.0 }, // m
    { 0.0,  3.0,  10.0 }, // m7
    { 3.0, 10.0,  14.0 }, // m9
    { 3.0, 10.0,  17.0 }, // m11
    { 2.0,  9.0,  16.0 }, // 69
    { 4.0, 11.0,  14.0 }, // M9
    { 4.0,  7.0,  11.0 }, // M7
    { 4.0,  7.0,  12.0 }, // M
  }
};

#else

// Original chord table:
// - wider, occupies more room in the spectrum.
// - minimum number of note changes between adjacent chords.
// - consistant with the chord table used for the sympathetic strings model.
const double chords[kMaxStringSynthPolyphony][kNumChords][kMaxChordSize] = {
  {
    { -24.0, -12.0, 0.0, 0.01, 0.02, 11.99, 12.0, 24.0 },
    { -24.0, -12.0, 0.0, 3.0,  7.0,  10.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 3.0,  7.0,  12.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 3.0,  7.0,  14.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 3.0,  7.0,  17.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 6.99, 7.0,  18.99, 19.0, 24.0 },
    { -24.0, -12.0, 0.0, 4.0,  7.0,  17.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 4.0,  7.0,  14.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 4.0,  7.0,  12.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 4.0,  7.0,  11.0,  19.0, 24.0 },
    { -24.0, -12.0, 0.0, 5.0,  7.0,  12.0,  17.0, 24.0 },
  },
  {
    { -24.0, -12.0, 0.0, 0.01, 12.0, 12.01 },
    { -24.0, -12.0, 0.0, 3.00, 7.0,  10.0 },
    { -24.0, -12.0, 0.0, 3.00, 7.0,  12.0 },
    { -24.0, -12.0, 0.0, 3.00, 7.0,  14.0 },
    { -24.0, -12.0, 0.0, 3.00, 7.0,  17.0 },
    { -24.0, -12.0, 0.0, 6.99, 12.0, 19.0 },
    { -24.0, -12.0, 0.0, 4.00, 7.0,  17.0 },
    { -24.0, -12.0, 0.0, 4.00, 7.0,  14.0 },
    { -24.0, -12.0, 0.0, 4.00, 7.0,  12.0 },
    { -24.0, -12.0, 0.0, 4.00, 7.0,  11.0 },
    { -24.0, -12.0, 0.0, 5.00, 7.0, 12.0 },
  },
  {
    { -12.0, 0.0, 0.01, 12.0 },
    { -12.0, 3.0, 7.0,  10.0 },
    { -12.0, 3.0, 7.0,  12.0 },
    { -12.0, 3.0, 7.0,  14.0 },
    { -12.0, 3.0, 7.0,  17.0 },
    { -12.0, 7.0, 12.0, 19.0 },
    { -12.0, 4.0, 7.0,  17.0 },
    { -12.0, 4.0, 7.0,  14.0 },
    { -12.0, 4.0, 7.0,  12.0 },
    { -12.0, 4.0, 7.0,  11.0 },
    { -12.0, 5.0, 7.0, 12.0 },
  },
  {
    { 0.0, 0.01, 12.0 },
    { 0.0, 3.0,  10.0 },
    { 0.0, 3.0,  7.0 },
    { 0.0, 3.0,  14.0 },
    { 0.0, 3.0,  17.0 },
    { 0.0, 7.0,  19.0 },
    { 0.0, 4.0,  17.0 },
    { 0.0, 4.0,  14.0 },
    { 0.0, 4.0,  7.0 },
    { 0.0, 4.0,  11.0 },
    { 0.0, 5.0,  7.0 },
  }
};

#endif  // BRYAN_CHORDS

void StringSynthPart::ProcessEnvelopes(
    double shape,
    uint8_t* flags,
    double* values) {
  double decay = shape;
  double attack = 0.0;
  if (shape < 0.5) {
    attack = 0.0;
  } else {
    attack = (shape - 0.5) * 2.0;
  }
  
  // Convert the arbitrary values to actual units.
    double period = Dsp::getSr() / Dsp::getBlockSize();
    //double period = Dsp::getSr() / kMaxBlockSize;
  double attack_time = SemitonesToRatio(attack * 96.0) * 0.005 * period;
  // double decay_time = SemitonesToRatio(decay * 96.0) * 0.125f * period;
  double decay_time = SemitonesToRatio(decay * 84.0) * 0.180 * period;
  double attack_rate = 1.0 / attack_time;
  double decay_rate = 1.0 / decay_time;
  
  for (int32_t i = 0; i < polyphony_; ++i) {
    double drone = shape < 0.98 ? 0.0 : (shape - 0.98) * 55.0;
    if (drone >= 1.0) drone = 1.0;

    group_[i].envelope.set_ad(attack_rate, decay_rate);
    double value = group_[i].envelope.Process(flags[i]);
    values[i] = value + (1.0 - value) * drone;
  }
}

const int32_t kFormantTableSize = 5;
const double formants[kFormantTableSize][kNumFormants] = {
  { 700, 1100, 2400 },
  { 500, 1300, 1700 },
  { 400, 2000, 2500 },
  { 600, 800, 2400 },
  { 300, 900, 2200 },
};

void StringSynthPart::ProcessFormantFilter(
    double vowel,
    double shift,
    double resonance,
    double* out,
    double* aux,
    size_t size) {
  for (size_t i = 0; i < size; ++i) {
    filter_in_buffer_[i] = out[i] + aux[i];
  }
  fill(&out[0], &out[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);

  vowel *= (kFormantTableSize - 1.001);
  MAKE_INTEGRAL_FRACTIONAL(vowel);
  
  for (int32_t i = 0; i < kNumFormants; ++i) {
    double a = formants[vowel_integral][i];
    double b = formants[vowel_integral + 1][i];
    double f = a + (b - a) * vowel_fractional;
    f *= shift;
      formant_filter_[i].set_f_q<FREQUENCY_DIRTY>(f / Dsp::getSr(), resonance);
    formant_filter_[i].Process<FILTER_MODE_BAND_PASS>(
        filter_in_buffer_,
        filter_out_buffer_,
        size);
    const double pan = i * 0.3 + 0.2;
    for (size_t j = 0; j < size; ++j) {
      out[j] += filter_out_buffer_[j] * pan * 0.5;
      aux[j] += filter_out_buffer_[j] * (1.0 - pan) * 0.5;
    }
  }
}

struct ChordNote {
  double note;
  double amplitude;
};

void StringSynthPart::Process(
    const PerformanceState& performance_state,
    const Patch& patch,
    const double* in,
    double* out,
    double* aux,
    size_t size) {
  // Assign note to a voice.
  uint8_t envelope_flags[kMaxStringSynthPolyphony];
  
  fill(&envelope_flags[0], &envelope_flags[polyphony_], 0);
  note_filter_.Process(performance_state.note, performance_state.strum);
  if (performance_state.strum) {
    group_[active_group_].tonic = note_filter_.stable_note();
    envelope_flags[active_group_] = ENVELOPE_FLAG_FALLING_EDGE;
    active_group_ = (active_group_ + 1) % polyphony_;
    envelope_flags[active_group_] = ENVELOPE_FLAG_RISING_EDGE;
    acquisition_delay_ = 3;
  }
  if (acquisition_delay_) {
    --acquisition_delay_;
  } else {
    group_[active_group_].tonic = note_filter_.note();
    group_[active_group_].chord = performance_state.chord;
    group_[active_group_].structure = patch.structure;
    envelope_flags[active_group_] |= ENVELOPE_FLAG_GATE;
  }

  // Process envelopes.
  double envelope_values[kMaxStringSynthPolyphony];
  ProcessEnvelopes(patch.damping, envelope_flags, envelope_values);
  
  copy(&in[0], &in[size], &aux[0]);
  copy(&in[0], &in[size], &out[0]);
  int32_t chord_size = min(kStringSynthVoices / polyphony_, kMaxChordSize);
  for (int32_t group = 0; group < polyphony_; ++group) {
    ChordNote notes[kMaxChordSize];
    double harmonics[kNumHarmonics * 2];
    
    ComputeRegistration(
        envelope_values[group] * 0.25,
        patch.brightness,
        harmonics);
    
    // Note enough polyphony for smooth transition between chords.
    for (int32_t i = 0; i < chord_size; ++i) {
        // TODO: 'chord' enthaelt muell!!!
      double n = chords[polyphony_ - 1][group_[group].chord][i];
      notes[i].note = n;
      notes[i].amplitude = n >= 0.0 && n <= 17.0 ? 1.0 : 0.7;
    }

    for (int32_t chord_note = 0; chord_note < chord_size; ++chord_note) {
      double note = 0.0;
      note += group_[group].tonic;
      note += performance_state.tonic;
      note += performance_state.fm;
      note += notes[chord_note].note;
      
      double amplitudes[kNumHarmonics * 2];
      for (int32_t i = 0; i < kNumHarmonics * 2; ++i) {
        amplitudes[i] = notes[chord_note].amplitude * harmonics[i];
      }
      
      // Fold truncated harmonics.
        //size_t
      int32_t num_harmonics = polyphony_ >= 2 && chord_note < 2
          ? kNumHarmonics - 1
          : kNumHarmonics;
        for (int32_t i = num_harmonics; i < kNumHarmonics; ++i) {
        amplitudes[2 * (num_harmonics - 1)] += amplitudes[2 * i];
        amplitudes[2 * (num_harmonics - 1) + 1] += amplitudes[2 * i + 1];
      }
        //std::cout << "synth_note: " << note << "\n";
      double frequency = SemitonesToRatio(note - 69.0) * Dsp::getA3();
      voice_[group * chord_size + chord_note].Render(
          frequency,
          amplitudes,
          num_harmonics,
          (group + chord_note) & 1 ? out : aux,
          size);
    }
  }
  
  if (clear_fx_) {
    reverb_.Clear();
    clear_fx_ = false;
  }
  
  switch (fx_type_) {
    case FX_FORMANT:
    case FX_FORMANT_2:
      ProcessFormantFilter(
          patch.position,
          fx_type_ == FX_FORMANT ? 1.0 : 1.1,
          fx_type_ == FX_FORMANT ? 25.0 : 10.0,
          out,
          aux,
          size);
      break;

    case FX_CHORUS:
      chorus_.set_amount(patch.position);
      chorus_.set_depth(0.15 + 0.5 * patch.position);
      chorus_.Process(out, aux, size);
      break;
    
    case FX_ENSEMBLE:
      ensemble_.set_amount(patch.position * (2.0 - patch.position));
      ensemble_.set_depth(0.2 + 0.8 * patch.position * patch.position);
      ensemble_.Process(out, aux, size);
      break;
  
    case FX_REVERB:
    case FX_REVERB_2:
      reverb_.set_amount(patch.position * 0.5);
      reverb_.set_diffusion(0.625);
      reverb_.set_time(fx_type_ == FX_REVERB
        ? (0.5 + 0.49 * patch.position)
        : (0.3 + 0.6 * patch.position));
      reverb_.set_input_gain(0.2);
      reverb_.set_lp(fx_type_ == FX_REVERB ? 0.3 : 0.6);
      reverb_.Process(out, aux, size);
      break;
    
    default:
      break;
  }

  // Prevent main signal cancellation when EVEN gets summed with ODD through
  // normalization.
  for (size_t i = 0; i < size; ++i) {
    aux[i] = -aux[i];
  }
  limiter_.Process(out, aux, size, 1.0);
}

}  // namespace rings
