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
// Chords: wavetable and divide-down organ/string machine.

#include "plaits/dsp/chords/chord_bank.h"

#include "stmlib/dsp/units.h"

namespace plaits {

using namespace stmlib;

#ifdef JON_CHORDS

// Alternative chord table by Jon Butler jonbutler88@gmail.com
/* static */
const double ChordBank::chords_[kChordNumChords][kChordNumNotes] = {
  // Fixed Intervals
  { 0.00, 0.01, 11.99, 12.00 },  // Octave
  { 0.00, 7.00,  7.01, 12.00 },  // Fifth
  // Minor
  { 0.00, 3.00,  7.00, 12.00 },  // Minor
  { 0.00, 3.00,  7.00, 10.00 },  // Minor 7th
  { 0.00, 3.00, 10.00, 14.00 },  // Minor 9th
  { 0.00, 3.00, 10.00, 17.00 },  // Minor 11th
  // Major
  { 0.00, 4.00,  7.00, 12.00 },  // Major
  { 0.00, 4.00,  7.00, 11.00 },  // Major 7th
  { 0.00, 4.00, 11.00, 14.00 },  // Major 9th
  // Colour Chords
  { 0.00, 5.00,  7.00, 12.00 },  // Sus4
  { 0.00, 2.00,  9.00, 16.00 },  // 69
  { 0.00, 4.00,  7.00,  9.00 },  // 6th
  { 0.00, 7.00, 16.00, 23.00 },  // 10th (Spread maj7)
  { 0.00, 4.00,  7.00, 10.00 },  // Dominant 7th
  { 0.00, 7.00, 10.00, 13.00 },  // Dominant 7th (b9)
  { 0.00, 3.00,  6.00, 10.00 },  // Half Diminished
  { 0.00, 3.00,  6.00,  9.00 },  // Fully Diminished
};

#else

/* static */
const double ChordBank::chords_[kChordNumChords][kChordNumNotes] = {
  { 0.00, 0.01, 11.99, 12.00 },  // OCT
  { 0.00, 7.00,  7.01, 12.00 },  // 5
  { 0.00, 5.00,  7.00, 12.00 },  // sus4
  { 0.00, 3.00,  7.00, 12.00 },  // m
  { 0.00, 3.00,  7.00, 10.00 },  // m7
  { 0.00, 3.00, 10.00, 14.00 },  // m9
  { 0.00, 3.00, 10.00, 17.00 },  // m11
  { 0.00, 2.00,  9.00, 16.00 },  // 69
  { 0.00, 4.00, 11.00, 14.00 },  // M9
  { 0.00, 4.00,  7.00, 11.00 },  // M7
  { 0.00, 4.00,  7.00, 12.00 },  // M
};

#endif  // JON_CHORDS

void ChordBank::Init(BufferAllocator* allocator) {
  ratios_ = allocator->Allocate<double>(kChordNumChords * kChordNumNotes);
  note_count_ = allocator->Allocate<int>(kChordNumChords);
  sorted_ratios_ = allocator->Allocate<double>(kChordNumNotes);

  chord_index_quantizer_.Init(kChordNumChords, 0.075, false);
}

void ChordBank::Reset() {
  for (int i = 0; i < kChordNumChords; ++i) {
    int count = 0;
    for (int j = 0; j < kChordNumNotes; ++j) {
      ratios_[i * kChordNumNotes + j] = SemitonesToRatio(chords_[i][j]);
      if (chords_[i][j] !=  0.01 && chords_[i][j] !=  7.01 && \
          chords_[i][j] != 11.99 && chords_[i][j] != 12.00) {
        ++count;
      }
    }
    note_count_[i] = count;
  }
  Sort();
}

int ChordBank::ComputeChordInversion(
    double inversion,
    double* ratios,
    double* amplitudes) {
  const double* base_ratio = this->ratios();
  inversion = inversion * double(kChordNumNotes * kChordNumVoices);

  MAKE_INTEGRAL_FRACTIONAL(inversion);

  int num_rotations = inversion_integral / kChordNumNotes;
  int rotated_note = inversion_integral % kChordNumNotes;

  const double kBaseGain = 0.25;

  int mask = 0;

  for (int i = 0; i < kChordNumNotes; ++i) {
    double transposition = 0.25 * static_cast<double>(
        1 << ((kChordNumNotes - 1 + inversion_integral - i) / kChordNumNotes));
    int target_voice = (i - num_rotations + kChordNumVoices) % kChordNumVoices;
    int previous_voice = (target_voice - 1 + kChordNumVoices) % kChordNumVoices;

    if (i == rotated_note) {
      ratios[target_voice] = base_ratio[i] * transposition;
      ratios[previous_voice] = ratios[target_voice] * 2.0;
      amplitudes[previous_voice] = kBaseGain * inversion_fractional;
      amplitudes[target_voice] = kBaseGain * (1.0 - inversion_fractional);
    } else if (i < rotated_note) {
      ratios[previous_voice] = base_ratio[i] * transposition;
      amplitudes[previous_voice] = kBaseGain;
    } else {
      ratios[target_voice] = base_ratio[i] * transposition;
      amplitudes[target_voice] = kBaseGain;
    }

    if (i == 0) {
      if (i >= rotated_note) {
        mask |= 1 << target_voice;
      }
      if (i <= rotated_note) {
        mask |= 1 << previous_voice;
      }
    }
  }
  return mask;
}

}  // namespace plaits
