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
// String machine emulation with filter and chorus.

#include "plaits/dsp/engine2/string_machine_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void StringMachineEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kChordNumNotes; ++i) {
    divide_down_voice_[i].Init();
  }
  chords_.Init(allocator);
  morph_lp_ = 0.0;
  timbre_lp_ = 0.0;
  svf_[0].Init();
  svf_[1].Init();
  ensemble_.Init(allocator->Allocate<Ensemble::E::T>(1024));
}

void StringMachineEngine::Reset() {
  chords_.Reset();
  ensemble_.Reset();
}

const int kRegistrationTableSize = 11;
const double registrations[kRegistrationTableSize][kChordNumHarmonics * 2] = {
  { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0 },    // Saw
  { 0.5, 0.0, 0.5, 0.0, 0.0, 0.0 },    // Saw + saw
  { 0.4, 0.0, 0.2, 0.0, 0.4, 0.0 },    // Full saw
  { 0.3, 0.0, 0.0, 0.3, 0.0, 0.4 },    // Full saw + square hybrid
  { 0.3, 0.0, 0.0, 0.0, 0.0, 0.7 },    // Saw + high square harmo
  { 0.2, 0.0, 0.0, 0.2, 0.0, 0.6 },    // Weird hybrid
  { 0.0, 0.2, 0.1, 0.0, 0.2, 0.5 },    // Sawsquare high harmo
  { 0.0, 0.3, 0.0, 0.3, 0.0, 0.4 },    // Square high armo
  { 0.0, 0.4, 0.0, 0.3, 0.0, 0.3 },    // Full square
  { 0.0, 0.5, 0.0, 0.5, 0.0, 0.0 },    // Square + Square
  { 0.0, 1.0, 0.0, 0.0, 0.0, 0.0 },    // Square
};

void StringMachineEngine::ComputeRegistration(
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

void StringMachineEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  ONE_POLE(morph_lp_, parameters.morph, 0.1);
  ONE_POLE(timbre_lp_, parameters.timbre, 0.1);

  chords_.set_chord(parameters.harmonics);

  double harmonics[kChordNumHarmonics * 2 + 2];
  double registration = max(morph_lp_, 0.0);
  ComputeRegistration(registration, harmonics);
  harmonics[kChordNumHarmonics * 2] = 0.0;

  // Render string/organ sound.
  fill(&out[0], &out[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);
  const double f0 = NoteToFrequency(parameters.note) * 0.998;
  for (int note = 0; note < kChordNumNotes; ++note) {
    const double note_f0 = f0 * chords_.ratio(note);
    double divide_down_gain = 4.0 - note_f0 * 32.0;
    CONSTRAIN(divide_down_gain, 0.0, 1.0);
    divide_down_voice_[note].Render(
        note_f0,
        harmonics,
        0.25 * divide_down_gain,
        note & 1 ? aux : out,
        size);
  }

  // Pass through VCF.
  const double cutoff = 2.2 * f0 * SemitonesToRatio(120.0 * parameters.timbre);
  svf_[0].set_f_q<FREQUENCY_DIRTY>(cutoff, 1.0);
  svf_[1].set_f_q<FREQUENCY_DIRTY>(cutoff * 1.5, 1.0);

  // Mixdown.
  for (size_t i = 0; i < size; ++i) {
    const double l = svf_[0].Process<FILTER_MODE_LOW_PASS>(out[i]);
    const double r = svf_[1].Process<FILTER_MODE_LOW_PASS>(aux[i]);
    out[i] = 0.66 * l + 0.33 * r;
    aux[i] = 0.66 * r + 0.33 * l;
  }

  // Ensemble FX.
  const double amount = abs(parameters.timbre - 0.5) * 2.0;
  const double depth = 0.35 + 0.65 * parameters.timbre;
  ensemble_.set_amount(amount);
  ensemble_.set_depth(depth);
  ensemble_.Process(out, aux, size);
}

}  // namespace plaits
