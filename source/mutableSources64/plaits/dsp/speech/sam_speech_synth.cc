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
// SAM-inspired speech synth (as used in Shruthi/Ambika/Braids).

#include "plaits/dsp/speech/sam_speech_synth.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/resources.h"


namespace plaits {

using namespace std;
using namespace stmlib;

void SAMSpeechSynth::Init() {
  phase_ = 0.0;
  frequency_ = 0.0;
  pulse_next_sample_ = 0.0;
  pulse_lp_ = 0.0;

  fill(&formant_phase_[0], &formant_phase_[3], 0);
  consonant_samples_ = 0;
  consonant_index_ = 0.0;
}

// Phoneme data

/* static */
const SAMSpeechSynth::Phoneme SAMSpeechSynth::phonemes_[] = {
  { { { 60, 15 }, { 90, 13 }, { 200, 1 } } },
  { { { 40, 13 }, { 114, 12 }, { 139, 6 } } },
  { { { 33, 14 }, { 155, 12 }, { 209, 7 } } },
  { { { 22, 13 }, { 189, 10 }, { 247, 8 } } },
  { { { 51, 15 }, { 99, 12 }, { 195, 1 } } },
  { { { 29, 13 }, { 65, 8 }, { 180, 0 } } },
  { { { 13, 12 }, { 103, 3 }, { 182, 0 } } },
  { { { 20, 15 }, { 114, 3 }, { 213, 0 } } },
  { { { 13, 7 }, { 164, 3 }, { 222, 14 } } },
  { { { 13, 9 }, { 121, 9 }, { 254, 0 } } },
  { { { 40, 12 }, { 112, 10 }, { 114, 5 } } },
  { { { 24, 13 }, { 54, 8 }, { 157, 0 } } },
  { { { 33, 14 }, { 155, 12 }, { 166, 7 } } },
  { { { 36, 14 }, { 83, 8 }, { 249, 1 } } },
  { { { 40, 14 }, { 114, 12 }, { 139, 6 } } },
  { { { 13, 5 }, { 58, 5 }, { 182, 5 } } },
  { { { 13, 7 }, { 164, 10 }, { 222, 14 } } },
  { { { 13, 7 }, { 164, 10 }, { 222, 14 } } }  // GUARD
};

/* static */
const double SAMSpeechSynth::formant_amplitude_lut[] = {
  0.03125000,  0.03756299,  0.04515131,  0.05427259,  0.06523652,
  0.07841532,  0.09425646,  0.11329776,  0.13618570,  0.16369736,
  0.19676682,  0.23651683,  0.28429697,  0.34172946,  0.41076422,
  0.49374509
};

void SAMSpeechSynth::InterpolatePhonemeData(
  double phoneme,
  double formant_shift,
  uint32_t* formant_frequency,
  double* formant_amplitude) {
  MAKE_INTEGRAL_FRACTIONAL(phoneme);

  const Phoneme& p_1 = phonemes_[phoneme_integral];
  const Phoneme& p_2 = phonemes_[phoneme_integral + 1];

  formant_shift = 1.0 + formant_shift * 2.5;
  for (int i = 0; i < kSAMNumFormants; ++i) {
    double f_1 = p_1.formant[i].frequency;
    double f_2 = p_2.formant[i].frequency;
    double f = f_1 + (f_2 - f_1) * phoneme_fractional;
    f *= 8.0 * formant_shift * 4294967296.0 / kSampleRate;
    formant_frequency[i] = static_cast<uint32_t>(f);

    double a_1 = formant_amplitude_lut[p_1.formant[i].amplitude];
    double a_2 = formant_amplitude_lut[p_2.formant[i].amplitude];
    formant_amplitude[i] = a_1 + (a_2 - a_1) * phoneme_fractional;
  }
}

void SAMSpeechSynth::Render(
    bool consonant,
    double frequency,
    double vowel,
    double formant_shift,
    double* excitation,
    double* output,
    size_t size) {
  if (frequency >= 0.0625) {
    frequency = 0.0625;
  }

  if (consonant) {
      consonant_samples_ = kSampleRate * 0.05;
    int r = (vowel + 3.0 * frequency + 7.0 * formant_shift) * 8.0;
    consonant_index_ = (r % kSAMNumConsonants);
  }
  consonant_samples_ -= min(consonant_samples_, size);

  double phoneme = consonant_samples_
      ? (consonant_index_ + kSAMNumVowels)
      : vowel * (kSAMNumVowels - 1.0001f);

  uint32_t formant_frequency[kSAMNumFormants];
  double formant_amplitude[kSAMNumFormants];

  InterpolatePhonemeData(
      phoneme,
      formant_shift,
      formant_frequency,
      formant_amplitude);

  ParameterInterpolator fm(&frequency_, frequency, size);
  double pulse_next_sample = pulse_next_sample_;

  while (size--) {
    double pulse_this_sample = pulse_next_sample;
    pulse_next_sample = 0.0;
    const double frequency = fm.Next();
    phase_ += frequency;

    if (phase_ >= 1.0) {
      phase_ -= 1.0;
      double t = phase_ / frequency;
      formant_phase_[0] = static_cast<uint32_t>(
          t * static_cast<double>(formant_frequency[0]));
      formant_phase_[1] = static_cast<uint32_t>(
          t * static_cast<double>(formant_frequency[1]));
      formant_phase_[2] = static_cast<uint32_t>(
          t * static_cast<double>(formant_frequency[2]));
      pulse_this_sample -= ThisBlepSample(t);
      pulse_next_sample -= NextBlepSample(t);
    } else {
      formant_phase_[0] += formant_frequency[0];
      formant_phase_[1] += formant_frequency[1];
      formant_phase_[2] += formant_frequency[2];
    }
    pulse_next_sample += phase_;

    double d = pulse_this_sample - 0.5 - pulse_lp_;
    pulse_lp_ += min(16.0 * frequency, 1.0) * d;
    *excitation++ = d;

    double s = 0;
    // s += lut_sine[formant_phase_[0] >> 22] * formant_amplitude[0];
    // s += lut_sine[formant_phase_[1] >> 22] * formant_amplitude[1];
    // s += lut_sine[formant_phase_[2] >> 22] * formant_amplitude[2];
    s += SineRaw(formant_phase_[0]) * formant_amplitude[0];
    s += SineRaw(formant_phase_[1]) * formant_amplitude[1];
    s += SineRaw(formant_phase_[2]) * formant_amplitude[2];
    s *= (1.0 - phase_);
    *output++ = s;
  }
  pulse_next_sample_ = pulse_next_sample;
}

}  // namespace plaits
