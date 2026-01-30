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
// Chords: wavetable and divide-down organ/string machine.

#include "plaits/dsp/engine/chord_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void ChordEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kChordNumVoices; ++i) {
    divide_down_voice_[i].Init();
    wavetable_voice_[i].Init();
  }
  chords_.Init(allocator);

  morph_lp_ = 0.0;
  timbre_lp_ = 0.0;
}

void ChordEngine::Reset() {
    chords_.Reset();
}

const double fade_point[kChordNumVoices] = {
  0.55, 0.47, 0.49, 0.51, 0.53
};

const int kRegistrationTableSize = 8;
const double registrations[kRegistrationTableSize][kChordNumHarmonics * 2] = {
  { 0.0, 1.0, 0.0, 0.0, 0.0, 0.0 },  // Square
  { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0 },  // Saw
  { 0.5, 0.0, 0.5, 0.0, 0.0, 0.0 },  // Saw + saw
  { 0.33, 0.0, 0.33, 0.0, 0.33, 0.0 },  // Full saw
  { 0.33, 0.0, 0.0, 0.33, 0.0, 0.33 },  // Full saw + square hybrid
  { 0.5, 0.0, 0.0, 0.0, 0.0, 0.5 },  // Saw + high square harmo
  { 0.0, 0.5, 0.0, 0.0, 0.0, 0.5 },  // Square + high square harmo
  { 0.0, 0.1, 0.1, 0.0, 0.2, 0.6 },  // // Saw+square + high harmo
};

void ChordEngine::ComputeRegistration(
    double registration,
    double* amplitudes) {
  registration *= (kRegistrationTableSize - 1.001);
  MAKE_INTEGRAL_FRACTIONAL(registration);

  for (int i = 0; i < kChordNumHarmonics * 2; ++i) {
    double a = registrations[registration_integral][i];
    double b = registrations[registration_integral + 1][i];
    amplitudes[i] = a + (b - a) * registration_fractional;
  }
}

#define WAVE(bank, row, column) &wav_integrated_waves[(bank * 64 + row * 8 + column) * 132]

const int16_t* const wavetable[] = {
  WAVE(2, 6, 1),
  WAVE(2, 6, 6),
  WAVE(2, 6, 4),
  WAVE(0, 6, 0),
  WAVE(0, 6, 1),
  WAVE(0, 6, 2),
  WAVE(0, 6, 7),
  WAVE(2, 4, 7),
  WAVE(2, 4, 6),
  WAVE(2, 4, 5),
  WAVE(2, 4, 4),
  WAVE(2, 4, 3),
  WAVE(2, 4, 2),
  WAVE(2, 4, 1),
  WAVE(2, 4, 0),
};

void ChordEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  ONE_POLE(morph_lp_, parameters.morph, 0.1f);
  ONE_POLE(timbre_lp_, parameters.timbre, 0.1f);

  chords_.set_chord(parameters.harmonics);

  double harmonics[kChordNumHarmonics * 2 + 2];
  double note_amplitudes[kChordNumVoices];
  double registration = max(1.0 - morph_lp_ * 2.15, 0.0);

  ComputeRegistration(registration, harmonics);
  harmonics[kChordNumHarmonics * 2] = 0.0;

  double ratios[kChordNumVoices];
  int aux_note_mask = chords_.ComputeChordInversion(
      timbre_lp_,
      ratios,
      note_amplitudes);

  fill(&out[0], &out[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);

  const double f0 = NoteToFrequency(parameters.note) * 0.998;
  const double waveform = max((morph_lp_ - 0.535) * 2.15, 0.0);

  for (int note = 0; note < kChordNumVoices; ++note) {
    double wavetable_amount = 50.0 * (morph_lp_ - fade_point[note]);
    CONSTRAIN(wavetable_amount, 0.0, 1.0);

    double divide_down_amount = 1.0 - wavetable_amount;
    double* destination = (1 << note) & aux_note_mask ? aux : out;

    const double note_f0 = f0 * ratios[note];
    double divide_down_gain = 4.0 - note_f0 * 32.0;
    CONSTRAIN(divide_down_gain, 0.0, 1.0);
    divide_down_amount *= divide_down_gain;

    if (wavetable_amount) {
      wavetable_voice_[note].Render(
          note_f0 * 1.004,
          note_amplitudes[note] * wavetable_amount,
          waveform,
          wavetable,
          destination,
          size);
    }

    if (divide_down_amount) {
      divide_down_voice_[note].Render(
          note_f0,
          harmonics,
          note_amplitudes[note] * divide_down_amount,
          destination,
          size);
    }
  }

  for (size_t i = 0; i < size; ++i) {
    out[i] += aux[i];
    aux[i] *= 3.0;
  }
}

}  // namespace plaits
